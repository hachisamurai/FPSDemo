// 职责：正式Hub环境显式Editor Standalone运行回归。通过正常StartRun建立Hub，实读闭包、实际CMC走门、首Visibility命中与FScreenshotRequest。
// 不修改生产Bootstrap/资产/碰撞/查询/钱包/开奖结果；仅本GI临时槽，普通Editor/PIE/非Editor不会创建本Subsystem。
// 独立WallWashColorAB只改唯一墙洗灯温度；冻结/实读23灯并在所有退出路径恢复，不保存包或改变材质/全局光/PP/CVar。
#include "Tests/DemoHubEnvironmentIntegrationAudit.h"
#include "Characters/DemoCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/Texture.h"
#include "EngineUtils.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/FileManager.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopEnvironment.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Serialization/JsonSerializer.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UnrealClient.h"

namespace
{
    /** 将实际世界cm写为JSON数值，不从截图像素或作者目标反推尺寸。 */
    TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& Value)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        return {MakeShared<FJsonValueNumber>(Value.X),MakeShared<FJsonValueNumber>(Value.Y),MakeShared<FJsonValueNumber>(Value.Z)};
    }
    /** 只读追加一条有界测量；Owner为本次报告对象，不跨线程。 */
    void AppendFact(const TSharedPtr<FJsonObject>& Owner,const FString& Key,const TSharedPtr<FJsonObject>& Item)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        const TArray<TSharedPtr<FJsonValue>>* Existing=nullptr; TArray<TSharedPtr<FJsonValue>> Values; // 复制当前有界数组，再追加事实。
        if(Owner->TryGetArrayField(Key,Existing)) Values=*Existing;
        Values.Add(MakeShared<FJsonValueObject>(Item)); Owner->SetArrayField(Key,Values);
    }
    /** 同步只读当前灯真实属性；EntryId为闭包稳定ID，Light为已核同World借用组件；不读取目标值或渲染曝光历史。 */
    TSharedPtr<FJsonObject> CaptureLightRecord(const FName& EntryId,const ULocalLightComponent* Light)
    {
        DEMO_LOG_STATIC_CALL(Light);
        TSharedPtr<FJsonObject> Record=MakeShared<FJsonObject>(); // 本次独立实读对象；只返回数值/路径，不强持有灯。
        const FTransform Transform=Light->GetComponentTransform(); const FQuat Rotation=Transform.GetRotation(); // 实际组件世界cm/四元数/比例，非Entry作者目标。
        Record->SetStringField(TEXT("entry_id"),EntryId.ToString()); Record->SetStringField(TEXT("component"),Light->GetPathName());
        Record->SetStringField(TEXT("class"),Light->GetClass()->GetPathName()); Record->SetNumberField(TEXT("temperature_kelvin"),Light->Temperature);
        Record->SetNumberField(TEXT("intensity"),Light->Intensity); Record->SetNumberField(TEXT("units_enum"),static_cast<int32>(Light->GetLightUnits()));
        Record->SetBoolField(TEXT("use_temperature"),Light->bUseTemperature); Record->SetStringField(TEXT("light_color_rgba8_hex"),Light->LightColor.ToHex());
        Record->SetArrayField(TEXT("world_location_cm"),VectorJson(Transform.GetLocation())); Record->SetArrayField(TEXT("world_scale"),VectorJson(Transform.GetScale3D()));
        Record->SetArrayField(TEXT("world_rotation_xyzw"),{MakeShared<FJsonValueNumber>(Rotation.X),MakeShared<FJsonValueNumber>(Rotation.Y),MakeShared<FJsonValueNumber>(Rotation.Z),MakeShared<FJsonValueNumber>(Rotation.W)});
        Record->SetNumberField(TEXT("attenuation_radius_cm"),Light->AttenuationRadius); Record->SetNumberField(TEXT("mobility_enum"),static_cast<int32>(Light->Mobility));
        Record->SetBoolField(TEXT("visible"),Light->IsVisible()); Record->SetBoolField(TEXT("hidden_in_game"),Light->bHiddenInGame);
        Record->SetBoolField(TEXT("cast_shadows"),Light->CastShadows); Record->SetBoolField(TEXT("cast_static_shadows"),Light->CastStaticShadows);
        Record->SetBoolField(TEXT("cast_dynamic_shadows"),Light->CastDynamicShadows); Record->SetBoolField(TEXT("cast_volumetric_shadow"),Light->bCastVolumetricShadow);
        Record->SetNumberField(TEXT("shadow_bias"),Light->ShadowBias); Record->SetNumberField(TEXT("shadow_slope_bias"),Light->ShadowSlopeBias);
        Record->SetNumberField(TEXT("shadow_resolution_scale"),Light->ShadowResolutionScale); Record->SetNumberField(TEXT("contact_shadow_length"),Light->ContactShadowLength);
        Record->SetNumberField(TEXT("specular_scale"),Light->SpecularScale); Record->SetNumberField(TEXT("indirect_lighting_intensity"),Light->IndirectLightingIntensity);
        Record->SetNumberField(TEXT("volumetric_scattering_intensity"),Light->VolumetricScatteringIntensity); Record->SetNumberField(TEXT("inverse_exposure_blend"),Light->InverseExposureBlend);
        if(const UPointLightComponent* Point=Cast<UPointLightComponent>(Light)) // 原生Point形状和衰减实际值；不把Rect误记为Point。
        {
            Record->SetNumberField(TEXT("source_radius_cm"),Point->SourceRadius); Record->SetNumberField(TEXT("soft_source_radius_cm"),Point->SoftSourceRadius);
            Record->SetNumberField(TEXT("source_length_cm"),Point->SourceLength); Record->SetNumberField(TEXT("falloff_exponent"),Point->LightFalloffExponent);
            Record->SetBoolField(TEXT("inverse_squared_falloff"),Point->bUseInverseSquaredFalloff);
        }
        if(const URectLightComponent* Rect=Cast<URectLightComponent>(Light)) // 原生Rect尺寸/遮扉/源纹理只读冻结。
        {
            Record->SetNumberField(TEXT("source_width_cm"),Rect->SourceWidth); Record->SetNumberField(TEXT("source_height_cm"),Rect->SourceHeight);
            Record->SetNumberField(TEXT("barn_door_angle_deg"),Rect->BarnDoorAngle); Record->SetNumberField(TEXT("barn_door_length_cm"),Rect->BarnDoorLength);
            Record->SetStringField(TEXT("source_texture"),GetPathNameSafe(Rect->SourceTexture.Get()));
        }
        return Record;
    }
}

bool UDemoHubEnvironmentIntegrationAudit::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const UWorld* World=Cast<UWorld>(Outer); // 仅创建阶段借用当前World，不缓存到全局。
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone
        &&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoHubEnvironmentAudit"));
#else
    return false;
#endif
}
bool UDemoHubEnvironmentIntegrationAudit::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    return WorldType==EWorldType::Game;
#else
    return false;
#endif
}
void UDemoHubEnvironmentIntegrationAudit::OnWorldBeginPlay(UWorld& InWorld)
{
    DEMO_LOG_CALL(); Super::OnWorldBeginPlay(InWorld);
#if WITH_EDITOR
    if(InWorld.WorldType!=EWorldType::Game||InWorld.GetNetMode()!=NM_Standalone||!FParse::Param(FCommandLine::Get(),TEXT("game"))||!FParse::Param(FCommandLine::Get(),TEXT("DemoHubEnvironmentAudit")))
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ENV_RUNTIME_AUDIT_REJECT explicit isolated Editor Standalone required")); return; }
    bActive=true; bLightingAB=FParse::Param(FCommandLine::Get(),TEXT("DemoHubEnvironmentLightingAB"));
    bWallWashColorAB=FParse::Param(FCommandLine::Get(),TEXT("DemoHubEnvironmentWallWashColorAB"));
    StartedRealAt=FPlatformTime::Seconds(); NextRealAt=StartedRealAt+.5; Facts=MakeShared<FJsonObject>();
    Facts->SetBoolField(TEXT("lighting_ab_diagnostic"),bLightingAB);
    Facts->SetBoolField(TEXT("wallwash_color_ab_diagnostic"),bWallWashColorAB);
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_ENV_RUNTIME_AUDIT_MODE lighting_ab_diagnostic=%d wallwash_color_ab_diagnostic=%d"),bLightingAB,bWallWashColorAB);
    if(!CheckOutputBudget(TEXT("before_runtime_start"))) { Finish(false); return; } // 不在低余量盘启动本GI槽/图证写入。
    EvidenceDirectory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("TechnicalArt/HubShops20261115/FormalRuntime"),FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    Check(IFileManager::Get().MakeDirectory(*EvidenceDirectory,true),TEXT("create unique local non-progress evidence directory"));
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_ENV_RUNTIME_AUDIT_BEGIN directory=%s"),*EvidenceDirectory);
    if(!Check(!(bLightingAB&&bWallWashColorAB),TEXT("LightingAB and WallWashColorAB are mutually exclusive; combined switches rejected before StartRun or any light mutation"))) Finish(false);
#endif
}
bool UDemoHubEnvironmentIntegrationAudit::IsTickable() const { DEMO_LOG_TICK(); return bActive&&!bFinished&&!IsTemplate(); }
TStatId UDemoHubEnvironmentIntegrationAudit::GetStatId() const { DEMO_LOG_TICK(); RETURN_QUICK_DECLARE_CYCLE_STAT(UDemoHubEnvironmentIntegrationAudit,STATGROUP_Tickables); }
void UDemoHubEnvironmentIntegrationAudit::Deinitialize()
{
    DEMO_LOG_CALL(); bActive=false;
    RestoreDiagnosticLights(); // World仍有效时恢复；已完成路径幂等，不在销毁后的弱对象上补写。
    RestoreWallWashColorDiagnostic(); // 色温诊断独立恢复，仅目标温度可写；原强度诊断恢复函数不接管此快照。
    if(ADemoCharacter* Pawn=Player.Get()) { Pawn->ConsumeMovementInputVector(); if(UCharacterMovementComponent* Movement=Pawn->GetCharacterMovement()) Movement->StopMovementImmediately(); }
    Controller.Reset(); Player.Reset(); Slots.Reset(); GunTargets.Reset(); Super::Deinitialize();
}
bool UDemoHubEnvironmentIntegrationAudit::Check(bool bCondition,const FString& Message)
{
    DEMO_LOG_CALL();
    const FString Record=FString::Printf(TEXT("%s step=%d %s"),bCondition?TEXT("PASS"):TEXT("FAIL"),Step,*Message);
    Checks.Add(Record); UE_LOG(LogFPSDemo,Display,TEXT("HUB_ENV_RUNTIME_AUDIT %s"),*Record);
    if(!bCondition) bFailed=true;
    return bCondition;
}
bool UDemoHubEnvironmentIntegrationAudit::CheckOutputBudget(const FString& Stage)
{
    DEMO_LOG_CALL(); uint64 TotalBytes=0,FreeBytes=0; // 实际项目盘字节数，不沿用启动器旧快照。
    const bool bMeasured=FPlatformMisc::GetDiskTotalAndFreeSpace(FPaths::ProjectDir(),TotalBytes,FreeBytes);
    if(Facts.IsValid())
    {
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("stage"),Stage); Item->SetNumberField(TEXT("free_bytes"),static_cast<double>(FreeBytes)); Item->SetBoolField(TEXT("measurement_succeeded"),bMeasured);
        AppendFact(Facts,TEXT("disk_budget_checks"),Item);
    }
    return Check(bMeasured&&FreeBytes>=10ull*1024ull*1024ull*1024ull,TEXT("runtime output retains measured10GiB project-disk reserve: ")+Stage);
}
bool UDemoHubEnvironmentIntegrationAudit::ValidateActualHub(const UDemoHubShopDefinition* Definition)
{
    DEMO_LOG_CALL();
    TArray<ADemoHubShopEnvironmentRoot*> Roots; TArray<ADemoHubShopBlockout*> Blockouts; TArray<ADemoGunDisplay*> Displays; TArray<ADemoCasinoDevice*> Devices; // 当前World实数，不按标签猜Gameplay。
    for(TActorIterator<ADemoHubShopEnvironmentRoot> It(GetWorld());It;++It) Roots.Add(*It);
    for(TActorIterator<ADemoHubShopBlockout> It(GetWorld());It;++It) Blockouts.Add(*It);
    for(TActorIterator<ADemoGunDisplay> It(GetWorld());It;++It) Displays.Add(*It);
    for(TActorIterator<ADemoCasinoDevice> It(GetWorld());It;++It) Devices.Add(*It);
    if(!Check(Definition&&Roots.Num()==1&&Blockouts.Num()==1&&Displays.Num()==3&&Devices.Num()==7,TEXT("normal Hub owns exactly one environment root, one generated logical Blockout, three guns and seven devices"))) return false;
    ADemoHubShopEnvironmentRoot* Environment=Roots[0]; ADemoHubShopBlockout* Blockout=Blockouts[0]; // 本函数同步借用，被World拥有。
    HubFrame=Blockout->GetActorTransform(); FString Error;
    if(!Check(Environment->EnvironmentId==TEXT("hubshops.ta20261115.r01")&&Environment->Revision==1&&Environment->Entries.Num()==350&&Environment->GetActorTransform().Equals(HubFrame,.01f),TEXT("actual environment identity/revision/350 persistent entries agree with actual Hub frame"))
        ||!Check(Environment->ValidateEnvironment(Definition,HubFrame,Error),TEXT("actual native environment closure validation: ")+Error)
        ||!Check(Blockout->IsUsingAuthoredEnvironment()&&Blockout->GetGeometryCount()==16,TEXT("actual generated Blockout selects authored environment and preserves16 logical anchors"))) return false;
    TArray<UStaticMeshComponent*> LegacyMeshes; TArray<ULightComponent*> LegacyLights; TArray<UTextRenderComponent*> LegacyText;
    Blockout->GetComponents(LegacyMeshes); Blockout->GetComponents(LegacyLights); Blockout->GetComponents(LegacyText);
    if(!Check(LegacyMeshes.IsEmpty()&&LegacyLights.IsEmpty()&&LegacyText.IsEmpty(),TEXT("old generated walls/counter/backboard/signs/lights are zero, not hidden beneath new environment"))) return false;
    TSet<FName> MemberIds; int32 MeshCount=0,PointCount=0,RectCount=0; // 每个实际成员仅计一次，不含原Gameplay/样板。
    for(const FDemoHubShopEnvironmentEntry& Entry:Environment->Entries)
    {
        AActor* Member=Entry.Actor;
        if(!Check(IsValid(Member)&&!MemberIds.Contains(Entry.EntryId)&&!Member->IsHidden(),TEXT("actual environment member valid/unique/not fallback-hidden: ")+Entry.EntryId.ToString())) return false;
        MemberIds.Add(Entry.EntryId);
        USceneComponent* Component=Member->GetRootComponent();
        if(!Check(Component&&Component->IsRegistered()&&Component->IsVisible()&&!Component->bHiddenInGame,TEXT("actual environment component registered/visible: ")+Entry.EntryId.ToString())) return false;
        const FString ClassName=Member->GetClass()->GetPathName();
        if(ClassName==TEXT("/Script/Engine.StaticMeshActor")) ++MeshCount;
        else if(ClassName==TEXT("/Script/Engine.PointLight")) ++PointCount;
        else if(ClassName==TEXT("/Script/Engine.RectLight")) ++RectCount;
        else return Check(false,TEXT("unexpected actual authored environment class ")+ClassName);
    }
    if(!Check(MeshCount==327&&PointCount==8&&RectCount==15,TEXT("actual persistent members are327 Mesh/8Point/15Rect, one set only"))) return false;
    UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    if(!Check(Registry&&Registry->GetWorldEpoch()!=0,TEXT("normal authoritative Hub registry and epoch remain active"))) return false;
    GunTargets.Reset();
    for(const FDemoHubDisplayBinding& Binding:Definition->Displays)
    {
        ADemoGunDisplay* Display=Registry->FindDisplay(Binding.DisplayId);
        if(!Check(Display&&Display->GetActorTransform().Equals(Binding.Transform*HubFrame,.01f),TEXT("original gun stableID/root preserved ")+Binding.DisplayId.ToString())) return false;
        GunTargets.Add(Display);
    }
    for(const FDemoHubCasinoBinding& Binding:Definition->Devices)
    {
        ADemoCasinoDevice* Device=Registry->FindDevice(Binding.DeviceId);
        if(!Check(Device&&Device->Mode==Binding.Mode&&Device->GetActorTransform().Equals(Binding.Transform*HubFrame,.01f),TEXT("original device stableID/Mode/root preserved ")+Binding.DeviceId.ToString())) return false;
        if(Binding.Mode==1) Slots=Device;
    }
    for(const FDemoHubShopAnchor& Anchor:Definition->Anchors)
    {
        FTransform Actual;
        if(!Check(Blockout->GetAnchorTransform(Anchor.AnchorId,Actual)&&Actual.Equals(Anchor.Transform*HubFrame,.01f),TEXT("logical anchor matches actual original DA ")+Anchor.AnchorId.ToString())) return false;
    }
    UDemoCasinoControlsComponent* Controls=Slots.IsValid()?Slots->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    if(!Check(Controls&&Controls->IsReady()&&Controls->GetModelProfile()!=nullptr,TEXT("actual original S01 formal Profile and query component remain ready"))) return false;
    ControlIds.Reset();
    for(int32 Digit=0;Digit<10;++Digit) ControlIds.Add(FName(*FString::Printf(TEXT("amount.digit.%d"),Digit)));
    ControlIds.Append({FName(TEXT("amount.clear")),FName(TEXT("amount.backspace")),FName(TEXT("amount.commit")),FName(TEXT("slots.pull")),FName(TEXT("device.exit"))});
    Facts->SetStringField(TEXT("world"),GetWorld()->GetPathName()); Facts->SetStringField(TEXT("environment_id"),Environment->EnvironmentId.ToString()); Facts->SetNumberField(TEXT("revision"),Environment->Revision);
    Facts->SetNumberField(TEXT("members"),Environment->Entries.Num()); Facts->SetNumberField(TEXT("meshes"),MeshCount); Facts->SetNumberField(TEXT("point_lights"),PointCount); Facts->SetNumberField(TEXT("rect_lights"),RectCount);
    Facts->SetNumberField(TEXT("legacy_meshes"),LegacyMeshes.Num()); Facts->SetNumberField(TEXT("legacy_lights"),LegacyLights.Num()); Facts->SetNumberField(TEXT("logical_anchors"),Blockout->GetGeometryCount());
    Facts->SetNumberField(TEXT("actual_registered_guns"),Displays.Num()); Facts->SetNumberField(TEXT("actual_registered_devices"),Devices.Num()); Facts->SetArrayField(TEXT("actual_hub_origin_cm"),VectorJson(HubFrame.GetLocation()));
    return true;
}
bool UDemoHubEnvironmentIntegrationAudit::PlacePawn(const FVector& LocalPoint,const FVector& LocalLookAt)
{
    DEMO_LOG_CALL(); ADemoCharacter* Pawn=Player.Get(); ADemoPlayerController* PC=Controller.Get();
    if(!Check(Pawn&&PC&&PC->GetPawn()==Pawn,TEXT("capture/movement station uses current possessed Avatar"))) return false;
    const FVector Position=HubFrame.TransformPosition(LocalPoint),Look=HubFrame.TransformPosition(LocalLookAt);
    const FRotator BodyRotation(0.f,(Look-Position).Rotation().Yaw,0.f); // 只转角色水平朝向；俯仰仍归正常ControlRotation，不能人为倾斜胶囊过门。
    if(!Check(Pawn->TeleportTo(Position,BodyRotation),TEXT("real upright Pawn TeleportTo reaches requested audit start/capture station without penetration"))) return false;
    Pawn->ConsumeMovementInputVector(); Pawn->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation((Look-Pawn->GetSharedViewLocation()).Rotation());
    return true;
}
bool UDemoHubEnvironmentIntegrationAudit::BeginDoorWalk(bool bCasino,const UDemoHubShopDefinition* Definition)
{
    DEMO_LOG_CALL(); if(!Player.IsValid()||!Definition) return Check(false,TEXT("door movement inputs missing"));
    const FVector Center=bCasino?Definition->CasinoCenter:Definition->GunShopCenter,Size=bCasino?Definition->CasinoInteriorSize:Definition->GunShopInteriorSize;
    const double HalfHeight=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Start(Center.X-Size.X*.5-200.,Center.Y,HalfHeight+2.);
    if(!PlacePawn(Start,Start+FVector(650.,0.,65.))) return false;
    WalkName=bCasino?TEXT("casino_west_door"):TEXT("gunshop_west_door"); WalkStart=Player->GetActorLocation(); WalkDirection=HubFrame.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal(); WalkStartedAt=FPlatformTime::Seconds();
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_ENV_RUNTIME_WALK_BEGIN name=%s start=%s direction=%s target_progress_cm=650"),*WalkName,*WalkStart.ToString(),*WalkDirection.ToString());
    return true;
}
bool UDemoHubEnvironmentIntegrationAudit::AdvanceDoorWalk()
{
    DEMO_LOG_TICK(); ADemoCharacter* Pawn=Player.Get(); if(!Pawn) return Check(false,TEXT("Pawn lost during actual door walk"));
    const double Progress=FVector::DotProduct(Pawn->GetActorLocation()-WalkStart,WalkDirection),Seconds=FPlatformTime::Seconds()-WalkStartedAt;
    if(Progress>=650.)
    {
        Pawn->ConsumeMovementInputVector(); Pawn->GetCharacterMovement()->StopMovementImmediately();
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"),WalkName); Item->SetArrayField(TEXT("start_world_cm"),VectorJson(WalkStart)); Item->SetArrayField(TEXT("end_world_cm"),VectorJson(Pawn->GetActorLocation()));
        Item->SetNumberField(TEXT("actual_progress_cm"),Progress); Item->SetNumberField(TEXT("real_seconds"),Seconds); Item->SetStringField(TEXT("method"),TEXT("one collision-aware TeleportTo outside door, then normal CharacterMovement AddMovementInput; no end teleport or collision disabling")); AppendFact(Facts,TEXT("actual_walks"),Item);
        return Check(true,TEXT("normal CharacterMovement crossed actual west door: ")+WalkName);
    }
    if(Seconds>8.) return Check(false,FString::Printf(TEXT("door movement blocked/ignored after8s: %s progress=%.3f location=%s"),*WalkName,Progress,*Pawn->GetActorLocation().ToString()));
    Pawn->AddMovementInput(WalkDirection,1.f,false); // 尊重原Controller输入门，不强制绕过忽略输入/碰撞。
    return false;
}
void UDemoHubEnvironmentIntegrationAudit::BeginCapture(const FString& Name,int32 NextStep)
{
    DEMO_LOG_CALL(); PendingCaptureName=Name; PendingCapture=FPaths::Combine(EvidenceDirectory,Name+TEXT(".png"));
    CaptureStartedAt=FPlatformTime::Seconds(); CaptureStartedFrame=FrameCount; CaptureStartedEngineFrame=GFrameCounter; CaptureRequestedAt=0.; CaptureNextStep=NextStep; bCaptureRequested=false;
}
bool UDemoHubEnvironmentIntegrationAudit::AdvanceCapture()
{
    DEMO_LOG_TICK();
    const double Seconds=FPlatformTime::Seconds()-CaptureStartedAt;
    if(!bCaptureRequested)
    {
        const bool bDiagnosticCapture=bLightingAB||bWallWashColorAB; // 两种互斥诊断都用原3秒/120实际Tick和引擎帧门，默认Audit仍0.4秒。
        if(Seconds<(bDiagnosticCapture?3.:.4)||(bDiagnosticCapture&&(FrameCount-CaptureStartedFrame<120||GFrameCounter-CaptureStartedEngineFrame<120))) return false;
        if(bWallWashColorAB&&!ValidateWallWashColorLights(Step==201?ColorOriginalTemperature:5000.f,TEXT("before_capture_")+PendingCaptureName)) return false; // 等待期间任何受保护灯属性漂移都拒绝采样。
        if(!CheckOutputBudget(TEXT("before_screenshot_")+PendingCaptureName)) return false;
        const FIntPoint ActualSize=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue; // 当前真正游戏Viewport，不凭启动参数猜尺寸。
        if(!Check(ActualSize==FIntPoint(1280,720),TEXT("fixed1280x720 actual viewport; no full-frame sequence or oversized capture"))) return false;
        if(!Check(!FScreenshotRequest::IsScreenshotRequested(),TEXT("no overlapping screenshot request"))) return false;
        FVector Eye; FRotator Rotation; Controller->GetPlayerViewPoint(Eye,Rotation);
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"),PendingCaptureName); Item->SetStringField(TEXT("file"),PendingCapture); Item->SetArrayField(TEXT("actual_view_world_cm"),VectorJson(Eye));
        Item->SetArrayField(TEXT("actual_view_pitch_yaw_roll_deg"),VectorJson(FVector(Rotation.Pitch,Rotation.Yaw,Rotation.Roll))); Item->SetNumberField(TEXT("fov_deg"),Controller->PlayerCameraManager?Controller->PlayerCameraManager->GetFOVAngle():0.f);
        if(bDiagnosticCapture)
        {
            const FName ViewId=(!bWallWashColorAB&&(Step==101||Step==105))?FName(TEXT("exterior")):FName(TEXT("gunshop")); // ColorAB仅同一枪店，不进入外观状态。
            const float Fov=Controller->PlayerCameraManager?Controller->PlayerCameraManager->GetFOVAngle():0.f; // 当前真实相机角度，不写入FOV伪配对。
            if(bWallWashColorAB&&!Check(FMath::IsNearlyEqual(Fov,90.f,.001f)&&Rotation.Equals(FRotator(-1.180738544503412,53.434149358551004,0.),.001),TEXT("ColorAB actual capture view retains fixed gunshop rotation and actual FOV90deg"))) return false;
            if(bWallWashColorAB?Step==201:Step<104) { DiagnosticOriginalViews.Add(ViewId,FTransform(Rotation,Eye)); DiagnosticOriginalFovs.Add(ViewId,Fov); }
            else
            {
                const FTransform* Original=DiagnosticOriginalViews.Find(ViewId); const float* OriginalFov=DiagnosticOriginalFovs.Find(ViewId); // 同步借用本夹具纯值快照。
                if(!Check(Original&&OriginalFov&&Eye.Equals(Original->GetLocation(),.01)&&Rotation.Equals(Original->Rotator(),.001)&&FMath::IsNearlyEqual(Fov,*OriginalFov,.001f),TEXT("AB actual eye<=0.01cm, rotation<=0.001deg and FOV<=0.001deg match original A view"))) return false;
                Item->SetNumberField(TEXT("ab_eye_delta_cm"),FVector::Dist(Eye,Original->GetLocation()));
                Item->SetNumberField(TEXT("ab_pitch_delta_deg"),FRotator::NormalizeAxis(Rotation.Pitch-Original->Rotator().Pitch));
                Item->SetNumberField(TEXT("ab_yaw_delta_deg"),FRotator::NormalizeAxis(Rotation.Yaw-Original->Rotator().Yaw));
                if(bWallWashColorAB)
                {
                    Item->SetArrayField(TEXT("ab_eye_delta_xyz_cm"),VectorJson(Eye-Original->GetLocation()));
                    Item->SetNumberField(TEXT("ab_roll_delta_deg"),FRotator::NormalizeAxis(Rotation.Roll-Original->Rotator().Roll)); Item->SetNumberField(TEXT("ab_fov_delta_deg"),Fov-*OriginalFov);
                }
            }
        }
        Item->SetStringField(TEXT("view_source"),TEXT("actual possessed Pawn/PlayerController view, ordinary FScreenshotRequest; no SceneCapture or replacement camera"));
        Item->SetNumberField(TEXT("actual_settle_real_seconds"),Seconds); Item->SetNumberField(TEXT("actual_settle_ticks"),static_cast<double>(FrameCount-CaptureStartedFrame));
        Item->SetNumberField(TEXT("actual_settle_engine_frames"),static_cast<double>(GFrameCounter-CaptureStartedEngineFrame));
        if(Controller->PlayerCameraManager)
        {
            const FMinimalViewInfo& View=Controller->PlayerCameraManager->GetCameraCacheView(); // 同步借用相机缓存，不保存跨帧引用；不是最终Volume混合或实时曝光标量。
            const FPostProcessSettings& Settings=View.PostProcessSettings;
            Item->SetNumberField(TEXT("camera_cached_pp_blend_weight"),View.PostProcessBlendWeight);
            Item->SetBoolField(TEXT("camera_cached_override_exposure_method"),Settings.bOverride_AutoExposureMethod);
            Item->SetNumberField(TEXT("camera_cached_exposure_method_enum"),static_cast<int32>(Settings.AutoExposureMethod));
            Item->SetBoolField(TEXT("camera_cached_override_exposure_bias"),Settings.bOverride_AutoExposureBias);
            Item->SetNumberField(TEXT("camera_cached_exposure_bias"),Settings.AutoExposureBias);
            Item->SetStringField(TEXT("exposure_measurement_limit"),TEXT("Camera cache configuration only; final volume blend and live eye-adaptation multiplier NOT_MEASURED"));
        }
        AppendFact(Facts,TEXT("requested_screenshots"),Item); FScreenshotRequest::RequestScreenshot(PendingCapture,false,false); CaptureRequestedFrame=FrameCount; CaptureRequestedAt=FPlatformTime::Seconds(); bCaptureRequested=true;
        return false;
    }
    if(FrameCount>CaptureRequestedFrame&&IFileManager::Get().FileSize(*PendingCapture)>1000)
    {
        if(bWallWashColorAB&&!ValidateWallWashColorLights(Step==201?ColorOriginalTemperature:5000.f,TEXT("after_written_")+PendingCaptureName)) return false; // PNG写盘后仍从真实灯复核，不能只保留写入瞬间的通过。
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"),PendingCaptureName); Item->SetStringField(TEXT("file"),PendingCapture); Item->SetNumberField(TEXT("bytes"),IFileManager::Get().FileSize(*PendingCapture));
        AppendFact(Facts,TEXT("actual_written_screenshots"),Item); Check(true,TEXT("actual normal game PNG written after request: ")+PendingCaptureName); Step=CaptureNextStep; NextRealAt=FPlatformTime::Seconds()+.1; return true;
    }
    if(FPlatformTime::Seconds()-CaptureRequestedAt>3.) return Check(false,TEXT("requested actual game screenshot did not write in3s after actual request: ")+PendingCaptureName);
    return false;
}
bool UDemoHubEnvironmentIntegrationAudit::ApplyLightingDiagnostic(bool bReduced)
{
    DEMO_LOG_CALL();
    if(!Check(bLightingAB,TEXT("local-light diagnostic requires explicit isolated AB mode"))) return false;
    ADemoHubShopEnvironmentRoot* Root=nullptr; int32 RootCount=0; // 当前World同步借用，仅从正式闭包找灯，不按Label或全局类枚举改灯。
    for(TActorIterator<ADemoHubShopEnvironmentRoot> It(GetWorld());It;++It) { Root=*It; ++RootCount; }
    if(!Check(RootCount==1&&Root&&Root->Entries.Num()==350,TEXT("AB changes only the unique original350-member environment"))) return false;
    if(DiagnosticLights.IsEmpty())
    {
        int32 Points=0,Rects=0; // 首次按实际精确原生类冻结8Point/15Rect，不能借任意全局灯补数。
        for(const FDemoHubShopEnvironmentEntry& Entry:Root->Entries)
        {
            ULocalLightComponent* Light=IsValid(Entry.Actor)?Entry.Actor->FindComponentByClass<ULocalLightComponent>():nullptr;
            if(!Light) continue;
            const FString ClassPath=Entry.Actor->GetClass()->GetPathName();
            if(ClassPath==TEXT("/Script/Engine.PointLight")) ++Points;
            else if(ClassPath==TEXT("/Script/Engine.RectLight")) ++Rects;
            else return Check(false,TEXT("AB refuses unexpected local-light class"));
            DiagnosticLights.Add(Light); DiagnosticLightIds.Add(Entry.EntryId); DiagnosticOriginalIntensities.Add(Light->Intensity);
        }
        if(!Check(Points==8&&Rects==15&&DiagnosticLights.Num()==23,TEXT("AB froze exact8Point/15Rect stableIds and original intensities"))) return false;
    }
    if(!RestoreDiagnosticLights()) return Check(false,TEXT("AB original light restore failed before next fixed stage"));
    for(int32 Index=0;Index<DiagnosticLights.Num();++Index)
    {
        ULocalLightComponent* Light=DiagnosticLights[Index].Get();
        if(!Check(Light&&Light->GetWorld()==GetWorld(),TEXT("AB frozen light still belongs to currentWorld"))) return false;
        const float Before=DiagnosticOriginalIntensities[Index],After=Before*(bReduced?.0625f:1.f); // 只按首次冻结原值计算，不把自动曝光假设当固定等价。
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("entry_id"),DiagnosticLightIds[Index].ToString()); Item->SetBoolField(TEXT("reduced_stage"),bReduced);
        Item->SetNumberField(TEXT("original_intensity"),Before); Item->SetNumberField(TEXT("requested_intensity"),After); Item->SetNumberField(TEXT("unchanged_units_enum"),static_cast<int32>(Light->GetLightUnits()));
        Item->SetNumberField(TEXT("unchanged_temperature_kelvin"),Light->Temperature); Item->SetBoolField(TEXT("unchanged_use_temperature"),Light->bUseTemperature);
        Light->SetIntensity(After); bDiagnosticLightsRestored=!bReduced;
        Item->SetNumberField(TEXT("applied_intensity"),Light->Intensity); AppendFact(Facts,TEXT("diagnostic_local_light_changes"),Item);
        if(!Check(Light->Intensity==After,TEXT("AB intensity setter readback matches requested value: ")+DiagnosticLightIds[Index].ToString())) return false; // Static等组件可能拒绝动态写；不能按目标值假报生效。
    }
    Facts->SetStringField(TEXT("diagnostic_scope"),TEXT("A original saved local lights; B only23 environment LocalLight intensities multiplied by1/16 in thisWorld memory. No assets saved, no temperature/material/global-light/exposure/CVar change. Auto exposure adaptation may respond: this is not a manual exposure equivalence or art acceptance."));
    Facts->SetStringField(TEXT("diagnostic_id"),TEXT("DIAGNOSTIC_LIGHTING_AB_NOT_ACCEPTANCE"));
    Facts->SetStringField(TEXT("unmeasured_exposure"),TEXT("Live eye-adaptation multiplier not measured;3s/120ticks is a fixed warm-up rule, not convergence proof. Same-processA-exterior,A-gunshop,B-exterior,B-gunshop retains exposure/history order."));
    return Check(DiagnosticLights.Num()==23,TEXT("AB applies original or original/16 to exactly23 local lights in memory only"));
}
bool UDemoHubEnvironmentIntegrationAudit::RestoreDiagnosticLights()
{
    DEMO_LOG_CALL();
    if(bDiagnosticLightsRestored) return true;
    bool bRestored=true; int32 Count=0; // 退出中尽力恢复全部仍有效同World灯；失效不写，也不伪称完整恢复。
    for(int32 Index=0;Index<DiagnosticLights.Num();++Index)
    {
        ULocalLightComponent* Light=DiagnosticLights[Index].Get();
        if(!Light||Light->GetWorld()!=GetWorld()||!DiagnosticOriginalIntensities.IsValidIndex(Index)) { bRestored=false; continue; }
        Light->SetIntensity(DiagnosticOriginalIntensities[Index]); bRestored&=Light->Intensity==DiagnosticOriginalIntensities[Index]; ++Count;
    }
    bDiagnosticLightsRestored=bRestored&&Count==23;
    if(Facts.IsValid()) { Facts->SetBoolField(TEXT("diagnostic_original_lights_restored"),bDiagnosticLightsRestored); Facts->SetNumberField(TEXT("diagnostic_restored_lights"),Count); }
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_ENV_LIGHTING_AB_RESTORE complete=%d actual_count=%d"),bDiagnosticLightsRestored,Count);
    return bDiagnosticLightsRestored;
}
bool UDemoHubEnvironmentIntegrationAudit::ApplyWallWashColorDiagnostic(bool bCandidate)
{
    DEMO_LOG_CALL();
    if(!Check(bWallWashColorAB&&!bLightingAB,TEXT("WallWash color diagnostic requires its exclusive explicit isolated mode"))) return false;
    if(ColorDiagnosticLights.IsEmpty())
    {
        ADemoHubShopEnvironmentRoot* Root=nullptr; int32 RootCount=0; // 同步借用唯一原正式闭包，不遍历或修改全局光。
        for(TActorIterator<ADemoHubShopEnvironmentRoot> It(GetWorld());It;++It) { Root=*It; ++RootCount; }
        if(!Check(RootCount==1&&Root&&Root->Entries.Num()==350,TEXT("ColorAB freezes only the unique original350-member environment"))) return false;
        int32 Points=0,Rects=0,Targets=0; // 当前闭包精确8Point/15Rect、唯一稳定墙洗ID；不以数量替代身份。
        for(const FDemoHubShopEnvironmentEntry& Entry:Root->Entries)
        {
            ULocalLightComponent* Light=IsValid(Entry.Actor)?Entry.Actor->FindComponentByClass<ULocalLightComponent>():nullptr; // 当前Actor拥有，只冻结弱引用。
            if(!Light) continue;
            const FString ClassPath=Entry.Actor->GetClass()->GetPathName(); // 原生Actor类须与已核闭包相同。
            if(ClassPath==TEXT("/Script/Engine.PointLight")) ++Points;
            else if(ClassPath==TEXT("/Script/Engine.RectLight")) ++Rects;
            else return Check(false,TEXT("ColorAB refuses unexpected local-light class"));
            if(!Check(Light->GetWorld()==GetWorld(),TEXT("ColorAB snapshot light belongs to currentWorld: ")+Entry.EntryId.ToString())) return false;
            ColorDiagnosticLights.Add(Light); ColorDiagnosticLightIds.Add(Entry.EntryId); ColorOriginalLightRecords.Add(CaptureLightRecord(Entry.EntryId,Light));
            AppendFact(Facts,TEXT("color_ab_original_lights"),ColorOriginalLightRecords.Last());
            if(Entry.EntryId==FName(TEXT("ta.hub.20261115.r01.Gun_WallWash"))) { ++Targets; ColorTargetIndex=ColorDiagnosticLights.Num()-1; ColorOriginalTemperature=Light->Temperature; }
        }
        if(!Check(Points==8&&Rects==15&&ColorDiagnosticLights.Num()==23&&Targets==1&&ColorDiagnosticLights.IsValidIndex(ColorTargetIndex),TEXT("ColorAB froze23 actual local-light records and exactly one Gun_WallWash EntryId"))) return false;
        bWallWashColorRestored=false; // 即使A后失败，也必须在退出核对目标原温度及所有23灯实际原态。
        const ULocalLightComponent* Target=ColorDiagnosticLights[ColorTargetIndex].Get(); // 上述同步冻结确认有效，非跨帧强引用。
        if(!Check(Target&&ColorOriginalTemperature==3400.f&&Target->Intensity==3000.f&&Target->bUseTemperature,TEXT("ColorAB original target actually3400K/intensity3000/use-temperature enabled; no intensity correction permitted"))) return false;
        Facts->SetStringField(TEXT("diagnostic_id"),TEXT("DIAGNOSTIC_WALLWASH_COLOR_AB_NOT_ACCEPTANCE"));
        Facts->SetStringField(TEXT("diagnostic_scope"),TEXT("A original Gun_WallWash3400K; B only the unique ta.hub.20261115.r01.Gun_WallWash temperature5000K using SetTemperature. All23 local-light actual property records checked, including target intensity3000/units/use-temperature/LightColor/transform/range/shadows/shape. Other22 lights, materials, global lights, PP and CVars receive no writes. In-memory diagnostic only; no package save, formal candidate or art acceptance."));
        Facts->SetStringField(TEXT("unmeasured_exposure"),TEXT("Final PP volume blend and live eye-adaptation multiplier NOT_MEASURED. Fixed3s/120ticks/GFrameCounter warm-up does not prove convergence or identical exposure. Same-process A-gunshop then B-gunshop retains A-to-B rendering/exposure history."));
    }
    if(!ValidateWallWashColorLights(ColorOriginalTemperature,bCandidate?TEXT("before_b_original_state"):TEXT("a_original_state"))) return false;
    if(bCandidate)
    {
        ULocalLightComponent* Target=ColorDiagnosticLights.IsValidIndex(ColorTargetIndex)?ColorDiagnosticLights[ColorTargetIndex].Get():nullptr; // 跨帧弱引用重新核World再写。
        if(!Check(Target&&Target->GetWorld()==GetWorld(),TEXT("ColorAB unique target remains valid in currentWorld before temperature setter"))) return false;
        TSharedPtr<FJsonObject> Change=MakeShared<FJsonObject>(); // 只记录实际前值/请求/写后读回，不据目标假报生效。
        Change->SetStringField(TEXT("entry_id"),ColorDiagnosticLightIds[ColorTargetIndex].ToString()); Change->SetNumberField(TEXT("original_temperature_kelvin"),ColorOriginalTemperature);
        Change->SetNumberField(TEXT("before_temperature_kelvin"),Target->Temperature); Change->SetNumberField(TEXT("requested_temperature_kelvin"),5000.f);
        Target->SetTemperature(5000.f); bWallWashColorRestored=false; // 唯一候选写入；禁止SetIntensity或其他setter假修色温。
        Change->SetNumberField(TEXT("actual_after_temperature_kelvin"),Target->Temperature); Change->SetNumberField(TEXT("actual_after_intensity"),Target->Intensity); AppendFact(Facts,TEXT("color_ab_temperature_changes"),Change);
        if(!Check(Target->Temperature==5000.f,TEXT("ColorAB SetTemperature5000 actual readback equals requested candidate"))) return false;
    }
    return ValidateWallWashColorLights(bCandidate?5000.f:ColorOriginalTemperature,bCandidate?TEXT("b_after_set_actual_state"):TEXT("a_unchanged_actual_state"));
}
bool UDemoHubEnvironmentIntegrationAudit::ValidateWallWashColorLights(float ExpectedTemperature,const FString& Stage)
{
    DEMO_LOG_CALL();
    bool bAllMatch=ColorDiagnosticLights.Num()==23&&ColorDiagnosticLightIds.Num()==23&&ColorOriginalLightRecords.Num()==23&&ColorDiagnosticLights.IsValidIndex(ColorTargetIndex); // 结构先核，不能越界补数或丢灯假通过。
    int32 Compared=0,UnchangedNonTargets=0; // 本次实际比较数；其他22灯必须逐个完全相等。
    for(int32 Index=0;Index<ColorDiagnosticLights.Num();++Index)
    {
        ULocalLightComponent* Light=ColorDiagnosticLights[Index].Get(); // 每次重新核弱引用及当前World，卸载过程中绝不写旧对象。
        if(!Light||Light->GetWorld()!=GetWorld()||!ColorDiagnosticLightIds.IsValidIndex(Index)||!ColorOriginalLightRecords.IsValidIndex(Index)||!ColorOriginalLightRecords[Index].IsValid())
        {
            UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ENV_COLOR_AB_COMPARE invalid snapshot/light stage=%s index=%d"),*Stage,Index); bAllMatch=false; continue;
        }
        TSharedPtr<FJsonObject> Expected=MakeShared<FJsonObject>(*ColorOriginalLightRecords[Index]); // 仅复制期望对象后替换目标温度；原态及actual均不改。
        if(Index==ColorTargetIndex) Expected->SetNumberField(TEXT("temperature_kelvin"),ExpectedTemperature);
        const TSharedPtr<FJsonObject> Actual=CaptureLightRecord(ColorDiagnosticLightIds[Index],Light); // 同步实读当前属性，不能用Expected替代验证对象。
        const bool bMatches=FJsonValue::CompareEqual(FJsonValueObject(Expected),FJsonValueObject(Actual)); // UE5.4.4原生JSON逐字段/数组精确比较，无自造比较框架。
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); // 每阶段最多23项的可复核实际记录。
        Item->SetStringField(TEXT("stage"),Stage); Item->SetStringField(TEXT("entry_id"),ColorDiagnosticLightIds[Index].ToString()); Item->SetBoolField(TEXT("target"),Index==ColorTargetIndex);
        Item->SetBoolField(TEXT("matches_expected_actual_properties"),bMatches); Item->SetObjectField(TEXT("actual"),Actual); AppendFact(Facts,TEXT("color_ab_light_comparisons"),Item);
        if(!bMatches) UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ENV_COLOR_AB_COMPARE mismatch stage=%s id=%s"),*Stage,*ColorDiagnosticLightIds[Index].ToString());
        bAllMatch&=bMatches; ++Compared; if(Index!=ColorTargetIndex&&bMatches) ++UnchangedNonTargets;
    }
    TSharedPtr<FJsonObject> Summary=MakeShared<FJsonObject>(); // 明确记录22非目标不变与目标只有温度例外，含恢复阶段。
    Summary->SetStringField(TEXT("stage"),Stage); Summary->SetNumberField(TEXT("expected_target_temperature_kelvin"),ExpectedTemperature); Summary->SetNumberField(TEXT("compared_lights"),Compared);
    Summary->SetNumberField(TEXT("unchanged_non_target_lights"),UnchangedNonTargets); Summary->SetBoolField(TEXT("all23_actual_properties_match"),bAllMatch&&Compared==23&&UnchangedNonTargets==22); AppendFact(Facts,TEXT("color_ab_comparison_summaries"),Summary);
    return Check(bAllMatch&&Compared==23&&UnchangedNonTargets==22,TEXT("ColorAB actual23-lamp snapshot comparison; only target temperature exception: ")+Stage);
}
bool UDemoHubEnvironmentIntegrationAudit::RestoreWallWashColorDiagnostic()
{
    DEMO_LOG_CALL();
    if(ColorDiagnosticLights.IsEmpty()) return true; // 普通Audit/旧LightingAB没有本诊断快照；已冻结时每条退出路径仍重新实读，不能用历史通过跳过。
    ULocalLightComponent* Target=ColorDiagnosticLights.IsValidIndex(ColorTargetIndex)?ColorDiagnosticLights[ColorTargetIndex].Get():nullptr; // 只恢复仍有效同World目标，不触碰其他22灯。
    const bool bTargetValid=Target&&Target->GetWorld()==GetWorld(); // World销毁或目标失效必须留失败，不用旧引用补写。
    if(bTargetValid) Target->SetTemperature(ColorOriginalTemperature); // 幂等写首次实际原温度；绝不借恢复名义调强度。
    const bool bReadback=bTargetValid&&Target->Temperature==ColorOriginalTemperature; // setter之后独立实读，即使目标失效也继续核其余灯留证。
    const bool bOriginalState=ValidateWallWashColorLights(ColorOriginalTemperature,TEXT("restore_original_state"));
    bWallWashColorRestored=bReadback&&bOriginalState;
    if(Facts.IsValid())
    {
        Facts->SetBoolField(TEXT("color_ab_original_lights_restored"),bWallWashColorRestored); Facts->SetBoolField(TEXT("color_ab_restore_temperature_readback_matches"),bReadback);
        Facts->SetNumberField(TEXT("color_ab_restored_temperature_kelvin"),bTargetValid?Target->Temperature:-1.f);
    }
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_ENV_COLOR_AB_RESTORE complete=%d target_valid=%d original_kelvin=%.1f actual_kelvin=%.1f"),bWallWashColorRestored,bTargetValid,ColorOriginalTemperature,bTargetValid?Target->Temperature:-1.f);
    return bWallWashColorRestored;
}
bool UDemoHubEnvironmentIntegrationAudit::PrepareGunRay(int32 Index,const UDemoHubShopDefinition* Definition)
{
    DEMO_LOG_CALL();
    if(!Check(GunTargets.IsValidIndex(Index)&&GunTargets[Index].IsValid()&&Definition,TEXT("actual registered gun ray target exists"))) return false;
    const FVector Focus=GunTargets[Index]->GetFocusPoint(); CurrentRayPoint=Focus;
    const FVector LocalFocus=HubFrame.InverseTransformPosition(Focus);
    const double HalfHeight=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if(!PlacePawn(FVector(LocalFocus.X,LocalFocus.Y-180.,HalfHeight+2.),LocalFocus)) return false;
    RayStartedAt=FPlatformTime::Seconds(); NextRealAt=RayStartedAt+.2; return true;
}
bool UDemoHubEnvironmentIntegrationAudit::AdvanceGunRay()
{
    DEMO_LOG_TICK(); ADemoCharacter* Pawn=Player.Get(); ADemoPlayerController* PC=Controller.Get(); ADemoGunDisplay* Target=GunTargets.IsValidIndex(TargetIndex)?GunTargets[TargetIndex].Get():nullptr;
    if(!Pawn||!PC||!Target) return Check(false,TEXT("actual gun ray target/Avatar lost"));
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(HubEnvironmentGunAudit),false,Pawn); const FVector Eye=Pawn->GetSharedViewLocation(); const FRotator Aim=PC->GetControlRotation();
    const bool bHit=GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Aim.Vector()*250.,ECC_Visibility,Params);
    if(bHit&&Hit.GetActor()==Target)
    {
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("display_id"),Target->DisplayId.ToString()); Item->SetArrayField(TEXT("eye_world_cm"),VectorJson(Eye)); Item->SetArrayField(TEXT("actual_focus_world_cm"),VectorJson(CurrentRayPoint));
        Item->SetStringField(TEXT("first_actor"),GetNameSafe(Hit.GetActor())); Item->SetStringField(TEXT("first_component"),GetNameSafe(Hit.GetComponent())); Item->SetNumberField(TEXT("distance_cm"),Hit.Distance); AppendFact(Facts,TEXT("actual_gun_first_hits"),Item);
        return Check(true,TEXT("actual first Visibility obstruction is original gun display: ")+Target->DisplayId.ToString());
    }
    if(FPlatformTime::Seconds()-RayStartedAt>2.) return Check(false,TEXT("gun first Visibility hit obstructed/wrong: ")+Target->DisplayId.ToString()+TEXT(" first=")+GetNameSafe(Hit.GetActor())+TEXT("/")+GetNameSafe(Hit.GetComponent()));
    PC->SetControlRotation((CurrentRayPoint-Eye).Rotation()); ++RayAimCorrections; NextRealAt=FPlatformTime::Seconds()+.05; return false;
}
bool UDemoHubEnvironmentIntegrationAudit::PrepareControlRay(int32 Index)
{
    DEMO_LOG_CALL(); UDemoCasinoControlsComponent* Controls=Slots.IsValid()?Slots->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    if(!Check(Controls&&ControlIds.IsValidIndex(Index)&&Controls->GetControlPoint(ControlIds[Index],CurrentRayPoint),TEXT("actual S01 fixed query center exists"))) return false;
    Controller->SetControlRotation((CurrentRayPoint-Player->GetSharedViewLocation()).Rotation()); RayStartedAt=FPlatformTime::Seconds(); NextRealAt=RayStartedAt+.2; return true;
}
bool UDemoHubEnvironmentIntegrationAudit::AdvanceControlRay()
{
    DEMO_LOG_TICK(); ADemoCharacter* Pawn=Player.Get(); ADemoPlayerController* PC=Controller.Get(); UDemoCasinoControlsComponent* Controls=Slots.IsValid()?Slots->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    if(!Pawn||!PC||!Controls||!ControlIds.IsValidIndex(TargetIndex)) return Check(false,TEXT("actual S01 query/Avatar lost"));
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(HubEnvironmentControlAudit),false,Pawn); const FVector Eye=Pawn->GetSharedViewLocation(); const FRotator Aim=PC->GetControlRotation();
    const bool bHit=GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Aim.Vector()*250.,ECC_Visibility,Params); const FDemoCasinoControlBinding* Binding=bHit?Controls->FindHit(Hit.GetComponent()):nullptr;
    FString Error; const bool bValid=Controls->ValidateControlHit(Pawn,ControlIds[TargetIndex],Error);
    if(bValid&&Binding&&Binding->ControlId==ControlIds[TargetIndex]&&Hit.GetActor()==Slots.Get())
    {
        TSharedPtr<FJsonObject> Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("control_id"),ControlIds[TargetIndex].ToString()); Item->SetArrayField(TEXT("eye_world_cm"),VectorJson(Eye)); Item->SetArrayField(TEXT("query_world_cm"),VectorJson(CurrentRayPoint));
        Item->SetStringField(TEXT("first_actor"),GetNameSafe(Hit.GetActor())); Item->SetStringField(TEXT("first_component"),GetNameSafe(Hit.GetComponent())); Item->SetNumberField(TEXT("distance_cm"),Hit.Distance); AppendFact(Facts,TEXT("actual_control_first_hits"),Item);
        return Check(true,TEXT("actual S01 ValidateControlHit and first Visibility component match ")+ControlIds[TargetIndex].ToString());
    }
    if(FPlatformTime::Seconds()-RayStartedAt>2.) return Check(false,TEXT("S01 first Visibility hit blocked/wrong: ")+ControlIds[TargetIndex].ToString()+TEXT(" ")+Error+TEXT(" first=")+GetNameSafe(Hit.GetActor())+TEXT("/")+GetNameSafe(Hit.GetComponent()));
    PC->SetControlRotation((CurrentRayPoint-Eye).Rotation()); ++RayAimCorrections; NextRealAt=FPlatformTime::Seconds()+.05; return false;
}
void UDemoHubEnvironmentIntegrationAudit::Finish(bool bSuccess)
{
    DEMO_LOG_CALL(); if(bFinished) return; bFinished=true;
    if(!RestoreDiagnosticLights()) bSuccess=false; // 包括失败/90秒超时；恢复证据先写入，再计算本次采样状态。
    if(!RestoreWallWashColorDiagnostic()) bSuccess=false; // 独立色温恢复与23灯原态实读不能被旧强度恢复替代。
    if(ADemoCharacter* Pawn=Player.Get()) { Pawn->ConsumeMovementInputVector(); Pawn->GetCharacterMovement()->StopMovementImmediately(); }
    if(!Facts.IsValid()) Facts=MakeShared<FJsonObject>();
    Facts->SetStringField(TEXT("id"),bWallWashColorAB?TEXT("ART-FR-A01/COLOR"):(bLightingAB?TEXT("TA-HUB-20261115/SCENE/LIGHTING-AB"):TEXT("TA-HUB-20261115/INTEGRATION/RUNTIME")));
    Facts->SetStringField(TEXT("status"),bSuccess?(bWallWashColorAB?TEXT("DIAGNOSTIC_WALLWASH_COLOR_AB_NOT_ACCEPTANCE"):(bLightingAB?TEXT("PASS_DIAGNOSTIC_CAPTURE_NOT_ACCEPTANCE"):TEXT("PASS_LISTED_EDITOR_STANDALONE_SCOPE"))):TEXT("FAIL_ACTUAL_RUNTIME_AUDIT"));
    Facts->SetStringField(TEXT("scope"),bLightingAB?TEXT("Explicit Editor Standalone fixed exterior/gunshop A/B4PNGs, each at least3real seconds and120ticks. B only23 local environment light intensities*1/16, in memory/no save. No movement/LOS revalidation, no new asset/game/art acceptance."):TEXT("Normal StartRun Hub, exactly one persisted350-member environment, logical legacy appearance0/anchors16, original3guns7devices, actual CharacterMovement through two west doors, first Visibility hits to3guns+15S01queries, actual Pawn-view game PNGs. No transaction, hardware input, multiplayer, Cook, performance or art acceptance."));
    if(bWallWashColorAB)
    {
        Facts->SetBoolField(TEXT("diagnostic_contract_passed"),bSuccess); Facts->SetStringField(TEXT("diagnostic_id"),TEXT("DIAGNOSTIC_WALLWASH_COLOR_AB_NOT_ACCEPTANCE"));
        Facts->SetStringField(TEXT("scope"),TEXT("Explicit Editor -game Standalone normal StartRun/ValidateActualHub, then only two1280x720 same-gunshop Pawn screenshots A3400K then B5000K, each >=3real seconds/120ticks/120engine frames. Only unique Gun_WallWash SetTemperature in currentWorld;23 local-light actual records and paired actual camera/FOV checked, original temperature and23-lamp original state restored. No exterior, movement/LOS revalidation, package save, final exposure measurement or art/game acceptance.5000K is diagnostic only, not a formal value."));
    }
    Facts->SetNumberField(TEXT("step"),Step); Facts->SetNumberField(TEXT("elapsed_real_seconds"),FPlatformTime::Seconds()-StartedRealAt); Facts->SetNumberField(TEXT("actual_aim_corrections"),RayAimCorrections);
    TArray<TSharedPtr<FJsonValue>> Values; for(const FString& Entry:Checks) Values.Add(MakeShared<FJsonValueString>(Entry)); Facts->SetArrayField(TEXT("checks"),Values);
    Facts->SetArrayField(TEXT("not_run"),{MakeShared<FJsonValueString>(TEXT("actual transactions/Owner result/amount UI")),MakeShared<FJsonValueString>(TEXT("Listen2-4players/late client/load policy")),MakeShared<FJsonValueString>(TEXT("Cook/Shipping/GPU-CPU performance/art acceptance")),MakeShared<FJsonValueString>(TEXT("travel/returnHub reconstruction"))});
    FString Text; const bool bWritten=!EvidenceDirectory.IsEmpty()&&FJsonSerializer::Serialize(Facts.ToSharedRef(),TJsonWriterFactory<>::Create(&Text))&&FFileHelper::SaveStringToFile(Text,*FPaths::Combine(EvidenceDirectory,TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    const bool bPassed=bSuccess&&bWritten;
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_ENV_RUNTIME_AUDIT_%s directory=%s"),bPassed?TEXT("SUCCESS"):TEXT("FAILED"),*EvidenceDirectory);
#if WITH_EDITOR
    FPlatformMisc::RequestExitWithStatus(false,bPassed?0:1); // 仅创建门已核的本岗独立Standalone进程，不关用户Editor或他人进程。
#endif
}
void UDemoHubEnvironmentIntegrationAudit::Tick(float DeltaTime)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if(!bActive||bFinished) return; ++FrameCount;
    const double RealNow=FPlatformTime::Seconds();
    if(!bFailed&&RealNow-StartedRealAt>90.) Check(false,TEXT("bounded runtime audit exceeded90 real seconds"));
    if(bFailed) { Finish(false); return; }
    if(RealNow<NextRealAt) return;
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0)); ADemoCharacter* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); UDemoRunSaves* Saves=GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr;
    if(!PC||!Pawn||!Mode||!State||!Saves) { NextRealAt=RealNow+.1; return; }
    Controller=PC; Player=Pawn; UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>(); const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr;
    switch(Step)
    {
    case 0:
    {
        UDemoMenuFlowComponent* Flow=PC->GetMenuFlow(); if(!Flow) { NextRealAt=RealNow+.1; return; }
        Flow->UpdateRealtime(0.f); if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) Flow->DispatchUIAction(EDemoUIAction::OverlayBack);
        if(!Check(!Flow->HasBlockingOverlay(),TEXT("initial guide closed through normal overlay route"))||!Check(Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("normal local temporary slot and original StartRun enter Hub"))) return;
        Saves->ConsumePending(); PC->OnRunReady(); Step=1; NextRealAt=RealNow+.5; break;
    }
    case 1:
        if(State->Phase!=EDemoPhase::Hub||!Definition||!Registry||!Registry->GetWorldEpoch()) { NextRealAt=RealNow+.1; return; }
        if(!ValidateActualHub(Definition)) return;
        if(bWallWashColorAB) { Step=200; NextRealAt=RealNow+.1; break; } // 正常StartRun与唯一350根/旧外观0/16anchors通过后才可进入色温诊断。
        if(bLightingAB) { Step=100; NextRealAt=RealNow+.1; break; } // 同正常Hub建立后才进入独立诊断；默认回归路线不变。
        if(!PlacePawn(FVector(-2350,-2200,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.),FVector(-100,-100,200))) return;
        BeginCapture(TEXT("01_FormalHub_Exterior_PawnView"),3); Step=2; break;
    case 2: case 6: case 10: case 16: AdvanceCapture(); break;
    case 3:
        if(!BeginDoorWalk(true,Definition)) return; Step=4; break;
    case 4:
        if(AdvanceDoorWalk()) { PC->SetControlRotation((HubFrame.TransformPosition(FVector(900,-1000,160))-Pawn->GetSharedViewLocation()).Rotation()); BeginCapture(TEXT("02_FormalHub_Casino_PawnView"),7); Step=6; }
        break;
    case 7:
        if(!BeginDoorWalk(false,Definition)) return; Step=8; break;
    case 8:
        if(AdvanceDoorWalk()) { PC->SetControlRotation((HubFrame.TransformPosition(FVector(600,2090,155))-Pawn->GetSharedViewLocation()).Rotation()); BeginCapture(TEXT("03_FormalHub_GunShop_PawnView"),11); Step=10; }
        break;
    case 11:
        TargetIndex=0; if(!PrepareGunRay(TargetIndex,Definition)) return; Step=12; break;
    case 12:
        if(AdvanceGunRay()) { ++TargetIndex; if(TargetIndex<GunTargets.Num()) PrepareGunRay(TargetIndex,Definition); else { Step=13; NextRealAt=RealNow+.1; } }
        break;
    case 13:
    {
        if(!Slots.IsValid()) { Check(false,TEXT("S01 lost before ray stations")); return; }
        const FVector Stand=Slots->GetActorTransform().TransformPosition(FVector(180,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.));
        const FVector Focus=Slots->GetFocusPoint(); if(!PlacePawn(HubFrame.InverseTransformPosition(Stand),HubFrame.InverseTransformPosition(Focus))) return;
        TargetIndex=0; if(!PrepareControlRay(TargetIndex)) return; Step=14; break;
    }
    case 14:
        if(AdvanceControlRay()) { ++TargetIndex; if(TargetIndex<ControlIds.Num()) PrepareControlRay(TargetIndex); else { PC->SetControlRotation((Slots->GetFocusPoint()-Pawn->GetSharedViewLocation()).Rotation()); BeginCapture(TEXT("04_FormalHub_S01_ReadDistance_PawnView"),17); Step=16; } }
        break;
    case 17:
        if(!ValidateActualHub(Definition)) return; // 正常走路/首射线后仍唯一，不只是最初静帧。
        Finish(true); break;
    case 100: case 104:
        if(!Check(RestoreDiagnosticLights(),TEXT("AB restores original lights before fixed stage"))||!ValidateActualHub(Definition)||!ApplyLightingDiagnostic(Step==104)) return;
        if(!PlacePawn(FVector(-2350,-2200,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.),FVector(-100,-100,200))) return;
        PC->SetControlRotation(FRotator(.5595296898763855,43.12687656426448,0.)); // 仅AB冻结A01实际视向deg，消除上一镜头SharedView参与LookAt的约0.13度偏差；正常PlacePawn不改。
        BeginCapture(Step==100?TEXT("01_LightingAB_A_Exterior"):TEXT("03_LightingAB_B_Exterior"),Step+2); ++Step; break;
    case 102: case 106:
        if(!Check(RestoreDiagnosticLights(),TEXT("AB restores original lights before fixed stage"))||!ValidateActualHub(Definition)||!ApplyLightingDiagnostic(Step==106)) return;
        if(!PlacePawn(FVector(160.590491,1500,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.),FVector(600,2090,155))) return;
        PC->SetControlRotation(FRotator(-1.180738544503412,53.434149358551004,0.)); // 两轮同一个冻结视向；实际眼位/旋转/FOV在请求截图前再次核对。
        BeginCapture(Step==102?TEXT("02_LightingAB_A_GunShop"):TEXT("04_LightingAB_B_GunShop"),Step+2); ++Step; break;
    case 101: case 103: case 105: case 107: AdvanceCapture(); break;
    case 108: Finish(true); break; // 四固定图写入即结束，不把原门/Query子项复制到本诊断结果。
    case 200: case 202:
    {
        if(!ValidateActualHub(Definition)||!ApplyWallWashColorDiagnostic(Step==202)) return;
        const FVector Station(160.590491,1500,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.); // 复用旧AB枪店同Pawn站位cm，固定相机来自实际PC，不增替身。
        if(!PlacePawn(Station,FVector(600,2090,155))) return;
        if(!Check(Pawn->GetActorLocation().Equals(HubFrame.TransformPosition(Station),.01),TEXT("ColorAB actual Pawn remains at frozen oldAB gunshop station within0.01cm per component"))) return;
        PC->SetControlRotation(FRotator(-1.180738544503412,53.434149358551004,0.)); // A/B均显式相同PC视向；截图请求前实测旋转/FOV90及配对。
        BeginCapture(Step==200?TEXT("01_WallWashColorAB_A_3400K_GunShop"):TEXT("02_WallWashColorAB_B_5000K_GunShop"),Step+2); ++Step; break;
    }
    case 201: case 203: AdvanceCapture(); break;
    case 204:
        if(!ValidateActualHub(Definition)||!ValidateWallWashColorLights(5000.f,TEXT("after_b_before_restore"))||!RestoreWallWashColorDiagnostic()) return;
        Step=205; NextRealAt=RealNow+.1; break;
    case 205: Finish(true); break; // 两图及原态恢复合同完成，仍不是正式灯值、艺术通过或整体玩法验收。
    default: Check(false,TEXT("unexpected audit step")); break;
    }
#endif
}
