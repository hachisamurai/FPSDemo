// 职责：合作加入URL与本地玩家凭据。使用：加入时附带兼容协议号，不能由旧客户端绕过服务器校验。
#include "Player/Coop/DemoCoopLocalPlayer.h"
#include "Player/Coop/DemoCoopProtocol.h"
#include "Player/Online/DemoOnlineIdentity.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"

FString UDemoCoopLocalPlayer::GetGameLoginOptions() const
{
    DEMO_LOG_CALL();
    const UDemoCoopSessionSubsystem* Identity = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>() : nullptr; // GI身份跨旅行稳定，不缓存旧World/PC。
    if (!Identity) { UE_LOG(LogFPSDemo, Warning, TEXT("COOP login identity unavailable")); return Super::GetGameLoginOptions(); }
    const UDemoOnlineIdentity* Online = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>(); // 首次握手时旧World可能仍是Standalone，不能借其NetMode选择恢复键。
    const FString Key = Online->IsPublic() ? Online->GetOnlinePlayerKey() : Identity->GetLocalPlayerKey(); // Public只发送已登录PUID；服务器仍从P2P源独立推导。
    return Super::GetGameLoginOptions() + TEXT("?DemoPlayerKey=") + Key
        + FString::Printf(TEXT("?%s=%s"), DemoCoopProtocol::LoginOption, DemoCoopProtocol::Version); // 统一版本源，不能只升级搜索广播而让实际登录继续携带旧值。
}
FUniqueNetIdRepl UDemoCoopLocalPlayer::GetPreferredUniqueNetId() const
{
    DEMO_LOG_TICK();
    const UDemoOnlineIdentity* Identity = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>() : nullptr; // GI可能在引擎构造LocalPlayer时尚未初始化。
    if (Identity && Identity->IsPublic()) return FUniqueNetIdRepl(Identity->GetLocalNetId());
    IOnlineSubsystem* LAN = GetWorld() ? Online::GetSubsystem(GetWorld(), FName(TEXT("NULL"))) : nullptr; // 避免注销后复用旧EOS ID。
    return LAN && LAN->GetIdentityInterface() ? FUniqueNetIdRepl(LAN->GetIdentityInterface()->GetUniquePlayerId(0)) : FUniqueNetIdRepl();
}
