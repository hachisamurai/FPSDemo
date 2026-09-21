#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoEnemyNavigation.generated.h"

/** 平面悬浮敌人的NavMesh路径跟随；由敌人Tick显式驱动，施法/死亡时不会独立移动。 */
UCLASS(ClassGroup=(Demo), meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoEnemyNavigation : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无独立Tick；所属Enemy控制导航与攻击的执行顺序。 */
    UDemoEnemyNavigation();
    /** Target为本帧活玩家；Range为停步距离cm，Speed为cm/s，DeltaSeconds为World帧秒。 */
    void Follow(class ADemoCharacter* Target, float Range, float Speed, float DeltaSeconds);
    /** Goal为战术世界位置cm；Target用于候选射击视线验证；Speed为cm/s、DeltaSeconds为World秒，失败返回false供追击回退。 */
    bool MoveTo(const FVector& Goal, class ADemoCharacter* Target, float Speed, float DeltaSeconds);
    /** Reason为稳定诊断键；暂停追击并丢弃旧路线，恢复时重新规划，不瞬移。 */
    void Stop(FName Reason);
    /** Target借用当前玩家，使用Visibility检查实际视线；防止距离够近却隔墙停步。 */
    bool CanSee(const class ADemoCharacter* Target) const;
    /** 测试/HUD只读状态，包含Following、InRange、Blocked、NoPath、NoNavigation等。 */
    FName GetStatus() const;
private:
    /** Speed/DeltaSeconds为本帧允许位移；追击与战术路线共用Sweep，不能通过横移绕过碰撞。 */
    void AdvancePath(float Speed, float DeltaSeconds);
    bool bPointMode = false; // 区分玩家追击和固定战术点，切换时丢弃不兼容的旧路线。
    /** Target/Range定义接近点；必要时搜索附近可见射击点；失败按重试间隔保留原地。 */
    bool Repath(const class ADemoCharacter* Target, float Range);
    /** Goal为地面世界位置cm；OutPoints接收完整可达路径，拒绝跨岛部分路径。 */
    bool FindRoute(const FVector& Goal, TArray<FVector>& OutPoints) const;
    /** NewStatus只有改变时记录普通日志，避免逐帧重复刷屏；接口调用仍记高频日志。 */
    void SetStatus(FName NewStatus);
    /** 调试开关打开时绘制实际缓存路径和状态，不影响寻路。 */
    void DrawNavigation() const;
    TArray<FVector> Points; // 本组件拥有地面路径点值，不持有世界Actor或共享导航路径。
    int32 PointIndex = 0; // 当前待到达路径点，重规划后从首个拐点开始。
    FVector LastGoal = FVector::ZeroVector; // 上次玩家地面位置，用于目标位移触发重规划。
    float NextRepathTime = 0.f; // World秒；失败/受阻最多每0.75秒重试一次。
    float NextRefreshTime = 0.f; // World秒；即使目标静止也每3秒刷新路径以响应动态障碍。
    float BlockedSeconds = 0.f; // 连续未取得有效移动的World秒，达到0.6秒触发重规划。
    FName Status = TEXT("Idle"); // 只属于本地权威实例的诊断状态，不复制/不写存档。
};
