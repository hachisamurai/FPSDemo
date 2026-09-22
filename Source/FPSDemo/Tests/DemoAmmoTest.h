#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoAmmoTest.generated.h"

/** 显式-DemoAmmoTest隔离回归；真实终端、存档、GAS周期和实体弹，不访问正式云账号。 */
UCLASS(Transient,NotBlueprintable)
class ADemoAmmoTest : public AActor
{
    GENERATED_BODY()
public:
    /** 创建低频测试状态机，失败返回非零进程码。 */
    ADemoAmmoTest();
    /** DeltaSeconds为游戏秒；使用真实World等待GE到期，不手工触发效果移除。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** Condition为用例结果，Message为非敏感说明；失败停止执行。 */
    bool Check(bool Condition,const TCHAR* Message);
    /** Delay为等待World秒，推进到下一测试步骤。 */
    void Advance(float Delay);
    /** Location为世界厘米；生成未注册GM的测试敌人，不影响自然清关。 */
    class ADemoEnemy* SpawnTarget(FVector Location);
    int32 Step=0; // 当前步骤，不复制，正常游戏不创建此Actor。
    float Next=1.f; // 下个步骤的World秒，保证ASC/UI已初始化。
    bool Failed=false; // 首次失败后不继续改动测试状态。
    TWeakObjectPtr<class ADemoEnemy> Target,Behind; // World持有，测试不延长敌人寿命。
    float Before=0; // 本次断言前HP快照。
    bool bShotPending=false; // 跨帧命中等待期间只提交一次Fire GA，不重复扣弹。
    float ShotDeadline=0.f; // 当前一枪的World秒截止，超时明确失败。
    TWeakObjectPtr<AActor> ShotWall; // 墙体必须保留到实体弹真正结束，不能发射同帧销毁。
    UPROPERTY() TObjectPtr<class UDemoRunSave> Snapshot; // 冰冻/穿透测试使用的已购买快照，独立值对象。
};
