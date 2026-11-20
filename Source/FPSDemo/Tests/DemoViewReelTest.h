// 职责：Editor抽取UI与设备偏好回归。使用：-DemoViewReelTest配隔离GameUserSettingsINI；可附加-DemoReelUIOnly独立验证UI，绝不代表相机通过。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR
#include "UI/Screens/DemoCasinoScreen.h" // 仅Editor保存只读真实控件快照，包体测试类不依赖表现探针。
#endif
#include "DemoViewReelTest.generated.h"

/** 仅Editor显式启动：真实跳跃相机采样 + 已结算UI动画回归，使用临时槽。 */
UCLASS(Transient,NotBlueprintable)
class ADemoViewReelTest : public AActor
{
    GENERATED_BODY()
public:
    /** PostUpdateWork在相机更新之后采样；暂停仍Tick，验证真实时间UI。 */
    ADemoViewReelTest();
    /** DeltaSeconds为World秒，状态机截止用单调时间；默认含相机检查，显式UIOnly跳过相机并输出不同的成功标记。 */
    virtual void Tick(float DeltaSeconds) override;
private:
#if WITH_EDITOR
    /** -DemoCasinoUITest专用真实时间状态机，检查七玩法真实CommonUI按钮、金额控件、结果动画、货箱续玩与GAS靶场；不执行相机测试。 */
    void TickCasino();
    FDemoCasinoAnimationProbe CasinoAnimation; // 同一结果树的上一采样值，必须跨实际Slate渲染帧变化，不能用Flow时间代替。
    FVector RangeReturnLocation; // 七玩法专项入场前位置，用于验证独立场景传送和自动返回。
#endif
    /** Value/Reason是实测断言；失败停止测试并退出隔离进程。 */
    bool Check(bool Value,const TCHAR* Reason);
    /** 使用真实控制台注册与生产交易/GAS验证三个Editor测试开关及关闭恢复；金币回收区分中奖到账和免扣费用，返回是否全部通过。 */
    bool CheckDebugCommands();
    int32 Stage=0; // 0初始化、1..3跳跃、30共同UI入口、4..10动画/连续三抽/余额边界/关页、11设备设置与调试、12结束。
    double Deadline=0; // 单调秒，暂停不会冻结。
    float GroundZ=0; // 起跳前胶囊高度，单位cm。
    float PeakZ=0; // 每次跳跃实际最高胶囊高度，不能只检查按键日志。
    float MaxCameraError=0; // 实际PCM与组件位置差cm，检出上一帧相机。
    float MaxOffsetError=0; // 相机相对胶囊Z偏移与60cm基线差。
    float MaxDelta=0; // 真实World帧耗时秒；作为报告，不把自动化负载误判运动代码。
    int32 Samples=0; // 实际空中渲染帧采样数。
    int32 Serial=0; // 一次UI提交后的账本版本，用于检出连点重复扣款。
    int32 Balance=0; // 一次UI提交后的银币余额，播放结束不得再扣一次。
    FName ResultId; // 真实已保存的停靠结果，不依赖随机胜负。
    float LastPosition=0; // 动画半程位置，暂停期间仍应前进。
    bool bFailed=false; // 第一次失败即停止。
};
