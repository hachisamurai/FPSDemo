// 职责：仅显式Editor -game -DemoOwnerShadowAudit的本人阴影代理有界运行夹具。
// 使用：真实本地临时槽/StartRun/装备/视点流程，至多4张1280x720与一个JSON；依赖正式Avatar与OwnerShadow，不改骨、DA或云策略。
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoOwnerShadowAudit.generated.h"

class ADemoCharacter;
class ADemoPlayerController;
class ACameraActor;
class FJsonObject;
class FJsonValue;

/** 普通Editor/PIE/包体不创建；显式独立Standalone完成后只退出本测试进程，不触碰用户或其它岗位进程。 */
UCLASS()
class FPSDEMO_API UDemoOwnerShadowAudit : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** Outer须为带显式-game参数的Standalone Game World，非Editor固定false。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** WorldType只允许Game，拒绝Editor/PIE/Preview。 */
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    /** InWorld归引擎；核渲染/磁盘条件，创建本次独占Saved证据目录。 */
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    /** World卸载停止夹具；只清弱引用，不请求退出或保存被测资产。 */
    virtual void Deinitialize() override;
private:
    /** InWorld/TickType/DeltaTime为真实World帧回调；EndFrame后读最终骨/相机，弱UObject绑定只在本World处理，结束/卸载解除。 */
    void OnPostActorTick(UWorld* InWorld,ELevelTick TickType,float DeltaTime);
    /** Condition失败锁存，Message只含本地测试诊断；下一Tick结束，不继续驱动生产输入。 */
    bool Check(bool Condition,const FString& Message);
    /** Stage为有限阶段名；至少保留10GiB，当前目录PNG+JSON不超过64MiB，不自动清理。 */
    bool CheckBudget(const FString& Stage);
    /** 创建临时20,000cm高处地台/灯光/外部观察机，只隔离取景，不保存地图或修改真实人物骨姿。 */
    bool PrepareStage();
    /** Label为有限阶段名；读取最终TP/FP/代理骨和标志，ExpectedActive只断言列明本机门，不表示美术通过。 */
    bool Sample(const FString& Label,bool ExpectedActive);
    /** Name为固定图片名，NextStep仅在PNG真实落盘且经过后续帧后执行；最多4张。 */
    bool BeginCapture(const FString& Name,int32 NextStep);
    /** 每次Tick检查唯一排队PNG，3真实秒超时；不在等待时更改姿态/视点。 */
    void AdvanceCapture();
    /** Success只覆盖运行合同；写一个JSON，明确视觉/Listen/Cook等未验，退出本显式Standalone进程。 */
    void Finish(bool Success);

    bool bActive=false; // 仅显式测试World开启，普通会话不注册运行行为。
    bool bFinished=false; // JSON与进程退出幂等闸。
    bool bFailed=false; // 首次断言失败锁存，不让后续正常样本覆盖失败。
    FDelegateHandle PostActorHandle; // 仅本次World的末帧弱UObject委托；Finish/Deinitialize精确移除，不跨线程捕获。
    int32 Step=0; // 编码内有限状态，非关卡/业务阶段。
    int32 ScreenshotCount=0; // 至多4次FScreenshotRequest，不录制序列。
    uint64 TickCount=0; // 本夹具真实Tick数，只核请求后经过帧。
    double StartedAt=0.; // 单调真实秒，45秒硬超时涵盖资源加载。
    double StartedWorldAt=0.; // 实际World游戏秒，用于报告有限场景时长。
    double NextWorldAt=0.; // 状态机实际游戏秒等待，不覆盖动画时钟。
    FString Directory; // 本次GUID独占Saved目录，不覆盖历史图或存档。
    TArray<FString> Checks; // 有界断言列表，不逐帧落日志或CSV。
    TArray<TSharedPtr<FJsonValue>> Samples; // 有限阶段纯值快照，不强持有被测UObject。
    TWeakObjectPtr<ADemoCharacter> Player; // 当前World真实拥有Pawn，跨World失效。
    TWeakObjectPtr<ADemoPlayerController> Controller; // 当前唯一Standalone本地PC，不伪造观察者。
    TWeakObjectPtr<ACameraActor> Observer; // 本World临时外部视点，只用于真实fallback测试。
    FString PendingScreenshot; // 唯一待落盘PNG，空字符串表示没有请求。
    uint64 ScreenshotFrame=0; // 排队时的本夹具TickCount。
    double ScreenshotAt=0.; // 排队时单调秒，3秒上限。
    int32 CaptureNextStep=0; // 实际落盘后才执行的后继步骤。
};
