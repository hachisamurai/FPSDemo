// 职责：真实Tab输入、全部天赋分页和GAS等级回归。使用：Editor专用-DemoPlayerStatusTest及隔离设备INI，CommonUI按钮不旁路业务。
#include "Tests/DemoPlayerStatusTest.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIActions.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Common/DemoMenuScreen.h"
#include "Framework/Application/SlateApplication.h"
#include "Settings/DemoGameUserSettings.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Save/DemoRunSave.h"
#include "Save/Codecs/DemoCheckpointJsonCodec.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerInput.h" // FInputKeyParams完整定义，测试真实输入队列而非直接模拟页面。
#include "Engine/GameInstance.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"
#include "UnrealClient.h"
#endif

ADemoPlayerStatusTest::ADemoPlayerStatusTest()
{ DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; }
#if WITH_EDITOR
bool ADemoPlayerStatusTest::Check(bool Condition,const TCHAR* Message)
{
    DEMO_LOG_CALL(); UE_LOG(LogFPSDemo,Display,TEXT("STATUS_TEST %s step=%d %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Step,Message);
    if(!Condition) { bFailed=true; FPlatformMisc::RequestExitWithStatus(false,1); }
    return Condition;
}
void ADemoPlayerStatusTest::Key(FKey Key,bool bDown)
{
    DEMO_LOG_CALL();
    auto* PC=UGameplayStatics::GetPlayerController(this,0); const auto* UI=PC->FindComponentByClass<UDemoUIManager>(); // 游戏态走EnhancedInput，菜单态走实际Slate路由。
    if(UI&&UI->UsesWidgetMenu())
    {
        if(bDown) FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key,FModifierKeysState(),0,false,0,0));
        else FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key,FModifierKeysState(),0,false,0,0));
    }
    else PC->InputKey(FInputKeyParams(Key,bDown?IE_Pressed:IE_Released,bDown?1.0:0.0));
}
#endif
void ADemoPlayerStatusTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    const double Now=FPlatformTime::Seconds(); // 暂停仍推进专项，不依赖OS窗口焦点。
    if(Started==0.) { Started=Now; Next=Now+1.; }
    if(bFailed||Now<Next) return;
    if(!Check(Now-Started<40.,TEXT("bounded realtime test"))) return;
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 显式单人夹具，非生产玩家选择策略。
    auto* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 每帧重新解析，避免旧Pawn指针。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 正式游戏入口。
    auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 仅本次Editor临时槽，无云子系统。
    if(!Check(PC&&Pawn&&Mode&&Saves,TEXT("world ready"))) return;
    auto* Progress=Pawn->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 本人权威账本。
    auto* Flow=PC->GetMenuFlow(); // 本地展示路由，不模拟Canvas状态。
    auto* UI=PC->FindComponentByClass<UDemoUIManager>(); if(UI) UI->Sync(); // 正式UMG协调器。
    FOutputDeviceNull Output; // 命令输出不需要解析，直接断言实际结果。
    switch(Step)
    {
    case 0:
    {
        Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())->DismissFirstRunGuide(); // 只写专项隔离INI。
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { if(!Check(UI&&UI->PressActionForTest(TEXT("OverlayBack")),TEXT("dismiss actual guide"))) return; }
        Flow->TabPressed();
        if(!Check(PC->GetMenuPage()==EDemoMenuPage::None,TEXT("Tab cannot open status in lobby"))) return;
        if(!Check(Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("start temporary hub"))) return;
        Saves->ConsumePending(); PC->OnRunReady();
        FDemoGambleProgress Legacy; Legacy.Talents={TEXT("overdrive")}; // 旧V6无等级字段。
        if(!Check(Legacy.Validate()&&Legacy.Rank(TEXT("overdrive"))==1&&Legacy.UpgradeTalent(TEXT("overdrive"))&&Legacy.Rank(TEXT("overdrive"))==2,TEXT("legacy implicit rank migrates to level two"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents supply:2 medic:2 ember:2 hunter:2 pierce:2 overdrive:2 twin:2 ricochet:2 siphon:2 precision:2 dashload:2"),Output,GetWorld());
        auto* Weapon=Pawn->GetWeaponComponent()->GetActiveWeapon(); // 正式默认手枪，不伪造武器实例。
        if(!Check(Progress->GetSnapshot().Gambling.Talents.Num()==11&&Pawn->GetDemoAttributes()->GetFireRateMultiplier()==2.25f
            &&FMath::IsNearlyEqual(Weapon->GetDamagePerPellet(),Weapon->Config.BaseDamage*.875f)&&Weapon->GetProjectilesPerShot()==2,TEXT("all talents rank two grant real GAS rate and stronger twin damage without more entities"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents twin:0"),Output,GetWorld());
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents twin:4294967297"),Output,GetWorld()); // 溢出也必须整笔拒绝。
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents twin:1.5"),Output,GetWorld()); // 禁止小数截断。
        if(!Check(Progress->GetSnapshot().Gambling.Rank(TEXT("overdrive"))==2,TEXT("invalid rank command leaves whole build unchanged"))) return;
        if(!Check(Mode->SaveCheckpoint(),TEXT("ranked checkpoint saved"))) return;
        auto* Disk=Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(Saves->SlotName(0),0)); // 真正磁盘读回，非只读GI副本。
        if(!Check(Disk&&Disk->Gambling.TalentRanks.Num()==11&&Disk->Gambling.Rank(TEXT("twin"))==2,TEXT("binary save preserves every rank"))) return;
        FString Error; // 纯JSON往返错误，无HTTP请求。
        auto Json=Saves->ExportCloud(0); // 使用正式字段白名单编码器。
        auto* Decoded=NewObject<UDemoRunSave>(this); // 独立候选，不覆盖活动存档。
        if(!Check(DemoCheckpointJsonCodec::Decode(Json,*Decoded,Error)&&Decoded->Gambling.Rank(TEXT("overdrive"))==2,TEXT("V6 optional rank JSON roundtrip"))) return;
        Json->GetObjectField(TEXT("gambling"))->RemoveField(TEXT("talentRanks")); // 模拟没有等级字段的旧V6槽。
        if(!Check(DemoCheckpointJsonCodec::Decode(Json,*Decoded,Error)&&Decoded->Gambling.Rank(TEXT("overdrive"))==1&&Decoded->Gambling.TalentRanks.IsEmpty(),TEXT("old JSON retains implicit level one without rewriting hash input"))) return;
        Json->GetObjectField(TEXT("gambling"))->SetArrayField(TEXT("talentRanks"),{MakeShared<FJsonValueNumber>(0)}); // 损坏长度/等级不能进入正式候选。
        if(!Check(!DemoCheckpointJsonCodec::Decode(Json,*Decoded,Error),TEXT("malformed rank JSON rejected"))) return;
        Key(EKeys::Tab,true); break;
    }
    case 1:
        Key(EKeys::Tab,false);
        UE_LOG(LogFPSDemo,Display,TEXT("STATUS_INPUT_PROBE page=%d paused=%d move=%d look=%d cursor=%d stack=%d"),int32(PC->GetMenuPage()),GetWorld()->IsPaused(),PC->IsMoveInputIgnored(),PC->IsLookInputIgnored(),PC->bShowMouseCursor,UI?UI->GetMenuStackCount():-1); // 拆开旧合取断言，区分CommonUI光标与GAS/暂停问题。
        if(!Check(PC->GetMenuPage()==EDemoMenuPage::PlayerStatus&&GetWorld()->IsPaused()&&PC->IsMoveInputIgnored()&&PC->bShowMouseCursor,TEXT("physical Tab opens paused status and captures input"))) return;
        if(!Check(PC->GetMenuSnapshot().OwnedTalentViews.Num()==11&&UI&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("StatusNext")),TEXT("server descriptions and actual UMG next-page button"))) return;
        Flow->DispatchUIAction(EDemoUIAction::PauseQuit);
        if(!Check(PC->GetMenuPage()==EDemoMenuPage::PlayerStatus&&!Pawn->CanUsePlayerSkills(),TEXT("status blocks stale pause actions and gameplay skills"))) return;
        FScreenshotRequest::RequestScreenshot(TEXT("PlayerStatus-Ranks-Page1.png"),true,false);
        break;
    case 2:
        if(!Check(UI&&UI->PressActionForTest(TEXT("StatusNext")),TEXT("press actual status pagination button"))) return;
        if(!Check(Flow->GetStatusPage()==1,TEXT("real status UI advances talent page"))) return;
        break;
    case 3:
        FScreenshotRequest::RequestScreenshot(TEXT("PlayerStatus-Ranks-Page2.png"),true,false);
        Key(EKeys::Tab,true); break;
    case 4:
        Key(EKeys::Tab,false);
        if(!Check(!GetWorld()->IsPaused()&&!PC->HasBlockingOverlay()&&!PC->IsMoveInputIgnored()&&!PC->bShowMouseCursor&&Pawn->CanUsePlayerSkills(),TEXT("physical Tab closes while paused and restores gameplay"))) return;
        Key(EKeys::Tab,true); break;
    case 5:
        Key(EKeys::Tab,false); Key(EKeys::Escape,true); break;
    case 6:
        Key(EKeys::Escape,false);
        if(!Check(!PC->HasBlockingOverlay()&&!GetWorld()->IsPaused(),TEXT("Esc closes status instead of opening nested pause menu"))) return;
        Flow->TabPressed(); GetWorld()->GetGameState<ADemoGameState>()->Phase=EDemoPhase::Reward; GetWorld()->GetGameState<ADemoGameState>()->LevelNumber=1; PC->PublishMenuSnapshot(); Flow->RefreshFromReplicatedState(); // 先生成真实待领奖投影。
        if(!Check(PC->GetMenuPage()==EDemoMenuPage::None&&!GetWorld()->IsPaused()&&PC->IsUpgradeMenuOpen(),TEXT("phase transition closes status and preserves mandatory reward"))) return;
        Flow->TabPressed();
        if(!Check(PC->IsUpgradeMenuOpen(),TEXT("Tab cannot skip mandatory reward"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents clear"),Output,GetWorld());
        if(!Check(Progress->GetSnapshot().Gambling.TalentRanks.IsEmpty()&&Pawn->GetDemoAttributes()->GetFireRateMultiplier()==1.f,TEXT("clear removes ranks and aggregated fire-rate GE"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_PLAYER_STATUS_TEST_SUCCESS")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); return;
    }
    ++Step; Next=Now+.35; // 每阶段至少经历输入/Canvas一帧；截图在下一帧呈现。
#endif
}
