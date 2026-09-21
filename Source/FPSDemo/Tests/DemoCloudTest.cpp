#include "Tests/DemoCloudTest.h"
#include "Player/DemoCloudSync.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Game/FPSDemoGameMode.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "AI/DemoEnemy.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "Debug/DemoLog.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"

ADemoCloudTest::ADemoCloudTest()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true; // 实际退出会暂停World，测试需继续观察HTTP成功/失败按钮行为。
    PrimaryActorTick.TickInterval = 0;
}
bool ADemoCloudTest::Check(bool Condition, const TCHAR* Name)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Display, TEXT("CLOUD_TEST %s %s"), Condition ? TEXT("PASS") : TEXT("FAIL"), Name);
    if (!Condition) { bFailed = true; FPlatformMisc::RequestExitWithStatus(false, 1); }
    return Condition;
}
void ADemoCloudTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    if (bFailed) return;
    if (Started == 0)
    {
        Started = FPlatformTime::Seconds();
#if WITH_EDITOR
        // 用户要求Editor目标完全离线，云回归必须转移到打包Development进程，不加越权测试豁免。
        Check(false, TEXT("cloud integration requires packaged Development game, editor is local-only"));
        return;
#endif
        FString Id; FGuid Parsed; // 没有合法测试GUID就拒绝，不能用真实玩家文件跑回归。
        if (!Check(FParse::Value(FCommandLine::Get(), TEXT("DemoCloudTestId="), Id) && FGuid::Parse(Id, Parsed) && Parsed.IsValid(), TEXT("isolated test identity"))) return;
        FParse::Value(FCommandLine::Get(), TEXT("DemoCloudTest="), Mode);
        if (!Check(Mode == TEXT("upload") || Mode == TEXT("restore") || Mode == TEXT("offline") || Mode == TEXT("exit-online") || Mode == TEXT("exit-offline"), TEXT("explicit test mode"))) return;
    }
    if (FPlatformTime::Seconds() - Started > 240) { Check(false, TEXT("cloud bounded completion timeout")); return; }
    UDemoCloudSync* Cloud = GetGameInstance()->GetSubsystem<UDemoCloudSync>(); // 本GI真实同步器，未mock HTTP。
    UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 独立测试永久存档。
    UDemoRunSaves* Runs = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 独立三槽文件。
    AFPSDemoGameMode* Game = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 真实权威战役。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 只读阶段，测试不能直接授予Victory。
    ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0)); // 当前世界控制器。
    ADemoCharacter* Player = PC ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr; // 真实GAS伤害来源。
    if (!Cloud || !Profile || !Runs || !Game || !State || !Player) return;
    const bool bSynced = Cloud->GetStatus() == TEXT("永久进度与战役检查点已同步"); // 只有发件箱清空、全部数据确认后才出现此状态。
    if (Mode.StartsWith(TEXT("exit-")))
    {
        // 真实打包目标、真实HTTP；只使用GUID隔离槽，可对测试进程覆盖API地址构造离线场景。
        if (Step == 0)
        {
            if (Mode == TEXT("exit-online") && !bSynced) return;
            if (!Check(Runs->CreateSlot(0) && Game->StartRun(),TEXT("exit owns fresh isolated run"))) return;
            State->Coins = 41; // 可辨识检查点数值，用来验证本地写入与随后上传。
            PC->OnRunReady(); PC->EscapePressed(); PC->QuitPressed();
            Step = 10; return;
        }
        if (Step == 10 && Mode == TEXT("exit-online"))
        {
            if (PC->GetQuitState() == EDemoQuitState::Failed) { Check(false,TEXT("online exit cloud accepted")); return; }
            if (PC->GetQuitState() == EDemoQuitState::WaitingCloud && Cloud->GetStatus() == TEXT("正在同步永久进度与检查点"))
            {
                // 第一份请求已固定为41金币；模拟在途时额外落盘，旧回执必须继续补传73金币才能退出。
                State->Coins = 73;
                if (!Check(Game->SaveCheckpoint(),TEXT("new checkpoint while old upload is in flight"))) return;
                Step = 11;
            }
            return;
        }
        if (Step == 11 && Mode == TEXT("exit-online"))
        {
            if (PC->GetQuitState() == EDemoQuitState::Failed) { Check(false,TEXT("newest checkpoint accepted")); return; }
            if (PC->GetQuitState() != EDemoQuitState::Saved) return;
            if (!Check(Cloud->PollExitSync()==EDemoCloudExitState::Complete && PC->IsQuitLocalSaved() && Runs->GetSlot(0)->Coins==73,TEXT("exit waits for latest snapshot after old receipt"))) return;
            UE_LOG(LogFPSDemo,Display,TEXT("DEMO_EXIT_CLOUD_SUCCESS: latest checkpoint confirmed before quit"));
            Step = 14; ExitObservedAt = FPlatformTime::Seconds(); return;
        }
        if (Step == 10 && Mode == TEXT("exit-offline"))
        {
            if (PC->GetQuitState() != EDemoQuitState::Failed) return;
            if (!Check(PC->IsQuitLocalSaved() && GetWorld()->IsPaused(),TEXT("network failure preserves local save and paused prompt"))) return;
            PC->RetryQuitSave(); Step = 12; return;
        }
        if (Step == 12)
        {
            if (PC->GetQuitState() != EDemoQuitState::Failed) return;
            PC->CancelQuitSave();
            if (!Check(PC->GetMenuPage()==EDemoMenuPage::Pause && GetWorld()->IsPaused(),TEXT("cloud failure cancel restores pause"))) return;
            PC->QuitPressed(); Step = 13; return;
        }
        if (Step == 13)
        {
            if (PC->GetQuitState() != EDemoQuitState::Failed) return;
            PC->QuitWithLocalSave();
            if (!Check(PC->GetQuitState()==EDemoQuitState::Saved && Runs->GetSlot(0)->Coins==41,TEXT("explicit local-only choice preserves checkpoint"))) return;
            UE_LOG(LogFPSDemo,Display,TEXT("DEMO_EXIT_OFFLINE_SUCCESS: retry/cancel/local-only quit after real HTTP failure"));
            Step = 14; ExitObservedAt = FPlatformTime::Seconds(); return;
        }
        if (Step == 14 && FPlatformTime::Seconds()-ExitObservedAt > 3) Check(false,TEXT("saved feedback must finish by quitting process"));
        return;
    }
    if (Step == 0)
    {
        if (Mode != TEXT("offline") && !bSynced) return;
        if (Mode == TEXT("restore"))
        {
            if (!Check(Profile->IsUnlocked(TEXT("rifle")) && !Profile->IsUnlocked(TEXT("sniper")), TEXT("permanent unlock restored from server"))) return;
            if (!Check(Runs->GetSlot(0) && Runs->GetSlot(0)->CompletedLevel == 10 && Runs->GetSlot(0)->Phase == EDemoPhase::Victory, TEXT("checkpoint restored after local files deleted"))) return;
            if (!Check(Player->GetWeaponComponent()->GetPrimaryIndex() == INDEX_NONE && Player->GetWeaponComponent()->GetActiveSlot() == 2, TEXT("cloud unlock does not auto equip primary"))) return;
            UE_LOG(LogFPSDemo, Display, TEXT("DEMO_CLOUD_RESTORE_SUCCESS"));
            SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 0); return;
        }
        if (!Check(Runs->CreateSlot(0) && Game->StartRun() && Game->SelectDifficulty(EDemoDifficulty::Easy), TEXT("fresh isolated run starts in hub"))) return;
        PC->OnRunReady(); Game->StartNextLevel(); Step = 1; return;
    }
    if (Step == 1)
    {
        if (State->Phase == EDemoPhase::Combat)
        { // 显式大括号使Reward分支归属于阶段判断，而不是循环内的IsAlive。
            for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 真实GE击杀，让GameMode处理金币、奖励、Boss和最终通关。
                if (It->IsAlive()) DemoEffects::Apply(Player->GetDemoASC(), It->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -100000.f);
        }
        else if (State->Phase == EDemoPhase::Reward) { PC->SelectUpgrade(0); Game->StartNextLevel(); }
        else if (State->Phase == EDemoPhase::Victory)
        {
            if (!Check(Profile->IsUnlocked(TEXT("rifle")) && Runs->GetSlot(0) && Runs->GetSlot(0)->CompletedLevel == 10, TEXT("full real campaign produces unlock and final checkpoint"))) return;
            Step = 2;
        }
        return;
    }
    if (Mode == TEXT("offline"))
    {
        if (!Check(!Profile->GetData().Clears.IsEmpty() && Profile->GetData().SyncedRevision < Profile->GetData().LocalRevision && Profile->CanSync(), TEXT("offline victory safely persists pending progress"))) return;
        UE_LOG(LogFPSDemo, Display, TEXT("DEMO_CLOUD_OFFLINE_SUCCESS"));
    }
    else
    {
        if (!bSynced) return;
        if (!Check(Profile->GetData().Clears.IsEmpty() && Profile->GetData().CloudClearedDifficulties.Contains(TEXT("easy")), TEXT("HTTP confirmation drains only accepted records"))) return;
        UE_LOG(LogFPSDemo, Display, TEXT("DEMO_CLOUD_UPLOAD_SUCCESS"));
    }
    SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 0);
}
