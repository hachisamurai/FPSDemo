// 职责：B09蓄势贯杀完整运行策略实现。依赖原BuildRuntime、WeaponComponent开镜状态与TargetState公开标定，无每帧Tick或第二ASC。
#include "GAS/Talents/Builds/DemoMarksmanBuildRuntime.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "GAS/Talents/RunBuildTargetState.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "AI/DemoEnemy.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"

void UDemoMarksmanBuildRuntime::OnInitialized()
{
    DEMO_LOG_CALL();
    Focus = 1.f; // 开战首枪天然满蓄，随后每次开火归零按停火/开镜/拉栓节奏回充。
    LastFocusTime = Now();
    EnsureMaintenance();
}

void UDemoMarksmanBuildRuntime::Handle(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK();
    if (!Active() || Selection().CoreId != TEXT("B09")) return;
    EnsureMaintenance();
    AdvanceFocus();
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>();
    const FDemoRunBuildSnapshot Choice = Selection();

    if (Event.Action == EDemoRunBuildAction::PreviewShot)
    {
        if (Focus >= 1.f - KINDA_SMALL_NUMBER)
        {
            Event.Shot.bMarksmanCharged = true;
            Event.Shot.MarksmanBaseBonus = Settings->MarksmanBaseBonus;
            Event.Shot.MarksmanExtraBonus = Settings->MarksmanBonusExtra;
        }
        return;
    }
    if (Event.Action == EDemoRunBuildAction::CommitShot)
    {
        // 开火即重置蓄势，迫使高射速连射无法持续满蓄，而拉栓狙1.33s间隔天然回满。
        Focus = 0.f;
        bNextFastCharge = false;
        LastFocusTime = Now();
        PublishFocus(true);
        return;
    }
    if (Event.Action == EDemoRunBuildAction::DashStarted)
    {
        // B09-C幽灵步态：冲刺不中断蓄势；其余路线冲刺清空当前未发射蓄势。
        if (Choice.Branch7 != TEXT("B09-C"))
        {
            Focus = 0.f;
            LastFocusTime = Now();
            PublishFocus(true);
        }
        return;
    }
    if (Event.Action == EDemoRunBuildAction::BeforeHit)
    {
        if (Event.bSecondary || !Event.Enemy.IsValid() || !Event.Shot.bMarksmanCharged) return;
        float Bonus = Event.Shot.MarksmanBaseBonus;
        if (Event.bWeakPoint || Event.HitDistance >= Settings->MarksmanLongRangeCm) Bonus += Event.Shot.MarksmanExtraBonus;
        Event.Multiplier *= (1.f + Bonus);
        return;
    }
    if (Event.Action == EDemoRunBuildAction::AfterHit)
    {
        if (Event.bSecondary || !Event.Enemy.IsValid() || Event.Damage <= 0.f || !Event.ShotId.IsValid() || !Event.Shot.bMarksmanCharged) return;
        // 多弹丸满蓄枪只允许结算一次溢伤/裂痕/标定，避免散弹连发8次溢伤震波。
        if (ConsumedShots.Contains(Event.ShotId)) return;
        if (ConsumedShots.Num() >= 128) ConsumedShots.Reset();
        ConsumedShots.Add(Event.ShotId);

        ADemoEnemy* Enemy = Event.Enemy.Get();
        ADemoWeaponBase* Weapon = Event.Weapon.Get();
        const FVector Origin = !Event.HitLocation.ContainsNaN() && !Event.HitLocation.IsNearlyZero() ? Event.HitLocation : Enemy->GetActorLocation();
        if (Choice.Branch7 == TEXT("B09-C") && Event.bWeakPoint)
        {
            ApplyTemporaryMoveSpeed(Settings->MarksmanWeakMoveBonusC, Settings->MarksmanWeakMoveSecondsC, WeakMoveHandle);
        }

        if (Event.bKilled)
        {
            const float OverkillBudget = FMath::Clamp(FMath::Max(Event.OverkillDamage * Settings->MarksmanOverkillRatio, Settings->MarksmanOverkillMin),
                Settings->MarksmanOverkillMin, Settings->MarksmanOverkillCap);
            if (Choice.Branch3 == TEXT("B09-A"))
            {
                TArray<ADemoEnemy*> Splash;
                CollectVisibleEnemies(Origin, Settings->MarksmanOverkillRadiusA, Settings->MarksmanOverkillTargetsA, Splash, Enemy);
                if (Splash.Num() > 0)
                {
                    // 总溢伤预算严格守恒，按实际可见敌人数均分。
                    const float PerTarget = OverkillBudget / static_cast<float>(Splash.Num());
                    for (ADemoEnemy* Other : Splash) Damage(Other, PerTarget);
                }
                if (Active())
                {
                    Focus = FMath::Max(Focus, Settings->MarksmanKillRefundFocusA);
                    LastFocusTime = Now();
                    if (Weapon && Weapon->GetOwner() == Pawn()) Weapon->TransferReserveRounds(1);
                    PublishFocus(true);
                }
                UE_LOG(LogFPSDemo, Log, TEXT("BUILD_MARKSMAN_OVERKILL_SPLASH budget=%.2f targets=%d"), OverkillBudget, Splash.Num());
            }
            else
            {
                const int32 HitCount = Pulse(Origin, Settings->MarksmanOverkillRadius, OverkillBudget, 1, Enemy);
                UE_LOG(LogFPSDemo, Log, TEXT("BUILD_MARKSMAN_OVERKILL_CHAIN budget=%.2f hit=%d"), OverkillBudget, HitCount);
            }
            return;
        }

        // 目标存活：处理B09-B贯心裂痕与B09-D猎杀标定。
        if (Enemy->IsAlive())
        {
            if (Choice.Branch3 == TEXT("B09-B") && Event.bWeakPoint)
            {
                const float Time = Now();
                for (auto It = Rifts.CreateIterator(); It; ++It)
                    if (!It.Key().IsValid() || !It.Key()->IsAlive() || Time >= It.Value().ExpireTime) It.RemoveCurrent();
                FDemoMarksmanRiftState& Rift = Rifts.FindOrAdd(Enemy);
                Rift.Stacks = FMath::Clamp(Rift.Stacks + 1, 1, Settings->MarksmanRiftMaxStacksB);
                Rift.ExpireTime = Time + Settings->MarksmanRiftDurationB;
                const float RiftDamage = FMath::Min(Settings->MarksmanRiftDamageCapB, Event.Damage * (Settings->MarksmanRiftRatioPerStackB * Rift.Stacks));
                const float Applied = Damage(Enemy, RiftDamage);
                bNextFastCharge = true;
                UE_LOG(LogFPSDemo, Log, TEXT("BUILD_MARKSMAN_RIFT stacks=%d damage=%.2f applied=%.2f"), Rift.Stacks, RiftDamage, Applied);
            }
            if (Choice.Branch7 == TEXT("B09-D") && IsValid(Enemy) && Enemy->IsAlive())
            {
                if (UDemoRunBuildTargetState* State = UDemoRunBuildTargetState::Find(Enemy, true))
                    State->ApplyTimedVulnerability(Build(), Event.ShotId, Settings->MarksmanMarkSecondsD, Settings->MarksmanMarkBonusD, true);
            }
        }
        return;
    }
    if (Event.Action == EDemoRunBuildAction::MarkedTargetKilled)
    {
        if (Choice.Branch7 != TEXT("B09-D") || !Event.bSuccess) return;
        Focus = 1.f;
        LastFocusTime = Now();
        if (ADemoWeaponBase* ActiveWeapon = Pawn()->GetWeaponComponent() ? Pawn()->GetWeaponComponent()->GetActiveWeapon() : nullptr)
            ActiveWeapon->TransferReserveRounds(1);
        PublishFocus(true);
        UE_LOG(LogFPSDemo, Log, TEXT("BUILD_MARKSMAN_MARK_KILL_REFUND focus=1.0"));
    }
}

void UDemoMarksmanBuildRuntime::AdvanceFocus()
{
    DEMO_LOG_TICK();
    if (!Active()) return;
    const float Time = Now();
    if (LastFocusTime <= 0.f) { LastFocusTime = Time; return; }
    const float Delta = FMath::Max(0.f, Time - LastFocusTime);
    LastFocusTime = Time;
    if (Delta <= 0.f || Focus >= 1.f) return;
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>();
    const bool bScoped = Pawn()->GetWeaponComponent() && Pawn()->GetWeaponComponent()->GetScopeLevel() > 0;
    const float BaseRate = 1.f / FMath::Max(.1f, Settings->MarksmanChargeSeconds);
    const float ScopeScale = bScoped ? Settings->MarksmanScopedRate : 1.f;
    const float FastScale = bNextFastCharge ? (1.f + Settings->MarksmanRiftSpeedBonusB) : 1.f;
    Focus = FMath::Clamp(Focus + Delta * BaseRate * ScopeScale * FastScale, 0.f, 1.f);
}

void UDemoMarksmanBuildRuntime::EnsureMaintenance()
{
    DEMO_LOG_TICK();
    if (LastFocusTime <= 0.f) LastFocusTime = Now();
    if (GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(MaintenanceTimer))
    {
        GetWorld()->GetTimerManager().SetTimer(MaintenanceTimer, this, &UDemoMarksmanBuildRuntime::Maintain, 0.1f, true);
        PublishFocus(true);
    }
}

void UDemoMarksmanBuildRuntime::Maintain()
{
    DEMO_LOG_TICK();
    if (!Active())
    {
        if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer);
        return;
    }
    AdvanceFocus();
    PublishFocus(false);
}

void UDemoMarksmanBuildRuntime::PublishFocus(bool bForce)
{
    DEMO_LOG_TICK();
    if (!Active()) return;
    const int32 Tier = FMath::Clamp(FMath::FloorToInt(Focus * 10.f + 0.001f), 0, 10);
    const FName Action = Tier >= 10 ? FName(TEXT("Marksman.Ready")) : FName(TEXT("Marksman.Charging"));
    const bool bHalf = Tier >= 5;
    if (bForce || Tier != LastPublishedTier || Action != LastPublishedAction)
    {
        LastPublishedTier = Tier;
        LastPublishedAction = Action;
        Publish(Action, Tier, 10, 0.f, 0.f);
    }
    if (bHalf != bLastHalfFocused)
    {
        bLastHalfFocused = bHalf;
        if (Selection().Branch7 == TEXT("B09-C") && Pawn() && Pawn()->GetWeaponComponent() && Pawn()->GetWeaponComponent()->GetScopeLevel() > 0)
            Pawn()->RefreshMovementSpeed();
    }
}

void UDemoMarksmanBuildRuntime::ResetRuntime()
{
    DEMO_LOG_CALL();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer);
    ClearTemporaryMoveSpeed(WeakMoveHandle);
    ADemoCharacter* Character = Pawn();
    Focus = 0.f;
    LastFocusTime = 0.f;
    bNextFastCharge = false;
    bLastHalfFocused = false;
    LastPublishedTier = -1;
    LastPublishedAction = NAME_None;
    ConsumedShots.Reset();
    Rifts.Reset();
    Super::ResetRuntime();
    if (IsValid(Character) && Character->GetWeaponComponent() && Character->GetWeaponComponent()->GetScopeLevel() > 0)
        Character->RefreshMovementSpeed();
}
