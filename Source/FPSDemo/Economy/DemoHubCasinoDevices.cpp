// 职责：七娱乐入口的一台一人授权及已启实体机台的租约存续。初次准入与物理持续操作分开，经济仍沿原Casino/保存事务，租约不拥有钱包或结果。
#include "Economy/DemoShopComponent.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Player/DemoPlayerCommands.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h" // 租约上下文来自服务器实际控件，不接受客户端选择Physical/Page。

FDemoCommandResult UDemoShopComponent::OpenCasinoDevice(ADemoCharacter* Player,FName DeviceId,uint32 WorldEpoch)
{
    DEMO_LOG_CALL();
    FString Error = GetHubShopBlockReason(Player); // 仅首次进入复用逻辑Hub/有效活动槽/生命/准备及模态门禁。
    UDemoHubShopWorldSubsystem* World = GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    ADemoCasinoDevice* Device = World ? World->FindDevice(DeviceId) : nullptr;
    const UDemoHubShopDefinition* Data = World ? World->GetDefinition() : nullptr;
    if (Error.IsEmpty() && (!Data || !Device)) Error = TEXT("娱乐设备尚未就绪");
    if (Error.IsEmpty()) World->ValidateUse(Player,Device,WorldEpoch,Error); // 权威眼位/同步视向/LOS，不接受任意客户端位置。
    if (!Error.IsEmpty()) return FDemoCommandResult::Reject(TEXT("DeviceRejected"),Error);
    const double Now = GetWorld()->GetTimeSeconds(); // World游戏秒，Esc单人暂停不消耗租约。
    if (const FDemoCasinoDeviceLease* Existing = DeviceLeases.Find(DeviceId))
        if (Existing->Player.IsValid() && Existing->Player.Get()!=Player && GetCasinoDeviceBlockReason(Existing->Player.Get(),DeviceId,Existing->Token,Existing->WorldEpoch,Existing->Mode).IsEmpty())
            return FDemoCommandResult::Reject(TEXT("DeviceBusy"),TEXT("该设备正在被其他玩家使用"));
    const FDemoPlayerProgressSnapshot& Progress = Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot(); // 一人一份持续局。
    if ((Progress.Gambling.Mine.Stake>0 && Device->Mode!=2) || (Progress.Gambling.Arcade.Mode>=0 && Device->Mode!=Progress.Gambling.Arcade.Mode))
        return FDemoCommandResult::Reject(TEXT("UnresolvedCasino"),TEXT("请先回到对应设备完成已有未结项目"));
    CloseCasinoDevice(Player); // 仅本人旧设备租约；不得一人同时占两台。
    if (Terminals.IsValid()) Terminals->EndInteraction(Player); // 明确撤旧终端会话，设备不冒充升级圆柱。
    FDemoCasinoDeviceLease Lease; // 新随机Token只发本人，未押注不写盘。
    Lease.Player=Player; Lease.Token=FGuid::NewGuid(); Lease.WorldEpoch=WorldEpoch; Lease.Mode=Device->Mode; Lease.ExpiresAt=Now+Data->DeviceLeaseSeconds;
    const UDemoCasinoControlsComponent* Controls=Device->FindComponentByClass<UDemoCasinoControlsComponent>(); // 当前服务器实际绑定，跨租约不能靠客户端降级回页面绕验证。
    Lease.ControlsRevision=Controls?Controls->GetBindingRevision():0; // 正式Profile版本或明确原型2；不能给新模型签旧常量。
    DeviceLeases.Add(DeviceId,Lease);
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_OPEN device=%s mode=%d"),*DeviceId.ToString(),Lease.Mode);
    return FDemoCommandResult::Applied(true,TEXT("设备已就绪"));
}

void UDemoShopComponent::CloseCasinoDevice(ADemoCharacter* Player,const FGuid& ExpectedToken)
{
    DEMO_LOG_CALL();
    for (auto It=DeviceLeases.CreateIterator();It;++It) // 仅有界七设备；不按ActorIterator扫描场景。
        if (It.Value().Player.Get()==Player && (!ExpectedToken.IsValid() || ExpectedToken==It.Value().Token)) It.RemoveCurrent();
}

FString UDemoShopComponent::GetCasinoDeviceBlockReason(ADemoCharacter* Player,FName DeviceId,const FGuid& Token,uint32 WorldEpoch,int32 Mode) const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr;
    const FDemoCasinoDeviceLease* Lease=DeviceLeases.Find(DeviceId); // 仅本World已批准会话，不能从UI生成。
    if (bStopped || !GetOwner()->HasAuthority() || !State || State->Phase!=EDemoPhase::Hub || !SaveCheckpointRequest.IsBound()) return TEXT("当前阶段不能使用设备"); // 投影可在提交广播栈读取同一有效租约；事务入口单独阻止重入。
    if (!Player || !Lease || Lease->Player.Get()!=Player || !Token.IsValid() || Lease->Token!=Token || Lease->WorldEpoch!=WorldEpoch || Lease->ExpiresAt<=GetWorld()->GetTimeSeconds() || Lease->Mode!=Mode) return TEXT("设备会话已过期或玩法不匹配，请重新交互");
    const UDemoHubShopWorldSubsystem* World=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    const ADemoCasinoDevice* Device=World?World->FindDevice(DeviceId):nullptr;
    const UDemoCasinoControlsComponent* Controls=Device?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr; // 同Mode换Profile/绑定后旧Page/Physical租约一并失效，必须重新批准。
    if (Controls && (Controls->GetBindingRevision()!=Lease->ControlsRevision || (Controls->IsModelRequested()&&!Controls->IsReady()))) return TEXT("设备绑定已更新，请重新交互");
    FString Error; // 原页面继续原整机距角规则；已批准物理机台的心跳/投影不再要求视向仍朝整机中心。
    if (!World || !Device) return TEXT("设备已离开或失效");
    const bool bPhysical=Lease->ControlsRevision>0; // 服务器签发的可信控件版本决定物理路径，不取客户端模式开关。
    const bool bGeometryValid=bPhysical?World->ValidateCasinoMaintain(Player,Device,WorldEpoch,Error):World->ValidateUse(Player,Device,WorldEpoch,Error);
    if (!bGeometryValid) return Error.IsEmpty()?TEXT("设备已离开或失效"):Error;
    return FString();
}

bool UDemoShopComponent::RenewCasinoDevice(ADemoCharacter* Player,FName DeviceId,const FGuid& Token,uint32 WorldEpoch)
{
    DEMO_LOG_CALL();
    FDemoCasinoDeviceLease* Lease=DeviceLeases.Find(DeviceId); // 只更新当前Token，不让迟到心跳复活旧租约。
    if (!Lease) return false;
    if (!GetCasinoDeviceBlockReason(Player,DeviceId,Token,WorldEpoch,Lease->Mode).IsEmpty())
    { if(Lease->Player.Get()==Player&&Lease->Token==Token) DeviceLeases.Remove(DeviceId); return false; } // 真实离开/死亡立即撤原Token；错Token不可撤他人租约。
    const UDemoHubShopDefinition* Data=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()->GetDefinition();
    Lease->ExpiresAt=GetWorld()->GetTimeSeconds()+Data->DeviceLeaseSeconds;
    return true;
}

void UDemoShopComponent::ProjectCasinoDevice(ADemoCharacter* Player,FDemoPlayerMenuSnapshot& View) const
{
    DEMO_LOG_CALL();
    for (const auto& Pair:DeviceLeases)
    {
        const FDemoCasinoDeviceLease& Lease=Pair.Value; // 只发本人，不复制其他设备使用者的Token。
        if (Lease.Player.Get()!=Player || !GetCasinoDeviceBlockReason(Player,Pair.Key,Lease.Token,Lease.WorldEpoch,Lease.Mode).IsEmpty()) continue;
        View.CasinoDeviceId=Pair.Key; View.CasinoDeviceToken=Lease.Token; View.HubWorldEpoch=Lease.WorldEpoch; View.CasinoDeviceMode=Lease.Mode;
        View.CasinoControlsRevision=Lease.ControlsRevision; // 仅Owner投影；不公开私有Token或本人局面给旁观。
        View.Terminal=nullptr; View.TerminalGeneration=0; View.TerminalBlockReason.Empty();
        return;
    }
}
