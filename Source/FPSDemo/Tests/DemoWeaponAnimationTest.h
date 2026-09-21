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
    /** bEmpty决定夹具Ammo0或1，备用恒为2；调用正式RequestReload/GAS，记录本次时长与唯一序号。 */
    bool StartReload(bool bEmpty);
    /** 检查实际链接实例、ASC主实例、机械网格及握持可见性；bCapture决定保存稳定待机截图。 */
    bool CheckEquipped(bool bCapture);
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
    // 本次合法ReloadSeconds副本，后续等待不读取可能改变的共享配置。
    float ReloadDuration = 0.f;
    // 本次开始时记录的武器序号，拒绝重复激活时必须保持相同。
    int32 ReloadSequence = 0;
    // 同次开始冻结的引擎Montage实例身份；完成/取消后审计仍必须属于此实例，避免同资源新动作混入。
    int32 ReloadMontageInstanceID = INDEX_NONE;
    // 正常换弹预期1+2=3、空仓预期0+2=2；仅用于断言，不能参与正式弹药结算。
    int32 ExpectedAmmo = 0;
    // 首个失败立即停止推进，防止破坏现场日志或继续修改夹具。
    bool bFailed = false;
    // 本次动作版本冻结副本，与运行时独立比较，避免测试只比较同一getter。
    bool bExpectedEmpty = false;
    // 显式-DemoWeaponAnimationFrames才启用；只改变测试Actor采样频率，不改变World/技能播放速率。
    bool bCaptureFrames = false;
    // 仅前两次完整换弹置true，覆盖3→4与5→6两段等待；取消用例不记录视频帧。
    bool bCapturingFullReload = false;
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
