#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UDemoShotContext;

namespace DemoProjectileDamage
{
    /** Shot为在飞弹强持有的上下文；Hit必须是本次运动阻挡；BaseDamage尚未乘部位；返回真实骨骼命中是否有效，零HP也可供穿透继续。 */
    FPSDEMO_API bool Apply(UDemoShotContext* Shot, const FHitResult& Hit, float BaseDamage, int32 PelletIndex);
}
