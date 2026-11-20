// 职责：确保近镜头Owner分区和真实Body拥有完整LOD骨缓冲；支持Leader或独立Owner窄图，UE5.4局部ForcedLOD不改骨/全局画质。
#include "Animation/DemoOwnerBodyLODComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

UDemoOwnerBodyLODComponent::UDemoOwnerBodyLODComponent()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PrePhysics;
    bAutoActivate=true; // 初始化后每帧只比较状态/整数，不逐帧创建资源或输出业务日志。
}

void UDemoOwnerBodyLODComponent::Initialize(USkeletalMeshComponent* Leader, USkeletalMeshComponent* Follower)
{
    DEMO_LOG_CALL(); RestoreLOD();
    if (Body.IsValid()) Body->RemoveTickPrerequisiteComponent(this);
    if (OwnerBody.IsValid()) OwnerBody->RemoveTickPrerequisiteComponent(this);
    Body=Leader; OwnerBody=Follower;
    if (!Leader || !Follower || Leader->GetOwner()!=GetOwner() || Follower->GetOwner()!=GetOwner())
    { UE_LOG(LogFPSDemo,Error,TEXT("OWNER_BODY_LOD_INIT_REJECT invalid/cross-Pawn components")); Body.Reset(); OwnerBody.Reset(); return; }
    Leader->AddTickPrerequisiteComponent(this); // UE在Mesh Tick开头选LOD，必须先于该处及异步动画求值。
    Follower->AddTickPrerequisiteComponent(this); // 独立Owner也会自行构建RequiredBones，LOD策略必须先于它，不只先于主Body。
    ValidateCoverage(); UpdatePolicy();
}

void UDemoOwnerBodyLODComponent::ValidateCoverage()
{
    DEMO_LOG_CALL();
    const USkeletalMesh* Leader=Body.IsValid()?Body->GetSkeletalMeshAsset():nullptr; // 游戏线程只读实际资源。
    const USkeletalMesh* Follower=OwnerBody.IsValid()?OwnerBody->GetSkeletalMeshAsset():nullptr;
    const FSkeletalMeshRenderData* L=Leader?Leader->GetResourceForRendering():nullptr;
    const FSkeletalMeshRenderData* F=Follower?Follower->GetResourceForRendering():nullptr;
    if (!L || !F || L->LODRenderData.IsEmpty() || F->LODRenderData.IsEmpty())
    { UE_LOG(LogFPSDemo,Error,TEXT("OWNER_BODY_LOD_COVERAGE missing LOD0 render data")); return; }
    int32 Missing=0; // 逐Section BoneMap覆盖真正蒙皮索引，不能只比较Skeleton指针/骨名总数。
    for (const FSkelMeshRenderSection& Section:F->LODRenderData[0].RenderSections)
        for (FBoneIndexType Index:Section.BoneMap)
        {
            const FName Name=Follower->GetRefSkeleton().GetBoneName(Index);
            const int32 LeaderIndex=Leader->GetRefSkeleton().FindBoneIndex(Name);
            if (LeaderIndex==INDEX_NONE || !L->LODRenderData[0].RequiredBones.Contains(static_cast<FBoneIndexType>(LeaderIndex)))
            { ++Missing; UE_LOG(LogFPSDemo,Error,TEXT("OWNER_BODY_LOD_MISSING bone=%s follower=%s"),*Name.ToString(),*GetNameSafe(Follower)); }
        }
    UE_LOG(LogFPSDemo,Log,TEXT("OWNER_BODY_LOD_COVERAGE leader=%s follower=%s required=%d missing=%d"),*GetNameSafe(Leader),*GetNameSafe(Follower),L->LODRenderData[0].RequiredBones.Num(),Missing);
}

void UDemoOwnerBodyLODComponent::UpdatePolicy()
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwner()); // 仅当前游戏线程同步借用，不跨线程访问控制器。
    const APlayerController* PC=Character?Cast<APlayerController>(Character->GetController()):nullptr;
    const bool bNeeded=IsActive() && Body.IsValid() && OwnerBody.IsValid() && Character && Character->IsLocallyControlled()
        && PC && PC->IsLocalController() && PC->GetViewTarget()==Character && !Character->IsCoopEliminated()
        && !Character->IsHidden() && Body->IsVisible() && !Body->bHiddenInGame;
    if (!bNeeded) { RestoreLOD(); return; }
    if (!bOverriding)
    {
        SavedBodyLOD=Body->GetForcedLOD(); SavedOwnerLOD=OwnerBody->GetForcedLOD(); bOverriding=true;
        UE_LOG(LogFPSDemo,Log,TEXT("OWNER_BODY_LOD_ENTER pawn=%s saved=%d/%d forced=1/1 (LOD0); remote unchanged"),*GetNameSafe(Character),SavedBodyLOD,SavedOwnerLOD);
    }
    // 画质/装备变化不能让Leader再次丢骨；只在整数确实不同时设置，普通每帧不重建Pose或刷新网格。
    if (Body->GetForcedLOD()!=1) Body->SetForcedLOD(1);
    if (OwnerBody->GetForcedLOD()!=1) OwnerBody->SetForcedLOD(1);
}

void UDemoOwnerBodyLODComponent::RestoreLOD()
{
    DEMO_LOG_TICK(); if (!bOverriding) return;
    if (Body.IsValid()) Body->SetForcedLOD(SavedBodyLOD);
    if (OwnerBody.IsValid()) OwnerBody->SetForcedLOD(SavedOwnerLOD);
    UE_LOG(LogFPSDemo,Log,TEXT("OWNER_BODY_LOD_EXIT pawn=%s restored=%d/%d"),*GetNameSafe(GetOwner()),SavedBodyLOD,SavedOwnerLOD);
    bOverriding=false;
}

void UDemoOwnerBodyLODComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,TickFunction); UpdatePolicy();
}

void UDemoOwnerBodyLODComponent::Deactivate()
{
    DEMO_LOG_CALL(); RestoreLOD(); Super::Deactivate();
}

void UDemoOwnerBodyLODComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); RestoreLOD(); if (Body.IsValid()) Body->RemoveTickPrerequisiteComponent(this);
    if (OwnerBody.IsValid()) OwnerBody->RemoveTickPrerequisiteComponent(this);
    Body.Reset(); OwnerBody.Reset(); Super::EndPlay(EndPlayReason);
}
