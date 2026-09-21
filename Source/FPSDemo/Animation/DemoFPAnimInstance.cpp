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
    // Pair只在同步预检借用，始终检查两种动作，不能等到空仓后才发现缺失资源。
    for (const FDemoReloadAnimationPair* Pair : { &TacticalReload, &EmptyReload })
    {
        if (!Error.IsEmpty()) break;
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
    SupportHandGrip = Set ? Set->SupportHandGrip : FVector::ZeroVector;
    const UDemoWeaponComponent* Equipment = Owner ? Owner->FindComponentByClass<UDemoWeaponComponent>() : nullptr; // 本帧只读装备，不在图查询。
    const ADemoWeaponBase* Weapon = Equipment ? Equipment->GetActiveWeapon() : nullptr; // 预览或初始化允许无武器。
    SupportHandIKAlpha = Set && Weapon && !Snapshot.bReloading && !Weapon->Config.bHideSupportArm ? Set->SupportHandIKAlpha : 0.f;
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
