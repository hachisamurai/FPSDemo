// 职责：四枪运行时动画、有限弹药、取消和拉栓测试状态；仅显式Editor专项使用，依赖真实GAS/装备流程。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoWeaponAnimationTest.generated.h"

class ADemoCharacter;
class ADemoWeaponBase;
class UAnimInstance;

/** 显式-DemoWeaponAnimationTest的独立World专项；使用真实配置子类、GAS和骨骼求值，不依赖UnrealEd。 */
UCLASS(Transient, NotBlueprintable)
class ADemoWeaponAnimationTest : public AActor
{
    GENERATED_BODY()
public:
    /** 开启跨帧状态机；不手工调用装填完成回调，也不改变正常游戏启动流程。 */
    ADemoWeaponAnimationTest();
    /** DeltaSeconds为World帧间隔秒；逐步等待真实GAS计时，失败或240秒超时以非零状态退出。 */
    virtual void Tick(float DeltaSeconds) override;
private:
#if WITH_EDITOR
    /** 独立游戏参数 -DemoReloadSeatTest，使用临时存档验证插入前后真实输入、有限备弹及第一关间出发。 */
    void TickReloadSeatTest();
    float SeatTestCreditDeadline = 0.f; // 插入帧冻结的真实B04凭据截止；重复发奖会延长此时钟。
#endif
    /** 显式Tube专项：验证立即入仓、重复/旧序号拒绝、循环、容量、有限备用、开火和切枪取消。 */
    void TickTubeReloadTest();
    /** 显式-DemoBoltCycleTest才运行的逐发专项；复用真实装备/GAS夹具，覆盖射速、末发、切枪和中断。 */
    void TickBoltCycleTest();
    // 专项场景0默认45RPM、1快75、2慢30、3末发、4切回续播、5外部中断；仅测试进程使用。
    int32 BoltCase = 0;
    // 开枪前冻结的动作间隔和配置RPM，断言不能从表现内部时钟直接复制期望。
    float ExpectedBoltSeconds = 0.f;
    float OriginalRPM = 0.f;
    // 每场景开火前实际腕和栓的位置，用于确认有真实骨骼运动。
    FVector BoltIdleWrist = FVector::ZeroVector;
    FTransform BoltRest = FTransform::Identity;
    // 每实际帧累计当前拉栓周期的最大位移cm；原包约40%抽栓、55%已推回，单帧检查会误判静止。
    float MaximumBoltTravel = 0.f, MaximumBoltWristTravel = 0.f;
    /** Condition为实际断言，Message为稳定日志用例名；失败只终止本测试进程。 */
    bool Check(bool Condition, const TCHAR* Message);
    /** NextStep为本专项状态，Delay为最短World等待秒数；不使用后台线程或墙钟计时。 */
    void Advance(int32 NextStep, float Delay = .15f);
    /** 建立Editor隔离档案、真实终端和无敌人干扰的Combat夹具；返回false时已输出断言失败。 */
    bool InitializeFixture();
    /** Index为主武器0..2；临时回到真实终端交互范围，装备后恢复玩家位置与Combat阶段。 */
    bool SelectAtTerminal(int32 Index);
    /** WeaponIndex为本测试0手枪/1步枪/2散弹/3狙击；终端负责主武器权限，数字槽入口负责主副切换。 */
    bool SelectCurrentWeapon();
    /** bEmpty决定夹具Ammo0或1；盒式备用2、管式备用1限制为单周期，调用正式GAS并冻结时长/序号。 */
    bool StartReload(bool bEmpty);
    /** 检查实际链接实例、ASC主实例、机械网格及握持可见性；bCapture决定保存稳定待机截图。 */
    bool CheckEquipped(bool bCapture);
    /** 稳定持枪比较真实主图与压缩Idle的双腕/指尖及左肘，防止只锁住手而肘平面错误；手枪跳过隐藏左臂。 */
    bool CheckSupportGrip();
    /** 显式诊断时在PostUpdateWork记录已求值左肘/腕与IK权重；不修改姿势或玩法，用于定位回握跳动。 */
    void AuditSupportArm();
    /** 检查已等待至动画中段的真实Montage、手部骨变换和锁定版本；CaptureName用于可选截图名。 */
    bool CheckReloadPose(const TCHAR* CaptureName);
    /** 仅在已等待动画更新或动作清理后检查真实同帧时钟；本次必须有样本且最大误差不超过2毫秒，开始首帧不调用。 */
    bool CheckReloadPhaseAudit();
    /** ExpectedAmmoValue/ExpectedReserveValue是本次转移或取消后应保留的发数；检查零件和表现状态均已复原。 */
    bool CheckClean(int32 ExpectedAmmoValue, int32 ExpectedReserveValue);
    /** 记录当前武器参考持枪时的骨组件空间变换；后续只比较枪内零件，避免相机/手臂运动干扰。 */
    void CaptureMechanicalRestPose();
    /** Name为截图叶名；仅显式Capture且存在RHI时请求下一帧截图，连续帧模式禁用此入口以避免覆盖请求。 */
    void Capture(const FString& Name);
    /** 每实际World帧调用；最多30fps截图并在内存记录采样时刻，完成姿势后一次写TSV，不影响正式计时或断言。 */
    void CaptureAnimationFrame();

    // 以下状态只归本测试Actor，World销毁时丢弃；0初始化、1装备、2..6完整换弹、7..10取消、11下一枪。
    int32 Step = 0;
    // 当前Catalog索引0..3；三种长枪通过真实终端选择，手枪通过固定副武器槽。
    int32 WeaponIndex = 0;
    // 单枪取消用例索引0..2，对应普通取消、意外Montage中断、切枪取消。
    int32 CancelIndex = 0;
    // 下一步最早World秒数；240秒总时限避免资源或技能异常导致专项挂起。
    float NextTime = 1.f;
    // 本次合法周期秒数副本：管式空仓选EmptyShellReloadSeconds，后续等待不重读共享配置。
    float ReloadDuration = 0.f;
    // 本次开始时记录的武器序号，拒绝重复激活时必须保持相同。
    int32 ReloadSequence = 0;
    // 同次开始冻结的引擎Montage实例身份；完成/取消后审计仍必须属于此实例，避免同资源新动作混入。
    int32 ReloadMontageInstanceID = INDEX_NONE;
    // 盒式备用2、管式单周期备用1时的完成后发数；仅用于断言，不能参与正式弹药结算。
    int32 ExpectedAmmo = 0;
    // 有限备用扣除实际补入量后剩余的发数；双管剩1发时只消耗1颗，不能强行清空备用。
    int32 ExpectedReserve = 0;
    // 首个失败立即停止推进，防止破坏现场日志或继续修改夹具。
    bool bFailed = false;
    // 本次动作版本冻结副本，与运行时独立比较，避免测试只比较同一getter。
    bool bExpectedEmpty = false;
    // 显式-DemoWeaponAnimationFrames才启用；只改变测试Actor采样频率，不改变World/技能播放速率。
    bool bCaptureFrames = false;
    // -DemoSupportArmAudit启用每实际帧完成骨求值后的诊断；仅测试Actor持有，不复制、不用于正式动画。
    bool bAuditSupportArm = false;
    // 仅前两次完整换弹置true，包含完成后0.15秒IK交接；取消用例不记录视频帧。
    bool bCapturingFullReload = false;
    // 当前套首次观察Gameplay换弹结束的World秒，-1表示尚未结束；额外录制淡出以看清回握而非只拍到结算。
    float FrameCaptureFinishedTime = -1.f;
    // 当前一套动作的连续索引，从0000开始；更换武器或普通/空仓版本时独立归零。
    int32 CaptureFrameIndex = 0;
    // 帧文件前缀，例如Pistol_Tactical；开始动作时冻结，后续不跟随测试索引变化。
    FString CaptureFramePrefix;
    // 下一次30fps采样最早World秒数，完成帧可提前一次，但同一实际帧绝不重发。
    float NextFrameCaptureTime = 0.f;
    // 本套StartReload冻结的World起点秒数，用于按真实帧间隔生成VFR视频并对齐归一化声音事件。
    float FrameCaptureStartTime = 0.f;
    // 当前套帧名与相对游戏秒数的TSV内存缓冲；只在动作完成时写一次磁盘，避免逐帧元数据IO扰动采样。
    FString CaptureTimingText;
    // 引擎实际帧编号防重复；FScreenshotRequest只容纳单个请求，不能同一帧覆盖上一张文件名。
    uint64 LastScreenshotFrame = MAX_uint64;
    // 初始固定主实例与当前测试武器均借用World对象，不延长Pawn/武器生命周期。
    TWeakObjectPtr<UAnimInstance> FixedMainInstance;
    TWeakObjectPtr<ADemoWeaponBase> TestedWeapon;
    // 同一枪待机时的手部世界坐标；玩家保持静止，用于验证手臂轨道实际改变求值姿势。
    FVector IdleLeftHand = FVector::ZeroVector;
    // 当前枪每根骨的组件空间参考姿势；不保留其他武器或Editor动画节点引用。
    TMap<FName,FTransform> MechanicalRestPose;
};
