// 职责：房间白名单及默认关闭的迁移开关。Project Settings配置，服务器BuildRequest；依赖真实Editor地图和显式Cook规则。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "World/Rooms/DemoRoomStreamingTypes.h"
#include "DemoRoomStreamingSettings.generated.h"
class UDemoRoomAtmosphereProfile; // 本机纯视觉DataAsset，不进入流送请求或存档。

/** 一个稳定RoomId对应的作者配置；不允许请求端覆盖地图或必要条件。 */
USTRUCT()
struct FPSDEMO_API FDemoRoomStreamDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Room") TSoftObjectPtr<UWorld> Map; // 真实/Game子关卡；目录必须在Cook清单。
    UPROPERTY(EditAnywhere, Category="Room") bool bRequireNavigation = true; // 此房间有敌人时保持开启。
    UPROPERTY(EditAnywhere, Category="Room", meta=(ClampMin="0", ClampMax="5000", Units="cm")) float PlayerEntryExclusionRadiusCm = 0.f; // 敌人中心至任一固定队员战斗落点的最小水平距离，cm；0沿用原布点。由安装目录读取，非网络请求参数。
    UPROPERTY(EditAnywhere, Category="Room") TArray<FName> RequiredActorTags; // 最多16个不重复的关键场景Actor标签。
    UPROPERTY(EditAnywhere, Category="Room") bool bUseAuthoredBounds = false; // 新房间显式启用真实占地/标记校验；旧配置默认保留40m锚点与椭圆。
    UPROPERTY(EditAnywhere, Category="Room") FVector LocalBoundsMin = FVector(-4000,-4000,-1000); // 本图碰撞几何的保守局部AABB下界cm，作者实读；非世界坐标。
    UPROPERTY(EditAnywhere, Category="Room") FVector LocalBoundsMax = FVector(4000,4000,1000); // 保守AABB上界cm，所有关键点/碰撞件必须被包含；不可用扩大值掩盖错误地图。
    UPROPERTY(EditAnywhere, Category="Room") TArray<FName> SpawnPointTags; // 新房间恰8个唯一作者标记tag，顺序冻结候选轮换；允许稳定ID中的点号，不接受路径。
    UPROPERTY(EditAnywhere, Category="Room") FName BossSpawnTag; // 唯一Boss作者标记tag；新房间所有关都验证，Boss是否生成仍由原Plan决定。
};

/**
 * ROOM-EXPOSURE-20261119：流送战斗房间的本机有界曝光档。房间锚点BeginPlay在各端读取，生成仅覆盖该房间实际几何包络的
 * PostProcess组件；不复制、不保存、不改Hub有界Volume/太阳/灯强度。只覆盖下列曝光字段，其余PP（Bias/Bloom/Lumen等）沿用全局默认。
 */
USTRUCT()
struct FPSDEMO_API FDemoRoomExposureProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Exposure") bool bEnabled = true; // false时房间不生成曝光组件，完全回到全局默认Histogram(−10..20EV100、SpeedDown1)。
    UPROPERTY(EditAnywhere, Category="Exposure", meta=(ClampMin="-10", ClampMax="20")) float MinEV100 = 3.5f; // 暗场景最亮曝光下限EV100（ExtendDefaultLuminanceRange开）；防止仅局部灯的暗房被抬成灰白，值越大越暗。
    UPROPERTY(EditAnywhere, Category="Exposure", meta=(ClampMin="-10", ClampMax="20")) float MaxEV100 = 8.f; // 亮场景最暗曝光上限EV100；必须大于MinEV100，否则引擎直接跳到目标且视为无效档。
    UPROPERTY(EditAnywhere, Category="Exposure", meta=(ClampMin="0.1", ClampMax="20")) float SpeedUpEVPerSecond = 6.f; // 暗→亮适应速度EV/s；全局默认3。
    UPROPERTY(EditAnywhere, Category="Exposure", meta=(ClampMin="0.1", ClampMax="20")) float SpeedDownEVPerSecond = 6.f; // 亮→暗适应速度EV/s；全局默认1会使Hub进战斗房间需数秒变亮，与Hub已接受的6一致。
    UPROPERTY(EditAnywhere, Category="Exposure", meta=(ClampMin="0", ClampMax="2000")) float BoundsMarginCm = 300.f; // 实际阻挡几何AABB向外扩展cm，覆盖屋檐/远景外沿；房间槽相距50000cm，不会进入Hub或另一槽。
    UPROPERTY(EditAnywhere, Category="Exposure", meta=(ClampMin="0", ClampMax="2000")) float BlendRadiusCm = 300.f; // Box外混合带cm，与Hub Volume同值。
    UPROPERTY(EditAnywhere, Category="Exposure") float Priority = 1.f; // PP优先级；低于Hub的10，两者空间本就不重叠。
};

/** 关闭时由调用方沿用旧区域；开启后任何缺配置都明确失败，不回退白模。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Demo Room Streaming"))
class FPSDEMO_API UDemoRoomStreamingSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Rooms") bool bEnabled = false; // 默认保持旧地图入口，真实地图准备完成后显式打开。
    UPROPERTY(Config, EditAnywhere, Category="Rooms", meta=(ClampMin="2")) int32 MinimumContentRevision = 2; // 新作者内容从修订2启用；V9冻结修订1仍使用原地图。
    UPROPERTY(Config, EditAnywhere, Category="Rooms") TMap<FName, FDemoRoomStreamDefinition> Rooms; // 最多32个稳定ID，所有端读取同一打包配置。
    UPROPERTY(Config, EditAnywhere, Category="Rooms") TArray<FVector> SlotLocations = { FVector(50000,0,10000), FVector(65000,0,10000) }; // 恰2个cm空间槽，默认旧值；大图由安装配置显式隔离，所有端同值，不由RPC任意指定。
    UPROPERTY(Config, EditAnywhere, Category="Exposure") FDemoRoomExposureProfile RoomExposure; // 所有流送房间共用的本机曝光档；纯视觉，各端独立读取同一打包配置，不进网络协议/存档。
    UPROPERTY(Config, EditAnywhere, Category="Atmosphere") TSoftObjectPtr<UDemoRoomAtmosphereProfile> DefaultAtmosphere; // 预留幕次推广；Phase A不消费此默认档，必须保持空，不能波及旧房。
    UPROPERTY(Config, EditAnywhere, Category="Atmosphere") TMap<FName, TSoftObjectPtr<UDemoRoomAtmosphereProfile>> RoomAtmospheres; // Phase A仅消费D03_L01显式键；坏包REJECT，不静默降级。
    UPROPERTY(Config, EditAnywhere, Category="Atmosphere") TMap<FName, FName> RoomActs; // 预留显式幕次映射；Phase A不消费、不猜幕次。
    UPROPERTY(Config, EditAnywhere, Category="Atmosphere") TMap<FName, TSoftObjectPtr<UDemoRoomAtmosphereProfile>> ActAtmospheres; // 预留幕次档；本轮保持空，推广需独立放行。
    /** Anchor在可信Level身份就绪后只解析显式L01；范围外或缺包/非法返回Error，其他房直接走旧BeginPlay路径。 */
    const UDemoRoomAtmosphereProfile* ResolveAtmosphere(FName RoomId, FString& Error) const;
    /** Phase A仅可信D03_L01且存在显式Room绑定时启用新生命周期；默认/Act档不能隐式扩大本轮范围。 */
    bool UsesPhaseAAtmosphere(FName RoomId) const;
    /** 服务器准备入口：输入已冻结的房间/代次/旧索引，输出规范请求或明确原因；不加载地图。 */
    bool BuildRequest(FName RoomId, int32 Generation, int32 ArenaIndex, FDemoRoomStreamRequest& Out, FString& Error) const;
    /** 各端调用前验证请求与本地白名单完全相同；不根据网络路径任意加载。 */
    bool ValidateRequest(const FDemoRoomStreamRequest& Request, FString& Error) const;
    /** 当前活动Transform必须属于本地安装槽；为Request选择另一槽并重新完整校验，不以代次奇偶覆盖脚下房间。 */
    bool UseOtherSlot(const FTransform& ActiveTransform, FDemoRoomStreamRequest& Request, FString& Error) const;
    /** 读取可信目录的有限局部占地，旧房间返回保守默认盒；只供准备/回收验证，不反向改变作者点。 */
    bool GetLocalBounds(FName RoomId, FBox& Out, FString& Error) const;
};
