#include "Animation/DemoFPAnimationAuthoring.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimLayerInterface.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"

namespace DemoFPAuthoring
{
    // 固定生成目录与接口名；所有路径由工程内部使用，禁止接受任意包名覆盖手工制作的资产。
    const FString Directory = TEXT("/Game/Weapons/Animations/");
    const FName LayerName = TEXT("WeaponBasePose"); // 与运行时LinkAnimClassLayers及测试使用同一个真实动画函数。

    /** Name是受控资产叶名，Parent是原生或公共父类；Mesh约束骨架；bInterface决定创建动画层接口。 */
    UAnimBlueprint* Blueprint(const FString& Name, UClass* Parent, USkeletalMesh* Mesh, bool bInterface = false)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::Blueprint %s"), *Name);
        const FString PackagePath = Directory + Name; // 重运行复用相同包，保留资源引用身份。
        UAnimBlueprint* Result = LoadObject<UAnimBlueprint>(nullptr, *(PackagePath + TEXT(".") + Name)); // 返回资产由包拥有。
        if (!Result)
        {
            UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>(); // 同步编辑器工厂，无跨帧引用。
            Factory->BlueprintType = bInterface ? BPTYPE_Interface : BPTYPE_Normal;
            Factory->ParentClass = bInterface ? UAnimInstance::StaticClass() : Parent;
            Factory->TargetSkeleton = bInterface ? nullptr : Mesh->GetSkeleton();
            Result = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),
                CreatePackage(*PackagePath), *Name, RF_Public | RF_Standalone, nullptr, GWarn));
            if (!Result)
            {
                UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_CREATE_FAILED asset=%s"), *PackagePath);
                return nullptr;
            }
            FAssetRegistryModule::AssetCreated(Result);
        }
        // 重建只支持原先相同类型/继承关系，避免错误路径静默覆盖设计师资产。
        if (Result->BlueprintType != (bInterface ? BPTYPE_Interface : BPTYPE_Normal)
            || (!bInterface && Result->ParentClass != Parent))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_PARENT_MISMATCH asset=%s parent=%s"),
                *PackagePath, *GetNameSafe(Result->ParentClass));
            return nullptr;
        }
        Result->Modify();
        Result->TargetSkeleton = bInterface ? nullptr : Mesh->GetSkeleton();
        if (!bInterface) Result->SetPreviewMesh(Mesh);
        return Result;
    }

    /** Graph属于正在同步构建的AnimBP；X/Y是便于后续人工查看的节点位置。 */
    template<class T> T* Node(UEdGraph* Graph, int32 X, int32 Y)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::Node graph=%s"), *GetNameSafe(Graph));
        FGraphNodeCreator<T> Creator(*Graph); // Finalize创建反射Pins，不能跨蓝图编译保存Creator。
        T* Result = Creator.CreateNode(); // 图拥有节点，调用方仅借用至本次编译。
        Result->NodePosX = X;
        Result->NodePosY = Y;
        Creator.Finalize();
        return Result;
    }

    /** Blueprint是待检索资产；Name是AnimGraph或接口图名称，返回借用指针，不创建或修改资产。 */
    UEdGraph* FindGraph(UAnimBlueprint* Blueprint, FName Name)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::FindGraph %s.%s"), *GetNameSafe(Blueprint), *Name.ToString());
        for (UEdGraph* Graph : Blueprint->FunctionGraphs) // 直接函数图由Blueprint拥有。
            if (Graph->GetFName() == Name) return Graph;
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces) // 实现图存储于接口描述而非FunctionGraphs。
            for (UEdGraph* Graph : Interface.Graphs) // 不跨编译保留节点/图地址。
                if (Graph->GetFName() == Name) return Graph;
        return nullptr;
    }

    /** Graph为本工具管理的图；删除旧节点但保留Graph GUID与接口身份，使重跑不产生失效链接。 */
    void ClearGraph(UEdGraph* Graph)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::ClearGraph %s"), *GetNameSafe(Graph));
        const TArray<UEdGraphNode*> OldNodes = Graph->Nodes; // 遍历副本，RemoveNode会修改原数组。
        for (UEdGraphNode* Old : OldNodes) Graph->RemoveNode(Old); // 旧节点由GC在图重建后回收。
    }

    /** From/To均来自本次图，Output/Input为真实Pin名；连接失败立即终止本次资产构建。 */
    bool Link(UEdGraphNode* From, FName Output, UEdGraphNode* To, FName Input)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::Link %s -> %s"), *Output.ToString(), *Input.ToString());
        UEdGraphPin* OutputPin = From->FindPin(Output); // 仅本次连接借用，后续重构节点会使其失效。
        UEdGraphPin* InputPin = To->FindPin(Input); // Schema负责检查动画姿势/资产数据类型。
        if (!OutputPin || !InputPin || !From->GetSchema()->TryCreateConnection(OutputPin, InputPin))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_LINK_FAILED graph=%s from=%s.%s to=%s.%s"),
                *GetNameSafe(From->GetGraph()), *GetNameSafe(From), *Output.ToString(), *GetNameSafe(To), *Input.ToString());
            return false;
        }
        return true;
    }

    /** Blueprint已完成图修改；成功必须生成可用UClass，不能仅以包创建成功作为建图成功。 */
    bool Compile(UAnimBlueprint* Blueprint)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::Compile %s"), *GetNameSafe(Blueprint));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass)
        {
            UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_COMPILE_FAILED asset=%s status=%d"), *Blueprint->GetPathName(), Blueprint->Status);
            return false;
        }
        Blueprint->MarkPackageDirty();
        return true;
    }

    /** Blueprint须实现指定LayerInterface；重复构建直接复用已有接口实现图，避免重复名称。 */
    bool EnsureInterface(UAnimBlueprint* Blueprint, UAnimBlueprint* LayerInterface)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::EnsureInterface %s"), *GetNameSafe(Blueprint));
        for (const FBPInterfaceDescription& Existing : Blueprint->ImplementedInterfaces) // 原接口描述由Blueprint持有。
            if (Existing.Interface == LayerInterface->GeneratedClass) return FindGraph(Blueprint, LayerName) != nullptr;
        if (!FBlueprintEditorUtils::ImplementNewInterface(Blueprint, LayerInterface->GeneratedClass->GetClassPathName()))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_INTERFACE_FAILED asset=%s"), *Blueprint->GetPathName());
            return false;
        }
        return FindGraph(Blueprint, LayerName) != nullptr;
    }

    /** Graph是接口实现或预览图，Idle为合法默认资产；bDynamic令公共图从原生IdlePose缓存读取子类配置。 */
    bool BuildPose(UEdGraph* Graph, UAnimSequence* Idle, bool bDynamic)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::BuildPose graph=%s dynamic=%d"), *GetNameSafe(Graph), bDynamic);
        if (!Graph) return false;
        ClearGraph(Graph);
        UAnimGraphNode_Root* Root = Node<UAnimGraphNode_Root>(Graph,400,0); // 图输出只保留一处，编译器据图名识别层入口。
        Root->Node.SetGroup(TEXT("FPWeaponLayers"));
        UAnimGraphNode_SequencePlayer* Player = Node<UAnimGraphNode_SequencePlayer>(Graph,0,0); // 循环基础姿势，无Gameplay执行逻辑。
        Player->Node.SetSequence(Idle);
        Player->Node.SetLoopAnimation(true);
        if (bDynamic)
        {
            for (FOptionalPinFromProperty& Pin : Player->ShowPinForProperties) // 引擎将Sequence默认隐藏，显式公开以接受原生缓存输入。
                if (Pin.PropertyName == TEXT("Sequence")) Pin.bShowPin = true;
            Player->ReconstructNode();
            UK2Node_VariableGet* IdleProperty = Node<UK2Node_VariableGet>(Graph,-300,0); // 只读当前实例成员，不访问World/Actor。
            IdleProperty->VariableReference.SetSelfMember(TEXT("IdlePose"));
            IdleProperty->ReconstructNode();
            if (!Link(IdleProperty,TEXT("IdlePose"),Player,TEXT("Sequence"))) return false;
        }
        if (UAnimationGraph* AnimationGraph = Cast<UAnimationGraph>(Graph)) // Linked layer切换向主图Inertialization提出过渡请求。
        {
            AnimationGraph->BlendOptions.BlendInTime = .16f;
            AnimationGraph->BlendOptions.BlendOutTime = .16f;
        }
        return Link(Player,TEXT("Pose"),Root,TEXT("Result"));
    }

    /** Main为固定主AnimBP；LayerInterface定义契约，Base为初始默认层；主实例唯一FPAction Slot接收ASC Montage。 */
    bool BuildMain(UAnimBlueprint* Main, UAnimBlueprint* LayerInterface, UAnimBlueprint* Base)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FPAuthoring::BuildMain"));
        UEdGraph* Graph = FindGraph(Main,TEXT("AnimGraph")); // 工厂拥有的主图，仅本工具的固定主实例使用。
        if (!Graph) return false;
        ClearGraph(Graph);
        UAnimGraphNode_LinkedAnimLayer* Layer = Node<UAnimGraphNode_LinkedAnimLayer>(Graph,-500,0); // 实际动态层节点，不用四分支静态Select冒充层链接。
        Layer->Node.Interface = LayerInterface->GeneratedClass;
        Layer->Node.Layer = LayerName;
        Layer->Node.InstanceClass = Base->GeneratedClass;
        Layer->InterfaceGuid = FindGraph(Main,LayerName)->InterfaceGuid;
        // UE5.4的SetLayerName是非导出MinimalAPI；反射写入同一个FMemberReference，避免依赖不可链接的Editor内部符号。
        FStructProperty* ReferenceProperty = FindFProperty<FStructProperty>(Layer->GetClass(),TEXT("FunctionReference")); // 只借用类字段元数据。
        if (!ReferenceProperty || ReferenceProperty->Struct != FMemberReference::StaticStruct())
        {
            UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_MEMBER_REFERENCE_MISSING"));
            return false;
        }
        FMemberReference* Reference = ReferenceProperty->ContainerPtrToValuePtr<FMemberReference>(Layer); // 同步写入当前节点，不延长节点生命周期。
        Reference->SetExternalMember(LayerName,LayerInterface->GeneratedClass,FindGraph(LayerInterface,LayerName)->GraphGuid);
        UEdGraphNode* LayerNode = Layer; // 通过导出基类虚接口重构Pins，不直接链接MinimalAPI子类的非导出成员。
        LayerNode->ReconstructNode();
        UAnimGraphNode_Slot* Slot = Node<UAnimGraphNode_Slot>(Graph,-120,0); // 开火与换弹共同使用主实例Slot，GAS决定互斥。
        Slot->Node.SlotName = TEXT("FPAction");
        // 支撑手IK只依赖独立ik_hand_gun锚点，不能读取自己的最终hand_l变换形成循环依赖。
        UAnimGraphNode_LocalToComponentSpace* ToComponent = Node<UAnimGraphNode_LocalToComponentSpace>(Graph,180,0); // 骨控制在组件姿势空间求值。
        UAnimGraphNode_TwoBoneIK* SupportHand = Node<UAnimGraphNode_TwoBoneIK>(Graph,460,0); // 主实例缓存的Alpha在换弹时为0，保留烘焙双手动作。
        SupportHand->Node.IKBone.BoneName = TEXT("hand_l");
        SupportHand->Node.EffectorLocationSpace = BCS_BoneSpace;
        SupportHand->Node.EffectorTarget.BoneReference.BoneName = TEXT("ik_hand_gun");
        SupportHand->Node.bTakeRotationFromEffectorSpace = false;
        SupportHand->Node.bMaintainEffectorRelRot = true;
        SupportHand->Node.bAllowStretching = false;
        SupportHand->Node.JointTargetLocationSpace = BCS_ComponentSpace;
        SupportHand->Node.JointTargetLocation = FVector(40.9807f,-18.9418f,111.0737f); // 模板左肩+(30,-18,-28)，与离线ArmIK肘平面一致；换弹IK权重变化不让肘部跳向镜头。
        SupportHand->Node.AlphaInputType = EAnimAlphaInputType::Float;
        for (FOptionalPinFromProperty& Pin : SupportHand->ShowPinForProperties) // 显式暴露运行时缓存输入，肘部朝向保留静态默认值。
        {
            if (Pin.PropertyName == TEXT("EffectorLocation") || Pin.PropertyName == TEXT("Alpha")) Pin.bShowPin = true;
            if (Pin.PropertyName == TEXT("JointTargetLocation")) Pin.bShowPin = false;
        }
        UEdGraphNode* SupportHandNode = SupportHand; // MinimalAPI通过基类虚接口刷新字段默认值与Pins。
        SupportHandNode->ReconstructNode();
        UK2Node_VariableGet* Grip = Node<UK2Node_VariableGet>(Graph,160,-240); // ik_hand_gun骨空间厘米，由武器配置和主实例采样提供。
        Grip->VariableReference.SetSelfMember(TEXT("SupportHandGrip"));
        Grip->ReconstructNode();
        UK2Node_VariableGet* IKAlpha = Node<UK2Node_VariableGet>(Graph,160,220); // [0,1]权重，换弹释放、待机恢复。
        IKAlpha->VariableReference.SetSelfMember(TEXT("SupportHandIKAlpha"));
        IKAlpha->ReconstructNode();
        UAnimGraphNode_ComponentToLocalSpace* ToLocal = Node<UAnimGraphNode_ComponentToLocalSpace>(Graph,780,0); // Slot后骨控制输出重新转为普通Pose。
        UAnimGraphNode_Inertialization* Blend = Node<UAnimGraphNode_Inertialization>(Graph,1060,0); // 接收层图的惯性过渡请求。
        UAnimGraphNode_Root* Root = Node<UAnimGraphNode_Root>(Graph,1380,0); // 固定主图最终输出。
        return Link(Layer,TEXT("Pose"),Slot,TEXT("Source"))
            && Link(Slot,TEXT("Pose"),ToComponent,TEXT("LocalPose"))
            // LocalToComponent的引擎输出Pin名是ComponentPose，区别于普通动画节点的Pose。
            && Link(ToComponent,TEXT("ComponentPose"),SupportHand,TEXT("ComponentPose"))
            && Link(Grip,TEXT("SupportHandGrip"),SupportHand,TEXT("EffectorLocation"))
            && Link(IKAlpha,TEXT("SupportHandIKAlpha"),SupportHand,TEXT("Alpha"))
            && Link(SupportHand,TEXT("Pose"),ToLocal,TEXT("ComponentPose"))
            && Link(ToLocal,TEXT("Pose"),Blend,TEXT("Source"))
            && Link(Blend,TEXT("Pose"),Root,TEXT("Result"));
    }
}

bool UDemoFPAnimationAuthoring::Build(USkeletalMesh* ArmsMesh, UAnimSequence* FallbackIdle)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] UDemoFPAnimationAuthoring::Build"));
    if (!ArmsMesh || !ArmsMesh->GetSkeleton() || !FallbackIdle || FallbackIdle->GetSkeleton() != ArmsMesh->GetSkeleton())
    {
        UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_INPUT_INVALID arms=%s idle=%s"), *GetNameSafe(ArmsMesh), *GetNameSafe(FallbackIdle));
        return false;
    }
    USkeleton* Skeleton = ArmsMesh->GetSkeleton(); // 只新增Slot，保留模板骨架参考姿势与已有动画兼容性。
    Skeleton->Modify();
    Skeleton->RegisterSlotNode(TEXT("FPAction"));
    Skeleton->SetSlotGroupName(TEXT("FPAction"),TEXT("DefaultGroup"));
    Skeleton->MarkPackageDirty();
    UAnimBlueprint* Interface = DemoFPAuthoring::Blueprint(TEXT("ALI_FPWeaponLayers"),nullptr,ArmsMesh,true); // 无骨架接口，函数本身只声明姿势输出。
    if (!Interface) return false;
    UEdGraph* InterfaceGraph = DemoFPAuthoring::FindGraph(Interface,DemoFPAuthoring::LayerName); // 重跑保留函数GUID，维持实现图映射。
    if (!InterfaceGraph)
    {
        InterfaceGraph = FBlueprintEditorUtils::CreateNewGraph(Interface,DemoFPAuthoring::LayerName,UAnimationGraph::StaticClass(),UAnimationGraphSchema::StaticClass());
        FBlueprintEditorUtils::AddDomainSpecificGraph(Interface,InterfaceGraph);
    }
    // 接口只保留Root签名节点；运行时实现由主图fallback或公共武器层提供。
    DemoFPAuthoring::ClearGraph(InterfaceGraph);
    UAnimGraphNode_Root* InterfaceRoot = DemoFPAuthoring::Node<UAnimGraphNode_Root>(InterfaceGraph,0,0); // 公共组令该接口实例统一管理。
    InterfaceRoot->Node.SetGroup(TEXT("FPWeaponLayers"));
    if (!DemoFPAuthoring::Compile(Interface)) return false;
    UAnimBlueprint* Base = DemoFPAuthoring::Blueprint(TEXT("ABP_FPWeaponLayers_Base"),UDemoWeaponLayerAnimInstance::StaticClass(),ArmsMesh); // 唯一含公共武器图的父类。
    UAnimBlueprint* Main = DemoFPAuthoring::Blueprint(TEXT("ABP_FPArms_Base"),UDemoFPAnimInstance::StaticClass(),ArmsMesh); // Mesh1P终身保持的主类。
    if (!Base || !Main || !DemoFPAuthoring::EnsureInterface(Base,Interface) || !DemoFPAuthoring::EnsureInterface(Main,Interface)) return false;
    if (!DemoFPAuthoring::BuildPose(DemoFPAuthoring::FindGraph(Base,DemoFPAuthoring::LayerName),FallbackIdle,true)
        || !DemoFPAuthoring::BuildPose(DemoFPAuthoring::FindGraph(Base,TEXT("AnimGraph")),FallbackIdle,true)
        || !DemoFPAuthoring::Compile(Base)) return false;
    UDemoWeaponLayerAnimInstance* BaseDefaults = Cast<UDemoWeaponLayerAnimInstance>(Base->GeneratedClass->GetDefaultObject()); // 公共层编辑器预览使用合法fallback资源。
    BaseDefaults->AnimSet.IdlePose = FallbackIdle;
    Base->MarkPackageDirty();
    if (!DemoFPAuthoring::BuildPose(DemoFPAuthoring::FindGraph(Main,DemoFPAuthoring::LayerName),FallbackIdle,true)
        || !DemoFPAuthoring::BuildMain(Main,Interface,Base) || !DemoFPAuthoring::Compile(Main)) return false;
    const TCHAR* WeaponNames[] = { TEXT("Pistol"),TEXT("Rifle"),TEXT("Shotgun"),TEXT("Sniper") }; // 四配置子类名称与武器定义约定一致。
    for (const TCHAR* WeaponName : WeaponNames) // 子类只继承图；不复制接口实现，也不加入EventGraph游戏逻辑。
    {
        UAnimBlueprint* Child = DemoFPAuthoring::Blueprint(FString(TEXT("ABP_FP_")) + WeaponName,Base->GeneratedClass,ArmsMesh); // Blueprint包持有，脚本随后填写AnimSet。
        if (!Child || !DemoFPAuthoring::Compile(Child)) return false;
    }
    UE_LOG(LogFPSDemo, Display, TEXT("FP_ANIMATION_GRAPHS_READY: ALI, fixed main FPAction/Inertialization, shared dynamic layer and four config children"));
    return true;
}

bool UDemoFPAnimationAuthoring::ConfigureLayer(const FString& WeaponName, const FDemoWeaponAnimationSet& AnimSet)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] UDemoFPAnimationAuthoring::ConfigureLayer %s"), *WeaponName);
    if (WeaponName != TEXT("Pistol") && WeaponName != TEXT("Rifle") && WeaponName != TEXT("Shotgun") && WeaponName != TEXT("Sniper"))
    {
        UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_CONFIG_INVALID_NAME %s"), *WeaponName);
        return false;
    }
    const FString Name = FString(TEXT("ABP_FP_")) + WeaponName; // 白名单决定唯一可写目标。
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr,*(DemoFPAuthoring::Directory + Name + TEXT(".") + Name)); // 已构建的配置子资产。
    UDemoWeaponLayerAnimInstance* Defaults = Blueprint && Blueprint->GeneratedClass
        ? Cast<UDemoWeaponLayerAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr; // 写默认配置，不改正在运行的实例。
    if (!Defaults || !AnimSet.IdlePose || AnimSet.IdlePose->GetSkeleton() != Blueprint->TargetSkeleton)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("FP_GRAPH_CONFIG_INVALID asset=%s idle=%s"), *GetNameSafe(Blueprint), *GetNameSafe(AnimSet.IdlePose));
        return false;
    }
    Blueprint->Modify();
    Defaults->Modify();
    Defaults->AnimSet = AnimSet;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Blueprint->MarkPackageDirty();
    return Blueprint->Status != BS_Error;
}
