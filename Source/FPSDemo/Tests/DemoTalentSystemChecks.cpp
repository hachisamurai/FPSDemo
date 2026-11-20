// 职责：模块化天赋的实际战斗专项。使用：-DemoCombatTalentTest由原状态机调用；仅WITH_EDITOR编译测试逻辑，不访问云数据。
#include "Tests/DemoCombatTalentTest.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "GAS/Talents/DemoTalentCatalog.h"
#include "GAS/Talents/DemoTalentAbility.h"
#include "GAS/Talents/DemoTalentDefinition.h"
#include "GAS/Talents/DemoTalentModifier.h"
#include "GAS/Talents/DemoTalentComponent.h"
#include "Tests/DemoTalentLifecycleProbe.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/Abilities/DemoTalentRhythm.h"
#include "GAS/Talents/Abilities/DemoTalentShatter.h"
#include "GAS/Talents/Abilities/DemoTalentContagion.h"
#include "GAS/Talents/Modifiers/DemoTalentExpose.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Combat/DemoProjectileDamage.h"
#include "AI/DemoEnemy.h"
#include "AbilitySystemComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
bool ADemoCombatTalentTest::CheckTalentSystem(ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    UAbilitySystemComponent* ASC=Player->GetAbilitySystemComponent(); // 真实PS拥有GAS，测试不构造伪ASC。
    FOutputDeviceNull Output; // Debug命令的返回不作为通过条件，实际GE/HP才是证据。
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents rhythm:2 expose:2 contagion:2 shatter:2"),Output,GetWorld());
    const UDemoTalentCatalog* Catalog=GetDefault<UDemoTalentCatalog>(); // 已验证内容只读目录。
    const UDemoTalentDefinition* Rhythm=Catalog->Find(TEXT("rhythm")); // 当前真正定义资产，兼容原稳定ID。
    const UDemoTalentDefinition* Expose=Catalog->Find(TEXT("expose")); // 破绽只需修正策略，不授予GA。
    if(!Check(Rhythm&&Expose&&Rhythm->Abilities.Num()==1&&Expose->Abilities.IsEmpty()&&Expose->Modifiers.Num()==1,TEXT("definition separates event GA from stateless modifier"))) return false;
    int32 GrantedBehaviors=0; // 仅统计本系统的Spec，忽略玩家开火/冲刺等主动技能。
    for(const FGameplayAbilitySpec& Spec:ASC->GetActivatableAbilities()) if(Cast<UDemoTalentDefinition>(Spec.SourceObject.Get())) ++GrantedBehaviors;
    if(!Check(GrantedBehaviors==3&&ASC->FindAbilitySpecFromClass(Rhythm->Abilities[0]),TEXT("four talents grant only three real event abilities"))) return false;
    UE_LOG(LogFPSDemo,Display,TEXT("TALENT_RUNTIME_CLASS expose=%s rhythm=%s definition=%s"),*Expose->Modifiers[0]->GetPathName(),*Rhythm->Abilities[0]->GetPathName(),*Rhythm->GetPathName());
    Target=SpawnTarget(Player,1000);
    if(!Check(Target.IsValid()&&MakeShot(Player),TEXT("expose real target and shot"))) return false;
    FHitResult Hit; // 真实PhysicsAsset机身区域，未伪造骨骼或命中倍率。
    if(!Check(FindHit(Target.Get(),EDemoEnemyHitRegion::Body,Hit),TEXT("expose physical hit probe"))) return false;
    UAbilitySystemComponent* TargetASC=Target->GetAbilitySystemComponent(); // 高HP靶，直接伤害不导致清关。
    TargetASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),1500);
    DemoProjectileDamage::Apply(Shot,Hit,100,0);
    if(!Check(FMath::IsNearlyEqual(Target->GetHealth(),1400),TEXT("exact thirty percent does not amplify direct damage"))) return false;
    DemoProjectileDamage::Apply(Shot,Hit,100,1);
    if(!Check(FMath::IsNearlyEqual(Target->GetHealth(),1268.75f),TEXT("rank two expose amplifies actual direct Health GE by 31.25 percent"))) return false;
    Target->Destroy();
    Target=SpawnTarget(Player,1000); Rear=SpawnTarget(Player,1250,180);
    if(!Check(Target.IsValid()&&Rear.IsValid(),TEXT("contagion source and visible nearby target"))) return false;
    FDemoAmmoEffectSnapshot Burn=FDemoAmmoEffectSnapshot::FromCatalog(GetDefault<UDemoAmmoCatalog>()); // 生产默认参数，测试固定4层低于5层阈值。
    Burn.BurnPeriod=1.f; Burn.BurnDuration=8.f; Burn.BurnThreshold=5; Burn.BurnDamagePerStack=2.f;
    for(int32 Layer=0;Layer<4;++Layer) Target->FindComponentByClass<UDemoAmmoStatus>()->ApplySnapshot(ASC,1,Burn); // 真正GAS栈，不直接写标签。
    DemoEffects::Apply(ASC,Target->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10000);
    FDemoAmmoEffectSnapshot Spread; int32 BurnLayers=0; bool bSpread=false; // CopyBurn输出证明首层数值与递归标记均来自真实GE。
    if(!Check(Rear->FindComponentByClass<UDemoAmmoStatus>()->CopyBurn(Spread,BurnLayers,bSpread)&&BurnLayers==2&&bSpread&&FMath::IsNearlyEqual(Spread.BurnDamagePerStack,2.5f),TEXT("kill spreads half real burn stacks with ranked damage and secondary origin"))) return false;
    Rear->FindComponentByClass<UDemoAmmoStatus>()->ApplySnapshot(ASC,1,Burn); // 后续普通燃烧弹补层会触发UE的Spec替换，必须仍保留防连锁标记。
    if(!Check(Rear->FindComponentByClass<UDemoAmmoStatus>()->CopyBurn(Spread,BurnLayers,bSpread)&&BurnLayers==3&&bSpread&&FMath::IsNearlyEqual(Spread.BurnDamagePerStack,2.5f),TEXT("normal bullet stack preserves spread origin and first-layer snapshot"))) return false;
    Target=SpawnTarget(Player,1400,380); // 接收者邻近的第三只靶，传播死亡不应继续感染。
    if(!Check(Target.IsValid(),TEXT("contagion chain sentinel"))) return false;
    DemoEffects::Apply(ASC,Rear->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10000);
    if(!Check(Target->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Burn)==0,TEXT("spread target cannot spread again even on direct finishing hit"))) return false;
    Target->Destroy();
    Target=SpawnTarget(Player,1000); Rear=SpawnTarget(Player,1250,180);
    if(!Check(Target.IsValid()&&Rear.IsValid(),TEXT("shatter source and adjacent target"))) return false;
    FGameplayEffectSpecHandle Freeze=ASC->MakeOutgoingSpec(UDemoFrozenEffect::StaticClass(),1,ASC->MakeEffectContext()); // 正式冻结GE，激活真实Frozen Tag。
    Freeze.Data->SetDuration(20.f,true); ASC->ApplyGameplayEffectSpecToTarget(*Freeze.Data.Get(),Target->GetAbilitySystemComponent());
    const float Before=Rear->GetHealth(); // 只比较真实HP差，不从伤害事件计数推断命中。
    DemoEffects::Apply(ASC,Target->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10000);
    if(!Check(FMath::IsNearlyEqual(Rear->GetHealth(),Before-50)&&FMath::IsNearlyEqual(Rear->GetAbilitySystemComponent()->GetNumericAttribute(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute()),.7f)
        &&!Rear->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Frozen)&&Rear->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Chill)==0,TEXT("rank two ice burst applies real damage and slow without freeze stacks"))) return false;
    Rear->Destroy();
    // 二次冰爆击杀冻结邻居时，第三靶不得承受额外连锁；使用500cm边界隔离第一爆炸。
    Target=SpawnTarget(Player,1000); Rear=SpawnTarget(Player,1400);
    ADemoEnemy* Sentinel=SpawnTarget(Player,1800); // World拥有，只在本次同步断言借用。
    if(!Check(Target.IsValid()&&Rear.IsValid()&&Sentinel,TEXT("ice chain fixtures"))) return false;
    ASC->ApplyGameplayEffectSpecToTarget(*Freeze.Data.Get(),Target->GetAbilitySystemComponent()); ASC->ApplyGameplayEffectSpecToTarget(*Freeze.Data.Get(),Rear->GetAbilitySystemComponent());
    Rear->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),20);
    DemoEffects::Apply(ASC,Target->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10000);
    if(!Check(!Rear->IsAlive()&&Sentinel->GetHealth()==5000,TEXT("secondary ice burst death cannot recurse into distant sentinel"))) return false;
    Sentinel->Destroy();
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents clear"),Output,GetWorld()); // 显式卸载才清理前面击杀产生的移速层；普通升级不能刷新。
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents rhythm:2"),Output,GetWorld()); // 新一轮独立时钟夹具。
    Target=SpawnTarget(Player,1000);
    if(!Check(Target.IsValid(),TEXT("first rhythm timing target"))) return false;
    DemoEffects::Apply(ASC,Target->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10000);
    if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetMoveSpeedMultiplier(),1.125f),TEXT("rank two rhythm grants actual speed GE"))) return false;
    FGameplayEffectQuery Query; Query.EffectSource=Rhythm; Query.EffectDefinition=Rhythm->Abilities[0]->GetDefaultObject<UDemoTalentRhythm>()->SpeedEffect; // 只读取本定义的移速层。
    const TArray<FActiveGameplayEffectHandle> SpeedLayers=ASC->GetActiveEffects(Query); // 后续升级必须保留同一真实句柄。
    const FGameplayAbilitySpec* BeforeSpec=ASC->FindAbilitySpecFromClass(Rhythm->Abilities[0]); // 当前借用，保存值后不得跨授予继续用指针。
    if(!Check(SpeedLayers.Num()==1&&BeforeSpec,TEXT("incremental sync fixtures use actual GE and ability spec"))) return false;
    const FGameplayAbilitySpecHandle AbilityHandle=BeforeSpec->Handle; // 升阶后应保持原句柄。
    const TWeakObjectPtr<UGameplayAbility> Instance=BeforeSpec->GetPrimaryInstance(); // 不延长运行实例寿命，验证没有Clear/Give。
    const float LayerStart=ASC->GetActiveGameplayEffect(SpeedLayers[0])->StartWorldTime; // 精确验证时钟没有刷新。
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents rhythm:2 expose:2 overdrive:1"),Output,GetWorld());
    if(!Check(ASC->GetActiveGameplayEffect(SpeedLayers[0])&&ASC->GetActiveGameplayEffect(SpeedLayers[0])->StartWorldTime==LayerStart
        &&FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetMoveSpeedMultiplier(),1.125f),TEXT("adding unrelated numeric talents preserves rhythm layer and clock"))) return false;
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents rhythm:3 expose:2 overdrive:2"),Output,GetWorld());
    const FGameplayAbilitySpec* Upgraded=ASC->FindAbilitySpecFromHandle(AbilityHandle); // 重新查询，不复用先前指针。
    if(!Check(Upgraded&&Upgraded->Level==3&&Upgraded->GetPrimaryInstance()==Instance.Get()&&ASC->GetActiveGameplayEffect(SpeedLayers[0])
        &&FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetFireRateMultiplier(),2.25f),TEXT("rank changes update GE and ability in place without discarding instance or old temporary layer"))) return false;
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents rhythm:2"),Output,GetWorld()); // 回滚等级/移除其他项用于后续两层自然到期测试。
    if(!Check(ASC->FindAbilitySpecFromHandle(AbilityHandle)&&ASC->FindAbilitySpecFromHandle(AbilityHandle)->Level==2&&ASC->GetActiveGameplayEffect(SpeedLayers[0])
        &&FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetFireRateMultiplier(),1.f),TEXT("selective removal restores rate while rollback preserves unrelated runtime state"))) return false;
    UDemoPlayerProgressComponent* OwnerProgress=Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 当前PS持有，测试同步栈期间有效。
    bool bReentered=false; bool bNestedCommitted=false; // 局部回调结果；委托在下面同步命令返回后立即解绑，不跨帧引用。
    // 捕获OwnerProgress借用指针及两个结果引用；真实射速GE属性委托在本游戏线程同步触发，PS活到调用结束，解绑后Lambda不可再调用。
    const FDelegateHandle Reentry=ASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetFireRateMultiplierAttribute()).AddLambda(
        [OwnerProgress,&bReentered,&bNestedCommitted](const FOnAttributeChangeData& Change) // Change为当前属性通知，只借用至回调返回。
        {
            UE_LOG(LogFPSDemo,Log,TEXT("[CALL] TalentTestReentrantLedger"));
            if(bReentered) return;
            bReentered=true;
            FDemoPlayerProgressSnapshot Nested=OwnerProgress->GetSnapshot(); // 以实际最新账本派生新提交，不能复用外层迭代数组。
            Nested.Gambling.Talents.Add(TEXT("expose")); Nested.Gambling.TalentRanks.Add(4);
            bNestedCommitted=OwnerProgress->Commit(Nested);
        });
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents rhythm:2 overdrive:1"),Output,GetWorld()); // 真实GE属性广播必须触发重入，而非直接调用私有同步方法。
    ASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetFireRateMultiplierAttribute()).Remove(Reentry);
    if(!Check(bReentered&&bNestedCommitted&&OwnerProgress->GetSnapshot().Gambling.Rank(TEXT("expose"))==4
        &&ASC->HasMatchingGameplayTag(Expose->TalentTag)&&FMath::IsNearlyEqual(Catalog->Evaluate(ASC).ExecuteMultiplier,1.375f)
        &&ASC->GetActiveGameplayEffect(SpeedLayers[0]),TEXT("reentrant GE callback safely applies latest ledger without invalidating iteration or unrelated layer"))) return false;
    IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents rhythm:2"),Output,GetWorld()); // 清除测试注入的数值项，保留自然计时夹具。
    const FGameplayAbilitySpecHandle ProbeHandle=ASC->GiveAbility(FGameplayAbilitySpec(UDemoTalentLifecycleProbe::StaticClass(),1,INDEX_NONE,const_cast<UDemoTalentDefinition*>(Rhythm))); // 测试借用已授权定义，生产目录没有测试GA。
    if(!Check(ASC->TryActivateAbility(ProbeHandle)&&ASC->FindAbilitySpecFromHandle(ProbeHandle)->IsActive(),TEXT("generic talent GA remains active with a real WaitDelay task"))) return false;
    Player->GetPlayerState<ADemoPlayerState>()->GetTalentComponent()->Sync(ASC,Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot().Gambling); // 幂等同步不能清除其他合法运行Ability。
    if(!Check(ASC->FindAbilitySpecFromHandle(ProbeHandle)->IsActive(),TEXT("idempotent talent sync leaves running asynchronous ability intact"))) return false;
    ASC->CancelAbilityHandle(ProbeHandle);
    if(!Check(!ASC->FindAbilitySpecFromHandle(ProbeHandle)->IsActive(),TEXT("explicit GAS cancellation ends asynchronous ability"))) return false;
    ASC->ClearAbility(ProbeHandle); // 只回收本测试授予的句柄，不污染下一次真实事件。
    return true;
}
#endif
