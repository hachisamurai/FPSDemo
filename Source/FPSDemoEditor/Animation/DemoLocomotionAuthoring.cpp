// 职责：生成可编辑二维移动资源与图连接；不烘焙骨骼、不改变玩法数值，资源保存由Python白名单调用者完成。
#include "Animation/DemoLocomotionAuthoring.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimGraphNode_DemoWeaponAnchor.h"
#include "Animation/AnimGraphNode_DemoPlayerAim.h"
#include "AnimGraphNode_BlendSpaceEvaluator.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimationGraphSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "Debug/DemoLog.h"

namespace DemoLocomotionAuthoring
{
    /** Graph拥有节点，X/Y为编辑器布局；只在当前同步建图事务内返回借用指针。 */
    template<class T> T* Node(UEdGraph* Graph,int32 X,int32 Y)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] LocomotionAuthoring::Node"));
        FGraphNodeCreator<T> Creator(*Graph); T* Result=Creator.CreateNode(); Result->NodePosX=X; Result->NodePosY=Y; Creator.Finalize(); return Result;
    }
    /** 连接现有Pin，失败记录具体端点，禁止用缺线图声称成功。 */
    bool Link(UEdGraphNode* From,FName Output,UEdGraphNode* To,FName Input)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] LocomotionAuthoring::Link %s -> %s"),*Output.ToString(),*Input.ToString());
        UEdGraphPin* A=From?From->FindPin(Output):nullptr; UEdGraphPin* B=To?To->FindPin(Input):nullptr; // 同步借用，后续Reconstruct之前使用。
        if (!A || !B || !From->GetSchema()->TryCreateConnection(A,B)) { UE_LOG(LogFPSDemo,Error,TEXT("LOCOMOTION_GRAPH_LINK_FAILED %s.%s -> %s.%s"),*GetNameSafe(From),*Output.ToString(),*GetNameSafe(To),*Input.ToString()); return false; }
        return true;
    }
    /** 将原生缓存变量接到节点输入；重复运行先拆旧输入，不重复叠加逻辑层。 */
    bool Variable(UEdGraph* Graph,FName Name,UEdGraphNode* Target,FName Pin)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] LocomotionAuthoring::Variable %s"),*Name.ToString());
        UEdGraphPin* Destination=Target->FindPin(Pin); if (!Destination) { UE_LOG(LogFPSDemo,Error,TEXT("LOCOMOTION_PIN_MISSING %s"),*Pin.ToString()); return false; }
        if (!Destination->LinkedTo.IsEmpty()) return true;
        UK2Node_VariableGet* Get=Node<UK2Node_VariableGet>(Graph,Target->NodePosX-250,Target->NodePosY+140); // Graph拥有读取节点。
        Get->VariableReference.SetSelfMember(Name); Get->ReconstructNode(); return Link(Get,Name,Target,Pin);
    }
    /** 只找主动画图，接口层和事件图不参与此次改动。 */
    UEdGraph* Graph(UAnimBlueprint* Blueprint)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] LocomotionAuthoring::Graph")); if (Blueprint) for (UEdGraph* G:Blueprint->FunctionGraphs) if (G->GetFName()==TEXT("AnimGraph")) return G; return nullptr; }
    /** 修改完成后编译并检查GeneratedClass，包只标脏不保存。 */
    bool Compile(UAnimBlueprint* Blueprint)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] LocomotionAuthoring::Compile")); FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); FKismetEditorUtilities::CompileBlueprint(Blueprint); if (Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("LOCOMOTION_GRAPH_COMPILE_FAILED %s"),*Blueprint->GetPathName()); return false; } Blueprint->MarkPackageDirty(); return true; }
}

UBlendSpace* UDemoLocomotionAuthoring::BuildSpace(const FString& PackagePath,const TArray<UAnimSequence*>& Clips,const TArray<FVector>& Coordinates)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!PackagePath.StartsWith(TEXT("/Game/Player/LocomotionV2/")) || Clips.IsEmpty() || Clips.Num()!=Coordinates.Num() || !Clips[0])
    { UE_LOG(LogFPSDemo,Error,TEXT("LOCOMOTION_SPACE_INPUT_REJECT")); return nullptr; }
    for (UAnimSequence* Clip:Clips) if (!Clip || Clip->GetSkeleton()!=Clips[0]->GetSkeleton()) { UE_LOG(LogFPSDemo,Error,TEXT("LOCOMOTION_SPACE_SKELETON_REJECT")); return nullptr; }
    const FString Name=FPackageName::GetLongPackageAssetName(PackagePath); // 受限新移动目录叶名。
    UBlendSpace* Space=LoadObject<UBlendSpace>(nullptr,*(PackagePath+TEXT(".")+Name)); // 包拥有，仅同步作者修改。
    if (!Space) { Space=NewObject<UBlendSpace>(CreatePackage(*PackagePath),*Name,RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Space); }
    Space->Modify(); Space->SetSkeleton(Clips[0]->GetSkeleton());
    while (Space->GetNumberOfBlendSamples()>0) Space->DeleteSample(Space->GetNumberOfBlendSamples()-1);
    FStructProperty* Parameters=FindFProperty<FStructProperty>(UBlendSpace::StaticClass(),TEXT("BlendParameters")); // 引擎受保护的可编辑固定数组，通过反射写同一属性。
    if (!Parameters || Parameters->ArrayDim<2) return nullptr;
    for (int32 Axis=0;Axis<2;++Axis)
    {
        FBlendParameter* Parameter=Parameters->ContainerPtrToValuePtr<FBlendParameter>(Space,Axis); // 当前资源轴配置，不持有跨事务指针。
        Parameter->DisplayName=Axis==0?TEXT("Right speed (cm/s)"):TEXT("Forward speed (cm/s)"); Parameter->Min=-650.f; Parameter->Max=650.f; Parameter->GridNum=8;
    }
    for (int32 Index=0;Index<Clips.Num();++Index) if (Space->AddSample(Clips[Index],Coordinates[Index])==INDEX_NONE) { UE_LOG(LogFPSDemo,Error,TEXT("LOCOMOTION_SAMPLE_REJECT index=%d"),Index); return nullptr; }
    Space->ValidateSampleData(); Space->ResampleData(); Space->PostEditChange(); Space->MarkPackageDirty(); return Space;
}

bool UDemoLocomotionAuthoring::InstallGraphs(UAnimBlueprint* Body,UAnimBlueprint* FirstPerson,UBlendSpace* DefaultSpace,UAnimSequence* DefaultFP)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    using namespace DemoLocomotionAuthoring;
    UEdGraph* BodyGraph=Graph(Body); UEdGraph* FPGraph=Graph(FirstPerson); // 调用者提供的明确图，不隐式修改其他角色。
    if (!BodyGraph || !FPGraph || !DefaultSpace || !DefaultFP) return false;
    Body->Modify(); FirstPerson->Modify();
    UAnimGraphNode_BlendListByInt* Legacy=nullptr; UAnimGraphNode_LayeredBoneBlend* Upper=nullptr; UAnimGraphNode_DemoPlayerAim* Aim=nullptr; // 原图已存在的三个唯一逻辑节点。
    UAnimGraphNode_TwoWayBlend* Ground=nullptr;
    for (UEdGraphNode* Item:BodyGraph->Nodes)
    {
        if (auto* Found=Cast<UAnimGraphNode_BlendListByInt>(Item)) Legacy=Found;
        if (auto* Found=Cast<UAnimGraphNode_DemoPlayerAim>(Item)) Aim=Found;
        if (Item->NodeComment==TEXT("DirectionalGroundMovement")) Ground=Cast<UAnimGraphNode_TwoWayBlend>(Item);
        if (auto* Found=Cast<UAnimGraphNode_LayeredBoneBlend>(Item))
            if (!Found->Node.LayerSetup.IsEmpty() && Found->Node.LayerSetup[0].BranchFilters.ContainsByPredicate([](const FBranchFilter& Filter){ return Filter.BoneName==TEXT("spine_01"); })) Upper=Found; // 无捕获同步查找主上身层；不使用运行时回调。
    }
    if (!Legacy || !Upper || !Aim) { UE_LOG(LogFPSDemo,Error,TEXT("LOCOMOTION_BODY_GRAPH_CONTRACT_MISSING")); return false; }
    if (!Ground)
    {
        UEdGraphPin* BasePin=Upper->FindPin(TEXT("BasePose")); UEdGraphPin* UpperPin=Upper->FindPin(TEXT("BlendPoses_0")); // 修改前保留旧输入，插入不重建这些节点。
        if (!BasePin || !UpperPin || BasePin->LinkedTo.Num()!=1 || UpperPin->LinkedTo.Num()!=1) return false;
        UEdGraphNode* PriorBase=BasePin->LinkedTo[0]->GetOwningNode(); const FName PriorBasePin=BasePin->LinkedTo[0]->PinName;
        UEdGraphNode* PriorUpper=UpperPin->LinkedTo[0]->GetOwningNode(); const FName PriorUpperPin=UpperPin->LinkedTo[0]->PinName;
        BasePin->BreakAllPinLinks(); UpperPin->BreakAllPinLinks(); Upper->FindPin(TEXT("BlendWeights_0"))->BreakAllPinLinks();
        Ground=Node<UAnimGraphNode_TwoWayBlend>(BodyGraph,-50,100); Ground->NodeComment=TEXT("DirectionalGroundMovement");
        UAnimGraphNode_BlendSpaceEvaluator* Space=Node<UAnimGraphNode_BlendSpaceEvaluator>(BodyGraph,-450,-200); // 归一化时间由同一连续步相提供，不触发Notify或RootMotion。
        Space->Node.SetBlendSpace(DefaultSpace); Space->Node.bTeleportToNormalizedTime=true;
        for (FOptionalPinFromProperty& Pin:Space->ShowPinForProperties) if (Pin.PropertyName==TEXT("BlendSpace")) Pin.bShowPin=true;
        static_cast<UEdGraphNode*>(Space)->ReconstructNode();
        UAnimGraphNode_DemoWeaponAnchor* Anchor=Node<UAnimGraphNode_DemoWeaponAnchor>(BodyGraph,250,-350); // 动作的独立枪根在局部上身混合之前对齐。
        if (!Link(PriorBase,PriorBasePin,Ground,TEXT("A")) || !Link(Space,TEXT("Pose"),Ground,TEXT("B"))
            || !Link(PriorUpper,PriorUpperPin,Anchor,TEXT("Upper")) || !Link(Anchor,TEXT("Pose"),Upper,TEXT("BlendPoses_0"))
            || !Variable(BodyGraph,TEXT("GroundBlendSpace"),Space,TEXT("BlendSpace")) || !Variable(BodyGraph,TEXT("GroundRight"),Space,TEXT("X"))
            || !Variable(BodyGraph,TEXT("GroundForward"),Space,TEXT("Y")) || !Variable(BodyGraph,TEXT("GaitPhase"),Space,TEXT("NormalizedTime"))
            || !Variable(BodyGraph,TEXT("GroundPoseWeight"),Ground,TEXT("Alpha")) || !Variable(BodyGraph,TEXT("ActionOverlayWeight"),Upper,TEXT("BlendWeights_0"))
            || !Variable(BodyGraph,TEXT("bDirectionalLocomotion"),Anchor,TEXT("bEnabled"))) return false;
    }
    // Pose outputs are single-consumer in the UE graph schema. Cache once and
    // use two reader nodes, otherwise connecting the anchor silently disconnects
    // the main lower-body input even though Blueprint compilation still succeeds.
    UAnimGraphNode_SaveCachedPose* Cache=nullptr; UAnimGraphNode_DemoWeaponAnchor* Anchor=nullptr; // Current graph owns all nodes.
    for (UEdGraphNode* Item:BodyGraph->Nodes)
    {
        if (auto* Found=Cast<UAnimGraphNode_SaveCachedPose>(Item)) if (Found->CacheName==TEXT("DirectionalGroundCache")) Cache=Found;
        if (auto* Found=Cast<UAnimGraphNode_DemoWeaponAnchor>(Item)) Anchor=Found;
    }
    if (!Anchor) return false;
    if (!Cache)
    {
        Ground->FindPin(TEXT("Pose"))->BreakAllPinLinks();
        Upper->FindPin(TEXT("BasePose"))->BreakAllPinLinks(); Anchor->FindPin(TEXT("Base"))->BreakAllPinLinks();
        Cache=Node<UAnimGraphNode_SaveCachedPose>(BodyGraph,200,150); Cache->CacheName=TEXT("DirectionalGroundCache"); Cache->ReconstructNode();
        if (!Link(Ground,TEXT("Pose"),Cache,TEXT("Pose"))) return false;
        for (int32 Reader=0;Reader<2;++Reader)
        {
            UAnimGraphNode_UseCachedPose* Use=Node<UAnimGraphNode_UseCachedPose>(BodyGraph,250,250+Reader*140); // Independent reader, shared evaluated ground pose.
            Use->SaveCachedPoseNode=Cache; Use->ReconstructNode();
            if (!Link(Use,TEXT("Pose"),Reader==0?static_cast<UEdGraphNode*>(Upper):Anchor,Reader==0?TEXT("BasePose"):TEXT("Base"))) return false;
        }
    }
    if (Upper->FindPin(TEXT("BasePose"))->LinkedTo.Num()!=1 || Anchor->FindPin(TEXT("Base"))->LinkedTo.Num()!=1)
    { UE_LOG(LogFPSDemo,Error,TEXT("LOCOMOTION_GROUND_CACHE_LINK_MISSING")); return false; }
    for (FOptionalPinFromProperty& Pin:Aim->ShowPinForProperties) if (Pin.PropertyName==TEXT("bAnchorAlreadyAligned")) Pin.bShowPin=true;
    Aim->ReconstructNode();
    if (!Variable(BodyGraph,TEXT("bDirectionalLocomotion"),Aim,TEXT("bAnchorAlreadyAligned"))) return false;
    bool HasFPMovement=false; UAnimGraphNode_Slot* Slot=nullptr; // 只在主FPAction前插入一次。
    for (UEdGraphNode* Item:FPGraph->Nodes) { if (Item->NodeComment==TEXT("AuthoredFirstPersonMovement")) HasFPMovement=true; if (auto* Found=Cast<UAnimGraphNode_Slot>(Item)) if (Found->Node.SlotName==TEXT("FPAction")) Slot=Found; }
    if (!Slot) return false;
    if (!HasFPMovement)
    {
        UEdGraphPin* Input=Slot->FindPin(TEXT("Source")); if (!Input || Input->LinkedTo.Num()!=1) return false;
        UEdGraphNode* Idle=Input->LinkedTo[0]->GetOwningNode(); const FName IdlePin=Input->LinkedTo[0]->PinName; Input->BreakAllPinLinks(); // 原武器Linked Layer不变。
        UAnimGraphNode_SequenceEvaluator* Current=Node<UAnimGraphNode_SequenceEvaluator>(FPGraph,-900,-500);
        UAnimGraphNode_SequenceEvaluator* Previous=Node<UAnimGraphNode_SequenceEvaluator>(FPGraph,-900,-800);
        for (UAnimGraphNode_SequenceEvaluator* Eval:{Current,Previous}) { Eval->Node.SetSequence(DefaultFP); Eval->Node.SetTeleportToExplicitTime(true); for (FOptionalPinFromProperty& Pin:Eval->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true; static_cast<UEdGraphNode*>(Eval)->ReconstructNode(); }
        UAnimGraphNode_TwoWayBlend* Transition=Node<UAnimGraphNode_TwoWayBlend>(FPGraph,-500,-450); // Walk/Dash/空中片段短过渡。
        UAnimGraphNode_TwoWayBlend* Weight=Node<UAnimGraphNode_TwoWayBlend>(FPGraph,-250,-200); Weight->NodeComment=TEXT("AuthoredFirstPersonMovement");
        if (!Link(Previous,TEXT("Pose"),Transition,TEXT("A")) || !Link(Current,TEXT("Pose"),Transition,TEXT("B")) || !Link(Idle,IdlePin,Weight,TEXT("A"))
            || !Link(Transition,TEXT("Pose"),Weight,TEXT("B")) || !Link(Weight,TEXT("Pose"),Slot,TEXT("Source"))
            || !Variable(FPGraph,TEXT("MovementPose"),Current,TEXT("Sequence")) || !Variable(FPGraph,TEXT("MovementTime"),Current,TEXT("ExplicitTime"))
            || !Variable(FPGraph,TEXT("PreviousMovementPose"),Previous,TEXT("Sequence")) || !Variable(FPGraph,TEXT("PreviousMovementTime"),Previous,TEXT("ExplicitTime"))
            || !Variable(FPGraph,TEXT("MovementBlendAlpha"),Transition,TEXT("Alpha")) || !Variable(FPGraph,TEXT("MovementWeight"),Weight,TEXT("Alpha"))) return false;
    }
    return Compile(Body) && Compile(FirstPerson);
}

bool UDemoLocomotionAuthoring::InstallLyraMotion(UAnimBlueprint* Body,UAnimSequence* DefaultMotion,const TArray<UAnimSequence*>& LegacyClips)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    using namespace DemoLocomotionAuthoring;
    UEdGraph* BodyGraph=Graph(Body); // 调用者持有图，本函数只修改指定对象不隐式保存。
    if (!BodyGraph || !DefaultMotion || DefaultMotion->IsValidAdditive() || LegacyClips.Num()!=9) return false;
    for (UAnimSequence* Clip:LegacyClips) if (!Clip || Clip->IsValidAdditive())
    { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_FULL_POSE_ADDITIVE_REJECT %s"),*GetNameSafe(Clip)); return false; }
    UAnimGraphNode_TwoWayBlend* Ground=nullptr; UAnimGraphNode_BlendListByInt* Legacy=nullptr; UAnimGraphNode_DemoPlayerAim* Aim=nullptr;
    bool Installed=false; // 稳定节点注释确保幂等，不叠加第二层。
    for (UEdGraphNode* Item:BodyGraph->Nodes)
    {
        if (Item->NodeComment==TEXT("DirectionalGroundMovement")) Ground=Cast<UAnimGraphNode_TwoWayBlend>(Item);
        if (Item->NodeComment==TEXT("LyraFullBodyMotion")) Installed=true;
        if (auto* Found=Cast<UAnimGraphNode_BlendListByInt>(Item)) Legacy=Found;
        if (auto* Found=Cast<UAnimGraphNode_DemoPlayerAim>(Item)) Aim=Found;
    }
    if (!Ground || !Legacy || !Aim) { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_GRAPH_CONTRACT_MISSING")); return false; }
    Body->Modify();
    for (int32 Index=0;Index<9;++Index)
    {
        UEdGraphPin* Pin=Legacy->FindPin(FName(*FString::Printf(TEXT("BlendPose_%d"),Index))); // 按状态输入索引替换，不能按重复MM_Run资源猜状态。
        UAnimGraphNode_SequenceEvaluator* Eval=Pin && Pin->LinkedTo.Num()==1?Cast<UAnimGraphNode_SequenceEvaluator>(Pin->LinkedTo[0]->GetOwningNode()):nullptr;
        if (!Eval) return false;
        Eval->Node.SetSequence(LegacyClips[Index]); // 即使兼容支路权重为0，也移除错误增量落地资源。
    }
    if (!Installed)
    {
        UEdGraphPin* Input=Ground->FindPin(TEXT("A")); if (!Input || Input->LinkedTo.Num()!=1) return false;
        UEdGraphNode* Prior=Input->LinkedTo[0]->GetOwningNode(); FName PriorPin=Input->LinkedTo[0]->PinName; Input->BreakAllPinLinks(); // 同步保存旧连线，无异步捕获。
        UAnimGraphNode_SequenceEvaluator* Current=Node<UAnimGraphNode_SequenceEvaluator>(BodyGraph,-900,1900);
        UAnimGraphNode_SequenceEvaluator* Previous=Node<UAnimGraphNode_SequenceEvaluator>(BodyGraph,-900,2200);
        for (UAnimGraphNode_SequenceEvaluator* Eval:{Current,Previous})
        {
            Eval->Node.SetSequence(DefaultMotion); Eval->Node.SetTeleportToExplicitTime(true); Eval->Node.SetShouldLoop(false);
            for (FOptionalPinFromProperty& Pin:Eval->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true;
            static_cast<UEdGraphNode*>(Eval)->ReconstructNode();
        }
        UAnimGraphNode_TwoWayBlend* Transition=Node<UAnimGraphNode_TwoWayBlend>(BodyGraph,-500,1900);
        UAnimGraphNode_TwoWayBlend* Weight=Node<UAnimGraphNode_TwoWayBlend>(BodyGraph,-200,1800); Weight->NodeComment=TEXT("LyraFullBodyMotion");
        if (!Link(Previous,TEXT("Pose"),Transition,TEXT("A")) || !Link(Current,TEXT("Pose"),Transition,TEXT("B"))
            || !Link(Prior,PriorPin,Weight,TEXT("A")) || !Link(Transition,TEXT("Pose"),Weight,TEXT("B")) || !Link(Weight,TEXT("Pose"),Ground,TEXT("A"))
            || !Variable(BodyGraph,TEXT("BodyMotionPose"),Current,TEXT("Sequence")) || !Variable(BodyGraph,TEXT("BodyMotionTime"),Current,TEXT("ExplicitTime"))
            || !Variable(BodyGraph,TEXT("PreviousBodyMotionPose"),Previous,TEXT("Sequence")) || !Variable(BodyGraph,TEXT("PreviousBodyMotionTime"),Previous,TEXT("ExplicitTime"))
            || !Variable(BodyGraph,TEXT("BodyMotionBlendAlpha"),Transition,TEXT("Alpha")) || !Variable(BodyGraph,TEXT("BodyMotionWeight"),Weight,TEXT("Alpha"))) return false;
    }
    Aim->ReconstructNode(); // 新属性引脚由结构定义显示；保留原有瞄准/近战连接。
    if (!Variable(BodyGraph,TEXT("AimBoreInChest"),Aim,TEXT("BoreInChest")) || !Variable(BodyGraph,TEXT("AimTargetDirection"),Aim,TEXT("TargetDirection"))
        || !Variable(BodyGraph,TEXT("bCalibratedBoreAim"),Aim,TEXT("bCalibratedBore")) || !Variable(BodyGraph,TEXT("AimAlignmentWeight"),Aim,TEXT("AlignmentWeight"))) return false;
    return Compile(Body);
}
