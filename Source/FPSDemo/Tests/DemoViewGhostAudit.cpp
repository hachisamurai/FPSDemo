// 职责：真实玩家视角的阴影/模糊/时间重建消融采样；用法-DemoViewGhostAudit -UseFixedTimeStep -FPS=60，只在Editor独立进程运行，不保存资产或设置。
#include "Tests/DemoViewGhostAudit.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/DemoOwnerBodyLODComponent.h" // 消融旧配置时显式停用；正式复录不触碰LOD策略。
#include "Animation/DemoPlayerAppearance.h" // 原问题隔离仅在本进程撤下新增相机清晰策略，绝不保存资产。
#include "Camera/CameraComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Game/FPSDemoGameMode.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/DemoPlayerController.h"
#include "UnrealClient.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h" // 新稳定眼位专项只在隔离GameWorld注册PreActor输入。

namespace
{
    // 每个阶段为独立原因隔离；原图并不被标为通过或失败。
    const TCHAR* ViewGhostCases[]={TEXT("Baseline"),TEXT("NoMotionBlur"),TEXT("NoPlayerShadows"),TEXT("HideOwnerPartition"),TEXT("FXAA_NoBlur"),TEXT("NoTemporalAA_WithBlur"),TEXT("NoPostProcess_NoBlur"),TEXT("FullBonesLOD0_NoBlur"),TEXT("FullBonesLOD0_WithBlur"),TEXT("RuntimePolicy")};
}

ADemoViewGhostAudit::ADemoViewGhostAudit()
{
    DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
#if WITH_EDITOR
    bClipReview=FParse::Param(FCommandLine::Get(),TEXT("DemoViewClipAudit")); // 只选择隔离检查计划，不保存或更改生产配置。
    bRuntimeReview=FParse::Param(FCommandLine::Get(),TEXT("DemoViewRuntimeAudit")); // 独立正式策略验收，不默默继承诊断LOD覆盖。
    bStableClipReview=FParse::Param(FCommandLine::Get(),TEXT("DemoStableViewClipAudit")); // 不改变旧夹具默认计划。
    bStableClipMelee=bStableClipReview && FParse::Param(FCommandLine::Get(),TEXT("DemoStableViewMelee")); // 两个独立进程分别验证枪械与近战分区。
#endif
}

void ADemoViewGhostAudit::Finish(bool Success,const TCHAR* Reason)
{
    DEMO_LOG_CALL(); if (bFinished) return; bFinished=true; SetActorTickEnabled(false);
    if (bStableClipReview) { StopStableViewClipInput(); Success=SaveStableViewClipReview(Success,Reason) && Success; }
    if (!Directory.IsEmpty()) Success=FFileHelper::SaveStringArrayToFile(Rows,*(Directory/TEXT("frames.csv"))) && Success;
    if (bClipReview && !Directory.IsEmpty()) Success=FFileHelper::SaveStringArrayToFile(ClipProbeRows,*(Directory/TEXT("clip_probes.csv"))) && Success;
    UE_LOG(LogFPSDemo,Display,TEXT("VIEW_GHOST_AUDIT_%s reason=%s rows=%d directory=%s; capture completion only, visual review required"),Success?TEXT("COMPLETE"):TEXT("FAILED"),Reason,Rows.Num(),*Directory);
    FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
}

void ADemoViewGhostAudit::ApplyCase()
{
    DEMO_LOG_CALL();
    if (bRuntimeReview || bStableClipReview)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("VIEW_GHOST_CASE case=RuntimePolicy; no render/LOD/PP/shadow override by test"));
        return; // 保留真实设置；只有生产策略组件可决定LOD。
    }
    if (UDemoOwnerBodyLODComponent* Policy=Player->FindComponentByClass<UDemoOwnerBodyLODComponent>()) Policy->Deactivate(); // 仅原因隔离进程恢复旧版基准。
    if (UDemoPlayerAppearance* ViewAppearance=Player->GetPlayerPresentation()->GetAppearance()) ViewAppearance->bDisableFirstPersonMotionBlur=false; // 仅内存测试配置，还原旧相机模糊以继续隔离原始原因。
    IConsoleVariable* Blur=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")); // 本进程CVar，不写GameUserSettings。
    IConsoleVariable* AA=IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")); // 0关闭/1FXAA；只在该消融阶段使用。
    if (!Blur || !AA || !Player.IsValid()) { Finish(false,TEXT("case render controls missing")); return; }
    Blur->Set(Case==1 || Case==4 || Case==6 || Case==7?0:OriginalBlur,ECVF_SetByCode);
    AA->Set(Case==4?1:Case==5?0:OriginalAA,ECVF_SetByCode);
    Player->GetMesh()->SetDisablePostProcessBlueprint(Case==6); // 仅该隔离阶段停用原Manny后处理，正式主图/瞄准/骨架不改。
    Player->GetMesh()->SetForcedLOD(Case>=7?1:0); // UE值1表示LOD0、0自动；仅为验证Leader能否提供Follower全部蒙皮骨。
    if (UDemoPlayerPresentationComponent* Presentation=Player->GetPlayerPresentation())
        if (USkeletalMeshComponent* OwnerPartition=Presentation->GetFirstPersonBodyMesh()) OwnerPartition->SetForcedLOD(Case>=7?1:0);
    Player->SetActorRotation(FRotator::ZeroRotator); Controller->SetControlRotation(FRotator(-25,0,0)); // 每轮从相同头身基准开始，避免45°自由头角累积改变A/B姿态。
    for (const auto& Pair:OriginalShadows) if (UPrimitiveComponent* Primitive=Pair.Key.Get()) Primitive->SetCastShadow(Case==2?false:Pair.Value);
    UE_LOG(LogFPSDemo,Log,TEXT("VIEW_GHOST_CASE case=%s blur=%d aa=%d hidden_body_shadow=%d postprocess_disabled=%d"),ViewGhostCases[Case],Blur->GetInt(),AA->GetInt(),Player->GetMesh()->CastShadow,Case==6);
}

void ADemoViewGhostAudit::DumpSkin(USkeletalMeshComponent* Mesh,const TCHAR* Label)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if (!Mesh || !Mesh->GetSkeletalMeshAsset()) { Finish(false,TEXT("skin diagnostic mesh missing")); return; }
    const FSkeletalMeshRenderData* Render=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering(); // 仅本游戏线程借用LOD0，不持有跨渲染更新指针。
    if (!Render || Render->LODRenderData.IsEmpty()) { Finish(false,TEXT("skin diagnostic render data missing")); return; }
    const FSkeletalMeshLODRenderData& LOD=Render->LODRenderData[0];
    FSkinWeightVertexBuffer* Weights=Mesh->GetSkinWeightBuffer(0); // 原组件实际权重，非自行重算或重新归一化。
    if (!Weights) { Finish(false,TEXT("CPU-readable skin weights unavailable")); return; }
    TArray<FMatrix44f> Matrices; Mesh->GetCurrentRefToLocalMatrices(Matrices,0); // 最终实际求值及逆绑定，LeaderPose使用引擎自身映射。
    TArray<FString> Vertices,Objects; Vertices.Add(TEXT("vertex,x,y,z,ref_x,ref_y,ref_z,dominant_bone,weight"));
    const FVector Eye=Player->GetSharedViewLocation(); // OBJ以真实眼位为原点，单位cm，避免上万厘米绝对位置妨碍DCC观察。
    const FReferenceSkeleton& Reference=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    for (uint32 Index=0;Index<LOD.GetNumVertices();++Index)
    {
        const FVector P(USkinnedMeshComponent::GetSkinnedVertexPosition(Mesh,Index,LOD,*Weights,Matrices));
        const FVector Ref(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index));
        int32 Section=0,SectionVertex=0; LOD.GetSectionFromVertexIndex(Index,Section,SectionVertex);
        uint16 Maximum=0;int32 Bone=0; // section-local索引必须经BoneMap映射到本资产真实骨序。
        for (uint32 Influence=0;Influence<Weights->GetMaxBoneInfluences();++Influence)
            if (Weights->GetBoneWeight(Index,Influence)>Maximum)
            { Maximum=Weights->GetBoneWeight(Index,Influence); Bone=LOD.RenderSections[Section].BoneMap[Weights->GetBoneIndex(Index,Influence)]; }
        Vertices.Add(FString::Printf(TEXT("%u,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%s,%u"),Index,P.X,P.Y,P.Z,Ref.X,Ref.Y,Ref.Z,*Reference.GetBoneName(Bone).ToString(),Maximum));
        const FVector World=Mesh->GetComponentTransform().TransformPosition(P)-Eye;
        Objects.Add(FString::Printf(TEXT("v %.7f %.7f %.7f"),World.X,World.Y,World.Z));
    }
    const FRawStaticIndexBuffer16or32Interface* Indices=LOD.MultiSizeIndexContainer.GetIndexBuffer(); // 原索引三角形，不根据屏幕剪影重建拓扑。
    for (int32 Index=0;Index<Indices->Num();Index+=3) Objects.Add(FString::Printf(TEXT("f %u %u %u"),Indices->Get(Index)+1,Indices->Get(Index+1)+1,Indices->Get(Index+2)+1));
    const FString Prefix=FString(ViewGhostCases[Case])+TEXT("_")+Label; // 每个Case单独保留，后续导出不能覆盖前一隔离组的蒙皮证据。
    if (!FFileHelper::SaveStringArrayToFile(Vertices,*(Directory/(Prefix+TEXT("_skin.csv")))) || !FFileHelper::SaveStringArrayToFile(Objects,*(Directory/(Prefix+TEXT("_skin.obj")))))
    { Finish(false,TEXT("skin diagnostic write failed")); return; }
    TArray<FString> Bones; Bones.Add(TEXT("bone,component_x,component_y,component_z,local_scale_x,local_scale_y,local_scale_z"));
    for (int32 Index=0;Index<Mesh->GetNumBones();++Index)
    {
        const FName Name=Mesh->GetBoneName(Index); const FVector P=Mesh->GetSocketLocation(Name)-Eye; // 实际世界位置相对真实相机，不混同上方组件空间顶点。
        const FVector Scale=Player->GetMesh()->GetBoneSpaceTransforms().IsValidIndex(Index)?Player->GetMesh()->GetBoneSpaceTransforms()[Index].GetScale3D():FVector::ZeroVector;
        Bones.Add(FString::Printf(TEXT("%s,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f"),*Name.ToString(),P.X,P.Y,P.Z,Scale.X,Scale.Y,Scale.Z));
    }
    FFileHelper::SaveStringArrayToFile(Bones,*(Directory/(Prefix+TEXT("_bones.csv"))));
    const UAnimInstance* Main=Player->GetMesh()->GetAnimInstance(); // 游戏线程本帧RequiredBones，只记录计数，不改变骨容器。
    UE_LOG(LogFPSDemo,Log,TEXT("VIEW_GHOST_SKIN_DUMP label=%s vertices=%u triangles=%d eye=%s rotation=%s leader_lod=%d follower_lod=%d compact_bones=%d"),Label,LOD.GetNumVertices(),Indices->Num()/3,*Eye.ToString(),*Controller->GetControlRotation().ToString(),Player->GetMesh()->GetPredictedLODLevel(),Mesh->GetPredictedLODLevel(),Main?Main->GetRequiredBones().GetCompactPoseNumBones():0);
#endif
}

void ADemoViewGhostAudit::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
#if !WITH_EDITOR
    Finish(false,TEXT("Editor-only isolation")); return;
#else
    const double Now=GetWorld()->GetTimeSeconds(); // 暖机/时长均按真实引擎固定步长，不按耗时加速。
    if (bFinished || Now<2.) return;
    if (Now>(bStableClipReview?65.:35.)) { Finish(false,TEXT("bounded diagnostic timeout")); return; }
    if (!bInitialized)
    {
        if (bStableClipReview && (!FParse::Param(FCommandLine::Get(),TEXT("game")) || GetWorld()->WorldType!=EWorldType::Game || GetNetMode()!=NM_Standalone))
        { UE_LOG(LogFPSDemo,Warning,TEXT("STABLE_VIEW_CLIP_REJECT requires isolated Editor -game Standalone; no editor exit requested")); bFinished=true; SetActorTickEnabled(false); return; } // 不在误加参数的普通Editor/PIE中触发RequestExit。
        if (!FApp::CanEverRender() || !FApp::UseFixedTimeStep() || !FMath::IsNearlyEqual(FApp::GetFixedDeltaTime(),1./60.,.00001))
        { Finish(false,TEXT("real RHI and fixed60Hz required")); return; }
        Controller=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0));
        AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前隔离World正式Hub入口，Editor存档限制继续生效。
        if (!Controller.IsValid() || !Mode) return;
        if (!Mode->StartRun()) { Finish(false,TEXT("real hub start failed")); return; }
        Controller->OnRunReady(); Player=Cast<ADemoCharacter>(Controller->GetPawn());
        if (!Player.IsValid()) { Finish(false,TEXT("local pawn missing")); return; }
        UDemoPlayerPresentationComponent* Presentation=Player->GetPlayerPresentation(); // 当前Pawn展示组件拥有者，先决条件仅诊断Actor持有。
        if (!Presentation || !Presentation->UsesFullBodyFirstPerson() || !Presentation->GetFirstPersonBodyMesh())
        { Finish(false,TEXT("actual full-body owner partition required")); return; }
        AddTickPrerequisiteComponent(Presentation); // 只在正式显隐/武器展示完成后施加临时消融，不与Tick显隐相互覆盖。
        Directory=FPaths::ProjectSavedDir()/(bStableClipReview?TEXT("StableViewClipAudit20261114"):bRuntimeReview?TEXT("ViewRuntimeAudit20261031"):bClipReview?TEXT("ViewClippingAudit20261030"):TEXT("ViewShadowAudit20261030"))/(bStableClipReview?(bStableClipMelee?TEXT("Melee"):TEXT("Weapon")):TEXT(""))/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
        if (!IFileManager::Get().MakeDirectory(*Directory,true)) { Finish(false,TEXT("evidence directory unavailable")); return; }
        if (!bStableClipReview) Rows.Add(bRuntimeReview && !bClipReview?TEXT("frame,time,pitch,yaw,leader_lod,follower_lod,compact_bones,forced_leader,forced_follower,blur,aa,visible_owner,melee,world_x,world_y,world_z,camera_blur_amount,camera_blur_override,camera_pp_weight"):
            bClipReview?TEXT("pose,frame,requested_pitch,requested_yaw,camera_pitch,camera_yaw,camera_x,camera_y,camera_z,fov,leader_lod,follower_lod"):
            TEXT("case,frame,pitch,yaw,camera_x,camera_y,camera_z,head_x,head_y,head_z,blur_quality,aa_method,owner_visible,body_cast_shadow,motion_blur_amount"));
        StartLocation=Player->GetActorLocation();
        OriginalBlur=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))->GetInt();
        OriginalAA=IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt();
        USkeletalMeshComponent* OwnerMesh=Presentation->GetFirstPersonBodyMesh();
        TInlineComponentArray<UPrimitiveComponent*> Primitives(Player.Get()); // 所有测试Case共用初始化基准，恢复枪/Body投影不能仅恢复一个组件。
        for (UPrimitiveComponent* Primitive:Primitives) OriginalShadows.Add(Primitive,Primitive->CastShadow);
        UE_LOG(LogFPSDemo,Log,TEXT("VIEW_GHOST_ACTUAL body=%s owner=%s owner_only=%d body_owner_hidden=%d body_hidden_shadow=%d owner_shadow=%d eye=%s original_blur=%d aa=%d"),
            *GetNameSafe(Player->GetMesh()->GetSkeletalMeshAsset()),*GetNameSafe(OwnerMesh->GetSkeletalMeshAsset()),OwnerMesh->bOnlyOwnerSee,
            Player->GetMesh()->bOwnerNoSee,Player->GetMesh()->bCastHiddenShadow,OwnerMesh->CastShadow,*Player->GetSharedViewLocation().ToString(),OriginalBlur,OriginalAA);
        for (int32 Slot=0;Slot<OwnerMesh->GetNumMaterials();++Slot) // 记录真正运行材质模式；不根据颗粒外观直接推断半透明。
            if (UMaterialInterface* Material=OwnerMesh->GetMaterial(Slot)) UE_LOG(LogFPSDemo,Log,TEXT("VIEW_GHOST_MATERIAL slot=%d material=%s blend_mode=%d"),Slot,*Material->GetPathName(),static_cast<int32>(Material->GetBlendMode()));
        bInitialized=true; Case=bRuntimeReview || bStableClipReview?9:bClipReview?7:FParse::Param(FCommandLine::Get(),TEXT("DemoViewGhostLODBlurOnly"))?8:FParse::Param(FCommandLine::Get(),TEXT("DemoViewGhostLODOnly"))?7:FParse::Param(FCommandLine::Get(),TEXT("DemoViewGhostPostProcessOnly"))?6:0; Frame=-60; ApplyCase();
        if (bStableClipReview && !BeginStableViewClipReview()) Finish(false,TEXT("stable clip fixture initialization failed"));
        return;
    }
    if (!Player.IsValid() || !Controller.IsValid()) { Finish(false,TEXT("diagnostic pawn destroyed")); return; }
    UDemoPlayerPresentationComponent* Presentation=Player->GetPlayerPresentation(); // 本帧组件借用，禁止跨线程。
    USkeletalMeshComponent* OwnerMesh=Presentation?Presentation->GetFirstPersonBodyMesh():nullptr;
    if (!OwnerMesh) { Finish(false,TEXT("owner partition disappeared")); return; }
    if (bStableClipReview) { TickStableViewClipReview(DeltaSeconds); return; } // 输入已在PreActor，末帧禁止再写控制旋转或重算相机。
    if (bClipReview) { TickClipReview(DeltaSeconds); return; } // 同一真实Avatar/图，单独检查18个稳定视线，不重新求解姿态。
    if (bRuntimeReview) { TickRuntimeReview(DeltaSeconds); return; } // 此分支不写ForcedLOD/画质，以免诊断补丁冒充正式效果。
    OwnerMesh->SetVisibility(Case!=3,false); // 仅本诊断帧隐藏拥有者分区；真实全身及动画继续求值。
    if ((Case==1 || Case>=6) && Frame==110) { DumpSkin(OwnerMesh,TEXT("Owner")); DumpSkin(Player->GetMesh(),TEXT("Body")); } // 每个隔离姿态仅导出一次，不在逐帧循环导出大数组。
    const double T=Frame<0?0.:static_cast<double>(Frame)/60.; // 每Case复用同一输入曲线，首帧先让时间AA暖机。
    float Pitch=-25.f,Yaw=0.f; // 与视频低头持枪相同范围；不改FOV、眼位或动画链。
    if (T>.4 && T<1.4) { Pitch=-25.f-30.f*FMath::Sin((T-.4)*PI*2.); Yaw=55.f*FMath::Sin((T-.4)*PI*4.); }
    else if (T>=1.4) { Pitch=-50.f; Yaw=25.f; }
    Controller->SetControlRotation(FRotator(Pitch,Yaw,0)); Controller->UpdateRotation(DeltaSeconds);
    if (Frame>=0)
    {
        const FVector Eye=Player->GetSharedViewLocation(),Head=Player->GetMesh()->GetSocketLocation(TEXT("head")); // 同帧世界cm，最终画面在帧末由真实拥有者相机渲染。
        Rows.Add(FString::Printf(TEXT("%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d,%.6f"),ViewGhostCases[Case],Frame,Pitch,Yaw,Eye.X,Eye.Y,Eye.Z,Head.X,Head.Y,Head.Z,
            IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))->GetInt(),IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt(),
            OwnerMesh->IsVisible(),Player->GetMesh()->CastShadow,Player->GetFirstPersonCameraComponent()->PostProcessSettings.MotionBlurAmount));
        if (!FScreenshotRequest::IsScreenshotRequested()) FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("%s_%03d.png"),ViewGhostCases[Case],Frame),false,false);
    }
    if (++Frame>=120)
    {
        if (++Case>=9) { Finish(true,TEXT("requested same-path render ablations captured")); return; } // 旧消融仍仅九组，RuntimePolicy必须另开独立进程。
        Frame=-60; ApplyCase();
    }
#endif
}
