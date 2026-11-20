// 职责：对隔离迁入的Lyra蓝图执行可复核父类适配；不修改Lyra源工程、不自动切正式引用。
#include "Animation/DemoLyraAuthoring.h"
#include "Animation/DemoLyraAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "UObject/CoreRedirects.h"
#include "UObject/UnrealType.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Debug/DemoLog.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "EdGraphUtilities.h"
#include "AnimationGraphSchema.h"
#include "K2Node_CallFunction.h" // 候选图显式使用蓝图函数/变量节点，设计者可在图内追踪。
#include "K2Node_VariableGet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_FunctionResult.h" // 只扩展原生纯资格函数返回值，进入/退出仍由原状态机复用。
#include "AnimGraphNode_StateMachine.h" // 精确找到Ready源函数的IdleSM，不修改编译中间图。
#include "AnimationStateMachineGraph.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateNodeBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "Kismet/KismetMathLibrary.h"
#include "Serialization/ArchiveReplaceObjectRef.h" // 迁入图的AnimGetter仍可能序列化旧原生父类，需显式替换再编译。
#include "Animation/AnimGraphNode_DemoWeaponAnchor.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_LayeredBoneBlend.h"

namespace DemoLyraAuthoring
{
    /** 同步借用主图，不处理接口子图或事件图。 */
    UEdGraph* MainGraph(UAnimBlueprint* Blueprint)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] LyraAuthoring::MainGraph"));
        if(Blueprint) for(UEdGraph* Graph:Blueprint->FunctionGraphs) if(Graph->GetFName()==TEXT("AnimGraph")) return Graph;
        return nullptr;
    }
    /** 唯一主输出节点；图所有权保持不变。 */
    UAnimGraphNode_Root* Root(UEdGraph* Graph)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] LyraAuthoring::Root"));
        if(Graph) for(UEdGraphNode* Node:Graph->Nodes) if(auto* Found=Cast<UAnimGraphNode_Root>(Node)) return Found;
        return nullptr;
    }
    /** 反向收集现有武器末端子图；地面Cache是截断边界，使用者仍需包含对应缓存节点。 */
    void CollectOverlay(UEdGraphNode* Node,TSet<UObject*>& Result)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] LyraAuthoring::CollectOverlay"));
        if(!Node || Result.Contains(Node)) return;
        Result.Add(Node);
        if(auto* Cache=Cast<UAnimGraphNode_SaveCachedPose>(Node)) if(Cache->CacheName==TEXT("DirectionalGroundCache")) return;
        if(auto* Use=Cast<UAnimGraphNode_UseCachedPose>(Node)) CollectOverlay(Use->SaveCachedPoseNode.Get(),Result);
        for(UEdGraphPin* Pin:Node->Pins) if(Pin && Pin->Direction==EGPD_Input)
            for(UEdGraphPin* Source:Pin->LinkedTo) CollectOverlay(Source->GetOwningNode(),Result);
    }
}

bool UDemoLyraAuthoring::InstallMeleeUpperBodySlot(UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); // 静态作者函数没有this，不能使用面向UObject实例的DEMO_LOG_CALL宏。
    UEdGraph* Graph=DemoLyraAuthoring::MainGraph(Blueprint); // 同步借用目标图，不复制或重建Lyra状态机。
    if (!Graph || Blueprint->GetPathName()!=TEXT("/Game/Characters/Heroes/Mannequin/Animations/ABP_Mannequin_Base.ABP_Mannequin_Base"))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_SLOT_GRAPH_REJECT")); return false; }
    UAnimGraphNode_DemoWeaponAnchor* Anchor=nullptr; // 上身映射前的唯一完整源输入，防止Slot输出绕过root/pelvis过滤。
    UAnimGraphNode_Slot* Slot=nullptr; bool bUpperFilter=false; // 稳定Slot名确保幂等，已有多个同名节点拒绝。
    for (UEdGraphNode* Node:Graph->Nodes)
    {
        if (auto* Found=Cast<UAnimGraphNode_DemoWeaponAnchor>(Node)) { if (Anchor) return false; Anchor=Found; }
        if (auto* Found=Cast<UAnimGraphNode_Slot>(Node); Found && Found->Node.SlotName==TEXT("DemoMeleeUpperBody")) { if (Slot) return false; Slot=Found; }
        if (auto* Layer=Cast<UAnimGraphNode_LayeredBoneBlend>(Node))
            for (const FInputBlendPose& Setup:Layer->Node.LayerSetup)
                for (const FBranchFilter& Filter:Setup.BranchFilters)
                    if (Filter.BoneName==TEXT("spine_01") && Filter.BlendDepth==0) bUpperFilter=true;
    }
    UEdGraphPin* Input=Anchor?Anchor->FindPin(TEXT("Upper")):nullptr;
    if (!bUpperFilter || !Input || Input->LinkedTo.Num()!=1)
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_SLOT_UPPER_CONTRACT_MISSING")); return false; }
    Blueprint->Modify();
    if (!Slot)
    {
        UEdGraphPin* Previous=Input->LinkedTo[0]; // 插入单个节点，保留全部既有Pose过渡与缓存；Pose引脚不可一接多。
        FGraphNodeCreator<UAnimGraphNode_Slot> Creator(*Graph); Slot=Creator.CreateNode();
        Slot->Node.SlotName=TEXT("DemoMeleeUpperBody"); Slot->Node.bAlwaysUpdateSourcePose=true;
        Slot->NodePosX=Anchor->NodePosX-220; Slot->NodePosY=Anchor->NodePosY;
        Slot->NodeComment=TEXT("Melee Montage -> anchor -> spine_01 layer; legs stay Lyra"); Creator.Finalize();
        Input->BreakLinkTo(Previous);
        if (!Graph->GetSchema()->TryCreateConnection(Previous,Slot->FindPin(TEXT("Source")))
            || !Graph->GetSchema()->TryCreateConnection(Slot->FindPin(TEXT("Pose")),Input))
        { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_SLOT_LINK_FAILED")); return false; }
    }
    if (Input->LinkedTo[0]->GetOwningNode()!=Slot) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status==BS_Error || !Blueprint->GeneratedClass) return false;
    UDemoLyraAnimInstance* Defaults=Cast<UDemoLyraAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject());
    if (!Defaults) return false;
    Blueprint->MarkPackageDirty(); // 安装节点不自动改变正式CDO开关；候选入口在实例初始化时显式启用。
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_SLOT_INSTALLED graph=%s spine_root=spine_01 root_motion=excluded"),*Blueprint->GetName()); return true;
}

bool UDemoLyraAuthoring::ConnectWeaponReadyPose(UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Blueprint || !Blueprint->GetPathName().StartsWith(TEXT("/Game/Player/LyraReady/")))
    { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_READY_WHITELIST_REJECT")); return false; }
    UEdGraph* Graph=nullptr; // 候选父图的原生权重更新函数，不修改其它状态/动画选片。
    for (UEdGraph* Item:Blueprint->FunctionGraphs) if (Item->GetName()==TEXT("Update Blend Weight Data")) Graph=Item;
    UK2Node_IfThenElse* Branch=Graph?FindObject<UK2Node_IfThenElse>(Graph,TEXT("K2Node_IfThenElse_0")):nullptr;
    UFunction* Getter=Blueprint->GeneratedClass?Blueprint->GeneratedClass->FindFunctionByName(TEXT("GetMainAnimBPThreadSafe")):nullptr;
    if (!Graph || !Branch || !Getter || Branch->GetConditionPin()->LinkedTo.Num()!=1)
    { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_READY_GRAPH_CONTRACT_MISSING")); return false; }
    for (UEdGraphNode* Node:Graph->Nodes) if (Node->NodeComment==TEXT("DemoWeaponReady: native HipFire pose"))
    { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_READY_ALREADY_CONNECTED")); return false; }
    Blueprint->Modify();
    UEdGraphPin* Previous=Branch->GetConditionPin()->LinkedTo[0]; // 原生开火计时/曲线条件仍保留为OR的一侧。
    UK2Node_CallFunction* Main=NewObject<UK2Node_CallFunction>(Graph); // 纯线程安全Getter，只访问原图缓存。
    Graph->AddNode(Main,false,false); Main->CreateNewGuid(); Main->SetFromFunction(Getter); Main->AllocateDefaultPins();
    UK2Node_VariableGet* Ready=NewObject<UK2Node_VariableGet>(Graph); // 读取主图继承的游戏线程快照，不访问Pawn。
    Graph->AddNode(Ready,false,false); Ready->CreateNewGuid();
    Ready->VariableReference.SetExternalMember(GET_MEMBER_NAME_CHECKED(UDemoLyraAnimInstance,bWeaponReadyLocomotion),UDemoLyraAnimInstance::StaticClass());
    Ready->AllocateDefaultPins();
    UK2Node_CallFunction* Either=NewObject<UK2Node_CallFunction>(Graph); // 只扩展举枪条件，不伪造Firing/ADS标签。
    Graph->AddNode(Either,false,false); Either->CreateNewGuid();
    Either->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BooleanOR"))); Either->AllocateDefaultPins();
    Main->NodePosX=Branch->NodePosX-700; Main->NodePosY=Branch->NodePosY-300;
    Ready->NodePosX=Branch->NodePosX-450; Ready->NodePosY=Branch->NodePosY-300;
    Either->NodePosX=Branch->NodePosX-200; Either->NodePosY=Branch->NodePosY-150;
    Either->NodeComment=TEXT("DemoWeaponReady: native HipFire pose"); Either->bCommentBubbleVisible=true;
    Branch->GetConditionPin()->BreakAllPinLinks();
    const UEdGraphSchema* Schema=Graph->GetSchema(); // 依照图类型校验连接，不手写Pin链表。
    const bool bConnected=Schema->TryCreateConnection(Main->GetReturnValuePin(),Ready->FindPin(UEdGraphSchema_K2::PN_Self))
        && Schema->TryCreateConnection(Ready->GetValuePin(),Either->FindPin(TEXT("A")))
        && Schema->TryCreateConnection(Previous,Either->FindPin(TEXT("B")))
        && Schema->TryCreateConnection(Either->GetReturnValuePin(),Branch->GetConditionPin());
    if (!bConnected) { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_READY_PIN_CONNECT_FAILED; candidate must not be saved")); return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const bool bValid=Blueprint->Status!=BS_Error && Blueprint->GeneratedClass; // 只有真实编译成功才允许调用者保存。
    UE_LOG(LogFPSDemo,Log,TEXT("LYRA_READY_GRAPH_COMPILED valid=%d"),bValid); return bValid;
}

bool UDemoLyraAuthoring::ConnectNativeIdleBreakGate(UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Blueprint || Blueprint->GetPathName()!=TEXT("/Game/Player/LyraReady/ABP_Ready_ItemBase.ABP_Ready_ItemBase"))
    { UE_LOG(LogFPSDemo,Error,TEXT("NATIVE_IDLE_GATE_WHITELIST_REJECT")); return false; }
    UEdGraph* Graph=nullptr; // 原生纯线程安全资格函数，原图同时从进入和提前退出转移调用。
    for (UEdGraph* Item:Blueprint->FunctionGraphs) if (Item->GetName()==TEXT("CanPlayIdleBreak")) Graph=Item;
    UK2Node_FunctionResult* Result=nullptr; int32 Results=0; // 明确单一Return合约，未知图形拒绝而非猜测改线。
    UK2Node_CallFunction* Existing=nullptr; // 幂等标识仅接受本次唯一AND，不重复套门。
    if (Graph) for (UEdGraphNode* Node:Graph->Nodes)
    {
        if (auto* Found=Cast<UK2Node_FunctionResult>(Node)) { Result=Found; ++Results; }
        if (Node->NodeComment==TEXT("DemoNativeIdleBreak: shared eligibility and early exit")) Existing=Cast<UK2Node_CallFunction>(Node);
    }
    UEdGraphPin* Return=Result?Result->FindPin(TEXT("ReturnValue")):nullptr; // 原结果输入；原表达式完整保留为AND一侧。
    UFunction* Getter=Blueprint->GeneratedClass?Blueprint->GeneratedClass->FindFunctionByName(TEXT("GetMainAnimBPThreadSafe")):nullptr;
    UAnimStateTransitionNode* Entry=nullptr; int32 Entries=0; // 原进入只有timer<=0，必须显式加资格，不能依赖可能为0的原Reset延迟。
    TArray<UEdGraph*> SourceGraphs; Blueprint->GetAllGraphs(SourceGraphs); // FullBody_IdleState是接口层图，不能仅查FunctionGraphs遗漏它。
    for (UEdGraph* Item:SourceGraphs) if (Item->GetOuter()==Blueprint && Item->GetName()==TEXT("FullBody_IdleState"))
        for (UEdGraphNode* Node:Item->Nodes) if (auto* Machine=Cast<UAnimGraphNode_StateMachine>(Node); Machine && Machine->EditorStateMachineGraph && Machine->EditorStateMachineGraph->GetName()==TEXT("IdleSM"))
            for (UEdGraphNode* State:Machine->EditorStateMachineGraph->Nodes) if (auto* Transition=Cast<UAnimStateTransitionNode>(State))
                if (Transition->GetPreviousState() && Transition->GetNextState() && Transition->GetPreviousState()->GetStateName()==TEXT("Idle") && Transition->GetNextState()->GetStateName()==TEXT("IdleBreak")) { Entry=Transition; ++Entries; }
    UAnimGraphNode_TransitionResult* EntryResult=nullptr; int32 EntryResults=0; // 唯一原timer布尔返回；不重做状态、切换优先级或混合时长。
    UK2Node_CallFunction* ExistingEntry=nullptr;
    if (Entry && Entry->BoundGraph) for (UEdGraphNode* Node:Entry->BoundGraph->Nodes)
    {
        if (auto* Found=Cast<UAnimGraphNode_TransitionResult>(Node)) { EntryResult=Found; ++EntryResults; }
        if (Node->NodeComment==TEXT("DemoNativeIdleBreak: timer AND shared eligibility")) ExistingEntry=Cast<UK2Node_CallFunction>(Node);
    }
    UEdGraphPin* EntryReturn=EntryResult?EntryResult->FindPin(TEXT("bCanEnterTransition")):nullptr; // 原timer<=0结果保留。
    UFunction* CanPlay=Blueprint->GeneratedClass?Blueprint->GeneratedClass->FindFunctionByName(TEXT("CanPlayIdleBreak")):nullptr;
    if (!Graph || Results!=1 || !Return || Return->LinkedTo.Num()!=1 || !Getter || Entries!=1 || EntryResults!=1 || !EntryReturn || EntryReturn->LinkedTo.Num()!=1 || !CanPlay)
    { UE_LOG(LogFPSDemo,Error,TEXT("NATIVE_IDLE_GATE_GRAPH_CONTRACT_MISSING results=%d"),Results); return false; }
    if (Existing || ExistingEntry)
    {
        const bool bValid=Existing && ExistingEntry && Return->LinkedTo[0]->GetOwningNode()==Existing && Existing->FunctionReference.GetMemberName()==TEXT("BooleanAND") && EntryReturn->LinkedTo[0]->GetOwningNode()==ExistingEntry && ExistingEntry->FunctionReference.GetMemberName()==TEXT("BooleanAND"); // 任一半装拒绝，不让缺失入口被幂等短路。
        UE_LOG(LogFPSDemo,Log,TEXT("NATIVE_IDLE_GATE_ALREADY_CONNECTED valid=%d"),bValid); return bValid;
    }
    Blueprint->Modify();
    UEdGraphPin* Previous=Return->LinkedTo[0]; // 保存原CanPlay全部姿态/标签/动画数组条件，绝不短接为单一Demo开关。
    UK2Node_CallFunction* Main=NewObject<UK2Node_CallFunction>(Graph); // 原有线程安全Getter，仅访问主图缓存。
    Graph->AddNode(Main,false,false); Main->CreateNewGuid(); Main->SetFromFunction(Getter); Main->AllocateDefaultPins();
    UK2Node_VariableGet* Allow=NewObject<UK2Node_VariableGet>(Graph); // NativeUpdate游戏线程先发布，工作线程只读纯值。
    Graph->AddNode(Allow,false,false); Allow->CreateNewGuid();
    Allow->VariableReference.SetExternalMember(GET_MEMBER_NAME_CHECKED(UDemoLyraAnimInstance,bAllowNativeIdleBreak),UDemoLyraAnimInstance::StaticClass()); Allow->AllocateDefaultPins();
    UK2Node_CallFunction* Both=NewObject<UK2Node_CallFunction>(Graph); // 一个函数共享进入与!CanPlay退出，不添加重复状态或改变转移时间。
    Graph->AddNode(Both,false,false); Both->CreateNewGuid(); Both->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BooleanAND"))); Both->AllocateDefaultPins();
    Main->NodePosX=Result->NodePosX-650; Main->NodePosY=Result->NodePosY+220;
    Allow->NodePosX=Result->NodePosX-420; Allow->NodePosY=Result->NodePosY+220;
    Both->NodePosX=Result->NodePosX-200; Both->NodePosY=Result->NodePosY;
    Both->NodeComment=TEXT("DemoNativeIdleBreak: shared eligibility and early exit"); Both->bCommentBubbleVisible=true;
    const UEdGraphSchema* Schema=Graph->GetSchema(); Return->BreakAllPinLinks(); // 仅一个返回边，全部连接通过图Schema校验。
    const bool bConnected=Schema->TryCreateConnection(Main->GetReturnValuePin(),Allow->FindPin(UEdGraphSchema_K2::PN_Self))
        && Schema->TryCreateConnection(Allow->GetValuePin(),Both->FindPin(TEXT("A")))
        && Schema->TryCreateConnection(Previous,Both->FindPin(TEXT("B")))
        && Schema->TryCreateConnection(Both->GetReturnValuePin(),Return);
    if (!bConnected) { UE_LOG(LogFPSDemo,Error,TEXT("NATIVE_IDLE_GATE_PIN_CONNECT_FAILED; do not save")); return false; }
    UEdGraph* EntryGraph=Entry->BoundGraph; // 只改原Idle→IdleBreak转移的布尔边，原0.4s仍由引擎处理。
    UEdGraphPin* PreviousTimer=EntryReturn->LinkedTo[0]; // 包含原延迟条件，不改TimeUntilNextIdleBreak数值。
    UK2Node_CallFunction* CanEnter=NewObject<UK2Node_CallFunction>(EntryGraph);
    EntryGraph->AddNode(CanEnter,false,false); CanEnter->CreateNewGuid(); CanEnter->SetFromFunction(CanPlay); CanEnter->AllocateDefaultPins(); // 原纯资格函数保留所有原条件及本次主图门。
    UK2Node_CallFunction* TimerAndGate=NewObject<UK2Node_CallFunction>(EntryGraph);
    EntryGraph->AddNode(TimerAndGate,false,false); TimerAndGate->CreateNewGuid(); TimerAndGate->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("BooleanAND"))); TimerAndGate->AllocateDefaultPins();
    CanEnter->NodePosX=EntryResult->NodePosX-420; CanEnter->NodePosY=EntryResult->NodePosY+180;
    TimerAndGate->NodePosX=EntryResult->NodePosX-200; TimerAndGate->NodePosY=EntryResult->NodePosY;
    TimerAndGate->NodeComment=TEXT("DemoNativeIdleBreak: timer AND shared eligibility"); TimerAndGate->bCommentBubbleVisible=true;
    EntryReturn->BreakAllPinLinks(); const UEdGraphSchema* EntrySchema=EntryGraph->GetSchema(); // Schema负责合法连接，不手写链接数组。
    const bool bEntryConnected=EntrySchema->TryCreateConnection(PreviousTimer,TimerAndGate->FindPin(TEXT("A")))
        && EntrySchema->TryCreateConnection(CanEnter->GetReturnValuePin(),TimerAndGate->FindPin(TEXT("B")))
        && EntrySchema->TryCreateConnection(TimerAndGate->GetReturnValuePin(),EntryReturn);
    if (!bEntryConnected) { UE_LOG(LogFPSDemo,Error,TEXT("NATIVE_IDLE_GATE_ENTRY_CONNECT_FAILED; do not save")); return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const bool bValid=Blueprint->Status!=BS_Error && Blueprint->GeneratedClass; // 真实编译后才交给Python保存，函数本身不保存。
    UE_LOG(LogFPSDemo,Log,TEXT("NATIVE_IDLE_GATE_GRAPH_COMPILED valid=%d original_conditions_preserved=1 original_exit_reused=1"),bValid); return bValid;
}

bool UDemoLyraAuthoring::CompileNativeIdleBreakDependent(UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    const TSet<FString> Allowed={TEXT("ABP_Ready_PistolFamily"),TEXT("ABP_Ready_RifleFamily"),TEXT("ABP_Ready_ShotgunFamily"),TEXT("ABP_Ready_pistol"),TEXT("ABP_Ready_rifle"),TEXT("ABP_Ready_shotgun"),TEXT("ABP_Ready_sniper")}; // 闭包精确白名单，不递归改全项目动画。
    if (!Blueprint || !Allowed.Contains(Blueprint->GetName()) || Blueprint->GetPathName()!=TEXT("/Game/Player/LyraReady/")+Blueprint->GetName()+TEXT(".")+Blueprint->GetName())
    { UE_LOG(LogFPSDemo,Error,TEXT("NATIVE_IDLE_DEPENDENT_WHITELIST_REJECT")); return false; }
    FKismetEditorUtilities::CompileBlueprint(Blueprint); // 父图编译后刷新派生字节码，不Reparent/重置CDO/改资源选片。
    const bool bValid=Blueprint->Status!=BS_Error && Blueprint->GeneratedClass;
    UE_LOG(LogFPSDemo,Log,TEXT("NATIVE_IDLE_DEPENDENT_COMPILED valid=%d asset=%s"),bValid,*Blueprint->GetPathName()); return bValid;
}

void UDemoLyraAuthoring::PrepareImport()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    TArray<FCoreRedirect> Redirects; // 仅本进程导入兼容，不写项目CoreRedirects配置。
    Redirects.Emplace(ECoreRedirectFlags::Type_Class,TEXT("/Script/LyraGame.LyraAnimInstance"),TEXT("/Script/FPSDemoEditor.DemoLyraImportInstance"));
    FCoreRedirects::AddRedirectList(Redirects,TEXT("DemoLyraCandidateImport"));
}

bool UDemoLyraAuthoring::AdaptBase(UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if(!Blueprint || !Blueprint->GetPathName().StartsWith(TEXT("/Game/Characters/Heroes/Mannequin/Animations/ABP_Mannequin_Base")))
    { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_ADAPT_WHITELIST_REJECT")); return false; }
    Blueprint->Modify();
    // 原图AimPitch与当前Demo瞄准缓存语义不同，改原图变量及其引用，禁止悄悄复用同名值。
    FBlueprintEditorUtils::RenameMemberVariable(Blueprint,TEXT("AimPitch"),TEXT("LyraAimPitch"));
    Blueprint->ParentClass=UDemoLyraAnimInstance::StaticClass();
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const bool Valid=Blueprint->Status!=BS_Error && Blueprint->GeneratedClass; // 真实编译结果，缺类或属性不能当作通过。
    if(Valid) { UE_LOG(LogFPSDemo,Log,TEXT("LYRA_ADAPT_COMPILE valid=1 class=%s"),*GetNameSafe(Blueprint->GeneratedClass)); }
    else { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_ADAPT_COMPILE valid=0 class=%s"),*GetNameSafe(Blueprint->GeneratedClass)); }
    return Valid;
}

bool UDemoLyraAuthoring::ValidateSkeletonAndDeclareCompatibility(USkeleton* Imported,USkeleton* Current)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if(!Imported || !Current || !Imported->GetPathName().StartsWith(TEXT("/Game/Characters/Heroes/Mannequin/"))) return false;
    const FReferenceSkeleton& A=Imported->GetReferenceSkeleton(); // 候选骨架拥有，只读原始参考姿态。
    const FReferenceSkeleton& B=Current->GetReferenceSkeleton(); // 正式骨架仅读，不改变绑定或骨长。
    float MaxPosition=0.f,MaxAngle=0.f,MaxScale=0.f; // cm、度、无量纲；按共有骨名对比，不按索引假定一致。
    int32 Common=0; // 虚拟骨另行由候选图使用，原始皮肤骨不能缺失或改父链。
    for(int32 Index=0;Index<B.GetRawBoneNum();++Index)
    {
        const FName Name=B.GetBoneName(Index);
        const int32 Other=A.FindRawBoneIndex(Name);
        if(Other==INDEX_NONE) { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_SKELETON_MISSING %s"),*Name.ToString()); return false; }
        const int32 ParentA=A.GetRawParentIndex(Other),ParentB=B.GetRawParentIndex(Index);
        if((ParentA==INDEX_NONE?NAME_None:A.GetBoneName(ParentA))!=(ParentB==INDEX_NONE?NAME_None:B.GetBoneName(ParentB)))
        { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_SKELETON_PARENT_MISMATCH %s"),*Name.ToString()); return false; }
        const FTransform& PoseA=A.GetRawRefBonePose()[Other]; const FTransform& PoseB=B.GetRawRefBonePose()[Index];
        MaxPosition=FMath::Max(MaxPosition,static_cast<float>(FVector::Distance(PoseA.GetTranslation(),PoseB.GetTranslation())));
        MaxAngle=FMath::Max(MaxAngle,static_cast<float>(FMath::RadiansToDegrees(PoseA.GetRotation().AngularDistance(PoseB.GetRotation()))));
        MaxScale=FMath::Max(MaxScale,static_cast<float>((PoseA.GetScale3D()-PoseB.GetScale3D()).GetAbsMax())); ++Common;
    }
    UE_LOG(LogFPSDemo,Display,TEXT("LYRA_SKELETON_REFERENCE common=%d position_cm=%.8f angle_deg=%.8f scale=%.8f"),Common,MaxPosition,MaxAngle,MaxScale);
    if(MaxPosition>.02f || MaxAngle>.05f || MaxScale>.0001f)
    { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_SKELETON_REFERENCE_REJECT needs explicit retargeting")); return false; }
    Imported->Modify(); Imported->AddCompatibleSkeleton(Current); Imported->MarkPackageDirty();
    return true;
}

bool UDemoLyraAuthoring::InstallWeaponOverlay(UAnimBlueprint* Native,UAnimBlueprint* Current)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    using namespace DemoLyraAuthoring;
    UEdGraph* Destination=MainGraph(Native); UEdGraph* Template=MainGraph(Current); // 只修改Destination。
    UAnimGraphNode_Root* NewRoot=Root(Destination); UAnimGraphNode_Root* OldRoot=Root(Template);
    if(!NewRoot || !OldRoot || Native==Current || !Native->ParentClass->IsChildOf(UDemoLyraAnimInstance::StaticClass())) return false;
    UEdGraphPin* NativeInput=NewRoot->FindPin(TEXT("Result")); UEdGraphPin* OldInput=OldRoot->FindPin(TEXT("Result"));
    if(!NativeInput || !OldInput || NativeInput->LinkedTo.Num()!=1 || OldInput->LinkedTo.Num()!=1) return false;
    for(UEdGraphNode* Node:Destination->Nodes) if(Node->NodeComment==TEXT("DemoNativeLyraWeaponOverlay"))
    { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_OVERLAY_ALREADY_INSTALLED")); return false; }
    UEdGraphPin* NativePose=NativeInput->LinkedTo[0]; // 原生输出仍保留整个状态机及脚步层。
    UEdGraphPin* OldPose=OldInput->LinkedTo[0];
    const FGuid FinalGuid=OldPose->GetOwningNode()->NodeGuid; const FName FinalPin=OldPose->PinName;
    TSet<UObject*> Selected; CollectOverlay(OldPose->GetOwningNode(),Selected); // 剪贴复制对象白名单，不改正式模板。
    FString Text; FEdGraphUtilities::ExportNodesToText(Selected,Text);
    Native->Modify(); TSet<UEdGraphNode*> Imported; FEdGraphUtilities::ImportNodesFromText(Destination,Text,Imported);
    UEdGraphNode* Final=nullptr; UAnimGraphNode_SaveCachedPose* GroundCache=nullptr; // 导入后按GUID/缓存名找契约点。
    for(UEdGraphNode* Node:Imported)
    {
        if(Node->NodeGuid==FinalGuid) Final=Node;
        if(auto* Cache=Cast<UAnimGraphNode_SaveCachedPose>(Node)) if(Cache->CacheName==TEXT("DirectionalGroundCache")) GroundCache=Cache;
        Node->CreateNewGuid(); Node->NodePosX+=2200; // 避免GUID碰撞，并在图中把武器末端放在原生移动右侧。
    }
    if(!Final || !GroundCache) { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_OVERLAY_CONTRACT_MISSING count=%d"),Imported.Num()); return false; }
    for(UEdGraphNode* Node:Imported) if(auto* Use=Cast<UAnimGraphNode_UseCachedPose>(Node))
    {
        const FStrProperty* CacheName=FindFProperty<FStrProperty>(Use->GetClass(),TEXT("NameOfCache")); // UE5.4缓存名为私有可序列化属性，使用反射核对。
        if((Use->SaveCachedPoseNode.IsValid() && Use->SaveCachedPoseNode->CacheName==TEXT("DirectionalGroundCache"))
           || (CacheName && CacheName->GetPropertyValue_InContainer(Use)==TEXT("DirectionalGroundCache"))) Use->SaveCachedPoseNode=GroundCache;
    }
    NativeInput->BreakAllPinLinks(); GroundCache->FindPin(TEXT("Pose"))->BreakAllPinLinks();
    if(!Destination->GetSchema()->TryCreateConnection(NativePose,GroundCache->FindPin(TEXT("Pose")))
       || !Destination->GetSchema()->TryCreateConnection(Final->FindPin(FinalPin),NativeInput)) return false;
    Final->NodeComment=TEXT("DemoNativeLyraWeaponOverlay");
    // 原Lyra的瞄准由Demo最后的公共胸部/枪框架接管；不能同时累计两次AimOffset。
    for(UEdGraphNode* Node:Destination->Nodes) if(auto* Layer=Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
        if(Layer->Node.Layer==TEXT("FullBody_Aiming"))
        {
            UEdGraphPin* Input=nullptr; UEdGraphPin* Output=Layer->FindPin(TEXT("Pose"));
            for(UEdGraphPin* Pin:Layer->Pins) if(Pin->Direction==EGPD_Input && Pin->PinType.PinSubCategoryObject==FPoseLink::StaticStruct()) Input=Pin;
            if(Input && Output && Input->LinkedTo.Num()==1)
            {
                UEdGraphPin* Before=Input->LinkedTo[0]; TArray<UEdGraphPin*> After=Output->LinkedTo;
                Input->BreakAllPinLinks(); Output->BreakAllPinLinks();
                for(UEdGraphPin* Pin:After) if(!Destination->GetSchema()->TryCreateConnection(Before,Pin)) return false;
                Layer->NodeComment=TEXT("Demo owns final aim; native aiming bypassed");
            }
            else { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_AIM_BYPASS_MISSING")); return false; }
        }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Native); FKismetEditorUtilities::CompileBlueprint(Native);
    const bool Valid=Native->Status!=BS_Error && Native->GeneratedClass;
    UE_LOG(LogFPSDemo,Log,TEXT("LYRA_OVERLAY_COMPILE valid=%d imported=%d"),Valid,Imported.Num()); return Valid;
}

bool UDemoLyraAuthoring::FinalizeImportedGraph(UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if(!Blueprint || (!Blueprint->GetPathName().StartsWith(TEXT("/Game/Characters/Heroes/Mannequin/"))
        && !Blueprint->GetPathName().StartsWith(TEXT("/Game/Player/LyraNative/"))))
    { UE_LOG(LogFPSDemo,Error,TEXT("LYRA_FINALIZE_WHITELIST_REJECT")); return false; }
    Blueprint->Modify();
    TMap<UObject*,UObject*> Replacements; // 只替换已知导入占位类，不修改任何外部正式资源。
    Replacements.Add(UDemoLyraImportInstance::StaticClass(),UDemoLyraAnimInstance::StaticClass());
    FArchiveReplaceObjectRef<UObject> Replace(Blueprint,Replacements,EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const bool Valid=Blueprint->Status!=BS_Error && Blueprint->GeneratedClass; // 真实编译失败不能写入候选。
    UE_LOG(LogFPSDemo,Log,TEXT("LYRA_FINALIZE graph=%s replacements=%d valid=%d"),*Blueprint->GetName(),Replace.GetCount(),Valid);
    if(Valid) Blueprint->MarkPackageDirty();
    return Valid;
}
