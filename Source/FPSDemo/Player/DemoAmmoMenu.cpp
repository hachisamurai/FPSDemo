#include "Player/DemoPlayerController.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Characters/DemoCharacter.h"
#include "Game/FPSDemoGameMode.h"
#include "Debug/DemoLog.h"

bool ADemoPlayerController::IsAmmoMenuOpen() const { DEMO_LOG_TICK(); return bMenuOpen&&!bRewardMenu&&TerminalPage==EDemoTerminalPage::Ammo; }
int32 ADemoPlayerController::GetInspectedAmmo() const { DEMO_LOG_TICK(); return InspectedAmmo; }
int32 ADemoPlayerController::GetPendingAmmoPrice() const { DEMO_LOG_TICK(); return PendingAmmoPrice; }
void ADemoPlayerController::OpenAmmoMenu()
{
    DEMO_LOG_CALL();
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // UI权威状态，不接受关间终端入口。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 单人权限入口。
    if(HasBlockingOverlay()||!State||State->Phase!=EDemoPhase::Hub||!Mode||InspectedWeapon!=INDEX_NONE||!Mode->GetTerminalBlockReason(Cast<ADemoCharacter>(GetPawn())).IsEmpty())
    {UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_MENU_REJECT phase/terminal/modal"));return;}
    TerminalPage=EDemoTerminalPage::Ammo; PendingAmmoPrice=INDEX_NONE;
    InspectedAmmo=GetPawn()->FindComponentByClass<UDemoAmmoComponent>()->GetSelected(); MenuMessage.Empty();
}
void ADemoPlayerController::InspectAmmo(int32 Index)
{
    DEMO_LOG_CALL();
    if(!IsAmmoMenuOpen()||HasBlockingOverlay()||Index<0||Index>3){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_INSPECT_REJECT"));return;}
    InspectedAmmo=Index;PendingAmmoPrice=INDEX_NONE;MenuMessage.Empty();
}
void ADemoPlayerController::AmmoAction()
{
    DEMO_LOG_CALL();
    if(!IsAmmoMenuOpen()||HasBlockingOverlay()||PendingAmmoPrice!=INDEX_NONE||!GetPawn()){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_ACTION_REJECT modal"));return;}
    UDemoAmmoComponent* Ammo=GetPawn()->FindComponentByClass<UDemoAmmoComponent>(); // Pawn拥有，点击时重新验证。
    MenuMessage=Ammo->GetBlockReason();
    // 普通弹不依赖特殊效果参数；损坏配置仍允许显式撤销特殊类型。
    if(!MenuMessage.IsEmpty()||(InspectedAmmo!=0&&!Ammo->GetCatalog()->Validate())){if(MenuMessage.IsEmpty())MenuMessage=TEXT("弹药配置无效，请检查目录");UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_ACTION_REJECT config/permission"));return;}
    if(Ammo->IsUnlocked(InspectedAmmo)){Ammo->Equip(InspectedAmmo,MenuMessage);return;}
    const int32 Cost=Ammo->GetCatalog()->Entries[InspectedAmmo].UnlockGoldCost; // 购买确认快照，与下一次提交报价对比。
    if(GetWorld()->GetGameState<ADemoGameState>()->Coins<Cost){MenuMessage=TEXT("金币不足");UE_LOG(LogFPSDemo,Log,TEXT("AMMO_ACTION_REJECT gold"));return;}
    PendingAmmoPrice=Cost;
}
void ADemoPlayerController::ConfirmAmmoPurchase(bool Confirm)
{
    DEMO_LOG_CALL();
    if(!IsAmmoMenuOpen()||HasBlockingOverlay()||PendingAmmoPrice==INDEX_NONE||!GetPawn()){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_CONFIRM_REJECT stale"));return;}
    const int32 Quote=PendingAmmoPrice; // 先消费意图，抵御重复点击。
    PendingAmmoPrice=INDEX_NONE;
    if(!Confirm){MenuMessage=TEXT("已取消，未扣除金币");return;}
    GetPawn()->FindComponentByClass<UDemoAmmoComponent>()->Purchase(InspectedAmmo,Quote,MenuMessage);
}
