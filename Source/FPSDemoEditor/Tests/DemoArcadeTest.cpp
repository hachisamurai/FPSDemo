// 职责：第二期娱乐规则与无封顶数值边界回归。使用：Automation RunTests FPSDemo.Economy.Casino；仅纯值，不访问账号或网络。
#include "Misc/AutomationTest.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Save/Codecs/DemoArcadeJson.h"
#include "Debug/DemoLog.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoArcadeTest,"FPSDemo.Economy.CasinoExpansion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
/** Parameters为框架输入；测试只使用独立规则副本和固定随机流，不能影响下一次玩家开奖。 */
bool FDemoArcadeTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    FDemoCasinoRules Rules; FDemoCasinoIntent Intent; FString Error; FRandomStream Random(4712); // 测试独占值。
    TestTrue(TEXT("Default expansion configuration"),Rules.Validate(Error));
    TestEqual(TEXT("No million payout ceiling"),DemoCasino::Pay(200000,18),3600000);
    TestEqual(TEXT("No hundred million wallet award ceiling"),DemoCasino::Pay(2000000,95),190000000);
    TestEqual(TEXT("Representable int32 boundary exact"),DemoCasino::Pay(MAX_int32,1),MAX_int32);
    TestEqual(TEXT("Overflow refused, not truncated"),DemoCasino::Pay(MAX_int32,1.1),INDEX_NONE);
    for(int32 Mode=0;Mode<7;++Mode) TestEqual(TEXT("Stable menu ID roundtrip"),DemoCasino::ModeForChoice(DemoCasino::ChoiceForMode(Mode)),Mode);
    TestEqual(TEXT("Legacy relic ID not mistaken for casino"),DemoCasino::ModeForChoice(8),INDEX_NONE);
    const TArray<TArray<int32>> Patterns={{1,2,3,4,6},{1,1,2,3,4},{1,1,2,2,3},{1,1,1,2,3},{2,3,4,5,6},{1,1,1,2,2},{1,1,1,1,2},{6,6,6,6,6}}; // 人工独立八种组合基准。
    for(int32 Tier=0;Tier<8;++Tier) TestEqual(TEXT("Highest dice pattern classified"),DemoCasino::DiceTier(Patterns[Tier]),Tier);
    Intent.Mode=4; Intent.Stake=200000;
    auto Dice=DemoCasino::OpenArcade(Rules,Intent,Random); Dice.Values={6,6,6,6,6}; // 固定已保存骰面，模拟保留五同。
    int32 Gold=150000000; FDemoCasinoResult Result; Intent.Operation=1; Intent.KeepMask=31; // 不重掷任何骰，预期2.4m总到账。
    TestTrue(TEXT("Keep all accepted once"),DemoCasino::AdvanceArcade(Dice,Intent,Gold,Result,Random,Error));
    TestEqual(TEXT("Five of a kind full uncapped amount"),Gold,152400000); TestEqual(TEXT("Paid exact"),Result.Paid,2400000);
    TestEqual(TEXT("Dice round consumed"),Dice.Mode,INDEX_NONE); TestFalse(TEXT("Cannot reroll completed round"),DemoCasino::AdvanceArcade(Dice,Intent,Gold,Result,Random,Error));
    Rules.ReactorChances={1,1}; Rules.ReactorReturn=1; Intent=FDemoCasinoIntent(); Intent.Mode=3;
    auto Reactor=DemoCasino::OpenArcade(Rules,Intent,Random); Gold=100; Intent.Operation=2;
    TestFalse(TEXT("Cannot collect before charging"),DemoCasino::AdvanceArcade(Reactor,Intent,Gold,Result,Random,Error));
    Intent.Operation=1; TestTrue(TEXT("Charge once"),DemoCasino::AdvanceArcade(Reactor,Intent,Gold,Result,Random,Error)); TestEqual(TEXT("Charge saves progress without paying"),Reactor.Step,1); TestEqual(TEXT("No premature payment"),Gold,100);
    TestTrue(TEXT("Last charge settles"),DemoCasino::AdvanceArcade(Reactor,Intent,Gold,Result,Random,Error)); TestEqual(TEXT("Last charge exact amount"),Gold,120);
    Rules=FDemoCasinoRules(); Intent.Mode=6; Intent.Operation=0; auto Range=DemoCasino::OpenArcade(Rules,Intent,Random); // 服务器记录满分成绩，测试档位边界。
    Range.Step=Range.Heads=Range.Shots=10; Gold=200; Intent.Operation=2;
    TestTrue(TEXT("Perfect range scores double per target"),DemoCasino::AdvanceArcade(Range,Intent,Gold,Result,Random,Error)); TestEqual(TEXT("Perfect range pays 2.5 inclusive"),Gold,250);
    Rules.ArenaHealth={1,1}; Rules.ArenaDamage={1,1}; Rules.ArenaHit={.8,.6}; // 独立推导：A先手 .8/.92，B先手 .4*.8/.92，各一半。
    TestTrue(TEXT("Arena analytic probability includes alternating misses"),FMath::IsNearlyEqual(DemoCasino::ArenaChance(Rules),.56/.92,1e-10));
    int32 Wins=0; // 1000独立种子，与精确理论比较，容许宽统计误差，不使用机器时钟。
    for(int32 Trial=0;Trial<1000;++Trial)
    {
        FRandomStream BattleRandom(Trial*47+13); FDemoCasinoResult Battle; // 纯战斗模拟。
        if(!TestTrue(TEXT("Finite battle terminates"),DemoCasino::RunArena(Rules,BattleRandom,Battle))) return false;
        Wins+=Battle.Winner==0; TestTrue(TEXT("Final event determines actual winner"),(Battle.Battle.Last()>0?0:1)==Battle.Winner);
    }
    TestTrue(TEXT("Sample matches public probability"),FMath::Abs(Wins/1000.-DemoCasino::ArenaChance(Rules))<.07);
    Intent=FDemoCasinoIntent(); Intent.Mode=4; auto Saved=DemoCasino::OpenArcade(Rules,Intent,Random); auto Json=DemoArcadeJson::Write(Saved); FDemoArcadeRound Decoded; // 严格JSON扩展往返。
    TestTrue(TEXT("Frozen dice JSON roundtrip"),DemoArcadeJson::Read(*Json,Decoded)); TestTrue(TEXT("Same revealed dice restored"),Saved.Values==Decoded.Values&&Saved.Payouts==Decoded.Payouts);
    Json->SetNumberField(TEXT("stake"),1.5); TestFalse(TEXT("Fractional integer rejected before truncation"),DemoArcadeJson::Read(*Json,Decoded));
    Json=DemoArcadeJson::Write(Saved); Json->SetNumberField(TEXT("unknown"),1); TestFalse(TEXT("Unknown extension key rejected"),DemoArcadeJson::Read(*Json,Decoded));
    return true;
}
#endif
