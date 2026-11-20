// 职责：Editor隔离World自然复现随机待机与枪手不同步，采集真实Linked Layer状态、锚点/最终Aim前后和可见结果。复用正式夹具，不改状态机、动画、配置或Gameplay。
#include "Tests/DemoPlayerAnimationTest.h"
#include "Animation/DemoLyraAnimInstance.h"
#include "Animation/DemoPlayerAnimProxy.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Camera/CameraActor.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Engine/SkeletalMesh.h" // 从当前网格真实参考骨长独立核对最终Pose，不能拿节点期望值代替结果。
#include "Misc/App.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h" // 中断测试使用真实开火/换弹入口，不调用结算回调或伪造Ammo。

namespace
{
    /** Transform是当前阶段组件空间值；写位置cm与单位四元数，不跨帧借用骨缓冲。 */
    TSharedPtr<FJsonValue> IdleTransform(const FTransform& Transform)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        const FVector P=Transform.GetLocation(); const FQuat Q=Transform.GetRotation(); // 值副本，便于离线比较平移与完整旋转。
        return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z),MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});
    }
    /** Poses按胸/右腕/左腕/枪骨，转成各自相对胸部值，排除正常呼吸/躯干位移。 */
    TSharedPtr<FJsonObject> IdlePose(const FTransform* Poses)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        TSharedPtr<FJsonObject> Data=MakeShared<FJsonObject>(); // 本帧数值对象，不保留Pose数组指针。
        Data->SetField(TEXT("chest"),IdleTransform(Poses[0])); Data->SetField(TEXT("right"),IdleTransform(Poses[1].GetRelativeTransform(Poses[0])));
        Data->SetField(TEXT("left"),IdleTransform(Poses[2].GetRelativeTransform(Poses[0]))); Data->SetField(TEXT("gun"),IdleTransform(Poses[3].GetRelativeTransform(Poses[0])));
        return Data;
    }
}

void ADemoPlayerAnimationTest::TickIdleBreakAudit(float Now)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (Stage<0)
    {
        if (!InitializeFixture()) return;
        int32 WeaponIndex=1; FParse::Value(FCommandLine::Get(),TEXT("DemoIdleBreakWeapon="),WeaponIndex); // 0手枪/1步枪/2霰弹/3狙击，未指定沿原步枪诊断。
        if (!Check(WeaponIndex>=0 && WeaponIndex<4,TEXT("idle audit weapon index valid")) || !EquipWeapon(WeaponIndex)) return;
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics);
        Player->SetActorRotation(FRotator::ZeroRotator); CastChecked<APlayerController>(Player->GetController())->SetControlRotation(FRotator::ZeroRotator);
        Stage=0; StageStart=Now; StageName=Weapon->GetWeaponId().ToString()+TEXT("_natural_idle"); NextSample=NextCapture=Now+.1f;
        Report->SetStringField(TEXT("scope"),TEXT("Actual weapon natural idle plus optional real input interruption; never writes state machine flags/timers or animation assets"));
    }
    FString Interrupt=TEXT("Natural"); FParse::Value(FCommandLine::Get(),TEXT("DemoIdleBreakInterrupt="),Interrupt); // 有限场景，拒绝任意路径/操作名。
    if (Interrupt!=TEXT("Natural") && Interrupt!=TEXT("Look") && Interrupt!=TEXT("Move") && Interrupt!=TEXT("Fire") && Interrupt!=TEXT("Reload")) { Check(false,TEXT("idle interruption mode invalid")); return; } // 只在拒绝时输出，合法路径不逐帧刷Display。
    UDemoLyraAnimInstance* CurrentMain=Cast<UDemoLyraAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 只通过真实已发布动作权重选择中断时机。
    if (Interrupt==TEXT("Reload") && !bIdleReloadPrepared && Now-StageStart>2.f)
    { Player->GetWeaponComponent()->StartFire(); Player->GetWeaponComponent()->StopFire(); bIdleReloadPrepared=true; } // 先实际扣一发，确保之后Reload满足弹匣门禁。
    if (Interrupt!=TEXT("Natural") && !bIdleInterruptSent && bIdleBreakObserved && Now-StageStart>8.f && CurrentMain && CurrentMain->IdleBreakMotionWeight>.8f)
    {
        bIdleInterruptSent=true; IdleInterruptTime=Now;
        if (Interrupt==TEXT("Look")) CastChecked<APlayerController>(Player->GetController())->SetControlRotation(FRotator(0,25,0));
        if (Interrupt==TEXT("Fire")) { Player->GetWeaponComponent()->StartFire(); Player->GetWeaponComponent()->StopFire(); }
        if (Interrupt==TEXT("Reload")) Player->GetWeaponComponent()->RequestReload();
        UE_LOG(LogFPSDemo,Display,TEXT("IDLE_FIX_REAL_INTERRUPT mode=%s seconds=%.3f"),*Interrupt,Now-StageStart);
    }
    if (Interrupt==TEXT("Move") && bIdleInterruptSent && Now-IdleInterruptTime<1.5f) Player->AddMovementInput(FVector::ForwardVector,1.f); // 保持真实输入以观察完整退出与步态恢复。
    const FVector Target=Player->GetActorLocation()+FVector(0,0,45); // 外部观察完整双臂与枪，保持原Owner可见性策略。
    const FVector View=Target+FVector(185,-230,55);
    Camera->SetActorLocationAndRotation(View,(Target-View).Rotation());
    CastChecked<APlayerController>(Player->GetController())->SetViewTarget(bFirstPersonCapture?static_cast<AActor*>(Player.Get()):static_cast<AActor*>(Camera.Get()));
    if (Now>=NextSample)
    {
        UDemoLyraAnimInstance* Main=Cast<UDemoLyraAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 主实例游戏线程借用，下面代理访问会等待任务完成。
        if (!Check(Main!=nullptr,TEXT("idle audit actual Lyra main instance ready"))) return;
        const FDemoIdleBreakPoseAudit Audit=Main->ReadIdleBreakAudit(); // 封装接口等待求值屏障后返回副本，绝不写回节点/姿态。
        TSharedPtr<FJsonObject> Frame=MakeShared<FJsonObject>(); // 当前帧纯值证据。
        Frame->SetNumberField(TEXT("seconds"),Now-StageStart); Frame->SetStringField(TEXT("upper_action"),Main->UpperBodyAction.ToString());
        Frame->SetNumberField(TEXT("alignment_weight"),Main->AimAlignmentWeight); Frame->SetNumberField(TEXT("overlay_weight"),Main->ActionOverlayWeight);
        Frame->SetNumberField(TEXT("native_idle_weight"),Main->NativeIdleBreakWeight); Frame->SetNumberField(TEXT("idle_intent_weight"),Main->IdleBreakMotionWeight); Frame->SetNumberField(TEXT("idle_applied"),Audit.IdleApplied);
        Frame->SetNumberField(TEXT("normal_aim_left_reach_error_cm"),Audit.ReachErrors[0]); Frame->SetNumberField(TEXT("normal_aim_right_reach_error_cm"),Audit.ReachErrors[1]); // 待机混入之前的正常瞄准IK误差；待机是局部Pose重建，没有独立腕位置目标。
        const FReferenceSkeleton& Skeleton=Player->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton(); // 与当前可见身体同网格，只读本帧借用。
        double LengthError=0.; // 六段肩/肘/腕实际长度相对参考值的最大误差cm，不允许以伸长换取接触。
        for (const FName Bone:{FName(TEXT("upperarm_l")),FName(TEXT("lowerarm_l")),FName(TEXT("hand_l")),FName(TEXT("upperarm_r")),FName(TEXT("lowerarm_r")),FName(TEXT("hand_r"))})
        {
            const int32 Index=Skeleton.FindBoneIndex(Bone),Parent=Skeleton.GetParentIndex(Index); // 固定契约骨已由正式节点验证，父关节用于独立长度测量。
            const double Actual=FVector::Dist(Player->GetMesh()->GetSocketLocation(Bone),Player->GetMesh()->GetSocketLocation(Skeleton.GetBoneName(Parent))); // World cm，正式角色Scale=1。
            LengthError=FMath::Max(LengthError,FMath::Abs(Actual-Skeleton.GetRefBonePose()[Index].GetTranslation().Size()));
        }
        Frame->SetNumberField(TEXT("arm_length_error_cm"),LengthError);
        Frame->SetNumberField(TEXT("speed"),Player->GetVelocity().Size2D()); Frame->SetNumberField(TEXT("control_yaw"),Player->GetBaseAimRotation().Yaw);
        Frame->SetBoolField(TEXT("interrupt_sent"),bIdleInterruptSent); Frame->SetNumberField(TEXT("after_interrupt_seconds"),bIdleInterruptSent?Now-IdleInterruptTime:-1.);
        Frame->SetNumberField(TEXT("magazine"),Weapon->GetAmmo()); Frame->SetBoolField(TEXT("reloading"),Weapon->IsReloading()); // 正式弹药接口，只读实际弹匣。
        Frame->SetNumberField(TEXT("anchor_evaluations"),Audit.AnchorCount); Frame->SetNumberField(TEXT("aim_evaluations"),Audit.AimCount);
        if (Audit.AnchorCount) { Frame->SetObjectField(TEXT("base"),IdlePose(Audit.NativeBase)); Frame->SetObjectField(TEXT("upper"),IdlePose(Audit.UpperAction)); Frame->SetField(TEXT("mapped_gun"),IdleTransform(Audit.MappedGun.GetRelativeTransform(Audit.NativeBase[0]))); }
        if (Audit.AimCount) { Frame->SetObjectField(TEXT("aim_in"),IdlePose(Audit.BeforeAim)); Frame->SetObjectField(TEXT("aim_out"),IdlePose(Audit.AfterAim)); }
        FTransform Final[4]; // 本帧真正可见身体骨姿，供识别最终Aim之后是否还有覆盖。
        const FName Names[4]={TEXT("spine_05"),TEXT("hand_r"),TEXT("hand_l"),TEXT("ik_hand_gun")}; // 与阶段快照相同的稳定顺序。
        for (int32 I=0;I<4;++I) Final[I]=Player->GetMesh()->GetSocketTransform(Names[I],RTS_Component);
        Frame->SetObjectField(TEXT("final"),IdlePose(Final));
        const USkeletalMeshComponent* Gun=FindPresentationWeapon(); // Presentation已在测试Tick之前执行。
        if (Gun) Frame->SetField(TEXT("visible_gun"),IdleTransform(Gun->GetComponentTransform().GetRelativeTransform(Player->GetMesh()->GetComponentTransform()).GetRelativeTransform(Final[0])));
        TArray<TSharedPtr<FJsonValue>> ActivePlayers; // 当前链接实例的实际状态与播放器，不以名称猜测正在播放，也不遮蔽Actor的Layers成员。
        const TSubclassOf<UAnimInstance>* Configured=Main->WeaponLayers.Find(Main->GetUpperWeaponId()); // 正式当前稳定ID对应的实际类。
        UAnimInstance* Layer=Main->GetLinkedAnimLayerInstanceByClass(Configured?*Configured:Main->DefaultLayer); // 不重新Link，也不修改子层变量。
        bool bActiveBreak=false; // 每帧重新判断，不把曾经活跃的缓存节点当作当前随机待机。
        if (Layer)
        {
            Frame->SetStringField(TEXT("layer_class"),Layer->GetClass()->GetPathName());
            const int32 Machine=Layer->GetStateMachineIndex(TEXT("IdleSM")); // 实际原生子状态机，缺失时明确None。
            const FString State=Machine==INDEX_NONE?TEXT("None"):Layer->GetCurrentStateName(Machine).ToString();
            Frame->SetStringField(TEXT("idle_state"),State); bActiveBreak=State.Contains(TEXT("Break"));
            for (TFieldIterator<FStructProperty> It(Layer->GetClass());It;++It)
            {
                const FStructProperty* Property=*It; // 类拥有的只读节点描述。
                if (!Property->Struct->IsChildOf(FAnimNode_AssetPlayerBase::StaticStruct())) continue;
                const FAnimNode_AssetPlayerBase* Node=Property->ContainerPtrToValuePtr<FAnimNode_AssetPlayerBase>(Layer); // 任务结束后的实际播放器缓存。
                const UAnimationAsset* Asset=Node->GetAnimAsset(); // 资产不复制/重存，只输出路径/时间/权重。
                if (!Asset || Node->GetCachedBlendWeight()<.001f) continue;
                TSharedPtr<FJsonObject> Row=MakeShared<FJsonObject>(); // 当前节点数值。
                Row->SetStringField(TEXT("asset"),Asset->GetPathName()); Row->SetNumberField(TEXT("time"),Node->GetAccumulatedTime()); Row->SetNumberField(TEXT("weight"),Node->GetCachedBlendWeight()); ActivePlayers.Add(MakeShared<FJsonValueObject>(Row));
            }
        }
        Frame->SetBoolField(TEXT("idle_break_state"),bActiveBreak); Frame->SetArrayField(TEXT("players"),ActivePlayers); Frames.Add(MakeShared<FJsonValueObject>(Frame));
        if (bActiveBreak && !bIdleBreakObserved) { bIdleBreakObserved=true; UE_LOG(LogFPSDemo,Display,TEXT("IDLE_BREAK_NATURAL_OBSERVED seconds=%.3f"),Now-StageStart); }
        NextSample=Now+.1f;
    }
    if (bCapture && FApp::CanEverRender() && Now>=NextCapture && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Directory=GetReviewRoot()/TEXT("Images"); // 诊断目录，不覆盖正式动画/准星验收。
        IFileManager::Get().MakeDirectory(*Directory,true); FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Idle-%05d.png"),ScreenshotIndex++),false,false); NextCapture=Now+.5f;
    }
    if (Now-StageStart>=35.f) { UE_LOG(LogFPSDemo,Display,TEXT("IDLE_BREAK_AUDIT_CAPTURE_COMPLETE observed_break=%d samples=%d"),bIdleBreakObserved,Frames.Num()); SaveAndExit(true); } // 完成只代表自然采集；没有观察到目标时必须在分析中说明未复现。
#endif
}
