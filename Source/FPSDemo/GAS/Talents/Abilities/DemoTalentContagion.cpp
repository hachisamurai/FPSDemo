// 职责：灼烧传染的数值/事件实现。使用：仅由天赋目录和GAS调用；新增技能使用新的派生文件，不向本文件追加分支。
#include "GAS/Talents/Abilities/DemoTalentContagion.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Debug/DemoLog.h"
UDemoTalentContagion::UDemoTalentContagion()
{
    DEMO_LOG_CALL(); 
    ListenFor(DemoTalentEvents::Kill); // 原生GAS事件触发，不在奖励系统枚举天赋。
}
FText UDemoTalentContagion::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    const float Scale=DemoGamble::Strength(Rank); // 服务投影冻结描述，UI不重算。
    return FText::FromString(FString::Printf(TEXT("击杀燃烧敌传播半数层，伤害×%.2f，不连锁"),Scale));
}

#include "AI/DemoEnemy.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "AbilitySystemComponent.h"
bool UDemoTalentContagion::OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank)
{
    DEMO_LOG_CALL();
    const auto* Death=Cast<UDemoTalentDeathContext>(Event.OptionalObject); // 基类事件桥强持有至本同步分发完成。
    ADemoEnemy* Origin=const_cast<ADemoEnemy*>(Cast<ADemoEnemy>(Event.Target)); // 尸体只提供位置，不写其已结束的Debuff。
    UAbilitySystemComponent* Source=GetAbilitySystemComponentFromActorInfo(); // 击杀者，传播DOT仍归同一玩家。
    if(!Death||!Origin||!Source||Death->bSecondary||Death->bSpreadBurn||Death->BurnStacks<=0||!Death->BurnSnapshot.Validate()) return false;
    FDemoAmmoEffectSnapshot Values=Death->BurnSnapshot; // 原燃烧首层参数；独立增强传播DOT，不放大爆炸/重复等级。
    Values.BurnDamagePerStack*=DemoGamble::Strength(Rank);
    const int32 Layers=FMath::Min(FMath::Max(1,Death->BurnStacks/2),Values.BurnThreshold-1); // 传播本身不能触发阈值爆炸，阈值1时没有合法传播层。
    if(Layers<1) return false;
    for(ADemoEnemy* Enemy:DemoTalentEvents::Nearby(Origin,Radius,MaxTargets)) // 数量/可见性上限统一约束。
    {
        UDemoAmmoStatus* Status=IsValid(Enemy)?Enemy->FindComponentByClass<UDemoAmmoStatus>():nullptr; // 每次GE可同步杀死其他目标，重新校验。
        if(!Status||!Enemy->IsAlive()||Status->Count(DemoAmmoTags::Burn)>0) continue; // 不污染已有燃烧的来源/首层参数，避免合并形成递归传播。
        for(int32 Layer=0;Layer<Layers&&Enemy->IsAlive();++Layer) // 层数最多31，不随等级增长数量。
            Status->ApplySnapshot(Source,1,Values,GetClass()->GetDefaultObject()); // CDO标记由GE上下文保留到DOT/死亡。
    }
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_CONTAGION rank=%d layers=%d"),Rank,Layers);
    return true; // 仅接受的事件才允许基类播放配置GC。
}
