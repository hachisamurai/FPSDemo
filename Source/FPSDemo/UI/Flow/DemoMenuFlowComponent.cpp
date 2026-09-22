#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/InputComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
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
UDemoMenuFlowComponent::UDemoMenuFlowComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
ADemoPlayerController* UDemoMenuFlowComponent::GetController() const { DEMO_LOG_TICK(); return CastChecked<ADemoPlayerController>(GetOwner()); }
void UDemoMenuFlowComponent::SetMenuInput(bool bEnabled) { DEMO_LOG_CALL(); if (!bStopped && GetController()->IsLocalController()) GetController()->SetMenuInput(bEnabled); }
ADemoInteractable* UDemoMenuFlowComponent::GetMenuTerminal() const { DEMO_LOG_TICK(); return MenuTerminal.Get(); }
uint64 UDemoMenuFlowComponent::GetMenuTerminalGeneration() const { DEMO_LOG_TICK(); return MenuTerminalGeneration; }
void UDemoMenuFlowComponent::UpdateRealtime(float DeltaTime)
{
    DEMO_LOG_TICK();
    if (bStopped || !GetController()->IsLocalController()) return;
    UpdateQuitSave(); // 无退出页面立即返回，不在HTTP回调中持有PC。
    if (DisplayConfirmDeadline > 0 && FPlatformTime::Seconds() >= DisplayConfirmDeadline) ConfirmDisplaySettings(false);
    if (bMenuOpen && !bRewardMenu && !MenuTerminal.IsValid())
    {
        bMenuOpen = false; MenuTerminalGeneration = 0; PendingAmmoPrice = INDEX_NONE; InspectedWeapon = INDEX_NONE;
        MenuMessage = TEXT("终端交互已结束"); RefreshMenuInput(); // 仅清下层终端，保留暂停/退出等顶层意图。
    }
}
void UDemoMenuFlowComponent::Shutdown()
{
    DEMO_LOG_CALL();
    if (bStopped) return;
    if (MenuPage == EDemoMenuPage::QuitSaving)
        if (UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->CancelExitSync(); // 取消UI等待，不删除耐久发件箱。
    if (DisplayConfirmDeadline > 0) ConfirmDisplaySettings(false); // 旅行/关闭PC时也恢复未确认显示模式。
    bStopped = true; QuitState = EDemoQuitState::Idle; MenuTerminal.Reset(); PendingNextLevelTerminal.Reset();
    MenuTerminalGeneration = NextTerminalGeneration = 0;
}
void UDemoMenuFlowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) { DEMO_LOG_CALL(); Shutdown(); Super::EndPlay(EndPlayReason); }
