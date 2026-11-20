#pragma once
#include "CoreMinimal.h"
#include "NetDriverEOSBase.h"
#include "DemoCoopNetDriver.generated.h"

/** 同一打包进程可选LAN或EOS；Listen初始化按GI选择传输，不改变全局OSS。 */
UCLASS(Transient, Config=Engine)
class FPSDEMO_API UDemoCoopNetDriver : public UNetDriverEOSBase
{
    GENERATED_BODY()
public:
    /** 始终由本驱动执行选择；LAN由父类IP passthrough处理，避免全局驱动热切换。 */
    virtual bool IsAvailable() const override;
    /** InNotify/URL为引擎连接参数，Error只返回固定诊断；公网身份未就绪则拒绝连接。 */
    virtual bool InitConnect(FNetworkNotify* InNotify, const FURL& URL, FString& Error) override;
    /** URL为本地Listen选项；LAN添加bUseIPSockets，Public拒绝任何IP回退。 */
    virtual bool InitListen(FNetworkNotify* InNotify, FURL& URL, bool bReuseAddressAndPort, FString& Error) override;
};
