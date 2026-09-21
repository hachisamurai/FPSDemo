#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoArmoryTest.generated.h"

/** -DemoArmoryTest专用真实World/渲染回归，三次十关胜利和死亡重载；存档自动隔离。 */
UCLASS(NotBlueprintable, Transient)
class ADemoArmoryTest : public AActor
{
    GENERATED_BODY()
public:
    /** 每0.1秒推进，保证Canvas完成至少一帧。 */
    ADemoArmoryTest();
    /** DeltaSeconds为帧间秒数，只在显式测试命令创建的Actor中驱动模拟。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** Condition预期断言，Message稳定用例名；失败结束隔离测试进程。 */
    bool Check(bool Condition, const TCHAR* Message);
    /** Next测试阶段，等待0.3秒让菜单/世界完成刷新。 */
    void Advance(int32 Next);
    /** Name固定截图名；仅写Saved/Screenshots/Armory。 */
    void Capture(const TCHAR* Name);
    // 每World独立步骤和下一次执行的游戏时间秒数，不持有旧World对象。
    int32 Step = 0;
    float NextTime = 2.f;
    // 避免失败之后再推进流程。
    bool bFailed = false;
};
