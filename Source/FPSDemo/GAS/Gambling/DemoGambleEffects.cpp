// 职责：经济账本到天赋管理组件的桥接与遗物保护。使用：Commit后增量同步本人天赋；本文件只持有遗物权限GE，不重建天赋/射速。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "AbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h" // 超频通过临时GE聚合独立射速属性。
#include "GAS/DemoTags.h"
#include "GameplayEffectExtension.h"
#include "NativeGameplayTags.h"
#include "Debug/DemoLog.h"
#include "GAS/Talents/DemoTalentCatalog.h"
#include "GAS/Talents/DemoTalentDefinition.h"
#include "GAS/Talents/DemoTalentComponent.h"
namespace
{
    // 原生注册保证打包与网络两端具有相同Tag字典，不能从远端存档创建任意Tag。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Overload,"Demo.Gamble.Relic.Overload");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Catalyst,"Demo.Gamble.Relic.Catalyst");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Rescue,"Demo.Gamble.Relic.Rescue");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Propulsion,"Demo.Gamble.Relic.Propulsion");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Protection,"Demo.Gamble.RescueProtection");
    /** Id仅接受固定策略目录；返回空Tag时不授予权限。 */
    FGameplayTag Tag(FName Id)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        const FGameplayTag Relics[]={Overload,Catalyst,Rescue,Propulsion}; // 不是任意远端Tag查找。
        if(const UDemoTalentDefinition* Definition=GetDefault<UDemoTalentCatalog>()->Find(Id)) return Definition->TalentTag; // 权限由定义资产声明，未知ID不会创建Tag。
        for(int32 Index=0;Index<4;++Index) if(Id==DemoGamble::Relic(Index)) return Relics[Index]; // Index为遗物游标。
        return FGameplayTag();
    }
}
UDemoGambleLoadoutEffect::UDemoGambleLoadoutEffect()
{
    DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::Infinite;
    // 原类名保留避免资产路径变化；射速已由Overdrive定义的专属GE负责。
}
UDemoGambleProtectionEffect::UDemoGambleProtectionEffect()
{ DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::HasDuration; DurationMagnitude=FScalableFloat(1.f); }
bool DemoGambleEffects::Has(const UAbilitySystemComponent* ASC,FName Id)
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return ASC&&!Id.IsNone()&&ASC->HasMatchingGameplayTag(Tag(Id)); }
int32 DemoGambleEffects::Rank(const UAbilitySystemComponent* ASC,FName Id)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const ADemoPlayerState* PS=ASC?Cast<ADemoPlayerState>(ASC->GetOwnerActor()):nullptr; // ASC所有者才是本人，不从Player0读取。
    return PS&&Has(ASC,Id)?PS->GetProgressComponent()->GetSnapshot().Gambling.Rank(Id):0;
}
float DemoGambleEffects::Strength(const UAbilitySystemComponent* ASC,FName Id)
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return DemoGamble::Strength(Rank(ASC,Id)); }
void DemoGambleEffects::Sync(UAbilitySystemComponent* ASC,const FDemoGambleProgress& Data)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if(!ASC||!ASC->IsOwnerActorAuthoritative()||!Data.Validate()) return;
    ADemoPlayerState* PS=Cast<ADemoPlayerState>(ASC->GetOwnerActor()); // 句柄管理器与ASC必须属于同一玩家。
    if(!PS||!PS->GetTalentComponent()) { UE_LOG(LogFPSDemo,Error,TEXT("GAMBLE_SYNC missing player talent component")); return; }
    PS->GetTalentComponent()->Sync(ASC,Data); // 只更新变化项，保留其他天赋的实例/持续效果。
    FGameplayTagContainer Desired; // 遗物权限不再与天赋共用同一GE。
    if(!Data.EquippedRelic.IsNone()) Desired.AddTag(Tag(Data.EquippedRelic));
    FGameplayEffectQuery Query; Query.EffectDefinition=UDemoGambleLoadoutEffect::StaticClass(); // 只查询本遗物权限GE。
    const TArray<FActiveGameplayEffectHandle> Existing=ASC->GetActiveEffects(Query); // 值副本，避免移除时迭代失效。
    if(Existing.Num()==1)
        if(const FActiveGameplayEffect* Active=ASC->GetActiveGameplayEffect(Existing[0])) // 同步借用，不保留跨帧指针。
            if(Active->Spec.DynamicGrantedTags==Desired) return; // 天赋升级不闪断未变化的遗物权限。
    ASC->RemoveActiveEffects(Query);
    if(Desired.IsEmpty()) return;
    FGameplayEffectSpecHandle Spec=ASC->MakeOutgoingSpec(UDemoGambleLoadoutEffect::StaticClass(),1,ASC->MakeEffectContext()); // 每次独立Spec由ASC持有。
    if(!Spec.IsValid()) { UE_LOG(LogFPSDemo,Error,TEXT("GAMBLE_TAGS spec unavailable")); return; }
    Spec.Data->DynamicGrantedTags=Desired; // 仅遗物Tag；天赋权限由逐项句柄拥有。
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}
bool DemoGambleEffects::GuardHealth(FGameplayEffectModCallbackData& Data,float Health)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    UAbilitySystemComponent* ASC=&Data.Target; // 当前同步伤害拥有者，不保留到回调外。
    const bool bDamage=Data.EvaluatedData.ModifierOp==EGameplayModOp::Additive?Data.EvaluatedData.Magnitude<0:
        Data.EvaluatedData.ModifierOp==EGameplayModOp::Override&&Data.EvaluatedData.Magnitude<Health; // 只处理项目使用的伤害语义。
    if(!bDamage) return true;
    if(ASC->HasMatchingGameplayTag(Protection)) { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_RESCUE protected")); return false; }
    const float After=Data.EvaluatedData.ModifierOp==EGameplayModOp::Additive?Health+Data.EvaluatedData.Magnitude:Data.EvaluatedData.Magnitude; // 不读尚未落地的新值。
    ADemoPlayerState* PS=Cast<ADemoPlayerState>(ASC->GetOwnerActor()); // 敌人ASC没有个人遗物。
    if(Health<=0||After>0||!PS||!PS->HasAuthority()||!Has(ASC,DemoGamble::Relic(2))||PS->GetProgressComponent()->GetSnapshot().Gambling.bRescueUsed) return true;
    FGameplayEffectSpecHandle Spec=ASC->MakeOutgoingSpec(UDemoGambleProtectionEffect::StaticClass(),1,ASC->MakeEffectContext()); // 先准备再消费一次资格。
    if(!Spec.IsValid()) return true;
    Spec.Data->SetDuration(GetDefault<UDemoGambleSettings>()->RescueSeconds,true);
    Spec.Data->DynamicGrantedTags.AddTag(Protection);
    FDemoPlayerProgressSnapshot Next=PS->GetProgressComponent()->GetSnapshot(); // 同轮换装不能恢复此位。
    Next.Gambling.bRescueUsed=true;
    if(!PS->GetProgressComponent()->Commit(Next)) return true;
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    Data.EvaluatedData.Magnitude=Data.EvaluatedData.ModifierOp==EGameplayModOp::Additive?1.f-Health:1.f;
    UE_LOG(LogFPSDemo,Display,TEXT("GAMBLE_RESCUE consumed player=%s"),*PS->GetName());
    return true;
}
