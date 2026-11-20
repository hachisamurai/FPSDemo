// 职责：Editor长枪专项真实装填/取消/恢复开火和当帧蒙皮证据；复用正式Actor、GAS、当前RenderLOD权重和引擎线性蒙皮，不修改骨/PP/姿态或推进第二次动画。
// 使用：原PlayerAnimationTest加-DemoGunHoldElbowReview -DemoFireArmJitterAudit -DemoGunElbowOperations；可30/60Hz原速RHI。额外-DemoReloadHoldAudit扩四枪32例并用独立Saved目录，旧计划保持。
#include "Tests/DemoPlayerAnimationTest.h"
#include "Animation/DemoPlayerAnimProxy.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/DemoPlayerAnimInstance.h" // 只观察立即开火前实际动作是否仍为Reload，不凭World时间相等声称无中间Hold。
#include "Animation/DemoLyraAnimInstance.h" // 单例中性休息用真实原生权重确认进入与真实转头后退出。
#include "Camera/CameraComponent.h" // Scope仅经正式接口改变FOV，专项不写相机位置/角度。
#include "Camera/PlayerCameraManager.h" // 只核本人实际ViewTarget的最终FOV；外部TP摄影机单独记录不当作镜内视图。
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SkeletalRenderPublic.h" // 只读本组件渲染对象LOD，仍不将CPU取样冒充GPU完成时间。
#include "Serialization/JsonSerializer.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

namespace
{
    /** 同步向本次独占文件写UTF8；不缓存整段皮肤帧，返回实际IO成功。 */
    bool ElbowSkinWrite(FArchive& File,const FString& Row)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ElbowSkinWrite"));
        FTCHARToUTF8 Bytes(*Row); File.Serialize(const_cast<ANSICHAR*>(Bytes.Get()),Bytes.Length()); return !File.IsError();
    }
    /** 当前变换纯值以cm/xyzw/实际scale写入元数据；不归一化掩盖异常。 */
    TSharedPtr<FJsonObject> ElbowSkinTransform(const FTransform& Value)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ElbowSkinTransform"));
        TSharedPtr<FJsonObject> Result=MakeShared<FJsonObject>();
        const FVector T=Value.GetLocation(),S=Value.GetScale3D(); const FQuat Q=Value.GetRotation();
        Result->SetArrayField(TEXT("t_cm"),{MakeShared<FJsonValueNumber>(T.X),MakeShared<FJsonValueNumber>(T.Y),MakeShared<FJsonValueNumber>(T.Z)});
        Result->SetArrayField(TEXT("q_xyzw"),{MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});
        Result->SetArrayField(TEXT("scale"),{MakeShared<FJsonValueNumber>(S.X),MakeShared<FJsonValueNumber>(S.Y),MakeShared<FJsonValueNumber>(S.Z)});return Result;
    }
}

void ADemoPlayerAnimationTest::TickGunElbowOperationInputs(float Now)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    const bool bReloadHoldAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoReloadHoldAudit")); // 仅由显式Editor操作计划进入，不影响生产输入。
    const int32 Operation=bReloadHoldAudit?Stage%8:Stage<15?Stage%5:0; // 原三枪五例/返回例；新四枪八例单独映射。
    const bool bCancelOperation=bReloadHoldAudit?(Operation==3 || Operation==4):Operation==3; // 新计划分别覆盖普通/空仓取消。
    const bool bFollowupOperation=bReloadHoldAudit?Operation>=5:Operation==4; // 新计划恢复Fire及两种Direct；老计划仍仅第4操作。
    const bool bDirectOperation=bReloadHoldAudit && Operation>=6; // Direct在第一真实退出观察时立即提交正式StartFire，另核是否确实无中间Hold。
    const float Elapsed=Now-StageStart;
    UDemoWeaponComponent* Equipment=Player->GetWeaponComponent(); // 本World角色拥有，同步借用。
    if (bReloadHoldAudit && bNativeIdleInterruptAudit && Operation==0)
    {
        const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 当前已完成NativeUpdate的缓存，退出再核真实权重归零。
        if (!bNativeIdleInterruptSubmitted && Elapsed>=4.f && Native && Native->NativeIdleBreakWeight>.95f)
        {
            APlayerController* PC=CastChecked<APlayerController>(Player->GetController());
            FRotator View=PC->GetControlRotation(); View.Yaw+=5.f; PC->SetControlRotation(View); // 一次真实转头输入，仍近中性；不设置bAllowNativeIdleBreak或状态机。
            bNativeIdleInterruptSubmitted=true;
            UE_LOG(LogFPSDemo,Display,TEXT("NATIVE_IDLE_AUDIT_LOOK_INTERRUPT stage=%d elapsed=%.6f native=%.6f"),Stage,Elapsed,Native->NativeIdleBreakWeight);
        }
        else if (bNativeIdleInterruptSubmitted && Native && Native->NativeIdleBreakWeight<.001f && !bNativeIdleExitObserved)
        { bNativeIdleExitObserved=true; UE_LOG(LogFPSDemo,Display,TEXT("NATIVE_IDLE_AUDIT_EXIT_OBSERVED stage=%d elapsed=%.6f"),Stage,Elapsed); }
    }
    const bool bScopeCase=bReloadHoldAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoScopeViewAudit")) && Stage==32; // 唯一可选第33例，其余32例流程不改。
    if (bScopeCase)
    {
        const APlayerController* PC=CastChecked<APlayerController>(Player->GetController()); // 本机真实控制器，不能用外部摄影机FOV验证本人开镜。
        UCameraComponent* View=Player->GetFirstPersonCameraComponent(); // 只读FOV/世界位置，变更仍经Aim GA与Scope接口。
        const TArray<TSharedPtr<FJsonValue>>* Cases=nullptr;
        if (!Report->TryGetArrayField(TEXT("cases"),Cases) || Cases->IsEmpty()) { FinishFireArmJitterAudit(false,TEXT("scope case metadata missing"));return; }
        const TSharedPtr<FJsonObject> Case=Cases->Last()->AsObject(); // 当前例总是最新追加；支持显式单例而不伪造前32条占位记录。
        if (ScopeViewAuditStep==0 && Elapsed>=.25f)
        {
            ScopeViewOriginalFOV=View->FieldOfView;ScopeViewActorBaseline=Player->GetActorLocation();
            ScopeViewSharedBaseline=Player->GetSharedViewLocation();ScopeViewComponentBaseline=View->GetComponentLocation();
            if (Weapon->GetWeaponId()!=TEXT("sniper") || Equipment->GetScopeLevel()!=0 || !FMath::IsNearlyEqual(ScopeViewOriginalFOV,90.f,.01f))
            { FinishFireArmJitterAudit(false,TEXT("scope fixture requires actual unscoped sniper and default90 FOV"));return; }
            Case->SetNumberField(TEXT("scope_original_fov"),ScopeViewOriginalFOV);
            Equipment->CycleAim(); ScopeViewAuditStep=1; // 正常一级Aim GA，不直接写ScopeLevel/FOV或回调。
        }
        else if (ScopeViewAuditStep>=1 && ScopeViewAuditStep<=3 && Elapsed>=.25f+.25f*ScopeViewAuditStep)
        {
            const int32 ExpectedLevel=ScopeViewAuditStep==3?0:ScopeViewAuditStep; // 一级/二级/退出按实际复制状态核对。
            const float ExpectedFOV=ScopeViewAuditStep==1?40.f:ScopeViewAuditStep==2?10.f:ScopeViewOriginalFOV;
            const float ActorDelta=FVector::Distance(Player->GetActorLocation(),ScopeViewActorBaseline);
            const float SharedDelta=FVector::Distance(Player->GetSharedViewLocation(),ScopeViewSharedBaseline);
            const float ComponentDelta=FVector::Distance(View->GetComponentLocation(),ScopeViewComponentBaseline);
            const bool bOwnerView=bFirstPersonCapture && PC->GetViewTarget()==Player.Get() && PC->PlayerCameraManager;
            const float ActualFOV=bOwnerView?PC->PlayerCameraManager->GetFOVAngle():View->FieldOfView; // 外部TP只断言真实组件，不伪称看见第一人称镜内画面。
            const bool bPassed=Equipment->GetScopeLevel()==ExpectedLevel && FMath::IsNearlyEqual(View->FieldOfView,ExpectedFOV,.01f)
                && (!bFirstPersonCapture || bOwnerView) && FMath::IsNearlyEqual(ActualFOV,ExpectedFOV,.01f) && ActorDelta<=.001f && SharedDelta<=.001f && ComponentDelta<=.001f;
            const FString Prefix=FString::Printf(TEXT("scope_step%d_"),ScopeViewAuditStep); // 明确分项数据，不让退出90替代两档40/10。
            Case->SetNumberField(Prefix+TEXT("actual_level"),Equipment->GetScopeLevel());Case->SetNumberField(Prefix+TEXT("component_fov"),View->FieldOfView);
            Case->SetNumberField(Prefix+TEXT("actual_view_fov"),ActualFOV);Case->SetBoolField(Prefix+TEXT("owner_view"),bOwnerView);
            Case->SetNumberField(Prefix+TEXT("actor_delta_cm"),ActorDelta);Case->SetNumberField(Prefix+TEXT("shared_delta_cm"),SharedDelta);Case->SetNumberField(Prefix+TEXT("camera_component_delta_cm"),ComponentDelta);
            Case->SetBoolField(Prefix+TEXT("passed"),bPassed);
            UE_LOG(LogFPSDemo,Display,TEXT("SCOPE_VIEW_AUDIT step=%d expected=%d fov=%.3f actualFov=%.3f owner=%d actorCm=%.6f sharedCm=%.6f componentCm=%.6f passed=%d"),ScopeViewAuditStep,ExpectedLevel,View->FieldOfView,ActualFOV,bOwnerView,ActorDelta,SharedDelta,ComponentDelta,bPassed);
            if (!bPassed) { FinishFireArmJitterAudit(false,TEXT("scope FOV90/40/10/90 or stable anchor contract failed"));return; }
            if (ScopeViewAuditStep==1) Equipment->CycleAim(); // 正式循环进入二级镜。
            else if (ScopeViewAuditStep==2) Equipment->StopAim(); // 正式取消Aim GA，核退出精确恢复，不手动SetFOV。
            ++ScopeViewAuditStep;
        }
        if (Elapsed<StageDuration)return;
        const bool bCompleted=ScopeViewAuditStep==4;Case->SetBoolField(TEXT("capture_coverage_passed"),bCompleted);Case->SetNumberField(TEXT("recorded_frames"),FireArmJitterCaseFrame);
        FinishFireArmJitterAudit(bCompleted,TEXT("32 reload cases and explicit sniper scope cycle captured; FOV/anchor checks only, visual/game/multiplayer acceptance pending"));return;
    }
    if (Operation==0 && !bReloadHoldAudit && Stage<15)
    {
        const float Phase=FMath::Clamp(Elapsed/StageDuration,0.f,1.f),Wave=FMath::Sin(2.f*PI*Phase);
        CastChecked<APlayerController>(Player->GetController())->SetControlRotation(FRotator(Wave*(Wave>=0?55.f:60.f),40.f*FMath::Sin(4.f*PI*Phase),0.f)); // 上一帧已冻结，下一帧真实视线驱动；无Pose覆盖。
    }
    if (Operation>0)
    {
        bGunElbowReloadObserved|=Weapon->IsReloading();
        if (bCancelOperation && !bGunElbowReloadCancelled && Elapsed>=.1f)
        { Equipment->CancelActions();bGunElbowReloadCancelled=true;UE_LOG(LogFPSDemo,Log,TEXT("GUN_ELBOW_REAL_CANCEL stage=%d world=%.6f"),Stage,Now); }
        if (!Weapon->IsReloading() && bGunElbowReloadObserved && !bGunElbowReloadCompleted)
        { bGunElbowReloadCompleted=true;GunElbowReloadExitTime=Now;UE_LOG(LogFPSDemo,Log,TEXT("GUN_ELBOW_RELOAD_EXIT stage=%d world=%.6f cancelled=%d"),Stage,Now,bGunElbowReloadCancelled); }
        if (bFollowupOperation && bGunElbowReloadCompleted && !bGunElbowFollowupShot && Now-GunElbowReloadExitTime>=(bDirectOperation?0.f:.025f))
        {
            const UDemoPlayerAnimInstance* ActualAnim=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 当前GT已采样的源状态，下一次普通动画Tick才消费新Shot。
            bReloadHoldDirectSourceObserved=ActualAnim && (ActualAnim->UpperBodyAction==TEXT("Reload") || ActualAnim->UpperBodyAction==TEXT("EmptyReload"));
            Equipment->StartFire();Equipment->StopFire();bGunElbowFollowupShot=true;
            ReloadHoldFollowupShotDelay=Weapon->GetShotPresentationState().Sequence==FireArmJitterStartShot+1?Weapon->GetShotPresentationState().ServerStartTime-GunElbowReloadExitTime:-1.f;
            UE_LOG(LogFPSDemo,Log,TEXT("GUN_ELBOW_RECOVERY_FIRE stage=%d direct=%d reload_source_before_input=%d observed_delay=%.6f shot_delay=%.6f seq=%d ammo=%d"),Stage,bDirectOperation,bReloadHoldDirectSourceObserved,Now-GunElbowReloadExitTime,ReloadHoldFollowupShotDelay,Weapon->GetShotPresentationState().Sequence,Weapon->GetAmmo()); // 延迟与实际源分别记录，零时间差本身不证明Direct。
        }
    }
    if (Elapsed<StageDuration)return;
    const bool bAmmo=Operation==0 || (bCancelOperation?Weapon->GetAmmo()==StartAmmo:Weapon->GetAmmo()==ExpectedAmmo-(bFollowupOperation?1:0) && Weapon->GetReserveAmmo()==ExpectedReserve);
    const bool bSuccess=FireArmJitterCaseFrame>=FMath::FloorToInt((StageDuration-.1f)*FireArmJitterSampleFPS) && bAmmo && !Weapon->IsReloading()
        && (!bNativeIdleInterruptAudit || (bNativeIdleInterruptSubmitted && bNativeIdleExitObserved)) // 显式新单例必须实际先进入，再经真实转头退出；不改变默认32/33例合同。
        && (Operation==0 || bGunElbowReloadObserved && bGunElbowReloadCompleted) && (!bCancelOperation || bGunElbowReloadCancelled)
        && (!bFollowupOperation || bGunElbowFollowupShot && Weapon->GetShotPresentationState().Sequence==FireArmJitterStartShot+1)
        && (!bDirectOperation || bReloadHoldDirectSourceObserved); // 未观察到真实无中间Hold则不授本例覆盖通过，原始数据仍保留。
    const TArray<TSharedPtr<FJsonValue>>* Cases=nullptr;
    if (Report->TryGetArrayField(TEXT("cases"),Cases) && !Cases->IsEmpty())
    {
        const TSharedPtr<FJsonObject> Case=Cases->Last()->AsObject(); // 单例保原stage编号，清单仅实际运行的一例，不制造跳过项通过。
        Case->SetBoolField(TEXT("capture_coverage_passed"),bSuccess);Case->SetNumberField(TEXT("recorded_frames"),FireArmJitterCaseFrame);
        Case->SetBoolField(TEXT("observed_reload"),bGunElbowReloadObserved);Case->SetBoolField(TEXT("observed_exit"),bGunElbowReloadCompleted);
        Case->SetBoolField(TEXT("cancel_submitted"),bGunElbowReloadCancelled);Case->SetBoolField(TEXT("followup_fire_submitted"),bGunElbowFollowupShot);Case->SetNumberField(TEXT("reload_exit_world_seconds"),GunElbowReloadExitTime);
        Case->SetBoolField(TEXT("direct_source_reload_before_fire"),bReloadHoldDirectSourceObserved);Case->SetNumberField(TEXT("successful_shot_delay_seconds"),ReloadHoldFollowupShotDelay);
        Case->SetNumberField(TEXT("final_ammo"),Weapon->GetAmmo());Case->SetNumberField(TEXT("final_reserve"),Weapon->GetReserveAmmo());
        Case->SetBoolField(TEXT("native_idle_look_interrupt_submitted"),bNativeIdleInterruptSubmitted);Case->SetBoolField(TEXT("native_idle_exit_observed_after_look"),bNativeIdleExitObserved);
    }
    UE_LOG(LogFPSDemo,Display,TEXT("GUN_ELBOW_OPERATION_END stage=%d case=%s passed=%d frames=%d ammo=%d"),Stage,*StageName,bSuccess,FireArmJitterCaseFrame,Weapon->GetAmmo());
    if (!bSuccess){FinishFireArmJitterAudit(false,TEXT("real gun operation coverage/ammo failed"));return;}
    if (ReloadHoldOnlyStage!=INDEX_NONE){FinishFireArmJitterAudit(true,TEXT("explicit single reload/hold stage captured; not complete32/33-case coverage"));return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowSkinProbe"))){FinishFireArmJitterAudit(GunElbowSkinProbeMask==3,TEXT("one formal hold sweep; cached skin matrix probe only, no pose acceptance"));return;}
    const int32 PlannedCases=bReloadHoldAudit?(FParse::Param(FCommandLine::Get(),TEXT("DemoScopeViewAudit"))?33:32):18; // Scope仅显式增加一例，旧32和18默认保持。
    if (++Stage==PlannedCases){FinishFireArmJitterAudit(true,bReloadHoldAudit?TEXT("32 four-gun reload/hold handoff cases captured; direct source observed; pose/visual acceptance pending"):TEXT("18 related gun operation/switch cases captured; pose/surface acceptance pending"));return;}
    if (!BeginFireArmJitterStage(Now))FinishFireArmJitterAudit(false,TEXT("next gun operation could not initialize"));
#endif
}

void ADemoPlayerAnimationTest::RecordGunElbowSkin(USkeletalMeshComponent* Mesh,const FDemoIdleBreakPoseAudit& Audit,const FString& Label)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if (!Mesh || !Mesh->GetSkeletalMeshAsset()){FinishFireArmJitterAudit(false,TEXT("skin actual mesh missing"));return;}
    const USkeletalMesh* Asset=Mesh->GetSkeletalMeshAsset(); // 当前真正可见资产，不借用旧几何缓存。
    const FSkeletalMeshRenderData* Render=Asset->GetResourceForRendering(); const int32 LodIndex=Mesh->GetPredictedLODLevel();
    if (!Render || !Render->LODRenderData.IsValidIndex(LodIndex)){FinishFireArmJitterAudit(false,TEXT("skin actual render LOD missing"));return;}
    const FSkeletalMeshLODRenderData& Lod=Render->LODRenderData[LodIndex]; FSkinWeightVertexBuffer* Weights=Mesh->GetSkinWeightBuffer(LodIndex);
    if (!Weights){FinishFireArmJitterAudit(false,TEXT("skin actual CPU weight buffer unavailable"));return;}
    const FReferenceSkeleton& Ref=Asset->GetRefSkeleton(); TArray<FMatrix44f> FinalMatrices;Mesh->GetCurrentRefToLocalMatrices(FinalMatrices,LodIndex); // 引擎自身Leader映射与最终PP矩阵，不调用Refresh/TickAnimation。
    const USkinnedMeshComponent* Leader=Mesh->LeaderPoseComponent.Get(); // Final缓存已经按本Mesh索引，无额外Leader重排。
    const USkinnedMeshComponent* Effective=Leader?Leader:Mesh;const TArray<uint8>& Visibility=Effective->GetBoneVisibilityStates(); // 引擎最终矩阵使用的实际可见性来源。
    const bool bCommonFrame=Mesh->GetComponentTransform().Equals(Player->GetMesh()->GetComponentTransform(),.0001); // 本项目Owner必须与Body共框架；不偷偷补另一套世界转换。
    if((Leader && Leader!=Player->GetMesh()) || !bCommonFrame || Mesh->GetRefPoseOverride().IsValid() || Weights->GetNumVertices()!=Lod.GetNumVertices()){FinishFireArmJitterAudit(false,TEXT("skin source leader/frame/inverse override/weight coverage differs from project contract"));return;}
    for(const FSkelMeshRenderSection& Section:Lod.RenderSections)for(FBoneIndexType Index:Section.BoneMap)
    {
        const int32 LeaderIndex=Leader?(Mesh->GetLeaderBoneMap().IsValidIndex(Index)?Mesh->GetLeaderBoneMap()[Index]:INDEX_NONE):static_cast<int32>(Index); // 只核真实用骨域映射，不重写已正确的Final缓存。
        if(!FinalMatrices.IsValidIndex(Index) || FinalMatrices[Index].ContainsNaN() || !Asset->GetRefBasesInvMatrix().IsValidIndex(Index) || Asset->GetRefBasesInvMatrix()[Index].ContainsNaN() || !Visibility.IsValidIndex(LeaderIndex) || Visibility[LeaderIndex]!=BVS_Visible || (Leader && Effective->GetSkinnedAsset()->GetRefSkeleton().GetBoneName(LeaderIndex)!=Ref.GetBoneName(Index))){FinishFireArmJitterAudit(false,TEXT("skin used bone matrix/leader/visibility contract invalid"));return;}
    }
    const TArray<FMatrix44f>& Inverse=Asset->GetRefBasesInvMatrix(); TArray<FMatrix44f> Before=FinalMatrices; // 未参与本LOD蒙皮的缺省骨不作为证据；实际Section骨必须全部有前PP值。
    bool bBefore=Audit.FireAimCount>0 && Audit.FireAnchorCount>0 && Audit.FireAfterAim.Num()==Audit.FirePoseNames.Num() && !Audit.FireAfterAim.IsEmpty() && Inverse.Num()==Ref.GetNum() && Before.Num()==Ref.GetNum();
    TArray<bool> Mapped;Mapped.Init(false,Ref.GetNum());
    for(int32 I=0;bBefore && I<Ref.GetNum();++I)
    {
        const int32 Compact=Audit.FirePoseNames.IndexOfByKey(Ref.GetBoneName(I));if(Compact==INDEX_NONE)continue;
        const FTransform CS=Audit.FireAfterAim[Compact]; // 已验证Body/Owner共框架，按引擎Follower公式直接使用Leader CS。
        Before[I]=Inverse[I]*FMatrix44f(CS.ToMatrixWithScale());Mapped[I]=true;
    }
    for(const FSkelMeshRenderSection& Section:Lod.RenderSections)for(FBoneIndexType Index:Section.BoneMap)if(!Mapped.IsValidIndex(Index) || !Mapped[Index])bBefore=false;
    TArray<FVector3f> FinalVertices,BeforeVertices; // 实际权重/Section映射下的缓存矩阵蒙皮，不执行第二次动画或PP。
    USkinnedMeshComponent::ComputeSkinnedPositions(Mesh,FinalVertices,FinalMatrices,Lod,*Weights);
    if(bBefore)USkinnedMeshComponent::ComputeSkinnedPositions(Mesh,BeforeVertices,Before,Lod,*Weights);
    if(FinalVertices.Num()!=static_cast<int32>(Lod.GetNumVertices()) || (bBefore && BeforeVertices.Num()!=FinalVertices.Num())){FinishFireArmJitterAudit(false,TEXT("cached matrix skin vertex count mismatch"));return;}
    // UE5.4 GetSkinnedVertexPosition(...,CachedRefToLocals)内部实例化<false>，忽略传入矩阵。
    // ComputeSkinnedPositions实例化<true>，因此前PP/最终必须走此路径；A06旧净差零不构成PP证据。
    const uint8 ProbeBit=Label==TEXT("Body")?1:2; double ProbeDelta=-1.; // 每个实际组件只鉴别一次，只扰动局部矩阵副本。
    TArray<TSharedPtr<FJsonValue>> ProbePoints; // 两个实际Section的有权重点及一个零权重点；只记录有限可独立复算样本。
    if(!(GunElbowSkinProbeMask&ProbeBit))
    {
        const int32 ProbeBone=Ref.FindBoneIndex(TEXT("lowerarm_twist_01_l")); // 当前真实有权重辅助骨，沿Z移动1cm应使相关顶点产生可测变化。
        if(!FinalMatrices.IsValidIndex(ProbeBone)){FinishFireArmJitterAudit(false,TEXT("cached skin probe actual helper missing"));return;}
        TArray<FMatrix44f> ProbeMatrices=FinalMatrices;ProbeMatrices[ProbeBone].M[3][2]+=1.f; // 不写组件、Reference或Before/Final缓存。
        TArray<FVector3f> ProbeVertices;USkinnedMeshComponent::ComputeSkinnedPositions(Mesh,ProbeVertices,ProbeMatrices,Lod,*Weights);
        ProbeDelta=0.;for(int32 I=0;I<FinalVertices.Num();++I)ProbeDelta=FMath::Max(ProbeDelta,static_cast<double>((ProbeVertices[I]-FinalVertices[I]).Size()));
        if(ProbeDelta<.001){FinishFireArmJitterAudit(false,TEXT("cached skin probe failed to distinguish matrix inputs"));return;}
        TSet<int32> SampledSections;bool bHasZero=false; // 不按固定顶点号假设Body与Owner共用渲染索引。
        for(uint32 V=0;V<Lod.GetNumVertices() && (SampledSections.Num()<2 || !bHasZero);++V)
        {
            int32 SectionIndex=0,SectionVertex=0;Lod.GetSectionFromVertexIndex(V,SectionIndex,SectionVertex);uint32 Total=0;TArray<TSharedPtr<FJsonValue>> Influences;
            FVector3f Manual=FVector3f::ZeroVector; // 与引擎缓存结果独立按真实调色板/量化权重求少量代表点LBS。
            for(uint32 J=0;J<Weights->GetMaxBoneInfluences();++J)
            {
                const uint16 Weight=Weights->GetBoneWeight(V,J);if(!Weight)continue;const int32 Palette=Weights->GetBoneIndex(V,J);
                if(!Lod.RenderSections[SectionIndex].BoneMap.IsValidIndex(Palette)){FinishFireArmJitterAudit(false,TEXT("probe weight palette invalid"));return;}
                const int32 Bone=Lod.RenderSections[SectionIndex].BoneMap[Palette];if(Bone==ProbeBone)Total+=Weight;
                Manual+=FinalMatrices[Bone].TransformPosition(Lod.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V))*(static_cast<float>(Weight)/65535.f); // UE返回标准16位RawBoneWeight，无重新归一化。
                TSharedPtr<FJsonObject> Influence=MakeShared<FJsonObject>();Influence->SetNumberField(TEXT("mesh_bone_index"),Bone);Influence->SetStringField(TEXT("bone"),Ref.GetBoneName(Bone).ToString());Influence->SetNumberField(TEXT("raw_weight_u16"),Weight);
                TArray<TSharedPtr<FJsonValue>> MatrixValues;for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)MatrixValues.Add(MakeShared<FJsonValueNumber>(FinalMatrices[Bone].M[R][C]));Influence->SetArrayField(TEXT("final_ref_to_local_row_major"),MatrixValues);Influences.Add(MakeShared<FJsonValueObject>(Influence));
            }
            if((Total && SampledSections.Contains(SectionIndex)) || (!Total && bHasZero))continue;
            const FVector3f Actual=ProbeVertices[V]-FinalVertices[V],Expected(0,0,static_cast<float>(Total)/65535.f); // Z平移1cm的独立预测只依赖该辅助骨的原始权重。
            if((Actual-Expected).Size()>.0003f || (Manual-FinalVertices[V]).Size()>.0003f){FinishFireArmJitterAudit(false,TEXT("cached skin representative weight/identity prediction failed"));return;}
            TSharedPtr<FJsonObject> Point=MakeShared<FJsonObject>();Point->SetNumberField(TEXT("render_vertex"),V);Point->SetNumberField(TEXT("section"),SectionIndex);Point->SetNumberField(TEXT("helper_weight_u16"),Total);Point->SetNumberField(TEXT("expected_delta_z_cm"),Expected.Z);Point->SetNumberField(TEXT("actual_delta_z_cm"),Actual.Z);Point->SetNumberField(TEXT("manual_lbs_error_cm"),(Manual-FinalVertices[V]).Size());Point->SetObjectField(TEXT("reference_point"),ElbowSkinTransform(FTransform(FVector(Lod.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)))));Point->SetArrayField(TEXT("influences"),Influences);ProbePoints.Add(MakeShared<FJsonValueObject>(Point));
            if(Total)SampledSections.Add(SectionIndex);else bHasZero=true;
        }
        GunElbowSkinProbeMask|=ProbeBit;
        UE_LOG(LogFPSDemo,Log,TEXT("GUN_ELBOW_CACHED_SKIN_PROBE label=%s helper=%s local_copy_z_cm=1 max_vertex_delta_cm=%.6f"),*Label,*Ref.GetBoneName(ProbeBone).ToString(),ProbeDelta);
    }
    const FString Directory=FireArmJitterDirectory/TEXT("Surface");IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Prefix=FString::Printf(TEXT("%02d_%05d_%s_L%d"),Stage,FireArmJitterFrame,*Label,LodIndex);
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*(Directory/(Prefix+TEXT(".csv")))));
    if(!File || !ElbowSkinWrite(*File,TEXT("render_vertex,section,ref_x,ref_y,ref_z,final_x,final_y,final_z,before_pp_x,before_pp_y,before_pp_z,pp_surface_delta_cm,left_weight_sum_u16\n"))){FinishFireArmJitterAudit(false,TEXT("skin output file unavailable"));return;}
    int32 Selected=0;double Maximum=0.; // 本帧左臂ROI实际最终线性蒙皮与前PP的净差，不据此自动判PP有错。
    for(uint32 V=0;V<Lod.GetNumVertices();++V)
    {
        int32 Section=0,SectionVertex=0;Lod.GetSectionFromVertexIndex(V,Section,SectionVertex);uint32 Left=0;
        for(uint32 J=0;J<Weights->GetMaxBoneInfluences();++J)
        {
            const uint16 Weight=Weights->GetBoneWeight(V,J);if(!Weight)continue;
            const int32 Palette=Weights->GetBoneIndex(V,J); // 实际非零影响必须位于该RenderSection调色板，不能让ensure后跳过当成成功。
            if(!Lod.RenderSections[Section].BoneMap.IsValidIndex(Palette)){FinishFireArmJitterAudit(false,TEXT("skin actual weight palette index invalid"));return;}
            const int32 Bone=Lod.RenderSections[Section].BoneMap[Palette];
            const FString Name=Ref.GetBoneName(Bone).ToString();bool bLeft=Name.EndsWith(TEXT("_l")) && (Name.StartsWith(TEXT("upperarm_")) || Name.StartsWith(TEXT("lowerarm_")) || Name.StartsWith(TEXT("wrist_")) || Name.StartsWith(TEXT("clavicle_")));
            for(int32 Parent=Bone;!bLeft && Parent!=INDEX_NONE;Parent=Ref.GetParentIndex(Parent))bLeft=Ref.GetBoneName(Parent)==TEXT("upperarm_l");
            if(bLeft)Left+=Weight;
        }
        if(!Left)continue;
        const FVector R(Lod.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)); const FVector P(FinalVertices[V]);
        const FVector B=bBefore?FVector(BeforeVertices[V]):FVector::ZeroVector;
        const double Difference=bBefore?FVector::Distance(P,B):-1.;Maximum=FMath::Max(Maximum,Difference);++Selected;
        const FString BeforeCells=bBefore?FString::Printf(TEXT("%.8f,%.8f,%.8f,%.8f"),B.X,B.Y,B.Z,Difference):TEXT(",,,-1"); // 缺失前PP不补假位置。
        if(!ElbowSkinWrite(*File,FString::Printf(TEXT("%u,%d,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%s,%u\n"),V,Section,R.X,R.Y,R.Z,P.X,P.Y,P.Z,*BeforeCells,Left))){FinishFireArmJitterAudit(false,TEXT("skin CSV write failed"));return;}
    }
    File->Flush();
    TSharedPtr<FJsonObject> Meta=MakeShared<FJsonObject>();Meta->SetStringField(TEXT("mesh"),Asset->GetPathName());Meta->SetStringField(TEXT("label"),Label);
    Meta->SetStringField(TEXT("leader"),GetPathNameSafe(Mesh->LeaderPoseComponent.Get()));Meta->SetNumberField(TEXT("lod"),LodIndex);Meta->SetNumberField(TEXT("forced_lod"),Mesh->GetForcedLOD());
    Meta->SetNumberField(TEXT("frame"),FireArmJitterFrame);Meta->SetNumberField(TEXT("stage"),Stage);Meta->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds());Meta->SetNumberField(TEXT("render_vertices"),Lod.GetNumVertices());
    Meta->SetNumberField(TEXT("selected_left_vertices"),Selected);Meta->SetBoolField(TEXT("before_pp_available"),bBefore);Meta->SetNumberField(TEXT("max_pp_surface_delta_cm"),Maximum);
    Meta->SetStringField(TEXT("skin_method"),TEXT("UE5.4 ComputeSkinnedPositions cached matrices true; A06 prior single-vertex PP deltas invalid"));
    Meta->SetStringField(TEXT("interval_scope"),TEXT("same-sample AfterAim snapshot to final CPU LBS; not isolated PP, GPU, morph/cloth/WPO acceptance"));
    Meta->SetBoolField(TEXT("common_body_owner_frame"),bCommonFrame);Meta->SetBoolField(TEXT("ref_pose_override"),Mesh->GetRefPoseOverride().IsValid());
    Meta->SetNumberField(TEXT("bone_transform_revision"),Mesh->GetBoneTransformRevisionNumber());Meta->SetNumberField(TEXT("anchor_count"),Audit.FireAnchorCount);Meta->SetNumberField(TEXT("aim_count"),Audit.FireAimCount);
    Meta->SetNumberField(TEXT("pending_first_lod"),Render->PendingFirstLODIdx);Meta->SetNumberField(TEXT("current_first_lod"),Render->CurrentFirstLODIdx);
    Meta->SetNumberField(TEXT("render_object_lod_at_cpu_sample"),Mesh->MeshObject?Mesh->MeshObject->GetLOD():INDEX_NONE); // 不强制等待RenderThread，不伪称本帧GPU绘制已完成。
    Meta->SetBoolField(TEXT("visible_flag"),Mesh->IsVisible());Meta->SetBoolField(TEXT("hidden_in_game"),Mesh->bHiddenInGame);Meta->SetBoolField(TEXT("only_owner_see"),Mesh->bOnlyOwnerSee);Meta->SetBoolField(TEXT("owner_no_see"),Mesh->bOwnerNoSee);
    Meta->SetBoolField(TEXT("owner_fp_capture"),bFirstPersonCapture);Meta->SetBoolField(TEXT("expected_owner_view"),bFirstPersonCapture && CastChecked<APlayerController>(Player->GetController())->GetViewTarget()==Player.Get());
    Meta->SetStringField(TEXT("additional_vertex_deformation"),TEXT("morph/cloth/deformer/WPO not isolated by CPU LBS; actual RHI review required")); // 5.4实例API未导出/配置接口protected，不改引擎或推测未测状态。
    Meta->SetNumberField(TEXT("cached_matrix_probe_delta_cm"),ProbeDelta); // -1表示本组件本轮已在此前实际采样帧完成鉴别。
    Meta->SetArrayField(TEXT("cached_matrix_representative_points"),ProbePoints);
    Meta->SetObjectField(TEXT("component_to_world"),ElbowSkinTransform(Mesh->GetComponentTransform()));
    TArray<TSharedPtr<FJsonValue>> Sections; // 保存本LOD真正BoneMap，供当前RenderVertex/权重与骨名对应。
    for(const FSkelMeshRenderSection& Section:Lod.RenderSections)
    {
        TSharedPtr<FJsonObject> Row=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Bones;
        TArray<TSharedPtr<FJsonValue>> Mappings; // 供少量点独立复核本Mesh索引/Leader索引与可见性，不按相同render_vertex跨网格比较。
        for(FBoneIndexType Index:Section.BoneMap)
        {
            Bones.Add(MakeShared<FJsonValueString>(Ref.GetBoneName(Index).ToString()));
            TSharedPtr<FJsonObject> Map=MakeShared<FJsonObject>();const int32 LeaderIndex=Leader?Mesh->GetLeaderBoneMap()[Index]:static_cast<int32>(Index);
            Map->SetNumberField(TEXT("mesh_index"),Index);Map->SetNumberField(TEXT("leader_index"),LeaderIndex);Map->SetStringField(TEXT("bone"),Ref.GetBoneName(Index).ToString());Map->SetNumberField(TEXT("visibility"),Visibility[LeaderIndex]);Mappings.Add(MakeShared<FJsonValueObject>(Map));
        }
        Row->SetNumberField(TEXT("base_vertex"),Section.BaseVertexIndex);Row->SetNumberField(TEXT("vertices"),Section.NumVertices);Row->SetArrayField(TEXT("bone_map"),Bones);Sections.Add(MakeShared<FJsonValueObject>(Row));
        Row->SetArrayField(TEXT("used_bone_mapping"),Mappings);
    }
    Meta->SetArrayField(TEXT("sections"),Sections);FString Json;
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
    if(!FJsonSerializer::Serialize(Meta.ToSharedRef(),Writer) || !FFileHelper::SaveStringToFile(Json,*(Directory/(Prefix+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)){FinishFireArmJitterAudit(false,TEXT("skin metadata write failed"));return;}
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_ELBOW_SKIN frame=%d stage=%d label=%s lod=%d vertices=%d before=%d maxPP=%.4f"),FireArmJitterFrame,Stage,*Label,LodIndex,Selected,bBefore,Maximum);
#endif
}
