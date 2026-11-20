// 职责：拒绝非法兵种参数，并集中定义特殊小怪替换槽白名单/上限；供表验证、生成计划、并发容量和直接出生握手使用，依赖DemoLog。
#include "AI/DemoEnemyRosterSettings.h"
#include "Debug/DemoLog.h"
bool FDemoEnemyRosterSettings::IsValid() const
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    // 所有参与时间/空间计算的字段先检查有限，防止NaN让前摇和寻路永久挂起；Beacon为当前最大合法枚举值。
    if (static_cast<uint8>(Kind)>static_cast<uint8>(EDemoRosterKind::Beacon)
        ||!FMath::IsFinite(Shield)||Shield<0||Shield>1000000
        ||!FMath::IsFinite(ShieldArc)||ShieldArc<=0||ShieldArc>180
        ||!FMath::IsFinite(ShieldAbsorb)||ShieldAbsorb<0||ShieldAbsorb>1
        ||!FMath::IsFinite(Stagger)||Stagger<.1f||Stagger>10
        ||!FMath::IsFinite(Cooldown)||Cooldown<.5f||Cooldown>60
        ||!FMath::IsFinite(Windup)||Windup<.7f||Windup>10
        ||!FMath::IsFinite(Radius)||Radius<50||Radius>800
        ||!FMath::IsFinite(HealRange)||HealRange<100||HealRange>2000
        ||!FMath::IsFinite(HealFraction)||HealFraction<=0||HealFraction>.2f
        ||!FMath::IsFinite(InterruptFraction)||InterruptFraction<=0||InterruptFraction>1
        ||!FMath::IsFinite(Recovery)||Recovery<1||Recovery>5) return false;
    // 地面落点/锁定光束都需要至少1.2秒可读预警，难度不能把它缩到无法躲避。
    if ((Kind==EDemoRosterKind::Bombardier||Kind==EDemoRosterKind::Weaver||Kind==EDemoRosterKind::Beacon)&&Windup<1.2f) return false;
    if (Kind==EDemoRosterKind::Mender&&Windup<1.5f) return false;
    return Kind!=EDemoRosterKind::Exploder||Windup>=1.f;
}
bool DemoRosterRules::IsSpecialMinion(EDemoRosterKind Kind)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 自由函数无this，不能用DEMO_LOG_TICK；生成容量检查按调度频率调用，只走逐帧级别。
    return Kind==EDemoRosterKind::Guard||Kind==EDemoRosterKind::Bombardier||Kind==EDemoRosterKind::Mender
        ||Kind==EDemoRosterKind::Exploder||Kind==EDemoRosterKind::Beacon;
}
int32 DemoRosterRules::SpecialCap(EDemoRosterKind Kind)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    // 修复者只允许1只，避免互相治疗形成拖延；其余特殊小怪2只，与原2/2/1/2规则一致，封锁眼同为2。
    return !IsSpecialMinion(Kind)?0:Kind==EDemoRosterKind::Mender?1:2;
}
