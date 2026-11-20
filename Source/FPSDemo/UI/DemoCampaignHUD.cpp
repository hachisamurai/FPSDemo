// 职责：保留核心旧菜单、S1 R02无框目标导航、分区进场短显、全阶段3D方向路标与右上角区域拓扑小地图（R02常驻文字按2026-11-19减负规则：同一信息只出现一处）。
// 用法：原HUD每帧调用；依赖拥有者菜单/GS复制、本World活动DemoRoomAnchor及公开任务/终端/敌人位置，仅本地只读绘制，不修改任何玩法真值或网络状态。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Game/DemoGameState.h"
#include "Game/Objectives/DemoObjectiveDevice.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Player/Coop/Ping/DemoTeamPingComponent.h"
#include "World/Rooms/DemoRoomAnchor.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Interaction/DemoInteractable.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"

namespace
{
    /** 单关内单个功能分区（A/B/C/D/E）的局部平面包络定义；单位cm，相对于本关ADemoRoomAnchor局部原点。 */
    struct FDemoTacticalZoneDef
    {
        const TCHAR* Id = TEXT(""); // 分区短代号（A..E），只读静态常量。
        const TCHAR* Name = TEXT(""); // 分区中文战术名称（如“公路入口”），只读静态常量。
        float MinX = 0.f; // 局部X下界（cm）。
        float MinY = 0.f; // 局部Y下界（cm）。
        float MaxX = 0.f; // 局部X上界（cm）。
        float MaxY = 0.f; // 局部Y上界（cm）。
    };

    /** 单关内单条主通道或侧路迂回走廊的局部平面包络定义；单位cm，相对于本关ADemoRoomAnchor局部原点。 */
    struct FDemoTacticalCorridorDef
    {
        float MinX = 0.f; // 走廊局部X下界（cm）。
        float MinY = 0.f; // 走廊局部Y下界（cm）。
        float MaxX = 0.f; // 走廊局部X上界（cm）。
        float MaxY = 0.f; // 走廊局部Y上界（cm）。
        bool bSideLoop = false; // true表示侧路迂回回环（side_xx），false表示主推进通道（main_xx）。
    };

    /** 单关完整空间拓扑定义；与layout_manifest_R03冻结几何一一对应，零运行时读盘。 */
    struct FDemoTacticalLevelTopology
    {
        int32 LevelNumber = 1; // 战役关卡编号1..10。
        const TCHAR* Title = TEXT(""); // 关卡叙事地点名（如“外围公路与铁丝网”）。
        const TCHAR* ShapeLabel = TEXT(""); // 空间构型短标签（如“道路折线”）。
        float FootprintX = 11200.f; // 关卡总宽cm（局部中心0）。
        float FootprintY = 8000.f; // 关卡总深cm（局部中心0）。
        const FDemoTacticalZoneDef* Zones = nullptr; // 本关功能分区静态数组首指针，生命周期与进程相同。
        int32 ZoneCount = 0; // 功能分区数量（4或5）。
        const FDemoTacticalCorridorDef* Corridors = nullptr; // 本关走廊静态数组首指针，生命周期与进程相同。
        int32 CorridorCount = 0; // 走廊矩形数量。
    };

    // L01 外围公路与铁丝网（道路折线）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL01[] = {
        {TEXT("A"), TEXT("公路入口"), -5100.f, -3500.f, -2700.f, -1500.f},
        {TEXT("B"), TEXT("折线查验坪"), -1900.f, -3500.f, 1100.f, -1100.f},
        {TEXT("C"), TEXT("围栏岗哨"), -1700.f, 200.f, 1300.f, 2600.f},
        {TEXT("D"), TEXT("内侧通路"), 2500.f, 600.f, 5100.f, 3400.f}
    };
    const FDemoTacticalCorridorDef CorridorsL01[] = {
        {-4200.f, -2800.f, -100.f, -2200.f, false}, {-700.f, -2800.f, -100.f, -2000.f, false},
        {-700.f, -2600.f, -100.f, 1700.f, false}, {-700.f, 1100.f, 100.f, 1700.f, false},
        {-500.f, 1100.f, 4100.f, 1700.f, false}, {3500.f, 1100.f, 4100.f, 2300.f, false},
        {-640.f, -2540.f, 4040.f, -2060.f, true}, {3560.f, -2540.f, 4040.f, 2240.f, true}
    };

    // L02 废弃检疫站（检疫双翼）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL02[] = {
        {TEXT("A"), TEXT("弃车入口"), -1400.f, -4300.f, 1400.f, -2500.f},
        {TEXT("B"), TEXT("西检疫翼"), -4200.f, -1600.f, -1600.f, 1400.f},
        {TEXT("C"), TEXT("东检疫翼"), 1600.f, -1600.f, 4200.f, 1400.f},
        {TEXT("D"), TEXT("内侧闸口"), -1400.f, 2400.f, 1400.f, 4200.f}
    };
    const FDemoTacticalCorridorDef CorridorsL02[] = {
        {-3200.f, -3700.f, 300.f, -3100.f, false}, {-3200.f, -3700.f, -2600.f, 200.f, false},
        {-3200.f, -400.f, 3200.f, 200.f, false}, {2600.f, -400.f, 3200.f, 3600.f, false},
        {-300.f, 3000.f, 3200.f, 3600.f, false}, {-3140.f, -340.f, -2660.f, 3540.f, true},
        {-3140.f, 3060.f, 240.f, 3540.f, true}
    };

    // L03 集装箱货场（货场错列）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL03[] = {
        {TEXT("A"), TEXT("入场栈口"), -5900.f, -4400.f, -3300.f, -2200.f},
        {TEXT("B"), TEXT("北侧箱列"), -2100.f, -4500.f, 1100.f, -1700.f},
        {TEXT("C"), TEXT("积水转运坪"), -4600.f, 500.f, -800.f, 4300.f},
        {TEXT("D"), TEXT("内侧货闸"), 2400.f, 200.f, 5800.f, 4400.f}
    };
    const FDemoTacticalCorridorDef CorridorsL03[] = {
        {-4900.f, -3600.f, -200.f, -3000.f, false}, {-800.f, -3600.f, -200.f, -2800.f, false},
        {-800.f, -3400.f, -200.f, -300.f, false}, {-3000.f, -900.f, -200.f, -300.f, false},
        {-3000.f, -900.f, -2400.f, 2700.f, false}, {-3000.f, 2100.f, 4400.f, 2700.f, false},
        {3800.f, 2000.f, 4400.f, 2700.f, false}, {-740.f, -3340.f, 4340.f, -2860.f, true},
        {3860.f, -3340.f, 4340.f, 2540.f, true}
    };

    // L04 旧港调度站（U形室内）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL04[] = {
        {TEXT("A"), TEXT("到达门厅"), -4000.f, -4800.f, -1400.f, -2200.f},
        {TEXT("B"), TEXT("调度主厅"), -4000.f, -600.f, -1400.f, 3600.f},
        {TEXT("C"), TEXT("转运记录间"), 100.f, 1900.f, 3900.f, 5000.f},
        {TEXT("D"), TEXT("内区接驳"), 1300.f, -4800.f, 3900.f, -1700.f}
    };
    const FDemoTacticalCorridorDef CorridorsL04[] = {
        {-3000.f, -3800.f, -2400.f, 1800.f, false}, {-3000.f, 1200.f, -2400.f, 3750.f, false},
        {-3000.f, 3150.f, 2300.f, 3750.f, false}, {1700.f, 3150.f, 2900.f, 3750.f, false},
        {2300.f, -3550.f, 2900.f, 3750.f, false}, {-2940.f, 1260.f, 140.f, 1740.f, true},
        {-340.f, -1240.f, 140.f, 1740.f, true}, {-340.f, -1240.f, 2840.f, -760.f, true},
        {2360.f, -3490.f, 2840.f, -760.f, true}
    };

    // L05 东区装卸平台（码头双长边）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL05[] = {
        {TEXT("A"), TEXT("码头接入"), -6600.f, -4600.f, -3800.f, -1800.f},
        {TEXT("B"), TEXT("西装卸长棚"), -6600.f, 1200.f, -2400.f, 4800.f},
        {TEXT("C"), TEXT("东侧吊装坪"), 0.f, 1200.f, 6200.f, 4800.f},
        {TEXT("D"), TEXT("内港出口"), 3400.f, -4800.f, 6600.f, -1400.f}
    };
    const FDemoTacticalCorridorDef CorridorsL05[] = {
        {-5500.f, -3500.f, -4900.f, 3300.f, false}, {-5500.f, 2700.f, -4200.f, 3300.f, false},
        {-4800.f, 2700.f, 3400.f, 3300.f, false}, {2800.f, 2700.f, 5300.f, 3300.f, false},
        {4700.f, -3400.f, 5300.f, 3300.f, false}, {-4740.f, 2760.f, -1060.f, 3240.f, true},
        {-1540.f, -3340.f, -1060.f, 3240.f, true}, {-1540.f, -3340.f, 5240.f, -2860.f, true}
    };

    // L06 地下供电隧道（S形维护回路）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL06[] = {
        {TEXT("A"), TEXT("供电入口"), -6300.f, -3100.f, -3900.f, -1100.f},
        {TEXT("B"), TEXT("南配电室"), -2200.f, -3100.f, 600.f, -1100.f},
        {TEXT("C"), TEXT("北维护室"), -2200.f, 1100.f, 1000.f, 3100.f},
        {TEXT("D"), TEXT("深区电闸"), 3800.f, 700.f, 6200.f, 3100.f}
    };
    const FDemoTacticalCorridorDef CorridorsL06[] = {
        {-5400.f, -2400.f, -500.f, -1800.f, false}, {-1100.f, -2400.f, -500.f, 2400.f, false},
        {-1100.f, 1800.f, -300.f, 2400.f, false}, {-900.f, 1800.f, 5300.f, 2400.f, false},
        {4700.f, 1600.f, 5300.f, 2400.f, false}, {-1040.f, -2340.f, 5240.f, -1860.f, true},
        {4760.f, -2340.f, 5240.f, 2140.f, true}
    };

    // L07 试验档案库（环廊＋记录间）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL07[] = {
        {TEXT("A"), TEXT("检索门厅"), -1400.f, -5000.f, 1400.f, -2800.f},
        {TEXT("B"), TEXT("西侧档架"), -4500.f, -1700.f, -1700.f, 2100.f},
        {TEXT("C"), TEXT("封闭记录间"), -900.f, -1300.f, 1900.f, 2100.f},
        {TEXT("D"), TEXT("回廊出口"), 1800.f, 2700.f, 4600.f, 5000.f}
    };
    const FDemoTacticalCorridorDef CorridorsL07[] = {
        {-3400.f, -4200.f, 300.f, -3600.f, false}, {-3400.f, -4200.f, -2800.f, 500.f, false},
        {-3400.f, -100.f, 800.f, 500.f, false}, {200.f, -100.f, 800.f, 700.f, false},
        {200.f, 100.f, 3500.f, 700.f, false}, {2900.f, 100.f, 3500.f, 4150.f, false},
        {-3340.f, -40.f, -2860.f, 4090.f, true}, {-3340.f, 3610.f, 3440.f, 4090.f, true}
    };

    // L08 防务驻军营地（三块错列营区）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL08[] = {
        {TEXT("A"), TEXT("外环哨口"), -6600.f, -4900.f, -4200.f, -2500.f},
        {TEXT("B"), TEXT("第一营块"), -2800.f, -4800.f, 600.f, -1600.f},
        {TEXT("C"), TEXT("第二营块"), -5200.f, 900.f, -1300.f, 4700.f},
        {TEXT("D"), TEXT("第三营块"), 1900.f, 400.f, 5800.f, 4600.f},
        {TEXT("E"), TEXT("内环接驳"), 4100.f, -4900.f, 6600.f, -2100.f}
    };
    const FDemoTacticalCorridorDef CorridorsL08[] = {
        {-5700.f, -4000.f, -800.f, -3400.f, false}, {-1400.f, -4000.f, -800.f, -2900.f, false},
        {-1400.f, -3500.f, -800.f, -200.f, false}, {-3550.f, -800.f, -800.f, -200.f, false},
        {-3550.f, -800.f, -2950.f, 3100.f, false}, {-3550.f, 2500.f, 4150.f, 3100.f, false},
        {3550.f, 2200.f, 4150.f, 3100.f, false}, {3550.f, 2200.f, 5650.f, 2800.f, false},
        {5050.f, -3800.f, 5650.f, 2800.f, false}, {-1340.f, -3440.f, 1820.f, -2960.f, true},
        {1340.f, -3440.f, 1820.f, 2740.f, true}, {1340.f, 2260.f, 4090.f, 2740.f, true}
    };

    // L09 冷却塔连廊（塔基双廊环）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL09[] = {
        {TEXT("A"), TEXT("塔基入口"), -7100.f, -3200.f, -4500.f, -600.f},
        {TEXT("B"), TEXT("北侧连廊"), -3100.f, -3300.f, 100.f, -900.f},
        {TEXT("C"), TEXT("南侧塔庭"), -3000.f, 800.f, 400.f, 3300.f},
        {TEXT("D"), TEXT("冷库接驳"), 3900.f, -600.f, 7000.f, 2800.f}
    };
    const FDemoTacticalCorridorDef CorridorsL09[] = {
        {-6100.f, -2200.f, -1200.f, -1600.f, false}, {-1800.f, -2400.f, -1200.f, -1600.f, false},
        {-1800.f, -2400.f, -1200.f, 2350.f, false}, {-1800.f, 1750.f, -1000.f, 2350.f, false},
        {-1600.f, 1750.f, 5750.f, 2350.f, false}, {5150.f, 800.f, 5750.f, 2350.f, false},
        {-1740.f, -2340.f, 5690.f, -1860.f, true}, {5210.f, -2340.f, 5690.f, 1340.f, true}
    };

    // L10 十号冷库核心室（前庭→通用Boss场）分区与通道静态表。
    const FDemoTacticalZoneDef ZonesL10[] = {
        {TEXT("A"), TEXT("封锁前庭"), -5700.f, -5600.f, -2900.f, -2600.f},
        {TEXT("B"), TEXT("核心首领场"), -2100.f, -5600.f, 4300.f, 800.f},
        {TEXT("C"), TEXT("隔离冷库"), 100.f, 2100.f, 3500.f, 5300.f},
        {TEXT("D"), TEXT("安全离场廊"), -5700.f, 2100.f, -2100.f, 5300.f}
    };
    const FDemoTacticalCorridorDef CorridorsL10[] = {
        {-4600.f, -4400.f, 1400.f, -3800.f, false}, {800.f, -4400.f, 1400.f, -2100.f, false},
        {800.f, -2700.f, 2100.f, -2100.f, false}, {1500.f, -2700.f, 2100.f, 4000.f, false},
        {-4200.f, 3400.f, 2100.f, 4000.f, false}, {-1540.f, -2640.f, 1340.f, -2160.f, true},
        {-1540.f, -2640.f, -1060.f, 240.f, true}, {-1540.f, -240.f, 3740.f, 240.f, true},
        {3260.f, -2640.f, 3740.f, 240.f, true}, {860.f, -2640.f, 3740.f, -2160.f, true}
    };

    // 十关完整拓扑主表；索引0..9对应LevelNumber 1..10。
    const FDemoTacticalLevelTopology LevelTopologies[] = {
        {1, TEXT("外围公路与铁丝网"), TEXT("道路折线"), 11200.f, 8000.f, ZonesL01, UE_ARRAY_COUNT(ZonesL01), CorridorsL01, UE_ARRAY_COUNT(CorridorsL01)},
        {2, TEXT("废弃检疫站"), TEXT("检疫双翼"), 9600.f, 9600.f, ZonesL02, UE_ARRAY_COUNT(ZonesL02), CorridorsL02, UE_ARRAY_COUNT(CorridorsL02)},
        {3, TEXT("集装箱货场"), TEXT("货场错列"), 12800.f, 10400.f, ZonesL03, UE_ARRAY_COUNT(ZonesL03), CorridorsL03, UE_ARRAY_COUNT(CorridorsL03)},
        {4, TEXT("旧港调度站"), TEXT("U形室内"), 9600.f, 11200.f, ZonesL04, UE_ARRAY_COUNT(ZonesL04), CorridorsL04, UE_ARRAY_COUNT(CorridorsL04)},
        {5, TEXT("东区装卸平台"), TEXT("码头双长边"), 14400.f, 11200.f, ZonesL05, UE_ARRAY_COUNT(ZonesL05), CorridorsL05, UE_ARRAY_COUNT(CorridorsL05)},
        {6, TEXT("地下供电隧道"), TEXT("S形维护回路"), 13600.f, 7200.f, ZonesL06, UE_ARRAY_COUNT(ZonesL06), CorridorsL06, UE_ARRAY_COUNT(CorridorsL06)},
        {7, TEXT("试验档案库"), TEXT("环廊＋记录间"), 10400.f, 11200.f, ZonesL07, UE_ARRAY_COUNT(ZonesL07), CorridorsL07, UE_ARRAY_COUNT(CorridorsL07)},
        {8, TEXT("防务驻军营地"), TEXT("三块错列营区"), 14400.f, 11200.f, ZonesL08, UE_ARRAY_COUNT(ZonesL08), CorridorsL08, UE_ARRAY_COUNT(CorridorsL08)},
        {9, TEXT("冷却塔连廊"), TEXT("塔基双廊环"), 15200.f, 8000.f, ZonesL09, UE_ARRAY_COUNT(ZonesL09), CorridorsL09, UE_ARRAY_COUNT(CorridorsL09)},
        {10, TEXT("十号冷库核心室"), TEXT("核心冷库"), 12800.f, 12800.f, ZonesL10, UE_ARRAY_COUNT(ZonesL10), CorridorsL10, UE_ARRAY_COUNT(CorridorsL10)}
    };

    /** 按关卡号1..10查询静态空间拓扑；无尽模式超出10关时按1..10循环映射，非法关卡返回nullptr。 */
    const FDemoTacticalLevelTopology* GetLevelTopology(int32 LevelNumber)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (LevelNumber <= 0) return nullptr;
        const int32 Index = (LevelNumber - 1) % UE_ARRAY_COUNT(LevelTopologies); // 兼容1..10及无尽轮次映射。
        return &LevelTopologies[Index];
    }

    /** 在当前World查找距玩家/视角最近的已就绪流送房间锚点；槽位间距50000cm，25000cm门限可严格排除待退役旧槽。 */
    const ADemoRoomAnchor* FindActiveRoomAnchor(const UWorld* World, const FVector& ReferenceWorldLocation)
    {
        DEMO_LOG_STATIC_TICK(World);
        if (!World || ReferenceWorldLocation.ContainsNaN()) return nullptr;
        const ADemoRoomAnchor* BestAnchor = nullptr; // 当前帧只读借用，不跨帧缓存。
        float BestDistSq = FMath::Square(25000.f); // 仅匹配当前激活流送槽位范围内的房间根锚点。
        for (TActorIterator<ADemoRoomAnchor> It(World); It; ++It)
        {
            const ADemoRoomAnchor* Candidate = *It;
            if (!IsValid(Candidate) || Candidate->IsHidden()) continue;
            const float DistSq = FVector::DistSquared2D(Candidate->GetActorLocation(), ReferenceWorldLocation);
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestAnchor = Candidate;
            }
        }
        return BestAnchor;
    }

    /** 将房间局部坐标(LocalPos)分类到具体功能区或通道：OutCurrentZoneIndex为所在分区（不在分区内为INDEX_NONE），OutNearestZoneIndex为最近分区，OutInSideLoop标识是否处于侧路回环。 */
    void ClassifyLocalPosition(const FDemoTacticalLevelTopology& Topo, const FVector& LocalPos,
        int32& OutCurrentZoneIndex, int32& OutNearestZoneIndex, bool& OutInSideLoop)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        OutCurrentZoneIndex = INDEX_NONE;
        OutNearestZoneIndex = 0;
        OutInSideLoop = false;
        float BestDistSq = MAX_flt; // 与各分区几何中心的平面距离平方。
        const float MarginCm = 80.f; // 门口过渡带容差cm，避免站在门框处频繁闪烁状态。
        for (int32 Index = 0; Index < Topo.ZoneCount; ++Index)
        {
            const FDemoTacticalZoneDef& Zone = Topo.Zones[Index];
            if (OutCurrentZoneIndex == INDEX_NONE
                && LocalPos.X >= Zone.MinX - MarginCm && LocalPos.X <= Zone.MaxX + MarginCm
                && LocalPos.Y >= Zone.MinY - MarginCm && LocalPos.Y <= Zone.MaxY + MarginCm)
            {
                OutCurrentZoneIndex = Index;
            }
            const FVector2D Center((Zone.MinX + Zone.MaxX) * .5f, (Zone.MinY + Zone.MaxY) * .5f);
            const float DistSq = FVector2D::DistSquared(FVector2D(LocalPos.X, LocalPos.Y), Center);
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                OutNearestZoneIndex = Index;
            }
        }
        if (OutCurrentZoneIndex == INDEX_NONE)
        {
            for (int32 Index = 0; Index < Topo.CorridorCount; ++Index)
            {
                const FDemoTacticalCorridorDef& Corr = Topo.Corridors[Index];
                if (Corr.bSideLoop
                    && LocalPos.X >= Corr.MinX - 60.f && LocalPos.X <= Corr.MaxX + 60.f
                    && LocalPos.Y >= Corr.MinY - 60.f && LocalPos.Y <= Corr.MaxY + 60.f)
                {
                    OutInSideLoop = true;
                    break;
                }
            }
        }
    }

    /** 判定当前流送房间锚点是否属于D03多区大地图（L_D03_01..10）；只读外包名与入场偏移，不访问服务器目录。 */
    bool IsD03RoomAnchor(const ADemoRoomAnchor* Anchor)
    {
        DEMO_LOG_STATIC_TICK(Anchor);
        if (!IsValid(Anchor)) return false;
        if (const UPackage* Pkg = Anchor->GetOutermost())
        {
            const FString PkgName = Pkg->GetName();
            if (PkgName.Contains(TEXT("L_D03_")) || PkgName.Contains(TEXT("PortLockdown"))) return true;
        }
        return Anchor->CombatPlayerOffset.GetAbsMax() > 2000.f || Anchor->ShopOffset.GetAbsMax() > 2000.f;
    }

    // 2026-11-19文字减负：原FormatZoneTag（路标后缀“ [B区·折线查验坪]”）已删除；分区位置由雷达高亮与进场短显表达，路标只留短名。

    /** 生成左上角当前所在分区与全图推进路线面包屑文案；OutLocationLine为当前位置与下一步指引，OutRouteLine为A→B→C→D全链高亮。2026-11-19起仅旧非R02页型使用。 */
    void BuildZoneBreadcrumb(const FDemoTacticalLevelTopology& Topo, int32 CurrentZoneIdx, int32 NearestZoneIdx,
        bool bInSideLoop, int32 RecommendedZoneIdx, FString& OutLocationLine, FString& OutRouteLine)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        const int32 ActiveIdx = CurrentZoneIdx != INDEX_NONE ? CurrentZoneIdx : NearestZoneIdx;
        const FDemoTacticalZoneDef& NearestZone = Topo.Zones[FMath::Clamp(ActiveIdx, 0, Topo.ZoneCount - 1)];
        const int32 GuideIdx = (RecommendedZoneIdx >= 0 && RecommendedZoneIdx < Topo.ZoneCount)
            ? RecommendedZoneIdx
            : FMath::Min(ActiveIdx + 1, Topo.ZoneCount - 1);
        const FDemoTacticalZoneDef& GuideZone = Topo.Zones[GuideIdx];
        if (CurrentZoneIdx != INDEX_NONE)
        {
            if (GuideIdx != CurrentZoneIdx)
            {
                OutLocationLine = FString::Printf(TEXT("当前位置：%s区 · %s  ｜  推进指引：前往 %s区 · %s"),
                    NearestZone.Id, NearestZone.Name, GuideZone.Id, GuideZone.Name);
            }
            else
            {
                OutLocationLine = FString::Printf(TEXT("当前位置：%s区 · %s（当前目标所在战区）"),
                    NearestZone.Id, NearestZone.Name);
            }
        }
        else
        {
            OutLocationLine = FString::Printf(TEXT("当前位置：%s（邻近 %s区·%s）  ｜  推进指引：前往 %s区 · %s"),
                bInSideLoop ? TEXT("侧路迂回回环") : TEXT("主联络通道"),
                NearestZone.Id, NearestZone.Name, GuideZone.Id, GuideZone.Name);
        }

        OutRouteLine = TEXT("区域路线：");
        for (int32 Index = 0; Index < Topo.ZoneCount; ++Index)
        {
            if (Index > 0) OutRouteLine += TEXT(" → ");
            const FDemoTacticalZoneDef& Z = Topo.Zones[Index];
            if (Index == CurrentZoneIdx)
            {
                OutRouteLine += FString::Printf(TEXT("【★%s %s】"), Z.Id, Z.Name);
            }
            else if (CurrentZoneIdx == INDEX_NONE && Index == NearestZoneIdx)
            {
                OutRouteLine += FString::Printf(TEXT("【~%s %s】"), Z.Id, Z.Name);
            }
            else
            {
                OutRouteLine += FString::Printf(TEXT("%s %s"), Z.Id, Z.Name);
            }
        }
    }

    /**
     * 解析安全区（Hub）中心、地板尺寸、军械商店与金币娱乐屋局部包围盒；OutAreaName非空时写入玩家当前所在安全区分区名（仅旧非R02页型显示）。
     * 2026-11-19文字减负：原OutBreadcrumb“分区导览”整行已删除，雷达已直接标注两屋位置。
     */
    void ClassifyHubPosition(const UWorld* World, const FVector& PlayerWorldPos, FVector& OutHubOrigin,
        FVector2D& OutHubFloorSize, FBox2D& OutGunShopBox, FBox2D& OutCasinoBox, FString* OutAreaName)
    {
        DEMO_LOG_STATIC_TICK(World);
        OutHubOrigin = FVector(-6000.f, 0.f, 10000.f);
        OutHubFloorSize = UDemoEncounterAreaSubsystem::GetDefaultHubFloorSizeCm();
        if (World)
        {
            if (const UDemoEncounterAreaSubsystem* Areas = World->GetSubsystem<UDemoEncounterAreaSubsystem>())
            {
                OutHubOrigin = Areas->GetAreaCenter(-1);
            }
        }
        FVector GunCenter(500.f, 800.f, 0.f), GunSize(1800.f, 1400.f, 360.f);
        FVector CasinoCenter(500.f, -800.f, 0.f), CasinoSize(1400.f, 1200.f, 360.f);
        if (World)
        {
            if (const UDemoHubShopWorldSubsystem* HubShops = World->GetSubsystem<UDemoHubShopWorldSubsystem>())
            {
                if (const UDemoHubShopDefinition* Def = HubShops->GetDefinition())
                {
                    GunCenter = Def->GunShopCenter;
                    GunSize = Def->GunShopInteriorSize;
                    CasinoCenter = Def->CasinoCenter;
                    CasinoSize = Def->CasinoInteriorSize;
                    OutHubFloorSize = Def->HubFloorSizeCm;
                }
            }
        }
        OutGunShopBox = FBox2D(FVector2D(GunCenter.X - GunSize.X * .5f, GunCenter.Y - GunSize.Y * .5f),
            FVector2D(GunCenter.X + GunSize.X * .5f, GunCenter.Y + GunSize.Y * .5f));
        OutCasinoBox = FBox2D(FVector2D(CasinoCenter.X - CasinoSize.X * .5f, CasinoCenter.Y - CasinoSize.Y * .5f),
            FVector2D(CasinoCenter.X + CasinoSize.X * .5f, CasinoCenter.Y + CasinoSize.Y * .5f));
        if (!OutAreaName) return; // 雷达只需要包围盒，不生成分区名字符串。
        const FVector2D Local2D(PlayerWorldPos.X - OutHubOrigin.X, PlayerWorldPos.Y - OutHubOrigin.Y);
        if (OutGunShopBox.ExpandBy(40.f).IsInside(Local2D)) *OutAreaName = TEXT("军械商店（东北屋）");
        else if (OutCasinoBox.ExpandBy(40.f).IsInside(Local2D)) *OutAreaName = TEXT("金币娱乐屋（东南屋）");
        else if (FMath::Abs(Local2D.X) <= 520.f && FMath::Abs(Local2D.Y) <= 580.f) *OutAreaName = TEXT("中央整备终端区");
        else *OutAreaName = TEXT("备战大厅庭院");
    }
}

void ADemoHUD::DrawCoreMenu(ADemoPlayerController* PC)
{
    DEMO_LOG_TICK();
    const FDemoPlayerMenuSnapshot& View=GetPlayerView(); // 此控制器专属，只借用到绘制结束。
    const float X=ViewWidth*.5f-480.f,Y=ViewHeight*.5f-328.f; // Legacy Canvas回退紧凑展示七行；正式CommonUI使用原ScrollBox的三列多行。
    Panel(X,Y,960,656,GetPanelSurface(),12);
    Label(TEXT("本轮流派核心"),X+32,Y+32,30,DemoUI::Ink);
    Label(TEXT("免费选择一种 · 出发后锁定 · 第3/7关选择分支 · 死亡重生保留，整轮结束清除"),X+32,Y+86,15,DemoUI::Muted);
    Button(TEXT("TerminalStats"),TEXT("返回升级"),X+696,Y+28,118,36,true,true);
    Button(TEXT("Close"),TEXT("关闭"),X+830,Y+28,98,36,true,true);
    for(int32 Index=0;Index<FMath::Min(DemoRunBuild::CoreCount(),View.CoreOffers.Num());++Index)
    {
        const FDemoShopOfferSnapshot& Offer=View.CoreOffers[Index]; // 权威生成名称/规则与可选原因。
        const float CardY=Y+124+68.f*Index; // 七行均使用同源绘制/点击边界，不裁掉后四核心按钮。
        Panel(X+32,CardY,896,62,DemoUI::Raised,4);
        Label(Offer.Title,X+46,CardY+19,21,Offer.bSelected?DemoUI::Accent:DemoUI::Ink);
        for(int32 Line=0;Line<FMath::Min(2,FMath::DivideAndRoundUp(Offer.Detail.Len(),32));++Line) // Canvas兼容摘要；完整限制在正式可滚动UMG页显示。
            Label(Offer.Detail.Mid(Line*32,32),X+238,CardY+12+Line*20,13,DemoUI::Muted);
        Button(FName(*FString::Printf(TEXT("Core%d"),Index)),Offer.bSelected?TEXT("已选择"):TEXT("选择此核心"),X+746,CardY+10,164,42,Offer.BlockReason.IsEmpty());
    }
    Label(PC->GetMenuMessage(),X+32,Y+616,15,DemoUI::Gold);
}

void ADemoHUD::DrawTacticalMinimap(const ADemoGameState* State, ADemoPlayerController* PC)
{
    DEMO_LOG_TICK();
    if (!Canvas || !State || !PC || !GetWorld() || UIScale <= 0.f) return;
    if (State->Phase != EDemoPhase::Hub && State->Phase != EDemoPhase::Combat && State->Phase != EDemoPhase::Intermission) return;
    const ADemoCharacter* Player = Cast<ADemoCharacter>(PC->GetPawn()); // 本帧只读借用；开镜或操作实体娱乐机时隐藏雷达。
    if (Player && Player->GetWeaponComponent() && Player->GetWeaponComponent()->GetScopeLevel() > 0) return;
    if (const UDemoCasinoDeviceInputComponent* DeviceInput = PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>())
    {
        if (DeviceInput->IsOperating()) return;
    }

    FVector ViewLocation = FVector::ZeroVector;
    FRotator ViewRotation = FRotator::ZeroRotator;
    PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
    const FVector RefWorldPos = (Player && !Player->IsCoopEliminated()) ? Player->GetActorLocation() : ViewLocation; // 存活取角色胶囊中心，观战取实际相机位置。

    const FLinearColor Ink(FColor(233, 227, 215));
    const FLinearColor Amber(FColor(190, 148, 85));
    const FLinearColor Ember(FColor(196, 76, 52));
    const FLinearColor Cyan(0.32f, 0.86f, 0.72f, 1.f);

    const float MapX = ViewWidth - 252.f; // 右上角钱包下方固定锚点，不与右上银币/右下弹药重叠。
    const float MapY = 90.f;
    const float MapW = 208.f;
    const float MapH = 160.f;
    Panel(MapX, MapY, MapW, MapH, FLinearColor(0.025f, 0.032f, 0.042f, 0.82f), 3.f);

    // 绘制1px战术外框线。
    const FLinearColor FrameColor(0.45f, 0.50f, 0.56f, 0.55f);
    DrawLine(MapX * UIScale, MapY * UIScale, (MapX + MapW) * UIScale, MapY * UIScale, FrameColor, 1.f);
    DrawLine(MapX * UIScale, (MapY + MapH) * UIScale, (MapX + MapW) * UIScale, (MapY + MapH) * UIScale, FrameColor, 1.f);
    DrawLine(MapX * UIScale, MapY * UIScale, MapX * UIScale, (MapY + MapH) * UIScale, FrameColor, 1.f);
    DrawLine((MapX + MapW) * UIScale, MapY * UIScale, (MapX + MapW) * UIScale, (MapY + MapH) * UIScale, FrameColor, 1.f);

    const float InnerX = MapX + 10.f;
    const float InnerY = MapY + 10.f; // 2026-11-19起雷达不画标题/图例文字，上下各留10px边距，空间全部给地图。
    const float InnerW = MapW - 20.f;
    const float InnerH = MapH - 20.f;
    const float InnerCenterX = InnerX + InnerW * .5f;
    const float InnerCenterY = InnerY + InnerH * .5f;

    FTransform SpaceOrigin = FTransform::Identity; // 局部坐标系基准（Hub中心或流送房间Anchor）。
    float FootprintX = 7200.f, FootprintY = 7200.f;

    if (State->Phase == EDemoPhase::Hub)
    {
        FVector HubOrigin = FVector::ZeroVector;
        FVector2D HubFloorSize(5600.f, 5600.f);
        FBox2D GunBox, CasinoBox;
        ClassifyHubPosition(GetWorld(), RefWorldPos, HubOrigin, HubFloorSize, GunBox, CasinoBox, nullptr);
        SpaceOrigin = FTransform(HubOrigin);
        FootprintX = FMath::Max(4000.f, static_cast<float>(HubFloorSize.X) + 600.f);
        FootprintY = FMath::Max(4000.f, static_cast<float>(HubFloorSize.Y) + 600.f);
        const float Scale = FMath::Min(InnerW / FootprintX, InnerH / FootprintY);
        // 原“安全区 · 区域雷达”标题与区域名已删除：左上大标题已写安全区，所在屋由下方房间高亮表达。

        // 绘制安全区主庭院底板。
        const float FloorW = static_cast<float>(HubFloorSize.X) * Scale;
        const float FloorH = static_cast<float>(HubFloorSize.Y) * Scale;
        Panel(InnerCenterX - FloorW * .5f, InnerCenterY - FloorH * .5f, FloorW, FloorH, FLinearColor(0.14f, 0.17f, 0.21f, 0.80f), 1.f);

        // 绘制军械商店与金币娱乐屋分区矩形。
        const auto DrawHubRoom = [&](const FBox2D& Box, const TCHAR* RoomTitle, FLinearColor AccentColor)
        {
            // 捕获：本帧栈上只读InnerCenterX/Y、Scale与ADemoHUD成员绘制接口；仅当前同步绘制调用，不跨帧保存。
            const float RX = InnerCenterX + static_cast<float>(Box.Min.X) * Scale;
            const float RY = InnerCenterY + static_cast<float>(Box.Min.Y) * Scale;
            const float RW = FMath::Max(6.f, static_cast<float>(Box.Max.X - Box.Min.X) * Scale);
            const float RH = FMath::Max(6.f, static_cast<float>(Box.Max.Y - Box.Min.Y) * Scale);
            const FVector LocalPlayer = SpaceOrigin.InverseTransformPosition(RefWorldPos);
            const bool bInsideRoom = Box.ExpandBy(40.f).IsInside(FVector2D(LocalPlayer.X, LocalPlayer.Y));
            Panel(RX, RY, RW, RH, bInsideRoom ? AccentColor.CopyWithNewOpacity(0.32f) : FLinearColor(0.22f, 0.26f, 0.32f, 0.90f), 1.f);
            const FLinearColor Border = bInsideRoom ? AccentColor : AccentColor.CopyWithNewOpacity(0.65f);
            DrawLine(RX * UIScale, RY * UIScale, (RX + RW) * UIScale, RY * UIScale, Border, 1.2f * UIScale);
            DrawLine(RX * UIScale, (RY + RH) * UIScale, (RX + RW) * UIScale, (RY + RH) * UIScale, Border, 1.2f * UIScale);
            DrawLine(RX * UIScale, RY * UIScale, RX * UIScale, (RY + RH) * UIScale, Border, 1.2f * UIScale);
            DrawLine((RX + RW) * UIScale, RY * UIScale, (RX + RW) * UIScale, (RY + RH) * UIScale, Border, 1.2f * UIScale);
            Label(RoomTitle, RX + RW * .5f, RY + RH * .5f - 6.f, 10.f, bInsideRoom ? Amber : Ink, true);
        };
        DrawHubRoom(GunBox, TEXT("军械店"), Amber);
        DrawHubRoom(CasinoBox, TEXT("娱乐屋"), Cyan);
    }
    else
    {
        const ADemoRoomAnchor* Anchor = FindActiveRoomAnchor(GetWorld(), RefWorldPos);
        const FDemoTacticalLevelTopology* Topo = GetLevelTopology(State->LevelNumber);
        const bool bUseD03Topo = IsD03RoomAnchor(Anchor) && Topo;
        if (bUseD03Topo)
        {
            SpaceOrigin = Anchor->GetActorTransform();
            FootprintX = Topo->FootprintX + 800.f;
            FootprintY = Topo->FootprintY + 800.f;
            const float Scale = FMath::Min(InnerW / FootprintX, InnerH / FootprintY);
            const FVector LocalPlayer = SpaceOrigin.InverseTransformPosition(RefWorldPos);
            int32 CurrentZoneIdx = INDEX_NONE, NearestZoneIdx = 0;
            bool bInSideLoop = false;
            ClassifyLocalPosition(*Topo, LocalPlayer, CurrentZoneIdx, NearestZoneIdx, bInSideLoop);
            // 原“L01 · 地点名 [A区]”标题已删除：关数/地点名在左上目标行，当前分区由下方矩形琥珀高亮与进场短显表达。

            // 1. 先画主通道与侧路回环底层。
            for (int32 CIdx = 0; CIdx < Topo->CorridorCount; ++CIdx)
            {
                const FDemoTacticalCorridorDef& Corr = Topo->Corridors[CIdx];
                const float CX = InnerCenterX + Corr.MinX * Scale;
                const float CY = InnerCenterY + Corr.MinY * Scale;
                const float CW = FMath::Max(2.f, (Corr.MaxX - Corr.MinX) * Scale);
                const float CH = FMath::Max(2.f, (Corr.MaxY - Corr.MinY) * Scale);
                Panel(CX, CY, CW, CH, Corr.bSideLoop ? FLinearColor(0.24f, 0.19f, 0.14f, 0.72f) : FLinearColor(0.14f, 0.17f, 0.22f, 0.82f), 0.f);
            }

            // 2. 绘制A→B→C→D推进主路线引导连线。
            for (int32 ZIdx = 0; ZIdx + 1 < Topo->ZoneCount; ++ZIdx)
            {
                const FDemoTacticalZoneDef& ZA = Topo->Zones[ZIdx];
                const FDemoTacticalZoneDef& ZB = Topo->Zones[ZIdx + 1];
                const float AX = InnerCenterX + (ZA.MinX + ZA.MaxX) * .5f * Scale;
                const float AY = InnerCenterY + (ZA.MinY + ZA.MaxY) * .5f * Scale;
                const float BX = InnerCenterX + (ZB.MinX + ZB.MaxX) * .5f * Scale;
                const float BY = InnerCenterY + (ZB.MinY + ZB.MaxY) * .5f * Scale;
                DrawLine(AX * UIScale, AY * UIScale, BX * UIScale, BY * UIScale, Amber.CopyWithNewOpacity(0.32f), 1.2f * UIScale);
            }

            // 3. 绘制各功能分区矩形与代号（A/B/C/D/E），高亮玩家当前所在分区。
            for (int32 ZIdx = 0; ZIdx < Topo->ZoneCount; ++ZIdx)
            {
                const FDemoTacticalZoneDef& Zone = Topo->Zones[ZIdx];
                const float ZX = InnerCenterX + Zone.MinX * Scale;
                const float ZY = InnerCenterY + Zone.MinY * Scale;
                const float ZW = FMath::Max(8.f, (Zone.MaxX - Zone.MinX) * Scale);
                const float ZH = FMath::Max(8.f, (Zone.MaxY - Zone.MinY) * Scale);
                const bool bIsCurrent = (ZIdx == CurrentZoneIdx);
                Panel(ZX, ZY, ZW, ZH, bIsCurrent ? Amber.CopyWithNewOpacity(0.30f) : FLinearColor(0.20f, 0.24f, 0.30f, 0.88f), 1.f);
                const FLinearColor ZBorder = bIsCurrent ? Amber : FLinearColor(0.46f, 0.52f, 0.58f, 0.60f);
                const float BorderThickness = (bIsCurrent ? 1.6f : 1.f) * UIScale;
                DrawLine(ZX * UIScale, ZY * UIScale, (ZX + ZW) * UIScale, ZY * UIScale, ZBorder, BorderThickness);
                DrawLine(ZX * UIScale, (ZY + ZH) * UIScale, (ZX + ZW) * UIScale, (ZY + ZH) * UIScale, ZBorder, BorderThickness);
                DrawLine(ZX * UIScale, ZY * UIScale, ZX * UIScale, (ZY + ZH) * UIScale, ZBorder, BorderThickness);
                DrawLine((ZX + ZW) * UIScale, ZY * UIScale, (ZX + ZW) * UIScale, (ZY + ZH) * UIScale, ZBorder, BorderThickness);
                Label(Zone.Id, ZX + ZW * .5f, ZY + ZH * .5f - 6.f, 10.5f, bIsCurrent ? Amber : Ink, true);
            }
        }
        else
        {
            // 兼容旧三战区或非D03小房间：以当前房间锚点或玩家附近战区中心为雷达原点。
            FVector FallbackCenter = Anchor ? Anchor->GetActorLocation() : RefWorldPos;
            if (!Anchor)
            {
                if (const UDemoEncounterAreaSubsystem* Areas = GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>())
                {
                    FallbackCenter = Areas->GetAreaCenter(FMath::Clamp((State->LevelNumber - 1) % 3, 0, 2));
                }
            }
            SpaceOrigin = FTransform(FallbackCenter);
            FootprintX = 8000.f;
            FootprintY = 8000.f;
            Panel(InnerX, InnerY, InnerW, InnerH, FLinearColor(0.12f, 0.15f, 0.19f, 0.72f), 1.f);
        }
    }

    const float MapScale = FMath::Min(InnerW / FMath::Max(1000.f, FootprintX), InnerH / FMath::Max(1000.f, FootprintY));
    const auto ProjectToMinimap = [&](const FVector& WorldPos) -> FVector2D
    {
        // 捕获：本帧栈上只读SpaceOrigin、InnerCenterX/Y、InnerX/Y/W/H及MapScale；仅在当前DrawTacticalMinimap调用内同步转换坐标。
        const FVector Local = SpaceOrigin.InverseTransformPosition(WorldPos);
        const float SX = FMath::Clamp(InnerCenterX + Local.X * MapScale, InnerX + 2.f, InnerX + InnerW - 2.f);
        const float SY = FMath::Clamp(InnerCenterY + Local.Y * MapScale, InnerY + 2.f, InnerY + InnerH - 2.f);
        return FVector2D(SX, SY);
    };

    // 4. 绘制活动控制点/设施/终端/存活敌人/队友与本人位置朝向。
    if (State->Phase == EDemoPhase::Combat)
    {
        if (State->Objective.Kind == EDemoObjectiveKind::RotatingControl && State->Objective.State == EDemoObjectiveState::Active
            && !State->Objective.ActiveLocation.IsNearlyZero())
        {
            const FVector2D Pt = ProjectToMinimap(State->Objective.ActiveLocation);
            Panel(Pt.X - 4.5f, Pt.Y - 4.5f, 9.f, 9.f, Amber.CopyWithNewOpacity(0.38f), 1.f);
            Panel(Pt.X - 2.f, Pt.Y - 2.f, 4.f, 4.f, Amber, 1.f);
        }
        for (TActorIterator<ADemoObjectiveDevice> It(GetWorld()); It; ++It)
        {
            const ADemoObjectiveDevice* Device = *It;
            if (!IsValid(Device) || Device->IsHidden()) continue;
            const FDemoObjectiveDeviceView View = Device->GetStatusView();
            if (View.bDestroyed) continue;
            const FVector2D Pt = ProjectToMinimap(Device->GetActorLocation());
            Panel(Pt.X - 3.5f, Pt.Y - 3.5f, 7.f, 7.f, Amber, 1.f);
        }
    }

    // 绘制升级/下一关终端标记（安全区及清关备战阶段）。
    for (TActorIterator<ADemoInteractable> It(GetWorld()); It; ++It)
    {
        const ADemoInteractable* Term = *It;
        if (!IsValid(Term) || Term->IsHidden() || FVector::DistSquared2D(Term->GetActorLocation(), RefWorldPos) > FMath::Square(22000.f)) continue;
        const FVector2D Pt = ProjectToMinimap(Term->GetActorLocation());
        const bool bNextLevel = Term->GetPrompt().Contains(TEXT("下一关"));
        Panel(Pt.X - 3.f, Pt.Y - 3.f, 6.f, 6.f, bNextLevel ? Amber : Cyan, 1.f);
    }

    // 绘制存活敌人红点（Boss加大高亮）。
    if (State->Phase == EDemoPhase::Combat)
    {
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It)
        {
            const ADemoEnemy* Enemy = *It;
            if (!IsValid(Enemy) || Enemy->IsHidden() || !Enemy->IsAlive() || Enemy->GetHealth() <= 0.f) continue;
            const FVector2D Pt = ProjectToMinimap(Enemy->GetActorLocation());
            const float DotSize = Enemy->IsBoss() ? 6.5f : 3.8f;
            Panel(Pt.X - DotSize * .5f, Pt.Y - DotSize * .5f, DotSize, DotSize, Ember, 1.f);
        }
    }

    // 绘制合作队友位置点。
    for (TActorIterator<ADemoCharacter> It(GetWorld()); It; ++It)
    {
        const ADemoCharacter* Char = *It;
        if (!IsValid(Char) || Char == Player || Char->IsHidden() || Char->IsCoopEliminated()) continue;
        const FVector2D Pt = ProjectToMinimap(Char->GetActorLocation());
        Panel(Pt.X - 2.2f, Pt.Y - 2.2f, 4.4f, 4.4f, Cyan, 2.f);
    }

    // 绘制本人位置与相机朝向视锥。
    const FVector2D PlayerPt = ProjectToMinimap(RefWorldPos);
    const float RelativeYawDeg = ViewRotation.Yaw - SpaceOrigin.Rotator().Yaw;
    const float YawRad = FMath::DegreesToRadians(RelativeYawDeg);
    const float ConeHalfRad = 0.50f; // 约±28.6度视锥边线，直观反映当前镜头朝向。
    const float ConeLen = 12.f;
    const FVector2D ConeLeft = PlayerPt + FVector2D(FMath::Cos(YawRad - ConeHalfRad), FMath::Sin(YawRad - ConeHalfRad)) * ConeLen;
    const FVector2D ConeRight = PlayerPt + FVector2D(FMath::Cos(YawRad + ConeHalfRad), FMath::Sin(YawRad + ConeHalfRad)) * ConeLen;
    const FVector2D HeadingTip = PlayerPt + FVector2D(FMath::Cos(YawRad), FMath::Sin(YawRad)) * 9.f;
    DrawLine(PlayerPt.X * UIScale, PlayerPt.Y * UIScale, ConeLeft.X * UIScale, ConeLeft.Y * UIScale, Ink.CopyWithNewOpacity(0.50f), 1.2f * UIScale);
    DrawLine(PlayerPt.X * UIScale, PlayerPt.Y * UIScale, ConeRight.X * UIScale, ConeRight.Y * UIScale, Ink.CopyWithNewOpacity(0.50f), 1.2f * UIScale);
    DrawLine(PlayerPt.X * UIScale, PlayerPt.Y * UIScale, HeadingTip.X * UIScale, HeadingTip.Y * UIScale, Ink, 2.f * UIScale);
    Panel(PlayerPt.X - 2.6f, PlayerPt.Y - 2.6f, 5.2f, 5.2f, Ink, 2.5f);
    // 原底部图例已删除：终端点颜色与屏幕路标同色（青=升级、琥珀=出发），敌人剩余数已在左上目标行。
}

void ADemoHUD::DrawCampaignStatus(const ADemoGameState* State,ADemoPlayerController* PC)
{
    DEMO_LOG_TICK();
    if(!State||!PC||!GetWorld()) return;
    const FLinearColor Ink(FColor(233,227,215)); // S0已核R02前景Token；只改变显示，不把颜色当目标/技能许可。
    const FLinearColor Amber(FColor(190,148,85)); // 出发终端与关键目标强调色，与雷达出发终端点同色。
    const FLinearColor Cyan(0.32f, 0.86f, 0.72f, 1.f); // 升级终端路标色，与雷达升级终端点同色，替代已删除的雷达图例。
    const FLinearColor Ember(FColor(196,76,52)); // 威胁/首领方向引导色。

    FVector ViewLocation = FVector::ZeroVector;
    FRotator ViewRotation = FRotator::ZeroRotator;
    PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
    const ADemoCharacter* LocalPlayer = Cast<ADemoCharacter>(PC->GetPawn());
    const FVector RefWorldPos = (LocalPlayer && !LocalPlayer->IsCoopEliminated()) ? LocalPlayer->GetActorLocation() : ViewLocation;

    // 右上角战术小地图（安全区、战斗与清关整备全程提供空间定位）。
    DrawTacticalMinimap(State, PC);

    if(State->Phase==EDemoPhase::Hub && !State->bEndless)
    {
        const ADemoPlayerState* PS=PC->GetPlayerState<ADemoPlayerState>(); // Hub仅查询本人选择，不从队友PS读取私有构筑。
        const UDemoRunBuildComponent* Build=PS?PS->GetRunBuildComponent():nullptr;
        ZoneToastKey = INDEX_NONE; // 回安全区后下一次进关的首个分区重新短显。
        ZoneToastUntil = 0.;

        if(IsR02GameplayPresentation())
        {
            // 2026-11-19文字减负：只留一条“下一步做什么”。“当前位置/分区导览”两行删除，地点由左上“安全区”大标题与雷达表达。
            Label((!Build || !Build->HasSelectedCore()) ? TEXT("前往升级终端 · 选择流派核心") : TEXT("前往出发终端 · 开始行动"), 44.f, 102.f, 15.f, Ink);
        }
        else
        {
            FVector HubOrigin = FVector::ZeroVector; // 旧非R02页型保留原“当前位置”行，需要分区名。
            FVector2D HubFloorSize(5600.f, 5600.f);
            FBox2D GunBox, CasinoBox;
            FString HubAreaName;
            ClassifyHubPosition(GetWorld(), RefWorldPos, HubOrigin, HubFloorSize, GunBox, CasinoBox, &HubAreaName);
            Label(Build&&Build->HasSelectedCore()?TEXT("核心已选择 · 出发前可在终端更换"):TEXT("出发前请到终端选择一种流派核心"),48,182,15,Ink);
            Label(FString::Printf(TEXT("当前位置：%s"), *HubAreaName), 48.f, 204.f, 13.f, Amber);
        }

        // 操作实体娱乐设备时不叠加3D世界路标。
        const UDemoCasinoDeviceInputComponent* DeviceInput = PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>();
        if (!DeviceInput || !DeviceInput->IsOperating())
        {
            int32 HubLane = 0;
            for (TActorIterator<ADemoInteractable> It(GetWorld()); It; ++It)
            {
                const ADemoInteractable* Term = *It;
                if (!IsValid(Term) || Term->IsHidden() || FVector::DistSquared2D(Term->GetActorLocation(), RefWorldPos) > FMath::Square(15000.f)) continue;
                const bool bNextLevel = Term->GetPrompt().Contains(TEXT("下一关"));
                // 终端场景英文标牌已默认隐藏，此中文短名是终端唯一名称；颜色与雷达终端点一致。
                DrawWorldDirectionIndicator(Term->GetActorLocation() + FVector(0.f, 0.f, 125.f),
                    bNextLevel ? TEXT("出发终端") : TEXT("升级终端"),
                    bNextLevel ? Amber : Cyan, FMath::Min(HubLane++, 4));
            }
            // 原军械商店/金币娱乐屋入口常驻路标已删除：两屋在庭院内直接可见，雷达已标注“军械店/娱乐屋”。
        }
        return;
    }

    const ADemoRoomAnchor* ActiveAnchor = FindActiveRoomAnchor(GetWorld(), RefWorldPos);
    const FDemoTacticalLevelTopology* Topo = GetLevelTopology(State->LevelNumber);
    const bool bHasD03Topo = IsD03RoomAnchor(ActiveAnchor) && Topo;
    const TCHAR* ChineseLevels[]={TEXT(""),TEXT("第一关"),TEXT("第二关"),TEXT("第三关"),TEXT("第四关"),TEXT("第五关"),TEXT("第六关"),TEXT("第七关"),TEXT("第八关"),TEXT("第九关"),TEXT("第十关")}; // 纯显示，超出固定战役仍保真实阿拉伯关数。
    const FString LevelPrefix = State->LevelNumber>=1&&State->LevelNumber<=10?FString(ChineseLevels[State->LevelNumber]):FString::Printf(TEXT("第%d关"),State->LevelNumber);
    const FString LevelTitleSuffix = Topo ? FString::Printf(TEXT(" · %s"), Topo->Title) : FString();

    int32 CurrentZoneIdx = INDEX_NONE, NearestZoneIdx = 0;
    bool bInSideLoop = false;
    if (bHasD03Topo)
    {
        const FVector PlayerLocalPos = ActiveAnchor->GetActorTransform().InverseTransformPosition(RefWorldPos);
        ClassifyLocalPosition(*Topo, PlayerLocalPos, CurrentZoneIdx, NearestZoneIdx, bInSideLoop);
    }
    // 分区进场短显输入：仅在明确位于某功能分区时给出键与名称；走廊/非D03传INDEX_NONE，只续画未结束的短显。
    const FDemoTacticalZoneDef* ToastZone = (bHasD03Topo && CurrentZoneIdx != INDEX_NONE && CurrentZoneIdx < Topo->ZoneCount) ? &Topo->Zones[CurrentZoneIdx] : nullptr;
    const int32 ToastZoneKey = ToastZone ? State->LevelNumber * 16 + CurrentZoneIdx : INDEX_NONE; // 每关最多5区，16进位保证跨关不重复。

    if(State->Phase==EDemoPhase::Intermission)
    {
        int32 TermLane = 0;
        for (TActorIterator<ADemoInteractable> It(GetWorld()); It; ++It)
        {
            const ADemoInteractable* Term = *It;
            if (!IsValid(Term) || Term->IsHidden() || FVector::DistSquared2D(Term->GetActorLocation(), RefWorldPos) > FMath::Square(25000.f)) continue;
            const bool bNextLevel = Term->GetPrompt().Contains(TEXT("下一关"));
            // 2026-11-19文字减负：去掉“[B区·地点名]”后缀，终端位置由箭头与雷达终端点表达。
            DrawWorldDirectionIndicator(Term->GetActorLocation() + FVector(0.f, 0.f, 125.f),
                bNextLevel ? TEXT("出发终端") : TEXT("升级终端"),
                bNextLevel ? Amber : Cyan, FMath::Min(TermLane++, 4));
        }

        if (IsR02GameplayPresentation())
        {
            Label(FString::Printf(TEXT("%s%s · 区域肃清 · 前往终端整备出发"), *LevelPrefix, *LevelTitleSuffix), 44.f, 132.f, 18.f, Ink);
            // 原“当前位置/推进指引”与“区域路线”两行删除，改为换区短显。
            if (bHasD03Topo) DrawZoneEntryToast(ToastZoneKey, ToastZone ? ToastZone->Id : TEXT(""), ToastZone ? ToastZone->Name : TEXT(""), 158.f);
        }
        return;
    }

    if(State->Phase!=EDemoPhase::Combat) return;
    const FDemoObjectiveSnapshot& Objective=State->Objective; // 公共任务快照，不能由HUD累加进度。
    FString Title,Detail; // 一帧展示值；标题/进度分层，不将长控制点句子挤在一行。
    float Fraction=0.f; // 仅设施/控制点有权威分母时显示进度；歼灭不编造初始敌人数。
    if(Objective.Kind==EDemoObjectiveKind::Sabotage)
    {
        Title=TEXT("摧毁关键设施"); Detail=FString::Printf(TEXT("设施 %d / %d · 剩余敌人 %d"),Objective.Completed,Objective.Total,State->EnemiesRemaining);
        Fraction=static_cast<float>(Objective.Completed)/FMath::Max(1,Objective.Total);
    }
    else if(Objective.Kind==EDemoObjectiveKind::RotatingControl)
    {
        const TCHAR* Status=Objective.ControlStatus==EDemoControlStatus::Contested?TEXT("敌人争夺中"):Objective.ControlStatus==EDemoControlStatus::Capturing?TEXT("占领中"):Objective.ControlStatus==EDemoControlStatus::Relocating?TEXT("转移中"):TEXT("进入标记区域"); // 仅翻译服务器确认状态。
        Title=FString::Printf(TEXT("控制点 %d / %d · %s"),FMath::Min(Objective.Completed+1,Objective.Total),Objective.Total,Status);
        Detail=FString::Printf(TEXT("%.1f / %.0f 秒 · 剩余敌人 %d"),Objective.ProgressSeconds,Objective.RequiredSeconds,State->EnemiesRemaining);
        Fraction=Objective.ProgressSeconds/FMath::Max(1.f,Objective.RequiredSeconds);
    }
    else { Title=TEXT("清除区域内全部威胁"); Detail=FString::Printf(TEXT("剩余敌人 %02d · 怪物 Lv%d"),State->EnemiesRemaining,State->LevelNumber); }

    // 扫描当前关首要导航目标位置（用于推荐推进分区与3D世界方向指示）。
    int32 RecommendedZoneIdx = INDEX_NONE;
    const ADemoEnemy* NearestEnemy = nullptr;
    const ADemoEnemy* LiveBoss = nullptr;
    float NearestEnemyDistSq = MAX_flt;
    for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It)
    {
        const ADemoEnemy* Enemy = *It;
        if (!IsValid(Enemy) || Enemy->IsHidden() || !Enemy->IsAlive() || Enemy->GetHealth() <= 0.f) continue;
        if (Enemy->IsBoss()) LiveBoss = Enemy;
        const float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), RefWorldPos);
        if (DistSq < NearestEnemyDistSq)
        {
            NearestEnemyDistSq = DistSq;
            NearestEnemy = Enemy;
        }
    }

    FVector PrimaryGuideWorldPos = FVector::ZeroVector;
    if (Objective.Kind == EDemoObjectiveKind::RotatingControl && Objective.State == EDemoObjectiveState::Active && !Objective.ActiveLocation.IsNearlyZero())
    {
        PrimaryGuideWorldPos = Objective.ActiveLocation;
    }
    else if (Objective.Kind == EDemoObjectiveKind::Sabotage && Objective.State == EDemoObjectiveState::Active)
    {
        float BestDeviceDistSq = MAX_flt;
        for (TActorIterator<ADemoObjectiveDevice> It(GetWorld()); It; ++It)
        {
            const ADemoObjectiveDevice* Device = *It;
            if (!IsValid(Device) || Device->IsHidden() || Device->GetStatusView().bDestroyed) continue;
            const float DistSq = FVector::DistSquared(Device->GetActorLocation(), RefWorldPos);
            if (DistSq < BestDeviceDistSq)
            {
                BestDeviceDistSq = DistSq;
                PrimaryGuideWorldPos = Device->GetActorLocation();
            }
        }
        if (PrimaryGuideWorldPos.IsNearlyZero()) PrimaryGuideWorldPos = Objective.ActiveLocation;
    }
    else if (LiveBoss)
    {
        PrimaryGuideWorldPos = LiveBoss->GetActorLocation();
    }
    else if (NearestEnemy)
    {
        PrimaryGuideWorldPos = NearestEnemy->GetActorLocation();
    }

    if (bHasD03Topo && !PrimaryGuideWorldPos.IsNearlyZero())
    {
        int32 TargetCurr = INDEX_NONE, TargetNear = 0; bool bTargetLoop = false;
        ClassifyLocalPosition(*Topo, ActiveAnchor->GetActorTransform().InverseTransformPosition(PrimaryGuideWorldPos), TargetCurr, TargetNear, bTargetLoop);
        RecommendedZoneIdx = TargetCurr != INDEX_NONE ? TargetCurr : TargetNear;
    }

    if(IsR02GameplayPresentation())
    {
        const bool bBossHeader = GetR02LiveBoss() != nullptr;
        FString Compact; // 常态一行：含真实关数、地点名与完成本关所需核心目标事实。
        if(Objective.Kind==EDemoObjectiveKind::Sabotage) Compact=FString::Printf(TEXT("%s%s · 摧毁设施 %d/%d · 剩余 %02d"),*LevelPrefix,*LevelTitleSuffix,Objective.Completed,Objective.Total,State->EnemiesRemaining);
        else if(Objective.Kind==EDemoObjectiveKind::RotatingControl) Compact=FString::Printf(TEXT("%s%s · %s · %.1f/%.0f秒"),*LevelPrefix,*LevelTitleSuffix,*Title,Objective.ProgressSeconds,Objective.RequiredSeconds);
        else Compact=FString::Printf(TEXT("%s%s · 清除敌人 剩余 %02d"),*LevelPrefix,*LevelTitleSuffix,State->EnemiesRemaining);
        if(Objective.Kind!=EDemoObjectiveKind::KillAll||!bBossHeader) Label(Compact,44,132,18,Ink); // C07顶栏已含真关数；歼灭Boss不叠重复目标，设施/控制点与下方标记仍完整保留。
        if(Objective.Kind!=EDemoObjectiveKind::KillAll) R02HUDLine(44,160,280,Fraction,Ink);

        if (bHasD03Topo)
        {
            // 2026-11-19文字减负：原常驻“当前位置/推进指引”与“区域路线”两行删除，改为换区短显；推进方向由3D路标与雷达表达。
            const float ToastY = bBossHeader ? 42.f : (Objective.Kind != EDemoObjectiveKind::KillAll ? 168.f : 158.f); // Boss场左上罗马字已收起，置顶显示避免与120px小队列表重叠。
            DrawZoneEntryToast(ToastZoneKey, ToastZone ? ToastZone->Id : TEXT(""), ToastZone ? ToastZone->Name : TEXT(""), ToastY);
        }
    }
    else
    {
        Label(Title + LevelTitleSuffix, 48, 109, 18, Ink); Label(Detail, 48, 140, 15, Ink);
        if(Objective.Kind!=EDemoObjectiveKind::KillAll) R02HUDLine(48,178,310,Fraction,Ink);
        if (bHasD03Topo)
        {
            FString LocationLine, RouteLine;
            BuildZoneBreadcrumb(*Topo, CurrentZoneIdx, NearestZoneIdx, bInSideLoop, RecommendedZoneIdx, LocationLine, RouteLine);
            Label(LocationLine, 48.f, 190.f, 13.f, Amber);
        }
    }

    // 绘制全模式3D世界方向路标，确保隔墙、跨分区、高低差场景下始终有明确前进方向。
    // 2026-11-19文字减负：路标只写短名，不再带“[B区·地点名]”后缀和◇◆字符（屏内已画菱形、离屏已画箭头）；敌人与设施在屏内只留菱形。
    int32 NextIndicatorLane = 0;
    if (Objective.Kind == EDemoObjectiveKind::Sabotage && Objective.State == EDemoObjectiveState::Active)
    {
        bool bDrewAnyDevice = false;
        for (TActorIterator<ADemoObjectiveDevice> It(GetWorld()); It; ++It)
        {
            const ADemoObjectiveDevice* Device = *It;
            if (!IsValid(Device) || Device->IsHidden()) continue;
            const FDemoObjectiveDeviceView View = Device->GetStatusView();
            if (View.bDestroyed) continue;
            bDrewAnyDevice = true;
            DrawWorldDirectionIndicator(View.Anchor + FVector(0.f, 0.f, 45.f), TEXT("摧毁设施"),
                Amber, FMath::Min(NextIndicatorLane++, 4), false); // 屏内设施有血条，只留菱形。
        }
        if (!bDrewAnyDevice && !Objective.ActiveLocation.IsNearlyZero())
        {
            DrawWorldDirectionIndicator(Objective.ActiveLocation + FVector(0.f, 0.f, 150.f),
                TEXT("摧毁设施"), Amber, FMath::Min(NextIndicatorLane++, 4)); // 设施Actor尚未复制时没有血条，屏内仍写短名。
        }
    }
    else if (Objective.Kind == EDemoObjectiveKind::RotatingControl && Objective.State == EDemoObjectiveState::Active && !Objective.ActiveLocation.IsNearlyZero())
    {
        DrawWorldDirectionIndicator(Objective.ActiveLocation + FVector(0.f, 0.f, 150.f),
            TEXT("控制点"), Amber, FMath::Min(NextIndicatorLane++, 4));
    }
    else if (Objective.Kind == EDemoObjectiveKind::KillAll)
    {
        const ADemoEnemy* GuideEnemy = LiveBoss ? LiveBoss : NearestEnemy;
        if (GuideEnemy)
        {
            DrawWorldDirectionIndicator(GuideEnemy->GetActorLocation() + FVector(0.f, 0.f, 110.f),
                GuideEnemy->IsBoss() ? TEXT("首领") : TEXT("敌人"),
                Ember, FMath::Min(NextIndicatorLane++, 4), false); // 屏内敌人本身可见，不在准星附近写字；隔墙时菱形仍标出位置。
        }
        else if (bHasD03Topo && State->EnemiesRemaining > 0)
        {
            // 波次刷新间隙尚未复制出敌人Actor时，指向下一推进分区中心，避免玩家原地迷茫。
            const int32 ActiveIdx = CurrentZoneIdx != INDEX_NONE ? CurrentZoneIdx : NearestZoneIdx;
            const int32 NextIdx = FMath::Min(ActiveIdx + 1, Topo->ZoneCount - 1);
            const FDemoTacticalZoneDef& NextZone = Topo->Zones[NextIdx];
            const FVector LocalZoneCenter((NextZone.MinX + NextZone.MaxX) * .5f, (NextZone.MinY + NextZone.MaxY) * .5f, 120.f);
            const FVector WorldZoneCenter = ActiveAnchor->GetActorTransform().TransformPosition(LocalZoneCenter);
            DrawWorldDirectionIndicator(WorldZoneCenter,
                FString::Printf(TEXT("前往 %s区"), NextZone.Id),
                Amber, FMath::Min(NextIndicatorLane++, 4));
        }
    }

    // G键教学集中到首次操作提示；保留下面真实队伍标记及任务进度，避免每关重复提醒。
    const UDemoTeamPingComponent* Pings=State->GetTeamPingComponent(); // GS公共有界快照，本地HUD不自行裁定目标或发RPC。
    int32 PingLane = FMath::Max(1, NextIndicatorLane); // 当前公开数组最多四人，稳定顺序沿边错开，不改变权威标记和生命周期。
    if(Pings)
        for(const FDemoTeamPing& Ping:Pings->GetSnapshot())
        {
            FVector Location; // 本帧借用位置；过期与跨关由组件过滤，至多四个投影，不因背后投影失败直接丢失方向。
            if(!Pings->ResolveDisplayLocation(Ping,Location)) continue;
            const FString Name=Ping.Sender?Ping.Sender->GetPlayerName().Left(16):TEXT("队友"); // 限制远端名字长度，防止覆盖整屏。
            DrawWorldDirectionIndicator(Location, FString::Printf(TEXT("◇ %s · %s"),Ping.bEnemy?TEXT("敌人"):TEXT("地点"),*Name), Ink, FMath::Min(PingLane++, 4)); // 类型置前，镜内缩短名字时仍能区别敌人和地点；真实姓名及位置不改。
        }
}

void ADemoHUD::DrawZoneEntryToast(int32 ZoneKey, const TCHAR* ZoneId, const TCHAR* ZoneName, float Y)
{
    DEMO_LOG_TICK();
    // 设计：常驻面包屑改为“换区时短显一次”，玩家进入新分区得到地点名，其余时间左上只留目标行。
    // 边界：只读静态拓扑表名称，不影响导航/目标；单人暂停时World时间冻结，短显随画面一起停住。
    const UWorld* World = GetWorld();
    if (!World) return;
    const double Now = World->GetTimeSeconds(); // 本地World秒，仅用于显示计时。
    if (ZoneKey != INDEX_NONE && ZoneKey != ZoneToastKey)
    {
        ZoneToastKey = ZoneKey;
        ZoneToastText = FString::Printf(TEXT("%s区 · %s"), ZoneId ? ZoneId : TEXT(""), ZoneName ? ZoneName : TEXT(""));
        ZoneToastUntil = Now + 2.5; // 可读停留2.5秒，含末段0.6秒淡出。
        UE_LOG(LogFPSDemo, Log, TEXT("Zone entry toast: key=%d text=%s"), ZoneKey, *ZoneToastText);
    }
    if (Now >= ZoneToastUntil || ZoneToastText.IsEmpty()) return;
    const float Alpha = FMath::Clamp(static_cast<float>((ZoneToastUntil - Now) / 0.6), 0.f, 1.f); // 末0.6秒线性淡出。
    Label(ZoneToastText, 44.f, Y, 15.f, FLinearColor(FColor(190, 148, 85)).CopyWithNewOpacity(Alpha));
}
