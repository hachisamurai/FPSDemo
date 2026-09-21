#include "Animation/DemoEnemyAnimationAuthoring.h"
#include "Animation/DemoEnemyAppearance.h"
#include "Animation/DemoEnemyAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_Slot.h"
#include "AnimationGraphSchema.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_VariableGet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "Debug/DemoLog.h"

namespace DemoEnemyAuthoring
{
    /** Path为脚本专属包名；创建或借用已有同类型资产，保留 UObject 身份以支持重导入。 */
    template<class T> T* Asset(const FString& Path)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] EnemyAuthoring::Asset %s"),*Path);
        const FString Name=FPackageName::GetLongPackageAssetName(Path); // 包叶名同时是对象名。
        T* Result=LoadObject<T>(nullptr,*(Path+TEXT(".")+Name)); // 重运行使用原对象，不产生重复资源。
        if (!Result)
        {
            Result=NewObject<T>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);
            FAssetRegistryModule::AssetCreated(Result);
        }
        Result->Modify(); return Result;
    }
    /** Graph归AnimBP拥有；X/Y仅为编辑器布局，模板节点在构图后统一编译。 */
    template<class T> T* Node(UEdGraph* Graph,int32 X,int32 Y)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] EnemyAuthoring::Node"));
        FGraphNodeCreator<T> Creator(*Graph); // 同步创建器，Finalize分配反射Pin。
        T* Result=Creator.CreateNode(); // Graph拥有，不保存跨编译裸引用。
        Result->NodePosX=X; Result->NodePosY=Y; Creator.Finalize(); return Result;
    }
    /** 两节点和Pin名称由本次图结构提供；任何连接失败必须阻止成功标记。 */
    bool Link(UEdGraphNode* From,FName Output,UEdGraphNode* To,FName Input)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] EnemyAuthoring::Link %s -> %s"),*Output.ToString(),*Input.ToString());
        UEdGraphPin* A=From->FindPin(Output); // 仅构图期间借用，不能跨ReconstructNode。
        UEdGraphPin* B=To->FindPin(Input); // 输入Pin由图Schema检查类型。
        if (!A||!B||!From->GetSchema()->TryCreateConnection(A,B))
        { UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_GRAPH_LINK_FAILED %s %s"),*Output.ToString(),*Input.ToString()); return false; }
        return true;
    }
}
bool UDemoEnemyAnimationAuthoring::Build(const FString& Model,USkeletalMesh* Mesh,const TMap<FName,UAnimSequence*>& Clips)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] EnemyAnimationAuthoring::Build %s"),*Model);
    if ((Model!=TEXT("Chaser")&&Model!=TEXT("Warden"))||!Mesh||!Clips.Contains("Idle")||!Clips.Contains("Move")) return false;
    USkeleton* Skeleton=Mesh->GetSkeleton(); // 共享绑定资产只增加Slot，不改变参考姿势。
    for (const TPair<FName,UAnimSequence*>& Entry:Clips) // 所有动作必须兼容当前独立骨架。
        if (!Entry.Value||Entry.Value->GetSkeleton()!=Skeleton) { UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_CLIP_SKELETON mismatch")); return false; }
    Skeleton->Modify(); Skeleton->RegisterSlotNode(TEXT("FullBody")); Skeleton->SetSlotGroupName(TEXT("FullBody"),TEXT("DefaultGroup")); Skeleton->MarkPackageDirty();
    const FString Directory=TEXT("/Game/Enemies/Breach/Animation/"); // 专属生成目录，正式手改前复制资产。
    const FString Name=TEXT("ABP_")+Model; // 两骨架各自编译，不共享不兼容的图资产。
    UAnimBlueprint* BP=LoadObject<UAnimBlueprint>(nullptr,*(Directory+Name+TEXT(".")+Name)); // 重导入复用原包。
    if (!BP)
    {
        UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>(); // 临时Editor工厂，本函数同步完成建图。
        Factory->ParentClass=UDemoEnemyAnimInstance::StaticClass(); Factory->TargetSkeleton=Skeleton;
        BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*(Directory+Name)),*Name,RF_Public|RF_Standalone,nullptr,GWarn));
        if (!BP) return false;
        FAssetRegistryModule::AssetCreated(BP);
    }
    BP->Modify(); BP->TargetSkeleton=Skeleton; BP->SetPreviewMesh(Mesh);
    UEdGraph* Graph=nullptr; // 借用工厂产生的AnimGraph，原EventGraph无需脚本逻辑。
    for (UEdGraph* Candidate:BP->FunctionGraphs) if (Candidate->GetFName()==TEXT("AnimGraph")) Graph=Candidate;
    if (!Graph) { UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_GRAPH missing AnimGraph")); return false; }
    const TArray<UEdGraphNode*> OldNodes=Graph->Nodes; // 复制列表后删除，避免遍历期间改变数组。
    for (UEdGraphNode* Old:OldNodes) Graph->RemoveNode(Old);
    UAnimGraphNode_Root* Root=DemoEnemyAuthoring::Node<UAnimGraphNode_Root>(Graph,600,0); // 最终骨骼姿势输出。
    UAnimGraphNode_SequencePlayer* Idle=DemoEnemyAuthoring::Node<UAnimGraphNode_SequencePlayer>(Graph,-600,200); // 循环悬浮姿势。
    UAnimGraphNode_SequencePlayer* Move=DemoEnemyAuthoring::Node<UAnimGraphNode_SequencePlayer>(Graph,-600,0); // 循环前倾推进姿势。
    Idle->Node.SetSequence(Clips.FindChecked("Idle")); Idle->Node.SetLoopAnimation(true);
    Move->Node.SetSequence(Clips.FindChecked("Move")); Move->Node.SetLoopAnimation(true);
    UAnimGraphNode_BlendListByBool* Blend=DemoEnemyAuthoring::Node<UAnimGraphNode_BlendListByBool>(Graph,-200,0); // true为移动，false为待机。
    UAnimGraphNode_Slot* Slot=DemoEnemyAuthoring::Node<UAnimGraphNode_Slot>(Graph,250,0); // ASC Montage在此叠加全身动作。
    Slot->Node.SlotName=TEXT("FullBody");
    UK2Node_VariableGet* Moving=DemoEnemyAuthoring::Node<UK2Node_VariableGet>(Graph,-600,-200); // 只读取C++游戏线程缓存。
    Moving->VariableReference.SetSelfMember(GET_MEMBER_NAME_CHECKED(UDemoEnemyAnimInstance,bMoving)); Moving->ReconstructNode();
    if (!DemoEnemyAuthoring::Link(Move,"Pose",Blend,"BlendPose_0")||!DemoEnemyAuthoring::Link(Idle,"Pose",Blend,"BlendPose_1")
        ||!DemoEnemyAuthoring::Link(Moving,"bMoving",Blend,"bActiveValue")||!DemoEnemyAuthoring::Link(Blend,"Pose",Slot,"Source")
        ||!DemoEnemyAuthoring::Link(Slot,"Pose",Root,"Result")) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error||!BP->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_GRAPH compile failed")); return false; }
    BP->MarkPackageDirty();
    UDemoEnemyAppearance* Appearance=DemoEnemyAuthoring::Asset<UDemoEnemyAppearance>(Directory+TEXT("DA_")+Model); // 强引用完整资产闭包。
    Appearance->Mesh=Mesh; Appearance->AnimClass=BP->GeneratedClass; Appearance->Actions.Reset();
    for (const TPair<FName,UAnimSequence*>& Entry:Clips) // Idle/Move由图播放，其余保存可检查的真实Montage资产。
    {
        if (Entry.Key=="Idle"||Entry.Key=="Move") continue;
        UAnimMontage* Montage=DemoEnemyAuthoring::Asset<UAnimMontage>(Directory+TEXT("AM_")+Model+TEXT("_")+Entry.Key.ToString()); // 重运行更新同一对象。
        Montage->SetSkeleton(Skeleton); Montage->SlotAnimTracks.Reset(); Montage->CompositeSections.Reset();
        FSlotAnimationTrack& Track=Montage->SlotAnimTracks.AddDefaulted_GetRef(); // Montage持有单一全身轨道。
        Track.SlotName=TEXT("FullBody");
        FAnimSegment Segment; // 单个源序列、原速、一次播放；GA可按俯冲阶段时长缩放。
        Segment.SetAnimReference(Entry.Value); Segment.StartPos=0; Segment.AnimStartTime=0;
        Segment.AnimEndTime=Entry.Value->GetPlayLength(); Segment.AnimPlayRate=1; Segment.LoopingCount=1;
        Track.AnimTrack.AnimSegments.Add(Segment);
        static_cast<UAnimCompositeBase*>(Montage)->SetCompositeLength(Entry.Value->GetPlayLength());
        Montage->AddAnimCompositeSection(TEXT("Default"),0);
        Montage->BlendIn.SetBlendTime(Entry.Key=="Melee"?0.f:.06f); // 接触攻击无前摇，立即显示延伸姿势。
        Montage->BlendOut.SetBlendTime(.08f);
        Montage->bEnableAutoBlendOut=Entry.Key!="Death"; // 死亡保持最后姿势至Actor移除，不弹回待机。
        Montage->PostEditChange(); Montage->MarkPackageDirty(); Appearance->Actions.Add(Entry.Key,Montage);
    }
    Appearance->MarkPackageDirty(); UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_ANIMATION_ASSETS_READY %s actions=%d"),*Model,Appearance->Actions.Num());
    return true;
}
