// 职责：实体弹实际碰撞伤害结算。Enemy保留部位/GAS/回执，设施经IDemoDamageableTarget独立受伤；核心只消费实际直击事实，客户端无伤害权限。
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UDemoShotContext;

namespace DemoProjectileDamage
{
    /** Shot为在飞弹强持有上下文；Hit是实际运动阻挡；BaseDamage尚未乘部位，PelletIndex仅关联日志；bSecondary只标记折向派生（穿透仍直击），禁止重复触发核心；设施不走敌人奖励/控制。 */
    FPSDEMO_API bool Apply(UDemoShotContext* Shot, const FHitResult& Hit, float BaseDamage, int32 PelletIndex, bool bSecondary=false);
}
