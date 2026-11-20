#pragma once
#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimNode_DemoPlayerGrounding.h"
#include "AnimGraphNode_DemoPlayerGrounding.generated.h"

/** UncookedOnly图节点支持编辑器及Editor -game重编薄图；Cook后只保留FPSDemo中的运行骨控制。 */
UCLASS()
class FPSDEMOANIMGRAPH_API UAnimGraphNode_DemoPlayerGrounding : public UAnimGraphNode_SkeletalControlBase
{
    GENERATED_BODY()
public:
    // 图拥有的运行节点，包含真实鞋底常量和游戏线程传入的平面Pin。
    UPROPERTY(EditAnywhere, Category="Settings") FAnimNode_DemoPlayerGrounding Node;
    /** TitleType为编辑器显示上下文；标题无需访问World或加载资产。 */
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    /** 返回当前节点只读控制器地址，生命周期与AnimGraph节点一致。 */
    virtual const FAnimNode_SkeletalControlBase* GetNode() const override;
protected:
    /** 编辑器菜单/提示使用固定功能名，便于设计师定位最终地面适配。 */
    virtual FText GetControllerDescription() const override;
};
