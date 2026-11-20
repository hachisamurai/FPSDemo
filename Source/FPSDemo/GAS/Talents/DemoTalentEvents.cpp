// 职责：将真实死亡转换成GAS事件，统一限定二次范围效果成本。用法：只能由权威Health死亡边沿调用，不供UI发奖或远端任意目标请求。
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/DemoTalentAbility.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "AI/DemoEnemy.h"
#include "AbilitySystemComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"
namespace DemoTalentEvents
{
    UE_DEFINE_GAMEPLAY_TAG(FrostBurst,"GameplayCue.Demo.Talent.FrostBurst");
    UE_DEFINE_GAMEPLAY_TAG(Kill,"Demo.Event.Talent.Kill");
    UE_DEFINE_GAMEPLAY_TAG(TeamKill,"Demo.Event.Talent.TeamKill");
    UE_DEFINE_GAMEPLAY_TAG(CombatTransient,"Demo.Talent.CombatTransient");
}
void DemoTalentEvents::NotifyDeath(ADemoEnemy* Enemy,const FGameplayEffectContextHandle& Context)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    UAbilitySystemComponent* Source=Context.GetOriginalInstigatorAbilitySystemComponent(); // HealthGE原来源是唯一击杀者；队友不会重复AOE。
    if(!Enemy||!Enemy->HasAuthority()||!Source||!Source->IsOwnerActorAuthoritative()) return;
    TStrongObjectPtr<UDemoTalentDeathContext> Snapshot(NewObject<UDemoTalentDeathContext>()); // 同步嵌套GE可能触发GC，强引用仅活到分发结束。
    UDemoAmmoStatus* Status=Enemy->FindComponentByClass<UDemoAmmoStatus>(); // 死亡清理之前读取首层参数。
    if(Status) Status->CopyBurn(Snapshot->BurnSnapshot,Snapshot->BurnStacks,Snapshot->bSpreadBurn);
    Snapshot->bFrozen=Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Frozen);
    Snapshot->bSecondary=Cast<UDemoTalentAbility>(Context.GetSourceObject())!=nullptr;
    FGameplayEventData Event; // 不保存裸目标，GAS只在本同步调用中读取。
    Event.EventTag=Kill; Event.Instigator=Source->GetAvatarActor(); Event.Target=Enemy; Event.ContextHandle=Context; Event.OptionalObject=Snapshot.Get();
    Source->HandleGameplayEvent(Kill,&Event);
}
TArray<ADemoEnemy*> DemoTalentEvents::Nearby(ADemoEnemy* Origin,float Radius,int32 Limit)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    TArray<ADemoEnemy*> Result; // 只借用至调用栈，施加GE前必须重验目标存活。
    if(!Origin||!Origin->HasAuthority()||!FMath::IsFinite(Radius)||Radius<=0.f) return Result;
    Radius=FMath::Min(Radius,1500.f); Limit=FMath::Clamp(Limit,1,8); // 配置不允许创建全图无限连锁。
    for(TActorIterator<ADemoEnemy> It(Origin->GetWorld());It;++It) // 敌人总数由关卡目录限制，只在击杀事件查询。
    {
        ADemoEnemy* Enemy=*It; // World拥有，不能在下一事件复用裸指针。
        if(Enemy==Origin||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||FVector::DistSquared(Origin->GetActorLocation(),Enemy->GetActorLocation())>FMath::Square(Radius)) continue;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(TalentArea),false,Origin); // 尸体不阻挡自己的爆炸。
        FHitResult Hit; // 世界静态/动态墙面遮挡，不穿墙伤害。
        if(Origin->GetWorld()->LineTraceSingleByChannel(Hit,Origin->GetActorLocation(),Enemy->GetActorLocation(),ECC_Visibility,Params)&&Hit.GetActor()!=Enemy) continue;
        Result.Add(Enemy);
    }
    const FVector Center=Origin->GetActorLocation(); // 只捕获值，排序不持有或访问可能销毁的源Actor。
    // 捕获Center值；当前游戏线程同步排序，lambda不逃逸，按距离优先保证目标上限可预测。
    Result.Sort([Center](const ADemoEnemy& A,const ADemoEnemy& B){ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] TalentNearbySort")); return FVector::DistSquared(Center,A.GetActorLocation())<FVector::DistSquared(Center,B.GetActorLocation()); });
    if(Result.Num()>Limit) Result.SetNum(Limit,EAllowShrinking::No);
    return Result;
}
