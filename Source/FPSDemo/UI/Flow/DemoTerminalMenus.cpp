// 职责：终端/奖励与出发意图；业务经拥有者PC校验，不直接修改经济或核心。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/InputComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h" // 单独编译 Game 目标时，终端阶段校验需要完整的 UWorld 定义。
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/DemoInteractable.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Player/DemoCloudSync.h"
#include "Player/DemoPlayerProfile.h"
#include "Save/DemoRunSave.h"
#include "Settings/DemoGameUserSettings.h"
#include "UI/DemoHUD.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/DemoWeaponComponent.h"
void UDemoMenuFlowComponent::OpenUpgradeMenu(bool bReward)
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	// 确认框与升级菜单互斥，防止旧热区或重复交互覆盖正在处理的出发意图。
	if (HasBlockingOverlay() || bNextLevelConfirmationOpen) { UE_LOG(LogFPSDemo, Log, TEXT("Upgrade menu rejected: next-level confirmation open")); return; }
    if (!bReward && (!MenuTerminal.IsValid() || !GetController()->GetMenuSnapshot().TerminalBlockReason.IsEmpty()))
    { UE_LOG(LogFPSDemo,Log,TEXT("MENU_OPEN rejected: no approved terminal")); return; } // 客户端页只消费服务器批准结果，交易服务仍重验。

	bMenuOpen = true;
    ResetGamblePresentation(); // 新交互不能继承上一抽取的动画/确认状态。
	bRewardMenu = bReward;
	TerminalPage = EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; PendingGamblePrice=INDEX_NONE; // 页面变化撤销旧赌博确认。 // 每次交互默认进入属性页，清除上一终端的详情意图。
	InspectedWeapon = INDEX_NONE;
	MenuMessage.Empty();
	// 仅借用当前 Pawn；命名避免遮蔽 AController::Character 成员。
	if (ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetController()->GetPawn())) DemoPawn->StopFiring();
	SetMenuInput(true);
}
void UDemoMenuFlowComponent::CloseUpgradeMenu()
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Close terminal ignored beneath overlay")); return; } // 顶层模态不允许Tab误关奖励/终端。
	// 复用已有 Tab 绑定；取消确认只关闭本地窗口，不调用关卡推进。
	if (bNextLevelConfirmationOpen) { CancelNextLevelConfirmation(); return; }
	if(IsAmmoMenuOpen() && PendingAmmoPrice!=INDEX_NONE){ConfirmAmmoPurchase(false);return;} // Tab先取消购买确认。
	if (IsWeaponMenuOpen() && InspectedWeapon != INDEX_NONE) { CloseWeaponTip(); return; } // Tab先关详情，再按关闭终端。
	// 奖励必须选一次；按钮与快捷键拒绝时保留原阶段并记录原因。
	if (!bMenuOpen || bRewardMenu) { UE_LOG(LogFPSDemo, Log, TEXT("Close menu rejected: open=%d reward=%d"), bMenuOpen, bRewardMenu); return; }
    FDemoGameplayCommandRequest Request; // 本地关闭立即恢复输入，服务端单独撤销本人会话。
    Request.Command=bCasinoDevicePage?EDemoGameplayCommand::CloseCasinoDevice:EDemoGameplayCommand::EndInteraction;
    Request.CasinoDeviceToken=GetController()->GetMenuSnapshot().CasinoDeviceToken; GetController()->SubmitGameplayCommand(Request);
	bCasinoDevicePage=false;
	MenuTerminal.Reset(); MenuTerminalGeneration = 0;
	bMenuOpen = false;
    ResetGamblePresentation(); // Tab/关闭取消展示，但已提交的费用、奖励保留。
	TerminalPage = EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; PendingGamblePrice=INDEX_NONE; // 页面变化撤销旧赌博确认。
	InspectedWeapon = INDEX_NONE;
	UE_LOG(LogFPSDemo, Log, TEXT("UI menu closed; restoring game input"));
	SetMenuInput(false);
}
bool UDemoMenuFlowComponent::CanRequestNextLevel(const ADemoInteractable* Terminal) const
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 只读复制的战局，不访问客户端不存在的GM。
    const FDemoPlayerMenuSnapshot& View=GetController()->GetMenuSnapshot(); // 交互已由服务器建立，确认提交仍再次校验。
    return GetController()->IsLocalController() && State && IsValid(Terminal) && View.Terminal==Terminal && View.bNextLevel
        && !bMenuOpen && !HasBlockingOverlay()
        && (State->Phase==EDemoPhase::Hub || State->Phase==EDemoPhase::Intermission);
}
void UDemoMenuFlowComponent::OpenNextLevelConfirmation(ADemoInteractable* Terminal)
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	if (bNextLevelConfirmationOpen) { UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL open rejected: confirmation already open")); return; }
	if (!CanRequestNextLevel(Terminal)) return;
	NextTerminalGeneration = GetController()->GetMenuSnapshot().TerminalGeneration; // 记录权威回执对象版本。
	PendingNextLevelTerminal = Terminal;
	PendingCompletedLevel = GetWorld()->GetGameState<ADemoGameState>()->LevelNumber;
	bNextLevelConfirmationOpen = true;
	MenuMessage.Empty(); // 新页面不沿用上一轮购物或准备失败提示。
	bDifficultyChosen = false; // 安全区每次打开必须明确选择，避免Enter默认跳过。
	ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetController()->GetPawn()); // 校验后的当前角色，仅本次调用借用。
	if (DemoPawn) DemoPawn->StopFiring();
	if (DemoPawn) DemoPawn->GetCharacterMovement()->StopMovementImmediately(); // 停止残余速度，避免弹窗期间滑出交互范围。
	SetMenuInput(true);
	UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirmation opened target=%d"), PendingCompletedLevel + 1);
}
void UDemoMenuFlowComponent::CancelNextLevelConfirmation()
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	if (!bNextLevelConfirmationOpen) { UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL cancel ignored: no request")); return; }
    FDemoGameplayCommandRequest Request; // 明确取消也撤销服务器本人会话，隐藏页面不能继续持有交易权限。
    Request.Command=EDemoGameplayCommand::EndInteraction; GetController()->SubmitGameplayCommand(Request);
	NextTerminalGeneration = 0;
	bNextLevelConfirmationOpen = false;
	PendingNextLevelTerminal.Reset();
	PendingCompletedLevel = INDEX_NONE;
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 取消时重新读阶段，不能解除终局/大厅输入锁。
	SetMenuInput(bMenuOpen || !State || State->Phase == EDemoPhase::Lobby || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat);
	UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirmation closed; progress unchanged"));
}
void UDemoMenuFlowComponent::ConfirmNextLevel()
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Departure ignored beneath overlay")); return; } // 暂停页不接受下层Enter/旧按钮。
	if (!bNextLevelConfirmationOpen) { UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirm ignored: no request")); return; }
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 本次确认的真实进度，不能沿用打开时的显示值。
	if (!CanRequestNextLevel(PendingNextLevelTerminal.Get()) || !State || State->LevelNumber != PendingCompletedLevel)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL confirm rejected: request expired"));
		CancelNextLevelConfirmation();
		return;
	}
    if(!GetController()->GetMenuSnapshot().bCanLeadSession) { MenuMessage=TEXT("请先准备，等待房主出发"); UE_LOG(LogFPSDemo,Log,TEXT("NEXT_LEVEL host only")); return; }
	if (!State->TeamReady.bRequired && State->Phase == EDemoPhase::Hub && !bDifficultyChosen) { UE_LOG(LogFPSDemo, Log, TEXT("Choose difficulty before first departure")); return; }
    // 网络提交后由成功回执关闭页面；提前取消会撤销服务器会话，使紧随的出发请求失效。
    UE_LOG(LogFPSDemo, Log, TEXT("NEXT_LEVEL requested target=%d"), PendingCompletedLevel + 1);
    FDemoGameplayCommandRequest Request; // 关间出发意图只带当前终端，服务器验证房主身份和阶段。
    Request.Command=EDemoGameplayCommand::StartLevel; Request.Terminal=GetController()->GetMenuSnapshot().Terminal;
    Request.TerminalGeneration=GetController()->GetMenuSnapshot().TerminalGeneration;
    Request.ReadyRevision=State->TeamReady.Revision; Request.ExpectedLevel=State->LevelNumber;
    GetController()->SubmitGameplayCommand(Request);
}
bool UDemoMenuFlowComponent::IsNextLevelConfirmationOpen() const
{ DEMO_LOG_TICK(); return bNextLevelConfirmationOpen; }
void UDemoMenuFlowComponent::SelectUpgrade(int32 Choice)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController() || HasBlockingOverlay() || !bMenuOpen || TerminalPage!=EDemoTerminalPage::Stats || Choice<0 || Choice>2)
    { UE_LOG(LogFPSDemo,Log,TEXT("UPGRADE intent rejected page/index")); return; }
    FDemoGameplayCommandRequest Request; // 本人免费奖励或商品索引，服务器决定权限、价格和保存结果。
    Request.Command=bRewardMenu?EDemoGameplayCommand::ChooseReward:EDemoGameplayCommand::UpgradeStat;
    Request.Choice=Choice; Request.Terminal=MenuTerminal.Get(); Request.TerminalGeneration=MenuTerminalGeneration;
    Request.BuildRevision=GetController()->GetMenuSnapshot().Build.Revision; Request.ExpectedLevel=GetWorld()->GetGameState<ADemoGameState>()->LevelNumber;
    MenuMessage=TEXT("等待服务器确认"); GetController()->SubmitGameplayCommand(Request);
}
void UDemoMenuFlowComponent::ShowEndScreen()
{
	DEMO_LOG_CALL();
    ResetGamblePresentation(); // 死亡/结算关闭抽取展示，不让旧结果覆盖新页面提示。
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    MenuTerminal.Reset(); MenuTerminalGeneration = 0; NextTerminalGeneration = 0;
	// 失败可在弹窗期间发生；丢弃出发意图，不能让旧按钮在结算后恢复战斗。
	TerminalPage = EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; PendingGamblePrice=INDEX_NONE; // 页面变化撤销旧赌博确认。 // 丢弃当前武器详情，终局不能沿用旧终端装备请求。
	InspectedWeapon = INDEX_NONE;
	bNextLevelConfirmationOpen = false;
	PendingNextLevelTerminal.Reset();
	PendingCompletedLevel = INDEX_NONE;
	bMenuOpen = false;
	bRewardMenu = false;
	// 终局保留 Pawn 供界面读取属性，但清除持续射击。
	if (ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetController()->GetPawn())) DemoPawn->StopFiring();
	SetMenuInput(true);
}
bool UDemoMenuFlowComponent::IsUpgradeMenuOpen() const
{ DEMO_LOG_TICK(); return bMenuOpen; }
bool UDemoMenuFlowComponent::IsRewardMenu() const
{ DEMO_LOG_TICK(); return bRewardMenu; }
bool UDemoMenuFlowComponent::IsWeaponMenuOpen() const
{ DEMO_LOG_TICK(); return bMenuOpen && !bRewardMenu && TerminalPage==EDemoTerminalPage::Weapons; }
int32 UDemoMenuFlowComponent::GetInspectedWeapon() const
{ DEMO_LOG_TICK(); return IsWeaponMenuOpen() ? InspectedWeapon : INDEX_NONE; }
void UDemoMenuFlowComponent::SetTerminalWeaponPage(bool bWeapons)
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (!bMenuOpen || bRewardMenu) { UE_LOG(LogFPSDemo, Log, TEXT("Terminal tab rejected: shop not open")); return; }
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Terminal tab rejected: blocking overlay")); return; } // 暂停/设置覆盖终端时不能派发下层页签。
    if (bGambleWaiting || IsGambleConfirmOpen()) { UE_LOG(LogFPSDemo, Log, TEXT("Terminal tab rejected: outstanding gamble confirmation or reply")); return; }
	const FString Reason = GetController()->GetMenuSnapshot().TerminalBlockReason; // 服务端交互只读投影，页签切换不授予权限。
	if (!Reason.IsEmpty() || InspectedWeapon != INDEX_NONE) { MenuMessage = Reason; UE_LOG(LogFPSDemo, Log, TEXT("Terminal tab rejected: %s/tip"), *Reason); return; }
    if (!GambleReel.Cards.IsEmpty())
    {
        // 只有已提交的回执会生成卡片。返回首页可立即收起播放，其他页签仍不能穿透结果页。
        if (bWeapons || !IsGambleMenuOpen()) { UE_LOG(LogFPSDemo, Log, TEXT("Terminal tab rejected: active gamble result")); return; }
        UE_LOG(LogFPSDemo, Log, TEXT("GAMBLE_REEL return to terminal home rolling=%d result=%s"), GambleReel.bRolling, *GambleReel.ResultId.ToString());
        GambleReel.Reset();
    }
	TerminalPage = bWeapons?EDemoTerminalPage::Weapons:EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; PendingGamblePrice=INDEX_NONE; // 页面变化撤销旧赌博确认。 // 切页消费旧购买意图。
	MenuMessage.Empty();
	// 只在打开/重新进入武器页时读取软路径，异步结果由HUD保活，不在每帧UI绘制时加载。
	if (bWeapons)
	{
		const ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetController()->GetPawn()); // 本次同步借用当前玩家目录。
		if (ADemoHUD* HUD = GetController()->GetHUD<ADemoHUD>()) HUD->PrepareWeaponIcons(DemoPawn ? DemoPawn->GetWeaponComponent() : nullptr); // HUD弱委托负责后续加载生命周期。
	}
	UE_LOG(LogFPSDemo, Log, TEXT("TERMINAL_PAGE weapons=%d"), bWeapons);
}
void UDemoMenuFlowComponent::InspectWeapon(int32 CatalogIndex)
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	if (HasBlockingOverlay() || !IsWeaponMenuOpen() || InspectedWeapon != INDEX_NONE || DemoWeaponCatalog::IdAt(CatalogIndex).IsNone()) // 顶层模态阻止旧帧卡片意图穿透。
	{ UE_LOG(LogFPSDemo, Log, TEXT("Weapon tip rejected: page/modal/index")); return; }
	InspectedWeapon = CatalogIndex;
	MenuMessage.Empty();
}
void UDemoMenuFlowComponent::CloseWeaponTip()
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Weapon tip close rejected: blocking overlay")); return; } // 保留下层Tips，关闭顶层窗口后恢复。
	InspectedWeapon = INDEX_NONE;
	MenuMessage.Empty();
}
void UDemoMenuFlowComponent::EquipInspectedWeapon()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController() || HasBlockingOverlay() || !IsWeaponMenuOpen() || InspectedWeapon<1 || InspectedWeapon>3)
    { UE_LOG(LogFPSDemo,Log,TEXT("WEAPON equip intent rejected page/index")); return; }
    EquipCatalogWeapon(InspectedWeapon); // 仅兼容既有服务/动画夹具的显式查看索引，不再对应可见详情页。
}
void UDemoMenuFlowComponent::EquipCatalogWeapon(int32 CatalogIndex)
{
    DEMO_LOG_CALL();
    if(bStopped||!GetController()->IsLocalController()||HasBlockingOverlay()||!IsWeaponMenuOpen()||CatalogIndex<1||CatalogIndex>3)
    { UE_LOG(LogFPSDemo,Log,TEXT("WEAPON catalogue equip rejected shutdown/owner/overlay/page/index index=%d"),CatalogIndex); return; }
    FDemoGameplayCommandRequest Request; // 请求只冻结目录索引与当前真实终端代次；权限/金币/保存/临时装备仍由原服务处理。
    Request.Command=EDemoGameplayCommand::EquipWeapon; Request.Choice=CatalogIndex;
    Request.Terminal=MenuTerminal.Get(); Request.TerminalGeneration=MenuTerminalGeneration;
    MenuMessage=TEXT("等待服务器确认"); GetController()->SubmitGameplayCommand(Request);
}
void UDemoMenuFlowComponent::RetryProfileSave()
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	if (HasBlockingOverlay() || !IsWeaponMenuOpen() || InspectedWeapon != INDEX_NONE) { UE_LOG(LogFPSDemo, Log, TEXT("Save retry rejected: page/modal")); return; } // 只允许当前最上层终端发起保存。
	if (UDemoPlayerProfile* Profile = GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Profile->SaveProfile(); // 先写本地，再请求异步同步；不等待网络阻塞操作。
	if (UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->Retry(); // GI持有同步器，跨地图保持队列。
}
void UDemoMenuFlowComponent::CloudAction(int32 Choice)
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前世界阶段，旧帧按钮不能覆盖活动战役。
	if (HasBlockingOverlay() || !State || State->Phase != EDemoPhase::Lobby || Choice < 0 || Choice > 2) { UE_LOG(LogFPSDemo, Warning, TEXT("Cloud action rejected: modal/phase/choice")); return; }
	if (UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>()) // 当前GI拥有，不缓存到控制器。
	{ if (Choice == 0) Cloud->Retry(); else Cloud->ResolveConflict(Choice == 1); }
}
const FString& UDemoMenuFlowComponent::GetMenuMessage() const
{ DEMO_LOG_TICK(); return MenuMessage; }
void UDemoMenuFlowComponent::RestartPressed()
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
	if (HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Restart ignored beneath overlay")); return; } // 创建/暂停/设置必须用当前页的明确按钮操作。
	// Enter 必须有正在显示的确认框才允许确认；普通备战阶段按 Enter 仍不会直接跳关。
	if (bNextLevelConfirmationOpen) { ConfirmNextLevel(); return; }
	// Enter在大厅打开存档页，死亡清空成长回安全区，胜利清空成长/银币并保留金币回安全区；系统错误回大厅，其他阶段GM拒绝。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 借用当前状态。
	if (State && State->Phase == EDemoPhase::Lobby) { StartGamePressed(); return; }
	FDemoGameplayCommandRequest Request; // 全队重启仅服务器房主可执行。
    Request.Command=EDemoGameplayCommand::RestartRun; GetController()->SubmitGameplayCommand(Request);
}
