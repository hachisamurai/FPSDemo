// 职责：在临时成长清空前冻结个人战报，奖励器仅登记实际金币增量。使用PS默认组件与拥有者菜单投影，无网络请求/额外账本或持续采样。
#include "Progression/Summary/DemoRunSummaryComponent.h"
#include "Game/DemoGameState.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Debug/DemoLog.h"

UDemoRunSummaryComponent::UDemoRunSummaryComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick = false;
}

void UDemoRunSummaryComponent::RecordVictoryCredit(const FString& RunId, int32 Amount)
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || RunId.IsEmpty() || Amount < 0 || Amount > 1000000 || CreditRun == RunId)
    { UE_LOG(LogFPSDemo, Log, TEXT("RUN_SUMMARY_CREDIT rejected authority/value/duplicate")); return; }
    CreditRun = RunId; CreditAmount = Amount;
}

bool UDemoRunSummaryComponent::Capture(const FString& RunId, const ADemoGameState& State, EDemoRunOutcome Outcome, const FString& Reason)
{
    DEMO_LOG_CALL();
    const ADemoPlayerState* Player = Cast<ADemoPlayerState>(GetOwner()); // 只读取本组件所属PS，不查询房主的个人进度。
    if (!Player || !Player->HasAuthority() || State.GetWorld() != GetWorld() || RunId.IsEmpty() || Snapshot.RunId == RunId
        || !Player->GetProgressComponent() || !Player->GetRunBuildComponent())
    { UE_LOG(LogFPSDemo, Log, TEXT("RUN_SUMMARY_CAPTURE rejected authority/context/duplicate")); return false; }
    const FDemoPlayerProgressSnapshot& Progress = Player->GetProgressComponent()->GetSnapshot(); // 同栈只读，后续Reset不会改变这个已拷贝战报。
    FDemoRunSummarySnapshot Next; // 一次性替换完整纯值，不能发布半份个人结果。
    Next.RunId = RunId; Next.Outcome = Outcome; Next.Difficulty = State.Difficulty; Next.bEndless = State.bEndless;
    const EDemoPhase EndPhase = Outcome == EDemoRunOutcome::Victory ? EDemoPhase::Victory : EDemoPhase::Defeat; // 团灭摘要先于FailRun切阶段，预先绑定该次代数。
    Next.EndPhaseRevision = State.Phase == EndPhase ? State.PhaseRevision : State.PhaseRevision == MAX_uint32 ? 1u : State.PhaseRevision + 1u;
    Next.ReachedStage = FMath::Max(0, State.LevelNumber);
    Next.CompletedStages = FMath::Max(0, State.LevelNumber - (State.Phase == EDemoPhase::Combat && Outcome != EDemoRunOutcome::Victory ? 1 : 0));
    Next.LastObjective = State.Objective.Kind; Next.ObjectiveCompleted = State.Objective.Completed; Next.ObjectiveTotal = State.Objective.Total;
    Next.Build = Player->GetRunBuildComponent()->GetSnapshot(); Next.TeamKills = Progress.Kills;
    Next.AwardedGold = Outcome == EDemoRunOutcome::Victory && CreditRun == RunId ? CreditAmount : 0;
    Next.GoldBalance = Progress.Coins; Next.PistolChallenge = Progress.PistolChallenge;
    Next.bTemporaryReset = Outcome != EDemoRunOutcome::SystemError; Next.SilverRemoved = Next.bTemporaryReset ? Progress.SilverCoins : 0;
    Next.PermanentHealth = Progress.UpgradeProgress.PermanentHealth; Next.PermanentDamage = Progress.UpgradeProgress.PermanentDamage;
    Next.PermanentMagazine = Progress.UpgradeProgress.PermanentMagazine; Next.UnlockedWeapons = Progress.UnlockedWeaponIds;
    Next.Reason = Reason.Left(160);
    Snapshot = MoveTemp(Next);
    UE_LOG(LogFPSDemo, Log, TEXT("RUN_SUMMARY_CAPTURE outcome=%d completed=%d stage=%d kills=%d gold=%d silver_removed=%d"),
        int32(Outcome), Snapshot.CompletedStages, Snapshot.ReachedStage, Snapshot.TeamKills, Snapshot.AwardedGold, Snapshot.SilverRemoved);
    return true;
}

const FDemoRunSummarySnapshot& UDemoRunSummaryComponent::GetSnapshot() const { DEMO_LOG_TICK(); return Snapshot; }
