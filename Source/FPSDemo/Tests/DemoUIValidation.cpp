#include "Tests/DemoUIValidation.h"
#include "Debug/DemoLog.h"
#include "UI/DemoHUD.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "AI/DemoEnemy.h"
#include "Game/FPSDemoGameMode.h"
#include "Interaction/DemoInteractable.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoEffects.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "GameFramework/HUDHitBox.h"
#include "Misc/CommandLine.h"

namespace
{
	// 专用进程跨 OpenLevel：0 原地清关/通关 UI，1 死亡结算 UI，2 安全区重开截图后退出。
	int32 UIValidationRun = 0;
}

ADemoUIValidation::ADemoUIValidation()
{
	DEMO_LOG_CALL();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
}
bool ADemoUIValidation::Check(bool Condition, const TCHAR* Message)
{
	DEMO_LOG_CALL();
	UE_LOG(LogFPSDemo, Display, TEXT("UI CHECK %s: %s"), Condition ? TEXT("PASS") : TEXT("FAIL"), Message);
	if (!Condition) { bFailed = true; FPlatformMisc::RequestExitWithStatus(false, 1); }
	return Condition;
}
void ADemoUIValidation::Capture(const TCHAR* Name)
{
	DEMO_LOG_CALL();
	FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/DemoUI") / FString(Name) + TEXT(".png"), false, false);
}
void ADemoUIValidation::Advance(int32 Next)
{
	DEMO_LOG_CALL();
	Step = Next;
	NextTime = GetWorld()->GetTimeSeconds() + 0.3f;
}
void ADemoUIValidation::ClearEnemies()
{
	DEMO_LOG_CALL();
	// Player 与 It 均只借用当前 World；死亡回调会在下一帧推进 Reward 或 Victory。
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive()) DemoEffects::Apply(Player->GetDemoASC(), It->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -100000.f);
	}
}
bool ADemoUIValidation::ClickMenuPoint(FVector2D Offset, FName Expected, bool bLobbyAnchor)
{
	DEMO_LOG_CALL();
	// 此方法只解析测试视口热区，不移动系统鼠标或向其他应用发送输入。
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0)); // 当前控制器借用。
	ADemoHUD* HUD = PC->GetHUD<ADemoHUD>(); // 本步渲染形成的热区属于当前 HUD。
	int32 Width = 0; // 视口宽度，物理像素。
	int32 Height = 0; // 视口高度，物理像素。
	PC->GetViewportSize(Width, Height);
	const float Scale = FMath::Min(Width / 1280.f, Height / 720.f); // 与生产布局同一等比约定。
	// 大厅已改为左侧偏上锚点，商店/奖励仍居中；测试实际坐标，避免只检查热区名称存在。
	const FVector2D Anchor = bLobbyAnchor ? FVector2D(0.f, Height * 0.43f) : FVector2D(Width, Height) / 2.f;
	const FHUDHitBox* Hit = HUD->GetHitBoxAtCoordinates(Anchor + Offset * Scale, true); // 只在派发前借用，不跨重绘缓存。
	if (!Check(Hit && Hit->GetName() == Expected, TEXT("scaled screen point resolves to intended button"))) return false;
	HUD->NotifyHitBoxClick(Hit->GetName());
	return true;
}
void ADemoUIValidation::Tick(float DeltaSeconds)
{
	DEMO_LOG_TICK();
	Super::Tick(DeltaSeconds);
	if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
	if (!Check(GetWorld()->GetTimeSeconds() < 90.f, TEXT("UI test stays within world timeout"))) return;
	// 每步重新解析借用引用，重开后严禁访问上一 World 的 HUD、角色或属性。
	AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	ADemoCharacter* Player = PC ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr;
	ADemoHUD* HUD = PC ? PC->GetHUD<ADemoHUD>() : nullptr;
	if (!Check(Mode && State && Player && HUD && Player->GetDemoAttributes(), TEXT("UI runtime dependencies ready"))) return;
	// 图标专项复用现有UI测试的临时存档隔离；独立状态机不会同时推进正常菜单回归。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoEnemyStatusTest"))) { TickEnemyStatus(PC, Mode, Player, HUD); return; }
	// 本步读取当前装备；回归使用默认手枪，初始伤害20，终端伤害升级+5；弹药由武器实例持有。
	ADemoWeaponBase* Weapon = Player->GetWeaponComponent()->GetActiveWeapon();
	if (!Check(Weapon != nullptr, TEXT("weapon loadout initialized"))) return;
	const UDemoAttributeSet* Attributes = Player->GetDemoAttributes(); // 仅本步有效的只读属性。
	switch (Step)
	{
	case 0:
		// 死亡场景发生在新局安全区出发弹窗中，尚未清关，因此保留的金币也是0；有金币死亡由Session/Smoke覆盖。
		if (!Check(State->Phase == (UIValidationRun == 2 ? EDemoPhase::Hub : EDemoPhase::Lobby) && State->Coins == 0 && State->SilverCoins == 0 && Weapon->GetDamagePerPellet() == 20.f, TEXT("new world resets attributes and chooses correct lobby/hub destination"))) return;
		if (UIValidationRun == 2)
		{
			if (!Check(!PC->IsMoveInputIgnored() && !PC->bShowMouseCursor && Mode->GetShopTerminal(), TEXT("death return hub restores input and upgrade terminal"))) return;
			Capture(TEXT("11-DeathReturnHub"));
			Advance(24); // 留出渲染帧再退出，不能在截图请求同帧关闭进程。
			break;
		}
		// 难度已移到安全区终端，大厅只负责打开存档选择。
		if (!Check(HUD->GetHitBoxWithName(TEXT("StartGame")) && !HUD->GetHitBoxWithName(TEXT("Normal")), TEXT("lobby registers start without difficulty hitboxes"))) return;
		Capture(TEXT("00-Lobby"));
		Advance(40); // 同步验证半透明设置页与返回按钮的真实菜单切换，40..42 避开已有玩法步骤。
		break;
	case 40:
		HUD->NotifyHitBoxClick(TEXT("LobbySettings"));
		Advance(41);
		break;
	case 41:
		if (!Check(PC->IsSettingsOpen() && !HUD->GetHitBoxWithName(TEXT("StartGame")) && HUD->GetHitBoxWithName(TEXT("OverlayBack")), TEXT("settings replaces lobby controls and offers return"))) return;
		Capture(TEXT("00-LobbySettings"));
		Advance(42);
		break;
	case 42:
		HUD->NotifyHitBoxClick(TEXT("OverlayBack")); // 设置子页使用独立返回热区，不能复用主大厅按钮。
		Advance(1);
		break;
	case 1:
		// 验证实际开始热区的新路由；跨地图存档流程由DemoSessionTest覆盖，本测试继续复用当前World拍摄战斗UI。
		if (!ClickMenuPoint(FVector2D(246, 71), TEXT("StartGame"), true)) return;
		if (!Check(State->Phase == EDemoPhase::Lobby && PC->GetMenuPage() == EDemoMenuPage::Saves, TEXT("start routes to save selection"))) return;
		PC->CloseOverlay();
		Mode->StartRun();
		PC->OnRunReady();
		if (!Check(State->Phase == EDemoPhase::Hub && !PC->bShowMouseCursor && !PC->IsMoveInputIgnored(), TEXT("start restores game input"))) return;
		// 第二次运行验证弹窗中死亡：结算必须丢弃待确认请求，不能被旧按钮带入战斗。
		if (UIValidationRun == 1)
		{
			Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation() + FVector(-180,0,20));
			Mode->GetNextLevelTerminal()->Interact(Player);
			Advance(20);
		}
		else Advance(2);
		break;
	case 2:
		Capture(TEXT("01-Hub"));
		Advance(3);
		break;
	case 3:
		Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation() + FVector(-180, 0, 20));
		Mode->GetShopTerminal()->Interact(Player);
		Advance(4);
		break;
	case 4:
		if (!Check(!HUD->GetHitBoxWithName(TEXT("Upgrade0")) && HUD->GetHitBoxWithName(TEXT("Close")) && PC->bShowMouseCursor && PC->IsMoveInputIgnored(), TEXT("poor shop disables buy hitbox and locks movement"))) return;
		PC->SelectUpgrade(0);
		if (!Check(State->Coins == 0 && !PC->GetMenuMessage().IsEmpty(), TEXT("keyboard cannot bypass insufficient coins"))) return;
		Capture(TEXT("02-ShopDisabled"));
		Advance(5);
		break;
	case 5:
		HUD->NotifyHitBoxClick(TEXT("Close"));
		// 安全区入口需要明确选难度；清关后的入口仍使用是/否确认。
		Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation() + FVector(-180,0,20));
		Mode->GetNextLevelTerminal()->Interact(Player);
		Advance(46);
		break;
	case 46:
		if (!Check(State->Phase == EDemoPhase::Hub && PC->IsNextLevelConfirmationOpen() && HUD->GetHitBoxWithName(TEXT("Normal")), TEXT("hub terminal waits for explicit difficulty selection"))) return;
		Capture(TEXT("01b-HubConfirmation"));
		Advance(47); // 截图请求在独立帧完成，随后再确认。
		break;
	case 47:
		HUD->NotifyHitBoxClick(TEXT("Normal")); // 真实难度按钮同时确认首次出发。
		if (!Check(State->Phase == EDemoPhase::Combat && State->LevelNumber == 1 && !PC->IsNextLevelConfirmationOpen(), TEXT("difficulty confirms hub departure"))) return;
		Advance(6);
		break;
	case 6:
		Capture(TEXT("03-Combat"));
		Advance(7);
		break;
	case 7:
		ClearEnemies();
		Advance(8);
		break;
	case 8:
		if (!Check(State->Phase == EDemoPhase::Reward && HUD->GetHitBoxWithName(TEXT("Upgrade0")) && HUD->GetHitBoxWithName(TEXT("Upgrade1")) && HUD->GetHitBoxWithName(TEXT("Upgrade2")) && !HUD->GetHitBoxWithName(TEXT("Close")), TEXT("reward has three cards and no close hitbox"))) return;
		PC->CloseUpgradeMenu();
		if (!Check(PC->IsRewardMenu(), TEXT("reward cannot be dismissed by close shortcut"))) return;
		Capture(TEXT("04-Reward"));
		Advance(9);
		break;
	case 9:
		if (!ClickMenuPoint(FVector2D(-3, 19), TEXT("Upgrade1"))) return;
		if (!Check(State->Phase == EDemoPhase::Intermission && Player->GetHealAmount() == 55.f && State->Coins == 0, TEXT("free card stays in cleared arena and preserves coins"))) return;
		// 测试取景移动不属于生产清关流程；先等待相机稳定，避免传送当帧的运动模糊污染截图。
		Player->SetActorLocation(Mode->GetAreaCenter(0) + FVector(-650,0,100));
		PC->SetControlRotation(FRotator::ZeroRotator);
		Advance(26);
		break;
	case 26:
		// 相机已经渲染多个稳定帧，此时记录本关中心的一对终端及原地备战 HUD。
		Capture(TEXT("04b-ArenaTerminals"));
		Advance(25);
		break;
	case 25:
		Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation() + FVector(-180, 0, 20));
		Mode->GetShopTerminal()->Interact(Player);
		Advance(10);
		break;
	case 10:
		if (!Check(HUD->GetHitBoxWithName(TEXT("Upgrade0")) && Mode->GetUpgradeCost() == 20, TEXT("funded shop registers active purchase hitbox"))) return;
		Capture(TEXT("05-Shop"));
		Advance(11);
		break;
	case 11:
		Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation() + FVector(-1000, 0, 100)); // 在本关中离开购买范围，不返回安全区。
		Advance(12);
		break;
	case 12:
		if (!Check(!HUD->GetHitBoxWithName(TEXT("Upgrade0")), TEXT("leaving range removes buy hitbox even with enough coins"))) return;
		PC->SelectUpgrade(0);
		if (!Check(State->Coins == 0, TEXT("out-of-range keyboard transaction rejected"))) return;
		Capture(TEXT("06-ShopOutOfRange"));
		Advance(13);
		break;
	case 13:
		Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation() + FVector(-180, 0, 20));
		Advance(23); // 返回范围后先等待下一帧重建购买热区。
		break;
	case 23:
		if (!ClickMenuPoint(FVector2D(117, -73), TEXT("Upgrade0"))) return;
		if (!Check(State->Coins == 0 && State->SilverCoins == 30 && Mode->GetUpgradeCost(0) == 30 && Mode->GetUpgradeCost(1) == 20 && Mode->GetUpgradeCost(2) == 20 && Weapon->GetDamagePerPellet() == 25.f, TEXT("purchase updates coins, GAS damage and damage-only next price"))) return;
		Advance(14);
		break;
	case 14:
		Capture(TEXT("07-ShopAfterPurchase"));
		State->SilverCoins = 20; // UI夹具：伤害30买不起，其他属性20应仍能点击；不保存此临时测试余额。
		Advance(49);
		break;
	case 49:
		if (!Check(!HUD->GetHitBoxWithName(TEXT("Upgrade0")) && HUD->GetHitBoxWithName(TEXT("Upgrade1")) && HUD->GetHitBoxWithName(TEXT("Upgrade2")),
			TEXT("expensive attribute disabled without disabling affordable sibling attributes"))) return;
		Capture(TEXT("07b-IndependentPrices"));
		Advance(50); // 截图在渲染帧处理，先保留20银币到下一步，避免截图显示已恢复的30。
		break;
	case 50:
		State->SilverCoins = 30; // 恢复真实交易后的余额，后续购买仍走生产授权/扣费流程。
		Advance(15);
		break;
	case 15:
		HUD->NotifyHitBoxClick(TEXT("Upgrade2"));
		if (!Check(State->Coins == 0 && State->SilverCoins == 10 && Weapon->GetCapacity() == 16.f, TEXT("second category keeps its own initial price"))) return;
		Advance(16);
		break;
	case 16:
		if (!Check(!HUD->GetHitBoxWithName(TEXT("Upgrade0")), TEXT("spending last coins refreshes disabled hitboxes"))) return;
		HUD->NotifyHitBoxClick(TEXT("Close"));
		// 经真实终端打开下一关确认，覆盖“否/Tab/是”和按钮屏幕坐标，而非绕过 UI 推进。
		Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation() + FVector(-180,0,20));
		Mode->GetNextLevelTerminal()->Interact(Player);
		Advance(43);
		break;
	case 43:
		if (!Check(State->Phase == EDemoPhase::Intermission && State->LevelNumber == 1 && PC->IsNextLevelConfirmationOpen()
			&& PC->bShowMouseCursor && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored()
			&& HUD->GetHitBoxWithName(TEXT("NextLevelYes")) && HUD->GetHitBoxWithName(TEXT("NextLevelNo"))
			&& !HUD->GetHitBoxWithName(TEXT("Upgrade0")), TEXT("confirmation renders exclusive Yes/No controls and locks movement"))) return;
		Capture(TEXT("07b-NextLevelConfirmation"));
		Advance(48); // 截图必须在取消按钮执行前完成。
		break;
	case 48:
		if (!ClickMenuPoint(FVector2D(-129, 88), TEXT("NextLevelNo"))) return;
		Advance(44);
		break;
	case 44:
		if (!Check(!PC->IsNextLevelConfirmationOpen() && !PC->bShowMouseCursor && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored()
			&& State->Phase == EDemoPhase::Intermission && State->LevelNumber == 1 && State->Coins == 0 && State->SilverCoins == 10 && Weapon->GetDamagePerPellet() == 25.f
			&& !HUD->GetHitBoxWithName(TEXT("NextLevelYes")) && !HUD->GetHitBoxWithName(TEXT("NextLevelNo")), TEXT("No closes UI, removes hitboxes and preserves progression and upgrades"))) return;
		HUD->NotifyHitBoxClick(TEXT("NextLevelYes")); // 模拟已经关闭的旧按钮请求，必须忽略。
		Mode->GetNextLevelTerminal()->Interact(Player);
		PC->CloseUpgradeMenu(); // 实际 Tab 绑定入口应取消出发确认。
		if (!Check(!PC->IsNextLevelConfirmationOpen() && State->LevelNumber == 1 && !PC->IsMoveInputIgnored(), TEXT("Tab cancels confirmation without advancing"))) return;
		Mode->GetNextLevelTerminal()->Interact(Player);
		Advance(45);
		break;
	case 45:
		if (!ClickMenuPoint(FVector2D(129, 88), TEXT("NextLevelYes"))) return;
		HUD->NotifyHitBoxClick(TEXT("NextLevelYes")); // 连点不能重复消费同一个待确认请求。
		if (!Check(State->Phase == EDemoPhase::Combat && State->LevelNumber == 2 && !PC->IsNextLevelConfirmationOpen()
			&& !Mode->GetNextLevelTerminal() && !PC->IsMoveInputIgnored(), TEXT("Yes enters exactly one next stage and clears old terminal"))) return;
		Advance(17);
		break;
	case 17:
		// 保持配置原始生成和死亡流程，按实际关卡数推进；清场不是玩家操作验收。
		if (State->LevelNumber == DemoCombatConfig::LevelCount) { Capture(TEXT("08-Boss")); Advance(18); }
		else { ClearEnemies(); Advance(19); }
		break;
	case 18:
		ClearEnemies();
		Advance(19);
		break;
	case 19:
		if (State->Phase == EDemoPhase::Reward) { PC->SelectUpgrade(0); Mode->StartNextLevel(); Advance(17); }
		else if (State->Phase == EDemoPhase::Victory)
		{
			if (!Check(HUD->GetHitBoxWithName(TEXT("Restart")) != nullptr, TEXT("victory registers restart hitbox"))) return;
			Capture(TEXT("09-Victory"));
			Advance(22);
		}
		else Check(false, TEXT("clear resolves to reward or victory"));
		break;
	case 20:
		if (!Check(PC->IsNextLevelConfirmationOpen(), TEXT("death scenario starts with pending confirmation"))) return;
		DemoEffects::Apply(Player->GetDemoASC(), Player->GetDemoASC(), UDemoHealthEffect::StaticClass(), -10000.f);
		Advance(21);
		break;
	case 21:
		HUD->NotifyHitBoxClick(TEXT("NextLevelYes")); // 死亡后的旧热区请求不能重新推进。
		if (!Check(State->Phase == EDemoPhase::Defeat && !PC->IsNextLevelConfirmationOpen() && HUD->GetHitBoxWithName(TEXT("Restart"))
			&& !HUD->GetHitBoxWithName(TEXT("NextLevelYes")), TEXT("death clears confirmation and preserves end-screen input"))) return;
		Capture(TEXT("10-Defeat"));
		Advance(22);
		break;
	case 22:
		++UIValidationRun;
		HUD->NotifyHitBoxClick(TEXT("Restart"));
		if (UIValidationRun==1)
		{
			if (!Check(State->Phase==EDemoPhase::Hub && State->LevelNumber==0 && Mode->GetShopTerminal() && !PC->IsMoveInputIgnored(),TEXT("victory UI returns functional safe hub"))) return;
			Mode->TravelToRun(false); // 后续独立死亡UI夹具显式重载，不把Victory回Hub误当地图旅行。
		}
		SetActorTickEnabled(false);
		break;
	case 24:
		UE_LOG(LogFPSDemo, Display, TEXT("DEMO_UI_SUCCESS: arena rewards/shop, hitboxes, prices, victory and death-return safe hub verified"));
		SetActorTickEnabled(false);
		FPlatformMisc::RequestExitWithStatus(false, 0);
		break;
	default:
		Check(false, TEXT("invalid UI validation step"));
		break;
	}
}
