// 职责：正常Game/PIE中按本LocalPlayer实际AfterTone网格捕获可见轮廓并合成；不改主AA/用户设置。
// 使用：Focus/LocalView提交原253 Mask，生产Viewport与公开ViewExtension共同维护同帧/同Owner资源。
#include "Interaction/HubShops/Rendering/DemoLocalOutlineComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/BlendableInterface.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/ScopeLock.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "UnrealClient.h"

namespace
{
    /** 找已由Camera提前安装、引擎本帧复制的实际MID，不在Finalize临时新增主PP。 */
    UMaterialInstanceDynamic* FindOutlineMID(FSceneView& View,const UMaterial* Parent)
    {
        DEMO_LOG_STATIC_TICK(nullptr); UMaterialInstanceDynamic* Result=nullptr; // 同步借用，ViewState持有实际节点。
        FBlendableEntry* Iterator=nullptr; // 原生公开迭代游标，不存跨帧节点地址。
        while(FPostProcessMaterialNode* Node=View.FinalPostProcessSettings.BlendableManager.IterateBlendables<FPostProcessMaterialNode>(Iterator))
        {
            UMaterialInterface* Material=Node->GetMaterialInterface();
            if(Material&&Material->GetMaterial()==Parent) { if(Result) return nullptr; Result=Cast<UMaterialInstanceDynamic>(Material); if(!Result) return nullptr; }
        }
        return Result;
    }
    /** 同一实际主/捕获矩阵的最大数值差，仅数值规范重建门，不改变原材质深度容差。 */
    double OutlineMatrixDelta(const FMatrix& Actual,const FMatrix& Expected,int32 Rows=4,int32 Columns=4)
    {
        DEMO_LOG_STATIC_TICK(nullptr); double Maximum=0.;
        for(int32 Row=0;Row<Rows;++Row) for(int32 Column=0;Column<Columns;++Column) Maximum=FMath::Max(Maximum,FMath::Abs(Actual.M[Row][Column]-Expected.M[Row][Column]));
        return Maximum;
    }
}

/** RT仅复制stage纯值；身份地址只比较不解引用，GT不会持锁调用UObject/RHI。 */
struct FDemoLocalOutlineRenderState
{
    struct FStage
    {
        FIntPoint Size=FIntPoint::ZeroValue; // AfterTone实际SceneColor ViewRect.Size，非总allocation/window。
        FIntRect UnscaledRect=FIntRect(0,0,0,0); // 对应主View原输出区域，区分resize/分屏布局。
        uint64 Frame=0; // 原生Family.FrameCounter，观察有队列延迟，不冒作GPU生产帧号。
        int32 AA=INDEX_NONE; // 实际主AA，用户设置原样保留。
        int32 PrimaryMethod=INDEX_NONE; // 区分TAAU/空间比例路径，变化后重新观察。
        float SecondaryFraction=1.f; // 对应实际Family阶段比例，绝不强设为100。
        bool Valid=false; // 实际Tonemap回调写过的stage元数据。
    } Latest;
    FCriticalSection Mutex; // 仅小值同步，不跨锁渲染/访问UObject。
    const void* OwnerIdentity=nullptr; // GT写当前Pawn身份，RT仅比较ViewActor地址。
    const void* MainTarget=nullptr; // 生产主Viewport身份，排除其它PIE与SceneCapture。
    bool Enabled=false; // 只有当前请求需要stage观察时才订阅，不给无高亮正常画面加额外pass。
};

/** 本World公开扩展；GT核Capture/main，RT透传Tonemap并报告实际网格，不自建Renderer框架。 */
class FDemoLocalOutlineViewExtension final : public FWorldSceneViewExtension
{
public:
    /** Service仅GT弱引用；RT另持纯值State，EndPlay不留下UObject裸捕获。 */
    FDemoLocalOutlineViewExtension(const FAutoRegister& Register,UWorld* World,UDemoLocalOutlineComponent* Service,TSharedPtr<FDemoLocalOutlineRenderState,ESPMode::ThreadSafe> State)
        :FWorldSceneViewExtension(Register,World),Owner(Service),Shared(MoveTemp(State)) { DEMO_LOG_STATIC_CALL(Service); }
    /** 不改主ShowFlags。 */
    virtual void SetupViewFamily(FSceneViewFamily& Family) override { DEMO_LOG_STATIC_TICK(nullptr); }
    /** 不改主投影；Capture在真实提交前规范重建。 */
    virtual void SetupView(FSceneViewFamily& Family,FSceneView& View) override { DEMO_LOG_STATIC_TICK(nullptr); }
    /** 此时View已实际建立，弱服务只在GT使用，清理后不复活资源。 */
    virtual void BeginRenderViewFamily(FSceneViewFamily& Family) override
    { DEMO_LOG_STATIC_TICK(nullptr); if(UDemoLocalOutlineComponent* Service=Owner.Get()) Service->ObserveViewFamily(Family); }
    /** 在其它普通扩展之后核主视角；不替代材质同帧网格校验。 */
    virtual int32 GetPriority() const override { DEMO_LOG_STATIC_TICK(nullptr); return MIN_int32; }
    /** 仅需要轮廓的阶段订阅公开Tonemap回调；强ViewFamily保证本扩展活到构图完成。 */
    virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass,FAfterPassCallbackDelegateArray& Callbacks,bool Enabled) override
    {
        DEMO_LOG_STATIC_TICK(nullptr); if(Pass!=EPostProcessingPass::Tonemap) return;
        bool Needed=false; { FScopeLock Lock(&Shared->Mutex); Needed=Shared->Enabled; }
        if(Needed) Callbacks.Add(FAfterPassCallbackDelegate::CreateSP(this,&FDemoLocalOutlineViewExtension::AfterTonemap));
    }
private:
    /** RT只读SceneColor网格，完整透传OverrideOutput；不调UObject、不创建/resize资产。 */
    FScreenPassTexture AfterTonemap(FRDGBuilder& Builder,const FSceneView& View,const FPostProcessMaterialInputs& Inputs)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        const FScreenPassTextureSlice Slice=Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
        if(Slice.IsValid()&&View.Family&&!View.bIsSceneCapture)
        {
            FScopeLock Lock(&Shared->Mutex);
            if(Shared->Enabled&&static_cast<const void*>(View.ViewActor)==Shared->OwnerIdentity&&static_cast<const void*>(View.Family->RenderTarget)==Shared->MainTarget)
            {
                Shared->Latest.Size=Slice.ViewRect.Size(); Shared->Latest.UnscaledRect=View.UnscaledViewRect; Shared->Latest.Frame=View.Family->FrameCounter;
                Shared->Latest.AA=static_cast<int32>(View.AntiAliasingMethod); Shared->Latest.PrimaryMethod=static_cast<int32>(View.PrimaryScreenPercentageMethod);
                Shared->Latest.SecondaryFraction=View.Family->SecondaryViewFraction; Shared->Latest.Valid=true;
            }
        }
        FScreenPassTexture Color=FScreenPassTexture::CopyFromSlice(Builder,Slice); // UE5.4公开Slice转换；通常共享原纹理，不强制新拷贝。
        if(Inputs.OverrideOutput.IsValid())
        {
            if(Color.Texture!=Inputs.OverrideOutput.Texture||Color.ViewRect!=Inputs.OverrideOutput.ViewRect) AddDrawTexturePass(Builder,View,Color,Inputs.OverrideOutput);
            return Inputs.OverrideOutput; // 末pass必须真的写目标，不能只返回未写的OverrideOutput。
        }
        return Color;
    }
    TWeakObjectPtr<UDemoLocalOutlineComponent> Owner; // 仅GT访问的弱服务。
    TSharedPtr<FDemoLocalOutlineRenderState,ESPMode::ThreadSafe> Shared; // RT唯一状态，只含值与身份地址。
};

UDemoLocalOutlineCaptureComponent::UDemoLocalOutlineCaptureComponent(const FObjectInitializer& Initializer):Super(Initializer)
{
    DEMO_LOG_CALL(); bCaptureEveryFrame=false; bCaptureOnMovement=false; bAlwaysPersistRenderingState=true;
    CaptureSource=SCS_FinalColorLDR; bUseCustomProjectionMatrix=true;
    ShowFlags.SetAntiAliasing(false); ShowFlags.SetTemporalAA(false); ShowFlags.SetMotionBlur(false); ShowFlags.SetScreenPercentage(false);
}
const AActor* UDemoLocalOutlineCaptureComponent::GetViewOwner() const
{ DEMO_LOG_TICK(); const AActor* CaptureOwner=GetOwner(); return CaptureOwner?CaptureOwner->GetOwner():nullptr; }
ADemoLocalOutlineCapture::ADemoLocalOutlineCapture(const FObjectInitializer& Initializer)
    :Super(Initializer.SetDefaultSubobjectClass<UDemoLocalOutlineCaptureComponent>(TEXT("NewSceneCaptureComponent2D")))
{ DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=false; SetReplicates(false); SetActorEnableCollision(false); }
UDemoLocalOutlineComponent::UDemoLocalOutlineComponent()
{ DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.bTickEvenWhenPaused=true; SetIsReplicatedByDefault(false); }
bool UDemoLocalOutlineComponent::EnsureRenderBridge()
{
    DEMO_LOG_TICK(); if(ViewExtension&&RenderState) return true;
    const APlayerController* Controller=Cast<APlayerController>(GetOwner());
    if(!Controller||!Controller->IsLocalController()||!GetWorld()||!GetWorld()->IsGameWorld()||Controller->GetNetMode()==NM_DedicatedServer) return false;
    RenderState=MakeShared<FDemoLocalOutlineRenderState,ESPMode::ThreadSafe>();
    ViewExtension=FSceneViewExtensions::NewExtension<FDemoLocalOutlineViewExtension>(GetWorld(),this,RenderState);
    if(FSlateApplication::IsInitialized()) ActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this,&UDemoLocalOutlineComponent::OnApplicationActivationChanged);
    return true;
}
void UDemoLocalOutlineComponent::BeginPlay()
{ DEMO_LOG_CALL(); Super::BeginPlay(); const APlayerController* Controller=Cast<APlayerController>(GetOwner()); if(!Controller||!Controller->IsLocalController()) { SetComponentTickEnabled(false); return; } EnsureRenderBridge(); }
bool UDemoLocalOutlineComponent::SetOutlineTarget(UObject* Requester,APawn* Viewer,UCameraComponent* Camera,UPrimitiveComponent* Source,UPrimitiveComponent* Mask,UMaterialInterface* Composite,UMaterialInterface* CaptureMask)
{
    DEMO_LOG_CALL(); APlayerController* Controller=Cast<APlayerController>(GetOwner());
    const bool Valid=IsValid(Requester)&&Controller&&Controller->IsLocalController()&&Controller->GetPawn()==Viewer&&IsValid(Viewer)&&IsValid(Camera)&&Camera->GetOwner()==Viewer&&IsValid(Source)&&IsValid(Mask)&&Mask->GetOwner()==Viewer&&Source->GetWorld()==GetWorld()&&Mask->GetWorld()==GetWorld()&&Mask->IsRegistered()&&Mask->bOnlyOwnerSee&&Mask->bRenderCustomDepth&&Mask->CustomDepthStencilValue==253&&!Mask->bRenderInMainPass&&Composite&&CaptureMask;
    if(!Valid||!EnsureRenderBridge()) { if(RequestOwner.Get()==Requester) ClearCurrentTarget(TEXT("invalid requester/local source/mask/material")); UE_LOG(LogFPSDemo,Warning,TEXT("LOCAL_OUTLINE_REQUEST_REJECT requester=%s"),*GetNameSafe(Requester)); return false; }
    if(OwnsOutlineTarget(Requester,Mask)&&ViewerPawn.Get()==Viewer&&ViewCamera.Get()==Camera&&SourceMesh.Get()==Source&&CompositeParent==Composite&&CaptureParent==CaptureMask) return true;
    UTexture* DefaultTexture=nullptr; float DefaultValid=0.f; // 识别正式Composite契约，不把旧A04错当新双层材质运行。
    if(Composite->GetMaterial()->MaterialDomain!=MD_PostProcess||CaptureMask->GetMaterial()->MaterialDomain!=MD_PostProcess||Composite->GetMaterial()->BlendableLocation!=BL_SceneColorAfterTonemapping||CaptureMask->GetMaterial()->BlendableLocation!=BL_SceneColorAfterTonemapping||!Composite->GetTextureParameterValue(FMaterialParameterInfo(TEXT("OutlineMaskRT")),DefaultTexture)||!DefaultTexture||!Composite->GetScalarParameterValue(FMaterialParameterInfo(TEXT("CaptureValid")),DefaultValid))
    { if(RequestOwner.Get()==Requester) ClearCurrentTarget(TEXT("material contract mismatch")); UE_LOG(LogFPSDemo,Warning,TEXT("LOCAL_OUTLINE_MATERIAL_REJECT composite=%s mask=%s"),*GetNameSafe(Composite),*GetNameSafe(CaptureMask)); return false; }
    ClearCurrentTarget(TEXT("new target request"));
    if(CompositeParent!=Composite||CaptureParent!=CaptureMask||!CompositeMID||!CaptureMID)
    {
        CompositeParent=Composite; CaptureParent=CaptureMask; CompositeMID=UMaterialInstanceDynamic::Create(Composite,this); CaptureMID=UMaterialInstanceDynamic::Create(CaptureMask,this); bShaderCacheRequested=false;
    }
    if(!CompositeMID||!CaptureMID) { UE_LOG(LogFPSDemo,Warning,TEXT("LOCAL_OUTLINE_MID_CREATE_FAILED")); return false; }
    EmptyTexture=DefaultTexture; RequestOwner=Requester; ViewerPawn=Viewer; ViewCamera=Camera; SourceMesh=Source; TargetMask=Mask; ++Generation;
    CompositeMID->SetTextureParameterValue(TEXT("OutlineMaskRT"),EmptyTexture); CompositeMID->SetScalarParameterValue(TEXT("CaptureValid"),0.f); CompositeMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),2.f);
    CaptureMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),2.f);
    Camera->PostProcessSettings.AddBlendable(CompositeMID,1.f); // 必须早于LocalPlayer合入当前View，不在Finalize临时安装。
    { FScopeLock Lock(&RenderState->Mutex); RenderState->Enabled=true; RenderState->OwnerIdentity=Viewer; RenderState->MainTarget=nullptr; }
    UE_LOG(LogFPSDemo,Log,TEXT("LOCAL_OUTLINE_TARGET requester=%s source=%s generation=%u"),*GetNameSafe(Requester),*Source->GetName(),Generation);
    return true; // shader/实际stage/resize等待也保此请求所有权，Caller不能反复造Mask。
}
bool UDemoLocalOutlineComponent::OwnsOutlineTarget(const UObject* Requester,const UPrimitiveComponent* Mask) const
{ DEMO_LOG_TICK(); return RequestOwner.Get()==Requester&&TargetMask.Get()==Mask&&RequestOwner.IsValid()&&TargetMask.IsValid()&&ViewerPawn.IsValid()&&ViewCamera.IsValid(); }
UMaterialInstanceDynamic* UDemoLocalOutlineComponent::GetCompositeForOwner(const UObject* Requester) const
{ DEMO_LOG_TICK(); return RequestOwner.Get()==Requester&&RequestOwner.IsValid()?CompositeMID.Get():nullptr; }
void UDemoLocalOutlineComponent::ClearOutlineTarget(UObject* Requester)
{ DEMO_LOG_TICK(); if(RequestOwner.Get()==Requester) ClearCurrentTarget(TEXT("request owner cleared")); }
void UDemoLocalOutlineComponent::CloseFrame(const FString& Reason)
{
    DEMO_LOG_TICK(); if(bFrameClosed&&SubmissionFrame==GFrameCounter) return; bFrameClosed=true; bCaptureAccepted=false; LastAcceptedMainFrame=0;
    if(CompositeMID) CompositeMID->SetScalarParameterValue(TEXT("CaptureValid"),0.f);
    if(UMaterialInstanceDynamic* Current=ActualViewMID.Get()) Current->SetScalarParameterValue(TEXT("CaptureValid"),0.f);
    if(MaskRT) UKismetRenderingLibrary::ClearRenderTarget2D(this,MaskRT,FLinearColor::Transparent);
    if(LastCloseReason!=Reason) { LastCloseReason=Reason; UE_LOG(LogFPSDemo,Log,TEXT("LOCAL_OUTLINE_CLOSED reason=%s generation=%u"),*Reason,Generation); }
}
void UDemoLocalOutlineComponent::RetireCurrentRT()
{
    DEMO_LOG_CALL(); if(!MaskRT) return;
    if(CaptureActor) CaptureActor->GetCaptureComponent2D()->TextureTarget=nullptr;
    if(CompositeMID) { CompositeMID->SetScalarParameterValue(TEXT("CaptureValid"),0.f); CompositeMID->SetTextureParameterValue(TEXT("OutlineMaskRT"),EmptyTexture); }
    if(UMaterialInstanceDynamic* Current=ActualViewMID.Get()) { Current->SetScalarParameterValue(TEXT("CaptureValid"),0.f); Current->SetTextureParameterValue(TEXT("OutlineMaskRT"),EmptyTexture); }
    RetiredRTs.Add(MaskRT); MaskRT=nullptr; TUniquePtr<FRenderCommandFence> Fence=MakeUnique<FRenderCommandFence>(); Fence->BeginFence(); RetirementFences.Add(MoveTemp(Fence)); // 独立Fence，不用一次旧Fence豁免后来资源。
}
void UDemoLocalOutlineComponent::CollectRetiredRTs()
{
    DEMO_LOG_TICK(); for(int32 Index=RetirementFences.Num()-1;Index>=0;--Index) if(RetirementFences[Index]->IsFenceComplete()) { RetirementFences.RemoveAtSwap(Index); RetiredRTs.RemoveAtSwap(Index); }
}
void UDemoLocalOutlineComponent::ClearCurrentTarget(const FString& Reason)
{
    DEMO_LOG_CALL(); CloseFrame(Reason);
    if(RenderState) { FScopeLock Lock(&RenderState->Mutex); RenderState->Enabled=false; RenderState->OwnerIdentity=nullptr; RenderState->MainTarget=nullptr; }
    if(UCameraComponent* Camera=ViewCamera.Get()) if(CompositeMID) Camera->PostProcessSettings.RemoveBlendable(CompositeMID);
    RetireCurrentRT(); if(CaptureActor) CaptureActor->Destroy(); CaptureActor=nullptr; // 注销组件由UE释放自己的ViewStates。
    RequestOwner.Reset(); ViewerPawn.Reset(); ViewCamera.Reset(); SourceMesh.Reset(); TargetMask.Reset(); ActualViewMID.Reset(); PendingStage=FIntPoint::ZeroValue; SubmittedStage=FIntPoint::ZeroValue; MainRenderTarget=nullptr; ++Generation;
}
bool UDemoLocalOutlineComponent::MaterialsReady()
{
    DEMO_LOG_TICK(); if(!CompositeParent||!CaptureParent) return false;
    bool Ready=true; const bool RequestCache=!bShaderCacheRequested; // 一次机会同时处理两层，不因首层等待跳过另一层。
    for(UMaterialInterface* Parent:{CompositeParent.Get(),CaptureParent.Get()})
    {
        UMaterial* Base=Parent->GetMaterial(); const FMaterialResource* Resource=Base?Base->GetMaterialResource(GetWorld()->GetFeatureLevel()):nullptr;
#if WITH_EDITOR
        if((!Resource||!Resource->IsGameThreadShaderMapComplete())&&RequestCache&&Base) Base->CacheShaders(EMaterialShaderPrecompileMode::Default); // 一次正常异步准备，不在Tick同步等待/强制改StateId。
#endif
        if(!Resource||!Resource->IsGameThreadShaderMapComplete()) Ready=false;
    }
    bShaderCacheRequested=true; return Ready;
}
bool UDemoLocalOutlineComponent::PrepareResources()
{
    DEMO_LOG_TICK(); if(!ViewerPawn.IsValid()||!MaterialsReady()||PendingStage.X<1||PendingStage.Y<1) return false;
    const int64 Pixels=int64(PendingStage.X)*PendingStage.Y; // RGBA8颜色最多约64MiB/本机View，场景深度/State成本另计。
    if(PendingStage.X>8192||PendingStage.Y>8192||Pixels>16777216) { CloseFrame(TEXT("actual stage exceeds bounded texture size")); return false; }
    if(!CaptureActor)
    {
        FActorSpawnParameters Params; Params.Owner=ViewerPawn.Get(); Params.ObjectFlags=RF_Transient; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        CaptureActor=GetWorld()->SpawnActor<ADemoLocalOutlineCapture>(Params);
        if(!CaptureActor) { CloseFrame(TEXT("capture spawn failed")); return false; }
        CaptureActor->GetCaptureComponent2D()->PostProcessSettings.AddBlendable(CaptureMID,1.f); CaptureActor->GetCaptureComponent2D()->PostProcessBlendWeight=1.f;
    }
    if(MaskRT&&MaskRT->SizeX==PendingStage.X&&MaskRT->SizeY==PendingStage.Y) return true;
    RetireCurrentRT(); CollectRetiredRTs(); if(RetiredRTs.Num()>=4) { CloseFrame(TEXT("waiting for old render resources")); return false; }
    MaskRT=NewObject<UTextureRenderTarget2D>(this,NAME_None,RF_Transient); MaskRT->ClearColor=FLinearColor::Transparent; MaskRT->bAutoGenerateMips=false;
    MaskRT->Filter=TF_Nearest; MaskRT->AddressX=TA_Clamp; MaskRT->AddressY=TA_Clamp; MaskRT->RenderTargetFormat=RTF_RGBA8;
    MaskRT->InitCustomFormat(PendingStage.X,PendingStage.Y,PF_B8G8R8A8,true); MaskRT->UpdateResourceImmediate(true); CaptureActor->GetCaptureComponent2D()->TextureTarget=MaskRT;
    UKismetRenderingLibrary::ClearRenderTarget2D(this,MaskRT,FLinearColor::Transparent);
    UE_LOG(LogFPSDemo,Log,TEXT("LOCAL_OUTLINE_STAGE_READY stage=%dx%d generation=%u"),PendingStage.X,PendingStage.Y,Generation);
    return true;
}
void UDemoLocalOutlineComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    DEMO_LOG_TICK(); Super::TickComponent(Delta,TickType,Function); CollectRetiredRTs(); if(!RequestOwner.IsValid()) { if(CaptureActor||ViewCamera.IsValid()) ClearCurrentTarget(TEXT("request object expired")); return; }
    APlayerController* Controller=Cast<APlayerController>(GetOwner()); APawn* Viewer=ViewerPawn.Get();
    if(!Controller||!Controller->IsLocalController()||!Viewer||Controller->GetPawn()!=Viewer||Controller->GetViewTarget()!=Viewer||Controller->IsPaused()||!ViewCamera.IsValid()||!SourceMesh.IsValid()||!TargetMask.IsValid()||!TargetMask->IsRegistered()||TargetMask->GetOwner()!=Viewer)
    { ClearCurrentTarget(TEXT("local owner/avatar/camera/target context changed")); return; }
    if(!bApplicationActive) { CloseFrame(TEXT("application inactive")); return; }
    PrepareResources();
}
void UDemoLocalOutlineComponent::FinalizeLocalView(FSceneViewFamily* Family,ULocalPlayer* Player,FSceneView* View)
{
    DEMO_LOG_TICK(); if(!RequestOwner.IsValid()) return;
    SubmissionFrame=GFrameCounter; bFrameClosed=false; bCaptureAccepted=false;
    if(!bApplicationActive) { CloseFrame(TEXT("application inactive")); return; }
    APlayerController* Controller=Cast<APlayerController>(GetOwner()); APawn* Viewer=ViewerPawn.Get();
    if(!Family||!View||!Player||Player->GetPlayerController(GetWorld())!=Controller||!Controller||Controller->GetPawn()!=Viewer||Controller->GetViewTarget()!=Viewer||View->ViewActor!=Viewer||View->bIsSceneCapture||!TargetMask.IsValid()||!ViewCamera.IsValid()||!View->IsPerspectiveProjection()||View->StereoPass!=EStereoscopicPass::eSSP_FULL)
    { CloseFrame(TEXT("actual local main view is unavailable")); return; }
    MainRenderTarget=Family->RenderTarget;
    { FScopeLock Lock(&RenderState->Mutex); RenderState->Enabled=true; RenderState->OwnerIdentity=Viewer; RenderState->MainTarget=Family->RenderTarget; }
    FDemoLocalOutlineRenderState::FStage Stage; { FScopeLock Lock(&RenderState->Mutex); Stage=RenderState->Latest; }
    // 真正AfterTone观察优先；仅用窗口*比例预测会在空间AA/动态路径猜错，材质还在同帧再次核实际Input0。
    const bool StageMatches=Stage.Valid&&Stage.UnscaledRect==View->UnscaledViewRect&&Stage.AA==static_cast<int32>(View->AntiAliasingMethod)&&Stage.PrimaryMethod==static_cast<int32>(View->PrimaryScreenPercentageMethod)&&FMath::IsNearlyEqual(Stage.SecondaryFraction,Family->SecondaryViewFraction)&&Stage.Frame<=GFrameCounter&&GFrameCounter-Stage.Frame<=8;
    if(!StageMatches) { CloseFrame(TEXT("waiting for actual AfterTone stage")); return; }
    PendingStage=Stage.Size;
    UMaterialInstanceDynamic* Current=FindOutlineMID(*View,CompositeParent?CompositeParent->GetMaterial():nullptr); ActualViewMID=Current;
    if(!Current||!MaskRT||!CaptureActor||MaskRT->SizeX!=Stage.Size.X||MaskRT->SizeY!=Stage.Size.Y||!MaterialsReady()) { CloseFrame(TEXT("actual node/shader/stage resource pending")); return; }
    MainView=View->ViewMatrices.GetViewMatrix(); MainProjection=View->ViewMatrices.ComputeProjectionNoAAMatrix(); MainOrigin=View->ViewMatrices.GetViewOrigin(); MainRotation=View->ViewRotation;
    MainHidden=View->HiddenPrimitives; MainNearPlane=View->NearClippingPlane; MainNearDistance=View->NearClippingDistance; MainFOV=View->FOV; SubmittedStage=Stage.Size;
    if(MainView.ContainsNaN()||MainProjection.ContainsNaN()||MainOrigin.ContainsNaN()||MainRotation.ContainsNaN()||!View->bHasNearClippingPlane||!FMath::IsFinite(MainNearDistance)||MainNearDistance<=0.f) { CloseFrame(TEXT("invalid actual main projection/near")); return; }
    CaptureActor->SetActorLocationAndRotation(MainOrigin,MainRotation); USceneCaptureComponent2D* Capture=CaptureActor->GetCaptureComponent2D();
    Capture->CustomProjectionMatrix=View->ProjectionMatrixUnadjustedForRHI; Capture->FOVAngle=MainFOV;
    CompositeMID->SetScalarParameterValue(TEXT("CaptureValid"),0.f); Current->SetScalarParameterValue(TEXT("CaptureValid"),0.f);
    Current->SetTextureParameterValue(TEXT("OutlineMaskRT"),MaskRT); Current->SetVectorParameterValue(TEXT("RoiMinUV"),FLinearColor(0,0,0,0)); Current->SetVectorParameterValue(TEXT("RoiSizeUV"),FLinearColor(1,1,0,0));
    Current->SetVectorParameterValue(TEXT("MaskSizePx"),FLinearColor(Stage.Size.X,Stage.Size.Y,0,0)); Current->SetVectorParameterValue(TEXT("OutputSizePx"),FLinearColor(Stage.Size.X,Stage.Size.Y,0,0)); Current->SetScalarParameterValue(TEXT("OutlineWidthPx"),2.f);
    UKismetRenderingLibrary::ClearRenderTarget2D(this,MaskRT,FLinearColor::Transparent); Capture->CaptureScene(); // 同正常主View前提交；标准引擎RHI队列维护资源先写后读。
    if(!bCaptureAccepted) { CloseFrame(TEXT("actual capture view rejected")); return; }
    Current->SetScalarParameterValue(TEXT("CaptureValid"),1.f);
}
void UDemoLocalOutlineComponent::ObserveViewFamily(FSceneViewFamily& Family)
{
    DEMO_LOG_TICK(); if(!RequestOwner.IsValid()||!MaskRT||!CaptureActor||SubmissionFrame!=GFrameCounter) return;
    for(const FSceneView* Candidate:Family.Views)
    {
        FSceneView& View=*const_cast<FSceneView*>(Candidate); // 公开GT最终View，尚未入RT，不保留裸指针。
        const bool Capture=View.bIsSceneCapture&&Family.RenderTarget==MaskRT->GameThread_GetRenderTargetResource();
        if(View.bIsSceneCapture&&!Capture) continue;
        if(!Capture&&(Family.RenderTarget!=MainRenderTarget||View.ViewActor!=ViewerPawn.Get())) continue;
        if(View.ViewActor!=ViewerPawn.Get()) { CloseFrame(TEXT("capture owner changed")); return; }
        if(Capture)
        {
            const double Raw=OutlineMatrixDelta(View.ViewMatrices.GetViewMatrix(),MainView); const double Rotation=OutlineMatrixDelta(View.ViewMatrices.GetViewMatrix(),MainView,3,3);
            if(!FMath::IsFinite(Raw)||Raw>.001||Rotation>1.e-7||FVector::Distance(View.ViewMatrices.GetViewOrigin(),MainOrigin)>.001) { CloseFrame(TEXT("capture raw pose mismatch")); return; }
            View.ViewLocation=MainOrigin; View.ViewRotation=MainRotation; View.FOV=MainFOV; View.DesiredFOV=MainFOV; View.UpdateViewMatrix();
            View.CullingFrustum=View.ViewFrustum; View.CullingOrigin=View.ViewMatrices.GetViewOrigin();
            FPlane Near(0,0,0,0); View.bHasNearClippingPlane=View.ViewMatrices.GetViewProjectionMatrix().GetFrustumNearPlane(Near); View.NearClippingPlane=Near; View.NearClippingDistance=static_cast<float>(View.ViewMatrices.ComputeNearPlane());
            if(!View.bHasNearClippingPlane||!FMath::IsFinite(Near.W)||!Near.Equals(MainNearPlane,1.e-5)||!FMath::IsNearlyEqual(View.NearClippingDistance,MainNearDistance,1.e-6f)) { CloseFrame(TEXT("capture near mismatch")); return; }
            View.HiddenPrimitives=MainHidden; // 保本人隐藏物语义，未ShowOnly删掉世界前景遮挡。
            const UMaterialInstanceDynamic* MaskNode=FindOutlineMID(View,CaptureParent?CaptureParent->GetMaterial():nullptr);
            bCaptureAccepted=View.State&&View.AntiAliasingMethod==AAM_None&&View.ViewMatrices.GetTemporalAAJitter().IsNearlyZero()&&View.UnscaledViewRect.Size()==SubmittedStage&&MaskNode;
            if(!bCaptureAccepted) CloseFrame(TEXT("capture AA/state/actual mask/stage invalid"));
        }
        else
        {
            if(!bCaptureAccepted||!View.ViewMatrices.GetViewMatrix().Equals(MainView,1.e-5)||!View.ViewMatrices.ComputeProjectionNoAAMatrix().Equals(MainProjection,1.e-6)||FindOutlineMID(View,CompositeParent?CompositeParent->GetMaterial():nullptr)!=ActualViewMID.Get()) CloseFrame(TEXT("late main view changed"));
            else if(!bFrameClosed)
            {
                LastAcceptedMainFrame=GFrameCounter; // 仅真实main复核后登记，Capture自身回调不能单独算接受。
                if(LastReportedGeneration!=Generation)
                {
                    LastReportedGeneration=Generation;
                    UE_LOG(LogFPSDemo,Log,TEXT("LOCAL_OUTLINE_SUBMITTED stage=%dx%d frame=%llu generation=%u"),SubmittedStage.X,SubmittedStage.Y,LastAcceptedMainFrame,Generation);
                }
            }
        }
        if(Capture&&(!View.ViewMatrices.GetViewMatrix().Equals(MainView,1.e-5)||!View.ViewMatrices.ComputeProjectionNoAAMatrix().Equals(MainProjection,1.e-6))) CloseFrame(TEXT("canonical capture matrix mismatch"));
    }
    if(bCaptureAccepted&&!bFrameClosed) LastCloseReason.Reset();
}
#if WITH_EDITOR
bool UDemoLocalOutlineComponent::ReadLastSubmissionForSmoke(uint64& Frame,FIntPoint& Stage) const
{
    DEMO_LOG_TICK(); Frame=LastAcceptedMainFrame; Stage=SubmittedStage;
    return RequestOwner.IsValid()&&!bFrameClosed&&ActualViewMID.IsValid()&&Frame>0&&Frame<=GFrameCounter&&GFrameCounter-Frame<=2;
}
#endif
void UDemoLocalOutlineComponent::OnApplicationActivationChanged(bool Active)
{ DEMO_LOG_CALL(); bApplicationActive=Active; if(!Active&&RequestOwner.IsValid()) { CloseFrame(TEXT("application deactivated")); RetireCurrentRT(); } }
void UDemoLocalOutlineComponent::EndPlay(EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); if(ActivationHandle.IsValid()&&FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle); ActivationHandle.Reset();
    ClearCurrentTarget(TEXT("controller EndPlay")); ViewExtension.Reset(); RenderState.Reset(); CollectRetiredRTs(); Super::EndPlay(Reason);
}
