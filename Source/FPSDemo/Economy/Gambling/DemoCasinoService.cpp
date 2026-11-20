// 职责：七种金币玩法权威事务。使用：拥有者命令调用Shop.Casino，依赖同步保存委托；不在UI动画结束时发奖。
#include "Economy/DemoShopComponent.h"
#include "Economy/Gambling/DemoCasinoSettings.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Economy/Gambling/DemoCasinoRange.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Game/DemoGameState.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoEditorCheats.h"
#include "Debug/DemoLog.h"
#include "Templates/UnrealTemplate.h"

FDemoCommandResult UDemoShopComponent::Casino(ADemoCharacter* Player,const FDemoCasinoIntent& Intent,int32 Serial,FName DeviceId,const FGuid& DeviceToken,uint32 WorldEpoch)
{
    DEMO_LOG_CALL();
    const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 当前World公开阶段，不能使用旧终端。
    auto* PS=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr; // 仅当前拥有者账本。
    auto* Progress=PS?PS->GetProgressComponent():nullptr; // 服务器权威完整进度。
    const auto* Saves=GetWorld()&&GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr; // 当前GI保存服务。
    FString Error=DeviceToken.IsValid()?GetCasinoDeviceBlockReason(Player,DeviceId,DeviceToken,WorldEpoch,Intent.Mode):GetTerminalBlockReason(Player); // 实体设备用独立租约；旧终端继续原会话，不复制赔率或业务事务。
    const FDemoCasinoDeviceLease* Lease=DeviceToken.IsValid()?DeviceLeases.Find(DeviceId):nullptr; // 物理上下文只由服务器开租约时冻结。
    if (Error.IsEmpty() && Lease && Lease->ControlsRevision && ExecutingPhysicalToken!=DeviceToken) Error=TEXT("当前设备需要瞄准实际操作件"); // 旧Casino RPC即使持真Token也不能绕过控件首次命中验证；页面/旧终端不受此门影响。
    if(Error.IsEmpty()&&bTransactionInProgress) Error=TEXT("正在保存另一笔交易，请稍后再试"); // 同一服务串行；投影广播不因本标志撤销合法设备会话。
    if(Error.IsEmpty()&&(!State||State->Phase!=EDemoPhase::Hub)) Error=TEXT("金币娱乐仅安全区开放");
    if(Error.IsEmpty()&&(!Progress||!Saves||Saves->GetActiveSlot()==INDEX_NONE||!SaveCheckpointRequest.IsBound())) Error=TEXT("请先选择有效存档");
    if(Error.IsEmpty()&&(Serial<0||Serial==MAX_int32||Serial!=Progress->GetSnapshot().Gambling.Serial)) Error=TEXT("操作已过期，请重新确认");
    if(!Error.IsEmpty()) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_REJECT %s"),*Error); return FDemoCommandResult::Reject(TEXT("CasinoRejected"),Error); }
    if(Intent.Mode==6&&Intent.Operation==3)
    {
        if(!ADemoCasinoRange::Start(Player)) return FDemoCommandResult::Reject(TEXT("RangeFailed"),TEXT("训练靶不可用，保存的挑战仍保留"));
        return FDemoCommandResult::Applied(false,TEXT("训练已恢复，请瞄准靶子按左键"));
    }
    const TGuardValue<bool> Guard(bTransactionInProgress,true); // 同步游戏线程防保存回调重入，退出即释放。
    const FDemoPlayerProgressSnapshot Before=Progress->GetSnapshot(); // 包含服务端私有雷位的完整回滚副本，不传RPC。
    FDemoPlayerProgressSnapshot Next=Before; // 所有玩法只写候选钱包和货箱。
    FDemoCasinoResult Reveal; Reveal.Mode=Intent.Mode; Reveal.Serial=Serial+1; // 保存成功才允许回传的确定结果。
    int32 Debit=0; // 已开箱翻格和领取不再次扣款。
    if(Intent.Operation==0)
    {
        const auto& Rules=GetDefault<UDemoCasinoSettings>()->Rules; // 同步冻结CDO配置；已开箱操作不依赖新配置。
        if(!Rules.Validate(Error)) return FDemoCommandResult::Reject(TEXT("CasinoConfig"),Error);
        if(Intent.RulesKey!=Rules.Fingerprint()) return FDemoCommandResult::Reject(TEXT("CasinoChanged"),TEXT("配置已更新，请重新打开确认"));
        const auto Quote=DemoCasino::Quote(Rules,Intent); // 权威重算合法金额、选择与赔率。
        if(!Quote.Error.IsEmpty()) return FDemoCommandResult::Reject(TEXT("CasinoQuote"),Quote.Error);
        if(Next.Gambling.Mine.Stake>0||Next.Gambling.Arcade.Mode>=0) return FDemoCommandResult::Reject(TEXT("MinePending"),TEXT("请先完成当前未结的娱乐项目"));
        Debit=Intent.Stake;
#if WITH_EDITOR
        Debit=DemoEditorCheats::Debit(Player,true,Debit); // 仅Editor免扣；正式包不存在此控制入口。
#endif
        if(Next.Coins<Debit) return FDemoCommandResult::Reject(TEXT("CasinoFunds"),TEXT("个人金币不足"));
        // 以最大可能到账验证钱包空间，避免开奖成功却无法提交；货箱按完整最后一级金额检查。
        const int32 Maximum=Quote.MaximumPayout; // 不截断玩家收益。
        if(int64(Next.Coins)-Debit+Maximum>MAX_int32) return FDemoCommandResult::Reject(TEXT("CasinoOverflow"),TEXT("金币余额接近上限，请先消费"));
        Next.Coins-=Debit;
        const FGuid Entropy=FGuid::NewGuid(); // 每次合法新下注独立服务器种子，不接受客户端种子、不保存到公开结构。
        FRandomStream Random(int32(Entropy.A^Entropy.B^Entropy.C^Entropy.D)); // 此栈独占；测试通过纯规则的显式随机流复现。
        if(Intent.Mode==0)
        {
            Reveal.Number=DemoCasino::Draw(Rules.NumberWeights,Random);
            Reveal.Paid=DemoCasino::Matches(Intent,Reveal.Number)?Quote.Payout:0;
            Next.Gambling.LastResult=FString::Printf(TEXT("抽取完成：数字 %02d，到账 %d，净收益 %+d 金币"),Reveal.Number,Reveal.Paid,Reveal.Paid-Debit);
        }
        else if(Intent.Mode==1)
        {
            for(int32 Reel=0;Reel<3;++Reel) Reveal.Symbols.Add(DemoCasino::Draw(Rules.SymbolWeights,Random));
            Reveal.Paid=DemoCasino::Pay(Intent.Stake,DemoCasino::SlotMultiplier(Rules,Reveal.Symbols));
            Next.Gambling.LastResult=FString::Printf(TEXT("抽取完成：%s / %s / %s，到账 %d，净收益 %+d 金币"),*DemoCasino::SymbolName(Reveal.Symbols[0]),*DemoCasino::SymbolName(Reveal.Symbols[1]),*DemoCasino::SymbolName(Reveal.Symbols[2]),Reveal.Paid,Reveal.Paid-Debit);
        }
        else if(Intent.Mode>=3)
        {
            if(Intent.Mode==5)
            {
                if(!DemoCasino::RunArena(Rules,Random,Reveal)) return FDemoCommandResult::Reject(TEXT("ArenaFailed"),TEXT("战斗模拟失败，未扣款"));
                Reveal.Paid=Reveal.Winner==Intent.Selection?Quote.Payout:0;
                Next.Gambling.LastResult=FString::Printf(TEXT("斗兽场结束：%s胜利，到账%d，净收益%+d金币"),Reveal.Winner==0?TEXT("磁轨兽"):TEXT("装甲蛛"),Reveal.Paid,Reveal.Paid-Debit);
            }
            else
            {
                Next.Gambling.Arcade=DemoCasino::OpenArcade(Rules,Intent,Random); Next.Gambling.Arcade.Debited=Debit;
                if(Next.Gambling.Arcade.Mode<0) return FDemoCommandResult::Reject(TEXT("ArcadeFailed"),TEXT("无法创建合法局面，未扣款"));
                Next.Gambling.LastResult=FString::Printf(TEXT("%s已投入%d金币，局面已保存"),*DemoCasino::ModeName(Intent.Mode),Debit);
            }
        }
        else { Next.Gambling.Mine=DemoCasino::OpenMine(Rules,Intent,Random); Next.Gambling.Mine.Debited=Debit; Next.Gambling.LastResult=FString::Printf(TEXT("已投入 %d 金币，货箱已保存，请选择安全格"),Debit); }
        Next.Coins+=Reveal.Paid;
    }
    else if(Intent.Mode>=3)
    {
        const FGuid Entropy=FGuid::NewGuid(); FRandomStream Random(int32(Entropy.A^Entropy.B^Entropy.C^Entropy.D)); // 每步服务器随机，失败保存不公开候选结果。
        if(!DemoCasino::AdvanceArcade(Next.Gambling.Arcade,Intent,Next.Coins,Reveal,Random,Error)) return FDemoCommandResult::Reject(TEXT("ArcadeRejected"),Error);
        Next.Gambling.LastResult=Next.Gambling.Arcade.Mode<0?FString::Printf(TEXT("%s完成：到账%d，净收益%+d金币"),*DemoCasino::ModeName(Intent.Mode),Reveal.Paid,Reveal.Paid-Before.Gambling.Arcade.Debited):FString::Printf(TEXT("充能成功，第%d级已保存"),Next.Gambling.Arcade.Step);
    }
    else
    {
        if(!DemoCasino::AdvanceMine(Next.Gambling.Mine,Intent,Next.Coins,Reveal,Error))
        { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_MINE_REJECT %s"),*Error); return FDemoCommandResult::Reject(TEXT("MineRejected"),Error); }
        Next.Gambling.LastResult=Reveal.bBomb?FString::Printf(TEXT("拆弹失败：踩中爆炸格，本箱投入 %d 金币已消耗"),Before.Gambling.Mine.Debited):Next.Gambling.Mine.Stake==0?FString::Printf(TEXT("抽取完成：货箱到账 %d，净收益 %+d 金币"),Reveal.Paid,Reveal.Paid-Before.Gambling.Mine.Debited):FString::Printf(TEXT("安全格已保存，当前可领 %d 金币"),Next.Gambling.Mine.Available());
    }
    Next.Gambling.Serial=Reveal.Serial;
    if (Intent.Mode==1 && Intent.Operation==0) Next.Gambling.LastSlotsReceipt=Reveal; // 与原钱包候选在同一Save前写入；失败整份Before回滚，其他模式不覆盖此私人已结事实。
    if(!Progress->Commit(Next,false)||!SaveCheckpointRequest.Execute())
    {
        Progress->Commit(Before,false);
        UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_ROLLBACK player=%s mode=%d operation=%d"),*Player->GetName(),Intent.Mode,Intent.Operation);
        return FDemoCommandResult::Reject(TEXT("SaveFailed"),TEXT("保存失败，金币与货箱已恢复，本次操作未生效"));
    }
    Progress->PublishCommitted();
    const bool RangeUnavailable=Intent.Mode==6&&Intent.Operation==0&&!ADemoCasinoRange::Start(Player); // 已保存下注不能当成未扣款失败；场地不可用时保留合同并显示恢复入口。
    UE_LOG(LogFPSDemo,Display,TEXT("CASINO_COMMITTED player=%s serial=%d mode=%d operation=%d debit=%d paid=%d gold=%d"),*Player->GetName(),Reveal.Serial,Intent.Mode,Intent.Operation,Debit,Reveal.Paid,Next.Coins); // 关键成交，永不打印私有棋盘。
    FDemoCommandResult Result=FDemoCommandResult::Applied(true,Next.Gambling.LastResult); // 回执只含已公开结果，无未来雷位。
    if(RangeUnavailable) { Result.Code=TEXT("RangeUnavailable"); Result.Message=TEXT("挑战已保存，但训练场地暂不可用。可重试进入，或提前结束领取当前成绩；原时限继续计时。"); UE_LOG(LogFPSDemo,Warning,TEXT("RANGE_ROOM unavailable after saved bet; contract preserved")); }
    Result.CasinoResult=Reveal; return Result;
}
