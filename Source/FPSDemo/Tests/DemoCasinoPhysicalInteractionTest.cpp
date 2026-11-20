// 职责：CP-01一台老虎机真实交互/原事务/本地耐久/本人表现的Editor回归。使用-DemoCasinoPhysicalTest -game Standalone；不运行第二套规则，不修改R03资产或相机。
#include "Tests/DemoCasinoPhysicalInteractionTest.h"
#include "Characters/DemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Debug/DemoLog.h"
#include "Economy/DemoShopComponent.h"
#include "Economy/Gambling/DemoCasinoSettings.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoLocalViewComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h" // 正式分支只读实际Profile的part轴/rest/stop，与本人组件实测对照。
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h" // 夹具只读实际七设备绑定，前置声明不足以访问Devices。
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerState.h"
#include "Player/Coop/DemoCoopProtocol.h" // 只读记录公共唯一协议源，不硬编码当前数字或维护第二份常量。
#include "Save/DemoRunSave.h"
#include "Save/Codecs/DemoCheckpointJsonCodec.h" // 只作本机已落盘快照纯JSON往返，不创建Editor云子系统。
#include "Serialization/JsonSerializer.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UnrealClient.h" // 真实RHI截图请求，不使用模型示意图替代游戏视图。

ADemoCasinoPhysicalInteractionTest::ADemoCasinoPhysicalInteractionTest()
{
    DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=WITH_EDITOR; PrimaryActorTick.TickInterval=.05f;
}
void ADemoCasinoPhysicalInteractionTest::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
#if WITH_EDITOR
    if (!FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoPhysicalTest")) || !FParse::Param(FCommandLine::Get(),TEXT("game")) || GetWorld()->WorldType!=EWorldType::Game || GetNetMode()!=NM_Standalone)
    { UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_PHYSICAL_TEST_REJECT isolated Editor -game Standalone required")); SetActorTickEnabled(false); Destroy(); return; }
    StartedRealAt=FPlatformTime::Seconds(); NextAt=GetWorld()->GetTimeSeconds()+1.f;
    bModelAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoModelTest")); // 必须同时经过本夹具原Editor/Game/Standalone门。
    EvidenceDirectory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HubShops20261114/PhysicalSlots"),FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    if (!IFileManager::Get().MakeDirectory(*EvidenceDirectory,true)) Check(false,TEXT("create unique local test evidence directory"));
    UE_LOG(LogFPSDemo,Display,TEXT("CASINO_PHYSICAL_TEST_BEGIN scope=Editor_Standalone_one_slots_device directory=%s"),*EvidenceDirectory);
#endif
}
#if WITH_EDITOR
bool ADemoCasinoPhysicalInteractionTest::Check(bool Condition,const FString& Message)
{
    DEMO_LOG_CALL();
    const FString Entry=FString::Printf(TEXT("%s step=%d %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Step,*Message);
    CheckRecords.Add(Entry); UE_LOG(LogFPSDemo,Display,TEXT("CASINO_PHYSICAL_TEST %s"),*Entry);
    if (!Condition) bFailed=true; // Tick下一次处理退出，保存委托必须先返回并让生产事务回滚。
    return Condition;
}
bool ADemoCasinoPhysicalInteractionTest::AimAtControl(FName ControlId)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 仅显式单人夹具，生产组件不使用Player0。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    FVector Point; // 当前真实稳定查询中心；禁止用手写Transform冒充击中。
    if (!Check(Player && Controls && Controls->GetControlPoint(ControlId,Point),TEXT("target control exists in actual registered query component"))) return false;
    const FVector Eye=Player->GetSharedViewLocation(); // 与正式本机/权威商店射线同一当前眼位，不沿历史Pawn BaseEyeHeight构造测试瞄准。
    PC->SetControlRotation((Point-Eye).Rotation()); AimedControl=ControlId; ControlAimAt=GetWorld()->GetTimeSeconds(); OpeningRayValidSince=-1.;
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_PHYSICAL_TEST_AIM control=%s point=%s"),*ControlId.ToString(),*Point.ToString());
    NextAt=ControlAimAt+.25f; return true; // 给正常控制/眼位及Shop焦点稳定门实际帧时间，不推进另一套时钟。
}
bool ADemoCasinoPhysicalInteractionTest::InteractWithAimedControl()
{
    DEMO_LOG_CALL();
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    FString Error; // 服务端真实Ray原因可展示，不记录私人Token。
    if (!Player || !Controls || !Controls->ValidateControlHit(Player,AimedControl,Error))
    {
        OpeningRayValidSince=-1.; // 最后一次视点迭代后必须重新积累连续真实命中，不能继承最初Aim的等待时间。
        if (GetWorld()->GetTimeSeconds()-ControlAimAt>2.f) Check(false,FString::Printf(TEXT("actual first query hit did not settle for %s: %s"),*AimedControl.ToString(),*Error));
        else if (Player && Controls)
        {
            FVector Point; // Query目标仍同一真实固定组件；低头后的正常前探会改变眼位，测试须像玩家继续转头校正。
            if (Controls->GetControlPoint(AimedControl,Point))
            {
                const FVector Eye=Player->GetSharedViewLocation(); // 低头前探改变正常共享眼位，重新瞄准真实固定中心，不移动Query或相机。
                PC->SetControlRotation((Point-Eye).Rotation()); ++ControlAimCorrections; // 不写SharedViewFrame、不改相机/机台、不放宽首次命中断言；2秒超时不重置。
            }
        }
        NextAt=GetWorld()->GetTimeSeconds()+.1f; return false;
    }
    if (bModelAudit && (Step==2 || Step==20))
    {
        const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
        const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr;
        if (!Check(Definition!=nullptr,TEXT("actual opening Focus configuration remains available"))) return false;
        const double Now=GetWorld()->GetTimeSeconds(); // 正常World时钟；首次有效Query之后才等原Focus候选稳定，不推进另一套时钟。
        if (OpeningRayValidSince<0.) OpeningRayValidSince=Now;
        const double RequiredStable=Definition->FocusStableSeconds+2./Definition->ScanHz; // 两个原扫描间隔覆盖发现候选/提交Focus，不降低原门限或直接设Focus。
        if (Now-OpeningRayValidSince<RequiredStable) { NextAt=Now+.05; return false; }
        FVector PawnEye,ViewEye; FRotator PawnAim,ViewAim; // 同次真实权威眼位与本机最终视点，诊断正式模型初次Focus，不修改相机或门限。
        Player->GetActorEyesViewPoint(PawnEye,PawnAim); PC->GetPlayerViewPoint(ViewEye,ViewAim);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CasinoModelFocusAudit),false,Player); FHitResult PawnFirst,ViewFirst,Cover; // 保留真实墙/队友/相邻设备，仅忽略本人。
        const float Distance=Definition?Definition->InteractionDistanceCm:0.f; // 原配置距离，0仅表示前置缺失，不放宽失败条件。
        GetWorld()->LineTraceSingleByChannel(PawnFirst,PawnEye,PawnEye+PawnAim.Vector()*Distance,ECC_Visibility,Query);
        GetWorld()->LineTraceSingleByChannel(ViewFirst,ViewEye,ViewEye+ViewAim.Vector()*Distance,ECC_Visibility,Query);
        Query.AddIgnoredActor(Device.Get()); GetWorld()->LineTraceSingleByChannel(Cover,ViewEye,Device->GetFocusPoint(),ECC_Visibility,Query);
        FString UseError; FName UseCode; // 原权威ValidateUse只读校验，不创建租约/下注，不把中文作为逻辑分支。
        const bool bValidUse=Registry && Registry->ValidateUse(Player,Device.Get(),Registry->GetWorldEpoch(),UseError,&UseCode);
        const FVector FocusOffset=Device->GetFocusPoint()-ViewEye;
        const double FocusAngle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ViewAim.Vector(),FocusOffset.GetSafeNormal()),-1.,1.)));
        UE_LOG(LogFPSDemo,Display,TEXT("CASINO_MODEL_FOCUS_AUDIT step=%d skill=%d registered=%d pawnEye=%s pawnAim=%s viewEye=%s viewAim=%s pawnFirst=%s/%s viewFirst=%s/%s cover=%s/%s focusDistance=%.3f focusAngle=%.3f useValid=%d useCode=%s"),Step,Player->CanUsePlayerSkills(),PC->IsCoopRegistered(),*PawnEye.ToString(),*PawnAim.ToString(),*ViewEye.ToString(),*ViewAim.ToString(),*GetNameSafe(PawnFirst.GetActor()),*GetNameSafe(PawnFirst.GetComponent()),*GetNameSafe(ViewFirst.GetActor()),*GetNameSafe(ViewFirst.GetComponent()),*GetNameSafe(Cover.GetActor()),*GetNameSafe(Cover.GetComponent()),FocusOffset.Size(),FocusAngle,bValidUse,*UseCode.ToString());
        CaptureModelScreenshot(Step==2?TEXT("BeforeInitialOpen"):TEXT("BeforeReentry")); // 正常RHI当前视点，失败也保留，不用作者展示替代。
    }
    Player->TriggerWorldInteraction(); // 唯一真实生产交互路由；不直接调用Server RPC/SubmitCasino/纯规则。
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_PHYSICAL_TEST_INTERACT control=%s via=Character.TriggerWorldInteraction"),*AimedControl.ToString());
    NextAt=GetWorld()->GetTimeSeconds()+.12f; return true;
}
bool ADemoCasinoPhysicalInteractionTest::CheckStableQueries()
{
    DEMO_LOG_TICK();
    const UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    if (!Controls || QueryIds.Num()!=QueryBefore.Num()) return Check(false,TEXT("stable query component/version still available"));
    ++QuerySnapshotChecks;
    for (int32 Index=0;Index<QueryIds.Num();++Index)
    {
        FVector Current; // 每点实际世界cm，测量稳定件，绝不读取表现求解器目标。
        if (!Controls->GetControlPoint(QueryIds[Index],Current) || Current.ContainsNaN()) return Check(false,TEXT("actual query measurement is finite and available"));
        MaximumQueryDisplacementCm=FMath::Max(MaximumQueryDisplacementCm,FVector::Distance(Current,QueryBefore[Index]));
        if (!Current.Equals(QueryBefore[Index],.001))
            return Check(false,FString::Printf(TEXT("private animation moved or removed server query %s"),*QueryIds[Index].ToString()));
    }
    return true;
}
bool ADemoCasinoPhysicalInteractionTest::CheckNoFullscreenPage()
{
    DEMO_LOG_CALL();
    const ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    const UDemoMenuFlowComponent* Flow=PC?PC->GetMenuFlow():nullptr;
    const UDemoUIManager* UI=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr;
    return Check(PC && Flow && !PC->IsUpgradeMenuOpen() && !Flow->IsGambleMenuOpen() && !Flow->IsCasinoBetInputOpen() && (!UI || UI->GetActiveScreen()==nullptr),TEXT("physical operation has no Upgrade/CommonUI/casino fullscreen page"));
}
bool ADemoCasinoPhysicalInteractionTest::CheckDiskCheckpoint(int32 ExpectedCoins,int32 ExpectedSerial)
{
    DEMO_LOG_CALL();
    const UDemoRunSaves* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>();
    const UDemoRunSave* Disk=Saves && Saves->GetActiveSlot()>=0?Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(Saves->SlotName(Saves->GetActiveSlot()),0)):nullptr; // 从磁盘重新反序列化，不读GetSlot/ExportCloud缓存。
    if (!Check(Disk && Disk->Validate() && Disk->Coins==ExpectedCoins && Disk->Gambling.Serial==ExpectedSerial,TEXT("actual local slot file coins and gambling serial match committed authority"))) return false;
    if (!bModelAudit) return true;
    FString Error; UDemoRunSave* Decoded=NewObject<UDemoRunSave>(); // 纯值解析新对象，不访问HTTP或用缓存冒充磁盘。
    const TSharedPtr<FJsonObject> Json=DemoCheckpointJsonCodec::Encode(*Disk,Error);
    return Check(Disk->Gambling.LastSlotsReceipt.Mode==1 && Disk->Gambling.LastSlotsReceipt.Serial==CommittedSerial && Disk->Gambling.LastSlotsReceipt.Symbols==CommittedSymbols
        && Json.IsValid() && DemoCheckpointJsonCodec::Decode(Json,*Decoded,Error) && Decoded->Gambling.LastSlotsReceipt.Symbols==CommittedSymbols,TEXT("actual disk typed slot fact and explicit V10 JSON roundtrip preserve SavedSymbols without parsing text"));
}
bool ADemoCasinoPhysicalInteractionTest::RejectSave()
{
    DEMO_LOG_CALL(); const TGuardValue<bool> CallbackGuard(bInSaveCallback,true); ++SaveFailureCalls;
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    const UDemoPlayerProgressComponent* Progress=Player && Player->GetPlayerState<ADemoPlayerState>()?Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent():nullptr;
    UDemoCasinoDeviceInputComponent* Input=PC?PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>():nullptr;
    const UDemoCasinoLocalViewComponent* View=PC?PC->FindComponentByClass<UDemoCasinoLocalViewComponent>():nullptr;
    if (Check(Progress && Progress->GetSnapshot().Gambling.Serial==CommittedSerial+1,TEXT("failure seam actually sees a new candidate requiring save")))
    {
        if (bModelAudit) Check(Progress->GetSnapshot().Gambling.LastSlotsReceipt.Serial==CommittedSerial+1 && PC->GetMenuSnapshot().Progress.Gambling.LastSlotsReceipt.Serial==CommittedSerial,TEXT("failed-save seam stages typed candidate privately while Owner retains prior durable receipt"));
        Check(PC->GetMenuSnapshot().Progress.Gambling.Serial==CommittedSerial && View && View->GetLastResult().Serial<=CommittedSerial,TEXT("candidate serial/result not published before failed save"));
        if (Check(Input && Input->IsWaiting(),TEXT("true local Waiting is set before synchronous save callback")))
        {
            const int32 RequestBefore=Input->GetLastSubmittedRequestId(); // 真实私有nonce，重入不得产生第二笔意图。
            Player->TriggerWorldInteraction(); // 同瞄准方向、同生产E路由；只在实际Waiting保存栈做一次明确重入探针。
            bWaitingReentryObserved=Input->GetLastSubmittedRequestId()==RequestBefore && SaveFailureCalls==1;
            Check(bWaitingReentryObserved,TEXT("interaction during actual save Waiting emits no second request"));
        }
    }
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_PHYSICAL_TEST_SAVE_REJECT calls=%d"),SaveFailureCalls);
    return false; // 原Casino服务必须自行回滚，不在测试里恢复金币/Serial或构造失败回执。
}
void ADemoCasinoPhysicalInteractionTest::RestoreSaveBinding()
{
    DEMO_LOG_CALL();
    if (!bSaveOverrideInstalled || bInSaveCallback) return;
    if (AFPSDemoGameMode* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AFPSDemoGameMode>():nullptr)
        if (UDemoShopComponent* Shop=Mode->GetShopSystem()) Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(Mode,&AFPSDemoGameMode::SaveCheckpoint));
    bSaveOverrideInstalled=false;
}
void ADemoCasinoPhysicalInteractionTest::CaptureModelScreenshot(const FString& Name)
{
    DEMO_LOG_CALL();
    if (!bModelAudit) return;
    if (!Check(!FScreenshotRequest::IsScreenshotRequested(),TEXT("formal model screenshot is not overwritten by another pending capture"))) return;
    const FString File=EvidenceDirectory/(Name+TEXT(".png")); // 仅本次独占目录；真实游戏视点仍由正常PC/Camera更新。
    FScreenshotRequest::RequestScreenshot(File,false,false); ModelScreenshots.Add(File);
}
bool ADemoCasinoPhysicalInteractionTest::CheckModelPose(bool bSettled)
{
    DEMO_LOG_CALL();
    if (!bModelAudit) return true;
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 显式Standalone夹具唯一玩家，不进入生产Player0查询。
    UDemoCasinoLocalViewComponent* View=PC?PC->FindComponentByClass<UDemoCasinoLocalViewComponent>():nullptr;
    UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    const UDemoCasinoDeviceProfile* Profile=Controls?Controls->GetModelProfile():nullptr; // 只读实际启用，不拿配置路径当已渲染事实。
    if (!Check(Profile && View && Profile->Parts.Num()==21 && Profile->BindingRevision>=3,TEXT("actual device consumes a full formal S01 profile instead of prototype Cube"))) return false;
    ModelProfilePath=Profile->GetPathName();
    const FDemoCasinoResult& Result=View->GetLastResult();
    if (!Check(Result.Mode==1 && Result.Symbols.Num()==3,TEXT("mechanical audit uses only actual committed three-symbol receipt"))) return false;
    int32 Reels=0; bool bObservedMotion=false; // 所有三轴都必须存在，运动中只需实际观察至少一个离开中性。
    for (const FDemoCasinoModelPart& Part:Profile->Parts)
    {
        if (Part.Kind!=EDemoCasinoModelPartKind::Reel && Part.Kind!=EDemoCasinoModelPartKind::Lever) continue;
        FTransform Actual; // 从真实本人NoCollision StaticMeshComponent读取，而非LocalView计算目标。
        if (!Check(View->ReadModelPartPoseForAudit(Part.PartId,Actual),TEXT("actual private model mechanical component exists"))) return false;
        FTransform Expected=Part.RestTransform;
        if (Part.Kind==EDemoCasinoModelPartKind::Reel)
        {
            ++Reels;
            if (!Check(Result.Symbols.IsValidIndex(Part.ReelIndex) && Part.SymbolStopAngles.IsValidIndex(Result.Symbols[Part.ReelIndex]),TEXT("saved symbol maps to this measured reel stop"))) return false;
            Expected.SetRotation(Part.RestTransform.GetRotation()*FQuat(Part.MotionAxis,FMath::DegreesToRadians(Part.SymbolStopAngles[Result.Symbols[Part.ReelIndex]])));
        }
        const FTransform ExpectedWorld=Expected*Device->GetActorTransform(); // 预期源stop和设备框，不读取实际姿态反推目标。
        const FTransform NeutralWorld=Part.RestTransform*Device->GetActorTransform();
        bObservedMotion|=Actual.GetRotation().AngularDistance(NeutralWorld.GetRotation())>FMath::DegreesToRadians(.5f);
        if (bSettled && !Check(Actual.GetLocation().Equals(ExpectedWorld.GetLocation(),.01f) && Actual.GetRotation().AngularDistance(ExpectedWorld.GetRotation())<.0001f && Actual.GetScale3D().Equals(ExpectedWorld.GetScale3D(),.0001f),TEXT("actual mechanical component settles at SavedSymbols stop or lever rest without accumulated drift"))) return false;
    }
    if (!Check(Reels==3,TEXT("all three actual reel meshes are observed")) || (!bSettled && !Check(bObservedMotion,TEXT("actual committed model reveal moves a real reel/lever component")))) return false;
    ++ModelPoseChecks;
    return CheckStableQueries(); // 运动/静止都复核全部十五根查询框，不能让本人件带动权威锚点。
}
bool ADemoCasinoPhysicalInteractionTest::WriteReport(bool bPassed)
{
    DEMO_LOG_CALL();
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 固定小摘要，不保存玩家Token/完整账本或逐帧表演缓冲。
    Report->SetStringField(TEXT("id"),TEXT("HUB-SHOPS-20261114/CP-01/SLOTS-PHYSICAL"));
    Report->SetStringField(TEXT("status"),bPassed?TEXT("PASSED_LISTED_EDITOR_STANDALONE_SCOPE"):TEXT("FAILED_ACTUAL_FIXTURE"));
    Report->SetStringField(TEXT("scope"),TEXT("One Mode1 device, actual Query/Character interaction route, real local save and Owner feedback; no hardware key injection, other six devices, multiplayer, cloud, Cook or art acceptance."));
    Report->SetBoolField(TEXT("formal_model_audit"),bModelAudit); Report->SetStringField(TEXT("actual_model_profile"),ModelProfilePath);
    Report->SetNumberField(TEXT("actual_model_pose_checks"),ModelPoseChecks); Report->SetNumberField(TEXT("actual_committed_rounds"),ModelCommittedRounds);
    TArray<TSharedPtr<FJsonValue>> Captures; // 只列真实已写出的PNG，未完成请求不能冒充图像证据。
    for (const FString& File:ModelScreenshots) if (IFileManager::Get().FileExists(*File)) Captures.Add(MakeShared<FJsonValueString>(File));
    Report->SetArrayField(TEXT("actual_model_screenshots"),Captures);
    Report->SetNumberField(TEXT("step"),Step); Report->SetNumberField(TEXT("stake"),10);
    Report->SetNumberField(TEXT("coins_before"),BeforeCoins); Report->SetNumberField(TEXT("coins_committed"),CommittedCoins); Report->SetNumberField(TEXT("paid"),CommittedPaid);
    Report->SetNumberField(TEXT("gamble_serial_before"),BeforeSerial); Report->SetNumberField(TEXT("gamble_serial_committed"),CommittedSerial);
    Report->SetNumberField(TEXT("component_request_before"),BeforeRequestId); Report->SetNumberField(TEXT("failure_request_before"),RequestIdBeforeFailure);
    Report->SetNumberField(TEXT("save_failure_callback_calls"),SaveFailureCalls); Report->SetBoolField(TEXT("actual_waiting_reentry_probe"),bWaitingReentryObserved);
    Report->SetBoolField(TEXT("server_physical_lease_blocks_player_skills"),bPhysicalSkillGuardObserved); Report->SetBoolField(TEXT("legacy_casino_business_route_rejected"),bLegacyCasinoRouteRejected);
    Report->SetStringField(TEXT("coop_protocol_source_value"),DemoCoopProtocol::Version); // 当前构建实际引用值；本夹具不假称该版本多人已运行。
    Report->SetNumberField(TEXT("stable_query_controls"),QueryIds.Num()); Report->SetNumberField(TEXT("actual_query_snapshot_checks"),QuerySnapshotChecks); Report->SetNumberField(TEXT("maximum_query_displacement_cm"),MaximumQueryDisplacementCm);
    Report->SetNumberField(TEXT("actual_control_aim_corrections"),ControlAimCorrections);
    Report->SetStringField(TEXT("rules_fingerprint"),FString::Printf(TEXT("%08x"),GetDefault<UDemoCasinoSettings>()->Rules.Fingerprint()));
    TArray<TSharedPtr<FJsonValue>> Symbols; for (int32 Symbol:CommittedSymbols) Symbols.Add(MakeShared<FJsonValueNumber>(Symbol)); Report->SetArrayField(TEXT("committed_symbols"),Symbols);
    TArray<TSharedPtr<FJsonValue>> Checks; for (const FString& Entry:CheckRecords) Checks.Add(MakeShared<FJsonValueString>(Entry)); Report->SetArrayField(TEXT("checks"),Checks);
    FString Text; if (!FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text))) return false;
    return !EvidenceDirectory.IsEmpty() && FFileHelper::SaveStringToFile(Text,*FPaths::Combine(EvidenceDirectory,TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
#endif
void ADemoCasinoPhysicalInteractionTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    if (bFinished) return;
    if (!bFailed && (FPlatformTime::Seconds()-StartedRealAt>120. || GetWorld()->GetTimeSeconds()>60.f)) Check(false,TEXT("bounded explicit physical interaction fixture"));
    if (bFailed)
    { RestoreSaveBinding(); WriteReport(false); bFinished=true; UE_LOG(LogFPSDemo,Display,TEXT("CASINO_PHYSICAL_TEST_FAILED directory=%s"),*EvidenceDirectory); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const double Now=GetWorld()->GetTimeSeconds();
    if (((Step>=3 && Step<=14) || (bModelAudit && Step>=20 && Step<=26)) && !CheckStableQueries()) return; // 两局/重进的步骤等待和私有动画期间均逐Tick测量，不能等回位后才比较。
    if (Now<NextAt) return;
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本次明确单人Standalone；每步重取Avatar，非生产身份路径。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    UDemoRunSaves* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>();
    ADemoPlayerState* PS=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr;
    UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
    if (!PC || !Player || !Mode || !State || !Saves || !Progress) { NextAt=Now+.1f; return; }
    UDemoMenuFlowComponent* Flow=PC->GetMenuFlow();
    UDemoCasinoDeviceInputComponent* Input=PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>();
    UDemoCasinoLocalViewComponent* View=PC->FindComponentByClass<UDemoCasinoLocalViewComponent>();
    UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    switch (Step)
    {
    case 0:
    {
        if (!Check(Flow && Input && View,TEXT("actual PC has physical input and private local-view components"))) return;
        Flow->UpdateRealtime(0.f);
        if (Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) Flow->DispatchUIAction(EDemoUIAction::OverlayBack); // 正式关闭隔离设备的新手提示，不手写MenuPage。
        if (!Check(!Flow->HasBlockingOverlay(),TEXT("initial guide is closed through normal overlay route"))) return;
        if (UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Profile->SetDebugTestFlag(0,false); // 真正扣款，不把Editor免扣当经济通过。
        if (!Check(Saves->CreateSlot(0) && Mode->StartRun(),TEXT("create this GI temporary slot and start actual hub"))) return;
        Saves->ConsumePending(); PC->OnRunReady();
        FDemoPlayerProgressSnapshot Budget=Progress->GetSnapshot(); Budget.Coins=6000; // 仅隔离初始预算，之后不改金币/Serial/开奖结果。
        if (!Check(Progress->Commit(Budget) && Mode->SaveCheckpoint(),TEXT("persist isolated initial budget through real checkpoint service"))) return;
        Step=1; NextAt=Now+.5f; break;
    }
    case 1:
    {
        if (!Registry || State->Phase!=EDemoPhase::Hub || !Registry->GetDefinition()) { NextAt=Now+.1f; return; }
        const TArray<FDemoHubCasinoBinding>& Bindings=Registry->GetDefinition()->Devices; // 只读R03实际设备目录，不写DA/Map或硬编码旧布局坐标。
        for (const FDemoHubCasinoBinding& Binding:Bindings) if (Binding.Mode==1) { Device=Registry->FindDevice(Binding.DeviceId); break; }
        UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
        if (!Controls || !Controls->IsReady()) { NextAt=Now+.1f; return; }
        if (bModelAudit && !Check(Controls->GetModelProfile()!=nullptr,TEXT("formal model flag requires actual imported Profile, never silently accepts prototype"))) return;
        for (int32 Digit=0;Digit<10;++Digit) QueryIds.Add(FName(*FString::Printf(TEXT("amount.digit.%d"),Digit)));
        QueryIds.Append({FName(TEXT("amount.clear")),FName(TEXT("amount.backspace")),FName(TEXT("amount.commit")),FName(TEXT("slots.pull")),FName(TEXT("device.exit"))});
        for (FName Id:QueryIds) { FVector Point; if (!Check(Controls->GetControlPoint(Id,Point),TEXT("all fifteen actual query controls are present"))) return; QueryBefore.Add(Point); }
        const float HalfHeight=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector Stand=Device->GetActorTransform().TransformPosition(FVector(180,0,HalfHeight)); // 约170cm前方并留低键俯视余量；站位沿实际Device框，不改机台或相机。
        if (!Check(Player->TeleportTo(Stand,Player->GetActorRotation()),TEXT("fixture reaches actual device front without body penetration"))) return;
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->ConsumeMovementInputVector();
        if (!AimAtControl(TEXT("slots.pull"))) return;
        Step=2; NextAt=Now+.5f; break; // 超过真实FocusStableSeconds，首次仍走原Focus→ServerOpenDevice链。
    }
    case 2:
        if (InteractWithAimedControl()) { Step=3; NextAt=Now+.25f; }
        break;
    case 3:
    {
        if (!Input || !Input->IsOperating()) { if (Now-ControlAimAt>3.f) Check(false,TEXT("real focused interaction opens independent physical operation")); else NextAt=Now+.1f; return; }
        if (!Check(Input->GetCurrentDevice()==Device.Get(),TEXT("operation is bound to the actual registered slots device")) || !CheckNoFullscreenPage()) return;
        bPhysicalSkillGuardObserved=Mode->GetShopSystem()->IsPhysicalCasinoOperator(Player) && !Player->CanUsePlayerSkills();
        if (!Check(bPhysicalSkillGuardObserved,TEXT("server physical lease exists and skill gate rejects operation without a fullscreen menu"))) return;
        const int32 LegacyCoins=Progress->GetSnapshot().Coins,LegacySerial=Progress->GetSnapshot().Gambling.Serial; // 旧业务路由负例前真实权威值，不能复用之后新拉杆基线。
        const uint64 LegacySaveSerial=Saves->GetChangeSerial();
        const FDemoCasinoResult PreviousView=View->GetLastResult(); // 仅复制已公开结果，不读私有服务器候选。
        const FDemoPlayerMenuSnapshot Approved=PC->GetMenuSnapshot(); // 本人服务器批准Token仅用于正式负例输入，绝不写报告/日志。
        FDemoGameplayCommandRequest Legacy; // 明确测试旧业务路由，不注入新组件RPC或声称覆盖旧网络传输。
        Legacy.Command=EDemoGameplayCommand::Casino; Legacy.Casino.Mode=1; Legacy.Casino.Operation=0; Legacy.Casino.Stake=10; Legacy.Casino.RulesKey=Approved.CasinoRules.Fingerprint(); Legacy.GambleSerial=LegacySerial;
        Legacy.CasinoDeviceId=Approved.CasinoDeviceId; Legacy.CasinoDeviceToken=Approved.CasinoDeviceToken; Legacy.HubWorldEpoch=Approved.HubWorldEpoch;
        const FDemoCommandResult LegacyResult=Mode->ExecutePlayerCommand(PC,Legacy); // 即使真租约/真Serial/真Rules也不得绕过实际ControlId验证。
        bLegacyCasinoRouteRejected=LegacyResult.Status==EDemoCommandStatus::Rejected && Progress->GetSnapshot().Coins==LegacyCoins && Progress->GetSnapshot().Gambling.Serial==LegacySerial && Saves->GetChangeSerial()==LegacySaveSerial
            && View->GetLastResult().Mode==PreviousView.Mode && View->GetLastResult().Serial==PreviousView.Serial && View->GetLastResult().Symbols==PreviousView.Symbols;
        if (!Check(bLegacyCasinoRouteRejected,TEXT("legacy Casino business route cannot bypass physical control; coins/serial/save/local result unchanged (RPC transport not covered)"))) return;
        AmountControlIndex=0; Step=4; NextAt=Now+.1f; break;
    }
    case 4:
    {
        static const FName Keys[]={TEXT("amount.clear"),TEXT("amount.digit.1"),TEXT("amount.digit.0"),TEXT("amount.commit")}; // 实际逐键控件输入，不直接改CasinoDraft。
        if (!AimAtControl(Keys[AmountControlIndex])) return;
        Step=5; break;
    }
    case 5:
        if (!InteractWithAimedControl()) return;
        if (++AmountControlIndex<4) Step=4;
        else Step=6;
        break;
    case 6:
        if (!CheckNoFullscreenPage() || !Check(Flow->GetCasinoDraft().Mode==1 && Flow->GetCasinoDraft().Stake==10,TEXT("real clear/digit1/digit0/commit controls set a ten-gold slot wager"))) return;
        BeforeCoins=Progress->GetSnapshot().Coins; BeforeSerial=Progress->GetSnapshot().Gambling.Serial; BeforeRequestId=Input->GetLastSubmittedRequestId(); BeforeSaveSerial=Saves->GetChangeSerial();
        ModelRevealObservedRealAt=-1.; // 此处尚未提交，真实视觉时间必须从后续实际回执开始观测。
        if (!AimAtControl(TEXT("slots.pull"))) return; Step=7; break;
    case 7:
        if (!InteractWithAimedControl()) return;
        if (!Check(Input->GetLastSubmittedRequestId()==BeforeRequestId+1,TEXT("real lever creates exactly one new component request nonce"))) return;
        Step=8; NextAt=Now+.2f; break;
    case 8:
    {
        const FDemoCasinoResult& Result=View->GetLastResult(); // 仅实际Owner接收结果，测试不手工设置Committed或模拟随机。
        if (Input->IsWaiting() || Result.Serial<BeforeSerial+1) { if (Now-ControlAimAt>3.f) Check(false,TEXT("actual authoritative slot result reaches Owner local view")); else NextAt=Now+.1f; return; }
        if (bModelAudit)
        {
            const double RealNow=FPlatformTime::Seconds(); // 生产机械表演同样使用单调秒；同步Save可能耗225ms，不能以存前World截止当成已播放225ms。
            if (ModelRevealObservedRealAt<0.) ModelRevealObservedRealAt=RealNow;
            if (RealNow-ModelRevealObservedRealAt<.15) { NextAt=Now+.05f; return; } // 至少经过真实输入组件Tick后采MidPose，不手调组件/动画时钟或直接Refresh。
        }
        bool bSymbols=Result.Symbols.Num()==3; for (int32 Symbol:Result.Symbols) bSymbols&=Symbol>=0 && Symbol<=4;
        if (!Check(Result.Mode==1 && Result.Serial==BeforeSerial+1 && bSymbols,TEXT("Owner feedback contains three legal committed slot symbols"))) return;
        CommittedPaid=Result.Paid; CommittedSymbols=Result.Symbols; CommittedCoins=Progress->GetSnapshot().Coins; CommittedSerial=Progress->GetSnapshot().Gambling.Serial;
        ++ModelCommittedRounds;
        if (!Check(CommittedSerial==BeforeSerial+1 && int64(CommittedCoins)==int64(BeforeCoins)-10+CommittedPaid,TEXT("real transaction debits ten gold and credits only actual committed Paid"))) return;
        if (!Check(Saves->GetChangeSerial()>BeforeSaveSerial,TEXT("real checkpoint write advances GI save serial")) || !CheckDiskCheckpoint(CommittedCoins,CommittedSerial) || !CheckNoFullscreenPage()) return;
        if (!CheckModelPose(false)) return; CaptureModelScreenshot(TEXT("Round1_MidReveal"));
        Step=9; NextAt=Now+3.5f; break; // 允许已提交私有表演真实运行，再做失败路径，不缩短或伪造动画时钟。
    }
    case 9:
        if (!CheckModelPose(true)) return; CaptureModelScreenshot(TEXT("Round1_SavedSymbols"));
        SaveSerialBeforeFailure=Saves->GetChangeSerial(); RequestIdBeforeFailure=Input->GetLastSubmittedRequestId(); SaveFailureCalls=0;
        Mode->GetShopSystem()->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(this,&ADemoCasinoPhysicalInteractionTest::RejectSave)); bSaveOverrideInstalled=true;
        if (!AimAtControl(TEXT("slots.pull"))) { RestoreSaveBinding(); return; } Step=10; break;
    case 10:
        if (!InteractWithAimedControl()) return;
        RestoreSaveBinding(); Step=11; NextAt=Now+.2f; break;
    case 11:
        if (!Check(SaveFailureCalls==1 && bWaitingReentryObserved,TEXT("one actual save failure and synchronous Waiting reentry path observed"))) return;
        if (!Check(Input->GetLastSubmittedRequestId()==RequestIdBeforeFailure+1 && !Input->IsWaiting(),TEXT("failed lever emits one request and releases local Waiting"))) return;
        if (!Check(Progress->GetSnapshot().Coins==CommittedCoins && Progress->GetSnapshot().Gambling.Serial==CommittedSerial && Saves->GetChangeSerial()==SaveSerialBeforeFailure,TEXT("production save rejection rolls back coins/serial without a new save"))) return;
        if (!Check(View->GetLastResult().Serial==CommittedSerial && View->GetLastResult().Symbols==CommittedSymbols,TEXT("failed candidate does not replace last committed private result")) || !CheckDiskCheckpoint(CommittedCoins,CommittedSerial) || !CheckNoFullscreenPage()) return;
        if (!AimAtControl(TEXT("device.exit"))) return; Step=12; break;
    case 12:
        if (InteractWithAimedControl()) { Step=13; NextAt=Now+.2f; }
        break;
    case 13:
        if (!Check(!Input->IsOperating(),TEXT("actual EXIT control releases independent operation")) || !CheckStableQueries() || !CheckNoFullscreenPage()) return;
        if (!Check(Mode->BuildPlayerMenuSnapshot(PC).CasinoDeviceId.IsNone(),TEXT("server no longer projects a device lease after exit"))) return;
        if (bModelAudit) { if (!AimAtControl(TEXT("slots.pull"))) return; Step=20; break; } // 真退出后重新Focus开新租约，不能直接调用View.Open伪造恢复。
        Step=14; RestoreSaveBinding();
        if (!WriteReport(true)) { Check(false,TEXT("write actual isolated physical-slot report")); return; }
        bFinished=true; UE_LOG(LogFPSDemo,Display,TEXT("CASINO_PHYSICAL_TEST_SUCCESS scope=one_Mode1_Editor_Standalone_real_route_save_failure directory=%s"),*EvidenceDirectory); FPlatformMisc::RequestExitWithStatus(false,0); break;
    case 20:
        if (InteractWithAimedControl()) { Step=21; NextAt=Now+.25f; } break;
    case 21:
        if (!Input->IsOperating()) { if (Now-ControlAimAt>3.f) Check(false,TEXT("formal device reentry gets a new real lease")); else NextAt=Now+.1f; return; }
        if (!Check(View->GetLastResult().Serial==CommittedSerial && View->GetLastResult().Symbols==CommittedSymbols,TEXT("reentry restores original durable typed symbols without a new transaction")) || !CheckModelPose(true) || !CheckDiskCheckpoint(CommittedCoins,CommittedSerial)) return;
        CaptureModelScreenshot(TEXT("Reentry_DurableSymbols"));
        BeforeCoins=CommittedCoins; BeforeSerial=CommittedSerial; BeforeRequestId=Input->GetLastSubmittedRequestId(); BeforeSaveSerial=Saves->GetChangeSerial();
        ModelRevealObservedRealAt=-1.; // 新局真实回执重新开始单调采样；不继承上次已恢复的末帧时间。
        if (!AimAtControl(TEXT("slots.pull"))) return; Step=22; break;
    case 22:
        if (InteractWithAimedControl()) { Step=23; NextAt=Now+.2f; } break;
    case 23:
    {
        const FDemoCasinoResult& Result=View->GetLastResult(); // 第二次仍必须来自原真实保存/Owner回执。
        if (Input->IsWaiting() || Result.Serial<BeforeSerial+1) { if (Now-ControlAimAt>3.f) Check(false,TEXT("second real model round commits")); else NextAt=Now+.1f; return; }
        const double RealNow=FPlatformTime::Seconds(); // 与第一局同一真实视觉时钟，只等实际生产Tick，不给回执写入或机械组件赋值。
        if (ModelRevealObservedRealAt<0.) ModelRevealObservedRealAt=RealNow;
        if (RealNow-ModelRevealObservedRealAt<.15) { NextAt=Now+.05f; return; }
        if (!Check(Result.Serial==BeforeSerial+1 && Result.Symbols.Num()==3 && Input->GetLastSubmittedRequestId()==BeforeRequestId+1,TEXT("continuous model round uses exactly one new nonce and committed serial"))) return;
        CommittedPaid=Result.Paid; CommittedSymbols=Result.Symbols; CommittedCoins=Progress->GetSnapshot().Coins; CommittedSerial=Progress->GetSnapshot().Gambling.Serial; ++ModelCommittedRounds;
        if (!Check(int64(CommittedCoins)==int64(BeforeCoins)-10+CommittedPaid && Saves->GetChangeSerial()>BeforeSaveSerial,TEXT("continuous model round debits and pays only original transaction")) || !CheckDiskCheckpoint(CommittedCoins,CommittedSerial) || !CheckModelPose(false)) return;
        CaptureModelScreenshot(TEXT("Round2_MidReveal")); Step=24; NextAt=Now+3.5f; break;
    }
    case 24:
        if (!CheckModelPose(true)) return; CaptureModelScreenshot(TEXT("Round2_SavedSymbols"));
        if (!AimAtControl(TEXT("device.exit"))) return; Step=25; break;
    case 25:
        if (InteractWithAimedControl()) { Step=26; NextAt=Now+.25f; } break;
    case 26:
        if (!Check(ModelCommittedRounds==2 && !Input->IsOperating() && ModelPoseChecks>=5,TEXT("two real formal-model rounds and durable reentry completed, then actual exit")) || !CheckStableQueries() || !CheckNoFullscreenPage()) return;
        for (const FString& File:ModelScreenshots) if (!Check(IFileManager::Get().FileExists(*File),TEXT("requested actual model RHI screenshot was written"))) return;
        if (!WriteReport(true)) { Check(false,TEXT("write formal model evidence report")); return; }
        bFinished=true; UE_LOG(LogFPSDemo,Display,TEXT("CASINO_MODEL_TEST_SUCCESS directory=%s"),*EvidenceDirectory);
        UE_LOG(LogFPSDemo,Display,TEXT("CASINO_PHYSICAL_TEST_SUCCESS scope=formal_model_2rounds_typed_restore_save_failure directory=%s"),*EvidenceDirectory); FPlatformMisc::RequestExitWithStatus(false,0); break;
    default: Check(false,TEXT("unexpected physical interaction fixture stage")); break;
    }
#endif
}
void ADemoCasinoPhysicalInteractionTest::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    RestoreSaveBinding();
#endif
    Super::EndPlay(Reason);
}
