// 职责：实现隔离TP候选Montage/Slot图与离线RAW轨道采样同步；真实组件求值验证动画接入，禁止保存正式包。
// 依赖：同名头、DemoLog、AnimGraph工厂/编译器、FPreviewScene；所有操作同步且仅Editor模块可用。
#include "Animation/DemoMeleeAnimationAuthoring.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimationGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "PreviewScene.h"
#include "Debug/DemoLog.h"

namespace DemoMeleeAuthoring
{
    // Montage/预览图创建及Slot修复的原有目录；Finalize的新增范围单独限定，不能借此放开其他作者接口。
    const FString Prefix=TEXT("/Game/Weapons/Melee/Candidates/TPRebuild20261030/");

    // 仅Finalize可同步此修订的Manny动画采样率；不授权R02根目录、Preview、Rigs或其他候选。
    const FString FinalizeMannyR02Prefix=TEXT("/Game/Weapons/Melee/Candidates/Kevin2H_MannyR02/Manny/");

    // R04仅允许RAW Manny姿态候选同步采样率；不扩大Montage/图工厂或正式资源写权限。
    const FString FinalizeMannyBladeR04Prefix=TEXT("/Game/Weapons/Melee/Candidates/MannyBladePostureR04/Manny/");

    // R05只允许本次胸腰候选的两个完整包名；不放开整个Manny目录、其他对象或正式动画。
    const FString FinalizeMannyBladeR05IdlePackage=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR05/Manny/A_BladeTorsoR05_Idle");
    const FString FinalizeMannyBladeR05AttackPackage=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR05/Manny/A_BladeTorsoR05_Attack");

    // R06仅允许尾段连续回收修订的两个精确候选包；历史R05与正式R06资源不属于此额外白名单。
    const FString FinalizeMannyBladeR06IdlePackage=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR06/Manny/A_BladeTorsoR06_Idle");
    const FString FinalizeMannyBladeR06AttackPackage=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR06/Manny/A_BladeTorsoR06_Attack");

    // R07只同步本体避让修订的两个精确RAW包，不扩大作者接口或正式资产的写权限。
    const FString FinalizeMannyBladeR07IdlePackage=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeClearanceR07/Manny/A_BladeClearanceR07_Idle");
    const FString FinalizeMannyBladeR07AttackPackage=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeClearanceR07/Manny/A_BladeClearanceR07_Attack");

    // 仅Finalize可访问这一份120Hz兼容性副本，完整对象路径精确比较，不放开Probe目录或同包其他对象。
    const FString FinalizePrevious120HzObject=TEXT("/Game/Weapons/Melee/Candidates/Kevin2H_MannyR02/Probe/A_Previous120HzCompatibility.A_Previous120HzCompatibility");

    /** 只接受白名单新包；不加载/修改其他任务已有资产。 */
    bool CanCreate(const FString& Path)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring::CanCreate path=%s"),*Path);
        const FString ObjectPath=Path+TEXT(".")+FPackageName::GetLongPackageAssetName(Path); // 完整新对象名。
        if (!Path.StartsWith(Prefix) || !FPackageName::IsValidLongPackageName(Path) || LoadObject<UObject>(nullptr,*ObjectPath))
        { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_AUTHOR_REJECT path/existing=%s"),*Path); return false; }
        return true;
    }

    /** Graph拥有节点；仅同步建图调用，不跨蓝图编译保留借用指针。 */
    template<class T> T* Node(UEdGraph* Graph,int32 X)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] MeleeAuthoring::Node graph=%s"),*GetNameSafe(Graph));
        FGraphNodeCreator<T> Creator(*Graph); // 本次创建的节点容器。
        T* Result=Creator.CreateNode(); Result->NodePosX=X; Creator.Finalize(); return Result;
    }
}

UAnimMontage* UDemoMeleeAnimationAuthoring::CreateFullBodyMontage(const FString& PackagePath,UAnimSequence* Sequence)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs path=%s"),__FUNCTION__,*PackagePath);
    if (!IsInGameThread() || !Sequence || !Sequence->GetSkeleton() || Sequence->GetPlayLength()<=0 || !DemoMeleeAuthoring::CanCreate(PackagePath))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_MONTAGE_INPUT_REJECT")); return nullptr; }
    const FName Name(*FPackageName::GetLongPackageAssetName(PackagePath)); // 包拥有新Montage，调用者保存。
    UAnimMontage* Result=NewObject<UAnimMontage>(CreatePackage(*PackagePath),Name,RF_Public|RF_Standalone);
    Result->SetSkeleton(Sequence->GetSkeleton());
    Result->SlotAnimTracks.Reset(); Result->CompositeSections.Reset(); // UE构造器已加入DefaultSlot；必须清空再创建唯一轨道。
    FSlotAnimationTrack& Track=Result->SlotAnimTracks.AddDefaulted_GetRef(); // 独立全身预览Slot，不改正式上身层。
    Track.SlotName=TEXT("DefaultSlot");
    FAnimSegment Segment; // 完整源片段，1x，不裁剪、不倒放、不提取RootMotion。
    Segment.SetAnimReference(Sequence); Segment.StartPos=0; Segment.AnimStartTime=0; Segment.AnimEndTime=Sequence->GetPlayLength();
    Segment.AnimPlayRate=1; Segment.LoopingCount=1; Track.AnimTrack.AnimSegments.Add(Segment);
    static_cast<UAnimCompositeBase*>(Result)->SetCompositeLength(Sequence->GetPlayLength());
    Result->AddAnimCompositeSection(TEXT("Attack"),0); Result->BlendIn.SetBlendTime(.06f); Result->BlendOut.SetBlendTime(.10f);
    FAssetRegistryModule::AssetCreated(Result); Result->PostEditChange(); Result->MarkPackageDirty(); return Result;
}

int32 UDemoMeleeAnimationAuthoring::FinalizeCandidateTracks(UAnimSequence* Sequence)
{
    // 本函数为static，无this可供DEMO_LOG_CALL宏使用；保留相同Log类别/级别的显式入口埋点。
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs sequence=%s"),__FUNCTION__,*GetNameSafe(Sequence));
    if (!IsInGameThread() || !Sequence || !Sequence->GetDataModel())
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FINALIZE_TRACKS_REJECT invalid_thread_sequence_or_model")); return 0; }
    // 包名比较避免对象后缀/近似Probe前缀绕过范围；只修改当前Sequence，不加载或保存其他包。
    const FString CandidatePackage=Sequence->GetOutermost()->GetName();
    const bool bAllowedFinalizePath=CandidatePackage.StartsWith(DemoMeleeAuthoring::Prefix) ||
        CandidatePackage.StartsWith(DemoMeleeAuthoring::FinalizeMannyR02Prefix) ||
        CandidatePackage.StartsWith(DemoMeleeAuthoring::FinalizeMannyBladeR04Prefix) ||
        CandidatePackage==DemoMeleeAuthoring::FinalizeMannyBladeR05IdlePackage ||
        CandidatePackage==DemoMeleeAuthoring::FinalizeMannyBladeR05AttackPackage ||
        CandidatePackage==DemoMeleeAuthoring::FinalizeMannyBladeR06IdlePackage ||
        CandidatePackage==DemoMeleeAuthoring::FinalizeMannyBladeR06AttackPackage ||
        CandidatePackage==DemoMeleeAuthoring::FinalizeMannyBladeR07IdlePackage ||
        CandidatePackage==DemoMeleeAuthoring::FinalizeMannyBladeR07AttackPackage ||
        Sequence->GetPathName()==DemoMeleeAuthoring::FinalizePrevious120HzObject;
    if (!bAllowedFinalizePath)
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FINALIZE_TRACKS_REJECT source/path=%s"),*CandidatePackage); return 0; }
    const FFrameRate ModelRate=Sequence->GetDataModel()->GetFrameRate(); // 候选真实源频率；仅120/240Hz离线轨道可同步压缩目标，原120Hz行为保留。
    if (ModelRate!=FFrameRate(120,1) && ModelRate!=FFrameRate(240,1))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FINALIZE_TRACKS_REJECT source_rate=%d/%d expected=120_or_240"),ModelRate.Numerator,ModelRate.Denominator); return 0; }
    const double Before=Sequence->GetSamplingFrameRate().AsDecimal(); // SetFrameRate只改变DataModel，不保证默认30Hz压缩目标同步。
    Sequence->Modify(); Sequence->bAllowFrameStripping=false; Sequence->CompressionErrorThresholdScale=10.f;
    Sequence->GetController().NotifyPopulated(); // UE5.4 Populated分支将PlatformTargetFrameRate同步为模型的真实采样率。
    Sequence->PostEditChange(); Sequence->MarkPackageDirty();
    const int32 Result=FMath::RoundToInt(Sequence->GetSamplingFrameRate().AsDecimal()); // 调用者记录并验证运行fps。
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_TRACKS_FINALIZED source=%d/%d runtime_before=%.3f runtime_after=%d frame_stripping=0"),ModelRate.Numerator,ModelRate.Denominator,Before,Result);
    return Result;
}

bool UDemoMeleeAnimationAuthoring::RepairDuplicatePreviewSlot(UAnimMontage* Montage)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs montage=%s"),__FUNCTION__,*GetNameSafe(Montage));
    if (!IsInGameThread() || !Montage || !Montage->GetPathName().StartsWith(DemoMeleeAuthoring::Prefix) || Montage->SlotAnimTracks.Num()!=2 ||
        Montage->SlotAnimTracks[0].SlotName!=TEXT("DefaultSlot") || Montage->SlotAnimTracks[1].SlotName!=TEXT("DefaultSlot") ||
        !Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty() || Montage->SlotAnimTracks[1].AnimTrack.AnimSegments.Num()!=1)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_SLOT_REPAIR_REJECT unknown asset/signature")); return false; }
    Montage->Modify(); Montage->SlotAnimTracks.RemoveAt(0); // 仅移除构造器空轨，保留原唯一有效片段与时长。
    Montage->PostEditChange(); Montage->MarkPackageDirty(); return true;
}

UAnimBlueprint* UDemoMeleeAnimationAuthoring::CreatePreviewGraph(const FString& PackagePath,USkeletalMesh* Mesh,UAnimSequence* Idle)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs path=%s"),__FUNCTION__,*PackagePath);
    if (!IsInGameThread() || !Mesh || !Idle || Mesh->GetSkeleton()!=Idle->GetSkeleton() || !DemoMeleeAuthoring::CanCreate(PackagePath))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_PREVIEW_GRAPH_INPUT_REJECT")); return nullptr; }
    UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>(); // 同步工厂，不写共享类默认值。
    Factory->ParentClass=UAnimInstance::StaticClass(); Factory->TargetSkeleton=Mesh->GetSkeleton();
    UAnimBlueprint* Result=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*PackagePath),
        *FPackageName::GetLongPackageAssetName(PackagePath),RF_Public|RF_Standalone,nullptr,GWarn)); // 新包拥有。
    if (!Result) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_PREVIEW_GRAPH_CREATE_FAILED")); return nullptr; }
    FAssetRegistryModule::AssetCreated(Result); Result->SetPreviewMesh(Mesh);
    UEdGraph* Graph=nullptr; // 只借用本次新建AnimGraph。
    for (UEdGraph* Candidate:Result->FunctionGraphs) if (Candidate->GetFName()==TEXT("AnimGraph")) Graph=Candidate;
    if (!Graph) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_PREVIEW_GRAPH_MISSING")); return nullptr; }
    UAnimGraphNode_Root* Root=nullptr; // 保留工厂创建的输出根。
    for (UEdGraphNode* Item:Graph->Nodes) if (auto* Typed=Cast<UAnimGraphNode_Root>(Item)) Root=Typed;
    if (!Root) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_PREVIEW_ROOT_MISSING")); return nullptr; }
    auto* Player=DemoMeleeAuthoring::Node<UAnimGraphNode_SequencePlayer>(Graph,-400); // 待机仅作Montage之外的回落。
    Player->Node.SetSequence(Idle); Player->Node.SetLoopAnimation(true);
    auto* Slot=DemoMeleeAuthoring::Node<UAnimGraphNode_Slot>(Graph,-180); Slot->Node.SlotName=TEXT("DefaultSlot");
    if (!Graph->GetSchema()->TryCreateConnection(Player->FindPin(TEXT("Pose")),Slot->FindPin(TEXT("Source"))) ||
        !Graph->GetSchema()->TryCreateConnection(Slot->FindPin(TEXT("Pose")),Root->FindPin(TEXT("Result"))))
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_PREVIEW_SLOT_LINK_FAILED")); return nullptr; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Result); FKismetEditorUtilities::CompileBlueprint(Result);
    if (Result->Status==BS_Error || !Result->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_PREVIEW_COMPILE_FAILED")); return nullptr; }
    Result->MarkPackageDirty(); return Result;
}

TMap<FName,FTransform> UDemoMeleeAnimationAuthoring::SampleMontage(USkeletalMesh* Mesh,UAnimBlueprint* Blueprint,UAnimMontage* Montage,float Time,bool bPostProcess)
{
    DEMO_LOG_CALL();
    if (!IsInGameThread() || !Mesh || !Blueprint || !Blueprint->GeneratedClass || !Montage || Mesh->GetSkeleton()!=Montage->GetSkeleton() ||
        Blueprint->TargetSkeleton!=Mesh->GetSkeleton() || !FMath::IsFinite(Time) || Time<0 || Time>Montage->GetPlayLength()+KINDA_SMALL_NUMBER)
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_MONTAGE_PROBE_INPUT_REJECT")); return {}; }
    FAssetCompilingManager::Get().FinishAllCompilation();
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false)); // 一次调用隔离世界。
    USkeletalMeshComponent* Component=NewObject<USkeletalMeshComponent>(); // Scene持有，不访问游戏角色。
    Component->SetSkeletalMesh(Mesh); Component->SetForcedLOD(1); Component->SetDisablePostProcessBlueprint(!bPostProcess);
    Component->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Component->SetAnimInstanceClass(Blueprint->GeneratedClass); Scene.AddComponent(Component,FTransform::Identity);
    UAnimInstance* Instance=Component->GetAnimInstance(); // 注册后的实际实例，只借用至本函数结束。
    if (!Instance || Instance->Montage_Play(Montage,1.f)<=0) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_MONTAGE_PROBE_PLAY_FAILED")); return {}; }
    Instance->Montage_SetPlayRate(Montage,0.f); // 只固定源时钟；预热真实Slot权重至1，不改变源资产。
    for (int32 Warm=0;Warm<12;++Warm) { Component->TickAnimation(1.f/60.f,false); Component->RefreshBoneTransforms(); }
    Instance->Montage_SetPosition(Montage,FMath::Min(Time,Montage->GetPlayLength()-KINDA_SMALL_NUMBER));
    Component->TickAnimation(0.f,false); Component->RefreshBoneTransforms();
    if (Instance->GetCurrentActiveMontage()!=Montage) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_MONTAGE_PROBE_NOT_ACTIVE")); return {}; }
    const auto& Ref=Mesh->GetRefSkeleton(); const auto& Poses=Component->GetComponentSpaceTransforms(); // 当前Slot/PP真实输出。
    if (Poses.Num()!=Ref.GetNum()) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_MONTAGE_PROBE_INCOMPLETE")); return {}; }
    TMap<FName,FTransform> Result; // 返回独立值，临时组件销毁后仍有效。
    for (int32 Index=0;Index<Poses.Num();++Index)
    {
        if (Poses[Index].ContainsNaN()) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_MONTAGE_PROBE_NONFINITE")); return {}; }
        Result.Add(Ref.GetBoneName(Index),Poses[Index]);
    }
    return Result;
}
