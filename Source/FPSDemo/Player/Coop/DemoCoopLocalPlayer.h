// 职责：合作加入URL与本地玩家凭据。使用：加入时附带兼容协议号，不能由旧客户端绕过服务器校验。
#pragma once
#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "DemoCoopLocalPlayer.generated.h"

/** 每次网络登录（包括非无缝服务器旅行）都附带本机稳定身份，避免只在首次Join URL中注入。 */
UCLASS()
class FPSDEMO_API UDemoCoopLocalPlayer : public ULocalPlayer
{
    GENERATED_BODY()
public:
    /** 引擎构造登录报文时读取；不保存角色属性，不把设备GUID当作公网认证。 */
    virtual FString GetGameLoginOptions() const override;
    /** 按选定提供者取身份，不复用上一次LAN/EOS旅行留下的CachedUniqueId。 */
    virtual FUniqueNetIdRepl GetPreferredUniqueNetId() const override;
};
