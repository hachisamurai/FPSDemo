// 职责：无业务副作用的奖励带状态。使用：Begin校验权威结果，Update逐帧或Finish立即停靠；不执行随机、扣费或保存。
#include "UI/Flow/DemoGambleReel.h"
#include "Debug/DemoLog.h"

bool FDemoGambleReel::Begin(const FDemoGambleReveal& Reveal, const FString& Message, double Now)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    Reset();
    int32 ResultIndex=INDEX_NONE; // 精确ID匹配，不用中奖名称或新报价反推。
    TSet<FName> Seen; // 防止异常重复卡片让停靠语义不确定；只活到本次校验结束。
    for(int32 Index=0;Index<Reveal.Rewards.Num();++Index) // 目录最多64种候选加失败卡；仅为网络资源边界，不是抽取次数或携带限制。
    {
        const FDemoGambleReward& Card=Reveal.Rewards[Index]; // 同栈借用可靠回执。
        if(Card.Id.IsNone()||Card.Title.IsEmpty()||Seen.Contains(Card.Id))
        { UE_LOG(LogFPSDemo,Warning,TEXT("GAMBLE_REEL rejected duplicate/empty reward card")); return false; }
        Seen.Add(Card.Id);
        if(Card.Id==Reveal.ResultId) ResultIndex=Index;
    }
    if(Reveal.Serial<1||Reveal.Choice<0||Reveal.Choice>6||Reveal.Rewards.Num()>65||ResultIndex==INDEX_NONE||!FMath::IsFinite(Now))
    { UE_LOG(LogFPSDemo,Warning,TEXT("GAMBLE_REEL rejected invalid committed presentation")); return false; }
    Cards=Reveal.Rewards; ResultId=Reveal.ResultId; ResultMessage=Message; StartSeconds=Now;
    Target=Cards.Num()*FMath::Max(4,FMath::DivideAndRoundUp(20,Cards.Num()))+ResultIndex; // 无重复开奖；保证小奖池也有足够的可见滚动距离。
    bRolling=true;
    UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REEL_BEGIN serial=%d choice=%d cards=%d target=%s"),Reveal.Serial,Reveal.Choice,Cards.Num(),*ResultId.ToString());
    return true;
}
bool FDemoGambleReel::Update(double Now)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if(!bRolling) return false;
    const float Alpha=FMath::Clamp(static_cast<float>((Now-StartSeconds)/Duration),0.f,1.f); // 绝对时间抗掉帧；暂停仍继续展示。
    Position=Target*(1.f-FMath::Pow(1.f-Alpha,3.f)); // 三次减速曲线，终点速度为0，不跳到另一个奖品。
    if(Alpha<1.f) return false;
    Position=static_cast<float>(Target); bRolling=false;
    UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REEL_FINISH result=%s"),*ResultId.ToString());
    return true;
}
void FDemoGambleReel::Reset()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    Cards.Reset(); ResultMessage.Empty(); ResultId=NAME_None; Position=0; Target=0; StartSeconds=0; bRolling=false;
}

bool FDemoGambleReel::Finish()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if(!bRolling||Cards.IsEmpty()) return false;
    Position=static_cast<float>(Target); bRolling=false;
    UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REEL_SKIP result=%s"),*ResultId.ToString());
    return true;
}
