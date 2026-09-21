#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoEnemyAttackTest.generated.h"
class ADemoEnemy;

/** -DemoEnemyAttackTest专用World回归：真实碰撞/计时器/GE/GA，不向正常游戏注入测试状态。 */
UCLASS(Transient, NotBlueprintable)
class ADemoEnemyAttackTest : public AActor
{
    GENERATED_BODY()
public:
    /** 开启低频测试状态机，成功/失败以进程退出码报告。 */
    ADemoEnemyAttackTest();
    /** DeltaSeconds为帧间秒数；各步骤在真实World等待而非手工调用技能结束回调。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** -DemoSpawnSystemTest：真实组件队列、区域、取消、阻挡重试、异常销毁与奖励权限边界。 */
    void TickSpawnSystemTest();
    /** 组件事件同步回调；Enemy/Reason只在调用栈借用，计数用于验证一次性通知。 */
    void OnTestSpawnDefeated(ADemoEnemy* Enemy);
    void OnTestSpawnCleared();
    void OnTestSpawnFailed(const FString& Reason);
    /** 清理/伤害本测试Owner生成的敌人，bKill=false只停止Tick，Count限制真实死亡次数。 */
    void ProcessTestSpawnEnemies(bool bKill, int32 Count = MAX_int32);
    UPROPERTY() TObjectPtr<class UDemoEnemySpawnComponent> TestSpawner; // 测试Actor拥有并注册，随World卸载。
    int32 SpawnClears = 0, SpawnFailures = 0, SpawnKills = 0; // 只在本专项递增，普通敌人测试不使用。
    /** -DemoCloseCombatTest：实际生产混编、近战前摇、直线冲刺碰撞与取消。 */
    void TickCloseCombatTest();
    int32 CloseScenario=0; // 独立场景编号：近战命中/走开/冰冻、突进命中/侧闪/墙/阶段/冲刺窗口/死亡。
    float CloseBeforeHealth=0.f; // 当前场景玩家实际健康快照，用于精确一次伤害断言。
    float CloseStartTime=0.f; // 起手观察World秒，验证前摇不会提前结算。
    FVector CloseLockedDirection=FVector::ZeroVector; // 起手方向值快照，验证玩家移动后不追踪。
    /** -DemoEnemyHitZoneTest：真PhysicsAsset射线、三部位GAS伤害、护板开合及元素/穿透回归。 */
    void TickHitZoneTest();
    // 测试通过真实骨骼查询找到的稳定模型空间命中点；索引0机身、1手臂、2核心，不跨关卡保留。
    FVector HitZonePoints[3]={FVector::ZeroVector,FVector::ZeroVector,FVector::ZeroVector};
    float HitZoneHealth=0.f; // 当前步骤伤前生命值，供异步动画/周期效果后的结算断言。
    /** -DemoEnemyAnimationTest：真实World/AnimBP/GAS任务、骨姿势、冻结、优先级与死亡生命周期。 */
    void TickAnimationTest();
    FVector AnimationBone=FVector::ZeroVector; // 首次播放前forearm_l模型空间位置，验证姿势真的变化。
    /** -DemoBossDiveTest专项：真实GA/GE/扫掠、无敌/旧DOT/新Debuff、落地与取消。 */
    void TickDiveTest();
    int32 DiveScenario=0; // 当前独立场景：命中/冲刺/出圈/取消/阶段取消/升空死亡/动态阻挡/致命命中。
    float DiveHealth=0.f; // 悬停开始实际HP快照，验证持续伤害未执行。
    float DiveHoverTime=0.f; // 悬停开始World秒，验证3秒不提前结束。
    bool bDiveDashSent=false; // 本次俯冲只触发一次真实Dash GA，不连续刷新窗口。
    /** -DemoEnemyTacticsTest：真实配置混编、拉距侧移、GAS冰冻、二阶段与固定前摇回归。 */
    void TickTacticsTest();
    /** -DemoEnemyNavigationTest专项：真实动态掩体、路径跟随、隔墙近距Boss、阶段与前摇冻结。 */
    void TickNavigationTest();
    double NavigationDeadline = 0; // 本步骤真实时间上限秒，卡住明确失败，不用无限等待掩盖导航问题。
    /** Condition为断言，Message为可搜索用例说明；失败立即请求非零退出。 */
    bool Check(bool Condition, const TCHAR* Message);
    /** Delay为到下一步骤的World秒数，同时递增步骤。 */
    void Advance(float Delay);
    /** Location为厘米坐标，bBoss指定行为；测试自有敌人不加入GM战役注册表，不污染经济。 */
    ADemoEnemy* SpawnEnemy(const FVector& Location, bool bBoss);
    // 仅测试Actor拥有流程状态；所有世界对象弱引用，销毁后不能被测试保活。
    int32 Step = 0;
    float NextTime = 1.f;
    bool bFailed = false;
    TWeakObjectPtr<ADemoEnemy> Shooter;
    TWeakObjectPtr<ADemoEnemy> Boss;
    TWeakObjectPtr<AActor> Wall;
    // Boss开始前摇的世界位置，用于检查2秒内没有移动。
    FVector BossStart = FVector::ZeroVector;
};
