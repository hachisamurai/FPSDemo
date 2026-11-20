// 职责：移动/上身枪锚点映射的可编辑图包装；UncookedOnly模块，包体仅保留FPSDemo运行节点。
#pragma once
#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "Animation/AnimNode_DemoWeaponAnchor.h"
#include "AnimGraphNode_DemoWeaponAnchor.generated.h"

UCLASS()
class FPSDEMOANIMGRAPH_API UAnimGraphNode_DemoWeaponAnchor : public UAnimGraphNode_Base
{
    GENERATED_BODY()
public:
    // 编译至AnimInstance的原生节点与引脚，不持有World引用。
    UPROPERTY(EditAnywhere, Category="Settings") FAnimNode_DemoWeaponAnchor Node;
    /** 图节点标题显示坐标处理用途，TitleType不改变行为。 */
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
};
