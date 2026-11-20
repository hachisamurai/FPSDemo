// 职责：敌人权威生命/战斗组合及独立本地命中反应/尸体表现。使用：SpawnStats初始化，真实Health死亡边沿通知GAS并广播击败；弹簧/尸体只改网格，不授予伤害、奖励或Actor位移。
#include "AI/DemoEnemy.h"
#include "Weapons/Melee/DemoMeleeComponent.h" // GAS来源识别；近战金属声由实际伤害回执池唯一播放。
#include "AI/DemoEnemyRoster.h"
#include "GAS/DemoEnemyDefenseSet.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/DemoTalentCatalog.h"
// 对应头文件先包含，确保UBT独立编译检查本类头文件自包含；功能逻辑不变。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "AI/DemoEnemyTargeting.h" // 实现首先包含自身声明，保持独立编译的依赖边界。
#include "AI/DemoEnemyCombatComponent.h"
#include "AI/DemoEnemyKnockbackComponent.h" // 短时位移优先于常规AI，防同帧追击抵消击退。
#include "AI/DemoEnemyHitReactComponent.h" // 本地组件级弹簧与Facing配对，存活骨骼不启用模拟。
#include "Combat/Feedback/DemoHitFxSettings.h" // 只读重武器表现冷却，不改变AI/伤害时钟。
#include "Game/Flow/DemoCombatSession.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
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
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackComponent.h" // 只在已成立的Boss全图躲避回血分支通知本人表现。
#include "GameFramework/PlayerController.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h" // 主骨骼外观和ASC动画上下文。
#include "Animation/DemoEnemyPresentation.h" // 只提交动作，不把伤害移入动画回调。
#include "Animation/DemoEnemyAppearance.h" // 服务器在战术混编解析后冻结头部外观，客户端直接消费同一资产引用。
#include "Combat/DemoEnemyHitZones.h" // 统一瞄准/实体子弹通道；移动根与受击网格必须使用相同常量。
#include "Weapons/Projectiles/DemoShotContext.h" // 同一枪散弹共享反馈资格，GE Context只借用其弱SourceObject。
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "PhysicsEngine/PhysicsAsset.h" // 查询专用Kinematic/零约束资产必须直接回退，不把受击体误当布娃娃。
#include "PhysicsEngine/BodySetup.h" // UE5.4的USkeletalBodySetup随PhysicsAsset.h声明，基类定义使用BodySetup。
#include "PhysicsEngine/BodyInstance.h" // 本地尸体逐体严格碰撞与有限值检查，不改共享PhysicsAsset。

ADemoEnemy::ADemoEnemy()
{
	DEMO_LOG_CALL();
	PrimaryActorTick.bCanEverTick = true;
	Roster=CreateDefaultSubobject<UDemoEnemyRoster>(TEXT("Roster")); // 无第二套战斗Tick，统一由仲裁器调用。
    Defense=CreateDefaultSubobject<UDemoEnemyDefenseSet>(TEXT("Defense")); // 默认无盾，Guard配置后启用。
	CombatCoordinator=CreateDefaultSubobject<UDemoEnemyCombatComponent>(TEXT("CombatCoordinator")); // 无独立Tick，Actor明确驱动仲裁顺序。
    Knockback=CreateDefaultSubobject<UDemoEnemyKnockbackComponent>(TEXT("Knockback")); // 所有小怪/Boss共用执行器，不要求资产重建或物理模拟。
    HitReact=CreateDefaultSubobject<UDemoEnemyHitReactComponent>(TEXT("HitReact")); // 默认停Tick；非专服收到确认回执才启动，不创建第二套移动。
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
	AnimatedBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly); // 存活PhysicsAsset随动画做子弹查询；死亡后仅合格资产本地模拟，当前查询专用资产直接无碰撞推倒。
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
    AreaTelegraph=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AreaTelegraph")); // 旧DebugSphere保留诊断，正式预警在打包/客户端也可见。
    AreaTelegraph->SetupAttachment(Collision); AreaTelegraph->SetAbsolute(true,true,true); AreaTelegraph->SetStaticMesh(Sphere.Object);
    AreaTelegraph->SetCollisionEnabled(ECollisionEnabled::NoCollision); AreaTelegraph->SetCanEverAffectNavigation(false); AreaTelegraph->CastShadow=false;
    AreaTelegraph->SetWorldScale3D(FVector(6.f,6.f,.06f)); AreaTelegraph->SetHiddenInGame(true);
    if (Glow.Object) AreaTelegraph->SetMaterial(0,Glow.Object);
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
	SetActorEnableCollision(false); // 未完成出生握手不得进入移动/受击查询。
	if (HasAuthority()&&bSpawnPrepared) InitializePreparedSpawn();
	else if (!HasAuthority()) OnRep_SpawnPresentation();
}
void ADemoEnemy::Configure(const FDemoEnemySpawnStats& Stats)
{
    DEMO_LOG_CALL();
    if (!HasAuthority()||!PrepareSpawn(Stats)||!InitializePreparedSpawn()||!ActivateSpawn())
        UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_CONFIGURE rejected incomplete/duplicate initialization"));
}
const FDemoEnemyBodyDefinition& ADemoEnemy::ResolveBodyDefinition(const UDemoEnemyDefinition* Definition,bool bBoss) const
{
    DEMO_LOG_CALL();
    const UDemoEnemyDefinition* Selected=Definition?Definition:(EnemyDefinition?EnemyDefinition.Get():GetDefault<UDemoEnemyDefinition>()); // 表覆盖优先，其次旧BP可选定义，最后原生默认。
    return Selected->ResolveBody(bBoss);
}
bool ADemoEnemy::PrepareSpawn(const FDemoEnemySpawnStats& Stats,const FDemoEnemyBodyDefinition* BodyOverride)
{
    DEMO_LOG_CALL();
    FString Error; // 握手验证错误仅本次调用保存，失败不发布死亡事件。
    if (!HasAuthority()||bSpawnPrepared||bDead||!FMath::IsFinite(Stats.Health)||Stats.Health<=0.f||!FMath::IsFinite(Stats.AttackPower)||Stats.AttackPower<0.f
        ||!Stats.Roster.IsValid()||!FMath::IsFinite(Stats.MoveSpeed)||Stats.MoveSpeed<=0.f||Stats.MonsterLevel<1)
    { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_PREPARE rejected authority/state/stats")); return false; }
    FrozenBody=BodyOverride?*BodyOverride:ResolveBodyDefinition(Stats.Definition,Stats.bBoss);
    if (!FrozenBody.Validate(Error)) return false;
    FrozenHitProfile=DuplicateObject<UDemoEnemyHitProfile>(DemoEnemyHitZones::SelectValidProfile(FrozenBody.HitProfile),this); // 实例拥有可回收快照，网络仍发送原Cook资产引用。
    if (!FrozenHitProfile) { UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_PREPARE hit profile snapshot failed")); return false; }
    PreparedStats=Stats; if (Stats.Definition) EnemyDefinition=Stats.Definition; // 强持有配置，不能让Deferred间隙丢失资产引用。
    if (const AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) // 仅服务器在出生时接线；后续战斗只读会话对象。
        if (Mode->GetRunFlow()) CombatSession=Mode->GetRunFlow()->GetCombatSession();
    bSessionBound=CombatSession.IsValid(); bSpawnPrepared=true; bSpawnActive=false;
    SetActorEnableCollision(false); Collision->SetSphereRadius(FrozenBody.CollisionRadius); // FinishSpawning之前已使用最终尺寸，禁止默认48cm漏检Boss。
    UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_PREPARED actor=%s radius=%.1f session=%d"),*GetName(),FrozenBody.CollisionRadius,bSessionBound);
    return true;
}
bool ADemoEnemy::IsSpawnInitialized() const { DEMO_LOG_TICK(); return bHealthConfigured&&!bDead; }
bool ADemoEnemy::ActivateSpawn()
{
    DEMO_LOG_CALL();
    if (!HasAuthority()||!IsSpawnInitialized()||bSpawnActive||(bSessionBound&&(!CombatSession.IsValid()||!CombatSession->IsActive())))
    { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_ACTIVATE rejected state/session")); return false; }
    // 会话拥有弱撤销入口；Flow无需知道敌人攻击协调器和DOT组件的具体类型。
    if (CombatSession.IsValid() && !CombatSession->RegisterCleanup(this, FSimpleDelegate::CreateUObject(this, &ADemoEnemy::OnCombatSessionEnded))) return false;
    bSpawnActive=true; SetActorEnableCollision(true); UpdateVisualState(); ForceNetUpdate();
    UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_ACTIVE actor=%s"),*GetName()); return true;
}
void ADemoEnemy::DeactivateSpawn()
{
    DEMO_LOG_CALL(); StopLocalDeathImpulse(); bSpawnActive=false; SetActorEnableCollision(false); CancelPendingAttacks(); UpdateVisualState(); ForceNetUpdate();
}
bool ADemoEnemy::IsCombatSessionValid() const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 兼容手工生成，同时所有生产敌人再检查不可复用会话。
    return State&&State->Phase==EDemoPhase::Combat&&(!bSessionBound||(CombatSession.IsValid()&&CombatSession->IsActive()));
}
const UDemoEnemyHitProfile* ADemoEnemy::GetHitProfile() const
{
    DEMO_LOG_TICK(); return FrozenHitProfile?FrozenHitProfile.Get():DemoEnemyHitZones::SelectValidProfile(FrozenBody.HitProfile);
}
bool ADemoEnemy::IsCombatReady() const
{
    DEMO_LOG_TICK(); return HasAuthority()&&bSpawnActive&&bHealthConfigured&&!bDead&&!CombatCoordinator->IsCancelling();
}
bool ADemoEnemy::CanReceiveKnockback() const
{
    DEMO_LOG_TICK();
    return !bBossEnemy && IsCombatReady() && IsCombatSessionValid(); // Boss身份来自服务器出生快照；免疫仅拦位移，不能阻断已结算的近战伤害。
}
ADemoCharacter* ADemoEnemy::GetCombatTarget() const { DEMO_LOG_TICK(); return CombatCoordinator->ResolveTarget(); }
bool ADemoEnemy::InitializePreparedSpawn()
{
    DEMO_LOG_CALL();
    if (bHealthConfigured) return true; // BeginPlay和FinishSpawning返回后的调用共享幂等结果。
    if (!bSpawnPrepared||!HasActorBegunPlay()||bDead) { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_INITIALIZE rejected prepare/beginplay/dead")); return false; }
    const FDemoEnemySpawnStats& Stats=PreparedStats; // 本实例拥有的值快照，不再读取DT或蓝图CDO。
	// 单次出生配置不可重入；避免活怪被二次配置改变死亡奖励。
	const bool bBoss = Stats.bBoss; // 仅用于初始化体积/外观，实际行为读取成员。
	const float Health = Stats.Health; // 已完成等级、难度两步计算的生命点数。
	bBossEnemy = bBoss;
	DiveSettings=Stats.DiveSlam;
	DiveSettings.bEnabled=Stats.bUseDive&&bBoss&&Stats.DiveSlam.bEnabled&&Stats.DiveSlam.IsValid(); // 非法直接调用不授予新技能。
	NextDiveTime=GetWorld()->GetTimeSeconds()+DiveSettings.InitialDelay;
	if (DiveSettings.bEnabled && HasAuthority()) DiveHandle=AbilitySystem->GiveAbility(FGameplayAbilitySpec(UDemoBossDiveAbility::StaticClass(),1));
	Tactics->Configure(Stats.Tactics,Stats.FormationSlot,Stats.bUseTactics); // 独立实例快照，旧直接生成默认不启用战术。
	// 特殊兵种优先于基础移动角色；必须在 Mixed 解析后选择，避免所有混编敌人共享同一个头部。
	const FName AppearanceKey(*(Stats.Roster.Kind!=EDemoRosterKind::None
		? StaticEnum<EDemoRosterKind>()->GetNameStringByValue(static_cast<int64>(Stats.Roster.Kind))
		: StaticEnum<EDemoEnemyRole>()->GetNameStringByValue(static_cast<int64>(Tactics->GetRole())))); // 稳定枚举键，无本地化标签或网络客户端推断。
	if (FrozenBody.Appearance)
	{
		const TObjectPtr<UDemoEnemyAppearance>* Variant=FrozenBody.Appearance->Variants.Find(AppearanceKey); // 借用父资产强引用，选择后由冻结配置继续持有。
		if (Variant)
		{
			FDemoEnemyBodyDefinition Candidate=FrozenBody; Candidate.Appearance=*Variant; // 本次出生候选值；验证失败不激活半配置敌人。
			FString Error; // 候选骨架/碰撞/受击规则错误，必须写日志以便定位漏导资产。
			if (!Candidate.Validate(Error)) { UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_HEAD_REJECT key=%s reason=%s"),*AppearanceKey.ToString(),*Error); return false; }
			FrozenBody.Appearance=*Variant;
		}
		else UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_HEAD_BASE key=%s appearance=%s (base/custom appearance)"),*AppearanceKey.ToString(),*GetNameSafe(FrozenBody.Appearance));
		UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_HEAD_SELECTED key=%s appearance=%s"),*AppearanceKey.ToString(),*GetNameSafe(FrozenBody.Appearance));
	}
	NextProjectileTime = GetWorld()->GetTimeSeconds()+2.f+Tactics->OpeningDelay(); // 首发至少2秒，混编错峰避免同帧齐射。
	// 直接C++调用也防御非法配置；数据表路径在开局阶段已严格验证并拒绝错误。
	AttackSettings = Stats.Attack.IsValid() ? Stats.Attack : FDemoEnemyAttackSettings();
	if (!Stats.Attack.IsValid()) UE_LOG(LogFPSDemo, Warning, TEXT("Invalid direct spawn attack settings; using defaults"));
	CloseCombat->Configure(Stats.CloseCombat,!bBoss&&(Tactics->GetRole()==EDemoEnemyRole::Melee||Tactics->GetRole()==EDemoEnemyRole::Charger||Stats.Roster.Kind==EDemoRosterKind::Guard),Tactics->GetRole()==EDemoEnemyRole::Charger,AttackSettings.MeleeInterval,Tactics->OpeningDelay()); // 统一角色解析结果，禁用战术不会误启用新攻击。
	NextGlobalTime = GetWorld()->GetTimeSeconds() + AttackSettings.GlobalInterval;
	MoveSpeed = Stats.MoveSpeed;
	CoinReward = Stats.CoinReward;
	MonsterLevel = Stats.MonsterLevel;
	AbilitySystem->SetNumericAttributeBase(UDemoAttributeSet::GetMaxHealthAttribute(), Health);
	AbilitySystem->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), Health);
	AbilitySystem->SetNumericAttributeBase(UDemoAttributeSet::GetAttackPowerAttribute(), Stats.AttackPower);
	UE_LOG(LogFPSDemo, Log, TEXT("SPAWN Lv%d boss=%d HP=%.2f ATK=%.2f silver=%d"), MonsterLevel, bBoss, Health, Stats.AttackPower, CoinReward);
	Visual->SetRelativeScale3D(FVector(FrozenBody.FallbackScale));
	Collision->SetSphereRadius(FrozenBody.CollisionRadius);
	// 模型已按厘米制作，禁止继承旧球体的2.4倍Boss比例；失败时保留可见回退并打印错误。
	FDemoEnemyBodyDefinition LocalBody=FrozenBody; LocalBody.HitProfile=FrozenHitProfile; // 骨骼调试/验证与权威伤害使用同一实例规则。
	if (!Presentation->Configure(LocalBody,AnimatedBody,AbilitySystem))
	{ UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_INITIALIZE failed presentation; remains inactive")); return false; }
	HitReact->Configure(AnimatedBody,Presentation); // 真实外观/动画配置后接入，不能冻结构造期默认变换。
	Visual->SetHiddenInGame(true);
	ConfigureWeaponCollisionResponses(); // Configure更换骨骼后再次校正响应；保留Presentation对非法受击资产的NoCollision拒绝。
	// HUD读取Boss标识选择头顶偏移和血条颜色，不在初始化时写展示文字或摄像机旋转。
	// 回退球体和骨骼灯带都使用独立MID；装甲保持灰白，仅灯带延续角色颜色辨识。
	if (UMaterialInstanceDynamic* Material = Visual->CreateAndSetMaterialInstanceDynamic(0)) // 网格拥有独立MID，用颜色辨认角色。
	{
		const FLinearColor RoleColor = Tactics->GetRole()==EDemoEnemyRole::Melee ? FLinearColor(.05f,.9f,.25f)
			: Tactics->GetRole()==EDemoEnemyRole::Charger ? FLinearColor(1.f,.5f,.02f)
			: Tactics->GetRole()==EDemoEnemyRole::Ranged ? FLinearColor(.04f,.35f,.9f)
			: (Tactics->GetRole()==EDemoEnemyRole::Flanker ? FLinearColor(.6f,.08f,.8f) : FLinearColor(.8f,.04f,.12f)); // 近战绿、冲刺橙；原三色不变，骨骼灯带与回退球同步。
		ReplicatedBody.RoleColor=bBoss?FLinearColor(1.f,.15f,.02f):RoleColor;
		Material->SetVectorParameterValue(TEXT("Color"),ReplicatedBody.RoleColor);
		const int32 LightSlot=AnimatedBody->GetMaterialIndex(TEXT("M_Enemy_Amber")); // 按稳定材质槽名查找，不能假设导出顺序。
		if (LightSlot!=INDEX_NONE)
			if (UMaterialInstanceDynamic* Light=AnimatedBody->CreateAndSetMaterialInstanceDynamic(LightSlot)) // 当前敌人强持有的灯带MID。
				Light->SetVectorParameterValue(TEXT("EnemyTint"),bBoss?FLinearColor(1.f,.15f,.02f):RoleColor);
	}
	// 完成整批初始属性写入后才接受后续 Health GE 的声音反馈。
	bHealthConfigured = true;
    Roster->Configure(Stats); // HP/ASC/骨骼准备完毕后授予扩展技能；尚未Activate不执行AI。
	ReplicatedBody.Body=FrozenBody; ReplicatedBody.bReady=true; ReplicatedBody.bBoss=bBossEnemy; ReplicatedBody.Level=MonsterLevel;
	UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_INITIALIZED actor=%s"),*GetName()); return true;
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
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (HasAuthority())
    {
        bKnockbackConsumedFrame=Knockback->Advance(DeltaSeconds); // 只消费原执行器公开返回值，不用UActorComponent::IsActive冒充实际击退状态。
        if (bKnockbackConsumedFrame)
        {
            HitReact->Reset(); // 原近战位移独占本帧，存活枪反应不得再叠加网格位移。
            UE_LOG(LogFPSDemo,VeryVerbose,TEXT("HIT_REACT_SKIPPED enemy=%s reason=knockback"),*GetName());
        }
        else CombatCoordinator->Update(DeltaSeconds); // 原AI/导航执行顺序不变，击退完成帧仍不追回。
        UpdateVisualState(); // 硬直期间继续发布已有受击/阶段表现，客户端仍只消费复制状态。
    }
    else OnRep_VisualState(); // 更新服务器时钟驱动的呼吸/地面预警，不创建攻击计时器。
    AdvanceLocalDeathImpulse(DeltaSeconds); // 仅本地尸体阶段有工作，不改原AI Tick开关或权威Actor位置。
}
void ADemoEnemy::ResolveBossAttack()
{
	DEMO_LOG_CALL();
	// 已撤销/重复回调不能绕过技能生命周期再次结算。
	if (!bAttackPending) { UE_LOG(LogFPSDemo, Log, TEXT("Boss area ignored: not pending")); return; }
	bAttackPending = false;
	// 延迟回调重新取对象与阶段，死亡/胜败/换关后不会结算旧伤害。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = PendingAreaTarget.Get(); // 固定前摇目标身份；下面AOE仍检查所有当前活玩家。
	if (!IsCombatReady()||!IsCombatSessionValid()||!State||!Player)
    { UE_LOG(LogFPSDemo,Log,TEXT("BOSS_AREA_REJECT source/session/state/locked target")); return; }
	DrawDebugSphere(GetWorld(), PendingAttackLocation, 300.f, 24, FColor::Red, false, 0.2f, 0, 6.f);
	for (TActorIterator<ADemoCharacter> It(GetWorld());It;++It) // 圈是共享空间；每名仍存活的参战者独立判断位置。
        if (IsCombatSessionValid()&&DemoEnemyTargeting::IsAliveParticipant(*It)&&FVector::Dist2D(It->GetActorLocation(),PendingAttackLocation)<=300.f)
            DemoEffects::ApplyDamage(AbilitySystem,It->GetAbilitySystemComponent(),GetAttackPower(),
                EDemoPlayerDamageKind::Area,PendingAttackLocation); // 锁定前摇中心，不能改用Boss后来位置。
    PendingAreaTarget.Reset();
}
void ADemoEnemy::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	DEMO_LOG_CALL();
	if (HasAuthority()&&bHealthConfigured&&!bDead) Roster->HealthChanged(Data.OldValue,Data.NewValue); // 实际掉血驱动打断/阶段，出生写值不触发。
	// 只在已初始化的真实伤害边沿检查二阶段；死亡不进入，治疗不退出，也不抢断现有2秒前摇。
	if (HasAuthority() && bHealthConfigured && !bDead && Data.NewValue<Data.OldValue && Tactics->UpdateRage(Data.NewValue,Attributes->GetMaxHealth()))
	{
		ReplicatedBody.RoleColor=FLinearColor(1.f,.02f,.4f); ForceNetUpdate(); // 二阶段灯带颜色也发送给所有相关客户端。
		Presentation->Play(TEXT("Rage")); // 正在施法时表现组件排队，不中断既有预警。
		if (UMaterialInstanceDynamic* Material=Cast<UMaterialInstanceDynamic>(Visual->GetMaterial(0))) // 借用本敌人MID，紫红机体标记二阶段。
			Material->SetVectorParameterValue(TEXT("Color"),FLinearColor(1.f,.02f,.4f));
		const int32 LightSlot=AnimatedBody->GetMaterialIndex(TEXT("M_Enemy_Amber")); // 只更新灯带，保持重甲材质可读。
		if (LightSlot!=INDEX_NONE)
			if (UMaterialInstanceDynamic* Light=Cast<UMaterialInstanceDynamic>(AnimatedBody->GetMaterial(LightSlot))) // 配置阶段创建的实例。
				Light->SetVectorParameterValue(TEXT("EnemyTint"),FLinearColor(1.f,.02f,.4f));
	}
	// HUD逐帧只读当前/最大生命，受伤和治疗直接改变进度，不维护第二份显示血量。
	// 仅实际扣血且尚未死亡时播放；治疗/生成初始化/零伤害不触发。致命一击先短闪，再进入无碰撞尸体动画。
	if (HasAuthority() && bHealthConfigured && !bDead && Data.NewValue < Data.OldValue
        && (!Data.GEModData || Data.GEModData->EffectSpec.Def->GetClass()!=UDemoBurnEffect::StaticClass())) // 灼烧周期不重复播放子弹肉体击中声。
	{
		UDemoShotContext* Shot=Data.GEModData?Cast<UDemoShotContext>(Data.GEModData->EffectSpec.GetContext().GetSourceObject()):nullptr; // 同步回调借用；所有在飞弹丸以UPROPERTY持有Context直到本次结算返回。
		const FHitResult* Impact = Data.GEModData ? Data.GEModData->EffectSpec.GetContext().GetHitResult() : nullptr; // 借用真实碰撞，无碰撞的派生伤害不升级为候选直击。
        EDemoEnemyHitRegion HitRegion = EDemoEnemyHitRegion::Body; float IgnoredMultiplier = 0.f;
        const bool bCore = Shot && Impact && FrozenHitProfile && FrozenHitProfile->ResolveBone(Impact->BoneName, HitRegion, IgnoredMultiplier) && HitRegion == EDemoEnemyHitRegion::Core;
        const bool bRedesigned = Shot && Impact ? Shot->UsesRedesignedFeedback() : (!Shot && GetDefault<UDemoCombatFeelSettings>()->UseForWeapon(TEXT("rifle"))); // 发射冻结；无Shot的元素爆炸/派生伤害在新反馈开启时同样不回退旧SC_Sword_Hit或中断ConfirmedImpact短闪。
        const bool bFirstFeedback = bRedesigned ? (!Shot || Shot->TryUpgradeFeedback(this, bCore, Data.NewValue <= 0.f)) : !Shot || Shot->TryMarkFeedback(this);
		if(bFirstFeedback || (!bRedesigned && Data.NewValue <= 0.f)) // 新候选允许身体→弱点→致死升档，旧路由保持首颗/致死规则。
		{
			const bool bMelee = Data.GEModData && Cast<UDemoMeleeComponent>(Data.GEModData->EffectSpec.GetContext().GetSourceObject()); // 只识别正式近战来源，其他伤害保留原声音。
			FVector ShotDirection = Impact ? (Impact->TraceEnd - Impact->TraceStart).GetSafeNormal() : FVector::ZeroVector; // 真实碰撞世界方向；派生伤害无方向不制造推力。
            if (ShotDirection.IsNearlyZero() && Impact) ShotDirection = (-Impact->ImpactNormal).GetSafeNormal();
            if (ShotDirection.ContainsNaN()) ShotDirection = FVector::ZeroVector;
            if (Data.NewValue>0.f && (!Shot || bMelee || bKnockbackConsumedFrame)) ShotDirection = FVector::ZeroVector; // 存活近战/派生伤害零方向撤销各端旧枪弹簧，避让原击退；致死真实Sweep方向仍允许尸体推倒，不再有活体击退叠加。
            const bool bHeavyWeapon = Shot && UDemoCombatFeelSettings::IsHeavyWeapon(Shot->GetWeaponId()); // 本枪身份，切枪不改变本次等级。
			MulticastHitFeedback(Data.NewValue<=0.f, bCore, bFirstFeedback && !bMelee && !bRedesigned, bRedesigned, ShotDirection, bHeavyWeapon); // 候选不叠Actor中心旧肉体声，近战仍沿原接触池。
		}
		else UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_FEEDBACK_DUPLICATE enemy=%s"),*GetName()); // 包体裁掉重复弹丸反馈日志。
	}
	if (!HasAuthority() || bDead || Data.NewValue > 0.f) return;
	bDead = true; bSpawnActive=false; Presentation->SetAlive(false); UpdateVisualState(); ForceNetUpdate();
	if(Data.GEModData) DemoTalentEvents::NotifyDeath(this,Data.GEModData->EffectSpec.GetContext()); // 死亡已闩锁；清状态前冻结Debuff，只通知真正击杀者。
	FindComponentByClass<UDemoAmmoStatus>()->Clear(); // 归零立即撤销DOT/控制/GC，避免延迟销毁期间残留。
	Navigation->Stop(TEXT("Dead")); // 归零当帧清路线，避免延迟销毁期间残留追击。
	SetActorEnableCollision(false);
	CancelPendingAttacks();
	Presentation->Play(TEXT("Death")); // 撤销旧攻击后播死亡；立即退出存活集合，尸体没有碰撞。
	OnDefeated.Broadcast(this); // 刷怪组件去重后再通知奖励系统；测试自有/非战役怪没有经济订阅者。
	// 不在 GAS 委托广播栈内同步销毁 ASC；下一帧后由生命周期清理。
	SetLifeSpan(FMath::Clamp(GetDefault<UDemoHitFxSettings>()->CorpseLifeSeconds,1.65f,3.f)); // OnDefeated已即时移除存活登记；本地尸体展示不延迟奖励/清关，也不依赖Notify销毁ASC。
}
void ADemoEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
	StopLocalDeathImpulse(); // 弱绑定下一帧回调和本地物理必须先于ASC/网格卸载停止。
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
	if (!IsCombatReady() || !IsCombatSessionValid() || bAttackPending || bGlobalAttackPending || AbilitySystem->HasMatchingGameplayTag(DemoBossTags::Casting) || Now < NextProjectileTime
		|| !State || State->Phase != EDemoPhase::Combat || !DemoEnemyTargeting::IsAliveParticipant(Player))
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
	// Actor/State均为当前World借用，全图技能影响本次战斗中每个存活参战者。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	const ADemoCharacter* Player = GetCombatTarget(); // 服务器统一目标选择，不取本地玩家序号。
	const float Now = GetWorld()->GetTimeSeconds(); // 技能间隔按开始时间而非帧数计算。
	if (!IsCombatReady() || !IsCombatSessionValid() || !bBossEnemy || bAttackPending || bGlobalAttackPending || AbilitySystem->HasMatchingGameplayTag(DemoBossTags::Casting)
		|| Now < NextGlobalTime || !State || State->Phase != EDemoPhase::Combat
		|| !DemoEnemyTargeting::IsAliveParticipant(Player))
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
    DEMO_LOG_CALL(); HitReact->Reset(); // 死亡、会话撤销、卸载均立即恢复最新Facing基准。
    CombatCoordinator->Cancel(TEXT("AttacksCancelled")); // 兼容公开入口，所有取消顺序由唯一协调器执行。
}
void ADemoEnemy::OnCombatSessionEnded()
{
    DEMO_LOG_CALL();
    CancelPendingAttacks();
    if (UDemoAmmoStatus* Status = FindComponentByClass<UDemoAmmoStatus>()) Status->Clear(); // Actor管理自身受击效果，退出会话不发伤害或击杀事件。
}
void ADemoEnemy::ResolveGlobalAttack()
{
	DEMO_LOG_CALL();
	if (!bGlobalAttackPending) { UE_LOG(LogFPSDemo, Log, TEXT("Global ignored: no pending cast")); return; }
	bGlobalAttackPending = false; // 先消费本次技能，回血与伤害均不能重入重复结算。
	SetGlobalTelegraph(false);
	// 重新验证阶段/关卡/生命；取消、死亡和离开战斗不算成功躲避，不发回血奖励。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
    if (!IsCombatReady()||!IsCombatSessionValid()||!State||State->LevelNumber!=GlobalAttackLevel)
    { UE_LOG(LogFPSDemo,Log,TEXT("BOSS_GLOBAL_CANCELLED stale source/session")); return; }
    Roster->LegacyFinished(); // 两秒前摇结束后进入输出窗口，不修改原躲避回血规则。
    GetWorld()->SpawnActor<ADemoAttackPulse>(GetActorLocation(),FRotator::ZeroRotator);
    for (TActorIterator<ADemoCharacter> It(GetWorld());It;++It) // 先判断每位存活玩家；一人阵亡不能阻止其他玩家独立躲避/受击。
    {
        ADemoCharacter* Player=*It; // 本次GE同步栈借用，绝不跨关保存此指针。
        if (!IsCombatSessionValid()||!DemoEnemyTargeting::IsAliveParticipant(Player)) continue;
        if (Player->IsDashEvading())
        {
            const bool bHealSubmitted=DemoEffects::Apply(AbilitySystem,Player->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),5.f+GetDefault<UDemoTalentCatalog>()->Evaluate(Player->GetAbilitySystemComponent()).EvadeHeal); // 保留原+5与医疗天赋收益；提交成功才通知已确认的躲避事实。
            if(bHealSubmitted)
                if(UDemoPlayerDamageFeedbackComponent* Feedback=UDemoPlayerDamageFeedbackComponent::Find(Cast<APlayerController>(Player->GetController()))) // 本次Player所属PC组件，不广播给队友。
                    Feedback->NotifyEvaded();
            UE_LOG(LogFPSDemo,Log,TEXT("BOSS_GLOBAL_EVADED player=%s heal=5 submitted=%d"),*Player->GetName(),bHealSubmitted);
            continue;
        }
        const float BeforeHealth=Player->GetDemoAttributes()->GetHealth(); // 减速只允许发生在本次真实扣血且存活后。
        DemoEffects::ApplyDamage(AbilitySystem,Player->GetAbilitySystemComponent(),GetAttackPower(),
            EDemoPlayerDamageKind::Global,GetActorLocation()); // Global显式Omni，无Boss方向箭头；减速仍仅真实扣血后结算。
        if (IsCombatSessionValid()&&Player->GetDemoAttributes()->GetHealth()<BeforeHealth&&Player->GetDemoAttributes()->GetHealth()>0.f)
            DemoEffects::ApplySlow(AbilitySystem,Player->GetAbilitySystemComponent(),AttackSettings.SlowMultiplier,AttackSettings.SlowDuration);
        UE_LOG(LogFPSDemo,Log,TEXT("BOSS_GLOBAL_HIT player=%s health=%.2f->%.2f"),*Player->GetName(),BeforeHealth,Player->GetDemoAttributes()->GetHealth());
    }
}

void ADemoEnemy::HandleGameplayCue(UObject* Self,FGameplayTag Tag,EGameplayCueEvent::Type Event,const FGameplayCueParameters& Parameters)
{
    DEMO_LOG_CALL();
    if(UDemoAmmoStatus* Status=FindComponentByClass<UDemoAmmoStatus>()) Status->HandleCue(Tag,Event,Parameters); // Actor拥有的本地表现处理器。
}

void ADemoEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADemoEnemy,ReplicatedBody); DOREPLIFETIME(ADemoEnemy,ReplicatedVisuals);
}
void ADemoEnemy::OnRep_SpawnPresentation()
{
    DEMO_LOG_CALL();
    if (HasAuthority()||!HasActorBegunPlay()||!ReplicatedBody.bReady) return;
    if (!bHealthConfigured)
    {
    FrozenBody=ReplicatedBody.Body; bBossEnemy=ReplicatedBody.bBoss; MonsterLevel=ReplicatedBody.Level;
    Collision->SetSphereRadius(FrozenBody.CollisionRadius);
    FrozenHitProfile=DuplicateObject<UDemoEnemyHitProfile>(DemoEnemyHitZones::SelectValidProfile(FrozenBody.HitProfile),this); // 本机实例副本用于调试，不承担客户端伤害权限。
    FDemoEnemyBodyDefinition LocalBody=FrozenBody; LocalBody.HitProfile=FrozenHitProfile; // 复制体型和本机规则分别明确所有权。
    if (!Presentation->Configure(LocalBody,AnimatedBody,AbilitySystem)) { SetActorEnableCollision(false); return; }
    HitReact->Configure(AnimatedBody,Presentation); // 客户端也在真实网格就绪后建立变换更新顺序。
    ConfigureWeaponCollisionResponses(); Visual->SetHiddenInGame(true); bHealthConfigured=true;
    }
    const int32 Slot=AnimatedBody->GetMaterialIndex(TEXT("M_Enemy_Amber")); // 与服务器出生灯带槽保持一致。
    if (Slot!=INDEX_NONE) if (UMaterialInstanceDynamic* Material=AnimatedBody->CreateAndSetMaterialInstanceDynamic(Slot)) Material->SetVectorParameterValue(TEXT("EnemyTint"),ReplicatedBody.RoleColor); // 网格持有MID。
    OnRep_VisualState();
}
void ADemoEnemy::PublishFacing(const FVector& Direction)
{
    DEMO_LOG_TICK(); ReplicatedVisuals.Facing=Direction.GetSafeNormal2D(); Presentation->SetFacingIntent(Direction);
}
void ADemoEnemy::UpdateVisualState()
{
    DEMO_LOG_TICK();
    if (!HasAuthority()) return;
    ReplicatedVisuals.bActive=bSpawnActive; ReplicatedVisuals.bDead=bDead; ReplicatedVisuals.bGlobalPending=bGlobalAttackPending;
    ReplicatedVisuals.GlobalStartedAt=GlobalWindupStart; ReplicatedVisuals.bAreaPending=bAttackPending; ReplicatedVisuals.AreaLocation=PendingAttackLocation;
    AreaTelegraph->SetHiddenInGame(!bAttackPending); if (bAttackPending) AreaTelegraph->SetWorldLocation(PendingAttackLocation); // 服务器本机和客户端共用相同世界落点。
}
void ADemoEnemy::OnRep_VisualState()
{
    DEMO_LOG_TICK();
    if (HasAuthority()||!bHealthConfigured) return;
    if (bDead!=ReplicatedVisuals.bDead) { bDead=ReplicatedVisuals.bDead; Presentation->SetAlive(!bDead); if(bDead) HitReact->Reset(); } // 仅边沿调用；死亡快照到达也不能保留旧弹簧。
    bSpawnActive=ReplicatedVisuals.bActive;
    const bool bLocalDying=LocalDeathMode!=EDemoLocalDeathMode::None; // 致死RPC可早于复制bDead；本地表现不能被较旧的活体快照重新启用查询。
    SetActorEnableCollision(LocalDeathMode==EDemoLocalDeathMode::Ragdoll || (bSpawnActive&&!bDead&&!bLocalDying)); // 仅本地合格尸体物理保持开启，其他组件已全NoCollision。
    if(!bLocalDying) Presentation->SetFacingIntent(ReplicatedVisuals.Facing); // 致死边沿已SetAlive(false)，不每帧重复调用日志型生命周期入口。
    TelegraphShell->SetHiddenInGame(!ReplicatedVisuals.bGlobalPending); TelegraphLight->SetVisibility(ReplicatedVisuals.bGlobalPending);
    AreaTelegraph->SetHiddenInGame(!ReplicatedVisuals.bAreaPending); if (ReplicatedVisuals.bAreaPending) AreaTelegraph->SetWorldLocation(ReplicatedVisuals.AreaLocation); // 无碰撞资源预警适用于打包客户端。
    if (ReplicatedVisuals.bGlobalPending)
    {
        const AGameStateBase* State=GetWorld()->GetGameState(); // 同步服务器时钟，只驱动视觉，网络延迟不产生第二份伤害时钟。
        const float Elapsed=(State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds())-ReplicatedVisuals.GlobalStartedAt; // 秒。
        TelegraphLight->SetIntensity(18000.f+10000.f*FMath::Sin(Elapsed*16.f)); TelegraphShell->SetRelativeScale3D(FVector(2.7f+.15f*FMath::Sin(Elapsed*16.f)));
    }
    if (ReplicatedVisuals.bAreaPending) DrawDebugSphere(GetWorld(),ReplicatedVisuals.AreaLocation,300.f,32,FColor::Orange,false,0.f,0,4.f); // 与旧地面预警相同表现，后续可替换为资产。
}

EDemoImpactSurface ADemoEnemy::GetImpactSurface(FName Bone) const
{
    DEMO_LOG_TICK();
    if (bBossEnemy) return EDemoImpactSurface::Armor; // 2026-11-19明确Boss全状态硬表面：骨覆盖不得使Boss出血雾，原金属火花保留。
    const EDemoImpactSurface* Override = BoneImpactSurfaces.Find(Bone); // 当前目标只读配置，真实命中结算前复制结果。
    const EDemoImpactSurface Surface = Override ? *Override : ImpactSurface; // 小怪可按骨覆盖；Boss上方已强制Armor，客户端不参与选择。
    return Surface == EDemoImpactSurface::Flesh ? Surface : EDemoImpactSurface::Armor; // 敌人不能冒充设施结果，非法枚举安全退硬表面。
}

void ADemoEnemy::MulticastHitFeedback_Implementation(bool bFatal, bool bCore, bool bPlaySound, bool bRedesigned, FVector_NetQuantizeNormal ShotDirection, bool bHeavyWeapon)
{
    DEMO_LOG_TICK();
    if (GetNetMode()!=NM_DedicatedServer)
    {
        if (bPlaySound) DemoWeaponAudio::PlayAtLocation(this,HitSound,GetActorLocation(),TEXT("Weapon.Hit")); // 空间肉体声仍保留，近耳确认层由原射手组件独立限流。
        if (bRedesigned) Presentation->FlashConfirmedImpact(bCore, bFatal);
        else Presentation->FlashHit(bCore, bFatal); // 旧表现保留作AB；候选也不能抢Boss红光预警。
        if (!bFatal) HitReact->Kick(ShotDirection,bCore,bHeavyWeapon); // 零方向先恢复旧弹簧，近战不叠加位移；Boss/死亡在组件守卫拒绝。
        else
        {
            HitReact->Reset(); // 致死边沿先归还存活偏移，尸体表现不继承旧弹簧。
            Presentation->SetAlive(false); // 本地fatal回执早于死亡快照时也立即冻结Facing，权威bDead仍走原死亡边沿。
            bLocalFatalReceipt=true; // 唯一生产授权边沿；公共本地入口不能把普通调用当致死。
            ApplyLocalDeathImpulse(ShotDirection,bCore,bHeavyWeapon);
        }
    }
    else if(bFatal) UE_LOG(LogFPSDemo,Log,TEXT("KILL_IMPULSE_SKIPPED enemy=%s reason=dedicated"),*GetName());
    if (HasAuthority()&&!bFatal&&!bBossEnemy)
    {
        const UDemoHitFxSettings* Fx=GetDefault<UDemoHitFxSettings>(); // 仅表现冷却，关开关恢复旧重武器尝试频率。
        const double Now=GetWorld()?GetWorld()->GetTimeSeconds():0.; // 服务器World秒，暂停不推进。
        if (!bHeavyWeapon || !Fx->bHitReact || Now-LastHeavyStaggerAt>=FMath::Clamp(Fx->HeavyStaggerCooldown,0.f,2.f))
        {
            if(bHeavyWeapon) LastHeavyStaggerAt=Now;
            Presentation->Play(TEXT("Hit")); // ASC权威Montage仍沿唯一原路径复制，客户端组件不调用GA。
        }
        else UE_LOG(LogFPSDemo,VeryVerbose,TEXT("HIT_REACT_SKIPPED enemy=%s reason=heavy_stagger_cooldown"),*GetName());
    }
}

void ADemoEnemy::ApplyLocalDeathImpulse(const FVector& ShotDirection, bool bCore, bool bHeavyWeapon)
{
    DEMO_LOG_CALL();
    if (GetNetMode()==NM_DedicatedServer || bBossEnemy)
    { bLocalFatalReceipt=false; UE_LOG(LogFPSDemo,Log,TEXT("KILL_IMPULSE_SKIPPED enemy=%s reason=%s"),*GetName(),bBossEnemy?TEXT("boss"):TEXT("dedicated")); return; }
    if (!bLocalFatalReceipt || LocalDeathMode!=EDemoLocalDeathMode::None || !GetWorld() || !GetDefault<UDemoHitFxSettings>()->bKillImpulse)
    { bLocalFatalReceipt=false; UE_LOG(LogFPSDemo,Log,TEXT("KILL_IMPULSE_SKIPPED enemy=%s reason=notfatal/duplicate/disabled"),*GetName()); return; }
    bLocalFatalReceipt=false; // 先消费致死授权，重复/外部直接调用不能二次创建本地尸体表现。
    if (ShotDirection.ContainsNaN() || ShotDirection.IsNearlyZero())
    { UE_LOG(LogFPSDemo,Log,TEXT("KILL_IMPULSE_SKIPPED enemy=%s reason=nodir"),*GetName()); return; }
    LocalDeathDirection=ShotDirection.GetSafeNormal(); bLocalDeathCore=bCore; bLocalDeathHeavy=bHeavyWeapon;
    LocalDeathMode=EDemoLocalDeathMode::Pending;
    // 本次Multicast早于服务器bDead/禁碰撞/Death Montage；UObject绑定推迟到这些权威处理之后，不捕获GE/HitResult裸引用。
    LocalDeathStartTimer=GetWorld()->GetTimerManager().SetTimerForNextTick(this,&ADemoEnemy::StartLocalDeathImpulse);
}

void ADemoEnemy::StartLocalDeathImpulse()
{
    DEMO_LOG_CALL();
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(LocalDeathStartTimer);
    const UDemoHitFxSettings* Fx=GetDefault<UDemoHitFxSettings>(); // 本地配置，不复制物理状态。
    if(LocalDeathMode!=EDemoLocalDeathMode::Pending) return;
    if(GetNetMode()==NM_DedicatedServer || bBossEnemy || !Fx->bKillImpulse || !AnimatedBody || !AnimatedBody->GetSkeletalMeshAsset())
    { UE_LOG(LogFPSDemo,Log,TEXT("KILL_IMPULSE_SKIPPED enemy=%s reason=dedicated/boss/disabled/mesh"),*GetName()); LocalDeathMode=EDemoLocalDeathMode::None; return; }
    HitReact->Reset(); Presentation->SetAlive(false); // 停活体倾斜/朝向，保持原死亡GA和动画资源不变。
    LocalDeathBase=AnimatedBody->GetRelativeTransform(); LocalDeathElapsed=0.f;
    LocalDeathSpeed=FMath::Clamp(bLocalDeathHeavy?Fx->KillHeavySpeed:bLocalDeathCore?Fx->KillHeadSpeed:Fx->KillBodySpeed,0.f,1500.f);
    // 必须先关闭所有其他Primitive；本地恢复Actor开关时不能复活球根/预警/武器查询。
    TInlineComponentArray<UPrimitiveComponent*> Primitives(this); // 只在死亡启动时借用本Actor已有组件，不创建新的表现UObject。
    for(UPrimitiveComponent* Primitive:Primitives) if(Primitive && Primitive!=AnimatedBody) Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AnimatedBody->SetCollisionResponseToAllChannels(ECR_Ignore);
    AnimatedBody->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);
    AnimatedBody->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
    AnimatedBody->SetCanEverAffectNavigation(false); AnimatedBody->SetGenerateOverlapEvents(false);
    UPhysicsAsset* Asset=AnimatedBody->GetPhysicsAsset(); // 共享只读资产，永不修改BodySetup/约束或保存资源。
    bool bCanSimulate=Asset && Asset->SkeletalBodySetups.Num()>0 && Asset->SkeletalBodySetups.Num()<=64; // 防异常资产产生无界模拟预算。
    if(bCanSimulate && Asset->SkeletalBodySetups.Num()>1 && Asset->ConstraintSetup.Num()<Asset->SkeletalBodySetups.Num()-1) bCanSimulate=false;
    if(bCanSimulate)
        for(const USkeletalBodySetup* Setup:Asset->SkeletalBodySetups)
            if(!Setup || Setup->PhysicsType==PhysType_Kinematic || !AnimatedBody->GetBodyInstance(Setup->BoneName)) { bCanSimulate=false; break; }
    if(!bCanSimulate)
    { BeginLocalDeathFallback(TEXT("query_only_or_invalid_physics_asset")); return; } // 当前PHYS_Breach全部Kinematic/零约束，直接回退，不能让部位凸体散架。
    SetActorEnableCollision(true); AnimatedBody->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    AnimatedBody->SetCollisionObjectType(ECC_PhysicsBody); // 本地尸体不作为Pawn，互相不因WorldDynamic响应堆叠。
    LocalDeathBodyOrigins.Reset(Asset->SkeletalBodySetups.Num()); // 合格模拟才分配一次有限位置数组，Tick不扩容。
    for(const USkeletalBodySetup* Setup:Asset->SkeletalBodySetups)
    {
        FBodyInstance* Body=AnimatedBody->GetBodyInstance(Setup->BoneName); // 只改本实例，PhysicsAsset的QueryOnly/配置保持只读。
        Body->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly); Body->SetObjectType(ECC_PhysicsBody);
        Body->SetResponseToAllChannels(ECR_Ignore); Body->SetResponseToChannel(ECC_WorldStatic,ECR_Block); Body->SetResponseToChannel(ECC_WorldDynamic,ECR_Block);
        LocalDeathBodyOrigins.Add(Body->GetUnrealWorldTransform().GetLocation());
    }
    AnimatedBody->bPauseAnims=true; // 本地暂停动画求值即可，不能取消权威ASC GA再让客户自行激活死亡GA。
    AnimatedBody->SetSimulatePhysics(true); AnimatedBody->SetAllBodiesPhysicsBlendWeight(1.f);
    if(!AnimatedBody->IsSimulatingPhysics()) { BeginLocalDeathFallback(TEXT("simulation_not_started")); return; }
    const FName Bone=bLocalDeathCore && AnimatedBody->GetBodyInstance(TEXT("head"))?FName(TEXT("head")):NAME_None; // 头部真实刚体存在才使用，否则根体。
    FBodyInstance* ImpulseBody=AnimatedBody->GetBodyInstance(Bone); // 同步借用实际施力体，不能把不存在socket当作有效作用点。
    if(!ImpulseBody) { BeginLocalDeathFallback(TEXT("missing_impulse_body")); return; }
    const FVector Direction=(LocalDeathDirection+FVector::UpVector*FMath::Clamp(Fx->KillUpBias,0.f,1.f)).GetSafeNormal(); // 世界方向叠少量上抬，不改变权威Actor位置。
    // UE5.4 AddImpulseAtLocation没有bVelChange；现成等价API明确以速度变化施加且支持指定世界作用点。
    AnimatedBody->AddVelocityChangeImpulseAtLocation(Direction*LocalDeathSpeed,ImpulseBody->GetUnrealWorldTransform().GetLocation(),Bone);
    LocalDeathMode=EDemoLocalDeathMode::Ragdoll; bLocalDeathInitialPhysicsCheck=false;
    // UE5.4 TimerManager在PostPhysics更新。当前回调在物理后开始模拟，下一次回调才已经历第一步，不能由下次PrePhysics Enemy Tick过早消费校验。
    LocalDeathStartTimer=GetWorld()->GetTimerManager().SetTimerForNextTick(this,&ADemoEnemy::ValidateInitialLocalDeathPhysics);
    UE_LOG(LogFPSDemo,Log,TEXT("KILL_IMPULSE enemy=%s mode=ragdoll tier=%s heavy=%d speed=%.0f"),*GetName(),bLocalDeathCore?TEXT("head"):TEXT("body"),bLocalDeathHeavy,LocalDeathSpeed);
}

void ADemoEnemy::BeginLocalDeathFallback(FName Reason)
{
    DEMO_LOG_CALL();
    if(!AnimatedBody) { LocalDeathMode=EDemoLocalDeathMode::None; UE_LOG(LogFPSDemo,Warning,TEXT("KILL_IMPULSE_SKIPPED enemy=%s reason=missing_fallback_mesh"),*GetName()); return; }
    AnimatedBody->SetAllBodiesSimulatePhysics(false); AnimatedBody->SetSimulatePhysics(false); AnimatedBody->SetAllBodiesPhysicsBlendWeight(0.f); AnimatedBody->bPauseAnims=false; // 逐体停止，覆盖PhysType_Simulated；SetSimulatePhysics单独只切Default体。
    AnimatedBody->SetCollisionEnabled(ECollisionEnabled::NoCollision); SetActorEnableCollision(false); // 运动学推倒无需物理阻挡，尸体始终不能命中/挡人/挡相机。
    AnimatedBody->SetRelativeTransform(LocalDeathBase); LocalDeathBodyOrigins.Reset(); LocalDeathElapsed=0.f; bLocalDeathInitialPhysicsCheck=false;
    LocalDeathMode=EDemoLocalDeathMode::Fallback;
    UE_LOG(LogFPSDemo,Log,TEXT("KILL_IMPULSE enemy=%s mode=fallback tier=%s heavy=%d speed=%.0f reason=%s"),*GetName(),bLocalDeathCore?TEXT("head"):TEXT("body"),bLocalDeathHeavy,LocalDeathSpeed,*Reason.ToString());
}

void ADemoEnemy::AdvanceLocalDeathImpulse(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if(LocalDeathMode==EDemoLocalDeathMode::None || LocalDeathMode==EDemoLocalDeathMode::Pending) return;
    if(!AnimatedBody || GetNetMode()==NM_DedicatedServer || bBossEnemy || !GetDefault<UDemoHitFxSettings>()->bKillImpulse)
    { StopLocalDeathImpulse(); UE_LOG(LogFPSDemo,VeryVerbose,TEXT("KILL_IMPULSE_SKIPPED enemy=%s reason=disabled/boss/dedicated/mesh"),*GetName()); return; }
    if(LocalDeathMode==EDemoLocalDeathMode::Holding) return;
    if(!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0.f)
    { UE_LOG(LogFPSDemo,Warning,TEXT("KILL_IMPULSE_SKIPPED enemy=%s reason=invalid_delta"),*GetName()); return; }
    const UDemoHitFxSettings* Fx=GetDefault<UDemoHitFxSettings>(); // 只读配置，低帧按累计时间直接计算最终位置。
    LocalDeathElapsed+=DeltaSeconds;
    if(LocalDeathMode==EDemoLocalDeathMode::Ragdoll)
    {
        const UPhysicsAsset* Asset=AnimatedBody->GetPhysicsAsset(); // 逐体核对实际物理位置/速度，不能只检查未移动的组件根。
        bool bSafe=Asset && Asset->SkeletalBodySetups.Num()==LocalDeathBodyOrigins.Num(); // 运行中资产替换/数量变化立即安全回退。
        for(int32 Index=0;bSafe && Index<LocalDeathBodyOrigins.Num();++Index)
        {
            const USkeletalBodySetup* Setup=Asset->SkeletalBodySetups[Index]; // 本帧只读骨名。
            FBodyInstance* Body=Setup?AnimatedBody->GetBodyInstance(Setup->BoneName):nullptr; // 模拟实例由网格持有。
            if(!Body) { bSafe=false; break; }
            const FTransform Pose=Body->GetUnrealWorldTransform(); // 世界物理姿态，包含旋转/位置有限值核查。
            const FVector Velocity=Body->GetUnrealWorldVelocity(); // 世界cm/s，NaN速度不能进入下一帧。
            const FVector AngularVelocity=Body->GetUnrealWorldAngularVelocityInRadians(); // 世界rad/s，角速度同样必须有限，避免先飞散一帧再发现位置异常。
            bSafe=!Pose.ContainsNaN() && !Velocity.ContainsNaN() && !AngularVelocity.ContainsNaN()
                && (!bLocalDeathInitialPhysicsCheck || FVector::DistSquared(Pose.GetLocation(),LocalDeathBodyOrigins[Index])<=FMath::Square(FMath::Clamp(Fx->KillMaxTravel,50.f,300.f))); // 飞离只核首帧，不能把后续正常650cm/s尸体运动误判成失稳。
        }
        if(!bSafe) BeginLocalDeathFallback(TEXT("simulation_unstable"));
        return;
    }
    const float Duration=FMath::Clamp(Fx->FallbackSeconds,.05f,1.f); // 秒，暂停不推进，长帧只提交限制内的最终偏移。
    const float T=FMath::Clamp(LocalDeathElapsed/Duration,0.f,1.f); // 归一化进度。
    const float Ease=1.f-FMath::Square(1.f-T); // 二次缓出，0.25秒完成而非永久平移速度。
    const float Distance=FMath::Clamp(bLocalDeathHeavy?Fx->FallbackHeavyDistance:Fx->FallbackBodyDistance,0.f,60.f); // cm，轻/重分档。
    const float Degrees=FMath::Clamp(bLocalDeathHeavy?Fx->FallbackHeavyDeg:Fx->FallbackBodyDeg,0.f,25.f); // 度，纯组件倾斜。
    const FTransform Parent=AnimatedBody->GetAttachParent()?AnimatedBody->GetAttachParent()->GetComponentTransform():FTransform::Identity; // 依据实际父变换转局部，不挪权威根。
    FVector Axis=FVector::CrossProduct(FVector::UpVector,LocalDeathDirection).GetSafeNormal(); // 推倒朝来弹后方。
    if(Axis.IsNearlyZero()) Axis=FVector::RightVector;
    const FVector ParentAxis=Parent.InverseTransformVectorNoScale(Axis).GetSafeNormal(); // 保持世界旋转轴，不把比例当角度。
    FTransform Pose=LocalDeathBase; // 每帧从同版实际基准计算，不累计位置/旋转误差。
    Pose.SetLocation(LocalDeathBase.GetLocation()+Parent.InverseTransformVector(LocalDeathDirection*Distance*Ease));
    Pose.SetRotation((FQuat(ParentAxis,FMath::DegreesToRadians(Degrees*Ease))*LocalDeathBase.GetRotation()).GetNormalized());
    AnimatedBody->SetRelativeTransform(Pose);
    if(T>=1.f) LocalDeathMode=EDemoLocalDeathMode::Holding; // 保持最后姿态直到权威寿命回收，不关闭Actor Tick。
}

void ADemoEnemy::ValidateInitialLocalDeathPhysics()
{
    DEMO_LOG_CALL();
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(LocalDeathStartTimer);
    if(LocalDeathMode!=EDemoLocalDeathMode::Ragdoll)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("KILL_IMPULSE_INITIAL_CHECK_SKIPPED enemy=%s reason=mode_changed"),*GetName()); return; }
    bLocalDeathInitialPhysicsCheck=true;
    AdvanceLocalDeathImpulse(0.f); // 复用逐刚体生产校验，不推进动画时间，也不复制一套测试判断。
    bLocalDeathInitialPhysicsCheck=false;
}

void ADemoEnemy::StopLocalDeathImpulse()
{
    DEMO_LOG_CALL();
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(LocalDeathStartTimer);
    bLocalFatalReceipt=false;
    if(AnimatedBody && LocalDeathMode!=EDemoLocalDeathMode::None && LocalDeathMode!=EDemoLocalDeathMode::Pending)
    {
        AnimatedBody->SetAllBodiesSimulatePhysics(false); AnimatedBody->SetSimulatePhysics(false); AnimatedBody->SetAllBodiesPhysicsBlendWeight(0.f); AnimatedBody->bPauseAnims=false; // 兼容未来显式Simulated体，不能只关组件Default体。
        AnimatedBody->SetCollisionEnabled(ECollisionEnabled::NoCollision); AnimatedBody->SetRelativeTransform(LocalDeathBase);
    }
    LocalDeathMode=EDemoLocalDeathMode::None; LocalDeathBodyOrigins.Reset(); LocalDeathDirection=FVector::ZeroVector; LocalDeathElapsed=0.f; bLocalDeathInitialPhysicsCheck=false;
}
