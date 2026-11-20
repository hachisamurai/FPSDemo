// 职责：B07固定损血回收、A近战/B弱点兑现、C实际冲刺保留、D守恒共享。使用原BuildRuntime权威门禁和Health GE；无Tick、Timer、存档或额外ASC。
#include "GAS/Talents/Builds/DemoCounterBuildRuntime.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

void UDemoCounterBuildRuntime::Handle(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK();
    if (!Active() || Selection().CoreId != TEXT("B07"))
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("COUNTER_REJECT inactive/avatar/session/core"));
        ClearCounter(); // 原GA仍负责最终Reset；不能在普通拒绝事件中撤掉本策略的Owner。
        return;
    }
    RefreshCounter();
    switch (Event.Action)
    {
    case EDemoRunBuildAction::PlayerDamaged:
        if (!IsCurrentImpact(Event.SourceEnemy.Get()))
        { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("COUNTER_DAMAGE_REJECT enemy/session")); return; }
        OpenCounter(Event.Damage);
        return;
    case EDemoRunBuildAction::DashStarted:
        BeginCounterDash(Event.Duration);
        return;
    case EDemoRunBuildAction::DashFinished:
        FinishCounterDash(Event.Distance);
        return;
    case EDemoRunBuildAction::Melee:
        if (Selection().Branch3 != TEXT("B07-B") && IsCurrentImpact(Event.Enemy.Get()))
            ConsumeCounter(Event.ActualDamage, false);
        else UE_LOG(LogFPSDemo, VeryVerbose, TEXT("COUNTER_MELEE_REJECT route/target/session"));
        return;
    case EDemoRunBuildAction::AfterHit:
    {
        const bool bValidWeakTarget = Event.Shot.PlannedProjectiles > 1 ? Event.bSameFirstEnemy : Event.bFirstEnemy; // 散弹任一弱点弹丸命中首敌亦可兑现，不限于第一颗碰撞弹丸。
        if (Selection().Branch3 == TEXT("B07-B") && Event.Shot.CoreId == TEXT("B07")
            && Event.bWeakPoint && bValidWeakTarget && !Event.bSecondary && Event.ShotId.IsValid()
            && IsCurrentImpact(Event.Enemy.Get()))
            ConsumeCounter(Event.Damage, true);
        else UE_LOG(LogFPSDemo, VeryVerbose, TEXT("COUNTER_SHOT_REJECT route/weakpoint/first/secondary/target"));
        return;
    }
    default:
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("COUNTER_EVENT_IGNORED action=%d"), static_cast<int32>(Event.Action));
        return;
    }
}

void UDemoCounterBuildRuntime::ResetRuntime()
{
    DEMO_LOG_CALL();
    ClearCounter();
    NextCounterTime = 0.f;
    Super::ResetRuntime();
}

void UDemoCounterBuildRuntime::RefreshCounter()
{
    DEMO_LOG_TICK();
    const float Time = Now(); // 同World游戏秒；暂停不消耗，与根冲刺完成Timer使用相同时钟。
    if (RecoverableBudget > 0.f && Time >= CounterEndTime && (!bDashPending || Time > DashResolveDeadline))
    {
        UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_EXPIRED budget=%.2f deadline=%.3f now=%.3f"), RecoverableBudget, CounterEndTime, Time);
        ClearCounter();
        PublishCounter();
    }
}

void UDemoCounterBuildRuntime::OpenCounter(float Loss)
{
    DEMO_LOG_TICK();
    if (!FMath::IsFinite(Loss) || Loss <= 0.f || RecoverableBudget > 0.f || Now() < NextCounterTime)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("COUNTER_OPEN_REJECT damage=%.2f existingBudget=%.2f next=%.3f"), Loss, RecoverableBudget, NextCounterTime);
        return;
    }
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>(); // 本栈只读项目配置，不修改CDO或受伤来源。
    if (!FMath::IsFinite(Settings->CounterWindow) || !FMath::IsFinite(Settings->CounterCooldown)
        || !FMath::IsFinite(Settings->CounterLossRatio) || !FMath::IsFinite(Settings->CounterBudgetCap)
        || Settings->CounterWindow <= 0.f || Settings->CounterCooldown < 0.f || Settings->CounterBudgetCap <= 0.f)
    { UE_LOG(LogFPSDemo, Warning, TEXT("COUNTER_OPEN_REJECT invalid settings")); return; }
    // 即使作者错误配置更高比例，仍遵守本核心最多回收本次实际失血一半的公平预算。
    RecoverableBudget = FMath::Min(Loss * FMath::Clamp(Settings->CounterLossRatio, 0.f, 0.5f), Settings->CounterBudgetCap);
    if (RecoverableBudget <= 0.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_OPEN_REJECT zero configured budget")); return; }
    CounterEndTime = Now() + Settings->CounterWindow;
    NextCounterTime = Now() + Settings->CounterCooldown;
    DashResolveDeadline = 0.f;
    bDashClaimed = false;
    bDashPending = false;
    UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_OPEN loss=%.2f budget=%.2f windowEnd=%.3f cooldownEnd=%.3f"), Loss, RecoverableBudget, CounterEndTime, NextCounterTime);
    PublishCounter();
}

void UDemoCounterBuildRuntime::BeginCounterDash(float Duration)
{
    DEMO_LOG_CALL();
    if (Selection().Branch7 != TEXT("B07-C") || RecoverableBudget <= 0.f || Now() >= CounterEndTime
        || bDashClaimed || !FMath::IsFinite(Duration) || Duration <= 0.f || Duration > 3.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_DASH_REJECT branch/window/claimed/duration")); return; }
    bDashClaimed = true;
    bDashPending = true;
    DashResolveDeadline = Now() + Duration + 0.25f; // 根单次Timer正常在预计结束验证；有限调度容错不额外授予可用窗口。
    UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_DASH_PENDING originalEnd=%.3f resolveBy=%.3f"), CounterEndTime, DashResolveDeadline);
    PublishCounter();
}

void UDemoCounterBuildRuntime::FinishCounterDash(float Distance)
{
    DEMO_LOG_CALL();
    if (!bDashPending || RecoverableBudget <= 0.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_DASH_FINISH_REJECT no original opportunity")); return; }
    bDashPending = false;
    DashResolveDeadline = 0.f;
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>(); // 根已把无效/不足真实位移归零；本策略不再推测位移是否成功。
    if (FMath::IsFinite(Distance) && Distance > 0.f && FMath::IsFinite(Settings->CounterDashGrace) && Settings->CounterDashGrace > 0.f)
    {
        CounterEndTime = FMath::Max(CounterEndTime, Now() + Settings->CounterDashGrace);
        UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_DASH_CONFIRMED distance=%.2f windowEnd=%.3f"), Distance, CounterEndTime);
    }
    else UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_DASH_FAILED distance=%.2f originalEnd=%.3f"), Distance, CounterEndTime);
    RefreshCounter(); // 失败时沿用原截止，已经到期则销毁原预算，不给予任何宽限。
    PublishCounter();
}

bool UDemoCounterBuildRuntime::IsCurrentImpact(const ADemoEnemy* Enemy) const
{
    DEMO_LOG_TICK();
    return IsValid(Enemy) && Enemy->GetWorld() == GetWorld() && Enemy->IsCombatSessionValid();
    // 根入口已经证明本次真实扣血；不能用命中后IsAlive筛掉合法致死打击，也不能借此复活玩家。
}

void UDemoCounterBuildRuntime::ConsumeCounter(float ActualDamage, bool bWeakPointRoute)
{
    DEMO_LOG_TICK();
    if (!Active() || RecoverableBudget <= 0.f || Now() >= CounterEndTime || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.f)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("COUNTER_CONSUME_REJECT active/window/actualDamage")); return; }
    const FDemoRunBuildSnapshot Choice = Selection(); // 冻结本次分支，后续Health GE同步回调不能改变分配规则。
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>(); // 配置只读，同步结算前把实际预算算为值。
    const float Ratio = bWeakPointRoute ? Settings->CounterWeakRatio
        : Choice.Branch3 == TEXT("B07-A") ? Settings->CounterHeavyRatio : Settings->CounterMeleeRatio; // A只提高真实近战伤害兑现比例，不突破损失一半/Cap。
    if (!FMath::IsFinite(Ratio) || Ratio <= 0.f)
    { UE_LOG(LogFPSDemo, Warning, TEXT("COUNTER_CONSUME_REJECT invalid return ratio")); return; }
    const float Total = FMath::Min(RecoverableBudget, ActualDamage * Ratio); // 不先按本人缺血裁总量；本人满血时D仍可把原有限份额给队友。
    if (!FMath::IsFinite(Total) || Total <= 0.f)
    { UE_LOG(LogFPSDemo, Warning, TEXT("COUNTER_CONSUME_REJECT invalid finite total")); return; }
    ADemoCharacter* ShareTarget = Choice.Branch7 == TEXT("B07-D") ? FindShareTarget(Settings->CounterShareRadius) : nullptr; // 只借用本次同步栈；无队友则完整预算归本人。
    const float ShareRatio = ShareTarget && FMath::IsFinite(Settings->CounterShareRatio)
        ? FMath::Clamp(Settings->CounterShareRatio, 0.f, 1.f) : 0.f; // 两份严格由同一Total分割，不能按队员数量复制预算。
    const float TeamBudget = Total * ShareRatio; // 分配前冻结，不按实际治疗结果重算或把未用份额补发。
    const float SelfBudget = Total - TeamBudget; // GE会各自钳制缺失HP，未使用预算作废。
    ClearCounter(); // 必须先消费再提交Health GE，属性/其他GAS回调重入不得二次领取同一受伤凭据。
    const float SelfHealed = Heal(Pawn(), SelfBudget); // 使用原Avatar与原ASC，只对存活玩家执行。
    const float TeamHealed = ShareTarget && TeamBudget > 0.f ? Heal(ShareTarget, TeamBudget) : 0.f; // 本人治疗引发状态变化时，基类Active重新门禁。
    UE_LOG(LogFPSDemo, Log, TEXT("COUNTER_CONSUMED route=%s total=%.2f self=%.2f team=%.2f teammate=%s cooldownEnd=%.3f"),
        bWeakPointRoute ? TEXT("weakpoint") : TEXT("melee"), Total, SelfHealed, TeamHealed, *GetNameSafe(ShareTarget), NextCounterTime);
    PublishCounter();
}

ADemoCharacter* UDemoCounterBuildRuntime::FindShareTarget(float Radius) const
{
    DEMO_LOG_TICK();
    if (!Active() || !FMath::IsFinite(Radius) || Radius <= 0.f) return nullptr;
    const AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 只借用当前World权威固定队伍；不扫描任意Pawn或Player0。
    const UDemoCoopRosterComponent* Roster = Mode ? Mode->GetCoopRoster() : nullptr; // 服务归GM，策略不保活、不写队伍身份。
    if (!Roster) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("COUNTER_SHARE_REJECT no roster")); return nullptr; }
    ADemoCharacter* Selected = nullptr; // 本栈的唯一最低生命比例候选，返回后只用于立即Health GE。
    float LowestRatio = 1.f; // 满血成员不消费他人的共享份额，找不到缺血队友则正常本人领取。
    for (ADemoCharacter* Candidate : Roster->GetPlayers(true))
    {
        const UDemoAttributeSet* Attributes = IsValid(Candidate) ? Candidate->GetDemoAttributes() : nullptr; // 固定队伍可能在同步回调中撤Avatar，逐项重验。
        UAbilitySystemComponent* CandidateASC = IsValid(Candidate) ? Candidate->GetAbilitySystemComponent() : nullptr; // 同Avatar才可领取，PS不能给旧Pawn治疗。
        if (!IsValid(Candidate) || Candidate == Pawn() || Candidate->GetWorld() != GetWorld() || !Candidate->HasAuthority()
            || Candidate->IsCoopEliminated() || !Attributes || !CandidateASC || CandidateASC->GetAvatarActor() != Candidate
            || FVector::DistSquared(Pawn()->GetActorLocation(), Candidate->GetActorLocation()) > FMath::Square(Radius)) continue;
        const float Health = Attributes->GetHealth(); // 当前真实HP，不能使用客户端界面或永久MaxHealth账本。
        const float Maximum = Attributes->GetMaxHealth(); // GAS当前上限，分享目标按比例而非绝对缺血排序。
        if (!FMath::IsFinite(Health) || !FMath::IsFinite(Maximum) || Health <= 0.f || Maximum <= 0.f || Health >= Maximum) continue;
        const float HealthRatio = Health / Maximum; // 同时存活且缺血，比例应在0..1。
        if (!Selected || HealthRatio < LowestRatio || (HealthRatio == LowestRatio && Candidate->GetUniqueID() < Selected->GetUniqueID()))
        { Selected = Candidate; LowestRatio = HealthRatio; }
    }
    return Selected;
}

void UDemoCounterBuildRuntime::ClearCounter()
{
    DEMO_LOG_TICK();
    RecoverableBudget = 0.f;
    CounterEndTime = 0.f;
    DashResolveDeadline = 0.f;
    bDashClaimed = false;
    bDashPending = false;
}

void UDemoCounterBuildRuntime::PublishCounter()
{
    DEMO_LOG_TICK();
    const UDemoExpandedBuildSettings* Settings = GetDefault<UDemoExpandedBuildSettings>(); // 非法配置时也安全发布0容量，不制造整数溢出。
    const int32 Capacity = FMath::IsFinite(Settings->CounterBudgetCap) ? FMath::CeilToInt(FMath::Clamp(Settings->CounterBudgetCap, 0.f, 100000.f)) : 0; // HUD单位HP，向上取整仅展示。
    const int32 Stacks = FMath::CeilToInt(FMath::Clamp(RecoverableBudget, 0.f, 100000.f)); // 实际结算始终使用float原预算，不按取整数增益。
    const FName Action = RecoverableBudget > 0.f ? (bDashPending ? TEXT("counter_dash") : TEXT("counter_ready"))
        : Now() < NextCounterTime ? TEXT("counter_cooldown") : TEXT("counter_idle"); // 名称为公开表现状态，不是可反向请求的权限。
    Publish(Action, Stacks, Capacity, CounterEndTime, NextCounterTime);
}
