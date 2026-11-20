// 职责：提供离线动画图节点标题；依赖运行节点，不在游戏包体中加载本文件。
#include "Animation/AnimGraphNode_DemoWeaponAnchor.h"
#include "Debug/DemoLog.h"
FText UAnimGraphNode_DemoWeaponAnchor::GetNodeTitle(ENodeTitleType::Type TitleType) const
{ DEMO_LOG_TICK(); return FText::FromString(TEXT("Preserve Locomotion Spine / Align Weapon Anchor")); }
