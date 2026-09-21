#pragma once
#include "Game/DemoCombatConfig.h"

/** 固定战役与无尽共用的兵种节奏；只改出生快照，不修改怪物模板或具体攻击实现。 */
namespace DemoEncounterRules
{
    constexpr int32 RangedInterval = 5; // 每五关的混编池加入远程，其他关只有近战/冲刺。
    constexpr int32 BossInterval = 10; // 每十关额外生成一只Boss，模板引用来自Level10。
    /** Level为正关号、Index为从0开始的小怪序号（不含Boss）、Seed为当前RunId哈希；Stats为该只出生快照。返回false表示输入错误。 */
    FPSDEMO_API bool AssignMinionRole(int32 Level, int32 Index, uint32 Seed, FDemoEnemySpawnStats& Stats);
}
