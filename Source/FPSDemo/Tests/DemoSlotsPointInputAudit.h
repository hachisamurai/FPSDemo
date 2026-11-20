// 职责：Editor显式Standalone的老虎机中心点/真实Enhanced Input左键/租约/按键机械与耐久回归。
// 使用：-game -DemoSlotsPointInputAudit；末帧有界状态机，至多4张1280x720、64MiB和90秒；不改生产资产、原控件或云策略。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/StrongObjectPtr.h" // 显式候选测试期间强持有唯一DA与候选，禁止GC造成恢复责任丢失。
#include "Materials/Material.h" // TStrongObjectPtr默认构造的UObject类型断言需要完整候选类型。
#include "Interaction/HubShops/DemoHubShopDefinition.h" // 同上；只有夹具借用公开材质软引用，不修改生产目录实现。
#include "DemoSlotsPointInputAudit.generated.h"
class ADemoCharacter;
class ADemoPlayerController;
class ADemoCasinoDevice;
class ADemoGunDisplay;
class UDemoCasinoDeviceProfile;
class UInputAction;
class FJsonValue;
class FJsonObject;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UMaterial;
class UMaterialInterface;
class UDemoHubShopDefinition;

/** 普通Editor/PIE/非Editor不创建；真实业务动作只由正常E路由与IA_Fire注入Started/Completed产生。 */
UCLASS()
class FPSDEMO_API UDemoSlotsPointInputAudit : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 显式T02候选在首次Focus缓存之前依赖Hub目录，借用其公开材质字段并保存原CVar；普通夹具无候选副作用。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** Outer必须为Editor构建的显式-game Standalone Game World；包体固定false，不增加联网调试入口。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** WorldType来自引擎，仅Game支持；Editor/PIE/Preview不安装夹具。 */
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    /** 绑定本World末帧弱UObject回调；InWorld归引擎，只创建独占证据目录。 */
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    /** World卸载撤本夹具委托与注入，绝不停止用户或其它岗位进程。 */
    virtual void Deinitialize() override;
private:
#if WITH_EDITOR
    /** 显式唯一T02路径/哈希/PP位置/干净DA与单World租约前置；不触发任何资产编辑或保存。 */
    bool InstallCandidate(FSubsystemCollectionBase& Collection);
    /** World为本测试World时幂等恢复候选/CVar/相机/参数，不清除他人Package dirty标记。 */
    void OnCandidateWorldCleanup(UWorld* World,bool SessionEnded,bool CleanupResources);
    /** 只解除自有正常截图委托及匹配Filename请求；候选六组结束后仍保留材质与jitter1直到全夹具退出。 */
    void ReleaseTemporalCapture();
    /** 全退出路径恢复本夹具借用的软引用/width有效值/ControlRotation；外部DA或dirty变化明确失败。 */
    bool RestoreCandidate();
    /** T02六组off/on实验的GT状态机，逐样本视向/真实IA按压，不改生产源Mesh或Query。 */
    void AdvanceCandidateProbe();
    /** 当前有效测量回调记录有限RGB、真实姿态和off/on对照；不保存逐帧像素到磁盘/JSON。 */
    bool RecordCandidateSample(const TSharedPtr<FJsonObject>& Frame);
    /** 慢yaw请求前等待与回调时复核同帧真实SharedViewLocation/实际组件及POV；Error列首个失败字段及实际数值，不主动更新Camera。 */
    bool ReadCandidateSlowView(FString& Error) const;
    /** 仅fixed_on16个有效已匹配RGB effect mask的空间/翻转诊断；不代表语义Stencil或整个候选稳定性。 */
    bool AddCandidateEffectTemporalSummary(const TSharedPtr<FJsonObject>& Phase);
    /** 额外RifleOblique有限准备/两张on-off截图收尾；只借正常ShopFocus Tick与注册枪，不发E/Fire/购买。 */
    void AdvanceGunFixture();
    /** 读取实际枪/克隆公开Mesh/材料/LeaderPose/世界框、直接Visibility命中与本人Camera MID；不要求枪mask附着。 */
    bool ReadGunOutlineContext(FTransform& Source,FTransform& Outline,FString& Error) const;
    /** World/TickType/Delta为EndFrame后真实回调；只处理本World，90秒失败停止，不逐帧落文件。 */
    void OnPostActorTick(UWorld* World,ELevelTick TickType,float Delta);
    /** Condition为实测真值，Message不含Token或私有快照；首个失败锁存，下帧退出。 */
    bool Check(bool Condition,const FString& Message);
    /** Stage为有限步骤标签；实测磁盘至少10GiB，图/报告/abslog总预算64MiB。 */
    bool CheckBudget(const FString& Stage);
    /** 按当前bInjectDown向真实EnhancedPlayerInput注入Action值，供下一正常输入处理；不直接调业务路由。 */
    bool InjectPrimary();
    /** Id为实际注册控件名；从真实Avatar共享眼位朝Query中心设置正常ControlRotation。 */
    bool AimControl(FName Id);
    /** 实际首次射线及当前焦点都匹配才可按；2秒内有限视向校正，不移动Query/相机或扩大门限。 */
    bool AimedControlReady();
    /** 每次末帧核全部15个固定Query点不位移；仅累计最大值/次数，不输出逐帧CSV或日志。 */
    bool CheckQueries();
    /** 为无经济的数值按键设置下一次真实IA_Fire手势；Expected为后置草稿，Resume为完成后有限状态。 */
    bool BeginKeyPress(FName Id,const FString& Expected,int32 Resume);
    /** 读取当前受测Key实际组件位移并累计峰；不读取LocalView内部求值器目标，允许尚未压下。 */
    void ReadKeyMotion();
    /** Expected为当前准星控件；读取实际已注册253轮廓和Camera材质绑定，并与真实动态Key/杆的最终变换配对，Label供有限报告。 */
    bool CheckFocusedOutline(FName Expected,const FString& Label);
    /** 只数当前实际Avatar上注册、启用253自定义深度的可见组件，捕获旧mask残留；不推断其它玩家/视图。 */
    int32 CountPawnSilhouetteMasks() const;
    /** 仅显式TemporalProbe在数字0稳定未按下时借唯一mask/MID并保存jitter原值/全部flags，启动1→0→1小样本。 */
    bool BeginTemporalProbe();
    /** 仅GT末帧请求正常已绘制截图并处理上一回调结果；每段弃8个有效回调再16个样本，不在Tick读RHI。 */
    void AdvanceTemporalProbe();
    /** Width/Height/Bitmap为UE5.4 ProcessScreenShots正常绘制完成的GT回调；只消费自己请求的1280x720内存图，不排下一请求/改CVar/退出。 */
    void OnTemporalScreenshotCaptured(int32 Width,int32 Height,const TArray<FColor>& Bitmap);
    /** Phase为当前16个有效测量帧汇总；输出96x96白像素/相邻翻转空间计数并交叉核样本总和，失败只标采样失败。 */
    bool AddTemporalSpatialSummary(const TSharedPtr<FJsonObject>& Phase);
    /** 请求与截图回调均读取实际Camera/最终POV/FOV/源/同mask/MID上下文；Facts为有限纯值快照，漂移则拒绝采样。 */
    bool ReadTemporalContext(TSharedPtr<FJsonObject>& Facts);
    /** Message为明确采样失败，不能当作材质稳定/闪烁证据；只锁存失败，下一GT Tick清理并退出。 */
    void FailTemporalSampling(const FString& Message);
    /** 成功/失败/卸载都恢复本进程原jitter值与完整flags；普通夹具未保存时零副作用，返回真实恢复结果。 */
    bool RestoreTemporalProbe();
    /** 等真实单调时钟采到按下/回Rest，同时查草稿和请求号；不以固定步长World秒替代机械视觉时间。 */
    void AdvanceKeyPress();
    /** 实际读取三鼓/杆组件；bSettled=true时核已保存Symbols对应stop，false仅要求实际机械运动。 */
    bool CheckReelMotion(bool bSettled);
    /** 从本GI实际临时存档文件反序列化币/Serial/typed receipt；不读GI缓存冒充磁盘。 */
    bool CheckDisk();
    /** Name为固定PNG标签；不切换相机或改材质/骨/投影，严格4图、唯一排队请求。 */
    bool Capture(const FString& Name);
    /** Success仅指列明单机合同；输出一个JSON并退出本显式进程，未验范围明确保留。 */
    void Finish(bool Success);
    TWeakObjectPtr<ADemoCharacter> Player; // 当前World实际Avatar，换Pawn立即失效。
    TWeakObjectPtr<ADemoPlayerController> Controller; // 当前真实Standalone本地PC，不创造控制器。
    TWeakObjectPtr<ADemoCasinoDevice> Device; // 从实际Hub注册表选Mode1，不额外生成第二台设备。
    TWeakObjectPtr<UDemoCasinoDeviceProfile> Model; // Controls强持有的正式Profile；本夹具只读，绝不写资产。
    TWeakObjectPtr<UInputAction> FireAction; // 现有IMC还持有同一IA_Fire资产；失效则失败，不添加替代动作。
    FDelegateHandle PostActorHandle; // 本World末帧委托；Finish/Deinitialize精确解除。
    TArray<FName> QueryIds; // 15个固定合法语义，用实际组件点验证。
    TArray<FVector> QueryRest; // 操作前实际Query世界位置cm。
    TSet<FName> PressedControls; // 真实IA_Fire完成的有限控件集合，不由预期序列直接填充。
    TArray<FString> Checks; // 有限断言，不重复记录每帧成功。
    TArray<TSharedPtr<FJsonValue>> OutlineFacts; // 少量实际轮廓/源变换、材质路径和组件数量，不逐帧输出。
    TArray<FString> Shots; // 至多4个实际PNG绝对路径，Finish核存在。
    FString Directory; // 每轮时间+GUID目录，不覆盖历史。
    FString PendingShot; // 全局唯一待落盘PNG；等待期间保持手势，不推进动作。
    uint64 ShotFrame=0; // 排队的GFrameCounter，后续帧且真实文件存在才继续。
    double ShotAt=0.; // 单调真实秒，截图等待上限3秒。
    int32 Step=0; // 有限业务审查步骤，100..102为真实键手势子状态。
    int32 ResumeStep=0; // 该次按键完成后恢复的有限状态。
    int32 DigitIndex=0; // 0..9依次按，不直接写草稿。
    int32 QueryChecks=0; // 实际整套Query位置核对次数。
    int32 AimCorrections=0; // 真实射线未稳定时有限校正次数。
    double MaximumQueryShift=0.; // 实际Query最大位移cm。
    double StartedReal=0.; // 单调真实秒，整个夹具90秒硬上限。
    double StartedWorld=0.; // 真实World游戏秒，只报告/续租覆盖使用。
    double NextReal=0.; // 机械/输入等待的单调截止，避免World固定步长提前断言。
    double AimStarted=0.; // 当前控件瞄准开始真实秒，不因重校正延长2秒上限。
    double SweepWorld=0.; // 已获真实租约之后开始12游戏秒多心跳扫视。
    double NextSweepWorld=0.; // 下次有限扫视World秒。
    int32 SweepIndex=0; // 0..11循环数字/屏/拉杆，不执行点击。
    FString BarrierAmount; // 进入时持续按住的草稿基线，不将未触发当成功输入。
    FName AimedControl; // 实际准备点击的稳定控件名，不作为RPC权限替身。
    FName PressPart; // 当前真实Key模型PartId；用于实际组件读取。
    FTransform PressRest; // 该Part可信rest映射到设备世界，比较最终组件，不取内部动画目标。
    double MaximumKeyTravel=0.; // 当前键实际最大位移cm，真实帧累积。
    double MaximumObservedKeyTravel=0.; // 全轮最大真实键程cm。
    double MaximumOutlineTravel=0.; // 实际轮廓相对同一源Key rest的最大位移cm，须跟随按压而非固定Query。
    double MaximumOutlineSourceError=0.; // 轮廓与实际动态源Mesh位置的最大配对误差cm。
    double MaximumOutlineAngleError=0.; // 轮廓与实际源Mesh旋转的最大配对角差deg。
    int32 OutlinePosePairSamples=0; // 实际逐帧读到轮廓与动态Key的配对次数，只记录汇总。
    double PressStarted=0.; // 该次手势开始单调秒；不是下注/动画权威时间。
    FString ExpectedAmount; // 键动作后置草稿；提交键还核已确认Stake。
    FString NumericExpected; // 0..9真实输入后的累计期望值，仅断言不写生产草稿。
    int32 RequestBeforeKey=0; // 普通金额键不得发经济RPC。
    int32 MechanicalKeysChecked=0; // 真实采到按下并回Rest的键数。
    int32 BeforeCoins=0,BeforeSerial=0,BeforeRequest=0; // 一次真实拉杆的权威钱包/Serial/请求基线。
    uint64 BeforeSave=0; // 初始预算后实际GI保存序号，成功拉杆必须递增。
    int32 CommittedCoins=0,CommittedSerial=0,CommittedPaid=0; // 原事务真实提交后的结果，不构造随机符号。
    TArray<int32> CommittedSymbols; // 已保存typed receipt的三符号，仅用作止位/重进核对。
    double PullStarted=0.; // 长按拉杆起始单调秒，跨揭晓后仍不得重复投注。
    double ReceiptObserved=-1.; // 首次看到真实已提交回执的单调秒，机械从该时刻保守等待。
    bool bInjectDown=false; // 唯一虚拟IA_Fire值；每真实末帧注入，下一原生输入Tick处理。
    bool bFailed=false,bFinished=false; // 首错锁存/退出幂等；失败后不继续交易。
    bool bPressedShot=false,bRevealShot=false; // 每类固定图只请求一次，包含实际机械采样。
    bool bLongHoldCrossPassed=false,bEntryReleaseBarrierPassed=false; // 实测释放屏障/跨键持按；不是按设计直接赋true。
    bool bOutlineDigitOneObserved=false,bOutlineSwitchPassed=false; // 实测1→2焦点轮廓切换，不把提示文案变化当轮廓。
    bool bOutlinePressedMotionObserved=false,bOutlineClearedOnExit=false; // 实际253 mask跟按压和关闭后无当前Pawn残留；不是跨玩家隔离验收。
    bool bTemporalRequested=false,bTemporalActive=false,bTemporalComplete=false; // 额外显式flag才运行，无普通截图/RHI/CVar额外工作。
    bool bTemporalOriginalSaved=false,bTemporalRestored=false,bTemporalReferenceReady=false; // 恢复责任/真实恢复证据/首次采样参考点。
    int32 TemporalOriginalValue=0,TemporalPhase=0,TemporalWarmFrames=0; // 原jitter整数、ABA段号0..2、真实弃帧计数0..8。
    uint32 TemporalOriginalFlags=0; // IConsoleVariable完整原flags，恢复时不能只还数值却留下Console优先级。
    uint64 TemporalLastFrame=MAX_uint64; // 每GT末帧至多安排一次请求，不在截图委托内递归请求。
    FDelegateHandle TemporalCaptureHandle; // 仅显式probe绑定的截图委托，恢复时只Remove自己，不ClearAll。
    bool bTemporalCapturePending=false,bTemporalCaptureReady=false,bTemporalSamplingFailure=false; // 自有请求在途/回调已退栈待GT处理/明确采样失败。
    uint64 TemporalRequestSerial=0,TemporalCompletedSerial=0,TemporalRequestedFrame=0,TemporalLastCallbackFrame=MAX_uint64; // 自有请求顺序/有效回调帧，禁止重复或跨段混样。
    int32 TemporalPendingPhase=0,TemporalValidDrawnCallbacks=0,TemporalRejectedCallbacks=0; // 请求冻结段号、真实有效及拒绝回调计数。
    double TemporalRequestedAt=0.; // 3秒正常截图回调超时；只取消匹配Filename的自有请求。
    FString TemporalOwnedCaptureName; // FScreenshotRequest归一化后的自有内存请求名，不应生成PNG/EXR。
    TSharedPtr<FJsonObject> TemporalRequestContext; // 当前请求时实际上下文，与绘制回调再次实读结果配对。
    TArray<TSharedPtr<FJsonValue>> TemporalWarmupCaptures; // 每段8条有效暖机回调的帧/序列/真实RGB范围，不保存原像素。
    FName TemporalPart; // 稳定数字0的真实动态PartId，probe全程不按压。
    FString TemporalMaterial; // 真实active MID的Parent材质路径，不按资产名猜。
    TWeakObjectPtr<UPrimitiveComponent> TemporalMask; // 本Avatar唯一已注册253 mask的实际对象身份，三段须相同。
    TWeakObjectPtr<UMaterialInstanceDynamic> TemporalMID; // 实际Camera WeightedBlendables中的唯一匹配MID，三段须相同。
    FTransform TemporalCameraReference,TemporalFinalViewReference,TemporalSourceReference,TemporalOutlineReference; // 第一有效采样的实际组件Camera/最终POV/源/mask世界框，跨ABA共享基准。
    float TemporalComponentFOV=0.f,TemporalFinalFOV=0.f; // 第一采样的真实组件与最终相机FOV度数，不能仅核ControlRotation。
    TArray<FColor> TemporalPixels; // 正常截图回调内从1280x720内存图提取96x96，复用临时缓冲，不直接读RHI或落盘。
    TArray<uint8> TemporalFirstMask,TemporalPreviousMask; // 每段第一/上一白像素二值mask，9216字节×2，不保存逐帧图。
    TArray<uint8> TemporalWhiteCounts,TemporalFlipCounts; // 每段16个有效测量回调的逐像素白计数0..16/相邻翻转0..15；各9216字节，不含8个暖机回调。
    TArray<TSharedPtr<FJsonValue>> TemporalSamples,TemporalPhases; // 每段16条小统计与3段汇总，数量有界。
    TSharedPtr<FJsonObject> TemporalMetadata; // 仅probe存在的运行CVar/身份/ROI说明；Final报告合并实际恢复结果。
    TStrongObjectPtr<UDemoHubShopDefinition> CandidateDefinition; // 只持当前World实际DA，临时public软引用恢复前不得GC。
    TStrongObjectPtr<UMaterial> CandidateMaterial; // 只允许固定T02资产，无任意路径/T01/fallback。
    TSoftObjectPtr<UMaterialInterface> CandidateOriginalMaterial; // DA原软引用精确副本；不Modify/MarkDirty/Save。
    FDelegateHandle CandidateCleanupHandle; // 自有WorldCleanup弱UObject句柄，仅移除自己。
    bool bCandidateRequested=false,bCandidateApplied=false,bCandidateRestored=false,bCandidateOriginalDirty=false; // 显式模式/借用责任/真实恢复证据/原包必须干净。
    bool bCandidateShaderReadinessLogged=false; // 仅首次实际World末帧与超时记录shader状态，避免常态逐帧刷屏。
    bool bCandidateWidthSaved=false,bCandidateCameraSaved=false,bCandidateGesture=false; // width与视向恢复责任、当前按压组是否已正常注入一次手势。
    float CandidateOriginalWidth=0.f; // 同一实际MID的原有效OutlineWidthPx（要求2），退出恢复有效值。
    FRotator CandidateOriginalRotation; // 实验前真实ControlRotation；慢转头结束恢复，不改生产相机架构。
    FVector CandidatePawnLocation; // 六组固定真实Pawn位置cm，不以相机ControlRotation替代位置门禁。
    double CandidatePhaseStarted=0.,CandidateGestureStarted=0.,CandidateNextSample=0.,CandidateYawChangedAt=0.; // 真实暖机>=1秒/真实手势/采样>=.04秒/实际POV稳定等待。
    double CandidateYawOffset=0.,CandidateMaximumPressTravel=0.; // 慢yaw当前计划偏移±.15deg，以及按压组实读源运动cm。
    int32 CandidateRequestBefore=0; // 两次正常digit0按压不得产生经济请求，最终草稿仍为0。
    TArray<TArray<FColor>> CandidateOffPixels; // 每对off组最多16个96x96 RGB帧，只留内存约.6MiB，下一对覆盖，不导出逐帧像素。
    TArray<FTransform> CandidateOffSources; // 与off图一一对应的真实源姿态，press最近邻仅部分配对须明示。
    TArray<double> CandidateOffYaws; // 与off图对应的计划yaw，slow仅同序同yaw配对。
    TArray<uint8> CandidateEffectCounts,CandidateEffectFlipCounts,CandidatePreviousEffectMask; // 仅fixed_on的9216格effect0..16/flip0..15/前有效mask，零区域为AABB诊断框外，不落逐帧图。
    TArray<FColor> CandidatePreviousEffectPixels; // 仅fixed_on上一有效on ROI，36KiB临时RGB用于effect并集强度差标量；不输出原像素。
    bool bGunTarget=false; // 仅显式DemoOutlineTarget=RifleOblique且T02模式；默认Keycap原六组不变。
    TWeakObjectPtr<ADemoGunDisplay> GunDisplay; // 固定注册gunshop_rifle_01，必须DA商品WeaponId=rifle。
    TWeakObjectPtr<UPrimitiveComponent> GunSource; // 实际GetDisplayMesh源，全部组冻结对象身份/模型/材料/世界框。
    FVector GunAimPoint; // 实际注册Focus中心，原准星Visibility射线必须先击中该展示。
    double GunSettleStarted=0.,GunObliqueAngle=0.,GunFocusDistance=0.; // 原视图/Focus正常生成最多2真实秒；实测斜角deg与眼位到Focus距离cm。
    int32 GunInitialCoins=0,GunInitialSerial=0; // 完成初始隔离预算后固定经济基线，整个枪实验不得交易。
#endif
};
