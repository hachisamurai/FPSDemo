// 职责：三种金币玩法的数值校验、概率与固定货箱运算。使用：全部操作值副本，无World、存盘、网络或UI依赖。
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Debug/DemoLog.h"
#include "Misc/Crc.h"
namespace
{
    /** Mask为非负25位集合；本地逐位统计，不解释位含义。 */
    int32 Bits(int32 Mask)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); int32 Count=0; /* 当前值的置位数。 */ for(;Mask>0;Mask&=Mask-1) ++Count; return Count; }
    /** Values必须固定Count项、有限非负，权重额外要求总和大于0；限制量级便于跨端稳定计算。 */
    bool Numbers(const TArray<double>& Values,int32 Count,bool Weights)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        double Sum=0; // 验证总质量；每项1e6上限避免求和溢出。
        if(Values.Num()!=Count) return false;
        for(double Value:Values) { if(!FMath::IsFinite(Value)||Value<0||Value>1000000) return false; Sum+=Value; }
        return !Weights||Sum>0;
    }
}
FDemoCasinoRules::FDemoCasinoRules()
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    NumberWeights.Init(1.,100);
    for(int32 Count=1;Count<=50;++Count) NumberMultipliers.Add(95./Count);
}
bool FDemoCasinoRules::Validate(FString& Error) const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    Error=TEXT("金币娱乐配置非法，请检查下注、权重与倍率");
    if(MinBet<1||BetStep<1
        ||Presets.Num()!=4||!FMath::IsFinite(PairMultiplier)||PairMultiplier<0||PairMultiplier>1000000
        ||!FMath::IsFinite(MineReturn)||MineReturn<=0||MineReturn>1||BombChoices.Num()!=3
        ||!Numbers(NumberWeights,100,true)||!Numbers(NumberMultipliers,50,false)||!Numbers(SymbolWeights,5,true)||!Numbers(TripleMultipliers,5,false)) return false;
    for(int32 Bet:Presets) if(Bet<MinBet) return false;
    TSet<int32> Seen; // 本次验证雷数去重。
    for(int32 Bomb:BombChoices) { if(Bomb<1||Bomb>24||Seen.Contains(Bomb)) return false; Seen.Add(Bomb); }
    double Total=0,Odd=0; // 数字总权重与奇数权重。
    for(int32 Index=0;Index<100;++Index) { Total+=NumberWeights[Index]; if(Index%2) Odd+=NumberWeights[Index]; }
    // 校验全部合法区间及奇偶，非均匀配置也不能生成正期望策略；整金币向下取整只会进一步降低返还。
    for(int32 Low=0;Low<100;++Low)
    {
        double Weight=0; // 当前起点的递增区间权重。
        for(int32 Count=1;Count<=50&&Low+Count<=100;++Count)
        { Weight+=NumberWeights[Low+Count-1]; if(Weight/Total*NumberMultipliers[Count-1]>1.+1e-9) { Error=TEXT("数字权重与倍率形成正期望，请一起调整"); return false; } }
    }
    if(FMath::Max(Odd,Total-Odd)/Total*NumberMultipliers[49]>1.+1e-9) return false;
    double SymbolsTotal=0,Expected=0; // 精确枚举125项的老虎机期望。
    for(double Weight:SymbolWeights) SymbolsTotal+=Weight;
    for(int32 A=0;A<5;++A) for(int32 B=0;B<5;++B) for(int32 C=0;C<5;++C)
        Expected+=SymbolWeights[A]*SymbolWeights[B]*SymbolWeights[C]/FMath::Pow(SymbolsTotal,3.)*DemoCasino::SlotMultiplier(*this,{A,B,C});
    if(Expected>1.+1e-9) { Error=TEXT("老虎机权重与倍率形成正期望，请一起调整"); return false; }
    if(!DemoCasino::ValidateArcadeRules(*this,Error)) return false;
    Error.Empty(); return true;
}
uint32 FDemoCasinoRules::Fingerprint() const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    FString Text=FString::Printf(TEXT("uncapped-v3/%d/%d/%.17g/%.17g"),MinBet,BetStep,PairMultiplier,MineReturn); // 规范字段顺序和足够double精度，版本区分旧下注上限规则。
    for(const auto* Array:{&Presets,&BombChoices,&ArenaHealth,&ArenaDamage,&RangeScores}) { Text+=TEXT("|"); for(int32 Value:*Array) Text+=FString::Printf(TEXT("%d,"),Value); }
    Text+=FString::Printf(TEXT("/%d/%d/%.17g/%.17g"),RangeSeconds,RangeShots,ReactorReturn,ArenaReturn);
    for(const auto* Array:{&NumberWeights,&NumberMultipliers,&SymbolWeights,&TripleMultipliers,&ReactorChances,&DiceWeights,&DiceMultipliers,&ArenaHit,&RangeMultipliers}) { Text+=TEXT("|"); for(double Value:*Array) Text+=FString::Printf(TEXT("%.17g,"),Value); }
    return FCrc::StrCrc32(*Text);
}
bool FDemoMineRound::Validate() const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if(Stake==0) return Debited==0&&Bombs==0&&BombMask==0&&OpenMask==0&&Payouts.IsEmpty();
    if(Stake<1||Debited<0||Debited>Stake||Bombs<1||Bombs>24||BombMask<1||BombMask>0x1ffffff||OpenMask<0||OpenMask>0x1ffffff
        ||Bits(BombMask)!=Bombs||(OpenMask&BombMask)!=0||Payouts.Num()!=25-Bombs||OpenCount()>=25-Bombs) return false;
    int32 Previous=0; // 冻结金额单调不减，上限与保存协议一致。
    for(int32 Value:Payouts) { if(Value<Previous) return false; Previous=Value; }
    return true;
}
FDemoMineRound FDemoMineRound::PublicCopy() const
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); FDemoMineRound Copy=*this; /* 值隔离避免清空服务端雷位。 */ Copy.BombMask=0; return Copy; }
int32 FDemoMineRound::OpenCount() const { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return Bits(OpenMask); }
int32 FDemoMineRound::Available() const { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return Payouts.IsValidIndex(OpenCount()-1)?Payouts[OpenCount()-1]:0; }
bool DemoCasino::Matches(const FDemoCasinoIntent& Intent,int32 Number)
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return Intent.Selection==1?Number%2==1:Intent.Selection==2?Number%2==0:Number>=Intent.Low&&Number<=Intent.High; }
int32 DemoCasino::Pay(int32 Stake,double Multiplier)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const double Amount=FMath::FloorToDouble(double(Stake)*Multiplier+1e-8); // double可精确表示int32，先验证再转换；不压低奖金。
    if(Stake<1||Multiplier<0||!FMath::IsFinite(Multiplier)||!FMath::IsFinite(Amount)||Amount<0||Amount>MAX_int32) return INDEX_NONE;
    return int32(Amount);
}
bool DemoCasino::ParseStake(const FString& Text,int32& Stake,FString& Error)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    const FString Digits=Text.TrimStartAndEnd(); // 允许粘贴时首尾空格，不接受符号、小数、指数或分组分隔符。
    Error=TEXT("请输入大于0的整金币数字");
    int64 Value=0; // 每位累加后检查int32，避免Atoi溢出、截断或将非法文本变成0。
    if(Digits.IsEmpty()) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_INPUT rejected empty")); return false; }
    for(TCHAR Digit:Digits)
    {
        if(Digit<TEXT('0')||Digit>TEXT('9')) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_INPUT rejected non-digit")); return false; }
        Value=Value*10+(Digit-TEXT('0'));
        if(Value>MAX_int32) { Error=TEXT("金额超出整数存储范围，请输入较小数字"); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_INPUT rejected integer overflow")); return false; }
    }
    if(Value==0) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_INPUT rejected zero")); return false; }
    Stake=int32(Value); Error.Empty(); return true;
}
FDemoCasinoQuote DemoCasino::Quote(const FDemoCasinoRules& Rules,const FDemoCasinoIntent& Intent)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    FDemoCasinoQuote Quote; // 失败返回明确禁用原因，不提供伪造成功报价。
    // 调用方先Validate配置，避免逐帧重复枚举全部区间；这里仍检查必要索引。
    if(Intent.Mode<0||Intent.Mode>6||Intent.Operation!=0||Intent.Stake<Rules.MinBet)
    { Quote.Error=TEXT("玩法或下注金额超出配置范围"); return Quote; }
    if(Intent.Mode>=3) return QuoteArcade(Rules,Intent);
    if(Intent.Mode==0)
    {
        if(Intent.Selection<0||Intent.Selection>2||Intent.Low<0||Intent.Low>99||Intent.High<Intent.Low||Intent.High>99)
        { Quote.Error=TEXT("数字选择参数非法"); return Quote; } // 先验证边界再相减，远端INT32极值不能触发溢出。
        const int32 Count=Intent.Selection==0?Intent.High-Intent.Low+1:50; // 区间含两端，单双号各50个。
        if(Intent.Selection<0||Intent.Selection>2||Intent.Low<0||Intent.High>99||Count<1||Count>50||!Rules.NumberMultipliers.IsValidIndex(Count-1)||Rules.NumberWeights.Num()!=100)
        { Quote.Error=TEXT("请选择00..99内、最多50个数字的范围"); return Quote; }
        double Total=0,Selected=0; // 按实际权重算概率，而非区间数量假定均匀。
        for(int32 Number=0;Number<100;++Number) { Total+=Rules.NumberWeights[Number]; if(Matches(Intent,Number)) Selected+=Rules.NumberWeights[Number]; }
        Quote.Chance=Total>0?Selected/Total:0; Quote.Multiplier=Rules.NumberMultipliers[Count-1]; Quote.Payout=Pay(Intent.Stake,Quote.Multiplier);
    }
    else if(Intent.Mode==1)
    {
        if(Rules.SymbolWeights.Num()!=5||Rules.TripleMultipliers.Num()!=5) { Quote.Error=TEXT("老虎机配置无效"); return Quote; }
        double Total=0; // 独立转轴质量。
        for(double Weight:Rules.SymbolWeights) Total+=Weight;
        for(int32 A=0;A<5;++A) for(int32 B=0;B<5;++B) for(int32 C=0;C<5;++C)
        {
            const double Multiplier=SlotMultiplier(Rules,{A,B,C}),Probability=Rules.SymbolWeights[A]*Rules.SymbolWeights[B]*Rules.SymbolWeights[C]/FMath::Pow(Total,3.); // 当前组合唯一倍率与实际发生率。
            if(Pay(Intent.Stake,Multiplier)>0) Quote.Chance+=Probability; // 整数取整后真正到账才属于中奖。
            if(Probability>0) Quote.Multiplier=FMath::Max(Quote.Multiplier,Multiplier); // 零权重符号不能抬高可得最高金额。
        }
        Quote.Payout=Pay(Intent.Stake,Quote.Multiplier);
    }
    else
    {
        if(!Rules.BombChoices.Contains(Intent.Bombs)) { Quote.Error=TEXT("请选择配置的货箱难度"); return Quote; }
        Quote.Chance=(25.-Intent.Bombs)/25.; Quote.Multiplier=Rules.MineReturn/Quote.Chance; Quote.Payout=Pay(Intent.Stake,Quote.Multiplier);
    }
    Quote.MaximumPayout=Quote.Payout;
    if(Intent.Mode==2)
    {
        double Survival=1; // 检查最后一级真实乘积，无奖金封顶，不能开箱后才发现冻结表溢出。
        for(int32 Index=0;Index<25-Intent.Bombs;++Index) Survival*=double(25-Intent.Bombs-Index)/(25-Index);
        Quote.MaximumPayout=Pay(Intent.Stake,Rules.MineReturn/Survival);
    }
    if(Quote.Payout<0||Quote.MaximumPayout<0) Quote.Error=TEXT("奖金超出整数存储范围，请降低下注金额");
    return Quote;
}
int32 DemoCasino::Draw(const TArray<double>& Weights,FRandomStream& Random)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    double Total=0; // 调用前已经验证总和>0。
    for(double Weight:Weights) Total+=Weight;
    double Roll=double(Random.GetUnsignedInt())/4294967296.*Total; // 半开[0,total)，零权重不被选中。
    for(int32 Index=0;Index<Weights.Num();++Index) { if(Roll<Weights[Index]) return Index; Roll-=Weights[Index]; }
    for(int32 Index=Weights.Num()-1;Index>=0;--Index) if(Weights[Index]>0) return Index; // 只为浮点累积末位误差兜底。
    return INDEX_NONE;
}
double DemoCasino::SlotMultiplier(const FDemoCasinoRules& Rules,const TArray<int32>& Symbols)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if(Symbols.Num()!=3||Symbols[0]<0||Symbols[0]>4||Symbols[1]<0||Symbols[1]>4||Symbols[2]<0||Symbols[2]>4||Rules.TripleMultipliers.Num()!=5) return 0;
    if(Symbols[0]==Symbols[1]&&Symbols[1]==Symbols[2]) return Rules.TripleMultipliers[Symbols[0]];
    return Symbols[0]==Symbols[1]||Symbols[0]==Symbols[2]||Symbols[1]==Symbols[2]?Rules.PairMultiplier:0;
}
FDemoMineRound DemoCasino::OpenMine(const FDemoCasinoRules& Rules,const FDemoCasinoIntent& Intent,FRandomStream& Random)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    FDemoMineRound Mine; Mine.Stake=Mine.Debited=Intent.Stake; Mine.Bombs=Intent.Bombs; // 本次候选；事务可按Editor实际扣款覆盖Debited，不持有随机流。
    TArray<int32> Tiles; // Fisher-Yates无放回抽样，任一等雷数棋盘概率相同。
    for(int32 Tile=0;Tile<25;++Tile) Tiles.Add(Tile);
    for(int32 Index=0;Index<Mine.Bombs;++Index) { const int32 Pick=Random.RandRange(Index,24); /* 剩余格均匀抽取。 */ Tiles.Swap(Index,Pick); Mine.BombMask|=1<<Tiles[Index]; }
    double Survival=1; // 累计成功概率，冻结后改配置不影响已开的箱子。
    for(int32 Index=0;Index<25-Mine.Bombs;++Index) { Survival*=double(25-Mine.Bombs-Index)/(25-Index); Mine.Payouts.Add(Pay(Mine.Stake,Rules.MineReturn/Survival)); }
    return Mine;
}
bool DemoCasino::AdvanceMine(FDemoMineRound& Mine,const FDemoCasinoIntent& Intent,int32& Gold,FDemoCasinoResult& Result,FString& Error)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    Error=TEXT("货箱状态或操作已过期");
    if(Intent.Mode!=2||Mine.Stake==0||!Mine.Validate()||Intent.Stake!=Mine.Stake||Gold<0) return false;
    FDemoMineRound Next=Mine; // 纯值回滚：全部校验通过才写输入。
    int32 Paid=0; bool Bomb=false; // 本次确定结果，不重新掷骰。
    if(Intent.Operation==1)
    {
        if(Intent.Tile<0||Intent.Tile>=25||(Mine.OpenMask&(1<<Intent.Tile))) return false;
        Bomb=(Mine.BombMask&(1<<Intent.Tile))!=0;
        if(Bomb) Next=FDemoMineRound();
        else { Next.OpenMask|=1<<Intent.Tile; if(Next.OpenCount()==25-Next.Bombs) { Paid=Next.Available(); Next=FDemoMineRound(); } }
    }
    else if(Intent.Operation==2&&Mine.OpenCount()>0) { Paid=Mine.Available(); Next=FDemoMineRound(); }
    else return false;
    if(int64(Gold)+Paid>MAX_int32) { Error=TEXT("金币余额无法容纳到账，请先消费后再领取"); return false; }
    Mine=Next; Gold+=Paid; Result.Paid=Paid; Result.Tile=Intent.Operation==1?Intent.Tile:INDEX_NONE; Result.bBomb=Bomb; Error.Empty(); return true;
}
FString DemoCasino::SymbolName(int32 Index)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const TCHAR* Names[]={TEXT("普通弹"),TEXT("火焰弹"),TEXT("冰冻弹"),TEXT("穿透弹"),TEXT("金币")}; // 与符号配置固定顺序一致。
    return Index>=0&&Index<5?Names[Index]:TEXT("?");
}
