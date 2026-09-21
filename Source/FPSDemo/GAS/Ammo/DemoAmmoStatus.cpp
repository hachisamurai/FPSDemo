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
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 防止最后一只怪死亡后仍结算过期状态。
    if(!GetOwner()->HasAuthority()||!Source||!ASC||ASC->HasMatchingGameplayTag(DemoBossTags::Invulnerable)||!Config||!Config->Validate()||!CastChecked<ADemoEnemy>(GetOwner())->IsAlive()||!State||State->Phase!=EDemoPhase::Combat||(Type!=1&&Type!=2))
    { UE_LOG(LogFPSDemo,Log,TEXT("AMMO_STATUS_REJECT invulnerable/phase/config/source/dead/type")); return; }
    Catalog=Config;
    FGameplayEffectSpecHandle Spec=Source->MakeOutgoingSpec(Type==1?UDemoBurnEffect::StaticClass():UDemoChillEffect::StaticClass(),1,Source->MakeEffectContext()); // 只读源ASC归属玩家PS。
    Spec.Data->SetDuration(Type==1?Config->BurnDuration:Config->ChillDuration,true);
    if(Type==1){Spec.Data->Period=Config->BurnPeriod;Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-Config->BurnDamagePerStack);}
    Source->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(),ASC);
}
void UDemoAmmoStatus::Added(UAbilitySystemComponent* Target,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle Handle)
{
    DEMO_LOG_CALL();
    if(Spec.Def->GetClass()!=UDemoBurnEffect::StaticClass()&&Spec.Def->GetClass()!=UDemoChillEffect::StaticClass())return;
    if(!StackBindings.Contains(Handle))StackBindings.Add(Handle,ASC->OnGameplayEffectStackChangeDelegate(Handle)->AddUObject(this,&UDemoAmmoStatus::StackChanged));
    StackChanged(Handle,ASC->GetCurrentStackCount(Handle),0); // 第一层也检查，使阈值1有效；重入通过bConsuming保护。
}
void UDemoAmmoStatus::StackChanged(FActiveGameplayEffectHandle Handle,int32 NewCount,int32 OldCount)
{
    DEMO_LOG_CALL();
    if(bConsuming||!Catalog||!ASC||!CastChecked<ADemoEnemy>(GetOwner())->IsAlive())return;
    const FActiveGameplayEffect* Active=ASC->GetActiveGameplayEffect(Handle); // 只借用至移除前，绝不在Remove后解引用。
    if(!Active)return;
    const bool Fire=Active->Spec.Def->GetClass()==UDemoBurnEffect::StaticClass(); // 回调只绑定两种栈GE。
    UAbilitySystemComponent* Source=Active->Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent(); // DOT来源维持玩家ASC归属。
    UE_LOG(LogFPSDemo,Log,TEXT("AMMO_STACK target=%s fire=%d count=%d->%d"),*GetOwner()->GetName(),Fire,OldCount,NewCount);
    if(NewCount>=(Fire?Catalog->BurnThreshold:Catalog->FreezeThreshold)&&Source)
    {
        bConsuming=true; ASC->RemoveActiveGameplayEffect(Handle);
        if(Fire)
        {
            ASC->ExecuteGameplayCue(DemoAmmoTags::ExplosionCue,FGameplayCueParameters()); // GC仅表现；伤害独立GE。
            DemoEffects::Apply(Source,ASC,UDemoHealthEffect::StaticClass(),-Catalog->ExplosionDamage);
        }
        else
        {
            FGameplayEffectSpecHandle Frozen=Source->MakeOutgoingSpec(UDemoFrozenEffect::StaticClass(),1,Source->MakeEffectContext()); // 冻结先于免疫提交。
            Frozen.Data->SetDuration(Catalog->FreezeDuration,true);
            const FActiveGameplayEffectHandle Freeze=Source->ApplyGameplayEffectSpecToTarget(*Frozen.Data.Get(),ASC); // 失败不授予免疫。
            if(Freeze.IsValid())
            {
                FGameplayEffectSpecHandle Immune=Source->MakeOutgoingSpec(UDemoFrostImmunityEffect::StaticClass(),1,Source->MakeEffectContext()); // 免疫覆盖冻结及解冻后时长。
                Immune.Data->SetDuration(Catalog->FreezeDuration+Catalog->PostThawImmunityDuration,true);
                if(!Source->ApplyGameplayEffectSpecToTarget(*Immune.Data.Get(),ASC).IsValid()){ASC->RemoveActiveGameplayEffect(Freeze);UE_LOG(LogFPSDemo,Error,TEXT("AMMO_FREEZE rollback immunity failure"));}
            }
        }
        bConsuming=false;
    }
    else if(!Fire)
    {
        if(SlowHandle.IsValid())ASC->RemoveActiveGameplayEffect(SlowHandle);
        FGameplayEffectSpecHandle Slow=ASC->MakeOutgoingSpec(UDemoChillMoveEffect::StaticClass(),1,ASC->MakeEffectContext()); // 独立倍率GE不叠乘，冰霜移除会撤销。
        Slow.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,1-FMath::Min(Catalog->MaxSlow,Catalog->SlowPerStack*NewCount));
        SlowHandle=ASC->ApplyGameplayEffectSpecToSelf(*Slow.Data.Get());
    }
    RefreshVisual();
}
void UDemoAmmoStatus::Removed(const FActiveGameplayEffect& Effect)
{
    DEMO_LOG_CALL();
    if(const FDelegateHandle* Binding=StackBindings.Find(Effect.Handle))if(auto* Delegate=ASC->OnGameplayEffectStackChangeDelegate(Effect.Handle))Delegate->Remove(*Binding); // 同步移除精确解绑。
    StackBindings.Remove(Effect.Handle);
    if(Effect.Spec.Def->GetClass()==UDemoChillEffect::StaticClass()&&SlowHandle.IsValid())
    { const FActiveGameplayEffectHandle Previous=SlowHandle; /* 先失效防止递归移除。 */ SlowHandle.Invalidate(); ASC->RemoveActiveGameplayEffect(Previous); }
    RefreshVisual();
}
void UDemoAmmoStatus::Clear()
{
    DEMO_LOG_CALL(); if(!ASC)return;
    FGameplayTagContainer Tags; Tags.AddTag(DemoAmmoTags::Burn); Tags.AddTag(DemoAmmoTags::Chill); Tags.AddTag(DemoAmmoTags::Frozen); Tags.AddTag(DemoAmmoTags::Immune); // 不误清理玩家成长或Boss技能。
    ASC->RemoveActiveEffectsWithGrantedTags(Tags);
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
    StackBindings.Empty(); Super::EndPlay(EndPlayReason);
}
