// 职责：第二期娱乐的纯值规则、报价和持续局面验证。使用：Casino权威事务调用；不访问Actor、网络或文件，不在UI重抽结果。
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Debug/DemoLog.h"

namespace
{
    /** Values为固定Count项的权重/倍率；权重至少一项正数。不允许NaN/无穷参与概率运算。 */
    bool FiniteValues(const TArray<double>& Values,int32 Count,bool bWeights)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        double Sum=0; // double有界和，非有限结果拒绝。
        if(Values.Num()!=Count) return false;
        for(double Value:Values) { if(!FMath::IsFinite(Value)||Value<0) return false; Sum+=Value; }
        return FMath::IsFinite(Sum)&&(!bWeights||Sum>0);
    }
}
int32 DemoCasino::ModeForChoice(int32 Choice)
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return Choice>=4&&Choice<=6?Choice-4:Choice>=12&&Choice<=15?Choice-9:INDEX_NONE; }
int32 DemoCasino::ChoiceForMode(int32 Mode)
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return Mode>=0&&Mode<3?Mode+4:Mode>=3&&Mode<=6?Mode+9:INDEX_NONE; }
FString DemoCasino::ModeName(int32 Mode)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const TCHAR* Names[]={TEXT("数字竞猜"),TEXT("军械老虎机"),TEXT("拆弹货箱"),TEXT("反应堆过载"),TEXT("战术骰阵"),TEXT("机械斗兽场"),TEXT("赏金靶场")}; // 协议模式0..6稳定顺序。
    return Mode>=0&&Mode<7?Names[Mode]:TEXT("未知玩法");
}
bool DemoCasino::ValidateArcadeRules(const FDemoCasinoRules& Rules,FString& Error)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    Error=TEXT("扩展玩法配置非法，请检查充能率、骰面、战斗与靶场档位");
    if(Rules.ReactorChances.IsEmpty()||Rules.ReactorChances.Num()>10||!FMath::IsFinite(Rules.ReactorReturn)||Rules.ReactorReturn<=0||Rules.ReactorReturn>1
        ||!FiniteValues(Rules.DiceWeights,6,true)||!FiniteValues(Rules.DiceMultipliers,8,false)
        ||Rules.ArenaHealth.Num()!=2||Rules.ArenaDamage.Num()!=2||Rules.ArenaHit.Num()!=2||!FMath::IsFinite(Rules.ArenaReturn)||Rules.ArenaReturn<=0||Rules.ArenaReturn>1
        ||Rules.RangeSeconds<5||Rules.RangeSeconds>120||Rules.RangeShots<1||Rules.RangeShots>100||Rules.RangeScores.Num()!=5||Rules.RangeScores[0]!=0||!FiniteValues(Rules.RangeMultipliers,5,false)) return false;
    for(double Chance:Rules.ReactorChances) if(!FMath::IsFinite(Chance)||Chance<=0||Chance>1) return false;
    for(int32 Side=0;Side<2;++Side) if(Rules.ArenaHealth[Side]<1||Rules.ArenaHealth[Side]>60||Rules.ArenaDamage[Side]<1||Rules.ArenaDamage[Side]>60||!FMath::IsFinite(Rules.ArenaHit[Side])||Rules.ArenaHit[Side]<=0||Rules.ArenaHit[Side]>1) return false;
    for(int32 Index=1;Index<5;++Index) if(Rules.RangeScores[Index]<=Rules.RangeScores[Index-1]||Rules.RangeScores[Index]>20||Rules.RangeMultipliers[Index]<Rules.RangeMultipliers[Index-1]) return false;
    Error.Empty(); return true;
}
bool FDemoArcadeRound::Validate() const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if(Mode==INDEX_NONE) return Stake==0&&Debited==0&&Step==0&&Values.IsEmpty()&&Chances.IsEmpty()&&Payouts.IsEmpty()&&DeadlineMs==0&&Seed==0&&Shots==0&&Heads==0;
    if(Stake<1||Debited<0||Debited>Stake||Step<0) return false;
    for(int32 Paid:Payouts) if(Paid<0) return false;
    if(Mode==3)
    {
        if(Chances.IsEmpty()||Chances.Num()>10||Payouts.Num()!=Chances.Num()||Step>=Chances.Num()||!Values.IsEmpty()||DeadlineMs!=0||Seed!=0||Shots!=0||Heads!=0) return false;
        for(int32 Index=0;Index<Chances.Num();++Index) if(!FMath::IsFinite(Chances[Index])||Chances[Index]<=0||Chances[Index]>1||(Index>0&&Payouts[Index]<Payouts[Index-1])) return false;
        return true;
    }
    if(Mode==4) return Step==0&&DemoCasino::DiceTier(Values)>=0&&FiniteValues(Chances,6,true)&&Payouts.Num()==8&&DeadlineMs==0&&Seed==0&&Shots==0&&Heads==0;
    if(Mode!=6||Values.Num()!=6||Payouts.Num()!=5||!Chances.IsEmpty()||Step>10||Heads<0||Heads>Step||Shots<Step||Values[0]<1||Values[0]>100||Shots>Values[0]||Values[1]!=0||DeadlineMs<1||DeadlineMs>253402300799999ll) return false;
    for(int32 Index=2;Index<6;++Index) if(Values[Index]<=Values[Index-1]||Values[Index]>20||Payouts[Index-1]<Payouts[Index-2]) return false;
    return true;
}
int32 DemoCasino::DiceTier(const TArray<int32>& Dice)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    int32 Counts[7]={}; // 一次分类计数，索引1..6；对子/顺子不能叠加赔付。
    if(Dice.Num()!=5) return INDEX_NONE;
    for(int32 Face:Dice) { if(Face<1||Face>6) return INDEX_NONE; ++Counts[Face]; }
    int32 Max=0,Pairs=0,Unique=0; // 组合优先级五同>四同>葫芦>顺子>三同>两对>一对。
    for(int32 Face=1;Face<=6;++Face) { Max=FMath::Max(Max,Counts[Face]); Pairs+=Counts[Face]==2; Unique+=Counts[Face]>0; }
    return Max==5?7:Max==4?6:Max==3&&Pairs==1?5:Unique==5&&(Counts[1]==0||Counts[6]==0)?4:Max==3?3:Pairs==2?2:Pairs==1?1:0;
}
FString DemoCasino::DiceTierName(int32 Tier)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const TCHAR* Names[]={TEXT("无组合"),TEXT("一对"),TEXT("两对"),TEXT("三同"),TEXT("顺子"),TEXT("葫芦"),TEXT("四同"),TEXT("五同")}; // 与赔付配置索引一致。
    return Tier>=0&&Tier<8?Names[Tier]:TEXT("无效骰面");
}
double DemoCasino::ArenaChance(const FDemoCasinoRules& Rules)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    double A[61][61]={},B[61][61]={}; // 按递增生命计算两个先手状态，最多60x60，均为本栈数据。
    const double HitA=Rules.ArenaHit[0],HitB=Rules.ArenaHit[1],Denominator=HitA+(1-HitA)*HitB; // 消去双失手；稳定写法避免极低命中时1-1抵消。
    for(int32 HP=1;HP<=Rules.ArenaHealth[0];++HP) A[HP][0]=B[HP][0]=1;
    for(int32 HP=1;HP<=Rules.ArenaHealth[0];++HP) for(int32 Enemy=1;Enemy<=Rules.ArenaHealth[1];++Enemy)
    {
        const double HitWin=B[HP][FMath::Max(0,Enemy-Rules.ArenaDamage[0])],HitLose=A[FMath::Max(0,HP-Rules.ArenaDamage[1])][Enemy]; // 命中后仅访问更低生命状态。
        A[HP][Enemy]=(HitA*HitWin+(1-HitA)*HitB*HitLose)/Denominator;
        B[HP][Enemy]=HitB*HitLose+(1-HitB)*A[HP][Enemy];
    }
    return .5*(A[Rules.ArenaHealth[0]][Rules.ArenaHealth[1]]+B[Rules.ArenaHealth[0]][Rules.ArenaHealth[1]]);
}
FDemoCasinoQuote DemoCasino::QuoteArcade(const FDemoCasinoRules& Rules,const FDemoCasinoIntent& Intent)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    FDemoCasinoQuote Quote; // 独立公开预览，真实值在服务器重新生成。
    if(Intent.Mode==3)
    {
        double Survival=1; // 各步概率乘积，最大赔付是最后一步。
        for(double Chance:Rules.ReactorChances) Survival*=Chance;
        Quote.Chance=Rules.ReactorChances[0]; Quote.Multiplier=Rules.ReactorReturn/Quote.Chance; Quote.Payout=Pay(Intent.Stake,Quote.Multiplier); Quote.MaximumPayout=Pay(Intent.Stake,Rules.ReactorReturn/Survival);
    }
    else if(Intent.Mode==4||Intent.Mode==6)
    {
        const auto& Multipliers=Intent.Mode==4?Rules.DiceMultipliers:Rules.RangeMultipliers; // 骰局策略决定收益，靶场由技术决定，不伪造统一中奖率。
        for(double Value:Multipliers) Quote.Multiplier=FMath::Max(Quote.Multiplier,Value);
        Quote.Payout=Quote.MaximumPayout=Pay(Intent.Stake,Quote.Multiplier);
    }
    else if(Intent.Mode==5)
    {
        if(Intent.Selection<0||Intent.Selection>1) { Quote.Error=TEXT("请选择其中一只机械兽"); return Quote; }
        const double WinA=ArenaChance(Rules); // 实际配置精确胜率，轮次历史不影响。
        Quote.Chance=Intent.Selection==0?WinA:1-WinA;
        if(Quote.Chance<=0) { Quote.Error=TEXT("所选机械兽无法获胜"); return Quote; }
        Quote.Multiplier=Rules.ArenaReturn/Quote.Chance; Quote.Payout=Quote.MaximumPayout=Pay(Intent.Stake,Quote.Multiplier);
    }
    else Quote.Error=TEXT("未知扩展玩法");
    if(Quote.Payout<0||Quote.MaximumPayout<0) Quote.Error=TEXT("奖金超出整数存储范围，请降低下注金额");
    return Quote;
}
FDemoArcadeRound DemoCasino::OpenArcade(const FDemoCasinoRules& Rules,const FDemoCasinoIntent& Intent,FRandomStream& Random)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    FDemoArcadeRound Round; Round.Mode=Intent.Mode; Round.Stake=Round.Debited=Intent.Stake; // 开局冻结合同，外层按真实扣款覆盖Debited。
    if(Intent.Mode==3)
    {
        Round.Chances=Rules.ReactorChances; double Survival=1; // 同一次局面累计生存概率。
        for(double Chance:Round.Chances) { Survival*=Chance; Round.Payouts.Add(Pay(Intent.Stake,Rules.ReactorReturn/Survival)); }
    }
    else if(Intent.Mode==4)
    {
        Round.Chances=Rules.DiceWeights;
        for(int32 Index=0;Index<5;++Index) Round.Values.Add(Draw(Round.Chances,Random)+1);
        for(double Multiplier:Rules.DiceMultipliers) Round.Payouts.Add(Pay(Intent.Stake,Multiplier));
    }
    else if(Intent.Mode==6)
    {
        Round.Values.Add(Rules.RangeShots); Round.Values.Append(Rules.RangeScores);
        for(double Multiplier:Rules.RangeMultipliers) Round.Payouts.Add(Pay(Intent.Stake,Multiplier));
        Round.DeadlineMs=FDateTime::UtcNow().GetTicks()/ETimespan::TicksPerMillisecond-FDateTime(1970,1,1).GetTicks()/ETimespan::TicksPerMillisecond+int64(Rules.RangeSeconds)*1000;
        Round.Seed=int32(Random.GetUnsignedInt());
    }
    if(!Round.Validate()) { UE_LOG(LogFPSDemo,Warning,TEXT("ARCADE_OPEN rejected invalid/overflow candidate")); return FDemoArcadeRound(); }
    return Round;
}
bool DemoCasino::AdvanceArcade(FDemoArcadeRound& Round,const FDemoCasinoIntent& Intent,int32& Gold,FDemoCasinoResult& Result,FRandomStream& Random,FString& Error)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    Error=TEXT("持续玩法操作已过期");
    if(!Round.Validate()||Round.Mode<0||Intent.Mode!=Round.Mode||Intent.Stake!=Round.Stake||Gold<0) return false;
    FDemoArcadeRound Next=Round; int32 Paid=0; bool Finished=false; // 全部在候选计算，溢出保持原局面。
    if(Round.Mode==3)
    {
        if(Intent.Operation==2&&Round.Step>0) { Paid=Round.Payouts[Round.Step-1]; Finished=true; }
        else if(Intent.Operation==1)
        {
            if(double(Random.GetUnsignedInt())/4294967296.>=Round.Chances[Round.Step]) { Finished=true; Result.bBomb=true; }
            else { ++Next.Step; if(Next.Step==Next.Chances.Num()) { Paid=Next.Payouts.Last(); Finished=true; } }
        }
        else return false;
    }
    else if(Round.Mode==4)
    {
        if((Intent.Operation!=1&&Intent.Operation!=2)||Intent.KeepMask<0||Intent.KeepMask>31) return false;
        if(Intent.Operation==1) for(int32 Index=0;Index<5;++Index) if(!(Intent.KeepMask&(1<<Index))) Next.Values[Index]=Draw(Next.Chances,Random)+1;
        Result.Symbols=Next.Values; Result.Winner=DiceTier(Next.Values); Paid=Next.Payouts[Result.Winner]; Finished=true;
    }
    else if(Round.Mode==6&&Intent.Operation==2)
    {
        const int32 Score=Round.Step+Round.Heads; // 一靶一次得分：身体1、头部额外1。
        for(int32 Tier=0;Tier<5;++Tier) if(Score>=Round.Values[Tier+1]) Paid=Round.Payouts[Tier];
        Finished=true;
    }
    else return false;
    if(int64(Gold)+Paid>MAX_int32) { Error=TEXT("到账导致整数溢出，请先消费金币再领取"); return false; }
    if(Finished) Next=FDemoArcadeRound();
    Gold+=Paid; Round=Next; Result.Paid=Paid; Error.Empty(); return true;
}
bool DemoCasino::RunArena(const FDemoCasinoRules& Rules,FRandomStream& Random,FDemoCasinoResult& Result)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    TArray<int32> Health=Rules.ArenaHealth; int32 Side=Random.RandRange(0,1); // 本次战斗独立生命与随机先手，胜率计算使用同一规则。
    Result.Battle.Reset();
    for(int32 Attack=0;Attack<120;++Attack) // 每轮至少一次真实命中，生命上界决定最多120次，极低命中也不超时重抽。
    {
        const double Hit=Rules.ArenaHit[Side],OtherHit=Rules.ArenaHit[1-Side],HitFirst=Hit/(Hit+(1-Hit)*OtherHit); // 精确条件化略过双方都未命中的循环，获胜概率与无限逐回合模拟一致。
        if(double(Random.GetUnsignedInt())/4294967296.<(1-Hit)*(1-OtherHit)) { Result.Battle.Add(Side==0?1:-1); Result.Battle.Add(Side==0?-1:1); } // 连续双失手压缩为一组可视闪避，未改变生命或随机胜者。
        if(double(Random.GetUnsignedInt())/4294967296.>=HitFirst) { Result.Battle.Add(Side==0?1:-1); Side=1-Side; } // 当前方失手，另一方先命中。
        const int32 Damage=Rules.ArenaDamage[Side]; // 本步保证一次命中，下一步从受击方开始。
        Result.Battle.Add((Side==0?1:-1)*(Damage+1)); Health[1-Side]-=Damage;
        if(Health[1-Side]<=0) { Result.Winner=Side; return true; }
        Side=1-Side;
    }
    UE_LOG(LogFPSDemo,Warning,TEXT("ARENA invalid health contract; no wager committed")); return false;
}
