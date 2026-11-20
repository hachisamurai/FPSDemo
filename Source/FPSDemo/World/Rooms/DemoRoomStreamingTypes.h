// 职责：房间流送的有界值请求。服务器从作者白名单构造，客户端只执行同代次请求；不包含玩家进度或活动Actor引用。
#pragma once
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "DemoRoomStreamingTypes.generated.h"

/** PollPrepared的三态结果；Pending必须保留原房间，Failed由上层取消本次出发。 */
enum class EDemoRoomLoadStatus : uint8 { Pending, Ready, Failed };

/** 同一次准备在所有端使用相同名字和变换；Generation仅在本World有效，禁止客户端自选资源。 */
USTRUCT()
struct FPSDEMO_API FDemoRoomStreamRequest
{
    GENERATED_BODY()
    UPROPERTY() FName RoomId; // 作者稳定房间ID，不是任意地图路径。
    UPROPERTY() FSoftObjectPath Map; // 白名单中的/Game地图对象路径。
    UPROPERTY() FName InstanceName; // 服务端分配的唯一包名后缀，各端完全一致。
    UPROPERTY() FTransform Transform; // 本次两个隔离空间槽之一，单位cm，缩放固定1。
    UPROPERTY() int32 Generation = 0; // 正数准备代次；旧回调不得提交新请求。
    UPROPERTY() int32 ArenaIndex = INDEX_NONE; // 原十关规则的0..2区域索引，仅作为兼容查询键。
    UPROPERTY() bool bRequireNavigation = true; // 只在服务器检查AI导航，客户端检查复制令牌和碰撞。
    UPROPERTY() TArray<FName> RequiredActorTags; // 最多16个唯一标签；每个必须对应本子关卡唯一已BeginPlay Actor。
};
