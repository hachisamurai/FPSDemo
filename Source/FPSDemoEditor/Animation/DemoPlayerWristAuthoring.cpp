// 职责：生成可编辑的最终姿态腕部图；保留输入动画全部曲线和姿态，修正节点在主动画/瞄准IK之后执行。
#include "Animation/DemoPlayerWristAuthoring.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "Animation/AnimGraphNode_DemoPlayerWrist.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Debug/DemoLog.h"

namespace DemoWristAuthoring
{
    /** Graph持有新节点；X为只影响编辑器布局的横坐标，Finalize创建引脚。 */
    template<class T> T* Node(UEdGraph* Graph,int32 X)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] WristAuthoring.Node"));
        FGraphNodeCreator<T> Creator(*Graph); T* Result=Creator.CreateNode(); // 同步创建，指针归图持有。
        Result->NodePosX=X; Creator.Finalize(); return Result;
    }
    /** 同图短期节点/引脚借用；引脚缺失或Schema拒绝时记录失败，不使用直接裸连接。 */
    bool Link(UEdGraphNode* A,FName Out,UEdGraphNode* B,FName In)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] WristAuthoring.Link"));
        UEdGraphPin* Output=A->FindPin(Out); UEdGraphPin* Input=B->FindPin(In);
        const bool bSuccess=Output && Input && A->GetSchema()->TryCreateConnection(Output,Input);
        if (!bSuccess) UE_LOG(LogFPSDemo,Error,TEXT("WRIST_GRAPH_LINK_FAILED %s -> %s"),*Out.ToString(),*In.ToString());
        return bSuccess;
    }
}

UAnimBlueprint* UDemoPlayerWristAuthoring::BuildGraph(USkeletalMesh* Mesh,bool bOriginalMannequinTwist)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Mesh || !Mesh->GetSkeleton()) { UE_LOG(LogFPSDemo,Error,TEXT("WRIST_GRAPH_MISSING_MESH")); return nullptr; }
    const bool bModelMatched=Mesh->GetRefSkeleton().FindBoneIndex(TEXT("forearm_roll_01_l"))!=INDEX_NONE; // 独立新骨架不覆盖旧后处理资产。
    const TCHAR* Name=bOriginalMannequinTwist?TEXT("ABP_FourView01_TwistPostProcess"):bModelMatched?TEXT("ABP_HumanRig01_RollPostProcess"):TEXT("ABP_Player_WristPostProcess");
    const FString Path=FString(bOriginalMannequinTwist?TEXT("/Game/Player/FourView01/"):bModelMatched?TEXT("/Game/Player/RigRebuild/HumanRig01/"):TEXT("/Game/Player/Animations/"))+Name;
    UAnimBlueprint* BP=LoadObject<UAnimBlueprint>(nullptr,*(Path+TEXT(".")+Name)); // 允许重跑固定生成图。
    if (!BP)
    {
        UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>(); // 同步工厂，不持久保存。
        Factory->ParentClass=UAnimInstance::StaticClass(); Factory->TargetSkeleton=Mesh->GetSkeleton();
        BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*Path),Name,RF_Public|RF_Standalone,nullptr,GWarn));
        if (BP) FAssetRegistryModule::AssetCreated(BP);
    }
    if (!BP || BP->TargetSkeleton!=Mesh->GetSkeleton()) { UE_LOG(LogFPSDemo,Error,TEXT("WRIST_GRAPH_SKELETON_MISMATCH")); return nullptr; }
    BP->Modify(); BP->SetPreviewMesh(Mesh);
    UEdGraph* Graph=nullptr; // BP拥有AnimGraph，仅本次构图借用。
    for (UEdGraph* Item:BP->FunctionGraphs) if (Item->GetFName()==TEXT("AnimGraph")) Graph=Item;
    if (!Graph) { UE_LOG(LogFPSDemo,Error,TEXT("WRIST_GRAPH_MISSING_ANIMGRAPH")); return nullptr; }
    const TArray<UEdGraphNode*> Previous=Graph->Nodes; // 删除副本列表以免迭代失效。
    for (UEdGraphNode* Item:Previous) Graph->RemoveNode(Item);
    using namespace DemoWristAuthoring;
    UAnimGraphNode_LinkedInputPose* Input=Node<UAnimGraphNode_LinkedInputPose>(Graph,0);
    UAnimGraphNode_LocalToComponentSpace* ToComponent=Node<UAnimGraphNode_LocalToComponentSpace>(Graph,240);
    UAnimGraphNode_DemoPlayerWrist* Wrist=Node<UAnimGraphNode_DemoPlayerWrist>(Graph,480);
    Wrist->Node.bUseOriginalMannequinTwist=bOriginalMannequinTwist;
    if (bModelMatched || bOriginalMannequinTwist) Wrist->Node.Chains.Reset(); // 新图不保存旧腕桥引用，分配链在LOD初始化时构建。
    UAnimGraphNode_ComponentToLocalSpace* ToLocal=Node<UAnimGraphNode_ComponentToLocalSpace>(Graph,800);
    UAnimGraphNode_Root* Root=Node<UAnimGraphNode_Root>(Graph,1040);
    if (!Link(Input,TEXT("Pose"),ToComponent,TEXT("LocalPose")) || !Link(ToComponent,TEXT("ComponentPose"),Wrist,TEXT("ComponentPose"))
        || !Link(Wrist,TEXT("Pose"),ToLocal,TEXT("ComponentPose")) || !Link(ToLocal,TEXT("Pose"),Root,TEXT("Result"))) return nullptr;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error || !BP->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("WRIST_GRAPH_COMPILE_FAILED")); return nullptr; }
    BP->MarkPackageDirty(); UE_LOG(LogFPSDemo,Display,TEXT("WRIST_GRAPH_BUILT %s"),*Path); return BP;
}

bool UDemoPlayerWristAuthoring::Assign(USkeletalMesh* Mesh,UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Mesh || !Blueprint || !Blueprint->GeneratedClass || Blueprint->TargetSkeleton!=Mesh->GetSkeleton())
    { UE_LOG(LogFPSDemo,Error,TEXT("WRIST_ASSIGN_INVALID_MESH_GRAPH")); return false; }
    if (Mesh->GetPostProcessAnimBlueprint() && Mesh->GetPostProcessAnimBlueprint()!=Blueprint->GeneratedClass)
    { UE_LOG(LogFPSDemo,Error,TEXT("WRIST_ASSIGN_OTHER_POSTPROCESS")); return false; }
    Mesh->Modify(); Mesh->SetPostProcessAnimBlueprint(Blueprint->GeneratedClass.Get()); Mesh->MarkPackageDirty();
    UE_LOG(LogFPSDemo,Display,TEXT("WRIST_POSTPROCESS_ASSIGNED %s"),*Mesh->GetPathName()); return true;
}

bool UDemoPlayerWristAuthoring::ValidateModelMatched(USkeletalMesh* Mesh,UAnimSequence* Sequence)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Mesh || !Sequence || Mesh->GetSkeleton()!=Sequence->GetSkeleton() || !Mesh->GetPostProcessAnimBlueprint()
        || Mesh->GetRefSkeleton().FindBoneIndex(TEXT("forearm_roll_01_l"))==INDEX_NONE)
    { UE_LOG(LogFPSDemo,Error,TEXT("HUMAN_ROLL_TEST_INVALID_INPUT")); return false; }
    const UWorld::InitializationValues Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false); // CreateWorld会初始化一次，不能重复InitializeNewWorld。
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings); // 仅本同步诊断拥有，不加入运行游戏/存档子系统。
    if (!World) return false;
    USkeletalMeshComponent* Component=NewObject<USkeletalMeshComponent>(World); // World销毁时释放，测试结束先注销组件。
    Component->SetSkeletalMesh(Mesh); Component->RegisterComponentWithWorld(World);
    Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Component->SetAnimation(Sequence); Component->Stop();
    double MaximumPosition=0.,MaximumRotation=0.,MaximumMainPosition=0.; // 与Blender烘焙基准比较，单位cm/度。
    bool bEvaluated=false; // 防止后处理实例未初始化仍得到看似零差异。
    const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
    for (int32 Frame=0;Frame<=360;++Frame)
    {
        Component->SetDisablePostProcessBlueprint(true);
        Component->SetPosition(Frame/60.f,false); Component->TickAnimation(0.f,false); Component->RefreshBoneTransforms();
        const TArray<FTransform> Baked=Component->GetComponentSpaceTransforms(); // 独立值副本，不跨Tick借用组件缓冲。
        Component->SetDisablePostProcessBlueprint(false);
        Component->SetPosition(Frame/60.f,false); Component->TickAnimation(0.f,false); Component->RefreshBoneTransforms();
        bEvaluated|=Component->GetPostProcessInstance()!=nullptr;
        const TArray<FTransform>& Final=Component->GetComponentSpaceTransforms();
        if (Baked.Num()!=Final.Num()) { MaximumPosition=1.e6; break; }
        for (int32 Index=0;Index<Final.Num();++Index)
        {
            const double Position=FVector::Dist(Baked[Index].GetLocation(),Final[Index].GetLocation());
            if (Ref.GetBoneName(Index).ToString().StartsWith(TEXT("forearm_roll_")))
            {
                MaximumPosition=FMath::Max(MaximumPosition,Position);
                MaximumRotation=FMath::Max(MaximumRotation,FMath::RadiansToDegrees(Baked[Index].GetRotation().AngularDistance(Final[Index].GetRotation())));
            }
            else MaximumMainPosition=FMath::Max(MaximumMainPosition,Position);
        }
    }
    Component->UnregisterComponent(); World->DestroyWorld(false);
    const bool bPassed=bEvaluated && MaximumPosition<.01 && MaximumRotation<.2 && MaximumMainPosition<.001;
    UE_LOG(LogFPSDemo,Display,TEXT("HUMAN_ROLL_TEST_RESULT pass=%d instance=%d frames=361 rollPositionCm=%.7f rollRotationDeg=%.7f mainPositionCm=%.7f"),
        bPassed,bEvaluated,MaximumPosition,MaximumRotation,MaximumMainPosition);
    return bPassed;
}
