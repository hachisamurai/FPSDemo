// 职责：在真实 UE 游戏中审查 R02 B04/B07；真实预算、正式终端按钮及正式出发路径与后置表现夹具分开。
// 边界：4000金币为显式隔离预算；paid-ammo原交易检查后单独开启Editor临时权限测试直接装配，不伪造永久来源、扣费、保存回执或十关结果。
#include "Tests/DemoR02S1Audit.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "AI/DemoEnemy.h"
#include "AssetCompilingManager.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/DemoCharacter.h"
#include "Combat/Feedback/DemoHitFeedbackComponent.h" // 仅读真实枪弹扣血后交付的拥有者命中缓存；夹具绝不调用Confirm或改写事件。
#include "CommonInputSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Interaction/DemoInteractable.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h" // 独立 OnEndFrame watchdog 在 World 暂停时仍核超时，不推进游戏夹具。
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h" // Editor临时装备前后只读比较真实永久档案，不伪造拥有权。
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerState.h"
#include "Save/DemoRunSave.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "Tests/DemoUILayoutChecks.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/R02/DemoR02EditorialButton.h"
#include "UnrealClient.h"
#include "UObject/Package.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h" // 稳定弹药ID来自固定目录；旧AmmoOffers投影未填Id，不把默认None当实际库存身份。
#endif

bool UDemoR02S1Audit::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const UWorld* World=Cast<UWorld>(Outer); // 生命周期由引擎管理，只同步检查创建对象。
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone
        &&World->GetOutermost()->GetName()==TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")
        &&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoR02S1Audit"))
        &&!FParse::Param(FCommandLine::Get(),TEXT("DemoCommonUITest"))&&!FParse::Param(FCommandLine::Get(),TEXT("DemoR02StyleBoard"))
        &&!FParse::Param(FCommandLine::Get(),TEXT("DemoOutlineProductionSmoke"));
#else
    return false;
#endif
}

bool UDemoR02S1Audit::DoesSupportWorldType(EWorldType::Type Type) const
{
    DEMO_LOG_CALL();
    return WITH_EDITOR&&Type==EWorldType::Game;
}

void UDemoR02S1Audit::OnWorldBeginPlay(UWorld& World)
{
    DEMO_LOG_CALL();
    Super::OnWorldBeginPlay(World);
#if WITH_EDITOR
    Started=FPlatformTime::Seconds();
    bInteractive=FParse::Param(FCommandLine::Get(),TEXT("DemoR02S1Interactive"));
    if(!FParse::Value(FCommandLine::Get(),TEXT("DemoR02S1Output="),Directory))
        Directory=FPaths::ProjectSavedDir()/TEXT("Tests/R02S1")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    Directory=FPaths::ConvertRelativePathToFull(Directory);
    TArray<FString> ExistingPNGs; // 输出目录只允许本次启动器的日志/隔离 INI；旧证据必须留在原处，绝不覆盖。
    IFileManager::Get().FindFiles(ExistingPNGs,*(Directory/TEXT("*.png")),true,false);
    if(IFileManager::Get().FileExists(*(Directory/TEXT("audit.json")))||!ExistingPNGs.IsEmpty())
    {
        bFinished=true; // 尚未注册任何委托；拒绝重用时不调用 Finish，以免改写旧 audit.json。
        UE_LOG(LogFPSDemo,Error,TEXT("R02_S1_AUDIT_FAIL refusing existing audit.json/PNG evidence directory=%s"),*Directory);
        FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
    if(!IFileManager::Get().MakeDirectory(*Directory,true))
    { bFinished=true; UE_LOG(LogFPSDemo,Error,TEXT("R02_S1_AUDIT_FAIL cannot create output directory")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Notes.Add(TEXT("Editor-only GI temporary slot 0; no cloud; no persistent campaign progress."));
    Notes.Add(TEXT("Launcher responsibility: independent process and fresh evidence directory per resolution, with a separate -GameUserSettingsINI file for this run. This isolates device FirstRunGuide acknowledgement; the harness rejects existing audit.json/PNG evidence."));
    Notes.Add(bInteractive?TEXT("Interactive handoff: default genuine new-slot wallet; no fixture budget injected."):TEXT("Explicit isolated fixture budget: 4000 gold before transactions; no weapon ownership injected."));
    TickHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&UDemoR02S1Audit::OnPostTick); // 本 World GT 弱 UObject 绑定；不跨线程或跨旅行持有自身。
    WatchdogHandle=FCoreDelegates::OnEndFrame.AddUObject(this,&UDemoR02S1Audit::OnFrameWatchdog); // GT 引擎帧弱绑定独立于 World Tick；暂停不再使有界审查无限等待。
#endif
}

void UDemoR02S1Audit::Deinitialize()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if(TickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
    TickHandle.Reset();
    if(WatchdogHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(WatchdogHandle);
    WatchdogHandle.Reset();
    RestorePresentationFixture();
    if(bInputFiltered&&PC.IsValid()&&PC->GetLocalPlayer())
        if(UCommonInputSubsystem* Input=PC->GetLocalPlayer()->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("R02S1Audit"),false);
    bInputFiltered=false;
#endif
    Super::Deinitialize();
}

#if WITH_EDITOR
bool UDemoR02S1Audit::Check(bool bValue,const FString& Reason)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Display,TEXT("R02_S1_CHECK_%s stage=%d %s"),bValue?TEXT("PASS"):TEXT("FAIL"),Stage,*Reason);
    Assertions.Add((bValue?TEXT("PASS: "):TEXT("FAIL: "))+Reason);
    if(bValue) ++Passed; else Finish(false,Reason);
    return bValue;
}

bool UDemoR02S1Audit::Press(FName Action)
{
    DEMO_LOG_CALL();
    UDemoUIManager* UI=PC.IsValid()?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 借用正式 PC 创建的唯一协调器。
    if(UI) UI->Sync();
    const bool bAccepted=UI&&UI->PressActionForTest(Action); // 只触发实际顶层已启用按钮，不直调经济入口。
    if(UI) UI->Sync();
    return Check(bAccepted,FString::Printf(TEXT("actual current CommonUI button accepted: %s"),*Action.ToString()));
}

void UDemoR02S1Audit::Key(FKey InputKey)
{
    DEMO_LOG_CALL();
    FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(InputKey,FModifierKeysState(),0,false,0,0));
    FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(InputKey,FModifierKeysState(),0,false,0,0));
}

void UDemoR02S1Audit::Next(int32 NextStage,double Delay)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Log,TEXT("R02_S1_STAGE %d -> %d delay=%.2f"),Stage,NextStage,Delay);
    Stage=NextStage; StepStarted=FPlatformTime::Seconds(); Deadline=StepStarted+Delay;
}

bool UDemoR02S1Audit::CheckR02Page(FName RepresentativeAction)
{
    DEMO_LOG_CALL();
    UDemoUIManager* UI=PC.IsValid()?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 当前生产协调器，不新建根。
    UDemoMenuScreen* Screen=UI?UI->GetActiveScreen():nullptr; // 当前真实顶层页面，不能验证已拆卸旧实例。
    UDemoMenuButton* Button=Screen?Screen->FindAction(RepresentativeAction):nullptr; // 当前树中的代表动作。
    return Check(Screen&&Screen->IsActivated()&&Screen->IsUsingR02Presentation(),TEXT("actual activated production screen uses R02 presentation"))
        &&Check(Button&&Button->IsA<UDemoR02EditorialButton>()&&Button->GetTypedOuter<UCommonActivatableWidget>()==Screen,
            TEXT("actual R02 CommonUI button has current Activatable in its Outer parent chain"))
        &&Check(Screen->HasUserFocusedDescendants(PC.Get())||Screen->HasAnyUserFocus(),TEXT("actual R02 page owns Slate focus without forced focus injection"));
}

bool UDemoR02S1Audit::CheckStaleButton(TWeakObjectPtr<UDemoMenuButton> Button,const FString& Context)
{
    DEMO_LOG_CALL();
    if(!Button.IsValid())
    {
        Notes.Add(Context+TEXT(": 旧对象已回收，未执行旧对象点击 / old widget invalid or reclaimed; no stale-object click was exercised."));
        Assertions.Add(TEXT("NOT_RUN: ")+Context+TEXT(": old widget already invalid/reclaimed; stale-object click was not exercised."));
        UE_LOG(LogFPSDemo,Display,TEXT("R02_S1_CHECK_NOT_RUN stage=%d %s old widget invalid/reclaimed; stale click not exercised"),Stage,*Context);
        return true; // 这里只允许状态机继续；不增加 Passed，也不把未执行点击写成 PASS。
    }
    return Check(!Button->PressForTest(),Context+TEXT(": actual retained live old widget rejects PressForTest"));
}

bool UDemoR02S1Audit::Capture(const FString& Name,const FString& Scope,bool bValidateLayout)
{
    DEMO_LOG_CALL();
    if(!Check(PendingPNG.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested(),TEXT("normal screenshot request has no competing in-flight request"))) return false;
    const FString TargetPNG=Directory/(Name+TEXT(".png")); // 每次请求再次核实目标不存在，禁止把旧同名 PNG 当作新帧完成。
    if(!Check(!IFileManager::Get().FileExists(*TargetPNG),TEXT("normal screenshot target is fresh and will not overwrite evidence: ")+TargetPNG)) return false;
    UDemoUIManager* UI=PC.IsValid()?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 截图同帧只借用当前顶层。
    if(bValidateLayout&&UI&&UI->GetActiveScreen()) DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // Slate OnEndFrame 测实际已绘制控件几何，不读重建中的缓存。
    PendingPNG=TargetPNG; PNGs.Add(PendingPNG); CaptureScopes.Add(Scope);
    ShotFrame=GFrameCounter; ShotStarted=FPlatformTime::Seconds();
    FScreenshotRequest::RequestScreenshot(PendingPNG,true,false); // 包含真实 Canvas/Slate；无 RT、无像素替身、无截图背景资产。
    UE_LOG(LogFPSDemo,Log,TEXT("R02_S1_CAPTURE scope=%s file=%s"),*Scope,*PendingPNG);
    return true;
}

void UDemoR02S1Audit::RestorePresentationFixture()
{
    DEMO_LOG_CALL();
    if(bForcedEliminated&&Player.IsValid()&&PC.IsValid()&&PC->GetPawn()==Player.Get()) Player->SetCoopEliminated(false);
    bForcedEliminated=false;
    if(PresentationBoss.IsValid()) { PresentationBoss->DeactivateSpawn(); PresentationBoss->Destroy(); }
    PresentationBoss.Reset();
}

void UDemoR02S1Audit::Finish(bool bSuccess,const FString& Reason,bool bHandOff)
{
    DEMO_LOG_CALL();
    if(bFinished) return;
    bFinished=true;
    if(TickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
    TickHandle.Reset();
    if(WatchdogHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(WatchdogHandle);
    WatchdogHandle.Reset();
    RestorePresentationFixture();
    if(bInputFiltered&&PC.IsValid()&&PC->GetLocalPlayer())
        if(UCommonInputSubsystem* Input=PC->GetLocalPlayer()->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("R02S1Audit"),false);
    bInputFiltered=false;
    const FString Status=bSuccess?(bHandOff?TEXT("INTERACTIVE_READY"):TEXT("PASS_RUNTIME_WITH_PRESENTATION_SAMPLES")):TEXT("FAIL"); // 交付状态不把有限运行写成完整视觉验收。
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 只写本进程证据，不持有 JSON 到 World 卸载后。
    Report->SetBoolField(TEXT("success"),bSuccess); Report->SetStringField(TEXT("status"),Status); Report->SetStringField(TEXT("reason"),Reason);
    Report->SetBoolField(TEXT("interactive_ready"),bHandOff&&bSuccess); Report->SetNumberField(TEXT("stage"),Stage); Report->SetNumberField(TEXT("passed_assertions"),Passed);
    Report->SetNumberField(TEXT("elapsed_real_seconds"),FPlatformTime::Seconds()-Started);
    Report->SetBoolField(TEXT("real_combat_entry_observed"),bTrueCombatObserved); Report->SetBoolField(TEXT("real_runtime_damage_observed"),bObservedRealDamage);
    Report->SetBoolField(TEXT("real_shot_requested"),bRealShotRequested); Report->SetBoolField(TEXT("real_shot_spent_ammo"),bRealShotSpentAmmo);
    Report->SetBoolField(TEXT("real_hit_feedback_observed"),bObservedRealHit);
    Report->SetBoolField(TEXT("real_hit_visible_capture"),bRealHitVisibleCapture);
    Report->SetBoolField(TEXT("synthetic_presentation_samples"),bSyntheticStarted);
    Report->SetBoolField(TEXT("synthetic_low_health_ge_applied"),bSyntheticLowHealthGEApplied);
    Report->SetBoolField(TEXT("editor_direct_equip_fixture_observed"),bDirectEquipObserved);
    Report->SetStringField(TEXT("scope"),TEXT("Real production CommonUI/terminal requests, isolated paid-ammo transactions and legal single-player first Combat entry. After paid-ammo checks, a separately labelled EDITOR_TEMPORARY_EQUIPMENT_FIXTURE uses the original UnlockAll command and direct UI rifle equip/repeat, proving no permanent progress/profile/save mutation; it does not prove genuine purchased rifle ownership. Later SYNTHETIC_PRESENTATION frames are explicit bounded visual fixtures only. No ten-level completion, actual Boss encounter, real cooperative death/spectator, RPC replay, unlocked-sniper ADS, EOS, Cook or Shipping acceptance."));
    int32 Width=0,Height=0; if(PC.IsValid()) PC->GetViewportSize(Width,Height); // 只记实际本机视口物理像素；外层另验 PNG 元数据。
    Report->SetNumberField(TEXT("viewport_width"),Width); Report->SetNumberField(TEXT("viewport_height"),Height);
    TArray<TSharedPtr<FJsonValue>> Images,Scopes,Checks,Limits; // 平行数组顺序与请求 PNG 一致；不嵌入图片数据。
    for(const FString& Value:PNGs) Images.Add(MakeShared<FJsonValueString>(Value));
    for(const FString& Value:CaptureScopes) Scopes.Add(MakeShared<FJsonValueString>(Value));
    for(const FString& Value:Assertions) Checks.Add(MakeShared<FJsonValueString>(Value));
    for(const FString& Value:Notes) Limits.Add(MakeShared<FJsonValueString>(Value));
    Report->SetArrayField(TEXT("pngs"),Images); Report->SetArrayField(TEXT("capture_scopes"),Scopes); Report->SetArrayField(TEXT("assertions"),Checks); Report->SetArrayField(TEXT("notes"),Limits);
    FString Text; // 序列化目标只含本次有限断言与绝对证据路径。
    const FString ReportPath=Directory/TEXT("audit.json"); // 结束时再次保护目标，外部迟到的同名证据不能被覆盖。
    const bool bReportExists=IFileManager::Get().FileExists(*ReportPath);
    if(bReportExists) UE_LOG(LogFPSDemo,Error,TEXT("R02_S1_AUDIT_FAIL refusing to overwrite report=%s"),*ReportPath);
    const bool bWritten=!bReportExists&&FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text))&&FFileHelper::SaveStringToFile(Text,*ReportPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogFPSDemo,Display,TEXT("%s stage=%d assertions=%d directory=%s reason=%s"),bSuccess&&bWritten?(bHandOff?TEXT("R02_S1_INTERACTIVE_READY"):TEXT("R02_S1_AUDIT_PASS")):TEXT("R02_S1_AUDIT_FAIL"),Stage,Passed,*Directory,*Reason);
    if(!bHandOff||!bSuccess||!bWritten) FPlatformMisc::RequestExitWithStatus(false,bSuccess&&bWritten?0:1);
}

void UDemoR02S1Audit::OnFrameWatchdog()
{
    DEMO_LOG_TICK();
    if(!bFinished&&FPlatformTime::Seconds()-Started>600.)
        Finish(false,TEXT("600-second independent end-frame watchdog deadline, including World pause"));
}

void UDemoR02S1Audit::OnPostTick(UWorld* World,ELevelTick Type,float Delta)
{
    DEMO_LOG_TICK();
    if(bFinished||World!=GetWorld()||Type==LEVELTICK_TimeOnly) return;
    const double Now=FPlatformTime::Seconds(); // 截图、暂停和异步准备均使用真实秒，不靠 World 时钟超时。
    FString LayoutError; // 前一截图帧末的实际几何结果，不以源码布局尺寸自证。
    const int32 LayoutResult=DemoUILayoutChecks::ProcessPending(LayoutError);
    if(LayoutResult&&!Check(LayoutResult>0,TEXT("rendered CommonUI geometry: ")+LayoutError)) return;
    if(!PendingPNG.IsEmpty())
    {
        if(GFrameCounter>ShotFrame&&IFileManager::Get().FileSize(*PendingPNG)>1000)
        {
            if(FPaths::GetCleanFilename(PendingPNG)==TEXT("10a_B07_Hit_REAL.png")) bRealHitVisibleCapture=true; // 可见回执请求与实际PNG完成分别核实，不把仅请求当截图交付。
            PendingPNG.Reset();
        }
        else if(Now-ShotStarted>8.) Finish(false,TEXT("normal PNG was not written within 8 seconds"));
        return;
    }
    const bool bShaderBusy=GShaderCompilingManager&&GShaderCompilingManager->IsCompiling(); // 当前真实编译事实，311只读命中观察不受其静稳等待阻塞。
    const int32 ResourceJobs=FAssetCompilingManager::Get().GetNumRemainingAssets(); // 实际资产编译任务数；为0才允许短时命中截图。
    if(bShaderBusy||ResourceJobs>0)
    {
        ResourceStableSince=-1.;
        if(Stage!=311) return; // 普通页面仍等资源；311只读缓存必须及时采样，不能用.5秒门错过.24秒真实Marker。
    }
    else if(ResourceStableSince<0.) ResourceStableSince=Now;
    if((Stage!=311&&Now-ResourceStableSince<.5)||Now<Deadline) return;
    ADemoPlayerController* Controller=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(World,0)); // 每次借用实际拥有者，替换 PC 不保留裸指针。
    ADemoCharacter* Pawn=Controller?Cast<ADemoCharacter>(Controller->GetPawn()):nullptr;
    AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>();
    ADemoGameState* State=World->GetGameState<ADemoGameState>();
    ADemoPlayerState* PS=Controller?Controller->GetPlayerState<ADemoPlayerState>():nullptr;
    UDemoUIManager* UI=Controller?Controller->FindComponentByClass<UDemoUIManager>():nullptr;
    UDemoMenuFlowComponent* Flow=Controller?Controller->GetMenuFlow():nullptr;
    UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
    UDemoWeaponComponent* Equipment=Pawn?Pawn->GetWeaponComponent():nullptr;
    if(!Controller||!Pawn||!Mode||!State||!PS||!UI||!Flow||!Progress||!Equipment||!Pawn->GetDemoAttributes()||World->GetTimeSeconds()<1.) return;
    PC=Controller; Player=Pawn; UI->Sync();
    if(((Stage>0&&Stage<30)||(Stage>=211&&Stage<=217))&&Now-StepStarted>30.) { Finish(false,TEXT("production UI stage exceeded 30 seconds")); return; }
    switch(Stage)
    {
    case 0:
    {
        Flow->UpdateRealtime(0.f);
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { if(Press(TEXT("OverlayBack"))) Next(0); return; }
        if(!bInteractive) if(UCommonInputSubsystem* Input=PC->GetLocalPlayer()->GetSubsystem<UCommonInputSubsystem>())
        { Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("R02S1Audit"),true); bInputFiltered=true; } // 仅自有自动化进程过滤实体手柄干扰；交互模式不设过滤。
        UDemoRunSaves* Saves=World->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // WITH_EDITOR 每 GI 独立本地临时槽，无云服务。
        if(!Check(!Flow->HasBlockingOverlay()&&Saves&&Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("production slot 0 creation and StartRun reach genuine Hub"))) return;
        Saves->ConsumePending(); PC->OnRunReady();
        if(!bInteractive)
        {
            FDemoPlayerProgressSnapshot Budget=Progress->GetSnapshot(); // 明确只改夹具初始预算，不改权限或交易结果。
            Budget.Coins=4000;
            if(!Check(Progress->Commit(Budget)&&Mode->SaveCheckpoint(),TEXT("explicit 4000-gold isolated budget committed before UI purchases"))) return;
        }
        InitialCoins=Progress->GetSnapshot().Coins; PrimaryBeforeInspection=PC->GetMenuSnapshot().PrimaryWeaponId;
        PC->PublishMenuSnapshot(); Next(1); return;
    }
    case 1:
        if(!Mode->GetShopTerminal()) return;
        Pawn->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // 只定位测试角色到真实终端有效距离；Capsule 不嵌入地面。
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Mode->GetShopTerminal()->Interact(Pawn); UI->Sync(); Next(2); return;
    case 2:
        if(!Check(Flow->IsUpgradeMenuOpen()&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0,TEXT("production terminal has exactly one real menu stack entry"))) return;
        if(!Check(PC->IsMoveInputIgnored()&&PC->IsLookInputIgnored()&&PC->bShowMouseCursor,TEXT("actual terminal owns movement/look/cursor policy"))) return;
        if(Press(TEXT("TerminalWeapons"))) Next(3); return;
    case 3:
        if(!CheckR02Page(TEXT("Weapon0"))) return;
        if(Capture(TEXT("01_B04_Weapons_Default"),TEXT("REAL_RUNTIME / genuine new-slot default pistol and terminal"))) Next(4); return;
    case 4:
        if(bInteractive) { Finish(true,TEXT("formal R02 weapon terminal is open; audit stops and normal game remains playable"),true); return; }
        if(Press(TEXT("Weapon1"))) Next(5); return;
    case 5:
        if(!Check(Flow->IsWeaponMenuOpen()&&Flow->GetInspectedWeapon()==INDEX_NONE&&UI->GetModalStackCount()==0
            &&PC->GetMenuSnapshot().PrimaryWeaponId==PrimaryBeforeInspection&&Progress->GetSnapshot().Coins==InitialCoins,
            TEXT("weapon catalogue inspection alone opens no modal, equips nothing and spends no gold"))) return;
        if(!CheckR02Page(TEXT("UI.WeaponEquip"))) return;
        StaleUnderlying=UI->GetActiveScreen()->FindAction(TEXT("Weapon1"));
        if(Capture(TEXT("02_B04_Weapons_Locked"),TEXT("REAL_RUNTIME / genuine locked rifle; no ownership injection"))) Next(6); return;
    case 6:
    {
        UDemoMenuScreen* Screen=UI->GetActiveScreen(); // 当前仍是武器目录，用户已删除的说明模态不能出现。
        const UDemoMenuButton* Equip=Screen?Screen->FindAction(TEXT("UI.WeaponEquip")):nullptr;
        if(!Check(Screen&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0&&Flow->GetInspectedWeapon()==INDEX_NONE
            &&!Screen->FindAction(TEXT("UI.WeaponDetails"))&&!Screen->FindAction(TEXT("WeaponTipClose"))&&Equip&&!Equip->GetIsEnabled(),
            TEXT("genuine locked rifle remains inline with disabled direct equip and no removed detail-modal actions"))) return;
        if(!Check(!UI->PressActionForTest(TEXT("UI.WeaponEquip"))&&Progress->GetSnapshot().Coins==InitialCoins
            &&PC->GetMenuSnapshot().PrimaryWeaponId==PrimaryBeforeInspection,TEXT("actual disabled locked-rifle equip rejects the click without wallet or primary changes"))) return;
        if(Capture(TEXT("03_B04_Weapons_NoModal"),TEXT("REAL_RUNTIME / genuine locked rifle has inline ownership and disabled direct equip; no weapon detail modal"))) Next(9); return;
    }
    case 9:
        if(!Check(UI->GetModalStackCount()==0&&(UI->GetActiveScreen()->HasUserFocusedDescendants(PC.Get())||UI->GetActiveScreen()->HasAnyUserFocus()),TEXT("catalogue keeps actual parent focus without a removed details/back layer"))) return;
        if(Press(TEXT("Weapon0"))) Next(10); return;
    case 10:
    {
        const UDemoMenuButton* Equip=UI->GetActiveScreen()?UI->GetActiveScreen()->FindAction(TEXT("UI.WeaponEquip")):nullptr; // 固定副枪说明留本页，不创建装配确认或更换主槽。
        if(!Check(Flow->GetInspectedWeapon()==INDEX_NONE&&UI->GetModalStackCount()==0&&Equip&&!Equip->GetIsEnabled()
            &&PC->GetMenuSnapshot().PrimaryWeaponId==PrimaryBeforeInspection,TEXT("fixed pistol remains inline, cannot equip into primary and opens no detail modal"))) return;
        Next(11); return;
    }
    case 11:
        if(Press(TEXT("TerminalAmmo"))&&Press(TEXT("Ammo1"))) Next(12); return;
    case 12:
    {
        const auto& Offers=PC->GetMenuSnapshot().AmmoOffers; // 实际当前拥有者报价；不在夹具写死默认价格。
        if(!Check(Flow->IsAmmoMenuOpen()&&Offers.IsValidIndex(1)&&!Offers[1].bUnlocked&&Offers[1].Cost>0&&Offers[1].BlockReason.IsEmpty(),TEXT("genuine new-slot paid ammunition is locked with an actionable server quote"))) return;
        FDemoLoadoutSnapshot Loadout; Equipment->CaptureSnapshot(Loadout); // 真实库存读回，独立于页面文字。
        InitialAmmoSelection=Loadout.SelectedAmmoId; AmmoId=UDemoAmmoCatalog::IdAt(1); QuotedPrice=Offers[1].Cost; // 报价只读服务器Cost；身份采用同一固定目录白名单，绝不从未填充的Offer.Id推断。
        if(!Check(!AmmoId.IsNone(),TEXT("selected paid-ammo stable catalogue ID is valid: ")+AmmoId.ToString())) return;
        if(!CheckR02Page(TEXT("AmmoAction"))) return;
        if(Capture(TEXT("04_B04_Ammo_Locked"),TEXT("REAL_RUNTIME / genuine locked fire-ammo server quote"))) Next(13); return;
    }
    case 13:
        StaleUnderlying=UI->GetActiveScreen()->FindAction(TEXT("AmmoAction"));
        if(Press(TEXT("AmmoAction"))) Next(14); return;
    case 14:
    {
        UDemoMenuButton* Cancel=UI->GetActiveScreen()->FindAction(TEXT("AmmoBuyNo")); // 实际当前确认页安全默认动作。
        if(!Check(Flow->GetPendingAmmoPrice()==QuotedPrice&&UI->GetModalStackCount()==1&&Cancel
            &&(Cancel->HasUserFocus(PC.Get())||Cancel->HasUserFocusedDescendants(PC.Get())),TEXT("real ammo confirmation retains quote and actual Cancel focus"))) return;
        if(!CheckStaleButton(StaleUnderlying,TEXT("ammo confirmation underlying buy action"))) return;
        if(!Check(Progress->GetSnapshot().Coins==InitialCoins,TEXT("opening ammo confirmation has not debited actual wallet"))) return;
        if(Capture(TEXT("05_B04_Ammo_Confirm"),TEXT("REAL_RUNTIME / quoted paid purchase with safe cancellation focus"))) Next(15); return;
    }
    case 15:
        Key(EKeys::Enter); Next(16); return; // 真正键盘确认必须触发默认取消，而非强制调用取消业务。
    case 16:
        if(!Check(Flow->GetPendingAmmoPrice()==INDEX_NONE&&UI->GetModalStackCount()==0&&Progress->GetSnapshot().Coins==InitialCoins,TEXT("real focused Enter cancels purchase without debit"))) return;
        if(Press(TEXT("AmmoAction"))) Next(17); return;
    case 17:
        StaleConfirm=UI->GetActiveScreen()->FindAction(TEXT("AmmoBuyYes"));
        if(Press(TEXT("AmmoBuyYes"))) Next(18); return;
    case 18:
    {
        FDemoLoadoutSnapshot Loadout; Equipment->CaptureSnapshot(Loadout); // 原事务后的库存事实，不修改返回值。
        if(!Check(Progress->GetSnapshot().Coins==InitialCoins-QuotedPrice,FString::Printf(TEXT("actual paid purchase wallet: before=%d quote=%d after=%d expected=%d"),
            InitialCoins,QuotedPrice,Progress->GetSnapshot().Coins,InitialCoins-QuotedPrice))) return;
        TArray<FString> ActualAmmoIds; // 独立记录真实库存身份，合并失败不再掩盖哪个分量与期望不同。
        for(const FName& Id:Loadout.UnlockedAmmoIds) ActualAmmoIds.Add(Id.ToString());
        if(!Check(Loadout.UnlockedAmmoIds.Contains(AmmoId),FString::Printf(TEXT("actual purchase unlocked stable ID=%s; actual unlocked=[%s]"),
            *AmmoId.ToString(),*FString::Join(ActualAmmoIds,TEXT(","))))) return;
        if(!Check(Loadout.SelectedAmmoId==InitialAmmoSelection,FString::Printf(TEXT("actual purchase leaves previous ammo equipped: before=%s after=%s"),
            *InitialAmmoSelection.ToString(),*Loadout.SelectedAmmoId.ToString()))) return;
        if(!Check(UI->GetModalStackCount()==0,FString::Printf(TEXT("actual purchase confirmation closed: modal_stack_count=%d"),UI->GetModalStackCount()))) return;
        if(!Check(!UI->IsFeedbackWarning()&&!UI->GetFeedbackText().IsEmpty(),TEXT("actual owner purchase receipt is presented as Committed, not Rejected or AppliedPendingSave"))) return;
        Notes.Add(TEXT("Actual purchase receipt display: ")+UI->GetFeedbackText()); // 读取正式可靠回执投影，不注入或重放客户端结果。
        if(!CheckStaleButton(StaleConfirm,TEXT("completed purchase old confirmation"))) return;
        if(!Check(Progress->GetSnapshot().Coins==InitialCoins-QuotedPrice,TEXT("actual completed purchase wallet remains debited exactly once"))) return;
        if(Capture(TEXT("06_B04_Ammo_Purchased_NotEquipped"),TEXT("REAL_RUNTIME / actual purchase committed, actual previous ammo still equipped"))) Next(19); return;
    }
    case 19:
        if(Press(TEXT("AmmoAction"))) Next(20); return;
    case 20:
    {
        FDemoLoadoutSnapshot Loadout; Equipment->CaptureSnapshot(Loadout); // 显式装配后的实际服务器装备值。
        if(!Check(Loadout.SelectedAmmoId==AmmoId,FString::Printf(TEXT("separate actual EquipAmmo selection: expected=%s actual=%s"),
            *AmmoId.ToString(),*Loadout.SelectedAmmoId.ToString()))) return;
        if(!Check(Progress->GetSnapshot().Coins==InitialCoins-QuotedPrice,FString::Printf(TEXT("actual EquipAmmo adds no debit: expected=%d actual=%d"),
            InitialCoins-QuotedPrice,Progress->GetSnapshot().Coins))) return;
        if(!Check(!UI->IsFeedbackWarning()&&!UI->GetFeedbackText().IsEmpty(),TEXT("actual owner EquipAmmo receipt is presented as Committed"))) return;
        if(Capture(TEXT("07_B04_Ammo_Equipped"),TEXT("REAL_RUNTIME / actual purchased fire ammunition explicitly equipped"))) Next(211); return;
    }
    case 211:
    {
        UDemoPlayerProfile* Profile=World->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // GI持有，永久档案只作当前内存对照，不输出账号数据。
        UDemoRunSaves* Saves=World->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 本GI真实临时保存服务，序号不由夹具推进。
        if(!Check(Profile&&Saves,TEXT("Editor direct-equip fixture has actual profile and run-save services"))) return;
        DurableProgressBeforeDirectEquip.Reset(); ProfileBeforeDirectEquip.Reset();
        FDemoPlayerProgressSnapshot::StaticStruct()->ExportText(DurableProgressBeforeDirectEquip,&Progress->GetSnapshot(),nullptr,nullptr,PPF_None,nullptr);
        FDemoPlayerProfileData::StaticStruct()->ExportText(ProfileBeforeDirectEquip,&Profile->GetData(),nullptr,nullptr,PPF_None,nullptr);
        SaveSerialBeforeDirectEquip=Saves->GetChangeSerial();
        Notes.Add(TEXT("EDITOR_TEMPORARY_EQUIPMENT_FIXTURE starts after paid ammo purchase/equip checks: actual Demo.Debug.UnlockAll grants only this Standalone GI test permission; actual UI direct-equip submits original authority request. Permanent progress/profile/save serial are compared unchanged. A single explicit inventory ammo decrement tests repeated equip does not refill; this is not player gunfire."));
        PC->ConsoleCommand(TEXT("Demo.Debug.UnlockAll"),false); // 仅显式WITH_EDITOR夹具；使用原控制台入口刷新权限，不能直接改解锁来源或菜单投影。
        Next(212); return;
    }
    case 212:
        if(Press(TEXT("TerminalWeapons"))&&Press(TEXT("Weapon1"))) Next(213); return;
    case 213:
    {
        const UDemoMenuButton* Equip=UI->GetActiveScreen()?UI->GetActiveScreen()->FindAction(TEXT("UI.WeaponEquip")):nullptr; // 实际本页明确装配按钮，目录预览本身不装配。
        if(!Check(Flow->IsWeaponMenuOpen()&&Flow->GetInspectedWeapon()==INDEX_NONE&&UI->GetModalStackCount()==0&&Equip&&Equip->GetIsEnabled()
            &&Equipment->GetPrimaryIndex()==INDEX_NONE,TEXT("actual Editor-permitted rifle preview exposes direct equip without selecting a primary or creating a modal"))) return;
        if(Press(TEXT("UI.WeaponEquip"))) Next(214); return;
    }
    case 214:
    {
        UDemoPlayerProfile* Profile=World->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 当前GI只读档案，临时装备不能记偏好或推进修订。
        UDemoRunSaves* Saves=World->GetGameInstance()->GetSubsystem<UDemoRunSaves>();
        FString CurrentProgress,CurrentProfile; // 仅同步内存对比，不写入报告/日志的私人档案正文。
        FDemoPlayerProgressSnapshot::StaticStruct()->ExportText(CurrentProgress,&Progress->GetSnapshot(),nullptr,nullptr,PPF_None,nullptr);
        if(Profile) FDemoPlayerProfileData::StaticStruct()->ExportText(CurrentProfile,&Profile->GetData(),nullptr,nullptr,PPF_None,nullptr);
        ADemoWeaponBase* Rifle=Equipment->GetActiveWeapon(); // 真实当前Pawn持有的原库存Actor。
        if(!Check(Rifle&&Rifle->GetWeaponId()==TEXT("rifle")&&Equipment->GetPrimaryIndex()==0&&Equipment->GetActiveSlot()==1
            &&UI->GetModalStackCount()==0&&Flow->GetInspectedWeapon()==INDEX_NONE&&Flow->GetMenuMessage().Contains(TEXT("已临时装备")),
            TEXT("actual UI direct equip applies original Editor temporary rifle/slot-1 result in place without a detail modal"))) return;
        if(!Check(Profile&&Saves&&CurrentProgress==DurableProgressBeforeDirectEquip&&CurrentProfile==ProfileBeforeDirectEquip
            &&Saves->GetChangeSerial()==SaveSerialBeforeDirectEquip,TEXT("actual temporary direct equip leaves complete permanent progress, profile and save serial unchanged"))) return;
        Rifle->ModifyAmmo(-1); // 明确的库存边界夹具：制造非满弹样本，重复装配不得免费补弹；不当作射击/命中证据。
        DirectEquipAmmo=Rifle->GetAmmo(); DirectEquipReserve=Rifle->GetReserveAmmo();
        if(Press(TEXT("UI.WeaponEquip"))) Next(215); return;
    }
    case 215:
    {
        UDemoPlayerProfile* Profile=World->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 重复请求后仍独立读永久状态。
        UDemoRunSaves* Saves=World->GetGameInstance()->GetSubsystem<UDemoRunSaves>();
        FString CurrentProgress,CurrentProfile; // 私有全文只在本同步栈比较，不记录内容。
        FDemoPlayerProgressSnapshot::StaticStruct()->ExportText(CurrentProgress,&Progress->GetSnapshot(),nullptr,nullptr,PPF_None,nullptr);
        if(Profile) FDemoPlayerProfileData::StaticStruct()->ExportText(CurrentProfile,&Profile->GetData(),nullptr,nullptr,PPF_None,nullptr);
        if(!Check(Equipment->GetActiveWeapon()&&Equipment->GetActiveWeapon()->GetWeaponId()==TEXT("rifle")&&Equipment->GetActiveSlot()==1
            &&Equipment->GetActiveWeapon()->GetAmmo()==DirectEquipAmmo&&Equipment->GetActiveWeapon()->GetReserveAmmo()==DirectEquipReserve
            &&UI->GetModalStackCount()==0&&Profile&&Saves&&CurrentProgress==DurableProgressBeforeDirectEquip&&CurrentProfile==ProfileBeforeDirectEquip
            &&Saves->GetChangeSerial()==SaveSerialBeforeDirectEquip,TEXT("repeated actual direct-equip action preserves non-full ammo, reserves, permanent records and in-place catalogue"))) return;
        bDirectEquipObserved=true;
        if(Capture(TEXT("07a_B04_DirectEquip_EditorFixture"),TEXT("EDITOR_TEMPORARY_EQUIPMENT_FIXTURE / original console permission then actual direct UI rifle equip/repeat; no debit, permanent source or save mutation; ammo decrement is an explicit fixture"))) Next(216); return;
    }
    case 216:
        if(!Press(TEXT("Close"))) return; // 必须真实关菜单后，原数字槽位API才允许切回固定手枪。
        if(!Check(!Flow->IsUpgradeMenuOpen()&&!Flow->HasBlockingOverlay()&&Equipment->EquipSlot(2)&&Equipment->GetActiveSlot()==2
            &&Equipment->GetPrimaryIndex()==0&&Equipment->GetActiveWeapon()&&Equipment->GetActiveWeapon()->GetWeaponId()==TEXT("pistol"),
            TEXT("original slot-2 action succeeds only after real menu close and keeps the selected primary while restoring pistol activity"))) return;
        Next(217); return;
    case 217:
        if(!Check(Mode->GetShopTerminal()!=nullptr,TEXT("original shop terminal remains for the post-fixture core/departure path"))) return;
        Mode->GetShopTerminal()->Interact(Pawn); UI->Sync(); Next(21); return; // 同一合法距离重新走批准会话，不写菜单或终端令牌。
    case 21:
        if(Press(TEXT("TerminalCore"))) Next(22); return;
    case 22:
        if(Press(TEXT("Core0"))) Next(23); return;
    case 23:
        if(!Check(!PC->GetMenuSnapshot().Build.CoreId.IsNone(),TEXT("legal core selection is supplied by the production request and owner snapshot"))) return;
        UI->Back(); UI->Sync(); Next(24); return;
    case 24:
        UI->Back(); UI->Sync(); Next(25); return;
    case 25:
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { if(Press(TEXT("OverlayBack"))) Next(25); return; }
        if(!Check(!Flow->IsUpgradeMenuOpen()&&!UI->UsesWidgetMenu()&&UI->GetMenuStackCount()==0&&UI->GetModalStackCount()==0
            &&!PC->bShowMouseCursor&&!PC->IsMoveInputIgnored()&&!PC->IsLookInputIgnored(),TEXT("real back chain closes terminal and restores movement, look and game cursor policy"))) return;
        if(Capture(TEXT("08_B07_Hub"),TEXT("REAL_RUNTIME / genuine playable Hub HUD after formal terminal closes"),false)) Next(26); return;
    case 26:
        if(!Check(Mode->GetNextLevelTerminal()!=nullptr,TEXT("genuine Hub departure terminal exists"))) return;
        Pawn->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(100,0,110)); // 原终端交互范围内；不更改 CombatSession 或关号。
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Mode->GetNextLevelTerminal()->Interact(Pawn); UI->Sync(); Next(27); return;
    case 27:
        if(!Check(Flow->IsNextLevelConfirmationOpen()&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("Normal")),TEXT("formal departure exposes actual Normal difficulty action"))) return;
        if(Press(TEXT("Normal"))) Next(30,.2); return; // 单人正式难度按钮按原流程发 StartLevel，不直接写公开 Phase。
    case 30:
    {
        const UDemoCombatSession* Session=Mode->GetRunFlow()?Mode->GetRunFlow()->GetCombatSession():nullptr; // 正式启动提交的可撤销凭据。
        if(State->Phase!=EDemoPhase::Combat||State->LevelNumber!=1||State->EnemiesRemaining<=0||!Session||!Session->IsActive())
        { if(Now-StepStarted>45.) Finish(false,TEXT("legal first-combat departure did not finish its production preparation barrier")); return; }
        if(!Check(!UI->UsesWidgetMenu()&&!PC->IsMoveInputIgnored()&&!PC->IsLookInputIgnored(),TEXT("formal first Combat has active session, actual enemies and restored player input"))) return;
        bTrueCombatObserved=true;
        if(Equipment->GetActiveWeapon()&&!Equipment->GetActiveWeapon()->Config.bSupportsScope)
        {
            Equipment->CycleAim();
            if(!Check(Equipment->GetScopeLevel()==0,TEXT("genuine default pistol correctly rejects ADS instead of injecting sniper ownership"))) return;
            Notes.Add(TEXT("Actual sniper ADS is NOT RUN: the separately labelled temporary-permission fixture equipped only rifle and then restored active pistol; default-pistol ADS rejection passed."));
        }
        if(Capture(TEXT("09_B07_Combat"),TEXT("REAL_RUNTIME / legally started first Combat, active session and actual enemy count"),false)) Next(31,.2); return;
    }
    case 31:
    {
        ADemoEnemy* Nearest=nullptr; float Distance=MAX_flt; // 只选当前生产刷怪器生成的真实存活敌人，不生成攻击者。
        for(TActorIterator<ADemoEnemy> It(World);It;++It)
            if(It->IsAlive()&&It->IsCombatReady()&&FVector::DistSquared(It->GetActorLocation(),Pawn->GetActorLocation())<Distance)
            { Nearest=*It; Distance=FVector::DistSquared(It->GetActorLocation(),Pawn->GetActorLocation()); }
        if(Nearest)
        {
            RealShotTarget=Nearest; // 不延长真实敌人寿命；后续发射前重新核存活，已死则不伪造替代目标。
            const FVector Away=(Pawn->GetActorLocation()-Nearest->GetActorLocation()).GetSafeNormal2D(); // 只做夹具站位，让正常 AI 自己攻击；不调用伤害或伪造受击时间。
            FVector Stand=Nearest->GetActorLocation()+Away*210.f; Stand.Z=Pawn->GetActorLocation().Z;
            Pawn->TeleportTo(Stand,Pawn->GetActorRotation()); Pawn->GetCharacterMovement()->StopMovementImmediately();
            PC->SetControlRotation((Nearest->GetActorLocation()-Pawn->GetSharedViewLocation()).Rotation());
        }
        HealthBeforeDamageWait=Pawn->GetDemoAttributes()->GetHealth(); DamageWorldTimeBefore=Pawn->LastDamageTime;
        Next(310,.1); return; // 让真实摄像机/角色视向落定，再沿正常武器输入链请求一次发射。
    }
    case 310:
    {
        if(State->Phase==EDemoPhase::Combat&&!Pawn->IsCoopEliminated()&&Pawn->CanUseFirearmActions()&&Equipment->GetActiveWeapon())
        {
            if(RealShotTarget.IsValid()&&RealShotTarget->IsAlive()) PC->SetControlRotation((RealShotTarget->GetActorLocation()-Pawn->GetSharedViewLocation()).Rotation());
            const int32 Before=Equipment->GetActiveWeapon()->GetAmmo(); // 当前真实枪实例读数；请求与扣弹分别记录，不用扣弹推断碰撞。
            RealShotWorldTime=World->GetTimeSeconds(); bRealShotRequested=true;
            Equipment->StartFire(); Equipment->StopFire(); // 正式Enhanced Input包装器也转发这两个入口；单次离散输入，不引入自动重试、枪权或伤害参数。
            const int32 After=Equipment->GetActiveWeapon()?Equipment->GetActiveWeapon()->GetAmmo():Before;
            bRealShotSpentAmmo=After<Before;
            Notes.Add(FString::Printf(TEXT("REAL_RUNTIME one original StartFire/StopFire request: ammo before=%d after=%d; ammo consumption alone is not hit evidence."),Before,After));
        }
        else Notes.Add(TEXT("REAL_RUNTIME single-shot request NOT_RUN: original firearm action guard or actual active weapon unavailable."));
        Next(311,.01); return;
    }
    case 311:
    {
        const UDemoHitFeedbackComponent* Feedback=Pawn->FindComponentByClass<UDemoHitFeedbackComponent>(); // 实际拥有者缓存只读，不合成ShotId或Confirm回调。
        const float MarkerAge=Feedback?Feedback->GetMarkerAge():-1.f; // 回执事实与可见窗口分别采样；过期不等于未命中。
        const float MarkerDuration=Feedback?(Feedback->GetMarkerKind()==2?Feedback->KillMarkerSeconds:Feedback->HitMarkerSeconds):0.f;
        if(!bHitObservationDiagnosticLogged)
        {
            bHitObservationDiagnosticLogged=true;
            UE_LOG(LogFPSDemo,Display,TEXT("R02_S1_HIT_DIAG hitCount=%d markerAge=%.4f markerDuration=%.4f shotWorldTime=%.6f worldTime=%.6f shaderBusy=%d resourceJobs=%d stableAge=%.4f"),
                Feedback?Feedback->GetHits().Num():0,MarkerAge,MarkerDuration,RealShotWorldTime,static_cast<double>(World->GetTimeSeconds()),bShaderBusy,ResourceJobs,
                ResourceStableSince>=0.?Now-ResourceStableSince:-1.); // 仅本次311首次打印一次，不反复Tick日志、不改标记寿命。
        }
        if(bRealShotRequested&&Feedback)
        {
            for(const FDemoHitFeedbackDisplay& Display:Feedback->GetHits())
                if(Display.Hit.ShotId.IsValid()&&Display.Hit.Damage>0.f&&Display.StartedAt>=RealShotWorldTime-.001
                    )
                {
                    if(!bObservedRealHit)
                    {
                        bObservedRealHit=true; // 新ShotId/正伤害/本次发射后的StartedAt已证明真实回执，不能因Marker过期撤销此事实。
                        Notes.Add(FString::Printf(TEXT("REAL_RUNTIME new actual hit receipt observed: ShotId=%s target=%u actual HP loss=%.3f marker_age=%.4f kind=%d; receipt observation is independent of visible screenshot capture."),
                            *Display.Hit.ShotId.ToString(EGuidFormats::DigitsWithHyphens),Display.Hit.TargetId,Display.Hit.Damage,MarkerAge,Feedback->GetMarkerKind()));
                    }
                    if(!bShaderBusy&&ResourceJobs==0&&State->Phase==EDemoPhase::Combat&&!UI->UsesWidgetMenu()&&!Flow->HasBlockingOverlay()
                        &&MarkerAge>=0.f&&MarkerAge<MarkerDuration)
                    {
                        if(Capture(TEXT("10a_B07_Hit_REAL"),TEXT("REAL_RUNTIME / new real positive-damage ShotId receipt; actual marker visible and shader/asset compilation idle at normal screenshot request"),false)) Next(32,.01);
                        return;
                    }
                }
        }
        if(!bRealShotRequested||Now-StepStarted>2.||(bObservedRealHit&&MarkerAge>=MarkerDuration))
        {
            Notes.Add(bObservedRealHit
                ?TEXT("REAL_RUNTIME positive-damage receipt WAS OBSERVED; visible hit screenshot NOT_CAPTURED before marker expiry/resource readiness. Marker expiry is not treated as no hit.")
                :TEXT("REAL_RUNTIME new positive-damage receipt NOT_OBSERVED within the bounded single-shot wait; no hit screenshot or hit PASS is claimed."));
            Assertions.Add(bObservedRealHit?TEXT("NOT_OBSERVED: visible real-hit screenshot only; new positive-damage receipt was observed.")
                :TEXT("NOT_OBSERVED: new positive-damage receipt and visible real-hit screenshot within bounded two-second wait."));
            UE_LOG(LogFPSDemo,Display,TEXT("R02_S1_HIT_NOT_CAPTURED requested=%d spent_ammo=%d receipt_observed=%d marker_age=%.4f shaderBusy=%d resourceJobs=%d"),
                bRealShotRequested,bRealShotSpentAmmo,bObservedRealHit,MarkerAge,bShaderBusy,ResourceJobs);
            Next(32,.01);
        }
        return;
    }
    case 32:
        if(State->Phase!=EDemoPhase::Combat||Pawn->IsCoopEliminated()) { Finish(false,TEXT("real combat ended before bounded HUD/damage observation completed")); return; }
        if(Pawn->GetDemoAttributes()->GetHealth()<HealthBeforeDamageWait&&Pawn->LastDamageTime>DamageWorldTimeBefore)
        {
            bObservedRealDamage=true;
            Notes.Add(TEXT("REAL_RUNTIME damage: actual HP fell and actual LastDamageTime advanced while normal first Combat AI ran; no fixture GE had yet been applied."));
            if(Capture(TEXT("10_B07_Damage_REAL"),TEXT("REAL_RUNTIME / naturally committed health loss during actual first Combat; no forced damage"),false)) Next(33,.1); return;
        }
        if(Now-StepStarted>12.) { Notes.Add(TEXT("Natural first-Combat damage screenshot NOT OBSERVED within the bounded 12-second wait; synthetic low-health frame is separate.")); Next(33,.1); }
        return;
    case 33:
    {
        bSyntheticStarted=true;
        Notes.Add(TEXT("SYNTHETIC_PRESENTATION begins here: deactivate current AI and conditionally apply a self-source environment GE only if current HP exceeds the low-health sample target; no real-hit or encounter claim."));
        for(TActorIterator<ADemoEnemy> It(World);It;++It) { It->DeactivateSpawn(); It->SetActorTickEnabled(false); } // 仅自有审查进程后置视觉夹具，防 AI 在样本期间击杀；不保存或继续真实回归。
        const float Current=Pawn->GetDemoAttributes()->GetHealth(); // 实际当前 HP，夹具不写 LastDamageTime 或 HUD 数值。
        const float Target=FMath::Min(28.f,Pawn->GetDemoAttributes()->GetMaxHealth()*.28f);
        if(Current>Target)
        {
            bSyntheticLowHealthGEApplied=DemoEffects::ApplyDamage(Pawn->GetAbilitySystemComponent(),Pawn->GetAbilitySystemComponent(),Current-Target,
                EDemoPlayerDamageKind::Environment,Pawn->GetActorLocation());
            if(!Check(bSyntheticLowHealthGEApplied,TEXT("conditional SYNTHETIC low-health self-source environment GE submitted successfully"))) return;
            Notes.Add(TEXT("Low-health frame: the conditional self-source environment GE was actually submitted; displayed HP still comes from the real ASC."));
        }
        else Notes.Add(TEXT("Low-health frame: actual HP was already at or below target; NO low-health fixture GE was submitted."));
        Next(34,.1); return;
    }
    case 34:
        if(!Check(Pawn->GetDemoAttributes()->GetHealth()>0&&Pawn->GetDemoAttributes()->GetHealth()<=Pawn->GetDemoAttributes()->GetMaxHealth()*.3f,TEXT("SYNTHETIC low-health sample reads actual positive health under 30 percent"))) return;
        if(Capture(TEXT("11_B07_LowHealth_SYNTHETIC"),bSyntheticLowHealthGEApplied
            ?TEXT("SYNTHETIC_PRESENTATION / conditional self-source environment GE actually submitted; real ASC/HUD, no enemy hit claim")
            :TEXT("SYNTHETIC_PRESENTATION / AI deactivated, actual HP already low; no additional fixture GE submitted"),false)) Next(35); return;
    case 35:
    {
        FDemoEnemySpawnStats Stats; FString Error; // 使用项目正式第10关 Boss 模板解析，样本不计入战役人数/奖励。
        if(!Check(Mode->GetSpawnStats(10,true,Stats,Error),TEXT("SYNTHETIC Boss presentation resolves actual project level-10 template: ")+Error)) return;
        Stats.CoinReward=0; // 显式非战役样本无奖励；不会绑定正式刷怪器死亡委托。
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 样本远离实际视线，避免投影血条和实体挡住真实场景。
        PresentationBoss=World->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),Pawn->GetActorLocation()+FVector(20000,0,0),FRotator::ZeroRotator,Params);
        if(!Check(PresentationBoss.IsValid(),TEXT("SYNTHETIC isolated Boss instance created"))) return;
        PresentationBoss->Configure(Stats); PresentationBoss->DeactivateSpawn(); PresentationBoss->SetActorTickEnabled(false); PresentationBoss->SetActorHiddenInGame(true);
        if(!Check(PresentationBoss->IsBoss()&&PresentationBoss->IsSpawnInitialized()&&PresentationBoss->IsAlive(),TEXT("SYNTHETIC Boss public identity/health are actual configured Actor values"))) return;
        Next(36); return;
    }
    case 36:
        if(Capture(TEXT("12_B07_Boss_SYNTHETIC"),TEXT("SYNTHETIC_PRESENTATION / hidden inactive level-10-template Boss supplies real public HUD fields; no actual Boss encounter"),false)) Next(37); return;
    case 37:
        Pawn->SetCoopEliminated(true); bForcedEliminated=true; // 明确仅表现夹具；无假队友、无多人连接、无阵亡裁决或清关复活声明。
        Notes.Add(TEXT("SYNTHETIC spectator waiting: only self eliminated flag is staged; no teammate, player count, death reward, network or respawn outcome is fabricated."));
        Next(38); return;
    case 38:
        if(Capture(TEXT("13_B07_SpectatorWaiting_SYNTHETIC"),TEXT("SYNTHETIC_PRESENTATION / spectator waiting branch with no teammate; not a cooperative runtime test"),false)) Next(39); return;
    case 39:
        Finish(true,TEXT("bounded genuine B04 transactions/first-Combat runtime checks passed; synthetic B07 cases separately labelled")); return;
    default:
        Finish(false,TEXT("unexpected R02 S1 audit stage")); return;
    }
}
#endif
