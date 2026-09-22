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

#include "UI/Flow/DemoMenuFlowComponent.h"
ADemoPlayerController::ADemoPlayerController()
{
	DEMO_LOG_CALL();
    MenuFlow = CreateDefaultSubobject<UDemoMenuFlowComponent>(TEXT("MenuFlow")); // 页面状态独立，输入资源仍由PC持有。
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
void ADemoPlayerController::SelectFirst()
{ DEMO_LOG_CALL(); SelectUpgrade(0); }
void ADemoPlayerController::SelectSecond()
{ DEMO_LOG_CALL(); SelectUpgrade(1); }
void ADemoPlayerController::SelectThird()
{ DEMO_LOG_CALL(); SelectUpgrade(2); }
void ADemoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
	MenuFlow->Shutdown(); // 先撤销页面/退出意图，再移除输入映射。
	if (ULocalPlayer* Local = GetLocalPlayer())
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (CombatMappingContext) Subsystem->RemoveMappingContext(CombatMappingContext);
			if (MappingContext) Subsystem->RemoveMappingContext(MappingContext);
		}
	Super::EndPlay(EndPlayReason);
}
void ADemoPlayerController::PlayerTick(float DeltaTime)
{ DEMO_LOG_TICK(); Super::PlayerTick(DeltaTime); MenuFlow->UpdateRealtime(DeltaTime); }
ADemoInteractable* ADemoPlayerController::GetMenuTerminal() const { DEMO_LOG_TICK(); return MenuFlow->GetMenuTerminal(); }
uint64 ADemoPlayerController::GetMenuTerminalGeneration() const { DEMO_LOG_TICK(); return MenuFlow->GetMenuTerminalGeneration(); }
UDemoMenuFlowComponent* ADemoPlayerController::GetMenuFlow() const { DEMO_LOG_TICK(); return MenuFlow; }
void ADemoPlayerController::OpenUpgradeMenu(bool bReward)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->OpenUpgradeMenu(bReward);
}
void ADemoPlayerController::CloseUpgradeMenu()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->CloseUpgradeMenu();
}
void ADemoPlayerController::OpenNextLevelConfirmation(ADemoInteractable* Terminal)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->OpenNextLevelConfirmation(Terminal);
}
void ADemoPlayerController::CancelNextLevelConfirmation()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->CancelNextLevelConfirmation();
}
void ADemoPlayerController::ConfirmNextLevel()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ConfirmNextLevel();
}
bool ADemoPlayerController::IsNextLevelConfirmationOpen() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->IsNextLevelConfirmationOpen();
}
void ADemoPlayerController::SelectUpgrade(int32 Choice)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->SelectUpgrade(Choice);
}
void ADemoPlayerController::ShowEndScreen()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ShowEndScreen();
}
bool ADemoPlayerController::IsUpgradeMenuOpen() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->IsUpgradeMenuOpen();
}
bool ADemoPlayerController::IsRewardMenu() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->IsRewardMenu();
}
bool ADemoPlayerController::IsWeaponMenuOpen() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->IsWeaponMenuOpen();
}
int32 ADemoPlayerController::GetInspectedWeapon() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->GetInspectedWeapon();
}
void ADemoPlayerController::SetTerminalWeaponPage(bool bWeapons)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->SetTerminalWeaponPage(bWeapons);
}
void ADemoPlayerController::InspectWeapon(int32 CatalogIndex)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->InspectWeapon(CatalogIndex);
}
void ADemoPlayerController::CloseWeaponTip()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->CloseWeaponTip();
}
void ADemoPlayerController::EquipInspectedWeapon()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->EquipInspectedWeapon();
}
void ADemoPlayerController::RetryProfileSave()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->RetryProfileSave();
}
void ADemoPlayerController::CloudAction(int32 Choice)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->CloudAction(Choice);
}
const FString& ADemoPlayerController::GetMenuMessage() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->GetMenuMessage();
}
void ADemoPlayerController::RestartPressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->RestartPressed();
}
void ADemoPlayerController::ShowLobby()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ShowLobby();
}
void ADemoPlayerController::StartGamePressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->StartGamePressed();
}
void ADemoPlayerController::DifficultyPressed(EDemoDifficulty Difficulty)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->DifficultyPressed(Difficulty);
}
void ADemoPlayerController::EndlessPressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->EndlessPressed();
}
void ADemoPlayerController::ShowEndlessUnlockTip()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ShowEndlessUnlockTip();
}
EDemoMenuPage ADemoPlayerController::GetMenuPage() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->GetMenuPage();
}
bool ADemoPlayerController::HasBlockingOverlay() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->HasBlockingOverlay();
}
bool ADemoPlayerController::IsPauseMenuOpen() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->IsPauseMenuOpen();
}
void ADemoPlayerController::EscapePressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->EscapePressed();
}
void ADemoPlayerController::CloseOverlay()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->CloseOverlay();
}
void ADemoPlayerController::SelectSaveSlot(int32 Index)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->SelectSaveSlot(Index);
}
void ADemoPlayerController::ConfirmCreateSave(bool Confirm)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ConfirmCreateSave(Confirm);
}
void ADemoPlayerController::OnRunReady()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->OnRunReady();
}
void ADemoPlayerController::ReturnHubPressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ReturnHubPressed();
}
void ADemoPlayerController::ConfirmReturnHub(bool Confirm)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ConfirmReturnHub(Confirm);
}
void ADemoPlayerController::ReturnLobbyPressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ReturnLobbyPressed();
}
void ADemoPlayerController::SettingsPressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->SettingsPressed();
}
bool ADemoPlayerController::IsSettingsOpen() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->IsSettingsOpen();
}
void ADemoPlayerController::CycleSetting(int32 Setting, int32 Direction)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->CycleSetting(Setting, Direction);
}
FString ADemoPlayerController::GetSettingText(int32 Setting) const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->GetSettingText(Setting);
}
void ADemoPlayerController::ApplyUserSettings()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ApplyUserSettings();
}
void ADemoPlayerController::ConfirmDisplaySettings(bool Confirm)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ConfirmDisplaySettings(Confirm);
}
float ADemoPlayerController::GetDisplayConfirmSeconds() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->GetDisplayConfirmSeconds();
}
EDemoQuitState ADemoPlayerController::GetQuitState() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->GetQuitState();
}
bool ADemoPlayerController::IsQuitLocalSaved() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->IsQuitLocalSaved();
}
void ADemoPlayerController::QuitPressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->QuitPressed();
}
void ADemoPlayerController::RetryQuitSave()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->RetryQuitSave();
}
void ADemoPlayerController::QuitWithLocalSave()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->QuitWithLocalSave();
}
void ADemoPlayerController::CancelQuitSave()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->CancelQuitSave();
}
bool ADemoPlayerController::IsAmmoMenuOpen() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->IsAmmoMenuOpen();
}
int32 ADemoPlayerController::GetInspectedAmmo() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->GetInspectedAmmo();
}
int32 ADemoPlayerController::GetPendingAmmoPrice() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return MenuFlow->GetPendingAmmoPrice();
}
void ADemoPlayerController::OpenAmmoMenu()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->OpenAmmoMenu();
}
void ADemoPlayerController::InspectAmmo(int32 Index)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->InspectAmmo(Index);
}
void ADemoPlayerController::AmmoAction()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->AmmoAction();
}
void ADemoPlayerController::ConfirmAmmoPurchase(bool Confirm)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	MenuFlow->ConfirmAmmoPurchase(Confirm);
}
