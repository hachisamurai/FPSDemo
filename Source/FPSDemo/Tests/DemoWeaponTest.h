#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoWeaponTest.generated.h"
class ADemoEnemy;
class ADemoWeaponBase;

/** 显式-DemoWeaponTest的独立World测试，物理键→IMC→GAS→实例，不用于正常游戏。 */
UCLASS(Transient, NotBlueprintable)
class ADemoWeaponTest : public AActor
{
    GENERATED_BODY()
public:
    /** 开启低频状态机；实际AbilityTask/输入跨帧处理，不手工调用完成回调。 */
    ADemoWeaponTest();
    /** DeltaSeconds为World帧间隔秒数；失败或90秒超时即退出测试进程。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** Condition是断言结果，Message为日志用例名称，失败阻止后续状态修改。 */
    bool Check(bool Condition, const TCHAR* Message);
    /** Delay为下一步骤的最短World等待秒数，步骤自动递增。 */
    void Advance(float Delay = .15f);
    /** Key为真实FKey，bDown选择按下/释放，进入Controller输入栈而非直接调用武器函数。 */
    void SendKey(FKey Key, bool bDown);
    /** Name为截图文件名；仅-DemoWeaponCapture时请求真实RHI下一帧渲染。 */
    void Capture(const TCHAR* Name);
    /** Index为主武器0..2；仅本测试暂时置Hub并移到真实终端走装备UI，结束恢复原位置/阶段。 */
    bool SelectAtTerminal(int32 Index);
    /** Label为实模截图名；骨骼枪迁移后校验真实活动网格、稳定锚点与空静态组件，记录相机空间姿势。 */
    bool CheckWeaponVisual(const TCHAR* Label);
    // 测试流程仅随本World存在，不跨OpenLevel持久化。
    int32 Step = 0;
    float NextTime = 1.f;
    bool bFailed = false;
    // 测试借用对象不延长其生命周期；销毁/切换后通过有效性检查。
    TWeakObjectPtr<ADemoWeaponBase> Pistol;
    TWeakObjectPtr<ADemoWeaponBase> Rifle;
    TWeakObjectPtr<ADemoEnemy> Target;
    FVector MenuReturnLocation=FVector::ZeroVector; // 菜单输入专项借用真实终端后恢复空中靶场，防截图/后续开火位置漂移。
};
