// 职责：金币娱乐纯规则回归。使用：Automation RunTests FPSDemo.Economy.Casino；固定种子不进入生产接口，不访问网络或玩家文件。
#include "Misc/AutomationTest.h"
#include "Economy/Gambling/DemoCasinoSettings.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Debug/DemoLog.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoCasinoTest,"FPSDemo.Economy.Casino",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
/** Parameters由自动化框架提供，本专项不接收覆盖配置；测试独立副本避免修改生产CDO。 */
bool FDemoCasinoTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FDemoCasinoTest::RunTest"));
    const auto& Rules=GetDefault<UDemoCasinoSettings>()->Rules; // 验证INI实际加载，而非只测构造默认。
    FString Error; // 本次规则错误，正常必须为空。
    int32 Parsed=71; // 失败必须保持此前草稿；解析使用真实输入规则。
    for(const FString& Invalid:{FString(),FString(TEXT("0")),FString(TEXT("-1")),FString(TEXT("1.2")),FString(TEXT("1e4")),FString(TEXT("12x")),FString(TEXT("2147483648")),FString(TEXT("999999999999999999"))})
    { TestFalse(TEXT("Invalid/overflow input rejected"),DemoCasino::ParseStake(Invalid,Parsed,Error)); TestEqual(TEXT("Invalid input preserves prior draft"),Parsed,71); }
    TestTrue(TEXT("Pasted positive integer accepted"),DemoCasino::ParseStake(TEXT(" 005001 "),Parsed,Error));
    TestEqual(TEXT("Pasted integer exact"),Parsed,5001);
    TestTrue(TEXT("Storage boundary accepted"),DemoCasino::ParseStake(TEXT("2147483647"),Parsed,Error));
    TestEqual(TEXT("Boundary not wrapped"),Parsed,MAX_int32);
    if(!TestTrue(TEXT("Production casino config valid"),Rules.Validate(Error))) { AddError(Error); return false; }
    FDemoCasinoIntent Intent; // 默认20金币；验证五种覆盖宽度。
    const int32 Widths[]={50,25,10,5,1},Pays[]={38,76,190,380,1900}; // 独立人工基准，非复用算法计算期望。
    for(int32 Index=0;Index<5;++Index)
    {
        Intent.High=Widths[Index]-1;
        const auto Quote=DemoCasino::Quote(Rules,Intent); // 实际生产报价。
        TestTrue(TEXT("Number quote allowed"),Quote.Error.IsEmpty());
        TestTrue(TEXT("Number actual probability"),FMath::IsNearlyEqual(Quote.Chance,Widths[Index]/100.,1e-9));
        TestEqual(TEXT("Number inclusive payout"),Quote.Payout,Pays[Index]);
    }
    Intent.Low=Intent.High=99;
    TestTrue(TEXT("99 inclusive boundary hits"),DemoCasino::Matches(Intent,99));
    TestFalse(TEXT("Exact 99 rejects 98"),DemoCasino::Matches(Intent,98));
    Intent.Selection=1; TestTrue(TEXT("Odd includes 99"),DemoCasino::Matches(Intent,99)); TestFalse(TEXT("Odd excludes zero"),DemoCasino::Matches(Intent,0));
    Intent.Selection=2; TestTrue(TEXT("Even includes zero"),DemoCasino::Matches(Intent,0));
    Intent.Selection=0; Intent.Stake=5000; TestTrue(TEXT("Single number accepts stakes above retired cap"),DemoCasino::Quote(Rules,Intent).Error.IsEmpty());
    Intent.Stake=20; Intent.Low=0; Intent.High=50; TestFalse(TEXT("51 numbers rejected"),DemoCasino::Quote(Rules,Intent).Error.IsEmpty());
    Intent.High=49; Intent.Stake=MAX_int32;
    for(int32 Mode=0;Mode<3;++Mode) { Intent.Mode=Mode; TestFalse(TEXT("Unrepresentable result rejected before debit"),DemoCasino::Quote(Rules,Intent).Error.IsEmpty()); }
    FRandomStream LargeRandom(812); // 大额货箱存档结构不得残留旧百万下注限制。
    Intent.Stake=4000;
    const auto LargeMine=DemoCasino::OpenMine(Rules,Intent,LargeRandom);
    TestTrue(TEXT("Large fixed mine is saveable"),LargeMine.Validate());
    TestEqual(TEXT("Unrepresentable payout is rejected without truncation"),DemoCasino::Pay(MAX_int32,2.),INDEX_NONE); // 已移除玩法封顶字段，保留整数溢出边界回归。
    double Sum=0; int32 Pairs=0,Triples=0,Misses=0; // 精确125组合计数和返还，不使用随机抽样误差作为证据。
    for(int32 A=0;A<5;++A) for(int32 B=0;B<5;++B) for(int32 C=0;C<5;++C)
    {
        const double Multiplier=DemoCasino::SlotMultiplier(Rules,{A,B,C}); // 唯一最高优先级规则。
        Sum+=Multiplier;
        if(A==B&&B==C) ++Triples; else if(A==B||A==C||B==C) ++Pairs; else { ++Misses; TestEqual(TEXT("All different pays zero"),Multiplier,0.); }
    }
    TestEqual(TEXT("Exactly five triples"),Triples,5); TestEqual(TEXT("Exactly sixty pairs"),Pairs,60); TestEqual(TEXT("Exactly sixty misses"),Misses,60);
    TestTrue(TEXT("Slot expected return 94.4 percent"),FMath::IsNearlyEqual(Sum/125.,.944,1e-9));
    TestEqual(TEXT("Triple gold does not add pair reward"),DemoCasino::SlotMultiplier(Rules,{4,4,4}),18.);
    TestEqual(TEXT("Fractional gold floors once"),DemoCasino::Pay(11,1.2),13);
    FDemoCasinoRules Changed=Rules; // 每次非法配置只修改私有副本。
    Changed.NumberWeights[0]=1000; TestFalse(TEXT("Biased positive expectation rejected"),Changed.Validate(Error));
    Changed=Rules; Changed.PairMultiplier=10; TestFalse(TEXT("Slot positive expectation rejected"),Changed.Validate(Error));
    Changed=Rules; Changed.NumberWeights[0]=std::numeric_limits<double>::quiet_NaN(); TestFalse(TEXT("NaN weights rejected"),Changed.Validate(Error));
    Changed=Rules; Changed.SymbolWeights={0,0,0,0,0}; TestFalse(TEXT("Zero total rejected"),Changed.Validate(Error));
    Changed=Rules; Changed.BombChoices={4,4,8}; TestFalse(TEXT("Duplicate mine choices rejected"),Changed.Validate(Error));
    Changed=Rules; Changed.MinBet=0; TestFalse(TEXT("Free betting rejected"),Changed.Validate(Error));
    Changed=Rules; Changed.MinBet=2; TestNotEqual(TEXT("Rules fingerprint changes"),Changed.Fingerprint(),Rules.Fingerprint());
    Changed=Rules; Changed.NumberWeights[0]=2; Changed.NumberMultipliers.Init(.1,50); // 保持合法期望的非均匀配置，验证概率确实使用权重。
    TestTrue(TEXT("Custom weighted config accepted"),Changed.Validate(Error));
    Intent=FDemoCasinoIntent(); Intent.Low=Intent.High=0;
    TestTrue(TEXT("Number chance reflects actual configured weights"),FMath::IsNearlyEqual(DemoCasino::Quote(Changed,Intent).Chance,2./101.,1e-9));
    Intent.Low=MIN_int32; Intent.High=MAX_int32;
    TestFalse(TEXT("Extreme remote range rejected before arithmetic"),DemoCasino::Quote(Rules,Intent).Error.IsEmpty());
    for(int32 Bombs:Rules.BombChoices)
    {
        Intent=FDemoCasinoIntent(); Intent.Mode=2; Intent.Bombs=Bombs;
        FRandomStream Random(1742+Bombs); // 重现只发生在测试，生产服务器自行取种子。
        auto Mine=DemoCasino::OpenMine(Rules,Intent,Random);
        const auto Original=Mine; // 不变棋盘用于验证安全格和爆炸。
        TestTrue(TEXT("Fixed board structurally valid"),Mine.Validate());
        TestEqual(TEXT("Public copy contains no bombs"),Mine.PublicCopy().BombMask,0);
        TestEqual(TEXT("Public projection does not mutate source"),Mine.BombMask,Original.BombMask);
        int32 Gold=980; FDemoCasinoResult Result; // 已扣20的一次事务候选。
        Intent.Operation=2; TestFalse(TEXT("Cannot cash out before revealing"),DemoCasino::AdvanceMine(Mine,Intent,Gold,Result,Error));
        Intent.Operation=1;
        for(int32 Tile=0;Tile<25;++Tile) if(!(Original.BombMask&(1<<Tile)))
        {
            Intent.Tile=Tile;
            if(!TestTrue(TEXT("Safe tile or final automatic payout accepted"),DemoCasino::AdvanceMine(Mine,Intent,Gold,Result,Error))) return false;
            if(Mine.Stake>0) { TestEqual(TEXT("Board unchanged across reveals"),Mine.BombMask,Original.BombMask); TestFalse(TEXT("Duplicate tile refused"),DemoCasino::AdvanceMine(Mine,Intent,Gold,Result,Error)); }
        }
        TestEqual(TEXT("Last safe tile automatically pays frozen total"),Gold,980+Original.Payouts.Last());
        TestEqual(TEXT("All safe clears pending round"),Mine.Stake,0);
        TestFalse(TEXT("Finished box cannot pay again"),DemoCasino::AdvanceMine(Mine,Intent,Gold,Result,Error));
        Mine=Original; Intent.Tile=0; while(!(Original.BombMask&(1<<Intent.Tile))) ++Intent.Tile; Gold=980;
        TestTrue(TEXT("Bomb accepted from fixed board"),DemoCasino::AdvanceMine(Mine,Intent,Gold,Result,Error));
        TestTrue(TEXT("Bomb ends without any second debit"),Result.bBomb&&Gold==980&&Mine.Stake==0);
        Mine=Original; Intent.Tile=0; while(Original.BombMask&(1<<Intent.Tile)) ++Intent.Tile; Gold=980;
        DemoCasino::AdvanceMine(Mine,Intent,Gold,Result,Error); Intent.Operation=2;
        const auto BeforeCash=Mine; Gold=MAX_int32; // 领取失败不能吞掉箱子或改变冻结金额。
        TestFalse(TEXT("Overflow cashout refused"),DemoCasino::AdvanceMine(Mine,Intent,Gold,Result,Error));
        TestTrue(TEXT("Overflow preserves pending box"),Mine.OpenMask==BeforeCash.OpenMask&&Mine.BombMask==BeforeCash.BombMask&&Gold==MAX_int32);
    }
    const auto* Ammo=LoadObject<UDemoAmmoCatalog>(nullptr,TEXT("/Game/Data/Ammo/DA_AmmoCatalog.DA_AmmoCatalog")); // 原弹药金币直购不可被娱乐符号改变。
    if(!TestNotNull(TEXT("Saved ammo catalog"),Ammo)) return false;
    const int32 Prices[]={0,1500,2000,2500}; // 前轮用户批准的资产价格基线。
    for(int32 Index=0;Index<4;++Index) { TestEqual(TEXT("Ammo purchase price retained"),Ammo->Entries[Index].UnlockGoldCost,Prices[Index]); TestNotNull(TEXT("Ammo icon retained"),Ammo->Entries[Index].Icon.Get()); }
    return true;
}
#endif
