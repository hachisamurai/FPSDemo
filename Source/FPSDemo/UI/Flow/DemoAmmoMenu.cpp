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
bool UDemoMenuFlowComponent::IsAmmoMenuOpen() const
{ DEMO_LOG_TICK(); return bMenuOpen&&!bRewardMenu&&TerminalPage==EDemoTerminalPage::Ammo; }
int32 UDemoMenuFlowComponent::GetInspectedAmmo() const
{ DEMO_LOG_TICK(); return InspectedAmmo; }
int32 UDemoMenuFlowComponent::GetPendingAmmoPrice() const
{ DEMO_LOG_TICK(); return PendingAmmoPrice; }
void UDemoMenuFlowComponent::OpenAmmoMenu()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // UI权威状态，不接受关间终端入口。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 单人权限入口。
    if(HasBlockingOverlay()||!State||State->Phase!=EDemoPhase::Hub||!Mode||InspectedWeapon!=INDEX_NONE||!Mode->GetTerminalBlockReason(Cast<ADemoCharacter>(GetController()->GetPawn())).IsEmpty())
    {UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_MENU_REJECT phase/terminal/modal"));return;}
    TerminalPage=EDemoTerminalPage::Ammo; PendingAmmoPrice=INDEX_NONE;
    InspectedAmmo=GetController()->GetPawn()->FindComponentByClass<UDemoAmmoComponent>()->GetSelected(); MenuMessage.Empty();
}
void UDemoMenuFlowComponent::InspectAmmo(int32 Index)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if(!IsAmmoMenuOpen()||HasBlockingOverlay()||Index<0||Index>3){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_INSPECT_REJECT"));return;}
    InspectedAmmo=Index;PendingAmmoPrice=INDEX_NONE;MenuMessage.Empty();
}
void UDemoMenuFlowComponent::AmmoAction()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if(!IsAmmoMenuOpen()||HasBlockingOverlay()||PendingAmmoPrice!=INDEX_NONE||!GetController()->GetPawn()){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_ACTION_REJECT modal"));return;}
    UDemoAmmoComponent* Ammo=GetController()->GetPawn()->FindComponentByClass<UDemoAmmoComponent>(); // Pawn拥有，点击时重新验证。
    MenuMessage=Ammo->GetBlockReason();
    // 普通弹不依赖特殊效果参数；损坏配置仍允许显式撤销特殊类型。
    if(!MenuMessage.IsEmpty()||(InspectedAmmo!=0&&!Ammo->GetCatalog()->Validate())){if(MenuMessage.IsEmpty())MenuMessage=TEXT("弹药配置无效，请检查目录");UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_ACTION_REJECT config/permission"));return;}
    if(Ammo->IsUnlocked(InspectedAmmo)){Ammo->Equip(InspectedAmmo,MenuMessage);return;}
    const int32 Cost=Ammo->GetCatalog()->Entries[InspectedAmmo].UnlockGoldCost; // 购买确认快照，与下一次提交报价对比。
    if(GetWorld()->GetGameState<ADemoGameState>()->Coins<Cost){MenuMessage=TEXT("金币不足");UE_LOG(LogFPSDemo,Log,TEXT("AMMO_ACTION_REJECT gold"));return;}
    PendingAmmoPrice=Cost;
}
void UDemoMenuFlowComponent::ConfirmAmmoPurchase(bool Confirm)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if(!IsAmmoMenuOpen()||HasBlockingOverlay()||PendingAmmoPrice==INDEX_NONE||!GetController()->GetPawn()){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_CONFIRM_REJECT stale"));return;}
    const int32 Quote=PendingAmmoPrice; // 先消费意图，抵御重复点击。
    PendingAmmoPrice=INDEX_NONE;
    if(!Confirm){MenuMessage=TEXT("已取消，未扣除金币");return;}
    GetController()->GetPawn()->FindComponentByClass<UDemoAmmoComponent>()->Purchase(InspectedAmmo,Quote,MenuMessage);
}
