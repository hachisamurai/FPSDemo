#include "Animation/DemoWeaponAnimationComponent.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoGameplayAbility.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

UDemoWeaponAnimationComponent::UDemoWeaponAnimationComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
    MainAnimClass = TSoftClassPtr<UDemoFPAnimInstance>(FSoftObjectPath(TEXT("/Game/Weapons/Animations/ABP_FPArms_Base.ABP_FPArms_Base_C")));
}

ADemoCharacter* UDemoWeaponAnimationComponent::GetCharacter() const
{
    DEMO_LOG_TICK();
    return Cast<ADemoCharacter>(GetOwner());
}

bool UDemoWeaponAnimationComponent::InitializeArms()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 仅同步借用Pawn，构造/CDO阶段不能初始化动画。
    if (!Character || !Character->GetMesh1P()) { UE_LOG(LogFPSDemo, Error, TEXT("FP_ANIM_INIT_REJECT no Mesh1P")); return false; }
    USkeletalMeshComponent* Arms = Character->GetMesh1P(); // Pawn拥有，组件只设置其固定主类。
    UClass* MainClass = MainAnimClass.LoadSynchronous(); // 首次装备加载，硬引用实例和Cook目录保活依赖。
    if (!MainClass) { UE_LOG(LogFPSDemo, Error, TEXT("FP_ANIM_INIT_REJECT missing ABP_FPArms_Base")); return false; }
    if (!bCachedArmsTransform)
    {
        BaseArmsLocation = Arms->GetRelativeLocation();
        bCachedArmsTransform = true;
    }
    if (!Arms->GetAnimInstance() || Arms->GetAnimInstance()->GetClass() != MainClass)
    {
        ClearReloadPresentation();
        LinkedLayerClass = nullptr;
        Arms->SetAnimInstanceClass(MainClass); // 只在首次或真正重建时调用，正常切枪保持主实例地址不变。
        UE_LOG(LogFPSDemo, Log, TEXT("FP_MAIN_INSTANCE_INITIALIZED class=%s"), *GetNameSafe(MainClass));
    }
    Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Arms->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
    if (Character->GetDemoASC()) Character->GetDemoASC()->SetFirstPersonAnimationMesh(Arms);
    return Cast<UDemoFPAnimInstance>(Arms->GetAnimInstance()) != nullptr;
}

bool UDemoWeaponAnimationComponent::ValidateWeapon(const ADemoWeaponBase* Weapon, FString& Error) const
{
    DEMO_LOG_CALL();
    Error.Empty();
    const ADemoCharacter* Character = GetCharacter(); // 当前Owner/骨架同步预检，失效时不提交切换。
    if (!Weapon || !Character || !Character->GetMesh1P()) Error = TEXT("Missing weapon/character/arms");
    else if (!Weapon->Config.WeaponAnimLayerClass) return true; // 保留外部旧武器兼容；四把交付武器均必须配置，专项测试严格检查。
    else if (Weapon->Config.StaticMesh || !Weapon->Config.Mesh) Error = TEXT("Linked weapon requires skeletal Mesh and empty StaticMesh");
    else if (!Character->GetMesh1P()->DoesSocketExist(Weapon->Config.AttachSocket)) Error = TEXT("Missing stable weapon anchor");
    else
    {
        const UDemoWeaponLayerAnimInstance* Defaults = Weapon->Config.WeaponAnimLayerClass.GetDefaultObject(); // CDO只读，不能存储本次换弹。
        if (!Defaults || !Weapon->Config.WeaponAnimLayerClass->FindFunctionByName(TEXT("WeaponBasePose"))) Error = TEXT("Weapon layer must implement WeaponBasePose");
        else if (!Defaults->AnimSet.Validate(Character->GetMesh1P()->GetSkeletalMeshAsset(), Weapon->Config.Mesh, Error)) {}
        else if (Defaults->AnimSet.MagazinePresentationMesh && !Character->GetMesh1P()->DoesSocketExist(Defaults->AnimSet.MagazineHandSocket)) Error = TEXT("Missing magazine hand socket");
    }
    if (!Error.IsEmpty()) UE_LOG(LogFPSDemo, Error, TEXT("FP_EQUIP_PRECHECK_REJECT weapon=%s reason=%s"), *GetNameSafe(Weapon), *Error);
    return Error.IsEmpty();
}

bool UDemoWeaponAnimationComponent::EquipWeapon(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_CALL();
    FString Error; // 仅本次调用存活的配置拒绝原因，失败不改已有层。
    if (!ValidateWeapon(Weapon, Error)) return false;
    ADemoCharacter* Character = GetCharacter(); // 预检后Owner仍在同一游戏线程有效。
    USkeletalMeshComponent* Arms = Character->GetMesh1P(); // 固定主图的所属网格。
    const TSubclassOf<UDemoWeaponLayerAnimInstance> PreviousClass = LinkedLayerClass; // 链接失败时恢复旧层，不恢复已取消换弹。
    if (Weapon->Config.WeaponAnimLayerClass && !InitializeArms()) return false;
    ClearReloadPresentation();
    if (LinkedLayerClass) Arms->UnlinkAnimClassLayers(LinkedLayerClass);
    if (Weapon->Config.WeaponAnimLayerClass)
    {
        Arms->LinkAnimClassLayers(Weapon->Config.WeaponAnimLayerClass);
        if (!Arms->GetAnimInstance() || !Arms->GetAnimInstance()->GetLinkedAnimLayerInstanceByClass(Weapon->Config.WeaponAnimLayerClass))
        {
            Arms->UnlinkAnimClassLayers(Weapon->Config.WeaponAnimLayerClass);
            if (PreviousClass) Arms->LinkAnimClassLayers(PreviousClass);
            LinkedLayerClass = PreviousClass;
            UE_LOG(LogFPSDemo, Error, TEXT("FP_LAYER_LINK_ROLLBACK rejected=%s previous=%s"), *GetNameSafe(Weapon->Config.WeaponAnimLayerClass), *GetNameSafe(PreviousClass));
            return false;
        }
    }
    LinkedLayerClass = Weapon->Config.WeaponAnimLayerClass;
    LinkedMainInstance = Cast<UDemoFPAnimInstance>(Arms->GetAnimInstance());
    EquippedWeapon = Weapon;
    EquipStartTime = GetWorld()->GetTimeSeconds();
    if (UDemoFPAnimInstance* Main = Cast<UDemoFPAnimInstance>(Arms->GetAnimInstance())) // 链接当帧直接发布资源，防止首次图求值读到空IdlePose。
    {
        const FDemoWeaponAnimationSet* Set = GetAnimationSet(); // 新类CDO只读配置。
        Main->IdlePose = Set ? Set->IdlePose : nullptr;
        Main->Snapshot = GetSnapshot();
        Main->SupportHandGrip = Set ? Set->SupportHandGrip : FVector::ZeroVector;
        Main->SupportHandIKAlpha = Set && !Weapon->Config.bHideSupportArm ? Set->SupportHandIKAlpha : 0.f;
    }
    Weapon->GetWeaponSkeletalMesh()->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
    // 手臂PreUpdate确定本帧共同phase后枪才求值，避免枪先求值而手部已进入下一帧姿势。
    Weapon->GetWeaponSkeletalMesh()->PrimaryComponentTick.AddPrerequisite(Arms, Arms->PrimaryComponentTick);
    if (Character->GetDemoASC()) Character->GetDemoASC()->SetFirstPersonAnimationMesh(Arms);
    RestoreWeaponPose(Weapon);
    UE_LOG(LogFPSDemo, Log, TEXT("FP_LAYER_LINKED weapon=%s main=%s layer=%s"), *Weapon->GetName(), *GetNameSafe(Arms->GetAnimInstance()), *GetNameSafe(LinkedLayerClass));
    return true;
}

const FDemoWeaponAnimationSet* UDemoWeaponAnimationComponent::GetAnimationSet() const
{
    DEMO_LOG_TICK();
    const UDemoWeaponLayerAnimInstance* Defaults = LinkedLayerClass ? LinkedLayerClass.GetDefaultObject() : nullptr; // 借用由类保活的CDO。
    return Defaults ? &Defaults->AnimSet : nullptr;
}

void UDemoWeaponAnimationComponent::PlayFire(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_CALL();
    const FDemoWeaponAnimationSet* Set = GetAnimationSet(); // 当前装备CDO只读，技能已提交弹药。
    ADemoCharacter* Character = GetCharacter(); // 仅游戏线程同步访问。
    if (Weapon != EquippedWeapon.Get() || ReloadWeapon.IsValid() || !Set || !Set->FireMontage || !Character) return;
    UAnimInstance* Main = Character->GetMesh1P()->GetAnimInstance(); // 开火只在主实例FPAction Slot播放。
    if (!Main || Main->Montage_Play(Set->FireMontage) <= 0.f) UE_LOG(LogFPSDemo, Warning, TEXT("FP_FIRE_PRESENTATION_FAILED weapon=%s"), *GetNameSafe(Weapon));
}

bool UDemoWeaponAnimationComponent::BeginReloadPresentation(ADemoWeaponBase* Weapon, UGameplayAbility* Ability, int32 Sequence, float Duration, bool bEmpty)
{
    DEMO_LOG_CALL();
    const FDemoWeaponAnimationSet* Set = GetAnimationSet(); // 同时给GA/动画使用的配置单一来源。
    ADemoCharacter* Character = GetCharacter(); // 同步借用当前Pawn。
    if (Weapon != EquippedWeapon.Get() || !Ability || !Set || !Character || !FMath::IsFinite(Duration) || Duration <= 0.f)
    { UE_LOG(LogFPSDemo, Warning, TEXT("FP_RELOAD_PRESENTATION_DEGRADED weapon=%s seq=%d"), *GetNameSafe(Weapon), Sequence); return false; }
    ClearReloadPresentation();
    ReloadWeapon = Weapon;
    ReloadAbility = Ability;
    ReloadSequence = Sequence;
    ReloadDuration = Duration;
    ReloadStartTime = GetWorld()->GetTimeSeconds();
    ReloadProgress = 0.f;
    bEmptyReload = bEmpty;
    ActivePair = bEmpty ? Set->EmptyReload : Set->TacticalReload;
    NextSoundEvent = 0;
    PhaseAudit = FDemoReloadPhaseAudit(); // 每次真实动作单独累计；清理保留最后结果供回归读取。
    PhaseAudit.Sequence = Sequence;
    Character->GetMesh1P()->UnHideBoneByName(TEXT("upperarm_l")); // 手枪平时隐藏左臂，换弹期间必须恢复。
    USkeletalMeshComponent* Gun = Weapon->GetWeaponSkeletalMesh(); // 本武器根网格与手臂是两个Skeleton，分别驱动。
    Gun->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Gun->SetAnimation(ActivePair.WeaponSequence);
    Gun->Stop(); // 枪由显式phase取样，不让单序列实例另跑独立时钟。
    Gun->SetPosition(0.f, false);
    UDemoAbilitySystemComponent* ASC = Character->GetDemoASC(); // GAS显式使用Mesh1P的主AnimInstance。
    UAnimInstance* Main = Character->GetMesh1P()->GetAnimInstance(); // 保留主实例，不在子层创建第二Montage播放器。
    if (ASC) ASC->SetFirstPersonAnimationMesh(Character->GetMesh1P());
    const float Rate = ActivePair.ArmsMontage ? ActivePair.ArmsMontage->GetPlayLength() / Duration : 1.f; // 将单段素材映射到唯一玩法时长。
    if (!ASC || !Main || !ActivePair.ArmsMontage || ASC->PlayMontage(Ability, Ability->GetCurrentActivationInfo(), ActivePair.ArmsMontage, Rate) <= 0.f)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("FP_RELOAD_MONTAGE_DEGRADED weapon=%s seq=%d; gameplay timer continues"), *Weapon->GetName(), Sequence);
        ClearReloadPresentation();
        return false;
    }
    FAnimMontageInstance* Instance = Main->GetActiveInstanceForMontage(ActivePair.ArmsMontage); // 播放成功后仅本次同步借用，跨帧只保存ID。
    if (!Instance)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("FP_RELOAD_MONTAGE_INSTANCE_MISSING weapon=%s seq=%d"), *Weapon->GetName(), Sequence);
        ClearReloadPresentation();
        return false;
    }
    ReloadMontageInstanceID = Instance->GetInstanceID();
    PhaseAudit.MontageInstanceID = ReloadMontageInstanceID;
    FOnMontageBlendingOutStarted BlendDelegate; // UObject弱绑定；只在当前Montage回调，同步清理用bClearing阻止重入。
    BlendDelegate.BindUObject(this, &UDemoWeaponAnimationComponent::OnReloadMontageBlendingOut, Sequence, ReloadMontageInstanceID);
    Main->Montage_SetBlendingOutDelegate(BlendDelegate, ActivePair.ArmsMontage);
    UpdateReloadPhase(0.f, true);
    UE_LOG(LogFPSDemo, Log, TEXT("FP_RELOAD_START weapon=%s seq=%d empty=%d duration=%.3f montage=%s rate=%.3f"), *Weapon->GetName(), Sequence, bEmpty, Duration, *GetNameSafe(ActivePair.ArmsMontage), Rate);
    return true;
}

void UDemoWeaponAnimationComponent::PrepareReloadAnimationUpdate(UDemoFPAnimInstance* Main, float DeltaSeconds)
{
    DEMO_LOG_TICK();
    check(IsInGameThread()); // FAnimMontageInstance与世界时钟仅在UE PreUpdate游戏线程边界访问。
    ADemoWeaponBase* Weapon = ReloadWeapon.Get(); // 每帧重新验证弱武器/序号，不能同步已经取消的新装备。
    if (bClearing || !Weapon || Main != LinkedMainInstance.Get() || Weapon != EquippedWeapon.Get()
        || Weapon->GetReloadSequence() != ReloadSequence || !Weapon->IsReloading() || !ActivePair.ArmsMontage) return;
    FAnimMontageInstance* Instance = Main->GetMontageInstanceForID(ReloadMontageInstanceID); // ID查询包含自然混出阶段，不会串到重播的同一个Montage资源。
    if (!Instance || Instance->Montage != ActivePair.ArmsMontage || !Instance->IsPlaying()) return;
    const float Progress = FMath::Clamp((GetWorld()->GetTimeSeconds()-ReloadStartTime)/FMath::Max(ReloadDuration,KINDA_SMALL_NUMBER),0.f,1.f); // 唯一游戏时钟终点。
    const float TargetPosition = Progress*ActivePair.ArmsMontage->GetPlayLength(); // 当前素材秒数，长度变化不改变技能时长。
    // UE5.4 UpdateAnimation顺序为PreUpdate→UpdateMontage→NativeUpdate→图求值。
    // ForcedNextTo使本次Montage推进直接走到共同终点，不再额外叠加首帧DeltaSeconds；保留正常播放、权重和Notify区间，不重启Montage。
    // Delta为0时引擎不推进，保持已对齐位置；实际暂停时World进度同样冻结。
    if (DeltaSeconds > 0.f) Instance->SetNextPositionWithEvents(TargetPosition);
    UpdateReloadPhase(Progress,true); // 输入若晚于本帧组件Tick，仍在枪求值前发布相同phase；声音游标不会重复触发。
}

void UDemoWeaponAnimationComponent::SampleReloadAnimationPhase(UDemoFPAnimInstance* Main)
{
    DEMO_LOG_TICK();
    check(IsInGameThread()); // NativeUpdate位于Montage已推进但图尚未并行求值的游戏线程边界。
    ADemoWeaponBase* Weapon = ReloadWeapon.Get(); // 当前动作弱引用，清理/销毁后不产生伪测量样本。
    if (bClearing || !Weapon || Main != LinkedMainInstance.Get() || Weapon != EquippedWeapon.Get()
        || Weapon->GetReloadSequence() != ReloadSequence || !ActivePair.ArmsMontage || !ActivePair.WeaponSequence) return;
    const FAnimMontageInstance* Instance = Main->GetMontageInstanceForID(ReloadMontageInstanceID); // 读取实际引擎实例，不能把目标phase当测量值。
    if (!Instance || Instance->Montage != ActivePair.ArmsMontage) return;
    PhaseAudit.ExpectedProgress = FMath::Clamp((GetWorld()->GetTimeSeconds()-ReloadStartTime)/FMath::Max(ReloadDuration,KINDA_SMALL_NUMBER),0.f,1.f);
    PhaseAudit.ArmsProgress = Instance->GetPosition()/FMath::Max(ActivePair.ArmsMontage->GetPlayLength(),KINDA_SMALL_NUMBER);
    PhaseAudit.WeaponProgress = Weapon->GetWeaponSkeletalMesh()->GetPosition()/FMath::Max(ActivePair.WeaponSequence->GetPlayLength(),KINDA_SMALL_NUMBER);
    const float ClockError = FMath::Max(FMath::Abs(PhaseAudit.ArmsProgress-PhaseAudit.ExpectedProgress),FMath::Abs(PhaseAudit.WeaponProgress-PhaseAudit.ExpectedProgress)); // 双网格分别相对游戏时钟的无单位偏差。
    PhaseAudit.LastErrorSeconds = FMath::Max(ClockError,FMath::Abs(PhaseAudit.ArmsProgress-PhaseAudit.WeaponProgress))*ReloadDuration;
    PhaseAudit.MaximumErrorSeconds = FMath::Max(PhaseAudit.MaximumErrorSeconds,PhaseAudit.LastErrorSeconds);
    ++PhaseAudit.SampleCount;
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FP_RELOAD_PHASE_SAMPLE seq=%d montageID=%d samples=%d expected=%.6f arms=%.6f gun=%.6f errorSeconds=%.6f"),
        ReloadSequence,ReloadMontageInstanceID,PhaseAudit.SampleCount,PhaseAudit.ExpectedProgress,PhaseAudit.ArmsProgress,PhaseAudit.WeaponProgress,PhaseAudit.LastErrorSeconds);
    if (PhaseAudit.LastErrorSeconds > .002f) UE_LOG(LogFPSDemo, Warning, TEXT("FP_RELOAD_PHASE_MISMATCH weapon=%s seq=%d errorSeconds=%.6f expected=%.6f arms=%.6f gun=%.6f"),
        *Weapon->GetName(),ReloadSequence,PhaseAudit.LastErrorSeconds,PhaseAudit.ExpectedProgress,PhaseAudit.ArmsProgress,PhaseAudit.WeaponProgress);
}

void UDemoWeaponAnimationComponent::UpdateReloadPhase(float Progress, bool bEmitSounds)
{
    DEMO_LOG_TICK();
    ADemoWeaponBase* Weapon = ReloadWeapon.Get(); // 每帧重新验证弱引用，不让旧动作接触新装备。
    if (!Weapon || Weapon != EquippedWeapon.Get()) return;
    ReloadProgress = FMath::Clamp(Progress, 0.f, 1.f);
    if (ActivePair.WeaponSequence) Weapon->GetWeaponSkeletalMesh()->SetPosition(ReloadProgress * ActivePair.WeaponSequence->GetPlayLength(), false);
    SetMagazineInHand(ReloadProgress >= ActivePair.MagazineDetachPhase && ReloadProgress < ActivePair.MagazineAttachPhase);
    while (ActivePair.SoundEvents.IsValidIndex(NextSoundEvent) && ActivePair.SoundEvents[NextSoundEvent].NormalizedTime <= ReloadProgress + KINDA_SMALL_NUMBER)
    {
        const FDemoReloadSoundEvent& Event = ActivePair.SoundEvents[NextSoundEvent++]; // 单调游标先消费，声音播放失败也不下帧重试/重复。
        if (!bEmitSounds || !Event.Sound) continue;
        UAudioComponent* Audio = UGameplayStatics::SpawnSoundAttached(Event.Sound, Weapon->GetWeaponSkeletalMesh(), NAME_None, FVector::ZeroVector,
            EAttachLocation::KeepRelativeOffset, true, Event.VolumeMultiplier, Event.PitchMultiplier, 0.f, nullptr, nullptr, false); // Pawn拥有的短机械声，关闭AutoDestroy由统一清理管理。
        if (Audio) ReloadAudio.Add(Audio);
        UE_LOG(LogFPSDemo, Log, TEXT("FP_RELOAD_SOUND weapon=%s seq=%d event=%d phase=%.3f sound=%s component=%s"), *Weapon->GetName(), ReloadSequence, NextSoundEvent-1, Event.NormalizedTime, *GetNameSafe(Event.Sound), *GetNameSafe(Audio));
    }
}

void UDemoWeaponAnimationComponent::SetMagazineInHand(bool bShow)
{
    DEMO_LOG_TICK();
    ADemoWeaponBase* Weapon = ReloadWeapon.Get(); // 当前动作借用枪，取消期间仍有效再恢复。
    ADemoCharacter* Character = GetCharacter(); // 手臂组件Owner。
    const FDemoWeaponAnimationSet* Set = GetAnimationSet(); // 只读当前装备资源。
    bShow = bShow && Weapon && Character && Set && Set->MagazinePresentationMesh;
    if (bShow == bMagazineInHand) return;
    bMagazineInHand = bShow;
    if (bShow)
    {
        if (!HandMagazine)
        {
            HandMagazine = NewObject<UStaticMeshComponent>(Character, TEXT("ReloadHandMagazine"));
            HandMagazine->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            HandMagazine->SetOnlyOwnerSee(true);
            HandMagazine->CastShadow = false;
            HandMagazine->RegisterComponent();
        }
        HandMagazine->SetStaticMesh(Set->MagazinePresentationMesh);
        HandMagazine->AttachToComponent(Character->GetMesh1P(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Set->MagazineHandSocket);
        HandMagazine->SetRelativeTransform(Set->MagazineHandOffset);
        HandMagazine->SetHiddenInGame(false);
        Weapon->GetWeaponSkeletalMesh()->HideBoneByName(TEXT("magazine"), PBO_None);
    }
    else
    {
        if (HandMagazine) HandMagazine->SetHiddenInGame(true);
        if (Weapon) Weapon->GetWeaponSkeletalMesh()->UnHideBoneByName(TEXT("magazine"));
    }
    UE_LOG(LogFPSDemo, Log, TEXT("FP_RELOAD_MAGAZINE seq=%d inHand=%d"), ReloadSequence, bShow);
}

void UDemoWeaponAnimationComponent::EndReloadPresentation(ADemoWeaponBase* Weapon, int32 Sequence, bool bCompleted)
{
    DEMO_LOG_CALL();
    if (Weapon != ReloadWeapon.Get() || Sequence != ReloadSequence)
    { UE_LOG(LogFPSDemo, Verbose, TEXT("FP_RELOAD_END_IGNORED stale weapon=%s seq=%d current=%d"), *GetNameSafe(Weapon), Sequence, ReloadSequence); return; }
    if (bCompleted) UpdateReloadPhase(1.f, true);
    UE_LOG(LogFPSDemo, Log, TEXT("FP_RELOAD_END weapon=%s seq=%d completed=%d phase=%.3f"), *GetNameSafe(Weapon), Sequence, bCompleted, ReloadProgress);
    ClearReloadPresentation();
}

void UDemoWeaponAnimationComponent::ClearReloadPresentation()
{
    DEMO_LOG_CALL();
    if (bClearing) return;
    bClearing = true;
    ADemoWeaponBase* Weapon = ReloadWeapon.Get(); // 清理顺序先恢复旧枪，再失效弱引用，不误操作新枪。
    if (Weapon) UE_LOG(LogFPSDemo, Log, TEXT("FP_RELOAD_PHASE_AUDIT weapon=%s seq=%d montageID=%d samples=%d maxErrorSeconds=%.6f"),
        *Weapon->GetName(),ReloadSequence,ReloadMontageInstanceID,PhaseAudit.SampleCount,PhaseAudit.MaximumErrorSeconds);
    SetMagazineInHand(false);
    for (UAudioComponent* Audio : ReloadAudio) // 每个组件仅本次借用，Stop阻止取消后的旧机械尾音。
        if (IsValid(Audio)) { Audio->Stop(); Audio->DestroyComponent(); }
    ReloadAudio.Empty();
    ADemoCharacter* Character = GetCharacter(); // World卸载时可空，不要求ASC仍存在。
    if (Character && Character->GetMesh1P()->GetAnimInstance() && ActivePair.ArmsMontage)
        Character->GetMesh1P()->GetAnimInstance()->Montage_Stop(.06f, ActivePair.ArmsMontage);
    if (Weapon)
    {
        // Empty手枪首帧是后锁套筒，不能取0帧当待机；无Sequence时SingleNode按真实参考姿势求值。
        Weapon->GetWeaponSkeletalMesh()->SetAnimation(nullptr);
        Weapon->GetWeaponSkeletalMesh()->UnHideBoneByName(TEXT("magazine"));
    }
    ReloadWeapon.Reset();
    ReloadAbility.Reset();
    ActivePair = FDemoReloadAnimationPair();
    ReloadProgress = 0.f;
    ReloadDuration = 0.f;
    ReloadMontageInstanceID = INDEX_NONE;
    NextSoundEvent = 0;
    bEmptyReload = false;
    if (EquippedWeapon.IsValid()) RestoreWeaponPose(EquippedWeapon.Get());
    bClearing = false;
}

void UDemoWeaponAnimationComponent::OnReloadMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, int32 ExpectedSequence, int32 ExpectedInstanceID)
{
    DEMO_LOG_CALL();
    if (bClearing || Montage != ActivePair.ArmsMontage || !bInterrupted || !ReloadWeapon.IsValid()
        || ExpectedSequence != ReloadSequence || ExpectedInstanceID != ReloadMontageInstanceID) return;
    UE_LOG(LogFPSDemo, Warning, TEXT("FP_RELOAD_INTERRUPTED weapon=%s seq=%d"), *GetNameSafe(ReloadWeapon.Get()), ReloadSequence);
    ADemoCharacter* Character = GetCharacter(); // 同步GAS取消，技能EndAbility将调用本组件统一清理。
    if (Character && Character->GetDemoASC() && ReloadAbility.IsValid()) Character->GetDemoASC()->CancelDemoAbility(UDemoReloadAbility::StaticClass());
    else ClearReloadPresentation();
}

void UDemoWeaponAnimationComponent::RestoreWeaponPose(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 仅当前装备可以修改共享手臂。
    if (!Character || Weapon != EquippedWeapon.Get() || ReloadWeapon.IsValid()) return;
    if (Weapon->Config.WeaponAnimLayerClass) Weapon->GetWeaponSkeletalMesh()->SetAnimation(nullptr); // 结束/取消后的枪机、弹匣始终回参考姿势。
    if (Weapon->Config.bHideSupportArm) Character->GetMesh1P()->HideBoneByName(TEXT("upperarm_l"), PBO_None);
    else Character->GetMesh1P()->UnHideBoneByName(TEXT("upperarm_l"));
}

void UDemoWeaponAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    DEMO_LOG_TICK();
    Super::TickComponent(DeltaTime, TickType, TickFunction);
    ADemoCharacter* Character = GetCharacter(); // 不在动画工作线程访问Pawn，所有状态发布前采样。
    if (!Character) return;
    if (ActivePair.ArmsMontage && !ReloadWeapon.IsValid())
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("FP_RELOAD_OWNER_LOST seq=%d; clearing presentation"), ReloadSequence);
        ClearReloadPresentation(); // 外部销毁/GC失效时也停止手臂动作，不等待一个永远不会到来的武器回调。
    }
    if (LinkedLayerClass && Character->GetMesh1P()->GetAnimInstance() != LinkedMainInstance.Get() && EquippedWeapon.IsValid())
    {
        // 实例重建是异常生命周期边界而非每帧资源加载；先取消原动作，迟到回调不能驱动新实例。
        UE_LOG(LogFPSDemo, Warning, TEXT("FP_MAIN_INSTANCE_REBUILT old=%s new=%s; cancel and relink"), *GetNameSafe(LinkedMainInstance.Get()), *GetNameSafe(Character->GetMesh1P()->GetAnimInstance()));
        Character->GetWeaponComponent()->CancelActions();
        if (!EquipWeapon(EquippedWeapon.Get())) { UE_LOG(LogFPSDemo, Error, TEXT("FP_MAIN_INSTANCE_REBUILD_FAILED")); return; }
    }
    if (ReloadWeapon.IsValid())
    {
        if (ReloadWeapon != EquippedWeapon || !ReloadWeapon->IsReloading() || ReloadWeapon->GetReloadSequence() != ReloadSequence) ClearReloadPresentation();
        else UpdateReloadPhase((GetWorld()->GetTimeSeconds()-ReloadStartTime)/FMath::Max(ReloadDuration, KINDA_SMALL_NUMBER), true);
    }
    const FDemoWeaponAnimationSet* Set = GetAnimationSet(); // 当前CDO配置，不在Tick加载任何资源。
    if (Set && bCachedArmsTransform)
    {
        const FDemoFPAnimationSnapshot State = GetSnapshot(); // 与动画图同一快照，单位厘米/秒与[0,1]。
        const float Time = GetWorld()->GetTimeSeconds(); // 游戏时间随暂停/时间缩放，视觉和GAS等待一致。
        const float Sway = FMath::Clamp(State.Speed/650.f, 0.f, 1.f)*Set->MoveSwayCentimeters*(State.bReloading ? .25f : 1.f); // 换弹减少轻摆便于读动作。
        const FVector Offset(0.f, FMath::Sin(Time*8.f)*Sway, FMath::Cos(Time*16.f)*Sway*.5f-(1.f-State.EquipBlendAlpha)*4.f); // 原始手臂空间的轻摆/短举枪过渡。
        Character->GetMesh1P()->SetRelativeLocation(BaseArmsLocation+Offset);
    }
}

FDemoFPAnimationSnapshot UDemoWeaponAnimationComponent::GetSnapshot() const
{
    DEMO_LOG_TICK();
    FDemoFPAnimationSnapshot State; // 纯值返回，调用方拥有拷贝，不持有World/ASC指针。
    const ADemoCharacter* Character = GetCharacter(); // 游戏线程NativeUpdate/组件Tick同步借用。
    const FDemoWeaponAnimationSet* Set = GetAnimationSet(); // 只读配置，允许尚未装备。
    if (Character)
    {
        State.Speed = Character->GetVelocity().Size2D();
        State.bAiming = Character->GetWeaponComponent()->GetScopeLevel() > 0;
    }
    State.bReloading = EquippedWeapon.IsValid() && EquippedWeapon->IsReloading(); // 表现播放失败仍以Gameplay武器装填为真值，避免错误恢复IK。
    State.ReloadProgress = ReloadProgress;
    State.bEmptyReload = State.bReloading && EquippedWeapon.IsValid() && EquippedWeapon->IsEmptyReload();
    State.EquipBlendAlpha = Set && Set->EquipBlendSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp((GetWorld()->GetTimeSeconds()-EquipStartTime)/Set->EquipBlendSeconds, 0.f, 1.f) : 1.f;
    return State;
}

TSubclassOf<UDemoWeaponLayerAnimInstance> UDemoWeaponAnimationComponent::GetLinkedLayerClass() const { DEMO_LOG_TICK(); return LinkedLayerClass; }
float UDemoWeaponAnimationComponent::GetReloadProgress() const { DEMO_LOG_TICK(); return ReloadProgress; }
bool UDemoWeaponAnimationComponent::IsReloadPresentationActive() const { DEMO_LOG_TICK(); return ReloadWeapon.IsValid(); }
bool UDemoWeaponAnimationComponent::IsEmptyReload() const { DEMO_LOG_TICK(); return bEmptyReload; }
int32 UDemoWeaponAnimationComponent::GetReloadSequence() const { DEMO_LOG_TICK(); return ReloadSequence; }
FDemoReloadPhaseAudit UDemoWeaponAnimationComponent::GetReloadPhaseAudit() const { DEMO_LOG_TICK(); return PhaseAudit; }

void UDemoWeaponAnimationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    ClearReloadPresentation();
    if (HandMagazine) { HandMagazine->DestroyComponent(); HandMagazine = nullptr; }
    EquippedWeapon.Reset();
    LinkedMainInstance.Reset();
    LinkedLayerClass = nullptr;
    Super::EndPlay(EndPlayReason);
}
