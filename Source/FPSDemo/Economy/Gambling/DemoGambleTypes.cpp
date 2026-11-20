// 职责：抽取账本与展示值结构。使用：保存稳定ID/等级并经天赋目录查询策略；Validate在事务提交和读档时拒绝非法值。
#include "Economy/Gambling/DemoGambleTypes.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Debug/DemoLog.h"
#include "GAS/Talents/DemoTalentCatalog.h"
#include "GAS/Talents/DemoTalentDefinition.h"
int32 DemoGamble::TalentCount()
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return GetDefault<UDemoTalentCatalog>()->Entries().Num(); }
FName DemoGamble::Talent(int32 Index)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const auto& Entries=GetDefault<UDemoTalentCatalog>()->Entries(); // 可信目录只读有序视图；未知索引不构造ID。
    return Entries.IsValidIndex(Index)?Entries[Index]->TalentId:NAME_None;
}
FName DemoGamble::Relic(int32 Index)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const FName Ids[]={TEXT("overload"),TEXT("catalyst"),TEXT("rescue"),TEXT("propulsion")}; // 前两项火力、第三生存、第四战术。
    return Index>=0&&Index<4?Ids[Index]:NAME_None;
}
FString DemoGamble::Name(FName Id)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const TCHAR* Relics[]={TEXT("超载弹匣"),TEXT("燃烧催化器"),TEXT("紧急医疗器"),TEXT("应急推进器")}; // 中文展示。
    if(const UDemoTalentDefinition* Talent=GetDefault<UDemoTalentCatalog>()->Find(Id)) return Talent->DisplayName.ToString(); // 显示名归独立定义资产。
    for(int32 Index=0;Index<4;++Index)if(Id==Relic(Index))return Relics[Index];
    return FString();
}
float DemoGamble::Strength(int32 Rank)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    return Rank>0?1.f+GetDefault<UDemoGambleSettings>()->RankGrowth*FMath::Log2(float(Rank)):0.f;
}
float DemoGamble::Fraction(float Base,int32 Rank)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    return Rank>0?1.f-(1.f-FMath::Clamp(Base,0.f,1.f))/Strength(Rank):0.f;
}
FString DemoGamble::DescribeTalent(FName Id,int32 Rank)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if(Rank<=0) return FString();
    const UDemoTalentDefinition* Definition=GetDefault<UDemoTalentCatalog>()->Find(Id); // 策略输出归定义组合；经济/HUD不重写公式。
    return Definition?Definition->Describe(Rank).ToString():FString();
}

int32 FDemoGambleProgress::Rank(FName Id) const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const int32 Index=Talents.IndexOfByKey(Id); // 查询不补写旧档，保持旧请求的JSON哈希不变。
    return Index==INDEX_NONE?0:TalentRanks.IsValidIndex(Index)?TalentRanks[Index]:1;
}
bool FDemoGambleProgress::UpgradeTalent(FName Id)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs id=%s"),__FUNCTION__,*Id.ToString());
    bool bKnown=false; // 只接收固定目录，不从控制台/远端生成任意Tag。
    for(int32 Index=0;Index<DemoGamble::TalentCount();++Index) bKnown|=Id==DemoGamble::Talent(Index); // 目录游标，不限制已拥有槽数。
    if(!bKnown||!Validate()||Rank(Id)==MAX_int32) { UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_RANK rejected invalid/overflow")); return false; }
    if(TalentRanks.IsEmpty()) TalentRanks.Init(1,Talents.Num()); // 第一次写入等级时将旧档显式迁移为1级。
    const int32 Index=Talents.IndexOfByKey(Id); // 已拥有只升级，不重复保存ID。
    if(Index==INDEX_NONE) { Talents.Add(Id); TalentRanks.Add(1); }
    else ++TalentRanks[Index];
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_RANK id=%s rank=%d"),*Id.ToString(),Rank(Id));
    return true;
}
bool FDemoGambleProgress::ValidateLastSlotsReceipt() const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const FDemoCasinoResult& R=LastSlotsReceipt; // 本人已保存纯值，无未揭雷位/随机种子。
    if (R.Number!=INDEX_NONE || R.Tile!=INDEX_NONE || R.Winner!=INDEX_NONE || R.bBomb || !R.Battle.IsEmpty() || R.Paid<0) return false;
    if (R.Mode==INDEX_NONE) return R.Serial==INDEX_NONE && R.Paid==0 && R.Symbols.IsEmpty(); // 旧档缺字段只能全空，不能隐藏残余奖面。
    if (R.Mode!=1 || R.Serial<1 || R.Serial>Serial || R.Symbols.Num()!=3) return false;
    for (int32 Symbol:R.Symbols) if (Symbol<0 || Symbol>4) return false;
    return true;
}
bool FDemoGambleProgress::Validate() const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 值结构不是UObject，保留TICK级别入口；旧字段不构成玩法额度。
    // Node/Attempts仅做V6旧字段格式校验，新交易不累计也不将其作为次数预算。
    if((!Mine.Validate()||!Arcade.Validate()||!ValidateLastSlotsReceipt()||(Mine.Stake>0&&Arcade.Mode>=0))||Talents.Num()>DemoGamble::TalentCount()||Relics.Num()>4||Failures.Num()!=3||Node<0||Attempts<0||Attempts>100||Serial<0||LastResult.Len()>256)return false;
    for(int32 Value:Failures)if(Value<0||Value>100)return false; // 保存值与配置分离，调整保底不会损坏旧档。
    if(!TalentRanks.IsEmpty()&&TalentRanks.Num()!=Talents.Num()) return false; // 并行数组必须完整，不接受部分迁移。
    for(int32 Level:TalentRanks) if(Level<1) return false; // 只有正等级；int32上界是存储边界，不是策划等级上限。
    TSet<FName> Seen; // 每个集合分别去重，且只接受本类别ID。
    for(FName Id:Talents){bool Known=false; /* 本项白名单结果，包括追加的六项。 */ for(int32 Index=0;Index<DemoGamble::TalentCount();++Index)Known|=Id==DemoGamble::Talent(Index);if(!Known||Seen.Contains(Id))return false;Seen.Add(Id);}
    Seen.Reset();
    for(FName Id:Relics){bool Known=false; /* 不能把天赋ID伪装永久遗物。 */ for(int32 Index=0;Index<4;++Index)Known|=Id==DemoGamble::Relic(Index);if(!Known||Seen.Contains(Id))return false;Seen.Add(Id);}
    return EquippedRelic.IsNone()||Relics.Contains(EquippedRelic);
}
void FDemoGambleProgress::ResetRun()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    Talents.Empty();TalentRanks.Empty();Node=Attempts=0;bRescueUsed=false;LastResult.Empty(); // 不重置Serial/LastSlotsReceipt，保留老虎机事实；旧确认仍不能跨本轮重放。
}
bool UDemoGambleSettings::Validate() const
{
    DEMO_LOG_CALL();
    const float Values[]={AttributeChance,TalentChance,DamageGain,HealthGain,MagazineGain,EvadeHeal,ExplosionBonus,FrozenBonus,PierceBonus,CapacityBonus,ReloadPenalty,BurnExtraSeconds,ExplosionPenalty,DashCooldownReduction,DashDistanceReduction,RescueSeconds}; // 统一拒绝NaN/Inf和负值；金币配置独立校验。
    for(float Value:Values)if(!FMath::IsFinite(Value)||Value<0||Value>10000)return false;
    // 每项显式上下界同样拒绝NaN；不让错误配置进入随机/弹体分配或治疗预算。
    return OverdriveRate>=1&&OverdriveRate<=4&&TwinDamageRatio>0&&TwinDamageRatio<=1&&TwinSpreadDegrees>=0&&TwinSpreadDegrees<=5
        &&RicochetRatio>0&&RicochetRatio<=1&&RicochetRadius>=1&&RicochetRadius<=3000&&SiphonRatio>0&&SiphonRatio<=.2f
        &&SiphonPerSecond>=1&&SiphonPerSecond<=100&&DashLoadFraction>0&&DashLoadFraction<=1
        &&AttributeCost>0&&AttributeCost<=1000000&&TalentCost>0&&TalentCost<=1000000
        &&AttributeChance<=1&&TalentChance<=1&&RankGrowth>0&&RankGrowth<=1 // 不限制天赋种类或抽取次数，等级只增强效果。
        &&DamageGain>0&&HealthGain>0&&MagazineGain>=1&&FMath::IsNearlyEqual(MagazineGain,float(FMath::RoundToInt(MagazineGain)))&&SupplyKills>0&&SupplyKills<=10000
        &&ExplosionPenalty<=.9f&&DashCooldownReduction<=.9f&&DashDistanceReduction<=.9f&&RescueSeconds>=.1f&&RescueSeconds<=10&&CapacityBonus<=1&&ReloadPenalty<=1;
}
