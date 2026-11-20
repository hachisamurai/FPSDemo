// 职责：本人正常尺度阴影代理的Editor隔离运行审查，仅-DemoOwnerShadowAudit -game Standalone启用。
// 真实StartRun/PC视点/装备/挥击；读取最终骨与四张普通RHI PNG。无连续序列/视频，不修改被测骨/DA/材质/投影标志或TA地图资产。
#include "Tests/DemoOwnerShadowAudit.h"
#include "Animation/DemoOwnerShadowComponent.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/DemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
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
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Melee/DemoMeleeComponent.h"

namespace
{
    /** 纯只读组件快照；骨只取有限名称的真实世界/局部框，不显式Refresh/Tick或从源动画重采。 */
    TSharedPtr<FJsonObject> ShadowAuditMesh(const USkinnedMeshComponent* Mesh)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        TSharedPtr<FJsonObject> Out=MakeShared<FJsonObject>();
        if(!Mesh) { Out->SetBoolField(TEXT("present"),false); return Out; }
        Out->SetBoolField(TEXT("present"),true); Out->SetStringField(TEXT("component"),Mesh->GetPathName());
        Out->SetStringField(TEXT("mesh"),GetPathNameSafe(Mesh->GetSkinnedAsset())); Out->SetStringField(TEXT("world_transform"),Mesh->GetComponentTransform().ToString());
        Out->SetBoolField(TEXT("cast_shadow"),Mesh->CastShadow); Out->SetBoolField(TEXT("hidden_shadow"),Mesh->bCastHiddenShadow); Out->SetBoolField(TEXT("hidden_in_game"),Mesh->bHiddenInGame);
        Out->SetBoolField(TEXT("hidden_in_scene_capture"),Mesh->bHiddenInSceneCapture); Out->SetBoolField(TEXT("visible_in_ray_tracing"),Mesh->bVisibleInRayTracing);
        TSharedPtr<FJsonObject> Bones=MakeShared<FJsonObject>(); // 有限真实已求值骨框，单位cm，不读solver目标。
        const TArray<FTransform>& CS=Mesh->GetComponentSpaceTransforms();
        for(const FName Name:{FName(TEXT("pelvis")),FName(TEXT("spine_01")),FName(TEXT("spine_05")),FName(TEXT("upperarm_l")),FName(TEXT("lowerarm_l")),FName(TEXT("hand_l")),FName(TEXT("upperarm_r")),FName(TEXT("lowerarm_r")),FName(TEXT("hand_r")),FName(TEXT("thumb_03_r")),FName(TEXT("foot_r"))})
        {
            const int32 Index=Mesh->GetBoneIndex(Name); if(!CS.IsValidIndex(Index)) continue;
            TSharedPtr<FJsonObject> Bone=MakeShared<FJsonObject>(); Bone->SetStringField(TEXT("component_space"),CS[Index].ToString()); Bone->SetStringField(TEXT("world"),Mesh->GetBoneTransform(Index).ToString());
            const FName Parent=Mesh->GetParentBone(Name); const int32 ParentIndex=Mesh->GetBoneIndex(Parent);
            Bone->SetStringField(TEXT("local"),(CS.IsValidIndex(ParentIndex)?CS[Index].GetRelativeTransform(CS[ParentIndex]):CS[Index]).ToString()); Bones->SetObjectField(Name.ToString(),Bone);
        }
        Out->SetObjectField(TEXT("bones"),Bones); return Out;
    }
}

bool UDemoOwnerShadowAudit::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const UWorld* World=Cast<UWorld>(Outer);
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone&&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoOwnerShadowAudit"));
#else
    return false;
#endif
}
bool UDemoOwnerShadowAudit::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    return WorldType==EWorldType::Game;
#else
    return false;
#endif
}
void UDemoOwnerShadowAudit::OnWorldBeginPlay(UWorld& InWorld)
{
    DEMO_LOG_CALL(); Super::OnWorldBeginPlay(InWorld);
#if WITH_EDITOR
    if(!ShouldCreateSubsystem(&InWorld)) return;
    bActive=true; StartedAt=FPlatformTime::Seconds(); StartedWorldAt=InWorld.GetTimeSeconds(); NextWorldAt=StartedWorldAt+.5;
    if(!Check(FApp::CanEverRender(),TEXT("actual RHI available"))||!CheckBudget(TEXT("begin"))) { Finish(false); return; }
    Directory=FPaths::ProjectSavedDir()/TEXT("ShadowProxy20261115/Runtime")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    if(!Check(IFileManager::Get().MakeDirectory(*Directory,true),TEXT("create unique bounded evidence directory"))) { Finish(false); return; }
    PostActorHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&UDemoOwnerShadowAudit::OnPostActorTick); // 本World游戏线程EndFrame后；没有Lambda/裸捕获，Finish和卸载显式解除。
    UE_LOG(LogFPSDemo,Display,TEXT("OWNER_SHADOW_AUDIT_BEGIN directory=%s max_png=4 max_bytes=67108864"),*Directory);
#endif
}
void UDemoOwnerShadowAudit::Deinitialize()
{
    DEMO_LOG_CALL(); bActive=false;
    if(PostActorHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(PostActorHandle); PostActorHandle.Reset();
    Player.Reset(); Controller.Reset(); Observer.Reset(); Super::Deinitialize();
}
bool UDemoOwnerShadowAudit::Check(bool Condition,const FString& Message)
{
    DEMO_LOG_CALL(); Checks.Add(FString::Printf(TEXT("%s step=%d %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Step,*Message));
    UE_LOG(LogFPSDemo,Log,TEXT("OWNER_SHADOW_AUDIT_CHECK %s"),*Checks.Last()); if(!Condition) bFailed=true; return Condition;
}
bool UDemoOwnerShadowAudit::CheckBudget(const FString& Stage)
{
    DEMO_LOG_CALL(); uint64 Total=0,Free=0; int64 EvidenceBytes=0; // 本次目录实际字节与项目盘实测，非历史清理快照。
    TArray<FString> Files; if(!Directory.IsEmpty()) IFileManager::Get().FindFilesRecursive(Files,*Directory,TEXT("*"),true,false);
    for(const FString& File:Files) EvidenceBytes+=FMath::Max<int64>(0,IFileManager::Get().FileSize(*File));
    FString LogFile; // 显式启动日志可能在证据目录外，计入同一64MiB预算；正常启动无abslog时只核本夹具输出。
    if(FParse::Value(FCommandLine::Get(),TEXT("abslog="),LogFile)&&!Files.Contains(LogFile)) EvidenceBytes+=FMath::Max<int64>(0,IFileManager::Get().FileSize(*LogFile));
    const bool Measured=FPlatformMisc::GetDiskTotalAndFreeSpace(FPaths::ProjectDir(),Total,Free);
    return Check(Measured&&Free>=10ull*1024ull*1024ull*1024ull&&EvidenceBytes<64ll*1024ll*1024ll,FString::Printf(TEXT("measured10GiB reserve and64MiB evidence budget stage=%s free_bytes=%llu evidence_bytes=%lld measured=%d"),*Stage,static_cast<unsigned long long>(Free),static_cast<long long>(EvidenceBytes),Measured));
}
bool UDemoOwnerShadowAudit::PrepareStage()
{
    DEMO_LOG_CALL(); ADemoCharacter* Pawn=Player.Get(); ADemoPlayerController* PC=Controller.Get();
    if(!Check(Pawn&&PC,TEXT("actual possessed Avatar exists"))) return false;
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    AStaticMeshActor* Floor=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,19995),FRotator::ZeroRotator);
    if(!Check(Cube&&Floor,TEXT("isolated physical shadow-floor fixture exists"))) return false;
    Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable); Floor->GetStaticMeshComponent()->SetStaticMesh(Cube); Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(40,40,.1)); Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    ADirectionalLight* Light=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-40,125,0));
    if(!Check(Light!=nullptr,TEXT("fixture directional shadow light exists"))) return false;
    Light->GetLightComponent()->SetMobility(EComponentMobility::Movable); Light->GetLightComponent()->SetIntensity(3.f);
    Pawn->GetCharacterMovement()->StopMovementImmediately(); Pawn->SetActorLocation(FVector(0,0,20000+Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2)); Pawn->SetActorRotation(FRotator::ZeroRotator); Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Observer=GetWorld()->SpawnActor<ACameraActor>(Pawn->GetActorLocation()+FVector(-320,-280,110),FRotator::ZeroRotator);
    if(!Check(Observer.IsValid(),TEXT("temporary external view target exists"))) return false;
    Observer->SetActorRotation((Pawn->GetActorLocation()-Observer->GetActorLocation()).Rotation()); Observer->GetCameraComponent()->SetFieldOfView(60.f);
    PC->SetViewTarget(Pawn); PC->SetControlRotation(FRotator(-55,0,0));
    UDemoMeleeComponent* Melee=Pawn->FindComponentByClass<UDemoMeleeComponent>();
    return Check(Melee&&Melee->SetEquipped(true),TEXT("normal Hub melee equip allowed without gameplay bypass"));
}
bool UDemoOwnerShadowAudit::Sample(const FString& Label,bool ExpectedActive)
{
    DEMO_LOG_CALL(); ADemoCharacter* Pawn=Player.Get(); ADemoPlayerController* PC=Controller.Get();
    UDemoOwnerShadowComponent* Policy=Pawn?Pawn->FindComponentByClass<UDemoOwnerShadowComponent>():nullptr;
    UDemoMeleeComponent* Melee=Pawn?Pawn->FindComponentByClass<UDemoMeleeComponent>():nullptr;
    if(!Check(Pawn&&PC&&Policy&&Melee&&PC->GetPawn()==Pawn,TEXT("sample exact owned Avatar/policy: ")+Label)) return false;
    UPoseableMeshComponent* Body=Policy->GetBodyProxy(); UPrimitiveComponent* Weapon=Policy->GetWeaponProxy();
    USkeletalMeshComponent* FP=Melee->IsEquipped()?Melee->GetFirstPersonArmsMesh():Pawn->GetMesh1P();
    TSharedPtr<FJsonObject> Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("label"),Label); Value->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds()); Value->SetNumberField(TEXT("engine_frame"),static_cast<double>(GFrameCounter));
    Value->SetBoolField(TEXT("proxy_active"),Policy->IsProxyActive()); Value->SetBoolField(TEXT("melee_equipped"),Melee->IsEquipped()); Value->SetBoolField(TEXT("attacking"),Melee->IsAttacking()); Value->SetStringField(TEXT("view_target"),GetPathNameSafe(PC->GetViewTarget()));
    Value->SetStringField(TEXT("control_rotation"),PC->GetControlRotation().ToString()); Value->SetStringField(TEXT("final_view_location"),PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraLocation().ToString():TEXT("missing"));
    Value->SetObjectField(TEXT("tp_body"),ShadowAuditMesh(Pawn->GetMesh())); Value->SetObjectField(TEXT("fp_arms"),ShadowAuditMesh(FP)); Value->SetObjectField(TEXT("proxy_body"),ShadowAuditMesh(Body));
    if(Weapon) { Value->SetStringField(TEXT("weapon_proxy"),Weapon->GetPathName()); Value->SetStringField(TEXT("weapon_proxy_world"),Weapon->GetComponentTransform().ToString()); Value->SetBoolField(TEXT("weapon_hidden_shadow"),Weapon->bCastHiddenShadow); }
    TSharedPtr<FJsonObject> Angles=MakeShared<FJsonObject>(); double MaxUpperArmError=0.; int32 UpperArmSamples=0; // 实际最终world Q差；单手枪隐藏FP左臂时核对TP自然左臂，双手枪IK核对双腕与右上臂。
    const bool bHiddenFpLeft=!Melee->IsEquipped()&&FP&&FP->IsBoneHiddenByName(TEXT("upperarm_l"));
    if(Body&&FP) for(const FName Bone:{FName(TEXT("upperarm_l")),FName(TEXT("hand_l")),FName(TEXT("upperarm_r")),FName(TEXT("hand_r"))})
        if(Body->GetBoneIndex(Bone)!=INDEX_NONE&&FP->GetBoneIndex(Bone)!=INDEX_NONE)
        {
            const bool bLeftBone=Bone==TEXT("upperarm_l")||Bone==TEXT("hand_l");
            const USkeletalMeshComponent* ExpectedSource=(bHiddenFpLeft&&bLeftBone)?Pawn->GetMesh():FP;
            const double Error=FMath::RadiansToDegrees(Body->GetSocketQuaternion(Bone).AngularDistance(ExpectedSource->GetSocketQuaternion(Bone))); Angles->SetNumberField(Bone.ToString(),Error);
            const bool bCheckedArm=(Bone==TEXT("upperarm_r"))||(Melee->IsEquipped()&&Bone==TEXT("upperarm_l"))||(bHiddenFpLeft&&Bone==TEXT("upperarm_l"))||(!Melee->IsEquipped()&&!bHiddenFpLeft&&Bone==TEXT("hand_l"));
            if(bCheckedArm) { ++UpperArmSamples; MaxUpperArmError=FMath::Max(MaxUpperArmError,Error); }
        }
    const double NormalUpperLength=FVector::Distance(Pawn->GetMesh()->GetSocketLocation(TEXT("upperarm_r")),Pawn->GetMesh()->GetSocketLocation(TEXT("lowerarm_r"))); // 独立读取真实TP当前物理长度，不读代理内部求解缓存。
    const double ProxyUpperLength=Body?FVector::Distance(Body->GetSocketLocation(TEXT("upperarm_r")),Body->GetSocketLocation(TEXT("lowerarm_r"))):0.;
    Value->SetNumberField(TEXT("tp_upperarm_length_cm"),NormalUpperLength); Value->SetNumberField(TEXT("proxy_upperarm_length_cm"),ProxyUpperLength);
    Value->SetObjectField(TEXT("proxy_to_fp_world_rotation_error_deg"),Angles); Samples.Add(MakeShared<FJsonValueObject>(Value));
    if(!Check(Policy->IsProxyActive()==ExpectedActive,TEXT("actual owner/external-view activation agrees: ")+Label)) return false;
    if(ExpectedActive)
        return Check(Body&&Weapon&&Body->IsRegistered()&&Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&Body->GetComponentScale().Equals(Pawn->GetMesh()->GetComponentScale(),.001f)&&Body->CastShadow&&Body->bCastHiddenShadow&&Body->bHiddenInGame&&Body->bHiddenInSceneCapture&&!Body->bVisibleInRayTracing&&!Pawn->GetMesh()->bCastHiddenShadow,TEXT("normal-scale isolated shadow proxy replaces original hidden body shadow: ")+Label)
            &&Check(UpperArmSamples==2&&MaxUpperArmError<=.1&&NormalUpperLength>1.&&FMath::Abs(ProxyUpperLength-NormalUpperLength)<.05,FString::Printf(TEXT("actual normal arm length and FP world upperarm orientation label=%s angle_deg=%.6f tp_cm=%.6f proxy_cm=%.6f"),*Label,MaxUpperArmError,NormalUpperLength,ProxyUpperLength));
    return Check(Pawn->GetMesh()->bCastHiddenShadow,TEXT("external view restores original TP hidden-shadow policy: ")+Label);
}
bool UDemoOwnerShadowAudit::BeginCapture(const FString& Name,int32 NextStep)
{
    DEMO_LOG_CALL(); const FIntPoint Size=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue;
    if(!CheckBudget(Name)||!Check(ScreenshotCount<4&&PendingScreenshot.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested()&&Size==FIntPoint(1280,720),TEXT("one fixed1280x720 screenshot, max4"))) return false;
    PendingScreenshot=Directory/(Name+TEXT(".png")); ScreenshotFrame=TickCount; ScreenshotAt=FPlatformTime::Seconds(); CaptureNextStep=NextStep; ++ScreenshotCount;
    FScreenshotRequest::RequestScreenshot(PendingScreenshot,false,false); return true;
}
void UDemoOwnerShadowAudit::AdvanceCapture()
{
    DEMO_LOG_TICK();
    if(TickCount>ScreenshotFrame&&IFileManager::Get().FileSize(*PendingScreenshot)>1000) { Check(true,TEXT("PNG written after actual render frame: ")+FPaths::GetCleanFilename(PendingScreenshot)); PendingScreenshot.Reset(); Step=CaptureNextStep; NextWorldAt=GetWorld()->GetTimeSeconds()+.1; }
    else if(FPlatformTime::Seconds()-ScreenshotAt>3.) Check(false,TEXT("PNG did not write in3 real seconds"));
}
void UDemoOwnerShadowAudit::Finish(bool Success)
{
    DEMO_LOG_CALL(); if(bFinished) return; bFinished=true;
    if(PostActorHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(PostActorHandle); PostActorHandle.Reset();
    TSharedPtr<FJsonObject> Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("id"),TEXT("MELEE-SHADOW-20261115/OWNER-PROXY-RUNTIME")); Report->SetStringField(TEXT("status"),Success?TEXT("PASS_LISTED_RUNTIME_CONTRACT_ONLY"):TEXT("FAIL_RUNTIME_CONTRACT"));
    Report->SetStringField(TEXT("scope"),TEXT("Normal temporary Editor slot/StartRun/Hub equipment; final body/FP/proxy bone snapshots; owner/external-view fallback, pistol route and melee re-equip;4 bounded RHI PNGs. Flags are not visual acceptance."));
    Report->SetNumberField(TEXT("elapsed_world_seconds"),GetWorld()->GetTimeSeconds()-StartedWorldAt); Report->SetNumberField(TEXT("elapsed_real_seconds"),FPlatformTime::Seconds()-StartedAt); Report->SetNumberField(TEXT("requested_pngs"),ScreenshotCount); Report->SetArrayField(TEXT("samples"),Samples);
    TArray<TSharedPtr<FJsonValue>> Values; for(const FString& Item:Checks) Values.Add(MakeShared<FJsonValueString>(Item)); Report->SetArrayField(TEXT("checks"),Values);
    Report->SetArrayField(TEXT("not_run"),{MakeShared<FJsonValueString>(TEXT("art/naturalness/shadow silhouette visual acceptance")),MakeShared<FJsonValueString>(TEXT("actual SceneCapture/HWRT/reflection/VSM multi-view isolation")),MakeShared<FJsonValueString>(TEXT("rifle/shotgun/sniper, scope, death/respawn/late-avatar/multi-LOD")),MakeShared<FJsonValueString>(TEXT("Listen2-4/EOS/Cook/Shipping/performance"))});
    FString Text; const bool Written=!Directory.IsEmpty()&&FJsonSerializer::Serialize(Report.ToSharedRef(),TJsonWriterFactory<>::Create(&Text))&&FFileHelper::SaveStringToFile(Text,*(Directory/TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogFPSDemo,Display,TEXT("OWNER_SHADOW_AUDIT_%s directory=%s pngs=%d; listed contract only, visual review required"),Success&&Written?TEXT("SUCCESS"):TEXT("FAILED"),*Directory,ScreenshotCount);
#if WITH_EDITOR
    FPlatformMisc::RequestExitWithStatus(false,Success&&Written?0:1);
#endif
}
void UDemoOwnerShadowAudit::OnPostActorTick(UWorld* InWorld,ELevelTick TickType,float DeltaTime)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if(!bActive||bFinished||InWorld!=GetWorld()||TickType==LEVELTICK_TimeOnly) return; ++TickCount;
    if(!bFailed&&FPlatformTime::Seconds()-StartedAt>45.) Check(false,TEXT("bounded45 real-second total timeout"));
    if(bFailed) { Finish(false); return; }
    if(!PendingScreenshot.IsEmpty()) { AdvanceCapture(); return; }
    if(GetWorld()->GetTimeSeconds()<NextWorldAt) return;
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0)); ADemoCharacter* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); UDemoRunSaves* Saves=GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr;
    if(!PC||!Pawn||!Mode||!Saves) return; Player=Pawn; Controller=PC;
    UDemoMeleeComponent* Melee=Pawn->FindComponentByClass<UDemoMeleeComponent>(); if(!Check(Melee!=nullptr,TEXT("actual melee component exists"))) return;
    const double Now=GetWorld()->GetTimeSeconds(); // 真实游戏时间，未改变源Idle/攻击/恢复时钟。
    switch(Step)
    {
    case 0:
    {
        UDemoMenuFlowComponent* Menu=PC->GetMenuFlow(); if(!Menu) return;
        Menu->UpdateRealtime(0.f); if(Menu->GetMenuPage()==EDemoMenuPage::FirstRunGuide) Menu->DispatchUIAction(EDemoUIAction::OverlayBack);
        if(!Check(!Menu->HasBlockingOverlay()&&Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("normal local temporary slot and StartRun"))) return;
        Saves->ConsumePending(); PC->OnRunReady(); Step=1; NextWorldAt=Now+.4; return;
    }
    case 1: if(PrepareStage()) { Step=2; NextWorldAt=Now+.6; } return;
    case 2: if(Sample(TEXT("melee_idle_down"),true)) BeginCapture(TEXT("01_FP_Melee_Idle_Down"),3); return;
    case 3: PC->SetControlRotation(FRotator(55,0,0)); Step=4; NextWorldAt=Now+.4; return;
    case 4:
        if(!Sample(TEXT("melee_idle_up"),true)) return; PC->SetControlRotation(FRotator(-40,0,0)); Melee->RequestAttack(0);
        if(!Check(Melee->IsAttacking()&&Melee->GetDefinition()&&Melee->GetDefinition()->Attacks.IsValidIndex(0),TEXT("real Hub GAS melee attack accepted"))) return;
        Step=5; NextWorldAt=Now+Melee->GetDefinition()->Attacks[0].Duration*.35; return;
    case 5: if(Sample(TEXT("melee_attack"),true)) BeginCapture(TEXT("02_FP_Melee_Attack_Down"),6); return;
    case 6: PC->SetViewTarget(Observer.Get()); Step=7; NextWorldAt=Now+.4; return;
    case 7: if(Sample(TEXT("external_tp_fallback"),false)) BeginCapture(TEXT("03_External_TP_Fallback"),8); return;
    case 8:
        PC->SetViewTarget(Pawn); PC->SetControlRotation(FRotator(-35,0,0));
        if(!Check(Pawn->GetWeaponComponent()&&Pawn->GetWeaponComponent()->EquipSlot(2),TEXT("normal switch to default secondary pistol"))) return;
        Step=9; NextWorldAt=Now+.6; return;
    case 9: if(Sample(TEXT("pistol_owner"),true)) BeginCapture(TEXT("04_FP_Pistol_Down"),10); return;
    case 10: if(!Check(Melee->SetEquipped(true),TEXT("normal melee re-equip after pistol"))) return; PC->SetControlRotation(FRotator::ZeroRotator); Step=11; NextWorldAt=Now+.4; return;
    case 11: if(!Sample(TEXT("melee_re_equipped"),true)) return; PC->SetViewTarget(Observer.Get()); Step=12; NextWorldAt=Now+.3; return;
    case 12: if(Sample(TEXT("final_external_fallback"),false)&&Check(ScreenshotCount==4,TEXT("exactly4 screenshots requested"))&&CheckBudget(TEXT("finish"))) Finish(true); return;
    default: Check(false,TEXT("unexpected finite audit state")); return;
    }
#endif
}
