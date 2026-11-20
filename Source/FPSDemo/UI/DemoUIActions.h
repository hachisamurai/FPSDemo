// 职责：本地Canvas意图枚举与解码。使用：固定热区名映射动作；不得把任意字符串视作业务授权。
#pragma once
#include "CoreMinimal.h"

/** 本地Canvas意图白名单；不携带玩家身份/余额，网络业务另经拥有者PC网关。 */
enum class EDemoUIAction : uint8
{
    Unknown, TerminalCore, Core0, Core1, Core2, // 核心终端固定白名单。
    StatusPrevious, StatusNext, // Tab面板本地天赋翻页，只允许该覆盖层。
    GambleSkipAnimation, // 本机勾选状态，不发经济RPC。
    GambleContinue, // 本地结果页按显示费用续抽；沿用当前项目，绝不携带中奖结果。
    GamblePoolPrevious, GamblePoolNext, // 本地天赋奖池详情翻页，无经济请求。
    TerminalGamble, GambleAction, GambleYes, GambleNo, Gamble0, Gamble1, Gamble2, Gamble3, Gamble4, Gamble5, Gamble6, Gamble7, Gamble8, Gamble9, Gamble10, Gamble11, // 赌博页固定白名单，客户端不携带奖励参数。
    CloudRetry, // 基础页面/终端意图。
    CloudLocal, // 基础页面/终端意图。
    CloudRemote, // 基础页面/终端意图。
    TerminalAmmo, // 基础页面/终端意图。
    Ammo0, // 基础页面/终端意图。
    Ammo1, // 基础页面/终端意图。
    Ammo2, // 基础页面/终端意图。
    Ammo3, // 基础页面/终端意图。
    AmmoAction, // 基础页面/终端意图。
    AmmoBuyYes, // 基础页面/终端意图。
    AmmoBuyNo, // 基础页面/终端意图。
    TerminalStats, // 基础页面/终端意图。
    TerminalWeapons, // 基础页面/终端意图。
    Weapon0, // 基础页面/终端意图。
    Weapon1, // 基础页面/终端意图。
    Weapon2, // 基础页面/终端意图。
    Weapon3, // 基础页面/终端意图。
    WeaponTipClose, // 基础页面/终端意图。
    WeaponEquip, // 基础页面/终端意图。
    ProfileSave, // 基础页面/终端意图。
    Upgrade0, // 基础页面/终端意图。
    Upgrade1, // 基础页面/终端意图。
    Upgrade2, // 基础页面/终端意图。
    Close, // 基础页面/终端意图。
    NextLevelYes, // 基础页面/终端意图。
    NextLevelNo, // 基础页面/终端意图。
    TeamReady, TeamNotReady, // 显式准备/取消，避免toggle重放反转。
    Restart, // 基础页面/终端意图。
    StartGame, // 基础页面/终端意图。
    Easy, // 基础页面/终端意图。
    Normal, // 基础页面/终端意图。
    Hard, // 基础页面/终端意图。
    Hell, // 基础页面/终端意图。
    Endless, // 基础页面/终端意图。
    LobbySettings, // 基础页面/终端意图。
    QuitGame, // 基础页面/终端意图。
    Multiplayer, // 大厅打开房间浏览器并搜索。
    RoomRefresh, RoomCreate, RoomPrevious, RoomNext, // 房间页显式请求，服务负责状态校验。
    RoomLAN, RoomPublic, RoomAccount, RoomQuick, // 本机提供者选择、登录/注销和快速加入；不走战斗RPC。
    Room0, Room1, Room2, Room3, Room4, // 本页五行；HUD同时传递实际绘制版本防止过期点击。
    OverlayBack, // 顶层模态页意图。
    Save0, // 顶层模态页意图。
    Save1, // 顶层模态页意图。
    Save2, // 顶层模态页意图。
    CreateYes, // 顶层模态页意图。
    CreateNo, // 顶层模态页意图。
    PauseHub, // 顶层模态页意图。
    HubYes, // 顶层模态页意图。
    HubNo, // 顶层模态页意图。
    PauseSettings, // 顶层模态页意图。
    PauseLobby, // 顶层模态页意图。
    PauseQuit, // 顶层模态页意图。
    QuitRetry, // 顶层模态页意图。
    QuitLocal, // 顶层模态页意图。
    QuitCancel, // 顶层模态页意图。
    SettingsApply, // 顶层模态页意图。
    DisplayYes, // 顶层模态页意图。
    DisplayNo, // 顶层模态页意图。
    Setting0Minus, // 顶层模态页意图。
    Setting0Plus, // 顶层模态页意图。
    Setting1Minus, // 顶层模态页意图。
    Setting1Plus, // 顶层模态页意图。
    Setting2Minus, // 顶层模态页意图。
    Setting2Plus, // 顶层模态页意图。
    Setting3Minus, // 顶层模态页意图。
    Setting3Plus, // 顶层模态页意图。
    Setting4Minus, // 帧同步设置的明确切换，不修改游戏存档。
    Setting4Plus, // 与Minus同为布尔切换，沿用设备设置布局。
    Core3, Core4, Core5, Core6, Core7, Core8, // 追加B04～B09本地选择白名单，保持已有动作序号；服务器仍按中央目录/阶段/修订裁决。
};
namespace DemoUIActions
{
    /** HitBox为Canvas固定名称；未知名称返回Unknown，禁止解析任意字符串作为业务命令。 */
    EDemoUIAction Decode(FName HitBox);
    /** Action为已经解码意图；顶层页存在时只有此集合可透传，未知/旧下层热区仍消费。 */
    bool IsOverlay(EDemoUIAction Action);
}
