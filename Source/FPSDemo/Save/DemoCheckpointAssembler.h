#pragma once
#include "CoreMinimal.h"
class ADemoCharacter;
class ADemoGameState;
class UDemoRunSave;
/** 同步借用World对象转换值快照，不持有对象、不读写文件、不启动异步任务。 */
namespace DemoCheckpointAssembler
{
    /** RunId为本轮身份，State/Player为就绪运行值，Out为调用方拥有的临时快照；缺属性/库存返回false。 */
    bool Capture(const FString& RunId, const ADemoGameState& State, const ADemoCharacter& Player, UDemoRunSave& Out);
    /** Data已经验证，State/Player是当前新Avatar；重建失败返回Error，调用方负责留大厅且不覆盖文件。 */
    bool Apply(const UDemoRunSave& Data, ADemoGameState& State, ADemoCharacter& Player, FString& Error);
}
