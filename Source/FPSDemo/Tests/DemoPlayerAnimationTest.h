// 职责：Editor隔离World内运行玩家FP/TP、瞄准及动作回归；仅读最终骨姿势，使用真实GAS与装备事务。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/Archive.h" // 开火专项独占CSV写流，结束前显式Flush；不把逐帧161骨数据积压在JSON内存。
#include "DemoPlayerAnimationTest.generated.h"

class ADemoCharacter;
class ADemoWeaponBase;
class ACameraActor;
class USkeletalMeshComponent;
struct FDemoIdleBreakPoseAudit; // Editor工作线程纯值快照，仅同步读，不保存骨缓冲引用。

/** 显式Editor隔离运行的玩家动画验收；驱动真实Movement/GAS，采样实际求值蒙皮骨矩阵，不重播假测试Pose。 */
UCLASS(Transient, NotBlueprintable)
class ADemoPlayerAnimationTest : public AActor
{
    GENERATED_BODY()
public:
    /** 只创建验收调度器，正式游戏必须通过显式命令行才生成本Actor。 */
    ADemoPlayerAnimationTest();
    /** DeltaSeconds为真实World步长秒，PostUpdateWork读取本帧最终骨姿势并推进隔离夹具。 */
    virtual void Tick(float DeltaSeconds) override;
    /** World结束时移除仅录制模式注册的弱UObject全局回调，其他验收不注册该委托。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    /** Condition为独立断言，Message为稳定诊断描述；失败保留已有遥测并退出专属测试进程。 */
    bool Check(bool Condition, const FString& Message);
    /** 创建Editor临时档案、测试平台与外部摄影机；不保存Content或持久玩家进度。 */
    bool InitializeFixture();
    bool bAwaitingFixtureCombat=false; // 新房间异步流送启动后只等待真实Combat回调，不反复重建夹具/伪造CombatSession；总超时仍由Tick约束。
    /** Index为0手枪/1步枪/2霰弹/3狙击；通过实际终端/槽位装备，不绕过权限。 */
    bool EquipWeapon(int32 Index);
    /** 开始当前Stage；重置平台落点，输入来自正常Movement/GAS接口，时长按真实武器配置决定。 */
    bool BeginStage();
    /** 当前阶段完成后验证结果；只接受真实运动、GAS标签和弹药结算，不手工调用完成回调。 */
    bool FinishStage();
    /** 记录Body及展示枪实际骨矩阵；Capture截图用同一World相机，不改变OwnerNoSee设置。 */
    void RecordFrame();
    /** bSuccess表示所有独立检查完成；在退出前保存包含失败现场的JSON报告。 */
    void SaveAndExit(bool bSuccess);
    /** 从Pawn组件寻找第三人称展示枪，匹配当前正式枪网格并排除第一人称/全身网格。 */
    USkeletalMeshComponent* FindPresentationWeapon() const;

    TWeakObjectPtr<ADemoCharacter> Player; // 当前隔离World的正式Pawn，不跨旅行或测试进程保活。
    TWeakObjectPtr<ADemoWeaponBase> Weapon; // 当前实际库存对象；换枪阶段重新取得。
    TWeakObjectPtr<ACameraActor> Camera; // 本World的外部观察相机，不替代玩家移动控制器。
    TSharedPtr<FJsonObject> Report; // 本测试游戏线程独占，退出前一次序列化到Saved/PlayerAcceptance。
    TArray<TSharedPtr<FJsonValue>> Frames; // 默认15Hz完整骨采样，MotionOnly60Hz；仅本测试持有，正式游戏不分配。
    TSet<int32> ObservedStates; // 当前阶段真实运行图状态，不通过手工设置AnimInstance伪造覆盖。
    int32 Stage = -1; // -1初始化，0..6移动，7..26四枪各五个动作。
    int32 StageSamples = 0; // 本阶段实际遥测帧数，拒绝没有发生播放的空通过。
    int32 ScreenshotIndex = 0; // 全流程连续图像索引，防止不同阶段覆盖。
    int32 StartAmmo = 0; // 开始动作时冻结真实弹药，结算期望不能借动画内部值计算。
    int32 ExpectedAmmo = 0; // 按真实物理容量截断补弹；固定双管不得超过2，取消保持原值。
    int32 ExpectedReserve = 0; // 未转入容量的备用弹仍保留，禁止把双管剩余1发当作错误。
    float StageStart = 0.f; // World秒，暂停时不推进测试，完成时不按墙钟猜动画进度。
    bool bLyraAimAudit=false,bLyraJumped=false; // 显式专项在四枪待机阶段发起真实跳跃，追加阈值内水平瞄准；每阶段清理。
    float StageDuration = 0.f; // 当前动作秒数，装填来自冻结的真实GAS时长。
    float NextSample = 0.f; // 下一次15/60Hz骨采样的最早World秒。
    float NextCapture = 0.f; // 下一次8/30Hz截图最早World秒，图像只辅助视觉，矩阵采样独立。
    float MaximumSpeed = 0.f; // 本阶段观测到的最大水平速度，验证Dash/Run真的执行。
    float MaximumFootTravel = 0.f; // 去除Actor位移后的足骨行程，拒绝静态A-Pose冒充移动。
    FVector FirstFoot = FVector::ZeroVector; // 首采样足骨组件位置，采样生命周期仅本阶段。
    FVector PlatformSpawn = FVector::ZeroVector; // 当前专属平台中心脚下平面+胶囊半高，单位世界厘米。
    FString StageName; // 写入截图和JSON的稳定动作标签，不读未经净化的外部文件名。
    bool bFailed = false; // 首次失败后不再推进，防止后续阶段覆盖原因。
    /** 返回本轮验收输出根；候选和-DemoNativeAcceptance正式冒烟分别隔离，不改变资产选择。 */
    FString GetReviewRoot() const;
    bool bSawFalling = false; // Jump阶段必须经历真实MovementMode falling。
    bool bSawDash = false; // Sprint阶段必须实际出现GAS躲闪标签。
    bool bCancelled = false; // Cancel阶段只调用一次正式取消接口。
    bool bCapture = false; // 显式-DemoPlayerCapture且有效RHI时请求图片。
    bool bFirstPersonCapture = false; // 显式-DemoPlayerFirstPersonCapture使用真实拥有者镜头，复核新臂与原四枪的实际屏幕效果。
    bool bMotionOnly = false; // 显式-DemoPlayerMotionOnly只跑七种移动并以60Hz采样，独立核对短暂支撑期。
    bool bStartJitterAudit=false; // 显式Editor诊断：四枪独立起停及带头身夹角起步，60Hz保留原生图和最终肩骨数据，不修改正式资产。
    bool bAimContractAudit=false; // 显式Editor诊断：四枪水平/俯仰组合，同时采样真实FP/TP枪口；采集完成不代表美术通过。
    /** Now为World秒；驱动四枪各11种真实视线组合，保持身体阈值规则，不写动画Pose。 */
    void TickAimContractAudit(float Now);
    /** Frame为当前已求值骨帧；追加FP/TP枪口、相机与控制方向，区分视差与角度偏离。 */
    void RecordAimContractState(TSharedPtr<FJsonObject> Frame);
    /** Now为游戏秒；在真实装备/全身图上切换近远遮挡和视角，独立测量枪管射线到准星落点的误差；Baseline只记录修复前值。 */
    void TickMuzzleFocusAudit(float Now);
    /** Now为World秒；自然等待随机待机并采集各处理阶段，不触发/重置IdleBreak计时器或写入动画状态。 */
    void TickIdleBreakAudit(float Now);
    // 当前自然待机诊断已经观察到真实IdleBreak状态或活动播放器；仅决定采集是否覆盖目标，不改变状态机。
    bool bIdleBreakObserved=false;
    bool bIdleInterruptSent=false; // 真实操作中断专项只提交一次，防止重复Fire/Reload伪造持续状态。
    float IdleInterruptTime=-1.f; // 实际提交操作的World秒，供恢复时间与移动输入窗口断言。
    bool bIdleReloadPrepared=false; // 换弹专项先真实开一枪腾出容量，不直接修改弹药。
    /** Now为游戏秒；录制模式在PrePhysics输入同一四枪/视角/深度计划，不写动画缓存或正式资产。 */
    void TickMuzzleFocusRecording(float Now);
    /** World/Type/DeltaSeconds来自World结束ActorTick回调；只在本World动画/展示更新后抓取实际RHI帧，不在回调改变输入。 */
    void CaptureMuzzleFocusRecording(UWorld* World,ELevelTick Type,float DeltaSeconds);
    // 本测试注册的全局PostActorTick句柄；弱UObject绑定，EndPlay精确移除，不捕获裸this。
    FDelegateHandle MuzzleRecordingHandle;
    // 当前武器录制前的真实固定步长预热帧；每枪60帧，确保选枪/动画过渡完成后才采集。
    int32 MuzzleRecordingWarmup=0;
    // 仅专项World拥有的可移动遮挡板；复用同一Actor，退出随World销毁，不保存地图或正式资产。
    UPROPERTY(Transient) TObjectPtr<AActor> MuzzleFocusWall;
    FString MotionIsolation; // 固定Current/NoAim/Native/Ready输出身份；非空时起步后多走3秒，覆盖完整循环及循环接缝。
    bool bShoulderShadowAudit=false; // 显式截图专项调整隔离World太阳方向并低头20度，确保前方能看到真正全身影子，不改正式关卡。
    /** Now为World秒；通过真实Movement输入复现起步，保留停稳过程，不用瞬移分割每次起步。 */
    void TickStartJitterAudit(float Now);
    /** 在最终求值之后记录主图及子层反射缓存；仅读数据，不能借诊断写入图状态。 */
    void RecordStartJitterState(TSharedPtr<FJsonObject> Frame);
    bool bAimAudit=false; // 显式-DemoPlayerAimTest复用真实四枪动作夹具，独立输出，不替换旧美术验收报告。
    bool bYawAudit=false; // 显式-DemoPlayerYawTest同时测试水平±60请求/±45头角和既有俯仰；独立保存原始骨矩阵。
    bool bMovementViewAudit=false; // 显式-DemoPlayerMoveYawTest增加静止中间头角及移动对齐，单独输出，避免覆盖旧验收。
    int32 YawCorner=0; // 普通专项0..3四角；移动/衣领专项0..10，先七个水平头角，再四个俯仰组合。
    float AimRequestedYaw=0.f; // 水平专项当前显式控制请求（度），与Pitch独立；用于断言而不从实际身体倒推期望。
    bool bAimRaised=false; // 每个Hold阶段只切换一次低头->抬头，用于比较真实枪口方向。
    float AimRequestedPitch=0.f; // 测试控制器请求度数±120，必须经生产UpdateRotation限幅，不能直接写相机/动画缓存。
    float AimMinimumObserved=90.f, AimMaximumObserved=-90.f; // 当前Hold阶段实际动画俯仰极值，拒绝未接入瞄准层的静态Pose。
    float MuzzleMinimumObserved=90.f, MuzzleMaximumObserved=-90.f; // 真实TP枪口世界俯仰极值，不能只检查动画参数而漏掉未接图。
    int32 AimInvalidSamples=0; // 控制方向越界、FP父链错误或瞄准层未启用的实际样本数，任何一个均不通过。
    /** Pitch为故意越界的测试输入度数；走生产Controller旋转处理，不修改CameraManager限制或AnimInstance。 */
    void RequestAimAuditPitch(float Pitch);
    /** 第三人称手枪跑动专项；Now为World秒，驱动真实移动/装填/取消并记录60Hz最终姿势。 */
    void TickPistolRunArmAudit(float Now);
    /** 初始化当前跑动专项阶段；只通过正式武器和Movement接口改变状态。 */
    bool BeginPistolRunArmStage();
    bool bPistolRunArmAudit=false; // 显式-DemoPistolRunArmTest；独立输出，避免覆盖其他外观验收。
    /** DeltaSeconds为本次已完成求值的World秒；显式Editor开火专项，先读本帧骨姿再提交下一帧真实输入。 */
    void TickFireArmJitterAudit(float DeltaSeconds);
    /** Now为World秒；装备本例手枪/步枪并初始化隔离弹药、静止输入与记录标识，不写Shot或动画缓存。 */
    bool BeginFireArmJitterStage(float Now);
    /** DeltaSeconds为本帧World步长；在Presentation先决条件之后流式记录真实Fire时钟及各骨姿处理阶段。 */
    void RecordFireArmJitterFrame(float DeltaSeconds);
    /** Now为已采样帧World秒；原长枪18例或显式四枪装填衔接32例提交真实装填/取消/恢复开火，并核唯一弹药结算。 */
    void TickGunElbowOperationInputs(float Now);
    /** 在指定实际帧导出当前LOD线性蒙皮与AfterAim对照；仅读矩阵/真实权重，不刷新Pose、不再执行PP。 */
    void RecordGunElbowSkin(USkeletalMeshComponent* Mesh,const FDemoIdleBreakPoseAudit& Audit,const FString& Label);
    uint8 GunElbowSkinProbeMask=0; // 本次Editor采集已完成的局部缓存鉴别测试：Body位1、Owner位2；不修改活Pose或资产。
    bool bGunElbowReloadObserved=false; // 本例曾真实进入GAS装填，不能按配置时长推测。
    bool bGunElbowReloadCompleted=false; // 采到真实IsReloading退出边沿。
    bool bGunElbowReloadCancelled=false; // 本例已从正式CancelActions入口取消。
    bool bGunElbowFollowupShot=false; // 恢复例已提交一次真实Fire；成功与否另核Shot/Ammo。
    float GunElbowReloadExitTime=-1.f; // 真实装填退出World秒；仅本阶段，不修改服务器动作时钟。
    bool bReloadHoldDirectSourceObserved=false; // 专项立即开火时，上一实际动画状态仍是Reload/EmptyReload；不以零World秒差代替此事实。
    float ReloadHoldFollowupShotDelay=-1.f; // 成功Shot服务器时刻减实际装填退出观察时刻，秒；负值表示无成功Shot，不伪称覆盖同帧。
    int32 ReloadHoldOnlyStage=INDEX_NONE; // 仅显式Editor参数-DemoReloadHoldOnlyStage=0..32单例重现；默认完整32/33例不变。
    float ReloadHoldTailSeconds=0.f; // 仅显式单例延长观察尾部[0,12]秒，不改真实操作/GAS时长；0保原计划。
    bool bNativeIdleInterruptAudit=false; // 仅单例hold_reference+长尾：经过4秒且真实原生权重>.95后才ControlRotation+5度，观察退出，不写状态机。
    bool bNativeIdleInterruptSubmitted=false; // 每例只提交一次真实转头，防逐帧输入重新刷新重入计时。
    bool bNativeIdleExitObserved=false; // 真实转头之后观察到原生IdleBreak权重归零，不能只用主图门false冒充退出。
    int32 ScopeViewAuditStep=0; // 显式Scope专项0等待稳定/1一级镜/2二级镜/3退出/4完成，非生产瞄准状态。
    float ScopeViewOriginalFOV=90.f; // 专项真正进入开镜前的本机CameraComponent FOV度，退出必须恢复该值。
    FVector ScopeViewActorBaseline=FVector::ZeroVector; // 物理落稳后捕获的实际Actor位置cm，不把初始落地位移归因于Scope。
    FVector ScopeViewSharedBaseline=FVector::ZeroVector; // 同时刻共享基础眼位cm；本项只验证Scope自身不改变锚点。
    FVector ScopeViewComponentBaseline=FVector::ZeroVector; // 同时刻CameraComponent世界位置cm，和最终FOV分开核对。
    /** bSuccess仅表示计划及数据采集通过；Reason写入清单，关闭CSV流后退出专属测试进程，不宣称动作验收。 */
    void FinishFireArmJitterAudit(bool bSuccess,const FString& Reason);
    // 只在WITH_EDITOR构造中读取显式命令行，非Editor始终false；不选择候选资产或改变正式动画逻辑。
    bool bFireArmJitterAudit=false;
    bool bFireArmJitterCapture=false; // 可选真实RHI帧；NullRHI时不得生成假的截图或声称渲染通过。
    float FireArmJitterSampleFPS=60.f; // 显式Editor专项只允许30/60/120真实固定步长，默认保持旧60Hz基线。
    bool bFireRecoilAimCorners=false; // 新后坐复核：各枪三例使用平视/抬头侧视/低头移动，不改生产阈值。
    float FireRecoilTestRateScale=1.f; // Editor隔离夹具GAS射速倍率[0.5,3]；只在初例设置一次，不改武器CDO或持久成长。
    int32 FireRecoilStartMontageCount=0; // 本例真实Body后坐启动次数，和成功Shot差分独立对照。
    bool bFireRecoilOperationBoundary=false; // 可选Editor专项，仅Stage0/3改为真实装填完成后0.02..0.10秒单击；普通六例保持原计划。
    bool bFireRecoilBoundaryObservedReload=false; // 本例已采样真实IsReloading=true，不允许凭预计装填时长伪造完成边沿。
    float FireRecoilBoundaryReloadFinishedAt=-1.f; // 首次采到真实IsReloading退出的World秒，负值表示尚未完成。
    float FireRecoilBoundaryShotDelay=-1.f; // 实际成功Shot的ServerStart与上述World边沿之差；单机隔离World两时钟一致。
    int32 FireRecoilBoundaryOldOperationSamples=0; // 新策略重新启用后仍引用Reload/EmptyReload/Bolt的异常帧数；应为0。
    TArray<FQuat> FireRecoilBoundaryPreviousArmLocals; // 本专项前一真实Final帧8个主臂关节的局部旋转，按实际Manny父链反算，不缓存整个动画。
    bool bFireRecoilBoundaryPreviousAdditive=false; // 辨认实际策略false→true边沿，不能把整个装填动作的大旋转误判为后坐启用跳变。
    bool bFireRecoilBoundaryActivationObserved=false; // 成功Shot后确实观察到新策略启用；缺观察不算平滑通过。
    float FireRecoilBoundaryActivationLocalStepDegrees=0.f; // 策略启用相邻帧主臂局部旋转最大角差，单位度；用来捕获已复现的33度腕部突跳。
    float FireRecoilBoundaryAdjacentLocalStepDegrees=0.f; // 成功Shot之后全部相邻Final帧的主臂局部角差峰值，报告用，不等同动作质量验收。
    float FireRecoilBoundaryActivationSourceSeconds=-1.f; // 实际启用时已赶上的Montage源秒数；不修改源时钟或隐瞒表现启用延迟。
    FString FireArmJitterDirectory; // Current/NoAim等有限隔离名与本次时间戳，永不覆盖旧采样。
    TUniquePtr<FArchive> FireArmJitterFrames; // 一行/已求值帧的时钟与阶段可用性；本测试独占直到Finish。
    TUniquePtr<FArchive> FireArmJitterPoses; // Final真实Mesh骨及四阶段Compact骨的CS变换，逐帧写流。
    TUniquePtr<FArchive> FireArmJitterEvents; // 输入提交/Stop/RestoreAmmo/阶段边沿的World秒，区别于下一帧Pose。
    int32 FireArmJitterFrame=0,FireArmJitterCaseFrame=0; // 总采样帧/本例采样帧，不以磁盘耗时推进动作。
    int32 FireArmJitterScreenshots=0; // 实际RHI请求数；每例末0.1秒停止截图后才切例/退出，媒体帧数允许少于数值帧。
    int32 FireArmJitterStartShot=0,FireArmJitterLastShot=0; // 本枪本例真实Shot序号边界，不能由测试伪造。
    int32 FireArmJitterFireSamples=0; // 本例确实求值到Fire的帧数，防止只有输入事件而无动作。
    int32 GunElbowBoltSamples=0; // 三长枪诊断中真实Bolt求值帧数；狙击原路由没有独立Fire序列，不能将正常拉栓视为“没播放”。
    float FireArmJitterMaxSpeed=0.f; // 本例真实速度cm/s；移动连发必须经CharacterMovement达到。
    float FireArmJitterNextPress=0.f; // 半自动下一次真实点击允许秒数，沿当前GetFireInterval而非每帧重启。
    bool bFireArmJitterPressed=false,bFireArmJitterReleased=false; // 本例自动枪只按/放一次；单发始终Start后立即Stop。
    bool bFireArmJitterFinishing=false; // 防止IO或断言失败在关闭/退出过程中递归写入失效流。
    uint32 FireArmJitterLastAnchorCount=0,FireArmJitterLastAimCount=0; // 判定该帧节点真的执行，不把残留数组当新阶段。
    bool bRunArmActionStarted=false; // 每阶段预跑0.6秒后只发起一次真实动作，验证跑步->换弹的过渡。
    float RunArmActionStart=0.f; // 本阶段实际发起动作的World秒；取消相对此时刻延迟0.1秒。
    /** 八方向专项的阶段入口；每枪只在初始Idle复位，换向和停止保持真实速度/步相。 */
    bool BeginDirectionalStage();
    /** Now/DeltaSeconds为World秒；检查连续步相、实际握点与跑动操作优先级。 */
    void TickDirectionalAudit(float Now,float DeltaSeconds);
    bool bDirectionalAudit=false; // 仅显式DemoDirectionalMovementTest，输出独立目录。
    float PreviousGaitPhase=-1.f; // 上一实际求值步相，-1表示尚无可比帧。
    float MaximumContactDrift=0.f; // 本阶段手骨相对实际枪锚偏差厘米；动作操作阶段不要求固定握点。
    FTransform HoldLeft,HoldRight; // 每把枪稳定Idle实际最终握姿，仅本测试使用。
    bool bHoldMeasured=false; // 换枪时丢弃上一把枪的握姿参考。
    bool bDirectionalStagePending=false; // 截图在帧末完成；下一Tick才换枪/退出，避免末帧图片混入下个阶段。
};
