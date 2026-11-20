#include "Animation/AnimGraphNode_DemoPlayerAim.h"
#include "Debug/DemoLog.h"
FText UAnimGraphNode_DemoPlayerAim::GetNodeTitle(ENodeTitleType::Type TitleType) const
{ DEMO_LOG_TICK(); return FText::FromString(TEXT("Player Camera Aim (Final Upper Body)")); }
const FAnimNode_SkeletalControlBase* UAnimGraphNode_DemoPlayerAim::GetNode() const
{ DEMO_LOG_TICK(); return &Node; }
FText UAnimGraphNode_DemoPlayerAim::GetControllerDescription() const
{ DEMO_LOG_TICK(); return FText::FromString(TEXT("Player Camera Aim")); }
