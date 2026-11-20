// 职责：T03隔离full-view参考的本机捕获/主合成桥，给TA纯白描边候选提供真实Game接点。
// 边界：无正式引用/资产/网络修改；只用Engine公开View/Blendable接口，不访问Renderer私有头。
#include "Tests/DemoOutlineROIPrototype.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/BlendableInterface.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "UnrealClient.h"

namespace
{
    const FIntPoint ReferenceSize(1280,720); // 首轮合法full-view参考，非ROI裁剪/性能优化。
    const TCHAR* MaskPath=TEXT("/Game/Developers/OutlineTemporal20261116/T03/M_HubOutline_ROI_Mask_T03.M_HubOutline_ROI_Mask_T03"); // TA固定白名单，非客户端路径。
    const TCHAR* CompositePath=TEXT("/Game/Developers/OutlineTemporal20261116/T03/M_HubOutline_ROI_Composite_T03.M_HubOutline_ROI_Composite_T03"); // 只有显式原型会Load。
    const TCHAR* ProductionPath=TEXT("/Game/Data/HubShops/M_HubGunSilhouette_PP.M_HubGunSilhouette_PP"); // 只辨认现有A04实例，不改其资产。

    /** GT在已经建立的FinalPP中找唯一候选MID；引擎从Camera实例复制参数，不能只更新原实例。 */
    UMaterialInstanceDynamic* FindViewMID(FSceneView& View,const UMaterial* Base)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        UMaterialInstanceDynamic* Found=nullptr; // 只在同步View生命周期内借用，返回值由ViewState持有。
        FBlendableEntry* Iterator=nullptr; // 公开BlendableManager迭代游标，不保留跨帧node地址。
        while(FPostProcessMaterialNode* Node=View.FinalPostProcessSettings.BlendableManager.IterateBlendables<FPostProcessMaterialNode>(Iterator))
        {
            UMaterialInterface* Material=Node->GetMaterialInterface(); // 当前View实际将提交的材质引用。
            if(Material&&Material->GetMaterial()==Base)
            {
                if(Found) return nullptr; // 不猜测重复候选的顺序；原型要求唯一。
                Found=Cast<UMaterialInstanceDynamic>(Material);
                if(!Found) return nullptr;
            }
        }
        return Found;
    }

    /** 同帧数值一致性门，不扩大任何深度容差；矩阵double构建误差单独限定。 */
    bool SameView(const FSceneView& View,const FMatrix& ExpectedView,const FMatrix& ExpectedProjection)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        return View.ViewMatrices.GetViewMatrix().Equals(ExpectedView,1.e-5)&&View.ViewMatrices.ComputeProjectionNoAAMatrix().Equals(ExpectedProjection,1.e-6);
    }

    /** 只作首错数值证据：16元素最大绝对差；不以此自动提高验收容差或深度epsilon。 */
    double MatrixDelta(const FMatrix& Actual,const FMatrix& Expected)
    {
        DEMO_LOG_STATIC_TICK(nullptr); double Maximum=0.; // 同步局部double，不保留矩阵引用。
        for(int32 Row=0;Row<4;++Row) for(int32 Column=0;Column<4;++Column) Maximum=FMath::Max(Maximum,FMath::Abs(Actual.M[Row][Column]-Expected.M[Row][Column]));
        return Maximum;
    }
}

/** 只含值/不解引用的身份指针；扩展RT回调不碰UObject或GT服务。 */
struct FDemoOutlineROIRenderState
{
    struct FObserved
    {
        uint64 Frame=0; // 原生ViewFamily.FrameCounter，不用GT此刻时钟推断渲染帧。
        uint64 Sequence=0; // 此服务RT后处理观察顺序，非GPU timestamp。
        int32 AA=INDEX_NONE; // 实际进入后处理的View AA。
        FVector2D Jitter=FVector2D::ZeroVector; // 同阶段实际投影jitter。
        FMatrix View=FMatrix::Identity; // 数值快照，不持有FSceneView。
        FMatrix Projection=FMatrix::Identity; // 去jitter实际RHI投影快照。
        FIntRect Rect=FIntRect(0,0,0,0); // 公开UnscaledViewRect；后处理真实网格还由材质Input0检测。
        bool HasNearPlane=false; // 实际进入后处理时近面派生状态，非默认构造字段。
        float NearDistance=0.f; // 世界cm，源投影保持。
        FPlane NearPlane=FPlane(0,0,0,0); // 当前实际世界近面数值，不含对象引用。
    } Capture,Main;
    FCriticalSection Mutex; // 只保护几份小值；GT/RT无等待GPU、无跨锁调用引擎。
    const void* OwnerIdentity=nullptr; // 仅比较地址，绝不在RT解引用Actor。
    const void* CaptureTarget=nullptr; // 本RT资源身份，防其他SceneCapture混入同World。
    const void* MainTarget=nullptr; // 只比较主Viewport资源身份，避免其它同Owner View覆盖记录。
    bool Enabled=false; // GT清理先关闭，再让已排队RT回调自然释放共享state。
    uint64 Sequence=0; // Mutex保护的本服务RT单调序号。
};

/** 限定本World的公开扩展：GT接实际Capture/main，RT只读最终View，不注册Renderer私有回调。 */
class FDemoOutlineROIViewExtension final : public FWorldSceneViewExtension
{
public:
    /** AutoRegister由引擎提供；只弱持服务，渲染线程另持纯值共享state。 */
    FDemoOutlineROIViewExtension(const FAutoRegister& Register,UWorld* World,UDemoOutlineROIPrototype* Service,TSharedPtr<FDemoOutlineROIRenderState,ESPMode::ThreadSafe> State)
        :FWorldSceneViewExtension(Register,World),Owner(Service),RenderState(MoveTemp(State))
    { DEMO_LOG_STATIC_CALL(Service); }
    /** 无自定义ShowFlags变更；真实配置由捕获组件/主View各自所有。 */
    virtual void SetupViewFamily(FSceneViewFamily& Family) override { DEMO_LOG_STATIC_TICK(nullptr); }
    /** 不修改主View投影，保正式TSR；Capture隐藏集在提交前同步。 */
    virtual void SetupView(FSceneViewFamily& Family,FSceneView& View) override { DEMO_LOG_STATIC_TICK(nullptr); }
    /** GT已建完实际View，核标准Capture Mask实际node和已有主Composite，不生成新的主Camera项。 */
    virtual void BeginRenderViewFamily(FSceneViewFamily& Family) override
    { DEMO_LOG_STATIC_TICK(nullptr); if(UDemoOutlineROIPrototype* Service=Owner.Get()) Service->ObserveViewFamily(Family); }
    /** 最后做GT复核，避免已知其它扩展在本服务之前修改POV后漏查；不承诺处理XR late update。 */
    virtual int32 GetPriority() const override { DEMO_LOG_STATIC_TICK(nullptr); return MIN_int32; }
    /** RT后处理前只读AA/jitter/矩阵/Owner及帧值；不通过Alpha冒充GPU时序，不读取UObject。 */
    virtual void PrePostProcessPass_RenderThread(FRDGBuilder& Builder,const FSceneView& View,const FPostProcessingInputs& Inputs) override
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        FScopeLock Lock(&RenderState->Mutex); // 此回调只复制小值，UObject生命期完全不依赖RT。
        if(!RenderState->Enabled||!View.Family||static_cast<const void*>(View.ViewActor)!=RenderState->OwnerIdentity) return;
        const bool Capture=View.bIsSceneCapture; // 原生View标记，不根据名字猜测。
        if(Capture&&static_cast<const void*>(View.Family->RenderTarget)!=RenderState->CaptureTarget) return;
        if(!Capture&&static_cast<const void*>(View.Family->RenderTarget)!=RenderState->MainTarget) return;
        FDemoOutlineROIRenderState::FObserved& Record=Capture?RenderState->Capture:RenderState->Main; // 仅同服务两份快照。
        Record.Frame=View.Family->FrameCounter; Record.Sequence=++RenderState->Sequence; Record.AA=static_cast<int32>(View.AntiAliasingMethod);
        Record.Jitter=View.ViewMatrices.GetTemporalAAJitter(); Record.View=View.ViewMatrices.GetViewMatrix();
        Record.Projection=View.ViewMatrices.ComputeProjectionNoAAMatrix(); Record.Rect=View.UnscaledViewRect;
        Record.HasNearPlane=View.bHasNearClippingPlane; Record.NearDistance=View.NearClippingDistance; Record.NearPlane=View.NearClippingPlane;
    }
private:
    TWeakObjectPtr<UDemoOutlineROIPrototype> Owner; // 仅GT使用，清理后回调不复活服务。
    TSharedPtr<FDemoOutlineROIRenderState,ESPMode::ThreadSafe> RenderState; // RT唯一可访问的纯值状态，不含拥有UObject引用。
};

UDemoOutlineROICaptureComponent::UDemoOutlineROICaptureComponent(const FObjectInitializer& Initializer):Super(Initializer)
{
    DEMO_LOG_CALL(); bCaptureEveryFrame=false; bCaptureOnMovement=false; bAlwaysPersistRenderingState=true;
    CaptureSource=SCS_FinalColorLDR; bUseCustomProjectionMatrix=true;
    ShowFlags.SetAntiAliasing(false); ShowFlags.SetTemporalAA(false); ShowFlags.SetMotionBlur(false); ShowFlags.SetScreenPercentage(false);
}
const AActor* UDemoOutlineROICaptureComponent::GetViewOwner() const
{ DEMO_LOG_TICK(); const AActor* Actor=GetOwner(); return Actor?Actor->GetOwner():nullptr; }
ADemoOutlineROICapture::ADemoOutlineROICapture(const FObjectInitializer& Initializer)
    :Super(Initializer.SetDefaultSubobjectClass<UDemoOutlineROICaptureComponent>(TEXT("NewSceneCaptureComponent2D")))
{ DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=false; SetReplicates(false); SetActorEnableCollision(false); }
void UDemoOutlineROIViewportClient::FinalizeViews(FSceneViewFamily* Family,const TMap<ULocalPlayer*,FSceneView*>& Views)
{
    DEMO_LOG_TICK(); Super::FinalizeViews(Family,Views);
#if WITH_EDITOR
    if(UWorld* PrototypeWorld=GetWorld()) if(UDemoOutlineROIPrototype* Prototype=PrototypeWorld->GetSubsystem<UDemoOutlineROIPrototype>()) Prototype->FinalizePrototypeView(Family,Views);
#endif
}
bool UDemoOutlineROIPrototype::DoesSupportWorldType(EWorldType::Type Type) const
{ DEMO_LOG_CALL(); return WITH_EDITOR&&Type==EWorldType::Game; }
bool UDemoOutlineROIPrototype::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const UWorld* World=Cast<UWorld>(Outer); // 引擎候选World，不通过此函数创建世界/资源。
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone&&World->GetOutermost()->GetName()==TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")&&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoOutlineROIPrototype"));
#else
    return false;
#endif
}
void UDemoOutlineROIPrototype::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Super::Initialize(Collection);
#if WITH_EDITOR
    RenderState=MakeShared<FDemoOutlineROIRenderState,ESPMode::ThreadSafe>();
    ViewExtension=FSceneViewExtensions::NewExtension<FDemoOutlineROIViewExtension>(GetWorld(),this,RenderState);
    if(FSlateApplication::IsInitialized()) ActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this,&UDemoOutlineROIPrototype::OnApplicationActivationChanged); // 仅本进程GT弱绑定，Deinit解除。
    IConsoleVariable* Secondary=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SecondaryScreenPercentage.GameViewport")); // 唯一允许的进程局部借用，不写Config/主AA。
    if(!Secondary||(Secondary->GetFlags()&ECVF_ReadOnly)) { Failure=TEXT("secondary percentage unavailable/read-only"); return; }
    OriginalSecondaryValue=Secondary->GetFloat(); OriginalSecondaryFlags=static_cast<uint32>(Secondary->GetFlags()); SecondarySaved=true;
    Secondary->Set(100.f,ECVF_SetByConsole);
    if(!FMath::IsNearlyEqual(Secondary->GetFloat(),100.f)) Failure=TEXT("secondary percentage process override rejected");
    UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_INIT secondary_original=%.6f flags=%u actual=%.6f fullview_reference=1280x720"),OriginalSecondaryValue,OriginalSecondaryFlags,Secondary->GetFloat());
#endif
}
TStatId UDemoOutlineROIPrototype::GetStatId() const
{ DEMO_LOG_TICK(); RETURN_QUICK_DECLARE_CYCLE_STAT(UDemoOutlineROIPrototype,STATGROUP_Tickables); }
void UDemoOutlineROIPrototype::Tick(float DeltaTime)
{
    DEMO_LOG_TICK();
    if(!BoundMask.IsValid()) { if(BoundCamera.IsValid()||CaptureActor) ClearTarget(TEXT("bound mask invalidated")); return; }
    APawn* Pawn=BoundPawn.Get(); // GT短时借用，Avatar变化不沿用旧目标。
    if(!Pawn||!Pawn->IsLocallyControlled()||!BoundCamera.IsValid()||BoundMask->GetOwner()!=Pawn) { ClearTarget(TEXT("owner/camera changed")); return; }
    if(!MaskRT&&PendingSize.X>0&&!AllocatePendingRT()) RejectFrame(Failure);
}
bool UDemoOutlineROIPrototype::LoadMaterials()
{
    DEMO_LOG_TICK();
#if !WITH_EDITOR
    return false; // 此显式Editor候选加载/准备入口不在游戏包体启用；不是修补一个已证实Game编译错误。
#else
    if(MaterialsReady) return true;
    if(!MaterialLoadAttempted)
    {
        MaterialLoadAttempted=true; MaterialWaitStarted=FPlatformTime::Seconds();
        MaskMaterial=LoadObject<UMaterial>(nullptr,MaskPath); CompositeMaterial=LoadObject<UMaterial>(nullptr,CompositePath);
        for(UMaterial* Material:{MaskMaterial.Get(),CompositeMaterial.Get()}) // 仅固定两候选，一次正常缓存；不改StateId/Save。
        {
            if(!Material||Material->MaterialDomain!=MD_PostProcess||Material->BlendableLocation!=BL_SceneColorAfterTonemapping) { Failure=TEXT("fixed T03 material missing/domain/location mismatch"); return false; }
            const FMaterialResource* Resource=Material->GetMaterialResource(GetWorld()->GetFeatureLevel()); // 实际本World资源而非只看资产存在。
            if(!Resource||!Resource->IsGameThreadShaderMapComplete()) Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
            UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_SHADER_REQUEST material=%s feature=%d compiling=%d"),*Material->GetPathName(),static_cast<int32>(GetWorld()->GetFeatureLevel()),Material->IsCompiling());
        }
    }
    for(UMaterial* Material:{MaskMaterial.Get(),CompositeMaterial.Get()})
    {
        const FMaterialResource* Resource=Material?Material->GetMaterialResource(GetWorld()->GetFeatureLevel()):nullptr; // 一次Cache后的正常逐帧状态轮询。
        if(Resource&&Resource->GetCompileErrors().Num()>0) { Failure=TEXT("T03 shader compile error"); return false; }
        if(!Resource||!Resource->IsGameThreadShaderMapComplete())
        { if(FPlatformTime::Seconds()-MaterialWaitStarted>15.) Failure=TEXT("T03 actual shader readiness15s timeout"); return false; }
    }
    MaskMID=UMaterialInstanceDynamic::Create(MaskMaterial,this); CompositeMID=UMaterialInstanceDynamic::Create(CompositeMaterial,this);
    if(!MaskMID||!CompositeMID) { Failure=TEXT("T03 MID creation failed"); return false; }
    MaskMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),2.f); CompositeMID->SetScalarParameterValue(TEXT("CaptureValid"),0.f);
    MaterialsReady=true; return true;
#endif
}
bool UDemoOutlineROIPrototype::InstallComposite()
{
    DEMO_LOG_CALL(); UCameraComponent* Camera=BoundCamera.Get(); if(!Camera||!CompositeMID) return false;
    UMaterialInstanceDynamic* Production=nullptr; // 实际Camera当前唯一A04 MID；只临时静音，不热改DA或正式源。
    for(const FWeightedBlendable& Item:Camera->PostProcessSettings.WeightedBlendables.Array)
    {
        UMaterialInstanceDynamic* MID=Cast<UMaterialInstanceDynamic>(Item.Object); // 仅借用此相机现有项。
        if(MID&&MID->GetMaterial()&&MID->GetMaterial()->GetPathName()==ProductionPath)
        { if(Production) { Failure=TEXT("multiple production outline MIDs"); return false; } Production=MID; }
    }
    if(!Production||!Production->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineWidthPx")),BorrowedWidth)) { Failure=TEXT("production outline MID/width missing"); return false; }
    BorrowedProductionMID=Production; Production->SetScalarParameterValue(TEXT("OutlineWidthPx"),0.f);
    Camera->PostProcessSettings.AddBlendable(CompositeMID,1.f); // 在LocalPlayer实际View创建前安装，后续只更新真实node。
    return true;
}
bool UDemoOutlineROIPrototype::BindTarget(UCameraComponent* Camera,UPrimitiveComponent* Mask)
{
    DEMO_LOG_TICK();
#if !WITH_EDITOR
    return false;
#else
    APawn* Pawn=Mask?Cast<APawn>(Mask->GetOwner()):nullptr; // 必须是正常生产焦点创建到本Pawn的私有遮罩。
    if(!Failure.IsEmpty()||!Camera||!IsValid(Mask)||!Pawn||!Pawn->IsLocallyControlled()||Pawn->GetWorld()!=GetWorld()||Camera->GetOwner()!=Pawn||!Mask->IsRegistered()||!Mask->bOnlyOwnerSee||!Mask->bRenderCustomDepth||Mask->CustomDepthStencilValue!=253||Mask->bRenderInMainPass)
    { RejectFrame(Failure.IsEmpty()?TEXT("invalid actual local camera/production mask"):Failure); return false; }
    if(BoundMask.Get()==Mask&&BoundCamera.Get()==Camera) return true;
    if(!LoadMaterials()) { RejectFrame(Failure.IsEmpty()?TEXT("waiting for actual T03 shaders"):Failure); return false; }
    ClearTarget(TEXT("bind target generation")); Failure.Reset();
    BoundPawn=Pawn; BoundCamera=Camera; BoundMask=Mask; ++Generation; PendingSize=ReferenceSize; MismatchLogged=false; CanonicalViewLogged=false;
    FActorSpawnParameters Params; // 本World瞬态Actor，不入关卡保存/交易或网络。
    Params.Owner=Pawn; Params.ObjectFlags=RF_Transient; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    CaptureActor=GetWorld()->SpawnActor<ADemoOutlineROICapture>(Params);
    if(!CaptureActor||!AllocatePendingRT()||!InstallComposite()) { if(Failure.IsEmpty()) Failure=TEXT("capture spawn/install failed"); ClearTarget(*Failure); return false; }
    RequestedCaptureMaskWidth=2; MaskMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),2.f); // 新target generation不继承上轮脉冲0。
    CaptureActor->GetCaptureComponent2D()->PostProcessSettings.AddBlendable(MaskMID,1.f); // 标准PP有自己的State，Mask提前安装；不手工构造Capture node。
    CaptureActor->GetCaptureComponent2D()->PostProcessBlendWeight=1.f;
    UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_BIND generation=%u owner=%u mask=%u target=%s"),Generation,Pawn->GetUniqueID(),Mask->GetUniqueID(),*Mask->GetPathName());
    return true;
#endif
}
bool UDemoOutlineROIPrototype::AllocatePendingRT()
{
    DEMO_LOG_CALL(); if(PendingSize!=ReferenceSize||!CaptureActor) { Failure=TEXT("only fixed full-view RT size allowed"); return false; }
    if(MaskRT) return MaskRT->SizeX==ReferenceSize.X&&MaskRT->SizeY==ReferenceSize.Y;
    if(RetiredRTs.Num()>=8) { Failure=TEXT("prototype retired color RT budget reached"); return false; } // 有限试验最多9份4WH颜色，底层场景资源另测。
    MaskRT=NewObject<UTextureRenderTarget2D>(this,NAME_None,RF_Transient);
    MaskRT->ClearColor=FLinearColor::Transparent; MaskRT->bAutoGenerateMips=false; MaskRT->Filter=TF_Nearest; MaskRT->AddressX=TA_Clamp; MaskRT->AddressY=TA_Clamp;
    MaskRT->RenderTargetFormat=RTF_RGBA8; MaskRT->InitCustomFormat(ReferenceSize.X,ReferenceSize.Y,PF_B8G8R8A8,true); MaskRT->UpdateResourceImmediate(true);
    CaptureActor->GetCaptureComponent2D()->TextureTarget=MaskRT;
    UKismetRenderingLibrary::ClearRenderTarget2D(this,MaskRT,FLinearColor::Transparent);
    { FScopeLock Lock(&RenderState->Mutex); RenderState->CaptureTarget=static_cast<FRenderTarget*>(MaskRT->GameThread_GetRenderTargetResource()); RenderState->OwnerIdentity=BoundPawn.Get(); RenderState->Enabled=true; RenderState->Capture={}; RenderState->Main={}; }
    return true;
}
void UDemoOutlineROIPrototype::SetOutputEnabled(bool Enabled)
{
    DEMO_LOG_CALL(); OutputEnabled=Enabled;
    if(CompositeMID) CompositeMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),Enabled?2.f:0.f);
    if(UMaterialInstanceDynamic* MID=CurrentViewMID.Get()) MID->SetScalarParameterValue(TEXT("OutlineWidthPx"),Enabled?2.f:0.f);
}
bool UDemoOutlineROIPrototype::SetCaptureMaskWidthForAudit(int32 Width)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if((Width!=0&&Width!=2)||!MaskMID||!BoundMask.IsValid()||!CaptureActor) { UE_LOG(LogFPSDemo,Warning,TEXT("OUTLINE_ROI_MASK_WIDTH_REJECT width=%d bound=%d"),Width,BoundMask.IsValid()); return false; }
    RequestedCaptureMaskWidth=Width; MaskMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),static_cast<float>(Width));
    UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_MASK_WIDTH_REQUEST width=%d generation=%u frame=%llu"),Width,Generation,GFrameCounter);
    return true;
#else
    return false;
#endif
}
void UDemoOutlineROIPrototype::RejectFrame(const FString& Reason)
{
    DEMO_LOG_TICK();
    if(FrameRejected&&LastFrame.Frame==GFrameCounter) return; // 保本帧真实首错，不被“未观察/晚主View”级联覆盖。
    FrameRejected=true; LastFrame.Submitted=false; LastFrame.Reason=Reason;
    if(CompositeMID) CompositeMID->SetScalarParameterValue(TEXT("CaptureValid"),0.f);
    if(UMaterialInstanceDynamic* MID=CurrentViewMID.Get()) MID->SetScalarParameterValue(TEXT("CaptureValid"),0.f);
    if(MaskRT) UKismetRenderingLibrary::ClearRenderTarget2D(this,MaskRT,FLinearColor::Transparent);
    if(LastReject!=Reason) { UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_CLOSED generation=%u reason=%s"),Generation,*Reason); LastReject=Reason; }
}
void UDemoOutlineROIPrototype::FinalizePrototypeView(FSceneViewFamily* Family,const TMap<ULocalPlayer*,FSceneView*>& Views)
{
    DEMO_LOG_TICK(); if(!BoundMask.IsValid()) return;
    LastFrame={}; FrameRejected=false; LastFrame.Frame=GFrameCounter; LastFrame.Generation=Generation; LastFrame.OutputEnabled=OutputEnabled;
    APawn* Pawn=BoundPawn.Get(); FSceneView* View=Views.Num()==1?Views.CreateConstIterator().Value():nullptr; // 单Local有限范围，禁止选Player0代表队友。
    if(!Family||!View||Family->Views.Num()!=1||!Pawn||!Pawn->IsLocallyControlled()||View->ViewActor!=Pawn||!BoundCamera.IsValid()||!MaskRT||!CaptureActor||Family->bIsHDR||View->bIsSceneCapture||View->StereoPass!=EStereoscopicPass::eSSP_FULL||!View->IsPerspectiveProjection()||!View->State||View->AntiAliasingMethod!=AAM_TSR||Family->GetSecondarySpatialUpscalerInterface())
    { RejectFrame(TEXT("single local nonstereo SDR actual view/resources required")); return; }
    const FIntRect Full(FIntPoint::ZeroValue,ReferenceSize); // 无裁剪第一步，名义2 capture px即2 output px。
    const IConsoleVariable* Dynamic=IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicRes.OperationMode")); // 首轮不支持动态比例；实际AfterTone网格仍另由材质检查。
    const IConsoleVariable* Panini=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Upscale.Panini.D")); // 后置非线性变换不等于1texel=1输出px。
    if((Dynamic&&Dynamic->GetInt()!=0)||(Panini&&Panini->GetFloat()!=0.f)) { RejectFrame(TEXT("dynamic resolution/Panini unsupported")); return; }
    if(View->UnscaledViewRect!=Full||View->UnconstrainedViewRect!=Full||!Family->RenderTarget||Family->RenderTarget->GetSizeXY()!=ReferenceSize||!FMath::IsNearlyEqual(Family->SecondaryViewFraction,1.f))
    { RejectFrame(TEXT("actual full output/secondary ratio unsupported")); return; }
    UMaterialInstanceDynamic* ViewMID=FindViewMID(*View,CompositeMaterial); // 已有node才更新；绝不在Finalize新增主PP。
    CurrentViewMID=ViewMID;
    if(!ViewMID) { RejectFrame(TEXT("preinstalled composite not in actual view yet")); return; }
    SubmittedView=View->ViewMatrices.GetViewMatrix(); SubmittedProjection=View->ViewMatrices.ComputeProjectionNoAAMatrix(); SubmittedHidden=View->HiddenPrimitives; SubmittedNearPlane=View->NearClippingPlane;
    if(SubmittedView.ContainsNaN()||SubmittedProjection.ContainsNaN()||View->ProjectionMatrixUnadjustedForRHI.ContainsNaN()||View->ViewMatrices.GetViewOrigin().ContainsNaN()||View->ViewRotation.ContainsNaN())
    { RejectFrame(TEXT("nonfinite actual projection/POV")); return; }
    SubmittedMainTarget=Family->RenderTarget;
    { FScopeLock Lock(&RenderState->Mutex); RenderState->MainTarget=Family->RenderTarget; }
    LastFrame.OwnerId=Pawn->GetUniqueID(); LastFrame.MaskId=BoundMask->GetUniqueID(); LastFrame.ROI=Full; LastFrame.Output=ReferenceSize;
    LastFrame.Origin=View->ViewMatrices.GetViewOrigin(); LastFrame.Rotation=View->ViewRotation; LastFrame.MainAA=static_cast<int32>(View->AntiAliasingMethod); LastFrame.HiddenPrimitiveCount=SubmittedHidden.Num();
    LastFrame.MainNearDistance=View->NearClippingDistance;
    if(!View->bHasNearClippingPlane||!FMath::IsFinite(LastFrame.MainNearDistance)||LastFrame.MainNearDistance<=0.f) { RejectFrame(TEXT("main actual near plane/distance invalid")); return; }
    USceneCaptureComponent2D* Capture=CaptureActor->GetCaptureComponent2D(); // 当前GT Actor强引用持有实际自定义组件。
    CaptureActor->SetActorLocationAndRotation(LastFrame.Origin,LastFrame.Rotation);
    Capture->FOVAngle=View->FOV; Capture->CustomProjectionMatrix=View->ProjectionMatrixUnadjustedForRHI; // 原始矩阵仅由Capture构造做一次RHI调整，不能把主已调矩阵再调一次。
    ViewMID->SetScalarParameterValue(TEXT("CaptureValid"),0.f); CompositeMID->SetScalarParameterValue(TEXT("CaptureValid"),0.f);
    ViewMID->SetTextureParameterValue(TEXT("OutlineMaskRT"),MaskRT); ViewMID->SetVectorParameterValue(TEXT("RoiMinUV"),FLinearColor(0,0,0,0));
    ViewMID->SetVectorParameterValue(TEXT("RoiSizeUV"),FLinearColor(1,1,0,0)); ViewMID->SetVectorParameterValue(TEXT("MaskSizePx"),FLinearColor(1280,720,0,0)); ViewMID->SetVectorParameterValue(TEXT("OutputSizePx"),FLinearColor(1280,720,0,0));
    ViewMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),OutputEnabled?2.f:0.f);
    UKismetRenderingLibrary::ClearRenderTarget2D(this,MaskRT,FLinearColor::Transparent); // 同GT调用序先clear，再捕获rendercommand，再引擎主view；GPU同帧仍单独验。
    Capture->CaptureScene();
    if(!LastFrame.CaptureViewObserved) { RejectFrame(TEXT("capture actual view not observed/invalid")); return; }
    ViewMID->SetScalarParameterValue(TEXT("CaptureValid"),1.f); LastFrame.Reason=TEXT("capture submitted; awaiting main final recheck");
}
void UDemoOutlineROIPrototype::ObserveViewFamily(FSceneViewFamily& Family)
{
    DEMO_LOG_TICK(); if(!BoundMask.IsValid()||!CaptureActor||!MaskRT||Family.Views.Num()!=1||LastFrame.Frame!=GFrameCounter) return;
    FSceneView& View=*const_cast<FSceneView*>(Family.Views[0]); // 公开GT finalization钩子中的活View，未提交RT；不保留地址。
    const bool Capture=View.bIsSceneCapture&&Family.RenderTarget==MaskRT->GameThread_GetRenderTargetResource();
    if(View.bIsSceneCapture&&!Capture) return; // 同World其它捕获不受本原型影响。
    if(!Capture&&Family.RenderTarget!=SubmittedMainTarget) return;
    if(Capture&&View.ViewActor==BoundPawn.Get()&&LastFrame.Generation==Generation)
    {
        const double BeforeDelta=MatrixDelta(View.ViewMatrices.GetViewMatrix(),SubmittedView); // 原生Quat矩阵/主Euler矩阵各自构造的实测差，不猜测其幅度。
        double RotationDelta=0.; // 3x3无量纲方向差与平移cm分别限定，不能用重建掩盖错误相机位置。
        for(int32 Row=0;Row<3;++Row) for(int32 Column=0;Column<3;++Column) RotationDelta=FMath::Max(RotationDelta,FMath::Abs(View.ViewMatrices.GetViewMatrix().M[Row][Column]-SubmittedView.M[Row][Column]));
        const double OriginDelta=FVector::Distance(View.ViewMatrices.GetViewOrigin(),LastFrame.Origin); // cm；远小于原.1cm深度门，后者完全不变。
        if(!FMath::IsFinite(BeforeDelta)||BeforeDelta>.001||RotationDelta>1.e-7||OriginDelta>.001)
        {
            if(!CanonicalViewLogged) { CanonicalViewLogged=true; UE_LOG(LogFPSDemo,Warning,TEXT("OUTLINE_ROI_CANONICAL_REJECT raw_view_delta=%.15g rotation_delta=%.15g origin_delta_cm=%.15g"),BeforeDelta,RotationDelta,OriginDelta); }
            RejectFrame(TEXT("raw capture pose differs beyond bounded numerical reconstruction")); return;
        }
        // UE5.4.4 Capture helper未填写FSceneCaptureViewInfo额外Location/Rotation metadata；显式用实际主POV调用公开规范重建。
        // UpdateViewMatrix同步View/Inverse/Frustum/ShadowMatrices；不写Renderer私有状态，不松严格门或投影。
        View.ViewLocation=LastFrame.Origin; View.ViewRotation=LastFrame.Rotation; View.UpdateViewMatrix();
        View.CullingFrustum=View.ViewFrustum; View.CullingOrigin=View.ViewMatrices.GetViewOrigin(); // 当前明确非立体；公开Update未同步这两个派生字段。
        FPlane NearPlane(0,0,0,0); // 世界近面依赖ViewProjection，即使Projection不变也必须跟随View重算。
        View.bHasNearClippingPlane=View.ViewMatrices.GetViewProjectionMatrix().GetFrustumNearPlane(NearPlane);
        View.NearClippingPlane=NearPlane; View.NearClippingDistance=static_cast<float>(View.ViewMatrices.ComputeNearPlane());
        LastFrame.CaptureNearDistance=View.NearClippingDistance;
        LastFrame.CaptureNearSynchronized=View.bHasNearClippingPlane&&FMath::IsFinite(NearPlane.X)&&FMath::IsFinite(NearPlane.Y)&&FMath::IsFinite(NearPlane.Z)&&FMath::IsFinite(NearPlane.W)&&FMath::IsFinite(View.NearClippingDistance)&&FMath::IsNearlyEqual(View.NearClippingDistance,LastFrame.MainNearDistance,1.e-6f)&&NearPlane.Equals(SubmittedNearPlane,1.e-5);
        if(!LastFrame.CaptureNearSynchronized) { RejectFrame(TEXT("capture rebuilt near plane/distance invalid or differs from main")); return; }
        if(!CanonicalViewLogged)
        {
            CanonicalViewLogged=true;
            UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_CAPTURE_CANONICAL frame=%llu raw_view_max_delta=%.15g raw_rotation_delta=%.15g raw_origin_delta_cm=%.15g canonical_view_max_delta=%.15g projection_max_delta=%.15g"),GFrameCounter,BeforeDelta,RotationDelta,OriginDelta,MatrixDelta(View.ViewMatrices.GetViewMatrix(),SubmittedView),MatrixDelta(View.ViewMatrices.ComputeProjectionNoAAMatrix(),SubmittedProjection));
        }
    }
    if(View.ViewActor!=BoundPawn.Get()||LastFrame.Generation!=Generation||!SameView(View,SubmittedView,SubmittedProjection))
    {
        if(!MismatchLogged)
        {
            MismatchLogged=true;
            UE_LOG(LogFPSDemo,Warning,TEXT("OUTLINE_ROI_FIRST_VIEW_MISMATCH capture=%d frame=%llu owner_ok=%d generation_ok=%d view_equal=%d projection_equal=%d actual_origin=%s expected_origin=%s actual_rotation=%s expected_rotation=%s actual_view=%s expected_view=%s actual_projection=%s expected_projection=%s"),Capture,GFrameCounter,View.ViewActor==BoundPawn.Get(),LastFrame.Generation==Generation,View.ViewMatrices.GetViewMatrix().Equals(SubmittedView,1.e-5),View.ViewMatrices.ComputeProjectionNoAAMatrix().Equals(SubmittedProjection,1.e-6),*View.ViewMatrices.GetViewOrigin().ToString(),*LastFrame.Origin.ToString(),*View.ViewRotation.ToString(),*LastFrame.Rotation.ToString(),*View.ViewMatrices.GetViewMatrix().ToString(),*SubmittedView.ToString(),*View.ViewMatrices.ComputeProjectionNoAAMatrix().ToString(),*SubmittedProjection.ToString()); // 仅首错同步值证据，不保留View裸指针。
        }
        RejectFrame(TEXT("actual capture/main owner/generation/matrix mismatch")); return;
    }
    if(Capture)
    {
        LastFrame.CaptureAA=static_cast<int32>(View.AntiAliasingMethod); LastFrame.CaptureJitter=View.ViewMatrices.GetTemporalAAJitter();
        LastFrame.CaptureHasViewState=View.State!=nullptr;
        if(!View.State||View.AntiAliasingMethod!=AAM_None||!LastFrame.CaptureJitter.IsNearlyZero()||Family.EngineShowFlags.AntiAliasing||Family.EngineShowFlags.TemporalAA||Family.EngineShowFlags.MotionBlur||View.UnscaledViewRect!=LastFrame.ROI)
        { RejectFrame(TEXT("capture actual AA/jitter/state/rect unsupported")); return; }
        View.HiddenPrimitives=SubmittedHidden; // 原生Capture不继承主PC隐藏集；只同步主View，不用ShowOnly移走世界遮挡。
        UMaterialInstanceDynamic* ActualMask=FindViewMID(View,MaskMaterial); // 标准State复用的实际MID，不把源MID地址当渲染实例。
        float Width=-1.f; // 平时2px，显式有限因果见证允许请求0；必须核实际标准node当前值。
        const bool HasWidth=ActualMask&&ActualMask->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineWidthPx")),Width); // 真正当前node字段，独立于请求值。
        LastFrame.ActualCaptureMaskWidth=HasWidth&&(Width==0.f||Width==2.f)?static_cast<int32>(Width):INDEX_NONE;
        LastFrame.CaptureViewObserved=HasWidth&&Width==static_cast<float>(RequestedCaptureMaskWidth);
        if(!LastFrame.CaptureViewObserved) RejectFrame(FString::Printf(TEXT("capture actual mask node width invalid actual=%.3f expected=%d"),Width,RequestedCaptureMaskWidth));
        return;
    }
    if(!LastFrame.CaptureViewObserved||!BoundCamera.IsValid()||Family.bIsHDR||Family.GetSecondarySpatialUpscalerInterface()||static_cast<int32>(View.AntiAliasingMethod)!=LastFrame.MainAA||!FMath::IsNearlyEqual(Family.SecondaryViewFraction,1.f)||View.UnscaledViewRect!=LastFrame.ROI||View.UnconstrainedViewRect!=LastFrame.ROI||View.HiddenPrimitives.Num()!=SubmittedHidden.Num()||View.HiddenPrimitives.Difference(SubmittedHidden).Num()>0||FindViewMID(View,CompositeMaterial)!=CurrentViewMID.Get())
    { RejectFrame(TEXT("late main view/grid/hidden/composite changed")); return; }
    LastFrame.MainViewRechecked=true; LastFrame.Submitted=true; LastFrame.Reason=TEXT("GT clear/capture/main order valid; pixel/GPU acceptance separate"); LastReject.Reset();
}
bool UDemoOutlineROIPrototype::ReadFrameAudit(FDemoOutlineROIFrameAudit& Out) const
{
    DEMO_LOG_TICK(); Out=LastFrame;
    if(RenderState)
    {
        FScopeLock Lock(&RenderState->Mutex); // 正常截图已完成时只合并同frame，不把旧RT观察当当前帧。
        const auto& Capture=RenderState->Capture; const auto& Main=RenderState->Main; // 锁内值引用，不逃逸。
        Out.CaptureRenderObserved=Capture.Frame==Out.Frame&&Capture.Sequence>0; Out.MainRenderObserved=Main.Frame==Out.Frame&&Main.Sequence>0;
        if(Out.CaptureRenderObserved) { Out.CaptureRenderAA=Capture.AA; Out.CaptureRenderJitter=Capture.Jitter; Out.CaptureRenderSequence=Capture.Sequence; }
        if(Out.MainRenderObserved) Out.MainRenderSequence=Main.Sequence;
        Out.RenderViewsMatchSubmission=Out.CaptureRenderObserved&&Out.MainRenderObserved&&Capture.Sequence<Main.Sequence&&Capture.AA==AAM_None&&Capture.Jitter.IsNearlyZero()&&Capture.Rect==Out.ROI&&Main.Rect==Out.ROI&&Capture.View.Equals(SubmittedView,1.e-5)&&Main.View.Equals(SubmittedView,1.e-5)&&Capture.Projection.Equals(SubmittedProjection,1.e-6)&&Main.Projection.Equals(SubmittedProjection,1.e-6)&&Capture.HasNearPlane&&Main.HasNearPlane&&Capture.NearPlane.Equals(SubmittedNearPlane,1.e-5)&&Main.NearPlane.Equals(SubmittedNearPlane,1.e-5)&&FMath::IsNearlyEqual(Capture.NearDistance,Out.MainNearDistance,1.e-6f)&&FMath::IsNearlyEqual(Main.NearDistance,Out.MainNearDistance,1.e-6f);
    }
    return Out.Frame>0;
}
UTextureRenderTarget2D* UDemoOutlineROIPrototype::ReadMaskRT() const { DEMO_LOG_TICK(); return MaskRT.Get(); }
FString UDemoOutlineROIPrototype::ReadFailure() const { DEMO_LOG_TICK(); return Failure; }
void UDemoOutlineROIPrototype::ClearTarget(const TCHAR* Reason)
{
    DEMO_LOG_CALL(); RejectFrame(Reason?Reason:TEXT("clear target"));
    if(RenderState) { FScopeLock Lock(&RenderState->Mutex); RenderState->Enabled=false; RenderState->OwnerIdentity=nullptr; RenderState->CaptureTarget=nullptr; RenderState->MainTarget=nullptr; }
    if(UCameraComponent* Camera=BoundCamera.Get()) if(CompositeMID) Camera->PostProcessSettings.RemoveBlendable(CompositeMID);
    if(UMaterialInstanceDynamic* Production=BorrowedProductionMID.Get())
    {
        float Actual=0.f; // 不覆盖外部在借用期间对同MID做的新修改。
        if(Production->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineWidthPx")),Actual)&&Actual==0.f) Production->SetScalarParameterValue(TEXT("OutlineWidthPx"),BorrowedWidth);
        else { Failure=TEXT("borrowed production MID width changed externally"); UE_LOG(LogFPSDemo,Warning,TEXT("OUTLINE_ROI_RESTORE_REJECT foreign_width")); }
    }
    if(CaptureActor) CaptureActor->Destroy(); CaptureActor=nullptr;
    if(MaskRT) RetiredRTs.Add(MaskRT); MaskRT=nullptr; // 旧材质/RT提交完成前保拥有引用，Deinit交引擎延迟释放，不强制Flush每帧。
    BoundCamera.Reset(); BoundMask.Reset(); BoundPawn.Reset(); BorrowedProductionMID.Reset(); CurrentViewMID.Reset(); SubmittedMainTarget=nullptr; PendingSize=FIntPoint::ZeroValue; ++Generation;
}
void UDemoOutlineROIPrototype::OnApplicationActivationChanged(bool Active)
{ DEMO_LOG_CALL(); if(!Active) ClearTarget(TEXT("application deactivated")); }
bool UDemoOutlineROIPrototype::RestoreProcessSettings()
{
    DEMO_LOG_CALL(); if(!SecondarySaved) return true;
    IConsoleVariable* Secondary=IConsoleManager::Get().FindConsoleVariable(TEXT("r.SecondaryScreenPercentage.GameViewport")); // 原进程唯一借用项。
    if(!Secondary||!FMath::IsNearlyEqual(Secondary->GetFloat(),100.f)) { Failure=TEXT("secondary CVar changed externally; refusing overwrite"); return false; }
    Secondary->Set(OriginalSecondaryValue,ECVF_SetByConsole); Secondary->SetFlags(static_cast<EConsoleVariableFlags>(OriginalSecondaryFlags));
    const bool Restored=Secondary->GetFloat()==OriginalSecondaryValue&&static_cast<uint32>(Secondary->GetFlags())==OriginalSecondaryFlags;
    UE_LOG(LogFPSDemo,Log,TEXT("OUTLINE_ROI_RESTORE secondary_restored=%d original=%.6f actual=%.6f original_flags=%u actual_flags=%u"),Restored,OriginalSecondaryValue,Secondary->GetFloat(),OriginalSecondaryFlags,static_cast<uint32>(Secondary->GetFlags()));
    if(Restored) SecondarySaved=false; return Restored;
}
void UDemoOutlineROIPrototype::Deinitialize()
{
    DEMO_LOG_CALL();
    if(ActivationHandle.IsValid()&&FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle); ActivationHandle.Reset();
    ClearTarget(TEXT("world deinitialize")); RestoreProcessSettings(); ViewExtension.Reset(); RenderState.Reset();
    MaskMID=nullptr; CompositeMID=nullptr; MaskMaterial=nullptr; CompositeMaterial=nullptr; RetiredRTs.Reset(); Super::Deinitialize();
}
