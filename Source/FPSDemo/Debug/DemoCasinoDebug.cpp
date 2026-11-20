// 职责：Editor金币娱乐独立诊断。使用控制台Demo.Debug.Casino.Simulate <seed>复现纯规则，不改变真实棋盘、金币或存档；整个入口不进入包体。
#if WITH_EDITOR
#include "Economy/Gambling/DemoCasinoSettings.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Debug/DemoLog.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
namespace
{
    /** Args可选一个int32种子，World须单人试玩；Output仅本次同步调用借用，模拟不接触玩家账本。 */
    void Simulate(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CasinoDebug.Simulate"));
        if(!World||!World->IsGameWorld()||World->GetNetMode()!=NM_Standalone) { Output.Log(TEXT("仅限单人Editor试玩")); return; }
        int32 Seed=12345; // 默认可复现，仅模拟使用；绝不控制生产下一次开奖。
        if(Args.Num()>1||(Args.Num()==1&&!LexTryParseString(Seed,*Args[0]))) { Output.Log(TEXT("用法：Demo.Debug.Casino.Simulate 12345")); return; }
        const auto& Rules=GetDefault<UDemoCasinoSettings>()->Rules; FString Error; // 读取实际配置并完整校验。
        if(!Rules.Validate(Error)) { Output.Logf(TEXT("配置失败：%s"),*Error); return; }
        FRandomStream Random(Seed); FDemoCasinoIntent Intent; Intent.Stake=Rules.MinBet; // 独立随机流与合法模拟下注。
        const auto Quote=DemoCasino::Quote(Rules,Intent); // 默认竞猜小，不涉及角色币。
        const int32 Number=DemoCasino::Draw(Rules.NumberWeights,Random); // 此次模拟公开数字。
        TArray<int32> Symbols; for(int32 Index=0;Index<3;++Index) Symbols.Add(DemoCasino::Draw(Rules.SymbolWeights,Random)); // 三轴独立模拟。
        Output.Logf(TEXT("模拟 seed=%d，未扣币/未改实际开奖结果；数字%02d，猜小概率%.2f%%，命中到账%d"),Seed,Number,Quote.Chance*100,DemoCasino::Matches(Intent,Number)?Quote.Payout:0);
        Output.Logf(TEXT("老虎机：%s / %s / %s，到账%d"),*DemoCasino::SymbolName(Symbols[0]),*DemoCasino::SymbolName(Symbols[1]),*DemoCasino::SymbolName(Symbols[2]),DemoCasino::Pay(Intent.Stake,DemoCasino::SlotMultiplier(Rules,Symbols)));
        Intent.Mode=2; Intent.Bombs=Rules.BombChoices[0]; const auto Mine=DemoCasino::OpenMine(Rules,Intent,Random); // 完全独立模拟箱，不能替换正在玩的箱子。
        Output.Logf(TEXT("模拟货箱%d雷，首格安全后可领%d，全部安全格可领%d；规则指纹%u"),Mine.Bombs,Mine.Payouts[0],Mine.Payouts.Last(),Rules.Fingerprint());
        Intent.Mode=3; auto Reactor=DemoCasino::OpenArcade(Rules,Intent,Random); // 只模拟首个充能结果，与真实待结项目完全独立。
        int32 Gold=0; FDemoCasinoResult Result; Intent.Operation=1;
        DemoCasino::AdvanceArcade(Reactor,Intent,Gold,Result,Random,Error);
        Output.Logf(TEXT("反应堆：%s，当前级别%d，模拟到账%d"),Result.bBomb?TEXT("过载"):TEXT("充能成功"),Reactor.Step,Gold);
        Intent.Mode=4; Intent.Operation=0; auto Dice=DemoCasino::OpenArcade(Rules,Intent,Random); // 模拟五骰、保留前两骰重掷一次。
        Output.Logf(TEXT("战术骰初投：%d %d %d %d %d"),Dice.Values[0],Dice.Values[1],Dice.Values[2],Dice.Values[3],Dice.Values[4]);
        Intent.KeepMask=3; Intent.Operation=1; Gold=0; Result=FDemoCasinoResult(); DemoCasino::AdvanceArcade(Dice,Intent,Gold,Result,Random,Error);
        Output.Logf(TEXT("保留前两骰重掷：%s，到账%d"),*DemoCasino::DiceTierName(Result.Winner),Gold);
        Result=FDemoCasinoResult(); if(DemoCasino::RunArena(Rules,Random,Result)) Output.Logf(TEXT("斗兽场：A精确胜率%.4f%%，胜方%d，回放事件%d"),DemoCasino::ArenaChance(Rules)*100,Result.Winner,Result.Battle.Num());
        Intent.Mode=6; Intent.Operation=0; Intent.Stake=20; const auto Range=DemoCasino::OpenArcade(Rules,Intent,Random); // 只显示奖励合同，不启动真实训练。
        Output.Logf(TEXT("靶场：%d秒/%d发，满分20可得%d；实体射击测试在-DemoGambleTest"),Rules.RangeSeconds,Rules.RangeShots,Range.Payouts.Last());
        Output.Log(TEXT("试玩免扣金币：Demo.Debug.InfiniteGold 1；自动化：Automation RunTests FPSDemo.Economy.Casino；真实事务：独立Editor -DemoGambleTest"));
    }
    FAutoConsoleCommandWithWorldArgsAndOutputDevice SimulateCommand(TEXT("Demo.Debug.Casino.Simulate"),TEXT("Editor standalone only: simulate casino rules with optional integer seed; never changes wallet or live round."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Simulate)); // 模块卸载自动注销，非Editor完全不编译。
}
#endif
