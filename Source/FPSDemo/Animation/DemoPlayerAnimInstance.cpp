// 职责：全身移动/上身动作的游戏线程缓存，供当前ABP_PlayerManny及旧ABP_PlayerFullBody读取；枪械/近战/Locomotion DA提供序列和逐武器上身Montage策略，图处理分层、步相及地面适配。
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoPlayerAnimProxy.h" // NativeUpdate末发布本帧纯值；避免节点PreUpdate早于本函数导致一帧延迟。
#include "Animation/DemoLyraAnimInstance.h" // optional后坐仅由具备专用Slot生命周期的Body主实例接管，旧图保留Fire兼容路径。
#include "Weapons/Melee/DemoMeleeComponent.h" // 近战按当前定义提供上身片段，不新增每把武器分支。
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/DemoLocomotionSet.h" // 配置落地窗口，FP/TP读取同一实际物理边沿。
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h" // 后坐源时长与机械共用同一服务器phase，不改Montage资产。
#include "Engine/SkeletalMesh.h" // 资源预检读取当前真实身体Skeleton。
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Components/SkeletalMeshComponent.h" // 游戏线程把真实碰撞地面平面转换至当前身体组件空间。
#include "Engine/World.h" // 服务器同步时钟仅在游戏线程读取。
#include "Debug/DemoLog.h"

void UDemoPlayerAnimInstance::NativeInitializeAnimation()
{
    DEMO_LOG_CALL(); Super::NativeInitializeAnimation();
    Character = Cast<ADemoCharacter>(GetOwningActor());
    ResetReloadAimRecovery(TEXT("animation instance initialized")); // 重建同枪Avatar也不能携带旧实例的释放时钟。
    bMuzzleFocusActive=false; MuzzleFocusPoint=MuzzleFocusOffset=FVector::ZeroVector; MuzzleFocusDistance=0.f; MuzzleFocusWeapon=NAME_None; // Avatar/图重新初始化时清空焦点与距离历史，禁止同ID新枪复用旧World快照。
    AimPitch=0.f; HeadYaw=0.f; AimUpAxis=FVector::UpVector; AimSupportWeight=1.f; ShotgunAimRecovery=1.f; bAimWeaponActive=false; bAimInitialized=false; AimLogBand=0; // 初始化/重初始化同时清理头角与散弹枪恢复权重，不携带旧Avatar的姿态缓存。
    Speed = 0; LocomotionState = 0; LocomotionTime = 0; AirSeconds = 0; LandSeconds = 0;
    bFalling = false; bWasFalling = false; bSprinting = false;
    BodyMotionPose=nullptr; PreviousBodyMotionPose=nullptr; BodyMotionTime=PreviousBodyMotionTime=BodyMotionPhase=BodyMotionWeight=0.f;
    BodyMotionBlendAlpha=1.f; BodyMotionBlendSeconds=.08f; BodyMotionState=0; BodyMotionWeaponId=NAME_None;
    bDirectionalLocomotion=false; GroundBlendSpace=nullptr; GroundVelocity=FVector::ZeroVector; GroundRight=GroundForward=0.f; GaitPhase=0.f; GroundPoseWeight=0.f; ActionOverlayWeight=1.f; // Avatar重建独立初始化，转向/换枪不再重置步相。
    bGroundedForFeet=false; FootFloorPoint=FVector::ZeroVector; FootFloorNormal=FVector::UpVector; // 实例重建不得沿用旧World地面。
    UpperBodySequence = nullptr; UpperBodyWeight = 0; UpperBodyTime = 0; ActionProgress = 0; UpperBodyAction = NAME_None;
    MeleeFullBodyWeight=0.f; // Avatar/网格重建不沿用上一局的站立击打权重。
    MeleeAttackSequence=nullptr; MeleeAttackTime=0.f; // 实例重建不保留上次攻击引用或源时刻。
    bUseAdditiveGunRecoil=false; GunRecoilMontage=nullptr; GunRecoilSourceTime=GunRecoilPhase=0.f; GunRecoilShotSequence=0; RejectedGunRecoilMontage.Reset(); // 不跨Avatar保留成功枪身份或配置拒绝。
    // 实例可能因换Mesh重初始化；类默认值才是旧图的配置，不能把之前装备DA解析出的true当成永久默认。
    const UDemoPlayerAnimInstance* Defaults=GetClass()->GetDefaultObject<UDemoPlayerAnimInstance>(); // 只读生成类CDO，不改共享默认对象。
    bMeleeUpperBodyMontageFallback=Defaults && Defaults->bUseMeleeUpperBodyMontage;
    bUseMeleeUpperBodyMontage=bMeleeUpperBodyMontageFallback;
    IdleTime=0; WalkTime=0; RunTime=0; JumpStartTime=0; JumpLoopTime=0; JumpLandTime=0; SprintTime=0; StrafeLeftTime=0; StrafeRightTime=0;
    PreviousUpperBodySequence=nullptr; PreviousUpperBodyTime=0; PreviousActionProgress=0; PreviousUpperBodyAction=NAME_None;
    UpperBodyBlendAlpha=1; UpperBlendSeconds=.12f; UpperBlendDuration=.12f; UpperShotSequence=0; UpperWeaponId=NAME_None;
    RunLeftArmSequence=nullptr; RunLeftArmTime=0.f; RunLeftArmWeight=0.f; RunLeftArmMovementWeight=0.f; // 重建不沿用旧Pawn的摆臂相位或权重。
}

void UDemoPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::NativeUpdateAnimation(DeltaSeconds);
    GetProxyOnGameThread<FDemoPlayerAnimProxy>().bFocusActive=false; // 所有早退路径也关闭旧快照；引擎此时尚未派发本帧动画任务。
    MeleeAttackSequence=nullptr; MeleeAttackTime=0.f; // Avatar/Appearance早退也撤销本帧Slot输入，不能让派生类续播上一帧近战。
    bUseAdditiveGunRecoil=false; GunRecoilMontage=nullptr; GunRecoilSourceTime=GunRecoilPhase=0.f; GunRecoilShotSequence=0; // 早退也撤销旧后坐策略，派生类据此停止旧实例。
    if (!Character.IsValid()) { if(ReloadAimReleaseRemaining>0.f || !ReloadAimReleaseWeapon.IsNone())ResetReloadAimRecovery(TEXT("missing Avatar")); return; } // 无效Avatar可连续Tick；已清理后不逐帧刷生命周期调用日志。
    const UDemoPlayerPresentationComponent* Presentation = Character->GetPlayerPresentation(); // Pawn组件借用，只在游戏线程读取。
    const UDemoPlayerAppearance* Appearance = Presentation ? Presentation->GetAppearance() : nullptr; // 资产由组件保活。
    if (!Appearance || Appearance->MovementClips.Num() != 9) { if(ReloadAimReleaseRemaining>0.f || !ReloadAimReleaseWeapon.IsNone())ResetReloadAimRecovery(TEXT("missing/invalid Appearance")); return; }
    const UCharacterMovementComponent* Movement = Character->GetCharacterMovement(); // 移动真值由Character负责。
    const float Step = FMath::Max(0.f, DeltaSeconds); // 暂停时为0，不能使用墙钟推进。
    Speed = Character->IsCoopEliminated() ? 0.f : Character->GetVelocity().Size2D();
    const FVector LocalVelocity = Character->GetActorTransform().InverseTransformVectorNoScale(Character->GetVelocity()); // 移动方向只用于表现，不能写回角色速度。
    Direction = Speed > 1.f ? FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y,LocalVelocity.X)) : 0.f;
    bFalling = Movement && Movement->IsFalling();
    bGroundedForFeet = Movement && Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor() && !Character->IsCoopEliminated();
    if (bGroundedForFeet)
    {
        const FTransform MeshTransform=GetSkelMeshComponent()->GetComponentTransform(); // 当前身体变换只在游戏线程借用，复制平面给最终骨控制。
        FootFloorPoint=MeshTransform.InverseTransformPosition(Movement->CurrentFloor.HitResult.ImpactPoint);
        FootFloorNormal=MeshTransform.InverseTransformVectorNoScale(Movement->CurrentFloor.HitResult.ImpactNormal).GetSafeNormal();
    }
    bSprinting = !Character->IsCoopEliminated() && (Character->IsDashEvading() || Speed > 900.f);
    AirSeconds = bFalling ? AirSeconds + Step : 0.f;
    const float LandingDuration=Appearance->Locomotion?FMath::Clamp(Appearance->Locomotion->LandingSeconds,.05f,.5f):.16f; // 仅视觉恢复长度；跳跃/移动立即响应。
    if (!bFalling && bWasFalling && !bSprinting) LandSeconds = LandingDuration;
    else LandSeconds = FMath::Max(0.f, LandSeconds - Step);
    bWasFalling = bFalling;
    const int32 NextState = Character->IsCoopEliminated() ? 0 : bSprinting ? 6 : bFalling ?
        (AirSeconds < .18f && Character->GetVelocity().Z > 0.f ? 3 : 4) :
        LandSeconds > 0.f ? 5 : Speed>8.f && FMath::Abs(LocalVelocity.Y)>FMath::Abs(LocalVelocity.X) ? (LocalVelocity.Y>0.f?8:7) :
        Speed > Appearance->RunThreshold ? 2 : Speed > 8.f ? 1 : 0; // 横移使用独立侧脚步，换弹仍保留腿部状态。
    float* StateTimes[] = {&IdleTime,&WalkTime,&RunTime,&JumpStartTime,&JumpLoopTime,&JumpLandTime,&SprintTime,&StrafeLeftTime,&StrafeRightTime}; // 当前实例九个独立时钟借用，不跨帧持有指针。
    if (NextState != LocomotionState)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PLAYER_LOCOMOTION from=%d to=%d speed=%.1f falling=%d sprint=%d"), LocomotionState, NextState, Speed, bFalling, bSprinting);
        LocomotionState = NextState; *StateTimes[LocomotionState]=0.f;
    }
    const UAnimSequence* MovementClip = Appearance->MovementClips[LocomotionState]; // 固定下标契约已在导入验收。
    const float Rate = LocomotionState == 1 ? FMath::Clamp(Speed / FMath::Max(Appearance->AuthoredWalkSpeed,1.f), .25f, 2.5f) :
        LocomotionState == 2 ? FMath::Clamp(Speed / FMath::Max(Appearance->AuthoredRunSpeed,1.f), .25f, 1.5f) :
        LocomotionState >= 7 ? FMath::Clamp(Speed / FMath::Max(Appearance->AuthoredStrafeSpeed,1.f), .25f, 3.5f) :
        LocomotionState == 6 ? FMath::Clamp(Speed / 1300.f, .8f, 1.6f) : 1.f; // 速度驱动步频，原地动作不写回RootMotion。
    LocomotionTime = *StateTimes[LocomotionState] + Step * Rate * ((LocomotionState==1 || LocomotionState==2) && LocalVelocity.X < -Speed*.7f ? -1.f : 1.f); // 纯后退反向取样，横移使用专用循环。
    if (MovementClip)
    {
        const float Length = FMath::Max(MovementClip->GetPlayLength(),.01f); // 已导入资源长度，保护0长度外部误配置。
        if (LocomotionState==3) LocomotionTime=Length*FMath::Clamp(AirSeconds/.18f,0.f,1.f);
        else if (LocomotionState==5) LocomotionTime=Length*FMath::Clamp(1.f-LandSeconds/LandingDuration,0.f,1.f);
        else LocomotionTime=FMath::Fmod(FMath::Fmod(LocomotionTime,Length)+Length,Length); // 长帧/反向均保证在[0,Length)。
    }
    *StateTimes[LocomotionState]=LocomotionTime; // 其他状态冻结供混出，不被当前状态重置时间。
    const ADemoWeaponBase* Weapon = Character->GetWeaponComponent() ? Character->GetWeaponComponent()->GetActiveWeapon() : nullptr; // 只借用当前装备。
    const FDemoPlayerWeaponPoses* Poses = Weapon ? Appearance->WeaponPoses.Find(Weapon->GetWeaponId()) : nullptr; // 角色配置按稳定ID查询。
    FString RecoilError; // 当前游戏线程读取配置；非法值不进入动画工作线程或静默套用另一把枪。
    const bool bConfiguredAdditiveRecoil=Poses && Poses->RecoilMontage && IsA(UDemoLyraAnimInstance::StaticClass())
        && Poses->ValidateRecoilMontage(GetSkelMeshComponent()->GetSkeletalMeshAsset()?GetSkelMeshComponent()->GetSkeletalMeshAsset()->GetSkeleton():nullptr,RecoilError);
    if (Poses && Poses->RecoilMontage && !bConfiguredAdditiveRecoil && RejectedGunRecoilMontage!=Poses->RecoilMontage)
    { RejectedGunRecoilMontage=Poses->RecoilMontage; UE_LOG(LogFPSDemo,Warning,TEXT("GUN_RECOIL_CONFIG_REJECT owner=%s asset=%s reason=%s"),*GetNameSafe(Character.Get()),*GetNameSafe(Poses->RecoilMontage),*RecoilError); }
    else if (!Poses || !Poses->RecoilMontage || bConfiguredAdditiveRecoil) RejectedGunRecoilMontage.Reset();
    UAnimSequence* NextUpper = Poses ? Poses->Hold.Get() : nullptr; // 默认持枪无Notify，失配不冒用别枪动作。
    FName NextAction = NextUpper ? FName(TEXT("Hold")) : NAME_None; // 本帧诊断状态副本。
    FDemoBoltCycleState Shot = Weapon ? Weapon->GetShotPresentationState() : FDemoBoltCycleState(); // 可写本帧副本，近战用其序号复用已有过渡检测，不修改枪械真值。
    const float LastProgress = ActionProgress; // 动作切换前真实phase，取消时身体/机械同时冻结此值。
    ActionProgress = 0.f;
    if (Weapon && Poses)
    {
        const AGameStateBase* State = GetWorld()->GetGameState(); // 使用已有同步时钟，没有新RPC。
        const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds(); // 服务器和本机单人同样可用。
        if (Weapon->IsReloading())
        {
            NextUpper = Weapon->IsEmptyReload() ? Poses->EmptyReload.Get() : Poses->Reload.Get();
            NextAction = Weapon->IsEmptyReload() ? FName(TEXT("EmptyReload")) : FName(TEXT("Reload"));
            ActionProgress = FMath::Clamp(static_cast<float>((Now - Weapon->GetReloadServerStartTime()) / FMath::Max(Weapon->GetReloadDuration(), .01f)), 0.f, 1.f);
        }
        else
        {
            const float FireDuration=bConfiguredAdditiveRecoil?Poses->GetRecoilPresentationSeconds():Poses->Fire?FMath::Min(Poses->Fire->GetPlayLength(),Shot.Duration):0.f; // 新增量不挤进射击间隔；密集新枪按序号跨淡，Gameplay冷却保持原值。
            if (Weapon->Config.bCycleBoltAfterShot && Poses->Bolt && Shot.Sequence > 0 && Shot.Duration > 0 && Now >= Shot.ServerStartTime && Now < Shot.ServerStartTime + Shot.Duration)
            { NextUpper = Poses->Bolt; NextAction = TEXT("Bolt"); ActionProgress = FMath::Clamp(static_cast<float>((Now - Shot.ServerStartTime) / Shot.Duration), 0.f, 1.f); }
            else if (!Weapon->Config.bCycleBoltAfterShot && (bConfiguredAdditiveRecoil || Poses->Fire) && Shot.Sequence > 0 && Shot.Duration>0.f && FireDuration > 0 && Now >= Shot.ServerStartTime && Now < Shot.ServerStartTime + FireDuration)
            { NextUpper=bConfiguredAdditiveRecoil?Poses->Hold.Get():Poses->Fire.Get(); NextAction=TEXT("Fire"); ActionProgress=FMath::Clamp(static_cast<float>((Now-Shot.ServerStartTime)/FireDuration),0.f,1.f); }
        }
    }
    const UDemoMeleeComponent* Melee=Character->FindComponentByClass<UDemoMeleeComponent>(); // 游戏线程只读复制动作，图不直接访问组件。
    const UDemoMeleeDefinition* MeleeDefinition=Melee && Melee->IsEquipped()?Melee->GetDefinition():nullptr; // 当前装备定义由近战组件保活，仅本帧借用；无定义不访问姿态字段。
    const bool bMelee=MeleeDefinition!=nullptr; // 近战独立槽优先于仍保留库存的枪械。
    const bool bNextMeleeMontage=bMeleeUpperBodyMontageFallback || (MeleeDefinition && MeleeDefinition->bUseUpperBodyMontage); // 每武器显式开启，旧DA默认false保持原行为。
    if (bNextMeleeMontage!=bUseMeleeUpperBodyMontage)
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_UPPER_BODY_POLICY pawn=%s definition=%s enabled=%d fallback=%d"),*Character->GetName(),*GetNameSafe(MeleeDefinition),bNextMeleeMontage,bMeleeUpperBodyMontageFallback);
    bUseMeleeUpperBodyMontage=bNextMeleeMontage; // 在本帧选上身源前发布，派生Lyra随后同步Montage；不是对已有结果叠第二次攻击。
    if (bMelee)
    {
        float PoseTime=0.f; // GetPose同时提供源时刻，下面统一通过归一化进度发布给原有图。
        NextUpper=Melee->GetPose(false,PoseTime,ActionProgress,NextAction); Shot.Sequence=Melee->GetAttackSerial();
        if (bUseMeleeUpperBodyMontage)
        {
            if (Melee->IsAttacking()) { MeleeAttackSequence=NextUpper; MeleeAttackTime=PoseTime; }
            NextUpper=MeleeDefinition->ThirdPersonIdle; // 专用Slot只覆盖一次攻击；旧Evaluator不得再叠同一攻击。
        }
    }
    const FName NextWeaponId = bMelee?MeleeDefinition->WeaponId:Weapon ? Weapon->GetWeaponId() : NAME_None; // 身份隔离握姿，新增DA不改C++选择表。
    const bool bNewShot = (bMelee || NextAction==TEXT("Fire") || NextAction==TEXT("Bolt")) && Shot.Sequence!=UpperShotSequence; // 同招式下一次击打仍有独立时钟。
    const bool bPendingOperationBlend=PreviousUpperBodyAction==TEXT("Reload") || PreviousUpperBodyAction==TEXT("EmptyReload") || PreviousUpperBodyAction==TEXT("Bolt"); // 旧操作仍可能占有Hold混出的一部分姿态/Aim让权。
    const bool bPreserveOperationBlend=bConfiguredAdditiveRecoil && !bMelee && NextUpper && NextUpper==UpperBodySequence
        && NextWeaponId==UpperWeaponId && (NextAction==TEXT("Hold") || NextAction==TEXT("Fire"))
        && (UpperBodyAction==TEXT("Hold") || UpperBodyAction==TEXT("Fire")) && bPendingOperationBlend && UpperBodyBlendAlpha<.999f; // 同Hold基础的Fire标签不能丢弃尚未结束的操作混出；玩法Shot仍立即提交。
    if (NextAction != UpperBodyAction || NextUpper != UpperBodySequence || bNewShot)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PLAYER_UPPER action=%s asset=%s shot=%d"), *NextAction.ToString(), *GetNameSafe(NextUpper),Shot.Sequence);
        if (bPreserveOperationBlend)
        {
            UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_DEFER_OPERATION_BLEND owner=%s previous=%s next=%s seq=%d remaining=%.6f"),
                *GetNameSafe(Character.Get()),*PreviousUpperBodyAction.ToString(),*NextAction.ToString(),Shot.Sequence,FMath::Max(0.f,UpperBlendDuration-UpperBlendSeconds));
            // 保留PreviousSequence/Time/Action/Progress及原混出时钟；若改成当前Hold，AimAlignment会在新MG仍weight0时直接跳1。
        }
        else
        {
            const bool bSameWeapon = NextWeaponId==UpperWeaponId && UpperBodySequence && NextUpper; // 只同枪动作可共享机械混合，首次初始化直接取当前。
            PreviousUpperBodySequence=bSameWeapon?UpperBodySequence.Get():NextUpper;
            PreviousUpperBodyTime=bSameWeapon?UpperBodyTime:NextUpper?NextUpper->GetPlayLength()*ActionProgress:0.f;
            PreviousUpperBodyAction=bSameWeapon?UpperBodyAction:NextAction;
            PreviousActionProgress=bSameWeapon?LastProgress:ActionProgress;
            const bool bReloadBoundary=NextAction==TEXT("Reload") || NextAction==TEXT("EmptyReload") || UpperBodyAction==TEXT("Reload") || UpperBodyAction==TEXT("EmptyReload"); // 进入/退出装填都拥有原操作混合；同帧接Fire不能被枪后坐的.02/.03秒覆盖。
            const bool bNewRecoilTransition=!bReloadBoundary && bConfiguredAdditiveRecoil && !bMelee && (NextAction==TEXT("Fire") || UpperBodyAction==TEXT("Fire")); // 纯后坐边沿才采用枪配置；Reload→Fire仍立即更新真实Shot/phase，但先保全原源混出。
            UpperBlendDuration=bReloadBoundary?.12f:bNewRecoilTransition?(NextAction==TEXT("Fire")?(UpperBodyAction==TEXT("Fire")?Poses->RecoilRetriggerBlendSeconds:Poses->RecoilBlendInSeconds):Poses->RecoilBlendOutSeconds):(NextAction==TEXT("Fire") || UpperBodyAction==TEXT("Fire"))?.03f:.12f;
            if (bReloadBoundary && NextAction==TEXT("Fire"))
                UE_LOG(LogFPSDemo,Log,TEXT("RELOAD_HOLD_DIRECT_FIRE_HANDOFF owner=%s previous=%s duration=%.6f shot=%d phase=%.6f"),*GetNameSafe(Character.Get()),*UpperBodyAction.ToString(),UpperBlendDuration,Shot.Sequence,ActionProgress); // 证实无中间Hold的实际消费路径；日志不等于最终姿态通过。
            UpperBlendDuration=FMath::Max(UpperBlendDuration,.0001f); // 允许配置硬边沿0秒，但不能用0作SmoothStep分母。
            UpperBlendSeconds=bSameWeapon?0.f:UpperBlendDuration;
        }
    }
    UpperBlendSeconds=FMath::Min(UpperBlendDuration,UpperBlendSeconds+Step);
    UpperBodyBlendAlpha=FMath::SmoothStep(0.f,1.f,UpperBlendSeconds/UpperBlendDuration); // 相同权重同时驱动手/整枪锚点/内部机械，不单独平滑枪。
    UpperShotSequence=Shot.Sequence;
    UpperWeaponId=NextWeaponId;
    UpperBodyAction = NextAction; UpperBodySequence = NextUpper;
    UpperBodyTime=bMelee && bUseMeleeUpperBodyMontage?Melee->GetIdlePoseTime(false)
        :NextUpper?NextUpper->GetPlayLength()*(bConfiguredAdditiveRecoil && NextAction==TEXT("Fire")?0.f:ActionProgress):0.f; // Melee底层读独立Idle秒，攻击源仍仅Slot；枪械Fire继续固定Hold，禁止把攻击PoseTime误用成Idle时间。
    UpperBodyWeight = NextUpper && !Character->IsCoopEliminated() ? 1.f : 0.f;
    const bool bOperationBlend=NextAction==TEXT("Reload") || NextAction==TEXT("EmptyReload") || NextAction==TEXT("Bolt")
        || (UpperBodyBlendAlpha<.999f && (PreviousUpperBodyAction==TEXT("Reload") || PreviousUpperBodyAction==TEXT("EmptyReload") || PreviousUpperBodyAction==TEXT("Bolt"))); // 操作混出期间也禁止将其差量误当成后坐。
    bUseAdditiveGunRecoil=bConfiguredAdditiveRecoil && !bMelee && !bOperationBlend && UpperBodyWeight>0.f && (NextAction==TEXT("Hold") || NextAction==TEXT("Fire"));
    if (bUseAdditiveGunRecoil)
    {
        const bool bFinishedOperation=PreviousUpperBodyAction==TEXT("Reload") || PreviousUpperBodyAction==TEXT("EmptyReload") || PreviousUpperBodyAction==TEXT("Bolt"); // 本帧策略已确认操作混出完成，只清理仍被引用的旧操作端点。
        if (bFinishedOperation && UpperBodyBlendAlpha>=.999f)
        {
            // 后坐Montage的实际权重随后复用于机械混合；先把已完全退出的操作重基准到Hold，避免首枪weight=0又把旧Reload/Bolt拉回来。
            UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_REBASE owner=%s previous=%s current=%s"),*GetNameSafe(Character.Get()),*PreviousUpperBodyAction.ToString(),*UpperBodyAction.ToString());
            PreviousUpperBodySequence=UpperBodySequence; PreviousUpperBodyTime=0.f;
            PreviousUpperBodyAction=TEXT("Hold"); PreviousActionProgress=0.f; // 身体基础本来就是Hold0；新枪机械仍由其自身实际Montage相位/权重跨淡。
        }
        GunRecoilMontage=Poses->RecoilMontage; GunRecoilShotSequence=Shot.Sequence;
        GunRecoilPhase=NextAction==TEXT("Fire")?ActionProgress:0.f; GunRecoilSourceTime=GunRecoilMontage->GetPlayLength()*GunRecoilPhase; // Body/机械共用源phase，隐藏Mesh1P不是此时钟来源。
    }
    const float FullBodyTarget=!bUseMeleeUpperBodyMontage && MeleeDefinition && MeleeDefinition->bStationaryFullBody && Melee->IsAttacking() && bGroundedForFeet
        ?1.f-FMath::SmoothStep(3.f,MeleeDefinition->FullBodyFadeSpeed,Speed):0.f; // 缓慢刹停渐入；真实移动速度控制让位，空中立即禁止站姿覆盖。
    const float PreviousFullBody=MeleeFullBodyWeight; // 只记录权重启停边沿，不把逐帧混合日志提升等级。
    MeleeFullBodyWeight=bUseMeleeUpperBodyMontage?0.f:bGroundedForFeet?FMath::FInterpConstantTo(MeleeFullBodyWeight,FullBodyTarget,Step,1.f/(MeleeDefinition?MeleeDefinition->FullBodyBlendSeconds:.12f)):0.f; // 新路径腿/骨盆始终属于Locomotion，包括起停过渡。
    if ((PreviousFullBody<=0.f && MeleeFullBodyWeight>0.f) || (PreviousFullBody>0.f && MeleeFullBodyWeight<=0.f))
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_FULL_BODY_BLEND pawn=%s active=%d speed=%.2f"),*Character->GetName(),MeleeFullBodyWeight>0.f,Speed);
    UpdateRunLeftArm(Step,Appearance); // 使用同帧移动/换弹混合真值，换弹动作优先于单手跑动。
    UpdateLocomotion(Step,Appearance); // 同帧武器身份确定后选择配置；地面步相与上身操作分离。
    UpdateAim(Step,Appearance,NextWeaponId==TEXT("pistol")); // 在上身动作/过渡缓存完成后，瞄准层使用同帧动作进度。
    GetProxyOnGameThread<FDemoPlayerAnimProxy>().CaptureMuzzleFocus(*this); // 当前Pawn/装备更新后发布，同帧节点工作线程读取。
}

FName UDemoPlayerAnimInstance::GetUpperWeaponId() const { DEMO_LOG_TICK(); return UpperWeaponId; }

void UDemoPlayerAnimInstance::UpdateRunLeftArm(float DeltaSeconds, const UDemoPlayerAppearance* Appearance)
{
    DEMO_LOG_TICK();
    const float PreviousWeight=RunLeftArmWeight; // 仅边沿日志，避免每帧Log级刷屏。
    const FDemoPlayerWeaponPoses* Poses=Appearance?Appearance->WeaponPoses.Find(UpperWeaponId):nullptr; // 同帧当前枪只读配置。
    RunLeftArmSequence=Poses?Poses->RunLeftArm.Get():nullptr;
    if (!Character.IsValid() || Character->IsCoopEliminated() || UpperWeaponId!=TEXT("pistol") || !RunLeftArmSequence || UpperBodyWeight<=0.f)
    {
        RunLeftArmMovementWeight=0.f; RunLeftArmWeight=0.f; RunLeftArmTime=0.f;
        if (PreviousWeight>0.f) UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_RUN_ARM_DISABLED weapon=%s reason=weapon/asset/death"),*UpperWeaponId.ToString());
        return;
    }
    const bool bRunning=!bFalling && !bSprinting && Speed>Appearance->RunThreshold && (LocomotionState==2 || LocomotionState==7 || LocomotionState==8); // 按真实移动，不在原地按键/跳跃/Dash时伪造跑步。
    RunLeftArmMovementWeight=FMath::FInterpConstantTo(RunLeftArmMovementWeight,bRunning?1.f:0.f,DeltaSeconds,1.f/.12f);
    if (bRunning && Appearance->MovementClips.IsValidIndex(LocomotionState) && Appearance->MovementClips[LocomotionState])
    {
        const float LegLength=FMath::Max(.01f,Appearance->MovementClips[LocomotionState]->GetPlayLength()); // 源腿循环秒数，后退已在移动时钟反向取样。
        RunLeftArmTime=FMath::Clamp(LocomotionTime/LegLength,0.f,1.f)*RunLeftArmSequence->GetPlayLength();
    }
    const bool bCurrentReload=UpperBodyAction==TEXT("Reload") || UpperBodyAction==TEXT("EmptyReload"); // 完整装填期间权重为0，包括玩家持续跑动。
    const bool bPreviousReload=PreviousUpperBodyAction==TEXT("Reload") || PreviousUpperBodyAction==TEXT("EmptyReload"); // 结束/取消继续沿原0.12秒混出，避免左手瞬跳。
    const float ReloadWeight=FMath::Lerp(bPreviousReload?1.f:0.f,bCurrentReload?1.f:0.f,UpperBodyBlendAlpha); // 与手/枪机械相同过渡时钟，无独立蒙太奇抢占。
    RunLeftArmWeight=RunLeftArmMovementWeight*(1.f-ReloadWeight);
    if ((PreviousWeight>KINDA_SMALL_NUMBER)!=(RunLeftArmWeight>KINDA_SMALL_NUMBER))
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_RUN_ARM_BLEND active=%d speed=%.1f action=%s reloadWeight=%.3f"),RunLeftArmWeight>KINDA_SMALL_NUMBER,Speed,*UpperBodyAction.ToString(),ReloadWeight);
}
