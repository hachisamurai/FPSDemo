#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Spawning/DemoEnemySpawnArea.h"
#include "DemoEncounterAreaSubsystem.generated.h"

/** 当前区域的只读值快照；没有Actor引用，入场后不随地图编辑改变。 */
struct FPSDEMO_API FDemoAreaSnapshot
{
    FTransform Transform; // 地面原点和旋转，忽略Actor缩放，单位cm。
    FDemoSpawnGeometry Geometry; // 已验证的敌人出生几何。
    FVector CombatStart; // 玩家战斗入场世界位置cm。
    FVector PreparedStart; // 安全区/检查点入场世界位置cm。
    FVector ShopAnchor; // 升级终端的地面世界坐标cm，生成时另行射线对地。
    FVector NextAnchor; // 出发终端的地面世界坐标cm。
};

/** 每World区域注册/查询服务；不保存战局阶段、玩家或GameMode引用。 */
UCLASS()
class FPSDEMO_API UDemoEncounterAreaSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** WorldType仅Game/PIE创建，不在资产预览或编辑器场景自动生成地形。 */
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    /** Collection为当前World集合；只初始化索引，不要求GameMode/地图Actor已就绪。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** World卸载时独立清理弱引用，不假设其他Actor/组件的销毁先后。 */
    virtual void Deinitialize() override;
    /** Area为本World锚点，BeginPlay登记；重复通知幂等，区号冲突在查询时拒绝。 */
    void RegisterArea(ADemoEnemySpawnArea* Area);
    /** Area退出/卸载时撤销；不销毁地图拥有的Actor。 */
    void UnregisterArea(ADemoEnemySpawnArea* Area);
    /** 权威玩法显式请求准备地图；验证四区域或生成完整回退地形，失败撤销本次生成物。 */
    bool PrepareAreas();
    /** 已完成本World地形准备；只读查询，不代表任意流送区域均已可用。 */
    bool IsReady() const;
    /** Index为-1安全区/0..2战斗区，Error返回冲突/无效几何；FallbackCenter仅兼容旧区域测试/调用。 */
    bool ResolveArea(int32 Index, FDemoAreaSnapshot& OutArea, FString& Error, const FVector* FallbackCenter = nullptr) const;
    /** Index为-1..2；旧UI/测试位置查询适配，非法配置记日志并返回默认布局中心。 */
    FVector GetAreaCenter(int32 Index) const;
private:
    TSet<TWeakObjectPtr<ADemoEnemySpawnArea>> Areas; // World拥有锚点；弱引用不延长流送Actor生命。
    TSet<int32> AuthoredIndices; // 本World曾登记的区号；卸载后拒绝默认坐标，直到对应锚点重新注册。
    TArray<TWeakObjectPtr<AActor>> GeneratedGeometry; // 仅记录本服务生成的回退地形，失败可完整撤销。
    bool bAreasReady = false; // 本World地形准备完成，跨旅行不保留。
    /** 只删除服务自己的回退几何，不碰地图原有网格。 */
    void ClearGeneratedGeometry();
};
