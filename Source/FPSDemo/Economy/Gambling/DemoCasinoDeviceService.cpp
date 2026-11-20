// 职责：服务器真实控件授权到原Casino事务的窄适配；只在可信同步栈放行物理租约，原页面与旧终端保持原路。不复制概率、保存或Ready失效逻辑。
#include "Economy/DemoShopComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlTypes.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Engine/World.h"
#include "Templates/UnrealTemplate.h"
#include "Debug/DemoLog.h"

bool UDemoShopComponent::IsPhysicalCasinoOperator(const ADemoCharacter* Player) const
{
    DEMO_LOG_TICK();
    if (bStopped || !Player || !GetOwner() || !GetOwner()->HasAuthority() || !GetWorld()) return false;
    for (const auto& Pair:DeviceLeases)
        if (Pair.Value.ControlsRevision && Pair.Value.Player.Get()==Player && Pair.Value.ExpiresAt>GetWorld()->GetTimeSeconds()) return true; // 有界七租约、仅拒绝；释放/超时恢复，不在高频门里发RPC或重新打印LOS。
    return false;
}

FDemoCommandResult UDemoShopComponent::ExecuteCasinoControl(ADemoPlayerController* Controller,const FDemoCasinoControlRequest& Request)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Controller?Cast<ADemoCharacter>(Controller->GetPawn()):nullptr; // 从拥有者连接当前Pawn解析，不能上传玩家ID。
    AFPSDemoGameMode* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AFPSDemoGameMode>():nullptr;
    if (!Mode || !Controller || !Controller->HasAuthority() || !Controller->IsCoopRegistered() || !Pawn || Controller->GetWorld()!=GetWorld()) return FDemoCommandResult::Reject(TEXT("NotRegistered"),TEXT("玩家身份尚未就绪"));
    const FDemoCasinoDeviceLease* Lease=DeviceLeases.Find(Request.DeviceId); // 服务器冻结会话，客户端不能切为Page绕验证。
    FString Error=GetCasinoDeviceBlockReason(Pawn,Request.DeviceId,Request.Token,Request.WorldEpoch,1);
    UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    ADemoCasinoDevice* Device=Registry?Registry->FindDevice(Request.DeviceId):nullptr;
    UDemoCasinoControlsComponent* Controls=Device?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    const FDemoCasinoControlBinding* Binding=Controls?Controls->FindControl(Request.ControlId):nullptr;
    if (Error.IsEmpty() && (!Lease || !Lease->ControlsRevision || Lease->ControlsRevision!=Request.BindingRevision || !Controls || Request.BindingRevision!=Controls->GetBindingRevision() || !Binding || Binding->Action!=EDemoCasinoControlAction::PullSlots || Device->Mode!=1)) Error=TEXT("设备操作件或绑定版本已过期");
    if (Error.IsEmpty()) Controls->ValidateControlHit(Pawn,Request.ControlId,Error); // 真射线首次阻挡必须是所申请的拉杆，墙/队友/其它控件不忽略。
    if (!Error.IsEmpty()) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_REJECT device=%s control=%s reason=%s"),*Request.DeviceId.ToString(),*Request.ControlId.ToString(),*Error); return FDemoCommandResult::Reject(TEXT("ControlRejected"),Error); }
    if (bTransactionInProgress || ExecutingPhysicalToken.IsValid()) return FDemoCommandResult::Reject(TEXT("TransactionBusy"),TEXT("正在处理设备操作，请稍后再试"));
    FDemoGameplayCommandRequest Command; // 无客户端Mode/Op/Tile，全部从可信绑定推导，沿现命令成功后的Ready失效与原保存。
    Command.Command=EDemoGameplayCommand::Casino; Command.GambleSerial=Request.ExpectedSerial; Command.Casino.Mode=1; Command.Casino.Operation=0; Command.Casino.Stake=Request.Stake; Command.Casino.RulesKey=Request.RulesKey;
    Command.CasinoDeviceId=Request.DeviceId; Command.CasinoDeviceToken=Request.Token; Command.HubWorldEpoch=Request.WorldEpoch;
    const TGuardValue<FGuid> Permit(ExecutingPhysicalToken,Request.Token); // 仅本同步执行栈有效，Save回调重入仍受原bTransaction门；退出/失败均复原。
    return Mode->ExecutePlayerCommand(Controller,Command);
}
