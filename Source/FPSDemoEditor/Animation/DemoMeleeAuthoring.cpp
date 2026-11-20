// 职责：近战Linked Anim Layer建图与碰撞烘焙；按DA选择的参考/Idle基底与实际Body原子生成端点及武器frame，禁止运行时写资产。
// 用法：import_baseball_bat.py或install_manny_blade_r03_gameplay.py；依赖AnimGraph/AnimPose，输出由调用方验证保存。
#include "Animation/DemoMeleeAuthoring.h"
#include "Animation/DemoMeleeAnimInstance.h"
#include "Weapons/Melee/DemoMeleeDefinition.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_Inertialization.h"
#include "Misc/PackageName.h"
#include "Animation/AnimLayerInterface.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "AnimPose.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_TwoWayBlend.h" // 完整旧/新FP姿态一起混合，不能只平滑棍而让双手瞬跳。
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UnrealType.h"
#include "Debug/DemoLog.h"

namespace DemoMeleeAuthoring
{
    /** 轨迹烘焙采样时读取骨或插槽的组件空间变换；只读Pose/Skeleton，名称不存在时拒绝而非返回原点。 */
    bool AttachmentPose(const FAnimPose& Pose, const USkeleton* Skeleton, FName Name, FTransform& Result)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs attachment=%s"), __FUNCTION__, *Name.ToString());
        if (!Skeleton || !Pose.IsValid())
        { UE_LOG(LogFPSDemo, Warning, TEXT("MELEE_ATTACH_POSE_REJECT skeleton/pose")); return false; }
        if (Skeleton->FindSocket(Name))
        {
            Result = UAnimPoseExtensions::GetSocketPose(Pose, Name, EAnimPoseSpaces::World);
            return !Result.ContainsNaN();
        }
        if (Skeleton->GetReferenceSkeleton().FindBoneIndex(Name) != INDEX_NONE)
        {
            Result = UAnimPoseExtensions::GetBonePose(Pose, Name, EAnimPoseSpaces::World);
            return !Result.ContainsNaN();
        }
        UE_LOG(LogFPSDemo, Warning, TEXT("MELEE_ATTACH_POSE_REJECT missing attachment=%s"), *Name.ToString());
        return false;
    }
    /** 上身层中立烘焙：作者臂相对胸部保持，移除源Root/骨盆，并按WeaponAnchor同序叠脊柱。
      * IdleReference非空时使用已校验DA局部中立旋转，否则保留旧Skeleton参考；不在线修改源姿态。
      * Pose仅读，输出为模型组件空间，尚未乘BodyTransform；引用/指针仅在同步烘焙本次调用有效。 */
    bool NeutralUpperAttachment(const FAnimPose& Pose,const USkeleton* Skeleton,FName Name,FTransform& Attachment,FTransform& Chest,const TMap<FName,FQuat>* IdleReference)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs attachment=%s"),__FUNCTION__,*Name.ToString());
        if (!Skeleton || !Pose.IsValid()) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NEUTRAL_UPPER_REJECT skeleton/pose")); return false; }
        const FReferenceSkeleton& Reference=Skeleton->GetReferenceSkeleton(); // 只读当前实际161骨参考，不猜索引或骨长。
        const int32 Hip=Reference.FindBoneIndex(TEXT("pelvis")),UpperChest=Reference.FindBoneIndex(TEXT("spine_05"));
        if (Hip==INDEX_NONE || UpperChest==INDEX_NONE) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NEUTRAL_UPPER_REJECT missing pelvis/chest")); return false; }
        TArray<int32> Spine; // 从胸部向骨盆的真实索引链，下面反向父先子后。
        int32 Ancestor=UpperChest;
        while (Ancestor!=INDEX_NONE && Ancestor!=Hip) { Spine.Add(Ancestor); Ancestor=Reference.GetParentIndex(Ancestor); }
        if (Ancestor!=Hip) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NEUTRAL_UPPER_REJECT disconnected spine")); return false; }
        TArray<FTransform> ReferenceCS; ReferenceCS.SetNum(Reference.GetNum()); // 中立Root/骨盆组件空间，保留参考长度/scale。
        for (int32 Index=0;Index<Reference.GetNum();++Index)
        {
            const int32 Parent=Reference.GetParentIndex(Index);
            ReferenceCS[Index]=Parent==INDEX_NONE?Reference.GetRefBonePose()[Index]:Reference.GetRefBonePose()[Index]*ReferenceCS[Parent];
        }
        Chest=ReferenceCS[Hip];
        for (int32 Index=Spine.Num()-1;Index>=0;--Index)
        {
            const int32 Bone=Spine[Index];
            const FTransform SourceLocal=UAnimPoseExtensions::GetBonePose(Pose,Reference.GetBoneName(Bone),EAnimPoseSpaces::Local); // 作者普通骨平移不用于拉长中立移动上身。
            FTransform Mixed=Reference.GetRefBonePose()[Bone];
            FQuat Neutral=Mixed.GetRotation(); // 旧DA继续使用原ReferenceLocal，不改变已有轨迹表。
            if (IdleReference)
            {
                const FQuat* Rotation=IdleReference->Find(Reference.GetBoneName(Bone)); // 调用前整表校验，此处仍拒绝不完整父链。
                if (!Rotation) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NEUTRAL_UPPER_REJECT missing idle bone=%s"),*Reference.GetBoneName(Bone).ToString()); return false; }
                Neutral=*Rotation;
            }
            const FQuat Delta=(SourceLocal.GetRotation()*Neutral.Inverse()).GetNormalized(); // 与运行时一致：作者动态相对Idle/Ref，而非一律Skeleton参考。
            Mixed.SetRotation((Delta*Mixed.GetRotation()).GetNormalized()); // 与运行时WeaponAnchor相同Delta*Base顺序。
            Chest=Mixed*Chest; Chest.NormalizeRotation();
        }
        FTransform SourceAttachment; // 支持真实手骨或Skeleton Socket，仍只校准一次GripOffset。
        if (!AttachmentPose(Pose,Skeleton,Name,SourceAttachment)) return false;
        const FTransform SourceChest=UAnimPoseExtensions::GetBonePose(Pose,TEXT("spine_05"),EAnimPoseSpaces::World);
        Attachment=SourceAttachment.GetRelativeTransform(SourceChest)*Chest;
        Attachment.NormalizeRotation();
        return !Attachment.ContainsNaN() && !Chest.ContainsNaN();
    }
    /** Name是受控叶名，Mesh只约束骨架，bInterface创建无骨架接口；重跑复用包身份。 */
    UAnimBlueprint* Blueprint(const TCHAR* Name,USkeletalMesh* Mesh,bool bInterface=false)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring.Blueprint %s"),Name);
        const FString Path=FString(TEXT("/Game/Weapons/Melee/Animations/"))+Name; // 只写专属目录。
        UAnimBlueprint* BP=LoadObject<UAnimBlueprint>(nullptr,*(Path+TEXT(".")+Name)); // 包拥有，后续只借用。
        if (!BP)
        {
            UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>(); // 同步工厂，不跨线程持有。
            Factory->BlueprintType=bInterface?BPTYPE_Interface:BPTYPE_Normal;
            Factory->ParentClass=bInterface?UAnimInstance::StaticClass():UDemoMeleeAnimInstance::StaticClass(); Factory->TargetSkeleton=bInterface?nullptr:Mesh->GetSkeleton();
            BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*Path),Name,RF_Public|RF_Standalone,nullptr,GWarn));
            if (BP) FAssetRegistryModule::AssetCreated(BP);
        }
        if (BP) { BP->Modify(); if (!bInterface) BP->SetPreviewMesh(Mesh); } return BP;
    }
    /** BP/Name为当前包及图名，返回短期借用图，接口实现也一并检索。 */
    UEdGraph* Graph(UAnimBlueprint* BP,FName Name)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring.Graph"));
        for (UEdGraph* Item : BP->FunctionGraphs) if (Item->GetFName()==Name) return Item;
        for (const FBPInterfaceDescription& Interface : BP->ImplementedInterfaces) for (UEdGraph* Item : Interface.Graphs) if (Item->GetFName()==Name) return Item;
        return nullptr;
    }
    /** Graph由BP持有，X/Y是可读节点布局坐标，不参与运行时。 */
    template<class T> T* Node(UEdGraph* Graph,int32 X,int32 Y)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring.Node"));
        FGraphNodeCreator<T> Creator(*Graph); T* Result=Creator.CreateNode(); // 图拥有新节点，Finalize负责引脚。
        Result->NodePosX=X; Result->NodePosY=Y; Creator.Finalize(); return Result;
    }
    /** Graph仅为工具自己的图，删除副本列表避免迭代失效；用户定制图应复制后配置到DA。 */
    void Clear(UEdGraph* Graph)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring.Clear")); const TArray<UEdGraphNode*> Old=Graph->Nodes; for (UEdGraphNode* Item : Old) Graph->RemoveNode(Item); }
    /** A/B和Pin名称来自同一图；Schema拒绝不兼容连接，失败不报告成功。 */
    bool Link(UEdGraphNode* A,FName Out,UEdGraphNode* B,FName In)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring.Link"));
        UEdGraphPin* Output=A->FindPin(Out); UEdGraphPin* Input=B->FindPin(In); // 本次重构前短期借用。
        return Output && Input && A->GetSchema()->TryCreateConnection(Output,Input);
    }
    /** BP是当前变更资产；真实编译成功才允许后续连接/保存。 */
    bool Compile(UAnimBlueprint* BP)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring.Compile")); FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP); BP->MarkPackageDirty(); return BP->Status!=BS_Error && BP->GeneratedClass; }
    /** Graph是MeleePose实现或预览图，Idle是合法默认姿势；只读游戏线程缓存，关闭Notify副作用。 */
    bool Pose(UEdGraph* Graph,UAnimSequence* Idle)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring.Pose"));
        if (!Graph) return false; Clear(Graph);
        UAnimGraphNode_Root* Root=Node<UAnimGraphNode_Root>(Graph,500,0); Root->Node.SetGroup(TEXT("MeleeLayers")); // 唯一姿态输出。
        UAnimGraphNode_SequenceEvaluator* Eval=Node<UAnimGraphNode_SequenceEvaluator>(Graph,100,0); // 不运行Gameplay Notify。
        Eval->Node.SetSequence(Idle); Eval->Node.SetTeleportToExplicitTime(true); Eval->Node.SetShouldLoop(false);
        for (FOptionalPinFromProperty& Pin : Eval->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true;
        Eval->ReconstructNode();
        UK2Node_VariableGet* Sequence=Node<UK2Node_VariableGet>(Graph,-250,0); Sequence->VariableReference.SetSelfMember(TEXT("Pose")); Sequence->ReconstructNode(); // DA动态资源。
        UK2Node_VariableGet* Time=Node<UK2Node_VariableGet>(Graph,-250,150); Time->VariableReference.SetSelfMember(TEXT("PoseTime")); Time->ReconstructNode(); // 服务器phase对应源秒数。
        UAnimGraphNode_SequenceEvaluator* Previous=Node<UAnimGraphNode_SequenceEvaluator>(Graph,100,320); // 取消/换招冻结上一已显示姿势，仅表现，不运行Notify。
        Previous->Node.SetSequence(Idle); Previous->Node.SetTeleportToExplicitTime(true); Previous->Node.SetShouldLoop(false);
        for (FOptionalPinFromProperty& Pin:Previous->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true; // 动态资源与主线程缓存同源。
        Previous->ReconstructNode();
        UK2Node_VariableGet* OldPose=Node<UK2Node_VariableGet>(Graph,-250,320); OldPose->VariableReference.SetSelfMember(TEXT("PreviousPose")); OldPose->ReconstructNode(); // 本实例保活上一资源。
        UK2Node_VariableGet* OldTime=Node<UK2Node_VariableGet>(Graph,-250,450); OldTime->VariableReference.SetSelfMember(TEXT("PreviousTime")); OldTime->ReconstructNode(); // 冻结时刻，不补过期挥击。
        UK2Node_VariableGet* Alpha=Node<UK2Node_VariableGet>(Graph,100,570); Alpha->VariableReference.SetSelfMember(TEXT("BlendAlpha")); Alpha->ReconstructNode(); // DA配置的连续混合。
        UAnimGraphNode_TwoWayBlend* Blend=Node<UAnimGraphNode_TwoWayBlend>(Graph,450,150); Root->NodePosX=800; // 明确可编辑节点，双手与棍挂点一起混合。
        return Link(Sequence,TEXT("Pose"),Eval,TEXT("Sequence")) && Link(Time,TEXT("PoseTime"),Eval,TEXT("ExplicitTime"))
            && Link(OldPose,TEXT("PreviousPose"),Previous,TEXT("Sequence")) && Link(OldTime,TEXT("PreviousTime"),Previous,TEXT("ExplicitTime"))
            && Link(Previous,TEXT("Pose"),Blend,TEXT("A")) && Link(Eval,TEXT("Pose"),Blend,TEXT("B"))
            && Link(Alpha,TEXT("BlendAlpha"),Blend,TEXT("Alpha")) && Link(Blend,TEXT("Pose"),Root,TEXT("Result"));
    }
}
UAimOffsetBlendSpace1D* UDemoMeleeAuthoring::CreateFirstPersonLookOffset(const FString& PackagePath,const TArray<UAnimSequence*>& Clips,float DownDegrees,float UpDegrees)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!PackagePath.StartsWith(TEXT("/Game/Weapons/Melee/NativeIdleAim20261115/")) || !FPackageName::IsValidLongPackageName(PackagePath)
        || Clips.Num()!=3 || !Clips[0] || !Clips[0]->GetSkeleton() || !FMath::IsFinite(DownDegrees) || !FMath::IsFinite(UpDegrees) || DownDegrees>=0.f || UpDegrees<=0.f)
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_CREATE_REJECT path/samples/axis")); return nullptr; }
    for (UAnimSequence* Clip:Clips) // 同步借用三个已导入姿态，不重采样或写入动画。
        if (!Clip || Clip->GetSkeleton()!=Clips[0]->GetSkeleton() || Clip->AdditiveAnimType!=AAT_RotationOffsetMeshSpace)
        { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_CREATE_REJECT additive/skeleton")); return nullptr; }
    const FString Name=FPackageName::GetLongPackageAssetName(PackagePath); // 新候选独立包身份，禁止复跑覆盖已检查资产。
    if (FindPackage(nullptr,*PackagePath) || LoadObject<UObject>(nullptr,*(PackagePath+TEXT(".")+Name)))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_CREATE_REJECT existing=%s"),*PackagePath); return nullptr; }
    UAimOffsetBlendSpace1D* Space=NewObject<UAimOffsetBlendSpace1D>(CreatePackage(*PackagePath),*Name,RF_Public|RF_Standalone); // 包拥有，调用者明确保存。
    Space->SetSkeleton(Clips[0]->GetSkeleton());
    FStructProperty* Parameters=FindFProperty<FStructProperty>(UBlendSpace::StaticClass(),TEXT("BlendParameters")); // 与原生作者面板写同一固定属性。
    if (!Parameters) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_CREATE_REJECT axis property")); return nullptr; }
    FBlendParameter* Axis=Parameters->ContainerPtrToValuePtr<FBlendParameter>(Space,0);
    Axis->DisplayName=TEXT("Pitch"); Axis->Min=DownDegrees; Axis->Max=UpDegrees; Axis->GridNum=8;
    const float Positions[3]={DownDegrees,0.f,UpDegrees}; // 角度仅选择相对持握姿态，不重复旋转相机。
    for (int32 Index=0;Index<3;++Index)
        if (Space->AddSample(Clips[Index],FVector(Positions[Index],0,0))==INDEX_NONE)
        { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_CREATE_REJECT sample=%d"),Index); return nullptr; }
    Space->ValidateSampleData(); Space->ResampleData(); Space->PostEditChange(); Space->MarkPackageDirty(); FAssetRegistryModule::AssetCreated(Space);
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_LOOK_CREATE_SUCCESS %s"),*PackagePath); return Space;
}

UClass* UDemoMeleeAuthoring::BuildFirstPersonLookGraph(const FString& PackagePath,USkeletalMesh* Arms,UAnimSequence* Idle,UAimOffsetBlendSpace1D* Offset)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    using namespace DemoMeleeAuthoring;
    if (!PackagePath.StartsWith(TEXT("/Game/Weapons/Melee/NativeIdleAim20261115/")) || !FPackageName::IsValidLongPackageName(PackagePath)
        || !Arms || !Idle || !Offset || Arms->GetSkeleton()!=Idle->GetSkeleton() || Offset->GetSkeleton()!=Idle->GetSkeleton())
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_GRAPH_REJECT path/skeleton")); return nullptr; }
    const FString Name=FPackageName::GetLongPackageAssetName(PackagePath); // 仅创建新图，不重建正式或旧候选。
    if (FindPackage(nullptr,*PackagePath) || LoadObject<UObject>(nullptr,*(PackagePath+TEXT(".")+Name)))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_GRAPH_REJECT existing")); return nullptr; }
    UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>(); // 同步工厂，不跨线程/委托保留。
    Factory->ParentClass=UDemoMeleeAnimInstance::StaticClass(); Factory->TargetSkeleton=Arms->GetSkeleton();
    UAnimBlueprint* BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*PackagePath),*Name,RF_Public|RF_Standalone,nullptr,GWarn));
    if (!BP) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_GRAPH_REJECT factory")); return nullptr; }
    FAssetRegistryModule::AssetCreated(BP); BP->SetPreviewMesh(Arms);
    UEdGraph* G=Graph(BP,TEXT("AnimGraph")); // 工厂创建的独立主图，仅此图允许清理。
    if (!Pose(G,Idle)) return nullptr;
    UAnimGraphNode_Root* Root=nullptr; UAnimGraphNode_TwoWayBlend* Blend=nullptr; // 新建图的唯一输出/普通过渡节点。
    for (UEdGraphNode* Item:G->Nodes) { if (auto* Found=Cast<UAnimGraphNode_Root>(Item)) Root=Found; if (auto* Found=Cast<UAnimGraphNode_TwoWayBlend>(Item)) Blend=Found; }
    if (!Root || !Blend || !Root->FindPin(TEXT("Result"))) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_GRAPH_REJECT base graph")); return nullptr; }
    Root->FindPin(TEXT("Result"))->BreakAllPinLinks(); Root->NodePosX=1200;
    UAnimGraphNode_RotationOffsetBlendSpace* Look=Node<UAnimGraphNode_RotationOffsetBlendSpace>(G,800,0); // 引擎标准MeshSpace叠加，无自定义AnyThread骨骼写入。
    Look->Node.SetBlendSpace(Offset); Look->Node.SetPlayRate(0.f);
    for (FOptionalPinFromProperty& Pin:Look->ShowPinForProperties) if (Pin.PropertyName==TEXT("BlendSpace") || Pin.PropertyName==TEXT("X") || Pin.PropertyName==TEXT("Alpha")) Pin.bShowPin=true;
    static_cast<UEdGraphNode*>(Look)->ReconstructNode();
    for (FName Field:{FName(TEXT("LookOffset")),FName(TEXT("LookPitch")),FName(TEXT("LookWeight"))}) // 同步构图，不保存动态回调或捕获。
    {
        UK2Node_VariableGet* Get=Node<UK2Node_VariableGet>(G,450,650+100*(Field==TEXT("LookPitch")?1:Field==TEXT("LookWeight")?2:0));
        Get->VariableReference.SetSelfMember(Field); Get->ReconstructNode();
        const FName Input=Field==TEXT("LookOffset")?FName(TEXT("BlendSpace")):Field==TEXT("LookPitch")?FName(TEXT("X")):FName(TEXT("Alpha"));
        if (!Link(Get,Field,Look,Input)) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_GRAPH_REJECT pin=%s"),*Input.ToString()); return nullptr; }
    }
    if (!Link(Blend,TEXT("Pose"),Look,TEXT("BasePose")) || !Link(Look,TEXT("Pose"),Root,TEXT("Result")) || !Compile(BP))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_LOOK_GRAPH_REJECT link/compile")); return nullptr; }
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_LOOK_GRAPH_SUCCESS %s"),*PackagePath); return BP->GeneratedClass;
}

UClass* UDemoMeleeAuthoring::BuildFirstPersonRecoveryGraph(const FString& PackagePath,USkeletalMesh* Arms,UAnimSequence* Idle,UAimOffsetBlendSpace1D* Offset)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    using namespace DemoMeleeAuthoring;
    if (!PackagePath.StartsWith(TEXT("/Game/Weapons/Melee/NativeIdleAim20261115/RecoveryR02/")) || !FPackageName::IsValidLongPackageName(PackagePath)
        || !Arms || !Idle || !Offset || Arms->GetSkeleton()!=Idle->GetSkeleton() || Offset->GetSkeleton()!=Idle->GetSkeleton())
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RECOVERY_GRAPH_REJECT input")); return nullptr; }
    const FString Name=FPackageName::GetLongPackageAssetName(PackagePath); // 只新建，不覆盖历史R01和其源对照。
    if (FindPackage(nullptr,*PackagePath) || FPackageName::DoesPackageExist(PackagePath))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RECOVERY_GRAPH_REJECT existing")); return nullptr; }
    UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>();
    Factory->ParentClass=UDemoMeleeAnimInstance::StaticClass(); Factory->TargetSkeleton=Arms->GetSkeleton();
    UAnimBlueprint* BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*PackagePath),*Name,RF_Public|RF_Standalone,nullptr,GWarn));
    if (!BP) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RECOVERY_GRAPH_REJECT factory")); return nullptr; }
    FAssetRegistryModule::AssetCreated(BP); BP->SetPreviewMesh(Arms);
    UEdGraph* G=Graph(BP,TEXT("AnimGraph")); // 新候选自有图，原Pose普通/连续攻击分支原样复用。
    if (!Pose(G,Idle)) return nullptr;
    UAnimGraphNode_Root* Root=nullptr; UAnimGraphNode_TwoWayBlend* Attack=nullptr;
    for (UEdGraphNode* Item:G->Nodes) { if(auto* N=Cast<UAnimGraphNode_Root>(Item))Root=N; if(auto* N=Cast<UAnimGraphNode_TwoWayBlend>(Item))Attack=N; }
    if (!Root || !Attack) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RECOVERY_GRAPH_REJECT attack graph")); return nullptr; }
    Root->FindPin(TEXT("Result"))->BreakAllPinLinks(); Root->NodePosX=1700;
    UAnimGraphNode_SequenceEvaluator* Hold=Node<UAnimGraphNode_SequenceEvaluator>(G,400,-400); // 待机专用分支，切回时读取匹配Idle0，不把Look叠到正在混出的攻击上。
    Hold->Node.SetSequence(Idle); Hold->Node.SetTeleportToExplicitTime(true); Hold->Node.SetShouldLoop(false);
    for(FOptionalPinFromProperty& Pin:Hold->ShowPinForProperties)if(Pin.PropertyName==TEXT("Sequence"))Pin.bShowPin=true;
    Hold->ReconstructNode();
    UAnimGraphNode_RotationOffsetBlendSpace* Look=Node<UAnimGraphNode_RotationOffsetBlendSpace>(G,700,-400);
    Look->Node.SetBlendSpace(Offset); Look->Node.SetPlayRate(0.f); Look->Node.Alpha=1.f; // 完整Idle目标；其进入速度和偏移由同一个引擎惯性节点处理。
    for(FOptionalPinFromProperty& Pin:Look->ShowPinForProperties)if(Pin.PropertyName==TEXT("BlendSpace") || Pin.PropertyName==TEXT("X"))Pin.bShowPin=true;
    static_cast<UEdGraphNode*>(Look)->ReconstructNode();
    UAnimGraphNode_BlendListByBool* Select=Node<UAnimGraphNode_BlendListByBool>(G,1050,0); // true=Idle，false=攻击；只有跨这两态请求惯性化，连续正反手不触发。
    FEnumProperty* Transition=FindFProperty<FEnumProperty>(FAnimNode_BlendListBase::StaticStruct(),TEXT("TransitionType")); // UE5.4私有编辑属性经反射写同一作者字段，不改引擎访问权限。
    if(!Transition || Select->Node.GetBlendTimes().Num()!=2) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RECOVERY_GRAPH_REJECT transition schema")); return nullptr; }
    Transition->GetUnderlyingProperty()->SetIntPropertyValue(Transition->ContainerPtrToValuePtr<void>(&Select->Node),static_cast<int64>(EBlendListTransitionType::Inertialization));
    Select->ReconstructNode();
    UAnimGraphNode_Inertialization* Inertia=Node<UAnimGraphNode_Inertialization>(G,1400,0); // UE5.4原生保持先前实际姿态和速度的过渡，不自行实现骨骼弹簧。
    struct FInput { const TCHAR* Field; UEdGraphNode* Target; const TCHAR* Pin; }; // 同步建图小表，不跨线程/回调保存UObject。
    const FInput Inputs[]={{TEXT("Pose"),Hold,TEXT("Sequence")},{TEXT("PoseTime"),Hold,TEXT("ExplicitTime")},
        {TEXT("LookOffset"),Look,TEXT("BlendSpace")},{TEXT("LookPitch"),Look,TEXT("X")},
        {TEXT("bIdlePresentation"),Select,TEXT("bActiveValue")},{TEXT("RecoveryBlendSeconds"),Select,TEXT("BlendTime_0")}};
    for(const FInput& Input:Inputs)
    {
        UK2Node_VariableGet* Get=Node<UK2Node_VariableGet>(G,Input.Target->NodePosX-250,Input.Target->NodePosY+200);
        Get->VariableReference.SetSelfMember(Input.Field); Get->ReconstructNode();
        if(!Link(Get,Input.Field,Input.Target,Input.Pin)) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RECOVERY_GRAPH_REJECT pin=%s"),Input.Pin); return nullptr; }
    }
    if(!Link(Hold,TEXT("Pose"),Look,TEXT("BasePose")) || !Link(Look,TEXT("Pose"),Select,TEXT("BlendPose_0"))
        || !Link(Attack,TEXT("Pose"),Select,TEXT("BlendPose_1")) || !Link(Select,TEXT("Pose"),Inertia,TEXT("Source")) || !Link(Inertia,TEXT("Pose"),Root,TEXT("Result")) || !Compile(BP))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RECOVERY_GRAPH_REJECT links/compile")); return nullptr; }
    UDemoMeleeAnimInstance* Defaults=Cast<UDemoMeleeAnimInstance>(BP->GeneratedClass->GetDefaultObject());
    if(!Defaults) return nullptr;
    FBoolProperty* OptIn=FindFProperty<FBoolProperty>(Defaults->GetClass(),TEXT("bUseRecoveryInertialGraph"));
    if(!OptIn) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RECOVERY_GRAPH_REJECT native optin")); return nullptr; }
    OptIn->SetPropertyValue_InContainer(Defaults,true); BP->MarkPackageDirty();
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RECOVERY_GRAPH_SUCCESS %s"),*PackagePath); return BP->GeneratedClass;
}

UClass* UDemoMeleeAuthoring::BuildSOMAFirstPersonGraph(USkeletalMesh* Arms,UAnimSequence* Idle)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Arms || !Idle || Arms->GetSkeleton()!=Idle->GetSkeleton()) return nullptr;
    const FString Path=TEXT("/Game/Weapons/Melee/Candidates/SOMAReferenceIdle20261030/ABP_SOMA_RigidFP"); // 仅允许独立候选，不重建旧正式图。
    if (FindPackage(nullptr,*Path) || LoadObject<UAnimBlueprint>(nullptr,*(Path+TEXT(".ABP_SOMA_RigidFP"))))
    { UE_LOG(LogFPSDemo,Warning,TEXT("SOMA_FP_GRAPH_EXISTS preserve existing revision")); return nullptr; }
    UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>(); // 同步工厂，目标mesh与clip必须同骨架。
    Factory->ParentClass=UDemoMeleeAnimInstance::StaticClass(); Factory->TargetSkeleton=Arms->GetSkeleton();
    UAnimBlueprint* Blueprint=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*Path),TEXT("ABP_SOMA_RigidFP"),RF_Public|RF_Standalone,nullptr,GWarn));
    if (!Blueprint) return nullptr;
    FAssetRegistryModule::AssetCreated(Blueprint); Blueprint->SetPreviewMesh(Arms);
    if (!DemoMeleeAuthoring::Pose(DemoMeleeAuthoring::Graph(Blueprint,TEXT("AnimGraph")),Idle) || !DemoMeleeAuthoring::Compile(Blueprint)) return nullptr;
    return Blueprint->GeneratedClass;
}

bool UDemoMeleeAuthoring::BuildGraphs(USkeletalMesh* Arms,UAnimSequence* Idle)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Arms || !Idle || Arms->GetSkeleton()!=Idle->GetSkeleton()) return false;
    using namespace DemoMeleeAuthoring;
    const FName LayerName=TEXT("MeleePose"); // 公开层契约，设计师可以替换层实现。
    UAnimBlueprint* Interface=Blueprint(TEXT("ALI_MeleeLayers"),Arms,true); // 三份图都在独立近战目录。
    if (!Interface) return false;
    UEdGraph* Signature=Graph(Interface,LayerName); // 接口GUID重跑保留，避免生成断开的实现。
    if (!Signature) { Signature=FBlueprintEditorUtils::CreateNewGraph(Interface,LayerName,UAnimationGraph::StaticClass(),UAnimationGraphSchema::StaticClass()); FBlueprintEditorUtils::AddDomainSpecificGraph(Interface,Signature); }
    Clear(Signature); Node<UAnimGraphNode_Root>(Signature,0,0)->Node.SetGroup(TEXT("MeleeLayers")); if (!Compile(Interface)) return false;
    UAnimBlueprint* Layer=Blueprint(TEXT("ABP_MeleePose"),Arms); UAnimBlueprint* Main=Blueprint(TEXT("ABP_Melee_FP"),Arms); // 层图与主图独立可编辑。
    if (!Layer || !Main) return false;
    for (UAnimBlueprint* BP : {Layer,Main}) // 重跑只补缺失接口，不重复创建同名实现。
    {
        if (!Graph(BP,LayerName) && !FBlueprintEditorUtils::ImplementNewInterface(BP,Interface->GeneratedClass->GetClassPathName())) return false;
        if (!Pose(Graph(BP,LayerName),Idle)) return false;
    }
    if (!Pose(Graph(Layer,TEXT("AnimGraph")),Idle) || !Compile(Layer)) return false;
    UEdGraph* MainGraph=Graph(Main,TEXT("AnimGraph")); // 主图唯一Linked Anim Layer输出，未来可加入纯表现节点。
    if (!MainGraph) return false; Clear(MainGraph);
    UAnimGraphNode_LinkedAnimLayer* Linked=Node<UAnimGraphNode_LinkedAnimLayer>(MainGraph,0,0); // 真正引擎层节点，不是C++四类switch。
    Linked->Node.Interface=Interface->GeneratedClass; Linked->Node.Layer=LayerName; Linked->Node.InstanceClass=Layer->GeneratedClass; Linked->InterfaceGuid=Graph(Main,LayerName)->InterfaceGuid;
    FStructProperty* Property=FindFProperty<FStructProperty>(Linked->GetClass(),TEXT("FunctionReference")); // UE5.4 MinimalAPI成员通过反射设置。
    if (!Property) return false;
    FMemberReference* Reference=Property->ContainerPtrToValuePtr<FMemberReference>(Linked); // 节点拥有结构体，只在本次构图借用。
    Reference->SetExternalMember(LayerName,Interface->GeneratedClass,Signature->GraphGuid);
    UEdGraphNode* Generic=Linked; Generic->ReconstructNode();
    UAnimGraphNode_Root* Root=Node<UAnimGraphNode_Root>(MainGraph,450,0); // 最终输出。
    if (!Link(Linked,TEXT("Pose"),Root,TEXT("Result")) || !Compile(Main)) return false;
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_GRAPHS_SUCCESS ALI_MeleeLayers/ABP_MeleePose/ABP_Melee_FP")); return true;
}
bool UDemoMeleeAuthoring::BakeCollision(UDemoMeleeDefinition* Definition,FVector Origin,FVector Axis)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Definition || !Definition->ThirdPersonIdle || Axis.IsNearlyZero()) return false;
    FAnimPose Pose; FAnimPoseEvaluationOptions Options; // Editor实际解压序列姿态，含FBX导入轴修正。
    UAnimPoseExtensions::GetAnimPoseAtTime(Definition->ThirdPersonIdle,0.f,Options,Pose);
    if (!Pose.IsValid()) return false;
    const FTransform Desired(FQuat::FindBetweenNormals(FVector::UpVector,Axis.GetSafeNormal()),Origin); // 圆棍滚转不影响握柄形状。
    FTransform Hand; // 当前父骨或专用插槽的真实组件变换；插槽名称不能交给GetBonePose。
    if (!DemoMeleeAuthoring::AttachmentPose(Pose,Definition->ThirdPersonIdle->GetSkeleton(),Definition->HandSocket,Hand)) return false;
    Definition->Modify(); Definition->GripOffset=Desired.GetRelativeTransform(Hand);
    return RebakeConfiguredCollision(Definition);
}
bool UDemoMeleeAuthoring::RebakeConfiguredCollision(UDemoMeleeDefinition* Definition)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    // 兼容旧作者脚本的既有身体约定；新资产必须调用显式Body接口，不猜当前Appearance的Z偏移。
    return RebakeConfiguredCollisionWithBody(Definition,FTransform(FRotator(0,-90,0),FVector(0,0,-99)));
}
bool UDemoMeleeAuthoring::RebakeConfiguredCollisionWithBody(UDemoMeleeDefinition* Definition,const FTransform& BodyTransform)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs definition=%s body=%s"),__FUNCTION__,*GetNameSafe(Definition),*BodyTransform.ToHumanReadableString());
    if (!Definition || !Definition->ThirdPersonIdle || Definition->GripOffset.ContainsNaN())
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_BAKE_REJECT definition/idle/grip")); return false; }
    if (BodyTransform.ContainsNaN() || !BodyTransform.GetRotation().IsNormalized() || BodyTransform.GetScale3D().GetMin()<=0.f)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_BAKE_REJECT invalid explicit body transform")); return false; }
    TMap<FName,FQuat> IdleNeutralRotations; // 只在本次同步烘焙存活的五骨纯值；所有采样和Pivot共享同一策略。
    const TMap<FName,FQuat>* IdleReference=nullptr; // nullptr严格保留旧DA烘焙公式。
    if (Definition->bUseWeaponIdleSpineReference)
    {
        FString ReferenceError; // 烘焙失败原因，任何无效参考在修改DA之前拒绝。
        if (!Definition->CopySpineReference(IdleNeutralRotations,ReferenceError))
        { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_BAKE_REJECT idle_spine_reference %s"),*ReferenceError); return false; }
        IdleReference=&IdleNeutralRotations;
    }
    FAnimPose Pose; FAnimPoseEvaluationOptions Options; // 同步解压当前真实动画，不修改Skeleton或源动画。
    UAnimPoseExtensions::GetAnimPoseAtTime(Definition->ThirdPersonIdle,0.f,Options,Pose);
    if (!Pose.IsValid()) return false;
    const FTransform Body=BodyTransform; // 实际配置相对胶囊的身体转换，不能用审片Actor的世界变换替代。
    FTransform IdleAttachment,IdleChest=UAnimPoseExtensions::GetBonePose(Pose,TEXT("spine_05"),EAnimPoseSpaces::World); // 原全身策略保持；新上身策略移除作者骨盆。
    if (Definition->bUseUpperBodyMontage && !DemoMeleeAuthoring::NeutralUpperAttachment(Pose,Definition->ThirdPersonIdle->GetSkeleton(),Definition->HandSocket,IdleAttachment,IdleChest,IdleReference))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_BAKE_REJECT neutral upper-body chain")); return false; }
    const FVector Pivot=Body.TransformPosition(IdleChest.GetLocation()); // 全部成功才提交AimPivot和轨迹。
    TArray<FDemoMeleeAttack> Baked=Definition->Attacks; // 拷贝配置进行原子烘焙，失败不留下半套轨迹。
    for (FDemoMeleeAttack& Attack : Baked) // 实际导入动作采样，运行时不读取Blender JSON。
    {
        if (!Attack.ThirdPerson || Attack.ThirdPerson->GetSkeleton()!=Definition->ThirdPersonIdle->GetSkeleton()) return false;
        const int32 Count=FMath::CeilToInt(Attack.ThirdPerson->GetPlayLength()*120.f); // 包括端点，最大601由运行时验证。
        if (Count<1 || Count>600) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_BAKE_REJECT source clip must be >0 and <=5 seconds")); return false; }
        Attack.TracePoses.Reset(); Attack.WeaponFrames.Reset(); // 两份表原子提交，相位/数量严格一致。
        for (int32 Index=0;Index<=Count;++Index)
        {
            const double SampleTime=FMath::Min(static_cast<double>(Attack.ThirdPerson->GetPlayLength()),static_cast<double>(Attack.ThirdPerson->GetPlayLength())*Index/Count); // double归一化并夹末端，float先乘再除可能超过合法长度触发Ensure。
            UAnimPoseExtensions::GetAnimPoseAtTime(Attack.ThirdPerson,SampleTime,Options,Pose);
            if (!Pose.IsValid()) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_BAKE_INVALID_POSE attack=%s sample=%d"),*Attack.Id.ToString(),Index); return false; }
            FTransform Attachment; // 每个采样读取实际挂点；保持Socket与运行时AttachToComponent相同的组合顺序。
            if (!DemoMeleeAuthoring::AttachmentPose(Pose,Attack.ThirdPerson->GetSkeleton(),Definition->HandSocket,Attachment)) return false;
            if (Definition->bUseUpperBodyMontage)
            {
                FTransform NeutralChest; // 只属于本采样的中立层结果，不叠原源Root/Pelvis。
                if (!DemoMeleeAuthoring::NeutralUpperAttachment(Pose,Attack.ThirdPerson->GetSkeleton(),Definition->HandSocket,Attachment,NeutralChest,IdleReference))
                { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_BAKE_INVALID_UPPER attack=%s sample=%d"),*Attack.Id.ToString(),Index); return false; }
            }
            const FTransform Bat=Definition->GripOffset*Attachment*Body; // 原手骨和新插槽都支持，不改源动画。
            Attack.WeaponFrames.Add(Bat); // 完整刚体框供显式上身策略重锚，不把它当手腕目标或重复驱动模型。
            FDemoMeleeTracePose& Key=Attack.TracePoses.AddDefaulted_GetRef(); // DA持有采样值，不持有临时AnimPose。
            Key.Base=Bat.TransformPosition(Definition->TraceBase); Key.Tip=Bat.TransformPosition(Definition->TraceTip);
        }
    }
    Definition->Modify(); Definition->AimPivot=Pivot; Definition->Attacks=MoveTemp(Baked); // 保留设计师伤害/窗口/握点，只更新采样结果。
    Definition->MarkPackageDirty(); UE_LOG(LogFPSDemo,Display,TEXT("MELEE_COLLISION_BAKED grip=%s pivot=%s upper_neutral=%d idle_reference=%d revision=%s body=%s"),*Definition->GripOffset.ToHumanReadableString(),*Definition->AimPivot.ToString(),Definition->bUseUpperBodyMontage,Definition->bUseWeaponIdleSpineReference,*Definition->SpineReferenceRevision,*Body.ToHumanReadableString()); return true;
}
