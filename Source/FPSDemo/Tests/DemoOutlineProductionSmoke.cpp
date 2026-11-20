// 职责：极小默认生产入口Smoke；复用真实StartRun/ShopFocus/E/LocalView，只取原生提交事实和两张正常PNG。
// 边界：不改DA/材质/AA/secondary/默认UI，不读RT或调用Factory，成功也不宣称GPU像素/完整游戏验收。
#include "Tests/DemoOutlineProductionSmoke.h"
#include "Characters/DemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Interaction/HubShops/DemoShopFocusComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoLocalViewComponent.h"
#include "Interaction/HubShops/Rendering/DemoLocalOutlineComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Serialization/JsonSerializer.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UnrealClient.h"
#include "UObject/Package.h"

bool UDemoOutlineProductionSmoke::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const UWorld* World=Cast<UWorld>(Outer);
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone&&World->GetOutermost()->GetName()==TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")&&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoOutlineProductionSmoke"))&&!FParse::Param(FCommandLine::Get(),TEXT("DemoOutlineROIPrototype"))&&!FParse::Param(FCommandLine::Get(),TEXT("DemoSlotsPointInputAudit"));
#else
    return false;
#endif
}
bool UDemoOutlineProductionSmoke::DoesSupportWorldType(EWorldType::Type Type) const
{ DEMO_LOG_CALL(); return WITH_EDITOR&&Type==EWorldType::Game; }
void UDemoOutlineProductionSmoke::OnWorldBeginPlay(UWorld& World)
{
    DEMO_LOG_CALL(); Super::OnWorldBeginPlay(World);
#if WITH_EDITOR
    Started=FPlatformTime::Seconds(); Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("OutlineProduction20261116/Smoke")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    if(!IFileManager::Get().MakeDirectory(*Directory,true)) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    TickHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&UDemoOutlineProductionSmoke::OnPostTick); // 仅本World GT，结束精确移除。
#endif
}
void UDemoOutlineProductionSmoke::Deinitialize()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if(TickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle); TickHandle.Reset();
    if(PC.IsValid()&&Player.IsValid()&&Device.IsValid()&&PC->GetPawn()==Player.Get()) if(UDemoCasinoDeviceInputComponent* Input=PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); Input&&Input->IsOperating()&&Input->GetCurrentDevice()==Device.Get()) Input->ExitOperation(false);
#endif
    Super::Deinitialize();
}
#if WITH_EDITOR
void UDemoOutlineProductionSmoke::Aim(const FVector& Point)
{ DEMO_LOG_TICK(); if(PC.IsValid()&&Player.IsValid()) { PC->SetControlRotation((Point-Player->GetSharedViewLocation()).Rotation()); ++AimCorrections; } }
bool UDemoOutlineProductionSmoke::WaitNative(const UObject* Requester)
{
    DEMO_LOG_TICK(); UDemoLocalOutlineComponent* Renderer=PC.IsValid()?PC->FindComponentByClass<UDemoLocalOutlineComponent>():nullptr; UPrimitiveComponent* Mask=nullptr;
    TInlineComponentArray<UPrimitiveComponent*> Components(Player.Get()); for(UPrimitiveComponent* Component:Components) if(IsValid(Component)&&Component->IsRegistered()&&Component->bRenderCustomDepth&&Component->CustomDepthStencilValue==253&&Component->IsVisible()&&!Component->bHiddenInGame) { if(Mask) { StableSince=-1.; return false; } Mask=Component; }
    uint64 Frame=0; FIntPoint Size; if(!Renderer||!Mask||!Renderer->OwnsOutlineTarget(Requester,Mask)||!Renderer->ReadLastSubmissionForSmoke(Frame,Size)) { StableSince=-1.; return false; }
    if(Frame>LastNativeFrame) { ++NativeFreshSamples; LastNativeFrame=Frame; NativeSize=Size; } if(StableSince<0.) StableSince=FPlatformTime::Seconds(); return FPlatformTime::Seconds()-StableSince>=1.; // 原生提交最近<=2帧由Renderer自身核，不声称GPU像素。
}
void UDemoOutlineProductionSmoke::Capture(const FString& Name)
{
    DEMO_LOG_CALL(); if(PNGs.Num()>=2||FScreenshotRequest::IsScreenshotRequested()) { Finish(false,TEXT("PNG request limit/conflict")); return; }
    PendingPNG=Directory/(Name+TEXT(".png")); PNGs.Add(PendingPNG); ShotFrame=GFrameCounter; ShotStarted=FPlatformTime::Seconds(); FScreenshotRequest::RequestScreenshot(PendingPNG,false,false);
}
void UDemoOutlineProductionSmoke::OnPostTick(UWorld* World,ELevelTick Type,float Delta)
{
    DEMO_LOG_TICK(); if(bFinished||World!=GetWorld()||Type==LEVELTICK_TimeOnly) return; const double Now=FPlatformTime::Seconds();
    if(Now-Started>90.) { Finish(false,TEXT("90-second absolute smoke deadline")); return; }
    if(!PendingPNG.IsEmpty()) { if(GFrameCounter>ShotFrame&&IFileManager::Get().FileSize(*PendingPNG)>1000) PendingPNG.Reset(); else if(Now-ShotStarted>3.) Finish(false,TEXT("normal PNG timeout")); return; }
    ADemoPlayerController* Controller=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(World,0)); ADemoCharacter* Pawn=Controller?Cast<ADemoCharacter>(Controller->GetPawn()):nullptr; AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>(); UDemoRunSaves* Saves=World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr; UDemoHubShopWorldSubsystem* Registry=World->GetSubsystem<UDemoHubShopWorldSubsystem>(); const ADemoGameState* State=World->GetGameState<ADemoGameState>();
    if(!Controller||!Pawn||!Mode||!Saves||!Registry||!State) return; PC=Controller; Player=Pawn;
    if(Step==0) { UDemoMenuFlowComponent* Flow=PC->GetMenuFlow(); if(!Flow) return; Flow->UpdateRealtime(0.f); if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) Flow->DispatchUIAction(EDemoUIAction::OverlayBack); if(Flow->HasBlockingOverlay()||!Saves->CreateSlot(0)||!Mode->StartRun()) { Finish(false,TEXT("normal StartRun unavailable")); return; } Saves->ConsumePending(); PC->OnRunReady(); Step=1; return; }
    const UDemoHubShopDefinition* Data=Registry->GetDefinition(); if(!Data||State->Phase!=EDemoPhase::Hub) return;
    if(Step==1) { Gun=Registry->FindDisplay(TEXT("gunshop_rifle_01")); if(!Gun.IsValid()||!Gun->IsDisplayReady()) return; FVector Stand=Gun->GetFocusPoint()+Gun->GetActorTransform().TransformVectorNoScale(FVector(110,-200,0)); Stand.Z=Pawn->GetActorLocation().Z; if(!Pawn->TeleportTo(Stand,Pawn->GetActorRotation())) { Finish(false,TEXT("normal gun stand collision")); return; } Pawn->GetCharacterMovement()->StopMovementImmediately(); Aim(Gun->GetFocusPoint()); Step=2; StableSince=-1.; return; }
    if(Step==2) { FVector Eye; FRotator Rotation; PC->GetPlayerViewPoint(Eye,Rotation); FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(OutlineProductionSmoke),false,Pawn); World->LineTraceSingleByChannel(Hit,Eye,Eye+Rotation.Vector()*Data->InteractionDistanceCm,ECC_Visibility,Query); if(Hit.GetActor()!=Gun.Get()) { Aim(Gun->GetFocusPoint()); StableSince=-1.; return; } if(WaitNative(PC->FindComponentByClass<UDemoShopFocusComponent>())) { Step=3; Capture(TEXT("01_NormalProduction_Gun")); } return; }
    UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    if(Step==3) { for(const FDemoHubCasinoBinding& Binding:Data->Devices) if(Binding.Mode==1) { Device=Registry->FindDevice(Binding.DeviceId); break; } Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr; if(!Controls||!Controls->IsReady()) return; const FVector Stand=Device->GetActorTransform().TransformPosition(FVector(180,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())); if(!Pawn->TeleportTo(Stand,Pawn->GetActorRotation())) { Finish(false,TEXT("normal slots stand collision")); return; } Pawn->GetCharacterMovement()->StopMovementImmediately(); FVector Point; Controls->GetControlPoint(TEXT("slots.pull"),Point); Aim(Point); Step=4; StepStarted=Now; RaySinceWorld=-1.; return; }
    if(Step==4) { if(Now-StepStarted>2.) { Finish(false,TEXT("normal E ray/focus dwell unavailable")); return; } FString Error; if(!Controls||!Controls->ValidateControlHit(Pawn,TEXT("slots.pull"),Error)) { RaySinceWorld=-1.; FVector Point; if(Controls&&Controls->GetControlPoint(TEXT("slots.pull"),Point)) Aim(Point); return; } if(RaySinceWorld<0.) RaySinceWorld=World->GetTimeSeconds(); if(World->GetTimeSeconds()-RaySinceWorld<Data->FocusStableSeconds+2./Data->ScanHz+1./60.) return; Pawn->TriggerWorldInteraction(); Step=5; StepStarted=Now; return; }
    UDemoCasinoDeviceInputComponent* Input=PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); UDemoCasinoLocalViewComponent* View=PC->FindComponentByClass<UDemoCasinoLocalViewComponent>();
    if(Step==5) { if(!Input||!Input->IsOperating()||Input->GetCurrentDevice()!=Device.Get()) { if(Now-StepStarted>2.) Finish(false,TEXT("normal E lease unavailable")); return; } FVector Point; Controls->GetControlPoint(TEXT("amount.digit.0"),Point); Aim(Point); Step=6; StableSince=-1.; StepStarted=Now; return; }
    if(Step==6) { FName Id; FString Material; FTransform Pose; if(!View||!View->ReadFocusedOutlineForAudit(Id,Material,Pose)||Id!=TEXT("amount.digit.0")) { FVector Point; if(Controls&&Controls->GetControlPoint(TEXT("amount.digit.0"),Point)) Aim(Point); StableSince=-1.; if(Now-StepStarted>4.) Finish(false,TEXT("normal key focus unavailable")); return; } if(WaitNative(View)) { Step=7; Capture(TEXT("02_NormalProduction_Keycap")); } return; }
    if(Step==7) Finish(PNGs.Num()==2,TEXT("normal production entry observations only"));
}
void UDemoOutlineProductionSmoke::Finish(bool Success,const FString& Reason)
{
    DEMO_LOG_CALL(); if(bFinished) return; bFinished=true; if(TickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle); TickHandle.Reset();
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("status"),Success?TEXT("OBSERVED_NORMAL_PRODUCTION_ENTRIES"):TEXT("UNVERIFIED_NORMAL_PRODUCTION_ENTRY")); Report->SetStringField(TEXT("reason"),Reason); Report->SetStringField(TEXT("scope"),TEXT("Default production Viewport/DA; normal StartRun, registered gun Focus and E/digit0 LocalView; native Capture+main CPU submission only, no GPU pixels/RT readback/animation/performance/purchase/complete game acceptance.")); Report->SetNumberField(TEXT("elapsed_real_seconds"),FPlatformTime::Seconds()-Started); Report->SetNumberField(TEXT("fresh_native_samples"),NativeFreshSamples); Report->SetNumberField(TEXT("last_native_frame"),static_cast<double>(LastNativeFrame)); Report->SetStringField(TEXT("native_stage_size"),NativeSize.ToString()); Report->SetNumberField(TEXT("normal_aim_corrections"),AimCorrections); TArray<TSharedPtr<FJsonValue>> Images; for(const FString& PNG:PNGs) Images.Add(MakeShared<FJsonValueString>(PNG)); Report->SetArrayField(TEXT("pngs"),Images);
    FString Text; const bool Written=FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text))&&FFileHelper::SaveStringToFile(Text,*(Directory/TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); UE_LOG(LogFPSDemo,Display,TEXT("OUTLINE_PRODUCTION_SMOKE_%s directory=%s"),Success&&Written?TEXT("OBSERVED"):TEXT("UNVERIFIED"),*Directory); FPlatformMisc::RequestExitWithStatus(false,Success&&Written?0:1);
}
#endif
