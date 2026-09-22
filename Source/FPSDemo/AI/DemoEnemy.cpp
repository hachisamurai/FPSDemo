#include "AI/DemoEnemy.h" // 实现首先包含自身声明，保持独立编译的依赖边界。
#include "GameplayEffectExtension.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "AI/DemoEnemyNavigation.h"
#include "AI/DemoEnemyProjectile.h"
#include "AI/DemoAttackPulse.h"
#include "Audio/DemoWeaponAudio.h"
#include "Sound/SoundBase.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h" // AI只读取阶段；死亡通过事件通知刷怪服务，不依赖GameMode。
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h" // 主骨骼外观和ASC动画上下文。
#include "Animation/DemoEnemyPresentation.h" // 只提交动作，不把伤害移入动画回调。
#include "Combat/DemoEnemyHitZones.h" // 统一瞄准/实体子弹通道；移动根与受击网格必须使用相同常量。
#include "Weapons/Projectiles/DemoShotContext.h" // 同一枪散弹共享反馈资格，GE Context只借用其弱SourceObject。
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADemoEnemy::ADemoEnemy()
{
	DEMO_LOG_CALL();
	PrimaryActorTick.bCanEverTick = true;
	CreateDefaultSubobject<UDemoAmmoStatus>(TEXT("AmmoStatus")); // 敌人自身ASC的状态协调器。
	bReplicates = true;
	SetReplicateMovement(true);
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(48.f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionResponseToChannel(DemoEnemyHitZones::TraceChannel,ECR_Ignore); // 保留原墙体/导航阻挡，子弹穿过外球查找真实肢体。
	Collision->SetCollisionResponseToChannel(DemoEnemyHitZones::ProjectileChannel,ECR_Ignore); // 实体子弹同样穿过导航根，不能把球根当作机身命中。
	SetRootComponent(Collision);
	Collision->SetCanEverAffectNavigation(false); // 动态敌人不挖空自己的导航路线；同伴由Sweep与让行处理。
	Navigation = CreateDefaultSubobject<UDemoEnemyNavigation>(TEXT("Navigation")); // 保留原Actor与GAS所有权，仅替换追击算法。
	Tactics = CreateDefaultSubobject<UDemoEnemyTactics>(TEXT("Tactics")); // 角色分工由数据驱动，无需用户手工接线。
	CloseCombat=CreateDefaultSubobject<UDemoCloseCombat>(TEXT("CloseCombat")); // 只有新Melee/Charger角色启用，旧攻击仍保留。
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Drone"));
	Visual->SetupAttachment(Collision);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AnimatedBody=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AnimatedBody")); // 所有敌人仅此一个主骨骼组件。
	AnimatedBody->SetupAttachment(Collision);
	AnimatedBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly); // PhysicsAsset随动画更新，提供瞄准查询和子弹扫掠，不启用布娃娃或移动阻挡。
	AnimatedBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	AnimatedBody->SetCollisionResponseToChannel(DemoEnemyHitZones::TraceChannel,ECR_Block);
	AnimatedBody->SetCollisionResponseToChannel(DemoEnemyHitZones::ProjectileChannel,ECR_Block); // 由真实刚体返回BoneName，Mesh外观不产生额外伤害碰撞。
	AnimatedBody->SetGenerateOverlapEvents(false); // 部位判定依赖HitResult.BoneName，不产生重复Overlap伤害。
	AnimatedBody->SetCanEverAffectNavigation(false);
	Presentation=CreateDefaultSubobject<UDemoEnemyPresentation>(TEXT("Presentation")); // Actor持有，与ASC一同卸载。
	// 球体和材质均是引擎可打包资源。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	Visual->SetStaticMesh(Sphere.Object);
	// 显式使用模板纯色材质，默认基础球体的棋盘材质不支持颜色参数。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> EnemyMaterial(TEXT("/Game/LevelPrototyping/Materials/M_Solid"));
	if (EnemyMaterial.Object) Visual->SetMaterial(0, EnemyMaterial.Object);
	// 血条/状态图标由HUD以屏幕空间绘制，敌人不再持有会随朝向变斜的TextRender。
	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	Attributes = CreateDefaultSubobject<UDemoAttributeSet>(TEXT("Attributes"));
	// 真实资源由 Editor Python 导入/保存；与 Fire Cue 分目录，命中可单独调音。
	static ConstructorHelpers::FObjectFinder<USoundBase> HitCue(TEXT("/Game/Audio/SFX/Weapons/Sword/Hit/Cues/SC_Sword_Hit"));
	HitSound = HitCue.Object;
	// 独立红光壳避免改写敌人常态材质；Editor创建的加法发光材质也能在Shipping显示。
	TelegraphShell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GlobalTelegraphShell"));
	TelegraphShell->SetupAttachment(Collision);
	TelegraphShell->SetStaticMesh(Sphere.Object);
	TelegraphShell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TelegraphShell->SetRelativeScale3D(FVector(2.7f));
	TelegraphShell->SetHiddenInGame(true);
	TelegraphShell->CastShadow = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/EnemyAttacks/M_EnemyGlow"));
	if (Glow.Object) TelegraphShell->SetMaterial(0, Glow.Object);
	TelegraphLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlobalTelegraphLight"));
	TelegraphLight->SetupAttachment(Collision);
	TelegraphLight->SetLightColor(FLinearColor::Red);
	TelegraphLight->SetIntensity(18000.f);
	TelegraphLight->SetAttenuationRadius(900.f);
	TelegraphLight->SetCastShadows(false);
	TelegraphLight->SetVisibility(false);
}
void ADemoEnemy::BeginPlay()
{
	DEMO_LOG_CALL();
	Super::BeginPlay();
	ConfigureWeaponCollisionResponses(); // 旧已保存蓝图可能覆盖原生构造默认值，运行期恢复新增通道规则。
	AbilitySystem->InitAbilityActorInfo(this, this);
	HealthChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).AddUObject(this, &ADemoEnemy::OnHealthChanged);
	NextAttackTime = GetWorld()->GetTimeSeconds() + 2.f;
	// 第一发弹保留出生2秒缓冲；Boss首次全图按模板GlobalInterval延迟。
	NextProjectileTime = NextAttackTime;
}
void ADemoEnemy::Configure(const FDemoEnemySpawnStats& Stats)
{
	DEMO_LOG_CALL();
	// 先关闭受伤音效门控，避免基础属性初始化的负向变化被误判为战斗伤害。
	if (bHealthConfigured || bDead) { UE_LOG(LogFPSDemo, Warning, TEXT("Configure rejected: already configured or dead")); return; }
	bHealthConfigured = false;
	// 单次出生配置不可重入；避免活怪被二次配置改变死亡奖励。
	const bool bBoss = Stats.bBoss; // 仅用于初始化体积/外观，实际行为读取成员。
	const float Health = Stats.Health; // 已完成等级、难度两步计算的生命点数。
	bBossEnemy = bBoss;
	DiveSettings=Stats.DiveSlam;
	DiveSettings.bEnabled=Stats.bUseDive&&bBoss&&Stats.DiveSlam.bEnabled&&Stats.DiveSlam.IsValid(); // 非法直接调用不授予新技能。
	NextDiveTime=GetWorld()->GetTimeSeconds()+DiveSettings.InitialDelay;
	if (DiveSettings.bEnabled && HasAuthority()) DiveHandle=AbilitySystem->GiveAbility(FGameplayAbilitySpec(UDemoBossDiveAbility::StaticClass(),1));
	Tactics->Configure(Stats.Tactics,Stats.FormationSlot,Stats.bUseTactics); // 独立实例快照，旧直接生成默认不启用战术。
	NextProjectileTime = GetWorld()->GetTimeSeconds()+2.f+Tactics->OpeningDelay(); // 首发至少2秒，混编错峰避免同帧齐射。
	// 直接C++调用也防御非法配置；数据表路径在开局阶段已严格验证并拒绝错误。
	AttackSettings = Stats.Attack.IsValid() ? Stats.Attack : FDemoEnemyAttackSettings();
	if (!Stats.Attack.IsValid()) UE_LOG(LogFPSDemo, Warning, TEXT("Invalid direct spawn attack settings; using defaults"));
	CloseCombat->Configure(Stats.CloseCombat,!bBoss&&(Tactics->GetRole()==EDemoEnemyRole::Melee||Tactics->GetRole()==EDemoEnemyRole::Charger),Tactics->GetRole()==EDemoEnemyRole::Charger,AttackSettings.MeleeInterval,Tactics->OpeningDelay()); // 统一角色解析结果，禁用战术不会误启用新攻击。
	NextGlobalTime = GetWorld()->GetTimeSeconds() + AttackSettings.GlobalInterval;
	MoveSpeed = Stats.MoveSpeed;
	CoinReward = Stats.CoinReward;
	MonsterLevel = Stats.MonsterLevel;
	AbilitySystem->SetNumericAttributeBase(UDemoAttributeSet::GetMaxHealthAttribute(), Health);
	AbilitySystem->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), Health);
	AbilitySystem->SetNumericAttributeBase(UDemoAttributeSet::GetAttackPowerAttribute(), Stats.AttackPower);
	UE_LOG(LogFPSDemo, Log, TEXT("SPAWN Lv%d boss=%d HP=%.2f ATK=%.2f silver=%d"), MonsterLevel, bBoss, Health, Stats.AttackPower, CoinReward);
	Visual->SetRelativeScale3D(FVector(bBoss ? 2.4f : 1.f));
	Collision->SetSphereRadius(bBoss ? 115.f : 48.f);
	// 模型已按厘米制作，禁止继承旧球体的2.4倍Boss比例；失败时保留可见回退并打印错误。
	Visual->SetHiddenInGame(Presentation->Configure(bBoss,AnimatedBody));
	ConfigureWeaponCollisionResponses(); // Configure更换骨骼后再次校正响应；保留Presentation对非法受击资产的NoCollision拒绝。
	// HUD读取Boss标识选择头顶偏移和血条颜色，不在初始化时写展示文字或摄像机旋转。
	// 回退球体和骨骼灯带都使用独立MID；装甲保持灰白，仅灯带延续角色颜色辨识。
	if (UMaterialInstanceDynamic* Material = Visual->CreateAndSetMaterialInstanceDynamic(0)) // 网格拥有独立MID，用颜色辨认角色。
	{
		const FLinearColor RoleColor = Tactics->GetRole()==EDemoEnemyRole::Melee ? FLinearColor(.05f,.9f,.25f)
			: Tactics->GetRole()==EDemoEnemyRole::Charger ? FLinearColor(1.f,.5f,.02f)
			: Tactics->GetRole()==EDemoEnemyRole::Ranged ? FLinearColor(.04f,.35f,.9f)
			: (Tactics->GetRole()==EDemoEnemyRole::Flanker ? FLinearColor(.6f,.08f,.8f) : FLinearColor(.8f,.04f,.12f)); // 近战绿、冲刺橙；原三色不变，骨骼灯带与回退球同步。
		Material->SetVectorParameterValue(TEXT("Color"), bBoss ? FLinearColor(1.f,.15f,.02f) : RoleColor);
		const int32 LightSlot=AnimatedBody->GetMaterialIndex(TEXT("M_Enemy_Amber")); // 按稳定材质槽名查找，不能假设导出顺序。
		if (LightSlot!=INDEX_NONE)
			if (UMaterialInstanceDynamic* Light=AnimatedBody->CreateAndSetMaterialInstanceDynamic(LightSlot)) // 当前敌人强持有的灯带MID。
				Light->SetVectorParameterValue(TEXT("EnemyTint"),bBoss?FLinearColor(1.f,.15f,.02f):RoleColor);
	}
	// 完成整批初始属性写入后才接受后续 Health GE 的声音反馈。
	bHealthConfigured = true;
}
void ADemoEnemy::ConfigureWeaponCollisionResponses()
{
	DEMO_LOG_CALL();
	Collision->SetCollisionResponseToChannel(DemoEnemyHitZones::TraceChannel,ECR_Ignore);
	Collision->SetCollisionResponseToChannel(DemoEnemyHitZones::ProjectileChannel,ECR_Ignore);
	AnimatedBody->SetCollisionResponseToAllChannels(ECR_Ignore); // 骨骼只接受两类武器查询，玩家/导航/敌人飞行物仍沿用原移动根碰撞。
	AnimatedBody->SetCollisionResponseToChannel(DemoEnemyHitZones::TraceChannel,ECR_Block);
	AnimatedBody->SetCollisionResponseToChannel(DemoEnemyHitZones::ProjectileChannel,ECR_Block);
}
UAbilitySystemComponent* ADemoEnemy::GetAbilitySystemComponent() const { DEMO_LOG_TICK(); return AbilitySystem; }
bool ADemoEnemy::IsAlive() const { DEMO_LOG_TICK(); return !bDead; }
bool ADemoEnemy::IsBoss() const { DEMO_LOG_TICK(); return bBossEnemy; }
float ADemoEnemy::GetHealth() const { DEMO_LOG_TICK(); return Attributes->GetHealth(); }
float ADemoEnemy::GetMaxHealth() const { DEMO_LOG_TICK(); return Attributes->GetMaxHealth(); }
int32 ADemoEnemy::GetCoinReward() const { DEMO_LOG_TICK(); return CoinReward; }
int32 ADemoEnemy::GetMonsterLevel() const { DEMO_LOG_TICK(); return MonsterLevel; }
float ADemoEnemy::GetAttackPower() const { DEMO_LOG_TICK(); return Attributes->GetAttackPower(); }

void ADemoEnemy::Tick(float DeltaSeconds)
{
	DEMO_LOG_TICK();
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || bDead) { Navigation->Stop(TEXT("Inactive")); return; }
	// 双重限制：即使敌人意外残留，也不会在 Hub/Reward/终局追击安全区玩家。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!State || State->Phase != EDemoPhase::Combat || !Player || !Player->GetDemoAttributes() || Player->GetDemoAttributes()->GetHealth() <= 0.f)
	{
		if (bAttackPending || bGlobalAttackPending || AbilitySystem->HasMatchingGameplayTag(DemoBossTags::Casting)) CancelPendingAttacks();
		Navigation->Stop(TEXT("NonCombat")); // Hub/终局/玩家死亡立即丢弃旧追击路径。
		CloseCombat->Cancel(TEXT("NonCombat")); // 清除近战前摇/冲刺预警，安全区不结算旧攻击。
		Tactics->ResetMovement(); // 不把旧战术站位带入后续阶段。
		return;
	}
	// 全图前摇期间完全静止，只更新红光呼吸；不依赖帧数决定2秒结束时间。
	if (CloseCombat->Update(Player,MoveSpeed*Attributes->GetMoveSpeedMultiplier(),DeltaSeconds)) return; // 纯近身角色唯一行为入口，禁止旧接触攻击/飞行物叠加。
	if (UDemoBossDiveAbility* Dive=GetDiveAbility(); Dive&&Dive->IsActive()) // ASC拥有实例；当前同步帧驱动唯一空中移动。
	{ Navigation->Stop(TEXT("DiveCasting")); Dive->Advance(DeltaSeconds); return; }
	if (bGlobalAttackPending)
	{
		Navigation->Stop(TEXT("Casting")); // 固定2秒前摇保持静止，不存在独立移动Tick。
		TelegraphLight->SetIntensity(18000.f + 10000.f * FMath::Sin((GetWorld()->GetTimeSeconds() - GlobalWindupStart) * 16.f));
		TelegraphShell->SetRelativeScale3D(FVector(2.7f + .15f * FMath::Sin((GetWorld()->GetTimeSeconds() - GlobalWindupStart) * 16.f)));
		return;
	}
	// 到期俯冲优先，但绝不抢断已经开始的全图/地面前摇。
	if (bBossEnemy && TryStartDiveAttack()) { Navigation->Stop(TEXT("DiveCasting")); return; }
	// 全图技能不受视线或4500cm索敌半径限制；Boss在合法战斗阶段即可开始施法。
	if (bBossEnemy && TryStartGlobalAttack()) { Navigation->Stop(TEXT("Casting")); return; }
	// 只在自身竞技场半径内索敌，不能跨越区域追到安全区。
	const FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
	const float Distance = ToPlayer.Size2D();
	if (Distance > 4500.f) { Navigation->Stop(TEXT("OutOfRange")); return; }
	// 飞行物与原攻击各自冷却；原Boss地面预警中不叠放新的发射动作。
	if (!bAttackPending) TryFireProjectile(Player);
	// AI只处理玩法；血条的屏幕朝向不依赖追击Tick，因此前摇/静止时也保持水平。
	// 路径绕障取代直接朝玩家推移；距离够近但视线受阻仍继续寻路，避免停在掩体后。
	const float StopDistance = bBossEnemy ? 700.f : 130.f; // cm，仅控制原局部攻击；远程战术距离不能扩大近战命中半径。
	if (bAttackPending || AbilitySystem->HasMatchingGameplayTag(DemoAmmoTags::Frozen)) Navigation->Stop(TEXT("CastingOrFrozen")); // 只停移动，不阻断后续攻击。
	else Tactics->Move(Player,MoveSpeed * Attributes->GetMoveSpeedMultiplier(),DeltaSeconds); // 战术层选目标，Nav执行；冰霜GAS倍率继续覆盖所有移动。
	// 攻击节奏使用 World 时间，与帧率无关；Boss 提前锁定位置而非必中的追踪伤害。
	const float Now = GetWorld()->GetTimeSeconds();
	// 原局部攻击需要可见性；防止Boss隔墙连续前摇阻止绕行，全图技能仍按原规则释放。
	if (Distance <= StopDistance + 45.f && Now >= NextAttackTime && !bAttackPending && Navigation->CanSee(Player))
	{
		NextAttackTime = Now + Tactics->AttackInterval(bBossEnemy ? AttackSettings.AreaInterval : AttackSettings.MeleeInterval,bBossEnemy?1.f:.2f); // 二阶段只改变下一轮冷却，不缩短当前预警。
		if (bBossEnemy)
		{
			bAttackPending = true;
			PendingAttackLocation = Player->GetActorLocation() - FVector(0,0,90);
			DrawDebugSphere(GetWorld(), PendingAttackLocation, 300.f, 32, FColor::Orange, false, 0.9f, 0, 4.f);
			GetWorldTimerManager().SetTimer(BossAttackTimer, this, &ADemoEnemy::ResolveBossAttack, 0.9f, false);
			Presentation->Play(TEXT("Area")); // 释放姿势对齐0.9秒；实际伤害仍只由原回调执行一次。
			UE_LOG(LogFPSDemo, Log, TEXT("Boss telegraph at %s"), *PendingAttackLocation.ToString());
		}
		else
		{
			Presentation->Play(TEXT("Melee")); // 原近战是即时接触，动作从撞击姿势回收，不增加伤害前摇。
			// 墙体/高差阻挡近战；玩家跳跃也可躲过部分接触攻击。
			FHitResult SightHit;
			FCollisionQueryParams SightQuery(SCENE_QUERY_STAT(DemoMelee), false, this);
			GetWorld()->LineTraceSingleByChannel(SightHit, GetActorLocation(), Player->GetActorLocation(), ECC_Visibility, SightQuery);
			if (SightHit.GetActor() == Player && ToPlayer.Size() < 190.f)
				// 实际近战伤害和生成日志使用同一 GAS 属性，避免配置只改变展示数值。
				DemoEffects::Apply(AbilitySystem, Player->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -GetAttackPower());
		}
	}
}
void ADemoEnemy::ResolveBossAttack()
{
	DEMO_LOG_CALL();
	// 已撤销/重复回调不能绕过技能生命周期再次结算。
	if (!bAttackPending) { UE_LOG(LogFPSDemo, Log, TEXT("Boss area ignored: not pending")); return; }
	bAttackPending = false;
	// 延迟回调重新取对象与阶段，死亡/胜败/换关后不会结算旧伤害。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!HasAuthority() || bDead || !State || State->Phase != EDemoPhase::Combat || !Player) return;
	DrawDebugSphere(GetWorld(), PendingAttackLocation, 300.f, 24, FColor::Red, false, 0.2f, 0, 6.f);
	if (FVector::Dist2D(Player->GetActorLocation(), PendingAttackLocation) <= 300.f)
		// Boss 同样读取 GAS 攻击力，预警时间及半径不受难度倍率改变。
		DemoEffects::Apply(AbilitySystem, Player->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -GetAttackPower());
}
void ADemoEnemy::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	DEMO_LOG_CALL();
	// 只在已初始化的真实伤害边沿检查二阶段；死亡不进入，治疗不退出，也不抢断现有2秒前摇。
	if (HasAuthority() && bHealthConfigured && !bDead && Data.NewValue<Data.OldValue && Tactics->UpdateRage(Data.NewValue,Attributes->GetMaxHealth()))
	{
		Presentation->Play(TEXT("Rage")); // 正在施法时表现组件排队，不中断既有预警。
		if (UMaterialInstanceDynamic* Material=Cast<UMaterialInstanceDynamic>(Visual->GetMaterial(0))) // 借用本敌人MID，紫红机体标记二阶段。
			Material->SetVectorParameterValue(TEXT("Color"),FLinearColor(1.f,.02f,.4f));
		const int32 LightSlot=AnimatedBody->GetMaterialIndex(TEXT("M_Enemy_Amber")); // 只更新灯带，保持重甲材质可读。
		if (LightSlot!=INDEX_NONE)
			if (UMaterialInstanceDynamic* Light=Cast<UMaterialInstanceDynamic>(AnimatedBody->GetMaterial(LightSlot))) // 配置阶段创建的实例。
				Light->SetVectorParameterValue(TEXT("EnemyTint"),FLinearColor(1.f,.02f,.4f));
	}
	// HUD逐帧只读当前/最大生命，受伤和治疗直接改变进度，不维护第二份显示血量。
	// 仅实际扣血且尚未死亡时播放；治疗/生成初始化/零伤害不会触发。致命一击也先播再隐藏敌人。
	if (HasAuthority() && bHealthConfigured && !bDead && Data.NewValue < Data.OldValue
        && (!Data.GEModData || Data.GEModData->EffectSpec.Def->GetClass()!=UDemoBurnEffect::StaticClass())) // 灼烧周期不重复播放子弹肉体击中声。
	{
		UDemoShotContext* Shot=Data.GEModData?Cast<UDemoShotContext>(Data.GEModData->EffectSpec.GetContext().GetSourceObject()):nullptr; // 同步回调借用；所有在飞弹丸以UPROPERTY持有Context直到本次结算返回。
		const bool bFirstFeedback=!Shot||Shot->TryMarkFeedback(this); // 先登记再播放，散弹跨帧也最多一组反馈；爆炸等非子弹伤害保留原路径。
		if(bFirstFeedback)
		{
			DemoWeaponAudio::PlayAtLocation(this, HitSound, GetActorLocation(), TEXT("Weapon.Hit"));
			if (Data.NewValue>0.f) Presentation->Play(TEXT("Hit")); // 非致命且不抢占攻击；DOT不逐跳抖动。
		}
		else UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_FEEDBACK_DUPLICATE enemy=%s"),*GetName()); // 包体裁掉重复弹丸反馈日志。
	}
	if (!HasAuthority() || bDead || Data.NewValue > 0.f) return;
	bDead = true;
	FindComponentByClass<UDemoAmmoStatus>()->Clear(); // 归零立即撤销DOT/控制/GC，避免延迟销毁期间残留。
	Navigation->Stop(TEXT("Dead")); // 归零当帧清路线，避免延迟销毁期间残留追击。
	SetActorEnableCollision(false);
	CancelPendingAttacks();
	Presentation->Play(TEXT("Death")); // 撤销旧攻击后播死亡；立即退出存活集合，尸体没有碰撞。
	OnDefeated.Broadcast(this); // 刷怪组件去重后再通知奖励系统；测试自有/非战役怪没有经济订阅者。
	// 不在 GAS 委托广播栈内同步销毁 ASC；下一帧后由生命周期清理。
	SetLifeSpan(1.65f); // 死亡动作1.5秒后移除，不延迟清关/奖励，不依赖Notify销毁ASC。
}
void ADemoEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
	Navigation->Stop(TEXT("EndPlay")); // 仅持有路径点值，无异步导航委托遗留。
	CancelPendingAttacks();
	AbilitySystem->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
	// 异常销毁由刷怪组件订阅Actor.OnEndPlay处理；正常取消先解绑，不会触发虚假的击杀或失败。
	Super::EndPlay(EndPlayReason);
}

bool ADemoEnemy::TryFireProjectile(ADemoCharacter* Player)
{
	DEMO_LOG_TICK();
	if (CloseCombat->IsEnabled()) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_REJECT pure close-combat role")); return false; } // 直接外部调用同样拒绝。
	// State/Now为当前World快照，所有拒绝分支保留高频日志避免每帧Log级刷屏。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	const float Now = GetWorld()->GetTimeSeconds();
	if (!HasAuthority() || !bHealthConfigured || bDead || bAttackPending || bGlobalAttackPending || AbilitySystem->HasMatchingGameplayTag(DemoBossTags::Casting) || Now < NextProjectileTime
		|| !State || State->Phase != EDemoPhase::Combat || !Player || !Player->GetDemoAttributes() || Player->GetDemoAttributes()->GetHealth() <= 0.f)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("Projectile deferred: phase/authority/target/cooldown/cast"));
		return false;
	}
	// 有限预判只改变发射瞬间方向；视线仍检查真实玩家，弹体继续直线飞行并碰墙。
	const FVector Direction = (Tactics->AimPoint(Player,AttackSettings.ProjectileSpeed) - GetActorLocation()).GetSafeNormal(); // 世界归一化方向，冲刺时不外推。
	FHitResult SightHit; // 最近阻挡用于防止墙后射击，忽略自身碰撞。
	FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoEnemyProjectileSight), false, this);
	if (FVector::Dist(GetActorLocation(), Player->GetActorLocation()) > AttackSettings.ProjectileRange)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("Projectile deferred: out of range"));
		return false;
	}
	GetWorld()->LineTraceSingleByChannel(SightHit, GetActorLocation(), Player->GetActorLocation(), ECC_Visibility, Query);
	if (SightHit.GetActor() != Player)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("Projectile deferred: sight blocked by %s"), *GetNameSafe(SightHit.GetActor()));
		return false;
	}
	// 球体从中心发射并忽略来源，避免偏移枪口在近墙处穿越碰撞；Deferred保证首次Tick前完成来源初始化。
	const FTransform SpawnTransform(Direction.Rotation(), GetActorLocation());
	ADemoEnemyProjectile* Projectile = GetWorld()->SpawnActorDeferred<ADemoEnemyProjectile>(ADemoEnemyProjectile::StaticClass(), SpawnTransform,
		this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile) { UE_LOG(LogFPSDemo, Warning, TEXT("Projectile spawn failed")); return false; }
	NextProjectileTime = Now + Tactics->AttackInterval(AttackSettings.ProjectileInterval,.2f); // 成功生成才消费冷却，与公开接口契约一致。
	Projectile->Initialize(this, Direction, AttackSettings.ProjectileSpeed);
	Projectile->FinishSpawning(SpawnTransform);
	Presentation->Play(TEXT("Fire")); // 发射成功才表现后坐，保持中心发射的防穿墙规则。
	return true;
}
bool ADemoEnemy::TryStartGlobalAttack()
{
	DEMO_LOG_TICK();
	// Actor/State均为当前World借用，全图技能只影响本次战斗中存活的本地玩家。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	const ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const float Now = GetWorld()->GetTimeSeconds(); // 技能间隔按开始时间而非帧数计算。
	if (!HasAuthority() || !bHealthConfigured || bDead || !bBossEnemy || bAttackPending || bGlobalAttackPending || AbilitySystem->HasMatchingGameplayTag(DemoBossTags::Casting)
		|| Now < NextGlobalTime || !State || State->Phase != EDemoPhase::Combat
		|| !Player || !Player->GetDemoAttributes() || Player->GetDemoAttributes()->GetHealth() <= 0.f)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("Global attack deferred: phase/target/cooldown/cast"));
		return false;
	}
	bGlobalAttackPending = true;
	GlobalWindupStart = Now;
	GlobalAttackLevel = State->LevelNumber;
	NextGlobalTime = Now + Tactics->AttackInterval(AttackSettings.GlobalInterval,3.f); // 二阶段最多压至3秒开始间隔，固定2秒前摇不变。
	SetGlobalTelegraph(true);
	GetWorldTimerManager().SetTimer(GlobalAttackTimer, this, &ADemoEnemy::ResolveGlobalAttack, 2.f, false);
	Presentation->Play(TEXT("Global")); // 动作2秒处爆发；不另建伤害定时器。
	UE_LOG(LogFPSDemo, Log, TEXT("BOSS_GLOBAL_WINDUP seconds=2 source=%s position=%s level=%d"), *GetName(), *GetActorLocation().ToString(), GlobalAttackLevel);
	return true;
}
bool ADemoEnemy::IsGlobalAttackWindingUp() const { DEMO_LOG_TICK(); return bGlobalAttackPending; }
void ADemoEnemy::SetGlobalTelegraph(bool bEnabled)
{
	DEMO_LOG_CALL();
	TelegraphShell->SetHiddenInGame(!bEnabled);
	TelegraphLight->SetVisibility(bEnabled);
}
void ADemoEnemy::CancelPendingAttacks()
{
	DEMO_LOG_CALL();
	CloseCombat->Cancel(TEXT("AttacksCancelled")); // 死亡/EndPlay/阶段切换复用同一幂等取消，不产生延迟伤害。
	Presentation->Cancel(); // 阶段退出也撤销全身Slot，避免旧动作延续到非战斗。
	if (DiveHandle.IsValid()) AbilitySystem->CancelAbilityHandle(DiveHandle); // GAS EndAbility统一撤销无敌、特效和空中状态。
	GetWorldTimerManager().ClearTimer(BossAttackTimer);
	GetWorldTimerManager().ClearTimer(GlobalAttackTimer);
	bAttackPending = false;
	bGlobalAttackPending = false;
	Tactics->ResetMovement(); // 撤销施法时旧战术点同样失效，角色/二阶段状态仍归当前实例。
	SetGlobalTelegraph(false);
	UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_ATTACKS_CANCELLED %s"), *GetName());
}
void ADemoEnemy::ResolveGlobalAttack()
{
	DEMO_LOG_CALL();
	if (!bGlobalAttackPending) { UE_LOG(LogFPSDemo, Log, TEXT("Global ignored: no pending cast")); return; }
	bGlobalAttackPending = false; // 先消费本次技能，回血与伤害均不能重入重复结算。
	SetGlobalTelegraph(false);
	// 重新验证阶段/关卡/生命；取消、死亡和离开战斗不算成功躲避，不发回血奖励。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!HasAuthority() || bDead || !State || State->Phase != EDemoPhase::Combat || State->LevelNumber != GlobalAttackLevel
		|| !Player || !Player->GetDemoAttributes() || Player->GetDemoAttributes()->GetHealth() <= 0.f)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("BOSS_GLOBAL_CANCELLED stale source/phase/player"));
		return;
	}
	// 特效从实际释放位置产生，伤害为该时刻全地图结算，墙/距离不能规避。
	GetWorld()->SpawnActor<ADemoAttackPulse>(GetActorLocation(), FRotator::ZeroRotator);
	if (Player->IsDashEvading())
	{
		DemoEffects::Apply(AbilitySystem, Player->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), 5.f);
		UE_LOG(LogFPSDemo, Log, TEXT("BOSS_GLOBAL_EVADED heal=5 (clamped by MaxHealth)"));
		return;
	}
	// 只有健康实际减少才施加减速；0攻击力、无效GE或已死亡不会获得新的减速状态。
	const float BeforeHealth = Player->GetDemoAttributes()->GetHealth();
	DemoEffects::Apply(AbilitySystem, Player->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -GetAttackPower());
	if (Player->GetDemoAttributes()->GetHealth() < BeforeHealth && Player->GetDemoAttributes()->GetHealth() > 0.f)
		DemoEffects::ApplySlow(AbilitySystem, Player->GetAbilitySystemComponent(), AttackSettings.SlowMultiplier, AttackSettings.SlowDuration);
	UE_LOG(LogFPSDemo, Log, TEXT("BOSS_GLOBAL_HIT health=%.2f->%.2f"), BeforeHealth, Player->GetDemoAttributes()->GetHealth());
}

void ADemoEnemy::HandleGameplayCue(UObject* Self,FGameplayTag Tag,EGameplayCueEvent::Type Event,const FGameplayCueParameters& Parameters)
{
    DEMO_LOG_CALL();
    if(UDemoAmmoStatus* Status=FindComponentByClass<UDemoAmmoStatus>()) Status->HandleCue(Tag,Event,Parameters); // Actor拥有的本地表现处理器。
}
