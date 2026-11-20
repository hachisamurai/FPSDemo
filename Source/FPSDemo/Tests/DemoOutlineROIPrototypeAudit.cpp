// 职责：T03独立full-view参考取证；复用UE5.4正常截图生命周期与Root公开桥，不重复实现捕获/合成。
// 使用：run_outline_roi_prototype.ps1；TA封版/明确释放后才能统一构建运行，本文件存在不代表候选资产已就绪。
#include "Tests/DemoOutlineROIPrototypeAudit.h"
#include "Tests/DemoOutlineROIPrototype.h"
#include "Characters/DemoCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h"
#include "Economy/DemoShopComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GAS/DemoAttributeSet.h" // Keycap操作会按生产规则禁用CanUsePlayerSkills；存活必须直接核实际Health/Eliminated。
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h"
#include "Interaction/HubShops/Casino/DemoCasinoLocalViewComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerState.h"
#include "Save/DemoRunSave.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UnrealClient.h"
#include "UObject/Package.h"

bool UDemoOutlineROIPrototypeAudit::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const UWorld* World=Cast<UWorld>(Outer); // 只真实持久Game，排除引擎启动临时World，旧A08不得同时启用。
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone&&World->GetOutermost()->GetName()==TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")&&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoOutlineROIPrototype"))&&!FParse::Param(FCommandLine::Get(),TEXT("DemoSlotsPointInputAudit"));
#else
    return false;
#endif
}
bool UDemoOutlineROIPrototypeAudit::DoesSupportWorldType(EWorldType::Type Type) const
{ DEMO_LOG_CALL(); return WITH_EDITOR&&Type==EWorldType::Game; }
void UDemoOutlineROIPrototypeAudit::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Super::Initialize(Collection);
#if WITH_EDITOR
    Started=FPlatformTime::Seconds(); bTemporalWitness=FParse::Param(FCommandLine::Get(),TEXT("DemoOutlineROITemporalWitness")); FString Target; if(FParse::Value(FCommandLine::Get(),TEXT("DemoOutlineROITarget="),Target)) { bKeycap=Target==TEXT("Keycap"); if(!bKeycap&&Target!=TEXT("RifleOblique")) Fail(TEXT("UNVERIFIED: target must be RifleOblique or Keycap")); } Collection.InitializeDependency<UDemoHubShopWorldSubsystem>(); Collection.InitializeDependency<UDemoOutlineROIPrototype>(); Bridge=GetWorld()->GetSubsystem<UDemoOutlineROIPrototype>();
#endif
}
void UDemoOutlineROIPrototypeAudit::OnWorldBeginPlay(UWorld& World)
{
    DEMO_LOG_CALL(); Super::OnWorldBeginPlay(World);
#if WITH_EDITOR
    FString Requested; FParse::Value(FCommandLine::Get(),TEXT("DemoOutlineROIOutput="),Requested); Directory=FPaths::ConvertRelativePathToFull(Requested); const FString Saved=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    if(Requested.IsEmpty()||!FPaths::IsUnderDirectory(Directory,Saved)||IFileManager::Get().DirectoryExists(*Directory)||!IFileManager::Get().MakeDirectory(*Directory,true)) { Fail(TEXT("UNVERIFIED: output must be a fresh directory inside project Saved")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    if(!FApp::CanEverRender()||!Cast<UDemoOutlineROIViewportClient>(World.GetGameViewport())||!Bridge.IsValid()||!Budget()) { Fail(TEXT("UNVERIFIED: actual render/special viewport/Root bridge/budget unavailable")); Finish(false); return; }
    TickHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&UDemoOutlineROIPrototypeAudit::OnPostTick); // GT弱UObject，只本World，结束精确解除。
    CleanupHandle=FWorldDelegates::OnWorldCleanup.AddUObject(this,&UDemoOutlineROIPrototypeAudit::OnWorldCleanup);
#endif
}
void UDemoOutlineROIPrototypeAudit::Deinitialize()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    bWorldTeardown=true; bKeyDown=false; if(bKeycap) InjectKeyPrimary(); StopMemoryCapture(); if(Bridge.IsValid()) { if(bTemporalWitness&&bPulseMaskBorrowed) Bridge->SetCaptureMaskWidthForAudit(2); Bridge->ClearTarget(TEXT("audit deinitialize")); Bridge->RestoreProcessSettings(); }
    if(bKeycap&&Controller.IsValid()&&Player.IsValid()&&KeyDevice.IsValid()&&Controller->GetPawn()==Player.Get()) if(UDemoCasinoDeviceInputComponent* Input=Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); Input&&Input->IsOperating()&&Input->GetCurrentDevice()==KeyDevice.Get()) Input->ExitOperation(false); // 先清桥再撤本地源，卸载不向旧World发释放、不动新租约。
    if(TickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle); TickHandle.Reset();
    if(CleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle); CleanupHandle.Reset();
#endif
    Super::Deinitialize();
}
#if WITH_EDITOR
void UDemoOutlineROIPrototypeAudit::Fail(const FString& Message)
{ DEMO_LOG_CALL(); if(!bFailed) { Failure=Message; UE_LOG(LogFPSDemo,Warning,TEXT("OUTLINE_ROI_AUDIT_UNVERIFIED reason=%s"),*Message); } bFailed=true; }
bool UDemoOutlineROIPrototypeAudit::Budget() const
{
    DEMO_LOG_CALL(); uint64 Total=0,Free=0; const bool Measured=FPlatformMisc::GetDiskTotalAndFreeSpace(FPaths::ProjectDir(),Total,Free);
    TArray<FString> Files; IFileManager::Get().FindFilesRecursive(Files,*Directory,TEXT("*"),true,false); int64 Bytes=0; int32 Images=0;
    for(const FString& File:Files) { Bytes+=FMath::Max<int64>(0,IFileManager::Get().FileSize(*File)); Images+=FPaths::GetExtension(File).Equals(TEXT("png"),ESearchCase::IgnoreCase); }
    FString Log; if(FParse::Value(FCommandLine::Get(),TEXT("abslog="),Log)&&!Files.Contains(Log)) Bytes+=FMath::Max<int64>(0,IFileManager::Get().FileSize(*Log));
    return Measured&&Free>=uint64(10)*1024*1024*1024&&Bytes<=64LL*1024*1024&&Images<=4;
}
void UDemoOutlineROIPrototypeAudit::OnWorldCleanup(UWorld* World,bool SessionEnded,bool CleanupResources)
{ DEMO_LOG_CALL(); if(World==GetWorld()&&!bFinished) { bWorldTeardown=true; Fail(TEXT("UNVERIFIED: unexpected World cleanup")); Finish(false); } }
bool UDemoOutlineROIPrototypeAudit::ReadTarget(FString& Error) const
{
    DEMO_LOG_TICK(); if(bKeycap) return ReadKeyTarget(Error); const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>(); const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    const UPrimitiveComponent* S=Source.Get(); const UPrimitiveComponent* M=Mask.Get();
    if(!Registry||!Registry->GetDefinition()||!Display.IsValid()||Registry->FindDisplay(TEXT("gunshop_rifle_01"))!=Display.Get()||!Display->IsDisplayReady()||Display->GetDisplayMesh()!=S||!S||!S->IsRegistered()||!Player.IsValid()||!Controller.IsValid()||Controller->GetPawn()!=Player.Get()||Controller->GetViewTarget()!=Player.Get()||!Controller->IsCoopRegistered()||Controller->IsPaused()||!Player->CanUsePlayerSkills()||!State||State->Phase!=EDemoPhase::Hub) { Error=TEXT("actual registered rifle/Pawn/Hub/Coop/alive gate"); return false; }
    FVector Eye; FRotator Rotation; Controller->GetPlayerViewPoint(Eye,Rotation); FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(OutlineROIReference),false,Player.Get());
    GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Rotation.Vector()*Registry->GetDefinition()->InteractionDistanceCm,ECC_Visibility,Query);
    if(Hit.GetActor()!=Display.Get()) { Error=TEXT("first actual Visibility hit is ")+GetNameSafe(Hit.GetActor()); return false; }
    int32 Count=0; TInlineComponentArray<UPrimitiveComponent*> Components(Player.Get()); for(UPrimitiveComponent* Component:Components) if(IsValid(Component)&&Component->IsRegistered()&&Component->bRenderCustomDepth&&Component->CustomDepthStencilValue==253&&Component->IsVisible()&&!Component->bHiddenInGame) ++Count;
    if(!M||!M->IsRegistered()||Count!=1||M->GetOwner()!=Player.Get()||!M->bOnlyOwnerSee||M->bRenderInMainPass||M->bRenderInDepthPass||!M->bRenderCustomDepth||M->CustomDepthStencilValue!=253||!M->IsVisible()||M->bHiddenInGame||M->GetCollisionEnabled()!=ECollisionEnabled::NoCollision) { Error=TEXT("unique production private253 mask flags/owner"); return false; }
    bool Same=false; if(const UStaticMeshComponent* Static=Cast<UStaticMeshComponent>(S)) { const UStaticMeshComponent* Clone=Cast<UStaticMeshComponent>(M); Same=Clone&&Static->GetStaticMesh()&&Clone->GetStaticMesh()==Static->GetStaticMesh(); }
    else if(const USkeletalMeshComponent* Skin=Cast<USkeletalMeshComponent>(S)) { const USkeletalMeshComponent* Clone=Cast<USkeletalMeshComponent>(M); Same=Clone&&Skin->GetSkeletalMeshAsset()&&Clone->GetSkeletalMeshAsset()==Skin->GetSkeletalMeshAsset()&&Clone->LeaderPoseComponent.Get()==Skin; }
    Same&=M->GetNumMaterials()==S->GetNumMaterials(); for(int32 Index=0;Index<S->GetNumMaterials();++Index) Same&=M->GetMaterial(Index)==S->GetMaterial(Index);
    if(!Same||!S->GetComponentTransform().Equals(M->GetComponentTransform(),.000001)) { Error=TEXT("production clone model/material/LeaderPose/world mismatch; gun mask has no attach requirement"); return false; }
    return true;
}
bool UDemoOutlineROIPrototypeAudit::ReadKeyTarget(FString& Error) const
{
    DEMO_LOG_TICK(); const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>(); const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    const UDemoCasinoDeviceInputComponent* Input=Controller.IsValid()?Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>():nullptr; const UDemoCasinoLocalViewComponent* View=Controller.IsValid()?Controller->FindComponentByClass<UDemoCasinoLocalViewComponent>():nullptr; const UDemoCasinoControlsComponent* Controls=KeyDevice.IsValid()?KeyDevice->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    const ADemoPlayerState* PS=Player.IsValid()?Player->GetPlayerState<ADemoPlayerState>():nullptr; const UDemoAttributeSet* Attrs=PS?PS->GetAttributes():nullptr; const UDemoMenuFlowComponent* Flow=Controller.IsValid()?Controller->GetMenuFlow():nullptr; // 复用实体输入的生死/UI门，Operate本身是允许状态，不能用技能许可互斥它。
    const AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); FName Control; FString Material; FTransform Outline,Part;
    if(!Registry||!KeyDevice.IsValid()||Registry->FindDevice(KeyDevice->DeviceId)!=KeyDevice.Get()||!Player.IsValid()||!Controller.IsValid()||Controller->GetPawn()!=Player.Get()||Controller->GetViewTarget()!=Player.Get()||!Controller->IsCoopRegistered()||!Attrs||Attrs->GetHealth()<=0.f||Player->IsCoopEliminated()||Controller->IsPaused()||UGameplayStatics::IsGamePaused(GetWorld())||!Flow||Flow->HasBlockingOverlay()||Flow->IsUpgradeMenuOpen()||Flow->IsNextLevelConfirmationOpen()||!Flow->IsCasinoDeviceOperating()||!State||State->Phase!=EDemoPhase::Hub||!Input||!Input->IsOperating()||Input->GetCurrentDevice()!=KeyDevice.Get()||Input->GetFocusedControlId()!=TEXT("amount.digit.0")||!Mode||!Mode->GetShopSystem()||!Mode->GetShopSystem()->IsPhysicalCasinoOperator(Player.Get())||!Controls||!Controls->ValidateControlHit(Player.Get(),TEXT("amount.digit.0"),Error)||!View||!View->ReadFocusedOutlineForAudit(Control,Material,Outline)||Control!=TEXT("amount.digit.0")||!View->ReadModelPartPoseForAudit(KeyPart,Part)||!Source.IsValid()||!Mask.IsValid()) { Error=TEXT("actual keycap living Hub/Pawn/UI-positive-Operate/E lease/digit0 first-hit/LocalView/source gate: ")+Error; return false; }
    int32 Count=0; TInlineComponentArray<UPrimitiveComponent*> Components(Player.Get()); for(UPrimitiveComponent* Component:Components) if(IsValid(Component)&&Component->IsRegistered()&&Component->bRenderCustomDepth&&Component->CustomDepthStencilValue==253&&Component->IsVisible()&&!Component->bHiddenInGame) ++Count;
    if(Count!=1||!Mask->IsRegistered()||!Source->IsRegistered()||Mask->GetOwner()!=Player.Get()||Mask->GetAttachParent()!=Source.Get()||!Mask->bOnlyOwnerSee||Mask->bRenderInMainPass||Mask->bRenderInDepthPass||!Mask->bRenderCustomDepth||Mask->CustomDepthStencilValue!=253||!Mask->GetComponentTransform().Equals(Outline,.000001)||!Source->GetComponentTransform().Equals(Part,.000001)||!Part.Equals(Outline,.000001)) { Error=TEXT("actual keycap unique253 mask/attached actual dynamic source/pose mismatch"); return false; }
    for(int32 Index=0;Index<KeyQueryIds.Num();++Index) { FVector Point; if(!Controls->GetControlPoint(KeyQueryIds[Index],Point)||!Point.Equals(KeyQueryRest[Index],.001)) { Error=TEXT("root key Query moved: ")+KeyQueryIds[Index].ToString(); return false; } }
    return true;
}
bool UDemoOutlineROIPrototypeAudit::InjectKeyPrimary()
{
    DEMO_LOG_TICK(); if(!bKeycap||!Controller.IsValid()||!KeyFireAction.IsValid()) return false; UEnhancedPlayerInput* Input=Cast<UEnhancedPlayerInput>(Controller->PlayerInput); if(!Input) return false; Input->InjectInputForAction(KeyFireAction.Get(),FInputActionValue(bKeyDown)); return true; // 每末帧持续注入，原输入Tick才产生Started/Completed。
}
void UDemoOutlineROIPrototypeAudit::ReadKeyMotion()
{
    DEMO_LOG_TICK(); if(!bKeycap||!bBound||Stage<4||!Source.IsValid()||!Mask.IsValid()) return;
    const UDemoCasinoControlsComponent* Controls=KeyDevice.IsValid()?KeyDevice->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    if(!Controls||KeyQueryIds.Num()!=15||KeyQueryRest.Num()!=15) { Fail(TEXT("UNVERIFIED: original fifteen key Queries unavailable")); return; }
    for(int32 Index=0;Index<15;++Index) { FVector Point; if(!Controls->GetControlPoint(KeyQueryIds[Index],Point)) { Fail(TEXT("UNVERIFIED: original key Query missing")); return; } KeyQueryMaxShift=FMath::Max(KeyQueryMaxShift,FVector::Distance(Point,KeyQueryRest[Index])); }
    if(KeyQueryMaxShift>.001||!Source->GetComponentTransform().Equals(Mask->GetComponentTransform(),.000001)) { Fail(TEXT("UNVERIFIED: root Query moved or actual key/source mask diverged")); return; }
    KeyPressTravel=FVector::Distance(Source->GetComponentLocation(),SourceReference.GetLocation()); KeyMaxTravel=FMath::Max(KeyMaxTravel,KeyPressTravel);
    if(Stage==4&&(Group==2||Group==3)) { KeyPressPeak=FMath::Max(KeyPressPeak,KeyPressTravel); KeyGTMovingSamples+=KeyPressTravel>.01; const UDemoCasinoDeviceInputComponent* Input=Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); if(bKeyDown&&Input&&Input->GetPrimaryPressOwner()==TEXT("Device")) bKeyOwnedGesture=true; }
}
void UDemoOutlineROIPrototypeAudit::AdvanceKeyPreparation()
{
    DEMO_LOG_TICK(); const double Now=FPlatformTime::Seconds(); UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    if(Stage==11)
    {
        if(!Registry||!Registry->GetDefinition()) return; for(const FDemoHubCasinoBinding& Binding:Registry->GetDefinition()->Devices) if(Binding.Mode==1) { KeyDevice=Registry->FindDevice(Binding.DeviceId); break; }
        UDemoCasinoControlsComponent* Controls=KeyDevice.IsValid()?KeyDevice->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr; if(!Controls||!Controls->IsReady()) return; KeyProfile=const_cast<UDemoCasinoDeviceProfile*>(Controls->GetModelProfile()); if(!KeyProfile.IsValid()) return;
        for(const FDemoCasinoModelPart& Part:KeyProfile->Parts) if(Part.ControlId==TEXT("amount.digit.0")&&Part.Kind==EDemoCasinoModelPartKind::Key) KeyPart=Part.PartId;
        if(KeyPart.IsNone()) { Fail(TEXT("UNVERIFIED: real digit0 profile part missing")); return; }
        for(int32 Index=0;Index<10;++Index) KeyQueryIds.Add(FName(*FString::Printf(TEXT("amount.digit.%d"),Index))); KeyQueryIds.Append({FName(TEXT("amount.clear")),FName(TEXT("amount.backspace")),FName(TEXT("amount.commit")),FName(TEXT("slots.pull")),FName(TEXT("device.exit"))});
        for(FName Id:KeyQueryIds) { FVector Point; if(!Controls->GetControlPoint(Id,Point)) { Fail(TEXT("UNVERIFIED: fixed query missing ")+Id.ToString()); return; } KeyQueryRest.Add(Point); }
        const FVector Stand=KeyDevice->GetActorTransform().TransformPosition(FVector(180,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())); if(!Player->TeleportTo(Stand,Player->GetActorRotation())) { Fail(TEXT("UNVERIFIED: normal slots standing collision")); return; } Player->GetCharacterMovement()->StopMovementImmediately(); Player->ConsumeMovementInputVector(); Controls->GetControlPoint(TEXT("slots.pull"),AimPoint); Controller->SetControlRotation((AimPoint-Player->GetSharedViewLocation()).Rotation()); Stage=12; WaitStarted=Now; return;
    }
    UDemoCasinoControlsComponent* Controls=KeyDevice.IsValid()?KeyDevice->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr; UDemoCasinoDeviceInputComponent* Input=Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>();
    if(Stage==12)
    {
        if(Now-WaitStarted>2.) { Fail(TEXT("UNVERIFIED: normal slots ray/production-focus dwell exceeded original2 real seconds")); return; } if(Now-WaitStarted<.35) return;
        FString Error; if(!Controls||!Controls->ValidateControlHit(Player.Get(),TEXT("slots.pull"),Error)) { KeyRayStableSinceWorld=-1.; KeyRayStableElapsedWorld=0.; ReaimKeyControl(TEXT("slots.pull"),Error); return; }
        const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr; // 正常Focus按World秒20Hz/.15稳定候选，Validate命中本身不能冒充生产Focus已建立。
        if(!Definition||Definition->ScanHz<=0.f||Definition->FocusStableSeconds<0.f) { Fail(TEXT("UNVERIFIED: real Hub Focus scan/stable config unavailable")); return; }
        const double WorldSeconds=GetWorld()->GetTimeSeconds(); if(KeyRayStableSinceWorld<0.) KeyRayStableSinceWorld=WorldSeconds;
        KeyRayStableNeededWorld=Definition->FocusStableSeconds+2./Definition->ScanHz+1./60.; KeyRayStableElapsedWorld=WorldSeconds-KeyRayStableSinceWorld; // 两次最坏扫描相位+一正常60Hz帧，来自生产配置，不修改门限/不睡眠。
        if(KeyRayStableElapsedWorld<KeyRayStableNeededWorld) return;
        KeyETriggerFrame=GFrameCounter; UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_KEY_E_DWELL frame=%llu stable_world_needed=%.9f stable_world_elapsed=%.9f scan_hz=%.3f focus_stable_seconds=%.6f real_elapsed=%.6f"),KeyETriggerFrame,KeyRayStableNeededWorld,KeyRayStableElapsedWorld,Definition->ScanHz,Definition->FocusStableSeconds,Now-WaitStarted);
        Player->TriggerWorldInteraction(); Stage=13; WaitStarted=Now; return; // 唯一E；下一阶段只等原租约，不靠反复交互掩盖时序。
    }
    if(Stage==13) { if(!Input||!Input->IsOperating()||Input->GetCurrentDevice()!=KeyDevice.Get()) { if(Now-WaitStarted>2.) Fail(TEXT("UNVERIFIED: normal E physical lease not obtained")); return; } KeyAmountBefore=Controller->GetMenuFlow()->GetCasinoDeviceAmountText(); KeyRequestBefore=Input->GetLastSubmittedRequestId(); Controls->GetControlPoint(TEXT("amount.digit.0"),AimPoint); Controller->SetControlRotation((AimPoint-Player->GetSharedViewLocation()).Rotation()); Stage=14; WaitStarted=Now; return; }
    if(Stage==14)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(Player.Get()); for(UPrimitiveComponent* Component:Components) if(IsValid(Component)&&Component->IsRegistered()&&Component->bRenderCustomDepth&&Component->CustomDepthStencilValue==253&&Component->IsVisible()&&!Component->bHiddenInGame) { Mask=Component; Source=Cast<UPrimitiveComponent>(Component->GetAttachParent()); }
        FString Error; if(!ReadKeyTarget(Error)) { if(Now-WaitStarted>2.) Fail(TEXT("UNVERIFIED: normal digit0 focus/mask not ready: ")+Error); else ReaimKeyControl(TEXT("amount.digit.0"),Error); return; } Stage=2; WaitStarted=Now; return; // 并入原桥Bind，只读实际私有源，不用Query中心冒模型。
    }
}
void UDemoOutlineROIPrototypeAudit::ReaimKeyControl(FName ControlId,const FString& Reason)
{
    DEMO_LOG_TICK(); const UDemoCasinoControlsComponent* Controls=KeyDevice.IsValid()?KeyDevice->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr; FVector Point;
    if(!Player.IsValid()||!Controller.IsValid()||!Controls||!Controls->GetControlPoint(ControlId,Point)) return;
    const FVector SharedEye=Player->GetSharedViewLocation(); const FRotator SharedRotation=Player->GetViewRotation(); // 生产ValidateControlHit使用权威共享眼位/GetViewRotation，不能把旧相机POV当本帧眼位。
    if(!KeyAimFailureFacts.IsValid()) KeyAimFailureFacts=MakeShared<FJsonObject>();
    if(!KeyAimFailureFacts->HasField(ControlId.ToString()))
    {
        const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>(); const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr; const float Distance=Definition?Definition->InteractionDistanceCm:250.f;
        FVector POV; FRotator POVRotation; Controller->GetPlayerViewPoint(POV,POVRotation); FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(OutlineROIKeyAimDebug),false,Player.Get()); GetWorld()->LineTraceSingleByChannel(Hit,SharedEye,SharedEye+SharedRotation.Vector()*Distance,ECC_Visibility,Query);
        TSharedPtr<FJsonObject> Fact=MakeShared<FJsonObject>(); Fact->SetNumberField(TEXT("stage"),Stage); Fact->SetNumberField(TEXT("frame"),static_cast<double>(GFrameCounter)); Fact->SetNumberField(TEXT("stage_real_seconds"),FPlatformTime::Seconds()-WaitStarted); Fact->SetStringField(TEXT("reason"),Reason); Fact->SetStringField(TEXT("target_point"),Point.ToString()); Fact->SetStringField(TEXT("shared_eye"),SharedEye.ToString()); Fact->SetStringField(TEXT("authoritative_view_rotation"),SharedRotation.ToString()); Fact->SetStringField(TEXT("control_rotation"),Controller->GetControlRotation().ToString()); Fact->SetStringField(TEXT("actual_pov_eye"),POV.ToString()); Fact->SetStringField(TEXT("actual_pov_rotation"),POVRotation.ToString()); Fact->SetStringField(TEXT("first_hit_actor"),GetPathNameSafe(Hit.GetActor())); Fact->SetStringField(TEXT("first_hit_component"),GetPathNameSafe(Hit.GetComponent())); Fact->SetStringField(TEXT("first_hit_location"),Hit.Location.ToString()); Fact->SetBoolField(TEXT("blocking_hit"),Hit.bBlockingHit); Fact->SetNumberField(TEXT("ray_distance_cm"),Distance); KeyAimFailureFacts->SetObjectField(ControlId.ToString(),Fact);
        UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_KEY_AIM_FIRST_REFUSAL stage=%d control=%s shared_eye=%s view=%s target=%s first_actor=%s first_component=%s reason=%s"),Stage,*ControlId.ToString(),*SharedEye.ToString(),*SharedRotation.ToString(),*Point.ToString(),*GetPathNameSafe(Hit.GetActor()),*GetPathNameSafe(Hit.GetComponent()),*Reason);
    }
    Controller->SetControlRotation((Point-SharedEye).Rotation()); ++KeyAimCorrections; // 不重置WaitStarted，保原2秒截止；正常下一帧相机/SharedEye自然更新再验证。
}
bool UDemoOutlineROIPrototypeAudit::ReadView(FString& Error) const
{
    DEMO_LOG_TICK(); if(!ReadTarget(Error)) return false; const UCameraComponent* Camera=Player->GetFirstPersonCameraComponent(); if(!IsValid(Camera)) { Error=TEXT("actual first-person Camera is no longer valid"); return false; }
    FVector Location; FRotator Rotation; Controller->GetPlayerViewPoint(Location,Rotation); const float FOV=Controller->PlayerCameraManager?Controller->PlayerCameraManager->GetFOVAngle():0.f;
    FRotator ExpectedCamera=CameraReference.Rotator(),ExpectedPOV=POVReference.Rotator(); ExpectedCamera.Yaw+=YawOffset; ExpectedPOV.Yaw+=YawOffset;
    const FVector Eye=Player->GetSharedViewLocation(); const bool Slow=Stage==4&&(bKeycap?Group>=4:Group>=2),Press=bKeycap&&Stage==4&&(Group==2||Group==3); // Keycap仅press组放行同实际源/Mask运动；生产眼位慢yaw按原圆弧。
    const bool Position=Slow?(Camera->GetComponentLocation().Equals(Eye,.000001)&&Location.Equals(Eye,.000001)):(Camera->GetComponentTransform().Equals(CameraReference,.000001)&&FTransform(Rotation,Location).Equals(POVReference,.000001));
    const bool SourceStable=Press?Source->GetComponentTransform().Equals(Mask->GetComponentTransform(),.000001):(Source->GetComponentTransform().Equals(SourceReference,.000001)&&Mask->GetComponentTransform().Equals(MaskReference,.000001));
    if(!Player->GetActorLocation().Equals(PawnReference,.000001)||!SourceStable||!Position||!Camera->GetComponentRotation().Equals(ExpectedCamera,.003)||!Rotation.Equals(ExpectedPOV,.003)||!FMath::IsNearlyEqual(FOV,ReferenceFOV,.0001f)) { Error=FString::Printf(TEXT("actual view/source context phase=%d frame=%llu eye=%s camera=%s pov=%s camera_eye_error=%.12f pov_eye_error=%.12f yaw=%.9f wait=%.6f"),Group,GFrameCounter,*Eye.ToString(),*Camera->GetComponentLocation().ToString(),*Location.ToString(),FVector::Distance(Eye,Camera->GetComponentLocation()),FVector::Distance(Eye,Location),YawOffset,FPlatformTime::Seconds()-YawChangedAt); return false; }
    return true;
}
void UDemoOutlineROIPrototypeAudit::StopMemoryCapture()
{
    DEMO_LOG_CALL(); if(ScreenshotHandle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle); ScreenshotHandle.Reset();
    if(bPending&&FScreenshotRequest::IsScreenshotRequested()&&FScreenshotRequest::GetFilename()==OwnedRequest) FScreenshotRequest::Reset(); bPending=false; bCallbackReady=false;
}
bool UDemoOutlineROIPrototypeAudit::ReadRTWitness(const TArray<FColor>& MainBitmap,const TSharedPtr<FJsonObject>& Frame,bool Pulse)
{
    DEMO_LOG_CALL(); bRTAttempted=true; ++RTReadbacks; TSharedPtr<FJsonObject> Witness=MakeShared<FJsonObject>(); if(!RTWitness.IsValid()) RTWitness=Witness; LastRTWitness=Witness; UTextureRenderTarget2D* RT=Bridge->ReadMaskRT(); // 保首个正控与最后失败见证；只在正常回调读RT，不在Tick读RHI。
    const bool ResourceContract=RT&&RT->SizeX==1280&&RT->SizeY==720&&RT->GetFormat()==PF_B8G8R8A8&&RT->bForceLinearGamma&&!RT->bAutoGenerateMips&&RT->Filter==TF_Nearest&&RT->AddressX==TA_Clamp&&RT->AddressY==TA_Clamp;
    Witness->SetBoolField(TEXT("resource_contract"),ResourceContract); Witness->SetStringField(TEXT("scope"),TEXT("actual normal drawn-callback RT readback; CPU frame/gen/Owner/Mask submission + Alpha write do not prove GPU producer frame identity; no2px/full-visible-edge/Owner/occlusion/performance conclusion"));
    if(!ResourceContract) { Fail(TEXT("UNVERIFIED_RT_RESOURCE_CONTRACT")); return false; }
    FTextureRenderTargetResource* Resource=RT->GameThread_GetRenderTargetResource(); TArray<FColor> Pixels; FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
    if(!Resource||!Resource->ReadPixels(Pixels,Flags,FIntRect(0,0,1280,720))||Pixels.Num()!=1280*720||MainBitmap.Num()!=Pixels.Num()) { Fail(TEXT("UNVERIFIED_RT_NORMAL_CALLBACK_READBACK")); return false; }
    const bool BinaryWitness=bTemporalWitness||(bKeycap&&Group==3); TArray<uint8> Binary; if(BinaryWitness) Binary.SetNumUninitialized(Pixels.Num()); // press_on始终16个二值读回，其余只有显式时序才导RLE，不保RGBA序列。
    int32 Edges=0,NonBinaryR=0,NonzeroG=0,NonzeroB=0,AlphaNot255=0,WhiteEdges=0,NearWhiteEdges=0,EdgeMinimumRGB=255; // R最终二值edge；主图验收必须RGB精确255，近白250只作定位，A不是edge或GPU帧号。
    for(int32 Index=0;Index<Pixels.Num();++Index)
    {
        const FColor& P=Pixels[Index]; NonBinaryR+=P.R!=0&&P.R!=255; NonzeroG+=P.G!=0; NonzeroB+=P.B!=0; AlphaNot255+=P.A!=255;
        if(BinaryWitness) Binary[Index]=P.R==255?1:0;
        if(P.R==255) { ++Edges; const FColor& Main=MainBitmap[Index]; const int32 Minimum=FMath::Min3<int32>(Main.R,Main.G,Main.B),Maximum=FMath::Max3<int32>(Main.R,Main.G,Main.B); WhiteEdges+=Main.R==255&&Main.G==255&&Main.B==255; NearWhiteEdges+=Minimum>=250&&Maximum-Minimum<=2; EdgeMinimumRGB=FMath::Min(EdgeMinimumRGB,Minimum); }
    }
    Witness->SetNumberField(TEXT("cpu_drawn_frame"),static_cast<double>(GFrameCounter)); Witness->SetNumberField(TEXT("generation"),Generation); Witness->SetNumberField(TEXT("owner_id"),Player->GetUniqueID()); Witness->SetNumberField(TEXT("mask_id"),Mask->GetUniqueID()); Witness->SetNumberField(TEXT("pixels"),Pixels.Num()); Witness->SetNumberField(TEXT("r_edge_pixels"),Edges); Witness->SetNumberField(TEXT("r_nonbinary_pixels"),NonBinaryR); Witness->SetNumberField(TEXT("g_nonzero_pixels"),NonzeroG); Witness->SetNumberField(TEXT("b_nonzero_pixels"),NonzeroB); Witness->SetNumberField(TEXT("alpha_not255_pixels"),AlphaNot255); Witness->SetNumberField(TEXT("main_pure_white_at_rt_edge_pixels"),WhiteEdges); if(!BinaryWitness) Witness->SetStringField(TEXT("rt_rgba_crc32"),FString::Printf(TEXT("%08x"),FCrc::MemCrc32(Pixels.GetData(),Pixels.Num()*sizeof(FColor))));
    const bool NeedEdges=!Pulse||PulseIndex==1; bRTValid=(NeedEdges?Edges>0:Edges==0)&&NonBinaryR==0&&NonzeroG==0&&NonzeroB==0&&AlphaNot255==0&&WhiteEdges==Edges; Witness->SetBoolField(TEXT("listed_channel_and_main_white_contract"),bRTValid);
    Witness->SetStringField(TEXT("main_white_acceptance"),TEXT("Exact Main.R==255 && Main.G==255 && Main.B==255 at every R255 RT edge; near-white250 is diagnostic only")); Witness->SetNumberField(TEXT("main_near_white250_at_rt_edge_pixels_diagnostic"),NearWhiteEdges); Witness->SetNumberField(TEXT("main_rt_edge_min_rgb_diagnostic"),Edges>0?EdgeMinimumRGB:INDEX_NONE);
    if(BinaryWitness)
    {
        if(!bKeycap&&!Pulse&&PulseProbeIndices.IsEmpty())
        {
            if(FixedOffBaseline.Num()!=Pixels.Num()) { Fail(TEXT("UNVERIFIED: fixed_off first measured baseline missing")); return false; }
            TArray<int32> Eligible; for(int32 Index=0;Index<Pixels.Num();++Index) if(Binary[Index]&&FMath::Max3(FixedOffBaseline[Index].R,FixedOffBaseline[Index].G,FixedOffBaseline[Index].B)<=200) Eligible.Add(Index); // 仅真正由暗背景变成白沿的正控点，不用天然白背景。
            if(Eligible.IsEmpty()) { Fail(TEXT("UNVERIFIED: no distinguishable dark-baseline old-edge pulse probe")); return false; }
            const int32 Count=FMath::Min(64,Eligible.Num()); for(int32 Index=0;Index<Count;++Index) PulseProbeIndices.Add(Eligible[Count==1?0:Index*(Eligible.Num()-1)/(Count-1)]);
            PulseProbeEvidence=MakeShared<FJsonObject>(); PulseProbeEvidence->SetNumberField(TEXT("fixed_off_baseline_frame"),static_cast<double>(FixedOffBaselineFrame)); PulseProbeEvidence->SetNumberField(TEXT("eligible_r255_dark_baseline_pixels"),Eligible.Num()); PulseProbeEvidence->SetNumberField(TEXT("probe_count"),Count); PulseProbeEvidence->SetStringField(TEXT("selection"),TEXT("uniform<=64 from actual fixed_on R255; fixed_off first measured maxRGB<=200; flat index=y*1280+x; 0width expects minRGB<250, 2width exactRGB255"));
            TArray<TSharedPtr<FJsonValue>> ProbeRuns; for(int32 Index=0;Index<PulseProbeIndices.Num();) { const int32 Start=PulseProbeIndices[Index]; int32 Length=1; while(Index+Length<PulseProbeIndices.Num()&&PulseProbeIndices[Index+Length]==Start+Length) ++Length; TArray<TSharedPtr<FJsonValue>> Pair{MakeShared<FJsonValueNumber>(Start),MakeShared<FJsonValueNumber>(Length)}; ProbeRuns.Add(MakeShared<FJsonValueArray>(Pair)); Index+=Length; } PulseProbeEvidence->SetArrayField(TEXT("probe_one_runs"),ProbeRuns);
        }
        if(Pulse)
        {
            int32 Matches=0; for(int32 Index:PulseProbeIndices) { const FColor& Main=MainBitmap[Index]; Matches+=NeedEdges?(Main.R==255&&Main.G==255&&Main.B==255):(FMath::Min3(Main.R,Main.G,Main.B)<250); }
            Witness->SetNumberField(TEXT("old_positive_probes_checked"),PulseProbeIndices.Num()); Witness->SetNumberField(TEXT("old_positive_probes_matched"),Matches); Witness->SetNumberField(TEXT("old_positive_probe_failures"),PulseProbeIndices.Num()-Matches); Witness->SetStringField(TEXT("old_positive_expected_main"),NeedEdges?TEXT("exactRGB255"):TEXT("minRGB<250; old white edge must disappear"));
            bRTValid&=!PulseProbeIndices.IsEmpty()&&Matches==PulseProbeIndices.Num(); Witness->SetBoolField(TEXT("listed_channel_and_main_white_contract"),bRTValid);
        }
        if(Frame.IsValid()) for(const auto& Pair:Frame->Values) Witness->SetField(Pair.Key,Pair.Value); // 只复制当前已有纯值，再把见证挂回Frame，不能把Frame对象反挂形成JSON循环。
        Witness->SetStringField(TEXT("actual_source_world"),Source->GetComponentTransform().ToString()); Witness->SetStringField(TEXT("actual_mask_world"),Mask->GetComponentTransform().ToString()); Witness->SetNumberField(TEXT("source_unique_id"),Source->GetUniqueID()); Witness->SetNumberField(TEXT("rt_readback_ordinal"),RTReadbacks); Witness->SetStringField(TEXT("binary_r_crc32"),FString::Printf(TEXT("%08x"),FCrc::MemCrc32(Binary.GetData(),Binary.Num())));
        TArray<TSharedPtr<FJsonValue>> Runs; int32 Ones=0; // [start,length]表示row-major二值R=1的连续区间；未列部分为0，跨行连续按一维编码。
        for(int32 Index=0;Index<Binary.Num();) { if(Binary[Index]==0) { ++Index; continue; } const int32 Start=Index; while(Index<Binary.Num()&&Binary[Index]==1) ++Index; const int32 Length=Index-Start; Ones+=Length; TArray<TSharedPtr<FJsonValue>> Pair{MakeShared<FJsonValueNumber>(Start),MakeShared<FJsonValueNumber>(Length)}; Runs.Add(MakeShared<FJsonValueArray>(Pair)); if(Runs.Num()>8192) { Fail(TEXT("UNVERIFIED: finite temporal one-runs JSON budget exceeded8192; no truncation or stability conclusion")); return false; } }
        Witness->SetArrayField(TEXT("r_one_runs"),Runs); Witness->SetStringField(TEXT("r_encoding"),TEXT("1280x720 top-left Xright/Ydown; index=y*1280+x; byte binary0/1 CRC; one_runs[start,length], all omitted indices0"));
        if(Ones!=Edges) { Fail(TEXT("UNVERIFIED: binary R one-runs sum differs from edge count")); return false; }
        if(Frame.IsValid()) Frame->SetObjectField(TEXT("rt_witness"),Witness);
        if(!bRTValid) { Fail(Pulse?TEXT("UNVERIFIED_T03_FIRST_RESPONSE_MASK_PULSE_OR_OLD_EDGE_RESPONSE"):TEXT("UNVERIFIED_T03_RT_CHANNEL_OR_COMPOSITE_PURE_WHITE_CONTRACT")); return false; }
        if(!Pulse&&!(bKeycap&&Group==3?RecordPressR(Binary,Witness):RecordTemporalR(Binary,Witness))) return false;
    }
    if(!bRTValid) Fail(Pulse?TEXT("UNVERIFIED_T03_FIRST_RESPONSE_MASK_PULSE_OR_EXACT_WHITE"):TEXT("UNVERIFIED_T03_RT_CHANNEL_OR_COMPOSITE_PURE_WHITE_CONTRACT")); return bRTValid;
}
bool UDemoOutlineROIPrototypeAudit::RecordTemporalR(const TArray<uint8>& Binary,const TSharedPtr<FJsonObject>& Witness)
{
    DEMO_LOG_CALL(); const bool First=TemporalRFrames.IsEmpty(); // 仅16个有效fixed_on读回，首图相邻XOR为0，不计暖机。
    if(Binary.Num()!=1280*720||TemporalRFrames.Num()>=16) { Fail(TEXT("UNVERIFIED: bounded temporal R dimensions/sample count")); return false; }
    if(First) { TemporalRCounts.Init(0,Binary.Num()); TemporalRFlipCounts.Init(0,Binary.Num()); }
    if(TemporalRCounts.Num()!=Binary.Num()||TemporalRFlipCounts.Num()!=Binary.Num()||(!First&&PreviousTemporalR.Num()!=Binary.Num())) { Fail(TEXT("UNVERIFIED: temporal R count/previous dimensions")); return false; }
    int32 Xor=0; for(int32 Index=0;Index<Binary.Num();++Index) { if(Binary[Index]>1) { Fail(TEXT("UNVERIFIED: nonbinary temporal R byte")); return false; } TemporalRCounts[Index]+=Binary[Index]; if(!First) { const bool Flip=Binary[Index]!=PreviousTemporalR[Index]; Xor+=Flip; TemporalRFlipCounts[Index]+=Flip; } }
    Witness->SetNumberField(TEXT("r_temporal_sample_index"),TemporalRFrames.Num()); Witness->SetNumberField(TEXT("r_xor_previous_pixels"),Xor); PreviousTemporalR=Binary; TemporalRFrames.Add(MakeShared<FJsonValueObject>(Witness)); return true;
}
bool UDemoOutlineROIPrototypeAudit::RecordPressR(const TArray<uint8>& Binary,const TSharedPtr<FJsonObject>& Witness)
{
    DEMO_LOG_CALL(); const bool First=PressRFrames.IsEmpty();
    if(Binary.Num()!=1280*720||PressRFrames.Num()>=16||(!First&&PreviousPressR.Num()!=Binary.Num())) { Fail(TEXT("UNVERIFIED: press_on sixteen finite R dimensions/count")); return false; }
    int32 Xor=0; if(!First) for(int32 Index=0;Index<Binary.Num();++Index) Xor+=Binary[Index]!=PreviousPressR[Index];
    const double Travel=Witness->GetNumberField(TEXT("actual_key_travel_cm")); KeyMovingRTSamples+=Travel>.01&&Witness->GetNumberField(TEXT("r_edge_pixels"))>0;
    Witness->SetNumberField(TEXT("r_press_sample_index"),PressRFrames.Num()); Witness->SetNumberField(TEXT("r_xor_previous_pixels"),Xor); Witness->SetBoolField(TEXT("actual_moving_key_pose"),Travel>.01); Witness->SetStringField(TEXT("movement_scope"),TEXT("actual same-frame key/source+mask pose and RT R; motion XOR is recorded, never required zero; sixteen observations are not all moving poses")); PreviousPressR=Binary; PressRFrames.Add(MakeShared<FJsonValueObject>(Witness)); return true;
}
bool UDemoOutlineROIPrototypeAudit::FinalizeTemporalR()
{
    DEMO_LOG_CALL();
    if(TemporalRFrames.Num()!=16||TemporalRCounts.Num()!=1280*720||TemporalRFlipCounts.Num()!=1280*720) { Fail(TEXT("UNVERIFIED: sixteen finite R readbacks required")); return false; }
    TemporalRSummary=MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> CountRuns,FlipRuns; int32 Sum=0,Flips=0,Variable=0,Always=0,Never=0,CountCovered=0,FlipCovered=0; bool Valid=true; // 完整频次RLE含0区间，平面索引覆盖全部921600格。
    for(int32 Index=0;Index<TemporalRCounts.Num();++Index) { const int32 Count=TemporalRCounts[Index],Flip=TemporalRFlipCounts[Index]; Valid&=Count<=16&&Flip<=15&&((Count>0&&Count<16)==(Flip>0)); Sum+=Count; Flips+=Flip; Variable+=Count>0&&Count<16; Always+=Count==16; Never+=Count==0; }
    for(int32 Index=0;Index<TemporalRCounts.Num();) { const int32 Start=Index,Count=TemporalRCounts[Index]; while(Index<TemporalRCounts.Num()&&TemporalRCounts[Index]==Count) ++Index; CountCovered+=Index-Start; TArray<TSharedPtr<FJsonValue>> Run{MakeShared<FJsonValueNumber>(Start),MakeShared<FJsonValueNumber>(Index-Start),MakeShared<FJsonValueNumber>(Count)}; CountRuns.Add(MakeShared<FJsonValueArray>(Run)); if(CountRuns.Num()>32768) { Fail(TEXT("UNVERIFIED: temporal count-runs JSON budget exceeded32768; no truncation")); return false; } }
    for(int32 Index=0;Index<TemporalRFlipCounts.Num();) { const int32 Start=Index,Count=TemporalRFlipCounts[Index]; while(Index<TemporalRFlipCounts.Num()&&TemporalRFlipCounts[Index]==Count) ++Index; FlipCovered+=Index-Start; TArray<TSharedPtr<FJsonValue>> Run{MakeShared<FJsonValueNumber>(Start),MakeShared<FJsonValueNumber>(Index-Start),MakeShared<FJsonValueNumber>(Count)}; FlipRuns.Add(MakeShared<FJsonValueArray>(Run)); if(FlipRuns.Num()>32768) { Fail(TEXT("UNVERIFIED: temporal flip-runs JSON budget exceeded32768; no truncation")); return false; } }
    int32 FrameSum=0,FrameXor=0; for(int32 Index=0;Index<16;++Index) { const TSharedPtr<FJsonObject> Frame=TemporalRFrames[Index]->AsObject(); FrameSum+=static_cast<int32>(Frame->GetNumberField(TEXT("r_edge_pixels"))); const int32 Xor=static_cast<int32>(Frame->GetNumberField(TEXT("r_xor_previous_pixels"))); FrameXor+=Xor; Valid&=Index!=0||Xor==0; }
    Valid&=Sum==FrameSum&&Flips==FrameXor&&Variable+Always+Never==1280*720&&CountCovered==1280*720&&FlipCovered==1280*720;
    TemporalRSummary->SetStringField(TEXT("scope"),TEXT("Only16 valid fixed_on binary R observations; one_runs and byte0/1 CRC per sample, adjacent XOR and complete count/flip runs. Changes are recorded, not rejected or hidden. No claim between samples, full-game no-flicker, intensity/semantic coverage or generic GPU producer frame identity; mixed white/main color CRC are not this metric.")); TemporalRSummary->SetStringField(TEXT("encoding"),TEXT("1280x720 top-left; Xright/Ydown; index=y*1280+x; each count/flip run=[start,length,value], contiguous full921600 coverage")); TemporalRSummary->SetNumberField(TEXT("samples"),16); TemporalRSummary->SetArrayField(TEXT("count_runs"),CountRuns); TemporalRSummary->SetArrayField(TEXT("flip_count_runs"),FlipRuns); TemporalRSummary->SetNumberField(TEXT("variable_pixels"),Variable); TemporalRSummary->SetNumberField(TEXT("always_edge_pixels"),Always); TemporalRSummary->SetNumberField(TEXT("never_edge_pixels"),Never); TemporalRSummary->SetNumberField(TEXT("edge_sum"),Sum); TemporalRSummary->SetNumberField(TEXT("xor_previous_total"),Flips); TemporalRSummary->SetNumberField(TEXT("frame_edge_sum"),FrameSum); TemporalRSummary->SetNumberField(TEXT("frame_xor_previous_total"),FrameXor); TemporalRSummary->SetBoolField(TEXT("format_and_sum_invariants"),Valid); TemporalRSummary->SetBoolField(TEXT("changed_between_observed_samples"),Flips>0);
    if(!Valid) { Fail(TEXT("UNVERIFIED: temporal R frequency/xor invariants failed")); return false; } PreviousTemporalR.Reset(); TemporalRCounts.Reset(); TemporalRFlipCounts.Reset(); return true;
}
void UDemoOutlineROIPrototypeAudit::OnScreenshot(int32 Width,int32 Height,const TArray<FColor>& Bitmap)
{
    DEMO_LOG_TICK(); if(!bPending||(Stage!=4&&Stage!=10)) return; const double CallbackAt=FPlatformTime::Seconds(); const bool Pulse=Stage==10; const int32 ExpectedMaskWidth=Pulse?(PulseIndex==1?2:0):2; // 回调入口时钟冻结在像素扫描前；Composite在脉冲中常开。
    const bool Own=IsInGameThread()&&FScreenshotRequest::IsScreenshotRequested()&&FScreenshotRequest::GetFilename()==OwnedRequest&&GFrameCounter==RequestFrame&&RequestGroup==Group&&RequestSerial==CompletedSerial+1&&(LastCallbackFrame==MAX_uint64||GFrameCounter>LastCallbackFrame)&&CallbackAt>=RequestedAt&&CallbackAt-RequestedAt<=3.;
    bPending=false; bCallbackReady=true; FString Error; FDemoOutlineROIFrameAudit Audit;
    FVector ActualEye; FRotator ActualRotation; if(Controller.IsValid()) Controller->GetPlayerViewPoint(ActualEye,ActualRotation); // 当前真实POV对照提交Origin/Rotation，不只看计划ControlRotation。
    const bool BridgeFrame=Bridge.IsValid()&&Player.IsValid()&&Mask.IsValid()&&Controller.IsValid()&&Bridge->ReadFrameAudit(Audit)&&Audit.Frame==GFrameCounter&&Audit.Generation==Generation&&Audit.OwnerId==Player->GetUniqueID()&&Audit.MaskId==Mask->GetUniqueID()&&Audit.MainViewRechecked&&Audit.CaptureViewObserved&&Audit.CaptureHasViewState&&Audit.CaptureAA==0&&Audit.CaptureJitter.Equals(FVector2D::ZeroVector,.000001)&&Audit.CaptureRenderObserved&&Audit.MainRenderObserved&&Audit.CaptureRenderAA==0&&Audit.CaptureRenderJitter.Equals(FVector2D::ZeroVector,.000001)&&Audit.CaptureNearSynchronized&&FMath::Abs(Audit.CaptureNearDistance-Audit.MainNearDistance)<=.000001&&(!(bTemporalWitness||bKeycap)||Audit.ActualCaptureMaskWidth==ExpectedMaskWidth)&&Audit.RenderViewsMatchSubmission&&Audit.CaptureRenderSequence>0&&Audit.MainRenderSequence>Audit.CaptureRenderSequence&&Audit.Origin.Equals(ActualEye,.000001)&&Audit.Rotation.Equals(ActualRotation,.003)&&Audit.Submitted&&Audit.Output==FIntPoint(1280,720)&&Audit.ROI==FIntRect(0,0,1280,720)&&Audit.OutputEnabled==(Pulse||Group%2==1);
    if(!Own||Width!=1280||Height!=720||Bitmap.Num()!=1280*720||!ReadView(Error)||!BridgeFrame) { ++RejectedCallbacks; Fail(TEXT("UNVERIFIED_SAMPLING_CONTEXT: ")+Error+TEXT(" bridge_reason=")+Audit.Reason); return; }
    int32 White=0,Minimum=255,Maximum=0; int64 Sum=0; for(const FColor& Pixel:Bitmap) { const int32 Low=FMath::Min3<int32>(Pixel.R,Pixel.G,Pixel.B),High=FMath::Max3<int32>(Pixel.R,Pixel.G,Pixel.B); Minimum=FMath::Min(Minimum,Low); Maximum=FMath::Max(Maximum,High); Sum+=int32(Pixel.R)+int32(Pixel.G)+int32(Pixel.B); White+=Low>=250&&High-Low<=2; }
    if(Maximum==0) { ++RejectedCallbacks; Fail(TEXT("UNVERIFIED_SAMPLING: actual drawn screenshot is all RGB0")); return; }
    ++ValidCallbacks; CompletedSerial=RequestSerial; LastCallbackFrame=GFrameCounter; TSharedPtr<FJsonObject> Frame=MakeShared<FJsonObject>(); // 每有效回调都保存Root桥事实，不能以“调用Capture”代替观察。
    Frame->SetNumberField(TEXT("frame"),static_cast<double>(Audit.Frame)); Frame->SetNumberField(TEXT("request_frame"),static_cast<double>(RequestFrame)); Frame->SetNumberField(TEXT("request_serial"),static_cast<double>(RequestSerial)); Frame->SetNumberField(TEXT("generation"),Audit.Generation); Frame->SetNumberField(TEXT("owner_id"),Audit.OwnerId); Frame->SetNumberField(TEXT("mask_id"),Audit.MaskId); Frame->SetStringField(TEXT("roi"),TEXT("0,0,1280,720 exclusive; full view, no crop")); Frame->SetNumberField(TEXT("main_aa_actual"),Audit.MainAA); Frame->SetNumberField(TEXT("capture_aa_actual"),Audit.CaptureAA); Frame->SetStringField(TEXT("capture_jitter_actual"),Audit.CaptureJitter.ToString()); Frame->SetBoolField(TEXT("capture_view_observed"),Audit.CaptureViewObserved); Frame->SetBoolField(TEXT("main_view_rechecked"),Audit.MainViewRechecked); Frame->SetBoolField(TEXT("submitted_cpu_contract"),Audit.Submitted); Frame->SetBoolField(TEXT("output_enabled"),Audit.OutputEnabled); Frame->SetStringField(TEXT("actual_origin"),Audit.Origin.ToString()); Frame->SetStringField(TEXT("actual_rotation"),Audit.Rotation.ToString()); Frame->SetNumberField(TEXT("phase_real_seconds"),FPlatformTime::Seconds()-GroupStarted); Frame->SetNumberField(TEXT("actual_yaw_offset_degrees"),FMath::FindDeltaAngleDegrees(POVReference.Rotator().Yaw,Audit.Rotation.Yaw)); Frame->SetNumberField(TEXT("raw_rgb_min"),Minimum); Frame->SetNumberField(TEXT("raw_rgb_max"),Maximum); Frame->SetNumberField(TEXT("raw_rgb_mean"),double(Sum)/(Bitmap.Num()*3.)); Frame->SetNumberField(TEXT("mixed_white_pixels_nonsemantic"),White); Frame->SetStringField(TEXT("main_rgba_crc32"),FString::Printf(TEXT("%08x"),FCrc::MemCrc32(Bitmap.GetData(),Bitmap.Num()*sizeof(FColor))));
    Frame->SetBoolField(TEXT("capture_has_view_state"),Audit.CaptureHasViewState); Frame->SetBoolField(TEXT("capture_render_observed"),Audit.CaptureRenderObserved); Frame->SetBoolField(TEXT("main_render_observed"),Audit.MainRenderObserved); Frame->SetNumberField(TEXT("capture_render_aa_actual"),Audit.CaptureRenderAA); Frame->SetStringField(TEXT("capture_render_jitter_actual"),Audit.CaptureRenderJitter.ToString()); Frame->SetNumberField(TEXT("capture_render_sequence"),static_cast<double>(Audit.CaptureRenderSequence)); Frame->SetNumberField(TEXT("main_render_sequence"),static_cast<double>(Audit.MainRenderSequence)); Frame->SetBoolField(TEXT("render_views_match_submission"),Audit.RenderViewsMatchSubmission); Frame->SetNumberField(TEXT("hidden_primitive_count"),Audit.HiddenPrimitiveCount); // 标准PP要求持久State，关闭AA/jitter为独立条件；RT快照不是GPU帧身份。
    Frame->SetBoolField(TEXT("capture_near_synchronized"),Audit.CaptureNearSynchronized); Frame->SetNumberField(TEXT("capture_near_distance_cm"),Audit.CaptureNearDistance); Frame->SetNumberField(TEXT("main_near_distance_cm"),Audit.MainNearDistance); if(bTemporalWitness||bKeycap) Frame->SetNumberField(TEXT("actual_capture_mask_width"),Audit.ActualCaptureMaskWidth);
    Frame->SetNumberField(TEXT("request_real_seconds"),RequestedAt-Started); Frame->SetNumberField(TEXT("request_group_index"),RequestGroup); Frame->SetBoolField(TEXT("request_measured_eligible"),bRequestMeasured); Frame->SetNumberField(TEXT("callback_phase_real_seconds"),CallbackAt-GroupStarted); Frame->SetStringField(TEXT("eligibility_source"),Pulse?TEXT("pulse_no_warmup_first_response"):TEXT("frozen_at_request: warm_callbacks>=8 && request_phase_real_seconds>=1"));
    if(!Pulse) { Frame->SetNumberField(TEXT("request_phase_real_seconds"),RequestPhaseSeconds); Frame->SetNumberField(TEXT("phase_real_seconds"),RequestPhaseSeconds); } // 既有first_measured字段沿用请求资格时间，不能用921600像素处理结束时钟。
    if(bKeycap) { Frame->SetStringField(TEXT("actual_source_world"),Source->GetComponentTransform().ToString()); Frame->SetStringField(TEXT("actual_mask_world"),Mask->GetComponentTransform().ToString()); Frame->SetNumberField(TEXT("actual_key_travel_cm"),FVector::Distance(Source->GetComponentLocation(),SourceReference.GetLocation())); Frame->SetNumberField(TEXT("actual_mask_travel_cm"),FVector::Distance(Mask->GetComponentLocation(),MaskReference.GetLocation())); Frame->SetNumberField(TEXT("source_mask_position_error_cm"),FVector::Distance(Source->GetComponentLocation(),Mask->GetComponentLocation())); Frame->SetNumberField(TEXT("root_query_max_shift_cm"),KeyQueryMaxShift); }
    if(Pulse) { Frame->SetNumberField(TEXT("pulse_step"),PulseIndex); Frame->SetNumberField(TEXT("pulse_sample_index"),PulseSamples.Num()); Frame->SetNumberField(TEXT("requested_capture_mask_width"),ExpectedMaskWidth); Frame->SetBoolField(TEXT("first_response_without_warmup"),PulseSamples.IsEmpty()); if(!ReadRTWitness(Bitmap,Frame,true)) return; PulseSamples.Add(MakeShared<FJsonValueObject>(Frame)); NextSample=FPlatformTime::Seconds()+.04; return; } // 脉冲第一响应不弃帧、不套暖机。
    if(!bRequestMeasured) { if(Warmups.Num()>=256) { Fail(TEXT("UNVERIFIED: warmup exceeded256 valid callbacks")); return; } Warmups.Add(MakeShared<FJsonValueObject>(Frame)); return; }
    if(Warmups.Num()<8||RequestPhaseSeconds<1.||CallbackAt-GroupStarted<1.) { Fail(TEXT("UNVERIFIED: frozen request measurement eligibility inconsistent with callback phase")); return; }
    if(bTemporalWitness&&!bKeycap&&Group==0&&Samples.IsEmpty()) { FixedOffBaseline=Bitmap; FixedOffBaselineFrame=GFrameCounter; } // 仅枪pulse保首测off主图供旧边沿阴性，Keycap不重复pulse。
    if(Group==1&&(bTemporalWitness||!bRTAttempted)&&!ReadRTWitness(Bitmap,Frame)) return;
    if(bKeycap&&Group==3&&!ReadRTWitness(Bitmap,Frame)) return;
    Frame->SetNumberField(TEXT("sample_index"),Samples.Num()); Samples.Add(MakeShared<FJsonValueObject>(Frame)); NextSample=FPlatformTime::Seconds()+.04; // 回调只存结果，不能排新请求/切输出或结束进程。
}
void UDemoOutlineROIPrototypeAudit::AdvanceSamples()
{
    DEMO_LOG_TICK(); const double Now=FPlatformTime::Seconds(); const bool Press=bKeycap&&(Group==2||Group==3),Slow=bKeycap?Group>=4:Group>=2; if(bPending) { if(Now-RequestedAt>3.) Fail(TEXT("UNVERIFIED: normal screenshot callback timeout")); return; }
    if(bCallbackReady)
    {
        if(FScreenshotRequest::IsScreenshotRequested()) return; bCallbackReady=false; // ProcessScreenShots广播之后Reset，下一Tick才允许改阶段/下一张。
        if(Samples.Num()==16)
        {
            if(bTemporalWitness&&Group==1&&!FinalizeTemporalR()) return;
            if(Slow) { const double First=Samples[0]->AsObject()->GetNumberField(TEXT("actual_yaw_offset_degrees")),Last=Samples.Last()->AsObject()->GetNumberField(TEXT("actual_yaw_offset_degrees")); if(FMath::Abs(First+.15)>.003||FMath::Abs(Last-.15)>.003||Last-First<.29) { Fail(TEXT("UNVERIFIED: actual finalized slowyaw endpoints not exercised")); return; } }
            static const TCHAR* Labels[]={TEXT("fixed_off"),TEXT("fixed_on"),TEXT("slowyaw_off"),TEXT("slowyaw_on")}; static const TCHAR* KeyLabels[]={TEXT("fixed_off"),TEXT("fixed_on"),TEXT("press_off"),TEXT("press_on"),TEXT("slowyaw_off"),TEXT("slowyaw_on")}; TSharedPtr<FJsonObject> Result=MakeShared<FJsonObject>(); Result->SetStringField(TEXT("label"),bKeycap?KeyLabels[Group]:Labels[Group]); Result->SetArrayField(TEXT("warmup_callbacks"),Warmups); Result->SetArrayField(TEXT("samples"),Samples); Result->SetNumberField(TEXT("valid_measured_callbacks"),16); Result->SetNumberField(TEXT("warmup_valid_callbacks"),Warmups.Num()); Result->SetNumberField(TEXT("first_measured_phase_real_seconds"),Samples[0]->AsObject()->GetNumberField(TEXT("phase_real_seconds"))); Groups.Add(MakeShared<FJsonValueObject>(Result));
            if(Press)
            {
                const UDemoCasinoDeviceInputComponent* Input=Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); KeyAmountAfter=Controller->GetMenuFlow()->GetCasinoDeviceAmountText(); Result->SetNumberField(TEXT("actual_key_peak_travel_cm"),KeyPressPeak); Result->SetNumberField(TEXT("actual_key_final_travel_cm"),KeyPressTravel); Result->SetBoolField(TEXT("normal_device_primary_gesture_owned"),bKeyOwnedGesture); Result->SetNumberField(TEXT("moving_rt_samples"),Group==3?KeyMovingRTSamples:0);
                if(!bKeyGestureStarted||bKeyDown||!bKeyOwnedGesture||KeyPressPeak<=.01||KeyPressTravel>.01||!Input||Input->GetLastSubmittedRequestId()!=KeyRequestBefore||KeyAmountAfter!=TEXT("0")||(Group==3&&(PressRFrames.Num()!=16||KeyMovingRTSamples==0))) { Fail(TEXT("UNVERIFIED: actual key press/return/ownership/query/amount or moving-pose RT not exercised")); return; }
            }
            ++Group; Samples.Reset(); Warmups.Reset();
            if(Group==(bKeycap?6:4)) { if(!bTemporalWitness||bKeycap) StopMemoryCapture(); Controller->SetControlRotation(OriginalRotation); YawOffset=0.; Bridge->SetOutputEnabled(true); Stage=bTemporalWitness&&!bKeycap?9:5; WaitStarted=Now; return; }
            GroupStarted=Now; NextSample=Now; YawOffset=(bKeycap?Group>=4:Group>=2)?-.15:0.; FRotator Rotation=OriginalRotation; Rotation.Yaw+=YawOffset; Controller->SetControlRotation(Rotation); YawChangedAt=Now; Bridge->SetOutputEnabled(Group%2==1); if(bKeycap) { KeyPressPeak=0.; bKeyGestureStarted=false; bKeyOwnedGesture=false; if(Group==3) { PressRFrames.Reset(); PreviousPressR.Reset(); KeyMovingRTSamples=0; } } return;
        }
    }
    const bool Warming=Warmups.Num()<8||Now-GroupStarted<1.;
    if(Slow&&!Warming) { const double Offset=-.15+.3*Samples.Num()/15.; if(!FMath::IsNearlyEqual(Offset,YawOffset,.0000001)) { YawOffset=Offset; FRotator Rotation=OriginalRotation; Rotation.Yaw+=Offset; Controller->SetControlRotation(Rotation); YawChangedAt=Now; return; } }
    if(Now<NextSample||(Slow&&Now-YawChangedAt<.04)) return; FString Error; if(!ReadView(Error)) { if(Slow&&Now-YawChangedAt<=2.) return; Fail(TEXT("UNVERIFIED: actual production view did not settle: ")+Error); return; }
    if(Press&&!Warming&&!bKeyGestureStarted) { bKeyGestureStarted=true; bKeyDown=true; KeyPressStarted=Now; ++KeyGestures; } // 原IA_Fire真值从下一正常输入Tick生效，第一RT可为Rest；不能重演动作掩盖漏掉的移动帧。
    if(FScreenshotRequest::IsScreenshotRequested()) { Fail(TEXT("UNVERIFIED: foreign screenshot request appeared")); return; }
    RequestedAt=FPlatformTime::Seconds(); RequestPhaseSeconds=RequestedAt-GroupStarted; RequestGroup=Group; bRequestMeasured=Warmups.Num()>=8&&RequestPhaseSeconds>=1.; RequestFrame=GFrameCounter; ++RequestSerial; // 真正排截图前冻结资格，早于正常绘制及所有回调扫描。
    FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Memory_%03llu.png"),RequestSerial),false,false); OwnedRequest=FScreenshotRequest::GetFilename(); bPending=true;
}
void UDemoOutlineROIPrototypeAudit::AdvancePulse()
{
    DEMO_LOG_TICK(); const double Now=FPlatformTime::Seconds(); FString Error;
    if(Stage==9)
    {
        if(!ReadView(Error)) { if(Now-WaitStarted<=2.) return; Fail(TEXT("UNVERIFIED: fixed POV did not naturally settle before mask pulse: ")+Error); return; }
        if(!Bridge->SetCaptureMaskWidthForAudit(0)) { Fail(TEXT("UNVERIFIED: capture Mask0 pulse request rejected")); return; }
        bPulseMaskBorrowed=true; PulseIndex=0; PulseSamples.Reset(); Stage=10; NextSample=Now; // 本Tick直接请求第一响应，不等暖机或丢弃任何有效图。
    }
    if(bPending) { if(Now-RequestedAt>3.) Fail(TEXT("UNVERIFIED: pulse screenshot callback timeout")); return; }
    if(bCallbackReady)
    {
        if(FScreenshotRequest::IsScreenshotRequested()) return; bCallbackReady=false;
        if(PulseSamples.Num()==4)
        {
            static const TCHAR* Labels[]={TEXT("mask0_pre"),TEXT("mask2_on"),TEXT("mask0_post")}; TSharedPtr<FJsonObject> Step=MakeShared<FJsonObject>(); Step->SetStringField(TEXT("label"),Labels[PulseIndex]); Step->SetNumberField(TEXT("requested_capture_mask_width"),PulseIndex==1?2:0); Step->SetNumberField(TEXT("valid_callbacks"),4); Step->SetNumberField(TEXT("discarded_callbacks"),0); Step->SetBoolField(TEXT("composite_always_enabled"),true); Step->SetArrayField(TEXT("samples"),PulseSamples); Step->SetStringField(TEXT("scope"),TEXT("limited same-POV first-response causal witness at actual RT R and selected old-positive main probes; no generic GPU frame identity or full-game claim")); PulseGroups.Add(MakeShared<FJsonValueObject>(Step)); PulseSamples.Reset(); ++PulseIndex;
            if(PulseIndex==3) { if(!Bridge->SetCaptureMaskWidthForAudit(2)) { Fail(TEXT("UNVERIFIED: final capture Mask2 restore rejected")); return; } bPulseCompleted=true; StopMemoryCapture(); Bridge->SetOutputEnabled(true); Stage=5; WaitStarted=Now; return; }
            if(!Bridge->SetCaptureMaskWidthForAudit(PulseIndex==1?2:0)) { Fail(TEXT("UNVERIFIED: next capture mask pulse request rejected")); return; } NextSample=Now; // 切宽后同GT Tick立即取第一响应，不以.04秒延后隐藏旧图。
        }
    }
    if(Now<NextSample) return; if(!ReadView(Error)) { Fail(TEXT("UNVERIFIED: pulse fixed target/view drift: ")+Error); return; }
    if(FScreenshotRequest::IsScreenshotRequested()) { Fail(TEXT("UNVERIFIED: foreign request during finite pulse")); return; }
    RequestedAt=FPlatformTime::Seconds(); RequestGroup=Group; RequestPhaseSeconds=0.; bRequestMeasured=true; RequestFrame=GFrameCounter; ++RequestSerial; // pulse显式无warm，仍保请求关联，不复用前一组资格。
    FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Memory_%03llu.png"),RequestSerial),false,false); OwnedRequest=FScreenshotRequest::GetFilename(); bPending=true;
}
bool UDemoOutlineROIPrototypeAudit::RequestPNG(const FString& Name)
{
    DEMO_LOG_CALL(); if(PNGs.Num()>=2||ScreenshotHandle.IsValid()||!PendingPNG.IsEmpty()||FScreenshotRequest::IsScreenshotRequested()||!Budget()) { Fail(TEXT("UNVERIFIED: two-PNG/sole-request/evidence budget gate")); return false; }
    PendingPNG=Directory/(Name+TEXT(".png")); PNGFrame=GFrameCounter; PNGRequestedAt=FPlatformTime::Seconds(); PNGs.Add(PendingPNG); FScreenshotRequest::RequestScreenshot(PendingPNG,false,false); return true;
}
void UDemoOutlineROIPrototypeAudit::OnPostTick(UWorld* World,ELevelTick Type,float Delta)
{
    DEMO_LOG_TICK(); if(bFinished||World!=GetWorld()||Type==LEVELTICK_TimeOnly||LastTickFrame==GFrameCounter) return; LastTickFrame=GFrameCounter; const double Now=FPlatformTime::Seconds();
    if(Now-Started>90.) Fail(TEXT("UNVERIFIED:90 real-second fixture timeout")); if(bFailed) { Finish(false); return; }
    if(bKeycap&&Controller.IsValid()&&KeyFireAction.IsValid()) { if(bKeyDown&&Now-KeyPressStarted>=.25) bKeyDown=false; if(!InjectKeyPrimary()) { Fail(TEXT("UNVERIFIED: original EnhancedPlayerInput unavailable")); return; } ReadKeyMotion(); if(bFailed) return; } // 每真实GT读运动与根Query，截图等待也不能停掉释放事件。
    if(!PendingPNG.IsEmpty()) { if(GFrameCounter>PNGFrame&&IFileManager::Get().FileSize(*PendingPNG)>1000) PendingPNG.Reset(); else if(Now-PNGRequestedAt>3.) Fail(TEXT("UNVERIFIED: normal PNG write timeout")); return; }
    if(Stage==4) { AdvanceSamples(); return; }
    if(Stage==9||Stage==10) { AdvancePulse(); return; }
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(World,0)); ADemoCharacter* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>(); UDemoRunSaves* Saves=World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr; const ADemoGameState* State=World->GetGameState<ADemoGameState>(); ADemoPlayerState* PS=Pawn?Pawn->GetPlayerState<ADemoPlayerState>():nullptr; UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
    if(!PC||!Pawn||!Mode||!Saves||!State||!Progress||!Bridge.IsValid()) return; Player=Pawn; Controller=PC;
    UDemoHubShopWorldSubsystem* Registry=World->GetSubsystem<UDemoHubShopWorldSubsystem>();
    if(bKeycap&&Stage>=11&&Stage<=14) { AdvanceKeyPreparation(); return; }
    if(Stage==0)
    {
        UDemoMenuFlowComponent* Flow=PC->GetMenuFlow(); if(!Flow) return; Flow->UpdateRealtime(0.f); if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) Flow->DispatchUIAction(EDemoUIAction::OverlayBack);
        if(Flow->HasBlockingOverlay()||!Saves->CreateSlot(0)||!Mode->StartRun()) { Fail(TEXT("UNVERIFIED: normal temporary-slot StartRun")); return; } Saves->ConsumePending(); PC->OnRunReady();
        if(bKeycap) { FDemoPlayerProgressSnapshot BudgetSnapshot=Progress->GetSnapshot(); BudgetSnapshot.Coins=6000; if(!Progress->Commit(BudgetSnapshot)||!Mode->SaveCheckpoint()) { Fail(TEXT("UNVERIFIED: isolated Editor temporary keycap fixture budget")); return; } KeyFireAction=LoadObject<UInputAction>(nullptr,TEXT("/Game/Weapons/Input/IA_Fire.IA_Fire")); if(!KeyFireAction.IsValid()||!Cast<UEnhancedPlayerInput>(PC->PlayerInput)) { Fail(TEXT("UNVERIFIED: existing keycap IA_Fire/EnhancedPlayerInput")); return; } }
        InitialCoins=Progress->GetSnapshot().Coins; InitialSerial=Progress->GetSnapshot().Gambling.Serial; Stage=bKeycap?11:1; return;
    }
    if(Stage==1)
    {
        if(!Registry||!Registry->GetDefinition()||State->Phase!=EDemoPhase::Hub) return; ADemoGunDisplay* Gun=Registry->FindDisplay(TEXT("gunshop_rifle_01")); if(!Gun||!Gun->IsDisplayReady()||!Gun->GetDisplayMesh()) return;
        const FDemoHubDisplayBinding* Binding=Registry->GetDefinition()->FindDisplay(Gun->DisplayId); const FDemoHubWeaponOffer* Offer=Binding?Registry->GetDefinition()->FindOffer(Binding->OfferId):nullptr;
        if(!Offer||Offer->WeaponId!=TEXT("rifle")||Gun->OfferId!=Binding->OfferId) { Fail(TEXT("UNVERIFIED: registry gun binding is not rifle")); return; } Display=Gun; Source=Gun->GetDisplayMesh(); AimPoint=Gun->GetFocusPoint(); FVector Stand=AimPoint+Gun->GetActorTransform().TransformVectorNoScale(FVector(110,-200,0)); Stand.Z=Pawn->GetActorLocation().Z;
        if(!Pawn->TeleportTo(Stand,Pawn->GetActorRotation())) { Fail(TEXT("UNVERIFIED: normal oblique gun Teleport collision")); return; } Pawn->GetCharacterMovement()->StopMovementImmediately(); Pawn->ConsumeMovementInputVector(); PC->SetControlRotation((AimPoint-Pawn->GetSharedViewLocation()).Rotation()); WaitStarted=Now; Stage=2; return;
    }
    if(Stage==2)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(Pawn); for(UPrimitiveComponent* Component:Components) if(IsValid(Component)&&Component->IsRegistered()&&Component->bRenderCustomDepth&&Component->CustomDepthStencilValue==253&&Component->IsVisible()&&!Component->bHiddenInGame) Mask=Component;
        UCameraComponent* Camera=Pawn->GetFirstPersonCameraComponent(); int32 MIDs=0; for(const FWeightedBlendable& Entry:Camera->PostProcessSettings.WeightedBlendables.Array) if(UMaterialInstanceDynamic* MID=Cast<UMaterialInstanceDynamic>(Entry.Object); MID&&Entry.Weight>0.f&&MID->Parent&&MID->Parent->GetPathName()==TEXT("/Game/Data/HubShops/M_HubGunSilhouette_PP.M_HubGunSilhouette_PP")) { ProductionMID=MID; ++MIDs; }
        FString Error; if(MIDs!=1||!ReadTarget(Error)) { if(Now-WaitStarted>2.) Fail(TEXT("UNVERIFIED: normal production ShopFocus not ready: ")+Error); return; }
        OriginalWidth=ProductionMID->K2_GetScalarParameterValue(TEXT("OutlineWidthPx")); OriginalRotation=PC->GetControlRotation(); if(!bKeycap) { const FVector Outward=Display->GetActorTransform().TransformVectorNoScale(FVector(0,-1,0)).GetSafeNormal2D(); ObliqueAngle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Outward,(Pawn->GetSharedViewLocation()-AimPoint).GetSafeNormal2D()),-1.,1.))); } FocusDistance=FVector::Distance(AimPoint,Pawn->GetSharedViewLocation());
        if((!bKeycap&&ObliqueAngle<=20.)||OriginalWidth!=2.f) { Fail(TEXT("UNVERIFIED: actual oblique or original production width gate")); return; }
        if(BindStarted<=0.) BindStarted=Now; // 首次正常焦点准备完毕后计时；Root一次标准Cache后的异步shader等待不得冒称材质失败。
        if(!Bridge->BindTarget(Camera,Mask.Get())) { const FString Reason=Bridge->ReadFailure(); if(!Reason.IsEmpty()) Fail(TEXT("UNVERIFIED: Root BindTarget: ")+Reason); else if(Now-BindStarted>15.) Fail(TEXT("UNVERIFIED: actual shader readiness did not settle within15 real seconds")); return; }
        bBound=true; Bridge->SetOutputEnabled(false); Stage=3; WaitStarted=Now; return;
    }
    if(Stage==3)
    {
        FDemoOutlineROIFrameAudit Audit; if(!Bridge->ReadFrameAudit(Audit)||!Audit.Submitted||!Audit.MainViewRechecked||!Audit.CaptureViewObserved) { if(Now-WaitStarted>3.) Fail(TEXT("UNVERIFIED: Root first finalized submitted view: ")+Bridge->ReadFailure()+TEXT(" ")+Audit.Reason); return; }
        UCameraComponent* Camera=Pawn->GetFirstPersonCameraComponent(); FVector Location; FRotator Rotation; PC->GetPlayerViewPoint(Location,Rotation); PawnReference=Pawn->GetActorLocation(); SourceReference=Source->GetComponentTransform(); MaskReference=Mask->GetComponentTransform(); CameraReference=Camera->GetComponentTransform(); POVReference=FTransform(Rotation,Location); ReferenceFOV=PC->PlayerCameraManager->GetFOVAngle(); Generation=Audit.Generation;
        const IConsoleVariable* Delegate=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate")); FViewport* Viewport=World->GetGameViewport()->Viewport;
        if(!Delegate||Delegate->GetInt()!=1||!Viewport||Viewport->GetSceneHDREnabled()||Viewport->GetSizeXY()!=FIntPoint(1280,720)||FScreenshotRequest::IsScreenshotRequested()||UGameViewportClient::OnScreenshotCaptured().IsBound()||ProductionMID->K2_GetScalarParameterValue(TEXT("OutlineWidthPx"))!=0.f) { Fail(TEXT("UNVERIFIED: SDR/full-view/sole screenshot delegate/production width0 gate")); return; }
        ScreenshotHandle=UGameViewportClient::OnScreenshotCaptured().AddUObject(this,&UDemoOutlineROIPrototypeAudit::OnScreenshot); // GT弱UObject，正常ProcessScreenShots完成后消费内存，回调不排下一张。
        Group=0; GroupStarted=Now; NextSample=Now; Stage=4; return;
    }
    if(Stage==5||Stage==7)
    {
        if(Now-WaitStarted<1.) return; FString Error; if(!ReadView(Error)) { Fail(TEXT("UNVERIFIED: same fixed view PNG context: ")+Error); return; }
        if(bTemporalWitness) { FDemoOutlineROIFrameAudit Audit; if(!Bridge->ReadFrameAudit(Audit)||!Audit.Submitted||Audit.ActualCaptureMaskWidth!=2) { Fail(TEXT("UNVERIFIED: final Mask2 restoration not observed before PNG")); return; } bPulseMaskRestored=true; bPulseMaskBorrowed=false; }
        const bool On=Stage==5; Stage=On?6:8; RequestPNG(On?TEXT("01_T03_FullView_Fixed_On"):TEXT("02_T03_FullView_Fixed_Off")); return;
    }
    if(Stage==6) { Bridge->SetOutputEnabled(false); Stage=7; WaitStarted=Now; return; }
    if(Stage==8)
    {
        const bool WitnessContract=bKeycap?(PressRFrames.Num()==16&&KeyMovingRTSamples>0&&KeyGestures==2&&KeyQueryMaxShift<=.001&&PulseGroups.IsEmpty()&&RTReadbacks==(bTemporalWitness?32:17)&&(!bTemporalWitness||TemporalRFrames.Num()==16)):(!bTemporalWitness||(TemporalRFrames.Num()==16&&PulseGroups.Num()==3&&bPulseCompleted&&bPulseMaskRestored&&RTReadbacks==28));
        if(Groups.Num()!=(bKeycap?6:4)||PNGs.Num()!=2||!bRTValid||Progress->GetSnapshot().Coins!=InitialCoins||Progress->GetSnapshot().Gambling.Serial!=InitialSerial||!Budget()||!WitnessContract) { Fail(TEXT("UNVERIFIED: target groups/twoPNG/RT/witness bounds/economic baseline/budget")); return; } Finish(true); return;
    }
}
void UDemoOutlineROIPrototypeAudit::Finish(bool Success)
{
    DEMO_LOG_CALL(); if(bFinished) return; bFinished=true; bKeyDown=false; if(bKeycap) InjectKeyPrimary(); StopMemoryCapture(); bool ProcessRestored=false,MaskRestoreRequested=true; if(Bridge.IsValid()) { if(bTemporalWitness&&bPulseMaskBorrowed) MaskRestoreRequested=Bridge->SetCaptureMaskWidthForAudit(2); Bridge->ClearTarget(TEXT("audit finish")); ProcessRestored=Bridge->RestoreProcessSettings(); }
    if(Controller.IsValid()&&bBound) Controller->SetControlRotation(OriginalRotation); const bool WidthRestored=!bBound||(ProductionMID.IsValid()&&ProductionMID->K2_GetScalarParameterValue(TEXT("OutlineWidthPx"))==OriginalWidth); bCleanupVerified=ProcessRestored&&WidthRestored&&MaskRestoreRequested; Success&=bCleanupVerified;
    if(!bCleanupVerified&&Failure.IsEmpty()) Failure=TEXT("UNVERIFIED: production width or original process settings/full flags not restored");
    if(TickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle); TickHandle.Reset(); if(CleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle); CleanupHandle.Reset();
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("id"),TEXT("OUTLINE-ROI-T03-FULL-VIEW-REFERENCE")); Report->SetStringField(TEXT("status"),Success?TEXT("OBSERVED_LISTED_T03_REFERENCE_CONTRACTS_GRAPHICS_UNVERIFIED"):TEXT("UNVERIFIED_T03_REFERENCE")); Report->SetStringField(TEXT("failure"),Failure); Report->SetBoolField(TEXT("listed_observation_contracts_satisfied"),Success); Report->SetStringField(TEXT("scope"),TEXT("Single Editor standalone oblique registered rifle; full ROI0,0,1280,720/RT1280x720, four fixed/slowyaw off/on groups, one finite RT channel/main-white readback and two normal PNGs. Full-view reference is not crop/performance optimization or complete graphics acceptance.")); Report->SetStringField(TEXT("gpu_frame_identity_limit"),TEXT("CPU GFrameCounter/gen/Owner/Mask and Capture/Main view rechecks plus Alpha write do not prove GPU producer frame identity or no temporal reuse; independent GPU same-frame witness remains unverified."));
    Report->SetNumberField(TEXT("elapsed_real_seconds"),FPlatformTime::Seconds()-Started); Report->SetNumberField(TEXT("valid_drawn_callbacks"),ValidCallbacks); Report->SetNumberField(TEXT("rejected_callbacks"),RejectedCallbacks); Report->SetNumberField(TEXT("generation"),Generation); Report->SetStringField(TEXT("target_id"),TEXT("gunshop_rifle_01")); Report->SetStringField(TEXT("source_component"),Source.IsValid()?Source->GetPathName():TEXT("unavailable")); Report->SetNumberField(TEXT("oblique_angle_degrees"),ObliqueAngle); Report->SetNumberField(TEXT("focus_distance_cm"),FocusDistance); Report->SetBoolField(TEXT("production_width_restored"),bCleanupVerified); Report->SetBoolField(TEXT("rt_readback_attempted"),bRTAttempted); Report->SetArrayField(TEXT("groups"),Groups); Report->SetArrayField(TEXT("incomplete_samples"),Samples); Report->SetArrayField(TEXT("incomplete_warmups"),Warmups); if(RTWitness.IsValid()) Report->SetObjectField(TEXT("rt_witness"),RTWitness);
    Report->SetBoolField(TEXT("production_width_restored"),WidthRestored); Report->SetBoolField(TEXT("process_settings_original_values_and_flags_restored"),ProcessRestored); Report->SetStringField(TEXT("source_world"),Source.IsValid()?Source->GetComponentTransform().ToString():TEXT("unavailable")); Report->SetNumberField(TEXT("source_unique_id"),Source.IsValid()?Source->GetUniqueID():0); Report->SetNumberField(TEXT("mask_unique_id"),Mask.IsValid()?Mask->GetUniqueID():0);
    FString ModelPath; if(const UStaticMeshComponent* Static=Cast<UStaticMeshComponent>(Source.Get())) ModelPath=GetPathNameSafe(Static->GetStaticMesh()); else if(const USkeletalMeshComponent* Skin=Cast<USkeletalMeshComponent>(Source.Get())) ModelPath=GetPathNameSafe(Skin->GetSkeletalMeshAsset()); Report->SetStringField(TEXT("actual_source_model"),ModelPath); // 模型身份来自本轮实际组件，不从商品名猜资源。
    Report->SetBoolField(TEXT("temporal_witness_requested"),bTemporalWitness);
    Report->SetStringField(TEXT("outline_target"),bKeycap?TEXT("Keycap"):TEXT("RifleOblique"));
    if(bKeycap) { Report->SetNumberField(TEXT("keycap_aim_corrections"),KeyAimCorrections); Report->SetNumberField(TEXT("keycap_ray_stable_world_needed"),KeyRayStableNeededWorld); Report->SetNumberField(TEXT("keycap_ray_stable_world_elapsed"),KeyRayStableElapsedWorld); Report->SetNumberField(TEXT("keycap_e_trigger_frame"),static_cast<double>(KeyETriggerFrame)); if(KeyAimFailureFacts.IsValid()) Report->SetObjectField(TEXT("keycap_first_ray_failures"),KeyAimFailureFacts); }
    if(bKeycap)
    {
        Report->SetStringField(TEXT("scope"),TEXT("Keycap only: real Mode1 E lease, actual LocalView digit0 source/unique attached253 mask; six fixed/press/slowyaw off/on groups. Original IA_Fire events once per press group; press_on16 actual RT pose/white observations, fixed_on16 only when TemporalWitness (otherwise1). No mask pulse, betting/lever/purchase, whole-graphics or GPU-frame identity claim. Temporary Editor fixture wallet baseline is6000, digit0 draft edits are independent of unchanged wallet/Serial.")); Report->SetStringField(TEXT("target_id"),KeyDevice.IsValid()?KeyDevice->DeviceId.ToString()+TEXT("/amount.digit.0"):TEXT("keycap_unavailable")); Report->SetArrayField(TEXT("pulse_groups"),PulseGroups);
        TSharedPtr<FJsonObject> Motion=MakeShared<FJsonObject>(); Motion->SetArrayField(TEXT("press_on_rt_frames"),PressRFrames); Motion->SetNumberField(TEXT("moving_rt_samples"),KeyMovingRTSamples); Motion->SetNumberField(TEXT("moving_gt_samples"),KeyGTMovingSamples); Motion->SetNumberField(TEXT("normal_primary_gestures"),KeyGestures); Motion->SetNumberField(TEXT("maximum_actual_key_travel_cm"),KeyMaxTravel); Motion->SetNumberField(TEXT("root_query_count"),KeyQueryIds.Num()); Motion->SetNumberField(TEXT("root_query_max_shift_cm"),KeyQueryMaxShift); Motion->SetStringField(TEXT("amount_before"),KeyAmountBefore); Motion->SetStringField(TEXT("amount_after"),KeyAmountAfter); Motion->SetNumberField(TEXT("economic_request_before"),KeyRequestBefore); Motion->SetStringField(TEXT("scope"),TEXT("Each press group has one complete existing IA_Fire Started/Completed gesture; actual GT peak/return and16 press_on RT observations include measured movement. XOR may reflect real movement; sixteen observations are not sixteen moving poses. Root Query never animated.")); Report->SetObjectField(TEXT("keycap_motion"),Motion); Report->SetBoolField(TEXT("keycap_pulse_not_exercised"),true); Report->SetNumberField(TEXT("rt_readbacks"),RTReadbacks);
        const ADemoPlayerState* PS=Player.IsValid()?Player->GetPlayerState<ADemoPlayerState>():nullptr; const UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr; Report->SetNumberField(TEXT("wallet_initial_coins"),InitialCoins); Report->SetNumberField(TEXT("wallet_final_coins"),Progress?Progress->GetSnapshot().Coins:INDEX_NONE); Report->SetNumberField(TEXT("gambling_initial_serial"),InitialSerial); Report->SetNumberField(TEXT("gambling_final_serial"),Progress?Progress->GetSnapshot().Gambling.Serial:INDEX_NONE);
    }
    if(bTemporalWitness)
    {
        TSharedPtr<FJsonObject> Temporal=MakeShared<FJsonObject>(); Temporal->SetStringField(TEXT("scope"),bKeycap?TEXT("Keycap fixed_on16 binary R frequency observations plus press_on16 moving-pose RT, total32; keycap does not repeat gun pulses. No zero-XOR requirement for movement, no whole-color/mixed-white or full-game stability claim."):TEXT("16 fixed_on valid normal-callback RT reads only; binary R one-runs/CRC, adjacent XOR/frequency; no whole-color CRC/mixed-white flicker proof, no between-sample/full-game claim. 12 more finite pulse reads, total28; no raw RGBA frame export or extra PNG.")); Temporal->SetArrayField(TEXT("frames"),TemporalRFrames); if(TemporalRSummary.IsValid()) Temporal->SetObjectField(TEXT("summary"),TemporalRSummary); Report->SetObjectField(TEXT("temporal_r_witness"),Temporal); if(LastRTWitness.IsValid()) Report->SetObjectField(TEXT("last_rt_witness"),LastRTWitness); Report->SetArrayField(TEXT("pulse_groups"),PulseGroups); Report->SetArrayField(TEXT("pulse_incomplete_samples"),PulseSamples); if(PulseProbeEvidence.IsValid()) Report->SetObjectField(TEXT("pulse_probe_evidence"),PulseProbeEvidence); Report->SetBoolField(TEXT("pulse_completed"),bPulseCompleted); Report->SetBoolField(TEXT("capture_mask2_restored_and_observed"),bPulseMaskRestored); Report->SetBoolField(TEXT("capture_mask_restore_request_succeeded"),MaskRestoreRequested); Report->SetNumberField(TEXT("rt_readbacks"),RTReadbacks);
    }
    TArray<TSharedPtr<FJsonValue>> Images; for(const FString& PNG:PNGs) Images.Add(MakeShared<FJsonValueString>(PNG)); Report->SetArrayField(TEXT("pngs"),Images);
    TSharedPtr<FJsonObject> CVars=MakeShared<FJsonObject>(); for(const TCHAR* Name:{TEXT("r.AntiAliasingMethod"),TEXT("r.CustomDepthTemporalAAJitter"),TEXT("r.ScreenPercentage"),TEXT("r.SecondaryScreenPercentage.GameViewport"),TEXT("r.DynamicRes.OperationMode")}) if(const IConsoleVariable* Value=IConsoleManager::Get().FindConsoleVariable(Name)) CVars->SetStringField(Name,Value->GetString()); Report->SetObjectField(TEXT("nominal_cvars_not_actual_view_evidence"),CVars);
    Report->SetArrayField(TEXT("not_verified"),{MakeShared<FJsonValueString>(TEXT("GPU producer frame identity, pure-white2px complete visible-edge artistic acceptance, real motion/handheld/WPO/LOD/nearclip and thin/partial occluders")),MakeShared<FJsonValueString>(TEXT("Other Owner/view, target/Pawn swap, resize mismatch/cleanup negative cases, split/stereo/HDR, Listen/Cook, CPU/GPU/temporary-depth/peak-memory costs; no-cloud/asset edits"))});
    if(bKeycap&&Controller.IsValid()) { UDemoCasinoDeviceInputComponent* Input=Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); const bool OwnLease=Input&&Input->IsOperating()&&KeyDevice.IsValid()&&Player.IsValid()&&Input->GetCurrentDevice()==KeyDevice.Get()&&Controller->GetPawn()==Player.Get(); if(OwnLease) Input->ExitOperation(!bWorldTeardown); const bool Closed=!Input||!Input->IsOperating()||(KeyDevice.IsValid()&&Input->GetCurrentDevice()!=KeyDevice.Get()); Report->SetBoolField(TEXT("keycap_lease_closed_after_bridge_clear"),Closed); Report->SetBoolField(TEXT("keycap_cleanup_skipped_foreign_lease"),Input&&Input->IsOperating()&&!OwnLease); if(!Closed) { Success=false; Report->SetBoolField(TEXT("listed_observation_contracts_satisfied"),false); Report->SetStringField(TEXT("status"),TEXT("UNVERIFIED_T03_REFERENCE")); Report->SetStringField(TEXT("failure"),TEXT("UNVERIFIED: normal key lease cleanup failed")); } } // 只释放本Pawn+KeyDevice，先缓存纯值/清桥再撤源；teardown不通知旧服务器。
    FString Text; const bool Written=FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text))&&FFileHelper::SaveStringToFile(Text,*(Directory/TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogFPSDemo,Display,TEXT("OUTLINE_ROI_AUDIT_%s directory=%s"),Success&&Written?TEXT("OBSERVATIONS_RECORDED"):TEXT("UNVERIFIED"),*Directory); FPlatformMisc::RequestExitWithStatus(false,Success&&Written?0:1);
}
#endif
