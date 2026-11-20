// 职责：显式Editor两端外观/移动探针。使用：正常UI完成核心/Ready/异步出发，真实首关KillAll提交后才开始原动作观测；依赖正式LAN/临时档。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoPlayerNetworkTest.generated.h"

class ADemoCharacter;
class ADemoCoopSpectatorCamera;
class ACameraActor;
class FJsonObject;
class FJsonValue;
struct FHitResult;

/** 单脚真实蒙皮最低点及其两种地面基准；值仅用于本测试，不复制或驱动生产角色。 */
struct FDemoNetworkSoleMeasurement
{
    // 全部鞋点中到其脚下真实射线地面的最小Z高度cm，负值表示真实穿地。
    float GroundHeight = 0.f;
    // 同一个最低点到CurrentFloor真实支撑平面的Z高度cm，不能选另一点冲抵地形差。
    float SupportHeight = 0.f;
    // 在该最低点XY处，支撑平面减脚下地面的真实Z差cm；只有正差可以增加脚下高度上限。
    float SurfaceDelta = 0.f;
    // 所有鞋底采样点的两表面差绝对值最大值cm；超过2cm即不属于本白模微小装饰起伏范围。
    float MaximumAbsoluteSurfaceDelta = 0.f;
    // 与GroundHeight严格配对的蒙皮世界点及真实射线地面点，单位cm，归本次同步观测所有。
    FVector LowestWorld = FVector::ZeroVector, GroundWorld = FVector::ZeroVector;
};

/** 显式Editor LAN双进程外观探针；独立拥有者Actor承载测试RPC，不向生产PC添加调试协议。 */
UCLASS(Transient, NotBlueprintable)
class FPSDEMO_API ADemoPlayerNetworkTest : public AActor
{
    GENERATED_BODY()
public:
    /** 可复制探针只由-DemoPlayerNetworkTest房主创建；远端必须另带-DemoPlayerNetworkClient才执行。 */
    ADemoPlayerNetworkTest();
    /** DeltaSeconds为World秒；本机输入仍进入正式Movement/Weapon/GAS，观测放在最终骨求值之后。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** 服务器启动下一固定阶段，并通过本测试Actor的拥有者通道同步观测意图。 */
    void BeginStage(int32 NextStage);
    /** NextStage/Duration是测试计划；Pawn及期望色值来自服务器，客户端只用作断言，不写角色。 */
    UFUNCTION(Client, Reliable) void ClientBeginStage(int32 NextStage, float Duration, ADemoCharacter* Host,
        ADemoCharacter* Remote, uint8 HostColor, uint8 RemoteColor, FLinearColor HostTint, FLinearColor RemoteTint,
        float HostYawBase, float RemoteYawBase); // 两个YawBase仅为测试19阶段开始前的服务器实际身体方向快照（度），不驱动生产身体。
    /** 两端独立重置本阶段采样，Duration为本地World秒；不以客户端时间决定玩法结果。 */
    void ResetObservation(int32 NextStage, float Duration);
    /** 只从本机受控Pawn发起移动/射击/换弹，服务器不能代替自主代理提交输入。 */
    void DriveLocalPawn(float Elapsed);
    /** 读取当前真实骨、材质、动作及ViewTarget，累积本阶段证据，不修改动画实例。 */
    bool Observe();
    /** 检验累积证据并生成值报告；报告不能替代几何/视觉/四枪完整联机验收。 */
    bool FinishObservation(FString& Summary);
    /** StageIndex为服务器正等待的阶段；成功值仅是测试结果，不能改任何玩家或经济状态。 */
    UFUNCTION(Server, Reliable) void ServerObservation(int32 StageIndex, bool bSuccess, const FString& Summary);
    /** bSuccess/Message为最终探针结果；两端各写本地证据并延迟退出以排空可靠RPC。 */
    UFUNCTION(Client, Reliable) void ClientFinish(bool bSuccess, const FString& Message);
    /** 本进程只写Saved/PlayerAcceptance专用报告，不保存或读取用户持久进度。 */
    void Finish(bool bSuccess, const FString& Message);
    /** Message为稳定失败标签；记录两端失败后只结束显式探针进程。 */
    void Fail(const FString& Message);
    /** 返回上衣MID的实际线性色；材料缺失/不是实例/参数不存在则失败。 */
    static bool ReadTint(ADemoCharacter* Pawn, FLinearColor& Tint);
    /** 对实际双眼检查父骨/无碰撞/OwnerNoSee、真实瞄准源及受限目标；Subject来自当前阶段选定Pawn。 */
    bool ObserveEyes(ADemoCharacter* Subject);
    /** 返回当前阶段受观察Pawn；9..13房主视角，14..18客户端视角，其余保留原动作/观战规则。 */
    ADemoCharacter* GetSubject() const;
    /** 返回本阶段真实ControlRotation输入；眼珠不直接注入角度，不冻结身体Yaw。 */
    FRotator GetEyeStageAim() const;
    /** 显式水平专项19..38阶段的独立计划角度；从服务器冻结基线偏移，不读取生产HeadYaw推导期望。 */
    FRotator GetYawStageAim() const;
    /** 在复制/动画稳定后验证真实Actor、头骨和枪口；同时覆盖阈值内、边界、反向和±180换界。 */
    bool ObserveYaw();
    /** 对同一个生产纯值解算器检验前/左/右/上/下及组合限幅，确保非标准head rest也正确。 */
    bool ValidateEyeSolver() const;
    /** Subject为真实Pawn，PawnIndex固定0=host/1=remote；只读基线与最终Mesh，在原生平滑完成后验朝向/鞋底。 */
    bool ObserveBodyPlacement(ADemoCharacter* Subject, int32 PawnIndex);
    /** Subject/bLeft选择真实一侧；Out保存同点真实离地/支撑高度与表面差，缺地面/近水平支撑平面或权重异常返回false。 */
    static bool MeasureSoleFloor(ADemoCharacter* Subject, bool bLeft, FDemoNetworkSoleMeasurement& Out);
    /** Value为实际组件或配置变换；返回厘米位置/xyzw旋转/缩放的独立JSON值，不能据此写回角色。 */
    static TSharedRef<FJsonObject> PlacementTransformJson(const FTransform& Value);
    /** Hit来自原生CurrentFloor或本World只读射线；记录命中对象/表面，不能改变角色支撑面。 */
    static TSharedRef<FJsonObject> PlacementHitJson(const FHitResult& Hit);
    /** Subject为真实Pawn；bSoleMeasured控制两有效鞋点是否加入对比，LeftWorld/RightWorld只用于原XY查询。 */
    static TSharedRef<FJsonObject> PlacementFloorJson(ADemoCharacter* Subject, bool bSoleMeasured, const FVector& LeftWorld, const FVector& RightWorld);
    /** 仅显式LAN/RHI截图模式的9..18阶段建立本端独立相机；不拥有Pawn、不改变ControlRotation或早先观战。 */
    bool BeginCaptureStage();
    /** Elapsed为本阶段World秒；2.1秒后且双Pawn实际静止/平滑稳定才请求一次截图，失败返回false供探针结束。 */
    bool CaptureIfReady(float Elapsed);

    // 由服务器RPC传递的当前World两名真实玩家；GC强引用仅在本瞬时Actor生命周期内保留。
    UPROPERTY() TObjectPtr<ADemoCharacter> HostPawn;
    UPROPERTY() TObjectPtr<ADemoCharacter> RemotePawn;
    // 记录曾出现的客户端观战相机，复活后必须失效，不能只把它隐藏。
    TWeakObjectPtr<ADemoCoopSpectatorCamera> ObservedCamera;
    // 当前World等待就绪(-1)，0身份，1/4移动，2/5射击，3/6换弹，7死亡，8清关复活，9..18双端各五视角。
    int32 Stage = -1;
    // 两端独立本地秒数；可靠阶段回执允许任意合理网络延迟，不比较进程绝对时钟。
    float StageStart = 0.f, StageDuration = 0.f;
    // 本地观测完成与服务器收到远端回执互相独立，缺任意一方都不能推进。
    bool bLocalDone = false, bRemoteDone = false;
    // Standalone只发一次正式HostSlot；旅行后新World重新生成本Actor。
    bool bHostRequested = false;
    // 最终输出幂等门闩与墙钟截止；结束后不继续改游戏或重写报告。
    bool bFinished = false;
    double ExitAt = 0.0, StartedAt = 0.0;
    double SetupWaitSeconds=180.; // 本进程准备预算秒，构造读取30..600；不占原150/240秒动作预算。
    bool bInitialStagePending=false; // 首条测试RPC先于本端GS/UI时暂存，不代关菜单或伪造阶段。
    float PendingInitialDuration=0.f; // 服务器首阶段计划秒，仅在本端实际就绪后用于原ResetObservation。
    // 服务器首次绑定时冻结预期值，之后死亡复活也必须保持相同Pawn色号/MID。
    uint8 ExpectedHostColor = 255, ExpectedRemoteColor = 255;
    FLinearColor ExpectedHostTint = FLinearColor::Black, ExpectedRemoteTint = FLinearColor::Black;
    // 当前观察对象初始局部骨位置与累计最大偏移；排除Actor平移冒充动画。
    FVector FirstFoot = FVector::ZeroVector, FirstHand = FVector::ZeroVector;
    float MaximumFootTravel = 0.f, MaximumHandTravel = 0.f, MaximumSpeed = 0.f;
    // 展示枪真实组件骨Pose的首帧副本与最大平移cm，验证机械图确实求值而非只有Actor挂接移动。
    TArray<FTransform> FirstGunPose;
    float MaximumGunTravel = 0.f;
    // 采样数及公开时钟序号，初始值由真实武器获取，不手调射击或装填状态。
    int32 Samples = 0, InitialShot = 0, MaximumShot = 0, InitialReload = 0, MaximumReload = 0;
    // 阶段初始0.75秒复制宽限后任何无效颜色/网格样本均计失败，不能只筛选好帧通过。
    int32 InvalidSamples = 0;
    // 实际看到的上身动作/观战/复活；仅持有布尔证据，不驱动生产状态。
    bool bSawFire = false, bSawReload = false, bSawSpectator = false, bSawRevived = false;
    // 每个动作阶段最多三次真实扳机和一次真实换弹；死亡GE同样只提交一次。
    int32 FireRequests = 0;
    bool bReloadRequested = false, bDeathApplied = false;
    // 两端独立阶段摘要，最终JSON明确标明发送端与观测端。
    TArray<FString> Summaries;
    // 阶段目标控制视角确实经真实Pawn/RemoteViewPitch到达的样本数；0.75秒复制宽限后才计期望一致性。
    int32 EyeAimSamples = 0;
    // 最坏实际目标/显示角，度；每端独立报告，远端pitch允许压缩字节固有误差约1.42度。
    float MaximumEyeYaw = 0.f, MaximumEyePitch = 0.f, MaximumAimError = 0.f;
    // 阶段稳定后实际上身缓存/最终TP枪口相对预定世界瞄准方向的最大误差（度）；两端各自测量。
    float MaximumAimPoseError=0.f, MaximumMuzzleAimError=0.f;
    // 显式-DemoPlayerYawNetworkTest扩展至39阶段；基线只有19阶段冻结一次并经测试RPC发给对端。
    bool bYawAudit=false;
    float HostYawBaseline=0.f, RemoteYawBaseline=0.f;
    // 本阶段真实头/身体/持枪观测误差（度）；稳定样本每帧累计，任何超标即失败，不筛掉不利样本。
    float MaximumYawBodyError=0.f, MaximumYawHeadError=0.f, MaximumYawGunError=0.f;
    int32 YawSamples=0;
    // 客户端观战时，host眼方向已证实不同于本地观战Camera，避免GetPlayerViewPoint串视角。
    bool bSawIndependentEyeSpectator = false;
    // 两端分别观测两Pawn；下标0=host、1=remote，数据只存在本测试Actor，不参与生产复制。
    int32 PlacementSamples[2]={0,0}, StablePlacementSamples[2]={0,0}, GroundPlacementSamples[2]={0,0}, InvalidPlacementSamples[2]={0,0};
    // 缓存基线误差在复制宽限后计入；实际Mesh误差只在原生平滑完成/停止运动后计入，单位cm或度。
    float MaximumBaseLocationError[2]={0.f,0.f}, MaximumBaseRotationError[2]={0.f,0.f};
    float MaximumMeshLocationError[2]={0.f,0.f}, MaximumMeshRotationError[2]={0.f,0.f}, MaximumFacingError[2]={0.f,0.f}, MaximumControlYawError[2]={0.f,0.f};
    // 完整鞋底点到实际脚下地面的两脚最低高度极值cm；没有真实地面样本时GroundPlacementSamples为0并拒绝通过。
    float MinimumSoleHeight[2]={0.f,0.f}, MaximumSoleHeight[2]={0.f,0.f};
    // 同一最低鞋点到CurrentFloor平面的高度极值cm；另保存全部点表面差绝对最大值，不能把地形高差隐去。
    float MinimumSupportHeight[2]={0.f,0.f}, MaximumSupportHeight[2]={0.f,0.f}, MaximumSurfaceDelta[2]={0.f,0.f};
    // 每Pawn最多10Hz归档世界/相对变换，不削减逐Tick断言；跨阶段保留全部值证据，约千条。
    float NextPlacementRecord[2]={0.f,0.f};
    TArray<TSharedPtr<FJsonValue>> PlacementFrames;
    // 可选-DemoPlayerNetworkCapture仅本地读取；不复制、不增加生产协议，不把截图存在当作视觉通过。
    bool bCaptureRequested=false, bCaptureStageReady=false, bStageScreenshotRequested=false;
    // 本测试World拥有的无Owner独立相机；9阶段前不创建，World结束销毁，不保存关卡或改角色可见性。
    UPROPERTY() TObjectPtr<ACameraActor> CaptureCamera;
    // 每阶段固定端点/阶段PNG路径、请求UTC及旧文件时间；UE Windows文件时间只有整秒精度，须确认比旧文件更新。
    FString StageCapturePath;
    FDateTime StageCaptureRequestedAt, StageCapturePreviousTimestamp;
    // 跨阶段只保存本端截图请求和实际相机数值；主Agent另行查看原PNG，不能自动给出视觉验收结论。
    TArray<TSharedPtr<FJsonValue>> CaptureRecords;
    bool bPistolRunAudit=false; // 显式-DemoPistolRunArmNetworkTest，复用十九阶段但用真实全速移动和移动换弹。
    int32 RunArmSamples=0, ReloadArmSamples=0, ResumedRunArmSamples=0; // 每端实际摆臂、装填排他、装填后恢复样本；不参与网络复制。
    FVector FirstRunningHand=FVector::ZeroVector; // 首个稳定跑步时左腕相对肩的位置，排除整个角色平移/骨盆抖动。
    float RunningHandTravel=0.f; // 当前阶段左腕相对肩的最大行程cm，验证图真正驱动骨骼。
};
