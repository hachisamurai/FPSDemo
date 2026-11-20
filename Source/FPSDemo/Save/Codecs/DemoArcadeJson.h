// 职责：持续娱乐局面的可选JSON扩展。使用：检查点Codec调用；缺省保持旧档哈希，存在时严格验证每项数值。
#pragma once
#include "CoreMinimal.h"
#include "Economy/Gambling/DemoCasinoTypes.h"
#include "Dom/JsonObject.h"
namespace DemoArcadeJson
{
    /** Round已通过领域验证；返回不包含任何未来结果的独立JSON值。 */
    FPSDEMO_API TSharedRef<FJsonObject> Write(const FDemoArcadeRound& Round);
    /** Json为只读候选；严格拒绝未知键、小数截断及无效结构，成功才替换Out。 */
    FPSDEMO_API bool Read(const FJsonObject& Json,FDemoArcadeRound& Out);
}
