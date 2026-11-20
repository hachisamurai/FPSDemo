// 职责：入场资源握手的有限请求与公开状态。使用GM生成、PC本地异步预载并回报代数；不含钱包、存档或客户端可选择的地图路径。
#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "World/Rooms/DemoRoomStreamingTypes.h"
#include "DemoEncounterPreparationTypes.generated.h"

/** 服务器生成的资源与可选房间请求；房间实例先在服务器准备，客户端只加载收到的同名实例。 */
USTRUCT()
struct FPSDEMO_API FDemoEncounterPreloadRequest
{
    GENERATED_BODY()
    UPROPERTY() int32 Generation = 0; // 当前World正代数，迟到完成必须匹配；不保存。
    UPROPERTY() int32 Stage = 0; // 目标关号，仅显示/诊断，不授权客户端推进。
    UPROPERTY() TArray<FSoftObjectPath> Resources; // 服务器从冻结生成组/装备取得，最多64个/Game资源入口；客户端不得上传路径。
    UPROPERTY() FVector_NetQuantize LandingPoint = FVector::ZeroVector; // 本人预期入场胶囊中心，世界cm；先确认本地地面碰撞到达再回报。
    UPROPERTY() FDemoRoomStreamRequest Room; // RoomId为空走原区域；其余仅由服务器白名单生成，客户端不可挑选地图。
};

/** GS公开准备状态；玩家出发意图仍由TeamReady持有，资源握手是另一道门禁。 */
USTRUCT()
struct FPSDEMO_API FDemoEncounterPreparationView
{
    GENERATED_BODY()
    UPROPERTY() int32 Generation = 0; // 与请求代数对应，用于UI刷新，不接受作为交易权限。
    UPROPERTY() int32 Stage = 0; // 准备进入的关卡。
    UPROPERTY() int32 ReadyPlayers = 0; // 已确认资源与落脚点的连接数。
    UPROPERTY() int32 TotalPlayers = 0; // 入场准备冻结的连接数1..4。
    UPROPERTY() bool bActive = false; // 等待或全部就绪待提交；失败/取消/进入Combat置false。
    UPROPERTY() bool bReadyToCommit = false; // 业务与引擎ACK均通过后true；保存失败可重试，人数满额本身不代表已可提交。
    UPROPERTY() FString Status; // 有限公开说明，不能放账号、磁盘路径或私有数据。
};
