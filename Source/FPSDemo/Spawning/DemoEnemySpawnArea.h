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
