// 职责：玩家/队伍运行值与检查点的同步适配。使用：RunFlow调用；不持World引用，不替流程选择战役或写磁盘。
#pragma once
#include "CoreMinimal.h"
class ADemoCharacter;
class ADemoGameState;
class UDemoRunSave;
/** 同步借用World对象转换值快照，不持有对象、不读写文件、不启动异步任务。 */
namespace DemoCheckpointAssembler
{
    /** RunId为本轮身份，State/Player为就绪运行值，Out包含本人Build；Campaign三字段由RunFlow补齐，缺属性/库存/核心组件拒绝。 */
    bool Capture(const FString& RunId, const ADemoGameState& State, const ADemoCharacter& Player, UDemoRunSave& Out);
    /** Data已经验证，State/Player是当前新Avatar；重建失败返回Error，调用方负责留大厅且不覆盖文件。 */
    bool Apply(const UDemoRunSave& Data, ADemoGameState& State, ADemoCharacter& Player, FString& Error);
}
