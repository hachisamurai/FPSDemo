// 职责：将原版Manny接到既有玩法动画层。由restore_ue_mannequin.py传入复制图和九动作；不重烘动作、修改绑定矩阵或覆盖卡通资产。
#include "Animation/DemoPlayerAnimationAuthoring.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/AnimGraphNode_DemoPlayerGrounding.h"
#include "Animation/AnimGraphNode_DemoPlayerAim.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Debug/DemoLog.h"

bool UDemoPlayerAnimationAuthoring::ConfigureMannequin(USkeletalMesh* Body, UAnimBlueprint* Blueprint, const TArray<UAnimSequence*>& Clips)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    UDemoPlayerAppearance* Appearance=LoadObject<UDemoPlayerAppearance>(nullptr,TEXT("/Game/Player/DA_PlayerAppearance.DA_PlayerAppearance")); // 已备份正式配置，仅本同步事务借用。
    if (!Body || !Blueprint || !Appearance || Clips.Num()!=9 || Appearance->MovementClips.Num()!=9)
    { UE_LOG(LogFPSDemo,Error,TEXT("MANNEQUIN_CONFIG_INVALID_INPUT")); return false; }
    const TArray<TObjectPtr<UAnimSequence>> Previous=Appearance->MovementClips; // 改写前序列映射，用于只替换九个移动Evaluator。
    for (UAnimSequence* Clip:Clips)
        if (!Clip || !Body->GetSkeleton()->IsCompatibleForEditor(Clip->GetSkeleton()))
        { UE_LOG(LogFPSDemo,Error,TEXT("MANNEQUIN_CONFIG_INCOMPATIBLE_CLIP %s"),*GetNameSafe(Clip)); return false; }
    Blueprint->Modify(); Blueprint->TargetSkeleton=Body->GetSkeleton(); Blueprint->SetPreviewMesh(Body);
    int32 Replaced=0; // 必须找到全部九个移动节点，拒绝静默保留旧图中的动作。
    for (UEdGraph* Graph:Blueprint->FunctionGraphs)
        for (UEdGraphNode* Node:Graph->Nodes)
        {
            if (UAnimGraphNode_SequenceEvaluator* Player=Cast<UAnimGraphNode_SequenceEvaluator>(Node))
            {
                const int32 Index=Previous.IndexOfByKey(Player->Node.GetSequence()); // 上身动态Sequence不属于该列表。
                if (Index!=INDEX_NONE) { Player->Modify(); Player->Node.SetSequence(Clips[Index]); ++Replaced; }
            }
            if (UAnimGraphNode_DemoPlayerGrounding* Ground=Cast<UAnimGraphNode_DemoPlayerGrounding>(Node))
            {
                // 旧鞋底采样来自卡通八边鞋，绝不能用于原版网格；空样本使该节点明确停用，原版动作保持原足迹。
                Ground->Modify(); Ground->Node.LeftSole.Reset(); Ground->Node.RightSole.Reset();
            }
        }
    if (Replaced!=9) { UE_LOG(LogFPSDemo,Error,TEXT("MANNEQUIN_CONFIG_MOVEMENT_COUNT actual=%d expected=9"),Replaced); return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status==BS_Error || !Blueprint->GeneratedClass)
    { UE_LOG(LogFPSDemo,Error,TEXT("MANNEQUIN_CONFIG_COMPILE_FAILED")); return false; }
    Appearance->Modify(); Appearance->BodyMesh=Body; Appearance->MovementClips.Reset();
    for (UAnimSequence* Clip:Clips) Appearance->MovementClips.Add(Clip);
    Appearance->AnimClass=Blueprint->GeneratedClass; Appearance->HeadMesh=nullptr; Appearance->EyeMesh=nullptr;
    Appearance->LeftSole.Reset(); Appearance->RightSole.Reset();
    Appearance->BodyOffset=FTransform(FRotator(0,-90,0),FVector(0,0,-96)); // 与96cm胶囊半高匹配，移除旧模型额外3cm鞋底补偿。
    Appearance->AuthoredWalkSpeed=150.f; Appearance->AuthoredRunSpeed=500.f; // Epic模板Walk/Run的作者步频基准cm/s。
    Blueprint->MarkPackageDirty(); Appearance->MarkPackageDirty();
    UE_LOG(LogFPSDemo,Display,TEXT("MANNEQUIN_CONFIGURED body=%s graph=%s movement=%d custom_sole_disabled=1"),*Body->GetName(),*Blueprint->GetName(),Replaced);
    return true;
}
