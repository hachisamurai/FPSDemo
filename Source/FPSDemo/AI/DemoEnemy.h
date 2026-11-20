// 职责：敌人权威生命/战斗组合及独立本地命中反应/尸体表现。使用：SpawnStats初始化，真实Health死亡边沿通知GAS并广播击败；弹簧/尸体只改网格，不授予伤害、奖励或Actor位移。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemInterface.h"
#include "Game/DemoCombatConfig.h"
#include "GameplayCueInterface.h"
#include "GameplayAbilitySpec.h"
#include "AI/DemoEnemyDefinition.h"
#include "Engine/NetSerialization.h"
#include "Combat/Feedback/DemoCombatFeelSettings.h"
#include "DemoEnemy.generated.h"

class UAbilitySystemComponent;
class UDemoAttributeSet;
class USphereComponent;
class UStaticMeshComponent;
class USoundBase;
class UPointLightComponent;
struct FOnAttributeChangeData;
class ADemoEnemy;

/** 单机本地尸体表现阶段；非复制、非伤害状态，服务器bDead与击败计数始终独立。 */
enum class EDemoLocalDeathMode : uint8
{
    None, // 未取得本地致死表现授权或已明确清理。
    Pending, // 已消费致死回执，等下一帧权威死亡处理结束。
    Ragdoll, // 合格尸体物理运行，逐体有限值/位移监测。
    Fallback, // 无碰撞网格推倒按累计秒推进。
    Holding // 推倒已完成，等待权威LifeSpan回收。
};

/** 只复制客户端初始化表现所需数据；GAS属性仍由ASC复制，客户端不重算难度或执行AI。 */
USTRUCT()
struct FDemoEnemyReplicatedBody
{
    GENERATED_BODY()
    UPROPERTY() FDemoEnemyBodyDefinition Body; // 服务器出生冻结的外观/体型，同一个资源引用用于两端真实骨骼。
    UPROPERTY() bool bReady=false; // 与Body同一个复制属性提交，避免OnRep次序先看到半份配置。
    UPROPERTY() bool bBoss=false; // HUD身份，不赋予客户端伤害权限。
    UPROPERTY() int32 Level=1; // 头顶等级展示，服务端生产值。
    UPROPERTY() FLinearColor RoleColor=FLinearColor::White; // 出生灯带颜色，只读表现。
};

/** 动态表现状态值；迟加入也可恢复正在前摇的视觉，不复制定时器或伤害执行权限。 */
USTRUCT()
struct FDemoEnemyReplicatedVisuals
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantizeNormal Facing=FVector::ZeroVector; // 世界平面方向，来自服务器仲裁。
    UPROPERTY() bool bDead=false; // 立即禁用客户端尸体碰撞，动画仍由ASC同步。
    UPROPERTY() bool bActive=false; // 握手提交后才允许客户端碰撞查询。
    UPROPERTY() bool bGlobalPending=false; // 红光开关，不让客户端创建伤害计时器。
    UPROPERTY() float GlobalStartedAt=0.f; // 服务器World秒，客户以GameState同步时钟呈现呼吸。
    UPROPERTY() bool bAreaPending=false; // 固定落点前摇显示，同步值不追踪玩家。
    UPROPERTY() FVector_NetQuantize AreaLocation=FVector::ZeroVector; // 厘米世界位置，地面预警中心。
};
// 只在实际GAS归零边沿同步广播；刷怪器订阅登记对象，不让敌人依赖GameMode经济逻辑。
DECLARE_MULTICAST_DELEGATE_OneParam(FDemoEnemyDefeated, ADemoEnemy*);

/** 沿NavMesh路径悬浮追击；导航只负责移动，Boss预警与所有伤害仍由原GAS接口处理。 */
UCLASS()
class FPSDEMO_API ADemoEnemy : public AActor, public IAbilitySystemInterface, public IGameplayCueInterface
{
	GENERATED_BODY()
public:
    FDemoEnemyDefeated OnDefeated; // 死亡Actor由World延迟回收，接收者只能在广播栈内借用指针。
	/** 创建碰撞、外观、ASC、属性；敌人 ASC 随 Actor 销毁。 */
	ADemoEnemy();
	/** Self/Tag/Event/Parameters为GAS分发上下文；转交弹药GC处理器，不在此扣血。 */
	virtual void HandleGameplayCue(UObject* Self,FGameplayTag Tag,EGameplayCueEvent::Type Event,const FGameplayCueParameters& Parameters) override;
	/** 初始化自身 Owner/Avatar 并绑定健康变化。 */
	virtual void BeginPlay() override;
	/** DeltaSeconds为World帧秒；权威端先推进受击位移，未占用本帧才执行Combat AI，客户端只呈现复制位置。 */
	virtual void Tick(float DeltaSeconds) override;
	/** EndPlayReason 区分主动死亡与异常移除，解绑委托并清理攻击计时器。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** OutLifetimeProps注册表现快照；仅服务端执行AI，客户端读取ASC及Actor状态。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** GAS 标准访问接口，借用敌人自有 ASC。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	/** Stats 为刷怪计划已验证的实例快照；只在出生时写入 GAS 并冻结等级与银币。 */
	void Configure(const FDemoEnemySpawnStats& Stats);
	/** Deferred阶段复制Stats并禁用战斗/碰撞；BodyOverride为计划冻结体型，可空时解析DT/BP/原生默认。 */
	bool PrepareSpawn(const FDemoEnemySpawnStats& Stats,const FDemoEnemyBodyDefinition* BodyOverride=nullptr);
	/** 完成ASC/真实骨骼初始化后为true；刷怪器据此登记，失败不得消费队列。 */
	bool IsSpawnInitialized() const;
	/** 刷怪器绑定死亡/卸载回调后提交激活；失败由调用者销毁回滚，不发布击杀。 */
	bool ActivateSpawn();
	/** 取消计划先禁用战斗/碰撞再撤销技能；不广播死亡或奖励，可安全用于GAS同步回调栈。 */
	void DeactivateSpawn();
	/** 按定义解析出生体型，Spawner与实例初始化共用；Definition为可空DT覆盖，bBoss为冻结身份。 */
	const FDemoEnemyBodyDefinition& ResolveBodyDefinition(const UDemoEnemyDefinition* Definition,bool bBoss) const;
	/** 返回服务器仲裁器选择的存活目标，供既有GAS技能适配多人而无需查Player0。 */
	class ADemoCharacter* GetCombatTarget() const;
	/** 已完成出生且未进入取消/死亡；客户端不得据此自行执行AI。 */
	bool IsCombatReady() const;
	/** 服务端宿主在任何延迟结算前验证出生会话；非战斗/换关立即无效。 */
	bool IsCombatSessionValid() const;
    /** 仅活跃服务器小怪可位移；Boss所有阶段完全免击退，免位移不代表免伤，调用方不能覆盖此规则。 */
    bool CanReceiveKnockback() const;
	/** 命中解析按目标定义读取骨名/倍率；返回共享只读配置，外部不得修改。 */
	const class UDemoEnemyHitProfile* GetHitProfile() const;
    /** Bone来自真实命中，服务器调用；Boss恒Armor，小怪精确骨覆盖优先否则用ImpactSurface；仅表现表面，不改变伤害倍率。 */
    EDemoImpactSurface GetImpactSurface(FName Bone) const;
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback") EDemoImpactSurface ImpactSurface = EDemoImpactSurface::Flesh; // 普通敌人默认表面。2026-11-17用户确认按剧本D03（穿厚工业服的人类）从Armor改Flesh；模型仍是机械外形且Flesh不出金属火花，属已知不一致。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback") EDemoImpactSurface BossImpactSurface = EDemoImpactSurface::Armor; // 历史字段保留兼容已保存资产；2026-11-19用户要求Boss恒Armor，运行时不允许此字段覆盖。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback") TMap<FName, EDemoImpactSurface> BoneImpactSurfaces; // 小怪精确骨名表面覆盖；Boss现恒Armor且忽略此表，纯表现不复制，结果随回执发送。
	/** 返回生成时冻结的单次击杀银币，GM 去重后入账；不再次查表。 */
	int32 GetCoinReward() const;
	/** 返回怪物正等级（含无尽10关以上），供 HUD、掉落审计和测试读取。 */
	int32 GetMonsterLevel() const;
	/** 单次攻击实时读取 GAS AttackPower，后续 GE 增益也能影响实际伤害。 */
	float GetAttackPower() const;
	/** 敌人死亡后立即为 false，销毁延迟不影响清场计数。 */
	bool IsAlive() const;
	/** Boss 标识供 HUD 和银币结算读取。 */
	bool IsBoss() const;
    /** 仅已收到真实致死Multicast的本地表现可调用；方向为世界子弹单位向量，头部/重武器仅分档，下帧启动避免死亡禁碰撞覆盖。 */
    void ApplyLocalDeathImpulse(const FVector& ShotDirection, bool bCore, bool bHeavyWeapon);
	/** HUD 显示使用的当前/最大生命，防止直接写属性。 */
	float GetHealth() const;
	float GetMaxHealth() const;
	/** 同Tick使用的发射入口；Player为当前活玩家，检查视线/范围/独立冷却，成功生成才返回true。 */
	bool TryFireProjectile(class ADemoCharacter* Player);
	/** Boss全图技能入口；检查Combat/存活/冷却/未施法，开始固定2秒前摇，成功返回true。 */
	bool TryStartGlobalAttack();
	/** 返回当前是否处于2秒全图前摇，供UI与自动化检查使用，不允许外部修改。 */
	bool IsGlobalAttackWindingUp() const;
	/** 死亡、切阶段或EndPlay时调用，撤销两个Boss定时回调与红光，不产生伤害/回血。 */
	void CancelPendingAttacks();
    /** 战斗会话撤销回调；同时取消攻击与弹药持续效果，不产生死亡/奖励。 */
    void OnCombatSessionEnded();
	/** AI/测试共用的GAS俯冲入口；冷却、阶段、冻结、其他施法不满足时拒绝。 */
	bool TryStartDiveAttack();
	/** GA激活前的统一守卫；不消费冷却或修改状态。 */
	bool CanStartDiveAttack() const;
	/** 返回当前实例冻结配置；引用只在敌人生命周期内有效。 */
	const FDemoBossDiveSettings& GetDiveSettings() const;
	/** 获取已授予GA的活动实例，供Tick/测试查询；未授予返回空，不拥有返回指针。 */
	class UDemoBossDiveAbility* GetDiveAbility() const;
	/** GA结束回调，开始独立冷却并保护技能间隔，取消也不能立即重试。 */
	void OnDiveFinished();
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FDemoHitFxCorpseCollision; // Editor合同只访问真实入口/碰撞，不向产品暴露伪造死亡或调试权限。
#endif
	friend class UDemoEnemyRoster; // 扩展执行器共用宿主既有导航/技能时钟，避免第二套移动。
	friend class UDemoEnemyCombatComponent; // 仲裁迁移期仅此协调器访问既有执行器状态；外部不得修改攻击时钟。
	/** 已准备快照在 BeginPlay 后初始化；解析 Mixed/特殊兵种并冻结头部外观供复制，验证失败保持禁用、调用者回滚 Actor。 */
	bool InitializePreparedSpawn();
	/** 复制初始体型/身份后创建本机动画及查询碰撞；不写GAS基础属性、不授予攻击技能。 */
	UFUNCTION() void OnRep_SpawnPresentation();
	/** 接收服务器视觉状态；本地仅红光/前摇/转向，没有伤害时钟。 */
	UFUNCTION() void OnRep_VisualState();
	/** 本帧仲裁结果推送至本机与复制状态，避免表现反向查询AI；Direction为世界方向。 */
	void PublishFacing(const FVector& Direction);
	/** 服务器帧末捕获状态，客户端使用同步时钟更新纯表现。 */
	void UpdateVisualState();
	/** bFatal/bCore控制短闪与服务器Montage；bPlaySound仅首发；ShotDirection是世界子弹单位方向（零=未知），bHeavyWeapon冻结原发射身份，均仅表现。 */
	UFUNCTION(NetMulticast,Unreliable) void MulticastHitFeedback(bool bFatal, bool bCore, bool bPlaySound, bool bRedesigned, FVector_NetQuantizeNormal ShotDirection, bool bHeavyWeapon);
	UPROPERTY(ReplicatedUsing=OnRep_SpawnPresentation) FDemoEnemyReplicatedBody ReplicatedBody; // 原子出生快照；客户端收到前不启用受击。
	UPROPERTY(ReplicatedUsing=OnRep_VisualState) FDemoEnemyReplicatedVisuals ReplicatedVisuals; // 权威视觉快照，没有本地预测伤害。
	TWeakObjectPtr<class UDemoCombatSession> CombatSession; // 绑定本次开战对象，失效永不复活；不延长战局生命。
	bool bSessionBound=false; // 区分旧直接生成无会话夹具和曾绑定但已销毁的生产会话。
	UPROPERTY(VisibleAnywhere, Category="Demo|AI") TObjectPtr<class UDemoEnemyRoster> Roster; // 默认子对象；特殊兵种/双Boss仲裁和清理。
    UPROPERTY() TObjectPtr<class UDemoEnemyDefenseSet> Defense; // Enemy ASC拥有的可复制盾属性集。
	UPROPERTY(VisibleAnywhere, Category="Demo|AI") TObjectPtr<class UDemoEnemyCombatComponent> CombatCoordinator; // Enemy持有，集中AI优先级及取消生命周期。
    UPROPERTY(VisibleAnywhere, Category="Demo|AI") TObjectPtr<class UDemoEnemyKnockbackComponent> Knockback; // Enemy默认子对象，只有权威实例驱动；临时位移不保存，复用Actor位置复制。
    UPROPERTY(VisibleAnywhere, Category="Demo|Hit Feedback") TObjectPtr<class UDemoEnemyHitReactComponent> HitReact; // 本地有限网格弹簧；专服不Tick，死亡/击退/会话结束恢复，不改变Actor权威移动。
    bool bKnockbackConsumedFrame = false; // 最近权威帧是否由原击退消费；冻结后续枪反应方向为零，客户端近战零方向回执同时撤销旧弹簧。
    double LastHeavyStaggerAt = -100.; // 服务器World秒；重武器仅沿原表现Montage路径限频尝试，不授予玩法硬直。
    /** 致死回执的下一帧UObject回调；只改本地死亡网格，先严格碰撞再尝试合格物理资产，不改伤害/奖励。 */
    void StartLocalDeathImpulse();
    /** DeltaSeconds为World秒；推进运动学推倒或逐体持续核有限值、首个物理观察帧核最大位移，不关闭Enemy原AI Tick。 */
    void AdvanceLocalDeathImpulse(float DeltaSeconds);
    /** 模拟启动后的下一次TimerManager（PostPhysics）回调，确保已过一个物理步才开启300cm飞离检查；不依赖PrePhysics Actor Tick猜时序。 */
    void ValidateInitialLocalDeathPhysics();
    /** Reason为稳定诊断键；不适合模拟/失稳时停止本地物理并恢复原动画，转0.25秒网格级推倒。 */
    void BeginLocalDeathFallback(FName Reason);
    /** Deactivate/EndPlay清本地Timer/模拟；恢复启动前网格变换，所有尸体查询保持关闭，不改变死亡权威状态。 */
    void StopLocalDeathImpulse();
    bool bLocalFatalReceipt = false; // 只由本地真实fatal Multicast置位；公共入口消费授权，非致死不能启动模拟/推倒。
    EDemoLocalDeathMode LocalDeathMode = EDemoLocalDeathMode::None; // 本地阶段，不复制；专服永远None。
    FTimerHandle LocalDeathStartTimer; // 本Actor弱UObject绑定的启动/首次物理校验回调共用句柄，卸载/撤销时精确清理。
    FVector LocalDeathDirection = FVector::ZeroVector; // 致死回执冻结的世界飞行单位方向，不读取当前枪/玩家。
    bool bLocalDeathCore = false; // 当前尸体冻结头部档，只表现、不参与再次伤害。
    bool bLocalDeathHeavy = false; // 当前尸体冻结重武器档，优先选择重武器速度/推倒距离。
    float LocalDeathSpeed = 0.f; // 本地冻结速度变化量cm/s，现有查询专用PhysicsAsset会使用运动学回退。
    float LocalDeathElapsed = 0.f; // 尸体本地表现累计World秒；暂停不推进。
    bool bLocalDeathInitialPhysicsCheck = false; // 仅初次PostPhysics校验回调栈内打开300cm飞离门；后续正常冲量位移不误报，有限值始终检查。
    FTransform LocalDeathBase = FTransform::Identity; // 启动前实际网格相对变换，模拟失败/回退与卸载使用。
    TArray<FVector> LocalDeathBodyOrigins; // 仅合格模拟资产启动时分配一次；逐体保存世界cm位置，用于NaN/飞离保护。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Definition") TObjectPtr<UDemoEnemyDefinition> EnemyDefinition; // BP可覆盖的默认定义，DT显式定义优先；未设置使用原生CDO。
	UPROPERTY() FDemoEnemyBodyDefinition FrozenBody; // 实例持有已验证外观/骨名资源，与预生成尺寸一致。
	UPROPERTY() TObjectPtr<class UDemoEnemyHitProfile> FrozenHitProfile; // 出生时深拷贝骨名/倍率到实例，运行中修改共享资产不影响已生成敌人的受击规则；不复制此临时UObject。
	FDemoEnemySpawnStats PreparedStats; // Deferred到BeginPlay之间的值快照；Definition由EnemyDefinition额外保活。
	bool bSpawnPrepared=false; // Prepare只成功一次，禁止初始化回调二次配置。
	bool bSpawnActive=false; // 回调绑定后的提交门；Actor Tick/技能仅在true时执行。
	TWeakObjectPtr<class ADemoCharacter> PendingAreaTarget; // 地面前摇冻结玩家身份，玩家离开时取消，不能伤害新加入者。
	UPROPERTY(VisibleAnywhere, Category="Demo|AI") TObjectPtr<class UDemoCloseCombat> CloseCombat; // Enemy持有纯近身状态组件，无独立Tick。
	FDemoBossDiveSettings DiveSettings; // 单次生成配置；小怪及旧直接生成默认禁用。
	FGameplayAbilitySpecHandle DiveHandle; // 自身ASC拥有的GA句柄，Enemy只用于激活/取消。
	float NextDiveTime=0.f; // World秒，出生InitialDelay，结束后Cooldown（可受二阶段缩短）。
	// Enemy拥有的战术决策层，和Nav路径执行分离；无独立Tick、无行为树资产依赖。
	UPROPERTY(VisibleAnywhere, Category="Demo|AI") TObjectPtr<class UDemoEnemyTactics> Tactics;
	// Enemy强持有平面导航组件；只在Combat且没有前摇时驱动，不需要手动连接行为树。
	UPROPERTY(VisibleAnywhere, Category="Demo|Navigation") TObjectPtr<class UDemoEnemyNavigation> Navigation;
	/** 2秒UObject定时器回调：当前关卡活玩家被全图命中，或凭冲刺窗口躲避并回复5HP。 */
	void ResolveGlobalAttack();
	/** bEnabled控制Boss红光球壳与点光源；暂停/死亡/卸载时必须关闭。 */
	void SetGlobalTelegraph(bool bEnabled);
	// 每只敌人冻结的频率/速度配置；仅权威端使用，本Demo范围为单人。
	FDemoEnemyAttackSettings AttackSettings;
	// 独立飞行物/全图开始冷却截止World秒，前摇不补发被暂停的攻击。
	float NextProjectileTime = 0.f;
	float NextGlobalTime = 0.f;
	// 前摇开始World秒，用于红光脉冲；伤害实际由固定2秒定时器驱动。
	float GlobalWindupStart = 0.f;
	// 全图前摇冻结的关卡编号；复用同一物理竞技场也不能跨关结算。
	int32 GlobalAttackLevel = 0;
	// 独立于原0.9秒范围攻击的前摇，前摇期间禁止移动和其他攻击。
	bool bGlobalAttackPending = false;
	// UObject绑定计时器不保存裸Lambda；死亡、换阶段与EndPlay精确撤销。
	FTimerHandle GlobalAttackTimer;
	// 发光球壳与红色点光源归敌人Actor持有，只用于本地表现，没有伤害碰撞。
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TelegraphShell;
	UPROPERTY() TObjectPtr<UPointLightComponent> TelegraphLight;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> AreaTelegraph; // 打包也可见的地面预警，无碰撞；服务器锁定位置并复制视觉意图。
	// 编辑器生成的三变体命中 Cue，随敌人强引用保活，给 Cooker 提供完整依赖；仅本地表现。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Audio") TObjectPtr<USoundBase> HitSound;
	// Configure 中默认生命100可能降低到60；初始化/重配置不是受伤，不允许触发命中反馈。
	bool bHealthConfigured = false;
    /** Data 是本次生命变化；子弹按ShotContext去重声音/动作，后续致死弹丸可升级短闪，归零仍仅触发一次死亡和银币通知。 */
    void OnHealthChanged(const FOnAttributeChangeData& Data);
    /** 构造后/配置后重新设置查询响应，防旧蓝图CDO覆盖新通道；不启用缺失或非法PhysicsAsset的碰撞。 */
    void ConfigureWeaponCollisionResponses();
	/** Boss 预警结束回调；重新检查阶段/玩家位置，仅伤害仍在圈内的玩家。 */
	void ResolveBossAttack();
    // 球体仅为移动/AI视线判定根，单位厘米；忽略玩家WeaponTrace和PlayerProjectile以露出真实骨骼部位。
	UPROPERTY() TObjectPtr<USphereComponent> Collision;
	// 资产缺失时保留的球体回退，不单独碰撞；正常配置骨骼后隐藏。
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
	// 主骨骼网格归Enemy持有，ASC查找此唯一SkeletalMesh；存活PhysicsAsset仅查询子弹、球根移动；死亡仅合格模拟资产本地PhysicsOnly，现有查询资产走无碰撞推倒。
	UPROPERTY(VisibleAnywhere, Category="Demo|Animation") TObjectPtr<class USkeletalMeshComponent> AnimatedBody;
	// 表现组件只拥有动画请求生命周期，不拥有伤害/掉落或攻击定时器。
	UPROPERTY() TObjectPtr<class UDemoEnemyPresentation> Presentation;
	// 头顶血条改由本地HUD投影绘制，不创建随世界旋转的文字组件；生命真值仍来自下方GAS属性。
	// 敌人 ASC/属性由 Actor 强持有，Minimal 复制；当前 Demo 只支持单人。
	UPROPERTY() TObjectPtr<UAbilitySystemComponent> AbilitySystem;
	UPROPERTY() TObjectPtr<UDemoAttributeSet> Attributes;
	// 一次性死亡保护，不复制；在通知 GM 之前置位，处理同帧多次伤害。
	bool bDead = false;
	// Boss 类型在生成配置时设置，不在战斗中改变。
	bool bBossEnemy = false;
	// 基础移动速度cm/s，来自模板；实际战术移动再乘角色/二阶段与GAS倍率，不乘等级/难度。
	float MoveSpeed = 230.f;
	// 权威实例的冻结掉落；本 Demo 单人，UI 不依赖客户端复制这两个字段。
	int32 CoinReward = 0;
	int32 MonsterLevel = 1;
	// 下一次允许发起攻击的 World 秒数；生成后给予玩家 2 秒缓冲。
	float NextAttackTime = 0.f;
	// Boss 预警锁定的世界位置（厘米），预警期间不追踪玩家，冲刺可躲避。
	FVector PendingAttackLocation = FVector::ZeroVector;
	// Boss 是否等待范围伤害回调；避免重复启动预警。
	bool bAttackPending = false;
	// UObject 定时器随敌人 EndPlay 清理，禁止死亡后继续伤害。
	FTimerHandle BossAttackTimer;
	// 健康委托句柄，卸载时精确解绑。
	FDelegateHandle HealthChangedHandle;
};
