// 职责：Editor双进程真实按键冲刺/转身与可选移动骨姿验收；先由两端正常UI完成准备/异步出发，再自动观测。依赖正式LAN、当前区域、Movement与GAS，不写生产门禁或骨缓存。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoDashNetworkTest.generated.h"

class ADemoCharacter;
class ACameraActor;
class FJsonValue;

/** 显式Editor临时局双进程冲刺回归；真实按键、GAS和CharacterMovement，不向角色注入冲刺方向。 */
UCLASS(Transient, NotBlueprintable)
class FPSDEMO_API ADemoDashNetworkTest : public AActor
{
    GENERATED_BODY()
public:
    /** 只有-DemoDashNetworkTest创建权威实例，测试RPC使用真实远端PC拥有者。 */
    ADemoDashNetworkTest();
    /** DeltaSeconds为当前World秒；PostUpdate读取已执行Movement后的速度和GAS状态。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** NextStep为0..21固定场景；按当前已提交区域找真实地面复位站位，不改生产方向、速度结果或技能冷却。 */
    void BeginStep(int32 NextStep);
    /** NextStep与两个Pawn为服务器的测试计划；客户端仍从本机真实InputKey发送输入。 */
    UFUNCTION(Client, Reliable) void ClientBegin(int32 NextStep, ADemoCharacter* Host, ADemoCharacter* Remote);
    /** NextStep重置本进程观察值并释放旧按键；0阶段重启动作总计时，不让手工准备耗尽观察预算。 */
    void ResetStep(int32 NextStep);
    /** Key/Down为本机注入的真实按键及按下/释放；不控制桌面或另一进程。 */
    void Key(FKey Key, bool Down);
    /** Axis是左右X/前后Y的键盘期望；只转成WASD，不传给生产RPC。 */
    void MoveKeys(FVector2D Axis);
    /** 返回固定场景期望轴；零轴表示松键后沿朝向冲刺，不是沿旧速度。 */
    FVector2D ExpectedAxis() const;
    /** 本场景由远端玩家还是房主驱动；两端始终观察同一个真实Pawn。 */
    ADemoCharacter* Subject() const;
    /** Elapsed为本端场景秒；覆盖持键、同帧起步/松键/反向及4秒内重按。 */
    void Drive(float Elapsed);
    /** 只读实际速度/位移和GE，不把生产函数的方向返回值作为期望。 */
    void Observe();
    /** Pawn为本端真实观测对象，Elapsed为本案秒；肩部专项记录最终骨位置，可选外部相机截图，不写动画状态。 */
    void ObserveShoulder(ADemoCharacter* Pawn,float Elapsed);
    /** Pawn为两端同一测试对象；在真实跳跃/落地后同时核对复制视线、独立输入计划与FP/TP枪口。 */
    void ObserveAimContract(ADemoCharacter* Pawn,float Elapsed);
    /** 仅渲染专项在起步中保留额外1秒采样；返回测试计划秒，不修改World/动画/技能时钟。 */
    float ScenarioElapsed() const;
    /** 返回本端断言结果，Summary保留真实峰值方向、位移、窗口与冷却证据。 */
    bool CheckStep(FString& Summary);
    /** Index/Passed/Summary仅测试回执；不会写玩法状态或经济数据。 */
    UFUNCTION(Server, Reliable) void ServerResult(int32 Index, bool Passed, const FString& Summary);
    /** Passed/Message为最终结果；当前端先保存，再按客户端先退出的顺序排空RPC。 */
    UFUNCTION(Client, Reliable) void ClientFinish(bool Passed, const FString& Message);
    /** Passed/Message同步写Saved专用报告，不以Editor的Shell退出0代替结果。 */
    void Finish(bool Passed, const FString& Message);

    UPROPERTY() TObjectPtr<ADemoCharacter> HostPawn; // 当前World房主，正式Roster提供，测试结束随World释放。
    UPROPERTY() TObjectPtr<ADemoCharacter> RemotePawn; // 真实已登记远端Pawn，不创建模拟替身。
    int32 Step=-1; // -1等待正式房间；0..21执行；结束状态另由bFinished控制。
    double StartedAt=0, ExitAt=0; // 本端准备/动作各自起点及延迟退出的单调秒；进入0阶段时重置StartedAt，不比较跨进程时钟。
    double SetupWaitSeconds=180.; // Editor独立准备时限，命令行允许30..600秒；动作总时限仍为原220秒。
    bool bInitialStepPending=false; // 首次可靠计划可能早于本端GS/菜单收敛，暂存角色引用，条件满足才开始0阶段。
    float StepStart=0; // 本端World场景开始秒，收到计划时独立设置。
    bool bHostRequested=false, bFinished=false, bFinalPassed=false; // 当前测试生命周期，非复制玩法状态。
    bool bLocalDone=false, bRemoteDone=false; // 每场景两个进程均回执后才推进。
    bool bMoved=false, bPressed=false, bReleased=false, bRepeated=false, bRepeatReleased=false; // 每段真实按键只发送一次。
    bool bSawEvade=false, bWasEvading=false; // 本端实际读到的GE窗口及上一帧状态。
    int32 EvadeStarts=0, Samples=0; // 真实窗口上升沿必须恰好一次；样本不足不算通过。
    bool bCooldownSampled=false, bCooldownTagAtTwoSeconds=false; // 2.2秒采样锁存；Mixed复制下所有观察者只保证冷却Tag可见。
    float PeakSpeed=0, CooldownAtTwoSeconds=-1; // cm/s与秒；仅权威/拥有者可读GE剩余时长，非拥有者保留-1，冷却只测不清除。
    // 水平转头专项只在显式标记时追加20度视线和冲刺前WASD方向断言；保留旧冲刺报告目录。
    bool bYawAudit=false;
    bool bMovementViewAudit=false; // 显式移动朝向专项，在第20/21步检验行走/冲刺身体对齐和刹停后恢复只转头。
    bool bLocomotionAudit=false; // 显式DemoLocomotionNetwork额外检查两端最终骨动画，保持原网络玩法断言。
    bool bLyraAudit=false; // 显式DemoLyraNetwork追加真实跳跃、落地缩放和远端枪口方向验收。
    bool bAimContractAudit=false; // 显式DemoAimContractNetwork使用45/55度俯仰与±40度自由观察，证据独立保存，不修改生产网络。
    TArray<TSharedPtr<FJsonValue>> AimContractFrames; // 本端真实方向/位置采样；客户端报告不能由服务端拷贝生成。
    bool bShoulderAudit=false; // 显式肩部候选/旧版/正式验收启用，普通网络测试不采集额外骨帧。
    int32 ShoulderStartSamples=0; // 每案冲刺前真正起步窗口的采样计数，两端各自验证，不能用Dash代替起步。
    TArray<TSharedPtr<FJsonValue>> ShoulderFrames; // 当前端完整22案小型骨位置数据，Finish写本端文件，不复制给另一端冒充观察。
    UPROPERTY(Transient) TObjectPtr<ACameraActor> ShoulderCamera; // 可选隔离渲染相机，World销毁时释放，不修改正式相机配置。
    float NextShoulderCapture=0.f; // 本端World秒；截图限频，不以截图帧率冒充60Hz。
    int32 ShoulderScreenshot=0; // 本进程截图编号，配合step和端点隔离文件。
    bool bJumpRequested=false,bJumpReleased=false; // 每场景末段真实Space只按一次；不调用动画状态写入。
    int32 BoreSamples=0,JumpPoseSamples=0,LandPoseSamples=0,DashPoseSamples=0; // 两端各自最终骨姿样本，缺项不能通过。
    int32 NativeDashSamples=0; // 原生候选必须实际播放Montage，不能只凭旧BodyMotionState兼容缓存通过。
    float MaximumBoreError=0.f,MaximumRootScaleError=0.f; // 真实渲染枪口三维角差(度)及根骨缩放误差。
    int32 MovementPoseSamples=0; // 具备新配置且真实求值期间的样本数，每阶段清零。
    FVector FirstMovementFoot=FVector::ZeroVector; // 该阶段首个左足组件位置，去除Actor运动。
    float MovementFootTravel=0.f; // 相对首帧足骨行程厘米，拒绝图参数有效而腿静止。
    bool bStopLookApplied=false; // 拥有者仅在本阶段2.5秒后增加一次20度视线，不能不断覆盖自然结果。
    int32 MovingYawSamples=0,StoppedYawSamples=0; // 两端分别收集真实运动/停止后的朝向样本，缺样本直接失败。
    float MaximumMovingBodyError=0.f,MaximumStoppedBodyError=0.f,MaximumStoppedHeadError=0.f; // 度数，基于独立控制计划/实际Actor与动画头角，非生产解算器输出。
    float PeakWalkSpeed=0.f; // 真实冲刺前0.55..0.8秒峰值cm/s，不能用Dash速度验证普通MoveDemo。
    FVector WalkDirection=FVector::ZeroVector; // 与上方峰值配对的真实水平速度单位向量，两端分别观测。
    FVector PeakDirection=FVector::ZeroVector, DashStart=FVector::ZeroVector, DashEnd=FVector::ZeroVector; // 从真实Movement/位置采样，非期望方向注入。
    TArray<FString> Results; // 跨场景保留本端及服务器收到的远端摘要，用于保存最终证据。
};
