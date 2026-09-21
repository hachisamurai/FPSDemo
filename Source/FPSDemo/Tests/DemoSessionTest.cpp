#include "Tests/DemoSessionTest.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoCloudSync.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Characters/DemoCharacter.h"
#include "Game/FPSDemoGameMode.h"
#include "Interaction/DemoInteractable.h"
#include "Save/DemoRunSave.h"
#include "Settings/DemoGameUserSettings.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoGameplayAbility.h"
#include "GAS/DemoTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AI/DemoEnemy.h"
#include "UI/DemoHUD.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "HAL/PlatformFileManager.h"

namespace
{
    // 专用单进程跨OpenLevel步骤，仅-DemoSessionTest创建Actor；不污染正常游戏。
    int32 SessionStep = 0;
    FDateTime SlotCreatedAt; // 首次创建时间，跨旅行验证不可被最近保存时间替换。
    double PausedWorldTime = 0; // World时间为双精度，不能截断为float后做精确暂停断言。
    float OriginalSensitivity = 1; // 隔离设置文件中的原鼠标倍率，用于判断实际应用。
    FIntPoint OriginalResolution; // 显示超时应恢复的分辨率，像素。
    int32 SavedCoins = 0; // 清关后经济快照，验证读档不会重复发放击杀奖励。
    int32 VictoryRound = 0; // 专项实际通关轮数：先简单，再普通，随后确认可进入困难。
    TArray<double> VictoryValues; // 经济/GAS/技能/弹药数值快照，跨OpenLevel校验完全保留。
    FString CompletedRunId; // 刚完成战役的ID，续玩必须不同且重复读取不能新增解锁记录。
    int32 BeforeClearGold = 0; // 实际清关前金币，验证击杀不发金币且清关只发一次。
    int32 ExpectedClearGold = 0; // 由真实DataTable读取的本关金币奖励，不硬编码测试值。
    int32 BeforeClearSilver = 0; // 击杀前银币，逐怪累计实例快照后验证准确到账。
    FString ExitReadOnlyPath; // 退出专项仅暂时锁本GI隔离Profile文件；失败/成功都恢复可写。
}
ADemoSessionTest::ADemoSessionTest()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickInterval = 0;
    NextTime = FPlatformTime::Seconds()+1.0;
    // 配合-DemoSessionTest启动复用隔离存档/设置；仅首个World选择专项步骤，旅行不重置进度。
    if (SessionStep == 0 && FParse::Param(FCommandLine::Get(),TEXT("DemoVictoryContinuationTest"))) SessionStep = 100;
    if (SessionStep == 0 && FParse::Param(FCommandLine::Get(),TEXT("DemoExitSaveTest"))) SessionStep = 200; // 独立快测，不依赖旧战役步骤。
}
bool ADemoSessionTest::Check(bool Condition, const TCHAR* Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Display, TEXT("SESSION_TEST %s step=%d: %s"),Condition?TEXT("PASS"):TEXT("FAIL"),SessionStep,Message);
    if (!Condition)
    {
        // 测试失败也释放本测试设置的只读属性，确保Editor临时存档仍能在Deinitialize清理。
        if (!ExitReadOnlyPath.IsEmpty()) FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ExitReadOnlyPath,false);
        bFailed=true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,1);
    }
    return Condition;
}
void ADemoSessionTest::Advance(int32 Next, double Delay) { DEMO_LOG_CALL(); SessionStep=Next; NextTime=FPlatformTime::Seconds()+Delay; }
void ADemoSessionTest::Capture(const TCHAR* Name)
{
    DEMO_LOG_CALL();
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoSessionCapture"))) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/Session")/FString(Name)+TEXT(".png"),false,false);
}
void ADemoSessionTest::Escape(bool bDown)
{
    DEMO_LOG_CALL();
    UGameplayStatics::GetPlayerController(this,0)->InputKey(FInputKeyParams(EKeys::Escape,bDown?IE_Pressed:IE_Released,bDown?1.0:0.0));
}
bool ADemoSessionTest::Click(FName Name)
{
    DEMO_LOG_CALL();
    AHUD* HUD = UGameplayStatics::GetPlayerController(this,0)->GetHUD(); // 当前帧实际Canvas热区。
    if (!Check(HUD && HUD->GetHitBoxWithName(Name), *FString::Printf(TEXT("button exists: %s"),*Name.ToString()))) return false;
    HUD->NotifyHitBoxClick(Name);
    return true;
}
TArray<double> ADemoSessionTest::CaptureProgress() const
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前World经济，不跨地图借用。
    const ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 测试步骤已确认依赖就绪。
    const UDemoAttributeSet* Attributes = Player->GetDemoAttributes(); // GAS数值视图。
    const UDemoWeaponComponent* Equipment = Player->GetWeaponComponent(); // 当前实例库存/武器槽。
    const ADemoWeaponBase* Weapon = Equipment->GetActiveWeapon(); // 当前枪的独立弹药。
    TArray<double> Values; // double无损容纳int32及float，以顺序比较全部保留字段。
    Values.Add(State->Coins); Values.Add(State->TotalKills); Values.Add(State->Purchases); // 钱包、统计和下次价格依据。
    Values.Add(Attributes->GetHealth()); Values.Add(Attributes->GetMaxHealth()); // 当前HP和上限都不能被StartRun默认值覆盖。
    Values.Add(Attributes->GetWeaponDamageBonus()); Values.Add(Attributes->GetMagazineBonus()); // 两个全武器GAS加成。
    Values.Add(Player->GetHealAmount()); Values.Add(Player->GetDashSpeed()); // Pawn持有的两类技能成长。
    Values.Add(Equipment->GetPrimaryIndex()); Values.Add(Equipment->GetActiveSlot()); // 保留选定型号和主副槽。
    Values.Add(Weapon->GetAmmo()); Values.Add(Weapon->GetReserveAmmo()); // 非满弹药用于识别意外补满/初始化。
    Values.Add(State->SilverCoins); Values.Add(State->GoldPurchases); Values.Add(State->SilverPurchases); // 双币余额和独立价格也必须跨普通检查点恢复。
    for (int32 Choice=0; Choice<3; ++Choice) { Values.Add(State->UpgradeProgress.GoldLevels[Choice]); Values.Add(State->UpgradeProgress.SilverLevels[Choice]); } // 六项价格需逐项恢复。
    Values.Add(State->UpgradeProgress.PermanentDamage); Values.Add(State->UpgradeProgress.PermanentHealth); Values.Add(State->UpgradeProgress.PermanentMagazine); // 来源不能被总属性替代。
    return Values;
}
bool ADemoSessionTest::VerifyPlayerSkills(bool bAllowed)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 同步夹具，只借用当前World的角色。
    UDemoAbilitySystemComponent* ASC = Player ? Player->GetDemoASC() : nullptr; // 真实授予Dash/Heal的ASC，不直接调用动作替代GAS。
    if (!Check(ASC && Player->GetDemoAttributes(),TEXT("skill phase probe dependencies"))) return false;
    const float SavedHealth = Player->GetDemoAttributes()->GetHealth(); // 测试结束恢复原生命，不改变检查点经济/成长断言。
    const float MaxHealth = Player->GetDemoAttributes()->GetMaxHealth(); // 当前永久+临时上限，不假定100HP。
    FGameplayTagContainer Cooldowns; // 仅隔离本次测试的两个冷却，生产阶段切换不主动刷新冷却。
    Cooldowns.AddTag(DemoTags::DashCooldown); Cooldowns.AddTag(DemoTags::HealCooldown);
    ASC->RemoveActiveEffectsWithGrantedTags(Cooldowns);
    ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), MaxHealth-20.f); // 明确非满血，防止满血拒绝掩盖菜单权限错误。
    const bool bDash = ASC->ActivateDemoAbility(UDemoDashAbility::StaticClass()); // 真实GA Commit应创建冷却和躲避窗口。
    const bool bHeal = ASC->ActivateDemoAbility(UDemoHealAbility::StaticClass()); // 真实GE应恢复缺失的20HP。
    if (!Check(bDash==bAllowed && bHeal==bAllowed && Player->CanUsePlayerSkills()==bAllowed,TEXT("Dash/Heal follow playable phase and modal restrictions"))) return false;
    if (bAllowed)
    {
        if (!Check(Player->IsDashEvading() && ASC->GetCooldownRemaining(DemoTags::DashCooldown)>3.f && ASC->GetCooldownRemaining(DemoTags::HealCooldown)>11.f
            && Player->GetDemoAttributes()->GetHealth()==MaxHealth && !ASC->ActivateDemoAbility(UDemoDashAbility::StaticClass()),TEXT("out-of-combat skills execute GE, evade window and real cooldown"))) return false;
        ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),MaxHealth-20.f); // 非满血下重复治疗也必须被冷却拒绝。
        if (!Check(!ASC->ActivateDemoAbility(UDemoHealAbility::StaticClass()),TEXT("heal cooldown rejects injured repeat"))) return false;
    }
    else if (!Check(ASC->GetCooldownRemaining(DemoTags::DashCooldown)==0 && ASC->GetCooldownRemaining(DemoTags::HealCooldown)==0
        && Player->GetDemoAttributes()->GetHealth()==MaxHealth-20.f,TEXT("blocked skill does not heal or consume cooldown"))) return false;
    if (!Check(!ASC->ActivateDemoAbility(UDemoFireAbility::StaticClass()),TEXT("noncombat skill access does not enable weapon fire"))) return false;
    ASC->RemoveActiveEffectsWithGrantedTags(Cooldowns);
    ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),MaxHealth);
    if (bAllowed && !Check(!ASC->ActivateDemoAbility(UDemoHealAbility::StaticClass()) && ASC->GetCooldownRemaining(DemoTags::HealCooldown)==0,TEXT("full-health heal still rejects without cooldown"))) return false;
    ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),SavedHealth);
    Player->ClearAttackStatuses();
    Player->GetCharacterMovement()->ClearAccumulatedForces(); // LaunchCharacter排队的冲量必须清除，不能在后续步骤把角色推出终端。
    Player->GetCharacterMovement()->StopMovementImmediately();
    return true;
}
void ADemoSessionTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    if (bFailed || FPlatformTime::Seconds()<NextTime) return;
    // 每步重新取得World对象，跨地图只保留上方值类型，不复用旧Pawn/Controller。
    ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 当前本地输入路由借用。
    ADemoCharacter* Player = PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 当前World的Avatar，旅行后重新查找。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前权威流程入口。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 仅本步骤访问的关卡与经济数据。
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI持有的隔离测试栏位。
    UDemoGameUserSettings* Settings = Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // Engine持有，命令行指向测试INI。
    if (!Check(PC && Player && Mode && State && Saves && Settings,TEXT("runtime and settings class ready"))) return;
    const UDemoAttributeSet* Attributes = Player->GetDemoAttributes(); // 本步只读GAS视图。
    switch(SessionStep)
    {
    case 200:
        // 大厅取消必须解除本流程新增的暂停，重复退出/离线按钮不得穿透初始保存阶段。
        PC->QuitPressed(); PC->QuitPressed(); PC->QuitWithLocalSave();
        if (!Check(PC->GetMenuPage()==EDemoMenuPage::QuitSaving && PC->GetQuitState()==EDemoQuitState::SavingLocal && GetWorld()->IsPaused(),TEXT("quit starts paused modal; duplicate and early offline exit rejected"))) return;
        PC->CancelQuitSave();
        if (!Check(PC->GetMenuPage()==EDemoMenuPage::None && !GetWorld()->IsPaused(),TEXT("cancel from lobby restores unpaused lobby"))) return;
        if (!Check(Saves->CreateSlot(0) && Mode->StartRun(),TEXT("exit test owns isolated checkpoint"))) return;
        PC->OnRunReady(); PC->EscapePressed(); PC->QuitPressed();
        Advance(201,.3); break;
    case 201:
        if (!Check(PC->GetQuitState()==EDemoQuitState::Saved && PC->IsQuitLocalSaved(),TEXT("paused local-only exit saves profile and checkpoint"))) return;
        PC->CancelQuitSave();
        if (!Check(PC->GetMenuPage()==EDemoMenuPage::Pause && GetWorld()->IsPaused(),TEXT("cancel success countdown restores original pause menu"))) return;
        Advance(202,1.5); break;
    case 202:
        if (!Check(PC->GetMenuPage()==EDemoMenuPage::Pause,TEXT("cancelled success deadline cannot later quit"))) return;
        {
            UDemoPlayerProfile* ExitProfile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 仅测试随机槽，绝不触碰正式Profile。
            ExitReadOnlyPath = FPaths::ProjectSavedDir()/TEXT("SaveGames")/(ExitProfile->GetSlotName()+TEXT(".sav"));
            if (!Check(ExitProfile->GetSlotName().StartsWith(TEXT("DemoEditorProfile_")) || ExitProfile->GetSlotName().Contains(TEXT("Test")),TEXT("write failure injection restricted to isolated profile"))) return;
            if (!Check(FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ExitReadOnlyPath,true),TEXT("inject actual disk write rejection"))) return;
        }
        PC->QuitPressed(); Advance(203,.3); break;
    case 203:
        if (!Check(PC->GetQuitState()==EDemoQuitState::Failed && !PC->IsQuitLocalSaved(),TEXT("local write failure remains in modal and blocks offline exit"))) return;
        PC->QuitWithLocalSave();
        if (!Check(PC->GetQuitState()==EDemoQuitState::Failed,TEXT("local failure cannot claim saved or exit"))) return;
        Capture(TEXT("08-ExitLocalFailure"));
        Advance(204,.3); break;
    case 204:
        if (!Check(FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ExitReadOnlyPath,false),TEXT("restore writable isolated profile"))) return;
        ExitReadOnlyPath.Empty();
        if (!Click(TEXT("QuitRetry"))) return;
        Advance(205,.3); break;
    case 205:
        if (!Check(PC->GetQuitState()==EDemoQuitState::Saved && PC->IsQuitLocalSaved(),TEXT("retry writes both stores and shows saved state before exit"))) return;
#if WITH_EDITOR
        if (!Check(!GetGameInstance()->GetSubsystem<UDemoCloudSync>(),TEXT("editor quit does not create cloud subsystem"))) return;
#endif
        Capture(TEXT("09-ExitSaved"));
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_EXIT_SAVE_SUCCESS: lobby/pause cancellation, no delayed quit, disk failure guard, retry and saved feedback"));
        Advance(206,3.0); break;
    // 100以上是通关续玩专项：两次完整实际清关、终端解锁装备、Hub读档和旧Victory读档。
    case 100:
        PC->StartGamePressed(); PC->SelectSaveSlot(0);
        Advance(101); PC->ConfirmCreateSave(true); break;
    case 101:
        if (!Check(State->Phase==EDemoPhase::Hub && Saves->GetActiveSlot()==0,TEXT("victory fixture creates real isolated slot"))) return;
        SlotCreatedAt=Saves->GetSlot(0)->CreatedLocal;
        if (!VerifyPlayerSkills(true)) return; // 初始安全区与通关安全区使用同一许可，不依赖解锁档案。
        Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetNextLevelTerminal()->Interact(Player); PC->DifficultyPressed(EDemoDifficulty::Easy);
        Advance(102); break;
    case 102:
        {
        if (!Check(State->Phase==EDemoPhase::Combat,TEXT("continuation campaign actually enters combat"))) return;
        FDemoLevelRow Level; FDemoDifficultyRow Difficulty; FString Error; // 本帧配置值，前九关金币为0，只有最终关按难度计算整轮奖励。
        if (!Check(Mode->GetLevelConfig(State->LevelNumber,Level,Difficulty,Error),TEXT("currency reward config available"))) return;
        BeforeClearGold=State->Coins; ExpectedClearGold=State->LevelNumber==DemoCombatConfig::LevelCount?Difficulty.VictoryGoldReward:0; BeforeClearSilver=State->SilverCoins;
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 实际注册怪经GAS死亡推进，不直接伪造Victory或解锁状态。
        {
            BeforeClearSilver+=It->GetCoinReward();
            It->SetActorTickEnabled(false); DemoEffects::Apply(Player->GetDemoASC(),It->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-100000.f);
            Mode->NotifyEnemyKilled(*It); // 真实死亡后的重复通知必须不再发银币。
        }
        if (!Check(State->Coins==BeforeClearGold && State->SilverCoins==BeforeClearSilver,TEXT("kills and duplicate callbacks only award exact silver, never gold"))) return;
        Advance(103); break;
        }
    case 103:
        if (!Check(State->Coins==BeforeClearGold+ExpectedClearGold,TEXT("complete stage awards configured gold exactly once"))) return;
        if (State->Phase==EDemoPhase::Reward)
        {
            PC->SelectUpgrade(State->LevelNumber%3); // 覆盖伤害/治疗/冲刺三种成长。
            if (State->LevelNumber==1)
            {
                Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
                Mode->GetShopTerminal()->Interact(Player);
                if (!Check(Mode->GetUpgradeCost()==20,TEXT("silver prices independent of hub gold purchases"))) return;
                PC->SelectUpgrade(1); PC->SelectUpgrade(2); // 实际关间购买生命/容量，只消耗银币。
                if (!Check(State->Coins==BeforeClearGold+ExpectedClearGold && State->SilverCoins==BeforeClearSilver-40 && State->SilverPurchases==2 && Mode->GetUpgradeCost(0)==20 && Mode->GetUpgradeCost(1)==30 && Mode->GetUpgradeCost(2)==30,TEXT("arena purchases cost 20 each; other attributes retain their own prices"))) return;
                Capture(TEXT("11-SilverTerminal")); Advance(130); break;
            }
            Mode->StartNextLevel(); Advance(102);
        }
        else
        {
            if (!Check(State->Phase==EDemoPhase::Victory && State->LevelNumber==10,TEXT("ten real clears reach victory"))) return;
            if (!VerifyPlayerSkills(false)) return; // 结算为模态页，返回可行走Hub后才允许技能。
            if (!Check(State->SilverCoins==0 && State->Purchases==(VictoryRound==0?0:4) && !Mode->HasRunUpgrades() && Attributes->GetHealth()==(VictoryRound==0?100:125) && Attributes->GetWeaponDamageBonus()==(VictoryRound==0?0:10) && Attributes->GetMagazineBonus()==(VictoryRound==0?0:4) && Player->GetHealAmount()==35 && Player->GetDashSpeed()==1300,TEXT("victory clears silver/free upgrades while retaining permanent gold health/damage/magazine and prices"))) return;
            if (!Check(Mode->SaveCheckpoint(),TEXT("real victory snapshot persists"))) return;
            VictoryValues=CaptureProgress(); CompletedRunId=Saves->GetSlot(0)->RunId; ++VictoryRound;
            Capture(TEXT("08-VictoryContinue")); Advance(104);
        }
        break;
    case 130:
        if (!Check(PC->GetMenuMessage().Contains(TEXT("银币")) && !PC->GetMenuMessage().Contains(TEXT("金币")),TEXT("arena purchase feedback names actual silver currency"))) return;
        if (!VerifyPlayerSkills(false)) return; // 终端仍打开，不能按技能穿透菜单。
        PC->CloseUpgradeMenu();
        if (!VerifyPlayerSkills(true)) return; // 清关领奖后自由备战，冲刺/治疗不再被Combat限定拒绝。
        PC->EscapePressed();
        if (!VerifyPlayerSkills(false)) return; // 暂停时直接请求GA也不能绕过限制。
        PC->EscapePressed(); Mode->StartNextLevel(); Advance(102); break;
    case 104:
        if (VictoryRound==1)
        {
            if (!Click(TEXT("Restart"))) return; // 真实结算热区，应该回Hub而不是旅行大厅。
            Advance(105);
        }
        else
        {
            UDemoRunSave* Legacy = DuplicateObject<UDemoRunSave>(Saves->GetSlot(0),this); // 本帧复制真实Victory，稍后注入未清理临时成长模拟已有终局文件，不伪造二进制。
            Legacy->MaxHealth=250; Legacy->Health=150; Legacy->DamageBonus=45; Legacy->MagazineBonus=8; Legacy->HealAmount=95; Legacy->DashSpeed=2000; // V3终局读档也需幂等清除临时成长，但保留金币账本；V1/V2迁移由Editor专项验证。
            PC->EscapePressed(); PC->ReturnHubPressed(); // Victory暂停入口也保留金币/解锁，清空成长，不出现放弃确认。
            if (!Check(State->Phase==EDemoPhase::Hub && !GetWorld()->IsPaused() && CaptureProgress()==VictoryValues,TEXT("victory pause return preserves gold and reset baseline and unpauses"))) return;
            if (!Check(UGameplayStatics::SaveGameToSlot(Legacy,Saves->SlotName(0),0),TEXT("legacy victory fixture saved by UE"))) return;
            Advance(110); Mode->TravelToRun(false); // 显式测试旅行保留刚写的旧Victory文件，下一页重新从磁盘选槽。
        }
        break;
    case 105:
        if (!Check(State->Phase==EDemoPhase::Hub && State->LevelNumber==0 && Mode->GetShopTerminal() && Mode->GetNextLevelTerminal()
            && !PC->IsMoveInputIgnored() && !PC->HasBlockingOverlay() && CaptureProgress()==VictoryValues
            && Saves->GetSlot(0)->RunId!=CompletedRunId && Saves->GetSlot(0)->CreatedLocal==SlotCreatedAt,TEXT("victory button retains gold/unlocks and reset baseline values, restores terminals/input and creates next run ID"))) return;
        Mode->RestartDemo(); // 残留结算点击不能再次重置/返回大厅。
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetShopTerminal()->Interact(Player); PC->SetTerminalWeaponPage(true); PC->InspectWeapon(1); PC->EquipInspectedWeapon();
        PC->CloseWeaponTip(); PC->CloseUpgradeMenu(); // 详情和终端是两层菜单，分别关闭才能恢复移动/下一关交互。
        if (!Check(Player->GetWeaponComponent()->GetPrimaryIndex()==0,TEXT("easy clear rifle can be equipped immediately in safe hub"))) return;
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetShopTerminal()->Interact(Player); PC->SetTerminalWeaponPage(false);
        SavedCoins=State->Coins;
        PC->SelectUpgrade(0); PC->SelectUpgrade(0); PC->SelectUpgrade(1); PC->SelectUpgrade(2); // 伤害20+30，另两项各20；购买某项不会带涨其他项目。
        if (!Check(State->Coins==SavedCoins-90 && State->SilverCoins==0 && State->GoldPurchases==4 && State->SilverPurchases==0 && Mode->GetUpgradeCost(0)==40 && Mode->GetUpgradeCost(1)==30 && Mode->GetUpgradeCost(2)==30 && Attributes->GetWeaponDamageBonus()==10 && Attributes->GetMaxHealth()==125 && Attributes->GetMagazineBonus()==4,TEXT("hub spends 20+30 on damage and 20 each on health/magazine; prices independent"))) return;
        Capture(TEXT("12-GoldTerminal")); Advance(131); break;
    case 131:
        PC->CloseUpgradeMenu();
        if (!VerifyPlayerSkills(true)) return; // 实际通关后的Hub，同时覆盖永久生命125下的治疗上限。
        VictoryValues=CaptureProgress();
        Capture(TEXT("09-VictoryHub")); Advance(106); break;
    case 106:
        PC->EscapePressed(); Advance(107); PC->ReturnLobbyPressed(); break;
    case 107:
        PC->StartGamePressed(); PC->SelectSaveSlot(0); Advance(108); break;
    case 108:
        if (!Check(State->Phase==EDemoPhase::Hub && CaptureProgress()==VictoryValues && !PC->IsMoveInputIgnored(),TEXT("post-victory hub reload restores equipped rifle and full progression"))) return;
        Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetNextLevelTerminal()->Interact(Player);
        if (!VerifyPlayerSkills(false)) return; // 难度出发菜单也阻止技能，关闭后继续原战役回归。
        PC->DifficultyPressed(EDemoDifficulty::Normal);
        if (!Check(State->Phase==EDemoPhase::Combat && State->Difficulty==EDemoDifficulty::Normal && State->LevelNumber==1,TEXT("same save enters higher difficulty from stage one"))) return;
        Advance(102); break;
    case 110:
        PC->StartGamePressed(); PC->SelectSaveSlot(0); Advance(111); break;
    case 111:
        {
            const UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 独立永久档案，必须恰好两次不同难度通关。
            if (!Check(State->Phase==EDemoPhase::Hub && State->LevelNumber==0 && CaptureProgress()==VictoryValues && !PC->IsMoveInputIgnored()
                && Saves->GetSlot(0)->Phase==EDemoPhase::Hub && Saves->GetSlot(0)->RunId!=CompletedRunId && Saves->GetSlot(0)->CreatedLocal==SlotCreatedAt
                && Profile->GetData().Clears.Num()==2 && Profile->IsUnlocked(TEXT("rifle")) && Profile->IsUnlocked(TEXT("shotgun")),TEXT("legacy victory resumes hub, preserves gold and clears legacy upgrades and never duplicates clear records"))) return;
            Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
            Mode->GetShopTerminal()->Interact(Player); PC->SetTerminalWeaponPage(true); PC->InspectWeapon(2); PC->EquipInspectedWeapon();
            PC->CloseWeaponTip(); PC->CloseUpgradeMenu(); // 装备按钮保留详情，离开终端前需关闭两层窗口。
            if (!Check(Player->GetWeaponComponent()->GetPrimaryIndex()==1,TEXT("second difficulty independently unlocks usable shotgun"))) return;
            Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(-180,0,20));
            Mode->GetNextLevelTerminal()->Interact(Player); PC->DifficultyPressed(EDemoDifficulty::Hard);
            if (!Check(State->Phase==EDemoPhase::Combat && State->Difficulty==EDemoDifficulty::Hard && State->LevelNumber==1,TEXT("migrated existing save can play next higher difficulty"))) return;
            Capture(TEXT("10-VictoryNextDifficulty")); Advance(140);
        }
        break;
    case 140:
        // 获得真实银币后死亡，不清关，不得新增金币；死亡页面直接回大厅再读档检验持久化重置。
        SavedCoins=State->Coins;
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 冻结AI防止测试操作期间非确定性伤害，只击杀一只。
        { It->SetActorTickEnabled(false); }
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It)
        { DemoEffects::Apply(Player->GetDemoASC(),It->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-100000.f); break; }
        Player->ApplyAbilityReward(0); Player->ApplyAbilityReward(1); // 临时成长必须随着死亡清掉。
        if (!Check(State->SilverCoins>0 && State->Coins==SavedCoins,TEXT("unfinished combat earns silver but no clear gold"))) return;
        DemoEffects::Apply(Player->GetDemoASC(),Player->GetDemoASC(),UDemoHealthEffect::StaticClass(),-100000.f);
        if (!Check(State->Phase==EDemoPhase::Defeat && State->SilverCoins==0 && Saves->GetSlot(0)->Phase==EDemoPhase::Hub && Saves->GetSlot(0)->Coins==SavedCoins && Saves->GetSlot(0)->SilverCoins==0 && Saves->GetSlot(0)->DamageBonus==10 && Saves->GetSlot(0)->MaxHealth==125 && Saves->GetSlot(0)->MagazineBonus==4,TEXT("death immediately persists fresh hub retaining gold and clearing silver/growth"))) return;
        PC->EscapePressed(); Advance(141); PC->ReturnLobbyPressed(); break;
    case 141:
        PC->StartGamePressed(); PC->SelectSaveSlot(0); Advance(142); break;
    case 142:
        if (!Check(State->Phase==EDemoPhase::Hub && State->Coins==SavedCoins && State->SilverCoins==0 && !Mode->HasRunUpgrades() && State->Purchases==4 && Attributes->GetWeaponDamageBonus()==10 && Attributes->GetMaxHealth()==125 && Attributes->GetMagazineBonus()==4 && Mode->GetUpgradeCost(0)==40 && Mode->GetUpgradeCost(1)==30 && Mode->GetUpgradeCost(2)==30
            && GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()->IsUnlocked(TEXT("shotgun")),TEXT("death reload retains permanent gold stats/prices and unlocks, removes temporary growth/silver"))) return;
        // 已在Hub时“返回安全区”只关闭暂停，不重载World；下一步验证永久属性和价格保持不变。
        PC->EscapePressed(); Advance(143); PC->ReturnHubPressed(); break;
    case 143:
        if (!Check(State->Phase==EDemoPhase::Hub && !PC->HasBlockingOverlay() && Attributes->GetWeaponDamageBonus()==10
            && Attributes->GetMaxHealth()==125 && Attributes->GetMagazineBonus()==4 && Mode->GetUpgradeCost(0)==40 && State->Coins==SavedCoins,
            TEXT("permanent-only return skips confirmation and keeps all gold progression"))) return;
        Advance(112); break;
    case 112:
        Escape(true); Advance(113); break;
    case 113:
        Escape(false);
        UE_LOG(LogFPSDemo, Display,TEXT("DEMO_VICTORY_CONTINUATION_SUCCESS: two campaigns, difficulty victory gold only, permanent stats and per-attribute prices, death reload and permanent-only return"));
        if (!Click(TEXT("PauseQuit"))) return;
        Advance(114,3); break;
    case 114:
        Check(false,TEXT("victory test quit must exit process")); break;
    case 0:
        if (!Check(State->Phase==EDemoPhase::Lobby && !PC->GetHUD()->GetHitBoxWithName(TEXT("Normal")) && !Mode->SelectDifficulty(EDemoDifficulty::Hard),TEXT("lobby has no difficulty controls and rejects difficulty mutation"))) return;
        Capture(TEXT("00-Lobby"));
        Advance(40); break;
    case 40:
        if (!Click(TEXT("StartGame"))) return;
        Advance(1); break;
    case 1:
        if (!Check(State->Phase==EDemoPhase::Lobby && PC->GetMenuPage()==EDemoMenuPage::Saves && !Saves->SlotExists(0) && !PC->GetHUD()->GetHitBoxWithName(TEXT("StartGame")),TEXT("start hides main UI and displays empty slots without entering game"))) return;
        Capture(TEXT("01-Saves"));
        Advance(41); break;
    case 41:
        if (!Click(TEXT("Save0"))) return;
        PC->ConfirmCreateSave(false);
        if (!Check(!Saves->SlotExists(0) && PC->GetMenuPage()==EDemoMenuPage::Saves,TEXT("cancel creation writes no file"))) return;
        PC->SelectSaveSlot(0);
        Advance(2); break;
    case 2:
        Capture(TEXT("02-CreateConfirmation"));
        Advance(42); break;
    case 42:
        Advance(3);
        if (!Click(TEXT("CreateYes"))) return;
        break;
    case 3:
        if (!Check(State->Phase==EDemoPhase::Hub && Saves->GetActiveSlot()==0 && State->Coins==0 && !PC->HasBlockingOverlay(),TEXT("created slot enters fresh hub through new World"))) return;
        SlotCreatedAt=Saves->GetSlot(0)->CreatedLocal;
        if (!Check(FMath::Abs((FDateTime::Now()-SlotCreatedAt).GetTotalSeconds())<60,TEXT("slot records actual local creation time"))) return;
        Escape(true); Advance(4); break;
    case 4:
        Escape(false);
        if (!Check(GetWorld()->IsPaused() && PC->GetMenuPage()==EDemoMenuPage::Pause,TEXT("physical Esc pauses real World"))) return;
        PausedWorldTime=GetWorld()->GetTimeSeconds();
        Capture(TEXT("03-Pause"));
        Advance(5,.7); break;
    case 5:
        if (!Check(GetWorld()->GetTimeSeconds()==PausedWorldTime,TEXT("paused World time does not advance"))) return;
        Escape(true); Advance(47); break;
    case 47:
        Escape(false);
        if (!Check(!GetWorld()->IsPaused() && !PC->HasBlockingOverlay() && !PC->IsMoveInputIgnored(),TEXT("second physical Esc resumes World and gameplay input"))) return;
        Escape(true); Advance(48); break;
    case 48:
        Escape(false);
        if (!Check(GetWorld()->IsPaused(),TEXT("Esc can pause again after resume"))) return;
        if (!Click(TEXT("PauseSettings"))) return;
        Advance(6); break;
    case 6:
        Capture(TEXT("04-Settings"));
        Advance(43); break;
    case 43:
        OriginalSensitivity=Settings->GetMouseSensitivity();
        OriginalResolution=Settings->GetScreenResolution();
        // 画质使用完整Engine标度档，不把UI草稿变化当作实际应用。
        PC->CycleSetting(1,1);
        PC->CycleSetting(2,OriginalSensitivity<2.9f?1:-1);
        PC->ApplyUserSettings();
        if (!Check(Settings->GetOverallScalabilityLevel()>=0 && Settings->GetOverallScalabilityLevel()<=3,TEXT("quality applies to Engine scalability groups"))) return;
        if (!Check(!FMath::IsNearlyEqual(Settings->GetMouseSensitivity(),OriginalSensitivity),TEXT("sensitivity setting actually applies"))) return;
        Settings->LoadSettings(true);
        if (!Check(!FMath::IsNearlyEqual(Settings->GetMouseSensitivity(),OriginalSensitivity),TEXT("sensitivity persists in isolated settings INI"))) return;
        PC->CycleSetting(0,1); PC->ApplyUserSettings();
        if (!Check(PC->GetDisplayConfirmSeconds()>0,TEXT("display change starts confirmation while paused"))) return;
        Advance(7,16.0); break;
    case 7:
        if (!Check(GetWorld()->IsPaused() && PC->GetDisplayConfirmSeconds()==0 && Settings->GetScreenResolution()==OriginalResolution,TEXT("real-time timeout restores display while World stays paused"))) return;
        PC->CloseOverlay();
        Advance(8); break;
    case 8:
        Advance(9);
        if (!Click(TEXT("PauseHub"))) return;
        break;
    case 9:
        if (!Check(State->Phase==EDemoPhase::Hub && !GetWorld()->IsPaused() && Saves->GetSlot(0)->CreatedLocal==SlotCreatedAt,TEXT("no upgrades returns directly to fresh safe hub and preserves slot date"))) return;
        DemoEffects::Apply(Player->GetDemoASC(),Player->GetDemoASC(),UDemoPowerEffect::StaticClass(),5.f);
        Player->ApplyAbilityReward(1); // 免费医疗成长也属于返回确认范围。
        Player->GetWeaponComponent()->GetActiveWeapon()->RestoreAmmo(9,83); // 人工构造非满弹药检查点，验证实际武器实例恢复。
        State->Coins=80;
        if (!Check(Mode->SaveCheckpoint(),TEXT("hub upgrades save to active slot"))) return;
        Escape(true); Advance(10); break;
    case 10:
        Escape(false);
        if (!Check(Mode->HasRunUpgrades(),TEXT("detect both GAS and Pawn skill upgrades"))) return;
        Advance(11);
        if (!Click(TEXT("PauseLobby"))) return;
        break;
    case 11:
        if (!Check(State->Phase==EDemoPhase::Lobby && !GetWorld()->IsPaused(),TEXT("return lobby reloads clean unpaused World"))) return;
        if (!Click(TEXT("StartGame"))) return;
        Advance(12); break;
    case 12:
        Capture(TEXT("05-ExistingSave"));
        Advance(44); break;
    case 44:
        Advance(13);
        if (!Click(TEXT("Save0"))) return;
        break;
    case 13:
        if (!Check(State->Phase==EDemoPhase::Hub && State->Coins==80 && Attributes->GetWeaponDamageBonus()==5 && Player->GetHealAmount()==55,TEXT("existing slot restores coins, GAS upgrades and Pawn ability progression"))) return;
        if (!Check(Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo()==9 && Player->GetWeaponComponent()->GetActiveWeapon()->GetReserveAmmo()==83,TEXT("checkpoint restores magazine and reserve independently"))) return;
        Escape(true); Advance(14); break;
    case 14:
        Escape(false);
        PC->ReturnHubPressed();
        if (!Check(PC->GetMenuPage()==EDemoMenuPage::ReturnHub,TEXT("upgraded run requires explicit confirmation"))) return;
        PC->ConfirmReturnHub(false);
        if (!Check(State->Coins==80 && Attributes->GetWeaponDamageBonus()==5 && PC->IsPauseMenuOpen(),TEXT("No closes confirmation only, preserving progression and pause"))) return;
        PC->ReturnHubPressed();
        Advance(15); break;
    case 15:
        Capture(TEXT("06-ReturnConfirmation"));
        Advance(45); break;
    case 45:
        Advance(16);
        if (!Click(TEXT("HubYes"))) return;
        break;
    case 16:
        if (!Check(State->Phase==EDemoPhase::Hub && State->Coins==80 && State->SilverCoins==0 && Attributes->GetWeaponDamageBonus()==0 && Player->GetHealAmount()==35 && Saves->GetSlot(0)->CreatedLocal==SlotCreatedAt,TEXT("confirmed return clears silver/growth, retains gold and slot creation identity"))) return;
        Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetNextLevelTerminal()->Interact(Player);
        PC->ConfirmNextLevel();
        if (!Check(State->Phase==EDemoPhase::Hub,TEXT("Enter cannot bypass explicit difficulty choice"))) return;
        Advance(17); break;
    case 17:
        Capture(TEXT("07-Difficulty"));
        Advance(46); break;
    case 46:
        if (!Click(TEXT("Hard"))) return;
        if (!Check(State->Phase==EDemoPhase::Combat && State->Difficulty==EDemoDifficulty::Hard && !Mode->SelectDifficulty(EDemoDifficulty::Easy),TEXT("terminal difficulty selection starts and locks run"))) return;
        // 战斗中临时奖励不能替换出发前检查点，离开再载入必须恢复起点。
        State->SilverCoins=999; // 战斗未完成银币不能覆盖出发检查点，金币无清关奖励保持80。
        if (!Check(Mode->SaveCheckpoint() && Saves->GetSlot(0)->Phase==EDemoPhase::Hub && Saves->GetSlot(0)->Coins==80 && Saves->GetSlot(0)->SilverCoins==0,TEXT("combat progress cannot overwrite departure checkpoint"))) return;
        Escape(true); Advance(50); break;
    case 50:
        Escape(false); Advance(51); PC->ReturnLobbyPressed(); break;
    case 51:
        PC->StartGamePressed(); PC->SelectSaveSlot(0); Advance(52); break;
    case 52:
        if (!Check(State->Phase==EDemoPhase::Hub && State->Coins==80 && State->SilverCoins==0 && State->Difficulty==EDemoDifficulty::Hard,TEXT("unfinished combat resumes at departure checkpoint without partial loot"))) return;
        Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetNextLevelTerminal()->Interact(Player);
        PC->DifficultyPressed(EDemoDifficulty::Hard);
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 真实注册怪经GE清场，奖励由GameMode下一帧触发。
        { It->SetActorTickEnabled(false); DemoEffects::Apply(Player->GetDemoASC(),It->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-100000.f); }
        Advance(18); break;
    case 18:
        if (!Check(State->Phase==EDemoPhase::Reward && Saves->GetSlot(0)->Phase==EDemoPhase::Reward,TEXT("cleared level saves pending reward checkpoint"))) return;
        Escape(true); Advance(60); break;
    case 60:
        Escape(false);
        PC->SelectUpgrade(0); // 暂停不能通过旧奖励入口穿透领成长。
        if (!Check(State->Phase==EDemoPhase::Reward && Attributes->GetWeaponDamageBonus()==0,TEXT("pause blocks underlying reward input"))) return;
        Advance(61); PC->ReturnLobbyPressed(); break;
    case 61:
        PC->StartGamePressed(); PC->SelectSaveSlot(0); Advance(62); break;
    case 62:
        if (!Check(State->Phase==EDemoPhase::Reward && PC->IsRewardMenu() && Attributes->GetWeaponDamageBonus()==0,TEXT("pending reward checkpoint reopens mandatory reward UI"))) return;
        if (!Click(TEXT("Upgrade0"))) return;
        SavedCoins=State->Coins;
        if (!Check(State->Phase==EDemoPhase::Intermission && Saves->GetSlot(0)->DamageBonus==10,TEXT("reward persisted once with intermission phase"))) return;
        Escape(true); Advance(19); break;
    case 19:
        Escape(false);
        Advance(20);
        if (!Click(TEXT("PauseLobby"))) return;
        break;
    case 20:
        PC->StartGamePressed(); PC->SelectSaveSlot(0);
        Advance(21); break;
    case 21:
        if (!Check(State->Phase==EDemoPhase::Intermission && State->LevelNumber==1 && State->Coins==SavedCoins && Attributes->GetWeaponDamageBonus()==10
            && State->Difficulty==EDemoDifficulty::Hard && Mode->GetShopTerminal() && !PC->IsRewardMenu(),TEXT("read checkpoint restores cleared arena and does not grant reward twice"))) return;
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetShopTerminal()->Interact(Player);
        PC->SelectUpgrade(1);
        if (!Check(Saves->GetSlot(0)->MaxHealth==125 && Saves->GetSlot(0)->Purchases==1 && Saves->GetSlot(0)->SilverPurchases==1
            && Saves->GetSlot(0)->GoldPurchases==0 && Saves->GetSlot(0)->SilverCoins==30 && Saves->GetSlot(0)->Coins==SavedCoins,TEXT("terminal autosaves silver spend, independent price, attributes and retained gold"))) return;
        PC->CloseUpgradeMenu(); Escape(true); Advance(22); break;
    case 22:
        Escape(false); Advance(23); PC->ReturnLobbyPressed(); break;
    case 23:
        PC->StartGamePressed(); PC->SelectSaveSlot(1);
        Advance(24); PC->ConfirmCreateSave(true); break;
    case 24:
        if (!Check(Saves->GetActiveSlot()==1 && State->Phase==EDemoPhase::Hub && Attributes->GetMaxHealth()==100 && State->Coins==0
            && Saves->GetSlot(0)->MaxHealth==125,TEXT("second slot is independent and first checkpoint stays intact"))) return;
        {
            UDemoProfileSave* Invalid = NewObject<UDemoProfileSave>(this); // 用具体但错误的存档类制造槽2样本；USaveGame基类是抽象类，不能实例化。
            UGameplayStatics::SaveGameToSlot(Invalid,Saves->SlotName(2),0);
            Saves->RefreshSlots();
            if (!Check(Saves->SlotExists(2) && !Saves->GetSlot(2) && !Saves->SelectSlot(2) && !Saves->CreateSlot(2),TEXT("corrupt occupied slot is protected from overwrite"))) return;
        }
        Escape(true); Advance(25); break;
    case 25:
        Escape(false);
        UE_LOG(LogFPSDemo, Display,TEXT("DEMO_SESSION_SUCCESS: actual Esc pause, modal UI, settings timeout/persistence, slots, checkpoints, resets and terminal difficulty"));
        if (!Click(TEXT("PauseQuit"))) return;
        // QuitGame在Standalone请求退出；若未退出，3秒后测试失败，不假装退出按钮已生效。
        Advance(26,3.0); break;
    default: Check(false,TEXT("QuitGame should terminate test process")); break;
    }
}
