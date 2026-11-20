// 职责：本地输入、菜单和多人登记的控制器协调。使用：引擎生命周期/输入回调；UI状态归MenuFlow，武器动作经Pawn组件与服务器GAS校验。
#include "Player/DemoPlayerController.h"
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackComponent.h"
#include "Weapons/Melee/DemoMeleeComponent.h" // 数字3在原升级快捷键和独立近战槽之间按当前UI状态分发。
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Kismet/GameplayStatics.h"
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
#include "Interaction/HubShops/DemoShopFocusComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoLocalViewComponent.h"
#include "Interaction/HubShops/Rendering/DemoLocalOutlineComponent.h" // 正常PIE/Standalone默认渲染服务，不依赖审查flags。
#include "Debug/DemoLog.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Components/InputComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Settings/DemoGameUserSettings.h" // 本地PIE/游戏视口采用持久化帧同步，服务器不写显示设置。
#include "Kismet/KismetSystemLibrary.h"

#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Common/DemoUIManager.h"
#include "Player/Coop/Preparation/DemoLocalEncounterPreload.h"
ADemoPlayerController::ADemoPlayerController()
{
	DEMO_LOG_CALL();
    DamageFeedback=CreateDefaultSubobject<UDemoPlayerDamageFeedbackComponent>(TEXT("DamageFeedback")); // 默认子对象由引擎建立Owner RPC通道，远端不创建本机资源。
    MenuFlow = CreateDefaultSubobject<UDemoMenuFlowComponent>(TEXT("MenuFlow")); // 页面状态独立，输入资源仍由PC持有。
    CommonUI = CreateDefaultSubobject<UDemoUIManager>(TEXT("CommonUI")); // 每本地玩家独立CommonUI栈，远端不会创建Widget。
    LocalOutline=CreateDefaultSubobject<UDemoLocalOutlineComponent>(TEXT("LocalOutline")); // 先建立唯一渲染持有者；构造阶段不访问World/加载材质，具体资源由合法本机目标按需创建。
    ShopFocus = CreateDefaultSubobject<UDemoShopFocusComponent>(TEXT("ShopFocus")); // 只追加交互组件，不改现有第一人称相机或视向逻辑。
    CasinoDeviceInput=CreateDefaultSubobject<UDemoCasinoDeviceInputComponent>(TEXT("CasinoDeviceInput")); // 本人可靠RPC的默认子对象，非客户端无Owner的世界机台。
    CasinoLocalView=CreateDefaultSubobject<UDemoCasinoLocalViewComponent>(TEXT("CasinoLocalView")); // 本地私有表现独立，无共享碰撞/相机副作用。
    EncounterPreload = CreateDefaultSubobject<UDemoLocalEncounterPreload>(TEXT("EncounterPreload")); // PC卸载取消异步，房主与远端各自检查本地资源/地形。
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
    if (IsLocalController())
    {
        if(UDemoGameUserSettings* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())) Settings->ApplyFrameSync(); // 本地视口此时就绪；PIE同样需要r.VSyncEditor。
        ShowLobby();
        RegistrationDeadline=FPlatformTime::Seconds()+30.0; // 依赖晚到可重试，不能无限停留在未授权半连接状态。
        ConnectionStatus=GetNetMode()==NM_Standalone?FString():TEXT("正在登记队伍身份……");
        UpdateCoopRegistration(true);
    }
    BindProgressView();
}
void ADemoPlayerController::SetupInputComponent()
{
	DEMO_LOG_CALL();
	Super::SetupInputComponent();
	InstallVoiceInput(); // T使用Enhanced Input Started/Completed/Canceled，不依赖武器开火映射是否存在。
    InstallTeamPingInput(); // G使用同一Enhanced Input框架，PC拥有映射可跨Pawn重生。
    ShopFocus->InstallInput(InputComponent); // 统一Started交互；Character仅在缺作者Action时保留可配置键回退。
	// Esc必须在暂停时继续处理，否则无法通过同一按键恢复World。
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ADemoPlayerController::EscapePressed).bExecuteWhenPaused = true;
	// 非菜单时不吞掉1/2，允许Pawn的Enhanced Input处理武器槽；菜单时Combat映射已移除。
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ADemoPlayerController::SelectFirst).bConsumeInput = false;
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ADemoPlayerController::SelectSecond).bConsumeInput = false;
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ADemoPlayerController::SelectThird);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ADemoPlayerController::TabPressed).bExecuteWhenPaused=true; // Tab打开/关闭同一页，单人暂停后仍接收。
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ADemoPlayerController::RestartPressed);
}
void ADemoPlayerController::SetMenuInput(bool bEnabled)
{
	DEMO_LOG_CALL();
    if (!IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("INPUT_POLICY_REJECT nonlocal controller")); return; }
    if (bEnabled) { VoiceReleased(); DamageFeedback->ResetForLifecycle(); } // 本人菜单立即清装饰/呼吸，不改变服务器HP或伤害门禁。
    ADemoCharacter* LocalPawn = Cast<ADemoCharacter>(GetPawn()); // 同步借用本人的Avatar，换Pawn会使下方幂等键失效。
    ULocalPlayer* Local = GetLocalPlayer(); // 本控制器所属本地玩家，不能取全局Player0。
    UEnhancedInputLocalPlayerSubsystem* Subsystem = Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr; // 初始化期间允许为空，到达后会补应用。
    const bool bGameplayBlocked = !IsCoopRegistered() || (LocalPawn && LocalPawn->IsCoopEliminated()); // 身份未确认/观战不能驱动旧Avatar。
    const bool bBlockMovement = bEnabled || bGameplayBlocked; // 输入门禁统一真值，不把金币变动当成新模式。
    const bool bHasCombatMapping = Subsystem && CombatMappingContext && Subsystem->HasMappingContext(CombatMappingContext); // 只读实际映射，修复外部移除而非盲目重复Add。
    const bool bWantsCombatMapping = Subsystem && CombatMappingContext && !bBlockMovement; // 等依赖到齐再装配，缺资源不会假记为已激活。
    const bool bPawnChanged = InputPolicyPawn.Get() != GetPawn(); // 同PC重生/客户端Pawn晚到需重新应用阻塞。
    const bool bMenuChanged = !bInputPolicyApplied || bAppliedMenuInput != bEnabled; // 只有真正的菜单模式边沿才重抢Slate焦点。
    const bool bBlockChanged = !bInputPolicyApplied || bPawnChanged || bAppliedMovementBlocked != bBlockMovement
        || IsMoveInputIgnored() != bBlockMovement || IsLookInputIgnored() != bBlockMovement; // 只在门禁不一致时清锁计数。
    if (!bMenuChanged && !bBlockChanged && InputPolicySubsystem.Get() == Subsystem && bHasCombatMapping == bWantsCombatMapping)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("INPUT_POLICY_UNCHANGED menu=%d blocked=%d"), bEnabled, bBlockMovement); return; }

    // 先立门禁再清键；Flush可能产生Enhanced Canceled/Completed，回调不能趁菜单过渡重新开火。
    if (bBlockChanged)
    {
        ResetIgnoreMoveInput(); ResetIgnoreLookInput();
        SetIgnoreMoveInput(bBlockMovement); SetIgnoreLookInput(bBlockMovement);
        if (bBlockMovement)
        {
            FlushPressedKeys(); // 只在真正进入阻塞/更换被阻塞Pawn时清键，绝不在每次发银币时清理。
            if (LocalPawn)
            {
                LocalPawn->GetWeaponComponent()->CancelActions(); LocalPawn->StopJumping();
                LocalPawn->ConsumeMovementInputVector(); // 清除本帧尚未被CharacterMovement消费的移动意图。
                LocalPawn->GetCharacterMovement()->ClearAccumulatedForces(); // 撤销尚未执行的Launch，防关闭菜单后旧冲量重新推动玩家。
                LocalPawn->GetCharacterMovement()->StopMovementImmediately();
            }
        }
    }
    if (bHasCombatMapping != bWantsCombatMapping)
    {
        if (!bWantsCombatMapping) Subsystem->RemoveMappingContext(CombatMappingContext);
        else
        {
            FModifyContextOptions Options; // 只在真实从菜单回到游戏时忽略按住的布尔武器键，普通钱包刷新不触发重建。
            Options.bIgnoreAllPressedKeysUntilRelease = true;
            Subsystem->AddMappingContext(CombatMappingContext, 1, Options);
        }
    }
    if (bMenuChanged && (!CommonUI || (!CommonUI->UsesWidgetMenu()&&!CommonUI->OwnsInput()))) // 意图短暂关闭不等于栈已交还；同帧再开时CommonUI会复用输入配置，不能由PC提前隐藏光标。RestoreLegacyInput在真正清栈后恢复游戏模式。
    {
        bShowMouseCursor = bEnabled; bEnableClickEvents = bEnabled;
        if (bEnabled) SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
        else SetInputMode(FInputModeGameOnly()); // 同模式重复调用不再向Slate提交焦点/捕获操作。
    }
    bInputPolicyApplied = true; bAppliedMenuInput = bEnabled; bAppliedMovementBlocked = bBlockMovement;
    InputPolicyPawn = GetPawn(); InputPolicySubsystem = Subsystem;
    UE_LOG(LogFPSDemo, Log, TEXT("INPUT_POLICY_APPLIED menu=%d blocked=%d pawnChanged=%d mapping=%d mappingChanged=%d"),
        bEnabled, bBlockMovement, bPawnChanged, bWantsCombatMapping, bHasCombatMapping != bWantsCombatMapping);
}
void ADemoPlayerController::SelectFirst()
{ DEMO_LOG_CALL(); SelectUpgrade(0); }
void ADemoPlayerController::SelectSecond()
{ DEMO_LOG_CALL(); SelectUpgrade(1); }
void ADemoPlayerController::SelectThird()
{
    DEMO_LOG_CALL();
    if (IsUpgradeMenuOpen()) { SelectUpgrade(2); return; } // 消费本次选择，不因关闭奖励页又在同一输入帧装备武器。
    if (ADemoCharacter* ControlledCharacter=Cast<ADemoCharacter>(GetPawn())) // 当前Avatar同步借用，不能控制观战目标，也不遮蔽APlayerController::Player。
        if (UDemoMeleeComponent* Melee=ControlledCharacter->FindComponentByClass<UDemoMeleeComponent>()) Melee->EquipPressed();
}
void ADemoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
    RemoveVoiceInput(); // PS通常此时仍有效，先释放麦克风；PS.EndPlay还有独立兜底。
    RemoveTeamPingInput(); // 精确撤销本PC的G映射/绑定，不清除其他模块输入上下文。
    if (ObservedProgress.IsValid()) ObservedProgress->OnChanged.Remove(ProgressChangedHandle); // 卸载前解绑PS，避免旧PC刷新新World。
    ObservedProgress.Reset();
    CommonUI->Shutdown(); // 先移除控件与动作路由，防卸载回调访问旧Flow。
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
{
    DEMO_LOG_TICK(); Super::PlayerTick(DeltaTime); UpdateCoopRegistration(); MenuFlow->UpdateRealtime(DeltaTime);
    CommonUI->Sync(); // 模型同步后更新Widget栈，动画由已有控件推进，不调用权威经济服务。
#if WITH_EDITOR
    RecordRuntimeDiagnostics(DeltaTime); // 每秒小快照替代长期逐帧洪泛，保留真实卡顿/移动边界证据。
#endif
}
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
void ADemoPlayerController::TabPressed()
{ DEMO_LOG_CALL(); MenuFlow->TabPressed(); }
void ADemoPlayerController::EscapePressed()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	if (CasinoDeviceInput->IsOperating() && !HasBlockingOverlay()) { CasinoDeviceInput->ExitOperation(); return; } // 世界操作退出不伪装暂停/隐形菜单；保原顶层覆盖层优先级。
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

void ADemoPlayerController::OnPossess(APawn* InPawn)
{ DEMO_LOG_CALL(); Super::OnPossess(InPawn); DamageFeedback->RefreshAvatarBinding(); BindProgressView(); UpdateCoopRegistration(true); }
void ADemoPlayerController::OnRep_PlayerState()
{ DEMO_LOG_CALL(); Super::OnRep_PlayerState(); BindProgressView(); UpdateCoopRegistration(true); }
void ADemoPlayerController::OnRep_Pawn()
{ DEMO_LOG_CALL(); Super::OnRep_Pawn(); DamageFeedback->RefreshAvatarBinding(); BindProgressView(); UpdateCoopRegistration(true); MenuFlow->RefreshFromReplicatedState(); }
void ADemoPlayerController::BindProgressView()
{
    DEMO_LOG_CALL();
    ADemoPlayerState* State = GetPlayerState<ADemoPlayerState>(); // 当前PS，旅行与首次网络复制可能尚未就绪。
    UDemoPlayerProgressComponent* Progress = State ? State->GetProgressComponent() : nullptr; // PS拥有，不转移生命周期。
    if (ObservedProgress.Get() == Progress) return;
    if (ObservedProgress.IsValid()) ObservedProgress->OnChanged.Remove(ProgressChangedHandle);
    ObservedProgress = Progress;
    if (Progress) { ProgressChangedHandle=Progress->OnChanged.AddUObject(this,&ADemoPlayerController::OnProgressChanged); OnProgressChanged(); }
}
void ADemoPlayerController::OnProgressChanged()
{
    DEMO_LOG_CALL();
    if (ObservedProgress.IsValid()) MenuSnapshot.Progress=ObservedProgress->GetPublicSnapshot(); // 本机缓存也不保留私有雷位，服务器和客户端使用一致的脱敏规则。
    if (HasAuthority()) PublishMenuSnapshot();
    if (IsLocalController()) MenuFlow->RefreshFromReplicatedState();
}
void ADemoPlayerController::SubmitGameplayCommand(FDemoGameplayCommandRequest Request)
{
    DEMO_LOG_CALL();
    if (!IsLocalController() || NextRequestId == MAX_int32) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMAND rejected owner/sequence")); return; }
    Request.RequestId=++NextRequestId;
    ServerGameplayCommand(Request);
}
void ADemoPlayerController::ServerGameplayCommand_Implementation(const FDemoGameplayCommandRequest& Request)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 仅此服务器RPC层访问权威服务。
    if (!Mode || Request.RequestId<=LastServerRequestId || Request.RequestId<=0)
    { UE_LOG(LogFPSDemo,Warning,TEXT("COMMAND rejected stale request=%d"),Request.RequestId); return; }
    LastServerRequestId=Request.RequestId; // 执行前消费序号，同步GAS回调不能重入同一交易。
    FDemoCommandResult Result=IsCoopRegistered()?Mode->ExecutePlayerCommand(this,Request):FDemoCommandResult::Reject(TEXT("NotRegistered"),TEXT("队伍身份尚未登记，不能执行游戏操作")); // 服务器从this解析当前Pawn与个人进度。
    Result.RequestId=Request.RequestId;
    ClientGameplayCommandResult(Request.Command,Result,Mode->BuildPlayerMenuSnapshot(this));
}
void ADemoPlayerController::ServerRegisterCoopPlayer_Implementation(const FString& PlayerKey)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 服务器固定队伍注册，不把LAN GUID当认证令牌。
    ADemoCharacter* CurrentPawn=Cast<ADemoCharacter>(GetPawn()); // PS、Pawn和GAS可以晚于本机BeginPlay，未就绪只重试。
    if (!Mode || !GetPlayerState<ADemoPlayerState>() || !CurrentPawn || !CurrentPawn->GetDemoAttributes() || !GetWorld()->GetGameState<ADemoGameState>())
    { ClientCoopRegistrationResult(false,true,TEXT("正在等待角色与队伍就绪……")); return; }
    if (!Mode->RegisterCoopPlayer(this,PlayerKey))
    { UE_LOG(LogFPSDemo,Warning,TEXT("COOP registration rejected identity/phase/restore")); ClientCoopRegistrationResult(false,false,TEXT("无法加入固定队伍：身份已在线、队伍已满或恢复失败，请返回大厅后重试")); return; }
    bCoopRegistered=true;
    ClientCoopRegistrationResult(true,false,FString());
    PublishMenuSnapshot();
}
void ADemoPlayerController::UpdateCoopRegistration(bool bLifecycleReady)
{
    DEMO_LOG_TICK();
    if (!IsLocalController() || !HasActorBegunPlay() || bRegistrationFinished || RegistrationDeadline<=0.0) return;
    const double Now=FPlatformTime::Seconds(); // 单调时间不受游戏暂停、服务器时钟或用户修改本地时间影响。
    if (Now>=RegistrationDeadline)
    {
        bRegistrationFinished=true; bRegistrationAwaitingReply=false;
        ConnectionStatus=TEXT("队伍登记超时，请按 Esc 返回大厅后重新加入");
        UE_LOG(LogFPSDemo,Warning,TEXT("COOP registration timed out after 30 seconds"));
        GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->RegistrationFailed(ConnectionStatus); return; // 统一断开失败连接并回到房间列表。
    }
    if (bRegistrationAwaitingReply || (!bLifecycleReady && Now<NextRegistrationAttempt)) return;
    ADemoCharacter* CurrentPawn=Cast<ADemoCharacter>(GetPawn()); // 客户端Pawn/PS引用晚到时等待生命周期或下一次有界检查。
    if (!GetPlayerState<ADemoPlayerState>() || !CurrentPawn) { NextRegistrationAttempt=Now+1.0; return; }
    UDemoCoopSessionSubsystem* Session=GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>(); // 本GI临时/正式LAN键，不传入钱包。
    if (!Session) { NextRegistrationAttempt=Now+1.0; return; }
    bRegistrationAwaitingReply=true; NextRegistrationAttempt=Now+1.0;
    ServerRegisterCoopPlayer(Session->GetLocalPlayerKey());
}
void ADemoPlayerController::ClientCoopRegistrationResult_Implementation(bool bAccepted,bool bRetryable,const FString& Reason)
{
    DEMO_LOG_CALL();
    if (!IsLocalController() || (bRegistrationFinished && !bCoopRegistered)) return; // 超时后迟到成功不能重新授予本机输入。
    bRegistrationAwaitingReply=false;
    if (bAccepted) { bCoopRegistered=true; bRegistrationFinished=true; ConnectionStatus.Empty(); }
    else
    {
        ConnectionStatus=Reason; bRegistrationFinished=!bRetryable;
        NextRegistrationAttempt=FPlatformTime::Seconds()+1.0;
        UE_LOG(LogFPSDemo,Warning,TEXT("COOP registration reply retryable=%d"),bRetryable);
        if (!bRetryable) GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->RegistrationFailed(ConnectionStatus); // 明确拒绝立即释放服务器名额，不继续挂在观战/黑屏状态。
    }
    MenuFlow->RefreshFromReplicatedState();
}
bool ADemoPlayerController::IsCoopRegistered() const { DEMO_LOG_TICK(); return GetNetMode()==NM_Standalone || (HasAuthority() && IsLocalController()) || bCoopRegistered; }
const FString& ADemoPlayerController::GetConnectionStatus() const { DEMO_LOG_TICK(); return ConnectionStatus; }

void ADemoPlayerController::ClientRunCompletion_Implementation(const FDemoRunCompletion& Completion)
{
    DEMO_LOG_CALL();
    if (!IsLocalController()) return;
    if (UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Profile->RecordCompletion(Completion); // 仅本机账号事实进入Outbox。
}
void ADemoPlayerController::ClientGameplayCommandResult_Implementation(EDemoGameplayCommand Command,const FDemoCommandResult& Result,const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    if (!IsLocalController() || Result.RequestId<=LastClientResultId) { UE_LOG(LogFPSDemo,Log,TEXT("COMMAND stale reply ignored")); return; }
    LastClientResultId=Result.RequestId;
    MenuSnapshot=Snapshot;
    if(CommonUI) CommonUI->PresentCommandResult(Command,Result); // RequestId已校验；展示回执不驱动交易或提前推断落盘。
    if (Command==EDemoGameplayCommand::BeginInteraction && Result.IsApplied()) MenuFlow->OpenApprovedInteraction(Snapshot);
    bool bRememberPrimarySelection=true; // 普通权威装备保持原偏好；测试借用不改本机档案修订/原合法主枪偏好。
#if WITH_EDITOR
    bRememberPrimarySelection=Result.Code!=TEXT("EditorTemporaryEquipment");
#endif
    if (Command==EDemoGameplayCommand::EquipWeapon && Result.IsApplied() && !Snapshot.PrimaryWeaponId.IsNone() && bRememberPrimarySelection)
        if (UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Profile->RememberPrimary(Snapshot.PrimaryWeaponId); // 只更新拥有者本机偏好，不从远端选择写房主GI。
    MenuFlow->ApplyCommandResult(Command,Result);
}
void ADemoPlayerController::ClientRefreshMenuSnapshot_Implementation(const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    if (!IsLocalController()) return;
    MenuSnapshot=Snapshot;
    MenuFlow->RefreshFromReplicatedState();
}
void ADemoPlayerController::PublishMenuSnapshot()
{
    DEMO_LOG_CALL();
    if (!HasAuthority()) return;
    if (AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) ClientRefreshMenuSnapshot(Mode->BuildPlayerMenuSnapshot(this)); // 只发送给拥有此PC的连接。
}
const FDemoPlayerMenuSnapshot& ADemoPlayerController::GetMenuSnapshot() const { DEMO_LOG_TICK(); return MenuSnapshot; }
void ADemoPlayerController::AcceptHubShopMenuSnapshot(const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    if (!IsLocalController() || !Snapshot.CasinoDeviceToken.IsValid()) { UE_LOG(LogFPSDemo,Log,TEXT("HUB_DEVICE_VIEW rejected owner/session")); return; }
    MenuSnapshot=Snapshot; // 仅批准RPC赋值，Widget不自行合成服务端投影。
}
bool ADemoPlayerController::SetLocalMenuPaused(bool bPaused)
{
    DEMO_LOG_CALL();
    return GetNetMode()!=NM_Standalone || SetPause(bPaused); // Listen Server与远程客户端菜单均不冻结战局。
}
void ADemoPlayerController::LeaveCoopToLobby()
{
    DEMO_LOG_CALL();
    if (!IsLocalController() || GetNetMode()!=NM_Client) return;
    GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->Leave(); // 同时销毁本机OSS会话，避免下次加入提示AlreadyInSession。
}
void ADemoPlayerController::HostCoop()
{
    DEMO_LOG_CALL();
    if (!IsLocalController()) return;
    if (UDemoRoomSubsystem* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()) Rooms->HostCurrentHub(); // 控制台和UI共用广播/超时生命周期。
}
void ADemoPlayerController::JoinCoop(const FString& Address)
{
    DEMO_LOG_CALL();
    if (!IsLocalController()) return;
    if (UDemoCoopSessionSubsystem* Session=GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()) Session->JoinCoop(Address);
}

bool ADemoPlayerController::SaveOwnedProgressBeforeExit(FString& OutMessage)
{
    DEMO_LOG_CALL();
    if (!IsLocalController()) { OutMessage=TEXT("仅本机可以保存并退出"); return false; }
    UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 只保存此进程本机账号，不能代替其他玩家Profile。
    if (!Profile || !Profile->SaveProfile()) { OutMessage=Profile?Profile->GetStatus():TEXT("本机存档尚未就绪"); return false; }
    if (HasAuthority())
    {
        AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 仅房主/Standalone负责队伍检查点。
        if (!Mode || !Mode->SaveCheckpoint()) { OutMessage=TEXT("战局检查点保存失败，请重试"); return false; }
    }
    OutMessage=GetNetMode()==NM_Client?TEXT("本机进度已保存，队伍检查点由房主保存"):TEXT("本机与战局进度已保存");
    return true;
}
