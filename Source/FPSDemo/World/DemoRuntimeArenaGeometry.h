#pragma once
#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "DemoRuntimeArenaGeometry.generated.h"

/** 白模地图缺失时的网络地板/墙体；服务端生成一次，客户端接收相同碰撞几何。 */
UCLASS()
class FPSDEMO_API ADemoRuntimeArenaGeometry : public AStaticMeshActor
{
    GENERATED_BODY()
public:
    /** 双端CDO预设同一Cube/碰撞与Movable，避免普通StaticMeshActor在客户端不显示动态赋网格。 */
    ADemoRuntimeArenaGeometry();
};
