#include "Player/Online/DemoCoopNetDriver.h"
#include "Player/Online/DemoOnlineIdentity.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

bool UDemoCoopNetDriver::IsAvailable() const { DEMO_LOG_CALL(); return true; }
bool UDemoCoopNetDriver::InitConnect(FNetworkNotify* InNotify, const FURL& URL, FString& Error)
{
    DEMO_LOG_CALL();
    UWorld* DriverWorld = FindWorld(); // 引擎在旅行期设置的当前World，不能缓存上一个地图指针。
    const UDemoOnlineIdentity* Identity = DriverWorld && DriverWorld->GetGameInstance() ? DriverWorld->GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>() : nullptr; // GI身份跨地图存活。
    if (URL.Host.StartsWith(TEXT("EOS:")))
    {
        if (!Identity || !Identity->IsPublic() || !Identity->IsReady() || !UNetDriverEOSBase::IsAvailable() || !UDemoOnlineIdentity::IsEOSAddress(URL.Host))
        { Error = TEXT("EOS identity or transport unavailable"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS_CONNECT rejected identity/transport/address")); return false; }
        bIsUsingP2PSockets = true;
    }
    else if (Identity && Identity->IsPublic())
    { Error = TEXT("Public rooms require EOS P2P transport"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS_CONNECT IP fallback rejected")); return false; }
    else bIsUsingP2PSockets = false; // LAN甚至在后台存在EOS账号时也只使用IP。
    return Super::InitConnect(InNotify, URL, Error);
}
bool UDemoCoopNetDriver::InitListen(FNetworkNotify* InNotify, FURL& URL, bool bReuseAddressAndPort, FString& Error)
{
    DEMO_LOG_CALL();
    UWorld* DriverWorld = FindWorld(); // 本次Listen DriverWorld；GI可在OpenLevel前已经选择Public。
    const UDemoOnlineIdentity* Identity = DriverWorld && DriverWorld->GetGameInstance() ? DriverWorld->GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>() : nullptr; // 缺GI时按本地工具/编辑器LAN处理。
    if (Identity && Identity->IsPublic())
    {
        if (!Identity->IsReady() || !UNetDriverEOSBase::IsAvailable() || URL.HasOption(TEXT("bUseIPSockets")) || URL.HasOption(TEXT("bIsLanMatch")))
        { Error = TEXT("EOS listen identity or transport unavailable"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS_LISTEN rejected identity/transport/options")); return false; }
        bIsUsingP2PSockets = true;
    }
    else { bIsUsingP2PSockets = false; URL.AddOption(TEXT("bUseIPSockets")); }
    return Super::InitListen(InNotify, URL, bReuseAddressAndPort, Error);
}
