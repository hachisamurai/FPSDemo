// 职责：每个本机PC的正常游戏轮廓捕获/合成生命周期；枪墙与实体按钮只提交本人的Source/253 Mask。
// 使用：PC默认子对象，生产Viewport在主View建立后调用FinalizeLocalView；不依赖Editor审查命令或固定尺寸。
// 依赖：UE5.4公开SceneCapture/ViewExtension/Renderer接口；正式DA两材质由Unreal Editor创建。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "PrimitiveComponentId.h"
#include "RenderCommandFence.h"
#include "DemoLocalOutlineComponent.generated.h"

class APawn;
class UCameraComponent;
class ULocalPlayer;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UTexture;
class UTextureRenderTarget2D;
class FSceneView;
class FSceneViewFamily;
class FRenderTarget;
class FDemoLocalOutlineViewExtension;
struct FDemoLocalOutlineRenderState;

/** 标准手动Capture保留PP所需State，明确关闭AA/运动模糊；实际ViewOwner为本机Pawn。 */
UCLASS(Transient)
class FPSDEMO_API UDemoLocalOutlineCaptureComponent : public USceneCaptureComponent2D
{
    GENERATED_BODY()
public:
    /** 引擎默认子对象构造；不自动捕获/移动捕获，不改主视图AA。 */
    UDemoLocalOutlineCaptureComponent(const FObjectInitializer& Initializer=FObjectInitializer::Get());
    /** 每次渲染器建View返回Actor的Pawn Owner，保持OnlyOwnerSee隔离。 */
    virtual const AActor* GetViewOwner() const override;
};

/** 正常本机渲染服务拥有的无复制瞬态Actor，非调试入口/不保存进地图。 */
UCLASS(Transient)
class FPSDEMO_API ADemoLocalOutlineCapture : public ASceneCapture2D
{
    GENERATED_BODY()
public:
    /** 替换原生Capture子组件，关闭Actor碰撞/Tick，所有捕获由最终主View显式提交。 */
    ADemoLocalOutlineCapture(const FObjectInitializer& Initializer=FObjectInitializer::Get());
};

/** 本PC一组私有资源；多LocalPlayer各自持有，远端PC和专服不创建Capture或PP。 */
UCLASS(ClassGroup=(Demo),meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoLocalOutlineComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** PC默认子对象创建；本机逐帧仅验证请求/处理尺寸与异步退役，不计算玩法或经济。 */
    UDemoLocalOutlineComponent();
    /** 本机World扩展与弱失焦委托就绪；不加载任意资产/更改用户设置。 */
    virtual void BeginPlay() override;
    /** Delta为引擎秒，仅生命周期检查/资源就绪，不依赖World暂停推进表现。 */
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function) override;
    /** 退出/旅行只移除本组件的MID与Capture；ViewState随组件注销原生释放。 */
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

    /** 可信本机Focus/LocalView提交；true表示接受所有权（包括等待shader/resize），不是像素验收。
     * Requester是撤销凭据，Viewer/Camera/Source/Mask须同World且Mask归本人Pawn；不校验战斗技能门。
     */
    bool SetOutlineTarget(UObject* Requester,APawn* Viewer,UCameraComponent* Camera,UPrimitiveComponent* Source,UPrimitiveComponent* Mask,UMaterialInterface* Composite,UMaterialInterface* CaptureMask);
    /** 仅匹配当前Requester才清理；旧Focus的迟到清理不能撤新Operate目标。 */
    void ClearOutlineTarget(UObject* Requester);
    /** 只读逻辑请求所有权，与RT是否ready/当前尺寸无关，避免resize重建Mask。 */
    bool OwnsOutlineTarget(const UObject* Requester,const UPrimitiveComponent* Mask) const;
    /** 返回本请求的稳定Composite MID供原诊断读回；非本人或无请求返回空，不代表渲染已提交。 */
    UMaterialInstanceDynamic* GetCompositeForOwner(const UObject* Requester) const;
    /** 正常生产Viewport调用，借用本帧活View；不持有裸View/不修改主相机投影或设置。 */
    void FinalizeLocalView(FSceneViewFamily* Family,ULocalPlayer* Player,FSceneView* View);
    /** 公开扩展GT在Capture/main真正提交前核/规范Capture View；不由其它系统直接调用。 */
    void ObserveViewFamily(FSceneViewFamily& Family);
#if WITH_EDITOR
    /** 显式正常入口Smoke只读最近两帧的原生Capture+main接受结果；不读GPU像素/不改变目标或资源。 */
    bool ReadLastSubmissionForSmoke(uint64& Frame,FIntPoint& Stage) const;
#endif
private:
    /** 本机Game/PIE按需建立一次公开View扩展；生命周期可早于其他默认PC组件请求。 */
    bool EnsureRenderBridge();
    /** 无条件清本资源、递增代数并撤自己的Camera项；只由本服务生命周期或已核Requester调用。 */
    void ClearCurrentTarget(const FString& Reason);
    /** 当前View关闭参数并透明清当前RT；理由变化才输出业务日志，普通逐帧保TICK埋点。 */
    void CloseFrame(const FString& Reason);
    /** 只在普通Tick创建/替换实际stage尺寸RT；等待退役队列时暂不显示，不拉伸旧RT。 */
    bool PrepareResources();
    /** 当前两个正式材质的实际FeatureLevel资源是否就绪；Editor一次标准缓存，不逐帧同步编译。 */
    bool MaterialsReady();
    /** 当前RT从Capture/MID解绑并挂渲染命令Fence，完成后交正常GC释放。 */
    void RetireCurrentRT();
    /** GT轮询各自Fence，仅回收已过渲染队列的本组件引用，不等待GPU或清他人资源。 */
    void CollectRetiredRTs();
    /** Slate失焦只关输出/退役RT，保留逻辑请求；弱UObject绑定，EndPlay精确移除。 */
    void OnApplicationActivationChanged(bool Active);

    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CompositeParent; // 当前DA正式Composite强引用，不编辑/保存。
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CaptureParent; // 当前DA正式Mask强引用，原双深度门保持。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CompositeMID; // 跨同材质目标/resize稳定，Camera只挂此唯一项。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CaptureMID; // Capture标准PP参数实例，默认宽2，不复制到网络。
    UPROPERTY(Transient) TObjectPtr<UTexture> EmptyTexture; // 材质声明的Engine Black默认纹理，撤目标时替代旧RT引用。
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> MaskRT; // 当前stage大小、RGBA8线性/Nearest/Clamp/noMip瞬态纹理。
    UPROPERTY(Transient) TObjectPtr<ADemoLocalOutlineCapture> CaptureActor; // 当前Avatar的私有手动Capture，Actor Owner就是ViewOwner。
    UPROPERTY(Transient) TArray<TObjectPtr<UTextureRenderTarget2D>> RetiredRTs; // 强保已提交旧资源，Fence完成才移除本引用。
    TArray<TUniquePtr<FRenderCommandFence>> RetirementFences; // 与RetiredRTs同索引，仅GT操作，每个旧RT独立排队。
    TWeakObjectPtr<UObject> RequestOwner; // 逻辑请求者，弱引用不延长Focus/LocalView生命周期。
    TWeakObjectPtr<APawn> ViewerPawn; // 本PC当前Avatar，Pawn/World/ViewTarget变更立即撤销。
    TWeakObjectPtr<UCameraComponent> ViewCamera; // 真正已安装Composite的Camera，撤销精确Remove本项。
    TWeakObjectPtr<UPrimitiveComponent> SourceMesh; // 正常表现原件，只读且不改变其材质/Stencil。
    TWeakObjectPtr<UPrimitiveComponent> TargetMask; // 原工厂产生的本人253副本，仍由原Requester拥有/销毁。
    TWeakObjectPtr<UMaterialInstanceDynamic> ActualViewMID; // 本帧主FinalPP已经建立的复用MID，仅同步GT访问。
    TSharedPtr<FDemoLocalOutlineViewExtension,ESPMode::ThreadSafe> ViewExtension; // 本World扩展弱持本组件，RT只用纯值State。
    TSharedPtr<FDemoLocalOutlineRenderState,ESPMode::ThreadSafe> RenderState; // stage元数据邮箱；锁内不调用UObject/RHI。
    FMatrix MainView=FMatrix::Identity; // 当前已提交主View数学基准，世界cm，不跨线程读取。
    FMatrix MainProjection=FMatrix::Identity; // 主无jitter RHI投影，Capture输入另用Unadjusted矩阵。
    FPlane MainNearPlane=FPlane(0,0,0,0); // 主世界近面，用于Capture重建后的派生一致性门。
    TSet<FPrimitiveComponentId> MainHidden; // 镜像本LocalPlayer实际隐藏集合，保持普通世界遮挡。
    FVector MainOrigin=FVector::ZeroVector; // 本帧实际主眼位cm，不读取Player0代表别人。
    FRotator MainRotation=FRotator::ZeroRotator; // 本帧实际主视向，未初始化的Capture metadata不作来源。
    FIntPoint PendingStage=FIntPoint::ZeroValue; // 从实际AfterTone观察获得的下一次资源尺寸；非窗口总像素。
    FIntPoint SubmittedStage=FIntPoint::ZeroValue; // 本帧Capture与主Composite相同stage网格。
    const FRenderTarget* MainRenderTarget=nullptr; // 仅比较主Viewport身份，不从RT解引用。
    uint64 SubmissionFrame=0; // 当帧GFrameCounter，拒绝跨帧/旧代Capture授权。
    uint64 LastAcceptedMainFrame=0; // 最近Capture和main均通过的GT帧；关闭立即清零，非GPU像素凭证。
    uint32 Generation=0; // 每本PC目标代数；不属于网络/存档协议。
    uint32 LastReportedGeneration=MAX_uint32; // 每目标仅一次接受日志，避免每帧刷屏。
    float MainNearDistance=0.f; // 当前主近面距离cm，保原投影/距离不改。
    float MainFOV=90.f; // 当前主FOV/LOD metadata，Capture使用同值。
    FString LastCloseReason; // 默认只输出状态变化，避免高频日志膨胀。
    FDelegateHandle ActivationHandle; // 本World弱Slate委托，卸载前移除。
    bool bCaptureAccepted=false; // 本次Capture实际View门通过，不以仅调用CaptureScene授权。
    bool bFrameClosed=false; // 当帧首错锁存，后续级联不覆盖或刷多条日志。
    bool bShaderCacheRequested=false; // 当前两父材质只触发一次标准Editor缓存。
    bool bApplicationActive=true; // 激活事件更新；暂失焦只关输出/资源，逻辑请求仍由原Caller掌握。
};
