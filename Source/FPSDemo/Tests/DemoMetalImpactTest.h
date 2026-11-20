// 职责：金属命中端到端与固定池压力验收；仅Editor显式-DemoMetalImpactTest启动，使用本GI临时存档；依赖真实子弹/GAS/近战，截图可加-DemoMetalImpactCapture。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/Feedback/DemoMetalImpactSubsystem.h"
#include "DemoMetalImpactTest.generated.h"

UCLASS(Transient, NotBlueprintable)
class ADemoMetalImpactTest : public AActor
{
    GENERATED_BODY()
public:
    /** 仅显式开发入口创建，逐帧等实际事件，不阻塞引擎。 */
    ADemoMetalImpactTest();
    /** DeltaSeconds为引擎游戏秒；运行上限30秒，失败非零退出，不写正式进度。 */
    virtual void Tick(float DeltaSeconds) override;
private:
#if WITH_EDITOR
    /** 独立 -game 进程额外传 -DemoHitFxReel 时逐帧录制真实第一关手枪命中，使用本 GI 临时槽。 */
    void TickHitFxReel();
    FString ReelDirectory; // 单次进程的实际 RHI 帧目录，日志记录绝对路径。
    int32 ReelFrame=0; // 第一张起连续编号，按启动参数固定 60 Hz。
#endif
    /** Value断言，Message独立预期；失败停止夹具并非零退出。 */
    bool Check(bool Value, const TCHAR* Message);
    /** Delay为下一步World秒等待，递增步骤，不创建悬空Timer/Lambda。 */
    void Advance(float Delay);
    /** Name为Saved/MetalImpactAcceptance内叶名；仅显式截图参数且RHI存在时调用。 */
    void Capture(const TCHAR* Name);
    int32 Step = 0; // 此World步骤，旅行不复用。
    double NextAt = 1.; // 首秒等待生产Avatar初始化。
    double ImpactAt = -1.; // 首次真实火花出现后的截图定时，World秒。
    bool bFailed = false; // 断言失败闩锁，禁止继续修改夹具。
    bool bCaptured = false; // 本次真实击中仅截图一次。
    TWeakObjectPtr<class ADemoEnemy> Target; // World拥有的高血量生产怪物，测试不持有其ASC生命周期。
    TWeakObjectPtr<class ACameraActor> PreviewCamera; // 本机外部观察镜头，仅用于近战接触特效截图，不改生产人物/动作。
    FVector BodyPoint = FVector::ZeroVector; // 生产PhysicsAsset查询出的模型空间点，供真实枪口瞄准。
    float BeforeHealth = 0.f; // 每阶段实际血量快照，限流不得改变伤害。
    FDemoMetalImpactStats Before; // 阶段前纯值计数，避免将其他阶段事件误算进断言。
    int32 PoolComponents = 0; // 压力前实际Actor组件数量，不只相信内部统计。
};
