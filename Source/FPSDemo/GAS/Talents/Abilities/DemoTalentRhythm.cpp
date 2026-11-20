// 职责：猎杀节奏的数值/事件实现。使用：仅由天赋目录和GAS调用；新增技能使用新的派生文件，不向本文件追加分支。
#include "GAS/Talents/Abilities/DemoTalentRhythm.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/DemoTalentMoveEffect.h"
#include "GAS/Talents/DemoTalentDefinition.h"
#include "Debug/DemoLog.h"
UDemoTalentRhythm::UDemoTalentRhythm()
{
    DEMO_LOG_CALL(); 
    SpeedEffect=UDemoTalentMoveEffect::StaticClass(); // 蓝图可替换相同移速增量语义GE。
    ListenFor(DemoTalentEvents::Kill); // 原生GAS事件触发，不在奖励系统枚举天赋。
}
FText UDemoTalentRhythm::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    const float Scale=DemoGamble::Strength(Rank); // 服务投影冻结描述，UI不重算。
    return FText::FromString(FString::Printf(TEXT("击杀移速+%.1f%%，%g秒，最多%d独立层"),SpeedPerStack*Scale*100,Seconds,MaxStacks));
}

#include "AbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Engine/World.h"
bool UDemoTalentRhythm::OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank)
{
    DEMO_LOG_CALL();
    UAbilitySystemComponent* ASC=GetAbilitySystemComponentFromActorInfo(); // 当前实例归此ASC，独立于队友。
    if(!ASC||!SpeedEffect||SpeedEffect->GetDefaultObject<UGameplayEffect>()->DurationPolicy!=EGameplayEffectDurationType::HasDuration
        ||!FMath::IsFinite(Seconds)||Seconds<=0||!FMath::IsFinite(SpeedPerStack)||SpeedPerStack<0) return false;
    FGameplayEffectQuery Query; Query.EffectSource=GetTalentDefinition(); Query.EffectDefinition=SpeedEffect; // 每个定义隔离；同GA可被不同天赋复用。
    TArray<FActiveGameplayEffectHandle> Handles=ASC->GetActiveEffects(Query); // 值句柄副本，移除不会使迭代器失效。
    while(Handles.Num()>=FMath::Clamp(MaxStacks,1,3)) // 满层替换最早到期的一份，其余层时钟不刷新。
    {
        int32 Oldest=0; float Remaining=MAX_flt; // 当前最早到期层的下标与秒数。
        for(int32 Index=0;Index<Handles.Num();++Index)
            if(const FActiveGameplayEffect* Active=ASC->GetActiveGameplayEffect(Handles[Index])) // 同步借用有效GE。
                if(Active->GetTimeRemaining(GetWorld()->GetTimeSeconds())<Remaining) { Oldest=Index; Remaining=Active->GetTimeRemaining(GetWorld()->GetTimeSeconds()); }
        ASC->RemoveActiveGameplayEffect(Handles[Oldest]); Handles.RemoveAt(Oldest);
    }
    FGameplayEffectContextHandle Context=ASC->MakeEffectContext(); Context.AddSourceObject(GetTalentDefinition()); // 升级保留旧层时钟，卸载只清除此定义的本人效果。
    FGameplayEffectSpecHandle Spec=ASC->MakeOutgoingSpec(SpeedEffect,Rank,Context); // GE等级及SetByCaller均可用于蓝图表现。
    if(!Spec.IsValid()) return false;
    Spec.Data->SetDuration(FMath::Min(Seconds,60.f),true); Spec.Data->DynamicGrantedTags.AddTag(DemoTalentEvents::CombatTransient);
    Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,SpeedPerStack*DemoGamble::Strength(Rank));
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_RHYTHM rank=%d seconds=%.2f"),Rank,Seconds);
    return true; // 仅接受的事件才允许基类播放配置GC。
}
