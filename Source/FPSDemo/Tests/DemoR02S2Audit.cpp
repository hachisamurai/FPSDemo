// 职责：R02 S2 正式大厅/暂停/核心与奖励运行证据；测试GE清关、后置Boss表现样本明确分类，不冒充玩家实战。
// 依赖：正式 MenuFlow、CommonUI 栈、Editor GI 临时槽与正常 PNG；不写生产字段、不伪造保存、不点击正式退出动作。
#include "Tests/DemoR02S2Audit.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "AI/DemoEnemy.h"
#include "AssetCompilingManager.h"
#include "Characters/DemoCharacter.h"
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
#include "Kismet/GameplayStatics.h"
#include "Interaction/DemoInteractable.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerState.h"
#include "Save/DemoRunSave.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Tests/DemoUILayoutChecks.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoHUD.h"
#include "UI/R02/DemoR02EditorialButton.h"
#include "UI/Screens/DemoTerminalScreen.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Progression/Builds/DemoRunBuildTypes.h" // 出发页预览断言用可信核心目录ID。

namespace
{
    /** 仅显式独立测试进程的纯值上下文；正式 OpenLevel 不携带 UObject 强引用或生产状态。 */
    struct FR02S2Session
    {
        TWeakObjectPtr<UGameInstance> Owner; // 只识别首次真实 GI，拒绝同进程其他 GI 混入，不保持其存活。
        FDelegateHandle Watchdog; // 一次注册的进程 GT 末帧租约，正式旅行不重置绝对截止时间。
        FString Directory,PendingPNG; // 显式新目录和唯一在途正常 PNG，绝不覆盖旧证据。
        TArray<FString> PNGs,Scopes,Assertions,Notes; // 只记录本次有限真实事实；不含账号、凭据或他人进度。
        FString CreatedSlotKey; // 初次真实创建槽的实例键，仅用于旅行后相等校验，不写入报告或生产状态。
        FDateTime CreatedAt; // 首次真实创建时间；返回大厅后必须原样保留。
        double Started=0.,Deadline=0.,StepStarted=0.,ResourceStableSince=-1.,ShotStarted=0.; // 单调真实秒；暂停和旅行不重置 Started。
        uint64 ShotFrame=0,SaveSerialBeforeReturn=0,SaveSerialBeforeCancel=0; // 截图帧号及真实 GI 保存序号，只读对照。
        uint64 SaveSerialBeforeLobbyBridge=0; // 大厅真实 UI.LobbySlot0 桥接前的 GI 保存序号，独立于原 Saves 入口取消对照。
        uint64 SaveSerialBeforeCorePreview=0,SaveSerialBeforeReward=0,SaveSerialAfterReward=0; // 实际核心预览/奖励前后保存序号；预览和拒绝重复点击均不得推进。
        FName CoreIdBeforePreview; // 核心预览前真实已提交ID；左右预览不应改变它。
        FDemoPlayerProgressSnapshot RewardBefore,RewardAfter; // 本人领取前后真实值快照，只读对照，绝不回写生产组件。
        float RewardDamageBefore=0.f,RewardDamageAfter=0.f,RewardExpectedGain=0.f; // 实际ASC伤害加值及同次OwnerOffer增量，单位HP，不硬编码奖励数值。
        FString FixtureRunId; // 合法第一关开始时从当前实际槽冻结的本轮ID，仅相等校验，不写报告或冒充新凭据。
        int32 FixtureEnemyDeaths=0,FixtureDamageSubmissions=0,FixtureInitialEnemies=0,FixtureInitialTeamKills=0,FixtureInitialPistolChallenge=0,FrozenSampleActors=0; // 原Kills是每人的团队累计信用，环境GE死亡不声明个人最后一击或有效枪击。
        int32 ClearPhase=INDEX_NONE,ClearLevel=0,ClearObjectiveState=INDEX_NONE,ClearObjectiveRemaining=-1,ClearSpawnRemaining=-1,ClearSpawnAlive=-1,ClearTeamKills=0,ClearPistolChallenge=INDEX_NONE; // Stage26先完整记录所有真实分量，再逐项断言，失败也能复核。
        int32 Stage=0,Passed=0,WorldCount=0,CompletedPNGs=0,ViewportWidth=0,ViewportHeight=0,ShotWidth=0,ShotHeight=0; // 有限计数和实际视口物理像素。
        double PausedWorldTime=0.,PauseObservedWorldTime=0.; // GetTimeSeconds原生double世界秒；禁止缩为float，否则转换误差会冒充暂停推进。Observed只记录检查帧事实。
        uint64 PauseBaselineFrame=0,PauseObservedFrame=0; // 本次同World暂停基准/检查帧号，证明跨正常截图帧验证，不能同帧自证。
        bool bInitialized=false,bFinished=false,bCreatedThroughUI=false,bResumedByEnter=false,bReturnedThroughUI=false; // 生命周期及分别观测到的生产流程事实。
        bool bLobbySlot0BridgeObserved=false; // 真实直达原确认、默认 Enter 取消无保存变化并经 Back 回大厅后才记 true；不代表 Slot1/2 已点击验证。
        bool bPausedWorldFrozenObserved=false; // 同一真实World跨帧double原值完全相等且World/PC均暂停后才记true，不靠容差掩盖推进。
        bool bLegalCombatObserved=false,bDamageGEFixtureUsed=false,bFixtureClearObserved=false,bRewardMenuObserved=false,bRewardChosenOnce=false; // 合法出发、测试GE、正式清关奖励与单次领取分开记事实。
        bool bSyntheticBossSampleStarted=false,bSyntheticBossCaptured=false,bCorePreviewObserved=false; // Boss为显式非遭遇样本；核心预览检查不授予选择权限。
        bool bClearObservationRecorded=false,bClearSpawnerCompleteLatch=false,bClearSameSession=false,bClearSessionActive=false; // Spawner完成锁存是可重置的诊断值；业务验收取持久目标Completed/Reward及旧会话撤销。
    } Session; // 不复用到普通游戏；只有显式 Editor -game 入口访问。

    /** 将本次纯值证据一次写盘并解除进程 watchdog；无 World 仍可报告旅行卡死，再正常结束自有进程。 */
    void FinishSession(bool Success,const FString& Reason)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] R02S2Audit.FinishSession"));
        if(Session.bFinished) return;
        Session.bFinished=true;
        if(Session.Watchdog.IsValid()) FCoreDelegates::OnEndFrame.Remove(Session.Watchdog);
        Session.Watchdog.Reset();
        TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 同步序列化本次证据，不持有引擎对象。
        Report->SetBoolField(TEXT("success"),Success);
        Report->SetStringField(TEXT("status"),Success?TEXT("PASS_BOUNDED_S2_SINGLE_PLAYER"):TEXT("FAIL"));
        Report->SetStringField(TEXT("reason"),Reason);
        Report->SetStringField(TEXT("scope"),TEXT("REAL_RUNTIME: genuine initial Lobby, actual UI.LobbySlot0/original empty-slot confirmation and safe cancellation without saving, original StartGame/Saves/CreateYes travel, single-player Pause/focused Enter resume, original PauseLobby save-gated return and existing checkpoint. Resume this actual slot, preview core 1 then 0 without CoreId/save changes, formally choose Core0 and legally depart into first Combat. TEST_DAMAGE_GE_FIXTURE: terminate only live current-session production enemies through actual ASC Damage GE; original spawner/objective/Flow produce first-level clearance and actual C03 reward. This is a test-driven clear, not player combat. Actual Upgrade0 choice and rejected repeated widget actions must leave one reward consumed. SYNTHETIC_PRESENTATION: legal second-Combat context with test AI stopped and an explicit hidden inactive no-reward level-10-template Boss supplies actual public HUD fields for C07; not a real Boss encounter or level-10 completion. No direct Slot1/2 clicks, seven-core selection sweep, server RPC replay proof, reward branches at 3/7, multiplayer, save-failure injection, cloud, settings, full combat/ten-level completion, production exit-menu, Cook or Shipping acceptance. Device INI isolation, independent PNG decoding and visual review remain launcher/reviewer responsibilities."));
        Report->SetNumberField(TEXT("stage"),Session.Stage);
        Report->SetNumberField(TEXT("passed_assertions"),Session.Passed);
        Report->SetNumberField(TEXT("elapsed_real_seconds"),FPlatformTime::Seconds()-Session.Started);
        Report->SetNumberField(TEXT("worlds_observed"),Session.WorldCount);
        Report->SetNumberField(TEXT("png_completed_count"),Session.CompletedPNGs);
        Report->SetNumberField(TEXT("viewport_width"),Session.ViewportWidth);
        Report->SetNumberField(TEXT("viewport_height"),Session.ViewportHeight);
        Report->SetBoolField(TEXT("real_ui_slot_creation_observed"),Session.bCreatedThroughUI);
        Report->SetBoolField(TEXT("real_lobby_slot0_bridge_observed"),Session.bLobbySlot0BridgeObserved);
        Report->SetBoolField(TEXT("real_focused_enter_resume_observed"),Session.bResumedByEnter);
        Report->SetBoolField(TEXT("real_paused_world_time_frozen_observed"),Session.bPausedWorldFrozenObserved);
        Report->SetNumberField(TEXT("pause_baseline_world_seconds"),Session.PausedWorldTime);
        Report->SetNumberField(TEXT("pause_observed_world_seconds"),Session.PauseObservedWorldTime);
        Report->SetNumberField(TEXT("pause_observed_delta_seconds"),Session.PauseObservedWorldTime-Session.PausedWorldTime);
        Report->SetNumberField(TEXT("pause_baseline_frame"),static_cast<double>(Session.PauseBaselineFrame));
        Report->SetNumberField(TEXT("pause_observed_frame"),static_cast<double>(Session.PauseObservedFrame));
        Report->SetBoolField(TEXT("real_save_gated_return_observed"),Session.bReturnedThroughUI);
        Report->SetBoolField(TEXT("real_core_preview_without_commit_observed"),Session.bCorePreviewObserved);
        Report->SetBoolField(TEXT("real_combat_entry_observed"),Session.bLegalCombatObserved);
        Report->SetBoolField(TEXT("test_damage_ge_fixture_used"),Session.bDamageGEFixtureUsed);
        Report->SetBoolField(TEXT("test_damage_ge_clear_observed"),Session.bFixtureClearObserved);
        Report->SetNumberField(TEXT("test_damage_ge_submissions"),Session.FixtureDamageSubmissions);
        Report->SetNumberField(TEXT("test_damage_ge_enemy_deaths_observed"),Session.FixtureEnemyDeaths);
        Report->SetBoolField(TEXT("clear_observation_recorded"),Session.bClearObservationRecorded);
        Report->SetNumberField(TEXT("clear_observed_phase"),Session.ClearPhase);
        Report->SetNumberField(TEXT("clear_observed_level"),Session.ClearLevel);
        Report->SetNumberField(TEXT("clear_observed_objective_state"),Session.ClearObjectiveState);
        Report->SetNumberField(TEXT("clear_observed_objective_remaining"),Session.ClearObjectiveRemaining);
        Report->SetNumberField(TEXT("clear_observed_spawn_remaining"),Session.ClearSpawnRemaining);
        Report->SetNumberField(TEXT("clear_observed_spawn_alive"),Session.ClearSpawnAlive);
        Report->SetBoolField(TEXT("clear_observed_spawn_complete_latch"),Session.bClearSpawnerCompleteLatch);
        Report->SetBoolField(TEXT("clear_observed_same_combat_session"),Session.bClearSameSession);
        Report->SetBoolField(TEXT("clear_observed_combat_session_active"),Session.bClearSessionActive);
        Report->SetNumberField(TEXT("clear_initial_team_kill_credit"),Session.FixtureInitialTeamKills);
        Report->SetNumberField(TEXT("clear_observed_team_kill_credit"),Session.ClearTeamKills);
        Report->SetNumberField(TEXT("clear_initial_pistol_challenge"),Session.FixtureInitialPistolChallenge);
        Report->SetNumberField(TEXT("clear_observed_pistol_challenge"),Session.ClearPistolChallenge);
        Report->SetBoolField(TEXT("actual_reward_menu_observed"),Session.bRewardMenuObserved);
        Report->SetBoolField(TEXT("actual_reward_chosen_once_observed"),Session.bRewardChosenOnce);
        Report->SetBoolField(TEXT("synthetic_presentation_samples"),Session.bSyntheticBossSampleStarted);
        Report->SetBoolField(TEXT("synthetic_boss_sample_captured"),Session.bSyntheticBossCaptured);
        Report->SetNumberField(TEXT("synthetic_ai_actors_stopped"),Session.FrozenSampleActors);
        TArray<TSharedPtr<FJsonValue>> Images,Scopes,Checks,Notes; // 与请求顺序一致的证据列表，不嵌入 PNG 数据。
        for(const FString& Value:Session.PNGs) Images.Add(MakeShared<FJsonValueString>(Value));
        for(const FString& Value:Session.Scopes) Scopes.Add(MakeShared<FJsonValueString>(Value));
        for(const FString& Value:Session.Assertions) Checks.Add(MakeShared<FJsonValueString>(Value));
        for(const FString& Value:Session.Notes) Notes.Add(MakeShared<FJsonValueString>(Value));
        Report->SetArrayField(TEXT("pngs"),Images); Report->SetArrayField(TEXT("capture_scopes"),Scopes);
        Report->SetArrayField(TEXT("assertions"),Checks); Report->SetArrayField(TEXT("notes"),Notes);
        FString Text; // 本次 UTF-8 JSON，不改项目配置或正式存档。
        const FString Path=Session.Directory/TEXT("audit.json"); // 结束前再次拒绝外部迟到的同名报告。
        const bool Written=!IFileManager::Get().FileExists(*Path)&&FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text)) // 只有全新目标且完整序列化/落盘才记录报告成功。
            &&FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogFPSDemo,Display,TEXT("%s stage=%d assertions=%d directory=%s report_written=%d reason=%s"),
            Success&&Written?TEXT("R02_S2_AUDIT_PASS"):TEXT("R02_S2_AUDIT_FAIL"),Session.Stage,Session.Passed,*Session.Directory,Written,*Reason);
        FPlatformMisc::RequestExitWithStatus(false,Success&&Written?0:1); // 仅结束显式独立测试进程；正常 GI.Deinitialize 清理临时槽，不点击生产退出菜单或跳过保存门禁。
    }

    /** GT 引擎末帧只核首次启动后的 600 真实秒绝对截止；旅行/暂停均不续租，只失败一次。 */
    void ProcessWatchdog()
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] R02S2Audit.ProcessWatchdog"));
        if(Session.bInitialized&&!Session.bFinished&&FPlatformTime::Seconds()-Session.Started>600.)
            FinishSession(false,TEXT("600-second independent end-frame deadline, including paused World and travel gaps"));
    }

    /** 检查正常 PNG 签名、IHDR 物理尺寸及结束块；不替代启动器的独立解码/艺术验收。 */
    bool ReadNormalPNGSize(const FString& Path,int32& Width,int32& Height)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] R02S2Audit.ReadNormalPNGSize"));
        TArray<uint8> Bytes; // 只借用本次已写正常截图，不读取或拼接渲染目标。
        if(!FFileHelper::LoadFileToArray(Bytes,*Path)||Bytes.Num()<45) return false;
        const uint8 Signature[]={137,80,78,71,13,10,26,10}; // PNG 固定文件签名。
        if(FMemory::Memcmp(Bytes.GetData(),Signature,8)!=0||FMemory::Memcmp(Bytes.GetData()+12,"IHDR",4)!=0
            ||FMemory::Memcmp(Bytes.GetData()+Bytes.Num()-8,"IEND",4)!=0) return false;
        const uint32 W=(uint32(Bytes[16])<<24)|(uint32(Bytes[17])<<16)|(uint32(Bytes[18])<<8)|uint32(Bytes[19]); // IHDR 网络字节序像素宽。
        const uint32 H=(uint32(Bytes[20])<<24)|(uint32(Bytes[21])<<16)|(uint32(Bytes[22])<<8)|uint32(Bytes[23]); // IHDR 网络字节序像素高。
        if(W==0||H==0||W>MAX_int32||H>MAX_int32) return false;
        Width=int32(W); Height=int32(H); return true;
    }
}
#endif

bool UDemoR02S2Audit::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const UWorld* World=Cast<UWorld>(Outer); // 仅同步核创建对象，不缓存其他 World。
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone
        &&World->GetOutermost()->GetName()==TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")
        &&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoR02S2Audit"))
        &&!FParse::Param(FCommandLine::Get(),TEXT("DemoR02S1Audit"))&&!FParse::Param(FCommandLine::Get(),TEXT("DemoR02StyleBoard"))
        &&!FParse::Param(FCommandLine::Get(),TEXT("DemoCommonUITest"))&&!FParse::Param(FCommandLine::Get(),TEXT("DemoSessionUITest"))
        &&!FParse::Param(FCommandLine::Get(),TEXT("DemoOutlineProductionSmoke"));
#else
    return false;
#endif
}

bool UDemoR02S2Audit::DoesSupportWorldType(EWorldType::Type Type) const
{
    DEMO_LOG_CALL();
    return WITH_EDITOR&&Type==EWorldType::Game;
}

void UDemoR02S2Audit::OnWorldBeginPlay(UWorld& World)
{
    DEMO_LOG_CALL();
    Super::OnWorldBeginPlay(World);
#if WITH_EDITOR
    if(!Session.bInitialized)
    {
        FString Directory; // 显式命令行输出路径，不借用用户截图目录或设备设置。
        if(!FParse::Value(FCommandLine::Get(),TEXT("DemoR02S2Output="),Directory)||Directory.IsEmpty())
        { UE_LOG(LogFPSDemo,Error,TEXT("R02_S2_AUDIT_FAIL explicit -DemoR02S2Output is required")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        Directory=FPaths::ConvertRelativePathToFull(Directory);
        TArray<FString> ExistingPNGs; // 允许启动器预置日志/隔离 INI，但不覆盖任何旧 PNG 或报告。
        IFileManager::Get().FindFiles(ExistingPNGs,*(Directory/TEXT("*.png")),true,false);
        if(IFileManager::Get().FileExists(*(Directory/TEXT("audit.json")))||!ExistingPNGs.IsEmpty())
        { UE_LOG(LogFPSDemo,Error,TEXT("R02_S2_AUDIT_FAIL refusing existing audit.json/PNG directory=%s"),*Directory); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        if(!IFileManager::Get().MakeDirectory(*Directory,true))
        { UE_LOG(LogFPSDemo,Error,TEXT("R02_S2_AUDIT_FAIL cannot create output directory=%s"),*Directory); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        Session.Directory=Directory; Session.Owner=World.GetGameInstance(); Session.Started=FPlatformTime::Seconds();
        Session.StepStarted=Session.Started; Session.bInitialized=true;
        Session.Notes.Add(TEXT("Launcher must use a fresh independent process and fresh output directory per resolution, plus an independent -GameUserSettingsINI. Harness does not modify resolution or device preferences."));
        Session.Notes.Add(TEXT("Editor UDemoRunSaves owns GI-unique local temporary slots; harness does not write slot fields or invoke cloud. Normal GI teardown cleans this process's temporary progress."));
        Session.Notes.Add(TEXT("All PNGs use FScreenshotRequest with UI. Harness checks signature/IHDR/IEND and actual viewport dimensions; independent decoding and image review remain required. No QuitGame/PauseQuit action is executed. After reporting, the explicit test harness requests normal process exit so GI.Deinitialize cleans its temporary slots; launcher must wait for process exit, not force-kill it."));
        Session.Watchdog=FCoreDelegates::OnEndFrame.AddStatic(&ProcessWatchdog); // 无捕获 GT 静态回调；只访问本进程纯值上下文，跨 World 维持一次绝对截止。
    }
    if(Session.bFinished) return;
    if(Session.Owner.Get()!=World.GetGameInstance())
    { UE_LOG(LogFPSDemo,Error,TEXT("R02_S2_AUDIT_FAIL refusing a second GameInstance in the same audit process")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    bOwnsSession=true; ++Session.WorldCount; Session.ResourceStableSince=-1.;
    FrameHandle=FCoreDelegates::OnEndFrame.AddUObject(this,&UDemoR02S2Audit::OnEndFrame); // GT 弱 UObject 回调；暂停仍推进，旧 World 卸载精确移除。
    UE_LOG(LogFPSDemo,Log,TEXT("R02_S2_WORLD count=%d stage=%d"),Session.WorldCount,Session.Stage);
#endif
}

void UDemoR02S2Audit::Deinitialize()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if(FrameHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(FrameHandle);
    FrameHandle.Reset();
    RestorePresentationFixture();
    if(FilteredLocalPlayer.IsValid())
        if(UCommonInputSubsystem* Input=FilteredLocalPlayer->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("R02S2Audit"),false);
    FilteredLocalPlayer.Reset(); PC.Reset(); PauseSampleWorld.Reset(); FixtureCombatSession.Reset(); bOwnsSession=false;
#endif
    Super::Deinitialize();
}

#if WITH_EDITOR
bool UDemoR02S2Audit::Check(bool Value,const FString& Reason)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Display,TEXT("R02_S2_CHECK_%s stage=%d %s"),Value?TEXT("PASS"):TEXT("FAIL"),Session.Stage,*Reason);
    Session.Assertions.Add((Value?TEXT("PASS: "):TEXT("FAIL: "))+Reason);
    if(Value) ++Session.Passed; else Finish(false,Reason);
    return Value;
}

bool UDemoR02S2Audit::Press(FName Action)
{
    DEMO_LOG_CALL();
    UDemoUIManager* UI=PC.IsValid()?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 当前真实 PC 的唯一协调器。
    if(UI) UI->Sync();
    const bool Accepted=UI&&UI->PressActionForTest(Action); // 沿实际顶层按钮点击链；结果仍由下一步独立读取。
    if(UI) UI->Sync();
    return Check(Accepted,TEXT("actual top-screen CommonUI button accepted: ")+Action.ToString());
}

void UDemoR02S2Audit::Key(FKey Input)
{
    DEMO_LOG_CALL();
    if(!FSlateApplication::IsInitialized()) { Check(false,TEXT("actual Slate application is unavailable for key input")); return; }
    FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Input,FModifierKeysState(),0,false,0,0));
    FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Input,FModifierKeysState(),0,false,0,0));
}

void UDemoR02S2Audit::Next(int32 NextStage,double Delay)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Log,TEXT("R02_S2_STAGE %d -> %d delay=%.2f"),Session.Stage,NextStage,Delay);
    Session.Stage=NextStage; Session.StepStarted=FPlatformTime::Seconds(); Session.Deadline=Session.StepStarted+Delay;
}

bool UDemoR02S2Audit::CheckR02Page(FName Action)
{
    DEMO_LOG_CALL();
    UDemoUIManager* UI=PC.IsValid()?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 当前真实栈，不另建屏幕。
    UDemoMenuScreen* Screen=UI?UI->GetActiveScreen():nullptr; // 当前可交互活页，不是拆卸后的旧实例。
    UDemoMenuButton* Button=Screen?Screen->FindAction(Action):nullptr; // 代表动作由实际 WidgetTree 拥有。
    return Check(Screen&&Screen->IsActivated()&&Screen->IsUsingR02Presentation(),TEXT("actual activated production screen uses R02 presentation"))
        &&Check(Button&&Button->GetIsEnabled()&&Button->IsA<UDemoR02EditorialButton>()&&Button->GetTypedOuter<UCommonActivatableWidget>()==Screen,
            TEXT("enabled actual R02 button retains current Activatable in its Outer chain"))
        &&Check(Screen->HasUserFocusedDescendants(PC.Get())||Screen->HasAnyUserFocus(),TEXT("actual active R02 page owns Slate focus without forced injection"));
}

bool UDemoR02S2Audit::Capture(const FString& Name,const FString& Scope,bool ValidateLayout)
{
    DEMO_LOG_CALL();
    if(!Check(Session.PendingPNG.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested(),TEXT("normal screenshot has no competing in-flight request"))) return false;
    const FString Path=Session.Directory/(Name+TEXT(".png")); // 全新固定短名，不覆盖或重用历史帧。
    if(!Check(!IFileManager::Get().FileExists(*Path),TEXT("normal screenshot destination is fresh: ")+Path)) return false;
    UDemoUIManager* UI=PC.IsValid()?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 本帧真实活页，用共享严格几何检查。
    if(ValidateLayout&&UI&&UI->GetActiveScreen()) DemoUILayoutChecks::Queue(UI->GetActiveScreen());
    PC->GetViewportSize(Session.ShotWidth,Session.ShotHeight);
    Session.PendingPNG=Path; Session.PNGs.Add(Path); Session.Scopes.Add(Scope);
    Session.ShotFrame=GFrameCounter; Session.ShotStarted=FPlatformTime::Seconds();
    FScreenshotRequest::RequestScreenshot(Path,true,false); // 正常屏幕截图含 Slate/Canvas；不渲染替身场景、不读取 RT。
    UE_LOG(LogFPSDemo,Log,TEXT("R02_S2_CAPTURE scope=%s file=%s"),*Scope,*Path);
    return true;
}

void UDemoR02S2Audit::FreezeBossSampleActors()
{
    DEMO_LOG_TICK();
    if(!Session.bSyntheticBossSampleStarted||!GetWorld()) return;
    for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It)
    {
        if(!It->IsAlive()||*It==PresentationBoss.Get()) continue;
        if(It->IsCombatReady()) It->DeactivateSpawn(); // 仅显式后置样本；不广播死亡、不给奖励、不取消生产刷怪器，随后正常退出测试进程。
        if(It->IsActorTickEnabled()) { It->SetActorTickEnabled(false); ++Session.FrozenSampleActors; }
    }
}

void UDemoR02S2Audit::RestorePresentationFixture()
{
    DEMO_LOG_CALL();
    if(PresentationBoss.IsValid()) { PresentationBoss->DeactivateSpawn(); PresentationBoss->Destroy(); }
    PresentationBoss.Reset(); ConsumedRewardButton.Reset(); // 仅自有样本/弱控件；正式敌人由测试进程正常World卸载，不伪造战局恢复。
}

void UDemoR02S2Audit::Finish(bool Success,const FString& Reason)
{
    DEMO_LOG_CALL();
    if(FrameHandle.IsValid()) FCoreDelegates::OnEndFrame.Remove(FrameHandle);
    FrameHandle.Reset();
    RestorePresentationFixture();
    if(FilteredLocalPlayer.IsValid())
        if(UCommonInputSubsystem* Input=FilteredLocalPlayer->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("R02S2Audit"),false);
    FilteredLocalPlayer.Reset();
    FinishSession(Success,Reason);
}

void UDemoR02S2Audit::OnEndFrame()
{
    DEMO_LOG_TICK();
    UWorld* World=GetWorld(); // 每帧借用本 World，旅行后旧弱回调不再驱动。
    if(!bOwnsSession||!World||Session.Owner.Get()!=World->GetGameInstance()) return;
    if(Session.bFinished) { Finish(false,TEXT("session already ended by independent watchdog")); return; } // watchdog 先广播时也精确释放本 World 的手柄过滤/弱委托，不重写既有报告。
    FreezeBossSampleActors(); // 只在明确样本阶段执行；shader/资产等待期间新生成的真实AI也不能攻击后置表现样本。
    const double Now=FPlatformTime::Seconds(); // 真实秒在单人暂停下仍推进。
    FString LayoutError; // 上次截图已完成 Slate 绘制后的严格几何结果。
    const int32 LayoutResult=DemoUILayoutChecks::ProcessPending(LayoutError);
    if(LayoutResult&&!Check(LayoutResult>0,TEXT("rendered CommonUI geometry: ")+LayoutError)) return;
    if(!Session.PendingPNG.IsEmpty())
    {
        if(GFrameCounter>Session.ShotFrame&&IFileManager::Get().FileSize(*Session.PendingPNG)>1000)
        {
            int32 Width=0,Height=0; // 从本次新文件 IHDR 独立读回，不用请求参数冒充完成。
            if(!Check(ReadNormalPNGSize(Session.PendingPNG,Width,Height)&&Width==Session.ShotWidth&&Height==Session.ShotHeight,
                FString::Printf(TEXT("new normal PNG completed at actual viewport %dx%d: %s"),Session.ShotWidth,Session.ShotHeight,*Session.PendingPNG))) return;
            if(FPaths::GetCleanFilename(Session.PendingPNG)==TEXT("08_C07_BossHUD_Sample.png")) Session.bSyntheticBossCaptured=true; // 正常PNG真正落盘才记样本截图，不能把请求当交付。
            ++Session.CompletedPNGs; Session.PendingPNG.Reset();
        }
        else if(Now-Session.ShotStarted>8.) Finish(false,TEXT("normal PNG did not complete within eight real seconds"));
        return;
    }
    const bool ShaderBusy=GShaderCompilingManager&&GShaderCompilingManager->IsCompiling(); // 等真实 shader 空闲，不能把占位资源截图交付。
    const int32 AssetJobs=FAssetCompilingManager::Get().GetNumRemainingAssets(); // 等真实资产编译完成，不主动修改其任务。
    if(ShaderBusy||AssetJobs>0) { Session.ResourceStableSince=-1.; return; }
    if(Session.ResourceStableSince<0.) Session.ResourceStableSince=Now;
    if(Now-Session.ResourceStableSince<.5||Now<Session.Deadline) return;
    ADemoPlayerController* Controller=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(World,0)); // 每个 World 重取实际拥有者。
    ADemoCharacter* Pawn=Controller?Cast<ADemoCharacter>(Controller->GetPawn()):nullptr; // 当前本机真实Pawn，旅行后不借旧Avatar。
    ADemoGameState* State=World->GetGameState<ADemoGameState>(); // 当前正式复制阶段只读，不由夹具设置。
    AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>(); // Standalone真实权威组合根，不另建Flow或Spawner。
    ADemoPlayerState* PS=Controller?Controller->GetPlayerState<ADemoPlayerState>():nullptr; // 实际拥有者PS，本人经济/核心来源。
    UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr; // 只读本人账本用于奖励前后对照，夹具不Commit。
    UDemoUIManager* UI=Controller?Controller->FindComponentByClass<UDemoUIManager>():nullptr; // 生产协调器拥有实际 CommonUI 栈。
    UDemoMenuFlowComponent* Flow=Controller?Controller->GetMenuFlow():nullptr; // 当前 PC 的唯一菜单意图真值，只通过原入口变更。
    UDemoRunSaves* Saves=World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr; // 同一真实 GI 的临时槽服务，旅行不重建。
    if(!Controller||!Pawn||!State||!Mode||!PS||!Progress||!UI||!Flow||!Saves||!Pawn->GetDemoAttributes()||!Pawn->GetAbilitySystemComponent()||World->GetTimeSeconds()<.5f) return;
    PC=Controller; Controller->GetViewportSize(Session.ViewportWidth,Session.ViewportHeight);
    if(ULocalPlayer* Local=Controller->GetLocalPlayer())
        if(FilteredLocalPlayer.Get()!=Local)
        {
            if(UCommonInputSubsystem* Input=Local->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("R02S2Audit"),true);
            FilteredLocalPlayer=Local; // 只过滤自动化进程外部手柄干扰，不改设备 INI 或测试键盘焦点。
        }
    UI->Sync();
    if(Session.Stage>0&&Now-Session.StepStarted>60.) { Finish(false,TEXT("production UI/travel stage exceeded sixty real seconds")); return; }
    switch(Session.Stage)
    {
    case 0:
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { if(Press(TEXT("OverlayBack"))) Next(0); return; }
        if(!Check(State->Phase==EDemoPhase::Lobby&&Saves->GetActiveSlot()==INDEX_NONE&&!Saves->SlotExists(0)&&!Saves->SlotExists(1)&&!Saves->SlotExists(2),
            TEXT("actual initial Lobby has no active or existing GI temporary slot"))) return;
        if(!Check(!Flow->HasBlockingOverlay()&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0&&Controller->bShowMouseCursor
            &&Controller->IsMoveInputIgnored()&&Controller->IsLookInputIgnored(),TEXT("actual Lobby owns one menu stack and normal menu input policy"))) return;
        if(!CheckR02Page(TEXT("StartGame"))) return;
        if(Capture(TEXT("01_C02_Lobby"),TEXT("REAL_RUNTIME / genuine initial Lobby, no slot or phase injection"))) Next(100); return;
    case 100:
        if(!CheckR02Page(TEXT("UI.LobbySlot0"))) return;
        Session.SaveSerialBeforeLobbyBridge=Saves->GetChangeSerial();
        if(Press(TEXT("UI.LobbySlot0"))) Next(101); return; // 点击真实大厅控件，由生产桥转交原空槽确认，不直接调用 Flow::SelectSaveSlot。
    case 101:
    {
        UDemoMenuScreen* Screen=UI->GetActiveScreen(); // 桥接后原生产创建确认页，不能把大厅卡片选中当作已开确认。
        UDemoMenuButton* Cancel=Screen?Screen->FindAction(TEXT("CreateNo")):nullptr; // 当前实际安全默认按钮，只读其真实焦点。
        if(!Check(State->Phase==EDemoPhase::Lobby&&Flow->GetMenuPage()==EDemoMenuPage::CreateSave&&UI->GetMenuStackCount()==1
            &&UI->GetModalStackCount()==1&&Saves->GetActiveSlot()==INDEX_NONE&&!Saves->SlotExists(0)&&!Saves->SlotExists(1)&&!Saves->SlotExists(2)
            &&Saves->GetChangeSerial()==Session.SaveSerialBeforeLobbyBridge&&!UI->PressActionForTest(TEXT("UI.LobbySlot1")),
            TEXT("actual LobbySlot0 bridge opens original create modal without saving; modal blocks underlying direct slot action"))) return;
        if(!Check(Cancel&&Cancel->GetIsEnabled()&&(Cancel->HasUserFocus(Controller)||Cancel->HasUserFocusedDescendants(Controller)),
            TEXT("actual LobbySlot0 create confirmation defaults to Cancel without forced focus"))) return;
        if(Capture(TEXT("01a_C02_LobbySlot0_Create_DefaultCancel"),TEXT("REAL_RUNTIME / actual LobbySlot0 button bridges into original empty-slot CreateSave with default Cancel focus"))) Next(102); return;
    }
    case 102:
        Key(EKeys::Enter); Next(103); return; // 实际默认焦点 Enter 取消，不能直调原确认取消函数制造通过。
    case 103:
        if(!Check(State->Phase==EDemoPhase::Lobby&&Flow->GetMenuPage()==EDemoMenuPage::Saves&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0
            &&Saves->GetActiveSlot()==INDEX_NONE&&!Saves->SlotExists(0)&&!Saves->SlotExists(1)&&!Saves->SlotExists(2)
            &&Saves->GetChangeSerial()==Session.SaveSerialBeforeLobbyBridge,
            TEXT("actual LobbySlot0 default Enter cancellation returns to Saves with all slots empty and no save serial change"))) return;
        if(Capture(TEXT("01b_C02_LobbySlot0_Cancelled_Saves"),TEXT("REAL_RUNTIME / direct LobbySlot0 creation cancelled through actual default Enter; original Saves page and unchanged real temporary slots"))) Next(104); return;
    case 104:
        if(Press(TEXT("OverlayBack"))) Next(105); return; // 经原 Saves 返回按钮回大厅，不直接设置页面或重建大厅。
    case 105:
        if(!Check(State->Phase==EDemoPhase::Lobby&&!Flow->HasBlockingOverlay()&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0
            &&Saves->GetActiveSlot()==INDEX_NONE&&!Saves->SlotExists(0)&&!Saves->SlotExists(1)&&!Saves->SlotExists(2)
            &&Saves->GetChangeSerial()==Session.SaveSerialBeforeLobbyBridge&&Controller->bShowMouseCursor&&Controller->IsMoveInputIgnored()
            &&Controller->IsLookInputIgnored(),TEXT("actual Back returns from cancelled LobbySlot0 bridge to clean Lobby menu/input without creating a slot"))) return;
        if(!CheckR02Page(TEXT("StartGame"))) return;
        Session.bLobbySlot0BridgeObserved=true; Next(1); return; // 只有完整桥接/安全取消/原 Back 事实通过才记证据，再继续原 StartGame 回归。
    case 1:
        if(Press(TEXT("StartGame"))) Next(2); return;
    case 2:
        if(!Check(State->Phase==EDemoPhase::Lobby&&Flow->GetMenuPage()==EDemoMenuPage::Saves&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0,
            TEXT("actual StartGame opens Saves without starting the run"))) return;
        if(Capture(TEXT("02_Saves_Empty"),TEXT("REAL_RUNTIME / production Saves page with three genuinely empty temporary slots"))) Next(3); return;
    case 3:
        Session.SaveSerialBeforeCancel=Saves->GetChangeSerial();
        if(Press(TEXT("Save0"))) Next(4); return;
    case 4:
    {
        UDemoMenuScreen* Screen=UI->GetActiveScreen(); // 当前创建确认模态，不读取底层槽按钮焦点。
        UDemoMenuButton* Cancel=Screen?Screen->FindAction(TEXT("CreateNo")):nullptr;
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::CreateSave&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==1&&!Saves->SlotExists(0)
            &&!UI->PressActionForTest(TEXT("Save1")),TEXT("actual create modal blocks underlying slot actions and has not created slot 0"))) return;
        if(!Check(Cancel&&Cancel->GetIsEnabled()&&(Cancel->HasUserFocus(Controller)||Cancel->HasUserFocusedDescendants(Controller)),
            TEXT("actual create confirmation defaults to Cancel without forced focus"))) return;
        if(Capture(TEXT("03_Create_DefaultCancel"),TEXT("REAL_RUNTIME / actual empty-slot confirmation with real default Cancel focus"))) Next(5); return;
    }
    case 5:
        Key(EKeys::Enter); Next(6); return; // 真实焦点的 Enter 应走 CreateNo，不直接调用 ConfirmCreateSave(false)。
    case 6:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Saves&&UI->GetModalStackCount()==0&&Saves->GetActiveSlot()==INDEX_NONE
            &&!Saves->SlotExists(0)&&Saves->GetChangeSerial()==Session.SaveSerialBeforeCancel,TEXT("focused Enter cancelled creation without a slot or save serial change"))) return;
        if(Press(TEXT("Save0"))) Next(7); return;
    case 7:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::CreateSave, TEXT("actual second empty-slot selection opens a new creation confirmation"))) return;
        Next(8,1.); Press(TEXT("CreateYes")); return; // 先存有限步骤；正式按钮触发的 OpenLevel 由新 World 接续。
    case 8:
        if(State->Phase!=EDemoPhase::Hub) return;
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { if(Press(TEXT("OverlayBack"))) Next(8); return; }
        if(!Check(Session.WorldCount>=2&&Saves->GetActiveSlot()==0&&Saves->GetSlot(0)&&!Flow->HasBlockingOverlay()&&!UI->UsesWidgetMenu()
            &&UI->GetMenuStackCount()==0&&UI->GetModalStackCount()==0&&!World->IsPaused()&&!Controller->bShowMouseCursor
            &&!Controller->IsMoveInputIgnored()&&!Controller->IsLookInputIgnored(),TEXT("real CreateYes travelled into playable unpaused Hub with actual temporary slot 0"))) return;
        Session.bCreatedThroughUI=true; Session.CreatedAt=Saves->GetSlot(0)->CreatedLocal; Session.CreatedSlotKey=Saves->GetSlot(0)->SlotStorageKey;
        Controller->EscapePressed(); Next(9); return; // 沿原 Esc 入口开 Pause，不写 MenuPage 或暂停标志。
    case 9:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Pause&&Flow->IsPauseMenuOpen()&&World->IsPaused()&&UI->GetMenuStackCount()==1
            &&UI->GetModalStackCount()==0&&Controller->bShowMouseCursor&&Controller->IsMoveInputIgnored()&&Controller->IsLookInputIgnored(),
            TEXT("original Esc opens actual single-player paused World and menu input policy"))) return;
        if(!CheckR02Page(TEXT("OverlayBack"))) return;
        PauseSampleWorld=World; Session.PausedWorldTime=World->GetTimeSeconds();
        Session.PauseBaselineFrame=GFrameCounter;
        UE_LOG(LogFPSDemo,Log,TEXT("R02_S2_PAUSE_BASELINE world=%s worldTime=%.17g frame=%llu worldPaused=%d pcPaused=%d"),
            *World->GetPathName(),Session.PausedWorldTime,Session.PauseBaselineFrame,World->IsPaused(),Controller->IsPaused()); // 原double事实在截图前记录；不改变暂停或世界时钟。
        if(Capture(TEXT("04_C05_Pause"),TEXT("REAL_RUNTIME / actual original Esc Pause in genuine single-player Hub"))) Next(10); return;
    case 10:
    {
        UDemoMenuButton* Resume=UI->GetActiveScreen()?UI->GetActiveScreen()->FindAction(TEXT("OverlayBack")):nullptr; // 正式默认恢复动作。
        Session.PauseObservedWorldTime=World->GetTimeSeconds(); Session.PauseObservedFrame=GFrameCounter;
        UE_LOG(LogFPSDemo,Log,TEXT("R02_S2_PAUSE_OBSERVATION world=%s baseline=%.17g observed=%.17g delta=%.17g baselineFrame=%llu frame=%llu worldPaused=%d pcPaused=%d"),
            *World->GetPathName(),Session.PausedWorldTime,Session.PauseObservedWorldTime,Session.PauseObservedWorldTime-Session.PausedWorldTime,
            Session.PauseBaselineFrame,Session.PauseObservedFrame,World->IsPaused(),Controller->IsPaused()); // 实际暂停推进或引用错误必须保留原值，不增大容差制造通过。
        if(!Check(PauseSampleWorld.Get()==World&&World->IsPaused()&&Controller->IsPaused()&&Session.PauseObservedFrame>Session.PauseBaselineFrame
            &&Session.PauseObservedWorldTime==Session.PausedWorldTime,TEXT("actual single-player double World time stays exactly frozen across rendered Pause frames"))) return;
        Session.bPausedWorldFrozenObserved=true;
        if(!Check(Resume&&(Resume->HasUserFocus(Controller)||Resume->HasUserFocusedDescendants(Controller)),TEXT("actual Pause default focus belongs to OverlayBack resume"))) return;
        Key(EKeys::Enter); Next(11); return; // 通过实际焦点 Enter 触发 OverlayBack 原恢复链。
    }
    case 11:
        if(!Check(!Flow->HasBlockingOverlay()&&!Flow->IsPauseMenuOpen()&&!World->IsPaused()&&!UI->UsesWidgetMenu()&&UI->GetMenuStackCount()==0
            &&UI->GetModalStackCount()==0&&!Controller->bShowMouseCursor&&!Controller->IsMoveInputIgnored()&&!Controller->IsLookInputIgnored()
            &&World->GetTimeSeconds()>Session.PausedWorldTime,TEXT("actual focused Enter/OverlayBack resumes World time, cursor, movement, look and empty stacks"))) return;
        Session.bResumedByEnter=true; Controller->EscapePressed(); Next(12); return;
    case 12:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Pause&&World->IsPaused(),TEXT("original Esc reopens actual Pause after successful resume"))) return;
        Session.SaveSerialBeforeReturn=Saves->GetChangeSerial(); Next(13,1.); Press(TEXT("PauseLobby")); return; // 原权威命令先保存成功再旅行，不直接调用保存或 OpenLevel。
    case 13:
        if(State->Phase!=EDemoPhase::Lobby) return;
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { if(Press(TEXT("OverlayBack"))) Next(13); return; }
        if(!Check(Session.WorldCount>=3&&Saves->GetSlot(0)&&Saves->GetSlot(0)->SlotStorageKey==Session.CreatedSlotKey
            &&Saves->GetSlot(0)->CreatedLocal==Session.CreatedAt&&Saves->GetSlot(0)->Phase==EDemoPhase::Hub
            &&Saves->GetChangeSerial()>Session.SaveSerialBeforeReturn&&!World->IsPaused(),TEXT("original PauseLobby save barrier advanced actual serial and travelled to Lobby preserving Hub slot identity"))) return;
        Session.bReturnedThroughUI=true;
        if(!CheckR02Page(TEXT("StartGame"))) return;
        if(Capture(TEXT("05_C02_ReturnedLobby"),TEXT("REAL_RUNTIME / actual saved return-to-Lobby travel; no application exit action"))) Next(14); return;
    case 14:
        if(Press(TEXT("StartGame"))) Next(15); return;
    case 15:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Saves&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0&&Saves->SlotExists(0)
            &&Saves->GetSlot(0)&&!Saves->SlotExists(1)&&!Saves->SlotExists(2),TEXT("production Saves page reads genuine existing slot 0 and leaves other slots empty"))) return;
        if(Capture(TEXT("06_Saves_Existing"),TEXT("REAL_RUNTIME / production Saves reflects actual saved temporary Hub checkpoint after original return gate"))) Next(16); return;
    case 16:
        Next(17,1.); Press(TEXT("Save0")); return; // 原8图/保存门禁证据完整保留；真实已有槽继续按钮再次旅行，不手写Hub。
    case 17:
        if(State->Phase!=EDemoPhase::Hub) return;
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { if(Press(TEXT("OverlayBack"))) Next(17); return; }
        if(!Check(Session.WorldCount>=4&&Saves->GetActiveSlot()==0&&Saves->GetSlot(0)&&Saves->GetSlot(0)->SlotStorageKey==Session.CreatedSlotKey
            &&!Flow->HasBlockingOverlay()&&!UI->UsesWidgetMenu()&&!World->IsPaused()&&!Controller->IsMoveInputIgnored()&&!Controller->IsLookInputIgnored(),
            TEXT("actual existing-slot continue travelled into the same genuine playable Hub"))) return;
        if(!Mode->GetShopTerminal()) return;
        Pawn->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // 仅把测试角色放入原终端交互距离，不设置阶段/核心/装备。
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Mode->GetShopTerminal()->Interact(Pawn); UI->Sync(); Next(18); return;
    case 18:
        if(!Check(Flow->IsUpgradeMenuOpen()&&!Flow->IsRewardMenu(),TEXT("actual Hub shop interaction opens the original non-reward terminal"))) return;
        if(Press(TEXT("TerminalGamble"))) Next(1801,.2); return; // 2026-11-20 UIR-20261120：先经真实页签看R02金币娱乐/遗物，再继续原核心流程。
    case 1801:
        if(!Check(Flow->IsGambleMenuOpen()&&Flow->GetInspectedGamble()==INDEX_NONE,TEXT("actual TerminalGamble tab opens Hub gold arcade/relic directory"))) return;
        if(!CheckR02Page(TEXT("Casino.Open.0"))) return;
        if(Capture(TEXT("06b_R02_GoldArcadeRelics"),TEXT("REAL_RUNTIME / actual Hub terminal gold arcade + permanent relic list (UIR-20261120/TERMINAL-GAMBLE-R02)"))) Next(1802); return;
    case 1802:
        if(Press(TEXT("Gamble8"))) Next(1803,.2); return; // 原InspectGamble只查看遗物，不购买。
    case 1803:
        if(!Check(Flow->GetInspectedGamble()==8&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("GambleAction")),TEXT("actual relic inspect shows its detail and original GambleAction"))) return;
        if(Capture(TEXT("06c_R02_RelicDetail"),TEXT("REAL_RUNTIME / actual relic detail via Gamble8 inspect; no purchase submitted"))) Next(1804); return;
    case 1804:
        if(Press(TEXT("TerminalCore"))) Next(180); return;
    case 180:
        if(!Check(Flow->IsCoreMenuOpen()&&Controller->GetMenuSnapshot().CoreOffers.Num()==7,TEXT("actual core catalogue exposes seven authoritative core offers"))) return;
        Session.CoreIdBeforePreview=Controller->GetMenuSnapshot().Build.CoreId; Session.SaveSerialBeforeCorePreview=Saves->GetChangeSerial();
        if(Press(TEXT("UI.CorePreview1"))) Next(181); return; // 真实左侧预览按钮；不能直接选择Core1或设置私有预览索引。
    case 181:
    {
        UDemoTerminalScreen* Screen=Cast<UDemoTerminalScreen>(UI->GetActiveScreen()); // 实际切换后的当前核心页，读取纯预览getter，不设置其状态。
        UDemoMenuButton* Confirm=Screen?Screen->FindAction(TEXT("Core1")):nullptr; // 原Core1正式按钮存在才证明实际右侧已换到索引1。
        if(!Check(Screen&&Screen->GetPresentationCoreIndex()==1&&Confirm&&Confirm->GetIsEnabled()&&Controller->GetMenuSnapshot().Build.CoreId==Session.CoreIdBeforePreview
            &&Saves->GetChangeSerial()==Session.SaveSerialBeforeCorePreview,TEXT("actual preview 1 exposes Core1 confirm without committing CoreId or saving"))) return;
        if(Press(TEXT("UI.CorePreview0"))) Next(182); return;
    }
    case 182:
    {
        UDemoTerminalScreen* Screen=Cast<UDemoTerminalScreen>(UI->GetActiveScreen()); // 同一当前活页的真实只读预览索引。
        UDemoMenuButton* Confirm=Screen?Screen->FindAction(TEXT("Core0")):nullptr; // 原确认动作随实际预览回到索引0。
        if(!Check(Screen&&Screen->GetPresentationCoreIndex()==0&&Confirm&&Confirm->GetIsEnabled()&&Controller->GetMenuSnapshot().Build.CoreId==Session.CoreIdBeforePreview
            &&Saves->GetChangeSerial()==Session.SaveSerialBeforeCorePreview,TEXT("actual preview 0 restores Core0 confirm without committing CoreId or saving"))) return;
        if(!CheckR02Page(TEXT("Core0"))) return;
        Session.bCorePreviewObserved=true; Next(183,.1); return;
    }
    case 183:
        if(Capture(TEXT("06a_C03_CoreCatalogue"),TEXT("REAL_RUNTIME / actual seven-core catalogue; real preview 1 then 0 left CoreId and save serial unchanged; no selection yet"))) Next(19); return;
    case 19:
        if(Press(TEXT("Core0"))) Next(20); return; // 原正式Core0请求负责权威选择与保存，预览不能替代此动作。
    case 20:
        if(!Check(!Controller->GetMenuSnapshot().Build.CoreId.IsNone()&&Saves->GetChangeSerial()>Session.SaveSerialBeforeCorePreview,
            TEXT("actual Core0 confirmation committed a real core and advanced normal checkpoint save serial"))) return;
        UI->Back(); UI->Sync(); Next(21); return;
    case 21:
        UI->Back(); UI->Sync(); Next(22); return;
    case 22:
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { if(Press(TEXT("OverlayBack"))) Next(22); return; }
        if(!Check(!Flow->IsUpgradeMenuOpen()&&!Flow->HasBlockingOverlay()&&!UI->UsesWidgetMenu()&&UI->GetMenuStackCount()==0&&UI->GetModalStackCount()==0
            &&!Controller->bShowMouseCursor&&!Controller->IsMoveInputIgnored()&&!Controller->IsLookInputIgnored(),TEXT("original core terminal Back chain restores actual game input"))) return;
        if(!Check(Mode->GetNextLevelTerminal()!=nullptr,TEXT("actual Hub departure terminal exists after formal core choice"))) return;
        Pawn->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(100,0,110)); // 原终端真实范围内；后续仍必须通过其会话与明确难度按钮。
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Mode->GetNextLevelTerminal()->Interact(Pawn); UI->Sync(); Next(23); return;
    case 23:
        if(!Check(Flow->IsNextLevelConfirmationOpen()&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("Normal")),
            TEXT("actual initial departure exposes original Normal difficulty action"))) return;
        if(!Check(Flow->IsDepartureCorePickerOpen()&&UI->GetActiveScreen()->FindAction(TEXT("UI.CorePreview2"))&&UI->GetActiveScreen()->FindAction(TEXT("Core0")),
            TEXT("actual Hub departure exposes seven-core preview and original CoreN selection (UIR-20261120/DEPART-CORE)"))) return;
        Next(2301,.2); return;
    case 2301:
        if(Capture(TEXT("06d_R02_DepartureCorePicker"),TEXT("REAL_RUNTIME / actual Hub departure with inline core picker; Core0 already formally selected"))) Next(2302); return;
    case 2302:
        if(Press(TEXT("UI.CorePreview2"))) Next(2303,.2); return; // 纯本地预览，不提交Core2。
    case 2303:
    {
        UDemoTerminalScreen* Screen=Cast<UDemoTerminalScreen>(UI->GetActiveScreen()); // 当前出发页只读预览索引。
        if(!Check(Screen&&Screen->GetPresentationCoreIndex()==2&&Screen->FindAction(TEXT("Core2"))&&Controller->GetMenuSnapshot().Build.CoreId==DemoRunBuild::CoreAt(0),
            TEXT("actual departure preview 2 exposes Core2 confirm while authoritative core stays Core0"))) return;
        if(Capture(TEXT("06e_R02_DepartureCorePreview"),TEXT("REAL_RUNTIME / departure preview of core 03 without committing"))) Next(2304); return;
    }
    case 2304:
        if(Press(TEXT("Normal"))) Next(24,.01); return;
    case 24:
    {
        UDemoCombatSession* Combat=Mode->GetRunFlow()?Mode->GetRunFlow()->GetCombatSession():nullptr; // 正式准备屏障提交的真实会话，只读身份/存活，不创建替代凭据。
        if(State->Phase!=EDemoPhase::Combat||State->LevelNumber!=1||State->EnemiesRemaining<=0||!Combat||!Combat->IsActive()) return;
        if(!Check(State->Objective.Kind==EDemoObjectiveKind::KillAll&&!UI->UsesWidgetMenu()&&!Pawn->IsCoopEliminated()&&Pawn->GetDemoAttributes()->GetHealth()>0.f
            &&!Controller->IsMoveInputIgnored()&&!Controller->IsLookInputIgnored(),TEXT("legal first Combat has actual KillAll objective, active session, live enemies and restored player input"))) return;
        if(!Check(Saves->GetSlot(0)&&Combat->Matches(Saves->GetSlot(0)->RunId,1),TEXT("actual first Combat session matches the real active-slot run and level"))) return;
        FixtureCombatSession=Combat; Session.FixtureRunId=Saves->GetSlot(0)->RunId;
        Session.bLegalCombatObserved=true; Session.FixtureInitialEnemies=State->EnemiesRemaining; Session.FixtureInitialTeamKills=Progress->GetSnapshot().Kills;
        Session.FixtureInitialPistolChallenge=Progress->GetSnapshot().PistolChallenge;
        Session.Notes.Add(TEXT("TEST_DAMAGE_GE_FIXTURE starts after legal first Combat entry: the harness will submit actual environment-classified Damage GE to only live current-session production enemies. No player hit/aim/gunfeel/combat skill claim, direct Phase assignment, enemy-death notification or manual reward grant."));
        Next(25,.01); return;
    }
    case 25:
    {
        if(State->Phase==EDemoPhase::Reward) { Next(26,.1); return; } // 原Spawner/Objective/Flow自己清关，夹具只观察真实阶段边沿。
        if(State->Phase!=EDemoPhase::Combat||Pawn->IsCoopEliminated()||Pawn->GetDemoAttributes()->GetHealth()<=0.f)
        { Check(false,TEXT("test GE clear lost actual first Combat or living owner before original Reward")); return; } // 等待合法后续出生不逐帧打印PASS；失败立即停，不伪造清关。
        for(TActorIterator<ADemoEnemy> It(World);It;++It)
        {
            if(!It->IsAlive()||!It->IsCombatReady()||!It->IsCombatSessionValid()) continue;
            const float Damage=It->GetHealth()+It->GetMaxHealth()+1.f; // 本次真实正HP生成有限致死请求，不写目标属性或死亡标记。
            Session.bDamageGEFixtureUsed=true;
            const bool Submitted=DemoEffects::ApplyDamage(Pawn->GetAbilitySystemComponent(),It->GetAbilitySystemComponent(),Damage,
                EDemoPlayerDamageKind::Environment,Pawn->GetActorLocation()); // 实际ASC瞬时Health GE；生产死亡边沿负责Spawner去重和原奖励，不直接调OnEnemyKilled。
            if(Submitted) ++Session.FixtureDamageSubmissions;
            if(!Check(Submitted,TEXT("TEST_DAMAGE_GE_FIXTURE actual current-session enemy damage Spec submitted: ")+It->GetName())) return;
            if(!Check(!It->IsAlive()&&It->GetHealth()<=0.f,TEXT("TEST_DAMAGE_GE_FIXTURE actual enemy ASC reached death edge: ")+It->GetName())) return;
            ++Session.FixtureEnemyDeaths;
        }
        return; // 若计划尚有合法待生成敌人，后续帧只处理其真实激活实例；60秒阶段/600秒进程上限继续约束。
    }
    case 26:
    {
        UDemoEnemySpawnComponent* Spawner=Mode->GetSpawnSystem(); // 原Flow清关前已CancelEncounter，完成锁存会复位；不把运行期锁存当作Reward期持久事实。
        Session.ClearPhase=static_cast<int32>(State->Phase); Session.ClearLevel=State->LevelNumber;
        Session.ClearObjectiveState=static_cast<int32>(State->Objective.State); Session.ClearObjectiveRemaining=State->Objective.Remaining;
        Session.ClearSpawnRemaining=Spawner?Spawner->GetRemaining():-1; Session.ClearSpawnAlive=Spawner?Spawner->GetAliveCount():-1;
        Session.bClearSpawnerCompleteLatch=Spawner&&Spawner->IsEncounterComplete(); // 仅诊断；DemoCampaignFlow::ResolveObjectiveOutcome先Cancel再FinishLevel，正常可以false。
        Session.bClearSameSession=FixtureCombatSession.IsValid()&&Mode->GetRunFlow()&&Mode->GetRunFlow()->GetCombatSession()==FixtureCombatSession.Get();
        Session.bClearSessionActive=FixtureCombatSession.IsValid()&&FixtureCombatSession->IsActive();
        Session.ClearTeamKills=Progress->GetSnapshot().Kills; Session.ClearPistolChallenge=Progress->GetSnapshot().PistolChallenge; Session.bClearObservationRecorded=true;
        UE_LOG(LogFPSDemo,Log,TEXT("R02_S2_CLEAR_OBSERVATION phase=%d level=%d gsRemaining=%d objectiveState=%d objectiveRemaining=%d spawnRemaining=%d spawnAlive=%d spawnCompleteLatch=%d sameSession=%d sessionActive=%d initialEnemies=%d geSubmissions=%d geDeaths=%d teamKillCreditBefore=%d teamKillCreditAfter=%d pistolChallengeBefore=%d pistolChallengeAfter=%d"),
            Session.ClearPhase,Session.ClearLevel,State->EnemiesRemaining,Session.ClearObjectiveState,Session.ClearObjectiveRemaining,Session.ClearSpawnRemaining,Session.ClearSpawnAlive,
            Session.bClearSpawnerCompleteLatch,Session.bClearSameSession,Session.bClearSessionActive,Session.FixtureInitialEnemies,Session.FixtureDamageSubmissions,Session.FixtureEnemyDeaths,
            Session.FixtureInitialTeamKills,Session.ClearTeamKills,Session.FixtureInitialPistolChallenge,Session.ClearPistolChallenge); // 所有分量在任何失败前记录，不让首个短路掩盖实际值。
        if(!Check(State->Phase==EDemoPhase::Reward&&State->LevelNumber==1&&State->EnemiesRemaining==0,
            FString::Printf(TEXT("actual original Flow reached first Reward: phase=%d level=%d remaining=%d"),Session.ClearPhase,Session.ClearLevel,State->EnemiesRemaining))) return;
        if(!Check(State->Objective.Kind==EDemoObjectiveKind::KillAll&&State->Objective.State==EDemoObjectiveState::Completed&&State->Objective.Remaining==0,
            FString::Printf(TEXT("actual authoritative KillAll objective remains Completed after clearance: state=%d remaining=%d"),Session.ClearObjectiveState,Session.ClearObjectiveRemaining))) return;
        if(!Check(Session.bClearSameSession&&!Session.bClearSessionActive&&Saves->GetSlot(0)&&Saves->GetSlot(0)->RunId==Session.FixtureRunId,
            FString::Printf(TEXT("actual original first-Combat session is the same revoked session in the same saved run: same=%d active=%d"),Session.bClearSameSession,Session.bClearSessionActive))) return;
        if(!Check(Spawner&&Session.ClearSpawnRemaining==0&&Session.ClearSpawnAlive==0,
            FString::Printf(TEXT("actual production spawn population is empty after Flow cleanup: remaining=%d alive=%d transientCompleteLatch=%d"),Session.ClearSpawnRemaining,Session.ClearSpawnAlive,Session.bClearSpawnerCompleteLatch))) return;
        if(!Check(Session.FixtureInitialEnemies>0&&Session.FixtureEnemyDeaths==Session.FixtureInitialEnemies&&Session.FixtureDamageSubmissions==Session.FixtureEnemyDeaths,
            FString::Printf(TEXT("actual test GE death edges cover the original legal population once: initial=%d submitted=%d dead=%d"),Session.FixtureInitialEnemies,Session.FixtureDamageSubmissions,Session.FixtureEnemyDeaths))) return;
        if(!Check(Session.ClearTeamKills==Session.FixtureInitialTeamKills+Session.FixtureEnemyDeaths,
            FString::Printf(TEXT("original reward callbacks added team kill credit, not individual last-hit attribution: before=%d after=%d teamDeaths=%d"),Session.FixtureInitialTeamKills,Session.ClearTeamKills,Session.FixtureEnemyDeaths))) return;
        if(!Check(Session.ClearPistolChallenge==Session.FixtureInitialPistolChallenge,
            FString::Printf(TEXT("environment Damage GE fixture is not an effective firearm shot and leaves pistol challenge unchanged: before=%d after=%d"),Session.FixtureInitialPistolChallenge,Session.ClearPistolChallenge))) return;
        Session.Notes.Add(TEXT("Clearance observation uses persistent authoritative Objective.Completed/remaining=0, actual first Reward, original revoked CombatSession and empty spawn population. The production Flow normally CancelEncounter(true) before FinishLevel; this resets Spawner.bComplete, while HasResolvedEncounter is also a Combat-only synchronous predicate. Neither transient predicate is falsely required to stay true in Reward. Progress.Kills is original per-member team kill credit, not individual last-hit attribution; environment GE does not count as a firearm shot."));
        const FDemoPlayerMenuSnapshot& View=Controller->GetMenuSnapshot(); // 同次真实拥有者报价，免费奖励预览与实际领取都使用它。
        if(!Check(Flow->IsUpgradeMenuOpen()&&Flow->IsRewardMenu()&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0&&!View.bBranchReward
            &&View.bCanChooseReward&&View.Progress.LastRewardLevel<1&&View.RewardOffers.Num()==3,TEXT("actual first-level C03 has three authoritative unclaimed reward offers"))) return;
        for(int32 Index=0;Index<3;++Index)
        {
            UDemoMenuButton* Choice=UI->GetActiveScreen()?UI->GetActiveScreen()->FindAction(FName(*FString::Printf(TEXT("Upgrade%d"),Index))):nullptr; // 实际原三项按钮，不从概念图拼选项。
            if(!Check(Choice&&Choice->GetIsEnabled()&&View.RewardOffers[Index].BlockReason.IsEmpty(),TEXT("actual original reward choice is enabled: ")+FString::FromInt(Index))) return;
        }
        if(!CheckR02Page(TEXT("Upgrade0"))) return;
        if(!Check(UI->GetFeedbackText().IsEmpty(),TEXT("actual Reward page does not revive the obsolete original StartLevel preparation notice after fast legal clearance"))) return; // 原始回执保留在业务层；只核屏幕不再显示已被真实清关事实覆盖的短准备通知。
        Session.bFixtureClearObserved=true; Session.bRewardMenuObserved=true; Session.RewardBefore=Progress->GetSnapshot();
        Session.RewardDamageBefore=Pawn->GetDemoAttributes()->GetWeaponDamageBonus(); Session.RewardExpectedGain=View.RewardOffers[0].Gain;
        Session.SaveSerialBeforeReward=Saves->GetChangeSerial();
        if(!Check(Session.RewardExpectedGain>0.f,TEXT("genuine new-slot damage reward has a positive authoritative offered gain"))) return;
        if(Capture(TEXT("07_C03_RewardChoices"),TEXT("TEST_DAMAGE_GE_FIXTURE / legal first-level enemies died through real ASC Damage GE; original clear flow supplies actual C03 offers, not player combat"))) Next(27); return;
    }
    case 27:
        ConsumedRewardButton=UI->GetActiveScreen()?UI->GetActiveScreen()->FindAction(TEXT("Upgrade0")):nullptr; // 仅真实领取前保留弱旧按钮，不延长页面生命周期。
        if(Press(TEXT("Upgrade0"))) Next(28); return;
    case 28:
        if(!Check(State->Phase==EDemoPhase::Intermission&&State->LevelNumber==1&&Progress->GetSnapshot().LastRewardLevel==1
            &&!Controller->GetMenuSnapshot().bCanChooseReward&&FMath::IsNearlyEqual(Pawn->GetDemoAttributes()->GetWeaponDamageBonus(),Session.RewardDamageBefore+Session.RewardExpectedGain)
            &&Progress->GetSnapshot().Coins==Session.RewardBefore.Coins&&Progress->GetSnapshot().SilverCoins==Session.RewardBefore.SilverCoins
            &&Saves->GetChangeSerial()>Session.SaveSerialBeforeReward&&Saves->GetSlot(0)&&Saves->GetSlot(0)->Phase==EDemoPhase::Intermission
            &&FMath::IsNearlyEqual(Saves->GetSlot(0)->DamageBonus,Pawn->GetDemoAttributes()->GetWeaponDamageBonus()),
            TEXT("actual Upgrade0 consumed the one first-level reward, applied exactly its offered gain without debit, and saved Intermission"))) return;
        Session.RewardAfter=Progress->GetSnapshot(); Session.RewardDamageAfter=Pawn->GetDemoAttributes()->GetWeaponDamageBonus(); Session.SaveSerialAfterReward=Saves->GetChangeSerial();
        if(!Check(!UI->PressActionForTest(TEXT("Upgrade0")),TEXT("consumed reward has no current active Upgrade0 button accepting a repeated claim"))) return;
        if(ConsumedRewardButton.IsValid())
        { if(!Check(!ConsumedRewardButton->PressForTest(),TEXT("actual retained consumed reward button rejects a repeated stale-widget click"))) return; }
        else
        {
            Session.Assertions.Add(TEXT("NOT_RUN: consumed old reward widget already reclaimed; stale-object click not exercised. Current active-action rejection and unchanged real ledger are checked separately."));
            Session.Notes.Add(TEXT("Consumed reward old widget was invalid/reclaimed: no stale-object click PASS claimed, and no server RPC replay proof is claimed."));
            UE_LOG(LogFPSDemo,Display,TEXT("R02_S2_CHECK_NOT_RUN consumed reward old widget invalid/reclaimed; stale click not exercised"));
        }
        Next(29); return;
    case 29:
        if(!Check(FDemoPlayerProgressSnapshot::StaticStruct()->CompareScriptStruct(&Session.RewardAfter,&Progress->GetSnapshot(),0)
            &&Pawn->GetDemoAttributes()->GetWeaponDamageBonus()==Session.RewardDamageAfter&&Saves->GetChangeSerial()==Session.SaveSerialAfterReward
            &&Progress->GetSnapshot().LastRewardLevel==1,TEXT("rejected repeated reward actions leave actual progress, gain and save serial unchanged; exactly one reward remains consumed"))) return;
        Session.bRewardChosenOnce=true;
        if(!Check(!UI->UsesWidgetMenu()&&!Flow->HasBlockingOverlay()&&Mode->GetNextLevelTerminal()&&Mode->GetShopTerminal(),TEXT("actual Intermission closes reward and exposes its original shop and next-level terminals"))) return;
        Pawn->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // 2026-11-20：关间升级终端看R02冒险强化，只查看不投入。
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Mode->GetShopTerminal()->Interact(Pawn); UI->Sync(); Next(2901); return;
    case 2901:
        if(!Check(Flow->IsUpgradeMenuOpen()&&!Flow->IsRewardMenu(),TEXT("actual Intermission shop terminal opens"))) return;
        if(Press(TEXT("TerminalGamble"))) Next(2902,.2); return;
    case 2902:
        if(!CheckR02Page(TEXT("Gamble0"))) return;
        if(Capture(TEXT("07a_R02_AdventureUpgrades"),TEXT("REAL_RUNTIME / actual Intermission silver adventure upgrades, nothing inspected (UIR-20261120/ADVENTURE-DECLUTTER)"))) Next(2903); return;
    case 2903:
        if(Press(TEXT("Gamble3"))) Next(2904,.2); return; // 只查看天赋抽取，不投入银币。
    case 2904:
        if(!Check(Flow->GetInspectedGamble()==3,TEXT("actual Gamble3 inspect opens talent pool preview"))) return;
        if(Capture(TEXT("07b_R02_AdventureTalentPool"),TEXT("REAL_RUNTIME / talent draw inspected with one-at-a-time pool preview; no wager"))) Next(2905); return;
    case 2905:
        if(Press(TEXT("Close"))) Next(2906,.1); return;
    case 2906:
        if(!Check(!UI->UsesWidgetMenu()&&!Flow->IsUpgradeMenuOpen(),TEXT("actual Close leaves Intermission shop terminal"))) return;
        Pawn->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(100,0,110)); // 真实下一关入口，C07仍需要真实Combat门；不把Intermission伪装成战斗。
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Mode->GetNextLevelTerminal()->Interact(Pawn); UI->Sync(); Next(30); return;
    case 30:
        if(!Check(Flow->IsNextLevelConfirmationOpen()&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("NextLevelYes")),
            TEXT("actual Intermission departure exposes original NextLevelYes action"))) return;
        if(Press(TEXT("NextLevelYes"))) Next(31,.01); return;
    case 31:
    {
        const UDemoCombatSession* Combat=Mode->GetRunFlow()?Mode->GetRunFlow()->GetCombatSession():nullptr; // 合法第二关作为HUD的真实Combat上下文，不宣称第10关。
        if(State->Phase!=EDemoPhase::Combat||State->LevelNumber!=2||State->EnemiesRemaining<=0||!Combat||!Combat->IsActive()) return;
        if(!Check(!UI->UsesWidgetMenu()&&!Flow->HasBlockingOverlay()&&!Pawn->IsCoopEliminated()&&Pawn->GetDemoAttributes()->GetHealth()>0.f,
            TEXT("legal second Combat provides real active-session context before explicitly labelled Boss sample"))) return;
        Session.bSyntheticBossSampleStarted=true;
        Session.Notes.Add(TEXT("SYNTHETIC_PRESENTATION C07 begins only after legal level-2 departure: production AI is deactivated/stopped in this disposable test process, then an explicit hidden inactive no-reward level-10-template Boss supplies real public HUD fields. The displayed current stage remains real level 2; this is not level-10 progression, a Boss fight, or player combat acceptance."));
        FreezeBossSampleActors();
        for(TActorIterator<ADemoEnemy> It(World);It;++It)
            if(!Check(!It->IsBoss()||!It->IsAlive(),TEXT("no production live Boss competes with explicit C07 sample"))) return;
        FDemoEnemySpawnStats Stats; FString Error; // 从正式第10关模板解析身份/HP，样本不登记到当前Spawner或奖励计划。
        if(!Check(Mode->GetSpawnStats(10,true,Stats,Error),TEXT("SYNTHETIC Boss sample resolves actual project level-10 template: ")+Error)) return;
        Stats.CoinReward=0; // 明确无奖励表现Actor，不属于真实第2关人口，不授予任何通关事实。
        const int32 RemainingBeforeSample=State->EnemiesRemaining; // 生产人数只读对照，未注册样本不能改变它。
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 远离视线且隐藏，避免实体/头顶条干扰C07顶栏证据。
        PresentationBoss=World->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),Pawn->GetActorLocation()+FVector(20000,0,0),FRotator::ZeroRotator,Params);
        if(!Check(PresentationBoss.IsValid(),TEXT("SYNTHETIC explicit isolated Boss Actor created"))) return;
        PresentationBoss->Configure(Stats); PresentationBoss->DeactivateSpawn(); PresentationBoss->SetActorTickEnabled(false); PresentationBoss->SetActorHiddenInGame(true);
        if(!Check(PresentationBoss->IsBoss()&&PresentationBoss->IsSpawnInitialized()&&PresentationBoss->IsAlive()&&PresentationBoss->GetHealth()>0.f
            &&PresentationBoss->GetMaxHealth()>0.f&&PresentationBoss->GetCoinReward()==0&&State->EnemiesRemaining==RemainingBeforeSample,
            TEXT("SYNTHETIC Boss has actual configured public identity/HP and zero reward without changing real enemy population"))) return;
        Next(32,.2); return;
    }
    case 32:
        if(!Check(State->Phase==EDemoPhase::Combat&&State->LevelNumber==2&&PresentationBoss.IsValid()&&PresentationBoss->IsBoss()&&PresentationBoss->IsAlive()
            &&Controller->GetHUD()&&Controller->GetHUD()->IsA<ADemoHUD>()&&!UI->UsesWidgetMenu()&&!Flow->HasBlockingOverlay()&&!Pawn->IsCoopEliminated()
            &&Controller->GetViewTarget()==Pawn&&Pawn->GetWeaponComponent()&&Pawn->GetWeaponComponent()->GetScopeLevel()==0,
            TEXT("SYNTHETIC C07 screenshot has actual production HUD, public live Boss fields and original Combat/menu/owner/scope visibility gates"))) return;
        if(Capture(TEXT("08_C07_BossHUD_Sample"),TEXT("SYNTHETIC_PRESENTATION / legal level-2 Combat HUD; production AI stopped and explicit hidden inactive zero-reward level-10-template Boss supplies real public name/phase/HP, not a real Boss encounter"),false)) Next(33); return;
    case 33:
        if(!Check(Session.bSyntheticBossCaptured&&Session.bRewardChosenOnce,TEXT("actual C03 reward was chosen once and separately labelled normal C07 Boss sample PNG completed"))) return;
        Finish(true,TEXT("bounded original S2/core preview/real reward transaction passed; clear used explicit Damage GE test fixture and C07 is a separate synthetic Boss sample; normal process exit requested")); return;
    default:
        Finish(false,TEXT("unexpected R02 S2 audit stage")); return;
    }
}
#endif
