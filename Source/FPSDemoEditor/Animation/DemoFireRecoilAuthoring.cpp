// 职责：仅为FireRecoil20261108候选及新正式闭包配置真实UE动画；主图只允许添加唯一惰性枪后坐Slot。
// 使用同名头公开静态Editor API；120HzRAW数据由Blender源写入，当前Hold0是增量参考，调用方独占备份/保存/验证事务。
// Slot位于最终操作层之后、作者Aim之前；最终Aim如何保留动态差量由独立运行时实现，不能把本工具接线当成整体验收。
#include "Animation/DemoFireRecoilAuthoring.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/DemoPlayerAppearance.h" // 基准读取当前正式每枪Hold，不因制作器名称假定仍绑历史资源。
#include "Animation/AnimGraphNode_DemoPlayerAim.h"
#include "Animation/AnimGraphNode_DemoPlayerGrounding.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Slot.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h" // Skeleton预览网格的完整类型，仅只读身份检查。
#include "Dom/JsonObject.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Debug/DemoLog.h"

namespace DemoFireRecoilAuthoring
{
    const FString CandidateRoot=TEXT("/Game/Player/Candidates/FireRecoil20261108/"); // 本修订候选唯一输出，不放开其他Player制作目录。
    const FString FormalRoot=TEXT("/Game/Player/FireRecoil20261108/"); // 晋升只创建本修订新包，旧Hold/Fire源保持可回退。
    const FName SlotName=TEXT("DemoGunRecoil"),GroupName=TEXT("DemoGunRecoilGroup"); // 独立组避免抢占Melee/Dash Montage。
    const FString MainGraphPath=TEXT("/Game/Characters/Heroes/Mannequin/Animations/ABP_Mannequin_Base.ABP_Mannequin_Base"); // PropertyAccess必须保留原类身份。
    const FString SkeletonPath=TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"); // 只允许添加该实际骨架的Slot元数据。

    /** Path为包长名；只接受两枪×Hold/Fire或Montage的精确新包，拒绝相似前缀或任意候选。 */
    bool OwnsPackage(const FString& Path,bool bMontage)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs path=%s"),__FUNCTION__,*Path);
        for (const FString& Root:{CandidateRoot,FormalRoot})
            for (const TCHAR* Model:{TEXT("M1911"),TEXT("MX16A4")})
            {
                if (bMontage && Path==Root+TEXT("Montages/AM_FireRecoilR01_")+Model+TEXT("_Fire")) return true;
                if (!bMontage && (Path==Root+TEXT("Animations/A_FireRecoilR01_")+Model+TEXT("_Hold")
                    || Path==Root+TEXT("Animations/A_FireRecoilR01_")+Model+TEXT("_Fire"))) return true;
            }
        return false;
    }

    /** 本次同步借用正式AnimGraph，绝不遍历并重建所有状态机或事件图。 */
    UEdGraph* MainGraph(UAnimBlueprint* Blueprint)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        if (Blueprint) for (UEdGraph* Graph:Blueprint->FunctionGraphs)
            if (Graph && Graph->GetFName()==TEXT("AnimGraph")) return Graph;
        return nullptr;
    }

    /** 两引脚必须形成唯一双向连接；用于幂等检查，未知手工图拒绝覆盖。 */
    bool Linked(UEdGraphPin* A,UEdGraphPin* B)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        return A && B && A->LinkedTo.Num()==1 && B->LinkedTo.Num()==1 && A->LinkedTo[0]==B && B->LinkedTo[0]==A;
    }

    /** 依据候选固定Model身份核对当前正式Appearance引用；不以文件命名或旧路径猜测增量基准。 */
    bool MatchesFormalHold(const UAnimSequence* Sequence,const UAnimSequence* Hold)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        const FName Id=Sequence && Sequence->GetName().Contains(TEXT("_M1911_"))?FName(TEXT("pistol")):FName(TEXT("rifle")); // 调用者已验证两枪精确白名单。
        const UDemoPlayerAppearance* Appearance=LoadObject<UDemoPlayerAppearance>(nullptr,TEXT("/Game/Player/DA_PlayerAppearance.DA_PlayerAppearance"));
        const FDemoPlayerWeaponPoses* Poses=Appearance?Appearance->WeaponPoses.Find(Id):nullptr;
        return Poses && Hold && Poses->Hold==Hold;
    }
}

int32 UDemoFireRecoilAuthoring::PrepareRecoilSampling(UAnimSequence* Sequence)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs sequence=%s"),__FUNCTION__,*GetNameSafe(Sequence));
    using namespace DemoFireRecoilAuthoring;
    if (!IsInGameThread() || !Sequence || !Sequence->GetDataModel() || !Sequence->GetSkeleton()
        || !Sequence->GetOutermost()->GetName().StartsWith(CandidateRoot) || !OwnsPackage(Sequence->GetOutermost()->GetName(),false)
        || Sequence->GetDataModel()->GetFrameRate()!=FFrameRate(120,1) || Sequence->GetSkeleton()->GetPathName()!=SkeletonPath)
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_SAMPLING_PREPARE_REJECT thread/path/model/rate/skeleton")); return 0; }
    // UE5.4 OnModelModified(Populated)同步PlatformTargetFrameRate。此时controller bracket仍打开，
    // 只更新目标频率和记录通知；实际CalculateNumberOfSampledKeys/压缩留给keys完成后的BracketClosed。
    // 不能等close后才Finalize，否则0.25s×默认30Hz=7.5帧会先触发不兼容重采样ensure。
    Sequence->GetController().NotifyPopulated();
    const int32 Rate=FMath::RoundToInt(Sequence->GetSamplingFrameRate().AsDecimal());
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_SAMPLING_PREPARED asset=%s target_hz=%d no_postedit_or_compression=1"),*Sequence->GetPathName(),Rate);
    return Rate;
}

int32 UDemoFireRecoilAuthoring::FinalizeRecoilSequence(UAnimSequence* Sequence,UAnimSequence* ReferenceHold,bool bAdditive)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs sequence=%s additive=%d"),__FUNCTION__,*GetNameSafe(Sequence),bAdditive);
    using namespace DemoFireRecoilAuthoring;
    if (!IsInGameThread() || !Sequence || !Sequence->GetSkeleton() || !Sequence->GetDataModel()
        || !OwnsPackage(Sequence->GetOutermost()->GetName(),false) || Sequence->GetDataModel()->GetFrameRate()!=FFrameRate(120,1)
        || Sequence->GetSkeleton()->GetPathName()!=SkeletonPath)
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_SEQUENCE_REJECT thread/path/model/rate/skeleton")); return 0; }
    if (bAdditive && (!ReferenceHold || ReferenceHold==Sequence || ReferenceHold->GetSkeleton()!=Sequence->GetSkeleton()
        || ReferenceHold->AdditiveAnimType!=AAT_None || !MatchesFormalHold(Sequence,ReferenceHold)))
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_SEQUENCE_REJECT reference must be current formal absolute Hold0")); return 0; }
    Sequence->Modify(); Sequence->bEnableRootMotion=false; Sequence->bForceRootLock=false;
    Sequence->bAllowFrameStripping=false; Sequence->CompressionErrorThresholdScale=10.f;
    Sequence->AdditiveAnimType=bAdditive?AAT_RotationOffsetMeshSpace:AAT_None;
    Sequence->RefPoseType=bAdditive?ABPT_AnimFrame:ABPT_RefPose;
    Sequence->RefPoseSeq=bAdditive?ReferenceHold:nullptr; Sequence->RefFrameIndex=0;
    Sequence->GetController().NotifyPopulated(); // 同步PlatformTargetFrameRate与新Additive压缩基准，避免保留默认30Hz。
    Sequence->PostEditChange(); Sequence->MarkPackageDirty();
    const int32 Rate=FMath::RoundToInt(Sequence->GetSamplingFrameRate().AsDecimal()); // 真实目标采样率回读，不以DataModel设值代替结果。
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_SEQUENCE_CONFIGURED asset=%s rate=%d additive=%d base=%s frame=0"),*Sequence->GetPathName(),Rate,bAdditive,*GetNameSafe(ReferenceHold));
    return Rate;
}

UAnimMontage* UDemoFireRecoilAuthoring::CreateRecoilMontage(const FString& PackagePath,UAnimSequence* Sequence)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs path=%s"),__FUNCTION__,*PackagePath);
    using namespace DemoFireRecoilAuthoring;
    if (!IsInGameThread() || !OwnsPackage(PackagePath,true) || !Sequence || !Sequence->GetSkeleton()
        || !OwnsPackage(Sequence->GetOutermost()->GetName(),false) || Sequence->AdditiveAnimType!=AAT_RotationOffsetMeshSpace
        || !Sequence->RefPoseSeq || Sequence->RefPoseType!=ABPT_AnimFrame || Sequence->RefFrameIndex!=0 || Sequence->GetPlayLength()<=0.f)
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_MONTAGE_REJECT path/additive/reference")); return nullptr; }
    const FName Name(*FPackageName::GetLongPackageAssetName(PackagePath)); // 包拥有，不能创建带调用者任意后缀的资源。
    UAnimMontage* Result=LoadObject<UAnimMontage>(nullptr,*(PackagePath+TEXT(".")+Name.ToString())); // 显式refresh工具可复用同名对象，不隐式保存。
    if (!Result)
    {
        Result=NewObject<UAnimMontage>(CreatePackage(*PackagePath),Name,RF_Public|RF_Standalone);
        FAssetRegistryModule::AssetCreated(Result);
    }
    Result->Modify(); Result->SetSkeleton(Sequence->GetSkeleton());
    Result->SlotAnimTracks.Reset(); Result->CompositeSections.Reset(); // 清理构造器DefaultSlot，保持唯一且无空轨。
    FSlotAnimationTrack& Track=Result->SlotAnimTracks.AddDefaulted_GetRef(); // 只在本次新Montage写入完整源段。
    Track.SlotName=SlotName;
    FAnimSegment Segment; // 无裁剪/倒放/变速，动作实际时长不依赖Gameplay RPM。
    Segment.SetAnimReference(Sequence); Segment.StartPos=0.f; Segment.AnimStartTime=0.f; Segment.AnimEndTime=Sequence->GetPlayLength();
    Segment.AnimPlayRate=1.f; Segment.LoopingCount=1; Track.AnimTrack.AnimSegments.Add(Segment);
    static_cast<UAnimCompositeBase*>(Result)->SetCompositeLength(Sequence->GetPlayLength());
    Result->AddAnimCompositeSection(TEXT("Fire"),0.f); Result->RateScale=1.f;
    Result->BlendIn.SetBlendTime(.02f); Result->BlendOut.SetBlendTime(.06f); // 可由逐武器运行参数覆盖；资产默认保持可编辑。
    Result->bEnableAutoBlendOut=true;
    Result->PostEditChange(); Result->MarkPackageDirty();
    return Result;
}

bool UDemoFireRecoilAuthoring::RegisterRecoilSlot(USkeleton* Skeleton)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs skeleton=%s"),__FUNCTION__,*GetNameSafe(Skeleton));
    using namespace DemoFireRecoilAuthoring;
    if (!IsInGameThread() || !Skeleton || Skeleton->GetPathName()!=SkeletonPath)
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_SLOT_GROUP_REJECT skeleton/thread")); return false; }
    for (const FAnimSlotGroup& Group:Skeleton->GetSlotGroups())
        if (Group.SlotNames.Contains(SlotName))
        {
            const bool bMatch=Group.GroupName==GroupName;
            if (!bMatch) UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_SLOT_GROUP_REJECT existing slot belongs to %s"),*Group.GroupName.ToString());
            return bMatch; // 不重分配其他使用者已有的同名Slot。
        }
    Skeleton->Modify(); Skeleton->RegisterSlotNode(SlotName); Skeleton->SetSlotGroupName(SlotName,GroupName); Skeleton->MarkPackageDirty();
    return Skeleton->GetSlotGroupName(SlotName)==GroupName;
}

bool UDemoFireRecoilAuthoring::InstallRecoilSlot(UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs blueprint=%s"),__FUNCTION__,*GetNameSafe(Blueprint));
    using namespace DemoFireRecoilAuthoring;
    UEdGraph* Graph=MainGraph(Blueprint); // 只接同身份主图，避免克隆后破坏LyraPropertyAccess缓存契约。
    if (!IsInGameThread() || !Blueprint || Blueprint->GetPathName()!=MainGraphPath || !Graph)
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_GRAPH_REJECT path/thread/graph")); return false; }
    UAnimGraphNode_DemoPlayerAim* Aim=nullptr; UAnimGraphNode_DemoPlayerGrounding* Ground=nullptr; // 原有唯一的最终骨控制。
    UAnimGraphNode_Slot* Slot=nullptr; UAnimGraphNode_ComponentToLocalSpace* ToLocal=nullptr; UAnimGraphNode_LocalToComponentSpace* ToComponent=nullptr;
    for (UEdGraphNode* Node:Graph->Nodes)
    {
        if (auto* Found=Cast<UAnimGraphNode_DemoPlayerAim>(Node)) { if (Aim) return false; Aim=Found; }
        if (auto* Found=Cast<UAnimGraphNode_DemoPlayerGrounding>(Node)) { if (Ground) return false; Ground=Found; }
        if (auto* Found=Cast<UAnimGraphNode_Slot>(Node); Found && Found->Node.SlotName==SlotName) { if (Slot) return false; Slot=Found; }
        if (Node->NodeComment==TEXT("DemoGunRecoil CS input")) ToLocal=Cast<UAnimGraphNode_ComponentToLocalSpace>(Node);
        if (Node->NodeComment==TEXT("DemoGunRecoil CS output")) ToComponent=Cast<UAnimGraphNode_LocalToComponentSpace>(Node);
    }
    UEdGraphPin* Input=Aim?Aim->FindPin(TEXT("ComponentPose")):nullptr;
    UEdGraphPin* Previous=Ground?Ground->FindPin(TEXT("Pose")):nullptr;
    if (!Input || !Previous || Input->LinkedTo.Num()!=1)
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_GRAPH_REJECT final Aim/Ground contract")); return false; }
    if (Slot)
        return ToLocal && ToComponent && Slot->Node.bAlwaysUpdateSourcePose
            && Linked(Previous,ToLocal->FindPin(TEXT("ComponentPose")))
            && Linked(ToLocal->FindPin(TEXT("Pose")),Slot->FindPin(TEXT("Source")))
            && Linked(Slot->FindPin(TEXT("Pose")),ToComponent->FindPin(TEXT("LocalPose")))
            && Linked(ToComponent->FindPin(TEXT("ComponentPose")),Input); // 幂等只核查，不能修写未知手工拓扑。
    if (ToLocal || ToComponent || !Linked(Previous,Input))
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_GRAPH_REJECT unknown intermediary/cross links")); return false; }
    Blueprint->Modify();
    FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> LocalCreator(*Graph); ToLocal=LocalCreator.CreateNode();
    ToLocal->NodePosX=Aim->NodePosX-660; ToLocal->NodePosY=Aim->NodePosY;
    ToLocal->NodeComment=TEXT("DemoGunRecoil CS input"); LocalCreator.Finalize();
    FGraphNodeCreator<UAnimGraphNode_Slot> SlotCreator(*Graph); Slot=SlotCreator.CreateNode();
    Slot->Node.SlotName=SlotName; Slot->Node.bAlwaysUpdateSourcePose=true;
    Slot->NodePosX=Aim->NodePosX-440; Slot->NodePosY=Aim->NodePosY;
    Slot->NodeComment=TEXT("DemoGunRecoil: additive after weapon operations, before final Aim; null montage passes through"); SlotCreator.Finalize();
    FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> ComponentCreator(*Graph); ToComponent=ComponentCreator.CreateNode();
    ToComponent->NodePosX=Aim->NodePosX-220; ToComponent->NodePosY=Aim->NodePosY;
    ToComponent->NodeComment=TEXT("DemoGunRecoil CS output"); ComponentCreator.Finalize();
    Input->BreakLinkTo(Previous);
    const UEdGraphSchema* Schema=Graph->GetSchema(); // 通过Schema验证Pose空间，不手动改Pin链表。
    if (!Schema->TryCreateConnection(Previous,ToLocal->FindPin(TEXT("ComponentPose")))
        || !Schema->TryCreateConnection(ToLocal->FindPin(TEXT("Pose")),Slot->FindPin(TEXT("Source")))
        || !Schema->TryCreateConnection(Slot->FindPin(TEXT("Pose")),ToComponent->FindPin(TEXT("LocalPose")))
        || !Schema->TryCreateConnection(ToComponent->FindPin(TEXT("ComponentPose")),Input))
    { UE_LOG(LogFPSDemo,Error,TEXT("GUN_RECOIL_GRAPH_LINK_FAILED do not save")); return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const bool bValid=Blueprint->Status!=BS_Error && Blueprint->GeneratedClass;
    if (bValid) Blueprint->MarkPackageDirty();
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_GRAPH_INSTALLED valid=%d slot=%s"),bValid,*SlotName.ToString()); return bValid;
}

FString UDemoFireRecoilAuthoring::InspectRecoilAssets(UAnimSequence* Sequence,UAnimMontage* Montage)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    TSharedPtr<FJsonObject> Report=MakeShared<FJsonObject>(); // 同步只读JSON，不保存UObject或更改压缩数据。
    if (!IsInGameThread() || !Sequence || !Sequence->GetSkeleton() || !Montage)
    { Report->SetBoolField(TEXT("valid"),false); Report->SetStringField(TEXT("error"),TEXT("missing sequence/montage/thread")); }
    else
    {
        Report->SetStringField(TEXT("sequence"),Sequence->GetPathName()); Report->SetStringField(TEXT("montage"),Montage->GetPathName());
        Report->SetNumberField(TEXT("length_seconds"),Sequence->GetPlayLength()); Report->SetNumberField(TEXT("sampling_hz"),Sequence->GetSamplingFrameRate().AsDecimal());
        Report->SetNumberField(TEXT("additive_type"),Sequence->AdditiveAnimType); Report->SetNumberField(TEXT("base_pose_type"),Sequence->RefPoseType);
        Report->SetStringField(TEXT("reference_hold"),GetPathNameSafe(Sequence->RefPoseSeq.Get())); Report->SetNumberField(TEXT("reference_frame"),Sequence->RefFrameIndex);
        Report->SetBoolField(TEXT("root_motion"),Sequence->bEnableRootMotion); Report->SetNumberField(TEXT("slot_tracks"),Montage->SlotAnimTracks.Num());
        const bool bOne=Montage->SlotAnimTracks.Num()==1 && Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()==1; // 不访问未知空轨。
        bool bSegment=false; // 真实源片段必须完整原速，不能用名称/长度猜。
        if (bOne)
        {
            const FSlotAnimationTrack& Track=Montage->SlotAnimTracks[0]; const FAnimSegment& Segment=Track.AnimTrack.AnimSegments[0];
            Report->SetStringField(TEXT("slot"),Track.SlotName.ToString()); Report->SetStringField(TEXT("group"),Montage->GetGroupName().ToString());
            bSegment=Track.SlotName==DemoFireRecoilAuthoring::SlotName && Segment.GetAnimReference()==Sequence
                && FMath::IsNearlyZero(Segment.AnimStartTime) && FMath::IsNearlyZero(Segment.StartPos)
                && FMath::IsNearlyEqual(Segment.AnimEndTime,Sequence->GetPlayLength(),.0001f) && FMath::IsNearlyEqual(Segment.AnimPlayRate,1.f) && Segment.LoopingCount==1;
        }
        Report->SetBoolField(TEXT("valid"),bSegment && Sequence->AdditiveAnimType==AAT_RotationOffsetMeshSpace
            && Sequence->RefPoseType==ABPT_AnimFrame && Sequence->RefFrameIndex==0 && Sequence->RefPoseSeq
            && Sequence->GetSkeleton()==Montage->GetSkeleton() && !Sequence->bEnableRootMotion);
    }
    FString Result; const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Result); // 调用者解析字段作独立断言。
    FJsonSerializer::Serialize(Report.ToSharedRef(),Writer); return Result;
}

FString UDemoFireRecoilAuthoring::InspectSkeletonContract(USkeleton* Skeleton)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs skeleton=%s"),__FUNCTION__,*GetNameSafe(Skeleton));
    if (!IsInGameThread() || !Skeleton || Skeleton->GetPathName()!=DemoFireRecoilAuthoring::SkeletonPath) return TEXT("{}");
    TSharedPtr<FJsonObject> Root=MakeShared<FJsonObject>(); // 只读结构化契约，调用者只排除允许增加的Slot字段作比较。
    const FReferenceSkeleton& Ref=Skeleton->GetReferenceSkeleton();
    TArray<TSharedPtr<FJsonValue>> Bones; // 骨名、父索引、参考TQS完整保留原序；无量化容差比较。
    for (int32 Index=0;Index<Ref.GetNum();++Index)
    {
        const FTransform& Local=Ref.GetRefBonePose()[Index]; const FVector P=Local.GetLocation(),S=Local.GetScale3D(); const FQuat Q=Local.GetRotation();
        TSharedPtr<FJsonObject> Bone=MakeShared<FJsonObject>();
        Bone->SetStringField(TEXT("name"),Ref.GetBoneName(Index).ToString()); Bone->SetNumberField(TEXT("parent"),Ref.GetParentIndex(Index));
        TArray<TSharedPtr<FJsonValue>> Values; // 本骨十个实际double，不格式化为低精度文字。
        for (const double Value:{P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z}) Values.Add(MakeShared<FJsonValueNumber>(Value));
        Bone->SetArrayField(TEXT("tqs"),Values); Bones.Add(MakeShared<FJsonValueObject>(Bone));
    }
    Root->SetArrayField(TEXT("reference_skeleton"),Bones);
    Root->SetStringField(TEXT("preview_mesh"),GetPathNameSafe(Skeleton->GetPreviewMesh()));
    TArray<TSharedPtr<FJsonValue>> Compatible; // 原兼容Skeleton软路径顺序不变。
    for (const TSoftObjectPtr<USkeleton>& Item:Skeleton->GetCompatibleSkeletons()) Compatible.Add(MakeShared<FJsonValueString>(Item.ToSoftObjectPath().ToString()));
    Root->SetArrayField(TEXT("compatible_skeletons"),Compatible);
    TArray<TSharedPtr<FJsonValue>> Groups;
    for (const FAnimSlotGroup& Group:Skeleton->GetSlotGroups())
    {
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("group"),Group.GroupName.ToString());
        TArray<TSharedPtr<FJsonValue>> Slots; for (const FName Name:Group.SlotNames) Slots.Add(MakeShared<FJsonValueString>(Name.ToString()));
        Item->SetArrayField(TEXT("slots"),Slots); Groups.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("slot_groups"),Groups);
    FString Result; const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Result); FJsonSerializer::Serialize(Root.ToSharedRef(),Writer); return Result;
}

FString UDemoFireRecoilAuthoring::InspectRecoilGraph(UAnimBlueprint* Blueprint)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs blueprint=%s"),__FUNCTION__,*GetNameSafe(Blueprint));
    using namespace DemoFireRecoilAuthoring;
    UEdGraph* Graph=MainGraph(Blueprint);
    if (!IsInGameThread() || !Blueprint || Blueprint->GetPathName()!=MainGraphPath || !Graph) return TEXT("{}");
    TSharedPtr<FJsonObject> Root=MakeShared<FJsonObject>(); // 无Pose求值/图修改，仅生成可比较的对象身份和连接。
    Root->SetStringField(TEXT("parent_class"),GetPathNameSafe(Blueprint->ParentClass));
    Root->SetStringField(TEXT("generated_class"),GetPathNameSafe(Blueprint->GeneratedClass));
    Root->SetStringField(TEXT("skeleton"),GetPathNameSafe(Blueprint->TargetSkeleton));
    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (const UEdGraphNode* Node:Graph->Nodes)
    {
        TSharedPtr<FJsonObject> Value=MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("guid"),Node->NodeGuid.ToString(EGuidFormats::Digits));
        Value->SetStringField(TEXT("class"),Node->GetClass()->GetPathName()); Value->SetStringField(TEXT("comment"),Node->NodeComment);
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (const UEdGraphPin* Pin:Node->Pins)
        {
            TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("name"),Pin->PinName.ToString()); Item->SetNumberField(TEXT("direction"),Pin->Direction);
            Item->SetStringField(TEXT("default_value"),Pin->DefaultValue); Item->SetStringField(TEXT("default_object"),GetPathNameSafe(Pin->DefaultObject));
            Item->SetStringField(TEXT("default_text"),Pin->DefaultTextValue.ToString());
            TArray<TSharedPtr<FJsonValue>> Links;
            for (const UEdGraphPin* Link:Pin->LinkedTo)
                Links.Add(MakeShared<FJsonValueString>(Link->GetOwningNode()->NodeGuid.ToString(EGuidFormats::Digits)+TEXT(":")+Link->PinName.ToString()));
            Item->SetArrayField(TEXT("links"),Links); Pins.Add(MakeShared<FJsonValueObject>(Item));
        }
        Value->SetArrayField(TEXT("pins"),Pins); Nodes.Add(MakeShared<FJsonValueObject>(Value));
    }
    Root->SetArrayField(TEXT("nodes"),Nodes);
    FString Result; const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Result); FJsonSerializer::Serialize(Root.ToSharedRef(),Writer); return Result;
}
