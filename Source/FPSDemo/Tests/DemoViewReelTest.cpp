// 职责：Editor抽取UI与设备偏好回归。使用：-DemoViewReelTest配隔离GameUserSettingsINI；另加-DemoReelUIOnly可单独验证抽取且明确不运行跳跃检查。
#include "Tests/DemoViewReelTest.h"
#include "Characters/DemoCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h" // 实际InputKey参数定义，独立翻译单元不能依赖传递包含。
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Interaction/DemoInteractable.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoHUD.h" // 续抽验证走真实Canvas按钮白名单与命令链，不仅直接调用业务接口。
#include "Save/DemoRunSave.h"
#include "Settings/DemoGameUserSettings.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "HAL/IConsoleManager.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoEditorCheats.h" // 断言实际控制台设置状态，非Editor实现为空分支。
#include "Economy/DemoShopComponent.h"
#include "Economy/Gambling/DemoGambleService.h"
#include "Economy/Gambling/DemoCasinoSettings.h" // 无限金币免扣不再意味着中奖后余额必须为零。
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoEffects.h"
#include "AI/DemoEnemy.h"

ADemoViewReelTest::ADemoViewReelTest()
{ DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork; }
bool ADemoViewReelTest::Check(bool Value,const TCHAR* Reason)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Display,TEXT("VIEW_REEL_TEST %s stage=%d %s"),Value?TEXT("PASS"):TEXT("FAIL"),Stage,Reason);
    if(!Value){bFailed=true;FPlatformMisc::RequestExitWithStatus(false,1);} return Value;
}
void ADemoViewReelTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    if(bFailed) return;
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoUITest"))) { TickCasino(); return; } // 独立Editor专项，不把赌场UI验收冒充相机通过。
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 仅本专项隔离单人夹具。
    auto* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 每帧重新借用，旅行不持裸指针。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 真实生产事务入口。
    auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 仅专项为关间交互建立阶段夹具。
    const double Now=FPlatformTime::Seconds(); // 真实时间，暂停继续采样UI。
    if(GetWorld()->GetTimeSeconds()<1||!PC||!Player||!Mode||!State) return;
    auto* Flow=PC->GetMenuFlow(); // 本地正式UI状态，不模拟动画值。
    auto* Progress=Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 真实个人经济。
    if(Stage==0)
    {
        Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())->SetGambleAnimationSkipped(false); // 本测试隔离INI，先验证默认动画链。
        auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // Editor每GI临时槽，退出清理。
        if(!Check(Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("temporary slot and real safe hub ready"))) return;
        Saves->ConsumePending(); PC->OnRunReady();
        if(FParse::Param(FCommandLine::Get(),TEXT("DemoReelUIOnly"))) // 仅WITH_EDITOR测试状态机分离UI入口，不改变默认完整测试或生产相机逻辑。
        { UE_LOG(LogFPSDemo,Display,TEXT("JUMP_CAMERA_AUDIT NOT_RUN explicit DemoReelUIOnly")); Stage=30; Deadline=Now; return; }
        GroundZ=Player->GetActorLocation().Z; PeakZ=GroundZ;
        PC->InputKey(FInputKeyParams(EKeys::SpaceBar,IE_Pressed,1.0)); // 真实空格绑定，经CMC预测起跳。
        Stage=1; Deadline=Now+1.8; return;
    }
    if(Stage<=3)
    {
        PeakZ=FMath::Max(PeakZ,static_cast<float>(Player->GetActorLocation().Z));
        if(Player->GetCharacterMovement()->IsFalling())
        {
            ++Samples; MaxDelta=FMath::Max(MaxDelta,DeltaSeconds);
            MaxCameraError=FMath::Max(MaxCameraError,static_cast<float>(FVector::Distance(PC->PlayerCameraManager->GetCameraLocation(),Player->GetFirstPersonCameraComponent()->GetComponentLocation())));
            MaxOffsetError=FMath::Max(MaxOffsetError,static_cast<float>(FMath::Abs(Player->GetFirstPersonCameraComponent()->GetComponentLocation().Z-Player->GetActorLocation().Z-60.)));
        }
        if(Now<Deadline) return;
        PC->InputKey(FInputKeyParams(EKeys::SpaceBar,IE_Released,0.0));
        if(!Check(PeakZ>GroundZ+30&&Player->GetCharacterMovement()->IsMovingOnGround(),TEXT("actual jump rose and landed without forced movement reset"))) return;
        if(Stage<3){++Stage; PeakZ=GroundZ; PC->SetControlRotation(FRotator(0,Stage*35.f,0));PC->InputKey(FInputKeyParams(EKeys::SpaceBar,IE_Pressed,1.0));Deadline=Now+1.8;return;}
        UE_LOG(LogFPSDemo,Display,TEXT("JUMP_CAMERA_AUDIT samples=%d maxCameraErrorCm=%.6f maxOffsetErrorCm=%.6f maxFrameMs=%.3f"),Samples,MaxCameraError,MaxOffsetError,MaxDelta*1000);
        if(!Check(Samples>15&&MaxCameraError<.1f&&MaxOffsetError<.1f,TEXT("rendered camera follows same-frame capsule through all jump frames"))) return;
        if(!Check(IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))->GetInt()==1&&IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSyncEditor"))->GetInt()==1,TEXT("both gameplay and PIE presentation synchronization enabled"))) return;
        Stage=30; // 完整测试也进入同一UI验证阶段，不复制抽取断言或降低相机阈值。
    }
    if(Stage==30)
    {
        FDemoPlayerProgressSnapshot Data=Progress->GetSnapshot(); // 专项提供银币用于真实下注，不伪造抽取结果。
        Data.SilverCoins=1000; Progress->Commit(Data); State->Phase=EDemoPhase::Intermission;State->LevelNumber=1;
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,0));
        Mode->GetShopTerminal()->Interact(Player); Flow->OpenGambleMenu(); Flow->InspectGamble(0); Flow->RequestGamble(); Flow->ConfirmGamble(true); // 属性池持续可抽，验证同节点超过原两次限制。
        const auto& Reel=Flow->GetGambleReel(); // 同步本机可靠回执已经冻结，奖励已在存档。
        Serial=Progress->GetSnapshot().Gambling.Serial; Balance=Progress->GetSnapshot().SilverCoins; ResultId=Reel.ResultId;
        if(!Check(Reel.bRolling&&Reel.Cards.Num()==2&&Serial==1&&Balance==970,TEXT("committed draw starts UI with complete eligible attribute and failure pool"))) return;
        Flow->RequestGamble();Flow->ConfirmGamble(true); // 连点不可提交第二次事务。
        if(!Check(Progress->GetSnapshot().Gambling.Serial==Serial,TEXT("repeat click during reveal cannot charge again"))) return;
        Stage=4;Deadline=Now+.6;return;
    }
    if(Now<Deadline) return;
    if(Stage==4)
    {
        if(!Check(!PC->GetHUD()->GetHitBoxWithName(TEXT("GambleContinue")),TEXT("continue hitbox absent while reel is rolling"))) return; // 完整绘制数帧后检查真实热区。
        LastPosition=Flow->GetGambleReel().Position;
        FScreenshotRequest::RequestScreenshot(TEXT("Gambling-Reel-Rolling.png"),false,false);
        PC->SetLocalMenuPaused(true); Stage=5; Deadline=Now+.7; return; // 不打开暂停覆盖层，专门证明PC暂停Tick仍驱动动画。
    }
    if(Stage==5)
    {
        if(!Check(GetWorld()->IsPaused()&&Flow->GetGambleReel().Position>LastPosition,TEXT("reel progresses in real time while world paused"))) return;
        PC->SetLocalMenuPaused(false);Stage=6;Deadline=Now+2.2;return;
    }
    if(Stage==6)
    {
        const auto& Reel=Flow->GetGambleReel(); // 完成后必须准确落在原已结算ID，不能重新抽选。
        if(!Check(!Reel.bRolling&&!Reel.Cards.IsEmpty()&&Reel.Cards[FMath::RoundToInt(Reel.Position)%Reel.Cards.Num()].Id==ResultId
            &&Progress->GetSnapshot().Gambling.Serial==Serial&&Progress->GetSnapshot().SilverCoins==Balance,TEXT("animation stops on committed result without second transaction"))) return;
        FScreenshotRequest::RequestScreenshot(TEXT("Gambling-Reel-Result.png"),false,false);
        Stage=7;Deadline=Now+.3;return;
    }
    if(Stage==7)
    {
        auto* HUD=PC->GetHUD<ADemoHUD>(); // 本机真实Canvas拥有热区；停稳后一帧才允许检查。
        if(!Check(HUD&&HUD->GetHitBoxWithName(TEXT("GambleContinue"))&&HUD->GetHitBoxWithName(TEXT("GambleNo")),TEXT("settled result exposes continue and return buttons"))) return;
        HUD->NotifyHitBoxClick(TEXT("GambleContinue"));
        if(!Check(Flow->GetGambleReel().bRolling&&Progress->GetSnapshot().Gambling.Serial==Serial+1&&Progress->GetSnapshot().SilverCoins==Balance-30,
            TEXT("one continue click uses fresh serial and charges exactly one new draw"))) return;
        HUD->NotifyHitBoxClick(TEXT("GambleContinue")); // 模拟同一帧缓存按钮的第二次点击。
        if(!Check(Progress->GetSnapshot().Gambling.Serial==Serial+1&&Progress->GetSnapshot().SilverCoins==Balance-30,TEXT("stale continue double click cannot charge during new reveal"))) return;
        Stage=8;Deadline=Now+3.5;return;
    }
    if(Stage==8)
    {
        if(!Check(!Flow->GetGambleReel().bRolling&&Flow->GetContinueGambleBlockReason().IsEmpty(),TEXT("third draw at same node remains available after two settled draws"))) return;
        FDemoPlayerProgressSnapshot Data=Progress->GetSnapshot(); // 零余额夹具测试已揭晓页面失去购买资格，不修改中奖记录。
        Data.SilverCoins=0;Progress->Commit(Data);PC->PublishMenuSnapshot();
        Stage=9;Deadline=Now+.2;return; // 等待真实HUD重绘禁用按钮。
    }
    if(Stage==9)
    {
        if(!Check(!PC->GetHUD()->GetHitBoxWithName(TEXT("GambleContinue"))&&!Flow->GetContinueGambleBlockReason().IsEmpty(),TEXT("insufficient balance disables continue hitbox with reason"))) return;
        PC->GetHUD<ADemoHUD>()->NotifyHitBoxClick(TEXT("GambleContinue")); // 旧热区注入也必须由Flow拒绝，保留旧结果。
        if(!Check(!Flow->GetGambleReel().Cards.IsEmpty()&&!Flow->GetGambleReel().bRolling&&Progress->GetSnapshot().Gambling.Serial==Serial+1
            &&Progress->GetSnapshot().SilverCoins==0,TEXT("rejected continue retains previous result without charging"))) return;
        FDemoPlayerProgressSnapshot Data=Progress->GetSnapshot(); // 恢复第二次交易后的真实余额，仅用于完成第三次续抽验证。
        Data.SilverCoins=Balance-30;Progress->Commit(Data);PC->PublishMenuSnapshot();
        Stage=10;Deadline=Now+.2;return;
    }
    if(Stage==10)
    {
        if(!Check(PC->GetHUD()->GetHitBoxWithName(TEXT("GambleContinue"))!=nullptr,TEXT("funded result restores continue button"))) return;
        PC->GetHUD<ADemoHUD>()->NotifyHitBoxClick(TEXT("GambleContinue"));
        if(!Check(Flow->GetGambleReel().bRolling&&Progress->GetSnapshot().Gambling.Serial==Serial+2&&Progress->GetSnapshot().SilverCoins==Balance-60,
            TEXT("third same-node draw commits through continue without attempt limit"))) return;
        if(!Check(PC->GetHUD()->GetHitBoxWithName(TEXT("GambleSkipAnimation"))!=nullptr,TEXT("real checkbox hitbox visible on reveal page"))) return;
        PC->GetHUD<ADemoHUD>()->NotifyHitBoxClick(TEXT("GambleSkipAnimation")); // 真实UI白名单，不直接改Reel内部状态。
        const auto& Skipped=Flow->GetGambleReel(); // 同步停靠，不发生第二次随机或经济提交。
        if(!Check(Flow->IsGambleAnimationSkipped()&&!Skipped.bRolling&&Skipped.Cards[Skipped.Target%Skipped.Cards.Num()].Id==Skipped.ResultId
            &&Progress->GetSnapshot().Gambling.Serial==Serial+2,TEXT("checking skip during roll settles same committed reward"))) return;
        Flow->ContinueGamble();
        if(!Check(!Flow->GetGambleReel().bRolling&&!Flow->GetGambleReel().Cards.IsEmpty()&&Progress->GetSnapshot().Gambling.Serial==Serial+3
            &&Progress->GetSnapshot().SilverCoins==Balance-90,TEXT("skip preference next draw commits exactly once and immediately shows result"))) return;
        Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())->LoadSettings(true);
        if(!Check(Flow->IsGambleAnimationSkipped(),TEXT("skip checkbox persists as device setting"))) return;
        FDemoGambleReel InvalidReel; FDemoGambleReveal InvalidReply; // 空/非法服务器回执不能被Finish变成成功结果。
        if(!Check(!InvalidReel.Begin(InvalidReply,TEXT("invalid"),Now)&&!InvalidReel.Finish()&&InvalidReel.Cards.IsEmpty(),TEXT("skip never bypasses committed reward validation"))) return;
        FScreenshotRequest::RequestScreenshot(TEXT("Gambling-Skip-Result.png"),false,false);
        Stage=101;Deadline=Now+.3;return; // 留一帧真实绘制，截图请求完成后再切换到设置页。
    }
    if(Stage==101)
    {
        const int32 SavedSerial=Progress->GetSnapshot().Gambling.Serial; // 关闭之前已经落盘的最新交易版本。
        PC->CloseUpgradeMenu();
        if(!Check(Flow->GetGambleReel().Cards.IsEmpty()&&!Flow->IsGambleWaiting()&&Progress->GetSnapshot().Gambling.Serial==SavedSerial,TEXT("close cancels only presentation and retains committed reward"))) return;
        PC->EscapePressed(); PC->SettingsPressed(); FScreenshotRequest::RequestScreenshot(TEXT("FrameSync-Settings.png"),false,false); // 设置只能从暂停/大厅进入，走真实公开入口。
        Stage=11;Deadline=Now+.3;return;
    }
    if(Stage==11)
    {
        PC->CycleSetting(4,1); PC->ApplyUserSettings(); // 真实UI草稿/应用，隔离GameUserSettingsINI不改个人设置。
        auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // Engine拥有的设置对象，只借用。
        Settings->LoadSettings(true);
        if(!Check(!Settings->IsVSyncEnabled()&&IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSyncEditor"))->GetInt()==0,TEXT("explicit VSync off persists and updates editor presentation"))) return;
        PC->CycleSetting(4,1);PC->ApplyUserSettings();PC->CloseOverlay();PC->CloseOverlay(); // 设置返回暂停，再关闭暂停恢复交互。
        if(!CheckDebugCommands()) return;
        Stage=12;Deadline=Now+.3;return;
    }
    UE_LOG(LogFPSDemo,Display,TEXT("%s"),FParse::Param(FCommandLine::Get(),TEXT("DemoReelUIOnly"))?TEXT("DEMO_REEL_UI_ONLY_SUCCESS"):TEXT("DEMO_VIEW_REEL_TEST_SUCCESS"));SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,0); // 单独成功标记，UI专项不能冒充完整相机回归通过。
#endif
}

bool ADemoViewReelTest::CheckDebugCommands()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本专项唯一真实拥有者。
    auto* Player=Cast<ADemoCharacter>(PC->GetPawn()); // 当前本地Avatar。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 使用真实商店/保存事务。
    auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 只设置终端测试阶段，不产生通关事实。
    auto* Shop=Mode->FindComponentByClass<UDemoShopComponent>(); // 权威经济入口。
    auto* Progress=Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 此玩家独立账本。
    PC->ConsoleCommand(TEXT("Demo.Debug.InfiniteGold invalid"),true); // 验证参数拒绝不会被当成有效布尔。
    if(!Check(!DemoEditorCheats::Enabled(Player,0),TEXT("invalid debug argument leaves default disabled"))) return false;
    PC->ConsoleCommand(TEXT("Demo.Debug.InfiniteGold 1"),true); PC->ConsoleCommand(TEXT("Demo.Debug.InfiniteSilver 1"),true);
    FDemoPlayerProgressSnapshot Data=Progress->GetSnapshot(); // 零余额夹具证明确实绕过余额检查，而非仅不减少巨额数值。
    Data.Coins=0;Data.SilverCoins=0;Progress->Commit(Data); State->Phase=EDemoPhase::Hub;State->LevelNumber=0;
    Mode->GetShopTerminal()->Interact(Player);
    if(!Check(DemoEditorCheats::Enabled(Player,0)&&DemoEditorCheats::Enabled(Player,1)&&Shop->PurchaseUpgradeResult(0,Player).IsApplied()
        &&Progress->GetSnapshot().Coins==0&&Shop->GetPlayerUpgradeCost(Player,0)==30,TEXT("unlimited gold buys from zero but still raises only purchased price"))) return false;
    FString AmmoMessage; // 同步生产购买接口结果，失败保留可诊断文本。
    const int32 AmmoCost=Mode->BuildPlayerMenuSnapshot(PC).AmmoOffers[1].Cost; // 实际配置报价必须照常匹配。
    if(!Check(Shop->PurchaseAmmo(Player,1,AmmoCost,AmmoMessage)&&Progress->GetSnapshot().Coins==0,TEXT("unlimited gold also covers ammo unlock without forged balance"))) return false;
    FDemoCasinoIntent Casino; Casino.RulesKey=GetDefault<UDemoCasinoSettings>()->Rules.Fingerprint(); // 正式公开配置，调试仅免扣。
    const auto GoldResult=Shop->Casino(Player,Casino,Progress->GetSnapshot().Gambling.Serial); // 真正权威交易。
    if(!Check(GoldResult.IsApplied()&&Progress->GetSnapshot().Coins==GoldResult.CasinoResult.Paid,TEXT("unlimited gold casino preserves actual payout without debit"))) return false;
    Data=Progress->GetSnapshot(); Data.Coins=0; Progress->Commit(Data); // 还原后续余额不足断言的零余额夹具，不影响已验证的结算结果。
    PC->CloseUpgradeMenu(); State->Phase=EDemoPhase::Intermission;State->LevelNumber=2;Mode->GetShopTerminal()->Interact(Player);
    const auto SilverOffer=DemoGambleService::Quote(*Shop,Player,0); // 次数预算已移除；无限币只免本次费用，仍须正常报价/序号/保存。
    if(!Check(Shop->PurchaseUpgradeResult(0,Player).IsApplied()&&Shop->Gamble(Player,0,SilverOffer.Cost,Progress->GetSnapshot().Gambling.Serial).IsApplied()
        &&Progress->GetSnapshot().SilverCoins==0&&Progress->GetSnapshot().Gambling.Attempts==0,TEXT("unlimited silver covers upgrades and draws without accumulating retired attempt counters"))) return false;
    auto* ASC=Player->GetDemoASC(); // 真实GAS伤害链，不能以控制台布尔查询替代效果测试。
    ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),75); // 仅测试夹具基础生命，非伤害途径。
    PC->ConsoleCommand(TEXT("Demo.Debug.God 1"),true);DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),-10000);
    if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),75),TEXT("god blocks real lethal GE before death and rescue"))) return false;
    DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),5);
    if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),80),TEXT("god does not suppress healing"))) return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 高空无奖励注册靶。
    auto* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),FVector(80000,80000,10000),FRotator::ZeroRotator,Spawn); // World拥有，断言后同步清理。
    FDemoEnemySpawnStats Stats;Stats.Health=100;Stats.AttackPower=0;Enemy->Configure(Stats); // 足够生命不触发测试外死亡结算。
    DemoEffects::Apply(ASC,Enemy->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10);
    const bool bEnemyDamaged=FMath::IsNearlyEqual(Enemy->GetHealth(),90); // 先记录真值再销毁。
    Enemy->Destroy(); if(!Check(bEnemyDamaged,TEXT("god does not protect enemies"))) return false;
    PC->ConsoleCommand(TEXT("Demo.Debug.ResetCheats"),true); DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),-10);
    if(!Check(!DemoEditorCheats::Enabled(Player,0)&&!DemoEditorCheats::Enabled(Player,1)&&!DemoEditorCheats::Enabled(Player,2)
        &&Player->GetDemoAttributes()->GetHealth()==70&&!Shop->GetPurchaseBlockReason(Player,1).IsEmpty(),TEXT("reset disables all overrides and restores real damage and balance rejection"))) return false;
    return true;
#else
    return false; // 包体无命令/测试接口，启动器也不会创建本专项。
#endif
}
