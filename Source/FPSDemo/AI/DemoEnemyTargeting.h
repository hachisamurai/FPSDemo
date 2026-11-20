#pragma once
#include "CoreMinimal.h"

class ADemoCharacter;

/** 敌人决策和异步命中的共同参战资格；不读取客户端 GameMode 或固定玩家索引。 */
namespace DemoEnemyTargeting
{
    /** Player 仅在同步调用期间借用；只接受服务器当前已登记、存活且拥有者/ASC Avatar 匹配的角色。 */
    bool IsAliveParticipant(const ADemoCharacter* Player);
}
