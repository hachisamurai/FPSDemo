// 职责：只在Editor显式-game运行SlotsPointInput审查；正常槽/Hub/E租约与IA_Fire事件注入，真实组件/磁盘读取。
// 边界：不改相机骨/控件位置/正式资产/规则/开奖结果；至多4图、小JSON，90秒/64MiB/10GiB守门，非Editor不创建。
#include "Tests/DemoSlotsPointInputAudit.h"
#include "Characters/DemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h" // 只读本Avatar注册的253 mask数量，检测单通道旧组件残留。
#include "Components/StaticMeshComponent.h" // 额外墙枪只读实际源/正常Focus克隆的同模型契约，不调用工厂。
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h" // TemporalProbe同时核实际组件世界框与最终POV，不能只拿ControlRotation证明镜头稳定。
#include "Camera/PlayerCameraManager.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h"
#include "Economy/DemoShopComponent.h"
#include "EnhancedPlayerInput.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h" // 仅显式Editor探针临时ABA并完整恢复，不改生产配置或材质。
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h"
#include "Interaction/HubShops/Casino/DemoCasinoLocalViewComponent.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Crc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h" // 固定候选PP domain/location与游戏线程shader完成门，不使用Renderer私有接口。
#include "MaterialDomain.h" // 非Unity明确提供MD_PostProcess，不依赖PCH传递枚举。
#include "Engine/BlendableInterface.h" // 非Unity明确提供BL_SceneColorAfterDOF。
#include "MaterialShared.h"
#include "Misc/SecureHash.h" // 固定T02本机包SHA1前置校验，不接收任意资产路径。
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerState.h"
#include "Save/DemoRunSave.h"
#include "Serialization/JsonSerializer.h"
#include "UI/DemoHUD.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UnrealClient.h"
#include "UObject/Package.h" // 只读Package dirty前后状态，不清他人dirty或调用资产保存。
#include "Weapons/DemoWeaponComponent.h"

#if WITH_EDITOR
namespace
{
    TWeakObjectPtr<UDemoSlotsPointInputAudit> CandidateProcessLease; // Editor显式独立进程仅一个GameWorld可借用共享DA/CVar。
    const TCHAR* CandidateObjectPath=TEXT("/Game/Developers/OutlineTemporal20261116/T02/M_HubOutline_BeforeTSR_T02.M_HubOutline_BeforeTSR_T02"); // 唯一作者已释放的T02候选，不允许T01或fallback。
}
#endif

void UDemoSlotsPointInputAudit::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Super::Initialize(Collection);
#if WITH_EDITOR
    FString Candidate; bCandidateRequested=FParse::Value(FCommandLine::Get(),TEXT("DemoOutlineCandidate="),Candidate);
    bTemporalRequested=FParse::Param(FCommandLine::Get(),TEXT("DemoSlotsOutlineTemporalProbe"));
    FString Target; const bool HasTarget=FParse::Value(FCommandLine::Get(),TEXT("DemoOutlineTarget="),Target); // 仅枚举Keycap/RifleOblique，不接收对象路径。
    bGunTarget=HasTarget&&Target==TEXT("RifleOblique");
    if(HasTarget&&!Check((Target==TEXT("Keycap")||bGunTarget)&&(!bGunTarget||(bCandidateRequested&&Candidate==TEXT("BeforeTSR_T02")&&bTemporalRequested)),TEXT("outline target is Keycap or explicit T02 RifleOblique"))) bFailed=true;
    if(bCandidateRequested)
    {
        StartedReal=FPlatformTime::Seconds();
        UE_LOG(LogFPSDemo,Log,TEXT("SLOTS_CANDIDATE_INIT world=%s world_id=%u audit_id=%u"),*GetWorld()->GetPathName(),GetWorld()->GetUniqueID(),GetUniqueID()); // 同名Audit_0可属于不同World；用真实对象身份区分生命周期。
        if(!Check(Candidate==TEXT("BeforeTSR_T02")&&bTemporalRequested,TEXT("only explicit BeforeTSR_T02 plus temporal probe is allowed"))||!InstallCandidate(Collection)) bFailed=true;
    }
#endif
}

bool UDemoSlotsPointInputAudit::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const UWorld* World=Cast<UWorld>(Outer); // 引擎拥有的候选World，本入口不创建状态或输入。
    FString Candidate; const bool CandidateMode=FParse::Value(FCommandLine::Get(),TEXT("DemoOutlineCandidate="),Candidate); // 只对额外候选模式排除引擎启动临时GameWorld，旧普通/ABA入口保持。
    const bool CandidateWorld=!CandidateMode||(World&&World->GetOutermost()->GetName()==TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox"));
    if(CandidateMode) UE_LOG(LogFPSDemo,Log,TEXT("SLOTS_CANDIDATE_WORLD_GATE world=%s actual_persistent_map=%d"),World?*World->GetOutermost()->GetName():TEXT("none"),CandidateWorld);
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone&&CandidateWorld&&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoSlotsPointInputAudit"));
#else
    return false;
#endif
}
bool UDemoSlotsPointInputAudit::DoesSupportWorldType(EWorldType::Type WorldType) const
{ DEMO_LOG_CALL(); return WITH_EDITOR&&WorldType==EWorldType::Game; }
void UDemoSlotsPointInputAudit::OnWorldBeginPlay(UWorld& InWorld)
{
    DEMO_LOG_CALL(); Super::OnWorldBeginPlay(InWorld);
#if WITH_EDITOR
    if(StartedReal<=0.) StartedReal=FPlatformTime::Seconds(); StartedWorld=InWorld.GetTimeSeconds(); bTemporalRequested=FParse::Param(FCommandLine::Get(),TEXT("DemoSlotsOutlineTemporalProbe"));
    Directory=FPaths::ProjectSavedDir()/TEXT("SlotsPointInput20261116")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    if(!Check(FApp::CanEverRender()&&IFileManager::Get().MakeDirectory(*Directory,true),TEXT("actual RHI and unique evidence directory"))||!CheckBudget(TEXT("begin"))||bFailed) { Finish(false); return; }
    PostActorHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&UDemoSlotsPointInputAudit::OnPostActorTick); // 弱UObject/本World游戏线程；Finish和卸载都解除，不跨线程或留裸捕获。
#endif
}
void UDemoSlotsPointInputAudit::Deinitialize()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    RestoreTemporalProbe(); bInjectDown=false; InjectPrimary();
    if(PostActorHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(PostActorHandle); PostActorHandle.Reset();
#endif
    Super::Deinitialize();
}
#if WITH_EDITOR
bool UDemoSlotsPointInputAudit::InstallCandidate(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Collection.InitializeDependency<UDemoHubShopWorldSubsystem>();
    const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    UDemoHubShopDefinition* Definition=Registry?const_cast<UDemoHubShopDefinition*>(Registry->GetDefinition()):nullptr; // 仅此显式夹具借用公开软引用，生产仍按同目录首次加载。
    TArray<uint8> PackageBytes; const FString File=FPaths::ProjectContentDir()/TEXT("Developers/OutlineTemporal20261116/T02/M_HubOutline_BeforeTSR_T02.uasset"); // 固定作者包，不访问第三方路径。
    const bool HasBytes=FFileHelper::LoadFileToArray(PackageBytes,*File);
    const FString Hash=HasBytes?FSHA1::HashBuffer(PackageBytes.GetData(),PackageBytes.Num()).ToString():TEXT("missing");
    if(!Check(!CandidateProcessLease.IsValid()&&Definition&&!Definition->GetOutermost()->IsDirty()&&Hash.Equals(TEXT("560DC5397D155C13F3C90D8C36478B758A722EA2"),ESearchCase::IgnoreCase),TEXT("unique process/world lease, clean actual DA, exact T02 source SHA1 ")+Hash)) return false;
    CandidateMaterial.Reset(LoadObject<UMaterial>(nullptr,CandidateObjectPath));
    if(!Check(CandidateMaterial.IsValid()&&CandidateMaterial->GetPathName()==CandidateObjectPath&&CandidateMaterial->MaterialDomain==MD_PostProcess&&CandidateMaterial->BlendableLocation==BL_SceneColorAfterDOF,TEXT("T02 actual base material is PostProcess SceneColorAfterDOF"))) return false;
    const FMaterialResource* BeforeResource=CandidateMaterial->GetMaterialResource(GetWorld()->GetFeatureLevel()); // 先记录真实资源状态，区分地图时序修复与确需标准缓存，不预设null原因。
    const IConsoleVariable* QualityVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaterialQualityLevel")); // 仅记当前请求质量，不将CVar冒充材质资源实际已编译。
    UE_LOG(LogFPSDemo,Log,TEXT("SLOTS_CANDIDATE_SHADER_BEFORE world=%s feature_level=%d requested_quality=%d can_render=%d resource_null=%d is_compiling=%d compilation_finished=%d compile_errors=%d shader_map_complete=%d"),*GetWorld()->GetPathName(),static_cast<int32>(GetWorld()->GetFeatureLevel()),QualityVariable?QualityVariable->GetInt():INDEX_NONE,FApp::CanEverRender(),BeforeResource==nullptr,CandidateMaterial->IsCompiling(),BeforeResource&&BeforeResource->IsCompilationFinished(),BeforeResource?BeforeResource->GetCompileErrors().Num():INDEX_NONE,BeforeResource&&BeforeResource->IsGameThreadShaderMapComplete());
    if(!BeforeResource||!BeforeResource->IsGameThreadShaderMapComplete()) CandidateMaterial->CacheShaders(EMaterialShaderPrecompileMode::Synchronous); // 仅实际持久World缺资源时一次正常缓存；不强制改StateId、编辑或保存。
    const FMaterialResource* InitialResource=CandidateMaterial->GetMaterialResource(GetWorld()->GetFeatureLevel()); // 实际FeatureLevel资源，不以资产已加载或是否仍有编译任务代替shader-map完成。
    UE_LOG(LogFPSDemo,Log,TEXT("SLOTS_CANDIDATE_SHADER_CACHE world=%s feature_level=%d resource_null=%d is_compiling=%d shader_map_complete=%d elapsed_real=%.3f"),*GetWorld()->GetOutermost()->GetName(),static_cast<int32>(GetWorld()->GetFeatureLevel()),InitialResource==nullptr,CandidateMaterial->IsCompiling(),InitialResource&&InitialResource->IsGameThreadShaderMapComplete(),FPlatformTime::Seconds()-StartedReal);
    if(!Check(FPlatformTime::Seconds()-StartedReal<=90.,TEXT("candidate synchronous shader cache remains inside total90 real-second fixture budget"))) return false;
    IConsoleVariable* Jitter=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter"));
    if(!Check(Jitter&&Jitter->GetInt()==0&&!(Jitter->GetFlags()&ECVF_ReadOnly),TEXT("candidate snapshots actual original Config jitter0 before any candidate frame"))) return false;
    TemporalOriginalValue=Jitter->GetInt(); TemporalOriginalFlags=static_cast<uint32>(Jitter->GetFlags()); bTemporalOriginalSaved=true;
    CandidateDefinition.Reset(Definition); CandidateOriginalMaterial=Definition->HighlightMaterial; bCandidateOriginalDirty=Definition->GetOutermost()->IsDirty();
    CandidateProcessLease=this; Definition->HighlightMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(CandidateObjectPath)); bCandidateApplied=true;
    CandidateCleanupHandle=FWorldDelegates::OnWorldCleanup.AddUObject(this,&UDemoSlotsPointInputAudit::OnCandidateWorldCleanup); // 本World弱UObject清理，任何失败/卸载恢复；不跨线程。
    Jitter->Set(1,ECVF_SetByConsole); // 仅显式候选进程，从Initialize首次候选帧前一直保持到整个夹具退出。
    return Check(Jitter->GetInt()==1&&!Definition->GetOutermost()->IsDirty(),TEXT("temporary public DA reference and jitter1 applied without dirtying or saving assets"));
}
void UDemoSlotsPointInputAudit::OnCandidateWorldCleanup(UWorld* World,bool SessionEnded,bool CleanupResources)
{
    DEMO_LOG_CALL(); if(World!=GetWorld()) return;
    if(!bFinished) Check(false,TEXT("unexpected candidate World cleanup before fixture finish; abort rather than resume after rollback")); // 锁存失败后即使本World还有末帧回调也只能Finish(false)，不得继续用缓存候选。
    if(!RestoreTemporalProbe()) bFailed=true;
}
void UDemoSlotsPointInputAudit::ReleaseTemporalCapture()
{
    DEMO_LOG_CALL();
    if(TemporalCaptureHandle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(TemporalCaptureHandle); TemporalCaptureHandle.Reset();
    if(bTemporalCapturePending&&FScreenshotRequest::IsScreenshotRequested()&&FScreenshotRequest::GetFilename()==TemporalOwnedCaptureName) FScreenshotRequest::Reset();
    bTemporalCapturePending=false; bTemporalCaptureReady=false; bTemporalActive=false;
}
bool UDemoSlotsPointInputAudit::RestoreCandidate()
{
    DEMO_LOG_CALL(); if(!bCandidateRequested) return true;
    if(bCandidateWidthSaved&&TemporalMID.IsValid()) TemporalMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),CandidateOriginalWidth);
    bCandidateWidthSaved=false;
    if(bCandidateCameraSaved&&Controller.IsValid()) Controller->SetControlRotation(CandidateOriginalRotation); bCandidateCameraSaved=false;
    bool Restored=true; // 仅恢复仍属于本夹具的软引用；外部变更与dirty必须失败留证，不能清别人的dirty。
    if(bCandidateApplied)
    {
        Restored=CandidateDefinition.IsValid()&&CandidateDefinition->HighlightMaterial.ToSoftObjectPath()==FSoftObjectPath(CandidateObjectPath)&&CandidateDefinition->GetOutermost()->IsDirty()==bCandidateOriginalDirty;
        if(CandidateDefinition.IsValid()&&CandidateDefinition->HighlightMaterial.ToSoftObjectPath()==FSoftObjectPath(CandidateObjectPath)) CandidateDefinition->HighlightMaterial=CandidateOriginalMaterial;
        Restored&=CandidateDefinition.IsValid()&&CandidateDefinition->HighlightMaterial==CandidateOriginalMaterial;
        bCandidateApplied=false; bCandidateRestored=Restored;
        if(!Restored) Check(false,TEXT("candidate DA changed externally or package dirty changed; no blanket dirty clearing"));
    }
    if(CandidateCleanupHandle.IsValid()) FWorldDelegates::OnWorldCleanup.Remove(CandidateCleanupHandle); CandidateCleanupHandle.Reset();
    if(CandidateProcessLease.Get()==this) CandidateProcessLease.Reset();
    return Restored&&(!CandidateDefinition.IsValid()||bCandidateRestored);
}
bool UDemoSlotsPointInputAudit::Check(bool Condition,const FString& Message)
{
    DEMO_LOG_CALL(); const FString Value=FString::Printf(TEXT("%s step=%d %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Step,*Message);
    Checks.Add(Value); UE_LOG(LogFPSDemo,Log,TEXT("SLOTS_POINT_AUDIT_CHECK %s"),*Value); if(!Condition) bFailed=true; return Condition;
}
bool UDemoSlotsPointInputAudit::CheckBudget(const FString& Stage)
{
    DEMO_LOG_CALL(); uint64 Total=0,Free=0; const bool Measured=FPlatformMisc::GetDiskTotalAndFreeSpace(FPaths::ProjectDir(),Total,Free);
    TArray<FString> Files; IFileManager::Get().FindFilesRecursive(Files,*Directory,TEXT("*"),true,false);
    int64 Bytes=0; for(const FString& File:Files) Bytes+=FMath::Max<int64>(0,IFileManager::Get().FileSize(*File));
    FString Log; if(FParse::Value(FCommandLine::Get(),TEXT("abslog="),Log)&&!Files.Contains(Log)) Bytes+=FMath::Max<int64>(0,IFileManager::Get().FileSize(*Log));
    return Check(Measured&&Free>=uint64(10)*1024*1024*1024&&Bytes<=64LL*1024*1024,FString::Printf(TEXT("10GiB reserve/64MiB evidence stage=%s free=%llu bytes=%lld"),*Stage,Free,Bytes));
}
bool UDemoSlotsPointInputAudit::InjectPrimary()
{
    DEMO_LOG_TICK(); if(!Controller.IsValid()||!FireAction.IsValid()) return false;
    UEnhancedPlayerInput* Input=Cast<UEnhancedPlayerInput>(Controller->PlayerInput); // PC当前真实PlayerInput，不建替代输入组件或调用被测业务函数。
    if(!Input) return false; Input->InjectInputForAction(FireAction.Get(),FInputActionValue(bInjectDown)); return true;
}
bool UDemoSlotsPointInputAudit::AimControl(FName Id)
{
    DEMO_LOG_CALL(); const UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    FVector Point; if(!Check(Player.IsValid()&&Controller.IsValid()&&Controls&&Controls->GetControlPoint(Id,Point),TEXT("actual registered control point ")+Id.ToString())) return false;
    AimedControl=Id; AimStarted=FPlatformTime::Seconds(); Controller->SetControlRotation((Point-Player->GetSharedViewLocation()).Rotation()); return true;
}
bool UDemoSlotsPointInputAudit::AimedControlReady()
{
    DEMO_LOG_TICK(); const UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    const UDemoCasinoDeviceInputComponent* Input=Controller.IsValid()?Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>():nullptr;
    FString Error; const bool Hit=Controls&&Player.IsValid()&&Controls->ValidateControlHit(Player.Get(),AimedControl,Error);
    if(Hit&&(!Input||!Input->IsOperating()||Input->GetFocusedControlId()==AimedControl)) return true;
    if(FPlatformTime::Seconds()-AimStarted>2.) { Check(false,TEXT("first physical ray/focus did not settle: ")+AimedControl.ToString()+TEXT(" ")+Error); return false; }
    FVector Point; if(Controls&&Player.IsValid()&&Controls->GetControlPoint(AimedControl,Point)) { Controller->SetControlRotation((Point-Player->GetSharedViewLocation()).Rotation()); ++AimCorrections; }
    return false;
}
bool UDemoSlotsPointInputAudit::CheckQueries()
{
    DEMO_LOG_TICK(); const UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    if(!Controls||QueryIds.Num()!=15||QueryRest.Num()!=15) return Check(false,TEXT("fifteen query snapshot remains available"));
    ++QueryChecks;
    for(int32 Index=0;Index<15;++Index) { FVector Point; if(!Controls->GetControlPoint(QueryIds[Index],Point)) return Check(false,TEXT("query still exists")); MaximumQueryShift=FMath::Max(MaximumQueryShift,FVector::Distance(QueryRest[Index],Point)); if(!Point.Equals(QueryRest[Index],.001)) return Check(false,TEXT("private feedback moved fixed query: ")+QueryIds[Index].ToString()); }
    return true;
}
bool UDemoSlotsPointInputAudit::BeginKeyPress(FName Id,const FString& Expected,int32 Resume)
{
    DEMO_LOG_CALL(); if(!AimControl(Id)||!Model.IsValid()) return false;
    PressPart=NAME_None;
    for(const FDemoCasinoModelPart& Part:Model->Parts) if(Part.Kind==EDemoCasinoModelPartKind::Key&&Part.ControlId==Id) { PressPart=Part.PartId; PressRest=Part.RestTransform*Device->GetActorTransform(); break; }
    if(!Check(!PressPart.IsNone(),TEXT("same-version real key mesh exists ")+Id.ToString())) return false;
    ExpectedAmount=Expected; ResumeStep=Resume; MaximumKeyTravel=0.; Step=100; NextReal=FPlatformTime::Seconds()+.08; return true;
}
void UDemoSlotsPointInputAudit::ReadKeyMotion()
{
    DEMO_LOG_TICK(); if(PressPart.IsNone()||!Controller.IsValid()) return;
    const UDemoCasinoLocalViewComponent* View=Controller->FindComponentByClass<UDemoCasinoLocalViewComponent>();
    FTransform Actual; if(View&&View->ReadModelPartPoseForAudit(PressPart,Actual))
    {
        MaximumKeyTravel=FMath::Max(MaximumKeyTravel,FVector::Distance(Actual.GetTranslation(),PressRest.GetTranslation())); MaximumObservedKeyTravel=FMath::Max(MaximumObservedKeyTravel,MaximumKeyTravel);
        FName Control; FString Material; FTransform Outline; // 从实际组件读取，不拿预期运动公式重造轮廓位置。
        if(View->ReadFocusedOutlineForAudit(Control,Material,Outline)&&Control==AimedControl)
        {
            ++OutlinePosePairSamples;
            const double Distance=FVector::Distance(Outline.GetTranslation(),Actual.GetTranslation());
            const double Angle=FMath::RadiansToDegrees(Outline.GetRotation().AngularDistance(Actual.GetRotation()));
            MaximumOutlineSourceError=FMath::Max(MaximumOutlineSourceError,Distance); MaximumOutlineAngleError=FMath::Max(MaximumOutlineAngleError,Angle);
            const double Travel=FVector::Distance(Outline.GetTranslation(),PressRest.GetTranslation()); MaximumOutlineTravel=FMath::Max(MaximumOutlineTravel,Travel);
            if(AimedControl==TEXT("amount.digit.0")&&Travel>.05&&Distance<.01&&Angle<.01) bOutlinePressedMotionObserved=true;
            if(!bFailed&&(Distance>.01||Angle>.01||!Outline.GetScale3D().Equals(Actual.GetScale3D(),.0001))) Check(false,TEXT("actual edge mask failed to follow moving key Mesh, not Query"));
        }
    }
}
int32 UDemoSlotsPointInputAudit::CountPawnSilhouetteMasks() const
{
    DEMO_LOG_TICK(); if(!Player.IsValid()) return 0;
    TInlineComponentArray<UPrimitiveComponent*> Components(Player.Get()); // 当前真实Pawn拥有的有限组件；不枚举整个World/其它玩家。
    int32 Count=0;
    for(const UPrimitiveComponent* Component:Components)
        if(IsValid(Component)&&Component->IsRegistered()&&Component->bRenderCustomDepth&&Component->CustomDepthStencilValue==253&&Component->IsVisible()&&!Component->bHiddenInGame) ++Count;
    return Count;
}
bool UDemoSlotsPointInputAudit::CheckFocusedOutline(FName Expected,const FString& Label)
{
    DEMO_LOG_CALL(); const UDemoCasinoLocalViewComponent* View=Controller.IsValid()?Controller->FindComponentByClass<UDemoCasinoLocalViewComponent>():nullptr;
    FName ActualControl; FString Material; FTransform Outline;
    if(!Check(View&&View->ReadFocusedOutlineForAudit(ActualControl,Material,Outline)&&ActualControl==Expected&&!Material.IsEmpty(),TEXT("actual registered253 edge mask and camera material: ")+Label)) return false;
    FName PartId; // 依实际同版模型语义找到动态源，不能用静态Query位置验证按压。
    if(Model.IsValid()) for(const FDemoCasinoModelPart& Part:Model->Parts) if(Part.ControlId==Expected&&(Part.Kind==EDemoCasinoModelPartKind::Key||Part.Kind==EDemoCasinoModelPartKind::Lever)) { PartId=Part.PartId; break; }
    FTransform Source; const bool Read=!PartId.IsNone()&&View->ReadModelPartPoseForAudit(PartId,Source);
    const double Distance=Read?FVector::Distance(Outline.GetTranslation(),Source.GetTranslation()):MAX_dbl;
    const double Angle=Read?FMath::RadiansToDegrees(Outline.GetRotation().AngularDistance(Source.GetRotation())):MAX_dbl;
    const int32 Masks=CountPawnSilhouetteMasks();
    if(!Check(Read&&Distance<.01&&Angle<.01&&Outline.GetScale3D().Equals(Source.GetScale3D(),.0001)&&Masks==1,TEXT("one current-Pawn edge mask matches actual focused source; no old253 mask: ")+Label)) return false;
    TSharedPtr<FJsonObject> Fact=MakeShared<FJsonObject>(); Fact->SetStringField(TEXT("label"),Label); Fact->SetStringField(TEXT("control_id"),ActualControl.ToString()); Fact->SetStringField(TEXT("material"),Material); Fact->SetStringField(TEXT("source_part"),PartId.ToString());
    Fact->SetStringField(TEXT("actual_outline_world"),Outline.ToString()); Fact->SetStringField(TEXT("actual_source_world"),Source.ToString()); Fact->SetNumberField(TEXT("position_error_cm"),Distance); Fact->SetNumberField(TEXT("rotation_error_deg"),Angle); Fact->SetNumberField(TEXT("current_pawn_visible_stencil253_count"),Masks);
    OutlineFacts.Add(MakeShared<FJsonValueObject>(Fact)); return true;
}
bool UDemoSlotsPointInputAudit::ReadGunOutlineContext(FTransform& Source,FTransform& Outline,FString& Error) const
{
    DEMO_LOG_TICK();
    const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    const UCameraComponent* Camera=Player.IsValid()?Player->GetFirstPersonCameraComponent():nullptr;
    const UPrimitiveComponent* Mesh=GunSource.Get(); const UPrimitiveComponent* Mask=TemporalMask.Get();
    if(!bGunTarget||!Registry||!CandidateDefinition.IsValid()||!GunDisplay.IsValid()||Registry->FindDisplay(TEXT("gunshop_rifle_01"))!=GunDisplay.Get()||!GunDisplay->IsDisplayReady()||!Mesh||!Mesh->IsRegistered()||GunDisplay->GetDisplayMesh()!=Mesh||!Player.IsValid()||!Controller.IsValid()||!Controller->IsCoopRegistered()||Controller->GetPawn()!=Player.Get()||Controller->GetViewTarget()!=Player.Get()||Controller->IsPaused()||!Player->CanUsePlayerSkills()||!State||State->Phase!=EDemoPhase::Hub||!Camera||Camera->PostProcessBlendWeight<=0.f)
    { Error=TEXT("actual registered rifle/Hub/alive/Coop/Pawn/source/Camera gate failed"); return false; }
    FVector Eye; FRotator Rotation; Controller->GetPlayerViewPoint(Eye,Rotation); FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(SlotsGunOutlineAudit),false,Player.Get()); // 原实际视向第一Visibility命中，不能以软焦点或私有调用建立轮廓。
    GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Rotation.Vector()*CandidateDefinition->InteractionDistanceCm,ECC_Visibility,Query);
    if(Hit.GetActor()!=GunDisplay.Get()) { Error=TEXT("actual first Visibility actor is not registered rifle: ")+GetNameSafe(Hit.GetActor()); return false; }
    bool ActiveMID=false; for(const FWeightedBlendable& Entry:Camera->PostProcessSettings.WeightedBlendables.Array) ActiveMID|=Entry.Object==TemporalMID.Get()&&Entry.Weight>0.f;
    if(!Mask||!Mask->IsRegistered()||Mask->GetOwner()!=Player.Get()||!Mask->bOnlyOwnerSee||Mask->bRenderInMainPass||Mask->bRenderInDepthPass||!Mask->bRenderCustomDepth||Mask->CustomDepthStencilValue!=253||!Mask->IsVisible()||Mask->bHiddenInGame||Mask->GetCollisionEnabled()!=ECollisionEnabled::NoCollision||CountPawnSilhouetteMasks()!=1||!TemporalMID.IsValid()||TemporalMID->Parent!=CandidateMaterial.Get()||!ActiveMID)
    { Error=TEXT("normal ShopFocus actual unique private253 mask or active T02 Camera MID missing"); return false; }
    bool SameModel=false; // 正常枪mask独立世界变换，不采用Keycap的attach约束；骨骼源须真实LeaderPose指向该展示。
    if(const UStaticMeshComponent* Static=Cast<UStaticMeshComponent>(Mesh)) { const UStaticMeshComponent* Clone=Cast<UStaticMeshComponent>(Mask); SameModel=Clone&&Static->GetStaticMesh()&&Clone->GetStaticMesh()==Static->GetStaticMesh(); }
    else if(const USkeletalMeshComponent* Skin=Cast<USkeletalMeshComponent>(Mesh)) { const USkeletalMeshComponent* Clone=Cast<USkeletalMeshComponent>(Mask); SameModel=Clone&&Skin->GetSkeletalMeshAsset()&&Clone->GetSkeletalMeshAsset()==Skin->GetSkeletalMeshAsset()&&Clone->LeaderPoseComponent.Get()==Skin; }
    bool SameMaterials=Mesh->GetNumMaterials()==Mask->GetNumMaterials(); for(int32 Index=0;Index<Mesh->GetNumMaterials();++Index) SameMaterials&=Mesh->GetMaterial(Index)==Mask->GetMaterial(Index);
    Source=Mesh->GetComponentTransform(); Outline=Mask->GetComponentTransform();
    if(!SameModel||!SameMaterials||!Source.Equals(Outline,.000001)) { Error=TEXT("actual rifle mask/source model/material/leader-pose/world-transform mismatch"); return false; }
    return true;
}
void UDemoSlotsPointInputAudit::AdvanceGunFixture()
{
    DEMO_LOG_TICK(); const double Now=FPlatformTime::Seconds(); if(Now<NextReal) return;
    UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>(); const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    if(Step==200)
    {
        if(!Registry||!Registry->GetDefinition()||!State||State->Phase!=EDemoPhase::Hub) return;
        const UDemoHubShopDefinition* Definition=Registry->GetDefinition(); ADemoGunDisplay* Display=Registry->FindDisplay(TEXT("gunshop_rifle_01"));
        if(!Display||!Display->IsDisplayReady()||!Display->GetDisplayMesh()) return;
        const FDemoHubDisplayBinding* Binding=Definition->FindDisplay(TEXT("gunshop_rifle_01")); const FDemoHubWeaponOffer* Offer=Binding?Definition->FindOffer(Binding->OfferId):nullptr;
        if(!Check(Binding&&Offer&&Offer->WeaponId==TEXT("rifle")&&Display->OfferId==Binding->OfferId,TEXT("actual registered gunshop_rifle_01 DA binding resolves rifle"))) return;
        GunDisplay=Display; GunSource=Display->GetDisplayMesh(); GunAimPoint=Display->GetFocusPoint();
        FVector Stand=GunAimPoint+Display->GetActorTransform().TransformVectorNoScale(FVector(110,-200,0)); Stand.Z=Player->GetActorLocation().Z; // 作者局部-Y朝屋内200cm/+X切向110cm，保留真实当前地面Pawn Z，正常碰撞Teleport。
        if(!Check(Player->TeleportTo(Stand,Player->GetActorRotation()),TEXT("oblique gun placement uses normal collision-aware TeleportTo"))) return;
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->ConsumeMovementInputVector(); Controller->SetControlRotation((GunAimPoint-Player->GetSharedViewLocation()).Rotation());
        GunSettleStarted=Now; Step=201; return;
    }
    if(Step==201)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(Player.Get()); for(UPrimitiveComponent* Component:Components) if(IsValid(Component)&&Component->IsRegistered()&&Component->bRenderCustomDepth&&Component->CustomDepthStencilValue==253&&Component->IsVisible()&&!Component->bHiddenInGame) TemporalMask=Component; // 只观察正常ShopFocus Tick已创建对象。
        const UCameraComponent* Camera=Player->GetFirstPersonCameraComponent(); int32 Matches=0;
        for(const FWeightedBlendable& Entry:Camera->PostProcessSettings.WeightedBlendables.Array) if(UMaterialInstanceDynamic* MID=Cast<UMaterialInstanceDynamic>(Entry.Object); MID&&Entry.Weight>0.f&&MID->Parent==CandidateMaterial.Get()) { TemporalMID=MID; ++Matches; }
        FTransform Source,Outline; FString Error;
        if(Matches!=1||!ReadGunOutlineContext(Source,Outline,Error)) { if(Now-GunSettleStarted>2.) Check(false,TEXT("normal oblique rifle Focus/mask/MID did not settle within2 real seconds: ")+Error); return; }
        const FVector Outward=GunDisplay->GetActorTransform().TransformVectorNoScale(FVector(0,-1,0)).GetSafeNormal2D(); const FVector ToEye=(Player->GetSharedViewLocation()-GunAimPoint).GetSafeNormal2D();
        GunObliqueAngle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Outward,ToEye),-1.,1.))); GunFocusDistance=FVector::Distance(Player->GetSharedViewLocation(),GunAimPoint);
        if(!Check(GunObliqueAngle>20.&&GunFocusDistance<=CandidateDefinition->InteractionDistanceCm,TEXT("actual oblique registered rifle remains inside normal interaction distance"))) return;
        Step=202; BeginTemporalProbe(); return;
    }
    if(Step==203||Step==205)
    {
        FTransform Source,Outline; FString Error; const float Expected=Step==203?2.f:0.f;
        if(!Check(ReadGunOutlineContext(Source,Outline,Error)&&Source.Equals(TemporalSourceReference,.000001)&&Outline.Equals(TemporalOutlineReference,.000001)&&TemporalMID->K2_GetScalarParameterValue(TEXT("OutlineWidthPx"))==Expected,TEXT("same actual rifle source/mask/MID at original fixed screenshot view: ")+Error)) return;
        FVector Location; FRotator Rotation; Controller->GetPlayerViewPoint(Location,Rotation); if(!Check(FTransform(Rotation,Location).Equals(TemporalFinalViewReference,.000001),TEXT("gun on/off PNGs use same actual fixed final POV"))) return;
        Step=Step==203?204:206; Capture(Expected>0.f?TEXT("01_RifleOblique_Fixed_On_T02"):TEXT("02_RifleOblique_Fixed_Off_T02")); return;
    }
    if(Step==204) { bCandidateWidthSaved=true; TemporalMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),0.f); Step=205; NextReal=Now+1.; return; } // 正常已绘制帧暖机一真实秒后才取off图，不生成序列。
    if(Step==206)
    {
        TemporalMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),CandidateOriginalWidth); bCandidateWidthSaved=false;
        const ADemoPlayerState* PS=Player->GetPlayerState<ADemoPlayerState>(); const UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
        if(!Check(bTemporalComplete&&TemporalPhases.Num()==4&&Shots.Num()==2&&Progress&&Progress->GetSnapshot().Coins==GunInitialCoins&&Progress->GetSnapshot().Gambling.Serial==GunInitialSerial&&!FireAction.IsValid()&&!bInjectDown&&PressedControls.IsEmpty(),TEXT("four gun groups, two normal PNGs, no IA_Fire/E/purchase/bet after initial fixture budget"))||!CheckBudget(TEXT("gun_finish"))) return;
        Finish(true); return;
    }
    if(Step==202) Check(false,TEXT("gun temporal state became inactive before four groups completed"));
}
bool UDemoSlotsPointInputAudit::BeginTemporalProbe()
{
    DEMO_LOG_CALL();
    IConsoleVariable* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter")); // 引擎全局变量借用，仅本显式独立进程临时修改。
    const UDemoCasinoLocalViewComponent* View=Controller->FindComponentByClass<UDemoCasinoLocalViewComponent>();
    UCameraComponent* Camera=Player->GetFirstPersonCameraComponent(); FName Control; FTransform Outline;
    if(bGunTarget)
    {
        FTransform Source; FString Error; if(!Check(bTemporalRequested&&Variable&&!(Variable->GetFlags()&ECVF_ReadOnly)&&Camera&&ReadGunOutlineContext(Source,Outline,Error),TEXT("actual normal ShopFocus rifle outline ready for four-group partial ROI probe: ")+Error)) return false;
        Control=TEXT("gunshop_rifle_01"); TemporalPart=Control; TemporalMaterial=CandidateMaterial->GetPathName();
    }
    else if(!Check(bTemporalRequested&&Variable&&!(Variable->GetFlags()&ECVF_ReadOnly)&&View&&Camera&&View->ReadFocusedOutlineForAudit(Control,TemporalMaterial,Outline)&&Control==TEXT("amount.digit.0")&&CountPawnSilhouetteMasks()==1,TEXT("temporal probe has actual fixed digit0 edge and mutable jitter CVar"))) return false;
    TInlineComponentArray<UPrimitiveComponent*> Components(Player.Get());
    for(UPrimitiveComponent* Component:Components) if(IsValid(Component)&&Component->IsRegistered()&&Component->bRenderCustomDepth&&Component->CustomDepthStencilValue==253&&Component->IsVisible()&&!Component->bHiddenInGame) TemporalMask=Component;
    int32 Matches=0; // 同一相机只能有一个该材质MID；不能任取另一系统的同父材质对象当本轮证据。
    for(const FWeightedBlendable& Entry:Camera->PostProcessSettings.WeightedBlendables.Array)
        if(UMaterialInstanceDynamic* MID=Cast<UMaterialInstanceDynamic>(Entry.Object); MID&&Entry.Weight>0.f&&MID->Parent&&MID->Parent->GetPathName()==TemporalMaterial) { TemporalMID=MID; ++Matches; }
    if(!bGunTarget) for(const FDemoCasinoModelPart& Part:Model->Parts) if(Part.ControlId==TEXT("amount.digit.0")&&Part.Kind==EDemoCasinoModelPartKind::Key) TemporalPart=Part.PartId;
    if(!Check(TemporalMask.IsValid()&&TemporalMID.IsValid()&&Matches==1&&!TemporalPart.IsNone(),TEXT("temporal probe locks actual mask/MID object identity and dynamic source"))) return false;
    if(!bCandidateRequested) { TemporalOriginalValue=Variable->GetInt(); TemporalOriginalFlags=static_cast<uint32>(Variable->GetFlags()); bTemporalOriginalSaved=true; } // 候选已在Initialize保存原0/flags，绝不能把现在1误记为原值。
    TemporalMetadata=MakeShared<FJsonObject>(); TemporalMetadata->SetStringField(TEXT("scope"),TEXT("Optional Editor fixed actual camera/source/same253mask+sameCameraMID; normal drawn-frame screenshot delegate; jitter1/0/1 with8 valid warm callbacks+16 valid observations each; no raw sequence or improvement assertion."));
    TemporalMetadata->SetStringField(TEXT("mask_path"),TemporalMask->GetPathName()); TemporalMetadata->SetNumberField(TEXT("mask_unique_id"),TemporalMask->GetUniqueID()); TemporalMetadata->SetStringField(TEXT("mid_path"),TemporalMID->GetPathName()); TemporalMetadata->SetNumberField(TEXT("mid_unique_id"),TemporalMID->GetUniqueID()); TemporalMetadata->SetStringField(TEXT("material"),TemporalMaterial); TemporalMetadata->SetStringField(TEXT("source_part"),TemporalPart.ToString());
    TemporalMetadata->SetNumberField(TEXT("original_jitter"),TemporalOriginalValue); TemporalMetadata->SetNumberField(TEXT("original_flags"),TemporalOriginalFlags); TemporalMetadata->SetStringField(TEXT("roi"),TEXT("96x96 center cropped from UE5.4 ProcessScreenShots LDR1280x720 Bitmap callback bytes; white minRGB>=250 and maxRGB-minRGB<=2; HUD/key whites not semantically separated."));
    TemporalMetadata->SetStringField(TEXT("actual_view_aa_measurement"),TEXT("Runtime CVar and actual camera/component/POV recorded; actual FSceneView AA method/projection jitter is not measured by this fixture."));
    if(bCandidateRequested)
    {
        if(!Check(bCandidateApplied&&CandidateMaterial.IsValid()&&TemporalMID->Parent==CandidateMaterial.Get()&&TemporalOriginalValue==0,TEXT("T02 same actual Camera MID parent with original pre-first-frame jitter snapshot"))) return false;
        CandidateOriginalWidth=TemporalMID->K2_GetScalarParameterValue(TEXT("OutlineWidthPx"));
        if(!Check(FMath::IsNearlyEqual(CandidateOriginalWidth,2.f),TEXT("candidate original effective OutlineWidthPx is2"))) return false;
        bCandidateWidthSaved=true; CandidateOriginalRotation=Controller->GetControlRotation(); bCandidateCameraSaved=true; CandidatePawnLocation=Player->GetActorLocation();
        CandidateRequestBefore=Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>()->GetLastSubmittedRequestId();
        CandidatePhaseStarted=FPlatformTime::Seconds(); CandidateNextSample=CandidatePhaseStarted; CandidateYawOffset=0.; CandidateMaximumPressTravel=0.;
        TemporalMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),0.f); // 同一活跃MID宽度0/2正对照，不删除PP/clone或改权重。
        TemporalMetadata->SetStringField(TEXT("mode"),TEXT("BeforeTSR_T02 six groups fixed/press/slowyaw width0/2, all jitter1; original four-PNG input fixture retained"));
        TemporalMetadata->SetStringField(TEXT("candidate_actual_mid_parent"),TemporalMID->Parent->GetPathName()); TemporalMetadata->SetStringField(TEXT("candidate_actual_base_material"),CandidateMaterial->GetPathName());
        TemporalMetadata->SetNumberField(TEXT("candidate_actual_blendable_location"),static_cast<int32>(CandidateMaterial->BlendableLocation));
        TemporalMetadata->SetStringField(TEXT("candidate_source_sha1"),TEXT("560DC5397D155C13F3C90D8C36478B758A722EA2")); TemporalMetadata->SetStringField(TEXT("candidate_author_sha256"),TEXT("787FA0B2D26710D1A9C8F924919D9E1AF6715A0C97E7BDF11769E67A619321FD"));
        TemporalMetadata->SetStringField(TEXT("candidate_pairing_scope"),TEXT("Projected actual mask/source Mesh world-AABB eight corners padded3px intersect center ROI is diagnostic, not semantic stencil coverage. Sixteen off RGB ROIs held only in memory; press uses nearest measured off-source position and reports partial matches; slow uses same sample/yaw. Actual SceneView AA/jitter/input rect unmeasured."));
        if(bGunTarget)
        {
            TemporalMetadata->SetStringField(TEXT("scope"),TEXT("RifleOblique only: normal production ShopFocus Tick and actual first Visibility registered gun; four fixed/slowyaw width0/2 groups, all jitter1; partial center96 receiver/edge crop, not whole gun/stencil coverage; no E/Fire/purchase or keycap acceptance."));
            TemporalMetadata->SetStringField(TEXT("mode"),TEXT("BeforeTSR_T02 RifleOblique four groups fixed/slowyaw off/on")); TemporalMetadata->SetStringField(TEXT("roi_scope"),TEXT("PARTIAL_CENTER96_GUN_RECEIVER_EDGE; no measured effect means UNVERIFIED_ROI, not proof gun invisible"));
            TemporalMetadata->SetStringField(TEXT("target_id"),TEXT("gunshop_rifle_01")); TemporalMetadata->SetStringField(TEXT("source_path"),GunSource->GetPathName()); TemporalMetadata->SetNumberField(TEXT("source_unique_id"),GunSource->GetUniqueID()); TemporalMetadata->SetStringField(TEXT("actual_aim_point"),GunAimPoint.ToString()); TemporalMetadata->SetNumberField(TEXT("actual_oblique_angle_degrees"),GunObliqueAngle); TemporalMetadata->SetNumberField(TEXT("actual_focus_distance_cm"),GunFocusDistance);
            FString ModelPath; if(const UStaticMeshComponent* Static=Cast<UStaticMeshComponent>(GunSource.Get())) ModelPath=Static->GetStaticMesh()->GetPathName(); else if(const USkeletalMeshComponent* Skin=Cast<USkeletalMeshComponent>(GunSource.Get())) ModelPath=Skin->GetSkeletalMeshAsset()->GetPathName();
            TemporalMetadata->SetStringField(TEXT("actual_source_model"),ModelPath); TemporalMetadata->SetBoolField(TEXT("gun_mask_requires_attachment"),false); TemporalMetadata->SetStringField(TEXT("actual_view_aa_measurement"),TEXT("Output1280x720 and runtime nominal resolution CVars recorded; actual SceneView AA/jitter/input rect unmeasured."));
        }
    }
    TSharedPtr<FJsonObject> CVars=MakeShared<FJsonObject>();
    for(const TCHAR* Name:{TEXT("r.AntiAliasingMethod"),TEXT("r.PostProcessAAQuality"),TEXT("r.CustomDepth"),TEXT("r.CustomDepthTemporalAAJitter"),TEXT("r.ScreenPercentage"),TEXT("r.TemporalAA.Upsampling"),TEXT("r.TSR.History.ScreenPercentage"),TEXT("r.HDR.EnableHDROutput")}) if(const IConsoleVariable* CVar=IConsoleManager::Get().FindConsoleVariable(Name)) CVars->SetStringField(Name,CVar->GetString());
    IConsoleVariable* ScreenshotDelegate=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate")); // 默认1；不为采样改额外全局变量。
    FViewport* Viewport=GetWorld()->GetGameViewport()?GetWorld()->GetGameViewport()->Viewport:nullptr;
    if(!ScreenshotDelegate||ScreenshotDelegate->GetInt()!=1||!Viewport||Viewport->GetSceneHDREnabled()||Viewport->GetSizeXY()!=FIntPoint(1280,720)||FScreenshotRequest::IsScreenshotRequested()||UGameViewportClient::OnScreenshotCaptured().IsBound()) { FailTemporalSampling(TEXT("require default ScreenshotDelegate1, SDR1280x720, no pending request and no other screenshot listeners")); return false; }
    CVars->SetStringField(TEXT("r.ScreenshotDelegate"),ScreenshotDelegate->GetString()); TemporalMetadata->SetNumberField(TEXT("screenshot_delegate_original_flags"),static_cast<uint32>(ScreenshotDelegate->GetFlags()));
    TemporalMetadata->SetObjectField(TEXT("runtime_cvars_before"),CVars); TemporalPhase=0; TemporalWarmFrames=0; TemporalLastFrame=MAX_uint64; bTemporalActive=true;
    TemporalWhiteCounts.Reset(); TemporalFlipCounts.Reset(); // 新probe不继承上一轮空间计数；第一次有效测量回调才分配并清零。
    TemporalCaptureHandle=UGameViewportClient::OnScreenshotCaptured().AddUObject(this,&UDemoSlotsPointInputAudit::OnTemporalScreenshotCaptured); // 引擎ProcessScreenShots GT弱UObject委托；只本probe绑定，正常四PNG前解除。
    Variable->Set(1,ECVF_SetByConsole); // 只在显式独立进程的三段边沿设置最高已知优先级；结束恢复完整原flags。
    return Check(Variable->GetInt()==1,TEXT("temporal first A actually reads jitter1"));
}
bool UDemoSlotsPointInputAudit::RestoreTemporalProbe()
{
    DEMO_LOG_CALL();
    ReleaseTemporalCapture(); const bool CandidateRestored=RestoreCandidate(); // 六组结束只Release；整个夹具结束/失败/卸载才还候选及原jitter。
    if(!bTemporalOriginalSaved) return CandidateRestored;
    IConsoleVariable* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter"));
    if(Variable) { Variable->Set(TemporalOriginalValue,ECVF_SetByConsole); Variable->SetFlags(static_cast<EConsoleVariableFlags>(TemporalOriginalFlags)); }
    bTemporalRestored=Variable&&Variable->GetInt()==TemporalOriginalValue&&static_cast<uint32>(Variable->GetFlags())==TemporalOriginalFlags&&CandidateRestored;
    if(bTemporalRestored) bTemporalOriginalSaved=false; // 失败仍保留恢复责任，Finish/Deinitialize可再尝试，不隐去失败。
    UE_LOG(LogFPSDemo,Log,TEXT("SLOTS_TEMPORAL_RESTORE actual=%d value=%d flags=%u"),bTemporalRestored,Variable?Variable->GetInt():INDEX_NONE,Variable?static_cast<uint32>(Variable->GetFlags()):0);
    return bTemporalRestored;
}
void UDemoSlotsPointInputAudit::FailTemporalSampling(const FString& Message)
{
    DEMO_LOG_CALL(); bTemporalSamplingFailure=true;
    if(TemporalMetadata.IsValid()) TemporalMetadata->SetStringField(TEXT("sampling_failure_reason"),Message);
    if(!bFailed) Check(false,TEXT("SAMPLING_FAILURE_NOT_MATERIAL_EVIDENCE: ")+Message); // 只锁存；不在截图广播栈修改CVar/委托或退出。
}
bool UDemoSlotsPointInputAudit::ReadTemporalContext(TSharedPtr<FJsonObject>& Facts)
{
    DEMO_LOG_TICK();
    IConsoleVariable* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter"));
    const IConsoleVariable* DelegateVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate"));
    const int32 Expected=bCandidateRequested?1:TemporalPhase==1?0:1; // 候选六组始终1；旧ABA只在GT Tick切换，回调绝不改该值。
    const UDemoCasinoLocalViewComponent* View=Controller.IsValid()?Controller->FindComponentByClass<UDemoCasinoLocalViewComponent>():nullptr;
    const UDemoCasinoDeviceInputComponent* Input=Controller.IsValid()?Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>():nullptr;
    UCameraComponent* Camera=Player.IsValid()?Player->GetFirstPersonCameraComponent():nullptr;
    FViewport* Viewport=GetWorld()->GetGameViewport()?GetWorld()->GetGameViewport()->Viewport:nullptr;
    FName Control; FString Material; FTransform Outline,Source;
    bool HasSameMID=false; if(Camera) for(const FWeightedBlendable& Entry:Camera->PostProcessSettings.WeightedBlendables.Array) HasSameMID|=Entry.Object==TemporalMID.Get()&&Entry.Weight>0.f;
    FString TargetError; bool TargetContext=false; // 按钮走原真实Operate/API，额外枪走正常ShopFocus公开对象验证，不能混用附着约束。
    if(bGunTarget) { TargetContext=ReadGunOutlineContext(Source,Outline,TargetError); Control=TEXT("gunshop_rifle_01"); Material=CandidateMaterial.IsValid()?CandidateMaterial->GetPathName():TEXT(""); }
    else TargetContext=Input&&Input->IsOperating()&&Input->GetFocusedControlId()==TEXT("amount.digit.0")&&View&&View->ReadFocusedOutlineForAudit(Control,Material,Outline)&&Control==TEXT("amount.digit.0")&&Material==TemporalMaterial&&View->ReadModelPartPoseForAudit(TemporalPart,Source);
    if(!IsInGameThread()||!Variable||Variable->GetInt()!=Expected||!DelegateVariable||DelegateVariable->GetInt()!=1||!Camera||!Viewport||Viewport->GetSceneHDREnabled()||Viewport->GetSizeXY()!=FIntPoint(1280,720)||!TemporalMask.IsValid()||!TemporalMask->IsRegistered()||!TemporalMID.IsValid()||!HasSameMID||CountPawnSilhouetteMasks()!=1||!TargetContext)
    { FailTemporalSampling(TEXT("actual GT/SDR/size/cameraMID/mask/control/source/CVar context is not fixed: ")+TargetError); return false; }
    FVector Location; FRotator Rotation; Controller->GetPlayerViewPoint(Location,Rotation);
    const FTransform FinalView(Rotation,Location); const float FinalFOV=Controller->PlayerCameraManager?Controller->PlayerCameraManager->GetFOVAngle():0.f;
    if(!bTemporalReferenceReady) { TemporalCameraReference=Camera->GetComponentTransform(); TemporalFinalViewReference=FinalView; TemporalSourceReference=Source; TemporalOutlineReference=Outline; TemporalComponentFOV=Camera->FieldOfView; TemporalFinalFOV=FinalFOV; bTemporalReferenceReady=true; }
    bool PoseStable=Camera->GetComponentTransform().Equals(TemporalCameraReference,.000001)&&FinalView.Equals(TemporalFinalViewReference,.000001)&&Source.Equals(TemporalSourceReference,.000001)&&Outline.Equals(TemporalOutlineReference,.000001); // 普通ABA与候选fixed仍要求实际最终相机/源完全固定。
    FString PoseDiagnostic; // 仅失败写出实际眼位/各字段谓词，避免把生产稳定眼位随yaw的正常圆弧误判为漂移。
    const int32 MotionPhase=bGunTarget&&TemporalPhase>=2?TemporalPhase+2:TemporalPhase; // 枪0/1固定、2/3慢yaw映射为原运动语义4/5，Keycap六组时保持原索引。
    if(bCandidateRequested&&MotionPhase>=2)
    {
        const bool Press=MotionPhase<4; // 按压组仅放行真实Key/MASK一起运动；慢yaw只放行有界计划视向，不自动瞄回中心。
        if(Press)
        {
            PoseStable=Player->GetActorLocation().Equals(CandidatePawnLocation,.000001)&&Camera->GetComponentLocation().Equals(TemporalCameraReference.GetLocation(),.000001)&&FinalView.GetLocation().Equals(TemporalFinalViewReference.GetLocation(),.000001)&&Outline.Equals(Source,.000001)&&Camera->GetComponentTransform().Equals(TemporalCameraReference,.000001)&&FinalView.Equals(TemporalFinalViewReference,.000001);
        }
        else
        {
            const bool PawnFixed=Player->GetActorLocation().Equals(CandidatePawnLocation,.000001),SourceFixed=Source.Equals(TemporalSourceReference,.000001),MaskTogether=Outline.Equals(Source,.000001),ViewReady=ReadCandidateSlowView(PoseDiagnostic); // 眼位直接复用生产同帧SharedViewLocation，不固定最初位置、不扩大容差。
            PoseStable=PawnFixed&&SourceFixed&&MaskTogether&&ViewReady&&FMath::Abs(CandidateYawOffset)<=.150001;
            if(!PoseStable) PoseDiagnostic+=FString::Printf(TEXT(" pawn_fixed=%d source_fixed=%d mask_source_same=%d yaw_in_range=%d"),PawnFixed,SourceFixed,MaskTogether,FMath::Abs(CandidateYawOffset)<=.150001);
        }
    }
    if(!PoseStable||!FMath::IsNearlyEqual(Camera->FieldOfView,TemporalComponentFOV,.0001f)||!FMath::IsNearlyEqual(FinalFOV,TemporalFinalFOV,.0001f))
    { FailTemporalSampling(TEXT("actual camera-component/finalPOV/FOV/source/mask context failed: ")+PoseDiagnostic+FString::Printf(TEXT(" component_fov=%.9f expected_component_fov=%.9f final_fov=%.9f expected_final_fov=%.9f"),Camera->FieldOfView,TemporalComponentFOV,FinalFOV,TemporalFinalFOV)); return false; }
    Facts=MakeShared<FJsonObject>(); Facts->SetNumberField(TEXT("jitter"),Variable->GetInt());
    Facts->SetStringField(TEXT("actual_camera_component_world"),Camera->GetComponentTransform().ToString()); Facts->SetStringField(TEXT("actual_final_view_world"),FinalView.ToString()); Facts->SetNumberField(TEXT("actual_camera_component_fov"),Camera->FieldOfView); Facts->SetNumberField(TEXT("actual_final_view_fov"),FinalFOV); Facts->SetStringField(TEXT("actual_source_world"),Source.ToString()); Facts->SetStringField(TEXT("actual_outline_world"),Outline.ToString()); Facts->SetStringField(TEXT("control_id"),Control.ToString()); Facts->SetStringField(TEXT("same_mask_path"),TemporalMask->GetPathName()); Facts->SetNumberField(TEXT("same_mask_unique_id"),TemporalMask->GetUniqueID()); Facts->SetStringField(TEXT("same_mid_path"),TemporalMID->GetPathName()); Facts->SetNumberField(TEXT("same_mid_unique_id"),TemporalMID->GetUniqueID()); Facts->SetNumberField(TEXT("current_pawn_stencil253_count"),1);
    if(bCandidateRequested) { const float Width=TemporalMID->K2_GetScalarParameterValue(TEXT("OutlineWidthPx")); Facts->SetNumberField(TEXT("actual_outline_width"),Width); Facts->SetNumberField(TEXT("planned_yaw_offset_degrees"),CandidateYawOffset); Facts->SetNumberField(TEXT("actual_final_yaw_offset_degrees"),FMath::FindDeltaAngleDegrees(TemporalFinalViewReference.Rotator().Yaw,FinalView.Rotator().Yaw)); Facts->SetNumberField(TEXT("actual_source_travel_cm"),FVector::Distance(Source.GetLocation(),TemporalSourceReference.GetLocation())); if(!FMath::IsNearlyEqual(Width,TemporalPhase%2==0?0.f:2.f)) { FailTemporalSampling(TEXT("same candidate MID actual width does not match off/on phase")); return false; } }
    return true;
}
bool UDemoSlotsPointInputAudit::ReadCandidateSlowView(FString& Error) const
{
    DEMO_LOG_TICK();
    const UCameraComponent* Camera=Player.IsValid()?Player->GetFirstPersonCameraComponent():nullptr;
    if(!Camera||!Controller.IsValid()) { Error=TEXT("missing actual slowyaw Camera/Controller"); return false; }
    const FVector Eye=Player->GetSharedViewLocation(); FVector FinalLocation; FRotator FinalRotation; Controller->GetPlayerViewPoint(FinalLocation,FinalRotation); // 同帧生产眼位与实际缓存POV，绝不调用CalcCamera或手动更新相机。
    FRotator ExpectedComponent=TemporalCameraReference.Rotator(),ExpectedFinal=TemporalFinalViewReference.Rotator(); ExpectedComponent.Yaw+=CandidateYawOffset; ExpectedFinal.Yaw+=CandidateYawOffset;
    const bool ComponentLocation=Camera->GetComponentLocation().Equals(Eye,.000001),FinalEye=FinalLocation.Equals(Eye,.000001),ComponentRotation=Camera->GetComponentRotation().Equals(ExpectedComponent,.003),FinalYaw=FinalRotation.Equals(ExpectedFinal,.003);
    const TCHAR* FirstBad=!ComponentLocation?TEXT("component_location"):!FinalEye?TEXT("final_pov_location"):!ComponentRotation?TEXT("component_rotation"):!FinalYaw?TEXT("final_pov_rotation"):TEXT("none"); // 严格位置1e-6cm与既有旋转.003deg门分别记录。
    Error=FString::Printf(TEXT("first_bad=%s phase=%d frame=%llu expected_eye=%s actual_camera=%s actual_pov=%s camera_location_error_cm=%.12f pov_location_error_cm=%.12f component_rotation_error_deg=%.9f pov_rotation_error_deg=%.9f control_rotation=%s planned_yaw=%.9f yaw_changed_real=%.9f yaw_wait_real=%.9f component_location_ok=%d final_location_ok=%d component_rotation_ok=%d final_rotation_ok=%d"),FirstBad,TemporalPhase,GFrameCounter,*Eye.ToString(),*Camera->GetComponentLocation().ToString(),*FinalLocation.ToString(),FVector::Distance(Camera->GetComponentLocation(),Eye),FVector::Distance(FinalLocation,Eye),FMath::RadiansToDegrees(Camera->GetComponentQuat().AngularDistance(ExpectedComponent.Quaternion())),FMath::RadiansToDegrees(FinalRotation.Quaternion().AngularDistance(ExpectedFinal.Quaternion())),*Controller->GetControlRotation().ToString(),CandidateYawOffset,CandidateYawChangedAt,FPlatformTime::Seconds()-CandidateYawChangedAt,ComponentLocation,FinalEye,ComponentRotation,FinalYaw);
    return ComponentLocation&&FinalEye&&ComponentRotation&&FinalYaw;
}
void UDemoSlotsPointInputAudit::OnTemporalScreenshotCaptured(int32 Width,int32 Height,const TArray<FColor>& Bitmap)
{
    DEMO_LOG_TICK();
    if(!bTemporalActive||!bTemporalCapturePending) return; // 仅自有在途请求；全局委托没有World参数，不能消费无关联的外部截图。
    const double CallbackDelaySeconds=FPlatformTime::Seconds()-TemporalRequestedAt; // GT绘制阻塞时Tick无法检查超时，回调仍须验证真实延迟不超过3秒。
    const bool Own=IsInGameThread()&&FScreenshotRequest::IsScreenshotRequested()&&FScreenshotRequest::GetFilename()==TemporalOwnedCaptureName&&TemporalPendingPhase==TemporalPhase&&TemporalRequestSerial==TemporalCompletedSerial+1&&GFrameCounter>=TemporalRequestedFrame&&(TemporalLastCallbackFrame==MAX_uint64||GFrameCounter>TemporalLastCallbackFrame)&&CallbackDelaySeconds>=0.&&CallbackDelaySeconds<=3.;
    if(!Own) { ++TemporalRejectedCallbacks; FailTemporalSampling(TEXT("drawn callback filename/phase/request/frame association or three-second deadline failed")); return; }
    bTemporalCapturePending=false; bTemporalCaptureReady=true; // 引擎将在本广播返回后Reset；下一GT Tick才消费Ready/切段/再请求。
    if(Width!=1280||Height!=720||Bitmap.Num()!=1280*720)
    { ++TemporalRejectedCallbacks; FailTemporalSampling(TEXT("drawn LDR callback is empty or not actual1280x720")); return; }
    TSharedPtr<FJsonObject> Context; if(!ReadTemporalContext(Context)) { ++TemporalRejectedCallbacks; return; }
    if(bCandidateRequested)
    {
        bool SameDrawnContext=TemporalRequestContext.IsValid()&&GFrameCounter==TemporalRequestedFrame; // 候选运动样本只接受当前请求同绘制帧，不能以两个独立合法姿态冒充配对。
        if(TemporalRequestContext.IsValid())
        {
            for(const TCHAR* Key:{TEXT("actual_camera_component_world"),TEXT("actual_final_view_world"),TEXT("actual_source_world"),TEXT("actual_outline_world"),TEXT("control_id"),TEXT("same_mask_path"),TEXT("same_mid_path")})
                SameDrawnContext&=TemporalRequestContext->GetStringField(Key)==Context->GetStringField(Key);
            for(const TCHAR* Key:{TEXT("actual_camera_component_fov"),TEXT("actual_final_view_fov"),TEXT("same_mask_unique_id"),TEXT("same_mid_unique_id"),TEXT("jitter"),TEXT("actual_outline_width")})
                SameDrawnContext&=TemporalRequestContext->GetNumberField(Key)==Context->GetNumberField(Key);
        }
        if(!SameDrawnContext) { ++TemporalRejectedCallbacks; FailTemporalSampling(TEXT("candidate request and drawn callback camera/POV/source/mask/MID/width or frame differ")); return; }
    }
    TemporalPixels.SetNumUninitialized(96*96); TArray<uint8> Mask; Mask.SetNumUninitialized(96*96);
    int32 White=0,AgainstFirst=0,AgainstPrevious=0,RawMinimum=255,RawMaximum=0,RawNonzero=0;
    for(int32 Y=0;Y<96;++Y) for(int32 X=0;X<96;++X)
    {
        const int32 Index=Y*96+X; const FColor Pixel=Bitmap[(Height/2-48+Y)*Width+(Width/2-48+X)]; TemporalPixels[Index]=Pixel;
        const int32 Low=FMath::Min3<int32>(Pixel.R,Pixel.G,Pixel.B),High=FMath::Max3<int32>(Pixel.R,Pixel.G,Pixel.B);
        RawMinimum=FMath::Min(RawMinimum,Low); RawMaximum=FMath::Max(RawMaximum,High); RawNonzero+=High>0;
        Mask[Index]=(Low>=250&&High-Low<=2)?1:0; White+=Mask[Index];
        if(TemporalFirstMask.Num()==Mask.Num()) AgainstFirst+=Mask[Index]!=TemporalFirstMask[Index]; if(TemporalPreviousMask.Num()==Mask.Num()) AgainstPrevious+=Mask[Index]!=TemporalPreviousMask[Index];
    }
    TSharedPtr<FJsonObject> Frame=MakeShared<FJsonObject>(); Frame->SetNumberField(TEXT("frame"),static_cast<double>(GFrameCounter)); Frame->SetNumberField(TEXT("request_frame"),static_cast<double>(TemporalRequestedFrame)); Frame->SetNumberField(TEXT("request_serial"),static_cast<double>(TemporalRequestSerial)); Frame->SetNumberField(TEXT("phase_index"),TemporalPhase); Frame->SetNumberField(TEXT("capture_width"),Width); Frame->SetNumberField(TEXT("capture_height"),Height); Frame->SetStringField(TEXT("crop_rect"),TEXT("592,312,688,408 (96x96)")); Frame->SetNumberField(TEXT("raw_roi_min_channel"),RawMinimum); Frame->SetNumberField(TEXT("raw_roi_max_channel"),RawMaximum); Frame->SetNumberField(TEXT("raw_roi_nonzero_pixels"),RawNonzero); Frame->SetStringField(TEXT("raw_roi_crc32"),FString::Printf(TEXT("%08x"),FCrc::MemCrc32(TemporalPixels.GetData(),TemporalPixels.Num()*sizeof(FColor))));
    Frame->SetNumberField(TEXT("request_real_seconds"),TemporalRequestedAt-StartedReal); Frame->SetNumberField(TEXT("callback_real_seconds"),TemporalRequestedAt+CallbackDelaySeconds-StartedReal); Frame->SetNumberField(TEXT("callback_delay_seconds"),CallbackDelaySeconds); Frame->SetNumberField(TEXT("callback_frame_gap"),static_cast<double>(GFrameCounter-TemporalRequestedFrame)); // 同帧绘制可为0帧；真实秒和帧间隔分别记录，不能用固定World步长替代。
    if(RawMaximum==0||RawNonzero==0)
    { ++TemporalRejectedCallbacks; if(TemporalMetadata.IsValid()) TemporalMetadata->SetObjectField(TEXT("last_rejected_drawn_capture"),Frame); FailTemporalSampling(TEXT("drawn callback ROI is all RGB zero; not a stable material observation")); return; }
    ++TemporalValidDrawnCallbacks; TemporalCompletedSerial=TemporalRequestSerial; TemporalLastCallbackFrame=GFrameCounter;
    Frame->SetBoolField(TEXT("valid_drawn_callback"),true); Frame->SetObjectField(TEXT("request_context"),TemporalRequestContext); Frame->SetObjectField(TEXT("callback_context"),Context);
    const bool Warming=TemporalWarmFrames<8||(bCandidateRequested&&FPlatformTime::Seconds()-CandidatePhaseStarted<1.); // 候选同时满足>=1真实秒与>=8有效暖机回调。
    if(Warming) { if(bCandidateRequested&&TemporalWarmFrames>=256) { FailTemporalSampling(TEXT("candidate warmup exceeded256 valid callbacks before one real second")); return; } ++TemporalWarmFrames; TemporalWarmupCaptures.Add(MakeShared<FJsonValueObject>(Frame)); return; } // 只数真实有效绘制回调，绝不以Tick数或空读替代。
    const bool bFirstMeasured=TemporalSamples.IsEmpty(); // 暖机图不进入空间汇总，第一测量帧没有前帧翻转。
    if(bFirstMeasured) { TemporalWhiteCounts.Init(0,96*96); TemporalFlipCounts.Init(0,96*96); }
    if(Mask.Num()!=96*96||TemporalWhiteCounts.Num()!=Mask.Num()||TemporalFlipCounts.Num()!=Mask.Num()||TemporalSamples.Num()>=16||(!bFirstMeasured&&TemporalPreviousMask.Num()!=Mask.Num()))
    { FailTemporalSampling(TEXT("spatial accumulation dimensions or measured-sample bound failed")); return; }
    for(int32 Index=0;Index<Mask.Num();++Index)
    {
        TemporalWhiteCounts[Index]+=Mask[Index];
        if(!bFirstMeasured) TemporalFlipCounts[Index]+=Mask[Index]!=TemporalPreviousMask[Index]; // 必须先比较上一有效测量图，再覆盖Previous；不计暖机→首图。
    }
    if(TemporalSamples.IsEmpty()) TemporalFirstMask=Mask; TemporalPreviousMask=Mask;
    for(const auto& Pair:Context->Values) Frame->SetField(Pair.Key,Pair.Value); // 本GT同步拷纯JSON值，保留旧样本字段并另记请求/回调实际上下文。
    Frame->SetNumberField(TEXT("real_seconds"),FPlatformTime::Seconds()-StartedReal); Frame->SetNumberField(TEXT("sample_index"),TemporalSamples.Num()); Frame->SetNumberField(TEXT("white_pixels"),White); Frame->SetNumberField(TEXT("xor_first_pixels"),AgainstFirst); Frame->SetNumberField(TEXT("xor_previous_pixels"),AgainstPrevious); Frame->SetStringField(TEXT("white_mask_crc32"),FString::Printf(TEXT("%08x"),FCrc::MemCrc32(Mask.GetData(),Mask.Num())));
    if(bCandidateRequested) Frame->SetNumberField(TEXT("phase_real_seconds"),FPlatformTime::Seconds()-CandidatePhaseStarted); // 第一测量帧本身必须在>=1真实秒后，报告可独立核暖机门。
    if(bCandidateRequested&&!RecordCandidateSample(Frame)) return;
    TemporalSamples.Add(MakeShared<FJsonValueObject>(Frame)); // 此回调只存结果，不能Request下一张、切CVar、移除委托或退出。
}
bool UDemoSlotsPointInputAudit::RecordCandidateSample(const TSharedPtr<FJsonObject>& Frame)
{
    DEMO_LOG_TICK();
    FTransform Source; const UDemoCasinoLocalViewComponent* View=Controller->FindComponentByClass<UDemoCasinoLocalViewComponent>();
    const bool Read=bGunTarget?(GunSource.IsValid()&&GunSource->IsRegistered()):(View&&View->ReadModelPartPoseForAudit(TemporalPart,Source)); if(bGunTarget&&Read) Source=GunSource->GetComponentTransform();
    if(!Read||TemporalPixels.Num()!=96*96) { FailTemporalSampling(TEXT("candidate actual RGB/source sample missing")); return false; }
    int32 Red=0,Green=0,Blue=0; // LDR ROI真实RGB均值，阈值白色只作为辅助统计，不决定候选是否可见。
    for(const FColor& Pixel:TemporalPixels) { Red+=Pixel.R; Green+=Pixel.G; Blue+=Pixel.B; }
    Frame->SetNumberField(TEXT("raw_roi_mean_r"),Red/9216.); Frame->SetNumberField(TEXT("raw_roi_mean_g"),Green/9216.); Frame->SetNumberField(TEXT("raw_roi_mean_b"),Blue/9216.); Frame->SetNumberField(TEXT("raw_roi_mean_rgb"),(Red+Green+Blue)/(9216.*3.));
    const double Travel=FVector::Distance(Source.GetLocation(),TemporalSourceReference.GetLocation()); // 实际动画后的源Mesh，不读取内部运动目标或设置位姿。
    CandidateMaximumPressTravel=FMath::Max(CandidateMaximumPressTravel,Travel); Frame->SetNumberField(TEXT("actual_source_travel_cm"),Travel);
    if(TemporalPhase%2==0)
    {
        CandidateOffPixels.Add(TemporalPixels); CandidateOffSources.Add(Source); CandidateOffYaws.Add(CandidateYawOffset); // 最多16帧内存，下一off组覆盖，不导出像素序列。
        CandidateNextSample=FPlatformTime::Seconds()+.04; return true;
    }
    if(CandidateOffPixels.Num()!=16||CandidateOffSources.Num()!=16||CandidateOffYaws.Num()!=16) { FailTemporalSampling(TEXT("candidate on phase has no exact sixteen off RGB/pose records")); return false; }
    int32 PairIndex=TemporalSamples.Num(); double MatchDistance=MAX_dbl,MatchAngle=MAX_dbl; // fixed/slow按同序配对，press按真实源位置最近邻，未匹配帧仍明确保留但不冒充覆盖。
    if(!bGunTarget&&TemporalPhase==3)
    {
        for(int32 Index=0;Index<16;++Index)
        {
            const double Distance=FVector::Distance(Source.GetLocation(),CandidateOffSources[Index].GetLocation());
            if(Distance<MatchDistance) { MatchDistance=Distance; PairIndex=Index; }
        }
    }
    MatchDistance=FVector::Distance(Source.GetLocation(),CandidateOffSources[PairIndex].GetLocation()); MatchAngle=FMath::RadiansToDegrees(Source.GetRotation().AngularDistance(CandidateOffSources[PairIndex].GetRotation()));
    const bool PoseMatched=MatchDistance<=(!bGunTarget&&TemporalPhase==3?.03:.000001)&&MatchAngle<.01&&FMath::Abs(CandidateOffYaws[PairIndex]-CandidateYawOffset)<.000001;
    Frame->SetNumberField(TEXT("paired_off_sample_index"),PairIndex); Frame->SetNumberField(TEXT("paired_source_distance_cm"),MatchDistance); Frame->SetNumberField(TEXT("paired_source_rotation_degrees"),MatchAngle); Frame->SetBoolField(TEXT("paired_pose_matched"),PoseMatched);
    const FBox Bounds=TemporalMask->Bounds.GetBox(); FVector2D Minimum(MAX_dbl,MAX_dbl),Maximum(-MAX_dbl,-MAX_dbl); // 实际已注册源克隆的世界AABB8角投影，仅诊断目标区域，不声称语义Stencil轮廓像素。
    for(int32 Corner=0;Corner<8;++Corner)
    {
        FVector2D Screen; const FVector Point((Corner&1)?Bounds.Max.X:Bounds.Min.X,(Corner&2)?Bounds.Max.Y:Bounds.Min.Y,(Corner&4)?Bounds.Max.Z:Bounds.Min.Z);
        if(!Controller->ProjectWorldLocationToScreen(Point,Screen,false)) { FailTemporalSampling(TEXT("candidate diagnostic source bounds corner failed actual screen projection")); return false; }
        Minimum.X=FMath::Min(Minimum.X,Screen.X); Minimum.Y=FMath::Min(Minimum.Y,Screen.Y); Maximum.X=FMath::Max(Maximum.X,Screen.X); Maximum.Y=FMath::Max(Maximum.Y,Screen.Y);
    }
    const int32 Left=FMath::Clamp(FMath::FloorToInt(Minimum.X)-3,592,688),Top=FMath::Clamp(FMath::FloorToInt(Minimum.Y)-3,312,408),Right=FMath::Clamp(FMath::CeilToInt(Maximum.X)+3,592,688),Bottom=FMath::Clamp(FMath::CeilToInt(Maximum.Y)+3,312,408); // 实际截图坐标，右/下为exclusive。
    const int32 RegionPixels=FMath::Max(0,Right-Left)*FMath::Max(0,Bottom-Top); int32 Effects=0,MaximumContrast=0; int64 ContrastSum=0; // effect为超过16个off帧每通道RGB包络再2byte的像素；对比度用实际配对off图。
    TArray<uint8> EffectMask; if(TemporalPhase==1) EffectMask.Init(0,96*96); // 仅fixed_on临时mask；诊断框外保持0，不保存逐帧像素。
    if(RegionPixels<=0) { if(bGunTarget) { TemporalMetadata->SetStringField(TEXT("candidate_visibility_status"),TEXT("UNVERIFIED_ROI")); Check(false,TEXT("UNVERIFIED_ROI: actual oblique gun source bounds miss partial center96; no whole-gun visibility conclusion")); } else FailTemporalSampling(TEXT("candidate actual projected target diagnostic region misses center ROI")); return false; }
    for(int32 Y=Top;Y<Bottom;++Y) for(int32 X=Left;X<Right;++X)
    {
        const int32 Index=(Y-312)*96+X-592; const FColor& On=TemporalPixels[Index]; const FColor& Off=CandidateOffPixels[PairIndex][Index];
        int32 MinR=255,MinG=255,MinB=255,MaxR=0,MaxG=0,MaxB=0;
        for(const TArray<FColor>& OffFrame:CandidateOffPixels) { const FColor& P=OffFrame[Index]; MinR=FMath::Min<int32>(MinR,P.R); MinG=FMath::Min<int32>(MinG,P.G); MinB=FMath::Min<int32>(MinB,P.B); MaxR=FMath::Max<int32>(MaxR,P.R); MaxG=FMath::Max<int32>(MaxG,P.G); MaxB=FMath::Max<int32>(MaxB,P.B); }
        const bool Beyond=On.R<MinR-2||On.R>MaxR+2||On.G<MinG-2||On.G>MaxG+2||On.B<MinB-2||On.B>MaxB+2;
        if(TemporalPhase==1) EffectMask[Index]=Beyond?1:0;
        const int32 Contrast=FMath::Max3(FMath::Abs(int32(On.R)-int32(Off.R)),FMath::Abs(int32(On.G)-int32(Off.G)),FMath::Abs(int32(On.B)-int32(Off.B)));
        Effects+=Beyond; MaximumContrast=FMath::Max(MaximumContrast,Contrast); ContrastSum+=Contrast;
    }
    if(TemporalPhase==1)
    {
        const bool First=TemporalSamples.IsEmpty(); // 第一有效matched fixed_on图没有前图翻转，暖机图完全排除。
        if(First) { CandidateEffectCounts.Init(0,96*96); CandidateEffectFlipCounts.Init(0,96*96); CandidatePreviousEffectMask.Reset(); CandidatePreviousEffectPixels.Reset(); }
        if(!PoseMatched||EffectMask.Num()!=96*96||CandidateEffectCounts.Num()!=96*96||CandidateEffectFlipCounts.Num()!=96*96||(!First&&(CandidatePreviousEffectMask.Num()!=96*96||CandidatePreviousEffectPixels.Num()!=96*96))) { FailTemporalSampling(TEXT("fixed_on RGB effect temporal requires matched pose and9216-entry count/mask/previousRGB arrays")); return false; }
        int32 EffectCount=0,EffectXor=0; // 与既有Effects交叉核验，先读前有效mask，再覆写。
        int32 UnionPixels=0,MaximumDelta=0,DeltaAbove2=0; int64 DeltaSum=0; // 前/当前effect并集内8-bit RGB最大通道差；presence稳定不等于强度稳定。
        for(int32 Index=0;Index<96*96;++Index)
        {
            EffectCount+=EffectMask[Index]; CandidateEffectCounts[Index]+=EffectMask[Index];
            if(!First)
            {
                const bool Flip=EffectMask[Index]!=CandidatePreviousEffectMask[Index]; CandidateEffectFlipCounts[Index]+=Flip; EffectXor+=Flip;
                if(EffectMask[Index]||CandidatePreviousEffectMask[Index]) { const FColor& Current=TemporalPixels[Index]; const FColor& Previous=CandidatePreviousEffectPixels[Index]; const int32 Delta=FMath::Max3(FMath::Abs(int32(Current.R)-int32(Previous.R)),FMath::Abs(int32(Current.G)-int32(Previous.G)),FMath::Abs(int32(Current.B)-int32(Previous.B))); ++UnionPixels; MaximumDelta=FMath::Max(MaximumDelta,Delta); DeltaSum+=Delta; DeltaAbove2+=Delta>2; }
            }
        }
        if(EffectCount!=Effects) { FailTemporalSampling(TEXT("fixed_on RGB effect mask count differs from measured diagnostic Effects")); return false; }
        CandidatePreviousEffectMask=MoveTemp(EffectMask); CandidatePreviousEffectPixels=TemporalPixels; Frame->SetNumberField(TEXT("effect_pixel_count"),EffectCount); Frame->SetNumberField(TEXT("effect_xor_previous"),EffectXor);
        Frame->SetBoolField(TEXT("effect_intensity_previous_comparison"),!First); Frame->SetNumberField(TEXT("effect_intensity_union_pixels"),UnionPixels); Frame->SetNumberField(TEXT("effect_intensity_max_delta_bytes"),MaximumDelta); Frame->SetNumberField(TEXT("effect_intensity_mean_delta_bytes"),UnionPixels>0.?double(DeltaSum)/UnionPixels:0.); Frame->SetNumberField(TEXT("effect_intensity_gt2_pixels"),DeltaAbove2);
    }
    Frame->SetStringField(TEXT("effect_region_method"),TEXT("actual registered mask/source Mesh world AABB eight projected corners +3px, intersect96x96 ROI; diagnostic nonsemantic"));
    Frame->SetNumberField(TEXT("effect_region_left"),Left); Frame->SetNumberField(TEXT("effect_region_top"),Top); Frame->SetNumberField(TEXT("effect_region_right_exclusive"),Right); Frame->SetNumberField(TEXT("effect_region_bottom_exclusive"),Bottom);
    Frame->SetNumberField(TEXT("effect_region_pixels"),RegionPixels); Frame->SetNumberField(TEXT("effect_pixels_beyond_off_rgb_envelope_plus2"),Effects); Frame->SetNumberField(TEXT("paired_max_contrast_bytes"),MaximumContrast); Frame->SetNumberField(TEXT("paired_mean_contrast_bytes"),double(ContrastSum)/RegionPixels); Frame->SetNumberField(TEXT("diagnostic_effect_fraction"),double(Effects)/RegionPixels);
    CandidateNextSample=FPlatformTime::Seconds()+.04; return true;
}
bool UDemoSlotsPointInputAudit::AddCandidateEffectTemporalSummary(const TSharedPtr<FJsonObject>& Phase)
{
    DEMO_LOG_CALL();
    if(TemporalPhase!=1) return true; // 仅fixed_on；其它组及旧ABA没有额外计数/门槛。
    if(!Phase.IsValid()||TemporalSamples.Num()!=16||CandidateEffectCounts.Num()!=9216||CandidateEffectFlipCounts.Num()!=9216||CandidatePreviousEffectMask.Num()!=9216) { if(Phase.IsValid()) Phase->SetBoolField(TEXT("sampling_valid"),false); FailTemporalSampling(TEXT("fixed_on effect temporal dimensions/sample count failed")); return false; }
    TSharedPtr<FJsonObject> Summary=MakeShared<FJsonObject>(); Phase->SetObjectField(TEXT("diagnostic_effect_temporal"),Summary); // 实际RGB正对照诊断时序，不等于语义Stencil或候选整体稳定性验收。
    Summary->SetNumberField(TEXT("capture_width"),1280); Summary->SetNumberField(TEXT("capture_height"),720); Summary->SetNumberField(TEXT("origin_x"),592); Summary->SetNumberField(TEXT("origin_y"),312); Summary->SetNumberField(TEXT("width"),96); Summary->SetNumberField(TEXT("height"),96); Summary->SetNumberField(TEXT("max_x_exclusive"),688); Summary->SetNumberField(TEXT("max_y_exclusive"),408); Summary->SetNumberField(TEXT("measured_samples"),16);
    Summary->SetStringField(TEXT("coordinate_convention"),TEXT("top-left viewport; X right/Y down; row-major index=local_y*96+local_x; screen=(592+local_x,312+local_y)"));
    Summary->SetStringField(TEXT("scope"),TEXT("fixed_on matched poses only; RGB beyond all16off temporal envelopes+2bytes inside actual8corner projected source AABB+3px; zero outside diagnostic box; no semanticStencil or whole-candidate stability claim; aggregate cannot reconstruct order"));
    TArray<TSharedPtr<FJsonValue>> Counts,Flips,Variables; Counts.Reserve(9216); Flips.Reserve(9216); // 两个完整有界空间数组与仅可变像素坐标。
    int32 Sum=0,FlipSum=0,Never=0,Always=0,Variable=0,Transition=0,FrameEffects=0,FrameXor=0,IntensityUnion=0,IntensityAbove2=0,IntensityMaximum=0; double IntensityDeltaSum=0.; bool Valid=true; // 整数总和与原16条有效帧统计独立交叉核验；RGB强度差仅15对标量。
    for(int32 Index=0;Index<9216;++Index)
    {
        const int32 Count=CandidateEffectCounts[Index],Flip=CandidateEffectFlipCounts[Index],X=Index%96,Y=Index/96; const bool Vary=Count>0&&Count<16;
        Valid&=Count>=0&&Count<=16&&Flip>=0&&Flip<=15&&Vary==(Flip>0)&&X>=0&&X<96&&Y>=0&&Y<96&&Y*96+X==Index&&592+X<688&&312+Y<408;
        Sum+=Count; FlipSum+=Flip; Never+=Count==0; Always+=Count==16; Variable+=Vary; Transition+=Flip>0;
        Counts.Add(MakeShared<FJsonValueNumber>(Count)); Flips.Add(MakeShared<FJsonValueNumber>(Flip));
        if(Vary) { TSharedPtr<FJsonObject> Pixel=MakeShared<FJsonObject>(); Pixel->SetNumberField(TEXT("index"),Index); Pixel->SetNumberField(TEXT("local_x"),X); Pixel->SetNumberField(TEXT("local_y"),Y); Pixel->SetNumberField(TEXT("screen_x"),592+X); Pixel->SetNumberField(TEXT("screen_y"),312+Y); Pixel->SetNumberField(TEXT("count"),Count); Pixel->SetNumberField(TEXT("flips"),Flip); Variables.Add(MakeShared<FJsonValueObject>(Pixel)); }
    }
    for(int32 Index=0;Index<16;++Index)
    {
        const TSharedPtr<FJsonObject> Sample=TemporalSamples[Index]->AsObject(); const int32 Effect=static_cast<int32>(Sample->GetNumberField(TEXT("effect_pixel_count"))),Xor=static_cast<int32>(Sample->GetNumberField(TEXT("effect_xor_previous")));
        Valid&=Sample->GetBoolField(TEXT("paired_pose_matched"))&&Effect>=0&&Effect<=9216&&Xor>=0&&Xor<=9216&&(Index!=0||Xor==0)&&Sample->GetNumberField(TEXT("effect_pixels_beyond_off_rgb_envelope_plus2"))==Effect;
        const int32 Union=static_cast<int32>(Sample->GetNumberField(TEXT("effect_intensity_union_pixels"))),Above2=static_cast<int32>(Sample->GetNumberField(TEXT("effect_intensity_gt2_pixels"))),Maximum=static_cast<int32>(Sample->GetNumberField(TEXT("effect_intensity_max_delta_bytes"))); // 原始8-bit on图相邻差，只覆盖诊断effect并集。
        Valid&=Sample->GetBoolField(TEXT("effect_intensity_previous_comparison"))==(Index>0)&&Union>=0&&Union<=9216&&Above2>=0&&Above2<=Union&&Maximum>=0&&Maximum<=255&&(Index!=0||(Union==0&&Above2==0&&Maximum==0));
        IntensityUnion+=Union; IntensityAbove2+=Above2; IntensityMaximum=FMath::Max(IntensityMaximum,Maximum); IntensityDeltaSum+=Sample->GetNumberField(TEXT("effect_intensity_mean_delta_bytes"))*Union;
        FrameEffects+=Effect; FrameXor+=Xor;
    }
    Valid&=Counts.Num()==9216&&Flips.Num()==9216&&Variables.Num()==Variable&&Never+Always+Variable==9216&&Variable==Transition&&Sum==FrameEffects&&FlipSum==FrameXor;
    Summary->SetArrayField(TEXT("effect_count"),Counts); Summary->SetArrayField(TEXT("flip_count"),Flips); Summary->SetArrayField(TEXT("variable_pixels"),Variables); Summary->SetNumberField(TEXT("never_effect_pixels"),Never); Summary->SetNumberField(TEXT("always_effect_pixels"),Always); Summary->SetNumberField(TEXT("variable_pixels_count"),Variable); Summary->SetNumberField(TEXT("transition_pixels_count"),Transition); Summary->SetNumberField(TEXT("effect_sum"),Sum); Summary->SetNumberField(TEXT("flip_sum"),FlipSum); Summary->SetNumberField(TEXT("expected_frame_effect_sum"),FrameEffects); Summary->SetNumberField(TEXT("expected_frame_xor_total"),FrameXor); Summary->SetBoolField(TEXT("invariants_valid"),Valid); Phase->SetNumberField(TEXT("effect_xor_previous_total"),FrameXor);
    Summary->SetNumberField(TEXT("intensity_adjacent_frame_pairs"),15); Summary->SetNumberField(TEXT("intensity_union_pixel_sum"),IntensityUnion); Summary->SetNumberField(TEXT("intensity_gt2_pixel_sum"),IntensityAbove2); Summary->SetNumberField(TEXT("intensity_max_delta_bytes"),IntensityMaximum); Summary->SetNumberField(TEXT("intensity_mean_delta_bytes"),IntensityUnion>0?IntensityDeltaSum/IntensityUnion:0.); Summary->SetStringField(TEXT("intensity_scope"),TEXT("Actual adjacent on LDR RGB max-channel delta over current|previous diagnostic effect masks, first comparison=false. Presence flips and RGB deltas reported separately; no semantic/source-isolated or whole-candidate stability conclusion."));
    if(!Valid) { Phase->SetBoolField(TEXT("sampling_valid"),false); FailTemporalSampling(TEXT("fixed_on effect temporal ranges/coordinates/classification/frame sums failed")); return false; }
    return true;
}
bool UDemoSlotsPointInputAudit::AddTemporalSpatialSummary(const TSharedPtr<FJsonObject>& Phase)
{
    DEMO_LOG_CALL();
    // 固定中心ROI来自已验证的1280x720正常LDR回调；仅汇总阈值白色，不能当作Stencil语义分割或重建时间顺序。
    constexpr int32 Width=96,Height=96,OriginX=592,OriginY=312,SampleCount=16,PixelCount=Width*Height;
    TSharedPtr<FJsonObject> Spatial=MakeShared<FJsonObject>(); // 有界逐段空间汇总，随原report一次写入，不生成额外图或逐帧像素序列。
    if(!Phase.IsValid()||TemporalSamples.Num()!=SampleCount||TemporalWhiteCounts.Num()!=PixelCount||TemporalFlipCounts.Num()!=PixelCount||TemporalFirstMask.Num()!=PixelCount||TemporalPreviousMask.Num()!=PixelCount)
    {
        if(Phase.IsValid()) Phase->SetBoolField(TEXT("sampling_valid"),false);
        FailTemporalSampling(TEXT("spatial summary requires sixteen measured samples and four9216-entry masks/count arrays")); return false;
    }
    Phase->SetObjectField(TEXT("spatial_summary"),Spatial);
    Spatial->SetNumberField(TEXT("capture_width"),1280); Spatial->SetNumberField(TEXT("capture_height"),720);
    Spatial->SetNumberField(TEXT("origin_x"),OriginX); Spatial->SetNumberField(TEXT("origin_y"),OriginY);
    Spatial->SetNumberField(TEXT("width"),Width); Spatial->SetNumberField(TEXT("height"),Height);
    Spatial->SetNumberField(TEXT("max_x_exclusive"),OriginX+Width); Spatial->SetNumberField(TEXT("max_y_exclusive"),OriginY+Height);
    Spatial->SetNumberField(TEXT("measured_samples"),SampleCount); Spatial->SetNumberField(TEXT("pixel_count"),PixelCount);
    Spatial->SetStringField(TEXT("coordinate_convention"),TEXT("top-left viewport origin; X right, Y down; row-major index=local_y*96+local_x; screen=(592+local_x,312+local_y)"));
    Spatial->SetStringField(TEXT("white_threshold"),TEXT("min(R,G,B)>=250 && max(R,G,B)-min(R,G,B)<=2; LDR screenshot bytes"));
    Spatial->SetStringField(TEXT("scope"),TEXT("Only sixteen valid measured callbacks; eight warm callbacks excluded; threshold whites may include HUD/key, not semantic stencil; aggregate counts cannot reconstruct frame order."));
    TArray<TSharedPtr<FJsonValue>> Whites,Flips,VariablePixels; // 两个完整row-major数组与仅可变像素的稀疏坐标，没有任何逐帧mask。
    Whites.Reserve(PixelCount); Flips.Reserve(PixelCount);
    int32 Never=0,Always=0,Variable=0,Transitions=0,WhiteSum=0,FlipSum=0; // 逐像素分类数/总和，最大147456，整数精确可与帧统计交叉核验。
    bool Valid=true; // 维度、范围、坐标与计数恒等式全部成立才可保留sampling_valid。
    for(int32 Index=0;Index<PixelCount;++Index)
    {
        const int32 Count=TemporalWhiteCounts[Index],Flip=TemporalFlipCounts[Index]; // 当前像素在16帧中为白的次数及15个相邻区间翻转次数。
        const int32 X=Index%Width,Y=Index/Width; // ROI局部坐标，屏幕右/下递增，末像素为(687,407)。
        const bool IsVariable=Count>0&&Count<SampleCount;
        Valid&=Count>=0&&Count<=SampleCount&&Flip>=0&&Flip<SampleCount&&X>=0&&X<Width&&Y>=0&&Y<Height&&Y*Width+X==Index&&OriginX+X<688&&OriginY+Y<408&&IsVariable==(Flip>0);
        Never+=Count==0; Always+=Count==SampleCount; Variable+=IsVariable; Transitions+=Flip>0; WhiteSum+=Count; FlipSum+=Flip;
        Whites.Add(MakeShared<FJsonValueNumber>(Count)); Flips.Add(MakeShared<FJsonValueNumber>(Flip));
        if(IsVariable)
        {
            TSharedPtr<FJsonObject> Pixel=MakeShared<FJsonObject>(); // 只为0<count<16的像素提供实际完整截图坐标，供定位残余区域。
            Pixel->SetNumberField(TEXT("index"),Index); Pixel->SetNumberField(TEXT("local_x"),X); Pixel->SetNumberField(TEXT("local_y"),Y);
            Pixel->SetNumberField(TEXT("screen_x"),OriginX+X); Pixel->SetNumberField(TEXT("screen_y"),OriginY+Y);
            Pixel->SetNumberField(TEXT("count"),Count); Pixel->SetNumberField(TEXT("flips"),Flip);
            VariablePixels.Add(MakeShared<FJsonValueObject>(Pixel));
        }
    }
    int32 SampleWhiteSum=0,SampleFlipSum=0; // 独立从既有16条帧统计求和，不复用上面的计数来证明自己。
    for(int32 Index=0;Index<TemporalSamples.Num();++Index)
    {
        const TSharedPtr<FJsonObject> Sample=TemporalSamples[Index]->AsObject(); // 原回调已写入的真实绘制帧统计，此处仅交叉核验。
        const int32 White=static_cast<int32>(Sample->GetNumberField(TEXT("white_pixels"))),Flip=static_cast<int32>(Sample->GetNumberField(TEXT("xor_previous_pixels")));
        Valid&=Sample->GetNumberField(TEXT("capture_width"))==1280&&Sample->GetNumberField(TEXT("capture_height"))==720&&Sample->GetNumberField(TEXT("sample_index"))==Index&&White>=0&&White<=PixelCount&&Flip>=0&&Flip<=PixelCount&&(Index!=0||Flip==0);
        SampleWhiteSum+=White; SampleFlipSum+=Flip;
    }
    Valid&=Whites.Num()==PixelCount&&Flips.Num()==PixelCount&&VariablePixels.Num()==Variable&&Never+Always+Variable==PixelCount&&Variable==Transitions&&WhiteSum==SampleWhiteSum&&FlipSum==SampleFlipSum&&Phase->GetNumberField(TEXT("xor_previous_total"))==FlipSum;
    Spatial->SetArrayField(TEXT("white_count"),Whites); Spatial->SetArrayField(TEXT("flip_count"),Flips); Spatial->SetArrayField(TEXT("variable_pixels"),VariablePixels);
    Spatial->SetNumberField(TEXT("never_white_pixels"),Never); Spatial->SetNumberField(TEXT("always_white_pixels"),Always); Spatial->SetNumberField(TEXT("variable_pixels_count"),Variable); Spatial->SetNumberField(TEXT("transition_pixels_count"),Transitions);
    Spatial->SetNumberField(TEXT("white_sum"),WhiteSum); Spatial->SetNumberField(TEXT("flip_sum"),FlipSum); Spatial->SetNumberField(TEXT("sample_white_sum"),SampleWhiteSum); Spatial->SetNumberField(TEXT("sample_xor_previous_sum"),SampleFlipSum);
    Spatial->SetBoolField(TEXT("invariants_valid"),Valid);
    if(!Valid) { Phase->SetBoolField(TEXT("sampling_valid"),false); FailTemporalSampling(TEXT("spatial summary range/coordinate/classification or white/xor sum invariant failed")); return false; }
    return true;
}
void UDemoSlotsPointInputAudit::AdvanceCandidateProbe()
{
    DEMO_LOG_TICK(); if(!bTemporalActive||GFrameCounter==TemporalLastFrame) return; TemporalLastFrame=GFrameCounter;
    const double Now=FPlatformTime::Seconds(); // 所有暖机/按压/慢yaw等待使用真实秒，仍受整个夹具90秒总门禁。
    const int32 MotionPhase=bGunTarget&&TemporalPhase>=2?TemporalPhase+2:TemporalPhase; // 枪四组没有按压组，后两组复用实际慢yaw门。
    if(bCandidateGesture&&Now-CandidateGestureStarted>=.25) bInjectDown=false; // 原IA_Fire注入的唯一手势正常释放，不能直接调用金额/按压业务接口。
    if(bTemporalCapturePending) { if(Now-TemporalRequestedAt>3.) FailTemporalSampling(TEXT("candidate own drawn screenshot exceeded3 real seconds")); return; }
    if(bTemporalCaptureReady)
    {
        if(FScreenshotRequest::IsScreenshotRequested()) return;
        bTemporalCaptureReady=false;
        if(TemporalSamples.Num()==16)
        {
            static const TCHAR* Labels[]={TEXT("fixed_off"),TEXT("fixed_on"),TEXT("press_off"),TEXT("press_on"),TEXT("slowyaw_off"),TEXT("slowyaw_on")}; // 固定六组，不接受命令行扩展任意阶段。
            static const TCHAR* GunLabels[]={TEXT("gunfixed_off"),TEXT("gunfixed_on"),TEXT("gunslowyaw_off"),TEXT("gunslowyaw_on")}; // 唯一斜步枪补充，中心ROI只覆盖接收机/局部边沿。
            const TCHAR* Label=bGunTarget?GunLabels[TemporalPhase]:Labels[TemporalPhase];
            TSharedPtr<FJsonObject> Phase=MakeShared<FJsonObject>(); Phase->SetStringField(TEXT("label"),Label); Phase->SetNumberField(TEXT("actual_jitter"),1); Phase->SetNumberField(TEXT("actual_outline_width"),TemporalPhase%2==0?0:2);
            Phase->SetNumberField(TEXT("discarded_valid_drawn_callbacks"),TemporalWarmupCaptures.Num()); Phase->SetNumberField(TEXT("valid_drawn_callbacks"),TemporalWarmupCaptures.Num()+16); Phase->SetNumberField(TEXT("actual_viewport_observations"),16); Phase->SetNumberField(TEXT("phase_real_seconds"),Now-CandidatePhaseStarted);
            int32 WhiteMaximum=0,Matched=0,MovingMatched=0,EffectFrames=0; double WhiteSum=0.,XorSum=0.,Effects=0.,MaximumContrast=0.,RegionSum=0.; // 源于现有实际帧，不以white阈值代替RGB正对照可见性。
            for(const TSharedPtr<FJsonValue>& Value:TemporalSamples)
            {
                const TSharedPtr<FJsonObject> Sample=Value->AsObject(); WhiteMaximum=FMath::Max(WhiteMaximum,static_cast<int32>(Sample->GetNumberField(TEXT("white_pixels")))); WhiteSum+=Sample->GetNumberField(TEXT("white_pixels")); XorSum+=Sample->GetNumberField(TEXT("xor_previous_pixels"));
                if(TemporalPhase%2==1)
                {
                    const bool Pair=Sample->GetBoolField(TEXT("paired_pose_matched")); Matched+=Pair;
                    const double Effect=Sample->GetNumberField(TEXT("effect_pixels_beyond_off_rgb_envelope_plus2"));
                    if(Pair) { Effects+=Effect; RegionSum+=Sample->GetNumberField(TEXT("effect_region_pixels")); EffectFrames+=Effect>0.; MovingMatched+=Sample->GetNumberField(TEXT("actual_source_travel_cm"))>.01&&Effect>0.; MaximumContrast=FMath::Max(MaximumContrast,Sample->GetNumberField(TEXT("paired_max_contrast_bytes"))); }
                }
            }
            Phase->SetNumberField(TEXT("white_max_optional"),WhiteMaximum); Phase->SetNumberField(TEXT("white_mean_optional"),WhiteSum/16.); Phase->SetNumberField(TEXT("xor_previous_total"),XorSum); Phase->SetBoolField(TEXT("sampling_valid"),true); Phase->SetArrayField(TEXT("warmup_captures"),TemporalWarmupCaptures); Phase->SetArrayField(TEXT("samples"),TemporalSamples);
            Phase->SetNumberField(TEXT("actual_maximum_press_travel_cm"),CandidateMaximumPressTravel); Phase->SetNumberField(TEXT("pose_matched_on_samples"),Matched); Phase->SetNumberField(TEXT("pose_matched_sample_fraction"),Matched/16.); Phase->SetNumberField(TEXT("moving_pose_matched_effect_samples"),MovingMatched); Phase->SetNumberField(TEXT("matched_effect_frames"),EffectFrames); Phase->SetNumberField(TEXT("matched_effect_pixel_sum"),Effects); Phase->SetNumberField(TEXT("matched_max_contrast_bytes"),MaximumContrast); Phase->SetNumberField(TEXT("paired_diagnostic_effect_fraction"),RegionSum>0.?Effects/RegionSum:0.);
            const bool SpatialValid=AddTemporalSpatialSummary(Phase); const bool EffectValid=AddCandidateEffectTemporalSummary(Phase); TemporalPhases.Add(MakeShared<FJsonValueObject>(Phase)); if(!SpatialValid||!EffectValid) return;
            if(TemporalPhase==1) { CandidateEffectCounts.Reset(); CandidateEffectFlipCounts.Reset(); CandidatePreviousEffectMask.Reset(); CandidatePreviousEffectPixels.Reset(); } // JSON已持有聚合数值，立即清本段临时计数/RGB。
            const bool Press=MotionPhase==2||MotionPhase==3; const bool Returned=!Press||TemporalSamples.Last()->AsObject()->GetNumberField(TEXT("actual_source_travel_cm"))<=.01;
            const double FirstYaw=TemporalSamples[0]->AsObject()->GetNumberField(TEXT("actual_final_yaw_offset_degrees")),LastYaw=TemporalSamples.Last()->AsObject()->GetNumberField(TEXT("actual_final_yaw_offset_degrees")); // 实测最终POV偏移，不以计划数值冒充慢转头已执行。
            const bool YawExercised=MotionPhase<4||(FMath::Abs(FirstYaw+.15)<.003&&FMath::Abs(LastYaw-.15)<.003&&LastYaw-FirstYaw>.29);
            const bool Exercised=(!Press||(bCandidateGesture&&CandidateMaximumPressTravel>.01&&Returned))&&YawExercised;
            const bool Visible=TemporalPhase%2==0||(Matched>0&&Effects>0.&&MaximumContrast>2.&&(MotionPhase!=3||MovingMatched>0)&&(MotionPhase!=5||Matched==16));
            Phase->SetBoolField(TEXT("actual_motion_exercised_and_returned"),Exercised); Phase->SetStringField(TEXT("candidate_visibility_status"),Exercised&&Visible?TEXT("PASS_LISTED_DIAGNOSTIC_RGB_SCOPE"):TEXT("UNVERIFIED_CANDIDATE_VISIBILITY"));
            Phase->SetNumberField(TEXT("actual_first_final_yaw_offset_degrees"),FirstYaw); Phase->SetNumberField(TEXT("actual_last_final_yaw_offset_degrees"),LastYaw); Phase->SetBoolField(TEXT("actual_slow_yaw_exercised"),YawExercised); Phase->SetNumberField(TEXT("first_measured_phase_real_seconds"),TemporalSamples[0]->AsObject()->GetNumberField(TEXT("phase_real_seconds")));
            if(TemporalWarmupCaptures.Num()<8||TemporalWarmupCaptures.Num()>256||Now-CandidatePhaseStarted<1.) { Phase->SetBoolField(TEXT("sampling_valid"),false); FailTemporalSampling(TEXT("candidate warmup callback/real-duration bounds failed")); return; }
            if(!Exercised||!Visible) { if(bGunTarget) { Phase->SetStringField(TEXT("candidate_visibility_status"),TEXT("UNVERIFIED_ROI")); TemporalMetadata->SetStringField(TEXT("candidate_visibility_status"),TEXT("UNVERIFIED_ROI")); } Check(false,(bGunTarget?TEXT("UNVERIFIED_ROI: no proven effect in partial gun receiver/edge crop; not proof whole gun invisible: "):TEXT("CANDIDATE_VISIBILITY_UNVERIFIED: actual movement/return or pose-paired target RGB effect missing in "))+FString(Label)); return; }
            bInjectDown=false; bCandidateGesture=false;
            ++TemporalPhase; TemporalWarmFrames=0; TemporalSamples.Reset(); TemporalWarmupCaptures.Reset(); TemporalFirstMask.Reset(); TemporalPreviousMask.Reset(); TemporalWhiteCounts.Reset(); TemporalFlipCounts.Reset();
            if(TemporalPhase==1) { CandidateEffectCounts.Reset(); CandidateEffectFlipCounts.Reset(); CandidatePreviousEffectMask.Reset(); CandidatePreviousEffectPixels.Reset(); } // fixed_on入段先清，暖机不计。
            if(TemporalPhase==(bGunTarget?4:6))
            {
                ReleaseTemporalCapture(); TemporalMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),CandidateOriginalWidth); bCandidateWidthSaved=false;
                Controller->SetControlRotation(CandidateOriginalRotation); bCandidateCameraSaved=false; CandidateYawOffset=0.;
                const UDemoCasinoDeviceInputComponent* Input=Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>();
                if(bGunTarget) bTemporalComplete=Check(!FireAction.IsValid()&&!bInjectDown&&TemporalPhases.Num()==4&&TemporalMID->K2_GetScalarParameterValue(TEXT("OutlineWidthPx"))==CandidateOriginalWidth,TEXT("four partial oblique gun groups complete, width/camera restored; no IA_Fire/E/purchase; candidate+jitter1 retained until two PNGs and full exit"));
                else bTemporalComplete=Check(Input&&Input->GetLastSubmittedRequestId()==CandidateRequestBefore&&Controller->GetMenuFlow()->GetCasinoDeviceAmountText()==TEXT("0")&&TemporalMID->K2_GetScalarParameterValue(TEXT("OutlineWidthPx"))==CandidateOriginalWidth,TEXT("six candidate groups complete, real digit0 gestures keep amount0/no economic request, width/camera restored; candidate+jitter1 retained until full fixture exit"));
                TemporalMetadata->SetStringField(TEXT("candidate_visibility_status"),bTemporalComplete?TEXT("PASS_LISTED_DIAGNOSTIC_RGB_SCOPE"):TEXT("UNVERIFIED")); CandidateOffPixels.Reset(); CandidateOffSources.Reset(); CandidateOffYaws.Reset(); NextReal=Now+.08;
                if(bGunTarget) { Step=203; NextReal=Now+1.; } // 正常Camera恢复后暖机一秒再取两张普通on/off PNG，绝不回到Keycap控制流。
                return;
            }
            CandidatePhaseStarted=Now; CandidateNextSample=Now; CandidateMaximumPressTravel=0.;
            TemporalMID->SetScalarParameterValue(TEXT("OutlineWidthPx"),TemporalPhase%2==0?0.f:2.f);
            if(TemporalPhase%2==0) { CandidateOffPixels.Reset(); CandidateOffSources.Reset(); CandidateOffYaws.Reset(); }
            CandidateYawOffset=(bGunTarget?TemporalPhase>=2:TemporalPhase>=4)?-.15:0.; FRotator Rotation=CandidateOriginalRotation; Rotation.Yaw+=CandidateYawOffset; Controller->SetControlRotation(Rotation); CandidateYawChangedAt=Now;
            return; // 引擎上一截图Reset后切阶段，下一Tick才允许新请求。
        }
    }
    const bool Warming=TemporalWarmFrames<8||Now-CandidatePhaseStarted<1.;
    if(!Warming&&MotionPhase>=4)
    {
        const double Yaw=-.15+.3*TemporalSamples.Num()/15.; // 同16个小yaw偏移逐样本Off/On复现，不在移动中自动AimControl回中心。
        if(!FMath::IsNearlyEqual(Yaw,CandidateYawOffset,.0000001)) { CandidateYawOffset=Yaw; FRotator Rotation=CandidateOriginalRotation; Rotation.Yaw+=Yaw; Controller->SetControlRotation(Rotation); CandidateYawChangedAt=Now; return; }
    }
    if(Now<CandidateNextSample||(MotionPhase>=4&&Now-CandidateYawChangedAt<.04)) return;
    if(MotionPhase>=4) { FString ViewDiagnostic; if(!ReadCandidateSlowView(ViewDiagnostic)) { if(Now-CandidateYawChangedAt<=2.) return; FailTemporalSampling(TEXT("actual slowyaw camera/POV did not settle within2 real seconds: ")+ViewDiagnostic); return; } } // 等正常生产Camera更新，不手动求值或锁眼位。
    if(!Warming&&(MotionPhase==2||MotionPhase==3)&&!bCandidateGesture) { bCandidateGesture=true; CandidateGestureStarted=Now; bInjectDown=true; }
    TSharedPtr<FJsonObject> Context; if(!ReadTemporalContext(Context)) return;
    if(FScreenshotRequest::IsScreenshotRequested()) { FailTemporalSampling(TEXT("foreign screenshot appeared during candidate groups")); return; }
    TemporalRequestContext=Context; TemporalPendingPhase=TemporalPhase; TemporalRequestedFrame=GFrameCounter; TemporalRequestedAt=Now; ++TemporalRequestSerial;
    const FString Name=Directory/FString::Printf(TEXT("TemporalProbe_Memory_%03llu.png"),TemporalRequestSerial); // 正常LDR委托消费，保留自有Filename关联，无新PNG。
    FScreenshotRequest::RequestScreenshot(Name,false,false); TemporalOwnedCaptureName=FScreenshotRequest::GetFilename(); bTemporalCapturePending=true;
}
void UDemoSlotsPointInputAudit::AdvanceTemporalProbe()
{
    DEMO_LOG_TICK();
    if(bCandidateRequested) { AdvanceCandidateProbe(); return; }
    if(!bTemporalActive||GFrameCounter==TemporalLastFrame) return; TemporalLastFrame=GFrameCounter;
    if(bTemporalCapturePending) { if(FPlatformTime::Seconds()-TemporalRequestedAt>3.) FailTemporalSampling(TEXT("own drawn-frame screenshot callback exceeded3 real seconds")); return; }
    if(bTemporalCaptureReady)
    {
        if(FScreenshotRequest::IsScreenshotRequested()) return; // ProcessScreenShots广播之后的Reset尚未完成时绝不覆盖请求。
        bTemporalCaptureReady=false;
        if(TemporalSamples.Num()==16)
        {
            int32 Minimum=MAX_int32,Maximum=0; double Sum=0.,XorPrevious=0.,XorFirst=0.;
            for(const TSharedPtr<FJsonValue>& Value:TemporalSamples) { const TSharedPtr<FJsonObject> Object=Value->AsObject(); const int32 Count=static_cast<int32>(Object->GetNumberField(TEXT("white_pixels"))); Minimum=FMath::Min(Minimum,Count); Maximum=FMath::Max(Maximum,Count); Sum+=Count; XorPrevious+=Object->GetNumberField(TEXT("xor_previous_pixels")); XorFirst+=Object->GetNumberField(TEXT("xor_first_pixels")); }
            TSharedPtr<FJsonObject> Phase=MakeShared<FJsonObject>(); Phase->SetStringField(TEXT("label"),TemporalPhase==0?TEXT("A1_jitter1"):TemporalPhase==1?TEXT("B_jitter0"):TEXT("A2_jitter1")); Phase->SetNumberField(TEXT("actual_jitter"),TemporalPhase==1?0:1); Phase->SetNumberField(TEXT("discarded_actual_frames"),TemporalWarmFrames); Phase->SetNumberField(TEXT("discarded_valid_drawn_callbacks"),TemporalWarmupCaptures.Num()); Phase->SetNumberField(TEXT("valid_drawn_callbacks"),TemporalWarmupCaptures.Num()+TemporalSamples.Num()); Phase->SetNumberField(TEXT("actual_viewport_observations"),TemporalSamples.Num()); Phase->SetNumberField(TEXT("white_min"),Minimum); Phase->SetNumberField(TEXT("white_max"),Maximum); Phase->SetNumberField(TEXT("white_mean"),Sum/16.); Phase->SetNumberField(TEXT("xor_previous_total"),XorPrevious); Phase->SetNumberField(TEXT("xor_first_total"),XorFirst); Phase->SetBoolField(TEXT("sampling_valid"),Maximum>0&&TemporalWarmupCaptures.Num()==8); Phase->SetArrayField(TEXT("warmup_captures"),TemporalWarmupCaptures); Phase->SetArrayField(TEXT("samples"),TemporalSamples);
            const bool SpatialValid=AddTemporalSpatialSummary(Phase); // GT段边界只输出聚合计数；保留失败段证据后再停止，不新增采样/截图。
            TemporalPhases.Add(MakeShared<FJsonValueObject>(Phase)); if(!SpatialValid) return;
            if(Maximum<=0||TemporalWarmupCaptures.Num()!=8) { FailTemporalSampling(TEXT("phase has no threshold-white observations or not eight valid warm callbacks; no material conclusion")); return; }
            ++TemporalPhase; TemporalWarmFrames=0; TemporalSamples.Reset(); TemporalWarmupCaptures.Reset(); TemporalFirstMask.Reset(); TemporalPreviousMask.Reset(); TemporalWhiteCounts.Reset(); TemporalFlipCounts.Reset();
            if(TemporalPhase==3) { bTemporalComplete=RestoreTemporalProbe(); Check(bTemporalComplete,TEXT("three actual drawn-callback phases completed; own delegate removed and original jitter value/flags restored")); NextReal=FPlatformTime::Seconds()+.03; return; }
            IConsoleVariable* Variable=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepthTemporalAAJitter")); if(!Variable) { FailTemporalSampling(TEXT("jitter variable disappeared at phase boundary")); return; }
            Variable->Set(TemporalPhase==1?0:1,ECVF_SetByConsole); if(Variable->GetInt()!=(TemporalPhase==1?0:1)) FailTemporalSampling(TEXT("next phase jitter did not actually apply"));
            return; // 切段只在GT Tick；下一更晚Tick才排新请求，避免旧广播Reset清掉下一段。
        }
    }
    TSharedPtr<FJsonObject> Context; if(!ReadTemporalContext(Context)) return;
    if(FScreenshotRequest::IsScreenshotRequested()) { FailTemporalSampling(TEXT("another screenshot request appeared in isolated probe")); return; }
    TemporalRequestContext=Context; TemporalPendingPhase=TemporalPhase; TemporalRequestedFrame=GFrameCounter; TemporalRequestedAt=FPlatformTime::Seconds(); ++TemporalRequestSerial;
    const FString Name=Directory/FString::Printf(TEXT("TemporalProbe_Memory_%03llu.png"),TemporalRequestSerial); // 唯一关联名；绑定的正常LDR委托消费内存，绝不保存这些文件。
    FScreenshotRequest::RequestScreenshot(Name,false,false); TemporalOwnedCaptureName=FScreenshotRequest::GetFilename(); bTemporalCapturePending=true;
}
void UDemoSlotsPointInputAudit::AdvanceKeyPress()
{
    DEMO_LOG_TICK(); const double Real=FPlatformTime::Seconds(); UDemoMenuFlowComponent* Flow=Controller->GetMenuFlow();
    UDemoCasinoDeviceInputComponent* Input=Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>();
    if(Step==100)
    {
        if(Real<NextReal||!AimedControlReady()) return;
        if(bTemporalRequested&&!bTemporalComplete&&ResumeStep==16&&AimedControl==TEXT("amount.digit.0")) { BeginTemporalProbe(); return; }
        bInjectDown=true; PressStarted=Real; RequestBeforeKey=Input->GetLastSubmittedRequestId(); Step=101; return;
    }
    ReadKeyMotion();
    if(Step==101)
    {
        if(!bPressedShot&&ResumeStep==16&&AimedControl==TEXT("amount.digit.0")&&MaximumKeyTravel>.05&&Real-PressStarted>.04)
        { if(!CheckFocusedOutline(AimedControl,TEXT("digit0 pressed actual silhouette"))) return; bPressedShot=Capture(TEXT("02_Focused_Number_Pressed_Edge")); return; }
        if(Real-PressStarted<.25) return;
        if(!Check(Input->GetPrimaryPressOwner()==TEXT("Device"),TEXT("actual held primary gesture belongs to Device, not combat"))) return;
        bInjectDown=false; Step=102; NextReal=Real+.45; return;
    }
    if(Real<NextReal) return;
    FTransform Actual; const UDemoCasinoLocalViewComponent* View=Controller->FindComponentByClass<UDemoCasinoLocalViewComponent>();
    if(!Check(Flow->GetCasinoDeviceAmountText()==ExpectedAmount,TEXT("IA_Fire click applies expected local amount: ")+AimedControl.ToString())
       ||!Check(Input->GetLastSubmittedRequestId()==RequestBeforeKey,TEXT("amount key creates no economic request"))
       ||!Check(View&&View->ReadModelPartPoseForAudit(PressPart,Actual)&&MaximumKeyTravel>.01&&Actual.GetTranslation().Equals(PressRest.GetTranslation(),.01),FString::Printf(TEXT("actual key moved then returned to rest control=%s maximum_cm=%.6f"),*AimedControl.ToString(),MaximumKeyTravel))) return;
    if(AimedControl==TEXT("amount.commit")&&!Check(Flow->GetCasinoDraft().Stake==10,TEXT("actual SET BET commits ten-gold draft"))) return;
    PressedControls.Add(AimedControl); ++MechanicalKeysChecked; PressPart=NAME_None; Step=ResumeStep; NextReal=Real+.03;
}
bool UDemoSlotsPointInputAudit::CheckReelMotion(bool bSettled)
{
    DEMO_LOG_CALL(); const UDemoCasinoLocalViewComponent* View=Controller->FindComponentByClass<UDemoCasinoLocalViewComponent>();
    if(!View||!Model.IsValid()||!Device.IsValid()||CommittedSymbols.Num()!=3) return Check(false,TEXT("real committed three-symbol view exists"));
    bool Moved=false; int32 Reels=0;
    for(const FDemoCasinoModelPart& Part:Model->Parts)
    {
        if(Part.Kind!=EDemoCasinoModelPartKind::Reel&&Part.Kind!=EDemoCasinoModelPartKind::Lever) continue;
        FTransform Actual; if(!Check(View->ReadModelPartPoseForAudit(Part.PartId,Actual),TEXT("actual mechanical component readable"))) return false;
        FTransform Expected=Part.RestTransform;
        if(Part.Kind==EDemoCasinoModelPartKind::Reel) { ++Reels; if(!Part.SymbolStopAngles.IsValidIndex(CommittedSymbols[Part.ReelIndex])) return Check(false,TEXT("saved symbols index valid stop")); Expected.SetRotation(Expected.GetRotation()*FQuat(Part.MotionAxis,FMath::DegreesToRadians(Part.SymbolStopAngles[CommittedSymbols[Part.ReelIndex]]))); }
        const FTransform Neutral=Part.RestTransform*Device->GetActorTransform(); Moved|=Actual.GetRotation().AngularDistance(Neutral.GetRotation())>FMath::DegreesToRadians(.5f);
        const FTransform Stop=Expected*Device->GetActorTransform(); if(bSettled&&!Check(Actual.GetTranslation().Equals(Stop.GetTranslation(),.01)&&Actual.GetRotation().AngularDistance(Stop.GetRotation())<.0001,TEXT("actual SavedSymbols reel/lever settles to source stop"))) return false;
    }
    return Check(Reels==3&&(bSettled||Moved),bSettled?TEXT("three actual drums settled"):TEXT("saved transaction animates actual mechanical pieces"));
}
bool UDemoSlotsPointInputAudit::CheckDisk()
{
    DEMO_LOG_CALL(); const UDemoRunSaves* Saves=GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>();
    const UDemoRunSave* Disk=Saves?Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(Saves->SlotName(Saves->GetActiveSlot()),0)):nullptr;
    return Check(Disk&&Disk->Validate()&&Disk->Coins==CommittedCoins&&Disk->Gambling.Serial==CommittedSerial&&Disk->Gambling.LastSlotsReceipt.Serial==CommittedSerial&&Disk->Gambling.LastSlotsReceipt.Symbols==CommittedSymbols,TEXT("actual local checkpoint retains committed coins/serial/typed symbols"));
}
bool UDemoSlotsPointInputAudit::Capture(const FString& Name)
{
    DEMO_LOG_CALL(); const FIntPoint Size=GetWorld()->GetGameViewport()&&GetWorld()->GetGameViewport()->Viewport?GetWorld()->GetGameViewport()->Viewport->GetSizeXY():FIntPoint::ZeroValue;
    if(!CheckBudget(Name)||!Check(Shots.Num()<4&&PendingShot.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested()&&Size==FIntPoint(1280,720),TEXT("one actual1280x720 request, max4"))) return false;
    PendingShot=Directory/(Name+TEXT(".png")); ShotFrame=GFrameCounter; ShotAt=FPlatformTime::Seconds(); Shots.Add(PendingShot); FScreenshotRequest::RequestScreenshot(PendingShot,false,false); return true;
}
void UDemoSlotsPointInputAudit::Finish(bool Success)
{
    DEMO_LOG_CALL(); if(bFinished) return; bFinished=true; if(!RestoreTemporalProbe()) Success=false; bInjectDown=false; InjectPrimary();
    if(PostActorHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(PostActorHandle); PostActorHandle.Reset();
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); Report->SetStringField(TEXT("id"),TEXT("SLOTS-POINT-INPUT-20261116")); Report->SetStringField(TEXT("status"),Success?(bGunTarget?TEXT("PASS_RIFLE_OBLIQUE_PARTIAL_DIAGNOSTIC_SCOPE"):TEXT("PASS_LISTED_EDITOR_STANDALONE_SCOPE")):bTemporalSamplingFailure?TEXT("FAIL_SAMPLING_NOT_MATERIAL_EVIDENCE"):TEXT("FAIL_ACTUAL_POINT_INPUT_AUDIT"));
    Report->SetStringField(TEXT("input_path"),bGunTarget?TEXT("Normal production ShopFocus Tick/direct first Visibility registered rifle creates actual private mask/Camera MID. No IA_Fire load/injection, no E or purchase/bet; initial temporary-slot isolated budget only. Four fixed/slowyaw off/on groups and two normal PNGs; no15-key claims."):TEXT("UEnhancedPlayerInput::InjectInputForAction(existing IA_Fire bool) -> normal Started/Completed/Canceled bindings. No direct SubmitDeviceAction/TryRoute/FirePressed invocation. E entry uses Character.TriggerWorldInteraction."));
    Report->SetStringField(TEXT("outline_target"),bGunTarget?TEXT("RifleOblique"):TEXT("Keycap"));
    if(bGunTarget) { Report->SetStringField(TEXT("gun_target_id"),TEXT("gunshop_rifle_01")); Report->SetStringField(TEXT("gun_source_component"),GunSource.IsValid()?GunSource->GetPathName():TEXT("unavailable")); Report->SetNumberField(TEXT("gun_actual_oblique_angle_degrees"),GunObliqueAngle); Report->SetNumberField(TEXT("gun_actual_focus_distance_cm"),GunFocusDistance); Report->SetStringField(TEXT("gun_roi_scope"),TEXT("PARTIAL center96 receiver/edge; RGB diagnostic not whole gun or semanticStencil; Keycap/E/LMB/currency purchases not exercised.")); }
    Report->SetNumberField(TEXT("elapsed_real_seconds"),FPlatformTime::Seconds()-StartedReal); Report->SetNumberField(TEXT("elapsed_world_seconds"),GetWorld()->GetTimeSeconds()-StartedWorld);
    Report->SetNumberField(TEXT("query_checks"),QueryChecks); Report->SetNumberField(TEXT("maximum_query_shift_cm"),MaximumQueryShift); Report->SetNumberField(TEXT("aim_corrections"),AimCorrections); Report->SetNumberField(TEXT("mechanical_keys_measured"),MechanicalKeysChecked); Report->SetNumberField(TEXT("maximum_actual_key_travel_cm"),MaximumObservedKeyTravel);
    Report->SetBoolField(TEXT("entry_held_requires_release"),bEntryReleaseBarrierPassed); Report->SetBoolField(TEXT("held_cross_key_no_repeat"),bLongHoldCrossPassed); Report->SetNumberField(TEXT("unique_actual_controls_pressed"),PressedControls.Num()); Report->SetNumberField(TEXT("committed_serial"),CommittedSerial); Report->SetNumberField(TEXT("committed_paid"),CommittedPaid);
    Report->SetArrayField(TEXT("focused_mesh_outline_facts"),OutlineFacts); Report->SetNumberField(TEXT("actual_outline_key_pose_pair_samples"),OutlinePosePairSamples); Report->SetNumberField(TEXT("maximum_actual_outline_travel_cm"),MaximumOutlineTravel); Report->SetNumberField(TEXT("maximum_outline_source_position_error_cm"),MaximumOutlineSourceError); Report->SetNumberField(TEXT("maximum_outline_source_angle_error_deg"),MaximumOutlineAngleError);
    Report->SetBoolField(TEXT("actual_outline_follows_pressed_digit0"),bOutlinePressedMotionObserved); Report->SetBoolField(TEXT("actual_outline_changes_digit1_to2_without_old_mask"),bOutlineSwitchPassed); Report->SetBoolField(TEXT("actual_outline_cleared_on_exit_current_pawn"),bOutlineClearedOnExit);
    Report->SetBoolField(TEXT("temporal_outline_probe_requested"),bTemporalRequested);
    Report->SetBoolField(TEXT("before_tsr_t02_candidate_requested"),bCandidateRequested);
    if(bCandidateRequested) { Report->SetBoolField(TEXT("candidate_da_original_reference_restored"),bCandidateRestored); Report->SetBoolField(TEXT("candidate_original_package_dirty"),bCandidateOriginalDirty); Report->SetStringField(TEXT("candidate_final_actual_da_material"),CandidateDefinition.IsValid()?CandidateDefinition->HighlightMaterial.ToSoftObjectPath().ToString():TEXT("not_installed")); Report->SetBoolField(TEXT("candidate_final_actual_package_dirty"),CandidateDefinition.IsValid()&&CandidateDefinition->GetOutermost()->IsDirty()); }
    if(bTemporalRequested) { if(!TemporalMetadata.IsValid()) TemporalMetadata=MakeShared<FJsonObject>(); TemporalMetadata->SetBoolField(TEXT("completed"),bTemporalComplete); TemporalMetadata->SetBoolField(TEXT("sampling_failure"),bTemporalSamplingFailure); TemporalMetadata->SetNumberField(TEXT("valid_drawn_callbacks"),TemporalValidDrawnCallbacks); TemporalMetadata->SetNumberField(TEXT("rejected_callbacks"),TemporalRejectedCallbacks); TemporalMetadata->SetBoolField(TEXT("own_screenshot_delegate_removed"),!TemporalCaptureHandle.IsValid()); TemporalMetadata->SetBoolField(TEXT("original_value_and_flags_restored"),bTemporalRestored); TemporalMetadata->SetArrayField(TEXT("phases"),TemporalPhases); TemporalMetadata->SetArrayField(TEXT("incomplete_phase_samples"),TemporalSamples); TemporalMetadata->SetArrayField(TEXT("incomplete_phase_warm_callbacks"),TemporalWarmupCaptures); Report->SetObjectField(TEXT("temporal_outline_probe"),TemporalMetadata); }
    TArray<TSharedPtr<FJsonValue>> Values; for(const FString& Value:Checks) Values.Add(MakeShared<FJsonValueString>(Value)); Report->SetArrayField(TEXT("checks"),Values); Values.Reset(); for(const FString& Value:Shots) Values.Add(MakeShared<FJsonValueString>(Value)); Report->SetArrayField(TEXT("pngs"),Values);
    Report->SetArrayField(TEXT("not_run"),{MakeShared<FJsonValueString>(bGunTarget?TEXT("Keycap controls, E/IA_Fire inputs, purchase/bet transactions and whole-gun/semanticStencil coverage are not exercised in RifleOblique supplement"):TEXT("physical OS mouse hardware/mapping remap; injection is real EnhancedInput Action evaluation")),MakeShared<FJsonValueString>(TEXT("Listen2-4 latency/privacy/parallel users, EOS, save-failure injection/Waiting reentry, death/travel, Cook/Shipping, complete art/readability acceptance"))});
    FString Text; const bool Written=FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text))&&FFileHelper::SaveStringToFile(Text,*(Directory/TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogFPSDemo,Display,TEXT("SLOTS_POINT_AUDIT_%s directory=%s pngs=%d"),Success&&Written?TEXT("SUCCESS"):TEXT("FAILED"),*Directory,Shots.Num()); FPlatformMisc::RequestExitWithStatus(false,Success&&Written?0:1);
}
void UDemoSlotsPointInputAudit::OnPostActorTick(UWorld* World,ELevelTick TickType,float Delta)
{
    DEMO_LOG_TICK(); if(bFinished||World!=GetWorld()||TickType==LEVELTICK_TimeOnly) return;
    const double Real=FPlatformTime::Seconds(),Now=World->GetTimeSeconds(); // 机械等待真实秒；12秒租约探针沿生产World秒，不重设任何业务时钟。
    if(!bFailed&&Real-StartedReal>90.) Check(false,TEXT("bounded90 real-second timeout")); if(bFailed) { Finish(false); return; }
    if(bCandidateRequested)
    {
        if(!CandidateDefinition.IsValid()||!bCandidateApplied||CandidateDefinition->HighlightMaterial.ToSoftObjectPath()!=FSoftObjectPath(CandidateObjectPath)||CandidateDefinition->GetOutermost()->IsDirty()!=bCandidateOriginalDirty) { Check(false,TEXT("candidate actual DA reference/package dirty changed unexpectedly")); return; }
        const FMaterialResource* Resource=CandidateMaterial.IsValid()?CandidateMaterial->GetMaterialResource(World->GetFeatureLevel()):nullptr; // 游戏线程只读实际候选FeatureLevel shader map，不启动Renderer私有桥。
        if(!bCandidateShaderReadinessLogged||((!Resource||!Resource->IsGameThreadShaderMapComplete())&&Real-StartedReal>15.)) { UE_LOG(LogFPSDemo,Log,TEXT("SLOTS_CANDIDATE_SHADER_READY world=%s feature_level=%d resource_null=%d is_compiling=%d shader_map_complete=%d elapsed_real=%.3f"),*World->GetOutermost()->GetName(),static_cast<int32>(World->GetFeatureLevel()),Resource==nullptr,CandidateMaterial.IsValid()&&CandidateMaterial->IsCompiling(),Resource&&Resource->IsGameThreadShaderMapComplete(),Real-StartedReal); bCandidateShaderReadinessLogged=true; }
        if(!Resource||!Resource->IsGameThreadShaderMapComplete()) { if(Real-StartedReal>15.) Check(false,TEXT("candidate shader-ready preflight exceeded15 real seconds")); return; }
    }
    if(Controller.IsValid()&&FireAction.IsValid()&&!InjectPrimary()) { Check(false,TEXT("actual EnhancedPlayerInput is available")); return; }
    if(Device.IsValid()&&!QueryIds.IsEmpty()&&!CheckQueries()) return;
    if(!bGunTarget&&((Step>=6&&Step<=24)||Step>=100))
    {
        const UDemoCasinoDeviceInputComponent* Input=Controller.IsValid()?Controller->FindComponentByClass<UDemoCasinoDeviceInputComponent>():nullptr;
        if(!Input||!Input->IsOperating()) { Check(false,TEXT("physical lease remains active across keys/screen/lever")); return; }
    }
    if(!PendingShot.IsEmpty()) { if(GFrameCounter>ShotFrame&&IFileManager::Get().FileSize(*PendingShot)>1000) { Check(true,TEXT("native PNG written: ")+FPaths::GetCleanFilename(PendingShot)); PendingShot.Reset(); } else if(Real-ShotAt>3.) Check(false,TEXT("native PNG write timeout")); return; }
    if(bTemporalActive) { AdvanceTemporalProbe(); return; }
    if(bGunTarget&&Step>=200) { AdvanceGunFixture(); return; }
    if(Step>=100) { AdvanceKeyPress(); return; }
    if(Real<NextReal) return;
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(World,0)); ADemoCharacter* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>(); UDemoRunSaves* Saves=World->GetGameInstance()?World->GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr;
    ADemoGameState* State=World->GetGameState<ADemoGameState>(); ADemoPlayerState* PS=Pawn?Pawn->GetPlayerState<ADemoPlayerState>():nullptr; UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
    if(!PC||!Pawn||!Mode||!Saves||!State||!Progress) return; Player=Pawn; Controller=PC;
    UDemoMenuFlowComponent* Flow=PC->GetMenuFlow(); UDemoCasinoDeviceInputComponent* Input=PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); UDemoCasinoLocalViewComponent* View=PC->FindComponentByClass<UDemoCasinoLocalViewComponent>(); UDemoHubShopWorldSubsystem* Registry=World->GetSubsystem<UDemoHubShopWorldSubsystem>();
    switch(Step)
    {
    case 0:
    {
        if(!Check(Flow&&Input&&View,TEXT("real PC physical services exist"))) return; Flow->UpdateRealtime(0.f); if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) Flow->DispatchUIAction(EDemoUIAction::OverlayBack);
        if(!Check(!Flow->HasBlockingOverlay(),TEXT("normal guide/modal closed"))) return;
        if(UDemoPlayerProfile* Profile=World->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Profile->SetDebugTestFlag(0,false); // 仅本Editor GI关闭免扣，后续不改钱包/Serial/奖面。
        if(!Check(Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("normal temporary slot and StartRun"))) return; Saves->ConsumePending(); PC->OnRunReady();
        FDemoPlayerProgressSnapshot Budget=Progress->GetSnapshot(); Budget.Coins=6000; if(!Check(Progress->Commit(Budget)&&Mode->SaveCheckpoint(),TEXT("persist isolated initial six-thousand-gold budget"))) return;
        if(bGunTarget) { GunInitialCoins=Progress->GetSnapshot().Coins; GunInitialSerial=Progress->GetSnapshot().Gambling.Serial; Step=200; NextReal=Real+.3; return; } // 枪补充分流在加载/注入IA_Fire之前，不进入老虎机租约或购买。
        FireAction=LoadObject<UInputAction>(nullptr,TEXT("/Game/Weapons/Input/IA_Fire.IA_Fire")); if(!Check(FireAction.IsValid()&&Cast<UEnhancedPlayerInput>(PC->PlayerInput),TEXT("existing IA_Fire and real EnhancedPlayerInput"))) return;
        if(!Check(Pawn->GetWeaponComponent()&&Pawn->GetWeaponComponent()->EquipSlot(2),TEXT("normal default pistol equip before interaction"))) return; Step=1; NextReal=Real+.3; return;
    }
    case 1:
    {
        if(!Registry||!Registry->GetDefinition()||State->Phase!=EDemoPhase::Hub) return;
        for(const FDemoHubCasinoBinding& Binding:Registry->GetDefinition()->Devices) if(Binding.Mode==1) { Device=Registry->FindDevice(Binding.DeviceId); break; }
        UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr; if(!Controls||!Controls->IsReady()) return;
        Model=const_cast<UDemoCasinoDeviceProfile*>(Controls->GetModelProfile()); if(!Check(Model.IsValid(),TEXT("formal same-version model, not prototype"))) return;
        for(int32 Digit=0;Digit<10;++Digit) QueryIds.Add(FName(*FString::Printf(TEXT("amount.digit.%d"),Digit))); QueryIds.Append({FName(TEXT("amount.clear")),FName(TEXT("amount.backspace")),FName(TEXT("amount.commit")),FName(TEXT("slots.pull")),FName(TEXT("device.exit"))});
        for(FName Id:QueryIds) { FVector Point; if(!Check(Controls->GetControlPoint(Id,Point),TEXT("query exists ")+Id.ToString())) return; QueryRest.Add(Point); }
        const FVector Stand=Device->GetActorTransform().TransformPosition(FVector(180,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())); if(!Check(Pawn->TeleportTo(Stand,Pawn->GetActorRotation()),TEXT("actual device front reachable without collision bypass"))) return;
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Pawn->ConsumeMovementInputVector(); if(!AimControl(TEXT("slots.pull"))) return; Step=2; NextReal=Real+.5; return;
    }
    case 2:
    {
        if(!AimedControlReady()) return; ADemoHUD* HUD=Cast<ADemoHUD>(PC->GetHUD()); if(!Check(HUD&&HUD->ShouldDrawWorldReticle(State,Pawn),TEXT("Hub independent point-reticle policy visible"))) return;
        Step=3; Capture(TEXT("01_Hub_Point_Before_Operate")); return;
    }
    case 3: bInjectDown=true; Step=4; NextReal=Real+.2; return;
    case 4: if(!AimedControlReady()) return; Pawn->TriggerWorldInteraction(); Step=5; NextReal=Real+.2; return;
    case 5:
        if(!Input->IsOperating()) { if(Real-AimStarted>4.) Check(false,TEXT("normal E route obtains actual physical lease")); return; }
        BarrierAmount=Flow->GetCasinoDeviceAmountText(); if(!AimControl(TEXT("amount.digit.1"))) return; Step=6; NextReal=Real+.5; return;
    case 6:
        bEntryReleaseBarrierPassed=Flow->GetCasinoDeviceAmountText()==BarrierAmount&&Input->GetLastSubmittedRequestId()==0;
        if(!Check(bEntryReleaseBarrierPassed,TEXT("held IA_Fire across E entry cannot select before release"))) return; bInjectDown=false; Step=7; NextReal=Real+.2; return;
    case 7: SweepWorld=Now; NextSweepWorld=Now; SweepIndex=0; Step=8; return;
    case 8:
        if(Now>=NextSweepWorld)
        {
            NextSweepWorld=Now+.6; const int32 Index=SweepIndex++%12;
            if(Index<10) AimControl(FName(*FString::Printf(TEXT("amount.digit.%d"),Index)));
            else if(Index==10) PC->SetControlRotation((Device->GetActorTransform().TransformPosition(Model->ScreenTransform.GetTranslation())-Pawn->GetSharedViewLocation()).Rotation());
            else AimControl(TEXT("slots.pull"));
        }
        if(Now-SweepWorld<12.25) return;
        if(!Check(Input->IsOperating()&&Mode->GetShopSystem()->IsPhysicalCasinoOperator(Pawn),TEXT("twelve-game-second key/screen/lever sweep survives multiple server heartbeats"))) return;
        BeginKeyPress(TEXT("amount.clear"),TEXT(""),9); return;
    case 9: if(!AimControl(TEXT("amount.digit.1"))) return; Step=10; NextReal=Real+.08; return;
    case 10: if(!AimedControlReady()) return; bInjectDown=true; Step=11; NextReal=Real+.25; return;
    case 11:
        if(!Check(Flow->GetCasinoDeviceAmountText()==TEXT("1"),TEXT("fresh released IA_Fire selects one digit"))) return; PressedControls.Add(TEXT("amount.digit.1"));
        bOutlineDigitOneObserved=CheckFocusedOutline(TEXT("amount.digit.1"),TEXT("digit1 before held gaze switches")); if(!bOutlineDigitOneObserved||!AimControl(TEXT("amount.digit.2"))) return; Step=12; NextReal=Real+.7; return;
    case 12:
        bLongHoldCrossPassed=Flow->GetCasinoDeviceAmountText()==TEXT("1"); if(!Check(bLongHoldCrossPassed,TEXT("held primary crossing digit1 to2 does not type again"))) return;
        bOutlineSwitchPassed=bOutlineDigitOneObserved&&CheckFocusedOutline(TEXT("amount.digit.2"),TEXT("digit2 after held gaze switched; digit1 mask gone")); if(!bOutlineSwitchPassed) return; bInjectDown=false; Step=13; NextReal=Real+.2; return;
    case 13: BeginKeyPress(TEXT("amount.digit.2"),TEXT("12"),14); return;
    case 14: BeginKeyPress(TEXT("amount.clear"),TEXT(""),15); return;
    case 15: DigitIndex=0; NumericExpected.Empty(); Step=16; return;
    case 16:
        if(DigitIndex<10) { if(NumericExpected==TEXT("0")) NumericExpected.Empty(); NumericExpected+=FString::FromInt(DigitIndex); const FName Id(*FString::Printf(TEXT("amount.digit.%d"),DigitIndex++)); BeginKeyPress(Id,NumericExpected,16); return; }
        BeginKeyPress(TEXT("amount.backspace"),TEXT("12345678"),17); return;
    case 17: BeginKeyPress(TEXT("amount.clear"),TEXT(""),18); return;
    case 18: BeginKeyPress(TEXT("amount.digit.1"),TEXT("1"),19); return;
    case 19: BeginKeyPress(TEXT("amount.digit.0"),TEXT("10"),20); return;
    case 20: BeginKeyPress(TEXT("amount.commit"),TEXT("10"),21); return;
    case 21:
        BeforeCoins=Progress->GetSnapshot().Coins; BeforeSerial=Progress->GetSnapshot().Gambling.Serial; BeforeRequest=Input->GetLastSubmittedRequestId(); BeforeSave=Saves->GetChangeSerial(); ReceiptObserved=-1.; if(!AimControl(TEXT("slots.pull"))) return; Step=22; NextReal=Real+.1; return;
    case 22: if(!AimedControlReady()) return; bInjectDown=true; PullStarted=Real; Step=23; return;
    case 23:
    {
        if(Input->IsWaiting()||View->GetLastResult().Serial<BeforeSerial+1) { if(Real-PullStarted>4.) Check(false,TEXT("real IA_Fire pull commits via save/Owner receipt")); return; }
        const FDemoCasinoResult& Result=View->GetLastResult();
        if(ReceiptObserved<0.)
        {
            ReceiptObserved=Real; CommittedCoins=Progress->GetSnapshot().Coins; CommittedSerial=Progress->GetSnapshot().Gambling.Serial; CommittedPaid=Result.Paid; CommittedSymbols=Result.Symbols;
            if(!Check(CommittedSerial==BeforeSerial+1&&Input->GetLastSubmittedRequestId()==BeforeRequest+1&&int64(CommittedCoins)==int64(BeforeCoins)-10+CommittedPaid&&Saves->GetChangeSerial()>BeforeSave,TEXT("one actual pull nonce/save/debit and original payout"))||!CheckDisk()) return; PressedControls.Add(TEXT("slots.pull"));
        }
        if(!bRevealShot&&Real-ReceiptObserved>.15) { if(!CheckReelMotion(false)) return; bRevealShot=Capture(TEXT("03_Saved_Pull_Mechanical_Reveal")); return; }
        if(Real-PullStarted<3.5) return;
        if(!Check(Input->GetLastSubmittedRequestId()==BeforeRequest+1&&Progress->GetSnapshot().Gambling.Serial==CommittedSerial&&Progress->GetSnapshot().Coins==CommittedCoins,TEXT("hold through reveal does not issue second wager"))||!CheckReelMotion(true)) return; bInjectDown=false; Step=24; NextReal=Real+.2; return;
    }
    case 24: if(!AimControl(TEXT("device.exit"))) return; Step=25; NextReal=Real+.1; return;
    case 25: if(!AimedControlReady()) return; bInjectDown=true; Step=26; NextReal=Real+.2; return;
    case 26:
    {
        if(!Check(!Input->IsOperating(),TEXT("actual IA_Fire EXIT closes lease and local view"))) return;
        FName Control; FString Material; FTransform Outline; bOutlineClearedOnExit=!View->ReadFocusedOutlineForAudit(Control,Material,Outline)&&CountPawnSilhouetteMasks()==0;
        if(!Check(bOutlineClearedOnExit,TEXT("EXIT clears actual outline binding and leaves no current-Pawn253 mask"))) return;
        PressedControls.Add(TEXT("device.exit")); bInjectDown=false; if(!AimControl(TEXT("slots.pull"))) return; Step=27; NextReal=Real+.6; return;
    }
    case 27: if(!AimedControlReady()) return; Pawn->TriggerWorldInteraction(); Step=28; NextReal=Real+.2; return;
    case 28:
        if(!Input->IsOperating()) { if(Real-AimStarted>4.) Check(false,TEXT("normal E reentry obtains new lease")); return; }
        if(!Check(View->GetLastResult().Serial==CommittedSerial&&View->GetLastResult().Symbols==CommittedSymbols&&Progress->GetSnapshot().Coins==CommittedCoins&&Progress->GetSnapshot().Gambling.Serial==CommittedSerial&&Input->GetLastSubmittedRequestId()==BeforeRequest+1,TEXT("reentry restores durable symbols without a new request/debit"))||!CheckDisk()||!CheckReelMotion(true)) return;
        if(!CheckFocusedOutline(TEXT("slots.pull"),TEXT("new operation actual lever edge after durable reentry"))) return; Step=29; Capture(TEXT("04_Reentry_Durable_Result_Edge")); return;
    case 29:
        if(!Check(PressedControls.Num()==15&&Shots.Num()==4&&MechanicalKeysChecked>=13&&bEntryReleaseBarrierPassed&&bLongHoldCrossPassed&&bOutlinePressedMotionObserved&&bOutlineSwitchPassed&&bOutlineClearedOnExit&&OutlinePosePairSamples>0&&(!bTemporalRequested||bTemporalComplete),TEXT("fifteen controls, four native images and actual moving/retargeted/cleared edge mask covered"))||!CheckBudget(TEXT("finish"))) return;
        Finish(true); return;
    default: Check(false,TEXT("unexpected finite audit state")); return;
    }
}
#endif
