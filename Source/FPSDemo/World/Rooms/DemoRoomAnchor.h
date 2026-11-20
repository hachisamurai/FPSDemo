// 职责：一个流送房间唯一的作者锚点和服务器准备令牌；地图放置，继承Actor避免注册到旧0..2区域服务。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Spawning/DemoEnemySpawnArea.h"
#include "DemoRoomAnchor.generated.h"
struct FDemoAreaSnapshot; // 输出纯值快照，由旧区域服务与Flow共用。

UCLASS(Blueprintable)
class FPSDEMO_API ADemoRoomAnchor : public AActor
{
    GENERATED_BODY()
public:
    /** 创建无碰撞根节点及客户端视觉Tick；地图必须恰好放置一个，Tick不参与玩法或复制。 */
    ADemoRoomAnchor();
    /** 复制准备令牌，不复制静态作者几何；后者由同版地图保证。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(EditAnywhere, Category="Room") FDemoSpawnGeometry Geometry; // 本锚点局部cm的敌人生成几何。
    UPROPERTY(EditAnywhere, Category="Room") FVector CombatPlayerOffset = FVector(-1100, 0, 100); // 玩家胶囊中心入场位置cm。
    UPROPERTY(EditAnywhere, Category="Room") FVector PreparedPlayerOffset = FVector(-650, 0, 100); // 关间恢复入场中心cm。
    UPROPERTY(EditAnywhere, Category="Room") FVector ShopOffset = FVector(0, -300, 0); // 升级终端地面位置cm。
    UPROPERTY(EditAnywhere, Category="Room") FVector NextOffset = FVector(0, 300, 0); // 下一关终端地面位置cm。
    UPROPERTY(EditAnywhere, Category="Room") TArray<FVector> DeviceOffsets; // 恰好两个设施地面锚点，局部cm。
    UPROPERTY(EditAnywhere, Category="Room") TArray<FVector> ControlOffsets; // 恰好三个控制点地面锚点，局部cm。
    UPROPERTY(Replicated) int32 PreparedGeneration = 0; // 服务器通过房间/导航检查后写入；客户端同代才承认复制就绪。
    /** 显示后读取作者配置；AuthoredLocalBounds为可信目录的真实局部空间预算，空时保留旧40m规则；失败不返回半份快照。 */
    bool MakeSnapshot(FDemoAreaSnapshot& Out, FString& Error, const FBox* AuthoredLocalBounds=nullptr) const;
    /** 服务器在准备成功后提交正数令牌，重复值幂等；不会激活战斗。 */
    void SetPreparedGeneration(int32 Generation);
    /** 回收前只读本机相机是否仍在本PhaseA PP混合范围；legacy恒false，不改变玩法/复制，仅延迟安全卸载。 */
    bool HasLocalAtmosphereView() const;
protected:
    /** BeginPlay立即保留旧四字段曝光；身份就绪后只有显式L01接管，专服跳过，不改变旧房间视觉时序。 */
    virtual void BeginPlay() override;
    /** 每帧PostUpdateWork读取流送身份/本机实际相机；更新有界PP启用及局部雾，DeltaSeconds不决定玩法。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 卸载/退出显式注销自己拥有的Transient组件，移除覆写恢复原Hub混合，不写全局参数。 */
    virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
private:
    /** 原四字段曝光原算法；BeginPlay创建并随Level保持，不依赖新Active/退役门或Profile路径。 */
    void SetupRoomExposure();
    /** 可信流送身份出现时只初始化一次；非PhaseA停止本Actor视觉Tick，保留原曝光组件。 */
    void ResolveAtmosphereScope();
    /** 接管/卸载精确撤自己的PP和Shape；Reason仅日志，不改Hub/相机/全局。 */
    void DestroyExposure(const TCHAR* Reason);
    /** 显式L01身份就绪时一次构建：扫描本Level几何，校验Profile/空间隔离，失败不影响准备事务但不静默降级。 */
    void SetupRoomAtmosphere(FName RoomId, const FTransform& SlotTransform);
    /** 已提交(含等相机离开的退役)才启用有界PP，全部本机相机入房才注册雾，取消候选不激活。 */
    void UpdateLocalAtmosphere(bool bRoomCommitted);
    /** EndPlay/退出时移除本锚点自己的雾；Reason仅用于日志，不保存或修改Hub。 */
    void RestoreLocalFog(const TCHAR* Reason);
    UPROPERTY(Transient) TObjectPtr<class UBoxComponent> ExposureBounds; // 世界轴对齐、无碰撞的曝光范围壳；随Actor/Level卸载销毁，不复制不保存。
    UPROPERTY(Transient) TObjectPtr<class UPostProcessComponent> ExposureVolume; // Shape有界PP；legacy随Level，PhaseA须已提交且退役保留，不复制不保存。
    UPROPERTY(Transient) TObjectPtr<class UDemoRoomAtmosphereProfile> ResolvedProfile; // 本锚点持有已加载配置，防止软引用包被GC；只读消费。
    UPROPERTY(Transient) TObjectPtr<class ULocalFogVolumeComponent> LocalFog; // 本机入房期间唯一球雾；离开即销毁，绝不全局高度雾。
    FBox RoomGeometryBounds = FBox(ForceInit); // 保存后实际Level阻挡网格世界cm AABB，用于相机/雾包含判断。
    bool bAtmosphereAttempted = false; // 一次身份就绪后构建；非法资产不逐帧重复加载/刷日志。
    bool bUsingPhaseA = false; // 只有显式D03_L01接管后true；其他房间保原BeginPlay曝光与Level生命周期。
    bool bAtmosphereActive = false; // 上次视觉Active状态，仅用于边缘日志，不参与战斗提交。
};
