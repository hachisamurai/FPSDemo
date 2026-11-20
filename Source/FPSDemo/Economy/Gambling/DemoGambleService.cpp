// 职责：抽取报价及权威交易规则。使用：商店调用，同步校验钱包/目录/序号，存档失败回滚，不由动画发奖。
#include "Economy/Gambling/DemoGambleService.h"
#include "Debug/DemoEditorCheats.h" // 仅Editor可免扣对应币种；随机概率、等级合法性、情报和保存仍走原事务。
// 对应头文件先包含，确保UBT独立编译检查本类头文件自包含；功能逻辑不变。
#include "Templates/UnrealTemplate.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Economy/Gambling/DemoCasinoSettings.h" // 金币娱乐与银币策略解耦；本文件只保留娱乐入口标题和遗物直购。
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "Save/DemoRunSave.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"
#include "GAS/Talents/DemoTalentDefinition.h" // 奖池只理解内容定义，不依赖天赋的具体GAS执行类型。
#include "GAS/Talents/DemoTalentCatalog.h"
namespace
{
    /** Player为当前命令所属Avatar；不得查询Player0或队友钱包。 */
    UDemoPlayerProgressComponent* Ledger(ADemoCharacter* Player)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);const auto* PS=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr; /* PS拥有账本。 */ return PS?PS->GetProgressComponent():nullptr; }
    /** Player为权威Avatar；只生成当前弹药兼容的天赋池，已有项仍可升阶；金币娱乐不进入此目录。 */
    TArray<FName> Pool(ADemoCharacter* Player)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        TArray<FName> Result; // 同步独立候选值，抽取前构造完毕。
        const UDemoAmmoComponent* Ammo=Player->FindComponentByClass<UDemoAmmoComponent>(); // 服务器实际装配。
        for(const UDemoTalentDefinition* Definition:GetDefault<UDemoTalentCatalog>()->Entries()) // 同步只读内容元数据，不要求存在GA。
            if(Definition->RequiredAmmo<0||(Ammo&&Ammo->GetSelected()==Definition->RequiredAmmo)) Result.Add(Definition->TalentId);
        return Result;
    }
}
FDemoGambleOffer DemoGambleService::Quote(const UDemoShopComponent& Shop,ADemoCharacter* Player,int32 Choice)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 静态服务没有UObject this，使用与TICK相同级别的入口埋点。
    FDemoGambleOffer Offer;Offer.Choice=Choice; // 每次重新计算，不信客户端价格/胜率。
    const auto* LedgerValue=Ledger(Player); // 只读本人账本。
    const auto* State=Shop.GetWorld()->GetGameState<ADemoGameState>(); // 公开阶段决定币种。
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 固定同栈配置。
    const auto* Recovery=GetDefault<UDemoCasinoSettings>(); // 金币独立配置，银币不受其错误影响。
    if(Choice<0||Choice>15||!LedgerValue||!State||!Settings->Validate()||(Choice>=7&&Choice<=11&&(Recovery->RelicGoldCosts.Num()!=4))){Offer.BlockReason=TEXT("赌博配置或玩家不可用");UE_LOG(LogFPSDemo,Warning,TEXT("GAMBLE_QUOTE invalid config/player choice=%d"),Choice);return Offer;}
    const auto& Data=LedgerValue->GetSnapshot(); // 本调用只读引用。
    const auto& Gamble=Data.Gambling; // 个人已拥有奖励和幂等序号，旧次数不参与报价。
    Offer.BlockReason=Shop.GetTerminalBlockReason(Player);
    const auto* Saves=Shop.GetWorld()->GetGameInstance()?Shop.GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr; // 旅行/卸载时不解引用已撤销GI服务。
    if(Offer.BlockReason.IsEmpty()&&(!Saves||Saves->GetActiveSlot()==INDEX_NONE))Offer.BlockReason=TEXT("请先选择存档");
    if(Offer.BlockReason.IsEmpty()&&((Choice<4&&State->Phase!=EDemoPhase::Intermission)||(Choice>=4&&State->Phase!=EDemoPhase::Hub)))Offer.BlockReason=TEXT("银币仅关间使用，金币娱乐/遗物购买装配仅安全区");
    if(Choice<3)
    {
        const TCHAR* Names[]={TEXT("伤害强化"),TEXT("生命强化"),TEXT("弹匣强化")}; // 固定三种策略，数值来自配置。
        const float Gains[]={Settings->DamageGain,Settings->HealthGain,Settings->MagazineGain}; // 单次中奖增量。
        Offer.Title=Names[Choice];Offer.Cost=Settings->AttributeCost;Offer.Chance=Settings->AttributeChance;
        Offer.Detail=FString::Printf(TEXT("成功本局 +%g；失败无奖励；费用仅扣一次"),Gains[Choice]);
        if(Offer.Chance>0.f) Offer.Rewards.Add({FName(*FString::Printf(TEXT("attribute_%d"),Choice)),FString::Printf(TEXT("%s +%g"),Names[Choice],Gains[Choice])}); // 只有实际可能获得的属性进入滚动池。
        const auto* Attr=Player->GetDemoAttributes(); // 合计属性必须容纳本次收益。
        if(!Attr||(Choice==0?Attr->GetWeaponDamageBonus()+Gains[Choice]>10000:Choice==1?Attr->GetMaxHealth()+Gains[Choice]>10000000:Attr->GetMagazineBonus()+Gains[Choice]>10000))Offer.BlockReason=TEXT("属性已达上限");
    }
    else if(Choice==3)
    {
        Offer.Cost=Settings->TalentCost; Offer.Chance=Settings->TalentChance; Offer.Title=TEXT("天赋抽取");
        const TArray<FName> Candidates=Pool(Player); // 当前弹药对应的权威奖池，已拥有天赋仍在池中。
        if(Offer.Chance>0.f) for(FName Id:Candidates) Offer.Rewards.Add({Id,DemoGamble::Name(Id)+FString::Printf(TEXT(" Lv.%d"),Gamble.Rank(Id)<MAX_int32?Gamble.Rank(Id)+1:MAX_int32),DemoGamble::DescribeTalent(Id,Gamble.Rank(Id)<MAX_int32?Gamble.Rank(Id)+1:MAX_int32)});
        Offer.Detail=TEXT("奖池：");
        for(FName Id:Candidates)Offer.Detail+=DemoGamble::Name(Id)+TEXT("（")+DemoGamble::DescribeTalent(Id,Gamble.Rank(Id)<MAX_int32?Gamble.Rank(Id)+1:MAX_int32)+TEXT("）；");
        if(Candidates.IsEmpty())Offer.BlockReason=TEXT("奖池已全部拥有");
        for(FName Id:Candidates) if(Gamble.Rank(Id)==MAX_int32) Offer.BlockReason=TEXT("天赋等级超出存储范围"); // 扣费前检查极端溢出，绝不悄悄吞掉重复奖励。
    }
    else if(DemoCasino::ModeForChoice(Choice)>=0)
    {
        const TCHAR* Titles[]={TEXT("数字竞猜"),TEXT("军械老虎机"),TEXT("拆弹货箱")}; // 固定入口，详细规则由Casino模块单独报价。
        Offer.Title=DemoCasino::ModeName(DemoCasino::ModeForChoice(Choice)); Offer.Cost=Recovery->Rules.MinBet; Offer.Detail=TEXT("选择下注金额与玩法选项，所有倍率包含本金");
    }
    else
    {
        const FName Id=DemoGamble::Relic(Choice-8); // 7表示None卸下。
        Offer.Title=Choice==7?TEXT("卸下遗物"):DemoGamble::Name(Id);Offer.Chance=1;
        const TCHAR* Details[]={TEXT("容量+20%，换弹时间+15%"),TEXT("灼烧持续+1秒，爆炸伤害-10%"),TEXT("每轮首次致死保留1HP，保护1秒"),TEXT("冲刺冷却-15%，距离-10%")}; // 下方按实际配置格式化覆盖，避免调参后旧描述。
        Offer.Detail=Choice==7?TEXT("仅安全区更换，已消耗的医疗次数不恢复"):Details[Choice-8];
        if(Choice==8)Offer.Detail=FString::Printf(TEXT("容量+%.0f%%，换弹时间+%.0f%%"),Settings->CapacityBonus*100,Settings->ReloadPenalty*100);
        if(Choice==9)Offer.Detail=FString::Printf(TEXT("灼烧+%g秒，爆炸-%.0f%%"),Settings->BurnExtraSeconds,Settings->ExplosionPenalty*100);
        if(Choice==10)Offer.Detail=FString::Printf(TEXT("每轮首次致死保留1HP，保护%g秒"),Settings->RescueSeconds);
        if(Choice==11)Offer.Detail=FString::Printf(TEXT("冲刺冷却-%.0f%%，距离-%.0f%%"),Settings->DashCooldownReduction*100,Settings->DashDistanceReduction*100);
        if(Choice>7&&!Gamble.Relics.Contains(Id))
        {
            Offer.Cost=Recovery->RelicGoldCosts[Choice-8];
            if(Offer.Cost<1||Offer.Cost>10000000) Offer.BlockReason=TEXT("遗物价格配置非法");
            Offer.Detail+=FString::Printf(TEXT("；%d金币永久购买并装配，已有遗物免费切换。"),Offer.Cost);
        }
        if(Gamble.EquippedRelic==Id)Offer.BlockReason=TEXT("当前已装配");
    }
    if(Choice<4&&Offer.Chance<1.f) Offer.Rewards.Add({FName(TEXT("no_reward")),TEXT("未获得奖励")}); // 金币娱乐结果由独立模块处理，不进入银币滚动卡池。
    // 抽取不设节点/单局次数上限；仍由本次真实奖池、等级合法性和余额决定是否可提交。
    int32 RequiredCost=Offer.Cost; // 原报价仍显示配置值；余额验证按本次实际扣款。
#if WITH_EDITOR
    RequiredCost=DemoEditorCheats::Debit(Player,Choice>=4,Offer.Cost);
#endif
    if((Choice<4?Data.SilverCoins:Data.Coins)<RequiredCost)Offer.BlockReason=TEXT("个人货币不足");
    if(Gamble.Serial==MAX_int32)Offer.BlockReason=TEXT("交易序号已达上限");
    return Offer;
}
FDemoCommandResult UDemoShopComponent::Gamble(ADemoCharacter* Player,int32 Choice,int32 ExpectedPrice,int32 Serial)
{
    DEMO_LOG_CALL(); // 每次独立下注都经同一权威事务并留有调用记录。
    if(DemoCasino::ModeForChoice(Choice)>=0) return FDemoCommandResult::Reject(TEXT("RetiredRecovery"),TEXT("信标回收已替换，请使用金币娱乐页面"));
    const FDemoGambleOffer Offer=DemoGambleService::Quote(*this,Player,Choice); // 权威重验，随机数仅在通过全部校验后生成。
    UDemoPlayerProgressComponent* Progress=Ledger(Player); // 同步事务唯一可变账本。
    if(!Offer.BlockReason.IsEmpty()||!Progress||Offer.Cost!=ExpectedPrice||Progress->GetSnapshot().Gambling.Serial!=Serial||!SaveCheckpointRequest.IsBound())
    {
        UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REJECT choice=%d quote=%d serial=%d reason=%s"),Choice,ExpectedPrice,Serial,*Offer.BlockReason); // 包体编译去除调用级拒绝细节，保留关键提交/失败日志。
        return FDemoCommandResult::Reject(TEXT("GambleRejected"),Offer.BlockReason.IsEmpty()?TEXT("报价或确认已过期，请重新确认"):Offer.BlockReason);
    }
    const TGuardValue<bool> Guard(bTransactionInProgress,true); // 同游戏线程跨GAS/保存回调防重入，离栈自动释放。
    const FDemoPlayerProgressSnapshot Before=Progress->GetSnapshot(); // 交易完整回滚副本。
    FDemoPlayerProgressSnapshot Next=Before; // 不修改其他玩家，也不影响普通升级价格。
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 同栈冻结参数。
    const bool Won=Offer.Chance>=1.f||FMath::FRand()<Offer.Chance; // 结果保存前不发布；客户端不提供随机值。
    FName ResultId=TEXT("no_reward"); // 与实际随机选择同栈写入回执，UI不重新开奖。
    int32 Debit=Offer.Cost; // 实际扣款；只在Editor单人无限币模式免扣，其他事务限制不变。
#if WITH_EDITOR
    Debit=DemoEditorCheats::Debit(Player,Choice>=4,Offer.Cost);
#endif
    // V6的Node/Attempts仅保留旧档兼容，停止累计以免旧0..100校验形成隐藏次数上限。
    if(Choice<4) Next.SilverCoins-=Debit;
    else Next.Coins-=Debit;
    Next.Gambling.Serial++;
    Next.Gambling.LastResult=Won?TEXT("成功："):TEXT("本次未获得奖励，已消耗下注费用");
#if WITH_EDITOR
    if(!Won&&Debit==0&&Offer.Cost>0) Next.Gambling.LastResult=TEXT("测试模式：未获得奖励，本次未扣币"); // 不向测试者显示虚假的扣款反馈。
#endif
    if(Choice>=7)
    {
        Next.Gambling.EquippedRelic=DemoGamble::Relic(Choice-8);
        if(Choice>7) Next.Gambling.Relics.AddUnique(Next.Gambling.EquippedRelic); // 解锁与装配同一保存，失败都回滚。
        Next.Gambling.LastResult=Choice==7?TEXT("已卸下遗物"):(Offer.Cost>0?FString::Printf(TEXT("消耗%d金币，已购买并装配："),Debit):FString(TEXT("已装配：")))+Offer.Title;
    }
    else if(Choice==3&&Won)
    {
        const TArray<FName> Candidates=Pool(Player); // 同栈报价已经验证非空，中奖仅选择一次。
        ResultId=Candidates[FMath::RandHelper(Candidates.Num())];
        if(!Next.Gambling.UpgradeTalent(ResultId)) return FDemoCommandResult::Reject(TEXT("InvalidTalent"),TEXT("天赋等级异常，本次未扣费"));
        Next.Gambling.LastResult+=DemoGamble::Name(ResultId)+FString::Printf(TEXT(" Lv.%d"),Next.Gambling.Rank(ResultId));
    }
    else if(Won){Next.Gambling.LastResult+=Offer.Title;ResultId=FName(*FString::Printf(TEXT("attribute_%d"),Choice));} // 三属性结果与报价卡片使用相同稳定ID。
    UAbilitySystemComponent* ASC=Player->GetAbilitySystemComponent(); // 保存捕获真实GAS基础值，失败恢复原值。
    const FGameplayAttribute Attribute=Choice==0?UDemoAttributeSet::GetWeaponDamageBonusAttribute():Choice==1?UDemoAttributeSet::GetMaxHealthAttribute():UDemoAttributeSet::GetMagazineBonusAttribute(); // 只有属性中奖才使用。
    const float OldValue=ASC->GetNumericAttributeBase(Attribute),OldHealth=Player->GetDemoAttributes()->GetHealth(); // 回滚必须恢复生命和上限，不能留下免费治疗。
    FDemoLoadoutSnapshot BeforeLoadout; Player->GetWeaponComponent()->CaptureSnapshot(BeforeLoadout); // 更换容量遗物也必须支持保存失败恢复弹量。
    bool Applied=Progress->Commit(Next,false); // 保存器可读候选，但不广播可能提前揭晓结果的菜单RPC。
    if(Applied&&Won&&Choice<3)
    {
        const TSubclassOf<UGameplayEffect> Effect=Choice==0?UDemoPowerEffect::StaticClass():Choice==1?UDemoMaxHealthEffect::StaticClass():UDemoMagazineEffect::StaticClass(); // GAS固定白名单。
        const float Gain=Choice==0?Settings->DamageGain:Choice==1?Settings->HealthGain:Settings->MagazineGain; // 与投影同配置。
        Applied=DemoEffects::Apply(ASC,ASC,Effect,Gain);
        if(Applied&&Choice==1)Applied=DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),Gain);
    }
    if(Applied&&Choice>=7) Player->GetWeaponComponent()->ReconcileGambleCapacity(); // 回收多余子弹与新遗物一并保存。
    if(!Applied||!SaveCheckpointRequest.Execute())
    {
        Progress->Commit(Before,false); Player->GetWeaponComponent()->ReconcileGambleCapacity(&BeforeLoadout); ASC->SetNumericAttributeBase(Attribute,OldValue);ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),OldHealth);
        UE_LOG(LogFPSDemo,Warning,TEXT("GAMBLE_ROLLBACK player=%s choice=%d"),*Player->GetName(),Choice);
        return FDemoCommandResult::Reject(TEXT("SaveFailed"),TEXT("保存失败，本次费用与收益已全部撤销，请重试"));
    }
    Progress->PublishCommitted(); // 同一同步栈内保存已成功，此时才允许拥有者看到结果。
    UE_LOG(LogFPSDemo,Display,TEXT("GAMBLE_COMMITTED player=%s serial=%d choice=%d cost=%d won=%d goldDelta=%d gold=%d"),*Player->GetName(),Next.Gambling.Serial,Choice,Debit,Won,Next.Coins-Before.Coins,Next.Coins); // 关键日志报告净钱包变化，区分到账与本金。
    FDemoCommandResult Result=FDemoCommandResult::Applied(true,Next.Gambling.LastResult); // 仅落盘成功后复制已冻结奖池；不会改变V6存档字段/hash。
    if(Choice<7){Result.GambleReveal.Serial=Next.Gambling.Serial;Result.GambleReveal.Choice=Choice;Result.GambleReveal.Rewards=Offer.Rewards;Result.GambleReveal.ResultId=ResultId;}
    return Result;
}
