// 职责：腕部后处理的可编辑图节点；仅未Cook图依赖，游戏执行FPSDemo的原生骨控制器。
#pragma once
#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimNode_DemoPlayerWrist.h"
#include "AnimGraphNode_DemoPlayerWrist.generated.h"

UCLASS()
class FPSDEMOANIMGRAPH_API UAnimGraphNode_DemoPlayerWrist : public UAnimGraphNode_SkeletalControlBase
{
    GENERATED_BODY()
public:
    // 本图的运行节点常量，可在PostProcess AnimBP详情中配置和检查双腕骨链。
    UPROPERTY(EditAnywhere,Category="Settings") FAnimNode_DemoPlayerWrist Node;
    /** 编辑器请求展示标题时返回功能名称。 */
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    /** 返回当前图拥有的运行控制器，不跨图生命周期保存。 */
    virtual const FAnimNode_SkeletalControlBase* GetNode() const override;
protected:
    /** 提供动画图菜单中的可检索名称。 */
    virtual FText GetControllerDescription() const override;
};
