// 职责：腕部后处理节点的编辑器显示；依赖运行节点及项目日志，不包含姿态求解。
#include "Animation/AnimGraphNode_DemoPlayerWrist.h"
#include "Debug/DemoLog.h"
FText UAnimGraphNode_DemoPlayerWrist::GetNodeTitle(ENodeTitleType::Type TitleType) const
{ DEMO_LOG_TICK(); return FText::FromString(TEXT("Player Wrist Deformation (Final Pose)")); }
const FAnimNode_SkeletalControlBase* UAnimGraphNode_DemoPlayerWrist::GetNode() const
{ DEMO_LOG_TICK(); return &Node; }
FText UAnimGraphNode_DemoPlayerWrist::GetControllerDescription() const
{ DEMO_LOG_TICK(); return FText::FromString(TEXT("Player Wrist Deformation")); }
