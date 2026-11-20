#pragma once
#include "CoreMinimal.h"
#include "Animation/DemoPlayerSole.h"

class USkeletalMesh;
namespace DemoPlayerSoleAuthoring
{
    /** Mesh为当前实际导入Body；Left/Right输出Shoe槽最低平面4..32个唯一点（Art05为8个），拒绝非foot/ball蒙皮或缺数据。 */
    bool Extract(USkeletalMesh* Mesh, TArray<FDemoPlayerSolePoint>& Left, TArray<FDemoPlayerSolePoint>& Right);
}
