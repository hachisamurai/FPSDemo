// 职责：五种特殊小怪（护盾/迫击/修复/爆裂/封锁眼）和双Boss的权威决策、阶段与清理；Enemy驱动Update，RosterAbility管理施法生命周期。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "AI/DemoEnemyRosterSettings.h"
#include "AI/DemoRosterHazard.h"
#include "DemoEnemyRoster.generated.h"
class ADemoEnemy;
class ADemoCharacter;
struct FDemoEnemySpawnStats;
struct FGameplayEffectModCallbackData;
/** 只复制读条/盾壳状态，客户端绝不运行技能状态机。 */
USTRUCT()
struct FDemoRosterVisual
{
    GENERATED_BODY()
    UPROPERTY() EDemoRosterKind Kind=EDemoRosterKind::None; // 冻结身份，客户端颜色/形状选择。
    UPROPERTY() bool bCasting=false; // 准备期壳体可见。
    UPROPERTY() bool bShield=false; // 耐久耗尽立即关闭正面盾板。
    UPROPERTY() bool bStagger=false; // 硬直/大招恢复期核心灯提示。
    UPROPERTY() uint8 Phase=1; // 1..3锁存Boss阶段，不随治疗退回。
    UPROPERTY() FVector Facing=FVector::ForwardVector; // 服务器最近面对方向，用于120度护盾和表现一致。
    UPROPERTY() FVector HealTarget=FVector::ZeroVector; // 修复连线端点；只有bCasting的Mender显示。
};
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoEnemyRoster : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无独立战斗Tick；组件Tick仅表现，由Enemy仲裁调用Update。 */
    UDemoEnemyRoster();
    /** Stats为出生快照，初始化GAS盾耐久并授予唯一服务器能力。 */
    void Configure(const FDemoEnemySpawnStats& Stats);
    /** Player为本帧目标，DeltaSeconds为World秒；true表示本帧独占移动/攻击。 */
    bool Update(ADemoCharacter* Player,float DeltaSeconds);
    /** Data为Health负向GE预结算，按来源方向消耗盾；无命中来源的DOT不受正面盾拦截。 */
    void FilterDamage(FGameplayEffectModCallbackData& Data);
    /** Old/New为真实GAS健康边沿；累计打断修复，锁存Boss70/35阶段。 */
    void HealthChanged(float Old,float New);
    /** 会话/死亡/冰冻取消，bClearSupport决定是否同时撤销Boss召唤请求和所有子单位。 */
    void Cancel(bool bClearSupport=true);
    /** GA激活回调；仅仲裁先设置合法动作后允许，失败不留读条。 */
    bool BeginSkill();
    /** GA结束/取消回调；幂等清读条并开始下一轮冷却，不重新授予无敌。 */
    void FinishSkill(bool bCancelled);
    /** 既有俯冲/全图结束时调用；保留1.5秒输出窗口。 */
    void LegacyFinished();
    /** 只读身份/阶段供伤害、测试、Boss技能守卫使用。 */
    EDemoRosterKind GetKind() const;
    uint8 GetPhase() const;
    bool ManagesBoss() const;
    /** 只读运行状态，HUD/回归使用；不允许外部跳过GAS读条或修改治疗次数。 */
    bool IsCasting() const;
    bool IsRecovering() const;
    int32 GetHealCount() const;
    /** HUD读取复制身份/阶段形成名称；返回值独立，不暴露配置可写引用。 */
    FString GetLabel() const;
    /** Actor或组件EndPlay确保取消，不奖励子单位清理。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** DeltaTime/TickType/ThisTickFunction由引擎提供；仅刷新本地表现，不执行战斗决策。 */
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    /** OutLifetimeProps注册完整视觉快照。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    /** Player为当前活目标；选择合法技能并请求GAS激活，不跳过前摇。 */
    bool TrySkill(ADemoCharacter* Player);
    /** 读条结束唯一执行入口；先消费状态，再生成弹幕或治疗。 */
    void ExecuteSkill();
    /** 返回当前ASC持有的实例，未激活/未授予返回空。 */
    class UDemoRosterAbility* Ability() const;
    /** Visual/Damage为冻结危险物；bWarning标识随读条撤销的纯表现，其他弹道保留到命中/会话取消。 */
    ADemoRosterHazard* SpawnHazard(const FDemoHazardVisual& Visual,float Damage,bool bWarning=false);
    /** Seconds为World秒；取消当前读条和近战并提交硬直，延长不缩短现有恢复。 */
    void Stagger(float Seconds);
    /** Boss进入二阶段一次性请求子单位；由主刷怪器执行总并发和特殊兵种上限。 */
    void RequestSupport();
    /** Anchor死亡通知只统计当前Boss拥有装置；双锚均死亡才硬直一次。 */
    void OnSupportDefeated(ADemoEnemy* Enemy);
    /** 服务器刷怪器实际创建子单位后登记；Enemy为同步借用，记录弱引用。 */
public:
    void RegisterSupport(ADemoEnemy* Enemy);
private:
    /** 接收复制身份/读条，重建本地无碰撞几何；重复调用幂等。 */
    UFUNCTION() void OnRep_Visual();
    UPROPERTY(ReplicatedUsing=OnRep_Visual) FDemoRosterVisual Visual; // 客户只读状态。
    UPROPERTY() FDemoEnemyRosterSettings Config; // 本敌人的冻结可调参数。
    UPROPERTY() TObjectPtr<UDemoRosterSalvo> Salvo; // 当前动作账本，所有弹丸另持有引用直到落地。
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Ornament; // 盾板/背包/锚装置，运行期注册，Actor回收。
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> CastShell; // 施法或硬直核心壳。
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> HealBeam; // 纯表现修复连线。
    UPROPERTY() TObjectPtr<class UStaticMesh> CubeMesh; // CDO硬引用，Cook收集。
    UPROPERTY() TObjectPtr<class UStaticMesh> SphereMesh; // 施法壳使用的引擎资源。
    UPROPERTY() TObjectPtr<class UMaterialInterface> GlowMaterial; // 已有发光材质。
    TWeakObjectPtr<ADemoEnemy> HealRecipient; // 施法时锁定；死亡/移动超距/遮挡则失败。
    TArray<TWeakObjectPtr<ADemoRosterHazard>> Hazards; // 只跟踪本次会话危险物，不延长其生命。
    TArray<TWeakObjectPtr<ADemoRosterHazard>> Warnings; // 准备期可提前取消的纯预警。
    TWeakObjectPtr<ADemoRosterHazard> BeaconLane; // 封锁眼本次锁定光束（带伤害）；读条被取消时撤销，正常完成后交由危险物自行结算；World拥有生命周期。
    TArray<TWeakObjectPtr<ADemoEnemy>> Supports; // Boss自有子单位；Spawner拥有实际计数与撤销。
    FGameplayAbilitySpecHandle SkillHandle; // ASC拥有，组件只激活/取消。
    FVector LockedTarget=FVector::ZeroVector; // 当前动作锁定玩家世界位置cm。
    FVector Facing=FVector::ForwardVector; // 正面盾实时朝向，施法期间锁定。
    float Deadline=0.f; // 当前准备结束的World秒。
    float NextSkill=0.f; // 下一次可以准备的World秒。
    float RecoveryUntil=0.f; // 大招/破盾输出窗口到期World秒。
    float ChannelDamage=0.f; // 本次修复读条累计真实掉血。
    float NextAnchorAttack=0.f; // 二阶段锚点电弧下一次允许预警的World秒。
    int32 HealCount=0; // 被治疗次数，生命期最多3；不写存档。
    uint8 Action=0; // 0未准备，1专属技能，2Boss扇射，3落雷，4冲击环。
    int32 BossTurn=0; // Warden大招轮转索引，冷却未就绪不忙等反复激活。
    int32 AnchorsKilled=0; // 只有真实OnDefeated增加；清理不计数。
    bool bSupportRequested=false; // 70%一次性锁存，即使大伤害直达35%也只召唤一次。
    bool bExecuting=false; // 读条消费门禁，GE重入不能重复生成/治疗。
    bool bCancelling=false; // EndAbility/Spawner清理递归保护。
    bool bWasFrozen=false; // 冰冻进入边沿只撤销一次，避免每帧重置CD；不清理Boss子单位。
};
