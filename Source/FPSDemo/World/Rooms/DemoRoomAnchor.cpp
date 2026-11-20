// 职责：房间作者锚点转为有限世界快照，并复制权威准备代数；不生成敌人/目标、不决定关号，依赖场景根与网络属性。
// LIGHTING-REFACTOR-20261119/RENDER：本机按Active/视点管理有界PP与球雾；RoomExposure作为旧四字段回退，网络/存档不变。
#include "World/Rooms/DemoRoomAnchor.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomAtmosphereProfile.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"
#include "Components/LocalFogVolumeComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Debug/DemoLog.h"

ADemoRoomAnchor::ADemoRoomAnchor()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.bTickEvenWhenPaused = true; PrimaryActorTick.TickGroup = TG_PostUpdateWork; bReplicates = true; bAlwaysRelevant = true;
    SetReplicateMovement(false); SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot")));
    DeviceOffsets = { FVector(300, -650, 0), FVector(300, 650, 0) };
    ControlOffsets = { FVector(-400, -600, 0), FVector(500, 0, 0), FVector(-400, 600, 0) };
}

void ADemoRoomAnchor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ADemoRoomAnchor, PreparedGeneration);
}

bool ADemoRoomAnchor::MakeSnapshot(FDemoAreaSnapshot& Out, FString& Error, const FBox* AuthoredLocalBounds) const
{
    DEMO_LOG_TICK();
    Error.Reset();
    TArray<FVector> Offsets = { CombatPlayerOffset, PreparedPlayerOffset, ShopOffset, NextOffset }; // 本次真实使用的玩家/任务局部点；新房间出生另读精确标记，不以旧椭圆替代。
    if (!AuthoredLocalBounds) { Offsets.Add(Geometry.CampaignOffset); Offsets.Add(Geometry.BossOffset); } // 旧地图仍完整验证原出生几何。
    Offsets.Append(DeviceOffsets); Offsets.Append(ControlOffsets);
    bool bValid = DeviceOffsets.Num() == 2 && ControlOffsets.Num() == 3 && !GetActorTransform().ContainsNaN() && GetActorScale3D().Equals(FVector::OneVector)
        && !Geometry.CampaignRadius.ContainsNaN() && !Geometry.ReinforcementRadius.ContainsNaN()
        && Geometry.CampaignRadius.X > 0 && Geometry.CampaignRadius.Y > 0 && Geometry.CampaignRadius.GetMax() <= 4000
        && Geometry.ReinforcementRadius.X > 0 && Geometry.ReinforcementRadius.Y > 0 && Geometry.ReinforcementRadius.GetMax() <= 4000
        && FMath::IsFinite(Geometry.ReinforcementHeight) && Geometry.ReinforcementHeight > 0 && Geometry.ReinforcementHeight <= 1000;
    if (AuthoredLocalBounds) bValid &= AuthoredLocalBounds->IsValid && !AuthoredLocalBounds->Min.ContainsNaN() && !AuthoredLocalBounds->Max.ContainsNaN();
    for (const FVector& Point : Offsets) bValid &= !Point.ContainsNaN() && (AuthoredLocalBounds ? AuthoredLocalBounds->ExpandBy(2.f).IsInsideOrOn(Point) : Point.SizeSquared() <= FMath::Square(4000.f));
    if (!bValid) { Error = TEXT("房间锚点数量、局部坐标、缩放或出生几何无效"); return false; }
    FDemoAreaSnapshot Candidate; // 全量合法才赋值，失败不把一半新坐标交给Flow。
    Candidate.Transform = GetActorTransform(); Candidate.Geometry = Geometry; Candidate.bRoomAnchors = true;
    Candidate.CombatStart = Candidate.Transform.TransformPosition(CombatPlayerOffset); Candidate.PreparedStart = Candidate.Transform.TransformPosition(PreparedPlayerOffset);
    Candidate.ShopAnchor = Candidate.Transform.TransformPosition(ShopOffset); Candidate.NextAnchor = Candidate.Transform.TransformPosition(NextOffset);
    for (const FVector& Point : DeviceOffsets) Candidate.DeviceAnchors.Add(Candidate.Transform.TransformPosition(Point));
    for (const FVector& Point : ControlOffsets) Candidate.ControlAnchors.Add(Candidate.Transform.TransformPosition(Point));
    Out = MoveTemp(Candidate); return true;
}

void ADemoRoomAnchor::SetPreparedGeneration(int32 Generation)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || Generation <= 0) { UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_ANCHOR rejected authority/generation")); return; }
    if (PreparedGeneration == Generation) return;
    PreparedGeneration = Generation; ForceNetUpdate();
}

void ADemoRoomAnchor::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    if (!GetWorld() || !GetWorld()->IsGameWorld() || GetNetMode()==NM_DedicatedServer)
    { UE_LOG(LogFPSDemo,Log,TEXT("ROOM_ATMO_SKIP reason=NoRenderGameWorld anchor=%s"),*GetName()); SetActorTickEnabled(false); return; }
    SetupRoomExposure(); // 未纳入的旧房间必须在原时机/原范围/原四字段生效，不能等待新Active门。
    ResolveAtmosphereScope(); // 异步BeginPlay上下文未写回时只保旧，下一Tick再核可信Level身份。
}

void ADemoRoomAnchor::ResolveAtmosphereScope()
{
    DEMO_LOG_TICK();
    if (bAtmosphereAttempted) return;
    UDemoRoomStreamingSubsystem* Streaming=GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>(); // 当前World拥有，栈内借用。
    FName RoomId; FTransform SlotTransform; bool bCommitted=false; // 仅本地视觉身份，不参与准备提交。
    if (!Streaming || !Streaming->GetRoomRenderContext(GetLevel(),RoomId,SlotTransform,bCommitted)) return;
    bAtmosphereAttempted=true;
    if (!GetDefault<UDemoRoomStreamingSettings>()->UsesPhaseAAtmosphere(RoomId))
    { UE_LOG(LogFPSDemo,Log,TEXT("ROOM_ATMO_SKIP room=%s reason=LegacyLifecyclePreserved"),*RoomId.ToString()); SetActorTickEnabled(false); return; }
    bUsingPhaseA=true;
    DestroyExposure(TEXT("ExplicitPhaseATakeover")); // 先撤原件，再完整验证新档；显式坏包必须REJECT，不悄悄fallback。
    SetupRoomAtmosphere(RoomId,SlotTransform);
    UpdateLocalAtmosphere(bCommitted);
}

void ADemoRoomAnchor::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (!bAtmosphereAttempted) ResolveAtmosphereScope();
    if (!bUsingPhaseA) return; // 原曝光继续由其Level生命周期管理，不套新Active/Retiring门。
    UDemoRoomStreamingSubsystem* Streaming=GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>();
    FName RoomId; FTransform SlotTransform; bool bCommitted=false;
    const bool bKnown=Streaming && Streaming->GetRoomRenderContext(GetLevel(),RoomId,SlotTransform,bCommitted);
    UpdateLocalAtmosphere(bKnown && bCommitted);
}

void ADemoRoomAnchor::SetupRoomAtmosphere(FName RoomId, const FTransform& SlotTransform)
{
    DEMO_LOG_CALL();
    const UDemoRoomStreamingSettings* Settings=GetDefault<UDemoRoomStreamingSettings>(); // 安装只读配置，不信任网络资源路径。
    FString Error; // 同步失败原因，非法Profile不静默回退。
    ResolvedProfile=const_cast<UDemoRoomAtmosphereProfile*>(Settings->ResolveAtmosphere(RoomId,Error));
    if (!Error.IsEmpty()) { UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT anchor=%s reason=%s"),*GetName(),*Error); return; }
    if (!ResolvedProfile) { UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=MissingExplicitProfile room=%s"),*RoomId.ToString()); return; }
    if (!ResolvedProfile->bEnabled)
    { UE_LOG(LogFPSDemo,Log,TEXT("ROOM_ATMO_SKIP reason=ExplicitProfileDisabled room=%s"),*RoomId.ToString()); return; }
    FPostProcessSettings PP; // 白名单输出，不携带任意材质或渲染方法override。
    if (!ResolvedProfile->BuildSettings(PP,Error)) { UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT anchor=%s reason=%s"),*GetName(),*Error); return; }
    const float Margin=ResolvedProfile->BoundsMarginCm, Blend=ResolvedProfile->BlendRadiusCm, Priority=ResolvedProfile->Priority; // cm/cm/PP排序，不进网络。
    FBox Bounds(ForceInit); // 只读本Level阻挡几何，不包含其他槽/Hub或编辑器图标。
    if (const ULevel* Level=GetLevel())
        for (const AActor* Actor:Level->Actors)
        {
            if (!IsValid(Actor)) continue;
            TInlineComponentArray<UStaticMeshComponent*> Meshes; Actor->GetComponents(Meshes); // 当前Actor同步借用组件数组。
            for (const UStaticMeshComponent* Mesh:Meshes)
                if (IsValid(Mesh) && Mesh->IsRegistered() && Mesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision && Mesh->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block)
                {
                    const FBox Box=Mesh->Bounds.GetBox(); // 实际世界cm，与安全回收占地同口径。
                    if (Box.IsValid && !Box.Min.ContainsNaN() && !Box.Max.ContainsNaN()) Bounds+=Box;
                }
        }
    if (!Bounds.IsValid || Bounds.GetExtent().GetMax()>50000 || !FMath::IsFinite(Margin) || Margin<0 || Margin>2000
        || !FMath::IsFinite(Blend) || Blend<0 || Blend>2000 || !FMath::IsFinite(Priority) || Priority<0 || Priority>=10 || Settings->SlotLocations.Num()!=2)
    { UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=InvalidBoundsOrBlend room=%s"),*RoomId.ToString()); return; }
    const FBox Mixed=Bounds.ExpandBy(Margin+Blend); // 整个盒外混合带也必须远离Hub和另一个槽，不只核中心。
    bool bOwnSlot=false; // 必须精确属于可信目录槽。
    for (const FVector& Slot:Settings->SlotLocations)
    {
        if (Slot.Equals(SlotTransform.GetLocation())) { bOwnSlot=true; continue; }
        const FVector Separation=Slot-SlotTransform.GetLocation(); // 邻槽方向及半平面距离，cm。
        const FVector Axis=Separation.GetSafeNormal(); // 单位向量，AABB投影半径为extent·abs(axis)。
        const double Furthest=FVector::DotProduct(Mixed.GetCenter()-SlotTransform.GetLocation(),Axis)+FVector::DotProduct(Mixed.GetExtent(),Axis.GetAbs());
        if (Separation.Size()<10000 || Furthest>=Separation.Size()*0.5-1000)
        { UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=CrossSlotBlend room=%s"),*RoomId.ToString()); return; }
    }
    // 项目Hub东界约-3160cm；X=10000是保守常驻区禁入线，绝不随Profile扩大到Hub。
    if (!bOwnSlot || Mixed.Min.X<10000)
    { UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=PersistentHubOrUnknownSlot room=%s"),*RoomId.ToString()); return; }
    if (ResolvedProfile && ResolvedProfile->LocalFog.bEnabled)
    {
        const FDemoRoomLocalFogSettings& Fog=ResolvedProfile->LocalFog; // 球体须在未扩展的真实几何盒内。
        const FVector Center=Bounds.GetCenter()+Fog.CenterOffsetCm;
        if (!Bounds.IsInsideOrOn(Center-FVector(Fog.RadiusCm)) || !Bounds.IsInsideOrOn(Center+FVector(Fog.RadiusCm)))
        { UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=FogOutsideRoom room=%s"),*RoomId.ToString()); return; }
    }
    RoomGeometryBounds=Bounds;
    ExposureBounds=NewObject<UBoxComponent>(this,TEXT("RoomExposureBounds"),RF_Transient);
    ExposureBounds->SetupAttachment(GetRootComponent());
    ExposureBounds->SetUsingAbsoluteLocation(true); ExposureBounds->SetUsingAbsoluteRotation(true); ExposureBounds->SetUsingAbsoluteScale(true);
    ExposureBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision); ExposureBounds->SetGenerateOverlapEvents(false); ExposureBounds->SetCanEverAffectNavigation(false);
    ExposureBounds->SetHiddenInGame(true); ExposureBounds->SetWorldLocationAndRotation(Bounds.GetCenter(),FRotator::ZeroRotator);
    ExposureBounds->SetBoxExtent(Bounds.ExpandBy(Margin).GetExtent(),false); ExposureBounds->RegisterComponent();
    ExposureVolume=NewObject<UPostProcessComponent>(this,TEXT("RoomExposureVolume"),RF_Transient);
    ExposureVolume->SetupAttachment(ExposureBounds); // Shape父级是有界PP必要条件，不能直接挂ActorRoot。
    ExposureVolume->bUnbound=false; ExposureVolume->bEnabled=false; ExposureVolume->BlendRadius=Blend; ExposureVolume->BlendWeight=1; ExposureVolume->Priority=Priority;
    ExposureVolume->Settings=PP; ExposureVolume->RegisterComponent();
    UE_LOG(LogFPSDemo,Log,TEXT("ROOM_ATMO_READY room=%s profile=%s center=%s extent=%s blend=%g pending_disabled=1"),*RoomId.ToString(),*GetNameSafe(ResolvedProfile),*Bounds.GetCenter().ToString(),*Bounds.GetExtent().ToString(),Blend);
}

void ADemoRoomAnchor::UpdateLocalAtmosphere(bool bRoomCommitted)
{
    DEMO_LOG_TICK();
    const bool bEnable=bRoomCommitted && IsValid(ExposureVolume); // 无有效组件/配置时不宣称恢复或激活成功。
    if (ExposureVolume) ExposureVolume->bEnabled=bEnable;
    if (bAtmosphereActive!=bEnable)
    {
        bAtmosphereActive=bEnable;
        UE_LOG(LogFPSDemo,Log,TEXT("%s anchor=%s reason=%s"),bEnable?TEXT("ROOM_ATMO_READY"):TEXT("ROOM_ATMO_RESTORE"),*GetName(),bEnable?TEXT("CommittedBoundedPPIncludingRetirement"):TEXT("InactiveOrReleased"));
    }
    bool bAllViewsInside=bEnable; int32 LocalViewCount=0; // 世界雾组件不能按分屏视图隔离，跨区域时保守不创建。
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC=It->Get(); // 当前World本机控制器，仅本帧借用。
        if (!PC || !PC->IsLocalController()) continue;
        ++LocalViewCount;
        if (!PC->PlayerCameraManager || !RoomGeometryBounds.IsInsideOrOn(PC->PlayerCameraManager->GetCameraLocation())) bAllViewsInside=false;
    }
    if (!bAllViewsInside || LocalViewCount==0 || !ResolvedProfile || !ResolvedProfile->LocalFog.bEnabled)
    { RestoreLocalFog(TEXT("OutsideRoomOrInactiveOrSplitViews")); return; }
    if (LocalFog) return;
    const FDemoRoomLocalFogSettings& Fog=ResolvedProfile->LocalFog; // 已完成范围校验的同版配置，运行中不热改。
    LocalFog=NewObject<ULocalFogVolumeComponent>(this,TEXT("RoomLocalFog"),RF_Transient);
    LocalFog->SetupAttachment(GetRootComponent());
    LocalFog->SetUsingAbsoluteLocation(true); LocalFog->SetUsingAbsoluteRotation(true); LocalFog->SetUsingAbsoluteScale(true);
    LocalFog->SetWorldLocationAndRotation(RoomGeometryBounds.GetCenter()+Fog.CenterOffsetCm,FRotator::ZeroRotator);
    LocalFog->SetWorldScale3D(FVector(Fog.RadiusCm/ULocalFogVolumeComponent::GetBaseVolumeSize())); // 原生500cm基球，严格均匀缩放。
    LocalFog->RadialFogExtinction=Fog.RadialExtinction; LocalFog->HeightFogExtinction=Fog.HeightExtinction;
    LocalFog->HeightFogFalloff=Fog.HeightFalloff; LocalFog->HeightFogOffset=Fog.HeightOffset; LocalFog->FogPhaseG=Fog.PhaseG;
    LocalFog->FogAlbedo=Fog.Albedo; LocalFog->FogEmissive=FLinearColor::Black;
    LocalFog->RegisterComponent();
    UE_LOG(LogFPSDemo,Log,TEXT("ROOM_ATMO_READY anchor=%s reason=LocalFogEntered radius_cm=%g"),*GetName(),Fog.RadiusCm);
}

void ADemoRoomAnchor::RestoreLocalFog(const TCHAR* Reason)
{
    DEMO_LOG_TICK();
    if (!LocalFog) return;
    LocalFog->DestroyComponent(); LocalFog=nullptr;
    UE_LOG(LogFPSDemo,Log,TEXT("ROOM_ATMO_RESTORE anchor=%s reason=%s component=LocalFog"),*GetName(),Reason);
}

bool ADemoRoomAnchor::HasLocalAtmosphereView() const
{
    DEMO_LOG_TICK();
    if (!bUsingPhaseA || !ExposureVolume || !ExposureVolume->IsRegistered() || !GetWorld()) return false;
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC=It->Get(); // 只保护当前World真正本机视图；远端相机由该端流送服务负责。
        if (!PC || !PC->IsLocalController()) continue;
        if (!PC->PlayerCameraManager) return true; // 已有本机控制器但相机还未就绪，保守保留有界旧图。
        if (ExposureVolume->EncompassesPoint(PC->PlayerCameraManager->GetCameraLocation(),ExposureVolume->BlendRadius,nullptr)) return true;
    }
    return false;
}

void ADemoRoomAnchor::DestroyExposure(const TCHAR* Reason)
{
    DEMO_LOG_CALL();
    if (ExposureVolume) { ExposureVolume->bEnabled=false; ExposureVolume->DestroyComponent(); ExposureVolume=nullptr; }
    if (ExposureBounds) { ExposureBounds->DestroyComponent(); ExposureBounds=nullptr; }
    UE_LOG(LogFPSDemo,Log,TEXT("ROOM_ATMO_RESTORE anchor=%s reason=%s component=OwnedPP"),*GetName(),Reason);
}

void ADemoRoomAnchor::EndPlay(EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); RestoreLocalFog(TEXT("EndPlay")); DestroyExposure(TEXT("EndPlay"));
    ResolvedProfile=nullptr; RoomGeometryBounds=FBox(ForceInit); bAtmosphereActive=false; bUsingPhaseA=false;
    UE_LOG(LogFPSDemo,Log,TEXT("ROOM_ATMO_RESTORE anchor=%s reason=EndPlay hub_parameters_untouched=1"),*GetName());
    Super::EndPlay(EndPlayReason);
}

// Phase A之外逐字复用改前已核算法；不得把新Profile门禁套在此函数。
void ADemoRoomAnchor::SetupRoomExposure()
{
    DEMO_LOG_CALL();
    // 设计意图：十关候选图只有局部灯+全局3lux太阳，默认Histogram(−10..20EV100)会把暗房抬成灰白，SpeedDown1又让Hub→房间需数秒适应。
    // 这里只在本房间实际占地内收窄曝光范围并加快适应；Hub Volume、全局太阳/天光/灯强度与其他PP字段全部不动。
    const UDemoRoomStreamingSettings* Settings = GetDefault<UDemoRoomStreamingSettings>();
    const FDemoRoomExposureProfile& Profile = Settings->RoomExposure; // 只读CDO配置；各端同一打包值，不经网络。
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld() || IsNetMode(NM_DedicatedServer)) { UE_LOG(LogFPSDemo, Log, TEXT("ROOM_EXPOSURE_SKIP reason=NoRenderGameWorld anchor=%s"), *GetName()); return; }
    if (!Profile.bEnabled) { UE_LOG(LogFPSDemo, Log, TEXT("ROOM_EXPOSURE_SKIP reason=Disabled anchor=%s"), *GetName()); return; }
    if (ExposureVolume) { UE_LOG(LogFPSDemo, Log, TEXT("ROOM_EXPOSURE_SKIP reason=AlreadyCreated anchor=%s"), *GetName()); return; }
    if (!(Profile.MinEV100 < Profile.MaxEV100) || !FMath::IsFinite(Profile.MinEV100) || !FMath::IsFinite(Profile.MaxEV100))
    { UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_EXPOSURE_REJECT reason=InvalidEVRange min=%g max=%g anchor=%s"), Profile.MinEV100, Profile.MaxEV100, *GetName()); return; }
    const ULevel* Level = GetLevel(); // 锚点所属流送Level；只扫描本房间Actor，不读其他房间或Hub。
    FBox Bounds(ForceInit); // 本房间可阻挡StaticMesh世界AABB（cm），与房间回收占地同口径，额外含屋顶/墙。
    if (Level)
        for (const AActor* Actor : Level->Actors)
        {
            if (!IsValid(Actor)) continue;
            TInlineComponentArray<UStaticMeshComponent*> Meshes; Actor->GetComponents(Meshes); // 只读已注册原生组件，不创建代理。
            for (const UStaticMeshComponent* Mesh : Meshes)
                if (IsValid(Mesh) && Mesh->IsRegistered() && Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision && Mesh->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
                {
                    const FBox Box = Mesh->Bounds.GetBox();
                    if (Box.IsValid && !Box.Min.ContainsNaN() && !Box.Max.ContainsNaN()) Bounds += Box;
                }
        }
    if (!Bounds.IsValid || Bounds.GetExtent().GetMax() > 50000.f)
    { UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_EXPOSURE_REJECT reason=NoValidRoomBounds valid=%d anchor=%s"), static_cast<int32>(Bounds.IsValid), *GetName()); return; }
    Bounds = Bounds.ExpandBy(FMath::Max(0.f, Profile.BoundsMarginCm));

    ExposureBounds = NewObject<UBoxComponent>(this, TEXT("RoomExposureBounds"), RF_Transient);
    ExposureBounds->SetupAttachment(GetRootComponent());
    ExposureBounds->SetUsingAbsoluteLocation(true); ExposureBounds->SetUsingAbsoluteRotation(true); ExposureBounds->SetUsingAbsoluteScale(true); // 世界轴对齐AABB，不随锚点旋转放大。
    ExposureBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision); ExposureBounds->SetGenerateOverlapEvents(false); ExposureBounds->SetCanEverAffectNavigation(false);
    ExposureBounds->SetHiddenInGame(true); ExposureBounds->SetWorldLocationAndRotation(Bounds.GetCenter(), FRotator::ZeroRotator);
    ExposureBounds->SetBoxExtent(Bounds.GetExtent(), false); // 无碰撞时PostProcessComponent回退到该Box的AABB距离判定。
    ExposureBounds->RegisterComponent();

    ExposureVolume = NewObject<UPostProcessComponent>(this, TEXT("RoomExposureVolume"), RF_Transient);
    ExposureVolume->SetupAttachment(ExposureBounds); // 父级必须是ShapeComponent，否则引擎视为全局生效。
    ExposureVolume->bUnbound = false; ExposureVolume->bEnabled = true;
    ExposureVolume->BlendRadius = FMath::Max(0.f, Profile.BlendRadiusCm); ExposureVolume->BlendWeight = 1.f; ExposureVolume->Priority = Profile.Priority;
    FPostProcessSettings& PP = ExposureVolume->Settings; // 只打开下面四个曝光override位，未覆盖字段沿用全局/Camera混合结果。
    PP.bOverride_AutoExposureMinBrightness = true; PP.AutoExposureMinBrightness = Profile.MinEV100;
    PP.bOverride_AutoExposureMaxBrightness = true; PP.AutoExposureMaxBrightness = Profile.MaxEV100;
    PP.bOverride_AutoExposureSpeedUp = true; PP.AutoExposureSpeedUp = Profile.SpeedUpEVPerSecond;
    PP.bOverride_AutoExposureSpeedDown = true; PP.AutoExposureSpeedDown = Profile.SpeedDownEVPerSecond;
    ExposureVolume->RegisterComponent(); // OnRegister把自身插入World PP列表；Actor随Level卸载时自动移除。

    UE_LOG(LogFPSDemo, Log, TEXT("ROOM_EXPOSURE_READY anchor=%s level=%s center=%s extent=%s ev=[%g,%g] speed_up=%g speed_down=%g blend=%g priority=%g"),
        *GetName(), *GetNameSafe(Level ? Level->GetOuter() : nullptr), *Bounds.GetCenter().ToString(), *Bounds.GetExtent().ToString(),
        Profile.MinEV100, Profile.MaxEV100, Profile.SpeedUpEVPerSecond, Profile.SpeedDownEVPerSecond, ExposureVolume->BlendRadius, ExposureVolume->Priority);
}
