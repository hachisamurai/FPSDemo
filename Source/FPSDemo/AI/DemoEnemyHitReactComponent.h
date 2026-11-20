// 职责：存活小怪的本地组件级受击弹簧。使用：Enemy的既有Multicast驱动Kick，Presentation在转向前后卸载/叠加；不模拟骨骼、不改AI/伤害或权威Actor移动。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/DemoHitReactSpring.h"
#include "DemoEnemyHitReactComponent.generated.h"

/** 复用实际网格的有限表现偏移；Listen与客户各自消费不可靠回执，专服不Tick，不承诺逐帧碰撞完全一致。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoEnemyHitReactComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 创建默认关闭Tick的纯表现组件；仅非专服收到有效Kick才开始更新。 */
    UDemoEnemyHitReactComponent();
    /** Mesh由同Enemy拥有，Presentation配置完成后调用；建立Owner→Spring→Presentation顺序，不创建网格/物理对象。 */
    void Configure(class USkeletalMeshComponent* Mesh, class UDemoEnemyPresentation* Presentation);
    /** 世界来弹方向/已确认头部/重武器；零方向恢复旧偏移并拒绝新冲量，供近战回执避让原击退。 */
    void Kick(const FVector& ShotDirection, bool bCore, bool bHeavyWeapon);
    /** Presentation每帧Facing前移除上次实际叠加量，保留外部最新基准；无偏移时无操作。 */
    void RemoveAppliedOffset();
    /** Presentation每帧Facing后在当前相对变换叠加纯值弹簧；不缓存首次Kick的陈旧朝向。 */
    void ApplyOffset();
    /** 死亡/击退/会话清理/卸载恢复最新基准并停本组件Tick；不关闭Enemy原AI Tick。 */
    void Reset();
    /** DeltaTime为World秒；只推进弹簧，网格写入统一由Presentation在Facing后执行。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    /** 结束前归还变换；不留下跨World对象或定时器。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(Transient) TObjectPtr<class USkeletalMeshComponent> Body; // 同Actor主网格借用；UPROPERTY仅跟踪本次生命周期。
    FDemoHitReactSpring Spring; // 世界轴角/位移纯值，不复制、不存档。
    FVector AppliedLocalOffset = FVector::ZeroVector; // 上次实际写入的父空间位移cm，用于精确卸载。
    FQuat AppliedLocalRotation = FQuat::Identity; // 上次左乘的父空间旋转，用逆乘移除，不覆盖Facing基准。
    double LastKickAt = -100.; // 本World秒，短间隔命中只注入配置比例冲量。
    bool bApplied = false; // 是否有尚未归还网格的叠加量；Reset/Remove幂等。
};
