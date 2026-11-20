// 职责：B08抵近爆膛近距多弹丸满压震爆与A续压/B阔口/C护幕/D震颤结算。依赖原BuildRuntime、Knockback与TargetState，无Tick或额外ASC。
#include "GAS/Talents/Builds/DemoCloseQuartersBuildRuntime.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "GAS/Talents/RunBuildTargetState.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyKnockbackComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "AbilitySystemComponent.h"
#include "Debug/DemoLog.h"

void UDemoCloseQuartersBuildRuntime::Handle(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK();
    if (!Active() || Selection().CoreId != TEXT("B08")) return;
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>(); // 服务器已预检集中配置。
    const float Time = Now();
    if (PressureStacks > 0 && Time >= PressureEndTime)
    {
        PressureStacks = 0; PressureEndTime = 0.f;
        PublishState();
    }
    if (Event.Action == EDemoRunBuildAction::PreviewShot)
    {
        if (Selection().Branch3 == TEXT("B08-A") && Time < TightSpreadEndTime)
        {
            Event.Shot.SpreadScale *= Settings->CloseTightSpreadScale;
            Event.Shot.bCloseQuartersEmpowered = true;
        }
        return;
    }
    if (Event.Action == EDemoRunBuildAction::CommitShot)
    {
        if (Event.Shot.bCloseQuartersEmpowered)
        {
            TightSpreadEndTime = 0.f;
            PublishState();
        }
        return;
    }
    if (Event.Action == EDemoRunBuildAction::AfterHit)
    {
        if (Event.bSecondary || !Event.Enemy.IsValid() || Event.Damage <= 0.f || !Event.ShotId.IsValid()
            || Event.HitDistance > Settings->CloseRangeCm) return;
        BoundPendingShots();
        FDemoCloseQuartersShot& Entry = PendingShots.FindOrAdd(Event.ShotId);
        if (Entry.Serial == 0)
        {
            Entry.Serial = Event.Shot.CommitSerial;
            Entry.Weapon = Event.Weapon;
            Entry.AimDirection = Event.Shot.AimDirection;
            Entry.PlannedProjectiles = FMath::Max(1, Event.Shot.PlannedProjectiles);
        }
        ++Entry.ClosePellets;
        Entry.CloseDamage += Event.Damage;
        if (!Entry.PrimaryEnemy.IsValid() || Event.bFirstEnemy)
        {
            Entry.PrimaryEnemy = Event.Enemy;
            Entry.PrimaryImpact = Event.HitLocation;
        }
        return;
    }
    if (Event.Action == EDemoRunBuildAction::ShotResolved)
    {
        FDemoCloseQuartersShot Entry;
        if (!PendingShots.RemoveAndCopyValue(Event.ShotId, Entry) || Entry.ClosePellets <= 0) return;
        if (Time >= PressureEndTime) PressureStacks = 0;
        // 散弹多弹丸按实际近距命中弹丸数直接积层（贴脸8丸即满），单发武器每枪固定+2层（贴脸4枪满）。
        const int32 Added = Entry.PlannedProjectiles > 1 ? Entry.ClosePellets : Settings->CloseSingleShotStacks;
        PressureStacks = FMath::Min(Settings->ClosePressureMax, PressureStacks + FMath::Max(1, Added));
        PressureEndTime = Time + Settings->ClosePressureDecaySeconds;
        UE_LOG(LogFPSDemo, Log, TEXT("BUILD_CLOSE_PRESSURE added=%d stacks=%d/%d pellets=%d"), Added, PressureStacks, Settings->ClosePressureMax, Entry.ClosePellets);
        if (PressureStacks >= Settings->ClosePressureMax)
        {
            PressureStacks = 0;
            PressureEndTime = 0.f;
            TriggerBurst(Entry, Event.ShotId);
        }
        PublishState();
    }
}

void UDemoCloseQuartersBuildRuntime::TriggerBurst(const FDemoCloseQuartersShot& Shot, const FGuid& ShotId)
{
    DEMO_LOG_CALL();
    if (!Active()) return;
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>();
    const FDemoRunBuildSnapshot Choice = Selection();
    const bool bWideBranch = Choice.Branch3 == TEXT("B08-B");
    const float Cap = bWideBranch ? Settings->CloseBurstCapB : Settings->CloseBurstCap;
    const float BaseBurst = FMath::Clamp(FMath::Max(Shot.CloseDamage * Settings->CloseBurstRatio, Settings->CloseBurstMinDamage), Settings->CloseBurstMinDamage, Cap);
    ADemoEnemy* Primary = Shot.PrimaryEnemy.Get();
    const FVector Center = !Shot.PrimaryImpact.ContainsNaN() && !Shot.PrimaryImpact.IsNearlyZero()
        ? Shot.PrimaryImpact
        : (IsValid(Primary) ? Primary->GetActorLocation() : Pawn()->GetActorLocation());
    const float Radius = bWideBranch ? Settings->CloseBurstRadiusB : Settings->CloseBurstRadius;
    const int32 TargetLimit = bWideBranch ? Settings->CloseBurstTargetsB : Settings->CloseBurstTargets;
    const FVector Direction = bWideBranch ? Shot.AimDirection.GetSafeNormal2D() : FVector::ZeroVector;
    const float ConeCos = bWideBranch ? Settings->CloseBurstConeCosB : -1.f;

    // 复用既有冰蓝/非元素范围表现Cue（无独立资产依赖，绝不施加冰冻/燃烧Tag）。
    if (IsValid(Primary) && Primary->GetAbilitySystemComponent())
    {
        FGameplayCueParameters Cue; Cue.Location = Center; Cue.RawMagnitude = Radius;
        Primary->GetAbilitySystemComponent()->ExecuteGameplayCue(DemoTalentEvents::FrostBurst, Cue);
    }

    int32 DamagedCount = 0;
    int32 KilledCount = 0;
    const FVector Origin = Pawn()->GetActorLocation();
    auto ApplyImpact = [&](ADemoEnemy* Enemy, bool bIsPrimary)
    {
        if (!Active() || !IsValid(Enemy) || !Enemy->IsAlive() || !Enemy->IsCombatSessionValid()) return;
        const float Requested = (bIsPrimary && Enemy->IsBoss()) ? BaseBurst * Settings->CloseBossMultiplier : BaseBurst;
        const float Applied = Damage(Enemy, Requested);
        if (Applied <= 0.f) return;
        ++DamagedCount;
        if (!IsValid(Enemy) || !Enemy->IsAlive()) { ++KilledCount; return; }
        // 存活小怪承受短距震退；Boss完全免击退（已在上方获得集中冲击倍率）。
        if (!Enemy->IsBoss() && Settings->CloseKnockbackDistance > 0.f)
        {
            if (UDemoEnemyKnockbackComponent* Knockback = Enemy->FindComponentByClass<UDemoEnemyKnockbackComponent>())
            {
                FVector PushDir = (Enemy->GetActorLocation() - Origin).GetSafeNormal2D();
                if (PushDir.IsNearlyZero()) PushDir = Shot.AimDirection.GetSafeNormal2D();
                if (!PushDir.IsNearlyZero()) Knockback->Apply(PushDir, Settings->CloseKnockbackDistance, Settings->CloseKnockbackSeconds);
            }
        }
        if (Choice.Branch7 == TEXT("B08-D"))
        {
            if (UDemoRunBuildTargetState* State = UDemoRunBuildTargetState::Find(Enemy, true))
                State->ApplyTimedVulnerability(Build(), ShotId, Settings->CloseTremorSeconds, Settings->CloseTremorBonus, false);
        }
    };

    if (IsValid(Primary) && Primary->IsAlive()) ApplyImpact(Primary, true);
    TArray<ADemoEnemy*> SplashTargets;
    CollectVisibleEnemies(Center, Radius, FMath::Max(1, TargetLimit - (DamagedCount > 0 ? 1 : 0)), SplashTargets, Primary, Direction, ConeCos);
    for (ADemoEnemy* Enemy : SplashTargets) ApplyImpact(Enemy, false);

    const float Time = Now();
    if (!Active()) return;
    if (Choice.Branch3 == TEXT("B08-A"))
    {
        ADemoWeaponBase* Weapon = Shot.Weapon.Get();
        if (Weapon && Weapon->GetOwner() == Pawn() && Weapon->GetAmmo() <= 1 && Time >= NextReloadTime)
        {
            const int32 Moved = Weapon->TransferReserveRounds(1);
            if (Moved > 0) NextReloadTime = Time + Settings->CloseReloadCooldown;
            UE_LOG(LogFPSDemo, Log, TEXT("BUILD_CLOSE_RELOAD moved=%d ammo=%d"), Moved, Weapon->GetAmmo());
        }
        TightSpreadEndTime = Time + Settings->CloseTightSpreadSeconds;
    }
    else if (bWideBranch && KilledCount > 0)
    {
        ApplyTemporaryMoveSpeed(Settings->CloseKillMoveBonus, Settings->CloseKillMoveSeconds, KillMoveHandle);
    }
    if (Choice.Branch7 == TEXT("B08-C") && DamagedCount > 0 && Time >= NextGuardTime)
    {
        const float Healed = Heal(Pawn(), Settings->CloseGuardHeal);
        if (Healed > 0.f) NextGuardTime = Time + Settings->CloseGuardCooldown;
        UE_LOG(LogFPSDemo, Log, TEXT("BUILD_CLOSE_GUARD healed=%.2f"), Healed);
    }
    UE_LOG(LogFPSDemo, Log, TEXT("BUILD_CLOSE_BURST base=%.2f damaged=%d killed=%d wide=%d"), BaseBurst, DamagedCount, KilledCount, bWideBranch);
}

void UDemoCloseQuartersBuildRuntime::BoundPendingShots()
{
    DEMO_LOG_TICK();
    if (PendingShots.Num() < 128) return;
    FGuid Oldest; int64 OldestSerial = MAX_int64;
    for (const TPair<FGuid, FDemoCloseQuartersShot>& Pair : PendingShots)
        if (Pair.Value.Serial < OldestSerial) { OldestSerial = Pair.Value.Serial; Oldest = Pair.Key; }
    PendingShots.Remove(Oldest);
    UE_LOG(LogFPSDemo, Warning, TEXT("BUILD_CLOSE_PENDING_LIMIT evicted oldest; limit=128"));
}

void UDemoCloseQuartersBuildRuntime::PublishState()
{
    DEMO_LOG_TICK();
    if (!Active()) return;
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>();
    const float Time = Now();
    if (Selection().Branch3 == TEXT("B08-A") && Time < TightSpreadEndTime)
        Publish(TEXT("Close.Tight"), PressureStacks, Settings->ClosePressureMax, TightSpreadEndTime, NextReloadTime);
    else if (PressureStacks > 0 && Time < PressureEndTime)
        Publish(TEXT("Close.Pressure"), PressureStacks, Settings->ClosePressureMax, PressureEndTime, NextReloadTime);
    else
        Publish(TEXT("Close.Idle"), 0, Settings->ClosePressureMax, 0.f, FMath::Max(NextReloadTime, NextGuardTime));
}

void UDemoCloseQuartersBuildRuntime::ResetRuntime()
{
    DEMO_LOG_CALL();
    ClearTemporaryMoveSpeed(KillMoveHandle);
    PendingShots.Reset();
    PressureStacks = 0;
    PressureEndTime = 0.f;
    TightSpreadEndTime = 0.f;
    NextReloadTime = 0.f;
    NextGuardTime = 0.f;
    Super::ResetRuntime();
}
