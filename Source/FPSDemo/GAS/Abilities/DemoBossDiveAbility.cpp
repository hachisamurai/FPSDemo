#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoBossDiveVFX.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Game/DemoGameState.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Debug/DemoLog.h"

namespace DemoBossTags
{
    UE_DEFINE_GAMEPLAY_TAG(Invulnerable,"Demo.State.Invulnerable");
    UE_DEFINE_GAMEPLAY_TAG(Casting,"Demo.State.BossDiveCasting");
}
UDemoBossInvulnerableEffect::UDemoBossInvulnerableEffect()
{
    DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::Infinite;
    UTargetTagsGameplayEffectComponent* Tags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("State")); // GE拥有标签组件。
    FInheritedTagContainer Changes; Changes.AddTag(DemoBossTags::Invulnerable); // 只授予本技能无敌，不改其他元素状态。
    Tags->SetAndApplyTargetTagChanges(Changes); GEComponents.Add(Tags);
}
UDemoBossDiveAbility::UDemoBossDiveAbility()
{
    DEMO_LOG_CALL(); InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor; NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ActivationOwnedTags.AddTag(DemoBossTags::Casting); ActivationBlockedTags.AddTag(DemoBossTags::Casting);
}
bool UDemoBossDiveAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags,const FGameplayTagContainer* TargetTags,FGameplayTagContainer* OptionalRelevantTags) const
{
    DEMO_LOG_CALL();
    const ADemoEnemy* Enemy=ActorInfo?Cast<ADemoEnemy>(ActorInfo->AvatarActor.Get()):nullptr; // GAS当前Avatar，不跨帧保存ActorInfo。
    return Enemy&&Enemy->CanStartDiveAttack()&&Super::CanActivateAbility(Handle,ActorInfo,SourceTags,TargetTags,OptionalRelevantTags);
}
EDemoDivePhase UDemoBossDiveAbility::GetPhase() const { DEMO_LOG_TICK(); return Phase; }
FVector UDemoBossDiveAbility::GetLandingPoint() const { DEMO_LOG_TICK(); return Landing; }
bool UDemoBossDiveAbility::ClearFlight(const FVector& Start,const FVector& End,const ADemoCharacter* Target) const
{
    DEMO_LOG_CALL();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoDiveFlight),false,GetAvatarActorFromActorInfo()); // 不忽略地形/其他敌人，只忽略来源和玩家。
    Query.AddIgnoredActor(Target);
    FHitResult Hit; // 半径115cm，与真实Boss一致；目标检测由伤害阶段单独处理。
    const bool bBlocked=GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeSphere(115.f),Query);
    if (bBlocked) UE_LOG(LogFPSDemo,Log,TEXT("DIVE_FLIGHT_BLOCKED actor=%s"),*GetNameSafe(Hit.GetActor()));
    return !bBlocked;
}
void UDemoBossDiveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetAvatarActorFromActorInfo()); // 当前自身ASC Avatar。
    ADemoCharacter* Player=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0)); // 起飞时玩家仅用于移动忽略。
    bHasOrigin=false; bImpacted=false; Phase=EDemoDivePhase::None;
    if (!Enemy||!Player||!CommitAbility(Handle,ActorInfo,ActivationInfo)) { EndAbility(Handle,ActorInfo,ActivationInfo,true,true); return; }
    Settings=Enemy->GetDiveSettings(); Origin=Enemy->GetActorLocation(); Top=Origin+FVector(0,0,Settings.Height);
    if (!ClearFlight(Origin,Top,Player)) { UE_LOG(LogFPSDemo,Log,TEXT("DIVE_CANCEL no overhead clearance")); EndAbility(Handle,ActorInfo,ActivationInfo,true,true); return; }
    bHasOrigin=true; CastLevel=GetWorld()->GetGameState<ADemoGameState>()->LevelNumber;
    IgnoredPlayer=Player;
    CastChecked<UPrimitiveComponent>(Enemy->GetRootComponent())->IgnoreActorWhenMoving(Player,true); // 仅移动Sweep忽略玩家，Hitscan照常能击中Boss。
    VFX=GetWorld()->SpawnActor<ADemoBossDiveVFX>();
    SetPhase(EDemoDivePhase::Rising);
}
void UDemoBossDiveAbility::SetPhase(EDemoDivePhase Next)
{
    DEMO_LOG_CALL();
    if (Immunity.IsValid()) { GetAbilitySystemComponentFromActorInfo()->RemoveActiveGameplayEffect(Immunity); Immunity.Invalidate(); } // 离开悬停立即撤销，仅移除本次Handle。
    Phase=Next; PhaseStart=GetWorld()->GetTimeSeconds();
    if (Phase==EDemoDivePhase::Hovering)
    {
        const FGameplayEffectSpecHandle Spec=MakeOutgoingGameplayEffectSpec(UDemoBossInvulnerableEffect::StaticClass(),1); // 活动实例创建Spec，不修改共享CDO。
        if (Spec.IsValid()) Immunity=GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        if (!Immunity.IsValid()) { UE_LOG(LogFPSDemo,Error,TEXT("DIVE_CANCEL immunity application failed")); EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
        if (VFX) VFX->ShowCharge(Top);
    }
    if (Phase==EDemoDivePhase::Diving&&VFX) VFX->ShowTarget(Landing,Settings.Radius);
    if (Phase==EDemoDivePhase::Recovery&&VFX) VFX->ShowImpact();
    UE_LOG(LogFPSDemo,Log,TEXT("BOSS_DIVE_PHASE phase=%d time=%.3f"),static_cast<int32>(Phase),PhaseStart);
}
bool UDemoBossDiveAbility::LockLanding()
{
    DEMO_LOG_CALL();
    const ADemoCharacter* Player=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0)); // 只在此刻快照，俯冲不再查询位置。
    UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); // Recast提供可恢复平面追击的落点。
    const ANavigationData* Data=Nav?Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate):nullptr; // 不隐式创建空导航。
    if (!Player||!Data) { UE_LOG(LogFPSDemo,Log,TEXT("DIVE_CANCEL missing player/nav")); return false; }
    const FVector Wanted=Player->GetNavAgentLocation(); // 玩家当前脚下，禁止使用速度外推。
    for (int32 Index=0;Index<17;++Index) // 中心+两个8方向候选环；空间不足可取消，不能穿墙硬落。
    {
        const float Angle=((Index-1)%8)*PI/4.f; // 候选角弧度，固定顺序便于复现。
        const float Radius=Index==0?0.f:(Index<=8?250.f:450.f); // cm，优先最近的合法落点。
        const FVector Candidate=Wanted+FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*Radius; // 本次候选世界位置。
        FNavLocation Projected; // 投影结果为地面位置值，不保留多边形引用。
        if (!Nav->ProjectPointToNavigation(Candidate,Projected,FVector(180,180,250),Data)) continue;
        const FVector Center=Projected.Location+FVector(0,0,130); // 保留Boss原悬浮高度，半径115确保地面间隙。
        if (!ClearFlight(Top,Center,Player)) continue;
        Landing=Center; FlightSeconds=FMath::Max(Settings.DiveSeconds,FVector::Dist(Top,Landing)/Settings.MaxDiveSpeed);
        UE_LOG(LogFPSDemo,Log,TEXT("DIVE_LOCK player=%s landing=%s flight=%.3f"),*Wanted.ToString(),*Landing.ToString(),FlightSeconds);
        return true;
    }
    UE_LOG(LogFPSDemo,Log,TEXT("DIVE_CANCEL no legal landing route")); return false;
}
void UDemoBossDiveAbility::Advance(float DeltaSeconds)
{
    DEMO_LOG_TICK(); if (!IsActive()||DeltaSeconds<=0) return;
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetAvatarActorFromActorInfo()); // 同步移动对象，死亡/关卡切换取消。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 施法所在逻辑关卡快照校验。
    const ADemoCharacter* Player=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0)); // 当前目标必须存活。
    if (!Enemy||!Enemy->IsAlive()||!State||State->Phase!=EDemoPhase::Combat||State->LevelNumber!=CastLevel||!Player||Player->GetDemoAttributes()->GetHealth()<=0
        ||Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Frozen))
    { UE_LOG(LogFPSDemo,Log,TEXT("DIVE_CANCEL phase/level/death/frozen")); EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
    const float Age=GetWorld()->GetTimeSeconds()-PhaseStart; // World秒，暂停冻结，时间不依赖帧率。
    if (Phase==EDemoDivePhase::Rising||Phase==EDemoDivePhase::Diving)
    {
        const bool bRising=Phase==EDemoDivePhase::Rising; // 两阶段共用扫掠插值，但终点语义独立。
        const float Alpha=FMath::Clamp(Age/(bRising?Settings.RiseSeconds:FlightSeconds),0.f,1.f); // 当前行程比例。
        const FVector Position=FMath::Lerp(bRising?Origin:Top,bRising?Top:Landing,Alpha); // 不使用无碰撞Teleport。
        FHitResult Hit; // 新出现的动态障碍仍会中断；启动时的通道检测不是穿墙许可。
        Enemy->SetActorLocation(Position,true,&Hit);
        if (Hit.bBlockingHit) { UE_LOG(LogFPSDemo,Log,TEXT("DIVE_CANCEL dynamic obstruction %s"),*GetNameSafe(Hit.GetActor())); EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
        if (Alpha>=1)
        {
            if (bRising) SetPhase(EDemoDivePhase::Hovering);
            else { SetPhase(EDemoDivePhase::Recovery); Impact(); } // 先转恢复，伤害导致玩家死亡/阶段切换时不会重入落地结算。
        }
    }
    else if (Phase==EDemoDivePhase::Hovering&&Age>=3.f)
    {
        if (LockLanding()) SetPhase(EDemoDivePhase::Diving);
        else EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
    }
    else if (Phase==EDemoDivePhase::Recovery&&Age>=Settings.RecoverySeconds)
        EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);
}
void UDemoBossDiveAbility::Impact()
{
    DEMO_LOG_CALL(); if (bImpacted) { UE_LOG(LogFPSDemo,Log,TEXT("DIVE_IMPACT duplicate rejected")); return; } bImpacted=true;
    ADemoCharacter* Player=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0)); // 当次结算玩家，命中后GE可能切换阶段。
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetAvatarActorFromActorInfo()); // 来源ASC归敌人。
    if (!Player||!Enemy) return;
    const FVector Ground=Landing-FVector(0,0,130); // 已验证地面位置，垂直阈值按玩家脚下比较。
    if (FVector::Dist2D(Player->GetActorLocation(),Ground)>Settings.Radius||FMath::Abs(Player->GetNavAgentLocation().Z-Ground.Z)>180.f)
    { UE_LOG(LogFPSDemo,Log,TEXT("DIVE_EVADED outside radius/height")); return; }
    FHitResult Hit; // 地面上方30cm至玩家中心的视线，墙阻挡爆发伤害。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoDiveImpact),false,Enemy); // 忽略来源但保留地形和其他敌人。
    GetWorld()->LineTraceSingleByChannel(Hit,Ground+FVector(0,0,30),Player->GetActorLocation(),ECC_Visibility,Query);
    if (Hit.GetActor()!=Player) { UE_LOG(LogFPSDemo,Log,TEXT("DIVE_EVADED cover")); return; }
    if (Player->IsDashEvading()) { UE_LOG(LogFPSDemo,Log,TEXT("DIVE_EVADED dash; no heal reward")); return; }
    const float Before=Player->GetDemoAttributes()->GetHealth(); // 用真实健康变化决定击退，不能把提交成功当作伤害成功。
    DemoEffects::Apply(Enemy->GetAbilitySystemComponent(),Player->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-50.f);
    if (Player->GetDemoAttributes()->GetHealth()<Before&&Player->GetDemoAttributes()->GetHealth()>0)
    {
        FVector Direction=(Player->GetActorLocation()-Ground).GetSafeNormal2D(); // 从落点向外推，中心重合时用俯冲方向。
        if (Direction.IsNearlyZero()) Direction=(Landing-Top).GetSafeNormal2D();
        if (Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
        Player->LaunchCharacter(Direction*Settings.Knockback+FVector(0,0,Settings.KnockUp),true,true);
    }
    UE_LOG(LogFPSDemo,Log,TEXT("DIVE_HIT damage=50 health=%.1f->%.1f"),Before,Player->GetDemoAttributes()->GetHealth());
}
void UDemoBossDiveAbility::ReturnToGround()
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetAvatarActorFromActorInfo()); // 存活且World仍在时才能执行取消回收。
    if (!Enemy||!Enemy->IsAlive()||!bHasOrigin||Phase==EDemoDivePhase::Recovery||Phase==EDemoDivePhase::None) return;
    FHitResult Hit; // 原通道反向Sweep，仍尊重实时动态阻挡，不直接传送穿墙。
    if (Phase==EDemoDivePhase::Diving) Enemy->SetActorLocation(Top,true,&Hit);
    if (!Hit.bBlockingHit) Enemy->SetActorLocation(Origin,true,&Hit);
    if (Hit.bBlockingHit) UE_LOG(LogFPSDemo,Warning,TEXT("DIVE_RETURN_BLOCKED actor=%s; remains at last collision-safe point"),*GetNameSafe(Hit.GetActor()));
}
void UDemoBossDiveAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled)
{
    DEMO_LOG_CALL();
    if (!IsActive()) return; // 重复End不能重复冷却/清理或访问过期Avatar。
    if (Immunity.IsValid()) { GetAbilitySystemComponentFromActorInfo()->RemoveActiveGameplayEffect(Immunity); Immunity.Invalidate(); }
    if (bWasCancelled) ReturnToGround();
    if (VFX) { VFX->Destroy(); VFX=nullptr; }
    ADemoEnemy* Enemy=ActorInfo?Cast<ADemoEnemy>(ActorInfo->AvatarActor.Get()):nullptr; // GAS结束上下文只同步借用。
    if (Enemy)
    {
        if (IgnoredPlayer.IsValid()) CastChecked<UPrimitiveComponent>(Enemy->GetRootComponent())->IgnoreActorWhenMoving(IgnoredPlayer.Get(),false);
        Enemy->OnDiveFinished();
    }
    IgnoredPlayer.Reset(); Phase=EDemoDivePhase::None; bHasOrigin=false;
    Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}
