// 职责：双端探针准备门的无World纯值合同专项，覆盖正式UI等待、真实战斗前置、人数与时钟边界。
// 使用：Automation RunTests FPSDemo.Tests.NetworkProbeSetupContract；只调用生产Evaluate，不创建Actor/World、修改CDO、启动LAN或应用GE。
// 依赖：Runtime Tests/DemoNetworkProbeSetup的Editor-only纯值接口；本专项通过也不能代替真实UI/流送/双端观测验收。
#include "Misc/AutomationTest.h"
#include "Tests/DemoNetworkProbeSetup.h"
#include "Debug/DemoLog.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoNetworkProbeSetupContractTest,"FPSDemo.Tests.NetworkProbeSetupContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters由Automation提供，不作为外部路径或配置；仅建立独立FSnapshot值，逐项触达生产准备裁决。 */
bool FDemoNetworkProbeSetupContractTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FDemoNetworkProbeSetupContractTest::RunTest"));
    (void)Parameters;
    using namespace DemoNetworkProbeSetup;
    FString Reason=TEXT("old diagnostic"); // 独立输出缓存，每次Evaluate必须用当前结果替换，不能把旧错误误显示给玩家。
    FSnapshot Ready; // 人工固定的合法最小输入基线，不从生产Poll推导期望，也不构造World或Session对象。
    Ready.bListenServer=true; Ready.RegisteredPlayers=2; Ready.Phase=EPhase::Combat; Ready.Level=1;
    Ready.bCurrentSession=true; Ready.bObjectiveActive=true; Ready.bKillAll=true;
    TestTrue(TEXT("Exactly two players in real first active KillAll may begin"),Evaluate(Ready,0.,180.,Reason)==EResult::Ready);
    TestTrue(TEXT("Successful preparation clears old diagnostic"),Reason.IsEmpty());

    FSnapshot Default; // 刚启动未监听、未登记的默认值，只能等待正式房间/成员。
    TestTrue(TEXT("Default snapshot waits for formal party"),Evaluate(Default,0.,180.,Reason)==EResult::Waiting);
    TestTrue(TEXT("Party wait supplies current diagnostic"),Reason.StartsWith(TEXT("WaitingForFormalListenParty")));
    FSnapshot Candidate=Ready; // 每个用例从合法值独立改变一个边界，不读取或写入外部状态。
    Candidate.Phase=EPhase::Hub; Candidate.Level=0; Candidate.bCurrentSession=false; Candidate.bObjectiveActive=false; Candidate.bKillAll=false;
    TestTrue(TEXT("Formal UI in safe Hub remains waiting"),Evaluate(Candidate,1.,180.,Reason)==EResult::Waiting);
    TestTrue(TEXT("Hub wait describes formal UI preparation"),Reason.StartsWith(TEXT("WaitingForFormalUI")));
    Candidate.bCurrentSession=true; Candidate.bObjectiveActive=true; Candidate.bKillAll=true;
    TestTrue(TEXT("Stale combat flags cannot turn Hub into ready"),Evaluate(Candidate,1.,180.,Reason)==EResult::Waiting);
    Candidate=Ready; Candidate.Phase=EPhase::Lobby; Candidate.Level=0;
    TestTrue(TEXT("Lobby never starts an action plan"),Evaluate(Candidate,1.,180.,Reason)==EResult::Waiting);
    TestTrue(TEXT("Lobby wait replaces the prior Hub diagnostic"),Reason.StartsWith(TEXT("WaitingForSafeHub")));
    Candidate=Ready; Candidate.bObjectiveActive=false;
    TestTrue(TEXT("Combat phase before active objective remains waiting"),Evaluate(Candidate,2.,180.,Reason)==EResult::Waiting);
    TestTrue(TEXT("Streaming or objective activation wait is explicit"),Reason==TEXT("WaitingForActiveObjective"));
    Candidate=Ready; Candidate.bCurrentSession=false;
    TestTrue(TEXT("Phase alone cannot impersonate a current combat session"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("Missing current session has an explicit rejection"),Reason==TEXT("SetupCombatSessionInvalid"));
    Candidate.bObjectiveActive=false;
    TestTrue(TEXT("Inactive objective cannot hide a missing current session"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate=Ready; Candidate.bKillAll=false;
    TestTrue(TEXT("A live non-KillAll objective cannot run the old clear plan"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("Unsupported objective is diagnosed rather than silently skipped"),Reason==TEXT("SetupRequiresKillAllForExistingProbePlan"));
    Candidate=Ready; Candidate.Phase=EPhase::Other;
    TestTrue(TEXT("Cancelled reward intermission or end phase rejects setup"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate=Ready; Candidate.Level=0;
    TestTrue(TEXT("Combat level zero rejects a forged phase"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate.Level=2;
    TestTrue(TEXT("Second combat cannot reuse the first-level probe"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate.Level=-1;
    TestTrue(TEXT("Negative combat level is rejected"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate=Ready; Candidate.Phase=EPhase::Hub; Candidate.Level=1;
    TestTrue(TEXT("Nonfresh Hub checkpoint is rejected"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);

    Candidate=Ready; Candidate.RegisteredPlayers=0;
    TestTrue(TEXT("Zero registered players waits instead of inventing a party"),Evaluate(Candidate,2.,180.,Reason)==EResult::Waiting);
    Candidate.RegisteredPlayers=1;
    TestTrue(TEXT("One registered player waits for actual peer"),Evaluate(Candidate,2.,180.,Reason)==EResult::Waiting);
    Candidate.RegisteredPlayers=3;
    TestTrue(TEXT("Three players explicitly exceed this two-endpoint probe"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("Unsupported three-player roster has explicit diagnostic"),Reason==TEXT("SetupRequiresExactlyTwoRegisteredPlayers"));
    Candidate.RegisteredPlayers=4;
    TestTrue(TEXT("Four players cannot be reported as two-player coverage"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate.RegisteredPlayers=-1;
    TestTrue(TEXT("Corrupt negative roster size fails"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate=Ready; Candidate.bListenServer=false;
    TestTrue(TEXT("Two Pawn-like values without Listen Server still wait"),Evaluate(Candidate,2.,180.,Reason)==EResult::Waiting);

    FSnapshot HubOnly; // DashFlowHub保留原安全区技能模式，不借此授权Combat或跳过实际两人登记。
    HubOnly.bListenServer=true; HubOnly.RegisteredPlayers=2; HubOnly.Phase=EPhase::Hub; HubOnly.Level=0; HubOnly.bHubOnly=true;
    TestTrue(TEXT("Hub-only probe needs no combat session or objective"),Evaluate(HubOnly,2.,180.,Reason)==EResult::Ready);
    TestTrue(TEXT("Hub-only success clears a prior party wait diagnostic"),Reason.IsEmpty());
    Candidate=Ready; Candidate.bHubOnly=true;
    TestTrue(TEXT("Hub-only mode cannot silently run in Combat"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("Hub-only wrong phase explains initial-Hub requirement"),Reason==TEXT("HubOnlyProbeRequiresInitialHub"));
    Candidate=HubOnly; Candidate.Level=1;
    TestTrue(TEXT("Hub-only mode cannot run on nonfresh checkpoint"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate=HubOnly; Candidate.Phase=EPhase::Lobby;
    TestTrue(TEXT("Hub-only Lobby waits for actual safe Hub"),Evaluate(Candidate,2.,180.,Reason)==EResult::Waiting);
    Candidate=HubOnly; Candidate.Phase=EPhase::Other;
    TestTrue(TEXT("Hub-only cancelled phase fails"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);
    Candidate=HubOnly; Candidate.RegisteredPlayers=1;
    TestTrue(TEXT("Hub-only mode still requires the real peer"),Evaluate(Candidate,2.,180.,Reason)==EResult::Waiting);
    Candidate.RegisteredPlayers=3;
    TestTrue(TEXT("Hub-only mode does not enlarge roster coverage"),Evaluate(Candidate,2.,180.,Reason)==EResult::Failed);

    TestTrue(TEXT("Minimum timeout 30 seconds is legal"),Evaluate(Ready,0.,30.,Reason)==EResult::Ready);
    TestTrue(TEXT("Maximum timeout 600 seconds is legal"),Evaluate(Ready,0.,600.,Reason)==EResult::Ready);
    TestTrue(TEXT("Timeout below 30 seconds is illegal"),Evaluate(Ready,0.,29.999,Reason)==EResult::Failed);
    TestTrue(TEXT("Timeout above 600 seconds is illegal"),Evaluate(Ready,0.,600.001,Reason)==EResult::Failed);
    TestTrue(TEXT("Zero timeout is illegal"),Evaluate(Ready,0.,0.,Reason)==EResult::Failed);
    TestTrue(TEXT("Just before default deadline remains ready"),Evaluate(Ready,179.999,180.,Reason)==EResult::Ready);
    TestTrue(TEXT("Default deadline itself is a timeout"),Evaluate(Ready,180.,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("Timeout diagnostic replaces a previous success"),Reason.StartsWith(TEXT("SetupTimeout:")));
    TestTrue(TEXT("Past default deadline remains failed"),Evaluate(Ready,180.001,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("Just before minimum deadline remains legal"),Evaluate(Ready,29.999,30.,Reason)==EResult::Ready);
    TestTrue(TEXT("Minimum deadline itself times out"),Evaluate(Ready,30.,30.,Reason)==EResult::Failed);
    TestTrue(TEXT("Just before maximum deadline remains legal"),Evaluate(Ready,599.999,600.,Reason)==EResult::Ready);
    TestTrue(TEXT("Maximum deadline itself times out"),Evaluate(Ready,600.,600.,Reason)==EResult::Failed);
    TestTrue(TEXT("A ready Hub-only state cannot bypass preparation timeout"),Evaluate(HubOnly,180.,180.,Reason)==EResult::Failed);

    const double NaN=std::numeric_limits<double>::quiet_NaN(),Infinity=std::numeric_limits<double>::infinity(); // 非有限输入仅保存在测试局部，不能传给World/Timer或命令行配置。
    TestTrue(TEXT("Negative elapsed time is rejected"),Evaluate(Ready,-.001,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("NaN elapsed time is rejected"),Evaluate(Ready,NaN,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("Positive infinite elapsed time is rejected"),Evaluate(Ready,Infinity,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("Negative infinite elapsed time is rejected"),Evaluate(Ready,-Infinity,180.,Reason)==EResult::Failed);
    TestTrue(TEXT("NaN timeout is rejected"),Evaluate(Ready,0.,NaN,Reason)==EResult::Failed);
    TestTrue(TEXT("Positive infinite timeout is rejected"),Evaluate(Ready,0.,Infinity,Reason)==EResult::Failed);
    TestTrue(TEXT("Negative infinite timeout is rejected"),Evaluate(Ready,0.,-Infinity,Reason)==EResult::Failed);
    TestTrue(TEXT("Invalid clock diagnostic is explicit"),Reason==TEXT("SetupInvalidClockOrLimit"));
    Reason=TEXT("SetupTimeout: stale diagnostic");
    TestTrue(TEXT("A later legal snapshot can succeed after previous failed value"),Evaluate(Ready,0.,180.,Reason)==EResult::Ready);
    TestTrue(TEXT("Legal snapshot erases stale timeout text"),Reason.IsEmpty());

    return true; // Automation收集全部断言结果；此返回不代表LAN、菜单、真实Session或流送已经运行验收。
}
#endif
