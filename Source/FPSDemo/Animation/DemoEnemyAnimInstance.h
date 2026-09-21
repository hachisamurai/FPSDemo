#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DemoEnemyAnimInstance.generated.h"

/** 游戏线程收集 AActor 敌人的运动状态；图只读缓存，不从动画工作线程访问 ASC/World。 */
UCLASS(Transient, Blueprintable)
class FPSDEMO_API UDemoEnemyAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** 网格初始化/替换时重置差分原点；使用 OwningActor，不假设敌人是 Pawn。 */
    virtual void NativeInitializeAnimation() override;
    /** DeltaSeconds 为游戏秒；差分计算真实导航速度，传送/死亡/定身时重置移动姿势。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    // cm/s，仅本地表现缓存；不是权威移动或复制字段。
    UPROPERTY(BlueprintReadOnly, Category="Enemy") float Speed=0.f;
    // 使用 12/5 cm/s 滞回阈值，避免静止附近反复切换动画。
    UPROPERTY(BlueprintReadOnly, Category="Enemy") bool bMoving=false;
    // 本机 ASC Frozen 状态只抑制移动，不暂停攻击 Slot。
    UPROPERTY(BlueprintReadOnly, Category="Enemy") bool bMovementFrozen=false;
private:
    // 弱引用随网格拥有者失效，不让动画实例延长敌人生命周期。
    TWeakObjectPtr<class ADemoEnemy> Enemy;
    // 世界厘米，上一帧位置；超过 1500cm 视为传送而非移动速度。
    FVector PreviousLocation=FVector::ZeroVector;
};
