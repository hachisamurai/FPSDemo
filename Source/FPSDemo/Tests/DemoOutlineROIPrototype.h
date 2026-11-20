// 职责：T03显式Editor独立Game的Owner ROI原生桥；正式配置、DA、输入和高亮资产不变。
// 使用：仅-DemoOutlineROIPrototype及专用Viewport类创建World服务；固定单Local/1280x720/SDR。
// 依赖：UE5.4.4公开SceneCapture、FinalizeViews与SceneViewExtension；材质由独立TA真实Editor创建。
#pragma once

#include "CoreMinimal.h"
#include "CommonGameViewportClient.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "PrimitiveComponentId.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoOutlineROIPrototype.generated.h"

class UCameraComponent;
class UMaterial;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UTextureRenderTarget2D;
class FSceneView;
class FSceneViewFamily;
class FDemoOutlineROIViewExtension;
struct FDemoOutlineROIRenderState;

/** 仅GT读回的一帧提交证据；提交成功不等于GPU像素/艺术验收，失败会保留具体原因。 */
struct FDemoOutlineROIFrameAudit
{
    uint64 Frame = 0; // 本次实际GT GFrameCounter；仅同一绘制帧有效，不跨World。
    uint32 Generation = 0; // 本机绑定目标代数；换Mask/Pawn/清理递增，不是网络协议。
    uint32 OwnerId = 0; // 实际ViewActor/Pawn对象ID，非持久身份。
    uint32 MaskId = 0; // 实际生产253私有Mask对象ID，非测试直接造的轮廓。
    FIntRect ROI=FIntRect(0,0,0,0); // 输出物理像素整数区域，Max exclusive；RT texel与区域像素一一对应。
    FIntPoint Output = FIntPoint::ZeroValue; // 本次真实主View输出；首轮只接受1280x720。
    FVector Origin = FVector::ZeroVector; // 最终GT主View眼位，世界cm。
    FRotator Rotation = FRotator::ZeroRotator; // 最终GT主View角度，度。
    int32 MainAA = INDEX_NONE; // 实际FSceneView::AntiAliasingMethod，不使用CVar冒充。
    int32 CaptureAA = INDEX_NONE; // 捕获BeginRenderViewFamily实际FSceneView AA。
    FVector2D CaptureJitter = FVector2D::ZeroVector; // 捕获实际ViewMatrices的NDC jitter。
    bool CaptureViewObserved = false; // 原生捕获建View后被公开扩展实读；不是“已调用Capture”标记。
    bool CaptureHasViewState = false; // 标准PP要求State，持久渲染State与关闭AA/jitter是独立条件。
    int32 ActualCaptureMaskWidth = INDEX_NONE; // 本帧标准Capture实际node的0/2，不把请求值当渲染值。
    float MainNearDistance = 0.f; // 实际主View近面距离cm；当前投影不变。
    float CaptureNearDistance = 0.f; // 公开View重建后重新派生的捕获近面距离cm。
    bool CaptureNearSynchronized = false; // 世界近面/距离由本帧VP重新提取并与主View核同。
    bool MainViewRechecked = false; // BeginRenderViewFamily再次核实际主View，晚于FinalizeViews。
    bool Submitted = false; // clear→Capture→main提交门通过；不代表GPU完成或像素白/2px验收。
    bool OutputEnabled = true; // 原型0/2 on/off，仅Composite开关，不改变捕获edge。
    bool CaptureRenderObserved = false; // 同frame的PrePostProcess RT只读见证；不是GPU像素帧号。
    bool MainRenderObserved = false; // 主View同frame RT只读见证，不从渲染线程访问UObject。
    int32 CaptureRenderAA = INDEX_NONE; // 捕获实际进入后处理时AA；由线程安全快照读回。
    FVector2D CaptureRenderJitter = FVector2D::ZeroVector; // 捕获PrePostProcess时实际投影jitter。
    uint64 CaptureRenderSequence = 0; // 同服务渲染线程观察顺序，仅证明CPU渲染提交顺序。
    uint64 MainRenderSequence = 0; // 应晚于CaptureRenderSequence，不冒充GPU同帧完成。
    bool RenderViewsMatchSubmission = false; // 同帧RT前后处理实读的Owner/矩阵/零捕获jitter与GT提交一致。
    int32 HiddenPrimitiveCount = 0; // 主View隐藏集同步给Capture，保本地隐藏组件语义，不ShowOnly排除遮挡物。
    FString Reason = TEXT("not submitted"); // 当前拒绝/等待原因；状态变化才业务打印，逐帧埋点仍保留。
};

/** 瞬态捕获必须显式返回Pawn ViewOwner；仅SetOwner并不能改变引擎默认nullptr行为。 */
UCLASS(Transient)
class FPSDEMO_API UDemoOutlineROICaptureComponent : public USceneCaptureComponent2D
{
    GENERATED_BODY()
public:
    /** 默认构造关闭自动捕获/全部AA/运动模糊，保标准PP所需ViewState；不创建资源/World。 */
    UDemoOutlineROICaptureComponent(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    /** Renderer每次构造Capture View时取拥有Actor的Pawn Owner，不向其他View开放OnlyOwnerSee。 */
    virtual const AActor* GetViewOwner() const override;
};

/** 本机非复制的瞬态捕获Actor；只由显式原型服务生成、销毁。 */
UCLASS(Transient)
class FPSDEMO_API ADemoOutlineROICapture : public ASceneCapture2D
{
    GENERATED_BODY()
public:
    /** 替换原生Capture默认子对象，不改变引擎或正式SceneCapture类。 */
    ADemoOutlineROICapture(const FObjectInitializer& Initializer = FObjectInitializer::Get());
};

/** 仅独立试验启动器选择此CommonUI派生类；非Editor没有调试注册或创建原型。 */
UCLASS(Transient)
class FPSDEMO_API UDemoOutlineROIViewportClient : public UCommonGameViewportClient
{
    GENERATED_BODY()
public:
    /** 仍先执行CommonUI原行为，再将已创建的实际View借给本World原型；不安装Camera PP。 */
    virtual void FinalizeViews(FSceneViewFamily* Family, const TMap<ULocalPlayer*, FSceneView*>& Views) override;
};

/** 原型资源/目标的唯一World所有者；没有RPC、保存、正式目录写入或运行时任意资产路径。 */
UCLASS(Transient)
class FPSDEMO_API UDemoOutlineROIPrototype : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 显式Editor -game+目标地图+Standalone才创建，普通PIE及所有非Editor目标一律false。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** 只支持独立Game World；不在启动临时/Editor World中借用资源。 */
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    /** 初始化弱World扩展和固定候选引用；不修改正式DA或配置。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** 每帧只处理先前请求的RT尺寸/对象有效性；真正捕获由最终View入口提交。 */
    virtual void Tick(float DeltaTime) override;
    /** 引擎Tick统计标识，无额外业务状态。 */
    virtual TStatId GetStatId() const override;
    /** World卸载精确撤本MID、销毁本Capture/RT并恢复借用的生产MID宽度。 */
    virtual void Deinitialize() override;

    /** 夹具在正常焦点已生成唯一Mask后绑定；Camera/Mask须同World且属于同一本机Pawn。
     * 绑定前安装Composite，避免FinalizeViews临时加Camera PP晚一帧；返回false留原因。
     */
    bool BindTarget(UCameraComponent* Camera, UPrimitiveComponent* Mask);
    /** 夹具失焦、换目标、阴性或结束调用；幂等，仅释放此服务资源，不清他人的Blendables。 */
    void ClearTarget(const TCHAR* Reason);
    /** 夹具结束先ClearTarget再恢复唯一借用的SecondaryScreenPercentage原值/full flags；幂等，不写ini。 */
    bool RestoreProcessSettings();
    /** 同pass on/off：false设Composite宽0，true设2，不膨胀RT也不改正式资产。 */
    void SetOutputEnabled(bool Enabled);
    /** 显式Editor有限因果见证调用，只接受Mask宽0/2；Composite不变，下帧实际node仍须实读核对。 */
    bool SetCaptureMaskWidthForAudit(int32 Width);
    /** 专用Viewport只借用当前调用栈View；核门禁、建立整数ROI并按clear→Capture提交。 */
    void FinalizePrototypeView(FSceneViewFamily* Family, const TMap<ULocalPlayer*, FSceneView*>& Views);
    /** 公开扩展在真实Capture/main BeginRenderViewFamily读回最终GT状态；不从RT访问UObject。 */
    void ObserveViewFamily(FSceneViewFamily& Family);
    /** 夹具只读上一实际绘制帧，不触发捕获或伪造Submitted；返回是否已有帧记录。 */
    bool ReadFrameAudit(FDemoOutlineROIFrameAudit& Out) const;
    /** 夹具读唯一私有RT用于正常截图之后的有限技术读回；资源归服务，不得保存或销毁。 */
    UTextureRenderTarget2D* ReadMaskRT() const;
    /** 固定候选加载/shader准备失败或对象失效原因；空串不代表全部渲染验收。 */
    FString ReadFailure() const;
private:
    /** 关闭当前合成并透明清本RT；拒绝理由变化才输出Log，保留高频入口埋点。 */
    void RejectFrame(const FString& Reason);
    /** 在普通Tick/绑定而非FinalizeViews创建full-view参考的瞬态线性RGBA8资源；无目标不分配。 */
    bool AllocatePendingRT();
    /** 同步加载两条固定候选、核PostProcess/AfterTone和当前shader准备；不保存包。 */
    bool LoadMaterials();
    /** 给本Camera添加/撤销本Composite；借用生产A04 MID只临时宽0，退出还原自身快照。 */
    bool InstallComposite();
    /** Slate本进程失去激活立即清本目标；弱UObject委托，卸载前解除，离屏试验不伪造物理焦点验收。 */
    void OnApplicationActivationChanged(bool Active);

    UPROPERTY(Transient) TObjectPtr<UMaterial> MaskMaterial; // 固定TA Capture PP强引用，未编辑/保存。
    UPROPERTY(Transient) TObjectPtr<UMaterial> CompositeMaterial; // 固定TA主合成PP，默认CaptureValid0。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MaskMID; // 本Capture的参数实例，无共享材质修改。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CompositeMID; // 本Camera唯一新增的合成项。
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> MaskRT; // 仅当前target generation瞬态RT，线性/nearest/noMip。
    UPROPERTY(Transient) TObjectPtr<ADemoOutlineROICapture> CaptureActor; // 本World生成，无Tick/复制/碰撞。
    UPROPERTY(Transient) TArray<TObjectPtr<UTextureRenderTarget2D>> RetiredRTs; // 旧提交仍可能引用，保到服务结束让原生释放队列按序回收。
    TWeakObjectPtr<UCameraComponent> BoundCamera; // 目标Pawn实际第一人称Camera，非Player0推断。
    TWeakObjectPtr<UPrimitiveComponent> BoundMask; // 正常Focus/LocalView创建的唯一253私有副本，不由原型复制源。
    TWeakObjectPtr<APawn> BoundPawn; // Capture真实ViewOwner，仅同World本机Pawn。
    TWeakObjectPtr<UMaterialInstanceDynamic> BorrowedProductionMID; // 只暂时静音A04 pass，不改变DA或原材质。
    TWeakObjectPtr<UMaterialInstanceDynamic> CurrentViewMID; // 本帧实际FinalPP node的MID，原生Camera实例的拷贝；仅GT同步改参数。
    float BorrowedWidth = 0.f; // Bind时生产MID实际OutlineWidthPx，清理仅当仍为0才还原。
    FIntPoint PendingSize = FIntPoint::ZeroValue; // Finalize计算的下一Tick RT物理尺寸；0不分配。
    FMatrix SubmittedView = FMatrix::Identity; // 当前代同帧实际主ViewMatrix，供捕获与晚主View复核。
    FMatrix SubmittedProjection = FMatrix::Identity; // 当前代实际主View无jitter投影，保原Near与非对称项。
    FPlane SubmittedNearPlane = FPlane(0,0,0,0); // 主View实际世界近面，规范重建后捕获必须同源。
    TSet<FPrimitiveComponentId> SubmittedHidden; // 只复制实际主View隐藏集；Capture默认不会继承PC本地隐藏。
    const class FRenderTarget* SubmittedMainTarget = nullptr; // 仅比较当前主Family目标身份，不跨线程解引用。
    FDemoOutlineROIFrameAudit LastFrame; // 有限GT提交证据；无跨线程裸指针。
    TSharedPtr<FDemoOutlineROIViewExtension, ESPMode::ThreadSafe> ViewExtension; // 弱UObject世界扩展，清理先停合成再释放共享引用。
    TSharedPtr<FDemoOutlineROIRenderState, ESPMode::ThreadSafe> RenderState; // RT只读View字段的锁保护值快照，绝不持有/访问UObject。
    FString Failure; // 锁存初始化/资源失败原因；不作为用户游戏UI。
    FString LastReject; // 相同逐帧拒绝只Log一次，默认日志不刷屏。
    FDelegateHandle ActivationHandle; // Slate弱UObject激活变化委托，仅此服务拥有；Deinit精确撤销。
    uint32 Generation = 0; // 目标换代单调计数，仅当前World诊断。
    int32 RequestedCaptureMaskWidth = 2; // 仅Editor脉冲见证临时0/2，不写材质资产或正式DA。
    float OriginalSecondaryValue = 0.f; // 显式独立进程临时固定Secondary100前原浮点值。
    uint32 OriginalSecondaryFlags = 0; // 原完整CVar flags，退出按原字节语义恢复，不写配置。
    bool SecondarySaved = false; // 仅Initialize成功借用时才恢复，非Editor不会设置。
    bool OutputEnabled = true; // 本原型0/2对照，无生产配置语义。
    bool MaterialsReady = false; // 两固定候选实际shader map就绪，不等于像素验收。
    bool MaterialLoadAttempted = false; // 固定两候选仅Load/标准Cache一次，等待期间不每帧重发编译。
    double MaterialWaitStarted = 0.; // 首次候选shader请求真实秒；15秒准备门不覆盖整个夹具90秒守门。
    bool FrameRejected = false; // 一帧只保首个真实拒绝，后续级联不覆盖原因/重复日志。
    bool MismatchLogged = false; // 每个target generation只输出首个矩阵/Owner详细快照，不默认逐帧刷屏。
    bool CanonicalViewLogged = false; // 每代只记一次原生Capture构造与公开视图重建前后矩阵幅差。
};
