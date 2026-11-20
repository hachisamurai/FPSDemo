// 职责：闪避表现绑定与旧全身残影回退。默认由DemoDashFlow消费批准推进凭据；原模型池仅在配置启用Legacy时创建。
#include "VFX/DemoDashTrailComponent.h"
#include "VFX/DemoDashFXProfile.h"
#include "NiagaraComponent.h"
#include "Characters/DemoCharacter.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/DemoPlayerAppearance.h"
#include "AbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Debug/DemoLog.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

UDemoDashTrailComponent::UDemoDashTrailComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork; // 必须在最终Aim/足IK/网络平滑后取姿态，不能在GAS激活时复制旧一帧。
    SetIsReplicatedByDefault(true);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial(TEXT("/Game/VFX/Player/Dash/MI_DashAfterimage.MI_DashAfterimage")); // Editor生成的可调色材质实例，Cook硬引用闭包。
    GhostMaterial=DefaultMaterial.Object;
    static ConstructorHelpers::FObjectFinderOptional<UDemoDashFXProfile> DefaultFlow(TEXT("/Game/VFX/Player/Dash/DA_DashFlow.DA_DashFlow")); // 集中配置硬引用闭包；首次离线创建前允许缺失。
    FlowProfile=DefaultFlow.Get();
}

void UDemoDashTrailComponent::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    ReadAssetDefaults();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 只借用当前Avatar，不查第一个本地玩家。
    if (Pawn)
    {
        AddTickPrerequisiteComponent(Pawn->GetMesh()); // SkeletalMesh Tick完成后才读最终骨骼缓存，避免并行求值竞争。
        if (Pawn->GetPlayerPresentation()) AddTickPrerequisiteComponent(Pawn->GetPlayerPresentation()); // 本帧共用外观更新也先完成。
    }
    if ((!FlowProfile || FlowProfile->bLegacyAfterimages) && GetNetMode()!=NM_DedicatedServer && FApp::CanEverRender()) PreparePool(); // 默认流线不分配任何PoseableMesh。
    BindAbilitySystem(Pawn?Pawn->GetAbilitySystemComponent():nullptr);
}

void UDemoDashTrailComponent::BindAbilitySystem(UAbilitySystemComponent* NewASC)
{
    DEMO_LOG_CALL();
    if (BoundASC.Get()!=NewASC)
    {
        if (BoundASC.IsValid())
        {
            BoundASC->RegisterGameplayTagEvent(DemoTags::DashEvading).Remove(DashTagHandle);
            BoundASC->RegisterGameplayTagEvent(DemoTags::Dead).Remove(DeadTagHandle);
        }
        StopTrail(true); StopFlow(true); bWindowActive=false; BoundASC=NewASC;
        DashTagHandle.Reset(); DeadTagHandle.Reset();
        if (NewASC && GetNetMode()!=NM_DedicatedServer)
        {
            DashTagHandle=NewASC->RegisterGameplayTagEvent(DemoTags::DashEvading).AddUObject(this,&UDemoDashTrailComponent::OnStateTagChanged);
            DeadTagHandle=NewASC->RegisterGameplayTagEvent(DemoTags::Dead).AddUObject(this,&UDemoDashTrailComponent::OnStateTagChanged);
        }
    }
    RefreshFromAbilitySystem();
}

void UDemoDashTrailComponent::RefreshFromAbilitySystem()
{
    DEMO_LOG_CALL();
    if (!HasBegunPlay() || GetNetMode()==NM_DedicatedServer) return;
    const ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 模拟代理即使没有Controller也需要显示队友残影。
    const bool bInvalidAvatar=!Pawn || !BoundASC.IsValid() || BoundASC->GetAvatarActor()!=Pawn; // 旧Pawn不能消费PS新Avatar的窗口。
    const bool bDead=bInvalidAvatar || Pawn->IsCoopEliminated() || BoundASC->HasMatchingGameplayTag(DemoTags::Dead); // 短路保护失效ASC。
    if (bDead) { bWindowActive=false; StopTrail(true); StopFlow(true); return; }
    if (FlowProfile && !FlowProfile->bLegacyAfterimages) { RefreshApprovedDash(); return; } // 标签仅用于死亡/Avatar门禁，不再把0.45秒免伤当发射时长。
    const bool bWantsTrail=BoundASC->HasMatchingGameplayTag(DemoTags::DashEvading); // GE窗口与立即结束的Dash GA生命周期不同。
    if (bWindowActive==bWantsTrail) return;
    bWindowActive=bWantsTrail;
    if (bWindowActive) StartTrail(); else StopTrail(false);
}

void UDemoDashTrailComponent::OnStateTagChanged(FGameplayTag Tag, int32 Count)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Log,TEXT("DASH_TRAIL_STATE pawn=%s tag=%s count=%d"),*GetNameSafe(GetOwner()),*Tag.ToString(),Count);
    RefreshFromAbilitySystem();
}

UPoseableMeshComponent* UDemoDashTrailComponent::CreateGhostMesh(USkeletalMesh* Mesh, UMaterialInstanceDynamic* Material)
{
    DEMO_LOG_CALL();
    if (!Mesh || !Material || !GetOwner()) return nullptr;
    UPoseableMeshComponent* Ghost=NewObject<UPoseableMeshComponent>(GetOwner()); // Pawn拥有，组件只在预建池时创建，不逐帧分配。
    Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ghost->SetGenerateOverlapEvents(false); Ghost->SetCanEverAffectNavigation(false);
    Ghost->CastShadow=false; Ghost->bCastDynamicShadow=false; Ghost->bCastHiddenShadow=false;
    Ghost->bReceivesDecals=false;
    Ghost->SetOwnerNoSee(false); Ghost->SetOnlyOwnerSee(false); // 历史头部也完整显示；材质近相机淡出防自身穿越时遮挡。
    Ghost->SetHiddenInGame(true);
    Ghost->SetSkinnedAssetAndUpdate(Mesh); // 不挂到角色Root，不设LeaderPose；快照后不能继续跟随当前模型。
    Ghost->PrimaryComponentTick.bStartWithTickEnabled=false; // RegisterComponent不可重新启用默认Tick；快照只在采样时显式刷新。
    GetOwner()->AddInstanceComponent(Ghost);
    Ghost->RegisterComponent();
    Ghost->SetComponentTickEnabled(false); // PoseableMesh无需AnimBP，每次采样显式刷新一次骨骼，其余时间只改MID透明度。
    for (int32 Slot=0; Slot<Ghost->GetNumMaterials(); ++Slot) Ghost->SetMaterial(Slot,Material); // 每个身体材质槽均使用同一残像表现，不能残留不透明鞋/皮肤。
    return Ghost;
}

bool UDemoDashTrailComponent::PreparePool()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 本地唯一源Avatar，生命周期由World拥有。
    const UDemoPlayerAppearance* Appearance=Pawn && Pawn->GetPlayerPresentation()?Pawn->GetPlayerPresentation()->GetAppearance():nullptr; // Presentation已经解析的只读资源，不在此同步加载或改角色模型。
    USkeletalMesh* BodyMesh=Pawn && Pawn->GetMesh()?Pawn->GetMesh()->GetSkeletalMeshAsset():nullptr; // 实际已装配身体，不取旧第一人称手臂。
    USkeletalMesh* HeadMesh=Appearance?Appearance->HeadMesh.Get():nullptr; // 互补头部与身体使用同一骨架，不可只复制无头身体。
    if (!FApp::CanEverRender() || GetNetMode()==NM_DedicatedServer || !GhostMaterial || !Appearance || !BodyMesh || BodyMesh!=Appearance->BodyMesh)
    { UE_LOG(LogFPSDemo,Log,TEXT("DASH_TRAIL_POOL_DEFER missing render/material/full body")); return false; }
    if (Ghosts.Num()==MaxGhosts && PoolBodyMesh.Get()==BodyMesh && PoolHeadMesh.Get()==HeadMesh) return true;
    ReleasePool();
    Ghosts.SetNum(MaxGhosts); // ReadAssetDefaults已钳制2..16，数量在整个窗口固定。
    for (FDemoDashGhost& Ghost:Ghosts) // 同步初始化本地固定槽位，失败时整池释放，不留下半身可见残像。
    {
        Ghost.Material=UMaterialInstanceDynamic::Create(GhostMaterial,this);
        Ghost.Body=CreateGhostMesh(BodyMesh,Ghost.Material);
        if (HeadMesh) Ghost.Head=CreateGhostMesh(HeadMesh,Ghost.Material);
        if (!Ghost.Body || (HeadMesh && !Ghost.Head))
        { UE_LOG(LogFPSDemo,Warning,TEXT("DASH_TRAIL_POOL_FAILED allocation")); ReleasePool(); return false; }
    }
    PoolBodyMesh=BodyMesh; PoolHeadMesh=HeadMesh;
    ApplyParameters();
    UE_LOG(LogFPSDemo,Log,TEXT("DASH_TRAIL_POOL_READY pawn=%s slots=%d head=%d"),*GetNameSafe(Pawn),Ghosts.Num(),HeadMesh!=nullptr);
    return true;
}

void UDemoDashTrailComponent::StartTrail()
{
    DEMO_LOG_CALL();
    if (!GetWorld() || !PreparePool())
    { UE_LOG(LogFPSDemo,Warning,TEXT("DASH_TRAIL_SKIP full-body afterimage unavailable")); return; }
    ClearGhosts(); // 新窗口不与上一次复活/传送前的历史姿态相连。
    bEmitting=true;
    LastFramePosition=GetOwner()->GetActorLocation(); // 仅作为首帧断点基线，骨快照使用实际Body世界变换。
    SampleElapsed=SnapshotInterval; // 第一张在接下来的PostUpdate立即采样，不能在LaunchCharacter前取姿态。
    SetComponentTickEnabled(true);
    GetWorld()->GetTimerManager().SetTimer(EmissionTimer,this,&UDemoDashTrailComponent::OnEmissionTimeout,MaxEmissionSeconds,false);
    UE_LOG(LogFPSDemo,Log,TEXT("DASH_TRAIL_START mode=full_body pawn=%s color=%s"),*GetNameSafe(GetOwner()),*TrailColor.ToString());
}

void UDemoDashTrailComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,TickFunction);
    if (FlowProfile && !FlowProfile->bLegacyAfterimages) { TickFlow(DeltaTime); return; }
    const ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 游戏线程借用当前角色，不保留动画线程临时数据。
    if (!Pawn || Pawn->IsCoopEliminated() || !BoundASC.IsValid() || BoundASC->GetAvatarActor()!=Pawn)
    { StopTrail(true); return; }
    const FVector Position=Pawn->GetActorLocation(); // 断点按角色世界位置，正常Mesh网络平滑不被强制改写。
    if (FVector::DistSquared(Position,LastFramePosition)>FMath::Square(ResetDistance))
    {
        ClearGhosts(); // 传送/大校正不留跨场景长残像；后续有效位置重新采样。
        UE_LOG(LogFPSDemo,Log,TEXT("DASH_TRAIL_DISCONTINUITY pawn=%s"),*Pawn->GetName());
    }
    LastFramePosition=Position;
    bool bAnyAlive=false; // 本帧结束若没有活快照且不再生成，就停掉组件Tick。
    for (FDemoDashGhost& Ghost:Ghosts) // 最多16槽；高频入口只有VeryVerbose埋点，默认不逐帧输出。
    {
        if (Ghost.Age<0.f) continue;
        Ghost.Age+=FMath::Max(0.f,DeltaTime);
        if (Ghost.Age>=GhostLifetime)
        {
            Ghost.Age=-1.f;
            if (Ghost.Body) Ghost.Body->SetHiddenInGame(true);
            if (Ghost.Head) Ghost.Head->SetHiddenInGame(true);
            continue;
        }
        bAnyAlive=true;
        const float Fade=FMath::Square(1.f-Ghost.Age/GhostLifetime); // 越旧越淡，曲线连续，不让最后一张突然消失。
        if (Ghost.Material) Ghost.Material->SetScalarParameterValue(TEXT("GhostFade"),Fade);
    }
    if (bEmitting)
    {
        SampleElapsed+=FMath::Max(0.f,DeltaTime);
        if (SampleElapsed>=SnapshotInterval)
        {
            SampleElapsed=FMath::Fmod(SampleElapsed,SnapshotInterval); // 每帧最多一次，不在卡顿后追补几十张重叠人形。
            CaptureGhost();
        }
    }
    else if (!bAnyAlive) SetComponentTickEnabled(false);
}

void UDemoDashTrailComponent::CaptureGhost()
{
    DEMO_LOG_TICK();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 依赖Tick顺序，源身体动画/网络平滑已经完成。
    USkeletalMeshComponent* Source=Pawn?Pawn->GetMesh():nullptr; // 所有姿态都取共享全身，不采第一人称隐藏手臂或当前Ghost。
    if (!Source || Ghosts.IsEmpty() || Source->GetSkeletalMeshAsset()!=PoolBodyMesh.Get())
    { UE_LOG(LogFPSDemo,Warning,TEXT("DASH_TRAIL_CAPTURE_REJECT source changed")); StopTrail(true); return; }
    if (Source->GetBoneSpaceTransforms().Num()!=PoolBodyMesh->GetRefSkeleton().GetNum()) return; // 初始化/卸载尚未求值时跳过，防CopyPose慢路径越界或参考姿态残影。
    const FTransform Transform=Source->GetComponentTransform(); // 包含人物BodyOffset/缩放和CMC平滑；每张只冻结当时世界变换。
    if (Transform.ContainsNaN()) return;
    if (bHasSample && FVector::DistSquared(Transform.GetLocation(),LastSamplePosition)<FMath::Square(MinSampleDistance)) return;
    FDemoDashGhost& Ghost=Ghosts[NextGhost]; // 固定环形覆盖最旧槽，最大数量与预算不因连续输入增加。
    NextGhost=(NextGhost+1)%Ghosts.Num();
    for (UPoseableMeshComponent* Part:{Ghost.Body.Get(),Ghost.Head.Get()}) // 头部使用源Body骨姿态做同骨名映射，不复制LeaderPose头部空动画数组。
    {
        if (!Part) continue;
        Part->SetWorldTransform(Transform);
        Part->CopyPoseFromSkeletalComponent(Source); // 冻结当前已求值姿态，历史快照不会跟着下一帧动画变化。
        Part->RefreshBoneTransforms(); // 该快照组件无Tick，因此立即更新渲染骨矩阵/Bounds，避免显示参考T Pose。
        Part->SetHiddenInGame(false);
    }
    Ghost.Age=0.f;
    Ghost.Material->SetScalarParameterValue(TEXT("GhostFade"),1.f);
    LastSamplePosition=Transform.GetLocation(); bHasSample=true;
}

void UDemoDashTrailComponent::ClearGhosts()
{
    DEMO_LOG_CALL();
    for (FDemoDashGhost& Ghost:Ghosts) // 保留对象复用，不把循环闪避变成创建/销毁组件高峰。
    {
        Ghost.Age=-1.f;
        if (Ghost.Body) Ghost.Body->SetHiddenInGame(true);
        if (Ghost.Head) Ghost.Head->SetHiddenInGame(true);
    }
    NextGhost=0; SampleElapsed=0.f; bHasSample=false;
}

void UDemoDashTrailComponent::StopTrail(bool bImmediate)
{
    DEMO_LOG_CALL();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(EmissionTimer);
    if (bEmitting) UE_LOG(LogFPSDemo,Log,TEXT("DASH_TRAIL_STOP pawn=%s immediate=%d"),*GetNameSafe(GetOwner()),bImmediate);
    bEmitting=false;
    if (bImmediate) { ClearGhosts(); SetComponentTickEnabled(false); }
    // 非立即结束保留Tick，直到最后一张快照淡出；不再跟随源模型更新历史骨骼。
}

void UDemoDashTrailComponent::OnEmissionTimeout()
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Log,TEXT("DASH_TRAIL_TIMEOUT pawn=%s; cosmetic emission only"),*GetNameSafe(GetOwner()));
    StopTrail(false); // bWindowActive保持锁存，直到原GAS标签实际移除。
}

void UDemoDashTrailComponent::ReleasePool()
{
    DEMO_LOG_CALL();
    for (FDemoDashGhost& Ghost:Ghosts) // 仅销毁本组件建立的渲染副本，不操作源角色Body/Head。
    {
        if (Ghost.Body) Ghost.Body->DestroyComponent();
        if (Ghost.Head) Ghost.Head->DestroyComponent();
    }
    Ghosts.Reset(); PoolBodyMesh.Reset(); PoolHeadMesh.Reset(); NextGhost=0; bHasSample=false;
}

void UDemoDashTrailComponent::ApplyParameters()
{
    DEMO_LOG_CALL();
    const FLinearColor Color=FMath::IsFinite(TrailColor.R)&&FMath::IsFinite(TrailColor.G)&&FMath::IsFinite(TrailColor.B)&&FMath::IsFinite(TrailColor.A)
        ?FLinearColor(FMath::Clamp(TrailColor.R,0.f,4.f),FMath::Clamp(TrailColor.G,0.f,4.f),FMath::Clamp(TrailColor.B,0.f,4.f),FMath::Clamp(TrailColor.A,0.f,1.f))
        :FLinearColor(.08f,.65f,1.f,.8f); // 材质作者数据也需有限值兜底，不能向渲染线程传NaN。
    for (FDemoDashGhost& Ghost:Ghosts) if (Ghost.Material) Ghost.Material->SetVectorParameterValue(TEXT("TrailColor"),Color); // 每张MID同色，Fade独立；不污染人物本身材质。
    ApplyFlowParameters();
}

void UDemoDashTrailComponent::ReadAssetDefaults()
{
    DEMO_LOG_CALL();
    SnapshotInterval=FMath::IsFinite(SnapshotInterval)?FMath::Clamp(SnapshotInterval,.016f,.1f):.025f;
    GhostLifetime=FMath::IsFinite(GhostLifetime)?FMath::Clamp(GhostLifetime,.08f,.6f):.24f;
    MaxGhosts=FMath::Clamp(MaxGhosts,2,16);
    MinSampleDistance=FMath::IsFinite(MinSampleDistance)?FMath::Clamp(MinSampleDistance,0.f,20.f):3.f;
    ResetDistance=FMath::IsFinite(ResetDistance)?FMath::Clamp(ResetDistance,200.f,2000.f):400.f;
    MaxEmissionSeconds=FMath::IsFinite(MaxEmissionSeconds)?FMath::Clamp(MaxEmissionSeconds,.1f,2.f):.6f;
    if (FlowProfile && !FlowProfile->bLegacyAfterimages)
    {
        if (GetOwner() && GetOwner()->HasAuthority() && !bColorExplicitlySet && !bOverrideSystemDefaults) TrailColor=FlowProfile->Color;
        return; // 流线颜色来自配置，不受旧MI_DashAfterimage默认色覆盖。
    }
    if (!bOverrideSystemDefaults && GhostMaterial && GetOwner() && GetOwner()->HasAuthority() && !bColorExplicitlySet)
    {
        FLinearColor AssetColor=TrailColor; // 只读取默认实例色；客户端可能先收到复制值，不能在BeginPlay把它覆盖。
        if (GhostMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("TrailColor")),AssetColor)) TrailColor=AssetColor;
        else UE_LOG(LogFPSDemo,Warning,TEXT("DASH_TRAIL_CONFIG missing material TrailColor; component fallback"));
    }
}

void UDemoDashTrailComponent::SetTrailColor(FLinearColor NewColor)
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(NewColor.R) || !FMath::IsFinite(NewColor.G) || !FMath::IsFinite(NewColor.B) || !FMath::IsFinite(NewColor.A))
    { UE_LOG(LogFPSDemo,Warning,TEXT("DASH_TRAIL_COLOR_REJECT authority/invalid color")); return; }
    TrailColor=FLinearColor(FMath::Clamp(NewColor.R,0.f,4.f),FMath::Clamp(NewColor.G,0.f,4.f),FMath::Clamp(NewColor.B,0.f,4.f),FMath::Clamp(NewColor.A,0.f,1.f));
    bColorExplicitlySet=true;
    ApplyParameters(); GetOwner()->ForceNetUpdate(); // Listen Server立即生效，队友/拥有者OnRep只改各自本地快照材质。
}

void UDemoDashTrailComponent::OnRep_TrailColor()
{
    DEMO_LOG_CALL(); ApplyParameters();
}

void UDemoDashTrailComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDemoDashTrailComponent,TrailColor);
}

void UDemoDashTrailComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); BindAbilitySystem(nullptr); StopTrail(true); StopFlow(true); ReleasePool();
    if (FlowSystem) FlowSystem->DestroyComponent();
    FlowSystem=nullptr; FlowMaterial=nullptr;
    Super::EndPlay(Reason);
}

