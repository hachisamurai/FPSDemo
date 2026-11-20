// 职责：旧区域作者锚点与出生几何值；流送房间可追加本次准备的显式地面点，旧资产继续使用原椭圆。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoEnemySpawnArea.generated.h"

/** 相对区域Actor的出生几何；默认保持现有三块白模布局，开关时冻结。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoSpawnGeometry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Spawn") FVector2D CampaignRadius = FVector2D(650, 950); // 普通关椭圆半径cm，两轴必须>0。
    UPROPERTY(EditAnywhere, Category="Spawn") FVector CampaignOffset = FVector(350, 0, 110); // 普通小怪椭圆中心，Z为离地cm。
    UPROPERTY(EditAnywhere, Category="Spawn") FVector2D ReinforcementRadius = FVector2D(1100, 1200); // 无尽外围补怪椭圆半径cm。
    UPROPERTY(EditAnywhere, Category="Spawn") float ReinforcementHeight = 110.f; // 无尽小怪出生高度cm。
    UPROPERTY(EditAnywhere, Category="Spawn") FVector BossOffset = FVector(800, 0, 130); // 两种模式的Boss专用出生点，相对区域原点cm；受阻时尝试外圈。
    UPROPERTY(Transient) TArray<FVector> ExplicitSpawnOffsets; // 当前房间准备时由真实标记/本图地面验证冻结的局部floor-contact，最多64；不写地图/存档/网络，空时仍走旧椭圆。非空时BossOffset也是floor-contact。
    UPROPERTY(Transient) FName ExplicitSourceLevelPackage = NAME_None; // 非空显式点必须携带准备时Level的实际外包身份；纯值不保活UObject、不进地图/存档/RPC，重射线必须同源同层。
    static constexpr int32 CombatLandingSlotCount = 4; // 首个队员及三个合作槽；即使单人开场，也保留全部可能入场的位置。
    UPROPERTY(Transient) float PlayerEntryExclusionRadiusCm = 0.f; // 本场冻结的固定入场禁刷半径，世界XY厘米，0关闭；不随玩家移动，不写档或复制。
    UPROPERTY(Transient) TArray<FVector> ProtectedPlayerEntryPoints; // 启用时恰为四个玩家战斗落点的世界坐标；与本次准备及刷怪计划同寿命。
    /** 生成前及构造后检查敌人中心的世界XY距离；禁刷半径边界允许生成，活玩家距离由执行器另验。 */
    bool IsOutsidePlayerEntryZone(const FVector& WorldLocation) const;
};

/** 可直接拖入地图或派生蓝图；每个ArenaIndex最多一个，非复制、无Tick，不处理战斗。 */
UCLASS(Blueprintable)
class FPSDEMO_API ADemoEnemySpawnArea : public AActor
{
    GENERATED_BODY()
public:
    /** 创建仅编辑器可见的范围参考框，不影响碰撞或导航。 */
    ADemoEnemySpawnArea();
    UPROPERTY(EditAnywhere, Category="Spawn", meta=(ClampMin="-1", ClampMax="2")) int32 ArenaIndex = 0; // 对应DT_Levels区域编号0..2，-1为安全区，不是逻辑关卡数。
    /** 地图Actor开始运行后注册，不能在构造阶段访问世界服务。 */
    virtual void BeginPlay() override;
    /** EndPlayReason为卸载原因；先撤销锚点，防止后续请求使用旧流送对象。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    UPROPERTY(EditAnywhere, Category="Area") FVector CombatPlayerOffset = FVector(-1100, 0, 100); // 战斗入场相对位置cm。
    UPROPERTY(EditAnywhere, Category="Area") FVector PreparedPlayerOffset = FVector(-650, 0, 100); // Hub/关间读档入场相对位置cm。
    UPROPERTY(EditAnywhere, Category="Area") FVector ShopOffset = FVector(0, -300, 0); // 升级终端地面锚点cm。
    UPROPERTY(EditAnywhere, Category="Area") FVector NextOffset = FVector(0, 300, 0); // 下一关终端地面锚点cm。
    UPROPERTY(EditAnywhere, Category="Spawn") FDemoSpawnGeometry Geometry; // 设置原点/半径/高度；Actor旋转参与计算，缩放不参与，避免负缩放产生非法点。
    /** World为当前权威世界；Index匹配场景区域，无匹配沿FallbackCenter回退；输出值快照，重复区域或非法几何写Error并失败。 */
    static bool Resolve(UWorld* World, int32 Index, const FVector& FallbackCenter, FTransform& OutTransform, FDemoSpawnGeometry& OutGeometry, FString& Error);
private:
    UPROPERTY(VisibleAnywhere, Category="Spawn") TObjectPtr<class UBoxComponent> Preview; // World拥有的参考框，仅提示旧白模范围，实际出生参数由Geometry决定。
};
