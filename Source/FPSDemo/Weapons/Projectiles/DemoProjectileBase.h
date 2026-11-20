// 职责：服务器实体子弹准备/扫掠及冻结弹种的有界网络表现；弹射分段与接触短闪不回写运动/伤害，备战弹与客户端均不施伤。 本人首段使用发射帧最终可见枪口。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Projectiles/DemoProjectileConfig.h"
#include "Weapons/Projectiles/DemoProjectilePresentation.h"
#include "Weapons/Ammo/DemoAmmoVisualStyle.h"
#include "DemoProjectileBase.generated.h"

class UDemoShotContext;
class USphereComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UDemoProjectileMovementComponent;
class ADemoProjectileTracer;
class USceneComponent;

/** 权威实体子弹基类；原生可实例化保证旧武器无新增资产仍可运行，BP子类只负责数据和表现。 */
UCLASS(Blueprintable)
class FPSDEMO_API ADemoProjectileBase : public AActor
{
    GENERATED_BODY()
public:
    /** 创建小球扫掠根、无碰撞外观和定制移动组件；默认保持未激活，绝不在构造时飞行。 */
    ADemoProjectileBase();
    /** Transform为Editor/生成变换；应用视觉配置，但不会激活碰撞、运动或寿命。 */
    virtual void OnConstruction(const FTransform& Transform) override;
    /** EndPlayReason为旅行/取消/销毁来源；撤销表现，不访问下一World。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** World寿命到期的同步回调；与取消共用幂等清理，不伪造命中表现。 */
    virtual void LifeSpanExpired() override;
    /** OutLifetimeProps只复制飞行外观状态；ShotContext/碰撞去重/伤害数据从不发给客户端。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    /** 客户端根运动/Owner到达后仅刷新渲染子层，永不激活客户端Movement。 */
    virtual void OnRep_ReplicatedMovement() override;
    virtual void OnRep_Owner() override;

    // 派生BP的只读运行定义；Prepare再次校验，实例初始化后将运动参数冻结在组件上。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile") FDemoProjectileConfig Config;
    /** Context为同枪所有弹丸共享强引用，PelletIndex为0..63（含双弹），Direction为世界方向；准备成功也保持关闭碰撞。 */
    bool Prepare(UDemoShotContext* Context, int32 PelletIndex, const FVector& Direction);
    /** 仅成本/冷却/挑战已提交后调用；先写Flying再打开碰撞，最后通知BP表现。 */
    bool ActivateProjectile();
    /** 失败撤销、Flow切阶段、旅行共用幂等入口；不退款、不扣血、不生成命中特效。 */
    void CancelProjectile();
    /** 当前World所有玩家弹及独立亮条统一清理；World借用，可空，供Flow非Combat/Shutdown调用。 */
    static void CancelAll(UWorld* World);
    /** World为当前客户端世界；GS阶段复制后清旧弹/亮条，重显先到且匹配的新弹，未来阶段弹隐藏等待，不发送RPC。 */
    static void ReconcilePhase(UWorld* World);
    /** 准备状态查询仅供提交校验，不允许通过BP改变内部状态。 */
    bool IsPrepared() const;
    /** 已激活且发射阶段/来源有效才能执行移动；备战允许飞行，Movement每帧及碰撞后检查，不代表可施伤。 */
    bool CanSimulate() const;
    /** 返回剩余路径厘米；Movement据此截断本帧模拟时间，不能先越程命中再销毁。 */
    float GetRemainingDistance() const;
    /** Position为实际扫掠后球心；累计真实路径与飞行尾迹，碰撞中/帧末调用；不承担枪口火光，不改伤害。 */
    void RecordTravel(const FVector& Position);
    /** Hit为定制Movement唯一阻挡入口；true允许从真实接触点继续穿透，false终止模拟。 */
    bool ResolveImpact(const FHitResult& Hit);
    /** 发射当帧相机完成后刷新尚存的非复制子层；不推进Movement、累计路程或命中。 */
    void RefreshLaunchVisual();
    /** 测试/诊断只读借用共享Context及弹丸序号，不转移所有权。 */
    UDemoShotContext* GetShotContext() const;
    int32 GetPelletIndex() const;
    /** 同身份真实段在复制根运动之前/之后到达均只收紧本地桥接/终态；客户端绝不运行伤害。 */
    void ReceivePresentationSample(const FDemoProjectilePresentationSample& Sample);
#if WITH_EDITOR
    /** Editor专项只读采样：返回本机渲染根/实际累计距离/只读身份；不能触发飞行、伤害或改配置。 */
    FVector GetCosmeticLocationForAudit() const;
    /** Editor只读渲染父层实际world尺寸；用于与真实球根scale/Tracer端点分别检查，不改变发射。 */
    float GetCosmeticScaleForAudit() const;
    float GetActualDistanceForAudit() const;
    const FDemoProjectilePresentationLaunch& GetPresentationLaunchForAudit() const;
#endif
protected:
    /** C++完成Flying状态后触发；只允许表现，不得施伤/扣弹/重新激活子弹。 */
    UFUNCTION(BlueprintImplementableEvent, Category="Projectile|Cosmetic") void OnProjectileLaunched();
    /** Hit为真实运动接触，bEnemy命中敌人；伤害已由C++处理，此事件只能播放表现。 */
    UFUNCTION(BlueprintImplementableEvent, Category="Projectile|Cosmetic") void OnProjectileImpact(const FHitResult& Hit, bool bEnemy);
private:
    /** Hit为已结算首敌的真实接触；沿原规则转向并冻结表现边界，后续伤害仍只等待运动碰撞。 */
    bool TryRicochet(const FHitResult& Hit);
    /** 已实际扫过段之后反查首敌组件表面出口，仅发布穿透短闪；失败不伪造出口、不影响碰撞/伤害。 */
    void UpdatePiercingExit(const FVector& Position);
    /** 编辑器构造和准备共用：小球与Mesh分开设置，BP外观缩放不扩大伤害体积。 */
    void ApplyConfiguration();
    /** Hit为真实非敌人阻挡结果；只生成可空视觉/音效，不做额外射线。 */
    void PlayWallImpact(const FHitResult& Hit);
    /** bForce用于真实接触/结束端点；普通飞行限制每弹25Hz，只有首次广播允许观察者对齐可见枪口，不推迟服务器碰撞。 */
    void PublishNetworkTravel(bool bForce, bool bTerminal = false);
    /** 根据真实球心/路径更新无碰撞子层位置/尺寸；Boundary永久结束FP桥，Sphere/Movement/HP不接受其输出。 */
    FVector UpdateCosmeticPosition(float Distance, bool bBoundary = false);
    /** 成功激活/合法折向后创建纯表现亮条；DedicatedServer跳过，失败不影响Gameplay。 */
    void CreateLocalTracer(const FVector& Origin);
    /** 长飞行实体到达客户端后仅显示配置Mesh/Trail，关闭Movement和碰撞；短命弹由真实段RPC补表现。 */
    UFUNCTION() void OnRep_Flying();
    UPROPERTY(VisibleAnywhere, Category="Projectile") TObjectPtr<USphereComponent> Collision; // Actor拥有的唯一运动碰撞体。
    UPROPERTY(VisibleAnywhere, Category="Projectile") TObjectPtr<USceneComponent> CosmeticRoot; // 不复制的本地渲染子层；只带Visual/配置Trail，不移动Sphere。
    UPROPERTY(VisibleAnywhere, Category="Projectile") TObjectPtr<UStaticMeshComponent> Visual; // 无碰撞外观，只显示Mesh数据。
    UPROPERTY(VisibleAnywhere, Category="Projectile") TObjectPtr<UDemoProjectileMovementComponent> Movement; // Actor拥有的扫掠运动组件。
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Trail; // 激活后生成的拖尾组件，结束时删除，不跨World。
    UPROPERTY(Transient) TObjectPtr<ADemoProjectileTracer> Tracer; // World拥有的无碰撞亮条；正常命中后脱离本弹独立淡出，清场立即撤销。
    UPROPERTY(Transient) FDemoAmmoVisualStyle VisualStyle; // 本弹发射/客户端首次显现时复制的样式值；只从冻结AmmoType取，不读当前装备。
    bool bVisualStyleResolved = false; // 本World实例只解析一次目录，晚到Shooter不能改变在飞弹颜色。
    FVector RicochetPoint = FVector::ZeroVector; // 真实表面弹射接触cm，后续所有样本重发供丢首包补显；不写入存档。
    FVector RicochetDirection = FVector::ForwardVector; // 成功弹射瞬间的新方向，只作短闪朝向。
    bool bEnvironmentImpact = false; // 真实环境/设施接触事实，重发终包不重复显示；不代表真实扣血。
    FVector EnvironmentPoint = FVector::ZeroVector; // 真实环境接触cm，纯表现网络事实。
    FVector EnvironmentNormal = FVector::ForwardVector; // 真实接触单位法线，仅作环境特效朝向。
    TWeakObjectPtr<UPrimitiveComponent> PiercingComponent; // 首敌实际命中组件，服务器借用只读出口查询，不保活敌人。
    FVector PiercingEntry = FVector::ZeroVector; // 首敌真实入射点cm；排除反查误认入射面。
    FVector PiercingDirection = FVector::ForwardVector; // 冻结直线穿透方向，不改变Movement。
    bool bPiercingExit = false; // 每弹只冻结一次实际出口，后续网络段携带用于视觉补显。
    FVector PiercingExitPoint = FVector::ZeroVector; // 已实际越过的首敌表面cm，仅表现。
    UPROPERTY(Transient) TObjectPtr<UDemoShotContext> ShotContext; // 所有弹丸保活同枪上下文，GE Context本身只弱引用它。
    UPROPERTY(Transient) FDemoProjectileLaunchData Launch; // 单弹初始位置/方向/序号，不保存瞄准目标。
    UPROPERTY(ReplicatedUsing=OnRep_Flying) FDemoProjectilePresentationLaunch PresentationLaunch; // InitialOnly只读描述，与RPC共用GUID/Pellet；不含伤害。
    FVector LastCosmeticPosition = FVector::ZeroVector; // 本World最后视觉端点，不能回写实际飞长。
    float CosmeticTravelDistance = 0.f; // 映射后的可见累计路长cm，仅供Tracer长度。
    float CosmeticRenderScale = 1.f; // 同身份本地F0尺寸随真实飞长归1；只作用无碰撞子层，非复制/非物理值。
    float LastNetworkDistance = 0.f; // 上次发送的真实累计路长cm，网络序号独立于Movement步长。
    uint32 NetworkSequence = 0; // 每弹发布序号，首个为1，终态即使零长度也独立发布。
    float ClientPresentationDistance = 0.f; // 真实段确认的累计cm下界，只增；不能因旧复制根重开桥。
    TSet<TWeakObjectPtr<AActor>> HitActors; // 单弹物理去重，不保活目标，与全枪元素集合独立。
    FVector LastTravelLocation = FVector::ZeroVector; // 上次累计路径的球心，世界厘米。
    float TravelledDistance = 0.f; // 累计实际路程cm，只增不减。
    FVector LastNetworkPosition = FVector::ZeroVector; // 已广播末端世界cm，只向客户端发送真实飞过的后方段。
    float LastNetworkTime = 0.f; // 服务器World秒，限制纯表现频率；与伤害/运动时间步完全无关。
    bool bPublishedNetworkTravel = false; // 服务器本弹广播闩锁；首包丢失也不把后续/弹射段误当枪口段，不复制或持久化。
    float FirstHitBaseDamage = 0.f; // 穿透首敌的距离衰减后基数，不含首敌区域倍率。
    int32 AcceptedEnemyCount = 0; // 当前弹有效敌人数；穿透或单次弹射均最多2个。
    bool bRicochetUsed=false; // 单弹折向闩锁；折向后禁止再穿透/弹射。
    bool bPrepared = false; // 只允许成功Prepare一次。
    UPROPERTY(ReplicatedUsing=OnRep_Flying) bool bFlying = false; // 服务器唯一运动资格，客户端复制值只启用外观。
    UPROPERTY(ReplicatedUsing=OnRep_Flying) uint32 LaunchPhaseRevision = 0; // 本弹发射阶段代数；与GS不同步到达时仅延迟显现，不让旧阶段子弹重显。
    bool bResolvingImpact = false; // 同步GE死亡/清关重入保护。
    bool bConsumed = false; // Cancel/命中/寿命共用幂等结束闩锁。
    bool bContextRegistered=false; // 每颗准备弹只向整枪登记一次，Cancel/外部Destroy均精确撤销。
};
