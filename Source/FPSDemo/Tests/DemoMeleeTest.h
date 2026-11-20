// 职责：独立Editor临时World的近战验收与实机录制。使用-DemoMeleeTest或-DemoMeleeWideTest；附加-DemoMeleeTrialRecording录制固定60Hz地面双视角；独立-game -DemoMeleeComboRegression录当前配置真实点击/骨姿，依赖GAS/动画/RHI，不写正式存档或资产。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoMeleeTest.generated.h"
UCLASS(Transient,NotBlueprintable)
class ADemoMeleeTest : public AActor
{
    GENERATED_BODY()
public:
    /** 仅测试参数启动，延后等待本地Avatar初始化。 */
    ADemoMeleeTest();
    /** DeltaSeconds为World游戏秒，步骤等待有上界，失败以非零进程退出。 */
    virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
    /** 仅已启动跑动录制/新独立连招回归时恢复其临时输入/视角；其他验收模式维持原结束行为。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#endif
private:
    FString NativeIdleEvidenceDir; // -DemoNativeIdleAudit单次独立RHI证据目录；首次截图创建，不覆盖历史BodyView图。
#if WITH_EDITOR
    /** 双显式Editor -game Standalone专项：真实Combat/GA/敌人HP验证命中反馈及挥刀后取消；不修改DA或直接触发反馈。 */
    void TickMeleeCameraFeedbackAudit(float DeltaSeconds);
    /** 写本轮CSV/JSON及成功失败标记，清理隔离靶和当前攻击；仅本专项独立进程可以退出。 */
    void FinishMeleeCameraFeedbackAudit(bool bSuccess,const FString& Reason);
    double CameraAuditWallStart=0.,CameraAuditGameStart=0.; // 首次Tick时钟，最多90墙钟秒/35World秒，非生产动作时钟。
    double CameraAuditAttackStarted=0.,CameraAuditCancelAt=0.; // 当前真实起刀游戏秒及DA挥声相位后的取消截止。
    uint64 CameraAuditCancelFrame=0; // 实际CancelAttack调用的引擎帧号，至少跨一完整相机帧再检查清零。
    bool bCameraAuditFinished=false,bCameraAuditSawDamage=false,bCameraAuditSawHitFeedback=false; // 专项一次结束/实际HP下降/真实Modifier接受回执观测锁存。
    bool bCameraAuditComponentCancelVerified=false,bCameraAuditASCCancelVerified=false; // 两种真实取消入口分别跨原截止后通过，不能互相替代证据。
    float CameraAuditInitialHealth=0.f,CameraAuditPostHitHealth=0.f; // 真实敌人第一刀前后HP，第二刀远靶不得再变化。
    float CameraAuditMaximumRoll=0.f; // 已记录最终相机Roll绝对峰值，预算0.65度，不控制任何相机。
    int32 CameraAuditAmmo=0,CameraAuditFrames=0; // 隐藏枪弹初值和实际CSV采样数，仅本测试Actor拥有。
    uint32 CameraAuditHitCount=0; // 第一刀真实Modifier已接受命中数；不调用PlayHit/ConfirmCameraHit。
    FString CameraAuditDirectory; // Saved/CameraReloadFix20261114/HitFeedback_<stamp>唯一证据目录，不写Content。
    TArray<FString> CameraAuditRows; // 本World逐帧最终相机/组件/共享眼位/头骨/HP快照，结束一次写盘。
    TWeakObjectPtr<class ADemoCharacter> CameraAuditPawn; // 当前World拥有的测试Avatar，不跨旅行延命。
    /** 仅Editor独立-game显式模式：真实Hub中Key3/离散LMB，每帧PostUpdate记录FP/TP各4秒；不改变DA/源动画/原FP相机，不进入Combat。 */
    void TickComboRegression(float DeltaSeconds);
    /** 保存成功/失败具体原因和已采数据，释放输入与临时观察机；只有双显式门且Game World才允许退出独立进程。 */
    void FinishComboRegression(bool bSuccess,const FString& Reason);
    /** 幂等释放本夹具按键、临时相机/平台/补光；只在该独立模式启动后触发，不改正常测试流程。 */
    void RestoreComboRegression();
    double ComboRegressionWallStart=0.,ComboRegressionCaptureStart=0.; // 首Tick真实墙钟/首采样World秒，分别限制30秒和8秒录制；不写生产动作时钟。
    double ComboRegressionNextPress=0.,ComboRegressionReleaseAt=0.; // 下一次离散LMB游戏秒与当前真实释放截止，不在长帧补发一串虚假点击。
    bool bComboRegressionButtonDown=false,bComboRegressionFinished=false; // 夹具单键状态/结束闩锁，仅本测试Actor生命周期。
    int32 ComboRegressionFrame=0,ComboRegressionPressCount=0; // 真实PostUpdate采样帧/实际发出的点击数，非固定帧率估算。
    int32 ComboRegressionAttackFrames[2]={0,0}; // FP/TP录制段实际攻击样本数；成功只证明两段确实触发生产攻击。
    uint32 ComboRegressionPreviousSerial=0; // 本夹具上一真实动作序号，用于记录边界事件，不回写组件。
    FString ComboRegressionDir,ComboRegressionConfig; // 新Saved目录/起始完整DA属性文本；结束逐项比较保护当前时长、声音与全部配置。
    TArray<FString> ComboRegressionFrames,ComboRegressionBones,ComboRegressionEvents; // 逐帧实际时钟/最终骨与刀空间/真实输入事件，只写本轮Saved证据。
    TWeakObjectPtr<class ADemoCharacter> ComboRegressionPawn; // 当前独立World拥有，不跨旅行持有强引用。
    TWeakObjectPtr<class ADemoPlayerController> ComboRegressionController; // 本地真实输入控制器，结束仅释放本夹具按键。
    TWeakObjectPtr<class ACameraActor> ComboRegressionCamera; // TP独立观察机，原Pawn FP相机配置从未修改。
    TWeakObjectPtr<AActor> ComboRegressionPlatform,ComboRegressionFill; // 本World临时地面和中性补光，不写地图或资产。
    /** 显式RunningRecording：真实Walking下各录FP/跟随TP六秒；旧候选/正式单左扫保持原门禁，BladeR03核验正式刀并追加取消/卸装探针，不改DA。 */
    void TickRunningRecording();
    /** 保存当前CSV并恢复入口状态后退出；bSuccess只表示夹具断言，不代表视觉或多人验收。 */
    void FinishRunningRecording(bool bSuccess,const TCHAR* Reason);
    /** 正常结束、断言失败或World结束共用；可重复调用，不修改资产、存档或其他录制模式。 */
    void RestoreRunningRecording();
    TWeakObjectPtr<class ADemoCharacter> RunningPawn; // 本次实际Avatar，World拥有；结束时不访问已销毁对象。
    TWeakObjectPtr<class ADemoPlayerController> RunningController; // 本次本地控制器，仅用于恢复观察状态。
    TWeakObjectPtr<AActor> RunningOriginalViewTarget; // 进入夹具前的观察目标，不延长其生命周期。
    TWeakObjectPtr<AActor> RunningPlatform,RunningFill; // 临时平台/补光，由World拥有，恢复时销毁。
    FTransform RunningOriginalTransform; // 进入临时平台前的Actor变换，不修改源动画或骨骼。
    FRotator RunningOriginalControlRotation; // 输入朝向；录制TP镜头不写此角度。
    uint8 RunningOriginalMovementMode=0,RunningOriginalCustomMode=0; // 原CharacterMovement枚举/自定义模式的数值快照。
    bool bRunningRecordingActive=false,bRunningOriginalEquipped=false; // 仅本夹具生命周期；恢复函数清空Active以允许重入保护。
    int32 RunningOriginalBodyForcedLOD=0; // 仅BodyClearanceLOD0显式诊断保存Body原ForcedLOD，API1为LOD0，结束必须恢复。
    bool bRunningBodyForcedLODOverride=false; // 本World临时Body LOD0覆盖锁存，默认false；不改变生产Owner/远端LOD策略。
    TWeakObjectPtr<class UDemoOwnerBodyLODComponent> RunningSuspendedLODPolicy; // 仅显式LOD0诊断暂挂本地策略；先让其恢复原LOD，再独立覆盖，退出重新启用。
    bool bRunningLODPolicyWasActive=false; // 本次夹具进入前策略活跃快照；正常自动LOD录制不触碰它。
    int32 RunningGeometryFinalFrames=0; // BodyClearance实际采集完整161骨Final的帧数，预期720，不以稀疏快照冒充完整覆盖。
    FString RunningExpectedDefinition; // 命令行只读断言路径；从不用于加载或选择DA。
    TArray<FString> RunningBoneRows; // 核心骨组件位置/局部缩放逐帧CSV，不将全网格逐帧日志写到常规Log。
    int32 RunningOverlapCounts[4]={0,0,0,0}; // FP前进/横移、TP前进/横移的真实速度>100且攻击中样本数。
    bool bRunningBladeR03=false; // 仅显式Editor录制断言模式；不选择DA或开启生产动画层。
    bool bRunningBladePostureR04=false; // 独立姿态候选使用相同1.60s移动/取消/轨迹测试，路径门禁与正式R03证据分离。
    bool bRunningCancelVerified=false,bRunningUnequipVerified=false; // 完整四招之后的两项真实输入中断探针结果。
    int32 RunningCompleteSourceAttacks=0; // 只有720帧录完且实际序号+4/末尾待机断言通过后才记4，失败结果不伪报完整招数。
    bool bRunningHasLowerBodyFrame=false; // 每视图重置，避免传送/视角热身被误记为腿部运动。
    TArray<FQuat> RunningPreviousLowerBody; // 上次实际TP局部腿骨旋转快照，生命周期仅本录制World。
    int32 RunningLowerBodyMotionCounts[4]={0,0,0,0}; // 对应四种视图/方向：移动攻击中腿链真实旋转步长>0.05度的帧数。
    double RunningLowerBodyPeakStepDegrees[4]={0.,0.,0.,0.}; // 各组合实际腿链每帧旋转最大值，不作为视觉合格承诺。
    TArray<FString> RunningTraceRows; // BladeR03可见刀段与同作者phase的DA权威轨迹对照，单独CSV，不做隐式伤害授权。
    TArray<FString> RunningPostureRows; // 普通审片稀疏Final/Base/Upper；显式BodyClearance采全720帧161骨Final，最终姿态只读，不改变动画/输入。
    double RunningTraceMaxBaseError=0.,RunningTraceMaxTipError=0.; // 运行时全部攻击样本的空间差cm，记录实测，不以任意阈值掩盖偏差。
    double RunningHitTraceMaxBaseError=0.,RunningHitTraceMaxTipError=0.; // 仅配置有效窗口内的同相位差，正式轨迹对齐须另按这些数据审查。
#endif
    /** Editor隔离静态诊断：对比同一Mesh纯序列/实际Linked AnimBP；AuditViews查看同一FP组件侧面和骨骼，或RigRecording录3秒60Hz真实FP；不播放攻击、不写正式DA。 */
    void TickRigAudit();
    /** Editor显式录制真实Walking/GAS与60Hz时序；TwoHand为10秒完整FP左右扫，Power加同源目标框/纯序列对照；默认三镜头各3.6秒，不替代伤害验收。 */
    void TickTrialRecording();
    int32 TrialRecordingFrame=0; // 已请求RHI帧数；旧试录648帧三镜头，RigRecording180帧单静态，进程退出不复用。
    FString TrialRecordingDir; // 本次Saved内唯一日期目录，防止重跑覆盖旧录制证据。
    TArray<FString> TrialRecordingRows; // 本World逐帧CSV；具体列由试录/静态模式的标题行声明，结束一次写盘。
    /** 单横扫DA专项；先选本轮核心并等待正式Combat准备，再用真实GAS/骨碰撞测延迟、范围、遮挡与取消。仅-DemoMeleeTest分发，总时限90秒。 */
    void TickApexSweep();
    double ApexAttackStart=0.; // 当前测试起招World秒数，用于测量真实首次扣血，不写生产组件。
    double ApexFirstHit=-1.; // 当前样例首次扣血延迟秒；-1表示尚未命中。
    int32 ApexCase=0; // 当前空间覆盖样例下标，仅测试Actor拥有，进程结束销毁。
    int32 FirstPersonMotionFrame=0,ThirdPersonMotionFrame=0; // 可选连续RHI录制的各视角帧编号，仅本测试Actor生命周期有效。
    /** 独立宽横挥场景入口；仅显式WideTest驱动，复用本Actor的步骤/靶，不与原26步同时操作玩家。 */
    void TickWideSweep();
    /** 独立双视角截图：额外-DemoBodyViewTest启用；安全区真实输入，检查本地FP与第三人称全身，捕获与状态变更分开帧。 */
    void TickSharedBodyViews();
    /** Value为实际断言，Message为独立预期说明；失败停止后续测试。 */
    bool Check(bool Value,const TCHAR* Message);
    /** Seconds为下一步等待游戏秒，不阻塞引擎帧循环。 */
    void Advance(float Seconds);
    /** Name为专属截图叶名；显式Capture时保存RHI截图及同刻实际组件骨矩阵，排查离线动画与运行表现差异。 */
    void Capture(const TCHAR* Name);
    int32 Step=0; // 本World步骤，不跨地图缓存Actor。
    double NextTime=1.; // 真实游戏时钟等待点。
    bool bFailed=false; // 一次失败后不重复退出/修改夹具。
    TWeakObjectPtr<class ADemoEnemy> Target; // World拥有的真实敌人靶，高血量不触发天然清关。
    TWeakObjectPtr<AActor> Wall; // 遮挡夹具，由World销毁。
    TWeakObjectPtr<class ACameraActor> Camera; // 仅TP截图观察点，不改变Pawn控制器。
    float BeforeHealth=0.f; // 每用例独立HP快照，验证命中一次及取消后无迟到伤害。
    int32 BeforeAmmo=0; // 棍攻击不消耗隐藏枪弹药。
    uint32 Serial=0; // 检查恢复阶段不能重入同招式。
};
