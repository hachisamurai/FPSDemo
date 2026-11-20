// 职责：本地菜单与终端页面类型，无网络权威或持久经济含义。
#pragma once
#include "CoreMinimal.h"
#include "DemoMenuTypes.generated.h"

/** 本地终端互斥页签；只有页面协调器持有当前值，不复制。 */
enum class EDemoTerminalPage : uint8 { Stats, Weapons, Ammo, Core, Gambling /* 终端研究/冒险页，沿用同一会话 */ };
/** 保留原枚举值与反射名，移动头文件不改变现有蓝图契约或存档。 */
UENUM()
enum class EDemoMenuPage : uint8 { None, Saves, CreateSave, Pause, ReturnHub, Settings, QuitSaving, EndlessUnlock, Multiplayer, PlayerStatus /* 本地只读Tab页，不改变存档 */, FirstRunGuide /* 本机首次操作提示，关闭后持久记忆 */ }; // 追加枚举保持已有反射值；房间页由本地GI服务驱动。
/** 退出子流程状态，不承担页面路由；页面真值仍由MenuFlow持有。 */
enum class EDemoQuitState : uint8 { Idle, SavingLocal, WaitingCloud, Saved, Failed };
