// 职责：扩展核心共用门禁/派生GE/范围查询；仅服务器游戏线程同步执行，复用原ASC和CombatSession。
#include "GAS/Talents/Builds/DemoBuildRuntime.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/DemoTalentMoveEffect.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"
void UDemoBuildRuntime::Initialize(UDemoRunBuildComponent* Component)
{
    DEMO_LOG_CALL(); OwnerBuild=Component; OriginalPawn=Component&&Component->GetASC()?Cast<ADemoCharacter>(Component->GetASC()->GetAvatarActor()):nullptr; Session=UDemoCombatSession::FindActive(GetWorld());
    OnInitialized();
}
void UDemoBuildRuntime::Handle(UDemoRunBuildEventContext& Event) { DEMO_LOG_TICK(); }
void UDemoBuildRuntime::ResetRuntime() { DEMO_LOG_CALL(); Session.Reset(); OriginalPawn.Reset(); OwnerBuild.Reset(); }
UWorld* UDemoBuildRuntime::GetWorld() const { DEMO_LOG_TICK(); return OwnerBuild.IsValid()?OwnerBuild->GetWorld():nullptr; }
UDemoRunBuildComponent* UDemoBuildRuntime::Build() const { DEMO_LOG_TICK(); return OwnerBuild.Get(); }
ADemoCharacter* UDemoBuildRuntime::Pawn() const { DEMO_LOG_TICK(); return OriginalPawn.Get(); }
UAbilitySystemComponent* UDemoBuildRuntime::ASC() const { DEMO_LOG_TICK(); return Build()?Build()->GetASC():nullptr; }
FDemoRunBuildSnapshot UDemoBuildRuntime::Selection() const { DEMO_LOG_TICK(); return Build()?Build()->GetSnapshot():FDemoRunBuildSnapshot(); }
float UDemoBuildRuntime::Now() const { DEMO_LOG_TICK(); return GetWorld()?GetWorld()->GetTimeSeconds():0.f; }
bool UDemoBuildRuntime::Active() const
{
    DEMO_LOG_TICK(); const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr;
    return Build()&&Build()->GetOwner()->HasAuthority()&&Build()->IsEncounterEnabled()&&ASC()&&ASC()->GetAvatarActor()==Pawn()&&Pawn()&&!Pawn()->IsCoopEliminated()&&Pawn()->GetDemoAttributes()&&Pawn()->GetDemoAttributes()->GetHealth()>0.f&&State&&State->Phase==EDemoPhase::Combat&&Session.IsValid()&&Session->IsActive()&&UDemoCombatSession::FindActive(GetWorld())==Session.Get();
}
float UDemoBuildRuntime::Damage(ADemoEnemy* Enemy,float Amount) const
{
    DEMO_LOG_TICK(); if(!Active()||!IsValid(Enemy)||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||Enemy->GetWorld()!=GetWorld()||!FMath::IsFinite(Amount)||Amount<=0.f) return 0.f;
    UAbilitySystemComponent* Target=Enemy->GetAbilitySystemComponent(); if(!Target) return 0.f;
    const float Before=Enemy->GetHealth(); // 真实HP用于预算，不以请求伤害代替。
    FGameplayEffectContextHandle Context=ASC()->MakeEffectContext(); Context.AddSourceObject(GetTypedOuter<UDemoRunBuildAbility>());
    FGameplayEffectSpecHandle Spec=ASC()->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(),1.f,Context);
    if(!Spec.IsValid()) return 0.f;
    Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-Amount); ASC()->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(),Target);
    return IsValid(Enemy)?FMath::Max(0.f,Before-Enemy->GetHealth()):0.f;
}
float UDemoBuildRuntime::Heal(ADemoCharacter* Target,float Amount) const
{
    DEMO_LOG_TICK(); if(!Active()||!IsValid(Target)||Target->GetWorld()!=GetWorld()||Target->IsCoopEliminated()||!Target->GetDemoAttributes()||Target->GetDemoAttributes()->GetHealth()<=0.f||!FMath::IsFinite(Amount)||Amount<=0.f) return 0.f;
    UAbilitySystemComponent* TargetASC=Target->GetAbilitySystemComponent(); if(!TargetASC) return 0.f;
    const float Before=Target->GetDemoAttributes()->GetHealth();
    FGameplayEffectContextHandle Context=ASC()->MakeEffectContext(); Context.AddSourceObject(GetTypedOuter<UDemoRunBuildAbility>());
    FGameplayEffectSpecHandle Spec=ASC()->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(),1.f,Context); if(!Spec.IsValid()) return 0.f;
    Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,Amount); ASC()->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(),TargetASC);
    return FMath::Max(0.f,Target->GetDemoAttributes()->GetHealth()-Before);
}
void UDemoBuildRuntime::CollectVisibleEnemies(FVector Center,float Radius,int32 Limit,TArray<ADemoEnemy*>& OutTargets,ADemoEnemy* Exclude,FVector Direction,float ConeCos) const
{
    DEMO_LOG_CALL(); OutTargets.Reset();
    if(!Active()||Center.ContainsNaN()||!FMath::IsFinite(Radius)||Radius<=0.f||Limit<=0) return;
    Direction=Direction.GetSafeNormal2D();
    for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It)
    {
        ADemoEnemy* Enemy=*It; const FVector Delta=Enemy->GetActorLocation()-Center;
        if(Enemy==Exclude||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||Delta.SizeSquared()>FMath::Square(Radius)||(!Direction.IsNearlyZero()&&FVector::DotProduct(Direction,Delta.GetSafeNormal2D())<ConeCos)) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpandedBuildPulse),false,Pawn()); Query.AddIgnoredActor(Exclude); FHitResult Hit;
        if(GetWorld()->LineTraceSingleByChannel(Hit,Center,Enemy->GetActorLocation(),ECC_Visibility,Query)&&Hit.GetActor()!=Enemy) continue;
        OutTargets.Add(Enemy);
    }
    // 捕获只读中心，仅本栈稳定排序；同距按Actor序号裁决，无异步引用。
    OutTargets.Sort([Center](const ADemoEnemy& A,const ADemoEnemy& B){ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ExpandedPulseSort")); const double DA=FVector::DistSquared(Center,A.GetActorLocation()),DB=FVector::DistSquared(Center,B.GetActorLocation()); return DA==DB?A.GetUniqueID()<B.GetUniqueID():DA<DB; });
    if(OutTargets.Num()>Limit) OutTargets.SetNum(Limit,EAllowShrinking::No);
}
int32 UDemoBuildRuntime::Pulse(FVector Center,float Radius,float Amount,int32 Limit,ADemoEnemy* Exclude,FVector Direction,float ConeCos)
{
    DEMO_LOG_CALL(); TArray<ADemoEnemy*> Targets; CollectVisibleEnemies(Center,Radius,Limit,Targets,Exclude,Direction,ConeCos);
    int32 Count=0; for(ADemoEnemy* Enemy:Targets) if(Damage(Enemy,Amount)>0.f) ++Count;
    return Count;
}
void UDemoBuildRuntime::ApplyTemporaryMoveSpeed(float Bonus,float Seconds,FActiveGameplayEffectHandle& Handle) const
{
    DEMO_LOG_CALL(); if(!Active()||!ASC()||!FMath::IsFinite(Bonus)||Bonus<=0.f||!FMath::IsFinite(Seconds)||Seconds<=0.f) return;
    ClearTemporaryMoveSpeed(Handle);
    FGameplayEffectContextHandle Context=ASC()->MakeEffectContext(); Context.AddSourceObject(GetTypedOuter<UDemoRunBuildAbility>());
    FGameplayEffectSpecHandle Spec=ASC()->MakeOutgoingSpec(UDemoTalentMoveEffect::StaticClass(),1.f,Context);
    if(Spec.IsValid()) { Spec.Data->SetDuration(Seconds,true); Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,Bonus); Spec.Data->DynamicGrantedTags.AddTag(DemoTalentEvents::CombatTransient); Handle=ASC()->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()); }
}
void UDemoBuildRuntime::ClearTemporaryMoveSpeed(FActiveGameplayEffectHandle& Handle) const
{
    DEMO_LOG_CALL(); if(Handle.IsValid()&&ASC()) ASC()->RemoveActiveGameplayEffect(Handle);
    Handle.Invalidate();
}
void UDemoBuildRuntime::Publish(FName Action,int32 Stacks,int32 Capacity,float EndTime,float Cooldown)
{
    DEMO_LOG_TICK(); if(!Build()) return; FDemoRunBuildRuntimeView View; View.ActionState=Action; View.ActionStacks=Stacks; View.ActionCapacity=Capacity; View.ServerEndTime=EndTime; View.CooldownEndTime=Cooldown; Build()->SetRuntimeView(View);
}
