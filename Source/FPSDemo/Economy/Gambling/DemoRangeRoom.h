// 职责：赏金靶场独立场景的空间与碰撞。使用：Range服务器创建并指定拥有者，与目标移动/金币结算解耦。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoRangeRoom.generated.h"
class UStaticMeshComponent;
UCLASS(Config=Game)
class FPSDEMO_API ADemoRangeRoom : public AActor
{
    GENERATED_BODY()
public:
    /** 默认子对象确保客户端生成同一套房间；不在构造阶段读取World。 */
    ADemoRangeRoom();
    /** 仅权威选择没有场景碰撞/其他房间的位置；Owner为进入靶场的控制器，失败返回空。 */
    static ADemoRangeRoom* Create(APlayerController* Owner);
    /** 场地内射击起点；HalfHeight为当前胶囊半高(cm)，不使用固定角色身高。 */
    FVector Entrance(float HalfHeight) const;
private:
    UPROPERTY(Config) FVector AreaOrigin=FVector(100000,0,1000); // 独立训练区起点，世界cm；项目地图扩大时调整，不能与战斗区重叠。
    UPROPERTY(Config) float RoomSpacing=4000.f; // 同World个人房间之间最小间距cm，运行时至少3000。
};
