// 职责：逐项授予与原位升级，替代整套Clear/Give。用法：PS账本提交或Avatar重绑时同步；身份在GAS SourceObject指向只读Definition。
#include "GAS/Talents/DemoTalentComponent.h"
#include "GAS/Talents/DemoTalentCatalog.h"
#include "GAS/Talents/DemoTalentDefinition.h"
#include "GAS/Talents/DemoTalentAbility.h"
#include "GAS/Talents/DemoTalentPassiveEffects.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/RunBuildAbility.h" // 核心行为仍由本PS既有句柄所有者管理，不建立第二ASC或随机奖池项。
#include "Economy/Gambling/DemoGambleTypes.h"
#include "AbilitySystemComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"
#include "TimerManager.h"
UDemoTalentComponent::UDemoTalentComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; }
bool UDemoTalentComponent::PrepareEffects(UAbilitySystemComponent* ASC,const UDemoTalentDefinition* Definition,int32 Rank,TArray<FGameplayEffectSpecHandle>& Out) const
{
    DEMO_LOG_CALL(); Out.Reset();
    for(TSubclassOf<UGameplayEffect> Class:Definition->PassiveEffects) // 定义已验证为可逆持有效果。
    {
        FGameplayEffectContextHandle Context=ASC->MakeEffectContext(); Context.AddSourceObject(Definition); // 每项来源唯一且可复制为资产引用。
        FGameplayEffectSpecHandle Spec=ASC->MakeOutgoingSpec(Class,Rank,Context); // 预制所有数值后才修改任何旧效果。
        if(!Definition->ConfigureEffect(Rank,Spec)) { UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_SYNC invalid passive spec id=%s"),*Definition->TalentId.ToString()); return false; }
        Out.Add(Spec);
    }
    return true;
}
bool UDemoTalentComponent::ApplyRank(UAbilitySystemComponent* ASC,FDemoTalentGrant& Grant,int32 Rank)
{
    DEMO_LOG_CALL();
    TArray<FGameplayEffectSpecHandle> Prepared; // 单项原子预检，不因错误配置先移除旧等级。
    if(!PrepareEffects(ASC,Grant.Definition,Rank,Prepared)) return false;
    if(!ASC->GetActiveGameplayEffect(Grant.Permission))
    {
        FGameplayEffectContextHandle Context=ASC->MakeEffectContext(); Context.AddSourceObject(Grant.Definition); // 权限来源与被动一致。
        FGameplayEffectSpecHandle Permission=ASC->MakeOutgoingSpec(UDemoTalentPermissionEffect::StaticClass(),Rank,Context); // 仅授予本项Tag。
        if(!Permission.IsValid()) return false;
        Permission.Data->DynamicGrantedTags.AddTag(Grant.Definition->TalentTag);
        Grant.Permission=ASC->ApplyGameplayEffectSpecToSelf(*Permission.Data.Get());
        if(!ASC->GetActiveGameplayEffect(Grant.Permission)) return false;
    }
    Grant.Abilities.SetNum(Grant.Definition->Abilities.Num()); // 定义进程内只读，初始化/恢复丢失句柄才产生授予。
    for(int32 Index=0;Index<Grant.Abilities.Num();++Index) // 当前定义实现数组游标。
    {
        FGameplayAbilitySpec* Spec=ASC->FindAbilitySpecFromHandle(Grant.Abilities[Index]); // 仅本次调用借用，GiveAbility后不复用指针。
        if(Spec) { if(Spec->Level!=Rank) { Spec->Level=Rank; ASC->MarkAbilitySpecDirty(*Spec); } }
        else Grant.Abilities[Index]=ASC->GiveAbility(FGameplayAbilitySpec(Grant.Definition->Abilities[Index],Rank,INDEX_NONE,Grant.Definition));
    }
    Grant.Effects.SetNum(Prepared.Num());
    for(int32 Index=0;Index<Prepared.Num();++Index) // 持有效果与配置同序，绝不按全局GE类查询移除。
    {
        if(ASC->GetActiveGameplayEffect(Grant.Effects[Index]))
        {
            if(Grant.Rank!=Rank)
            {
                ASC->SetActiveGameplayEffectLevel(Grant.Effects[Index],Rank);
                ASC->UpdateActiveGameplayEffectSetByCallerMagnitudes(Grant.Effects[Index],Prepared[Index].Data->SetByCallerTagMagnitudes);
            }
        }
        else Grant.Effects[Index]=ASC->ApplyGameplayEffectSpecToSelf(*Prepared[Index].Data.Get());
        if(!ASC->GetActiveGameplayEffect(Grant.Effects[Index])) { UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_SYNC passive rejected id=%s"),*Grant.Definition->TalentId.ToString()); return false; }
    }
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_SYNC id=%s old=%d new=%d abilities=%d"),*Grant.Definition->TalentId.ToString(),Grant.Rank,Rank,Grant.Abilities.Num());
    Grant.Rank=Rank; return true; // 已产生的临时效果仍按触发时等级/原时钟自然结束。
}
void UDemoTalentComponent::Revoke(UAbilitySystemComponent* ASC,FDemoTalentGrant& Grant)
{
    DEMO_LOG_CALL();
    ASC->RemoveActiveGameplayEffect(Grant.Permission); // 先关权限门禁，再取消任务，防止取消回调重新触发同一天赋。
    for(FGameplayAbilitySpecHandle Handle:Grant.Abilities) { ASC->CancelAbilityHandle(Handle); ASC->ClearAbility(Handle); } // 只取消本项实例。
    for(FActiveGameplayEffectHandle Handle:Grant.Effects) ASC->RemoveActiveGameplayEffect(Handle); // 只撤销本项持有数值。
    FGameplayEffectQuery Query; Query.EffectSource=Grant.Definition; // 本人临时GE必须用Definition作为来源，移除不影响其他天赋时钟。
    ASC->RemoveActiveEffects(Query);
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_REVOKE id=%s"),*Grant.Definition->TalentId.ToString());
    Grant=FDemoTalentGrant();
}
void UDemoTalentComponent::Sync(UAbilitySystemComponent* ASC,const FDemoGambleProgress& Data)
{
    DEMO_LOG_CALL();
    if(!ASC||!ASC->IsOwnerActorAuthoritative()||ASC->GetOwnerActor()!=GetOwner()||!Data.Validate()||bEnding)
    { UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_SYNC rejected owner/authority/data/ending")); return; }
    if(BoundASC.IsValid()&&BoundASC.Get()!=ASC)
    { UE_LOG(LogFPSDemo,Error,TEXT("TALENT_SYNC rejected ASC replacement on same PS")); return; }
    BoundASC=ASC;
    if(bSyncing) { Pending=Data; bHasPending=true; UE_LOG(LogFPSDemo,Log,TEXT("TALENT_SYNC queued reentrant ledger")); return; }
    FDemoGambleProgress Desired=Data; // GAS委托可同步修改原PS账本；开始前复制，迭代期间不能借用原数组。
    TGuardValue<bool> Guard(bSyncing,true); // 只覆盖当前游戏线程调用栈，不持有跨线程锁。
    for(int32 Pass=0;Pass<8;++Pass) // 重入是内容事件，不应无限占用同一帧。
    {
        bHasPending=false; SyncSnapshot(ASC,Desired);
        if(!bHasPending) return;
        Desired=Pending; // 合并以最新已提交账本为准，无需重放过时的中间升级。
    }
    if(GetWorld()&&!GetWorld()->GetTimerManager().IsTimerActive(PendingTimer))
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_SYNC deferred repeated reentry; check passive callbacks"));
        PendingTimer=GetWorld()->GetTimerManager().SetTimerForNextTick(this,&UDemoTalentComponent::FlushPending); // UObject弱绑定，游戏线程下一帧执行，无局部捕获。
    }
}
void UDemoTalentComponent::FlushPending()
{
    DEMO_LOG_CALL();
    PendingTimer.Invalidate();
    if(bHasPending&&BoundASC.IsValid()&&!bEnding) Sync(BoundASC.Get(),Pending);
}
void UDemoTalentComponent::SyncSnapshot(UAbilitySystemComponent* ASC,const FDemoGambleProgress& Data)
{
    DEMO_LOG_CALL();
    for(auto It=Grants.CreateIterator();It;++It) // 只撤销账本已不再拥有的项。
        if(!Data.Talents.Contains(It.Key())) { Revoke(ASC,It.Value()); It.RemoveCurrent(); }
    for(FName Id:Data.Talents)
    {
        const UDemoTalentDefinition* Definition=GetDefault<UDemoTalentCatalog>()->Find(Id); // 可信定义，不读取网络路径。
        if(!Definition) continue;
        FDemoTalentGrant& Grant=Grants.FindOrAdd(Id); // 句柄表由本组件独占。
        Grant.Definition=const_cast<UDemoTalentDefinition*>(Definition);
        if(!ApplyRank(ASC,Grant,Data.Rank(Id))) UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_SYNC incomplete id=%s; retain/retry on next sync"),*Id.ToString());
    }
}
void UDemoTalentComponent::CancelActiveBehaviors()
{
    DEMO_LOG_CALL();
    if(!BoundASC.IsValid()||!BoundASC->IsOwnerActorAuthoritative()) return;
    TArray<FGameplayAbilitySpecHandle> Handles; // 先复制，取消回调可能同步提交新的账本，不能迭代共享句柄表。
    for(const auto& Entry:Grants) Handles.Append(Entry.Value.Abilities); // 仅此玩家天赋，不取消冲刺/治疗本身。
    for(FGameplayAbilitySpecHandle Handle:Handles) BoundASC->CancelAbilityHandle(Handle); // GAS负责结束AbilityTasks及释放实例内委托。
    if(FGameplayAbilitySpec* Spec=BoundASC->FindAbilitySpecFromHandle(RunBuildAbility)) // 个人死亡只清临时计数/回调；Core/Branch仍归PS，复活不会丢选择。
        if(UDemoRunBuildAbility* Core=Cast<UDemoRunBuildAbility>(Spec->GetPrimaryInstance())) Core->ResetRuntime();
}
FGameplayAbilitySpecHandle UDemoTalentComponent::EnsureRunBuildAbility(UAbilitySystemComponent* ASC,UObject* Source)
{
    DEMO_LOG_CALL();
    if(!ASC||!ASC->IsOwnerActorAuthoritative()||ASC->GetOwnerActor()!=GetOwner()||!Source||Source->GetOuter()!=GetOwner()||bEnding)
    { UE_LOG(LogFPSDemo,Warning,TEXT("BUILD_GRANT_REJECT owner/authority/source/ending")); return FGameplayAbilitySpecHandle(); }
    if(BoundASC.IsValid()&&BoundASC.Get()!=ASC) return FGameplayAbilitySpecHandle();
    BoundASC=ASC;
    if(!ASC->FindAbilitySpecFromHandle(RunBuildAbility)) RunBuildAbility=ASC->GiveAbility(FGameplayAbilitySpec(UDemoRunBuildAbility::StaticClass(),1,INDEX_NONE,Source));
    return RunBuildAbility;
}
void UDemoTalentComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    bEnding=true; bHasPending=false;
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(PendingTimer); // 退出后不能把排队账本重新授予。
    TGuardValue<bool> Guard(bSyncing,true); // EndPlay撤销可能同步触发GAS委托，此时不得再次修改句柄表。
    if(BoundASC.IsValid()&&BoundASC->IsOwnerActorAuthoritative())
    {
        if(FGameplayAbilitySpec* Spec=BoundASC->FindAbilitySpecFromHandle(RunBuildAbility)) // 实例拥有计数与Timer，先清理再撤销Spec。
            if(UDemoRunBuildAbility* Behavior=Cast<UDemoRunBuildAbility>(Spec->GetPrimaryInstance())) Behavior->ResetRuntime();
        BoundASC->CancelAbilityHandle(RunBuildAbility); BoundASC->ClearAbility(RunBuildAbility);
        for(auto& Entry:Grants) Revoke(BoundASC.Get(),Entry.Value); // TPair值归本组件；不按全局标签删除其他模块效果。
    }
    Grants.Reset(); BoundASC.Reset(); Super::EndPlay(Reason);
}
