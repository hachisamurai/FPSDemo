// 职责：服务器视线标记与全队只读复制。使用：拥有者PC提交无参数意图，GS组件解析真实射线；不造成伤害或写存档。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "DemoTeamPingComponent.generated.h"

class ADemoPlayerController;
class ADemoPlayerState;
class UDemoCombatSession;

/** 有界公开表现值；位置来自服务器射线，Sender不携带私人经济，Target未映射时使用Location。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoTeamPing
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<ADemoPlayerState> Sender; // 公开标记者身份，服务器每PS最多一项。
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Target; // 可选实际命中的活敌人；不接受客户端指定，销毁后自动失效。
    UPROPERTY(BlueprintReadOnly) FVector_NetQuantize Location = FVector::ZeroVector; // 权威命中/最大距离落点，单位世界cm。
    UPROPERTY(BlueprintReadOnly) FVector_NetQuantize TargetOffset = FVector::ZeroVector; // 敌人跟随的世界相对偏移cm，仅用于HUD位置。
    UPROPERTY(BlueprintReadOnly) float ExpiresAtServerTime = 0.f; // GS服务器时间秒，创建后5秒失效，客户端不得延长。
    UPROPERTY(BlueprintReadOnly) int32 Serial = 0; // 本World正序号，只用于表现去重，不作为游戏权限。
    UPROPERTY(BlueprintReadOnly) int32 LevelNumber = 0; // 原关号，复制阶段乱序时隐藏旧关标记。
    UPROPERTY(BlueprintReadOnly) bool bEnemy = false; // 敌人或地点标记；目标Actor暂未映射不改变种类。
};

// 本地纯表现通知，数组可同步查询；无参数避免订阅者跨回调保存数组引用。
DECLARE_MULTICAST_DELEGATE(FDemoTeamPingsChanged);

UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoTeamPingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** GS默认子对象，复制公共快照，无Tick；仅有标记/待发布时启用4Hz定时器。 */
    UDemoTeamPingComponent();
    /** Sender为RPC真实拥有者；仅权威Combat存活已登记玩家，服务器按本人视线射线决定结果，1秒限频。 */
    bool RequestPing(ADemoPlayerController* Sender);
    /** 当前已发布最多4项；HUD仍用ResolveDisplayLocation过滤过期/跨关/未就绪对象。 */
    const TArray<FDemoTeamPing>& GetSnapshot() const;
    /** Ping借用当前快照；OutLocation为本机显示世界cm，失效返回false，不发网络请求或改规则。 */
    bool ResolveDisplayLocation(const FDemoTeamPing& Ping, FVector& OutLocation) const;
    /** 引擎复制注册，只复制最终快照，不复制限频表或服务器运行集合。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    /** World退出时停止定时器并释放引用；CombatSession清理弱绑定不延长组件生命周期。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    FDemoTeamPingsChanged OnChanged; // HUD可订阅后立即查快照，避免错过首次复制。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Ping", meta=(ClampMin="100", ClampMax="100000")) float TraceDistance = 20000.f; // 最大射线长度cm，非法配置拒绝，不随客户端输入变化。
private:
    /** 服务器4Hz生命周期检查：到期、死亡、离线或目标消失移除；仅有变化才发布。 */
    void Maintain();
    /** 在当前游戏秒Now清理失效身份/标记，不操作钱包；返回是否改变ActivePings。 */
    bool Prune(float Now);
    /** 合并同一250ms窗口请求，最多4Hz复制/本机通知；立即到期由HUD时钟过滤。 */
    void PublishIfDue(float Now);
    /** 首个请求后启用同World UObject定时器；不捕获裸Actor或依赖Actor Tick。 */
    void EnsureMaintenance();
    /** CombatSession撤销时同步停止旧关标记，最终空快照经同一限频通道发布。 */
    void OnCombatEnded();
    /** 客户端复制和服务器发布共用通知，只驱动表现不反向发RPC。 */
    UFUNCTION() void OnRep_Snapshot();
    UPROPERTY(ReplicatedUsing=OnRep_Snapshot) TArray<FDemoTeamPing> Snapshot; // 全队公共最多4项，不写检查点。
    UPROPERTY(Transient) TArray<FDemoTeamPing> ActivePings; // 权威待发布集合，与Snapshot分开合并突发请求。
    TMap<TWeakObjectPtr<ADemoPlayerState>, float> NextAllowedAt; // 每PS下一次允许的游戏秒，死亡/换Pawn不绕过限频。
    TWeakObjectPtr<UDemoCombatSession> Session; // 本次战斗凭据，撤销后旧标记立即无效。
    FTimerHandle MaintenanceTimer; // World拥有，EndPlay清除；无存活项/限频记录时停止。
    float LastPublishedAt = -1.f; // 上次公共快照发布时间秒，首个请求可立即发布。
    int32 LastSerial = 0; // 本World序号，达到int32上限后从1开始；标记最大存活5秒。
    bool bDirty = false; // 等待发布的真实状态变化，空闲定时器不广播。
    bool bStopping = false; // EndPlay后拒绝迟到RPC/会话清理回调。
};
