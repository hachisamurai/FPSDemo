#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoSessionTest.generated.h"

/** -DemoSessionTest独立RHI回归；暂停期间用真实时间推进，存档/设置通过测试路径隔离。 */
UCLASS(Transient, NotBlueprintable)
class ADemoSessionTest : public AActor
{
    GENERATED_BODY()
public:
    /** 0 TickInterval使World暂停时测试仍运行，但步骤由真实时间限频。 */
    ADemoSessionTest();
    /** DeltaSeconds为World秒数（暂停可为0）；只驱动专用测试进程。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** Condition为断言，Message为用例名；失败请求非零退出。 */
    bool Check(bool Condition, const TCHAR* Message);
    /** Next为跨World步骤号，Delay为真实秒数，旅行前先记录下一步。 */
    void Advance(int32 Next, double Delay=.35);
    /** Name为本轮截图文件名，按-DemoSessionCapture显式请求。 */
    void Capture(const TCHAR* Name);
    /** bDown为Esc按下/释放，经过真实Controller输入栈。 */
    void Escape(bool bDown);
    /** Name必须是本帧HUD存在的按钮，走真实HUD路由，返回是否找到。 */
    bool Click(FName Name);
    /** 按固定顺序采集经济、GAS、技能及当前装备值；只返回数值，跨World不保留Actor。 */
    TArray<double> CaptureProgress() const;
    /** bAllowed为当前阶段/菜单预期；通过真实GAS激活验证技能/冷却，恢复夹具生命与冲量，不影响后续战役。 */
    bool VerifyPlayerSkills(bool bAllowed);
    // 当前Actor只负责等待，不持有跨World对象，失败后停止。
    double NextTime = 0;
    bool bFailed = false;
};
