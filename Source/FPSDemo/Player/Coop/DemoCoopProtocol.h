// 职责：合作网络契约的唯一版本源。使用：房间广播、LocalPlayer登录URL与GameMode准入共同引用；独立于磁盘/HTTP版本，无运行时服务或存档状态。
#pragma once

#include "CoreMinimal.h"

namespace DemoCoopProtocol
{
    inline constexpr TCHAR Version[] = TEXT("33"); // v33：同批实体弹表现Launch追加冻结AmmoType，Sample追加弹射段/点/方向、环境接触及穿出事实，旧端布局不兼容，拒绝混连。v32历史：FDemoMetalImpactEvent追加FleshTier/bHeavyWeapon；MulticastHitFeedback追加ShotDirection/bHeavyWeapon。v31历史：EDemoRosterKind追加Beacon（封锁眼）；v30历史：Owner个人Progress追加LastSlotsReceipt。磁盘V10/HTTP3不变。
    inline constexpr TCHAR LoginOption[] = TEXT("DemoProtocol"); // 引擎连接URL选项键，客户端拼接与服务器ParseOption共用，拒绝缺失或旧版本。
    inline constexpr TCHAR RoomPrefix[] = TEXT("FPSDemo-Coop-v"); // OSS搜索/广播标签前缀，与Version组合，不另存一份版本号。
}
