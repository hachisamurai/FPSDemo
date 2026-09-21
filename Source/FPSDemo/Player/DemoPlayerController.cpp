#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoCloudSync.h"
#include "UI/DemoHUD.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Engine/GameInstance.h"
#include "Game/FPSDemoGameMode.h"
#include "Interaction/DemoInteractable.h"
#include "Debug/DemoLog.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Components/InputComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetSystemLibrary.h"

ADemoPlayerController::ADemoPlayerController()
{
	DEMO_LOG_CALL();
	PrimaryActorTick.bTickEvenWhenPaused = true; // 设置回退、退出保存轮询和成功提示在暂停时仍更新。
	bShouldPerformFullTickWhenPaused = true;
	// 原映射保留WASD/鼠标轴，武器Action由独立上下文管理，以免菜单数字键冲突。
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> Mapping(TEXT("/Game/FirstPerson/Input/IMC_Default"));
	MappingContext = Mapping.Object;
	CombatMappingAsset = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Weapons/Input/IMC_Combat.IMC_Combat")));
}
void ADemoPlayerController::BeginPlay()
{
	DEMO_LOG_CALL();
	Super::BeginPlay();
	CombatMappingContext = CombatMappingAsset.LoadSynchronous();
	// LocalPlayer 只在本地控制器可用；专服不会尝试建立输入。
	if (ULocalPlayer* Local = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (MappingContext) Subsystem->AddMappingContext(MappingContext, 0);
		}
	}
	// 即使引擎 BeginPlay 顺序变化，也不会出现大厅第一帧可移动的窗口。
	ShowLobby();
}
void ADemoPlayerController::SetupInputComponent()
{
	DEMO_LOG_CALL();
	Super::SetupInputComponent();
	// Esc必须在暂停时继续处理，否则无法通过同一按键恢复World。
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ADemoPlayerController::EscapePressed).bExecuteWhenPaused = true;
	// 非菜单时不吞掉1/2，允许Pawn的Enhanced Input处理武器槽；菜单时Combat映射已移除。
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ADemoPlayerController::SelectFirst).bConsumeInput = false;
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ADemoPlayerController::SelectSecond).bConsumeInput = false;
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ADemoPlayerController::SelectThird);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ADemoPlayerController::CloseUpgradeMenu);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ADemoPlayerController::RestartPressed);
}
void ADemoPlayerController::SetMenuInput(bool bEnabled)
{
	DEMO_LOG_CALL();
	bShowMouseCursor = bEnabled;
	// 只管理本功能上下文；重新启用时忽略仍按住的键，关闭菜单不会自动开枪/切枪。
	if (ULocalPlayer* Local = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (CombatMappingContext)
			{
				if (bEnabled) Subsystem->RemoveMappingContext(CombatMappingContext);
				else
				{
					FModifyContextOptions Options; // 当前重建选项；不追溯菜单中按下的武器键。
					Options.bIgnoreAllPressedKeysUntilRelease = true;
					Subsystem->AddMappingContext(CombatMappingContext, 1, Options);
				}
			}
		}
	}
	if (bEnabled)
		if (ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn())) DemoPawn->GetWeaponComponent()->CancelActions();
	bEnableClickEvents = bEnabled;
	ResetIgnoreMoveInput();
	ResetIgnoreLookInput();
	SetIgnoreMoveInput(bEnabled);
	SetIgnoreLookInput(bEnabled);
	// HUD HitBox 使用游戏视口输入，因此 GameAndUI 不绑定额外 Slate 焦点。
	if (bEnabled) SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
	else SetInputMode(FInputModeGameOnly());
}
void ADemoPlayerController::OpenUpgradeMenu(bool bReward)
{
	DEMO_LOG_CALL();
	// 确认框与升级菜单互斥，防止旧热区或重复交互覆盖正在处理的出发意图。
	if (HasBlockingOverlay() || bNextLevelConfirmationOpen) { UE_LOG(LogFPSDemo, Log, TEXT("Upgrade menu rejected: next-level confirmation open")); return; }
	bMenuOpen = true;
	bRewardMenu = bReward;
	TerminalPage = EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; // 每次交互默认进入属性页，清除上一终端的详情意图。
	InspectedWeapon = INDEX_NONE;
	MenuMessage.Empty();
	// 仅借用当前 Pawn；命名避免遮蔽 AController::Character 成员。
	if (ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn())) DemoPawn->StopFiring();
	SetMenuInput(true);
}
void ADemoPlayerController::CloseUpgradeMenu()
{
	DEMO_LOG_CALL();
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Close terminal ignored beneath overlay")); return; } // 顶层模态不允许Tab误关奖励/终端。
	// 复用已有 Tab 绑定；取消确认只关闭本地窗口，不调用关卡推进。
	if (bNextLevelConfirmationOpen) { CancelNextLevelConfirmation(); return; }
	if(IsAmmoMenuOpen() && PendingAmmoPrice!=INDEX_NONE){ConfirmAmmoPurchase(false);return;} // Tab先取消购买确认。
	if (IsWeaponMenuOpen() && InspectedWeapon != INDEX_NONE) { CloseWeaponTip(); return; } // Tab先关详情，再按关闭终端。
	// 奖励必须选一次；按钮与快捷键拒绝时保留原阶段并记录原因。
	if (!bMenuOpen || bRewardMenu) { UE_LOG(LogFPSDemo, Log, TEXT("Close menu rejected: open=%d reward=%d"), bMenuOpen, bRewardMenu); return; }
	bMenuOpen = false;
	TerminalPage = EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE;
	InspectedWeapon = INDEX_NONE;
	UE_LOG(LogFPSDemo, Log, TEXT("UI menu closed; restoring game input"));
	SetMenuInput(false);
}
bool ADemoPlayerController::CanRequestNextLevel(const ADemoInteractable* Terminal) const
{
	DEMO_LOG_CALL();
	const AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 同步借用单人权威流程。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 本次检查的关卡快照。
	const ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn()); // 当前拥有的 Avatar，不缓存跨重开引用。
	if (!IsLocalController() || !Mode || !State || !DemoPawn || !IsValid(Terminal) || bMenuOpen || HasBlockingOverlay()
		|| (State->Phase != EDemoPhase::Hub && State->Phase != EDemoPhase::Intermission)
		|| State->LevelNumber < 0 || (!State->bEndless && State->LevelNumber >= DemoCombatConfig::LevelCount) || State->LevelNumber >= MAX_int32-1 // 无尽关间终端不受十关限制，防止编号整数溢出。
		|| Mode->GetNextLevelTerminal() != Terminal
		|| FVector::Dist(DemoPawn->GetActorLocation(), Terminal->GetActorLocation()) > 250.f)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL rejected: invalid owner/phase/menu/terminal/range"));
		return false;
	}
	return true;
}
void ADemoPlayerController::OpenNextLevelConfirmation(ADemoInteractable* Terminal)
{
	DEMO_LOG_CALL();
	if (bNextLevelConfirmationOpen) { UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL open rejected: confirmation already open")); return; }
	if (!CanRequestNextLevel(Terminal)) return;
	PendingNextLevelTerminal = Terminal;
	PendingCompletedLevel = GetWorld()->GetGameState<ADemoGameState>()->LevelNumber;
	bNextLevelConfirmationOpen = true;
	bDifficultyChosen = false; // 安全区每次打开必须明确选择，避免Enter默认跳过。
	ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn()); // 校验后的当前角色，仅本次调用借用。
	DemoPawn->StopFiring();
	DemoPawn->GetCharacterMovement()->StopMovementImmediately(); // 停止残余速度，避免弹窗期间滑出交互范围。
	SetMenuInput(true);
	UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirmation opened target=%d"), PendingCompletedLevel + 1);
}
void ADemoPlayerController::CancelNextLevelConfirmation()
{
	DEMO_LOG_CALL();
	if (!bNextLevelConfirmationOpen) { UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL cancel ignored: no request")); return; }
	bNextLevelConfirmationOpen = false;
	PendingNextLevelTerminal.Reset();
	PendingCompletedLevel = INDEX_NONE;
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 取消时重新读阶段，不能解除终局/大厅输入锁。
	SetMenuInput(bMenuOpen || !State || State->Phase == EDemoPhase::Lobby || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat);
	UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirmation closed; progress unchanged"));
}
void ADemoPlayerController::ConfirmNextLevel()
{
	DEMO_LOG_CALL();
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Departure ignored beneath overlay")); return; } // 暂停页不接受下层Enter/旧按钮。
	if (!bNextLevelConfirmationOpen) { UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirm ignored: no request")); return; }
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 本次确认的真实进度，不能沿用打开时的显示值。
	if (!CanRequestNextLevel(PendingNextLevelTerminal.Get()) || !State || State->LevelNumber != PendingCompletedLevel)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirm rejected: request expired"));
		CancelNextLevelConfirmation();
		return;
	}
	if (State->Phase == EDemoPhase::Hub && !bDifficultyChosen) { UE_LOG(LogFPSDemo, Log, TEXT("Choose difficulty before first departure")); return; }
	// 先消费请求再调用同步状态机：防止双击重复推进，也避免覆盖 StartNextLevel 失败后的终局输入。
	UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirmed target=%d"), PendingCompletedLevel + 1);
	CancelNextLevelConfirmation();
	GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->StartNextLevel();
}
bool ADemoPlayerController::IsNextLevelConfirmationOpen() const { DEMO_LOG_TICK(); return bNextLevelConfirmationOpen; }
void ADemoPlayerController::SelectUpgrade(int32 Choice)
{
	DEMO_LOG_CALL();
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Upgrade ignored beneath overlay")); return; } // 防止暂停时数字键购买下层属性。
	if (!bMenuOpen || TerminalPage!=EDemoTerminalPage::Stats) { UE_LOG(LogFPSDemo, Log, TEXT("Upgrade input ignored: closed/weapon page")); return; } // 武器页数字键不能误购属性。
	if (Choice < 0 || Choice > 2) { MenuMessage = TEXT("无效的升级选项"); UE_LOG(LogFPSDemo, Warning, TEXT("Upgrade rejected: choice=%d"), Choice); return; }
	// 单人本地服务器；网络客户端没有 AuthGameMode，明确拒绝而非发送未经实现的 RPC。
	AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
	if (!Mode) { MenuMessage = TEXT("当前仅支持单人模式"); UE_LOG(LogFPSDemo, Warning, TEXT("Upgrade rejected: no authority GameMode")); return; }
	if (bRewardMenu)
	{
		if (Mode->ChooseReward(Choice)) { bRewardMenu = false; CloseUpgradeMenu(); }
		else { MenuMessage = TEXT("奖励已领取或当前阶段不可选择"); UE_LOG(LogFPSDemo, Log, TEXT("Reward selection rejected")); }
	}
	else
	{
		// 读取原因用于准确反馈；PurchaseUpgrade 内仍重新检查，UI 不承担交易授权。
		ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn()); // 仅本次输入借用当前 Avatar。
		const FString Reason = Mode->GetPurchaseBlockReason(DemoPawn, Choice); // 当前所选项的阻塞原因，不能沿用伤害项价格。
		const int32 Cost = Mode->GetUpgradeCost(Choice); // 成功文案使用该项扣费前价格。
		if (!Reason.IsEmpty()) { MenuMessage = Reason; UE_LOG(LogFPSDemo, Log, TEXT("UI purchase rejected: %s"), *Reason); return; }
		MenuMessage = Mode->PurchaseUpgrade(Choice, DemoPawn)
			? FString::Printf(TEXT("升级成功，花费 %d %s · 属性已生效"), Cost, GetWorld()->GetGameState<ADemoGameState>()->Phase == EDemoPhase::Hub ? TEXT("金币") : TEXT("银币")) : TEXT("升级未生效，请重试并检查日志"); // 与权威购买使用同一区域判定，反馈不可错标另一钱包。
		UE_LOG(LogFPSDemo, Log, TEXT("UI purchase result: %s"), *MenuMessage);
	}
}
void ADemoPlayerController::SelectFirst() { DEMO_LOG_CALL(); SelectUpgrade(0); }
void ADemoPlayerController::SelectSecond() { DEMO_LOG_CALL(); SelectUpgrade(1); }
void ADemoPlayerController::SelectThird() { DEMO_LOG_CALL(); SelectUpgrade(2); }
void ADemoPlayerController::ShowEndScreen()
{
	DEMO_LOG_CALL();
	// 失败可在弹窗期间发生；丢弃出发意图，不能让旧按钮在结算后恢复战斗。
	TerminalPage = EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; // 丢弃当前武器详情，终局不能沿用旧终端装备请求。
	InspectedWeapon = INDEX_NONE;
	bNextLevelConfirmationOpen = false;
	PendingNextLevelTerminal.Reset();
	PendingCompletedLevel = INDEX_NONE;
	bMenuOpen = false;
	bRewardMenu = false;
	// 终局保留 Pawn 供界面读取属性，但清除持续射击。
	if (ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn())) DemoPawn->StopFiring();
	SetMenuInput(true);
}
bool ADemoPlayerController::IsUpgradeMenuOpen() const { DEMO_LOG_TICK(); return bMenuOpen; }
bool ADemoPlayerController::IsRewardMenu() const { DEMO_LOG_TICK(); return bRewardMenu; }
bool ADemoPlayerController::IsWeaponMenuOpen() const { DEMO_LOG_TICK(); return bMenuOpen && !bRewardMenu && TerminalPage==EDemoTerminalPage::Weapons; }
int32 ADemoPlayerController::GetInspectedWeapon() const { DEMO_LOG_TICK(); return IsWeaponMenuOpen() ? InspectedWeapon : INDEX_NONE; }
void ADemoPlayerController::SetTerminalWeaponPage(bool bWeapons)
{
	DEMO_LOG_CALL();
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Terminal tab rejected: blocking overlay")); return; } // 暂停/设置覆盖终端时不能派发下层页签。
	const AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前终端的权威状态，每次点击重新读取。
	const FString Reason = Mode ? Mode->GetTerminalBlockReason(Cast<ADemoCharacter>(GetPawn())) : TEXT("当前仅支持单人模式"); // 拒绝原因供HUD展示。
	if (!Reason.IsEmpty() || InspectedWeapon != INDEX_NONE) { MenuMessage = Reason; UE_LOG(LogFPSDemo, Log, TEXT("Terminal tab rejected: %s/tip"), *Reason); return; }
	TerminalPage = bWeapons?EDemoTerminalPage::Weapons:EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; // 切页消费旧购买意图。
	MenuMessage.Empty();
	// 只在打开/重新进入武器页时读取软路径，异步结果由HUD保活，不在每帧UI绘制时加载。
	if (bWeapons)
	{
		const ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn()); // 本次同步借用当前玩家目录。
		if (ADemoHUD* HUD = GetHUD<ADemoHUD>()) HUD->PrepareWeaponIcons(DemoPawn ? DemoPawn->GetWeaponComponent() : nullptr); // HUD弱委托负责后续加载生命周期。
	}
	UE_LOG(LogFPSDemo, Log, TEXT("TERMINAL_PAGE weapons=%d"), bWeapons);
}
void ADemoPlayerController::InspectWeapon(int32 CatalogIndex)
{
	DEMO_LOG_CALL();
	if (HasBlockingOverlay() || !IsWeaponMenuOpen() || InspectedWeapon != INDEX_NONE || DemoWeaponCatalog::IdAt(CatalogIndex).IsNone()) // 顶层模态阻止旧帧卡片意图穿透。
	{ UE_LOG(LogFPSDemo, Log, TEXT("Weapon tip rejected: page/modal/index")); return; }
	InspectedWeapon = CatalogIndex;
	MenuMessage.Empty();
}
void ADemoPlayerController::CloseWeaponTip()
{
	DEMO_LOG_CALL();
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Weapon tip close rejected: blocking overlay")); return; } // 保留下层Tips，关闭顶层窗口后恢复。
	InspectedWeapon = INDEX_NONE;
	MenuMessage.Empty();
}
void ADemoPlayerController::EquipInspectedWeapon()
{
	DEMO_LOG_CALL();
	ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn()); // 当前操作者，不缓存旧Pawn或Tips打开时的状态。
	const AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 权威终端验证。
	UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // GI持有的持久进度。
	if (HasBlockingOverlay() || !IsWeaponMenuOpen() || InspectedWeapon < 1 || InspectedWeapon > 3 || !DemoPawn || !Mode || !Profile) // 暂停/设置不得透传装备操作。
	{ UE_LOG(LogFPSDemo, Log, TEXT("Weapon equip rejected: no primary tip")); return; }
	const FString Reason = Mode->GetTerminalBlockReason(DemoPawn); // 展示当前失败原因；组件仍重复权限检查。
	if (!Reason.IsEmpty()) { MenuMessage = Reason; UE_LOG(LogFPSDemo, Log, TEXT("Weapon equip rejected: %s"), *Reason); return; }
	if (!Profile->IsUnlocked(DemoWeaponCatalog::IdAt(InspectedWeapon))) { MenuMessage = TEXT("尚未解锁，请先完成对应难度"); UE_LOG(LogFPSDemo, Log, TEXT("Weapon equip rejected: locked")); return; }
	MenuMessage = DemoPawn->GetWeaponComponent()->SelectPrimary(InspectedWeapon - 1) ? TEXT("主武器已装备 · 关闭终端后按 1 / 2 切换") : TEXT("装备失败，请检查武器配置与日志");
}
void ADemoPlayerController::RetryProfileSave()
{
	DEMO_LOG_CALL();
	if (HasBlockingOverlay() || !IsWeaponMenuOpen() || InspectedWeapon != INDEX_NONE) { UE_LOG(LogFPSDemo, Log, TEXT("Save retry rejected: page/modal")); return; } // 只允许当前最上层终端发起保存。
	if (UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Profile->SaveProfile(); // 先写本地，再请求异步同步；不等待网络阻塞操作。
	if (UDemoCloudSync* Cloud = GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->Retry(); // GI持有同步器，跨地图保持队列。
}
void ADemoPlayerController::CloudAction(int32 Choice)
{
	DEMO_LOG_CALL();
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前世界阶段，旧帧按钮不能覆盖活动战役。
	if (HasBlockingOverlay() || !State || State->Phase != EDemoPhase::Lobby || Choice < 0 || Choice > 2) { UE_LOG(LogFPSDemo, Warning, TEXT("Cloud action rejected: modal/phase/choice")); return; }
	if (UDemoCloudSync* Cloud = GetGameInstance()->GetSubsystem<UDemoCloudSync>()) // 当前GI拥有，不缓存到控制器。
	{ if (Choice == 0) Cloud->Retry(); else Cloud->ResolveConflict(Choice == 1); }
}
const FString& ADemoPlayerController::GetMenuMessage() const { DEMO_LOG_TICK(); return MenuMessage; }
void ADemoPlayerController::RestartPressed()
{
	DEMO_LOG_CALL();
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Restart ignored beneath overlay")); return; } // 创建/暂停/设置必须用当前页的明确按钮操作。
	// Enter 必须有正在显示的确认框才允许确认；普通备战阶段按 Enter 仍不会直接跳关。
	if (bNextLevelConfirmationOpen) { ConfirmNextLevel(); return; }
	// Enter在大厅打开存档页，死亡清空成长回安全区，胜利清空成长/银币并保留金币回安全区；系统错误回大厅，其他阶段GM拒绝。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 借用当前状态。
	if (State && State->Phase == EDemoPhase::Lobby) { StartGamePressed(); return; }
	if (AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) Mode->RestartDemo();
}

void ADemoPlayerController::ShowLobby()
{
	DEMO_LOG_CALL();
	// 大厅切换清理本地待确认引用；不携带上一局入口或进度。
	TerminalPage = EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; // 新大厅不携带武器页或详情窗口状态。
	InspectedWeapon = INDEX_NONE;
	bNextLevelConfirmationOpen = false;
	PendingNextLevelTerminal.Reset();
	PendingCompletedLevel = INDEX_NONE;
	bMenuOpen = false;
	bRewardMenu = false;
	MenuPage = EDemoMenuPage::None;
	bPauseMenuActive = false;
	MenuMessage.Empty();
	// Pawn 仍供 ASC 初始化使用；大厅背景图覆盖场景、菜单半透明且停用重力，等待 StartRun 放到安全区。
	if (ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetPawn()))
	{
		DemoPawn->StopFiring();
		DemoPawn->GetCharacterMovement()->DisableMovement();
	}
	SetMenuInput(true);
}
void ADemoPlayerController::StartGamePressed()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 只显示选择页，不直接开始World。
    if (!State || State->Phase != EDemoPhase::Lobby || HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Start menu rejected: phase/overlay")); return; }
    GetGameInstance()->GetSubsystem<UDemoRunSaves>()->RefreshSlots();
    MenuPage = EDemoMenuPage::Saves;
    MenuMessage.Empty();
    RefreshMenuInput();
}
void ADemoPlayerController::DifficultyPressed(EDemoDifficulty Difficulty)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 难度只来自安全区出发弹窗。
    if (!State || State->Phase != EDemoPhase::Hub || !bNextLevelConfirmationOpen || HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Difficulty input rejected: phase/departure/overlay")); return; }
    if (GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->SelectDifficulty(Difficulty)) { bDifficultyChosen = true; ConfirmNextLevel(); }
}
void ADemoPlayerController::EndlessPressed()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 只接受安全区当前终端弹窗。
    if (!State || State->Phase!=EDemoPhase::Hub || !bNextLevelConfirmationOpen || HasBlockingOverlay())
    { UE_LOG(LogFPSDemo,Warning,TEXT("Endless input rejected: phase/menu")); return; }
    if (GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->SelectEndless()) { bDifficultyChosen=true; ConfirmNextLevel(); }
}
void ADemoPlayerController::ShowEndlessUnlockTip()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 防止其他难度或半场战斗显示虚假解锁。
    if (!State || State->Phase!=EDemoPhase::Victory || State->Difficulty!=EDemoDifficulty::Hell)
    { UE_LOG(LogFPSDemo,Warning,TEXT("Endless tip rejected outside Hell victory")); return; }
    MenuPage=EDemoMenuPage::EndlessUnlock;
    MenuMessage.Empty(); RefreshMenuInput();
}

void ADemoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
	// 外部停止试玩/旅行也解除退出意图；耐久队列由GI保存，不持有旧控制器回调。
	if (MenuPage == EDemoMenuPage::QuitSaving)
		if (UDemoCloudSync* Cloud = GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->CancelExitSync();
	if (ULocalPlayer* Local = GetLocalPlayer())
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (CombatMappingContext) Subsystem->RemoveMappingContext(CombatMappingContext);
			if (MappingContext) Subsystem->RemoveMappingContext(MappingContext);
		}
	Super::EndPlay(EndPlayReason);
}
