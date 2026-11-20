// 职责：终端成对摆放的同步物理查询；TerminalComponent提供服务器纯值胜利快照，不移动玩家/导航点或创建Actor。
// 使用：仅创建终端时调用ResolvePair；复用所属Level地面、实际Box/玩家胶囊及完整Nav，不持有World/Actor跨帧引用。
#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FDemoAreaSnapshot;

/** 复活前冻结的服务器参战生存者值；不含客户端Camera或持久化/网络对象。 */
struct FDemoTerminalPlacementOrigin
{
    FVector Location=FVector::ZeroVector; // 胜利时实际胶囊中心，世界cm。
    FVector Forward=FVector::ForwardVector; // 实际Actor水平朝向，只决定候选优先顺序。
    float CapsuleRadius=34.f; // 实际玩家胶囊半径cm，仅用于通路查询。
    float CapsuleHalfHeight=98.f; // 实际玩家胶囊半高cm，不套用R120导航代理代替玩家体型。
    float MaxStepHeight=45.f; // 实际CMC最大台阶cm；物理备用通路仍不跨越断层。
    FString PlayerKey; // 当前Roster稳定身份，日志/排序使用，不进入存档。
    bool bLeader=false; // 权威本机房主优先；已阵亡房主不会进入此数组。
};

/** 已通过同步预检的两个精确Actor中心；后续严格Spawn，不允许引擎再挪走已验点。 */
struct FDemoTerminalPlacementPair
{
    FVector Shop=FVector::ZeroVector; // 升级终端Box中心世界cm。
    FVector Next=FVector::ZeroVector; // 下一关终端Box中心世界cm。
    FString PlayerKey; // 近身方案采用的胜利生存者；作者路径为空。
    int32 Attempts=0; // 本次有界候选数，仅汇总日志。
    float Yaw=0.f; // 两终端共用的Actor Yaw（度，0/90/180/270）；清关时让模型操作面（Actor −X）朝向清关位置，作者路径保持0。
};

namespace DemoTerminalPlacement
{
    inline constexpr float ContactLift=2.f; // 放置根碰撞体时的离地余量（cm）；地面枢轴的展示网格需减去同一值。
    /** World/Area为同次同步借用，HalfExtent来自真实终端CDO；Origins非空仅用于清关，否则保作者锚点附近的有界成对避障。 */
    bool ResolvePair(UWorld& World,const FDemoAreaSnapshot& Area,const FVector& HalfExtent,
        const TArray<FDemoTerminalPlacementOrigin>& Origins,FDemoTerminalPlacementPair& Out,FString& Error);
}
