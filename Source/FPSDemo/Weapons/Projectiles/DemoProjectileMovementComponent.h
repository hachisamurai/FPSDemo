#pragma once

#include "CoreMinimal.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "DemoProjectileMovementComponent.generated.h"

/** 保留引擎扫掠/子步，只定制唯一命中决策；穿透不调用会清空Velocity/UpdatedComponent的默认HandleImpact。 */
UCLASS()
class FPSDEMO_API UDemoProjectileMovementComponent : public UProjectileMovementComponent
{
    GENERATED_BODY()
public:
    /** DeltaTime为World秒；TickType/ThisTickFunction由引擎借用；限制本帧运动至剩余射程并检查上下文。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
    /** Hit为本次真实扫掠，TimeTick秒、MoveDelta厘米；SubTickTimeRemaining保留未处理时间，穿透从接触位置继续而非跳到背后。 */
    virtual EHandleBlockingHitResult HandleBlockingHit(const FHitResult& Hit, float TimeTick, const FVector& MoveDelta, float& SubTickTimeRemaining) override;
};
