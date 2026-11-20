// 职责：验证FP资源闭包并向动画图发布纯值快照；图求值不跨线程访问Actor、GAS或World。
#include "Animation/DemoFPAnimInstance.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Engine/SkeletalMesh.h"
#include "Debug/DemoLog.h"

bool FDemoWeaponAnimationSet::Validate(const USkeletalMesh* ArmsMesh, const USkeletalMesh* WeaponMesh, FString& Error) const
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    Error.Empty();
    if (!ArmsMesh || !WeaponMesh || !IdlePose || IdlePose->GetSkeleton() != ArmsMesh->GetSkeleton())
        Error = TEXT("Missing mesh/idle or idle does not use shared first-person skeleton");
    else if (!FMath::IsFinite(EquipBlendSeconds) || EquipBlendSeconds < 0.f || EquipBlendSeconds > 1.f
        || !FMath::IsFinite(MoveSwayCentimeters) || MoveSwayCentimeters < 0.f || MoveSwayCentimeters > 2.f
        || MagazineHandOffset.ContainsNaN() || MagazineHandOffset.GetScale3D().GetMin() <= 0.f || SupportHandGrip.ContainsNaN()
        || !FMath::IsFinite(SupportHandIKAlpha) || SupportHandIKAlpha < 0.f || SupportHandIKAlpha > 1.f)
        Error = TEXT("Invalid blend/sway/magazine offset");
    // Pair同步借用；两种换弹必需，选配拉栓一旦配置必须完整，武器开关另由装备预检约束。
    for (const FDemoReloadAnimationPair* Pair : { &TacticalReload, &EmptyReload, &BoltCycle })
    {
        if (!Error.IsEmpty()) break;
        if (Pair == &BoltCycle && !Pair->ArmsMontage && !Pair->WeaponSequence) continue;
        if (!Pair->ArmsMontage || !Pair->WeaponSequence || Pair->ArmsMontage->GetSkeleton() != ArmsMesh->GetSkeleton()
            || Pair->WeaponSequence->GetSkeleton() != WeaponMesh->GetSkeleton() || Pair->ArmsMontage->GetPlayLength() <= 0.f
            || Pair->WeaponSequence->GetPlayLength() <= 0.f)
        { Error = TEXT("Reload pair missing, zero length or mismatched skeleton"); break; }
        if (Pair->ArmsMontage->SlotAnimTracks.Num() != 1 || Pair->ArmsMontage->SlotAnimTracks[0].SlotName != TEXT("FPAction"))
        { Error = TEXT("Reload montage must have exactly one FPAction slot"); break; }
        if (!FMath::IsFinite(Pair->MagazineDetachPhase) || !FMath::IsFinite(Pair->MagazineAttachPhase)
            || Pair->MagazineDetachPhase < 0.f || Pair->MagazineDetachPhase >= Pair->MagazineAttachPhase || Pair->MagazineAttachPhase > 1.f)
        { Error = TEXT("Magazine phases must satisfy 0 <= detach < attach <= 1"); break; }
        float PreviousTime = -1.f; // 验证时只在本Pair存活，升序允许同帧多声但不允许倒序。
        for (const FDemoReloadSoundEvent& Event : Pair->SoundEvents) // 配置事件借用，运行事件游标不写回该数组。
        {
            if (!Event.Sound || !FMath::IsFinite(Event.NormalizedTime) || Event.NormalizedTime < PreviousTime || Event.NormalizedTime < 0.f || Event.NormalizedTime > 1.f
                || !FMath::IsFinite(Event.VolumeMultiplier) || Event.VolumeMultiplier < 0.f || Event.VolumeMultiplier > 2.f
                || !FMath::IsFinite(Event.PitchMultiplier) || Event.PitchMultiplier < .5f || Event.PitchMultiplier > 2.f)
            { Error = TEXT("Reload sound event missing or invalid ordered time/volume/pitch"); break; }
            PreviousTime = Event.NormalizedTime;
        }
    }
    if (FireMontage && ArmsMesh && (FireMontage->GetSkeleton() != ArmsMesh->GetSkeleton() || FireMontage->SlotAnimTracks.Num() != 1
        || FireMontage->SlotAnimTracks[0].SlotName != TEXT("FPAction"))) Error = TEXT("Fire montage must use shared arms skeleton and FPAction");
    for (const UAnimSequence* Mechanical : {WeaponIdlePose.Get(),WeaponEmptyPose.Get(),FireWeaponSequence.Get(),LastShotWeaponSequence.Get()}) // 选配短动作同枪骨架且时长有效。
        if (Mechanical && (!WeaponMesh || Mechanical->GetSkeleton()!=WeaponMesh->GetSkeleton() || Mechanical->GetPlayLength()<=0.f)) Error=TEXT("Fire/idle mechanical sequence skeleton or length invalid");
    if ((FireWeaponSequence || LastShotWeaponSequence) && !FireMontage) Error=TEXT("Mechanical fire requires paired arms montage");
    if (!Error.IsEmpty()) UE_LOG(LogFPSDemo, Error, TEXT("WEAPON_ANIM_CONFIG_REJECT %s"), *Error);
    return Error.IsEmpty();
}

void UDemoFPAnimInstance::NativeInitializeAnimation()
{
    DEMO_LOG_CALL();
    Super::NativeInitializeAnimation();
    Snapshot = FDemoFPAnimationSnapshot();
    IdlePose = nullptr;
    SupportHandGrip = FVector::ZeroVector;
    SupportHandIKAlpha = 0.f;
    HandAimInput=FDemoFPHandAimInput(); bLoggedHandAimActive=false; // FP-HANDAIM：重建实例不携带旧枪瞄准输入，等表现组件下一帧重新发布。
    MovementPose=nullptr; PreviousMovementPose=nullptr; MovementTime=0.f; PreviousMovementTime=0.f; MovementBlendAlpha=1.f; MovementWeight=0.f; MovementWeaponId=NAME_None; MovementBlendSeconds=.1f; // 重建不携带旧枪步态。
}

void UDemoFPAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::NativeUpdateAnimation(DeltaSeconds);
    // NativeUpdateAnimation是UE游戏线程的采样边界；工作线程图只读取已发布的Snapshot及资源。
    const AActor* Owner = GetOwningActor(); // 预览实例可能没有Pawn；不把Owner保存给工作线程。
    UDemoWeaponAnimationComponent* Animation = Owner ? Owner->FindComponentByClass<UDemoWeaponAnimationComponent>() : nullptr; // Owner组件本帧借用，仅游戏线程写审计值。
    if (Animation) Animation->SampleReloadAnimationPhase(this); // 此时UE已推进Montage并生成本帧求值数据，读取实际位置而非目标值。
    Snapshot = Animation ? Animation->GetSnapshot() : FDemoFPAnimationSnapshot();
    const FDemoWeaponAnimationSet* Set = Animation ? Animation->GetAnimationSet() : nullptr; // 类CDO只读配置，本次采样后不持有裸地址。
    IdlePose = Set ? Set->IdlePose : nullptr;
    UpdateMovement(DeltaSeconds); // 持枪基底之后、FPAction Slot之前的独立移动层。
    SupportHandGrip = Set ? Set->SupportHandGrip : FVector::ZeroVector;
    const UDemoWeaponComponent* Equipment = Owner ? Owner->FindComponentByClass<UDemoWeaponComponent>() : nullptr; // 本帧只读装备，不在图查询。
    const ADemoWeaponBase* Weapon = Equipment ? Equipment->GetActiveWeapon() : nullptr; // 预览或初始化允许无武器。
    float ReloadPoseWeight = 0.f; // 含已停止但尚在淡出的换弹Montage；Gameplay结束不等于Slot已退出。
    if (Set)
    {
        for (const FAnimMontageInstance* MontageInstance : MontageInstances) // 引擎拥有，仅NativeUpdate游戏线程同步借用，不交给并行动画图。
        {
            if (MontageInstance && (MontageInstance->Montage == Set->TacticalReload.ArmsMontage || MontageInstance->Montage == Set->EmptyReload.ArmsMontage))
                ReloadPoseWeight += MontageInstance->GetWeight(); // 包含取消后的BlendOut实例；不依赖GetActiveInstanceForMontage排除掉淡出状态。
        }
    }
    const float PreviousIKAlpha = SupportHandIKAlpha; // 只用于交接边界日志，不形成逐帧平滑反馈。
    SupportHandIKAlpha = Set && Weapon && !Snapshot.bReloading && !Weapon->Config.bHideSupportArm
        ? Set->SupportHandIKAlpha * (1.f-FMath::Clamp(ReloadPoseWeight,0.f,1.f)) : 0.f; // 按实际Slot淡出权重交接，取消/正常完成共用，避免单帧0→1。
    if ((PreviousIKAlpha<=KINDA_SMALL_NUMBER)!=(SupportHandIKAlpha<=KINDA_SMALL_NUMBER))
        UE_LOG(LogFPSDemo,Verbose,TEXT("FP_SUPPORT_IK_HANDOFF old=%.4f new=%.4f reloadWeight=%.4f gameplayReload=%d"),PreviousIKAlpha,SupportHandIKAlpha,ReloadPoseWeight,Snapshot.bReloading);
}

void UDemoFPAnimInstance::PreUpdateAnimation(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::PreUpdateAnimation(DeltaSeconds); // 先让引擎同步上一帧并准备Proxy，后续仍在游戏线程、早于UpdateMontage。
    const AActor* Owner = GetOwningActor(); // 编辑器预览允许空，无跨帧捕获。
    UDemoWeaponAnimationComponent* Animation = Owner ? Owner->FindComponentByClass<UDemoWeaponAnimationComponent>() : nullptr; // Pawn拥有的同步时钟协调器。
    if (Animation) Animation->PrepareReloadAnimationUpdate(this, DeltaSeconds);
}

void UDemoWeaponLayerAnimInstance::NativeInitializeAnimation()
{
    DEMO_LOG_CALL();
    Super::NativeInitializeAnimation();
    IdlePose = AnimSet.IdlePose;
    Snapshot = FDemoFPAnimationSnapshot();
}

void UDemoWeaponLayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::NativeUpdateAnimation(DeltaSeconds);
    // 层没有ASC绑定，NativeUpdate只复制主实例本帧值；两者均位于UE游戏线程更新边界。
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent(); // 当前网格仅本次借用，编辑器预览可空。
    const UDemoFPAnimInstance* Main = Mesh ? Cast<UDemoFPAnimInstance>(Mesh->GetAnimInstance()) : nullptr; // 固定主实例，不追World/Actor。
    Snapshot = Main ? Main->Snapshot : FDemoFPAnimationSnapshot();
    IdlePose = AnimSet.IdlePose;
}
