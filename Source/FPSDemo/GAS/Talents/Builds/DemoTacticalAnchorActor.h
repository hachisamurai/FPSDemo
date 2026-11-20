// 职责：B06战术锚点的有界公开表现。使用：服务器策略配置/发布状态；依赖引擎基础网格和现有M_Solid，不拥有充能、计时或伤害规则。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoTacticalAnchorActor.generated.h"

class ADemoPlayerState;
class UDemoAnchorBuildRuntime;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UCLASS()
class FPSDEMO_API ADemoTacticalAnchorActor : public AActor
{
    GENERATED_BODY()
public:
    /** 创建无碰撞/导航影响的地面范围和充能柱；不启用Actor Tick或客户端游戏规则。 */
    ADemoTacticalAnchorActor();
    /** 复制初始化后创建每Actor独立MID；资源缺失只降级表现，不决定技能命中。 */
    virtual void BeginPlay() override;
    /** Runtime仅服务器弱路由；OwnerPS公开识别主人，Radius厘米，EndsAt服务器World秒。仅合法部署后调用。 */
    void Configure(UDemoAnchorBuildRuntime* Runtime, ADemoPlayerState* OwnerPS, float Radius, float EndsAt);
    /** 只复制公开的充能档位；Count限定0..Threshold，Threshold至少1，Ready来自服务器策略。 */
    void SetCharge(int32 Count, int32 Threshold, bool bReady);
    /** 撤销服务器路由并短暂显示脉冲颜色；0.25秒寿命仅为表现退休，不再产生任何游戏事件。 */
    void PulseAndRetire();
    /** 权威直击串行通知使用的弱路由；退休、销毁或客户端返回空，不延长策略生命周期。 */
    UDemoAnchorBuildRuntime* GetRuntime() const;
    /** 引擎复制入口；仅公开表现数据，策略、经济、计时器和目标容器不复制。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    /** 属性到达和Listen Server本地状态变化时同步外观；无碰撞/无伤害，不按本机时间自行判定到期。 */
    UFUNCTION() void OnRep_Presentation();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Disc; // 世界地面圆盘，Actor拥有，不挡子弹或导航。
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ChargeColumn; // 无朝向文字的三维充能柱，任意视角均可见。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DiscMaterial; // 本Actor圆盘私有MID，不修改共享材质。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ColumnMaterial; // 本Actor充能柱私有MID。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) TObjectPtr<ADemoPlayerState> AnchorOwner; // 公开主人身份，非网络所有权/经济数据。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) float AreaRadius = 350.f; // 服务器规则对应的厘米半径，表现不能扩大判定。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) float EndServerTime = 0.f; // 公开结束时刻，仅供表现/观察，不在Actor内运行规则。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) int32 ChargeCount = 0; // 公开实际充能0..ChargeCapacity。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) int32 ChargeCapacity = 4; // 本次锚点阈值，不随客户端配置变化。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bCharged = false; // 充满提示，由策略确认。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bPulse = false; // 短暂退休表现；此时Runtime已断开，绝不再参与充能。
    TWeakObjectPtr<UDemoAnchorBuildRuntime> ServerRuntime; // 仅服务器弱路由，由ASC拥有的策略保活，不复制、不保存。
};
