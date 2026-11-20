// 职责：队伍出发准备的公开复制值；不含钱包、固定身份键或存档数据，生命周期仅当前World。
#pragma once
#include "CoreMinimal.h"
#include "DemoTeamReadyTypes.generated.h"
class ADemoPlayerState;

/** 一名在线成员的公开准备投影；PS用于本地辨认本人，显示名不参与授权。 */
USTRUCT()
struct FPSDEMO_API FDemoTeamReadyMember
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<ADemoPlayerState> Player = nullptr; // 当前World身份，Pawn替换不继承准备。
    UPROPERTY() FString Name; // 最多32字符的公开显示名，不包含账号键。
    UPROPERTY() FName CoreId; // 当前核心稳定ID，None代表旧模式或尚未选择。
    UPROPERTY() bool bReady = false; // 服务端接受的明确准备意图。
    UPROPERTY() FString BlockReason; // 空表示当前可以准备；Ready仍由独立字段表示。
};

/** GS广播的整队门禁，Revision防止旧页面越过换人/切模式后的重新确认。 */
USTRUCT()
struct FPSDEMO_API FDemoTeamReadySnapshot
{
    GENERATED_BODY()
    UPROPERTY() int32 Revision = 0; // World内单调意图版本，不写存档；每次明确状态变更递增。
    UPROPERTY() TArray<FDemoTeamReadyMember> Members; // 至多4位在线登记成员，离线固定成员不阻塞出发。
    UPROPERTY() bool bRequired = false; // 两人及以上需要显式准备，单人保留原直接出发。
    UPROPERTY() bool bCanDepart = false; // 展示提示，服务端出发瞬间仍全量重验。
    UPROPERTY() FString BlockReason; // 第一个阻塞成员及原因；空表示队伍门禁通过。
    UPROPERTY() FString LastChangeReason; // 本次撤销原因，例如装备变化或新成员加入。
};
