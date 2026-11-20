// 职责：定义资产验证与无状态策略组合。用法：Catalog首次加载、管理组件授予与武器快照调用；不根据天赋ID分派行为。
#include "GAS/Talents/DemoTalentDefinition.h"
#include "GAS/Talents/DemoTalentModifier.h"
#include "GAS/Talents/DemoTalentAbility.h"
#include "GameplayEffect.h"
#include "Debug/DemoLog.h"
namespace
{
    /** Definition为当前被校验资产，Reason为静态失败原因；统一记录拒绝，便于定位坏配置而非仅返回false。 */
    bool RejectDefinition(const UDemoTalentDefinition* Definition,const TCHAR* Reason)
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("[CALL] RejectDefinition asset=%s reason=%s"),*GetNameSafe(Definition),Reason);
        return false;
    }
}
bool UDemoTalentDefinition::Validate() const
{
    DEMO_LOG_CALL();
    if(TalentId.IsNone()||!TalentTag.IsValid()||DisplayName.IsEmpty()||RequiredAmmo < -1||RequiredAmmo > 3) return RejectDefinition(this,TEXT("identity/display/ammo"));
    for(const TSubclassOf<UDemoTalentModifier>& Class:Modifiers) // 只读可信内容引用，不从玩家账本加载类。
        if(!Class||Class->HasAnyClassFlags(CLASS_Abstract)) return RejectDefinition(this,TEXT("missing/abstract modifier"));
    TSet<UClass*> UniqueAbilities; // 同定义内不能重复授予同一类，避免同一事件执行两次。
    for(const TSubclassOf<UDemoTalentAbility>& Class:Abilities)
    {
        if(!Class||Class->HasAnyClassFlags(CLASS_Abstract)||UniqueAbilities.Contains(Class.Get())) return RejectDefinition(this,TEXT("missing/abstract/duplicate ability"));
        UniqueAbilities.Add(Class.Get());
    }
    for(const TSubclassOf<UGameplayEffect>& Class:PassiveEffects) // 只支持可逆持有效果；不让重建带来周期伤害/消费。
    {
        const UGameplayEffect* Effect=Class?Class->GetDefaultObject<UGameplayEffect>():nullptr; // CDO借用，只读。
        if(!Effect||Class->HasAnyClassFlags(CLASS_Abstract)||Effect->DurationPolicy!=EGameplayEffectDurationType::Infinite
            ||Effect->StackingType!=EGameplayEffectStackingType::None||Effect->Period.GetValueAtLevel(1.f)!=0.f) return RejectDefinition(this,TEXT("passive must be infinite/nonstacking/nonperiodic"));
    }
    return !Modifiers.IsEmpty()||!Abilities.IsEmpty()||!PassiveEffects.IsEmpty()?true:RejectDefinition(this,TEXT("empty implementation"));
}
void UDemoTalentDefinition::BuildModifiers(int32 Rank,FDemoTalentModifiers& Output) const
{
    DEMO_LOG_TICK();
    for(const TSubclassOf<UDemoTalentModifier>& Class:Modifiers) // 排序由内容定义固定，等级不能增加运行实体数量。
        if(Class) Class->GetDefaultObject<UDemoTalentModifier>()->BuildModifiers(Rank,Output);
}
FText UDemoTalentDefinition::Describe(int32 Rank) const
{
    DEMO_LOG_TICK();
    if(!Description.IsEmpty()) return Description;
    TArray<FString> Parts; // 当前UI请求局部文本；无世界对象或存档所有权。
    for(const TSubclassOf<UDemoTalentModifier>& Class:Modifiers)
        if(Class) { const FText Text=Class->GetDefaultObject<UDemoTalentModifier>()->Describe(Rank); /* 单策略等级说明。 */ if(!Text.IsEmpty()) Parts.Add(Text.ToString()); }
    for(const TSubclassOf<UDemoTalentAbility>& Class:Abilities)
        if(Class) { const FText Text=Class->GetDefaultObject<UDemoTalentAbility>()->Describe(Rank); /* GA自身参数说明。 */ if(!Text.IsEmpty()) Parts.Add(Text.ToString()); }
    return Parts.IsEmpty()?DisplayName:FText::FromString(FString::Join(Parts,TEXT("；")));
}
bool UDemoTalentDefinition::ConfigureEffect(int32 Rank,const FGameplayEffectSpecHandle& Spec) const
{
    DEMO_LOG_CALL();
    if(!Spec.IsValid()||Rank<1) return RejectDefinition(this,TEXT("invalid spec/rank"));
    for(const auto& Entry:EffectMagnitudes) // 本定义配置的等级曲线，不改变资产共享CDO。
    {
        const float Value=Entry.Value.GetValueAtLevel(Rank); // 当前等级数值，运行时拒绝NaN/Inf。
        if(!Entry.Key.IsValid()||!FMath::IsFinite(Value)) return RejectDefinition(this,TEXT("invalid curve tag/value"));
        Spec.Data->SetSetByCallerMagnitude(Entry.Key,Value);
    }
    for(const TSubclassOf<UDemoTalentModifier>& Class:Modifiers)
        if(Class) Class->GetDefaultObject<UDemoTalentModifier>()->ConfigureEffect(Rank,Spec);
    for(const auto& Entry:Spec.Data->SetByCallerTagMagnitudes) // 蓝图扩展也遵守有限值契约。
        if(!FMath::IsFinite(Entry.Value)) return RejectDefinition(this,TEXT("nonfinite tag magnitude"));
    if(!Spec.Data->SetByCallerNameMagnitudes.IsEmpty()) return RejectDefinition(this,TEXT("passive SetByCaller must use GameplayTag")); // UE5.4原位更新接口仅支持Tag，不能允许升阶后Name数值静默停留旧值。
    if(Spec.Data->Period!=0.f) return RejectDefinition(this,TEXT("passive curve becomes periodic at this rank")); // 曲线在高等级仍须满足无周期契约。
    return true;
}
