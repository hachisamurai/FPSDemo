// 职责：Canvas动作白名单分派。使用：Decode后先拒绝模态穿透；WeaponN只对应明确装配卡，直接提交原装备流程，不再打开详情。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIActions.h"
#include "Debug/DemoLog.h"
void UDemoMenuFlowComponent::DispatchUIAction(EDemoUIAction Action)
{
    DEMO_LOG_CALL();
    if(bStopped||!GetController()->IsLocalController()){UE_LOG(LogFPSDemo,Log,TEXT("UI_ACTION rejected owner/lifecycle"));return;}
    if(HasBlockingOverlay()!=DemoUIActions::IsOverlay(Action)){UE_LOG(LogFPSDemo,Log,TEXT("UI_ACTION rejected modal context"));return;}
    if(MenuPage==EDemoMenuPage::FirstRunGuide&&Action!=EDemoUIAction::OverlayBack)
    { UE_LOG(LogFPSDemo,Log,TEXT("FIRST_RUN_GUIDE rejected stale underlying action")); return; } // 只使用已有OverlayBack关闭按钮，下层大厅/设置热区不能穿透。
    if(MenuPage==EDemoMenuPage::PlayerStatus&&Action!=EDemoUIAction::OverlayBack&&Action!=EDemoUIAction::StatusPrevious&&Action!=EDemoUIAction::StatusNext)
    { UE_LOG(LogFPSDemo,Log,TEXT("STATUS_UI rejected stale overlay action")); return; } // 旧暂停/退出按钮不能穿透到只读页。
    if(IsGambleMenuOpen()&&!HasBlockingOverlay()&&(bGambleWaiting||!GambleReel.Cards.IsEmpty()))
    {
        // 停稳后仅开放明确的续抽/返回；滚动和等待中旧热区仍被拦截，Close始终可离开。
        if(Action==EDemoUIAction::GambleNo&&!bGambleWaiting&&!GambleReel.bRolling){DismissGambleReveal();return;}
        if(Action==EDemoUIAction::GambleSkipAnimation){ToggleGambleAnimation();return;} // 滚动中也允许立即停靠。
        if(Action==EDemoUIAction::GambleContinue){ContinueGamble();return;}
        if(Action!=EDemoUIAction::Close){UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REEL action rejected during reveal"));return;}
    }
    if(IsGambleMenuOpen()&&IsGambleConfirmOpen()&&Action!=EDemoUIAction::GambleYes&&Action!=EDemoUIAction::GambleNo) { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_UI rejected underlying action")); return; } // 旧热区/键盘不能穿透确认。
    switch(Action)
    {
        case EDemoUIAction::TerminalCore: OpenCoreMenu(); break;
        case EDemoUIAction::Core0: SelectCore(0); break;
        case EDemoUIAction::Core1: SelectCore(1); break;
        case EDemoUIAction::Core2: SelectCore(2); break;
        case EDemoUIAction::Core3: SelectCore(3); break;
        case EDemoUIAction::Core4: SelectCore(4); break;
        case EDemoUIAction::Core5: SelectCore(5); break;
        case EDemoUIAction::Core6: SelectCore(6); break;
        case EDemoUIAction::Core7: SelectCore(7); break;
        case EDemoUIAction::Core8: SelectCore(8); break;
        case EDemoUIAction::StatusPrevious: ChangeStatusPage(-1); break; // Page守卫防过期点击。
        case EDemoUIAction::StatusNext: ChangeStatusPage(1); break;
        case EDemoUIAction::TerminalGamble: OpenGambleMenu(); break;
        case EDemoUIAction::GambleAction: RequestGamble(); break;
        case EDemoUIAction::GamblePoolPrevious: ChangeGambleRewardPage(-1); break; // 页面守卫拒绝底层过期热区。
        case EDemoUIAction::GambleSkipAnimation: ToggleGambleAnimation(); break; // 不提交交易。
        case EDemoUIAction::GamblePoolNext: ChangeGambleRewardPage(1); break; // 无RPC或重新抽样。
        case EDemoUIAction::GambleYes: ConfirmGamble(true); break;
        case EDemoUIAction::GambleNo: ConfirmGamble(false); break;
        case EDemoUIAction::GambleContinue: ContinueGamble(); break; // 旧结果页热区需经Flow状态校验，不能在列表发起下注。
        case EDemoUIAction::Gamble0: InspectGamble(0); break;
        case EDemoUIAction::Gamble1: InspectGamble(1); break;
        case EDemoUIAction::Gamble2: InspectGamble(2); break;
        case EDemoUIAction::Gamble3: InspectGamble(3); break;
        case EDemoUIAction::Gamble4: InspectGamble(4); break;
        case EDemoUIAction::Gamble5: InspectGamble(5); break;
        case EDemoUIAction::Gamble6: InspectGamble(6); break;
        case EDemoUIAction::Gamble7: InspectGamble(7); break;
        case EDemoUIAction::Gamble8: InspectGamble(8); break;
        case EDemoUIAction::Gamble9: InspectGamble(9); break;
        case EDemoUIAction::Gamble10: InspectGamble(10); break;
        case EDemoUIAction::Gamble11: InspectGamble(11); break;
        case EDemoUIAction::CloudRetry: CloudAction(0); break;
        case EDemoUIAction::CloudLocal: CloudAction(1); break;
        case EDemoUIAction::CloudRemote: CloudAction(2); break;
        case EDemoUIAction::TerminalAmmo: OpenAmmoMenu(); break;
        case EDemoUIAction::Ammo0: InspectAmmo(0); break;
        case EDemoUIAction::Ammo1: InspectAmmo(1); break;
        case EDemoUIAction::Ammo2: InspectAmmo(2); break;
        case EDemoUIAction::Ammo3: InspectAmmo(3); break;
        case EDemoUIAction::AmmoAction: AmmoAction(); break;
        case EDemoUIAction::AmmoBuyYes: ConfirmAmmoPurchase(true); break;
        case EDemoUIAction::AmmoBuyNo: ConfirmAmmoPurchase(false); break;
        case EDemoUIAction::TerminalStats: SetTerminalWeaponPage(false); break;
        case EDemoUIAction::TerminalWeapons: SetTerminalWeaponPage(true); break;
        case EDemoUIAction::Weapon0: EquipCatalogWeapon(0); break;
        case EDemoUIAction::Weapon1: EquipCatalogWeapon(1); break;
        case EDemoUIAction::Weapon2: EquipCatalogWeapon(2); break;
        case EDemoUIAction::Weapon3: EquipCatalogWeapon(3); break;
        case EDemoUIAction::WeaponTipClose: CloseWeaponTip(); break;
        case EDemoUIAction::WeaponEquip: EquipInspectedWeapon(); break;
        case EDemoUIAction::ProfileSave: RetryProfileSave(); break;
        case EDemoUIAction::Upgrade0: SelectUpgrade(0); break;
        case EDemoUIAction::Upgrade1: SelectUpgrade(1); break;
        case EDemoUIAction::Upgrade2: SelectUpgrade(2); break;
        case EDemoUIAction::Close: CloseUpgradeMenu(); break;
        case EDemoUIAction::NextLevelYes: ConfirmNextLevel(); break;
        case EDemoUIAction::NextLevelNo: CancelNextLevelConfirmation(); break;
        case EDemoUIAction::TeamReady: SetTeamReady(true); break;
        case EDemoUIAction::TeamNotReady: SetTeamReady(false); break;
        case EDemoUIAction::Restart: RestartPressed(); break;
        case EDemoUIAction::StartGame: StartGamePressed(); break;
        // 房间业务不经过GameMode：这是本机连接意图，成功后才由服务器校验角色权限。
        case EDemoUIAction::Multiplayer: MultiplayerPressed(); break;
        case EDemoUIAction::RoomRefresh: RefreshRooms(); break;
        case EDemoUIAction::RoomCreate: CreateRoomPressed(); break;
        case EDemoUIAction::RoomPrevious: ChangeRoomPage(-1); break;
        case EDemoUIAction::RoomNext: ChangeRoomPage(1); break;
        case EDemoUIAction::RoomLAN: SelectRoomNetwork(false); break;
        case EDemoUIAction::RoomPublic: SelectRoomNetwork(true); break;
        case EDemoUIAction::RoomAccount: RoomAccountPressed(); break;
        case EDemoUIAction::RoomQuick: QuickJoinRoom(); break;
        case EDemoUIAction::Easy: DifficultyPressed(EDemoDifficulty::Easy); break;
        case EDemoUIAction::Normal: DifficultyPressed(EDemoDifficulty::Normal); break;
        case EDemoUIAction::Hard: DifficultyPressed(EDemoDifficulty::Hard); break;
        case EDemoUIAction::Hell: DifficultyPressed(EDemoDifficulty::Hell); break;
        case EDemoUIAction::Endless: EndlessPressed(); break;
        case EDemoUIAction::LobbySettings: SettingsPressed(); break;
        case EDemoUIAction::QuitGame: QuitPressed(); break;
        case EDemoUIAction::OverlayBack: CloseOverlay(); break;
        case EDemoUIAction::Save0: SelectSaveSlot(0); break;
        case EDemoUIAction::Save1: SelectSaveSlot(1); break;
        case EDemoUIAction::Save2: SelectSaveSlot(2); break;
        case EDemoUIAction::CreateYes: ConfirmCreateSave(true); break;
        case EDemoUIAction::CreateNo: ConfirmCreateSave(false); break;
        case EDemoUIAction::PauseHub: ReturnHubPressed(); break;
        case EDemoUIAction::HubYes: ConfirmReturnHub(true); break;
        case EDemoUIAction::HubNo: ConfirmReturnHub(false); break;
        case EDemoUIAction::PauseSettings: SettingsPressed(); break;
        case EDemoUIAction::PauseLobby: ReturnLobbyPressed(); break;
        case EDemoUIAction::PauseQuit: QuitPressed(); break;
        case EDemoUIAction::QuitRetry: RetryQuitSave(); break;
        case EDemoUIAction::QuitLocal: QuitWithLocalSave(); break;
        case EDemoUIAction::QuitCancel: CancelQuitSave(); break;
        case EDemoUIAction::SettingsApply: ApplyUserSettings(); break;
        case EDemoUIAction::DisplayYes: ConfirmDisplaySettings(true); break;
        case EDemoUIAction::DisplayNo: ConfirmDisplaySettings(false); break;
        case EDemoUIAction::Setting0Minus: CycleSetting(0,-1); break;
        case EDemoUIAction::Setting0Plus: CycleSetting(0,1); break;
        case EDemoUIAction::Setting1Minus: CycleSetting(1,-1); break;
        case EDemoUIAction::Setting1Plus: CycleSetting(1,1); break;
        case EDemoUIAction::Setting2Minus: CycleSetting(2,-1); break;
        case EDemoUIAction::Setting2Plus: CycleSetting(2,1); break;
        case EDemoUIAction::Setting3Minus: CycleSetting(3,-1); break;
        case EDemoUIAction::Setting3Plus: CycleSetting(3,1); break;
        case EDemoUIAction::Setting4Minus: CycleSetting(4,-1); break; // 统一设置草稿入口，不即时写盘。
        case EDemoUIAction::Setting4Plus: CycleSetting(4,1); break;
        default:UE_LOG(LogFPSDemo,Log,TEXT("UI_ACTION unknown rejected"));break;
    }
}
