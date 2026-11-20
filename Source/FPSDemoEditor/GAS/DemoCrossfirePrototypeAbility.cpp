// 职责：交叉火力Editor可玩原型与显式启停命令。使用：Demo.CrossfirePrototype 1/0，当前单人Editor战斗内本机PS唯一临时GA。
// 依赖：FPSDemo原GAS/真实伤害hook/Session撤销；命令不写存档、不改正式ID、不拦截原枪伤或近战伤害。
#include "GAS/DemoCrossfirePrototypeAbility.h"
#include "AbilitySystemComponent.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyRoster.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoCombatSession.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/PrimitiveComponent.h" // 点冲击上下文只借用真实目标的碰撞根，不产生新碰撞或弱点。
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"

namespace
{
    /** ConsoleWorld由引擎控制台路由；拒绝Editor地图、多人World、多本机Pawn及非法参数，不回退GWorld/Player0。 */
    void CrossfireCommand(const TArray<FString>& Args,UWorld* ConsoleWorld)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CrossfireCommand"));
        if(!IsInGameThread()||!ConsoleWorld||!ConsoleWorld->IsGameWorld()||ConsoleWorld->GetNetMode()!=NM_Standalone
            ||Args.Num()!=1||(Args[0]!=TEXT("0")&&Args[0]!=TEXT("1")))
        { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_COMMAND_REJECT use Demo.CrossfirePrototype 1/0 in a single-player Editor game World")); return; }
        ADemoCharacter* Pawn=nullptr; int32 LocalPlayers=0; // 仅指定World内唯一LocalController，不误用另一PIE窗口。
        for(FConstPlayerControllerIterator It=ConsoleWorld->GetPlayerControllerIterator();It;++It)
            if(APlayerController* Controller=It->Get()) if(Controller->IsLocalController())
            { ++LocalPlayers; Pawn=Cast<ADemoCharacter>(Controller->GetPawn()); }
        if(LocalPlayers!=1||!Pawn)
        { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_COMMAND_REJECT unique local DemoCharacter unavailable")); return; }
        FString Error; // 仅本次操作拒绝原因，不公开经济或访问云。
        const bool bSucceeded=Args[0]==TEXT("1")?UDemoCrossfirePrototypeAbility::EnableForPlayer(Pawn,Error):UDemoCrossfirePrototypeAbility::DisableForPlayer(Pawn,Error);
        UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_COMMAND enabled=%s success=%d reason=%s"),*Args[0],bSucceeded,*Error);
    }
    FAutoConsoleCommandWithWorldAndArgs CrossfireConsoleCommand(TEXT("Demo.CrossfirePrototype"),TEXT("Editor single-player Combat only: 1 enables B05 prototype; 0 removes it. No saved progress is changed."),FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CrossfireCommand)); // Editor模块卸载即注销；不持有World或玩家。
}

UDemoCrossfirePrototypeAbility::UDemoCrossfirePrototypeAbility()
{
    DEMO_LOG_CALL();
    InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
}
bool UDemoCrossfirePrototypeAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags,const FGameplayTagContainer* TargetTags,FGameplayTagContainer* OptionalRelevantTags) const
{
    DEMO_LOG_CALL();
    const auto* Pawn=ActorInfo?Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()):nullptr; // 原ASC当前Avatar，不查询其它玩家。
    const auto* PS=Pawn?Pawn->GetPlayerState<ADemoPlayerState>():nullptr;
    const UWorld* World=Pawn?Pawn->GetWorld():nullptr;
    const auto* State=World?World->GetGameState<ADemoGameState>():nullptr;
    if(!Pawn||!PS||!World||!World->IsGameWorld()||World->GetNetMode()!=NM_Standalone||World->GetNumPlayerControllers()!=1||!Pawn->HasAuthority()||!Pawn->IsLocallyControlled()
        ||Pawn->IsCoopEliminated()||!Pawn->GetDemoAttributes()||Pawn->GetDemoAttributes()->GetHealth()<=0.f||!State||State->Phase!=EDemoPhase::Combat||State->LevelNumber<1
        ||GetSourceObject(Handle,ActorInfo)!=PS||!FMath::IsFinite(WindowSeconds)||WindowSeconds<.1f||WindowSeconds>10.f
        ||!FMath::IsFinite(ImpactRatio)||ImpactRatio<=0.f||ImpactRatio>2.f||!FMath::IsFinite(MaximumImpactDamage)||MaximumImpactDamage<=0.f||MaximumImpactDamage>100.f)
    { UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_ACTIVATE_REJECT authority/solo/avatar/phase/source/config")); return false; }
    // 不借用正式天赋Definition/权限Tag；显式Editor授予的PS来源是候选授权，GAS死亡等通用门禁仍由父UGameplayAbility检查。
    return UGameplayAbility::CanActivateAbility(Handle,ActorInfo,SourceTags,TargetTags,OptionalRelevantTags);
}
bool UDemoCrossfirePrototypeAbility::EnableForPlayer(ADemoCharacter* Pawn,FString& Error)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UDemoCrossfirePrototypeAbility::EnableForPlayer")); Error.Reset();
    ADemoPlayerState* PS=IsValid(Pawn)?Pawn->GetPlayerState<ADemoPlayerState>():nullptr; // 只授予指定本人原ASC，不新建ASC或玩家数据。
    UAbilitySystemComponent* ASC=PS?PS->GetAbilitySystemComponent():nullptr;
    if(!Pawn||!Pawn->HasAuthority()||!ASC||!UDemoCombatSession::FindActive(Pawn->GetWorld()))
    { Error=TEXT("只允许有效Editor单人战斗内的存活本机玩家"); return false; }
    if(const FGameplayAbilitySpec* Existing=ASC->FindAbilitySpecFromClass(StaticClass()))
    {
        if(Existing->IsActive()) { Error=TEXT("交叉火力原型已经启用"); return true; }
        ASC->ClearAbility(Existing->Handle); // 清理上次未激活的本候选Spec，不碰其它GA。
    }
    const FGameplayAbilitySpecHandle Handle=ASC->GiveAbility(FGameplayAbilitySpec(StaticClass(),1,INDEX_NONE,PS)); // PS强持有GA；SourceObject只用于本人授权。
    if(!Handle.IsValid()||!ASC->TryActivateAbility(Handle,false))
    { if(Handle.IsValid()) ASC->ClearAbility(Handle); Error=TEXT("原型激活被阶段、单人限制、Avatar或配置门禁拒绝"); return false; }
    if(const FGameplayAbilitySpec* Spec=ASC->FindAbilitySpecFromHandle(Handle))
        if(Spec->IsActive()) { ASC->SetRemoveAbilityOnEnd(Handle); return true; } // 结束时自动移除候选句柄，旧World不残留权限。
    ASC->ClearAbility(Handle); Error=TEXT("原型未能注册当前CombatSession，已清理临时句柄"); return false;
}
bool UDemoCrossfirePrototypeAbility::DisableForPlayer(ADemoCharacter* Pawn,FString& Error)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UDemoCrossfirePrototypeAbility::DisableForPlayer")); Error.Reset();
    auto* PS=IsValid(Pawn)?Pawn->GetPlayerState<ADemoPlayerState>():nullptr;
    UAbilitySystemComponent* ASC=PS?PS->GetAbilitySystemComponent():nullptr; // 同Pawn本人ASC，取消不会遍历/影响其它玩家。
    if(!Pawn||!Pawn->HasAuthority()||!ASC) { Error=TEXT("本机候选玩家/ASC不可用"); return false; }
    if(const FGameplayAbilitySpec* Spec=ASC->FindAbilitySpecFromClass(StaticClass()))
    {
        const FGameplayAbilitySpecHandle Handle=Spec->Handle; // Cancel可能同步移除Spec，后续只保留值句柄。
        ASC->CancelAbilityHandle(Handle); ASC->ClearAbility(Handle);
    }
    return true;
}
void UDemoCrossfirePrototypeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    SourcePawn=ActorInfo?Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()):nullptr;
    Session=SourcePawn.IsValid()?UDemoCombatSession::FindActive(SourcePawn->GetWorld()):nullptr;
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr;
    EncounterLevel=State?State->LevelNumber:0; bStopping=false; ConsumedCharges=ConfirmedImpacts=0;
    if(!IsRuntimeValid()) { EndAbility(Handle,ActorInfo,ActivationInfo,false,true); return; }
    if(!DemoEditorCombatPrototype::RegisterView(SourcePawn.Get(),this,FDemoEditorCrossfireViewGetter::CreateUObject(this,&UDemoCrossfirePrototypeAbility::ReadView)))
    { EndAbility(Handle,ActorInfo,ActivationInfo,false,true); return; }
    if(!Session->RegisterCleanup(this,FSimpleDelegate::CreateUObject(this,&UDemoCrossfirePrototypeAbility::OnCombatEnded)))
    { EndAbility(Handle,ActorInfo,ActivationInfo,false,true); return; }
    if(auto* PS=SourcePawn->GetPlayerState<ADemoPlayerState>())
        if(auto* Build=PS->GetRunBuildComponent()) Build->EndEncounter(); // 显式候选覆盖，仅清原正式临时行为，不改选择/分支/存档；本场不自动恢复。
    MeleeBinding=DemoEditorCombatPrototype::MeleeConfirmed().AddUObject(this,&UDemoCrossfirePrototypeAbility::OnMeleeConfirmed);
    ShotBinding=DemoEditorCombatPrototype::ShotCommitted().AddUObject(this,&UDemoCrossfirePrototypeAbility::OnShotCommitted);
    DirectBinding=DemoEditorCombatPrototype::DirectConfirmed().AddUObject(this,&UDemoCrossfirePrototypeAbility::OnDirectConfirmed);
    if(UAbilitySystemComponent* ASC=GetAbilitySystemComponentFromActorInfo())
        HealthBinding=ASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).AddUObject(this,&UDemoCrossfirePrototypeAbility::OnSourceHealthChanged);
    UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_ACTIVE pawn=%s level=%d window=%.1f ratio=%.2f cap=%.1f original_hits_remain_normal"),*GetNameSafe(SourcePawn.Get()),EncounterLevel,WindowSeconds,ImpactRatio,MaximumImpactDamage);
}
bool UDemoCrossfirePrototypeAbility::IsRuntimeValid() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=SourcePawn.Get();
    const ADemoGameState* State=Pawn&&Pawn->GetWorld()?Pawn->GetWorld()->GetGameState<ADemoGameState>():nullptr;
    return !bStopping&&Pawn&&Pawn->HasAuthority()&&Pawn->IsLocallyControlled()&&Pawn->GetWorld()->GetNetMode()==NM_Standalone
        &&GetAvatarActorFromActorInfo()==Pawn&&!Pawn->IsCoopEliminated()&&Pawn->GetDemoAttributes()&&Pawn->GetDemoAttributes()->GetHealth()>0.f
        &&Session.IsValid()&&Session->IsActive()&&State&&State->Phase==EDemoPhase::Combat&&State->LevelNumber==EncounterLevel;
}
void UDemoCrossfirePrototypeAbility::OnMeleeConfirmed(ADemoCharacter* Source,ADemoEnemy* Enemy,float RawDamage,bool bKilled)
{
    DEMO_LOG_CALL();
    if(Source!=SourcePawn.Get()) return; // 桥为同步全局通知，只接收本人的真实事实。
    if(!IsRuntimeValid()||!IsValid(Enemy)||Enemy->GetWorld()!=GetWorld()||!Enemy->IsCombatSessionValid()||!Enemy->IsAlive()||bKilled||!FMath::IsFinite(RawDamage)||RawDamage<=0.f)
    { UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_ARM_REJECT ended/dead/target/value")); return; }
    ClearCharge(); ChargedTarget=Enemy; FrozenImpactDamage=FMath::Min(MaximumImpactDamage,RawDamage*ImpactRatio); ChargeExpiresAt=GetWorld()->GetTimeSeconds()+WindowSeconds;
    GetWorld()->GetTimerManager().SetTimer(ChargeTimer,this,&UDemoCrossfirePrototypeAbility::ExpireCharge,WindowSeconds,false); // 游戏线程弱UObject回调，消费/退出精确取消。
    UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_ARM target=%s seconds=%.1f impact=%.2f"),*Enemy->GetName(),WindowSeconds,FrozenImpactDamage);
}
void UDemoCrossfirePrototypeAbility::OnDirectConfirmed(ADemoCharacter* Source,ADemoEnemy* Enemy,const FGuid& ShotId,float ActualDamage,bool bKilled,bool bSecondary,bool bFirstTarget)
{
    DEMO_LOG_TICK();
    if(Source!=SourcePawn.Get()||bSecondary||!bFirstTarget||!ShotId.IsValid()||!FMath::IsFinite(ActualDamage)||ActualDamage<=0.f) return;
    if(!IsRuntimeValid()||Enemy!=ChargedTarget.Get()||!IsValid(Enemy)||Enemy->GetWorld()!=GetWorld()||GetWorld()->GetTimeSeconds()>=ChargeExpiresAt) return;
    if(!EligibleShots.Contains(ShotId)) { UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_CONSUME_REJECT shot_not_committed_after_melee id=%s"),*ShotId.ToString()); return; }
    const float Impact=FrozenImpactDamage; // 先冻结本次值再消费；同步死亡/天赋回调不可能再取到同一凭据。
    ClearCharge(); ConsumedCharges=ConsumedCharges<MAX_int32?ConsumedCharges+1:MAX_int32;
    if(bKilled||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid())
    { UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_CONSUMED shot=%s original_hit_killed_no_corpse_damage"),*ShotId.ToString()); return; }
    UAbilitySystemComponent* SourceASC=GetAbilitySystemComponentFromActorInfo();
    UAbilitySystemComponent* TargetASC=Enemy->GetAbilitySystemComponent();
    if(!SourceASC||!TargetASC||!FMath::IsFinite(Impact)||Impact<=0.f) return;
    const float Before=Enemy->GetHealth(); // 原枪伤已经完成；这里只测额外冲击自身真实HP损失。
    FGameplayEffectContextHandle Context=SourceASC->MakeEffectContext(); Context.AddSourceObject(GetDefault<UDemoCrossfirePrototypeAbility>()); // 派生Talent来源让旧冰爆/传播识别为二次伤害，不返充/递归。
    const FHitResult ImpactPoint(Enemy,Cast<UPrimitiveComponent>(Enemy->GetRootComponent()),Enemy->GetActorLocation(),FVector::UpVector); // 同目标点冲击不伪造骨骼弱点；提供点伤上下文以保留Guard现有方向盾过滤。
    Context.AddHitResult(ImpactPoint,true);
    FGameplayEffectSpecHandle Damage=SourceASC->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(),1.f,Context); // 原GE仍处理盾、Boss无敌、死亡和银币归属。
    if(!Damage.IsValid()) { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_IMPACT_REJECT invalid Health GE")); return; }
    Damage.Data->SetSetByCallerMagnitude(DemoEditorCombatPrototype::MagnitudeTag(),-Impact);
    SourceASC->ApplyGameplayEffectSpecToTarget(*Damage.Data.Get(),TargetASC); // 无元素状态/击退/冻结，不替换或拦截原枪伤。
    const float Actual=IsValid(Enemy)?FMath::Clamp(Before-Enemy->GetHealth(),0.f,Before):0.f;
    if(Actual>0.f) ConfirmedImpacts=ConfirmedImpacts<MAX_int32?ConfirmedImpacts+1:MAX_int32;
    UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_IMPACT shot=%s budget=%.2f actual=%.2f boss_no_control=%d"),*ShotId.ToString(),Impact,Actual,IsValid(Enemy)&&Enemy->IsBoss());
}
void UDemoCrossfirePrototypeAbility::OnShotCommitted(ADemoCharacter* Source,const FGuid& ShotId)
{
    DEMO_LOG_TICK();
    if(Source!=SourcePawn.Get()||!IsRuntimeValid()||!ShotId.IsValid()||!ChargedTarget.IsValid()||!ChargedTarget->IsAlive()||GetWorld()->GetTimeSeconds()>=ChargeExpiresAt) return;
    if(EligibleShots.Num()>=128&&!EligibleShots.Contains(ShotId))
    { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_SHOT_REJECT hard_limit=128 until_charge_reset")); return; }
    EligibleShots.Add(ShotId); // 只有成功提交的后续枪可用；同枪多弹丸共享ID，不逐弹添加。
}
void UDemoCrossfirePrototypeAbility::OnSourceHealthChanged(const FOnAttributeChangeData& Data)
{
    DEMO_LOG_CALL();
    if(Data.NewValue<=0.f&&!bStopping) EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,false,true);
}
void UDemoCrossfirePrototypeAbility::OnCombatEnded()
{
    DEMO_LOG_CALL(); if(!bStopping&&IsActive()) EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,false,true);
}
void UDemoCrossfirePrototypeAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo,const FGameplayAbilitySpec& Spec)
{
    DEMO_LOG_CALL();
    Super::OnAvatarSet(ActorInfo,Spec);
    if(IsActive()&&(!ActorInfo||!SourcePawn.IsValid()||ActorInfo->AvatarActor.Get()!=SourcePawn.Get())) EndAbility(CurrentSpecHandle,ActorInfo,CurrentActivationInfo,false,true);
}
void UDemoCrossfirePrototypeAbility::ExpireCharge()
{ DEMO_LOG_CALL(); ClearCharge(); UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_CHARGE_EXPIRED")); }
void UDemoCrossfirePrototypeAbility::ClearCharge()
{
    DEMO_LOG_CALL(); if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ChargeTimer);
    ChargedTarget.Reset(); ChargeExpiresAt=0.f; FrozenImpactDamage=0.f; EligibleShots.Reset();
}
FDemoEditorCrossfireView UDemoCrossfirePrototypeAbility::ReadView() const
{
    DEMO_LOG_TICK(); FDemoEditorCrossfireView View;
    if(!IsActive()||!IsRuntimeValid()) return View;
    View.bActive=true; View.ConsumedCharges=ConsumedCharges; View.ConfirmedImpacts=ConfirmedImpacts;
    if(ChargedTarget.IsValid()&&ChargedTarget->IsAlive()&&ChargeExpiresAt>GetWorld()->GetTimeSeconds())
    {
        View.Remaining=ChargeExpiresAt-GetWorld()->GetTimeSeconds(); View.ChargeDamage=FrozenImpactDamage;
        const auto* Roster=ChargedTarget->FindComponentByClass<UDemoEnemyRoster>(); // 仅公开目标名称，不读玩家或钱包。
        View.TargetName=Roster&&!Roster->GetLabel().IsEmpty()?Roster->GetLabel():ChargedTarget->IsBoss()?TEXT("首领"):TEXT("敌人");
    }
    return View;
}
void UDemoCrossfirePrototypeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled)
{
    DEMO_LOG_CALL(); if(bStopping) return; bStopping=true;
    DemoEditorCombatPrototype::MeleeConfirmed().Remove(MeleeBinding); MeleeBinding.Reset();
    DemoEditorCombatPrototype::ShotCommitted().Remove(ShotBinding); ShotBinding.Reset();
    DemoEditorCombatPrototype::DirectConfirmed().Remove(DirectBinding); DirectBinding.Reset();
    if(UAbilitySystemComponent* ASC=GetAbilitySystemComponentFromActorInfo()) if(HealthBinding.IsValid())
        ASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).Remove(HealthBinding);
    HealthBinding.Reset(); DemoEditorCombatPrototype::UnregisterView(SourcePawn.Get(),this); ClearCharge();
    UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_ENDED consumed=%d impacts=%d formal_core_stays_stopped_until_next_encounter"),ConsumedCharges,ConfirmedImpacts);
    SourcePawn.Reset(); Session.Reset(); EncounterLevel=0;
    Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}
