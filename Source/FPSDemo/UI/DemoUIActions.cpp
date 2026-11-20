// 职责：本地Canvas意图枚举与解码。使用：固定热区名映射动作；不得把任意字符串视作业务授权。
#include "UI/DemoUIActions.h"
#include "Debug/DemoLog.h"
EDemoUIAction DemoUIActions::Decode(FName HitBox)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    static const TMap<FName,EDemoUIAction> Actions={ // 进程只读映射，不绑定UObject、委托或跨World对象。
        {TEXT("TerminalCore"),EDemoUIAction::TerminalCore},
        {TEXT("Core0"),EDemoUIAction::Core0}, {TEXT("Core1"),EDemoUIAction::Core1}, {TEXT("Core2"),EDemoUIAction::Core2},
        {TEXT("Core3"),EDemoUIAction::Core3}, {TEXT("Core4"),EDemoUIAction::Core4}, {TEXT("Core5"),EDemoUIAction::Core5}, {TEXT("Core6"),EDemoUIAction::Core6}, {TEXT("Core7"),EDemoUIAction::Core7}, {TEXT("Core8"),EDemoUIAction::Core8}, // 仅可信安装目录的九个核心动作，任意字符串仍返回Unknown。
        {TEXT("TerminalGamble"),EDemoUIAction::TerminalGamble}, // 本地终端意图。
        {TEXT("GambleAction"),EDemoUIAction::GambleAction}, // 本地终端意图。
        {TEXT("GambleYes"),EDemoUIAction::GambleYes}, // 本地终端意图。
        {TEXT("GambleNo"),EDemoUIAction::GambleNo}, // 本地终端意图。
        {TEXT("GambleSkipAnimation"),EDemoUIAction::GambleSkipAnimation}, // 设备偏好入口。
        {TEXT("GambleContinue"),EDemoUIAction::GambleContinue}, // 仅停稳结果页允许；Flow再次校验最新报价。
        {TEXT("GamblePoolPrevious"),EDemoUIAction::GamblePoolPrevious}, // 只翻本地候选说明。
        {TEXT("GamblePoolNext"),EDemoUIAction::GamblePoolNext}, // 同上，不更改服务端池顺序。
        {TEXT("Gamble0"),EDemoUIAction::Gamble0}, // 本地终端意图。
        {TEXT("Gamble1"),EDemoUIAction::Gamble1}, // 本地终端意图。
        {TEXT("Gamble2"),EDemoUIAction::Gamble2}, // 本地终端意图。
        {TEXT("Gamble3"),EDemoUIAction::Gamble3}, // 本地终端意图。
        {TEXT("Gamble4"),EDemoUIAction::Gamble4}, // 本地终端意图。
        {TEXT("Gamble5"),EDemoUIAction::Gamble5}, // 本地终端意图。
        {TEXT("Gamble6"),EDemoUIAction::Gamble6}, // 本地终端意图。
        {TEXT("Gamble7"),EDemoUIAction::Gamble7}, // 本地终端意图。
        {TEXT("Gamble8"),EDemoUIAction::Gamble8}, // 本地终端意图。
        {TEXT("Gamble9"),EDemoUIAction::Gamble9}, // 本地终端意图。
        {TEXT("Gamble10"),EDemoUIAction::Gamble10}, // 本地终端意图。
        {TEXT("Gamble11"),EDemoUIAction::Gamble11}, // 本地终端意图。
        {TEXT("CloudRetry"),EDemoUIAction::CloudRetry},
        {TEXT("CloudLocal"),EDemoUIAction::CloudLocal},
        {TEXT("CloudRemote"),EDemoUIAction::CloudRemote},
        {TEXT("TerminalAmmo"),EDemoUIAction::TerminalAmmo},
        {TEXT("Ammo0"),EDemoUIAction::Ammo0},
        {TEXT("Ammo1"),EDemoUIAction::Ammo1},
        {TEXT("Ammo2"),EDemoUIAction::Ammo2},
        {TEXT("Ammo3"),EDemoUIAction::Ammo3},
        {TEXT("AmmoAction"),EDemoUIAction::AmmoAction},
        {TEXT("AmmoBuyYes"),EDemoUIAction::AmmoBuyYes},
        {TEXT("AmmoBuyNo"),EDemoUIAction::AmmoBuyNo},
        {TEXT("TerminalStats"),EDemoUIAction::TerminalStats},
        {TEXT("TerminalWeapons"),EDemoUIAction::TerminalWeapons},
        {TEXT("Weapon0"),EDemoUIAction::Weapon0},
        {TEXT("Weapon1"),EDemoUIAction::Weapon1},
        {TEXT("Weapon2"),EDemoUIAction::Weapon2},
        {TEXT("Weapon3"),EDemoUIAction::Weapon3},
        {TEXT("WeaponTipClose"),EDemoUIAction::WeaponTipClose},
        {TEXT("WeaponEquip"),EDemoUIAction::WeaponEquip},
        {TEXT("ProfileSave"),EDemoUIAction::ProfileSave},
        {TEXT("Upgrade0"),EDemoUIAction::Upgrade0},
        {TEXT("Upgrade1"),EDemoUIAction::Upgrade1},
        {TEXT("Upgrade2"),EDemoUIAction::Upgrade2},
        {TEXT("Close"),EDemoUIAction::Close},
        {TEXT("NextLevelYes"),EDemoUIAction::NextLevelYes},
        {TEXT("NextLevelNo"),EDemoUIAction::NextLevelNo},
        {TEXT("TeamReady"),EDemoUIAction::TeamReady},
        {TEXT("TeamNotReady"),EDemoUIAction::TeamNotReady},
        {TEXT("Restart"),EDemoUIAction::Restart},
        {TEXT("StartGame"),EDemoUIAction::StartGame},
        // LAN/EOS浏览与动态行输入均使用白名单，不接受自由字符串执行连接。
        {TEXT("Multiplayer"),EDemoUIAction::Multiplayer},
        {TEXT("RoomRefresh"),EDemoUIAction::RoomRefresh},
        {TEXT("RoomCreate"),EDemoUIAction::RoomCreate},
        {TEXT("RoomPrevious"),EDemoUIAction::RoomPrevious},
        {TEXT("RoomNext"),EDemoUIAction::RoomNext},
        {TEXT("RoomLAN"),EDemoUIAction::RoomLAN}, {TEXT("RoomPublic"),EDemoUIAction::RoomPublic},
        {TEXT("RoomAccount"),EDemoUIAction::RoomAccount}, {TEXT("RoomQuick"),EDemoUIAction::RoomQuick},
        {TEXT("Room0"),EDemoUIAction::Room0}, {TEXT("Room1"),EDemoUIAction::Room1},
        {TEXT("Room2"),EDemoUIAction::Room2}, {TEXT("Room3"),EDemoUIAction::Room3}, {TEXT("Room4"),EDemoUIAction::Room4},
        {TEXT("Easy"),EDemoUIAction::Easy},
        {TEXT("Normal"),EDemoUIAction::Normal},
        {TEXT("Hard"),EDemoUIAction::Hard},
        {TEXT("Hell"),EDemoUIAction::Hell},
        {TEXT("Endless"),EDemoUIAction::Endless},
        {TEXT("LobbySettings"),EDemoUIAction::LobbySettings},
        {TEXT("QuitGame"),EDemoUIAction::QuitGame},
        {TEXT("StatusPrevious"),EDemoUIAction::StatusPrevious}, // 仅状态面板翻页，服务器无对应业务命令。
        {TEXT("StatusNext"),EDemoUIAction::StatusNext},
        {TEXT("OverlayBack"),EDemoUIAction::OverlayBack},
        {TEXT("Save0"),EDemoUIAction::Save0},
        {TEXT("Save1"),EDemoUIAction::Save1},
        {TEXT("Save2"),EDemoUIAction::Save2},
        {TEXT("CreateYes"),EDemoUIAction::CreateYes},
        {TEXT("CreateNo"),EDemoUIAction::CreateNo},
        {TEXT("PauseHub"),EDemoUIAction::PauseHub},
        {TEXT("HubYes"),EDemoUIAction::HubYes},
        {TEXT("HubNo"),EDemoUIAction::HubNo},
        {TEXT("PauseSettings"),EDemoUIAction::PauseSettings},
        {TEXT("PauseLobby"),EDemoUIAction::PauseLobby},
        {TEXT("PauseQuit"),EDemoUIAction::PauseQuit},
        {TEXT("QuitRetry"),EDemoUIAction::QuitRetry},
        {TEXT("QuitLocal"),EDemoUIAction::QuitLocal},
        {TEXT("QuitCancel"),EDemoUIAction::QuitCancel},
        {TEXT("SettingsApply"),EDemoUIAction::SettingsApply},
        {TEXT("DisplayYes"),EDemoUIAction::DisplayYes},
        {TEXT("DisplayNo"),EDemoUIAction::DisplayNo},
        {TEXT("Setting0Minus"),EDemoUIAction::Setting0Minus},
        {TEXT("Setting0Plus"),EDemoUIAction::Setting0Plus},
        {TEXT("Setting1Minus"),EDemoUIAction::Setting1Minus},
        {TEXT("Setting1Plus"),EDemoUIAction::Setting1Plus},
        {TEXT("Setting2Minus"),EDemoUIAction::Setting2Minus},
        {TEXT("Setting2Plus"),EDemoUIAction::Setting2Plus},
        {TEXT("Setting3Minus"),EDemoUIAction::Setting3Minus},
        {TEXT("Setting3Plus"),EDemoUIAction::Setting3Plus},
        {TEXT("Setting4Minus"),EDemoUIAction::Setting4Minus}, // 新设置使用白名单，禁止通配控制台命令。
        {TEXT("Setting4Plus"),EDemoUIAction::Setting4Plus},
    };
    const EDemoUIAction* Found=Actions.Find(HitBox); // 只在此同步查询内借用静态Map元素。
    return Found?*Found:EDemoUIAction::Unknown;
}
bool DemoUIActions::IsOverlay(EDemoUIAction Action)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    switch(Action)
    {
        case EDemoUIAction::StatusPrevious: case EDemoUIAction::StatusNext: // 状态页属于覆盖层。
        case EDemoUIAction::OverlayBack:
        case EDemoUIAction::RoomRefresh: case EDemoUIAction::RoomCreate:
        case EDemoUIAction::RoomPrevious: case EDemoUIAction::RoomNext:
        case EDemoUIAction::RoomLAN: case EDemoUIAction::RoomPublic: case EDemoUIAction::RoomAccount: case EDemoUIAction::RoomQuick:
        case EDemoUIAction::Room0: case EDemoUIAction::Room1: case EDemoUIAction::Room2: case EDemoUIAction::Room3: case EDemoUIAction::Room4:
        case EDemoUIAction::Save0:
        case EDemoUIAction::Save1:
        case EDemoUIAction::Save2:
        case EDemoUIAction::CreateYes:
        case EDemoUIAction::CreateNo:
        case EDemoUIAction::PauseHub:
        case EDemoUIAction::HubYes:
        case EDemoUIAction::HubNo:
        case EDemoUIAction::PauseSettings:
        case EDemoUIAction::PauseLobby:
        case EDemoUIAction::PauseQuit:
        case EDemoUIAction::QuitRetry:
        case EDemoUIAction::QuitLocal:
        case EDemoUIAction::QuitCancel:
        case EDemoUIAction::SettingsApply:
        case EDemoUIAction::DisplayYes:
        case EDemoUIAction::DisplayNo:
        case EDemoUIAction::Setting0Minus:
        case EDemoUIAction::Setting0Plus:
        case EDemoUIAction::Setting1Minus:
        case EDemoUIAction::Setting1Plus:
        case EDemoUIAction::Setting2Minus:
        case EDemoUIAction::Setting2Plus:
        case EDemoUIAction::Setting3Minus:
        case EDemoUIAction::Setting3Plus:
        case EDemoUIAction::Setting4Minus: // 帧同步属于设置覆盖层，终端底层不可触发。
        case EDemoUIAction::Setting4Plus:
            return true;
        default:return false;
    }
}
