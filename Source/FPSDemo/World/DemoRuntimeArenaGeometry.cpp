#include "World/DemoRuntimeArenaGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"
ADemoRuntimeArenaGeometry::ADemoRuntimeArenaGeometry()
{
    DEMO_LOG_CALL();
    bReplicates = true; bAlwaysRelevant = true; SetReplicateMovement(true);
    GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    GetStaticMeshComponent()->SetIsReplicated(true); // 位置/缩放变化由UE组件复制，地形不会只存在于服务器。
    GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube")); // 已知引擎资源，由CDO在服务器和客户端共同保活。
    GetStaticMeshComponent()->SetStaticMesh(Cube.Object);
}
