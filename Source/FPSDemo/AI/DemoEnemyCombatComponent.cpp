// 职责：唯一权威攻击仲裁；依赖Enemy组件，Roster与旧技能互斥，取消时完整撤销。
#include "AI/DemoEnemyCombatComponent.h"
#include "AI/DemoEnemyRoster.h"
#include "AI/DemoEnemyTargeting.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyNavigation.h"
#include "AI/DemoEnemyKnockbackComponent.h" // 生命周期取消必须丢弃尚未执行的位移，不能带到后续会话。
#include "Animation/DemoEnemyPresentation.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"

UDemoEnemyCombatComponent::UDemoEnemyCombatComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; }
bool UDemoEnemyCombatComponent::IsCancelling() const { DEMO_LOG_TICK(); return bCancelling; }
ADemoCharacter* UDemoEnemyCombatComponent::ResolveTarget()
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 客户端不选择目标，也不依赖客户端GameMode。
    if (!GetOwner()||!GetOwner()->HasAuthority()||bCancelling||!State||State->Phase!=EDemoPhase::Combat) { Target.Reset(); return nullptr; }
    ADemoCharacter* Best=nullptr; // 本次同步遍历借用，最终只保存弱引用。
    double BestDistance=TNumericLimits<double>::Max(); // 平方厘米；最近活玩家优先，等距保留现有目标避免抖动。
    if (ADemoCharacter* Current=Target.Get(); DemoEnemyTargeting::IsAliveParticipant(Current)) // 保留目标也必须重验登记和阵亡状态，不能让离场角色继续吸引攻击。
    { Best=Current; BestDistance=FVector::DistSquared(Current->GetActorLocation(),GetOwner()->GetActorLocation()); }
    for (TActorIterator<ADemoCharacter> It(GetWorld());It;++It) // World内现存Pawn，不依赖本地控制器序号。
    {
        if (!DemoEnemyTargeting::IsAliveParticipant(*It)) continue; // 握手尚未完成的远端 Pawn 不属于本次战斗队伍。
        const double Distance=FVector::DistSquared(It->GetActorLocation(),GetOwner()->GetActorLocation()); // 使用真实位置，不改变视线/攻击范围守卫。
        if (Distance<BestDistance) { Best=*It; BestDistance=Distance; }
    }
    Target=Best; return Best;
}
void UDemoEnemyCombatComponent::Update(float DeltaSeconds)
{
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 宿主拥有执行器，本帧借用；组件不延長Actor生命周期。
    if (!Enemy) { DEMO_LOG_TICK(); return; }

	DEMO_LOG_TICK();
	if (!Enemy->IsCombatReady()) { Enemy->Navigation->Stop(TEXT("Inactive")); return; }
	// 双重限制：即使敌人意外残留，也不会在 Hub/Reward/终局追击安全区玩家。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 同World权威阶段快照，不拥有GameState。
	ADemoCharacter* Player = ResolveTarget(); // 当前权威活目标，支持2~4人合作，无Player0假设。
	if (!Enemy->IsCombatSessionValid() || !State || State->Phase != EDemoPhase::Combat || !DemoEnemyTargeting::IsAliveParticipant(Player)) // 执行器开始前再次检查，旧目标不能跨离场边界。
	{
		if (Enemy->bAttackPending || Enemy->bGlobalAttackPending || Enemy->AbilitySystem->HasMatchingGameplayTag(DemoBossTags::Casting)) Enemy->CancelPendingAttacks();
		Enemy->Navigation->Stop(TEXT("NonCombat")); // Hub/终局/玩家死亡立即丢弃旧追击路径。
		Enemy->Roster->Cancel(); // 特殊读条/弹幕/子单位不能带到下一会话。
		Enemy->CloseCombat->Cancel(TEXT("NonCombat")); // 清除近战前摇/冲刺预警，安全区不结算旧攻击。
		Enemy->Tactics->ResetMovement(); // 不把旧战术站位带入后续阶段。
		return;
	}
	// 全图前摇期间完全静止，只更新红光呼吸；不依赖帧数决定2秒结束时间。
	Enemy->PublishFacing(Enemy->CloseCombat->HasLockedFacing()?Enemy->CloseCombat->GetLockedDirection():Player->GetActorLocation()-Enemy->GetActorLocation()); // 决策生成方向，表现不反查技能。
	if (Enemy->Roster->Update(Player,DeltaSeconds)) return; // 新执行器优先，返回true时独占本帧移动和攻击。
	if (Enemy->CloseCombat->Update(Player,Enemy->MoveSpeed*Enemy->Attributes->GetMoveSpeedMultiplier(),DeltaSeconds))
	{ Enemy->PublishFacing(Enemy->CloseCombat->HasLockedFacing()?Enemy->CloseCombat->GetLockedDirection():Player->GetActorLocation()-Enemy->GetActorLocation()); return; } // 纯近身角色唯一行为入口，禁止旧接触攻击/飞行物叠加。
	if (UDemoBossDiveAbility* Dive=Enemy->GetDiveAbility(); Dive&&Dive->IsActive()) // ASC拥有实例；当前同步帧驱动唯一空中移动。
	{ Enemy->Navigation->Stop(TEXT("DiveCasting")); Dive->Advance(DeltaSeconds); return; }
	if (Enemy->bGlobalAttackPending)
	{
		Enemy->Navigation->Stop(TEXT("Casting")); // 固定2秒前摇保持静止，不存在独立移动Tick。
		Enemy->TelegraphLight->SetIntensity(18000.f + 10000.f * FMath::Sin((GetWorld()->GetTimeSeconds() - Enemy->GlobalWindupStart) * 16.f));
		Enemy->TelegraphShell->SetRelativeScale3D(FVector(2.7f + .15f * FMath::Sin((GetWorld()->GetTimeSeconds() - Enemy->GlobalWindupStart) * 16.f)));
		return;
	}
	// 到期俯冲优先，但绝不抢断已经开始的全图/地面前摇。
	if (Enemy->bBossEnemy && !Enemy->Roster->ManagesBoss() && Enemy->TryStartDiveAttack()) { Enemy->Navigation->Stop(TEXT("DiveCasting")); return; }
	// 全图技能不受视线或4500cm索敌半径限制；Boss在合法战斗阶段即可开始施法。
	if (Enemy->bBossEnemy && !Enemy->Roster->ManagesBoss() && Enemy->TryStartGlobalAttack()) { Enemy->Navigation->Stop(TEXT("Casting")); return; }
	// 只在自身竞技场半径内索敌，不能跨越区域追到安全区。
	const FVector ToPlayer = Player->GetActorLocation() - Enemy->GetActorLocation(); // 厘米方向，范围仍沿用旧技能规则。
	const float Distance = ToPlayer.Size2D(); // 平面距离厘米，索敌上限不改。
	if (Distance > 4500.f) { Enemy->Navigation->Stop(TEXT("OutOfRange")); return; }
	// 飞行物与原攻击各自冷却；原Boss地面预警中不叠放新的发射动作。
	if (!Enemy->bAttackPending) Enemy->TryFireProjectile(Player);
	// AI只处理玩法；血条的屏幕朝向不依赖追击Tick，因此前摇/静止时也保持水平。
	// 路径绕障取代直接朝玩家推移；距离够近但视线受阻仍继续寻路，避免停在掩体后。
	const float StopDistance = Enemy->bBossEnemy ? 700.f : 130.f; // cm，仅控制原局部攻击；远程战术距离不能扩大近战命中半径。
	if (Enemy->bAttackPending || Enemy->AbilitySystem->HasMatchingGameplayTag(DemoAmmoTags::Frozen)) Enemy->Navigation->Stop(TEXT("CastingOrFrozen")); // 只停移动，不阻断后续攻击。
	else Enemy->Tactics->Move(Player,Enemy->MoveSpeed * Enemy->Attributes->GetMoveSpeedMultiplier(),DeltaSeconds); // 战术层选目标，Nav执行；冰霜GAS倍率继续覆盖所有移动。
	// 攻击节奏使用 World 时间，与帧率无关；Boss 提前锁定位置而非必中的追踪伤害。
	const float Now = GetWorld()->GetTimeSeconds(); // World秒，所有技能计时受暂停控制。
	// 原局部攻击需要可见性；防止Boss隔墙连续前摇阻止绕行，全图技能仍按原规则释放。
	if (Distance <= StopDistance + 45.f && Now >= Enemy->NextAttackTime && !Enemy->bAttackPending && Enemy->Navigation->CanSee(Player))
	{
		Enemy->NextAttackTime = Now + Enemy->Tactics->AttackInterval(Enemy->bBossEnemy ? Enemy->AttackSettings.AreaInterval : Enemy->AttackSettings.MeleeInterval,Enemy->bBossEnemy?1.f:.2f); // 二阶段只改变下一轮冷却，不缩短当前预警。
		if (Enemy->bBossEnemy)
		{
			Enemy->bAttackPending = true;
			Enemy->PendingAreaTarget=Player; // 前摇目标身份固定，落点依旧使用开始时位置。
			Enemy->PendingAttackLocation = Player->GetActorLocation() - FVector(0,0,90);
			DrawDebugSphere(GetWorld(), Enemy->PendingAttackLocation, 300.f, 32, FColor::Orange, false, 0.9f, 0, 4.f);
			GetWorld()->GetTimerManager().SetTimer(Enemy->BossAttackTimer, Enemy, &ADemoEnemy::ResolveBossAttack, 0.9f, false);
			Enemy->Presentation->Play(TEXT("Area")); // 释放姿势对齐0.9秒；实际伤害仍只由原回调执行一次。
			UE_LOG(LogFPSDemo, Log, TEXT("Boss telegraph at %s"), *Enemy->PendingAttackLocation.ToString());
		}
		else
		{
			Enemy->Presentation->Play(TEXT("Melee")); // 原近战是即时接触，动作从撞击姿势回收，不增加伤害前摇。
			// 墙体/高差阻挡近战；玩家跳跃也可躲过部分接触攻击。
			FHitResult SightHit; // 接触攻击的真实最近阻挡，不允许隔墙伤害。
			FCollisionQueryParams SightQuery(SCENE_QUERY_STAT(DemoMelee), false, Enemy); // 只忽略来源敌人。
			GetWorld()->LineTraceSingleByChannel(SightHit, Enemy->GetActorLocation(), Player->GetActorLocation(), ECC_Visibility, SightQuery);
			if (SightHit.GetActor() == Player && ToPlayer.Size() < 190.f)
				// 实际近战伤害和生成日志使用同一 GAS 属性，避免配置只改变展示数值。
				DemoEffects::ApplyDamage(Enemy->AbilitySystem,Player->GetAbilitySystemComponent(),Enemy->GetAttackPower(),
                    EDemoPlayerDamageKind::Melee,Enemy->GetActorLocation(),FVector::ZeroVector,&SightHit);
		}
	}

}
void UDemoEnemyCombatComponent::Cancel(FName Reason)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 同步借用宿主，取消栈内不保留额外所有权。
    if (!Enemy||bCancelling) return;
    bCancelling=true; Target.Reset();
    Enemy->Knockback->Cancel(Reason); // 死亡、EndPlay、会话撤销统一清理；普通命中仅中断当前动作，不走本全量取消。
    Enemy->Roster->Cancel(); // 先关闭新能力和拥有的危险物，清理不能发布击杀奖励。
	Enemy->bAttackPending=false; Enemy->bGlobalAttackPending=false; Enemy->PendingAreaTarget.Reset();
	GetWorld()->GetTimerManager().ClearTimer(Enemy->BossAttackTimer); GetWorld()->GetTimerManager().ClearTimer(Enemy->GlobalAttackTimer);
	Enemy->PublishFacing(FVector::ZeroVector);
	Enemy->Navigation->Stop(Reason);
	Enemy->CloseCombat->Cancel(TEXT("AttacksCancelled")); // 死亡/EndPlay/阶段切换复用同一幂等取消，不产生延迟伤害。
	Enemy->Presentation->Cancel(); // 阶段退出也撤销全身Slot，避免旧动作延续到非战斗。
	if (Enemy->DiveHandle.IsValid()) Enemy->AbilitySystem->CancelAbilityHandle(Enemy->DiveHandle); // GAS EndAbility统一撤销无敌、特效和空中状态。

	Enemy->Tactics->ResetMovement(); // 撤销施法时旧战术点同样失效，角色/二阶段状态仍归当前实例。
	Enemy->SetGlobalTelegraph(false); Enemy->UpdateVisualState(); Enemy->ForceNetUpdate(); // 撤销红光/地面预警同栈提交，客户端不得保留旧技能提示。
	UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_ATTACKS_CANCELLED %s"), *Enemy->GetName());

    bCancelling=false;
}
