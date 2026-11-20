#pragma once
#include "CoreMinimal.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimNode_DemoPlayerAim.h"
#include "AnimGraphNode_DemoPlayerAim.generated.h"

/** 未Cook图节点；打包后只保留FPSDemo原生瞄准骨控制，不引入Editor依赖。 */
UCLASS()
class FPSDEMOANIMGRAPH_API UAnimGraphNode_DemoPlayerAim : public UAnimGraphNode_SkeletalControlBase
{
    GENERATED_BODY()
public:
    // 图编译时复制运行节点常量和引脚绑定；不持有场景角色。
    UPROPERTY(EditAnywhere,Category="Settings") FAnimNode_DemoPlayerAim Node;
    /** TitleType为图展示上下文；返回固定可读功能名。 */
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    /** 返回本图拥有的运行控制器地址，不跨生命周期保存。 */
    virtual const FAnimNode_SkeletalControlBase* GetNode() const override;
protected:
    /** 节点菜单名称供后续手工检查图连接。 */
    virtual FText GetControllerDescription() const override;
};
