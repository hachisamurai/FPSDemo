#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "DemoCoopSessionSubsystem.generated.h"

/** 本机 LAN 身份键，不是账号认证凭据，也不携带玩家经济数据。 */
UCLASS()
class UDemoCoopIdentitySave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() FString PlayerKey; // 本机生成 GUID，正式游戏跨进程保留以匹配房主固定队伍。
};

/** 本机连接意图和身份；队伍战局仍由房主 World/检查点负责。 */
UCLASS()
class FPSDEMO_API UDemoCoopSessionSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    /** Collection 为引擎 GI 容器；Editor 仅生成本 GI 临时键，不读写正式身份。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** LAN返回本机GUID；Public返回已认证PUID关联键，身份未就绪时为空，绝不退回设备GUID。 */
    FString GetLocalPlayerKey() const;
    /** 当前单人安全区转 Listen Server；先把活动槽标记恢复，最多四人由服务器校验。 */
    bool HostCoop();
    /** Address为LAN主机地址或EOS会话解析地址；按已选择提供者校验，不接受旅行options。 */
    bool JoinCoop(const FString& Address);
private:
    FString LocalPlayerKey; // 无联网认证含义，只用于可信 LAN Demo 的固定队伍关联。
};
