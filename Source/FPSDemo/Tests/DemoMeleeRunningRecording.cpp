// 职责：Editor临时World录制真实Walking与近战并行；旧候选/LeftOnly门禁保持，BladeR03核验正式刀，固定R04..R07门禁核验独立1.60s候选及真实Montage/腿链运动。
// 依赖真实GAS、Lyra/FP图和RHI；固定60Hz，FP/跟随TP各六秒，长柄刀追加取消与卸装清理；只写修订隔离Saved证据，不选择DA或改动画/正式资产/存档。
// Editor显式-DemoMeleeFrontReview仅改TP观察机并追加相机/尾段骨姿证据，FP和720帧输入协议不变；输出_Front后缀，不将背面历史录像冒充正面。
// -DemoMeleeBodyClearance额外采完整Final161与真实LOD；仅再指定-DemoMeleeBodyClearanceLOD0才临时强BodyLOD0，正常autoLOD及生产相机/图不改。
#include "Tests/DemoMeleeTest.h"
#if WITH_EDITOR
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h" // 正式刀可有伤害数值，但录制仅允许真实Hub，不能用测试夹具在Combat伤敌。
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Weapons/Melee/DemoMeleeDefinition.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoLyraAnimInstance.h" // 实际上身Montage及同步位置的检查，不以业务状态代替动画播放。
#include "Animation/DemoPlayerAnimProxy.h" // 显式R04只读输入快照，游戏线程在Body求值完成后取副本。
#include "Animation/DemoMeleeAnimInstance.h"
#include "Animation/DemoPlayerPresentationComponent.h" // 全身第一人称只核对共享LeaderPose，不要求存在独立FP双臂。
#include "Animation/AnimSequence.h"
#include "BoneContainer.h" // 只读实际RequiredBones的网格骨索引，不能将161数组长度当LOD纠形骨已参与求值。
#include "Animation/DemoOwnerBodyLODComponent.h" // 显式LOD0诊断先挂起本地自动策略，避免切TP时策略恢复覆盖诊断设置。
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/DirectionalLight.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Debug/DemoLog.h"

namespace DemoMeleeRunningRecording
{
    const FVector FrontReviewOffset(390.,-180.,95.); // 正面偏侧观察机相对Actor的世界cm偏移；按R05实机审片收近，以看清手肩及130cm刀，不跟随输入Yaw。
    const FVector FrontReviewTargetOffset(0.,0.,15.); // 观察胸手区域的Actor相对目标cm；未修改角色头部或真实FP相机。
    constexpr float FrontReviewFov=60.f; // 仅Editor正面TP观察机水平FOV；R05实机审片后从65度收窄，原背面55度与FP实际FOV保持。
    /** 逐帧只读实例实际RequiredBones，返回竖线分隔骨名供CSV；空实例记none，不补猜参考骨。 */
    FString RequiredBoneNames(const UAnimInstance* Instance)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoMeleeRunningRecording::RequiredBoneNames"));
        if (!Instance) return TEXT("none");
        const FBoneContainer& Container=Instance->GetRequiredBones(); // 当前本游戏线程只读，不跨帧借用代理/容器。
        if (!Container.IsValid()) return TEXT("uninitialized"); // 暖机或失效实例不能解引用空参考，也不能补成161骨已求值。
        const FReferenceSkeleton& Reference=Container.GetReferenceSkeleton(); // 本容器实际网格骨序，对应GetBoneIndicesArray的mesh pose indices。
        TArray<FString> Names; Names.Reserve(Container.GetBoneIndicesArray().Num()); // 临时CSV值，只保存名字不保存UObject引用。
        for (const FBoneIndexType Index:Container.GetBoneIndicesArray())
            if (Index<Reference.GetNum()) Names.Add(Reference.GetBoneName(Index).ToString());
        return Names.IsEmpty()?TEXT("none"):FString::Join(Names,TEXT("|"));
    }
    /** 只读R04帧：Final完整161骨、Base/Upper实测7骨，BaseView/UpperView额外实测头颈；组件cm，不补猜缺失骨。 */
    void AppendPostureSnapshot(USkeletalMeshComponent* Mesh,const UDemoPlayerAnimInstance* Instance,int32 Frame,TArray<FString>& Rows)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoMeleeRunningRecording::AppendPostureSnapshot"));
        if (!Mesh || !Instance || !Mesh->GetSkeletalMeshAsset()) return;
        const FDemoIdleBreakPoseAudit Audit=Instance->ReadIdleBreakAudit(); // 只复制纯值，不保留工作线程代理引用。
        const TArray<FTransform>& Poses=Mesh->GetComponentSpaceTransforms();
        for (int32 Index=0;Index<Poses.Num();++Index)
        {
            const FTransform& Pose=Poses[Index]; const FVector P=Pose.GetLocation(),S=Pose.GetScale3D(); const FQuat Q=Pose.GetRotation();
            Rows.Add(FString::Printf(TEXT("%d,Final,%s,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f"),Frame,*Mesh->GetBoneName(Index).ToString(),P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z));
        }
        for (int32 Index=0;Index<Audit.PostureNames.Num();++Index)
            for (int32 Kind=0;Kind<2;++Kind)
            {
                const TArray<FTransform>& Input=Kind==0?Audit.NativeBasePosture:Audit.UpperPosture;
                if (!Input.IsValidIndex(Index)) continue;
                const FTransform& Pose=Input[Index]; const FVector P=Pose.GetLocation(),S=Pose.GetScale3D(); const FQuat Q=Pose.GetRotation();
                Rows.Add(FString::Printf(TEXT("%d,%s,%s,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f"),Frame,Kind==0?TEXT("Base"):TEXT("Upper"),*Audit.PostureNames[Index].ToString(),P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z));
            }
        for (int32 Index=0;Index<Audit.ViewPostureNames.Num();++Index)
            for (int32 Kind=0;Kind<2;++Kind)
            {
                const TArray<FTransform>& Input=Kind==0?Audit.NativeBaseViewPosture:Audit.UpperViewPosture; // 单独命名kind，不破坏旧7骨输入契约。
                if (!Input.IsValidIndex(Index)) continue;
                const FTransform& Pose=Input[Index]; const FVector P=Pose.GetLocation(),S=Pose.GetScale3D(); const FQuat Q=Pose.GetRotation();
                Rows.Add(FString::Printf(TEXT("%d,%s,%s,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f"),Frame,Kind==0?TEXT("BaseView"):TEXT("UpperView"),*Audit.ViewPostureNames[Index].ToString(),P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z));
            }
    }
    /** 逐帧保存真实组件核心骨；检查有限变换及相对参考姿态的局部缩放，不对允许修形的helper骨做单位缩放假设。 */
    bool AppendBones(USkeletalMeshComponent* Mesh,const TCHAR* View,int32 Frame,TArray<FString>& Rows,double& MaxScaleError,bool bLowerBody=false)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoMeleeRunningRecording::AppendBones"));
        if (!Mesh || !Mesh->GetSkeletalMeshAsset()) return false;
        const TArray<FTransform>& Component=Mesh->GetComponentSpaceTransforms(); // 本次求值后组件空间，单位cm；不跨帧持有。
        const TArray<FTransform> Local=Mesh->GetBoneSpaceTransforms(); // UE5.4非const读取API返回局部变换快照；只读结果，不重新求解骨骼。
        const TArray<FTransform>& Reference=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose(); // 静态参考缩放，普通骨不能被拉长凑姿态。
        TArray<FName> Names={TEXT("root"),TEXT("pelvis"),TEXT("spine_01"),TEXT("spine_03"),TEXT("spine_05"),TEXT("clavicle_l"),TEXT("clavicle_r"),TEXT("upperarm_l"),TEXT("upperarm_r"),TEXT("lowerarm_l"),TEXT("lowerarm_r"),TEXT("hand_l"),TEXT("hand_r"),TEXT("neck_01"),TEXT("head")}; // 两个实际骨架共同语义，不靠屏幕左右猜测手别。
        if (bLowerBody) Names.Append({TEXT("thigh_l"),TEXT("thigh_r"),TEXT("calf_l"),TEXT("calf_r"),TEXT("foot_l"),TEXT("foot_r"),TEXT("ball_l"),TEXT("ball_r")}); // 仅正式刀完整Body附加；独立FP双臂不假定存在腿骨。
        for (const FName Name:Names)
        {
            const int32 Index=Mesh->GetBoneIndex(Name); // 实际Skeleton索引，不假设两个网格索引相同。
            if (!Component.IsValidIndex(Index) || !Local.IsValidIndex(Index) || !Reference.IsValidIndex(Index)) return false;
            if (Component[Index].ContainsNaN() || Local[Index].ContainsNaN()) return false;
            const FVector Position=Component[Index].GetLocation(),Scale=Local[Index].GetScale3D(); // 组件位置与本地缩放刻意分列，避免混同空间。
            const FQuat Rotation=Component[Index].GetRotation(); // 组件四元数xyzw供事后复核，不修改姿态。
            const double Error=(Scale-Reference[Index].GetScale3D()).GetAbsMax(); // 普通链参考缩放差；不是骨长或表面验收结论。
            MaxScaleError=FMath::Max(MaxScaleError,Error);
            Rows.Add(FString::Printf(TEXT("%d,%s,%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f"),Frame,View,*Name.ToString(),Position.X,Position.Y,Position.Z,Rotation.X,Rotation.Y,Rotation.Z,Rotation.W,Scale.X,Scale.Y,Scale.Z,Error));
        }
        return true;
    }

    /** 调用Editor只读生产轨迹采样，用实际求值phase对照独立可见刀段；不复制旧公式、不扣血或改变组件。 */
    bool TraceAlignment(const ADemoCharacter* Player,const UDemoMeleeDefinition* Definition,float Phase,FVector& VisibleBase,FVector& VisibleTip,FVector& BakedBase,FVector& BakedTip)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoMeleeRunningRecording::TraceAlignment"));
        if (!Player || !Definition || Definition->Attacks.Num()!=1 || !Player->GetMesh()) return false;
        const UDemoMeleeComponent* Melee=Player->FindComponentByClass<UDemoMeleeComponent>(); // 当前正式组件，debug入口直接只读生产TracePose，不创建另一份测试算法。
        if (!Melee || Melee->GetDefinition()!=Definition || !FMath::IsFinite(Phase) || !Melee->DebugSampleActiveTracePose(Phase,BakedBase,BakedTip)) return false;
        const FTransform Weapon=Definition->GripOffset*Player->GetMesh()->GetSocketTransform(Definition->HandSocket,RTS_World); // UE本地*父，仅一遍right hand挂接，实际最终分层Body骨姿。
        VisibleBase=Weapon.TransformPosition(Definition->TraceBase); VisibleTip=Weapon.TransformPosition(Definition->TraceTip);
        return !VisibleBase.ContainsNaN() && !VisibleTip.ContainsNaN() && !BakedBase.ContainsNaN() && !BakedTip.ContainsNaN();
    }
}

void ADemoMeleeTest::RestoreRunningRecording()
{
    DEMO_LOG_CALL();
    if (!bRunningRecordingActive) return;
    bRunningRecordingActive=false; // 先清理锁存，World销毁导致回调重入时不会重复操作角色。
    if (ADemoCharacter* Player=RunningPawn.Get())
    {
        if (bRunningBodyForcedLODOverride && Player->GetMesh())
        {
            Player->GetMesh()->SetForcedLOD(RunningOriginalBodyForcedLOD);
            bRunningBodyForcedLODOverride=false;
            UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BODY_GEOMETRY_LOD_RESTORE forced_body_lod=%d"),RunningOriginalBodyForcedLOD); // 仅恢复本夹具显式覆盖，不改正常Owner策略。
        }
        UDemoMeleeComponent* Melee=Player->FindComponentByClass<UDemoMeleeComponent>(); // 当前有效组件，恢复不创建新玩法对象。
        if (Melee) { Melee->CancelAttack(); Melee->SetEquipped(bRunningOriginalEquipped); RemoveTickPrerequisiteComponent(Melee); }
        Player->ConsumeMovementInputVector();
        if (UCharacterMovementComponent* Move=Player->GetCharacterMovement())
        {
            Move->StopMovementImmediately();
            Move->SetMovementMode(static_cast<EMovementMode>(RunningOriginalMovementMode),RunningOriginalCustomMode);
        }
        Player->SetActorTransform(RunningOriginalTransform,false,nullptr,ETeleportType::TeleportPhysics);
    }
    if (ADemoPlayerController* PC=RunningController.Get())
    {
        PC->SetControlRotation(RunningOriginalControlRotation);
        if (RunningOriginalViewTarget.IsValid()) PC->SetViewTarget(RunningOriginalViewTarget.Get());
        else if (RunningPawn.IsValid()) PC->SetViewTarget(RunningPawn.Get());
    }
    if (UDemoOwnerBodyLODComponent* Policy=RunningSuspendedLODPolicy.Get())
    {
        if (bRunningLODPolicyWasActive) Policy->Activate(true); // 观察目标和原Body LOD均已恢复；下一次正常Tick重新应用生产FP/TP策略。
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BODY_GEOMETRY_LOD_POLICY_RESTORE active=%d"),bRunningLODPolicyWasActive);
    }
    RunningSuspendedLODPolicy.Reset(); bRunningLODPolicyWasActive=false;
    if (Camera.IsValid()) { Camera->Destroy(); Camera.Reset(); }
    if (RunningPlatform.IsValid()) { RunningPlatform->Destroy(); RunningPlatform.Reset(); }
    if (RunningFill.IsValid()) { RunningFill->Destroy(); RunningFill.Reset(); }
}

void ADemoMeleeTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeComboRegression"))) RestoreComboRegression(); // 只释放新模式自己的瞬态输入/相机，不复用旧录制状态。
    if (bRunningRecordingActive) RestoreRunningRecording(); // 只有本录制启动过才清理，旧验收流程没有额外状态变更。
    Super::EndPlay(EndPlayReason);
}

void ADemoMeleeTest::FinishRunningRecording(bool bSuccess,const TCHAR* Reason)
{
    DEMO_LOG_CALL();
    bool bSaved=true; // 写盘失败也必须恢复输入/视角，不能留下夹具控制状态。
    if (!TrialRecordingDir.IsEmpty())
    {
        bSaved=FFileHelper::SaveStringArrayToFile(TrialRecordingRows,*(TrialRecordingDir/TEXT("frames.csv")));
        bSaved=FFileHelper::SaveStringArrayToFile(RunningBoneRows,*(TrialRecordingDir/TEXT("bones.csv"))) && bSaved;
        if (bRunningBladeR03) bSaved=FFileHelper::SaveStringArrayToFile(RunningTraceRows,*(TrialRecordingDir/TEXT("trace_alignment.csv"))) && bSaved;
        if (!RunningPostureRows.IsEmpty()) bSaved=FFileHelper::SaveStringArrayToFile(RunningPostureRows,*(TrialRecordingDir/TEXT("posture_snapshots.csv"))) && bSaved;
        const FString Result=FString::Printf(TEXT("status=%s\nreason=%s\ndefinition=%s\nframes=%d\nfixed_fps=60\nfp_forward_attack_samples=%d\nfp_side_attack_samples=%d\ntp_forward_attack_samples=%d\ntp_side_attack_samples=%d\nscope=actual movement/attack/RHI fixture; not visual acceptance or multiplayer\n"),bSuccess&&bSaved?TEXT("FIXTURE_ASSERTIONS_PASSED"):TEXT("FAILED"),Reason,*RunningExpectedDefinition,TrialRecordingFrame,RunningOverlapCounts[0],RunningOverlapCounts[1],RunningOverlapCounts[2],RunningOverlapCounts[3]); // 不把夹具执行成功标记为动作交付。
        FString CompleteResult=Result; // 新模式附加范围，旧结果字段/含义不变。
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBodyClearance")))
            CompleteResult+=FString::Printf(TEXT("body_clearance_mode=1\nbody_geometry_final_frames=%d\nbody_geometry_expected_bones=161\nbody_geometry_lod_mode=%s\n"),RunningGeometryFinalFrames,FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBodyClearanceLOD0"))?TEXT("explicit_body_lod0_diagnostic"):TEXT("normal_policy_actual_lod_recorded")); // 采到全骨不等于实际LOD体表通过；真实LOD/RequiredBones另逐帧记录。
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeFrontReview")))
        {
            CompleteResult+=FString::Printf(TEXT("front_review=1\nobserver_view=front_three_quarter\nobserver_offset_world_cm=%.6f,%.6f,%.6f\nobserver_target_actor_offset_cm=%.6f,%.6f,%.6f\nobserver_fov_degrees=%.6f\ntail_posture_dense_local_frames=280..320\n"),DemoMeleeRunningRecording::FrontReviewOffset.X,DemoMeleeRunningRecording::FrontReviewOffset.Y,DemoMeleeRunningRecording::FrontReviewOffset.Z,DemoMeleeRunningRecording::FrontReviewTargetOffset.X,DemoMeleeRunningRecording::FrontReviewTargetOffset.Y,DemoMeleeRunningRecording::FrontReviewTargetOffset.Z,DemoMeleeRunningRecording::FrontReviewFov); // 逐帧真实相机变换另附frames.csv；标明观察方向，不能混同原背面或FP。
        }
        if (bRunningBladeR03)
        {
            CompleteResult+=FString::Printf(TEXT("blade_r03_mode=1\ncomplete_source_attacks=%d\nexplicit_cancel_verified=%d\nunequip_cancel_verified=%d\nfp_forward_lowerbody_motion_samples=%d\nfp_side_lowerbody_motion_samples=%d\ntp_forward_lowerbody_motion_samples=%d\ntp_side_lowerbody_motion_samples=%d\nlowerbody_peak_step_degrees=%.8f,%.8f,%.8f,%.8f\n"),RunningCompleteSourceAttacks,bRunningCancelVerified,bRunningUnequipVerified,RunningLowerBodyMotionCounts[0],RunningLowerBodyMotionCounts[1],RunningLowerBodyMotionCounts[2],RunningLowerBodyMotionCounts[3],RunningLowerBodyPeakStepDegrees[0],RunningLowerBodyPeakStepDegrees[1],RunningLowerBodyPeakStepDegrees[2],RunningLowerBodyPeakStepDegrees[3]);
            CompleteResult+=FString::Printf(TEXT("trace_alignment_diagnostic_only=1\ntrace_alignment_base_max_cm=%.8f\ntrace_alignment_tip_max_cm=%.8f\nhit_window_alignment_base_max_cm=%.8f\nhit_window_alignment_tip_max_cm=%.8f\n"),RunningTraceMaxBaseError,RunningTraceMaxTipError,RunningHitTraceMaxBaseError,RunningHitTraceMaxTipError);
        }
        bSaved=FFileHelper::SaveStringToFile(CompleteResult,*(TrialRecordingDir/TEXT("result.txt"))) && bSaved;
    }
    RestoreRunningRecording();
    bFailed=!bSuccess || !bSaved;
    if (bFailed)
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RUNNING_RECORD_FAILED reason=%s saved=%d directory=%s"),Reason,bSaved,*TrialRecordingDir); }
    else
    { UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RUNNING_RECORD_SUCCESS frames=%d directory=%s"),TrialRecordingFrame,*TrialRecordingDir); }
    SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,bFailed?1:0);
}

void ADemoMeleeTest::TickRunningRecording()
{
    DEMO_LOG_TICK();
    const double Now=GetWorld()->GetTimeSeconds(); // 固定60Hz真实World时间，不手动推进动画时间或播放倍率。
    const bool bFrontReview=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeFrontReview")); // Editor录制夹具局部只读开关，仅TP构图/证据采样；不影响生产动画或FP变换。
    const bool bBodyClearance=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBodyClearance")); // 只读完整骨姿/LOD专项，默认不改变LOD选择或生产动画。
    const bool bBodyClearanceLOD0=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBodyClearanceLOD0")); // 第二个显式诊断开关，只有BodyClearance+Front才允许暂时强BodyLOD0。
    if (bRunningBodyForcedLODOverride && RunningPawn.IsValid() && RunningPawn->GetMesh() && RunningPawn->GetMesh()->GetForcedLOD()!=1)
        RunningPawn->GetMesh()->SetForcedLOD(1); // 暖机/切到观察机时Owner策略会正常恢复LOD；诊断仅将下一帧Body重新明确为LOD0，不写生产策略。
    if (Now<NextTime) return;
    // 同步局部断言只捕获this；本次Tick内调用，失败先保存/恢复再退出，不将Lambda交给异步线程或委托。
    const auto Require=[this](bool Value,const TCHAR* Reason)->bool { DEMO_LOG_TICK(); if (!Value) FinishRunningRecording(false,Reason); return Value; };
    if (!Require(Now<35.,TEXT("bounded recording time exceeded"))) return;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 真实候选GameMode，不构造假玩法阶段。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 唯一本地拥有者。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 当前真实Avatar。
    UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 输入走现有GAS/复制时间流程。
    UDemoMeleeDefinition* Definition=Melee?Melee->GetDefinition():nullptr; // 仅借用，不设置任何DA属性。
    UCharacterMovementComponent* Move=Player?Player->GetCharacterMovement():nullptr; // 真实Walking，不使用Flying或手写位置轨迹。
    if (!Require(Mode && PC && Player && Melee && Definition && Move,TEXT("runtime services missing"))) return;
    if (Step==0)
    {
        if (!Require(!bBodyClearanceLOD0 || (bBodyClearance && bFrontReview),TEXT("explicit BodyLOD0 diagnostic requires BodyClearance and FrontReview"))) return;
        if (!Require(FApp::UseFixedTimeStep() && FMath::IsNearlyEqual(FApp::GetFixedDeltaTime(),1./60.,.00001),TEXT("fixed 60 Hz required"))) return;
        if (!Require(FParse::Value(FCommandLine::Get(),TEXT("DemoMeleeExpectedDefinition="),RunningExpectedDefinition) && Definition->GetPathName()==RunningExpectedDefinition,TEXT("candidate definition path differs from assertion"))) return;
        const bool bAcceptedLeft=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeLeftOnly")); // Editor专项使用当前正式单左扫，原候选断言不放宽。
        const bool bClearanceCandidateR07=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladeClearanceR07")); // 本次完整源双臂避体候选，R06与正式DA保持可回退。
        const bool bTorsoCandidateR06=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladeTorsoR06")); // 本次仅尾段连续回收候选，旧胸肩动作与正式入口不覆盖。
        const bool bTorsoCandidateR05=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladeTorsoR05")); // 本次固定胸腰候选，不重用R04路径或放宽正常运行断言。
        bRunningBladePostureR04=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladePostureR04")); // 只核对固定旧候选，真实加载由组件Editor门禁完成。
        if (!Require(static_cast<int32>(bClearanceCandidateR07)+static_cast<int32>(bTorsoCandidateR06)+static_cast<int32>(bTorsoCandidateR05)+static_cast<int32>(bRunningBladePostureR04)<=1,TEXT("choose at most one fixed R04/R05/R06/R07 candidate revision"))) return;
        bRunningBladeR03=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladeR03")) || bRunningBladePostureR04 || bTorsoCandidateR05 || bTorsoCandidateR06 || bClearanceCandidateR07; // 固定候选复用真实移动/Montage/取消协议。
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleePostureR04")) || bTorsoCandidateR05 || bTorsoCandidateR06 || bClearanceCandidateR07 || bFrontReview || bBodyClearance) RunningPostureRows.Add(TEXT("frame,kind,bone,x,y,z,qx,qy,qz,qw,sx,sy,sz")); // BodyClearance完整采样，其余保持原稀疏诊断格式。
        const bool bExpectedPath=bClearanceCandidateR07?RunningExpectedDefinition==TEXT("/Game/Weapons/Melee/Candidates/MannyBladeClearanceR07/DA_BladeClearanceR07.DA_BladeClearanceR07")
            :bTorsoCandidateR06?RunningExpectedDefinition==TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR06/DA_BladeTorsoR06.DA_BladeTorsoR06")
            :bTorsoCandidateR05?RunningExpectedDefinition==TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR05/DA_BladeTorsoR05.DA_BladeTorsoR05")
            :bRunningBladePostureR04?RunningExpectedDefinition==TEXT("/Game/Weapons/Melee/Candidates/MannyBladePostureR04/DA_BladePostureR04.DA_BladePostureR04")
            :bRunningBladeR03?RunningExpectedDefinition==TEXT("/Game/Weapons/Melee/DA_LongGripBlade.DA_LongGripBlade")
            :bAcceptedLeft?RunningExpectedDefinition==TEXT("/Game/Weapons/Melee/DA_BaseballBat.DA_BaseballBat"):RunningExpectedDefinition.StartsWith(TEXT("/Game/Weapons/Melee/Candidates/")); // 正式新刀必须由普通运行默认真实加载，而非候选开关覆盖。
        if (!Require(!(bRunningBladeR03 && bAcceptedLeft) && bExpectedPath && Definition->bUseAuthoredSpine && !Definition->bStationaryFullBody,TEXT("authored spine required; moving legs must remain locomotion"))) return;
        if (!Require(Definition->Attacks.Num()>0 && FMath::IsNearlyEqual(Definition->Attacks[0].Duration,bRunningBladeR03?1.6f:bAcceptedLeft?1.2f:64.f/30.f,.002f),TEXT("expected original source duration"))) return;
        if (bRunningBladeR03)
        {
            if (!Require(Definition->Attacks.Num()==1 && Definition->HorizontalAttackSequence.IsEmpty() && Definition->Mesh && Definition->Mesh->GetPathName().StartsWith(TEXT("/Game/Weapons/Melee/LongGripBlade/"))
                && Definition->Attacks[0].ThirdPerson && Definition->Attacks[0].ThirdPerson->GetPathName().StartsWith(bClearanceCandidateR07?TEXT("/Game/Weapons/Melee/Candidates/MannyBladeClearanceR07/Manny/"):bTorsoCandidateR06?TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR06/Manny/"):bTorsoCandidateR05?TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR05/Manny/"):bRunningBladePostureR04?TEXT("/Game/Weapons/Melee/Candidates/MannyBladePostureR04/Manny/"):TEXT("/Game/Weapons/Melee/LongGripBlade/")),TEXT("blade must own exactly the selected1.60s source and new mesh"))) return;
            if (bClearanceCandidateR07 && !Require(Definition->ThirdPersonIdle && Definition->FirstPersonIdle==Definition->ThirdPersonIdle
                && Definition->ThirdPersonIdle->GetPathName()==TEXT("/Game/Weapons/Melee/Candidates/MannyBladeClearanceR07/Manny/A_BladeClearanceR07_Idle.A_BladeClearanceR07_Idle")
                && Definition->Attacks[0].FirstPerson==Definition->Attacks[0].ThirdPerson
                && Definition->Attacks[0].ThirdPerson->GetPathName()==TEXT("/Game/Weapons/Melee/Candidates/MannyBladeClearanceR07/Manny/A_BladeClearanceR07_Attack.A_BladeClearanceR07_Attack"),TEXT("R07 fixture requires exact shared Idle/Attack assets"))) return; // 不以目录中其他资产替代本体避让候选。
            if (bTorsoCandidateR06 && !Require(Definition->ThirdPersonIdle && Definition->FirstPersonIdle==Definition->ThirdPersonIdle
                && Definition->ThirdPersonIdle->GetPathName()==TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR06/Manny/A_BladeTorsoR06_Idle.A_BladeTorsoR06_Idle")
                && Definition->Attacks[0].FirstPerson==Definition->Attacks[0].ThirdPerson
                && Definition->Attacks[0].ThirdPerson->GetPathName()==TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR06/Manny/A_BladeTorsoR06_Attack.A_BladeTorsoR06_Attack"),TEXT("R06 fixture requires both exact shared Idle/Attack assets"))) return; // 精确两AS门禁，不以同目录的其他动画冒充尾段修复。
        }
        if (bAcceptedLeft)
        {
            if (!Require(Definition->Attacks.Num()==1 && Definition->HorizontalAttackSequence.IsEmpty() && Definition->Attacks[0].ThirdPerson->GetPathName()==TEXT("/Game/Weapons/Melee/Candidates/TPRebuild20261030/A_TP_Bat_Left_R01.A_TP_Bat_Left_R01"),TEXT("accepted left is the only configured attack"))) return;
        }
        else if (!bRunningBladeR03) for (const FDemoMeleeAttack& Attack:Definition->Attacks) if (!Require(Attack.Damage==0.f,TEXT("animation fixture requires zero candidate damage"))) return;
        RunningPawn=Player; RunningController=PC; RunningOriginalViewTarget=PC->GetViewTarget(); RunningOriginalTransform=Player->GetActorTransform(); RunningOriginalControlRotation=PC->GetControlRotation();
        RunningOriginalMovementMode=Move->MovementMode; RunningOriginalCustomMode=Move->CustomMovementMode; bRunningOriginalEquipped=Melee->IsEquipped(); bRunningRecordingActive=true;
        if (bBodyClearanceLOD0)
        {
            RunningSuspendedLODPolicy=Player->FindComponentByClass<UDemoOwnerBodyLODComponent>();
            if (UDemoOwnerBodyLODComponent* Policy=RunningSuspendedLODPolicy.Get())
            {
                bRunningLODPolicyWasActive=Policy->IsActive();
                if (bRunningLODPolicyWasActive) Policy->Deactivate(); // 先释放策略原有FP覆盖；不在姿态求值后改flag伪造LOD0。
                UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BODY_GEOMETRY_LOD_POLICY_SUSPEND was_active=%d"),bRunningLODPolicyWasActive);
            }
            RunningOriginalBodyForcedLOD=Player->GetMesh()->GetForcedLOD();
            bRunningBodyForcedLODOverride=true; Player->GetMesh()->SetForcedLOD(1);
            UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BODY_GEOMETRY_LOD_ENTER saved_body_lod=%d forced_body_lod=1 explicit_diagnostic=1"),RunningOriginalBodyForcedLOD); // 只本次World/Body；退出恢复，不修改远端或资产LOD。
        }
        if (!Require(Mode->StartRun(),TEXT("real temporary run did not start"))) return;
        PC->OnRunReady();
        if (bRunningBladeR03)
        {
            const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 允许正式刀伤害配置，但此夹具仅真实安全区，不授予Combat凭据。
            if (!Require(State && State->Phase==EDemoPhase::Hub,TEXT("formal blade movement recording must remain Hub without damage authority"))) return;
        }
        const float FloorZ=20000.f; // 远离正式关卡的临时顶面；平台8,000cm容纳完整前进/横移。
        AStaticMeshActor* Platform=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,FloorZ-10.f),FRotator::ZeroRotator); // World持有，恢复时销毁。
        if (!Require(Platform!=nullptr,TEXT("temporary platform spawn failed"))) return;
        RunningPlatform=Platform;
        Platform->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Platform->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Platform->SetActorScale3D(FVector(80,80,.2));
        Move->StopMovementImmediately(); Player->ConsumeMovementInputVector(); Move->SetMovementMode(MOVE_Walking);
        Player->SetActorLocation(FVector(0,0,FloorZ+Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
        PC->SetControlRotation(FRotator::ZeroRotator); Player->SetActorRotation(FRotator::ZeroRotator);
        if (!Require(Melee->SetEquipped(true),TEXT("real equip rejected"))) return;
        if (bAcceptedLeft) { Melee->RequestAttack(1); if (!Require(!Melee->IsAttacking(),TEXT("removed right sweep input rejected"))) return; }
        Serial=Melee->GetAttackSerial(); // 初始攻击序号只用于确认恰好四次真实攻击，不注入序号。
        Camera=GetWorld()->SpawnActor<ACameraActor>();
        if (!Require(Camera.IsValid(),TEXT("observer camera spawn failed"))) return;
        Camera->GetCameraComponent()->SetFieldOfView(bFrontReview?DemoMeleeRunningRecording::FrontReviewFov:55.f); // 只改变显式正面TP观察机；原背面和真实FP相机/FOV/手臂偏移不改。
        ADirectionalLight* Fill=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-35,140,0)); // 中性补光，不修改材质或动画。
        if (!Require(Fill!=nullptr,TEXT("temporary fill spawn failed"))) return;
        RunningFill=Fill; Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable); Fill->GetLightComponent()->SetIntensity(3.f);
        AddTickPrerequisiteComponent(Melee); // 本Actor在真实近战FP求值之后采样，而输入留给下一次Movement更新。
        TrialRecordingDir=FPaths::ProjectSavedDir()/(bClearanceCandidateR07?TEXT("MeleeAcceptance/BladeClearanceR07"):bTorsoCandidateR06?TEXT("MeleeAcceptance/BladeTorsoR06"):bTorsoCandidateR05?TEXT("MeleeAcceptance/BladeTorsoR05"):bRunningBladePostureR04?TEXT("MeleeAcceptance/BladePostureR04"):bRunningBladeR03?TEXT("MeleeAcceptance/LongGripBladeR03"):TEXT("MeleeAcceptance/RunningAttack20261029"))/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")); // 候选修订与正式证据隔离，不能覆盖历史背面/前视图。
        if (bFrontReview) TrialRecordingDir+=TEXT("_Front"); // 即使同秒启动也不覆盖旧背面记录，验证器仍可沿用原capture父目录与720帧协议。
        if (bBodyClearance) TrialRecordingDir+=bBodyClearanceLOD0?TEXT("_BodyGeometry_LOD0"):TEXT("_BodyGeometry_AutoLOD"); // 同秒两种LOD证据也隔离，不把固定LOD诊断冒充正常自动LOD。
        if (!Require(IFileManager::Get().MakeDirectory(*TrialRecordingDir,true),TEXT("evidence directory unavailable"))) return;
        TrialRecordingRows.Add(FString(TEXT("frame,game_seconds,view,view_frame,view_seconds,movement,attacking,serial,progress,grounded,fullbody_weight,speed,actual_fp_sequence,fp_pose_seconds,actual_tp_sequence,tp_pose_seconds,fp_blend_alpha,tp_blend_alpha,actor_x,actor_y,actor_z,control_yaw,camera_fov,max_core_local_scale_error"))+(bRunningBladeR03?TEXT(",montage_mode,montage_active,montage_position_seconds,montage_phase_error_seconds,lowerbody_step_degrees"):TEXT("")));
        if (bFrontReview) TrialRecordingRows.Last()+=TEXT(",front_review,view_camera_x,view_camera_y,view_camera_z,view_camera_pitch,view_camera_yaw,view_camera_roll"); // 仅新审片附加列，不移动任何既有CSV列；位置为World cm、角度为度。
        if (bBodyClearance) TrialRecordingRows.Last()+=TEXT(",body_clearance,body_predicted_lod,body_forced_lod,owner_predicted_lod,owner_forced_lod,body_required_bones,body_pp_required_bones,body_mesh,owner_mesh"); // 真正LOD/RequiredBones名字逐帧取值；predicted0为LOD0，forced1才是强制LOD0。
        RunningBoneRows.Add(TEXT("frame,mesh,bone,component_x,component_y,component_z,component_qx,component_qy,component_qz,component_qw,local_sx,local_sy,local_sz,reference_scale_error"));
        if (bRunningBladeR03) RunningTraceRows.Add(TEXT("frame,view,movement,speed,business_phase,evaluated_pose_phase,pose_business_time_delta_seconds,in_hit_window,base_error_cm,tip_error_cm,visible_base_x,visible_base_y,visible_base_z,visible_tip_x,visible_tip_y,visible_tip_z,baked_base_x,baked_base_y,baked_base_z,baked_tip_x,baked_tip_y,baked_tip_z"));
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RUNNING_RECORD_BEGIN definition=%s duration=%.6f fp_camera_unchanged=1 front=%d directory=%s"),*Definition->GetPathName(),Definition->Attacks[0].Duration,bFrontReview,*TrialRecordingDir);
        if (bFrontReview) UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RUNNING_FRONT_REVIEW_CONFIG offset_world_cm=(%.6f,%.6f,%.6f) target_actor_offset_cm=(%.6f,%.6f,%.6f) observer_fov=%.6f dense_tail_local_frames=280..320 fp_camera_unchanged=1"),DemoMeleeRunningRecording::FrontReviewOffset.X,DemoMeleeRunningRecording::FrontReviewOffset.Y,DemoMeleeRunningRecording::FrontReviewOffset.Z,DemoMeleeRunningRecording::FrontReviewTargetOffset.X,DemoMeleeRunningRecording::FrontReviewTargetOffset.Y,DemoMeleeRunningRecording::FrontReviewTargetOffset.Z,DemoMeleeRunningRecording::FrontReviewFov);
        if (bBodyClearance) UE_LOG(LogFPSDemo,Display,TEXT("MELEE_BODY_GEOMETRY_RECORD_BEGIN full_final_frames=720 bones=161 body_lod0_explicit=%d normal_lod_policy_unchanged=%d"),bBodyClearanceLOD0,!bBodyClearanceLOD0);
        Step=1; NextTime=Now+.8; return;
    }
    UDemoPlayerAnimInstance* TP=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 包括继承该类的实际Lyra主图。
    if (!Require(TP!=nullptr,TEXT("actual Lyra/player animation instance unavailable"))) return;
    if (bRunningBladeR03)
    {
        const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 整个真实录制/中断测试都留在Hub，不能临时打开Combat来允许动画。
        if (!Require(State && State->Phase==EDemoPhase::Hub && TP->bUseMeleeUpperBodyMontage,TEXT("formal blade must use configured upper-body Montage in Hub"))) return;
    }
    const int32 View=TrialRecordingFrame/360; // 0真实FP，1跟随TP；每段各自重置起点并在日志明确记录。
    if (Step==1)
    {
        if (!Require(!Melee->IsAttacking(),TEXT("previous full clip not complete before view reset"))) return;
        Move->StopMovementImmediately(); Player->ConsumeMovementInputVector();
        Player->SetActorLocation(FVector(0,0,20000.f+Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
        Player->SetActorRotation(FRotator::ZeroRotator); PC->SetControlRotation(FRotator::ZeroRotator); Move->SetMovementMode(MOVE_Walking);
        PC->SetViewTarget(View==0?static_cast<AActor*>(Player):static_cast<AActor*>(Camera.Get()));
        bRunningHasLowerBodyFrame=false; RunningPreviousLowerBody.Reset(); // 视图重置后的第一帧只建立骨姿基线，不把传送当步态运动。
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RUNNING_VIEW_RESET view=%d recording_frame=%d actor_start_reset=1 original_speed=1"),View,TrialRecordingFrame);
        Step=2; NextTime=Now+.4; return;
    }
    if (Step==2)
    {
        const int32 LocalFrame=TrialRecordingFrame%360; // 固定60Hz，事件帧对应0.8/3.6秒；源动画本身不重采样。
        const double ViewSeconds=LocalFrame/60.; // 影片标注时钟，与输入真实World计时并列。
        const bool bForward=LocalFrame>=30 && LocalFrame<210,bSide=LocalFrame>=210; // 0.5..3.5秒前进，3.5..6秒横移。
        if (bForward) Player->AddMovementInput(FVector::ForwardVector,1.f);
        else if (bSide) Player->AddMovementInput(FVector::RightVector,1.f);
        if (LocalFrame==48 || LocalFrame==216)
        {
            Melee->RequestAttack(0); // 沿用真实输入/GAS，不手写动画时间、位置、命中或暂停Movement。
            if (!Require(Melee->IsAttacking(),TEXT("actual attack request rejected"))) return;
        }
        if (View==1)
        {
            Camera->SetActorLocation(Player->GetActorLocation()+(bFrontReview?DemoMeleeRunningRecording::FrontReviewOffset:FVector(-270,-380,90))); // 世界固定前/后审片方向只跟随位置，不写输入Yaw、Pawn朝向或任何生产骨骼。
            Camera->SetActorRotation((Player->GetActorLocation()+(bFrontReview?DemoMeleeRunningRecording::FrontReviewTargetOffset:FVector(0,0,15))-Camera->GetActorLocation()).Rotation());
            if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0.f); // 本帧观察相机缓存跟随，零dt不推进任何角色动画。
        }
        USkeletalMeshComponent* FPArms=nullptr; // 只找当前近战双臂，不误读仍存在的枪械Mesh1P。
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
        for (USkeletalMeshComponent* Mesh:Meshes) if (Mesh->GetFName()==TEXT("MeleeFirstPersonArms")) FPArms=Mesh;
        const UDemoMeleeAnimInstance* FP=FPArms?Cast<UDemoMeleeAnimInstance>(FPArms->GetAnimInstance()):nullptr; // 原Linked图实际输入序列与相位。
        const UDemoPlayerPresentationComponent* Presentation=Player->GetPlayerPresentation(); // 当前Pawn保活；该策略全程不可在夹具内切换。
        const bool bSharedBody=Presentation && Presentation->UsesFullBodyFirstPerson() && !Definition->bIndependentFirstPersonDisplay; // 近战可独立显示原动作，枪械仍共享；必须检查真正的可见FP组件。
        if (Definition->bIndependentFirstPersonDisplay && Presentation && Presentation->UsesFullBodyFirstPerson())
        {
            const USkeletalMeshComponent* OwnerBody=Presentation->GetFirstPersonBodyMesh(); // 本机仍显示身体/腿，但不允许Body再画第二套手臂。
            if (!Require(OwnerBody && OwnerBody->GetSkeletalMeshAsset()==Definition->OwnerBodyWithoutArmsMesh && OwnerBody->LeaderPoseComponent.Get()==Player->GetMesh()
                && FPArms && !FPArms->bHiddenInGame && Player->GetMesh1P()->bHiddenInGame,TEXT("independent melee owner partition or visibility mismatch"))) return;
            if (FParse::Param(FCommandLine::Get(),TEXT("DemoSOMAFPAdapt")))
            {
                const FBox RenderBox=FPArms->Bounds.GetBox(); // 真正组件渲染边界，不能只检查骨姿/Hidden标志后就宣称画面可见。
                if (!Require(RenderBox.IsInsideOrOn(FPArms->GetSocketLocation(TEXT("hand_r"))) && RenderBox.IsInsideOrOn(FPArms->GetSocketLocation(TEXT("hand_l"))),
                    TEXT("FP render bounds exclude actual palms; mesh may be frustum culled"))) return;
            }
        }
        if (bSharedBody)
        {
            const USkeletalMeshComponent* OwnerBody=Presentation->GetFirstPersonBodyMesh(); // OnlyOwnerSee渲染分区的骨姿唯一来源必须是Body。
            if (!Require(OwnerBody && OwnerBody->LeaderPoseComponent.Get()==Player->GetMesh() && OwnerBody->bOnlyOwnerSee
                && !OwnerBody->bOwnerNoSee && !OwnerBody->bHiddenInGame && Player->GetMesh1P()->bHiddenInGame && !FPArms,
                TEXT("shared body/hidden legacy arms contract failed"))) return;
            FPArms=Player->GetMesh(); // 记录真正驱动本机显示的骨姿；不能用隐藏旧FP组件充当可见动画证据。
        }
        if (!Require((bSharedBody || (FP && FP->Pose)) && TP->UpperBodySequence,TEXT("actual FP/TP sequence missing"))) return;
        if (!Require(Move->IsMovingOnGround() && TP->bGroundedForFeet,TEXT("recorded pawn left real Walking ground"))) return;
        if (!Require(FMath::Abs(TP->MeleeFullBodyWeight)<.0001f,TEXT("fullbody layer unexpectedly overrides running legs"))) return;
        if (!Require(Definition->GetPathName()==RunningExpectedDefinition,TEXT("candidate changed during recording"))) return;
        const float Speed=Player->GetVelocity().Size2D(); // 真实Movement速度cm/s，不以输入向量代替运动事实。
        if (Melee->IsAttacking() && Speed>100.f && (bForward || bSide)) ++RunningOverlapCounts[View*2+(bSide?1:0)];
        if ((LocalFrame==60 || LocalFrame==240) && !Require(Melee->IsAttacking() && Speed>100.f,TEXT("attack must overlap actual movement above 100 cm/s"))) return;
        float PoseSeconds=0.f,Progress=0.f; FName Action; // 只读业务相位；图缓存可能在请求当帧仍是上一帧，CSV保留真实值不伪同步。
        Melee->GetPose(false,PoseSeconds,Progress,Action);
        const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(TP); // 实际主实例，供CSV记录播放器而非仅业务Attack标志。
        if (!RunningPostureRows.IsEmpty())
        {
            static const int32 Selected[]={0,40,60,66,70,78,80,84,86,96,110,125,170,220,225,238,245,250,252,265,300,350}; // 准备/接触/随挥/回位与横移，固定同刻前后对照。
            bool bCapturePosture=bBodyClearance || (bFrontReview && LocalFrame>=280 && LocalFrame<=320); // 本体专项全720帧完整Final；普通正面仍仅尾段密集，不改变时钟或插补动画。
            for (const int32 Sample:Selected) if (LocalFrame==Sample) { bCapturePosture=true; break; } // 保留原稀疏证据，同帧只保存一次。
            if (bCapturePosture)
            {
                if (bBodyClearance && !Require(Player->GetMesh()->GetComponentSpaceTransforms().Num()==161,TEXT("body geometry requires actual full161-bone component snapshot"))) return;
                DemoMeleeRunningRecording::AppendPostureSnapshot(Player->GetMesh(),TP,TrialRecordingFrame,RunningPostureRows);
                if (bBodyClearance) ++RunningGeometryFinalFrames; // 仅真正写入完整Final之后递增，不用数组存在冒充覆盖。
            }
            if (bFrontReview && (LocalFrame==280 || LocalFrame==320)) UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RUNNING_FRONT_TAIL_SNAPSHOT view=%d local_frame=%d recording_frame=%d sample_window=280..320"),View,LocalFrame,TrialRecordingFrame); // 高密度窗口边界可检索，其余逐帧调用保留VeryVerbose埋点。
        }
        const bool bMontageActive=Native && Native->CurrentMeleeMontage && Native->Montage_IsActive(Native->CurrentMeleeMontage);
        const float MontagePosition=bMontageActive?Native->Montage_GetPosition(Native->CurrentMeleeMontage):0.f; // 实际播放器秒。
        const float MontageError=bMontageActive?FMath::Abs(MontagePosition-PoseSeconds):0.f; // 与同帧业务作者时刻差，首个Request帧允许缓存未更新。
        if (TP->bUseMeleeUpperBodyMontage && Melee->IsAttacking() && LocalFrame!=48 && LocalFrame!=216)
        {
            if (!Require(bMontageActive && MontageError<.04f,TEXT("upper-body Montage absent or desynchronized"))) return;
        }
        double LowerBodyStep=0.; // 本帧实际TP腿链局部旋转最大步长，只有BladeR03模式采样并断言覆盖。
        if (bRunningBladeR03)
        {
            static const FName LegNames[]={TEXT("thigh_l"),TEXT("thigh_r"),TEXT("calf_l"),TEXT("calf_r"),TEXT("foot_l"),TEXT("foot_r")}; // Pelvis另随bones.csv记录，腿链提供不会被上身动画冻结的动态证据。
            const TArray<FTransform> Local=Player->GetMesh()->GetBoneSpaceTransforms(); // 实际图本帧已求值骨姿；只读快照。
            TArray<FQuat> Current; Current.Reserve(UE_ARRAY_COUNT(LegNames)); // 不跨帧借用组件变换数组。
            for (int32 Leg=0;Leg<UE_ARRAY_COUNT(LegNames);++Leg)
            {
                const int32 Index=Player->GetMesh()->GetBoneIndex(LegNames[Leg]);
                if (!Require(Local.IsValidIndex(Index) && !Local[Index].ContainsNaN(),TEXT("actual locomotion leg chain missing/nonfinite"))) return;
                const FQuat Rotation=Local[Index].GetRotation(); Current.Add(Rotation);
                if (bRunningHasLowerBodyFrame && RunningPreviousLowerBody.IsValidIndex(Leg)) LowerBodyStep=FMath::Max(LowerBodyStep,FMath::RadiansToDegrees(Rotation.AngularDistance(RunningPreviousLowerBody[Leg])));
            }
            RunningPreviousLowerBody=MoveTemp(Current); bRunningHasLowerBodyFrame=true;
            if (Melee->IsAttacking() && Speed>100.f && (bForward || bSide))
            {
                const int32 Bucket=View*2+(bSide?1:0); // 四种实际视图/方向分别通过，不能以一个方向掩盖另一个方向静腿。
                if (LowerBodyStep>.05) ++RunningLowerBodyMotionCounts[Bucket];
                RunningLowerBodyPeakStepDegrees[Bucket]=FMath::Max(RunningLowerBodyPeakStepDegrees[Bucket],LowerBodyStep);
            }
            if (Melee->IsAttacking() && bMontageActive && TP->MeleeAttackSequence && LocalFrame!=48 && LocalFrame!=216)
            {
                const float EvaluatedPhase=TP->MeleeAttackTime/TP->MeleeAttackSequence->GetPlayLength(); // 当前实际上身求值时刻，不能拿后一帧业务phase误判高速刀尖。
                FVector VisibleBase,VisibleTip,BakedBase,BakedTip; // 统一World cm，均按当前Actor与BaseAim旋转，不混组件/胶囊空间。
                if (!Require(DemoMeleeRunningRecording::TraceAlignment(Player,Definition,EvaluatedPhase,VisibleBase,VisibleTip,BakedBase,BakedTip),TEXT("blade visual/baked alignment diagnostic nonfinite or missing"))) return;
                const double BaseError=FVector::Distance(VisibleBase,BakedBase),TipError=FVector::Distance(VisibleTip,BakedTip);
                const bool bHitWindow=EvaluatedPhase>=Definition->Attacks[0].HitWindow.X && EvaluatedPhase<=Definition->Attacks[0].HitWindow.Y;
                RunningTraceMaxBaseError=FMath::Max(RunningTraceMaxBaseError,BaseError); RunningTraceMaxTipError=FMath::Max(RunningTraceMaxTipError,TipError);
                if (bHitWindow) { RunningHitTraceMaxBaseError=FMath::Max(RunningHitTraceMaxBaseError,BaseError); RunningHitTraceMaxTipError=FMath::Max(RunningHitTraceMaxTipError,TipError); }
                RunningTraceRows.Add(FString::Printf(TEXT("%d,%s,%s,%.8f,%.9f,%.9f,%.9f,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f"),TrialRecordingFrame,View==0?TEXT("FP"):TEXT("TP"),bSide?TEXT("side"):bForward?TEXT("forward"):TEXT("idle"),Speed,Progress,EvaluatedPhase,TP->MeleeAttackTime-PoseSeconds,bHitWindow,BaseError,TipError,VisibleBase.X,VisibleBase.Y,VisibleBase.Z,VisibleTip.X,VisibleTip.Y,VisibleTip.Z,BakedBase.X,BakedBase.Y,BakedBase.Z,BakedTip.X,BakedTip.Y,BakedTip.Z));
            }
        }
        double ScaleError=0.; // 核心骨局部缩放对参考值的差，不以此代替皮肤/接触审片。
        if (!Require(DemoMeleeRunningRecording::AppendBones(FPArms,TEXT("FP"),TrialRecordingFrame,RunningBoneRows,ScaleError,bRunningBladeR03 && bSharedBody) && DemoMeleeRunningRecording::AppendBones(Player->GetMesh(),TEXT("TP"),TrialRecordingFrame,RunningBoneRows,ScaleError,bRunningBladeR03),TEXT("nonfinite or missing core bone transforms"))) return;
        if (!Require(ScaleError<.002,TEXT("unexpected ordinary bone local scale"))) return;
        const FVector Position=Player->GetActorLocation(); // World cm，用于证明摄像机跟随与移动路线而非原地假跑。
        const float Fov=PC->PlayerCameraManager?PC->PlayerCameraManager->GetFOVAngle():0.f; // 实际当前镜头FOV；首视图不覆盖。
        const UAnimSequence* VisibleFP=bSharedBody?(TP->MeleeAttackSequence?TP->MeleeAttackSequence.Get():TP->UpperBodySequence.Get()):FP->Pose.Get(); // 共享Body在Slot启用时记录实际攻击，不能误记背景Idle。
        const UAnimSequence* VisibleTP=TP->MeleeAttackSequence?TP->MeleeAttackSequence.Get():TP->UpperBodySequence.Get(); // Montage启用后Evaluator仅待机，必须记录实际攻击源，不能把背景当播放器。
        const float VisibleTPTime=TP->MeleeAttackSequence?TP->MeleeAttackTime:TP->UpperBodyTime;
        const float VisibleFPTime=bSharedBody?(bRunningBladeR03?VisibleTPTime:TP->UpperBodyTime):FP->PoseTime; // 新刀共享Body按实际Montage时刻记录，不能误记固定背景Idle=0；旧CSV行为保留。
        FString Row=FString::Printf(TEXT("%d,%.8f,%s,%d,%.6f,%s,%d,%u,%.8f,%d,%.8f,%.6f,%s,%.8f,%s,%.8f,%.8f,%.8f,%.6f,%.6f,%.6f,%.6f,%.6f,%.9f"),TrialRecordingFrame,Now,View==0?TEXT("FP"):TEXT("TP"),LocalFrame,ViewSeconds,bSide?TEXT("side"):bForward?TEXT("forward"):TEXT("idle"),Melee->IsAttacking(),Melee->GetAttackSerial(),Progress,Move->IsMovingOnGround(),TP->MeleeFullBodyWeight,Speed,*VisibleFP->GetPathName(),VisibleFPTime,*VisibleTP->GetPathName(),VisibleTPTime,bSharedBody?TP->UpperBodyBlendAlpha:FP->BlendAlpha,TP->UpperBodyBlendAlpha,Position.X,Position.Y,Position.Z,PC->GetControlRotation().Yaw,Fov,ScaleError);
        if (bRunningBladeR03) Row+=FString::Printf(TEXT(",%d,%d,%.9f,%.9f,%.9f"),TP->bUseMeleeUpperBodyMontage,bMontageActive,MontagePosition,MontageError,LowerBodyStep);
        if (bFrontReview)
        {
            const FVector ViewCameraPosition=PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraLocation():FVector::ZeroVector; // 当前实际渲染视点World cm，FP保持原机位而非记录观察机的隐藏位置。
            const FRotator ViewCameraRotation=PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraRotation():FRotator::ZeroRotator; // 当前实际Pitch/Yaw/Roll度，结合既有camera_fov复核构图。
            Row+=FString::Printf(TEXT(",1,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f"),ViewCameraPosition.X,ViewCameraPosition.Y,ViewCameraPosition.Z,ViewCameraRotation.Pitch,ViewCameraRotation.Yaw,ViewCameraRotation.Roll);
            if (LocalFrame==0) UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RUNNING_FRONT_CAMERA view=%d front=%d location_world_cm=(%.9f,%.9f,%.9f) rotation_degrees=(%.9f,%.9f,%.9f) fov=%.9f"),View,View==1,ViewCameraPosition.X,ViewCameraPosition.Y,ViewCameraPosition.Z,ViewCameraRotation.Pitch,ViewCameraRotation.Yaw,ViewCameraRotation.Roll,Fov); // 两视图各记录一次精确实际相机；所有帧数值另存CSV。
        }
        if (bBodyClearance)
        {
            const USkeletalMeshComponent* BodyMesh=Player->GetMesh(); // 真实TP/Leader组件，本帧求值已结束，只借用到CSV生成结束。
            const USkeletalMeshComponent* OwnerMesh=Presentation?Presentation->GetFirstPersonBodyMesh():nullptr; // 实际Owner分区可能为空，明确记-1/none而不假造一致LOD。
            Row+=FString::Printf(TEXT(",1,%d,%d,%d,%d,%s,%s,%s,%s"),BodyMesh->GetPredictedLODLevel(),BodyMesh->GetForcedLOD(),OwnerMesh?OwnerMesh->GetPredictedLODLevel():-1,OwnerMesh?OwnerMesh->GetForcedLOD():-1,*DemoMeleeRunningRecording::RequiredBoneNames(TP),*DemoMeleeRunningRecording::RequiredBoneNames(BodyMesh->GetPostProcessInstance()),*GetPathNameSafe(BodyMesh->GetSkeletalMeshAsset()),OwnerMesh?*GetPathNameSafe(OwnerMesh->GetSkeletalMeshAsset()):TEXT("none"));
        }
        TrialRecordingRows.Add(Row);
        FScreenshotRequest::RequestScreenshot(TrialRecordingDir/FString::Printf(TEXT("Frame-%05d.png"),TrialRecordingFrame),false,false); // 每帧真实RHI像素，输出绝不称作者渲染。
        ++TrialRecordingFrame;
        if (TrialRecordingFrame>=720) { Step=3; NextTime=Now+.2; }
        else if (TrialRecordingFrame%360==0) { Step=1; NextTime=Now+.2; }
        return;
    }
    if (Step==3)
    {
        if (bBodyClearance && !Require(RunningGeometryFinalFrames==720,TEXT("body geometry did not sample all720 fullFinal poses"))) return;
        if (!Require(!Melee->IsAttacking() && Melee->GetAttackSerial()==Serial+4,TEXT("expected four uninterrupted source attacks then idle"))) return;
        for (const int32 Count:RunningOverlapCounts) if (!Require(Count>=60,TEXT("insufficient running and attacking overlap in one view/direction"))) return;
        RunningCompleteSourceAttacks=4; // 只在完整记录/真实四招/回位均成立后发布成功数量，后续探针另算。
        if (bRunningBladeR03)
        {
            for (int32 Bucket=0;Bucket<4;++Bucket)
                if (!Require(RunningLowerBodyMotionCounts[Bucket]>=30 && RunningLowerBodyPeakStepDegrees[Bucket]>.25,TEXT("actual leg rotations must continue through moving upper-body attacks"))) return;
            UE_LOG(LogFPSDemo,Display,TEXT("MELEE_BLADE_TRACE_ALIGNMENT_RESULT base_max_cm=%.6f tip_max_cm=%.6f hit_base_max_cm=%.6f hit_tip_max_cm=%.6f diagnostic_only=1"),RunningTraceMaxBaseError,RunningTraceMaxTipError,RunningHitTraceMaxBaseError,RunningHitTraceMaxTipError);
            Move->StopMovementImmediately(); Player->ConsumeMovementInputVector(); // 四段完整跑动已经结束；中断探针不再给残留移动输入。
            Melee->RequestAttack(0);
            if (!Require(Melee->IsAttacking() && Melee->GetAttackSerial()==Serial+5,TEXT("explicit cancel probe attack rejected"))) return;
            Step=4; NextTime=Now+.20; return;
        }
        FinishRunningRecording(true,TEXT("real movement/attack assertions passed; visual review still required"));
        return;
    }
    if (bRunningBladeR03 && (Step==4 || Step==8))
    {
        const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(TP); // 先让真实动画至少运行0.20s，再请求中断前RHI图。
        if (!Require(Melee->IsAttacking() && Native && Native->CurrentMeleeMontage && Native->Montage_IsActive(Native->CurrentMeleeMontage),TEXT("interruption probe requires an actually active upper-body Montage"))) return;
        const TCHAR* Name=Step==4?TEXT("Interrupt-BeforeCancel.png"):TEXT("Interrupt-BeforeUnequip.png");
        FScreenshotRequest::RequestScreenshot(TrialRecordingDir/Name,false,false); // 下一游戏Tick才改变装备/取消，避免同帧截图已经是取消后。
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_BLADE_INTERRUPT_PROBE stage=%s serial=%u montage_active=1"),Step==4?TEXT("before_cancel"):TEXT("before_unequip"),Melee->GetAttackSerial());
        ++Step; NextTime=Now+1./60.; return;
    }
    if (bRunningBladeR03 && Step==5)
    {
        Melee->CancelAttack(); // 与真实菜单/输入取消相同入口；不直接停止或清空动画实例变量。
        if (!Require(!Melee->IsAttacking(),TEXT("real cancel did not clear attack"))) return;
        Step=6; NextTime=Now+.20; return; // 原动画out=.10，额外一倍等待检查真实Slot已停止。
    }
    if (bRunningBladeR03 && Step==6)
    {
        const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(TP);
        if (!Require(!Melee->IsAttacking() && !TP->MeleeAttackSequence && Native && (!Native->CurrentMeleeMontage || !Native->Montage_IsActive(Native->CurrentMeleeMontage)) && FMath::Abs(TP->MeleeFullBodyWeight)<.0001f,TEXT("cancel left stale Montage/attack/fullbody output"))) return;
        bRunningCancelVerified=true;
        FScreenshotRequest::RequestScreenshot(TrialRecordingDir/TEXT("Interrupt-AfterCancel.png"),false,false);
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_BLADE_INTERRUPT_PROBE stage=after_cancel attack=0 montage_active=0 attack_sequence=none"));
        Step=7; NextTime=Now+Definition->Attacks[0].Duration+.1; return; // 保留真实取消冷却，不通过改NextAttackTime抢先重打。
    }
    if (bRunningBladeR03 && Step==7)
    {
        Melee->RequestAttack(0); // 第二项真实探针：在攻击播放中卸下近战，仍通过生产装备入口取消。
        if (!Require(Melee->IsAttacking() && Melee->GetAttackSerial()==Serial+6,TEXT("unequip probe attack rejected after preserved cooldown"))) return;
        Step=8; NextTime=Now+.20; return;
    }
    if (bRunningBladeR03 && Step==9)
    {
        if (!Require(Melee->SetEquipped(false) && !Melee->IsEquipped() && !Melee->IsAttacking(),TEXT("real unequip did not cancel attack"))) return;
        Step=10; NextTime=Now+.20; return;
    }
    if (bRunningBladeR03 && Step==10)
    {
        const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(TP);
        if (!Require(!Melee->IsEquipped() && !Melee->IsAttacking() && !TP->MeleeAttackSequence && Native && (!Native->CurrentMeleeMontage || !Native->Montage_IsActive(Native->CurrentMeleeMontage)) && FMath::Abs(TP->MeleeFullBodyWeight)<.0001f,TEXT("unequip left stale Montage/attack/fullbody output"))) return;
        bRunningUnequipVerified=true;
        FScreenshotRequest::RequestScreenshot(TrialRecordingDir/TEXT("Interrupt-AfterUnequip.png"),false,false);
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_BLADE_INTERRUPT_PROBE stage=after_unequip attack=0 equipped=0 montage_active=0 attack_sequence=none"));
        Step=11; NextTime=Now+.05; return; // 保留一帧以上给真实RHI写盘再退出。
    }
    if (bRunningBladeR03 && Step==11)
    {
        if (!Require(bRunningCancelVerified && bRunningUnequipVerified && Melee->GetAttackSerial()==Serial+6,TEXT("formal blade interruption probes incomplete or extra attack started"))) return;
        FinishRunningRecording(true,TEXT("four full moving attacks plus cancel/unequip cleared actual Montage; trace differences diagnostic only; visual review still required"));
    }
}
#endif
