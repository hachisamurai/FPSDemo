// 职责：R04主流程表现专项，为现有CommonUI测试追加准备/目标HUD/普通奖励/3与7关分支/冻结战报截图。
// 使用：WITH_EDITOR的-DemoCommonUITest -DemoGameplayUISpine；仅隔离World与GI临时档。阶段/目标/摘要是显式夹具，不是完整清关或经济/网络正确性的证据。
#include "Tests/DemoCommonUITest.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Tests/DemoUILayoutChecks.h"
#include "Characters/DemoCharacter.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Interaction/DemoInteractable.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Summary/DemoRunSummaryComponent.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Common/DemoMenuScreen.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

void ADemoCommonUITest::TickGameplaySpine()
{
    DEMO_LOG_CALL();
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本专项唯一本机拥有者，每步重新借用。
    auto* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    auto* PS=PC?PC->GetPlayerState<ADemoPlayerState>():nullptr;
    auto* State=GetWorld()->GetGameState<ADemoGameState>();
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    auto* UI=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr;
    if(!Check(PC&&Player&&PS&&State&&Mode&&UI,TEXT("spine presentation fixture has production owner and UI"))) return;
    auto* Flow=PC->GetMenuFlow(); // 原菜单状态机；不会另造动作路由或隐藏底层面板。
    auto* Build=PS->GetRunBuildComponent(); // RestoreSnapshot仅为表现状态，不声称该玩家真的通关了3/7关。
    switch(CurrentStage)
    {
    case 300:
        Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(100,0,110)); // 可站立的Capsule中心高度，不将角色塞入终端/地板。
        Mode->GetNextLevelTerminal()->Interact(Player); Next(301,.4); return;
    case 301:
        if(!Check(Flow->IsNextLevelConfirmationOpen()&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("Normal")),TEXT("spine initial departure retains difficulty and real cancellation actions"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen());
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Spine-01-Departure.png")),true,false);
        Next(302); return;
    case 302:
    {
        if(!Press(TEXT("NextLevelNo"))) return;
        State->Phase=EDemoPhase::Combat; ++State->PhaseRevision; State->LevelNumber=4; State->EnemiesRemaining=6; // 显式HUD数据夹具，无CombatSession，不允许验证伤害/核心行为。
        State->Objective.Kind=EDemoObjectiveKind::RotatingControl; State->Objective.Total=3; State->Objective.Completed=1;
        State->Objective.ProgressSeconds=8.f; State->Objective.RequiredSeconds=20.f; State->Objective.ControlStatus=EDemoControlStatus::Capturing;
        State->OnRep_RunView(); PC->PublishMenuSnapshot(); PC->OnRunReady();
        Next(303,.5); return;
    }
    case 303:
        if(!Check(!UI->UsesWidgetMenu()&&!Flow->IsUpgradeMenuOpen(),TEXT("spine combat shows one Canvas HUD with no residual menu stack"))) return;
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Spine-02-CombatHUD.png")),true,false);
        Next(304); return;
    case 304:
        State->Phase=EDemoPhase::Reward; ++State->PhaseRevision; State->LevelNumber=1; State->OnRep_RunView(); PC->PublishMenuSnapshot(); PC->OnRunReady();
        Next(305,.4); return;
    case 305:
        if(!Check(Flow->IsRewardMenu()&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("Upgrade0"))&&UI->GetActiveScreen()->FindAction(TEXT("Upgrade2"))&&!UI->GetActiveScreen()->FindAction(TEXT("Close")),TEXT("spine ordinary reward has three real choices and no skip action"))) return;
        Key(EKeys::Escape);
        if(!Check(Flow->IsRewardMenu(),TEXT("spine reward still rejects actual Escape skip"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen());
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Spine-03-Rewards.png")),true,false);
        Next(306); return;
    case 306:
    {
        FDemoRunBuildSnapshot Selection; Selection.CoreId=TEXT("B01"); Selection.bLocked=true; Selection.PendingBranchStage=3; // 合法但未实际赚取的检查点选择，仅测试分支表现。
        if(!Check(Build->RestoreSnapshot(Selection),TEXT("spine legal branch-three selection fixture restored"))) return;
        State->LevelNumber=3; ++State->PhaseRevision; State->OnRep_RunView(); PC->PublishMenuSnapshot(); PC->OnRunReady();
        Next(307,.4); return;
    }
    case 307:
        if(!Check(PC->GetMenuSnapshot().bBranchReward&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("Upgrade0"))&&UI->GetActiveScreen()->FindAction(TEXT("Upgrade1"))&&!UI->GetActiveScreen()->FindAction(TEXT("Upgrade2")),TEXT("spine branch-three reward is exactly two choices from server projection"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen());
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Spine-04-Branch3.png")),true,false);
        Next(308); return;
    case 308:
    {
        FDemoRunBuildSnapshot Selection; Selection.CoreId=TEXT("B01"); Selection.bLocked=true; Selection.Branch3=TEXT("B01-A"); Selection.PendingBranchStage=7;
        if(!Check(Build->RestoreSnapshot(Selection),TEXT("spine legal branch-seven selection fixture restored"))) return;
        State->LevelNumber=7; ++State->PhaseRevision; State->OnRep_RunView(); PC->PublishMenuSnapshot(); PC->OnRunReady();
        Next(309,.4); return;
    }
    case 309:
        if(!Check(PC->GetMenuSnapshot().bBranchReward&&PC->GetMenuSnapshot().RewardOffers.Num()==2&&PC->GetMenuSnapshot().RewardOffers[0].Id==TEXT("B01-C"),TEXT("spine seventh-stage branch keeps current-core C/D group"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen());
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Spine-05-Branch7.png")),true,false);
        Next(310); return;
    case 310:
        State->Phase=EDemoPhase::Victory; ++State->PhaseRevision; State->LevelNumber=10; State->OnRep_RunView();
        if(!Check(PS->GetRunSummaryComponent()->Capture(TEXT("R04-UI-fixture-victory"),*State,EDemoRunOutcome::Victory,TEXT("UI presentation fixture")),TEXT("spine summary uses production capture without granting any victory gold"))) return;
        PC->PublishMenuSnapshot(); PC->OnRunReady(); Next(311,.4); return;
    case 311:
        if(!Check(!UI->UsesWidgetMenu()&&PC->GetMenuSnapshot().Summary.EndPhaseRevision==State->PhaseRevision&&PC->GetMenuSnapshot().Summary.AwardedGold==0,TEXT("spine final report matches revision and displays actual zero credit instead of configured 750"))) return;
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Spine-06-VictoryReport.png")),true,false);
        Next(312); return;
    case 312:
        State->Phase=EDemoPhase::Combat; State->LevelNumber=4; // 与生产FailRun同序：先冻结尚未完成的当前Combat，再提交Defeat，已完成数应为3。
        if(!Check(PS->GetRunSummaryComponent()->Capture(TEXT("R04-UI-fixture-defeat"),*State,EDemoRunOutcome::Eliminated,TEXT("隔离UI阵亡表现夹具")),TEXT("spine defeat summary captured independently of prior victory"))) return;
        State->Phase=EDemoPhase::Defeat; ++State->PhaseRevision; State->bReturnToHubOnRestart=true; State->OnRep_RunView();
        PC->PublishMenuSnapshot(); PC->OnRunReady(); Next(313,.4); return;
    case 313:
        if(!Check(PC->GetMenuSnapshot().Summary.CompletedStages==3&&PC->GetMenuSnapshot().Summary.EndPhaseRevision==State->PhaseRevision,TEXT("spine defeat capture excludes current uncompleted combat and matches committed revision"))) return;
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Spine-07-DefeatReport.png")),true,false);
        Next(314); return;
    case 314:
        ++State->PhaseRevision; State->OnRep_RunView(); PC->OnRunReady(); Next(315,.4); return; // 陈旧本人摘要必须等待，不能用上一版数据宣称已经结算。
    case 315:
        if(!Check(PC->GetMenuSnapshot().Summary.EndPhaseRevision!=State->PhaseRevision,TEXT("spine stale result fixture has mismatched end revision"))) return;
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Spine-08-SummaryWaiting.png")),true,false);
        Next(316); return;
    case 316:
        State->Phase=EDemoPhase::Hub; ++State->PhaseRevision; State->LevelNumber=0; State->EnemiesRemaining=0; State->bReturnToHubOnRestart=false; State->Objective=FDemoObjectiveSnapshot();
        if(!Check(Build->RestoreSnapshot(FDemoRunBuildSnapshot()),TEXT("spine fixture restores initial core state before existing lifecycle regression"))) return;
        State->OnRep_RunView(); PC->PublishMenuSnapshot(); PC->OnRunReady();
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // 与初始菜单/生命周期夹具保持同一安全高度。
        Mode->GetShopTerminal()->Interact(Player); Next(22,.4); return;
    default: Check(false,TEXT("unexpected spine presentation fixture stage")); return;
    }
}
#endif
