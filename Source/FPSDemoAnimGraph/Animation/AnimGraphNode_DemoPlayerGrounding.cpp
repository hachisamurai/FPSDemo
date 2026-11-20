#include "Animation/AnimGraphNode_DemoPlayerGrounding.h"
#include "Debug/DemoLog.h" // UncookedOnly图定义生命周期仍遵守统一调用日志。

FText UAnimGraphNode_DemoPlayerGrounding::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    DEMO_LOG_TICK(); return FText::FromString(TEXT("Player Sole Grounding (Final Pose)"));
}
const FAnimNode_SkeletalControlBase* UAnimGraphNode_DemoPlayerGrounding::GetNode() const
{
    DEMO_LOG_TICK(); return &Node;
}
FText UAnimGraphNode_DemoPlayerGrounding::GetControllerDescription() const
{
    DEMO_LOG_TICK(); return FText::FromString(TEXT("Player Sole Grounding"));
}
