// 职责：Hub出屋短暂欠曝的独立Editor诊断；复用正常存档/StartRun/Pawn/Hub，原态与单字段候选分别view-step。
// 使用见同名头；公开SceneView最后已完成曝光只在RT读，GT记录有时延的观测，不冒称当前GPU帧或真实走门测量。
// 不保存资产、不改灯/AA/轮廓、不引入Renderer私有头；Camera仅SpeedDown值及其override位借用恢复。
// 可选-DemoHubExposureSavedVerify只核已保存有界Volume：两阶段Hub最终SpeedDown均6，再恢复Camera并在Volume域外核回1，不作原态AB。
#include "Tests/DemoHubExposureAudit.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/DemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/BrushComponent.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopEnvironment.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Serialization/JsonSerializer.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UObject/Package.h"
#include "UnrealClient.h"

/** RT/GT共享纯值；指针只作为身份地址比较，任何RT回调都不解引用UObject。 */
struct FDemoHubExposureRenderState
{
    struct FSample
    {
        bool bPresent=false; // 已有主View回调；false与曝光0分开记录。
        bool bValidBuffer=false; // 引擎公开HasValidEyeAdaptationBuffer事实，不推断GPU本帧完成。
        bool bCameraCut=false; // 实际主View切镜事实；Teleport若重置历史，不能据未复现基线声称修复。
        float LastExposure=0.f; // 公开GetLastEyeAdaptationExposure最后已完成值，0表示不可证。
        int32 Method=INDEX_NONE; // 最终混合AutoExposureMethod枚举。
        float Bias=0.f,MinEV=0.f,MaxEV=0.f,SpeedUp=0.f,SpeedDown=0.f; // 最终混合曝光参数，ExtendRange下Min/Max为EV100。
        FVector Origin=FVector::ZeroVector; // 实际渲染View世界cm。
        FVector Direction=FVector::ZeroVector; // 实际渲染View单位前向，不读GT当前控制角替代。
        uint64 Frame=0,Sequence=0; // Family.FrameCounter及本扩展序号，均不声称GPU生产帧身份。
        uint64 CameraCutCount=0; // 本观察器全部主View回调累计cut数，不按Phase清零；防止GT10Hz遗漏单帧cut。
        uint64 LastCutSequence=0; // 最近一次cut对应本扩展RT观察序号；0表示尚未观察到cut，非GPU源帧身份。
        double At=0.; // RT读取公开getter的单调秒；该getter自身返回更早完成结果。
    } Latest;
    FCriticalSection Mutex; // 只保护以下身份与小值，不持锁访问GT UObject。
    const void* PawnIdentity=nullptr; // GT写当前Pawn地址，RT仅比较ViewActor。
    const void* MainTarget=nullptr; // GT写真实主Viewport RenderTarget地址，排除私有Capture。
    bool bEnabled=false; // GT关闭后RT不再观察；共享对象由渲染扩展引用保活。
};

namespace
{
    /** 输出有限三维纯值；Value为同步借用的世界cm或单位方向。 */
    TArray<TSharedPtr<FJsonValue>> ExposureVector(const FVector& Value)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        return {MakeShared<FJsonValueNumber>(Value.X),MakeShared<FJsonValueNumber>(Value.Y),MakeShared<FJsonValueNumber>(Value.Z)};
    }
    /** 创建/BeginPlay共用显式门；World只在GT同步借用，不保存全局引用。 */
    bool IsExposureAuditWorld(const UWorld* World)
    {
        DEMO_LOG_STATIC_CALL(World);
#if WITH_EDITOR
        FString Room; // 只接受明确房间，避免默认房间掩盖启动参数错误。
        return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone
            &&World->GetPackage()->GetName()==TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")
            &&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoHubExposureAudit"))
            &&FParse::Value(FCommandLine::Get(),TEXT("DemoHubExposureRoom="),Room)&&(Room==TEXT("Gun")||Room==TEXT("Casino"))
            &&!FParse::Param(FCommandLine::Get(),TEXT("DemoHubEnvironmentAudit"));
#else
        return false;
#endif
    }
}

/** 公开World扩展，所有回调只读；RT只保存公开getter/最终PP/实际POV纯值，不创建额外渲染pass。 */
class FDemoHubExposureViewExtension final : public FWorldSceneViewExtension
{
public:
    /** InWorld只交基类弱引用；InShared由GT/RT共享保活到已排队回调释放。 */
    FDemoHubExposureViewExtension(const FAutoRegister& AutoRegister,UWorld* InWorld,const TSharedRef<FDemoHubExposureRenderState,ESPMode::ThreadSafe>& InShared)
        :FWorldSceneViewExtension(AutoRegister,InWorld),Shared(InShared) { DEMO_LOG_STATIC_CALL(InWorld); }
    /** GT无需修改ViewFamily；保留入口埋点。 */
    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override { DEMO_LOG_STATIC_TICK(nullptr); }
    /** GT无需修改View或最终PP；保留入口埋点。 */
    virtual void SetupView(FSceneViewFamily& InViewFamily,FSceneView& InView) override { DEMO_LOG_STATIC_TICK(nullptr); }
    /** GT无需排附加pass；保留入口埋点。 */
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override { DEMO_LOG_STATIC_TICK(nullptr); }
    /** RT在正常后处理前读公共最后完成曝光；Frame/At为观察时刻，绝非GPU当前帧结果。 */
    virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder,const FSceneView& View,const FPostProcessingInputs& Inputs) override
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        FScopeLock Lock(&Shared->Mutex); // 全程仅View与纯值，不访问Pawn/World UObject。
        if(!Shared->bEnabled||View.bIsSceneCapture||View.bIsReflectionCapture||View.bIsPlanarReflection||!View.Family
            ||View.ViewActor!=Shared->PawnIdentity||View.Family->RenderTarget!=Shared->MainTarget) return;
        FDemoHubExposureRenderState::FSample& Sample=Shared->Latest; // 仅当前锁内借用，GT复制后再序列化。
        Sample.bPresent=true; Sample.bValidBuffer=View.HasValidEyeAdaptationBuffer(); Sample.LastExposure=View.GetLastEyeAdaptationExposure();
        Sample.bCameraCut=View.bCameraCut;
        Sample.Frame=View.Family->FrameCounter; ++Sample.Sequence; Sample.At=FPlatformTime::Seconds();
        if(Sample.bCameraCut) { ++Sample.CameraCutCount; Sample.LastCutSequence=Sample.Sequence; } // 仅记录事实，不触发/禁止cut；GT用出屋前后累计差核查历史重置。
        Sample.Origin=View.ViewMatrices.GetViewOrigin(); Sample.Direction=View.GetViewDirection();
        const FPostProcessSettings& PP=View.FinalPostProcessSettings; // RT本帧最终混合只读，不跨帧保存引用。
        Sample.Method=static_cast<int32>(PP.AutoExposureMethod); Sample.Bias=PP.AutoExposureBias;
        Sample.MinEV=PP.AutoExposureMinBrightness; Sample.MaxEV=PP.AutoExposureMaxBrightness; Sample.SpeedUp=PP.AutoExposureSpeedUp; Sample.SpeedDown=PP.AutoExposureSpeedDown;
    }
private:
    TSharedRef<FDemoHubExposureRenderState,ESPMode::ThreadSafe> Shared; // 纯值桥；扩展无任何RT UObject弱引用解析。
};

bool UDemoHubExposureAudit::ShouldCreateSubsystem(UObject* Outer) const { DEMO_LOG_CALL(); return IsExposureAuditWorld(Cast<UWorld>(Outer)); }
bool UDemoHubExposureAudit::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    return WorldType==EWorldType::Game;
#else
    return false;
#endif
}
void UDemoHubExposureAudit::OnWorldBeginPlay(UWorld& InWorld)
{
    DEMO_LOG_CALL(); Super::OnWorldBeginPlay(InWorld);
#if WITH_EDITOR
    if(!IsExposureAuditWorld(&InWorld)) { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_EXPOSURE_REJECT explicit main-map Editor Standalone/room gate")); return; }
    FString Room; FParse::Value(FCommandLine::Get(),TEXT("DemoHubExposureRoom="),Room); bCasino=Room==TEXT("Casino");
    bActive=true; StartedAt=FPlatformTime::Seconds(); Facts=MakeShared<FJsonObject>();
    const bool bSavedVerify=FParse::Param(FCommandLine::Get(),TEXT("DemoHubExposureSavedVerify")); // 仅当前显式Editor诊断，保存版本验证不改正常游戏注册或Camera恢复范围。
    Facts->SetBoolField(TEXT("saved_volume_verify"),bSavedVerify); Facts->SetBoolField(TEXT("outside_saved_volume_speed_down_verified"),false);
    EvidenceDirectory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HubExposure20261117/Runtime"),Room+TEXT("_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    Facts->SetStringField(TEXT("room"),Room); Facts->SetStringField(TEXT("scope"),TEXT("Normal StartRun, real possessed Pawn view-step from interior to exterior; NOT a CharacterMovement door traversal. Baseline original Camera versus only SpeedDown override6 EV/s;8s interior warmup, exterior requested near0.1s and12s. No asset save, light/AA/highlight change or game/art acceptance."));
    if(bSavedVerify) Facts->SetStringField(TEXT("scope"),TEXT("Saved bounded Hub Volume verification, NOT original-state A/B. Phase0 leaves original Camera SpeedDown1/overridefalse and observes saved Volume final6; Phase1 only Camera SpeedDown6 override. Existing four view-step PNG names/timing retained. After restoring exactly the two Camera fields, collision-aware view-step to Hub-localX-6000cm waits0.3s and verifies fresh actual main View outside saved Volume+blend bounds has finalSpeedDown1 and unchanged other exposure parameters. No continuous walking, asset save, light/AA/highlight change or game acceptance."));
    Facts->SetStringField(TEXT("exposure_limit"),TEXT("Public last-completed exposure read on RT then copied to GT at10Hz; callback time/frame/POV are observations, not exposure GPU source-frame identity. Zero/no-buffer/stale repeated values cannot establish exposure evolution."));
    if(!Check(IFileManager::Get().MakeDirectory(*EvidenceDirectory,true),TEXT("unique Saved evidence directory"))||!CheckBudget(TEXT("start"))) { Finish(false); return; }
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_EXPOSURE_BEGIN room=%s directory=%s"),*Room,*EvidenceDirectory);
#endif
}
bool UDemoHubExposureAudit::IsTickable() const { DEMO_LOG_TICK(); return bActive&&!bFinished&&!IsTemplate(); }
TStatId UDemoHubExposureAudit::GetStatId() const { DEMO_LOG_TICK(); RETURN_QUICK_DECLARE_CYCLE_STAT(UDemoHubExposureAudit,STATGROUP_Tickables); }
bool UDemoHubExposureAudit::Check(bool bCondition,const FString& Message)
{
    DEMO_LOG_CALL();
    const FString Entry=FString::Printf(TEXT("%s phase=%d step=%d %s"),bCondition?TEXT("PASS"):TEXT("FAIL"),Phase,Step,*Message); // 有限诊断断言，不记录每帧字符串。
    if(Checks.Num()<1000) Checks.Add(Entry); UE_LOG(LogFPSDemo,Display,TEXT("HUB_EXPOSURE %s"),*Entry);
    if(!bCondition) bFailed=true; return bCondition;
}
bool UDemoHubExposureAudit::CheckBudget(const FString& Stage)
{
    DEMO_LOG_CALL(); uint64 Total=0,Free=0; int64 Output=0; TArray<FString> Files; // 当前项目盘与本目录实测字节，不使用启动器旧快照。
    const bool bDisk=FPlatformMisc::GetDiskTotalAndFreeSpace(FPaths::ProjectDir(),Total,Free);
    IFileManager::Get().FindFiles(Files,*FPaths::Combine(EvidenceDirectory,TEXT("*")),true,false);
    for(const FString& File:Files) Output+=FMath::Max<int64>(0,IFileManager::Get().FileSize(*FPaths::Combine(EvidenceDirectory,File)));
    return Check(bDisk&&Free>=10ull*1024ull*1024ull*1024ull&&Output<=64ll*1024ll*1024ll,FString::Printf(TEXT("budget %s free=%llu output=%lld"),*Stage,Free,Output));
}
bool UDemoHubExposureAudit::PrepareHub(const UDemoHubShopDefinition* Definition)
{
    DEMO_LOG_CALL(); TArray<ADemoHubShopBlockout*> Blocks; TArray<ADemoHubShopEnvironmentRoot*> Roots; // GT同步借用当前实际World，不生成替代环境。
    for(TActorIterator<ADemoHubShopBlockout> It(GetWorld());It;++It) Blocks.Add(*It);
    for(TActorIterator<ADemoHubShopEnvironmentRoot> It(GetWorld());It;++It) Roots.Add(*It);
    if(!Check(Definition&&Blocks.Num()==1&&Roots.Num()==1,TEXT("one actual generated logical Blockout and persisted environment"))) return false;
    HubFrame=Blocks[0]->GetActorTransform(); FString Error; // 与既有IntegrationAudit相同的真实HubFrame来源。
    if(!Check(Blocks[0]->IsUsingAuthoredEnvironment()&&Blocks[0]->GetGeometryCount()==16&&Roots[0]->GetActorTransform().Equals(HubFrame,.01f)
        &&Roots[0]->EnvironmentId==TEXT("hubshops.ta20261115.r01")&&Roots[0]->Revision==1&&Roots[0]->Entries.Num()==350
        &&Roots[0]->ValidateEnvironment(Definition,HubFrame,Error),TEXT("actual authored Hub closure and frame: ")+Error)) return false;
    UCameraComponent* ActualCamera=Player->GetFirstPersonCameraComponent(); // 原Pawn真正FP Camera；不创建替代Camera。
    if(!Check(ActualCamera&&ActualCamera->GetOwner()==Player.Get()&&Controller->IsLocalController()&&Controller->GetPawn()==Player.Get(),TEXT("actual locally possessed Pawn Camera"))) return false;
    Camera=ActualCamera; OriginalSpeedDown=ActualCamera->PostProcessSettings.AutoExposureSpeedDown; bOriginalOverride=ActualCamera->PostProcessSettings.bOverride_AutoExposureSpeedDown; bFrozen=true;
    Facts->SetNumberField(TEXT("original_camera_speed_down"),OriginalSpeedDown); Facts->SetBoolField(TEXT("original_camera_override_speed_down"),bOriginalOverride);
    if(!Check(OriginalSpeedDown==1.f,TEXT("baseline original Camera SpeedDown exactly1 EV/s"))) return false;
    if(Facts->GetBoolField(TEXT("saved_volume_verify"))&&!Check(!bOriginalOverride,TEXT("saved verification keeps original Camera SpeedDown overridefalse"))) return false;
    if(!Check(GetWorld()->GetGameViewport()&&GetWorld()->GetGameViewport()->Viewport&&GetWorld()->GetGameViewport()->Viewport->GetSizeXY()==FIntPoint(1280,720),TEXT("actual normal main viewport1280x720 bounded four PNGs"))) return false;
    RenderState=MakeShared<FDemoHubExposureRenderState,ESPMode::ThreadSafe>();
    RenderState->PawnIdentity=Player.Get(); RenderState->MainTarget=static_cast<const FRenderTarget*>(GetWorld()->GetGameViewport()->Viewport); RenderState->bEnabled=true;
    ViewExtension=FSceneViewExtensions::NewExtension<FDemoHubExposureViewExtension>(GetWorld(),RenderState.ToSharedRef());
    return true;
}
bool UDemoHubExposureAudit::RestoreCamera()
{
    DEMO_LOG_CALL(); if(!bFrozen) return true;
    UCameraComponent* Current=Camera.Get(); // 弱引用只在GT解析；失效不能伪称恢复成功。
    if(!Current||Current->GetWorld()!=GetWorld()) { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_EXPOSURE_RESTORE_FAILED original Camera unavailable")); return false; }
    Current->PostProcessSettings.AutoExposureSpeedDown=OriginalSpeedDown; Current->PostProcessSettings.bOverride_AutoExposureSpeedDown=bOriginalOverride;
    const bool bRestored=Current->PostProcessSettings.AutoExposureSpeedDown==OriginalSpeedDown&&Current->PostProcessSettings.bOverride_AutoExposureSpeedDown==bOriginalOverride;
    if(Facts) Facts->SetBoolField(TEXT("camera_two_fields_restored"),bRestored);
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_EXPOSURE_RESTORE complete=%d speed=%g override=%d"),bRestored,Current->PostProcessSettings.AutoExposureSpeedDown,static_cast<int32>(Current->PostProcessSettings.bOverride_AutoExposureSpeedDown));
    return bRestored;
}
bool UDemoHubExposureAudit::PlacePawn(const FVector& LocalPoint,const FVector& LocalLookAt)
{
    DEMO_LOG_CALL(); ADemoCharacter* Pawn=Player.Get(); ADemoPlayerController* PC=Controller.Get(); // GT同步借用，身份必须仍为准备时Pawn。
    if(!Check(Pawn&&PC&&PC->GetPawn()==Pawn&&PC->PlayerCameraManager&&PC->PlayerCameraManager->GetViewTarget()==Pawn,TEXT("view-step uses unchanged actual local possessed Pawn and ViewTarget"))) return false;
    const FVector Point=HubFrame.TransformPosition(LocalPoint),Look=HubFrame.TransformPosition(LocalLookAt); // 世界cm，A/B用同一预定局部站位。
    if(!Check(Pawn->TeleportTo(Point,FRotator(0.,(Look-Point).Rotation().Yaw,0.)),TEXT("collision-aware upright Pawn TeleportTo succeeds; view-step not walking"))) return false;
    Pawn->ConsumeMovementInputVector(); Pawn->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation((Look-Pawn->GetSharedViewLocation()).Rotation());
    return Check(Pawn->GetActorLocation().Equals(Point,.01),TEXT("actual Pawn station matches requested collision-safe position within0.01cm"));
}
bool UDemoHubExposureAudit::BeginPhase(const UDemoHubShopDefinition* Definition)
{
    DEMO_LOG_CALL(); if(!Check(RestoreCamera(),TEXT("phase begins from exact frozen two-field Camera state"))) return false;
    if(Phase==1) { Camera->PostProcessSettings.bOverride_AutoExposureSpeedDown=true; Camera->PostProcessSettings.AutoExposureSpeedDown=6.f; }
    if(!Check(Camera->PostProcessSettings.AutoExposureSpeedDown==(Phase==1?6.f:OriginalSpeedDown)
        &&Camera->PostProcessSettings.bOverride_AutoExposureSpeedDown==(Phase==1?true:bOriginalOverride),TEXT("only intended Camera SpeedDown pair read back"))) return false;
    const FVector Center=bCasino?Definition->CasinoCenter:Definition->GunShopCenter,Size=bCasino?Definition->CasinoInteriorSize:Definition->GunShopInteriorSize; // 当前正式DA局部cm。
    const double Height=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.; // 站姿胶囊保持现版，不倾斜/关碰撞。
    const FVector Inside(Center.X-Size.X*.5+460.590491,Center.Y,Height); // 复用既有西门CMC内站位的实际行程偏移。
    OutsidePoint=FVector(Center.X-Size.X*.5-200.,Center.Y,Height); OutsideLook=OutsidePoint+FVector(-1500.,0.,65.);
    if(!PlacePawn(Inside,Center+FVector(0.,0.,160.))) return false;
    WarmupAt=FPlatformTime::Seconds(); ExitAt=0.; NextSampleAt=0.; Step=2;
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_EXPOSURE_PHASE phase=%d speed_down=%g interior_warmup_seconds=8"),Phase,Camera->PostProcessSettings.AutoExposureSpeedDown);
    return true;
}
void UDemoHubExposureAudit::RecordSample(bool bForce)
{
    DEMO_LOG_TICK(); const double Now=FPlatformTime::Seconds(); // GT采样时刻，和RT观察时刻分别记录。
    if(!RenderState||(!bForce&&Now<NextSampleAt)) return; NextSampleAt=Now+.1;
    if(Samples.Num()>=1000) { Check(false,TEXT("bounded exposure sample array reached1000")); return; }
    FDemoHubExposureRenderState::FSample Latest; { FScopeLock Lock(&RenderState->Mutex); Latest=RenderState->Latest; } // 复制纯值后释放锁再访问JSON。
    TSharedPtr<FJsonObject> Row=MakeShared<FJsonObject>(); // 只在GT分配，无RT JSON或UObject引用。
    Row->SetNumberField(TEXT("phase"),Phase); Row->SetStringField(TEXT("station"),Step==5?TEXT("outside_saved_volume"):(ExitAt>0.?TEXT("exterior"):TEXT("interior_warmup")));
    Row->SetNumberField(TEXT("gt_observed_seconds"),Now-StartedAt); Row->SetNumberField(TEXT("seconds_after_view_step"),ExitAt>0.?Now-ExitAt:-1.);
    Row->SetNumberField(TEXT("warmup_elapsed_seconds"),Now-WarmupAt); Row->SetBoolField(TEXT("render_callback_present"),Latest.bPresent);
    Row->SetNumberField(TEXT("rt_observed_seconds"),Latest.bPresent?Latest.At-StartedAt:-1.); Row->SetNumberField(TEXT("rt_to_gt_age_seconds"),Latest.bPresent?Now-Latest.At:-1.);
    Row->SetNumberField(TEXT("rt_observation_sequence"),static_cast<double>(Latest.Sequence)); Row->SetNumberField(TEXT("view_family_frame_counter"),static_cast<double>(Latest.Frame));
    Row->SetBoolField(TEXT("valid_eye_adaptation_buffer"),Latest.bValidBuffer); Row->SetNumberField(TEXT("last_completed_exposure"),Latest.LastExposure);
    Row->SetBoolField(TEXT("view_camera_cut"),Latest.bCameraCut);
    Row->SetNumberField(TEXT("view_camera_cut_count_cumulative"),static_cast<double>(Latest.CameraCutCount));
    Row->SetNumberField(TEXT("view_last_camera_cut_observation_sequence"),static_cast<double>(Latest.LastCutSequence));
    Row->SetBoolField(TEXT("exposure_observation_usable"),Latest.bPresent&&Latest.bValidBuffer&&FMath::IsFinite(Latest.LastExposure)&&Latest.LastExposure>0.f);
    Row->SetArrayField(TEXT("view_origin_world_cm"),ExposureVector(Latest.Origin)); Row->SetArrayField(TEXT("view_direction"),ExposureVector(Latest.Direction));
    Row->SetNumberField(TEXT("final_exposure_method_enum"),Latest.Method); Row->SetNumberField(TEXT("final_bias"),Latest.Bias); Row->SetNumberField(TEXT("final_min_ev100"),Latest.MinEV); Row->SetNumberField(TEXT("final_max_ev100"),Latest.MaxEV);
    Row->SetNumberField(TEXT("final_speed_up_ev_per_s"),Latest.SpeedUp); Row->SetNumberField(TEXT("final_speed_down_ev_per_s"),Latest.SpeedDown);
    Samples.Add(MakeShared<FJsonValueObject>(Row));
}
bool UDemoHubExposureAudit::RequestCapture(const FString& Name,double TargetDelay)
{
    DEMO_LOG_CALL(); if(!Check(!bCapturePending&&!FScreenshotRequest::IsScreenshotRequested()&&Captures.Num()<4,TEXT("only one owned screenshot pending; maximum four PNGs"))||!CheckBudget(Name)) return false;
    PendingCapture=FPaths::Combine(EvidenceDirectory,Name+TEXT(".png")); CaptureAt=FPlatformTime::Seconds(); CaptureTarget=TargetDelay; CaptureTick=TickCount; bCapturePending=true;
    Controller->GetPlayerViewPoint(CaptureEye,CaptureRotation); CaptureFov=Controller->PlayerCameraManager->GetFOVAngle();
    FScreenshotRequest::RequestScreenshot(PendingCapture,false,false); // 正常主视口，不创建SceneCapture或替身Camera。
    UE_LOG(LogFPSDemo,Display,TEXT("HUB_EXPOSURE_SCREENSHOT_REQUEST phase=%d target=%.3f actual_after_step=%.6f file=%s"),Phase,TargetDelay,CaptureAt-ExitAt,*PendingCapture);
    return true;
}
bool UDemoHubExposureAudit::AdvanceCapture()
{
    DEMO_LOG_TICK(); if(!bCapturePending) return false; const double Now=FPlatformTime::Seconds(); const int64 Bytes=IFileManager::Get().FileSize(*PendingCapture); // GT只查本次固定输出。
    if(TickCount>CaptureTick&&Bytes>1000)
    {
        FVector Eye; FRotator Aim; Controller->GetPlayerViewPoint(Eye,Aim); // 当前正常PC视点是写盘观察时刻，不声称截图GPU对应帧。
        TSharedPtr<FJsonObject> Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("phase"),Phase); Row->SetStringField(TEXT("file"),PendingCapture);
        Row->SetNumberField(TEXT("target_after_step_seconds"),CaptureTarget); Row->SetNumberField(TEXT("actual_request_after_step_seconds"),CaptureAt-ExitAt); Row->SetNumberField(TEXT("actual_written_after_step_seconds"),Now-ExitAt);
        Row->SetNumberField(TEXT("request_to_written_seconds"),Now-CaptureAt); Row->SetNumberField(TEXT("png_bytes"),static_cast<double>(Bytes));
        Row->SetArrayField(TEXT("pc_eye_at_request_cm"),ExposureVector(CaptureEye)); Row->SetArrayField(TEXT("pc_rotation_at_request_deg"),ExposureVector(FVector(CaptureRotation.Pitch,CaptureRotation.Yaw,CaptureRotation.Roll))); Row->SetNumberField(TEXT("fov_at_request_deg"),CaptureFov);
        Row->SetBoolField(TEXT("request_later_than_target_plus_0_15s"),CaptureAt-ExitAt>CaptureTarget+.15); Row->SetArrayField(TEXT("pc_eye_at_write_cm"),ExposureVector(Eye));
        Row->SetArrayField(TEXT("pc_rotation_at_write_deg"),ExposureVector(FVector(Aim.Pitch,Aim.Yaw,Aim.Roll))); Row->SetNumberField(TEXT("fov_at_write_deg"),Controller->PlayerCameraManager->GetFOVAngle());
        Captures.Add(MakeShared<FJsonValueObject>(Row)); bCapturePending=false; return CheckBudget(TEXT("after_png_write"));
    }
    if(Now-CaptureAt>3.) Check(false,TEXT("normal FScreenshotRequest did not produce PNG within3 seconds")); return false;
}
void UDemoHubExposureAudit::Tick(float DeltaTime)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if(!bActive||bFinished) return; ++TickCount; const double Now=FPlatformTime::Seconds();
    const bool bSavedVerify=Facts->GetBoolField(TEXT("saved_volume_verify")); // 本进程冻结诊断模式，仅SavedVerify允许Step5；不新增UCLASS字段/正常注册。
    if(Now-StartedAt>90.) Check(false,TEXT("total bounded runtime exceeded90 real seconds"));
    if(bFailed) { Finish(false); return; }
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(GetWorld(),0)); ADemoCharacter* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); UDemoRunSaves* Saves=GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr;
    if(!PC||!Pawn||!Mode||!State||!Saves) { if(bFrozen) Check(false,TEXT("prepared actual local Pawn/PC/runtime service lost")); return; }
    if(bFrozen&&(Player.Get()!=Pawn||Controller.Get()!=PC||Camera.Get()!=Pawn->GetFirstPersonCameraComponent()||!PC->PlayerCameraManager||PC->PlayerCameraManager->GetViewTarget()!=Pawn)) { Check(false,TEXT("local Avatar/Camera/ViewTarget changed during diagnostic")); return; }
    Controller=PC; Player=Pawn; UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>(); const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr;
    if(Step>=2) RecordSample(false);
    switch(Step)
    {
    case 0:
    {
        UDemoMenuFlowComponent* Flow=PC->GetMenuFlow(); if(!Flow) return;
        Flow->UpdateRealtime(0.f); if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) Flow->DispatchUIAction(EDemoUIAction::OverlayBack);
        if(!Check(!Flow->HasBlockingOverlay()&&Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("original local temporary CreateSlot/StartRun enters Hub"))) return;
        Saves->ConsumePending(); PC->OnRunReady(); Step=1; break;
    }
    case 1:
        if(State->Phase!=EDemoPhase::Hub||!Definition||!Registry||!Registry->GetWorldEpoch()) return;
        // 原World服务用0.5秒Timer生成Blockout；Epoch在Initialize已有效，不能把首帧尚未生成误报成场景损坏。
        // 仅等待原服务自然完成，不主动触发生成；总90秒预算仍覆盖等待，重复/错场景留给PrepareHub严格拒绝。
        { TActorIterator<ADemoHubShopBlockout> Ready(GetWorld()); if(!Ready) return; }
        if(PrepareHub(Definition)) BeginPhase(Definition); break;
    case 2:
    {
        if(Now-WarmupAt<8.) return;
        FDemoHubExposureRenderState::FSample Latest; { FScopeLock Lock(&RenderState->Mutex); Latest=RenderState->Latest; } // 预热端点确认候选确实进入正常主View，而非只写到未参与混合的Camera。
        if(!Check(Latest.bPresent&&Now-Latest.At<1.&&Latest.SpeedDown==((Phase==1||bSavedVerify)?6.f:1.f),TEXT("fresh actual main View final SpeedDown matches phase and saved-verification mode; getter0 is separately inconclusive"))) return;
        if(bSavedVerify&&Phase==0)
        {
            // 冻结已保存Volume域内除SpeedDown以外的实际最终曝光，后续域外必须完全保持；这些值不是GPU当前EV。
            Facts->SetNumberField(TEXT("saved_verify_hub_final_method"),Latest.Method); Facts->SetNumberField(TEXT("saved_verify_hub_final_bias"),Latest.Bias);
            Facts->SetNumberField(TEXT("saved_verify_hub_final_min_ev100"),Latest.MinEV); Facts->SetNumberField(TEXT("saved_verify_hub_final_max_ev100"),Latest.MaxEV); Facts->SetNumberField(TEXT("saved_verify_hub_final_speed_up"),Latest.SpeedUp);
        }
        RecordSample(true); if(!PlacePawn(OutsidePoint,OutsideLook)) return; ExitAt=FPlatformTime::Seconds(); Step=3;
        UE_LOG(LogFPSDemo,Display,TEXT("HUB_EXPOSURE_VIEW_STEP phase=%d interior_elapsed=%.6f"),Phase,ExitAt-WarmupAt); break;
    }
    case 3:
        if(bCapturePending) { if(AdvanceCapture()) Step=4; break; }
        if(Now-ExitAt>=.1) RequestCapture(Phase==0?TEXT("01_Baseline_Exterior_Early"):TEXT("03_SpeedDown6_Exterior_Early"),.1); break;
    case 4:
        if(bCapturePending)
        {
            if(AdvanceCapture())
            {
                RecordSample(true);
                if(Phase==0) { Phase=1; BeginPhase(Definition); }
                else if(Check(Captures.Num()==4,TEXT("four actual PNGs written with actual timing")))
                {
                    if(!bSavedVerify) Finish(true);
                    else
                    {
                        if(!Check(RestoreCamera(),TEXT("saved verification restores exactly two Camera fields before outside-domain observation"))) return;
                        const double Height=Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.; // 保持普通碰撞/重力；域外只观察0.3秒，不扩步行范围。
                        if(!PlacePawn(FVector(-6000.,0.,Height),FVector(-7500.,0.,160.))) return;
                        ExitAt=FPlatformTime::Seconds(); Step=5;
                        UE_LOG(LogFPSDemo,Display,TEXT("HUB_EXPOSURE_SAVED_OUTSIDE_BEGIN requested_hub_local_x_cm=-6000 camera_original_speed=%g override=%d"),Camera->PostProcessSettings.AutoExposureSpeedDown,static_cast<int32>(Camera->PostProcessSettings.bOverride_AutoExposureSpeedDown));
                    }
                }
            }
            break;
        }
        // 先前6秒基线仍在恢复；延长到12秒以核对稳态，不能把未收敛的晚图差异误称亮度改变。
        if(Now-ExitAt>=12.) RequestCapture(Phase==0?TEXT("02_Baseline_Exterior_Late"):TEXT("04_SpeedDown6_Exterior_Late"),12.); break;
    case 5:
    {
        if(!Check(bSavedVerify,TEXT("outside-domain step requires explicit SavedVerify"))) return;
        if(Now-ExitAt<.3) return;
        FDemoHubExposureRenderState::FSample Latest; { FScopeLock Lock(&RenderState->Mutex); Latest=RenderState->Latest; } // 只取最新RT纯值，不能拿GT目标位置冒充实际渲染域外。
        bool bOutsideActualBounds=false; int32 SavedVolumes=0; // 仅计稳定tag的实际保存PPV；AABB含BlendRadius为保守域外证明。
        for(TActorIterator<APostProcessVolume> It(GetWorld());It;++It)
        {
            APostProcessVolume* Volume=*It; // 同步GT借用真实World对象，不在RT访问。
            if(!Volume->Tags.Contains(FName(TEXT("Demo.Hub.ExposureTransition")))) continue;
            ++SavedVolumes;
            if(!Check(Volume->bEnabled&&!Volume->bUnbound&&Volume->GetBrushComponent(),TEXT("saved tagged Volume remains enabled and bounded with actual brush"))) return;
            const FBox Influence=Volume->GetBrushComponent()->Bounds.GetBox().ExpandBy(Volume->BlendRadius); // 真实世界brush bounds外扩实际blend cm，不使用计划目标Box。
            bOutsideActualBounds=!Influence.IsInsideOrOn(Latest.Origin);
            Facts->SetArrayField(TEXT("outside_saved_volume_influence_min_world_cm"),ExposureVector(Influence.Min)); Facts->SetArrayField(TEXT("outside_saved_volume_influence_max_world_cm"),ExposureVector(Influence.Max));
        }
        const bool bUnchangedOtherExposure=Latest.Method==static_cast<int32>(Facts->GetNumberField(TEXT("saved_verify_hub_final_method")))
            &&Latest.Bias==Facts->GetNumberField(TEXT("saved_verify_hub_final_bias"))&&Latest.MinEV==Facts->GetNumberField(TEXT("saved_verify_hub_final_min_ev100"))
            &&Latest.MaxEV==Facts->GetNumberField(TEXT("saved_verify_hub_final_max_ev100"))&&Latest.SpeedUp==Facts->GetNumberField(TEXT("saved_verify_hub_final_speed_up")); // 域外只应回到SpeedDown1，其他原曝光值不变。
        if(!Check(Latest.bPresent&&Latest.At>ExitAt&&Now-Latest.At<.2&&Latest.SpeedDown==1.f&&SavedVolumes==1&&bOutsideActualBounds&&bUnchangedOtherExposure
            &&Camera->PostProcessSettings.AutoExposureSpeedDown==OriginalSpeedDown&&Camera->PostProcessSettings.bOverride_AutoExposureSpeedDown==bOriginalOverride,
            TEXT("fresh actual main View outside unique saved Volume+blend bounds has SpeedDown1, unchanged other exposure and restored Camera pair"))) return;
        RecordSample(true); Facts->SetBoolField(TEXT("outside_saved_volume_speed_down_verified"),true); Facts->SetNumberField(TEXT("outside_saved_volume_final_speed_down"),Latest.SpeedDown);
        Facts->SetArrayField(TEXT("outside_saved_volume_view_origin_world_cm"),ExposureVector(Latest.Origin)); Facts->SetNumberField(TEXT("outside_saved_volume_wait_seconds"),Now-ExitAt);
        Finish(true); break;
    }
    default: Check(false,TEXT("unexpected bounded diagnostic step")); break;
    }
#endif
}
void UDemoHubExposureAudit::Finish(bool bSuccess)
{
    DEMO_LOG_CALL(); if(bFinished) return; bFinished=true;
    if(RenderState) { FScopeLock Lock(&RenderState->Mutex); RenderState->bEnabled=false; } ViewExtension.Reset();
    if(!RestoreCamera()) bSuccess=false;
    if(bCapturePending&&FScreenshotRequest::IsScreenshotRequested()&&FScreenshotRequest::GetFilename()==PendingCapture) FScreenshotRequest::Reset();
    bCapturePending=false; if(ADemoCharacter* Pawn=Player.Get()) { Pawn->ConsumeMovementInputVector(); Pawn->GetCharacterMovement()->StopMovementImmediately(); }
    if(!Facts) Facts=MakeShared<FJsonObject>(); Facts->SetArrayField(TEXT("samples"),Samples); Facts->SetArrayField(TEXT("actual_written_captures"),Captures);
    TArray<TSharedPtr<FJsonValue>> Entries; for(const FString& Entry:Checks) Entries.Add(MakeShared<FJsonValueString>(Entry)); Facts->SetArrayField(TEXT("checks"),Entries);
    Facts->SetStringField(TEXT("status"),bSuccess?TEXT("PASS_VIEW_STEP_DIAGNOSTIC_NOT_ACCEPTANCE"):TEXT("FAIL_VIEW_STEP_DIAGNOSTIC")); Facts->SetNumberField(TEXT("elapsed_real_seconds"),FPlatformTime::Seconds()-StartedAt);
    Facts->SetBoolField(TEXT("light_aa_highlight_asset_changes_requested"),false); Facts->SetBoolField(TEXT("actual_walk_through_door_measured"),false);
    const bool bBudget=CheckBudget(TEXT("before_report")); // 最后断言也必须进入最终报告，不能序列化后才追加。
    Entries.Reset(); for(const FString& Entry:Checks) Entries.Add(MakeShared<FJsonValueString>(Entry)); Facts->SetArrayField(TEXT("checks"),Entries);
    FString Text; const bool bSerialized=FJsonSerializer::Serialize(Facts.ToSharedRef(),TJsonWriterFactory<>::Create(&Text)); // GT最终一次序列化，UTF8报告与目录合计另限64MiB。
    const FTCHARToUTF8 Utf8(*Text); int64 ExistingBytes=0; TArray<FString> OutputFiles; // 只计算本次目录，绝不枚举/删除玩家文件。
    IFileManager::Get().FindFiles(OutputFiles,*FPaths::Combine(EvidenceDirectory,TEXT("*")),true,false);
    for(const FString& File:OutputFiles) ExistingBytes+=FMath::Max<int64>(0,IFileManager::Get().FileSize(*FPaths::Combine(EvidenceDirectory,File)));
    const bool bWritten=bSerialized&&bBudget&&Utf8.Length()<4*1024*1024&&ExistingBytes+Utf8.Length()<=64ll*1024ll*1024ll
        &&FFileHelper::SaveStringToFile(Text,*FPaths::Combine(EvidenceDirectory,TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    const bool bPassed=bSuccess&&bWritten; UE_LOG(LogFPSDemo,Display,TEXT("HUB_EXPOSURE_%s directory=%s"),bPassed?TEXT("SUCCESS"):TEXT("FAILED"),*EvidenceDirectory);
#if WITH_EDITOR
    FPlatformMisc::RequestExitWithStatus(false,bPassed?0:1); // 只存在于完整显式Standalone创建门后的本次诊断进程。
#endif
}
void UDemoHubExposureAudit::Deinitialize()
{
    DEMO_LOG_CALL(); if(bActive&&!bFinished) Finish(false); else RestoreCamera();
    bActive=false; if(RenderState) { FScopeLock Lock(&RenderState->Mutex); RenderState->bEnabled=false; }
    ViewExtension.Reset(); RenderState.Reset(); Camera.Reset(); Player.Reset(); Controller.Reset(); Super::Deinitialize();
}
