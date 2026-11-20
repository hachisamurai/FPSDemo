// 职责：共享冰裂门禁、Boss独立标记和合作公开标记的目标端实现。使用：权威事件调用；所有伤害仍由现有Health GE处理。
#include "GAS/Talents/RunBuildTargetState.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/DemoTalentMoveEffect.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Builds/DemoRunBuildSettings.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "GAS/DemoTags.h"
#include "AI/DemoEnemy.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Debug/DemoLog.h"
UDemoRunBuildTargetState::UDemoRunBuildTargetState() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(true); }
UDemoRunBuildTargetState* UDemoRunBuildTargetState::Find(ADemoEnemy* Enemy,bool bCreate)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] UDemoRunBuildTargetState::Find")); if(!IsValid(Enemy)) return nullptr;
    UDemoRunBuildTargetState* Result=Enemy->FindComponentByClass<UDemoRunBuildTargetState>(); // Actor拥有组件，查询不保活。
    if(!Result&&bCreate&&Enemy->HasAuthority()) { Result=NewObject<UDemoRunBuildTargetState>(Enemy); Enemy->AddInstanceComponent(Result); Result->RegisterComponent(); }
    return Result;
}
bool UDemoRunBuildTargetState::TryIceTrigger(UDemoRunBuildComponent* Source)
{
    DEMO_LOG_CALL(); ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 同步目标借用。
    UAbilitySystemComponent* Target=Enemy?Enemy->GetAbilitySystemComponent():nullptr; // 原Enemy ASC。
    UAbilitySystemComponent* ASC=Source?Source->GetASC():nullptr; // 实际触发者ASC。
    const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>(); // 不可变项目数值，本次Spec复制时长。
    const float Now=GetWorld()->GetTimeSeconds(); // World游戏时钟。
    if(!Enemy||!Enemy->HasAuthority()||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||!Target||!ASC||Now<NextIceTime||Target->HasMatchingGameplayTag(DemoBossTags::Invulnerable)) return false;
    if(Enemy->IsBoss()) { BossMarks.Add(Source,Now+Config->BossMarkSeconds); NextIceTime=Now+Config->FreezeCooldown; return true; }
    if(Target->HasMatchingGameplayTag(DemoAmmoTags::Frozen)) { NextIceTime=Now+Config->FreezeCooldown; return true; } // 保留已有更长冻结，绝不刷新其截止。
    if(Target->HasMatchingGameplayTag(DemoAmmoTags::Immune)) return false;
    FGameplayEffectContextHandle Context=ASC->MakeEffectContext(); Context.AddSourceObject(GetDefault<UDemoRunBuildAbility>()); // 原派生来源标记只用于防递归，不伪造命中。
    FGameplayEffectSpecHandle Freeze=ASC->MakeOutgoingSpec(UDemoFrozenEffect::StaticClass(),1.f,Context); // 复用原冻结应用门禁/GC。
    if(!Freeze.IsValid()) return false;
    Freeze.Data->SetDuration(Config->FreezeSeconds,true);
    if(!ASC->ApplyGameplayEffectSpecToTarget(*Freeze.Data.Get(),Target).IsValid()) return false;
    NextIceTime=Now+Config->FreezeCooldown;
    FGameplayEffectSpecHandle Immune=ASC->MakeOutgoingSpec(UDemoFrostImmunityEffect::StaticClass(),1.f,Context); // 冻结及短暂免疫沿原GAS门禁，同目标不会被核心连续刷新。
    if(Immune.IsValid()) { Immune.Data->SetDuration(FMath::Max(Config->FreezeSeconds,Config->FreezeCooldown),true); ASC->ApplyGameplayEffectSpecToTarget(*Immune.Data.Get(),Target); }
    return true;
}
float UDemoRunBuildTargetState::ResolveMarks(UDemoRunBuildComponent* Source,const FGuid& ShotId,bool bConsume)
{
    DEMO_LOG_TICK(); if(!GetOwner()->HasAuthority()||!Source) return 1.f;
    const float Now=GetWorld()->GetTimeSeconds(); const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>(); // 本次同步时钟/集中数值。
    float Result=1.f; // 每种独立标记只乘一次，只有真正扣血才消费。
    if(const float* End=BossMarks.Find(Source)) if(*End>Now) { Result*=1.f+Config->BossBonus; if(bConsume) BossMarks.Remove(Source); }
    const AGameStateBase* State=GetWorld()->GetGameState(); // 固定队伍人数；阵亡不改为单人规则。
    const bool bSolo=!State||State->PlayerArray.Num()<=1;
    if(SingleExposeEndTime>Now&&ExposeSource.IsValid()&&ShotId!=ExposeShot&&(Source!=ExposeSource.Get()||bSolo))
    { Result*=1.f+Config->ExposeBonus; if(bConsume) ExpireExpose(); }
    if(TeamDebuffEndTime>Now&&TeamDebuffBonus>0.f&&ShotId!=TeamDebuffShot)
    { Result*=1.f+TeamDebuffBonus; } // B08-D/B09-D按时间维持，不因单次命中消费，也不与同源反复叠乘。
    for(auto It=BossMarks.CreateIterator();It;++It) if(!It.Key().IsValid()||It.Value()<=Now) It.RemoveCurrent(); // 每个合法事件修剪，最多固定队伍人数。
    return Result;
}
void UDemoRunBuildTargetState::SyncPublicExposeVisual(UDemoRunBuildComponent* Source)
{
    DEMO_LOG_TICK(); ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner());
    if(!Enemy||!Enemy->HasAuthority()||!GetWorld()) return;
    const float Now=GetWorld()->GetTimeSeconds();
    const float MaxEnd=FMath::Max(SingleExposeEndTime>Now?SingleExposeEndTime:0.f,TeamDebuffEndTime>Now?TeamDebuffEndTime:0.f);
    ExposedEndTime=MaxEnd;
    if(ExposeEffect.IsValid()&&Enemy->GetAbilitySystemComponent()) { Enemy->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(ExposeEffect); ExposeEffect.Invalidate(); }
    if(MaxEnd>Now&&Enemy->IsAlive()&&Source&&Source->GetASC()&&Enemy->GetAbilitySystemComponent())
    {
        const float Duration=FMath::Max(.05f,MaxEnd-Now);
        FGameplayEffectContextHandle Context=Source->GetASC()->MakeEffectContext(); Context.AddSourceObject(GetDefault<UDemoRunBuildAbility>());
        FGameplayEffectSpecHandle Mark=Source->GetASC()->MakeOutgoingSpec(UDemoTalentMoveEffect::StaticClass(),1.f,Context); // 零移速只作为有时限公开Tag容器，绝不修改基础移动。
        if(Mark.IsValid()) { Mark.Data->SetDuration(Duration,true); Mark.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,0.f); Mark.Data->DynamicGrantedTags.AddTag(DemoRunBuildTags::Exposed); Mark.Data->DynamicGrantedTags.AddTag(DemoTalentEvents::CombatTransient); ExposeEffect=Source->GetASC()->ApplyGameplayEffectSpecToTarget(*Mark.Data.Get(),Enemy->GetAbilitySystemComponent()); }
    }
    Enemy->ForceNetUpdate();
}
void UDemoRunBuildTargetState::Expose(UDemoRunBuildComponent* Source,const FGuid& ShotId)
{
    DEMO_LOG_CALL(); ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 目标需要仍存活才能显示/消费。
    if(!Enemy||!Enemy->HasAuthority()||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||!Source||!Source->GetASC()) return;
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ExposeTimer);
    ExposeSource=Source; ExposeShot=ShotId;
    const float Seconds=GetDefault<UDemoRunBuildSettings>()->ExposeSeconds; // 公开截止与GE/Timer共用时长。
    SingleExposeEndTime=GetWorld()->GetTimeSeconds()+Seconds;
    SyncPublicExposeVisual(Source);
    GetWorld()->GetTimerManager().SetTimer(ExposeTimer,this,&UDemoRunBuildTargetState::ExpireExpose,Seconds,false);
}
void UDemoRunBuildTargetState::ApplyTimedVulnerability(UDemoRunBuildComponent* Source,const FGuid& ShotId,float Seconds,float Bonus,bool bNotifyOnKill)
{
    DEMO_LOG_CALL(); ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner());
    if(!Enemy||!Enemy->HasAuthority()||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||!Source||!Source->GetASC()||!FMath::IsFinite(Seconds)||Seconds<=0.f||!FMath::IsFinite(Bonus)||Bonus<=0.f) return;
    const float Now=GetWorld()->GetTimeSeconds();
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TeamDebuffTimer);
    const float NewEnd=Now+Seconds;
    const bool bKeepStronger=TeamDebuffEndTime>Now&&TeamDebuffBonus>Bonus; // 不降级更强易伤，也不叠乘。
    if(!bKeepStronger) { TeamDebuffBonus=Bonus; TeamDebuffSource=Source; TeamDebuffShot=ShotId; bTeamDebuffNotifyKill=bNotifyOnKill; }
    else if(bNotifyOnKill) { TeamDebuffSource=Source; bTeamDebuffNotifyKill=true; }
    TeamDebuffEndTime=FMath::Max(TeamDebuffEndTime,NewEnd);
    SyncPublicExposeVisual(Source);
    GetWorld()->GetTimerManager().SetTimer(TeamDebuffTimer,this,&UDemoRunBuildTargetState::ExpireTeamDebuff,FMath::Max(.05f,TeamDebuffEndTime-Now),false);
}
void UDemoRunBuildTargetState::NotifyKilled(UDemoRunBuildComponent* Killer)
{
    DEMO_LOG_CALL(); ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner());
    if(!Enemy||!Enemy->HasAuthority()||!GetWorld()) return;
    const float Now=GetWorld()->GetTimeSeconds();
    if(!bTeamDebuffNotifyKill||TeamDebuffEndTime<=Now||!TeamDebuffSource.IsValid()) return;
    const AGameStateBase* State=GetWorld()->GetGameState();
    const bool bSolo=!State||State->PlayerArray.Num()<=1;
    UDemoRunBuildComponent* Marker=TeamDebuffSource.Get(); // 先冻结弱引用，再清空标记防重入。
    ExpireTeamDebuff();
    if(Killer&&(Killer!=Marker||bSolo)) Marker->NotifyMarkedTargetKilled(Enemy,Killer);
}
void UDemoRunBuildTargetState::ApplyBurstSlow(UDemoRunBuildComponent* Source)
{
    DEMO_LOG_CALL(); ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 本次存活小怪，Boss免控制。
    if(!Enemy||!Enemy->HasAuthority()||!Enemy->IsAlive()||Enemy->IsBoss()||!Enemy->IsCombatSessionValid()||!Source||!Source->GetASC()) return;
    UAbilitySystemComponent* Target=Enemy->GetAbilitySystemComponent(); // 原ASC负责移速聚合。
    if(SlowEffect.IsValid()) Target->RemoveActiveGameplayEffect(SlowEffect);
    const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>(); // 本次复制参数。
    FGameplayEffectContextHandle Context=Source->GetASC()->MakeEffectContext(); Context.AddSourceObject(GetDefault<UDemoRunBuildAbility>());
    FGameplayEffectSpecHandle Slow=Source->GetASC()->MakeOutgoingSpec(UDemoTalentMoveEffect::StaticClass(),1.f,Context);
    if(Slow.IsValid()) { Slow.Data->SetDuration(Config->BurstSlowSeconds,true); Slow.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-Config->BurstSlow); Slow.Data->DynamicGrantedTags.AddTag(DemoTalentEvents::CombatTransient); SlowEffect=Source->GetASC()->ApplyGameplayEffectSpecToTarget(*Slow.Data.Get(),Target); }
}
void UDemoRunBuildTargetState::ExpireExpose()
{
    DEMO_LOG_CALL(); if(!GetOwner()||!GetOwner()->HasAuthority()) return;
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ExposeTimer);
    SingleExposeEndTime=0.f; ExposeSource.Reset(); ExposeShot.Invalidate();
    SyncPublicExposeVisual(TeamDebuffSource.Get());
}
void UDemoRunBuildTargetState::ExpireTeamDebuff()
{
    DEMO_LOG_CALL(); if(!GetOwner()||!GetOwner()->HasAuthority()) return;
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TeamDebuffTimer);
    TeamDebuffEndTime=0.f; TeamDebuffBonus=0.f; TeamDebuffSource.Reset(); TeamDebuffShot.Invalidate(); bTeamDebuffNotifyKill=false;
    SyncPublicExposeVisual(ExposeSource.Get());
}
void UDemoRunBuildTargetState::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); if(GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(ExposeTimer); GetWorld()->GetTimerManager().ClearTimer(TeamDebuffTimer); }
    if(GetOwner()->HasAuthority()) { SingleExposeEndTime=0.f; TeamDebuffEndTime=0.f; ExpireExpose(); ExpireTeamDebuff(); if(ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner())) if(SlowEffect.IsValid()) Enemy->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(SlowEffect); }
    BossMarks.Reset(); Super::EndPlay(Reason);
}
void UDemoRunBuildTargetState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UDemoRunBuildTargetState,ExposedEndTime);
}
