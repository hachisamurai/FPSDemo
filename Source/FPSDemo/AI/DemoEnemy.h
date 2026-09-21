#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemInterface.h"
#include "Game/DemoCombatConfig.h"
#include "GameplayCueInterface.h"
#include "GameplayAbilitySpec.h"
#include "DemoEnemy.generated.h"

class UAbilitySystemComponent;
class UDemoAttributeSet;
class USphereComponent;
class UStaticMeshComponent;
class USoundBase;
class UPointLightComponent;
struct FOnAttributeChangeData;
class ADemoEnemy;
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
	/** DeltaSeconds 是帧间秒数；仅 Combat 权威端追击并发起攻击。 */
	virtual void Tick(float DeltaSeconds) override;
	/** EndPlayReason 区分主动死亡与异常移除，解绑委托并清理攻击计时器。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** GAS 标准访问接口，借用敌人自有 ASC。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	/** Stats 为刷怪计划已验证的实例快照；只在出生时写入 GAS 并冻结等级与银币。 */
	void Configure(const FDemoEnemySpawnStats& Stats);
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
	// 编辑器生成的三变体命中 Cue，随敌人强引用保活，给 Cooker 提供完整依赖；仅本地表现。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Audio") TObjectPtr<USoundBase> HitSound;
	// Configure 中默认生命100可能降低到60；初始化/重配置不是受伤，不允许触发命中反馈。
	bool bHealthConfigured = false;
	/** Data 是本次生命变化；由归零边沿触发一次死亡和银币通知。 */
	void OnHealthChanged(const FOnAttributeChangeData& Data);
	/** Boss 预警结束回调；重新检查阶段/玩家位置，仅伤害仍在圈内的玩家。 */
	void ResolveBossAttack();
	// 球体仅为移动/AI视线判定根，单位厘米；敌人间阻挡，但忽略玩家WeaponTrace以露出真实部位。
	UPROPERTY() TObjectPtr<USphereComponent> Collision;
	// 资产缺失时保留的球体回退，不单独碰撞；正常配置骨骼后隐藏。
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
	// 主骨骼网格归Enemy持有，ASC查找此唯一SkeletalMesh；PhysicsAsset仅查询玩家子弹，球根继续负责移动。
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
