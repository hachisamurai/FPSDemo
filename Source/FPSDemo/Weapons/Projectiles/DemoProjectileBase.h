#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Projectiles/DemoProjectileConfig.h"
#include "DemoProjectileBase.generated.h"

class UDemoShotContext;
class USphereComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UDemoProjectileMovementComponent;
class ADemoProjectileTracer;

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

    // 派生BP的只读运行定义；Prepare再次校验，实例初始化后将运动参数冻结在组件上。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile") FDemoProjectileConfig Config;
    /** Context为同枪所有弹丸共享强引用，PelletIndex为0..31，Direction为世界方向；FinishSpawning后调用，成功也保持关闭碰撞。 */
    bool Prepare(UDemoShotContext* Context, int32 PelletIndex, const FVector& Direction);
    /** 仅成本/冷却/挑战已提交后调用；先写Flying再打开碰撞，最后通知BP表现。 */
    bool ActivateProjectile();
    /** 失败撤销、Flow切阶段、旅行共用幂等入口；不退款、不扣血、不生成命中特效。 */
    void CancelProjectile();
    /** 当前World所有玩家弹及独立亮条统一清理；World借用，可空，供Flow非Combat/Shutdown调用。 */
    static void CancelAll(UWorld* World);
    /** 准备状态查询仅供提交校验，不允许通过BP改变内部状态。 */
    bool IsPrepared() const;
    /** 已激活且轮次/来源有效才能执行移动；Movement每帧及碰撞后都检查。 */
    bool CanSimulate() const;
    /** 返回剩余路径厘米；Movement据此截断本帧模拟时间，不能先越程命中再销毁。 */
    float GetRemainingDistance() const;
    /** Position为实际扫掠后的球心；累计每段路径，碰撞中及每帧末调用，重复相同位置无副作用。 */
    void RecordTravel(const FVector& Position);
    /** Hit为定制Movement唯一阻挡入口；true允许从真实接触点继续穿透，false终止模拟。 */
    bool ResolveImpact(const FHitResult& Hit);
    /** 测试/诊断只读借用共享Context及弹丸序号，不转移所有权。 */
    UDemoShotContext* GetShotContext() const;
    int32 GetPelletIndex() const;
protected:
    /** C++完成Flying状态后触发；只允许表现，不得施伤/扣弹/重新激活子弹。 */
    UFUNCTION(BlueprintImplementableEvent, Category="Projectile|Cosmetic") void OnProjectileLaunched();
    /** Hit为真实运动接触，bEnemy命中敌人；伤害已由C++处理，此事件只能播放表现。 */
    UFUNCTION(BlueprintImplementableEvent, Category="Projectile|Cosmetic") void OnProjectileImpact(const FHitResult& Hit, bool bEnemy);
private:
    /** 编辑器构造和准备共用：小球与Mesh分开设置，BP外观缩放不扩大伤害体积。 */
    void ApplyConfiguration();
    /** Hit为真实非敌人阻挡结果；只生成可空视觉/音效，不做额外射线。 */
    void PlayWallImpact(const FHitResult& Hit);
    UPROPERTY(VisibleAnywhere, Category="Projectile") TObjectPtr<USphereComponent> Collision; // Actor拥有的唯一运动碰撞体。
    UPROPERTY(VisibleAnywhere, Category="Projectile") TObjectPtr<UStaticMeshComponent> Visual; // 无碰撞外观，只显示Mesh数据。
    UPROPERTY(VisibleAnywhere, Category="Projectile") TObjectPtr<UDemoProjectileMovementComponent> Movement; // Actor拥有的扫掠运动组件。
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Trail; // 激活后生成的拖尾组件，结束时删除，不跨World。
    UPROPERTY(Transient) TObjectPtr<ADemoProjectileTracer> Tracer; // World拥有的无碰撞亮条；正常命中后脱离本弹独立淡出，清场立即撤销。
    UPROPERTY(Transient) TObjectPtr<UDemoShotContext> ShotContext; // 所有弹丸保活同枪上下文，GE Context本身只弱引用它。
    UPROPERTY(Transient) FDemoProjectileLaunchData Launch; // 单弹初始位置/方向/序号，不保存瞄准目标。
    TSet<TWeakObjectPtr<AActor>> HitActors; // 单弹物理去重，不保活目标，与全枪元素集合独立。
    FVector LastTravelLocation = FVector::ZeroVector; // 上次累计路径的球心，世界厘米。
    float TravelledDistance = 0.f; // 累计实际路程cm，只增不减。
    float FirstHitBaseDamage = 0.f; // 穿透首敌的距离衰减后基数，不含首敌区域倍率。
    int32 AcceptedEnemyCount = 0; // 当前弹有效敌人数，穿透最多2个。
    bool bPrepared = false; // 只允许成功Prepare一次。
    bool bFlying = false; // 唯一允许运动的阶段标识。
    bool bResolvingImpact = false; // 同步GE死亡/清关重入保护。
    bool bConsumed = false; // Cancel/命中/寿命共用幂等结束闩锁。
};
