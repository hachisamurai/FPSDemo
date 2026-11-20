// 职责：稳定第一人称低头/真实GAS冲刺的有界实机取证；-game -DemoViewGhostAudit -DemoStableViewClipAudit，另加-DemoStableViewMelee测独立近战。
// 依赖实际Avatar/OwnerBody/CameraManager；PreActor只发输入，PostUpdate只读最终镜头和实际线性蒙皮。A01/A02/A03仅内存覆盖两引用；A05先-DemoStableOwnerFPCandidate从创建前加载专用FP候选，下身/近战引用只在本进程消费，绝不保存正式资产/设置。
#include "Tests/DemoViewGhostAudit.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/DemoOwnerLowerBodyAnimInstance.h" // 独立下身只读快照另列，不把TP Dash/Leader矩阵误写成Owner姿态。
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/DemoCharacter.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/GameViewportClient.h" // 只读正式LocalPlayer实际投影，不猜FOV轴或约束宽高比。
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h" // 真实Owner MID父链与裁剪参数取证；不修改任何材质或模拟最终像素。
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h" // 候选只接受三项稳定ID，不接受任意资产路径或普通游玩覆盖。
#include "Misc/SecureHash.h"
#include "Player/DemoPlayerController.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Serialization/JsonSerializer.h"
#include "SceneView.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Weapons/DemoWeaponComponent.h" // 有界取证读取本帧实际装备的FP骨枪，不复制或重播发射。
#include "Weapons/DemoWeaponBase.h"

namespace
{
    /** 实际LOD蒙皮转换到最终视空间并导出三角；只在三个固定帧取证，不设置骨姿/LOD，不把双面深度诊断当RHI最终像素。 */
    TSharedPtr<FJsonObject> StableClipDepthMesh(USkeletalMeshComponent* Component,const FMinimalViewInfo& POV,const TCHAR* Role)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] StableClipDepthMesh"));
        if (!Component || !Component->GetSkeletalMeshAsset() || !Component->IsVisible() || Component->bHiddenInGame) return nullptr;
        const int32 LodIndex=Component->GetPredictedLODLevel(); // 实际当前LOD，Owner lowerbody可能跟随Leader。
        const FSkeletalMeshRenderData* Render=Component->GetSkeletalMeshAsset()->GetResourceForRendering();
        if (!Render || !Render->LODRenderData.IsValidIndex(LodIndex)) return nullptr;
        const FSkeletalMeshLODRenderData& Lod=Render->LODRenderData[LodIndex];
        const FSkinWeightVertexBuffer* Weights=Component->GetSkinWeightBuffer(LodIndex);
        if (!Weights || !Lod.StaticVertexBuffers.PositionVertexBuffer.GetVertexData()) return nullptr;
        TArray<FMatrix44f> Matrices; Component->GetCurrentRefToLocalMatrices(Matrices,LodIndex); // 真实同帧线性蒙皮，非求解器目标。
        TArray<FVector3f> Skinned; USkinnedMeshComponent::ComputeSkinnedPositions(Component,Skinned,Matrices,Lod,*Weights); // 本机5.4显式消费冻结矩阵的批量入口，避免单点重载内部重新取骨。
        if (Skinned.Num()!=static_cast<int32>(Lod.GetNumVertices())) return nullptr;
        const FQuat Inverse=POV.Rotation.Quaternion().Inverse(); // X前/Y右/Z上，投影参数由外层真实LocalPlayer矩阵给出。
        TArray<TSharedPtr<FJsonValue>> Vertices,Triangles; Vertices.Reserve(Lod.GetNumVertices());
        for (uint32 Vertex=0;Vertex<Lod.GetNumVertices();++Vertex)
        {
            const FVector World=Component->GetComponentTransform().TransformPosition(FVector(Skinned[Vertex]));
            const FVector Point=Inverse.RotateVector(World-POV.Location); // 厘米；FP点为真实已缩组件输出，另记比例供独立未缩投影对照。
            TArray<TSharedPtr<FJsonValue>> Values={MakeShared<FJsonValueNumber>(Point.X),MakeShared<FJsonValueNumber>(Point.Y),MakeShared<FJsonValueNumber>(Point.Z)};
            Vertices.Add(MakeShared<FJsonValueArray>(Values));
        }
        const FRawStaticIndexBuffer16or32Interface* Indices=Lod.MultiSizeIndexContainer.GetIndexBuffer();
        if (!Indices) return nullptr;
        for (const FSkelMeshRenderSection& Section:Lod.RenderSections)
        {
            if (Section.bDisabled) continue;
            for (uint32 Index=Section.BaseIndex;Index<Section.BaseIndex+Section.NumTriangles*3;++Index)
                Triangles.Add(MakeShared<FJsonValueNumber>(Indices->Get(Index)));
        }
        TSharedPtr<FJsonObject> Result=MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("role"),Role); Result->SetStringField(TEXT("component"),Component->GetPathName());
        Result->SetStringField(TEXT("asset"),Component->GetSkeletalMeshAsset()->GetPathName()); Result->SetNumberField(TEXT("lod"),LodIndex);
        Result->SetArrayField(TEXT("camera_vertices_xyz_cm"),Vertices); Result->SetArrayField(TEXT("triangle_indices"),Triangles);
        Result->SetStringField(TEXT("component_transform"),Component->GetComponentTransform().ToString());
        return Result;
    }
    /** 单次作者取证矩阵，逐行存16个实际元素；UE使用行向量p*M，Skin为组件空间，不混入世界/相机变换。 */
    template<typename MatrixType>
    TArray<TSharedPtr<FJsonValue>> StableClipMatrixValues(const MatrixType& Matrix)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] StableClipMatrixValues"));
        TArray<TSharedPtr<FJsonValue>> Values; Values.Reserve(16); // 有界4x4值数组，无UObject/线程引用。
        for (int32 Row=0;Row<4;++Row) for (int32 Column=0;Column<4;++Column)
            Values.Add(MakeShared<FJsonValueNumber>(static_cast<double>(Matrix.M[Row][Column])));
        return Values;
    }
    /** 将任意资产名/变换写成一个CSV字段；引号转义，防止FVector/FTransform中的逗号破坏列对应。 */
    FString StableClipCell(const FString& Value)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 纯函数无this；保留限时高频埋点。
        return TEXT("\"")+Value.Replace(TEXT("\""),TEXT("\"\""))+TEXT("\"");
    }

    /** 原点在相机，Direction为单位相机空间前向射线；双面相交仅提供几何见证，Facing另存供材质裁面解释。 */
    bool StableClipRayTriangle(const FVector& Direction,const FVector& A,const FVector& B,const FVector& C,double& Distance)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 纯数学函数不持UObject。
        const FVector Edge1=B-A,Edge2=C-A,Cross=FVector::CrossProduct(Direction,Edge2); // cm与单位方向，Möller–Trumbore双面测试。
        const double Determinant=FVector::DotProduct(Edge1,Cross); // 不用背面剔除冒充最终材质可见性。
        if (FMath::Abs(Determinant)<1.e-9) return false;
        const double Inverse=1./Determinant,U=FVector::DotProduct(-A,Cross)*Inverse;
        if (U<0. || U>1.) return false;
        const FVector Q=FVector::CrossProduct(-A,Edge1); // 原点到三角形A的矢量。
        const double V=FVector::DotProduct(Direction,Q)*Inverse;
        if (V<0. || U+V>1.) return false;
        Distance=FVector::DotProduct(Edge2,Q)*Inverse;
        return Distance>0. && Distance<=40.; // 只查40cm近身遮挡，腿脚正常远处可见不作为失败。
    }

    /** 读实例实际拥有的Dash Montage，而不是按方向猜配置；反射只在游戏线程验收代码使用，无生产字段扩展。 */
    UAnimMontage* StableClipDashMontage(UAnimInstance* Anim)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 只读实例，纯辅助函数无this。
        const FObjectProperty* Property=Anim?FindFProperty<FObjectProperty>(Anim->GetClass(),TEXT("CurrentDashMontage")):nullptr; // 现有Lyra适配器Transient字段。
        return Property?Cast<UAnimMontage>(Property->GetObjectPropertyValue_InContainer(Anim)):nullptr;
    }
}

void ADemoViewGhostAudit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if (bStableClipReview) StopStableViewClipInput();
    Super::EndPlay(EndPlayReason);
}

void ADemoViewGhostAudit::StopStableViewClipInput()
{
    DEMO_LOG_CALL();
    if (StableClipInputHandle.IsValid()) FWorldDelegates::OnWorldPreActorTick.Remove(StableClipInputHandle);
    StableClipInputHandle.Reset();
    if (Controller.IsValid())
        for (const FKey Key:{EKeys::W,EKeys::S,EKeys::D,EKeys::LeftShift,EKeys::SpaceBar})
            Controller->InputKey(FInputKeyParams(Key,IE_Released,0.)); // 只释放本专项实际使用的按键，当前隔离World唯一玩家。
}

bool ADemoViewGhostAudit::BeginStableViewClipReview()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if (!Player.IsValid() || !Controller.IsValid() || !Controller->PlayerCameraManager || !Player->CanUsePlayerSkills()) return false;
    UDemoMeleeComponent* Melee=Player->FindComponentByClass<UDemoMeleeComponent>(); // 正式默认定义与装备流程，不替换动画资源。
    if (!Melee) return false;
    Melee->SetEquipped(bStableClipMelee);
    if (Melee->IsEquipped()!=bStableClipMelee) return false;
    UDemoPlayerAppearance* Appearance=Player->GetPlayerPresentation()->GetAppearance(); // 本次实际消费资产；候选只在已核隔离Editor游戏进程内临时改引用。
    if (!Appearance || !Appearance->bUseStableFirstPersonView || Player->CaptureFullBodyViewFrame().bUseAnimatedHead) return false;
    FString CandidateId; // 缺参数为正式baseline；显式未知/缺资源必须失败，不能静默回退。
    if (FParse::Value(FCommandLine::Get(),TEXT("DemoStableViewCandidate="),CandidateId))
    {
        if (CandidateId!=TEXT("A01") && CandidateId!=TEXT("A02") && CandidateId!=TEXT("A03") && CandidateId!=TEXT("A05") && CandidateId!=TEXT("A06") && CandidateId!=TEXT("A07"))
        { UE_LOG(LogFPSDemo,Warning,TEXT("STABLE_VIEW_CANDIDATE_REJECT unknown_id=%s"),*CandidateId); return false; }
        const FString Root=TEXT("/Game/Player/StableView20261114/")+(CandidateId==TEXT("A06") || CandidateId==TEXT("A07")?FString(TEXT("A05")):CandidateId)+TEXT("/"); // A06/A07复用已核A05真实网格，不另造副本。
        const bool bIndependentCandidate=CandidateId==TEXT("A05") || CandidateId==TEXT("A06") || CandidateId==TEXT("A07"); // 策略须在Pawn创建前选择，不能在采样器中强行显隐。
        const TCHAR* CandidateFlag=CandidateId==TEXT("A07")?TEXT("DemoStableOwnerLowerPoseCandidate"):CandidateId==TEXT("A06")?TEXT("DemoStableOwnerFPDepthCandidate"):TEXT("DemoStableOwnerFPCandidate"); // 固定白名单参数，非任意加载路径。
        if (bIndependentCandidate && (!FParse::Param(FCommandLine::Get(),CandidateFlag) || !Appearance->bUseIndependentFirstPersonWeapons))
        { UE_LOG(LogFPSDemo,Warning,TEXT("STABLE_VIEW_CANDIDATE_REJECT id=A05 independent_owner_asset_not_preloaded")); return false; }
        const FString GunName=bIndependentCandidate?TEXT("SK_StableOwner_LowerBody_A05"):TEXT("SK_StableOwner_GunArmsLegs_")+CandidateId;
        const FString MeleeName=bIndependentCandidate?GunName:TEXT("SK_StableOwner_MeleeLegs_")+CandidateId; // 枪械与近战共用同一闭合下身，分别保留原FP枪臂/剑手。
        USkeletalMesh* GunCandidate=LoadObject<USkeletalMesh>(nullptr,*(Root+GunName+TEXT(".")+GunName)); // 两候选必须同时存在且与各自原分区同Skeleton。
        USkeletalMesh* MeleeCandidate=LoadObject<USkeletalMesh>(nullptr,*(Root+MeleeName+TEXT(".")+MeleeName));
        UDemoMeleeDefinition* Definition=Melee->GetDefinition(); // 现有DA只改Owner渲染分区，不动Gameplay/动作/碰撞字段。
        if (!GunCandidate || !MeleeCandidate || !Appearance->FirstPersonBodyMesh || !Definition || !Definition->OwnerBodyWithoutArmsMesh
            || GunCandidate->GetSkeleton()!=Appearance->FirstPersonBodyMesh->GetSkeleton() || MeleeCandidate->GetSkeleton()!=Definition->OwnerBodyWithoutArmsMesh->GetSkeleton())
        { UE_LOG(LogFPSDemo,Warning,TEXT("STABLE_VIEW_CANDIDATE_REJECT id=%s missing_candidate_or_skeleton_mismatch"),*CandidateId); return false; }
        Appearance->FirstPersonBodyMesh=GunCandidate; Definition->OwnerBodyWithoutArmsMesh=MeleeCandidate; // 只存当前进程内存，不MarkPackageDirty或SavePackage；下一正式Presentation Tick应用。
        UE_LOG(LogFPSDemo,Log,TEXT("STABLE_VIEW_CANDIDATE_APPLIED id=%s gun=%s melee=%s memory_only=1"),*CandidateId,*GunCandidate->GetPathName(),*MeleeCandidate->GetPathName());
    }
    Controller->SetViewTarget(Player.Get()); // 拥有者正式镜头；不另建摄影机，不手动重复UpdateCamera。
    Frame=0; StableClipCase=0;
    Rows.Add(TEXT("frame,engine_frame,input_frame,case,case_age,world_time,dt,desired_control,actual_control,view_target,actor_transform,body_transform,camera_component_transform,shared_eye,final_pov_location,final_pov_rotation,fov,aspect,width,height,near_cm,head_world,spine_world,clavicle_l_world,clavicle_r_world,owner_mesh,owner_transform,owner_lod,leader_lod,compact_bones,stable,melee,falling,dash_evading,dash_cooldown,dash_start,dash_elapsed,dash_direction,dash_speed,dash_force_seconds,dash_montage,dash_montage_playing,dash_montage_position,active_montage,velocity,view_near_cm,fp_render_scale,owner_anim_class,owner_leader,owner_bone_frame,owner_pose_state,owner_pose_ready,owner_air_weight,owner_gait_phase,owner_postprocess_disabled"));
    StableClipGeometryRows.Add(TEXT("frame,case,mesh,lod,visible,vertices,triangles,near_cut_triangles,near_cut_torso,near_cut_arms,near_cut_lower,rays_hit_within_40cm,closest_ray_cm,closest_ray_bone,near_cm,fov,width,height,projection_m00,projection_m11,projection_offset_x,projection_offset_y,constrained_rect"));
    StableClipRayRows.Add(TEXT("frame,case,ray,screen_y,screen_z,triangle,bone,distance_cm,camera_forward_cm,normal_dot_ray,material,two_sided,owner_clip_radius_cm,nearest_surface_mask_discard,material_parent"));
    StableClipComponentRows.Add(TEXT("frame,case,component,class,asset,visible,hidden_game,owner_no_see,only_owner_see,owner_render_eligible,transform"));
    StableClipAssetRows.Add(TEXT("object,package,file,sha1,bytes,state"));
    StableClipCaseRows.Add(TEXT("case,samples,saw_real_dash,saw_real_dash_montage,saw_falling,contract_complete"));
    RecordStableViewClipAsset(Appearance);
    RecordStableViewClipAsset(Melee->GetDefinition());
    RecordStableViewClipAsset(Appearance->FirstPersonBodyMesh); // 同时冻结两个当前显示分区的真实磁盘身份，区分正式baseline和内存候选。
    if (Melee->GetDefinition()) RecordStableViewClipAsset(Melee->GetDefinition()->OwnerBodyWithoutArmsMesh);
    RecordStableViewClipAsset(Player->GetMesh()->GetSkeletalMeshAsset());
    StableClipInputHandle=FWorldDelegates::OnWorldPreActorTick.AddUObject(this,&ADemoViewGhostAudit::DriveStableViewClipInput); // 弱UObject绑定；回调先核World，Finish/EndPlay精确解绑。
    UE_LOG(LogFPSDemo,Display,TEXT("STABLE_VIEW_CLIP_BEGIN melee=%d directory=%s; production render policy, actual input/GAS, no visual pass implied"),bStableClipMelee,*Directory);
    return StableClipInputHandle.IsValid();
#else
    return false;
#endif
}

void ADemoViewGhostAudit::DriveStableViewClipInput(UWorld* World,ELevelTick TickType,float DeltaSeconds)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (bFinished || World!=GetWorld() || !Player.IsValid() || !Controller.IsValid() || TickType==LEVELTICK_ViewportsOnly) return;
    if (bStableClipCompleted) return;
    const double Now=World->GetTimeSeconds(); // 真实游戏秒，固定60Hz；等待冷却也不调整时钟或删GE。
    StableClipInputFrame=GFrameCounter;
    if (bStableClipAdvancePending)
    {
        bStableClipAdvancePending=false; ++StableClipCase;
        for (const FKey Key:{EKeys::W,EKeys::S,EKeys::D,EKeys::LeftShift,EKeys::SpaceBar}) Controller->InputKey(FInputKeyParams(Key,IE_Released,0.));
        if (StableClipCase>=3) { bStableClipCompleted=true; return; }
        StableClipCaseStart=-1.;
    }
    if (StableClipCaseStart<0.)
    {
        Controller->SetControlRotation(FRotator(-60,0,0));
        if (Player->GetDemoASC()->HasMatchingGameplayTag(DemoTags::DashCooldown)) return; // 上一案真实冷却到期才进入下一案。
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->ConsumeMovementInputVector();
        Player->SetActorLocation(StartLocation,false,nullptr,ETeleportType::TeleportPhysics); Player->SetActorRotation(FRotator::ZeroRotator); // 仅独立案边沿复位，不在技能中强行修改位移。
        StableClipCaseStart=Now; StableClipDashRequestedAt=-1.; StableClipLastDashStart=Player->GetDashPresentation().ServerStartTime; StableClipCaseSamples=0;
        bStableClipDashRequested=false; bStableClipSawDash=false; bStableClipSawMontage=false; bStableClipSawFalling=false; bStableClipJumpPressed=false;
        UE_LOG(LogFPSDemo,Log,TEXT("STABLE_VIEW_CLIP_CASE case=%d world_time=%.4f"),StableClipCase,Now);
    }
    const double Age=Now-StableClipCaseStart; // 当前案从真实输入边沿开始，而非PostUpdate截图计数。
    float Pitch=-60.f,Yaw=0.f; // 共同低头下限；0号案平滑经过全部中间俯仰，不只摆一个极限姿势。
    if (StableClipCase==0) { Pitch=-60.f*FMath::SmoothStep(.1f,.7f,static_cast<float>(Age)); Yaw=45.f*FMath::SmoothStep(.8f,1.2f,static_cast<float>(Age)); }
    if (StableClipCase==1) Yaw=35.f*FMath::SmoothStep(.37f,.62f,static_cast<float>(Age)); // 正式前冲中转头，检查胸肩与稳定镜头的相对运动。
    Controller->SetControlRotation(FRotator(Pitch,Yaw,0)); // 不在这里求动画/相机，正式PC/CMC/Anim按正常顺序更新。
    if (StableClipCase==2 && !bStableClipJumpPressed)
    {
        Controller->InputKey(FInputKeyParams(EKeys::S,IE_Pressed,1.)); Controller->InputKey(FInputKeyParams(EKeys::D,IE_Pressed,1.));
        Controller->InputKey(FInputKeyParams(EKeys::SpaceBar,IE_Pressed,1.)); bStableClipJumpPressed=true; // 真实后右移动/空格，下一正式输入处理后起跳。
    }
    if (StableClipCase==2 && Age>.18) Controller->InputKey(FInputKeyParams(EKeys::SpaceBar,IE_Released,0.));
    if (StableClipCase>0 && Age>=.3 && !bStableClipDashRequested)
    {
        Controller->InputKey(FInputKeyParams(EKeys::LeftShift,IE_Pressed,1.));
        bStableClipDashRequested=true; StableClipDashRequestedAt=Now; // 仅发一次，拒绝必须失败，不每帧重试直到侥幸成功。
    }
    if (bStableClipDashRequested && Now-StableClipDashRequestedAt>.05) Controller->InputKey(FInputKeyParams(EKeys::LeftShift,IE_Released,0.));
    if (StableClipCase==2 && Age>.9)
    { Controller->InputKey(FInputKeyParams(EKeys::S,IE_Released,0.)); Controller->InputKey(FInputKeyParams(EKeys::D,IE_Released,0.)); }
#endif
}

void ADemoViewGhostAudit::RecordStableViewClipAsset(UObject* Asset)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if (!Asset || StableClipSeenAssets.Contains(Asset->GetPathName())) return;
    StableClipSeenAssets.Add(Asset->GetPathName());
    const FString Package=Asset->GetOutermost()->GetName(); // 真实已加载对象的包，不按当前武器ID猜路径。
    FString File,Hash,State=TEXT("transient_or_no_package_file");
    int64 Bytes=0; // 原文件字节数，0不意味着空资产；state解释未落盘动态对象。
    if (FPackageName::IsValidLongPackageName(Package))
    {
        File=FPackageName::LongPackageNameToFilename(Package,FPackageName::GetAssetPackageExtension());
        TArray<uint8> Data; // 每资产只读取一次；写完SHA后立即释放，不跨帧保留大包。
        if (FFileHelper::LoadFileToArray(Data,*File))
        {
            uint8 Digest[FSHA1::DigestSize]; FSHA1::HashBuffer(Data.GetData(),Data.Num(),Digest);
            Hash=BytesToHex(Digest,FSHA1::DigestSize).ToLower(); Bytes=Data.Num(); State=TEXT("package_file_sha1");
        }
    }
    StableClipAssetRows.Add(FString::Join(TArray<FString>{StableClipCell(Asset->GetPathName()),StableClipCell(Package),StableClipCell(File),Hash,FString::Printf(TEXT("%lld"),Bytes),State},TEXT(",")));
#endif
}

void ADemoViewGhostAudit::TickStableViewClipReview(float DeltaSeconds)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (bStableClipCompleted) { Finish(true,TEXT("three actual-input cases captured; geometric/RHI visual review remains required")); return; }
    if (Frame>=1800) { Finish(false,TEXT("bounded stable clip frame budget exceeded")); return; }
    if (!StableClipInputFrame) return; // 初始化发生在PostUpdate，等待下一次PreActor真正发输入再记录。
    USkeletalMeshComponent* Body=Player->GetMesh(); // 已完成正式骨姿求值，生产Presentation是本Actor的Tick先决条件。
    USkeletalMeshComponent* OwnerBody=Player->GetPlayerPresentation()->GetFirstPersonBodyMesh(); // 当前本人FP身体；避免局部名遮蔽AActor::Owner造成C4458构建失败。
    const UDemoPlayerAppearance* FrameAppearance=Player->GetPlayerPresentation()->GetAppearance(); // 当前实际消费配置，独立Owner合同不借Begin初始化函数的局部变量。
    if (!Body || !OwnerBody || !OwnerBody->GetSkeletalMeshAsset() || !Player->GetDemoASC() || Player->CaptureFullBodyViewFrame().bUseAnimatedHead || Controller->GetViewTarget()!=Player.Get() || StableClipInputFrame!=GFrameCounter)
    { Finish(false,TEXT("stable owner/ASC/view target or same-frame input contract changed")); return; }
    UAnimInstance* Anim=Body->GetAnimInstance(); UAnimMontage* DashMontage=StableClipDashMontage(Anim); // 实际实例持有的Montage，非按配置推断。
    const bool DashPlaying=Anim && DashMontage && Anim->Montage_IsPlaying(DashMontage);
    const FDemoDashPresentation& Dash=Player->GetDashPresentation();
    const double Now=GetWorld()->GetTimeSeconds(),Age=StableClipCaseStart<0.?-1.:Now-StableClipCaseStart;
    const bool ActiveCase=Age>=0.; // 冷却等待另以负age记录，不冒充下一案样本。
    if (ActiveCase)
    {
        bStableClipSawDash|=bStableClipDashRequested && Dash.ServerStartTime>StableClipLastDashStart && Player->IsDashEvading();
        bStableClipSawMontage|=bStableClipSawDash && DashPlaying;
        bStableClipSawFalling|=Player->GetCharacterMovement()->IsFalling();
    }
    if (bStableClipDashRequested && Now-StableClipDashRequestedAt>.55 && !bStableClipSawDash)
    { Finish(false,TEXT("real Shift did not commit a new GAS dash within bounded window")); return; }
    const FMinimalViewInfo& POV=Controller->PlayerCameraManager->GetCameraCacheView(); // UE5.4正式接口，读取本帧唯一CameraManager更新后的最终缓存。
    int32 Width=0,Height=0; Controller->GetViewportSize(Width,Height);
    const FRotator Desired(StableClipCase==0?-60.f*FMath::SmoothStep(.1f,.7f,static_cast<float>(FMath::Max(0.,Age))):-60.f,
        StableClipCase==0?45.f*FMath::SmoothStep(.8f,1.2f,static_cast<float>(FMath::Max(0.,Age))):StableClipCase==1?35.f*FMath::SmoothStep(.37f,.62f,static_cast<float>(FMath::Max(0.,Age))):0.f,0.f); // 计划值独立列，不能以它替代实际镜头。
    TArray<FString> Cells={FString::FromInt(Frame),FString::Printf(TEXT("%llu"),GFrameCounter),FString::Printf(TEXT("%llu"),StableClipInputFrame),FString::FromInt(StableClipCase),FString::Printf(TEXT("%.6f"),Age),FString::Printf(TEXT("%.6f"),Now),FString::Printf(TEXT("%.6f"),DeltaSeconds),
        StableClipCell(Desired.ToString()),StableClipCell(Controller->GetControlRotation().ToString()),StableClipCell(GetPathNameSafe(Controller->GetViewTarget())),StableClipCell(Player->GetActorTransform().ToString()),StableClipCell(Body->GetComponentTransform().ToString()),StableClipCell(Player->GetFirstPersonCameraComponent()->GetComponentTransform().ToString()),
        StableClipCell(Player->GetSharedViewLocation().ToString()),StableClipCell(POV.Location.ToString()),StableClipCell(POV.Rotation.ToString()),FString::SanitizeFloat(POV.FOV),FString::SanitizeFloat(POV.AspectRatio),FString::FromInt(Width),FString::FromInt(Height),FString::SanitizeFloat(GNearClippingPlane),
        StableClipCell(Body->GetSocketLocation(TEXT("head")).ToString()),StableClipCell(Body->GetSocketLocation(TEXT("spine_03")).ToString()),StableClipCell(Body->GetSocketLocation(TEXT("clavicle_l")).ToString()),StableClipCell(Body->GetSocketLocation(TEXT("clavicle_r")).ToString()),
        StableClipCell(GetPathNameSafe(OwnerBody->GetSkeletalMeshAsset())),StableClipCell(OwnerBody->GetComponentTransform().ToString()),FString::FromInt(OwnerBody->GetPredictedLODLevel()),FString::FromInt(Body->GetPredictedLODLevel()),FString::FromInt(Anim?Anim->GetRequiredBones().GetCompactPoseNumBones():0),
        FString::FromInt(!Player->CaptureFullBodyViewFrame().bUseAnimatedHead),FString::FromInt(bStableClipMelee),FString::FromInt(Player->GetCharacterMovement()->IsFalling()),FString::FromInt(Player->IsDashEvading()),FString::FromInt(Player->GetDemoASC()->HasMatchingGameplayTag(DemoTags::DashCooldown)),
        FString::SanitizeFloat(Dash.ServerStartTime),FString::SanitizeFloat(Now-Dash.ServerStartTime),StableClipCell(FVector(Dash.WorldDirection).ToString()),FString::SanitizeFloat(Dash.ForceSpeed),FString::SanitizeFloat(Dash.ForceSeconds),StableClipCell(GetPathNameSafe(DashMontage)),FString::FromInt(DashPlaying),FString::SanitizeFloat(Anim && DashMontage?Anim->Montage_GetPosition(DashMontage):-1.f),StableClipCell(GetPathNameSafe(Anim?Anim->GetCurrentActiveMontage():nullptr)),StableClipCell(Player->GetVelocity().ToString())};
    Cells.Add(FString::SanitizeFloat(POV.GetFinalPerspectiveNearClipPlane())); // 全局near列保留，另列实际本人view覆盖，避免错误宣称FP专用near。
    Cells.Add(FString::SanitizeFloat(Player->GetPlayerPresentation()->GetFirstPersonRenderScale()));
    const UDemoOwnerLowerBodyAnimInstance* LowerInstance=Cast<UDemoOwnerLowerBodyAnimInstance>(OwnerBody->GetAnimInstance()); // 只读本帧已完成的Owner自己的GT快照。
    const FDemoOwnerLowerBodyAudit Lower=LowerInstance?LowerInstance->ReadAudit():FDemoOwnerLowerBodyAudit();
    Cells.Append({StableClipCell(GetPathNameSafe(OwnerBody->GetAnimInstance()?OwnerBody->GetAnimInstance()->GetClass():nullptr)),StableClipCell(GetPathNameSafe(OwnerBody->LeaderPoseComponent.Get())),FString::Printf(TEXT("%u"),OwnerBody->GetCurrentBoneTransformFrame()),FString::FromInt(Lower.State),FString::FromInt(Lower.bResourcesReady),FString::SanitizeFloat(Lower.AirWeight),FString::SanitizeFloat(Lower.GaitPhase),FString::FromInt(OwnerBody->GetDisablePostProcessBlueprint())});
    if (FrameAppearance && FrameAppearance->OwnerLowerBodyAnimClass && (!LowerInstance || !Lower.bResourcesReady || OwnerBody->LeaderPoseComponent.IsValid() || !OwnerBody->GetDisablePostProcessBlueprint()))
    { Finish(false,TEXT("independent Owner lowerbody instance/resources/leader/postprocess contract changed")); return; }
    Rows.Add(FString::Join(Cells,TEXT(",")));
    if (!StableClipSeenAssets.Contains(OwnerBody->GetSkeletalMeshAsset()->GetPathName())) RecordStableViewClipAsset(OwnerBody->GetSkeletalMeshAsset());
    if (DashMontage && !StableClipSeenAssets.Contains(DashMontage->GetPathName())) RecordStableViewClipAsset(DashMontage);
    if (ActiveCase)
    {
        ++StableClipCaseSamples;
        if (!ProbeStableViewClipGeometry(OwnerBody)) { Finish(false,TEXT("actual Owner skin/projection unavailable")); return; }
        if (Frame%2==0 && !FScreenshotRequest::IsScreenshotRequested()) FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("C%d_F%04d.png"),StableClipCase,Frame),false,false); // 真RHI30Hz，涵盖起刀前/冲刺/恢复，不插帧。
    }
    if (ActiveCase && (Frame%15==0 || DashPlaying))
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(Player.Get()); // 同Pawn当前实际组件，独立FP手/刀/枪/VFX分别可追溯。
        for (UPrimitiveComponent* Component:Components)
        {
            USkeletalMeshComponent* Skinned=Cast<USkeletalMeshComponent>(Component);
            const bool Eligible=Component->IsVisible() && !Component->bHiddenInGame && !Component->bOwnerNoSee; // 只报告Owner过滤资格；不声称已通过视锥/材质/遮挡剔除。
            StableClipComponentRows.Add(FString::Join(TArray<FString>{FString::FromInt(Frame),FString::FromInt(StableClipCase),StableClipCell(Component->GetName()),StableClipCell(Component->GetClass()->GetPathName()),StableClipCell(Skinned?GetPathNameSafe(Skinned->GetSkeletalMeshAsset()):TEXT("non_skeletal_component")),FString::FromInt(Component->IsVisible()),FString::FromInt(Component->bHiddenInGame),FString::FromInt(Component->bOwnerNoSee),FString::FromInt(Component->bOnlyOwnerSee),FString::FromInt(Eligible),StableClipCell(Component->GetComponentTransform().ToString())},TEXT(",")));
            if (Skinned && Skinned->GetSkeletalMeshAsset() && !StableClipSeenAssets.Contains(Skinned->GetSkeletalMeshAsset()->GetPathName())) RecordStableViewClipAsset(Skinned->GetSkeletalMeshAsset());
        }
    }
    if (ActiveCase && Age>1.8)
    {
        const bool Contract=StableClipCase==0 || (bStableClipSawDash && bStableClipSawMontage && (StableClipCase!=2 || bStableClipSawFalling)); // 真实技能和动画均须观察到；几何数量不控制通过。
        StableClipCaseRows.Add(FString::Printf(TEXT("%d,%d,%d,%d,%d,%d"),StableClipCase,StableClipCaseSamples,bStableClipSawDash,bStableClipSawMontage,bStableClipSawFalling,Contract));
        if (!Contract) { Finish(false,TEXT("required actual dash montage or airborne input was not observed")); return; }
        ++StableClipCompletedCases; bStableClipAdvancePending=true; // 下一PreActor才清键与推进，保持当前截图/镜头一致。
    }
    ++Frame;
#endif
}

bool ADemoViewGhostAudit::ProbeStableViewClipGeometry(USkeletalMeshComponent* Mesh)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (!Mesh || !Mesh->GetSkeletalMeshAsset()) return false;
    const int32 LodIndex=Mesh->GetPredictedLODLevel(); // 实际当前LOD；不能拿诊断强制LOD0覆盖生产策略。
    const FSkeletalMeshRenderData* Render=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering();
    if (!Render || !Render->LODRenderData.IsValidIndex(LodIndex)) return false;
    const FSkeletalMeshLODRenderData& Lod=Render->LODRenderData[LodIndex];
    FSkinWeightVertexBuffer* Weights=Mesh->GetSkinWeightBuffer(LodIndex);
    if (!Weights || !Lod.StaticVertexBuffers.PositionVertexBuffer.GetVertexData() || !Lod.GetNumVertices()) return false; // 不可读CPU位置时明确失败，不能访问已释放缓冲。
    const FMinimalViewInfo& POV=Controller->PlayerCameraManager->GetCameraCacheView(); // 同一最终视图，避免使用已弃用的POV别名接口。
    int32 Width=0,Height=0; Controller->GetViewportSize(Width,Height);
    ULocalPlayer* Local=Controller->GetLocalPlayer(); // 当前拥有者的真实视口投影，含FOV轴、letterbox和off-center；不会再次UpdateCamera。
    FSceneViewProjectionData Projection;
    if (Width<=0 || Height<=0 || !Local || !Local->ViewportClient || !Local->ViewportClient->Viewport || !Local->GetProjectionData(Local->ViewportClient->Viewport,Projection)) return false;
    if (Projection.ProjectionMatrix.M[3][3]>=1. || !Projection.ViewOrigin.Equals(POV.Location,.01)) return false; // 不以透视探针解释正交/XR错位视图。
    const double Near=Projection.GetNearPlaneFromProjectionMatrix(),ScaleX=Projection.ProjectionMatrix.M[0][0],ScaleY=Projection.ProjectionMatrix.M[1][1]; // 引擎实际矩阵而非名义FOV公式。
    const double OffsetX=Projection.ProjectionMatrix.M[2][0],OffsetY=Projection.ProjectionMatrix.M[2][1]; // 实际非对称视锥中心偏移。
    if (!FMath::IsFinite(Near) || Near<=0. || ScaleX<=0. || ScaleY<=0.) return false;
    if (Frame==142 || Frame==404 || Frame==406)
    {
        const TSharedRef<FJsonObject> Depth=MakeShared<FJsonObject>(); // 三个固定帧独立诊断；只读当前实际蒙皮，不改渲染和动作。
        Depth->SetNumberField(TEXT("frame"),Frame); Depth->SetNumberField(TEXT("case"),StableClipCase);
        Depth->SetNumberField(TEXT("render_scale"),Player->GetPlayerPresentation()->GetFirstPersonRenderScale());
        Depth->SetNumberField(TEXT("near_cm"),Near); Depth->SetNumberField(TEXT("projection_m00"),ScaleX); Depth->SetNumberField(TEXT("projection_m11"),ScaleY);
        Depth->SetNumberField(TEXT("projection_offset_x"),OffsetX); Depth->SetNumberField(TEXT("projection_offset_y"),OffsetY);
        Depth->SetNumberField(TEXT("width"),Width); Depth->SetNumberField(TEXT("height"),Height);
        const FIntRect Rect=Projection.GetConstrainedViewRect(); // 真实letterbox区域；不能把完整viewport尺寸当投影矩形。
        TArray<TSharedPtr<FJsonValue>> RectValues={MakeShared<FJsonValueNumber>(Rect.Min.X),MakeShared<FJsonValueNumber>(Rect.Min.Y),MakeShared<FJsonValueNumber>(Rect.Max.X),MakeShared<FJsonValueNumber>(Rect.Max.Y)};
        Depth->SetArrayField(TEXT("constrained_rect"),RectValues);
        TArray<TSharedPtr<FJsonValue>> Meshes; // 实际可见FP骨枪/双臂与Owner下身；静态刀不纳入此窄深度报告，仍由真实RHI检查。
        if (const TSharedPtr<FJsonObject> BodyDepth=StableClipDepthMesh(Mesh,POV,TEXT("body"))) Meshes.Add(MakeShared<FJsonValueObject>(BodyDepth));
        TInlineComponentArray<USkeletalMeshComponent*> SkeletalComponents(Player.Get());
        for (USkeletalMeshComponent* Component:SkeletalComponents)
            if (Component==Player->GetMesh1P() || Component->GetFName()==TEXT("MeleeFirstPersonArms"))
                if (const TSharedPtr<FJsonObject> FPDepth=StableClipDepthMesh(Component,POV,TEXT("fp"))) Meshes.Add(MakeShared<FJsonValueObject>(FPDepth));
        ADemoWeaponBase* Weapon=Player->GetWeaponComponent()?Player->GetWeaponComponent()->GetActiveWeapon():nullptr; // 只借用实际装备，不走资产候选重建。
        if (Weapon && !bStableClipMelee)
            if (const TSharedPtr<FJsonObject> GunDepth=StableClipDepthMesh(Weapon->GetWeaponSkeletalMesh(),POV,TEXT("fp"))) Meshes.Add(MakeShared<FJsonValueObject>(GunDepth));
        Depth->SetArrayField(TEXT("meshes"),Meshes);
        FString Json; const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
        if (!FJsonSerializer::Serialize(Depth,Writer) || !FFileHelper::SaveStringToFile(Json,*(Directory/FString::Printf(TEXT("C%d_F%04d_Depth.json"),StableClipCase,Frame)))) return false;
    }
    TArray<FMatrix44f> Matrices; Mesh->GetCurrentRefToLocalMatrices(Matrices,LodIndex); // 当前真实求值的线性蒙皮，不使用求解器目标。
    TArray<FVector> Points; Points.Reserve(Lod.GetNumVertices());
    const FQuat Inverse=POV.Rotation.Quaternion().Inverse();
    for (uint32 Index=0;Index<Lod.GetNumVertices();++Index)
    {
        const FVector Skin(USkinnedMeshComponent::GetSkinnedVertexPosition(Mesh,Index,Lod,*Weights,Matrices));
        Points.Add(Inverse.RotateVector(Mesh->GetComponentTransform().TransformPosition(Skin)-POV.Location)); // 相机+X前/+Y右/+Z上，cm。
    }
    const FRawStaticIndexBuffer16or32Interface* Indices=Lod.MultiSizeIndexContainer.GetIndexBuffer();
    if (!Indices) return false;
    if (StableClipCase==1 && Frame==140) // 唯一固定前Dash帧，供新腰封顶点真正按同一UE最终皮肤矩阵求值；不伪造径向姿态。
    {
        const TSharedRef<FJsonObject> Contract=MakeShared<FJsonObject>(); // 单帧、单网格完整作者合同，写完即释放，不逐帧保留全皮肤。
        Contract->SetStringField(TEXT("mesh"),Mesh->GetSkeletalMeshAsset()->GetPathName());
        Contract->SetStringField(TEXT("matrix_convention"),TEXT("UE row-major 4x4, row-vector position*M; ref_to_local is actual current component-space skin matrix, not world"));
        Contract->SetNumberField(TEXT("frame"),Frame); Contract->SetNumberField(TEXT("lod"),LodIndex);
        Contract->SetArrayField(TEXT("mesh_to_world"),StableClipMatrixValues(Mesh->GetComponentTransform().ToMatrixWithScale()));
        Contract->SetArrayField(TEXT("camera_to_world"),StableClipMatrixValues(FTransform(POV.Rotation,POV.Location).ToMatrixWithScale()));
        Contract->SetNumberField(TEXT("fov"),POV.FOV); Contract->SetNumberField(TEXT("near_cm"),Near);
        Contract->SetNumberField(TEXT("projection_m00"),ScaleX); Contract->SetNumberField(TEXT("projection_m11"),ScaleY);
        TArray<TSharedPtr<FJsonValue>> BoneRows; BoneRows.Reserve(Matrices.Num()); // 实际索引与骨名一起记录，禁止假定Blender骨表/section局部索引相同。
        for (int32 Bone=0;Bone<Matrices.Num();++Bone)
        {
            const TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("index"),Bone); Row->SetStringField(TEXT("name"),Mesh->GetBoneName(Bone).ToString());
            Row->SetArrayField(TEXT("ref_to_local"),StableClipMatrixValues(Matrices[Bone]));
            BoneRows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Contract->SetArrayField(TEXT("bones"),BoneRows);
        TArray<TSharedPtr<FJsonValue>> VertexRows; VertexRows.Reserve(Lod.GetNumVertices()); // 原参考坐标、真实权重和实际CPU皮肤位置可独立重建对比，不读取求解目标。
        for (uint32 Vertex=0;Vertex<Lod.GetNumVertices();++Vertex)
        {
            int32 Section=0,SectionVertex=0; Lod.GetSectionFromVertexIndex(Vertex,Section,SectionVertex);
            const FSkelMeshRenderSection& Part=Lod.RenderSections[Section];
            const FVector3f Ref=Lod.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Vertex);
            const FVector Skin(USkinnedMeshComponent::GetSkinnedVertexPosition(Mesh,Vertex,Lod,*Weights,Matrices));
            const TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>();
            Row->SetNumberField(TEXT("index"),Vertex); Row->SetNumberField(TEXT("section"),Section);
            TArray<TSharedPtr<FJsonValue>> Reference,Actual,Influences; // 三维坐标与[全骨索引,原始uint16权重]，总权重另记以核引擎量化。
            for (int32 Axis=0;Axis<3;++Axis)
            { Reference.Add(MakeShared<FJsonValueNumber>(Ref[Axis])); Actual.Add(MakeShared<FJsonValueNumber>(Skin[Axis])); }
            uint32 Total=0;
            for (uint32 Influence=0;Influence<Weights->GetMaxBoneInfluences();++Influence)
            {
                const uint16 Weight=Weights->GetBoneWeight(Vertex,Influence);
                if (!Weight) continue;
                const uint16 Bone=Part.BoneMap[Weights->GetBoneIndex(Vertex,Influence)];
                TArray<TSharedPtr<FJsonValue>> Pair={MakeShared<FJsonValueNumber>(Bone),MakeShared<FJsonValueNumber>(Weight)};
                Influences.Add(MakeShared<FJsonValueArray>(Pair)); Total+=Weight;
            }
            Row->SetArrayField(TEXT("reference_cs"),Reference); Row->SetArrayField(TEXT("actual_skin_cs"),Actual);
            Row->SetArrayField(TEXT("raw_bone_weights"),Influences); Row->SetNumberField(TEXT("raw_weight_sum"),Total);
            VertexRows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Contract->SetArrayField(TEXT("vertices"),VertexRows);
        FString Json; const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
        const FString File=Directory/TEXT("C1_F0140_SkinContract.json"); // 当前时间戳目录内唯一固定帧，保留其他失败证据。
        if (!FJsonSerializer::Serialize(Contract,Writer) || !FFileHelper::SaveStringToFile(Json,*File)) return false;
        UE_LOG(LogFPSDemo,Log,TEXT("STABLE_VIEW_SKIN_CONTRACT_SAVED frame=%d bones=%d vertices=%u file=%s"),Frame,Matrices.Num(),Lod.GetNumVertices(),*File);
    }
    for (const FSkelMeshRenderSection& Section:Lod.RenderSections) // 每材质父链只记录一次真实包SHA，避免把仅存在但未使用的候选当实际消费。
    {
        UMaterialInterface* Material=Mesh->GetMaterial(Section.MaterialIndex); // 当前可见Owner覆盖槽，不能只读SkeletalMesh默认材质。
        while (Material && !StableClipSeenAssets.Contains(Material->GetPathName()))
        {
            RecordStableViewClipAsset(Material);
            const UMaterialInstance* Instance=Cast<UMaterialInstance>(Material); // MID/MI都沿实际Parent追到已保存图，借用仅限当前采样。
            Material=Instance?Instance->Parent.Get():nullptr;
        }
    }
    FVector Directions[9]; double Distances[9]; int32 RayTriangles[9],RayBones[9],RayMaterials[9]; double RayFacing[9]; // 九条固定NDC网格射线，仅保存最近40cm见证。
    for (int32 Ray=0;Ray<9;++Ray)
    { Directions[Ray]=FVector(1.,(((Ray%3)-1)*.65-OffsetX)/ScaleX,(((Ray/3)-1)*.65-OffsetY)/ScaleY).GetSafeNormal(); Distances[Ray]=41.; RayTriangles[Ray]=INDEX_NONE; RayBones[Ray]=0; RayMaterials[Ray]=0; RayFacing[Ray]=0.; }
    int32 Cuts=0,Torso=0,Arms=0,Lower=0; // 近面交线只作几何分类，不把它当可见材质裁面的最终判断。
    TArray<FString> CutRows; CutRows.Add(TEXT("triangle,bone,section,material,two_sided,normal_dot_view,y0,z0,y1,z1"));
    for (int32 Triangle=0;Triangle<Indices->Num()/3;++Triangle)
    {
        const uint32 Vertex=Indices->Get(Triangle*3);
        const FVector P[3]={Points[Vertex],Points[Indices->Get(Triangle*3+1)],Points[Indices->Get(Triangle*3+2)]};
        int32 Section=0,SectionVertex=0; Lod.GetSectionFromVertexIndex(Vertex,Section,SectionVertex);
        const FSkelMeshRenderSection& Part=Lod.RenderSections[Section];
        if (Part.bDisabled) continue; // 实际渲染禁用的Section不能算可见遮挡。
        uint16 Maximum=0; int32 Bone=0; // 首顶点最强权重只用于区域归因，原始三角ID同时保存。
        for (uint32 Influence=0;Influence<Weights->GetMaxBoneInfluences();++Influence)
            if (Weights->GetBoneWeight(Vertex,Influence)>Maximum)
            { Maximum=Weights->GetBoneWeight(Vertex,Influence); Bone=Part.BoneMap[Weights->GetBoneIndex(Vertex,Influence)]; }
        const FString BoneName=Mesh->GetBoneName(Bone).ToString();
        const FVector Normal=FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).GetSafeNormal();
        for (int32 Ray=0;Ray<9;++Ray)
        {
            double Distance=0.; // 双面几何最近命中，朝向/材质另列，不能用此数声称最终像素遮挡。
            if (StableClipRayTriangle(Directions[Ray],P[0],P[1],P[2],Distance) && Distance<Distances[Ray])
            { Distances[Ray]=Distance; RayTriangles[Ray]=Triangle; RayBones[Ray]=Bone; RayMaterials[Ray]=Part.MaterialIndex; RayFacing[Ray]=FVector::DotProduct(Normal,Directions[Ray]); }
        }
        if (FMath::Min3(P[0].X,P[1].X,P[2].X)>=Near || FMath::Max3(P[0].X,P[1].X,P[2].X)<=Near) continue;
        FVector Cross[2]; int32 Found=0; // 同原探针的三角/近平面交线，再裁到实际视锥矩形。
        for (int32 Edge=0;Edge<3 && Found<2;++Edge)
        { const FVector A=P[Edge],B=P[(Edge+1)%3]; if ((A.X<Near)!=(B.X<Near)) Cross[Found++]=FMath::Lerp(A,B,(Near-A.X)/(B.X-A.X)); }
        if (Found!=2) continue;
        double Low=0.,High=1.; bool Inside=true;
        for (int32 Axis=1;Axis<3;++Axis)
        {
            const double Scale=Axis==1?ScaleX:ScaleY,Offset=Axis==1?OffsetX:OffsetY,Minimum=Near*(-1.-Offset)/Scale,MaximumCoord=Near*(1.-Offset)/Scale,A=Cross[0][Axis],D=Cross[1][Axis]-A;
            if (FMath::Abs(D)<1.e-8) { if (A<Minimum || A>MaximumCoord) Inside=false; continue; }
            const double T0=(Minimum-A)/D,T1=(MaximumCoord-A)/D;
            Low=FMath::Max(Low,FMath::Min(T0,T1)); High=FMath::Min(High,FMath::Max(T0,T1));
        }
        if (!Inside || Low>High) continue;
        ++Cuts;
        if (BoneName.Contains(TEXT("neck")) || BoneName.Contains(TEXT("spine")) || BoneName.Contains(TEXT("clavicle"))) ++Torso;
        if (BoneName.Contains(TEXT("arm")) || BoneName.Contains(TEXT("hand"))) ++Arms;
        if (BoneName.Contains(TEXT("thigh")) || BoneName.Contains(TEXT("calf")) || BoneName.Contains(TEXT("foot")) || BoneName.Contains(TEXT("pelvis"))) ++Lower;
        if (CutRows.Num()<=256)
        {
            const FVector A=FMath::Lerp(Cross[0],Cross[1],Low),B=FMath::Lerp(Cross[0],Cross[1],High);
            UMaterialInterface* Material=Mesh->GetMaterial(Part.MaterialIndex);
            CutRows.Add(FString::Printf(TEXT("%d,%s,%d,%s,%d,%.7f,%.7f,%.7f,%.7f,%.7f"),Triangle,*BoneName,Section,*StableClipCell(GetPathNameSafe(Material)),Material && Material->IsTwoSided(),FVector::DotProduct(Normal,P[0].GetSafeNormal()),A.Y/Near*ScaleX+OffsetX,A.Z/Near*ScaleY+OffsetY,B.Y/Near*ScaleX+OffsetX,B.Z/Near*ScaleY+OffsetY));
        }
    }
    int32 RayHits=0; double Closest=41.; FString ClosestBone; // 无命中时输出-1，不把默认41cm误报为实际距离。
    for (int32 Ray=0;Ray<9;++Ray)
        if (RayTriangles[Ray]!=INDEX_NONE)
        {
            ++RayHits; const FString Bone=Mesh->GetBoneName(RayBones[Ray]).ToString();
            UMaterialInterface* Material=Mesh->GetMaterial(RayMaterials[Ray]);
            float Radius=0.f; // 实际材质有效厘米；原材质缺该参数时不应标成参与裁剪。
            const bool HasClip=Material && Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OwnerNearClipCentimeters")),Radius);
            const bool NearestDiscarded=HasClip && FMath::IsFinite(Radius) && Radius>0.f && Distances[Ray]<Radius; // 只解释最近见证面的Step，不替代其后表面/材质/完整RHI验收。
            const UMaterialInstance* Instance=Cast<UMaterialInstance>(Material); // 真实动态覆盖的直接父资产，另由assets.csv记录完整父链SHA。
            StableClipRayRows.Add(FString::Printf(TEXT("%d,%d,%d,%.2f,%.2f,%d,%s,%.7f,%.7f,%.7f,%s,%d,%.5f,%d,%s"),Frame,StableClipCase,Ray,((Ray%3)-1)*.65,((Ray/3)-1)*.65,RayTriangles[Ray],*Bone,Distances[Ray],Distances[Ray]*Directions[Ray].X,RayFacing[Ray],*StableClipCell(GetPathNameSafe(Material)),Material && Material->IsTwoSided(),HasClip?Radius:0.f,NearestDiscarded,*StableClipCell(Instance?GetPathNameSafe(Instance->Parent.Get()):TEXT("no_instance_parent"))));
            if (Distances[Ray]<Closest) { Closest=Distances[Ray]; ClosestBone=Bone; }
        }
    const bool Visible=Mesh->IsVisible() && !Mesh->bHiddenInGame && !Mesh->bOwnerNoSee;
    StableClipGeometryRows.Add(FString::Printf(TEXT("%d,%d,%s,%d,%d,%u,%d,%d,%d,%d,%d,%d,%.7f,%s,%.5f,%.5f,%d,%d,%.8f,%.8f,%.8f,%.8f,%s"),Frame,StableClipCase,*StableClipCell(GetPathNameSafe(Mesh->GetSkeletalMeshAsset())),LodIndex,Visible,Lod.GetNumVertices(),Indices->Num()/3,Cuts,Torso,Arms,Lower,RayHits,RayHits?Closest:-1.,*ClosestBone,Near,POV.FOV,Width,Height,ScaleX,ScaleY,OffsetX,OffsetY,*StableClipCell(Projection.GetConstrainedViewRect().ToString())));
    if (StableClipWitnesses[StableClipCase]<2 && (Cuts>0 || RayHits>0) && GetWorld()->GetTimeSeconds()-StableClipCaseStart>.45+StableClipWitnesses[StableClipCase]*.35)
    {
        const FString Prefix=Directory/FString::Printf(TEXT("C%d_F%04d_Witness%d"),StableClipCase,Frame,StableClipWitnesses[StableClipCase]++);
        TArray<FString> Obj; Obj.Add(TEXT("# Actual current owner CPU skin in final camera coordinates, cm; no visual acceptance implied"));
        for (const FVector& P:Points) Obj.Add(FString::Printf(TEXT("v %.7f %.7f %.7f"),P.X,P.Y,P.Z));
        for (int32 Index=0;Index<Indices->Num();Index+=3) Obj.Add(FString::Printf(TEXT("f %u %u %u"),Indices->Get(Index)+1,Indices->Get(Index+1)+1,Indices->Get(Index+2)+1));
        if (!FFileHelper::SaveStringArrayToFile(Obj,*(Prefix+TEXT(".obj"))) || !FFileHelper::SaveStringArrayToFile(CutRows,*(Prefix+TEXT("_near.csv")))) return false;
    }
    return true;
#else
    return false;
#endif
}

bool ADemoViewGhostAudit::SaveStableViewClipReview(bool Success,const TCHAR* Reason)
{
    DEMO_LOG_CALL();
    if (Directory.IsEmpty()) return false;
    bool Saved=FFileHelper::SaveStringArrayToFile(StableClipGeometryRows,*(Directory/TEXT("geometry.csv")));
    Saved=FFileHelper::SaveStringArrayToFile(StableClipRayRows,*(Directory/TEXT("rays.csv"))) && Saved;
    Saved=FFileHelper::SaveStringArrayToFile(StableClipComponentRows,*(Directory/TEXT("components.csv"))) && Saved;
    Saved=FFileHelper::SaveStringArrayToFile(StableClipAssetRows,*(Directory/TEXT("assets.csv"))) && Saved;
    Saved=FFileHelper::SaveStringArrayToFile(StableClipCaseRows,*(Directory/TEXT("cases.csv"))) && Saved;
    const TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>(); // 单个小型运行清单，明确技术取证与视觉验收边界。
    Result->SetStringField(TEXT("scope"),TEXT("Actual owner view, static low-look / forward GAS dash / airborne side-back GAS dash; production rendering and cooldowns unchanged. Geometry is double-sided diagnostic evidence, not a visual pass."));
    Result->SetBoolField(TEXT("capture_contract_complete"),Success && StableClipCompletedCases==3);
    Result->SetBoolField(TEXT("visual_acceptance"),false);
    Result->SetBoolField(TEXT("melee"),bStableClipMelee);
    Result->SetNumberField(TEXT("completed_cases"),StableClipCompletedCases);
    Result->SetNumberField(TEXT("frames"),Frame);
    Result->SetStringField(TEXT("reason"),Reason);
    Result->SetStringField(TEXT("command_line"),FCommandLine::Get());
    Result->SetStringField(TEXT("input_phase"),TEXT("OnWorldPreActorTick; PostUpdateWork samples camera once updated by normal World flow"));
    Result->SetStringField(TEXT("projection_limit"),TEXT("Actual LocalPlayer perspective projection and constrained view rect, fixed 60Hz; nine double-sided rays <=40cm; near cuts alone do not prove pixel occlusion or camera inside/outside"));
    Result->SetNumberField(TEXT("fixed_delta_seconds"),FApp::GetFixedDeltaTime());
    FString Json; const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
    Saved=FJsonSerializer::Serialize(Result,Writer) && FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("result.json"))) && Saved;
    UE_LOG(LogFPSDemo,Display,TEXT("STABLE_VIEW_CLIP_%s cases=%d frames=%d files_saved=%d visual_acceptance=0 directory=%s"),Success && StableClipCompletedCases==3 && Saved?TEXT("CAPTURE_COMPLETE"):TEXT("FAILED"),StableClipCompletedCases,Frame,Saved,*Directory);
    return Saved;
}
