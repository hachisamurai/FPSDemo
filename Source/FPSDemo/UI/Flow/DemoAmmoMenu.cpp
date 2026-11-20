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
    if(bGambleWaiting||!GambleReel.Cards.IsEmpty()) { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REEL ammo tab rejected during reveal")); return; } // 直接入口与白名单热区共享动画输入限制。
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // UI权威状态，不接受关间终端入口。
    if(HasBlockingOverlay()||!State||State->Phase!=EDemoPhase::Hub||InspectedWeapon!=INDEX_NONE||!GetController()->GetMenuSnapshot().TerminalBlockReason.IsEmpty())
    {UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_MENU_REJECT phase/terminal/modal"));return;}

    TerminalPage=EDemoTerminalPage::Ammo; PendingAmmoPrice=INDEX_NONE;
    InspectedAmmo=0; MenuMessage.Empty(); // 查看项与装配状态分开，默认普通弹，不读取客户端交易组件。
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
    const FDemoPlayerMenuSnapshot& View=GetController()->GetMenuSnapshot(); // 价格、解锁、拒绝原因均来自服务器投影。
    if (!View.AmmoOffers.IsValidIndex(InspectedAmmo)) { MenuMessage=TEXT("等待服务器弹药目录"); return; }
    const FDemoShopOfferSnapshot& Offer=View.AmmoOffers[InspectedAmmo]; // 借用当前已验证行，不跨回执保留引用。
    MenuMessage=Offer.BlockReason;
    if (!MenuMessage.IsEmpty()) return;
    if (Offer.bUnlocked)
    {
        FDemoGameplayCommandRequest Request; // 免费装配仍要通过本人服务器会话校验。
        Request.Command=EDemoGameplayCommand::EquipAmmo; Request.Choice=InspectedAmmo;
        Request.Terminal=MenuTerminal.Get(); Request.TerminalGeneration=MenuTerminalGeneration;
        GetController()->SubmitGameplayCommand(Request); return;
    }
    PendingAmmoPrice=Offer.Cost; // 只展示服务器报价；确认时服务器再次核对，不能客户端直接扣币。

}
void UDemoMenuFlowComponent::ConfirmAmmoPurchase(bool Confirm)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if(!IsAmmoMenuOpen()||HasBlockingOverlay()||PendingAmmoPrice==INDEX_NONE||!GetController()->GetPawn()){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_CONFIRM_REJECT stale"));return;}
    const int32 Quote=PendingAmmoPrice; // 先消费意图，抵御重复点击。
    PendingAmmoPrice=INDEX_NONE;
    if(!Confirm){MenuMessage=TEXT("已取消，未扣除金币");return;}
    FDemoGameplayCommandRequest Request; // 明确确认的报价，服务器原子提交并返回保存状态。
    Request.Command=EDemoGameplayCommand::PurchaseAmmo; Request.Choice=InspectedAmmo; Request.ExpectedPrice=Quote;
    Request.Terminal=MenuTerminal.Get(); Request.TerminalGeneration=MenuTerminalGeneration;
    GetController()->SubmitGameplayCommand(Request);
}
