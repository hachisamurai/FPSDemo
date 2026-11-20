// 职责：既有IA_Fire的本机单次归属、实体Operate细焦点及本人RPC/租约；数字只改草稿，下注沿原GM/Shop保存事务，不接管相机或隐藏全屏页。
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoLocalViewComponent.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Components/PrimitiveComponent.h" // 首次命中组件的真实身份用于有界诊断/可信绑定，不按显示名授权。
#include "Weapons/DemoWeaponComponent.h" // 只在进入/失焦时撤既有动作，不逐按键发StopFire RPC。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Common/DemoUIManager.h" // 拒绝原因沿既有本人字体/HUD短提示，不新开CommonUI全屏页。
#include "UI/DemoUIPresentation.h" // 本机轻按音沿已有设置/限频/CDO资产，不把按键音当经济成功。
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Economy/DemoShopComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"
#include "Framework/Application/SlateApplication.h" // Windows实际应用激活回调；不依赖主要移动平台广播的Core生命周期委托。

UDemoCasinoDeviceInputComponent::UDemoCasinoDeviceInputComponent()
{ DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=true; SetIsReplicatedByDefault(true); }
void UDemoCasinoDeviceInputComponent::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    if (PC() && PC()->IsLocalController() && FSlateApplication::IsInitialized()) DeactivateHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this,&UDemoCasinoDeviceInputComponent::OnApplicationActivationChanged); // 本地游戏线程弱UObject绑定，EndPlay核Slate生命后精确移除。
}
ADemoPlayerController* UDemoCasinoDeviceInputComponent::PC() const
{ DEMO_LOG_TICK(); return Cast<ADemoPlayerController>(GetOwner()); }
void UDemoCasinoDeviceInputComponent::FencePrimaryPress()
{
    DEMO_LOG_CALL();
    bAwaitPrimaryRelease=bAwaitPrimaryRelease || bPrimaryDown;
    PrimaryOwner=bAwaitPrimaryRelease?EDemoPrimaryPressOwner::Blocked:EDemoPrimaryPressOwner::None;
    PrimaryAvatar=PC()?PC()->GetPawn():nullptr; // 更新屏障所属Avatar，仅释放屏障，不将旧武器归属移给新Pawn。
}
void UDemoCasinoDeviceInputComponent::OnApplicationActivationChanged(bool bActive)
{
    DEMO_LOG_CALL();
    if (bActive) return; // 激活不自动恢复旧设备/旧按压，玩家必须重新给出输入。
    if (!PC() || !PC()->IsLocalController()) return;
    if (ADemoCharacter* Player=Cast<ADemoCharacter>(PC()->GetPawn())) if (Player->GetWeaponComponent()) Player->GetWeaponComponent()->CancelActions();
    FencePrimaryPress(); SetControlFocus(NAME_None,false,FString());
    if (bOperating) ExitOperation(); // 退出只释放旧Token，不自动重开或重交下注。
}
bool UDemoCasinoDeviceInputComponent::TryRoutePrimaryPressed()
{
    DEMO_LOG_CALL();
    if (!PC() || !PC()->IsLocalController()) return false;
    if (bPrimaryDown) { UE_LOG(LogFPSDemo,Log,TEXT("PRIMARY_INPUT_REJECT duplicate_started owner=%s"),*GetPrimaryPressOwner().ToString()); return true; } // 原武器持有者仍保归属，释放时必须正常StopFire。
    bPrimaryDown=true; PrimaryAvatar=PC()->GetPawn(); PrimaryOwner=EDemoPrimaryPressOwner::Blocked;
    const ADemoCharacter* Player=Cast<ADemoCharacter>(PC()->GetPawn()); // 本次输入绑定当前Pawn，不跨Avatar缓存授权。
    if (bAwaitPrimaryRelease || !Player || Player->IsCoopEliminated() || PC()->GetViewTarget()!=Player || PC()->IsPaused() || PC()->HasBlockingOverlay() || PC()->IsUpgradeMenuOpen() || PC()->IsNextLevelConfirmationOpen())
    { UE_LOG(LogFPSDemo,Log,TEXT("PRIMARY_INPUT_BLOCK context_or_release_barrier")); return true; }
    if (IsOperating()) { PrimaryOwner=EDemoPrimaryPressOwner::Device; ActivateAimedControl(); return true; }
    PrimaryOwner=EDemoPrimaryPressOwner::Weapon; return false; // 原StartFire继续负责靶场/近战/枪械与所有服务器门禁。
}
bool UDemoCasinoDeviceInputComponent::TryRoutePrimaryReleased()
{
    DEMO_LOG_CALL();
    const bool bCaptured=bPrimaryDown || bAwaitPrimaryRelease || PrimaryOwner!=EDemoPrimaryPressOwner::None;
    const bool bReleaseWeapon=PrimaryOwner==EDemoPrimaryPressOwner::Weapon && PC() && PrimaryAvatar.Get()==PC()->GetPawn();
    bPrimaryDown=false; bAwaitPrimaryRelease=false; PrimaryOwner=EDemoPrimaryPressOwner::None; PrimaryAvatar.Reset();
    return bCaptured && !bReleaseWeapon; // 数字/阻塞释放不发武器RPC；无捕获沿旧StopFiring兜底。
}
bool UDemoCasinoDeviceInputComponent::IsOperating() const
{ DEMO_LOG_TICK(); return bOperating && Device.IsValid() && Token.IsValid() && PC() && Avatar.Get()==PC()->GetPawn(); }
ADemoCasinoDevice* UDemoCasinoDeviceInputComponent::GetCurrentDevice() const
{ DEMO_LOG_TICK(); return IsOperating()?Device.Get():nullptr; }
int32 UDemoCasinoDeviceInputComponent::GetLastSubmittedRequestId() const
{ DEMO_LOG_TICK(); return NextRequestId; }
bool UDemoCasinoDeviceInputComponent::IsWaiting() const
{ DEMO_LOG_TICK(); return PendingRequestId>0; }
FName UDemoCasinoDeviceInputComponent::GetFocusedControlId() const { DEMO_LOG_TICK(); return FocusedControl; }
bool UDemoCasinoDeviceInputComponent::IsFocusedControlEnabled() const { DEMO_LOG_TICK(); return !FocusedControl.IsNone() && bFocusedEnabled; }
const FString& UDemoCasinoDeviceInputComponent::GetFocusedControlPrompt() const { DEMO_LOG_TICK(); return FocusedPrompt; }
FName UDemoCasinoDeviceInputComponent::GetPrimaryPressOwner() const
{
    DEMO_LOG_TICK();
    switch (PrimaryOwner) { case EDemoPrimaryPressOwner::Weapon: return TEXT("Weapon"); case EDemoPrimaryPressOwner::Device: return TEXT("Device"); case EDemoPrimaryPressOwner::Blocked: return TEXT("Blocked"); default: return NAME_None; }
}
bool UDemoCasinoDeviceInputComponent::BeginOperate(const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    if (!PC() || !PC()->IsLocalController() || PC()->HasBlockingOverlay() || Snapshot.CasinoDeviceMode!=1 || !Snapshot.CasinoDeviceToken.IsValid() || !Snapshot.CasinoControlsRevision) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_OPERATE_REJECT owner/modal/mode/token/revision")); return false; }
    UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    ADemoCasinoDevice* Target=Registry?Registry->FindDevice(Snapshot.CasinoDeviceId):nullptr;
    UDemoCasinoControlsComponent* Controls=Target?Target->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    if (!Controls || !Controls->IsReady() || Controls->GetBindingRevision()!=Snapshot.CasinoControlsRevision) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_OPERATE_REJECT unavailable local controls/revision")); return false; }
    if (bOperating) ExitOperation(); // 同Token的初次批准只由Focus分流一次，新会话切换先撤旧本机层。
    Device=Target; Avatar=PC()->GetPawn(); Token=Snapshot.CasinoDeviceToken; Epoch=Snapshot.HubWorldEpoch; Revision=Snapshot.CasinoControlsRevision; bOperating=true; PendingRequestId=0; NextHeartbeatAt=0.; NextControlScanAt=0.; ContextReadyUntil=GetWorld()->GetTimeSeconds()+2.;
    FencePrimaryPress(); // 获批时仍按住的主键必须先释放；不能把旧连射翻译为金额或下注。
    if (ADemoCharacter* Player=Cast<ADemoCharacter>(PC()->GetPawn())) if (Player->GetWeaponComponent()) Player->GetWeaponComponent()->CancelActions(); // 仅上下文边沿一次撤旧技能/装填，经济无调用。
    PC()->GetMenuFlow()->BeginCasinoDeviceOperation(Snapshot); // 初始化原草稿，bMenuOpen保持false，自由观察/移动。
    if (UDemoCasinoLocalViewComponent* View=PC()->FindComponentByClass<UDemoCasinoLocalViewComponent>()) View->Open(Target,Snapshot.Progress.Gambling); // 同次已批准Owner快照的typed耐久事实，不借另一槽旧PC缓存。
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_OPERATE_BEGIN device=%s revision=%u"),*Snapshot.CasinoDeviceId.ToString(),Revision); return true;
}
void UDemoCasinoDeviceInputComponent::ExitOperation(bool bNotifyServer)
{
    DEMO_LOG_CALL();
    const FGuid PreviousToken=Token; // 只用于精确撤销，退出先清本机状态，Listen同步回调不能复活旧Operate。
    const bool bWasOperating=bOperating; bOperating=false; PendingRequestId=0; Token.Invalidate(); Epoch=Revision=0; ActiveControl=NAME_None; Device.Reset(); Avatar.Reset(); NextControlScanAt=ContextReadyUntil=0.;
    if (bWasOperating) FencePrimaryPress();
    SetControlFocus(NAME_None,false,FString());
    if (PC() && PC()->IsLocalController())
    {
        if (UDemoCasinoLocalViewComponent* View=PC()->FindComponentByClass<UDemoCasinoLocalViewComponent>()) View->Close();
        if (bWasOperating) PC()->GetMenuFlow()->EndCasinoDeviceOperation();
        if (bNotifyServer && PreviousToken.IsValid()) ServerReleaseDevice(PreviousToken);
    }
    if (bWasOperating) UE_LOG(LogFPSDemo,Log,TEXT("CASINO_OPERATE_END notify=%d"),bNotifyServer);
}
bool UDemoCasinoDeviceInputComponent::TryConsumeInteraction()
{
    DEMO_LOG_CALL();
    return ActivateAimedControl(); // E旧兼容入口，与左键共用点击即时射线/禁用/交易路径。
}
bool UDemoCasinoDeviceInputComponent::TraceAimedControl(FDemoCasinoControlBinding& Binding,FHitResult& Hit,FString& Reason) const
{
    DEMO_LOG_TICK(); Binding=FDemoCasinoControlBinding(); Hit=FHitResult(); Reason=TEXT("NoControl"); // 输出先复位，失败诊断不能携带旧射线Actor/Component。
    if (!IsOperating() || !PC()->IsLocalController()) { Reason=TEXT("InactiveContext"); return false; }
    const ADemoCharacter* Player=Cast<ADemoCharacter>(PC()->GetPawn());
    const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr;
    const UDemoCasinoControlsComponent* Controls=Device->FindComponentByClass<UDemoCasinoControlsComponent>();
    if (!Player || !Definition || !Controls || !Controls->IsReady() || Controls->GetBindingRevision()!=Revision) { Reason=TEXT("ControlsNotReady"); return false; }
    const FVector Eyes=Player->GetSharedViewLocation(); const FRotator Aim=Player->GetViewRotation(); // 与服务器共享眼位/控制视向同契约，不读旧Camera缓存/枪口或客户端自报命中。
    if (Eyes.ContainsNaN() || Aim.ContainsNaN()) { Reason=TEXT("InvalidView"); return false; }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CasinoControlLocal),false,Player);
    if (!GetWorld()->LineTraceSingleByChannel(Hit,Eyes,Eyes+Aim.Vector()*Definition->InteractionDistanceCm,ECC_Visibility,Query)) { Reason=TEXT("NoHit"); return false; }
    const FDemoCasinoControlBinding* Found=Controls->FindHit(Hit.GetComponent()); // 当前设备可信Query指针精确匹配；壳/墙/队友不会被穿过挑后键。
    if (!Found) { Reason=TEXT("BlockedOrOtherComponent"); return false; }
    Binding=*Found; Reason.Empty(); return true;
}
bool UDemoCasinoDeviceInputComponent::CanActivateControl(const FDemoCasinoControlBinding& Binding,FString& Reason) const
{
    DEMO_LOG_TICK(); Reason.Empty();
    const ADemoCharacter* Player=PC()?Cast<ADemoCharacter>(PC()->GetPawn()):nullptr;
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr;
    if (!IsOperating() || !Player || !State || State->Phase!=EDemoPhase::Hub || Player->IsCoopEliminated() || PC()->GetViewTarget()!=Player || PC()->IsPaused() || PC()->HasBlockingOverlay() || PC()->IsUpgradeMenuOpen() || PC()->IsNextLevelConfirmationOpen())
    { Reason=TEXT("当前无法操作设备"); return false; }
    if (Binding.Action==EDemoCasinoControlAction::Exit) return true; // 保存中仍可退出，迟到已提交结果由原耐久回执恢复。
    const UDemoMenuFlowComponent* Flow=PC()->GetMenuFlow();
    if (PendingRequestId || Flow->IsGambleWaiting()) { Reason=TEXT("正在保存，请稍候"); return false; }
    if (Flow->IsCasinoRolling()) { Reason=TEXT("请等待本次结果揭晓"); return false; }
    if (Binding.Action==EDemoCasinoControlAction::PullSlots && Flow->GetCasinoDeviceAmountText()!=FString::FromInt(Flow->GetCasinoDraft().Stake)) { Reason=TEXT("请先按 SET BET 确认金额"); return false; }
    return true;
}
FString UDemoCasinoDeviceInputComponent::BuildControlPrompt(const FDemoCasinoControlBinding& Binding,bool bEnabled,const FString& Reason) const
{
    DEMO_LOG_TICK();
    if (!bEnabled) return Reason;
    switch (Binding.Action)
    {
    case EDemoCasinoControlAction::AmountDigit: return FString::Printf(TEXT("左键：数字 %d"),Binding.Index);
    case EDemoCasinoControlAction::AmountClear: return TEXT("左键：清空金额");
    case EDemoCasinoControlAction::AmountBackspace: return TEXT("左键：删除一位");
    case EDemoCasinoControlAction::AmountCommit: return TEXT("左键：确认金额");
    case EDemoCasinoControlAction::PullSlots: return FString::Printf(TEXT("左键：下注 %d 金币"),PC()->GetMenuFlow()->GetCasinoDraft().Stake);
    case EDemoCasinoControlAction::Exit: return TEXT("左键：离开设备");
    default: return TEXT("暂不可用");
    }
}
void UDemoCasinoDeviceInputComponent::SetControlFocus(FName ControlId,bool bEnabled,const FString& Prompt)
{
    DEMO_LOG_TICK();
    const bool bVisualChanged=FocusedControl!=ControlId || bFocusedEnabled!=bEnabled;
    FocusedControl=ControlId; bFocusedEnabled=bEnabled; FocusedPrompt=Prompt;
    if (!bVisualChanged) return;
    if (PC()) if (UDemoCasinoLocalViewComponent* View=PC()->FindComponentByClass<UDemoCasinoLocalViewComponent>()) View->SetFocusedControl(ControlId,bEnabled);
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_FOCUS device=%s control=%s enabled=%d"),Device.IsValid()?*Device->DeviceId.ToString():TEXT("None"),*ControlId.ToString(),bEnabled); // 只记ID/状态边沿，不泄露金额/Token或逐帧刷屏。
}
void UDemoCasinoDeviceInputComponent::RefreshControlFocus()
{
    DEMO_LOG_TICK();
    FDemoCasinoControlBinding Binding; FHitResult Hit; FString Reason; // 本次20Hz扫描纯值副本，不缓存指针作为点击授权。
    if (!TraceAimedControl(Binding,Hit,Reason)) { SetControlFocus(NAME_None,false,FString()); return; }
    const bool bEnabled=CanActivateControl(Binding,Reason);
    SetControlFocus(Binding.ControlId,bEnabled,BuildControlPrompt(Binding,bEnabled,Reason));
}
bool UDemoCasinoDeviceInputComponent::ActivateAimedControl()
{
    DEMO_LOG_CALL();
    if (!IsOperating() || !PC()->IsLocalController()) return false;
    if (PC()->HasBlockingOverlay() || PC()->IsUpgradeMenuOpen() || PC()->IsNextLevelConfirmationOpen() || PC()->IsPaused()) return true;
    const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    FString MaintainError; FName MaintainCode; // 本次即时存续复核仅拒绝动作；是否退出由同一Tick生命周期处理。
    if (!Registry || !Registry->ValidateCasinoMaintain(Cast<ADemoCharacter>(PC()->GetPawn()),Device.Get(),Epoch,MaintainError,&MaintainCode))
    { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_REJECT device=%s code=%s reason=%s"),*Device->DeviceId.ToString(),*MaintainCode.ToString(),*MaintainError); return true; }
    FDemoCasinoControlBinding Binding; FHitResult Hit; FString Reason; // 点击重新射线，不复用20Hz高亮或任何客户端经济结果。
    if (!TraceAimedControl(Binding,Hit,Reason))
    {
        SetControlFocus(NAME_None,false,FString());
        UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_NO_HIT device=%s actor=%s component=%s distance_cm=%.2f code=%s"),*Device->DeviceId.ToString(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),Hit.Distance,*Reason);
        if (UDemoUIManager* UI=PC()->FindComponentByClass<UDemoUIManager>()) UI->PresentCommandResult(EDemoGameplayCommand::Casino,FDemoCommandResult::Reject(TEXT("CasinoControlMiss"),TEXT("请将中心点对准设备按键")));
        return true;
    }
    const bool bEnabled=CanActivateControl(Binding,Reason);
    SetControlFocus(Binding.ControlId,bEnabled,BuildControlPrompt(Binding,bEnabled,Reason));
    if (!bEnabled)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_REJECT device=%s control=%s reason=%s"),*Device->DeviceId.ToString(),*Binding.ControlId.ToString(),*Reason);
        if (UDemoUIManager* UI=PC()->FindComponentByClass<UDemoUIManager>()) UI->PresentCommandResult(EDemoGameplayCommand::Casino,FDemoCommandResult::Reject(TEXT("CasinoControlDisabled"),Reason));
        return true;
    }
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_ACTIVATE device=%s control=%s actor=%s component=%s distance_cm=%.2f"),*Device->DeviceId.ToString(),*Binding.ControlId.ToString(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),Hit.Distance);
    if (UDemoCasinoLocalViewComponent* View=PC()->FindComponentByClass<UDemoCasinoLocalViewComponent>()) View->PressControl(Binding.ControlId); // 合法可用才播本人短反馈，禁用键不伪装已接受。
    DemoUI::PlayFeedback(PC(),EDemoUISound::Press); // 原UI音开关/限频生效；真实成功音仍只由保存回执触发。
    ActiveControl=Binding.ControlId; // 同步调用栈内凭据，SubmitCasino不能借异步旧焦点/旧控件提交。
    PC()->GetMenuFlow()->SubmitDeviceAction(Binding); ActiveControl=NAME_None;
    return true;
}
bool UDemoCasinoDeviceInputComponent::SubmitCasinoControl(const FDemoCasinoIntent& Intent,int32 ExpectedSerial)
{
    DEMO_LOG_CALL();
    if (!IsOperating() || !PC()->IsLocalController() || PendingRequestId || ActiveControl!=TEXT("slots.pull") || Intent.Mode!=1 || Intent.Operation!=0 || NextRequestId==MAX_int32) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_SUBMIT_REJECT operation/owner/wait/actual-control/sequence")); return false; }
    FDemoCasinoControlRequest Request; Request.RequestId=++NextRequestId; Request.DeviceId=Device->DeviceId; Request.Token=Token; Request.WorldEpoch=Epoch; Request.BindingRevision=Revision; Request.ControlId=ActiveControl; Request.ExpectedSerial=ExpectedSerial; Request.Stake=Intent.Stake; Request.RulesKey=Intent.RulesKey;
    PendingRequestId=Request.RequestId; // 必须先立Waiting，Listen拥有者可靠回执可能本栈同步返回。
    ServerSubmitDeviceAction(Request); return true;
}
void UDemoCasinoDeviceInputComponent::ServerSubmitDeviceAction_Implementation(const FDemoCasinoControlRequest& Request)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    FDemoCommandResult Result=FDemoCommandResult::Reject(TEXT("NotReady"),TEXT("设备服务尚未就绪"));
    if (PC() && Mode)
    {
        const double Now=GetWorld()->GetTimeSeconds(); // 游戏秒限频，不改变原Casino Serial或奖金。
        if (Request.RequestId<=LastServerRequestId || Request.RequestId<=0) Result=FDemoCommandResult::Reject(TEXT("StaleRequest"),TEXT("设备操作已过期"));
        else
        {
            LastServerRequestId=Request.RequestId; // 执行前消费，失败也不允许同nonce重入；重试须取新nonce和当前Serial。
            if (Now-LastServerActionAt<.1) Result=FDemoCommandResult::Reject(TEXT("RateLimited"),TEXT("操作太快，请稍后再试"));
            else { LastServerActionAt=Now; Result=Mode->GetShopSystem()->ExecuteCasinoControl(PC(),Request); }
        }
    }
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_RESULT request=%d device=%s control=%s code=%s committed=%d"),Request.RequestId,*Request.DeviceId.ToString(),*Request.ControlId.ToString(),*Result.Code.ToString(),Result.Status==EDemoCommandStatus::Committed);
    ClientDeviceActionResult(Request.RequestId,Request.Token,Result,Mode&&PC()?Mode->BuildPlayerMenuSnapshot(PC()):FDemoPlayerMenuSnapshot());
}
void UDemoCasinoDeviceInputComponent::ClientDeviceActionResult_Implementation(int32 RequestId,const FGuid& RequestToken,const FDemoCommandResult& Result,const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    if (!PC() || !PC()->IsLocalController() || RequestId!=PendingRequestId || RequestToken!=Token || !IsOperating()) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_LATE_RESULT visual ignored")); return; } // 真实进度已由原PublishCommitted向Owner复制；离开后不重开/补交。
    PendingRequestId=0;
    if (Snapshot.Progress.Gambling.Serial>=PC()->GetMenuSnapshot().Progress.Gambling.Serial) PC()->ClientRefreshMenuSnapshot(Snapshot); // 接收服务器现投影，不混用PC旧命令ResultId分域；允许空Token撤销。
    PC()->GetMenuFlow()->ReceiveCasinoDeviceResult(Result);
    if (UDemoUIManager* UI=PC()->FindComponentByClass<UDemoUIManager>()) UI->PresentCommandResult(EDemoGameplayCommand::Casino,Result); // 仅原拒绝短提示；已结老虎机结果仍由本人设备层呈现。
    if (Result.Status==EDemoCommandStatus::Committed && Result.CasinoResult.Mode==1)
        if (UDemoCasinoLocalViewComponent* View=PC()->FindComponentByClass<UDemoCasinoLocalViewComponent>()) View->AcceptCommittedResult(Result.CasinoResult);
}
void UDemoCasinoDeviceInputComponent::ServerMaintainDevice_Implementation(FName DeviceId,const FGuid& ExpectedToken,uint32 ExpectedEpoch)
{
    DEMO_LOG_CALL(); AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    if (!Mode || !PC() || !PC()->IsCoopRegistered() || !Mode->GetShopSystem()->RenewCasinoDevice(Cast<ADemoCharacter>(PC()->GetPawn()),DeviceId,ExpectedToken,ExpectedEpoch))
    { ClientDeviceLeaseLost(ExpectedToken); if (PC()) PC()->PublishMenuSnapshot(); }
}
void UDemoCasinoDeviceInputComponent::ClientDeviceLeaseLost_Implementation(const FGuid& ExpectedToken)
{ DEMO_LOG_CALL(); if (Token==ExpectedToken) ExitOperation(false); }
void UDemoCasinoDeviceInputComponent::ServerReleaseDevice_Implementation(const FGuid& ExpectedToken)
{
    DEMO_LOG_CALL(); if (!ExpectedToken.IsValid()) return;
    if (AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) if (PC()) { Mode->GetShopSystem()->CloseCasinoDevice(Cast<ADemoCharacter>(PC()->GetPawn()),ExpectedToken); PC()->PublishMenuSnapshot(); }
}
void UDemoCasinoDeviceInputComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTick)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,ThisTick);
    if (!PC() || !PC()->IsLocalController()) return;
    ADemoCharacter* Player=Cast<ADemoCharacter>(PC()->GetPawn()); // 当前Avatar只在本帧借用，主键归属不能跨重生/外部视点。
    if (bPrimaryDown && PrimaryOwner!=EDemoPrimaryPressOwner::Blocked && (PrimaryAvatar.Get()!=Player || !Player || Player->IsCoopEliminated() || PC()->GetViewTarget()!=Player || PC()->HasBlockingOverlay() || PC()->IsUpgradeMenuOpen() || PC()->IsNextLevelConfirmationOpen()))
    {
        if (PrimaryOwner==EDemoPrimaryPressOwner::Weapon) if (ADemoCharacter* PressPawn=Cast<ADemoCharacter>(PrimaryAvatar.Get())) PressPawn->StopFiring(); // 仅原按压Avatar一次边沿兜底，不误停新Pawn。
        FencePrimaryPress();
    }
    if (!bOperating) return;
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    const FDemoPlayerMenuSnapshot& Snapshot=PC()->GetMenuSnapshot();
    const UDemoCasinoControlsComponent* Controls=Device.IsValid()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr; // 本地绑定重建立即退出，不等服务器心跳。
    if (!IsOperating() || !State || State->Phase!=EDemoPhase::Hub || !Player || Player->IsCoopEliminated() || PC()->GetViewTarget()!=Player || !Controls || Controls->GetBindingRevision()!=Revision || PC()->HasBlockingOverlay() || PC()->IsUpgradeMenuOpen() || Snapshot.CasinoDeviceToken!=Token || Snapshot.HubWorldEpoch!=Epoch || Snapshot.CasinoControlsRevision!=Revision)
    { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_OPERATE_END_REASON code=ContextChanged phase=%d eliminated=%d controls_ready=%d"),State?static_cast<int32>(State->Phase):-1,Player?Player->IsCoopEliminated():true,Controls&&Controls->IsReady()); ExitOperation(); return; }
    const UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>(); const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr;
    const double Now=GetWorld()->GetTimeSeconds();
    if (!Registry || !Definition || Registry->GetWorldEpoch()==0)
    {
        SetControlFocus(NAME_None,false,TEXT("设备同步中，请稍候")); // 服务器已批准但客户端注册代数尚未到，最多等2秒；期间不发按钮或保活授权。
        if (Now>=ContextReadyUntil) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_OPERATE_END_REASON code=LocalRegistryNotReady")); ExitOperation(); }
        return;
    }
    if (Registry->GetWorldEpoch()!=Epoch) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_OPERATE_END_REASON code=WorldEpochMismatch")); ExitOperation(); return; }
    if (Now>=NextControlScanAt)
    {
        NextControlScanAt=Now+1./FMath::Clamp(Definition->ScanHz,5.f,30.f); // 存续与细焦点共用20Hz配置预算，不给15键逐帧Tick。
        FString Error; FName Code; // 共用本地/权威Maintain目的，不要求朝整机中心或把软焦点当租约。
        if (!Registry->ValidateCasinoMaintain(Player,Device.Get(),Epoch,Error,&Code)) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_OPERATE_END_REASON code=%s reason=%s"),*Code.ToString(),*Error); ExitOperation(); return; }
        RefreshControlFocus();
    }
    if (Now>=NextHeartbeatAt) { NextHeartbeatAt=Now+Definition->DeviceHeartbeatSeconds; ServerMaintainDevice(Device->DeviceId,Token,Epoch); }
    if (UDemoCasinoLocalViewComponent* View=PC()->FindComponentByClass<UDemoCasinoLocalViewComponent>()) View->Refresh();
}
void UDemoCasinoDeviceInputComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    if (DeactivateHandle.IsValid()) { if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(DeactivateHandle); DeactivateHandle.Reset(); }
    ExitOperation(false); PrimaryOwner=EDemoPrimaryPressOwner::None; bPrimaryDown=bAwaitPrimaryRelease=false; PrimaryAvatar.Reset();
    Super::EndPlay(Reason);
}
