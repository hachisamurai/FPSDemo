// 职责：第一人称拉栓的服务器同相位播放、声音及清理；动作不扣弹也不改变GAS射击成本。
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Characters/DemoCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"

void UDemoWeaponAnimationComponent::SyncBoltCycle(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // Owner仅在游戏线程同步借用，远端不播放本人的手臂。
    if (!Weapon || !Character || !Character->IsLocallyControlled() || Weapon != EquippedWeapon.Get()
        || !Character->GetWeaponComponent()->IsEquipped(Weapon) || !Weapon->Config.bCycleBoltAfterShot || ReloadWeapon.IsValid()) return;
    const FDemoBoltCycleState State = Weapon->GetBoltCycleState(); // 成功射击冻结值，OnRep和本地射击共用。
    const float Remaining = Weapon->GetBoltCycleRemaining(); // 服务端截止时刻，网络迟到不会延长射击间隔。
    if (State.Sequence == 0 || Remaining <= KINDA_SMALL_NUMBER || (BoltWeapon == Weapon && BoltAudit.Sequence == State.Sequence)) return;
    ClearBoltCycle();
    const FDemoWeaponAnimationSet* Set = GetAnimationSet(); // 当前类CDO保活资源，随后复制正在使用的动作对。
    UAnimInstance* Main = Character->GetMesh1P()->GetAnimInstance(); // 固定主AnimBP，子类只提供资源配置。
    if (!Set || !Set->BoltCycle.ArmsMontage || !Set->BoltCycle.WeaponSequence || !Main)
    { UE_LOG(LogFPSDemo, Error, TEXT("FP_BOLT_REJECT missing paired assets/main")); return; }
    BoltPair = Set->BoltCycle;
    BoltWeapon = Weapon;
    BoltDuration = State.Duration;
    BoltLocalStart = GetWorld()->GetTimeSeconds() - (State.Duration - Remaining);
    BoltAudit = FDemoReloadPhaseAudit();
    BoltAudit.Sequence = State.Sequence;
    const float Progress = FMath::Clamp((GetWorld()->GetTimeSeconds()-BoltLocalStart)/BoltDuration, 0.f, 1.f); // 与PreUpdate使用相同浮点运算，避免首帧微小反向Seek触发UE节段起点结束。
    const float Played = Main->Montage_Play(BoltPair.ArmsMontage, BoltPair.ArmsMontage->GetPlayLength() / BoltDuration,
        EMontagePlayReturnType::MontageLength, Progress * BoltPair.ArmsMontage->GetPlayLength()); // 素材长度不成为第二套射速。
    FAnimMontageInstance* Instance = Played > 0.f ? Main->GetActiveInstanceForMontage(BoltPair.ArmsMontage) : nullptr; // 本次同步借用。
    if (!Instance) { UE_LOG(LogFPSDemo, Error, TEXT("FP_BOLT_REJECT montage failed")); ClearBoltCycle(); return; }
    BoltMontageID = Instance->GetInstanceID();
    BoltAudit.MontageInstanceID = BoltMontageID;
    USkeletalMeshComponent* Gun = Weapon->GetWeaponSkeletalMesh(); // 使用相同phase手动取样，不能独立推进枪机时钟。
    Gun->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Gun->SetAnimation(BoltPair.WeaponSequence);
    Gun->Stop();
    Gun->SetPosition(Progress * BoltPair.WeaponSequence->GetPlayLength(), false);
    BoltSoundIndex = 0;
    while (BoltPair.SoundEvents.IsValidIndex(BoltSoundIndex) && BoltPair.SoundEvents[BoltSoundIndex].NormalizedTime < Progress) ++BoltSoundIndex;
    UE_LOG(LogFPSDemo, Log, TEXT("FP_BOLT_START weapon=%s seq=%d duration=%.6f remaining=%.6f phase=%.6f"), *Weapon->GetName(), State.Sequence, BoltDuration, Remaining, Progress);
}

void UDemoWeaponAnimationComponent::PrepareBoltCycle(UDemoFPAnimInstance* Main, float DeltaSeconds)
{
    DEMO_LOG_TICK();
    ADemoWeaponBase* Weapon = BoltWeapon.Get(); // 弱对象每帧验证，不能操作已卸载武器。
    if (!BoltPair.ArmsMontage) return;
    if (!Weapon || Weapon != EquippedWeapon.Get() || Main != LinkedMainInstance.Get()
        || !GetCharacter() || !GetCharacter()->GetWeaponComponent()->IsEquipped(Weapon)) { ClearBoltCycle(); return; }
    const float Progress = FMath::Clamp((GetWorld()->GetTimeSeconds() - BoltLocalStart) / BoltDuration, 0.f, 1.f); // 唯一冻结游戏时钟。
    if (Progress >= 1.f) { ClearBoltCycle(); return; } // 下一次开火同一截止时刻已被Gameplay允许；末帧为完整握持参考姿势。
    FAnimMontageInstance* Instance = Main->GetMontageInstanceForID(BoltMontageID); // 自然淡出仍按ID找到同一动作。
    if (!Instance || !Instance->IsPlaying()) { UE_LOG(LogFPSDemo, Log, TEXT("FP_BOLT_INTERRUPTED seq=%d"), BoltAudit.Sequence); ClearBoltCycle(); return; }
    if (DeltaSeconds > 0.f) Instance->SetNextPositionWithEvents(FMath::Max(Instance->GetPosition(), Progress * BoltPair.ArmsMontage->GetPlayLength())); // 仅防浮点舍入产生的反向子步，不以此增加Gameplay时长。
    Weapon->GetWeaponSkeletalMesh()->SetPosition(Progress * BoltPair.WeaponSequence->GetPlayLength(), false);
    while (BoltPair.SoundEvents.IsValidIndex(BoltSoundIndex) && BoltPair.SoundEvents[BoltSoundIndex].NormalizedTime <= Progress)
    {
        const FDemoReloadSoundEvent& Event = BoltPair.SoundEvents[BoltSoundIndex++]; // 游标只前进，跳帧不会丢事件或重复。
        UAudioComponent* Audio = UGameplayStatics::SpawnSoundAttached(Event.Sound, Weapon->GetWeaponSkeletalMesh(), NAME_None, FVector::ZeroVector,
            EAttachLocation::KeepRelativeOffset, true, Event.VolumeMultiplier, Event.PitchMultiplier, 0.f, nullptr, nullptr, false); // 短声由本协调器销毁。
        if (Audio) BoltAudio.Add(Audio);
        UE_LOG(LogFPSDemo, Log, TEXT("FP_BOLT_SOUND seq=%d phase=%.4f sound=%s"), BoltAudit.Sequence, Event.NormalizedTime, *GetNameSafe(Event.Sound));
    }
}

void UDemoWeaponAnimationComponent::SampleBoltCycle(UDemoFPAnimInstance* Main)
{
    DEMO_LOG_TICK();
    if (!BoltWeapon.IsValid() || Main != LinkedMainInstance.Get() || !BoltPair.ArmsMontage) return;
    const FAnimMontageInstance* Instance = Main->GetMontageInstanceForID(BoltMontageID); // 读取实际引擎位置而非回填目标值。
    if (!Instance) return;
    BoltAudit.ExpectedProgress = FMath::Clamp((GetWorld()->GetTimeSeconds()-BoltLocalStart)/BoltDuration, 0.f, 1.f);
    BoltAudit.ArmsProgress = Instance->GetPosition()/BoltPair.ArmsMontage->GetPlayLength();
    BoltAudit.WeaponProgress = BoltWeapon->GetWeaponSkeletalMesh()->GetPosition()/BoltPair.WeaponSequence->GetPlayLength();
    BoltAudit.LastErrorSeconds = FMath::Max3(FMath::Abs(BoltAudit.ArmsProgress-BoltAudit.ExpectedProgress),
        FMath::Abs(BoltAudit.WeaponProgress-BoltAudit.ExpectedProgress), FMath::Abs(BoltAudit.ArmsProgress-BoltAudit.WeaponProgress))*BoltDuration;
    BoltAudit.MaximumErrorSeconds = FMath::Max(BoltAudit.MaximumErrorSeconds, BoltAudit.LastErrorSeconds);
    ++BoltAudit.SampleCount;
    if (BoltAudit.LastErrorSeconds > .002f) UE_LOG(LogFPSDemo, Warning, TEXT("FP_BOLT_PHASE_MISMATCH seq=%d seconds=%.6f"), BoltAudit.Sequence, BoltAudit.LastErrorSeconds);
}

void UDemoWeaponAnimationComponent::ClearBoltCycle()
{
    DEMO_LOG_CALL();
    if (!BoltPair.ArmsMontage) return;
    UE_LOG(LogFPSDemo, Log, TEXT("FP_BOLT_END seq=%d samples=%d maxErrorSeconds=%.6f"), BoltAudit.Sequence, BoltAudit.SampleCount, BoltAudit.MaximumErrorSeconds);
    if (LinkedMainInstance.IsValid()) LinkedMainInstance->Montage_Stop(.04f, BoltPair.ArmsMontage); // 不绑定补弹/技能委托，清理不影响射击成本。
    ADemoWeaponBase* FinishedWeapon=BoltWeapon.Get(); // 同步清理后恢复本枪明确待机Pose，不能保留原包非闭锁参考姿势。
    for (UAudioComponent* Audio : BoltAudio) // 每次动作组件统一释放，切枪不会留下旧武器声音。
        if (IsValid(Audio)) { Audio->Stop(); Audio->DestroyComponent(); }
    BoltAudio.Reset();
    BoltWeapon.Reset();
    BoltPair = FDemoReloadAnimationPair();
    BoltMontageID = INDEX_NONE;
    BoltSoundIndex = 0;
    BoltDuration = 0.f;
    if (FinishedWeapon) RestoreWeaponPose(FinishedWeapon);
}

FDemoReloadPhaseAudit UDemoWeaponAnimationComponent::GetBoltCycleAudit() const
{
    DEMO_LOG_TICK();
    return BoltAudit;
}
