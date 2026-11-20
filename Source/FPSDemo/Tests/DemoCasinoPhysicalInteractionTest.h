// 职责：Editor显式Standalone老虎机实体输入回归；真实Query命中和Character交互路由驱动金额/拉杆，核原权威保存与本人表现，不改资产/概率/开奖结果。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoCasinoPhysicalInteractionTest.generated.h"
class ADemoCasinoDevice;

/** -DemoCasinoPhysicalTest配Editor -game Standalone；Bootstrap仅该参数创建，普通游戏无测试入口。 */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class FPSDEMO_API ADemoCasinoPhysicalInteractionTest : public AActor
{
    GENERATED_BODY()
public:
    /** 低频状态机，仅Editor启Tick；无几何/相机/玩家永久进度副作用。 */
    ADemoCasinoPhysicalInteractionTest();
    /** 验证Editor独立进程/显式参数后建立本次证据目录；不合条件只拒绝该夹具。 */
    virtual void BeginPlay() override;
    /** DeltaSeconds为World秒，等待真实注册/求值/输入/保存；不手工提交Casino或生成结果。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 清理故障注入的同步保存委托，不改变R03设备/地图/临时存档目录归属。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
#if WITH_EDITOR
    /** Condition是本次实际测量；首次失败停止继续驱动，退出推迟到同步保存栈退回之后。 */
    bool Check(bool Condition,const FString& Message);
    /** ControlId必须来自实际注册控件；只设置正常ControlRotation，下一帧再核真实Ray。 */
    bool AimAtControl(FName ControlId);
    /** 等待正常视向/Query就绪后调用Character::TriggerWorldInteraction一次；未命中不会模拟通过。 */
    bool InteractWithAimedControl();
    /** 全部稳定查询点与进入前同版记录比较，覆盖Listen本机私有表现运行期间。 */
    bool CheckStableQueries();
    /** 只读当前真实MenuFlow/CommonUI状态，确保世界操作没有借隐形全屏菜单授权。 */
    bool CheckNoFullscreenPage();
    /** 真实读取本GI临时槽文件，核当前期望Coins/Serial，而非只读GI缓存。 */
    bool CheckDiskCheckpoint(int32 ExpectedCoins,int32 ExpectedSerial);
    /** 同步UObject保存故障；观察候选未发布，并在Waiting时经相同交互路由做一次重入探针，返回false回滚。 */
    bool RejectSave();
    /** 幂等恢复当前World的原GameMode保存委托；不得在正在执行的故障回调内销毁其委托。 */
    void RestoreSaveBinding();
    /** 输出实际测量/断言至本次唯一Saved目录；不输出Token、雷位、全量私人快照。 */
    bool WriteReport(bool bPassed);
    /** 仅-DemoCasinoModelTest分支：核实际私有杆/鼓变换及真实已提交Symbols最终停位，非规则模拟。 */
    bool CheckModelPose(bool bSettled);
    /** 请求当前真实游戏视点PNG；不重建模型/相机，保存时只声明实际存在的图。 */
    void CaptureModelScreenshot(const FString& Name);
    bool bModelAudit=false; // 显式正式S01回归；旧CP01路径/断言不自动扩大验收。
    int32 ModelPoseChecks=0; // 本次实际读取并核验的正式机械姿态次数。
    int32 ModelCommittedRounds=0; // 原事务真实成功轮数，连续局测试要求2，不用构造回执。
    FString ModelProfilePath; // 当前Controls真实消费Profile，报告不按文件名猜已启用。
    TArray<FString> ModelScreenshots; // 请求的本次PNG，报告另核文件确实存在。
    int32 Step=0; // 状态机步骤，仅该显式隔离World拥有。
    int32 AmountControlIndex=0; // 四次实际金额键序列：清除、1、0、确认；不是向草稿直接赋值。
    double NextAt=0.; // World秒；等真实焦点稳定/输入与表现更新。
    double ControlAimAt=0.; // 当前控件瞄准的World秒，等待真实眼位/视向更新。
    double OpeningRayValidSince=-1.; // 初次Open/重进前连续真实Query命中的起始World秒；等待原FocusStable/扫描周期，不修改生产Focus。
    double ModelRevealObservedRealAt=-1.; // 本次真实Committed首次被观测的单调秒；存盘耗时不算Save之后才开始的机械表演时间，MidPose前等待实际刷新。
    int32 ControlAimCorrections=0; // 真实Ray尚未对准时按当前眼位重设正常ControlRotation次数；不移动眼位或查询件。
    double StartedRealAt=0.; // 单调秒总时限，异常暂停也不会永久挂起测试。
    FName AimedControl; // 本次准备真实点击的稳定控件ID，不是RPC授权替身。
    TWeakObjectPtr<ADemoCasinoDevice> Device; // 当前原World注册的老虎机；卸载即失效，不额外生成第二台设备。
    TArray<FName> QueryIds; // 该15个可信查询控件的稳定ID，供完整前后位置核对。
    TArray<FVector> QueryBefore; // 进入Operate前实际世界cm位置，表演不得修改。
    int32 QuerySnapshotChecks=0; // 实际完成的整套稳定点测量次数，不以一次前后静帧代替动态监测。
    double MaximumQueryDisplacementCm=0.; // 相对进入前实际查询点的最大位移cm，非法点立即失败。
    int32 BeforeCoins=0; // 成功拉杆前个人真实金币；Editor免扣已显式关闭。
    int32 BeforeSerial=INDEX_NONE; // 成功拉杆前真实GambleSerial。
    int32 BeforeRequestId=0; // 新输入组件真实私有请求号，与PC旧命令域分开。
    uint64 BeforeSaveSerial=0; // 成功前GI实际落盘ChangeSerial，只作本次保存证据。
    int32 CommittedPaid=0; // 本人实际已提交回执Paid；不调用纯规则计算/伪造奖面。
    int32 CommittedCoins=0; // 成功后的真实金币，供故障/重入不变核对。
    int32 CommittedSerial=INDEX_NONE; // 成功后的真实序号，也是故障回滚基线。
    TArray<int32> CommittedSymbols; // 本地实际接受的三个权威已结符号；无未来随机或雷位。
    uint64 SaveSerialBeforeFailure=0; // 注入失败前实际磁盘写入序号，失败不得增长。
    int32 RequestIdBeforeFailure=0; // 故障拉杆前新输入组件序号，仍只能新增一次请求。
    int32 SaveFailureCalls=0; // 故障委托实际调用次数，0不授保存失败路径覆盖。
    bool bSaveOverrideInstalled=false; // 仅本次故障窗口替换原同步保存委托，结束/卸载恢复。
    bool bInSaveCallback=false; // 防止Check失败在正在执行的委托内改变其生命周期。
    bool bWaitingReentryObserved=false; // 保存回调内真实Waiting重入未产生第二个请求的实际证据。
    bool bPhysicalSkillGuardObserved=false; // Operate后服务器物理租约为真且玩家技能入口被拒绝；不代表移动/相机被锁。
    bool bLegacyCasinoRouteRejected=false; // 旧Mode.ExecutePlayerCommand业务路由负例拒绝；不冒充旧RPC网络传输验收。
    bool bFailed=false; // 第一次失败后停止业务驱动，留原始证据。
    bool bFinished=false; // 成功/失败只输出一次报告与退出请求。
    FString EvidenceDirectory; // 本次独占Saved子目录，不覆盖历史失败或其它工单。
    TArray<FString> CheckRecords; // 有界断言结果文本，不累计逐帧日志或全量快照。
#endif
};
