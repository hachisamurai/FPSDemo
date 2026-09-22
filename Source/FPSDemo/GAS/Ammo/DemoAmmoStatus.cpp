#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "GAS/DemoAttributeSet.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoAttackPulse.h"
#include "Game/DemoGameState.h"
#include "Components/PointLightComponent.h"
#include "AbilitySystemComponent.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "Debug/DemoLog.h"

FDemoAmmoEffectSnapshot FDemoAmmoEffectSnapshot::FromCatalog(const UDemoAmmoCatalog* Config)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs catalog=%s"),__FUNCTION__,*GetNameSafe(Config));
    FDemoAmmoEffectSnapshot Result; // 返回纯值，不让在飞子弹或长寿命GE引用可变资产。
    if(!Config||!Config->Validate())
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_SNAPSHOT_REJECT missing/invalid catalog"));
        return Result;
    }
    Result.BurnDuration=Config->BurnDuration;
    Result.BurnPeriod=Config->BurnPeriod;
    Result.BurnDamagePerStack=Config->BurnDamagePerStack;
    Result.BurnThreshold=Config->BurnThreshold;
    Result.ExplosionDamage=Config->ExplosionDamage;
    Result.ChillDuration=Config->ChillDuration;
    Result.SlowPerStack=Config->SlowPerStack;
    Result.MaxSlow=Config->MaxSlow;
    Result.FreezeThreshold=Config->FreezeThreshold;
    Result.FreezeDuration=Config->FreezeDuration;
    Result.PostThawImmunityDuration=Config->PostThawImmunityDuration;
    Result.PiercingDamageMultiplier=Config->PiercingDamageMultiplier;
    Result.SecondaryDamageRatio=Config->SecondaryDamageRatio;
    Result.bInitialized=true;
    return Result;
}

bool FDemoAmmoEffectSnapshot::Validate() const
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    const float Values[]={BurnDuration,BurnPeriod,BurnDamagePerStack,ExplosionDamage,ChillDuration,SlowPerStack,MaxSlow,FreezeDuration,PostThawImmunityDuration,PiercingDamageMultiplier,SecondaryDamageRatio}; // 快照脱离目录后仍拒绝被外部写入的NaN、负值与越界比例。
    for(const float Value:Values) // 每个待检查值只在当前同步验证内借用。
    {
        if(!FMath::IsFinite(Value)||Value<0.f)
        {
            UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_SNAPSHOT_REJECT effect value not finite/nonnegative"));
            return false;
        }
    }
    const bool bValid=bInitialized&&BurnDuration>0.f&&BurnPeriod>0.f&&BurnPeriod<=BurnDuration&&ChillDuration>0.f&&FreezeDuration>0.f
        &&SlowPerStack<=.9f&&MaxSlow<=.9f&&SecondaryDamageRatio<=1.f&&BurnThreshold>=1&&BurnThreshold<=32&&FreezeThreshold>=1&&FreezeThreshold<=32; // 与目录约束一致，不改变既有弹药平衡范围。
    if(!bValid)UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_SNAPSHOT_REJECT uninitialized/duration/period/slow/threshold"));
    return bValid;
}

void UDemoAmmoStatus::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    ASC=CastChecked<ADemoEnemy>(GetOwner())->GetAbilitySystemComponent();
    AddedBinding=ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddUObject(this,&UDemoAmmoStatus::Added);
    RemovedBinding=ASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(this,&UDemoAmmoStatus::Removed);
    Glow=NewObject<UPointLightComponent>(GetOwner()); Glow->SetupAttachment(GetOwner()->GetRootComponent()); Glow->RegisterComponent(); // 状态光源和Boss红光相互独立。
    Glow->SetAttenuationRadius(200); Glow->SetCastShadows(false); Glow->SetVisibility(false);
}
int32 UDemoAmmoStatus::Count(FGameplayTag Tag) const
{
    DEMO_LOG_TICK(); if(!ASC)return 0;
    FGameplayTagContainer Tags; Tags.AddTag(Tag); // 查询真实ActiveGE，不使用Tag引用计数作为层数。
    const TArray<FActiveGameplayEffectHandle> Handles=ASC->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Tags)); // 本次值快照。
    return Handles.IsEmpty()?0:ASC->GetCurrentStackCount(Handles[0]);
}
void UDemoAmmoStatus::Apply(UAbilitySystemComponent* Source,int32 Type,const UDemoAmmoCatalog* Config)
{
    DEMO_LOG_CALL();
    ApplySnapshot(Source,Type,FDemoAmmoEffectSnapshot::FromCatalog(Config)); // 兼容测试和旧接口；后续回调绝不再读取Catalog。
}
void UDemoAmmoStatus::ApplySnapshot(UAbilitySystemComponent* Source,int32 Type,const FDemoAmmoEffectSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 防止最后一只怪死亡后仍结算过期状态。
    if(!GetOwner()->HasAuthority()||!Source||!ASC||ASC->HasMatchingGameplayTag(DemoBossTags::Invulnerable)||!Snapshot.Validate()||!CastChecked<ADemoEnemy>(GetOwner())->IsAlive()||!State||State->Phase!=EDemoPhase::Combat||(Type!=1&&Type!=2))
    { UE_LOG(LogFPSDemo,Log,TEXT("AMMO_STATUS_REJECT invulnerable/phase/config/source/dead/type")); return; }
    FDemoAmmoEffectSnapshot Values=Snapshot; // 新组用来弹快照；同类已有组必须保持首层的伤害、周期、时长和阈值。
    for(const TPair<FActiveGameplayEffectHandle,FDemoAmmoEffectSnapshot>& Entry:EffectSnapshots) // 火/冰句柄分别查询，不让后一种弹药覆盖前一种参数。
    {
        const FActiveGameplayEffect* Active=ASC->GetActiveGameplayEffect(Entry.Key); // 只借用到提交新GE前，不跨同步回调保留指针。
        if(Active&&Active->Spec.Def->GetClass()==(Type==1?UDemoBurnEffect::StaticClass():UDemoChillEffect::StaticClass()))
        {
            Values=Entry.Value;
            break;
        }
    }
    FGameplayEffectSpecHandle Spec=Source->MakeOutgoingSpec(Type==1?UDemoBurnEffect::StaticClass():UDemoChillEffect::StaticClass(),1,Source->MakeEffectContext()); // 只读源ASC归属玩家PS。
    if(!Spec.IsValid()){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_STATUS_REJECT invalid outgoing spec"));return;}
    Spec.Data->SetDuration(Type==1?Values.BurnDuration:Values.ChillDuration,true);
    if(Type==1){Spec.Data->Period=Values.BurnPeriod;Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-Values.BurnDamagePerStack);}
    PendingSnapshots.Add({Type,Values}); // Added/StackChanged可在Apply返回前执行；先发布首层上下文，支持游戏线程同步嵌套。
    const FActiveGameplayEffectHandle Applied=Source->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(),ASC); // GE拥有计时；阈值1可能在返回前已被消费，因此不能只按句柄存活推断失败。
    PendingSnapshots.Pop(EAllowShrinking::No);
    if(!Applied.IsValid())UE_LOG(LogFPSDemo,Log,TEXT("AMMO_STATUS_REJECT application blocked type=%d"),Type);
}
void UDemoAmmoStatus::Added(UAbilitySystemComponent* Target,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle Handle)
{
    DEMO_LOG_CALL();
    const int32 Type=Spec.Def->GetClass()==UDemoBurnEffect::StaticClass()?1:Spec.Def->GetClass()==UDemoChillEffect::StaticClass()?2:0; // 只为两种栈GE保存参数，冻结/减速独立生命周期。
    if(Type==0)return;
    if(!ASC||!ASC->GetActiveGameplayEffect(Handle))
    {
        UE_LOG(LogFPSDemo,Log,TEXT("AMMO_ADDED_IGNORE already consumed/removed stack type=%d"),Type);
        return; // 补层StackChanged可能先移除满层GE，迟到Added不能给失效句柄重新保存永不清理的快照。
    }
    if(!EffectSnapshots.Contains(Handle))
    {
        for(int32 Index=PendingSnapshots.Num()-1;Index>=0;--Index) // 取当前最内层同类Apply准备的值，不能借用另一种弹药参数。
        {
            if(PendingSnapshots[Index].Type==Type){EffectSnapshots.Add(Handle,PendingSnapshots[Index].Values);break;}
        }
    }
    if(!EffectSnapshots.Contains(Handle))
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_STATUS_REJECT external stack has no immutable snapshot type=%d"),Type);
        ASC->RemoveActiveGameplayEffect(Handle); // 非法外部裸GE不能遗留没有参数所有权的DOT或控制栈。
        return;
    }
    if(!StackBindings.Contains(Handle))
    {
        if(FOnActiveGameplayEffectStackChange* Delegate=ASC->OnGameplayEffectStackChangeDelegate(Handle)) // 借用ASC拥有的栈事件，Removed/EndPlay精确解绑。
            StackBindings.Add(Handle,Delegate->AddUObject(this,&UDemoAmmoStatus::StackChanged));
    }
    StackChanged(Handle,ASC->GetCurrentStackCount(Handle),0); // 第一层也检查，使阈值1有效；重入通过bConsuming保护。
}
void UDemoAmmoStatus::StackChanged(FActiveGameplayEffectHandle Handle,int32 NewCount,int32 OldCount)
{
    DEMO_LOG_CALL();
    if(bConsuming||!ASC||!CastChecked<ADemoEnemy>(GetOwner())->IsAlive())return;
    const FDemoAmmoEffectSnapshot* Found=EffectSnapshots.Find(Handle); // 仅在本段同步复制前借用Map元素，Remove可能立即使其失效。
    if(!Found){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_STACK_REJECT missing immutable snapshot"));return;}
    const FDemoAmmoEffectSnapshot Values=*Found; // 满层Remove会同步删Map；后续爆炸/冻结必须使用独立值副本。
    const FActiveGameplayEffect* Active=ASC->GetActiveGameplayEffect(Handle); // 只借用至移除前，绝不在Remove后解引用。
    if(!Active)return;
    const bool Fire=Active->Spec.Def->GetClass()==UDemoBurnEffect::StaticClass(); // 回调只绑定两种栈GE。
    UAbilitySystemComponent* Source=Active->Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent(); // DOT来源维持玩家ASC归属。
    UE_LOG(LogFPSDemo,Log,TEXT("AMMO_STACK target=%s fire=%d count=%d->%d"),*GetOwner()->GetName(),Fire,OldCount,NewCount);
    if(NewCount>=(Fire?Values.BurnThreshold:Values.FreezeThreshold)&&Source)
    {
        bConsuming=true; ASC->RemoveActiveGameplayEffect(Handle);
        if(Fire)
        {
            ASC->ExecuteGameplayCue(DemoAmmoTags::ExplosionCue,FGameplayCueParameters()); // GC仅表现；伤害独立GE。
            DemoEffects::Apply(Source,ASC,UDemoHealthEffect::StaticClass(),-Values.ExplosionDamage);
        }
        else
        {
            FGameplayEffectSpecHandle Frozen=Source->MakeOutgoingSpec(UDemoFrozenEffect::StaticClass(),1,Source->MakeEffectContext()); // 冻结先于免疫提交。
            Frozen.Data->SetDuration(Values.FreezeDuration,true);
            const FActiveGameplayEffectHandle Freeze=Source->ApplyGameplayEffectSpecToTarget(*Frozen.Data.Get(),ASC); // 失败不授予免疫。
            if(Freeze.IsValid())
            {
                FGameplayEffectSpecHandle Immune=Source->MakeOutgoingSpec(UDemoFrostImmunityEffect::StaticClass(),1,Source->MakeEffectContext()); // 免疫覆盖冻结及解冻后时长。
                Immune.Data->SetDuration(Values.FreezeDuration+Values.PostThawImmunityDuration,true);
                if(!Source->ApplyGameplayEffectSpecToTarget(*Immune.Data.Get(),ASC).IsValid()){ASC->RemoveActiveGameplayEffect(Freeze);UE_LOG(LogFPSDemo,Error,TEXT("AMMO_FREEZE rollback immunity failure"));}
            }
        }
        bConsuming=false;
    }
    else if(!Fire)
    {
        if(SlowHandle.IsValid())ASC->RemoveActiveGameplayEffect(SlowHandle);
        FGameplayEffectSpecHandle Slow=ASC->MakeOutgoingSpec(UDemoChillMoveEffect::StaticClass(),1,ASC->MakeEffectContext()); // 独立倍率GE不叠乘，冰霜移除会撤销。
        Slow.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,1-FMath::Min(Values.MaxSlow,Values.SlowPerStack*NewCount));
        SlowHandle=ASC->ApplyGameplayEffectSpecToSelf(*Slow.Data.Get());
    }
    RefreshVisual();
}
void UDemoAmmoStatus::Removed(const FActiveGameplayEffect& Effect)
{
    DEMO_LOG_CALL();
    if(const FDelegateHandle* Binding=StackBindings.Find(Effect.Handle))if(auto* Delegate=ASC->OnGameplayEffectStackChangeDelegate(Effect.Handle))Delegate->Remove(*Binding); // 同步移除精确解绑。
    StackBindings.Remove(Effect.Handle);
    EffectSnapshots.Remove(Effect.Handle); // 整组消失才释放首层参数；新一组可采用下一发的新快照。
    if(Effect.Spec.Def->GetClass()==UDemoChillEffect::StaticClass()&&SlowHandle.IsValid())
    { const FActiveGameplayEffectHandle Previous=SlowHandle; /* 先失效防止递归移除。 */ SlowHandle.Invalidate(); ASC->RemoveActiveGameplayEffect(Previous); }
    RefreshVisual();
}
void UDemoAmmoStatus::Clear()
{
    DEMO_LOG_CALL(); if(!ASC)return;
    FGameplayTagContainer Tags; Tags.AddTag(DemoAmmoTags::Burn); Tags.AddTag(DemoAmmoTags::Chill); Tags.AddTag(DemoAmmoTags::Frozen); Tags.AddTag(DemoAmmoTags::Immune); // 不误清理玩家成长或Boss技能。
    ASC->RemoveActiveEffectsWithGrantedTags(Tags);
    EffectSnapshots.Empty(); // 死亡/清场没有跨关残留快照；PendingSnapshots仍留给当前同步Apply正常退栈。
    if(SlowHandle.IsValid()){ASC->RemoveActiveGameplayEffect(SlowHandle);SlowHandle.Invalidate();}
    if(Glow)Glow->SetVisibility(false); // 死亡/离开战斗显式关闭表现，即使此前没有活跃GE。
}
void UDemoAmmoStatus::HandleCue(FGameplayTag Cue,EGameplayCueEvent::Type Event,const FGameplayCueParameters& Parameters)
{
    DEMO_LOG_CALL();
    if((Cue==DemoAmmoTags::ExplosionCue||Cue==DemoAmmoTags::PiercingCue)&&Event==EGameplayCueEvent::Executed)
    {
        ADemoAttackPulse* Pulse=GetWorld()->SpawnActor<ADemoAttackPulse>(GetOwner()->GetActorLocation(),FRotator::ZeroRotator); // 世界持有短生命纯表现Actor。
        if(Pulse)Pulse->EndRadius=Cue==DemoAmmoTags::ExplosionCue?180.f:60.f; // 单体爆炸/贯穿反馈不扩散全地图。
    }
    RefreshVisual();
}
void UDemoAmmoStatus::RefreshVisual()
{
    DEMO_LOG_CALL(); if(!ASC||!Glow)return;
    const bool Frozen=ASC->HasMatchingGameplayTag(DemoAmmoTags::Frozen); // Tag由GE寿命驱动，不用本地计时器。
    const int32 Burns=Count(DemoAmmoTags::Burn), Chills=Count(DemoAmmoTags::Chill); // 一帧同步的表现值。
    Glow->SetVisibility(Burns>0||Chills>0||Frozen); Glow->SetLightColor(Frozen||Chills>0?FLinearColor(.2f,.7f,1):FLinearColor(1,.2f,.02f)); Glow->SetIntensity(Frozen?8000:4000);
}
void UDemoAmmoStatus::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); Clear();
    if(ASC){ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(AddedBinding);ASC->OnAnyGameplayEffectRemovedDelegate().Remove(RemovedBinding);}
    StackBindings.Empty(); EffectSnapshots.Empty(); Super::EndPlay(EndPlayReason); // 不清同步准备栈，避免EndPlay发生在Apply回调内时破坏成对Pop。
}
