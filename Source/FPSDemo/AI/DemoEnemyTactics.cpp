#include "AI/DemoEnemyTactics.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyNavigation.h"
#include "Characters/DemoCharacter.h"
#include "Debug/DemoLog.h"

bool FDemoEnemyTacticsSettings::IsValid() const
{
    UE_LOG(LogFPSDemo,Log,TEXT("%hs"),__FUNCTION__);
    return static_cast<uint8>(Role) <= static_cast<uint8>(EDemoEnemyRole::Flanker)
        && FMath::IsFinite(MinimumRange) && MinimumRange >= 250 && MinimumRange < PreferredRange
        && FMath::IsFinite(PreferredRange) && PreferredRange < MaximumRange
        && FMath::IsFinite(MaximumRange) && MaximumRange <= 2500
        && FMath::IsFinite(RepositionInterval) && RepositionInterval >= 1 && RepositionInterval <= 6
        && FMath::IsFinite(FlankRange) && FlankRange >= 300 && FlankRange <= 1500
        && FMath::IsFinite(PressureSpeedMultiplier) && PressureSpeedMultiplier >= 1 && PressureSpeedMultiplier <= 2
        && FMath::IsFinite(PredictionSeconds) && PredictionSeconds >= 0 && PredictionSeconds <= .6f
        && FMath::IsFinite(RageHealthFraction) && RageHealthFraction >= .1f && RageHealthFraction <= .9f
        && FMath::IsFinite(RageIntervalMultiplier) && RageIntervalMultiplier >= .5f && RageIntervalMultiplier <= 1
        && FMath::IsFinite(RageSpeedMultiplier) && RageSpeedMultiplier >= 1 && RageSpeedMultiplier <= 1.5f;
}
UDemoEnemyTactics::UDemoEnemyTactics() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; }
void UDemoEnemyTactics::Configure(const FDemoEnemyTacticsSettings& Settings, int32 Slot, bool bEnabled)
{
    DEMO_LOG_CALL();
    const bool bValid = Settings.IsValid(); // 直接C++调用也拒绝非法值，不传播NaN到路径/弹道。
    Config = bValid ? Settings : FDemoEnemyTacticsSettings();
    bActive = bEnabled && bValid && Config.bEnabled;
    if (!bValid) UE_LOG(LogFPSDemo,Warning,TEXT("AI_TACTICS invalid settings: legacy fallback"));
    FormationSlot = FMath::Max(0,Slot);
    Role = Config.Role == EDemoEnemyRole::Mixed ? static_cast<EDemoEnemyRole>(1+FormationSlot%3) : Config.Role;
    bRage=false; ResetMovement();
    UE_LOG(LogFPSDemo,Log,TEXT("AI_ROLE actor=%s role=%d slot=%d enabled=%d"),*GetNameSafe(GetOwner()),static_cast<int32>(Role),FormationSlot,bActive);
}
EDemoEnemyRole UDemoEnemyTactics::GetRole() const { DEMO_LOG_TICK(); return Role; }
bool UDemoEnemyTactics::IsEnraged() const { DEMO_LOG_TICK(); return bRage; }
void UDemoEnemyTactics::SetState(FName NewState)
{
    DEMO_LOG_TICK();
    if (State==NewState) return;
    State=NewState;
    UE_LOG(LogFPSDemo,Log,TEXT("AI_TACTIC actor=%s state=%s"),*GetNameSafe(GetOwner()),*State.ToString());
}
void UDemoEnemyTactics::ResetMovement()
{
    DEMO_LOG_TICK();
    NextDecision=0; Goal=FVector::ZeroVector; SetState(TEXT("Idle"));
}
bool UDemoEnemyTactics::UpdateRage(float Health,float Maximum)
{
    DEMO_LOG_CALL();
    const ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // Actor拥有组件，此同步调用内借用。
    if (!bActive || bRage || !Enemy || !Enemy->HasAuthority() || !Enemy->IsBoss() || Health<=0 || Maximum<=0 || Health/Maximum>Config.RageHealthFraction) return false;
    bRage=true;
    UE_LOG(LogFPSDemo,Log,TEXT("AI_BOSS_PHASE2 actor=%s health=%.1f max=%.1f intervalMultiplier=%.2f"),*Enemy->GetName(),Health,Maximum,Config.RageIntervalMultiplier);
    return true;
}
float UDemoEnemyTactics::OpeningDelay() const { DEMO_LOG_TICK(); return bActive ? (FormationSlot%7)*.17f : 0.f; }
float UDemoEnemyTactics::AttackInterval(float Base,float Minimum) const
{
    DEMO_LOG_TICK();
    return FMath::Max(Minimum,Base*(bRage ? Config.RageIntervalMultiplier : 1.f));
}
FVector UDemoEnemyTactics::AimPoint(const ADemoCharacter* Player,float ProjectileSpeed) const
{
    DEMO_LOG_TICK();
    if (!Player) return GetOwner()->GetActorLocation();
    const FVector Current=Player->GetActorLocation(); // 发射瞬间瞄准快照，不在弹体飞行后更新。
    if (!bActive || Role==EDemoEnemyRole::Chaser || Player->IsDashEvading()) return Current;
    const float Lead=FMath::Min(Config.PredictionSeconds,FVector::Dist(Current,GetOwner()->GetActorLocation())/FMath::Max(200.f,ProjectileSpeed)); // 秒，上限保留变向躲避窗口。
    const FVector Offset=(Player->GetVelocity()*Lead).GetClampedToMaxSize(180.f); // cm，避免跳跃/异常高速产生超远预判。
    return Current+Offset;
}
void UDemoEnemyTactics::Move(ADemoCharacter* Player,float Speed,float DeltaSeconds)
{
    DEMO_LOG_TICK();
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 当前权威敌人，函数不保存目标引用。
    UDemoEnemyNavigation* Nav=Enemy ? Enemy->FindComponentByClass<UDemoEnemyNavigation>() : nullptr; // Enemy强持有的移动执行层。
    if (!Enemy || !Nav || !Player || !Enemy->HasAuthority()) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("AI_TACTIC invalid movement context")); return; }
    if (!bActive || Enemy->IsBoss() || Role==EDemoEnemyRole::Chaser)
    {
        SetState(Enemy->IsBoss() ? TEXT("BossAdvance") : TEXT("Chase"));
        Nav->Follow(Player,Enemy->IsBoss()?700.f:130.f,Speed*(bRage?Config.RageSpeedMultiplier:(bActive&&!Enemy->IsBoss()?Config.PressureSpeedMultiplier:1.f)),DeltaSeconds);
        return;
    }
    const float Distance=FVector::Dist2D(Enemy->GetActorLocation(),Player->GetActorLocation()); // cm，战术距离与真实近战判定相互独立。
    if (!Nav->CanSee(Player) || Distance>Config.MaximumRange)
    {
        SetState(TEXT("SeekSight")); NextDecision=0;
        Nav->Follow(Player,Config.PreferredRange,Speed,DeltaSeconds); return;
    }
    const float Now=GetWorld()->GetTimeSeconds(); // World秒；暂停不会积攒战术移动。
    if (State==TEXT("FallbackChase") && Now<NextDecision)
    { Nav->Follow(Player,Config.PreferredRange,Speed,DeltaSeconds); return; } // 失败只按战术周期重试，避免两种路径每帧互相清除。
    if (Now>=NextDecision)
    {
        NextDecision=Now+Config.RepositionInterval;
        const FVector Away=(Enemy->GetActorLocation()-Player->GetActorLocation()).GetSafeNormal2D(); // 从玩家指向本敌人的径向。
        const float Side=(FormationSlot/3)%2==0 ? 1.f : -1.f; // 相邻编队左右分散，可复现且不读随机种子。
        const bool bRetreat=Role==EDemoEnemyRole::Ranged && Distance<Config.MinimumRange; // 回退只在近距离触发，理想距离作为退出目标。
        const float Angle=bRetreat?0.f:(Role==EDemoEnemyRole::Flanker?65.f:25.f)*Side; // 度，远程小幅侧移，侧翼较大幅度换角度。
        Goal=Player->GetNavAgentLocation()+Away.RotateAngleAxis(Angle,FVector::UpVector)*(Role==EDemoEnemyRole::Flanker?Config.FlankRange:Config.PreferredRange);
        SetState(bRetreat?TEXT("Retreat"):(Role==EDemoEnemyRole::Flanker?TEXT("Flank"):TEXT("Strafe")));
    }
    // 候选点必须完整可达且有射击视线；失败退回追击，不能在墙角强行横移。
    if (!Nav->MoveTo(Goal,Player,Speed*(Role==EDemoEnemyRole::Flanker?Config.PressureSpeedMultiplier:1.f),DeltaSeconds))
    {
        SetState(TEXT("FallbackChase")); NextDecision=Now+Config.RepositionInterval;
        Nav->Follow(Player,Config.PreferredRange,Speed,DeltaSeconds);
    }
}
