// 职责：转移控制目标的可复制白模标记。由协调器更新纯表现值，地面圆盘不参与占区碰撞/伤害；客户端不能决定目标进度。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/Objectives/DemoObjectiveTypes.h"
#include "DemoControlPoint.generated.h"
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

UCLASS()
class FPSDEMO_API ADemoControlPoint : public AActor
{
    GENERATED_BODY()
public:
    /** 创建引擎圆柱与文字占位，无Tick/碰撞/导航障碍，正式美术可替换资产但不能接管规则。 */
    ADemoControlPoint();
    /** 生成后用服务器已复制的状态初始化材质，晚加入客户端也能正确显示。 */
    virtual void BeginPlay() override;
    /** Index为1..3顺序，Radius单位cm；仅协调器服务器调用一次。 */
    void Configure(int32 Index, float Radius);
    /** Status/Seconds为服务器状态与剩余读条秒，Completed决定已完成绿色显示。 */
    void UpdatePresentation(EDemoControlStatus Status, float Seconds, bool bCompleted);
    /** OutLifetimeProps由引擎填写；状态只复制显示，不复制玩家/敌人容器。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    /** 复制到达后更新文字/颜色/尺寸；缺少可选材质仍保留文字标识。 */
    UFUNCTION() void OnRep_State();
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Disc; // 无碰撞地面范围占位，Actor拥有。
    UPROPERTY() TObjectPtr<UTextRenderComponent> Label; // 当前点序号/状态提示。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material; // 本Actor私有MID，不能改共享材质。
    UPROPERTY(ReplicatedUsing=OnRep_State) int32 Number = 1; // 公开顺序1..3。
    UPROPERTY(ReplicatedUsing=OnRep_State) float AreaRadius = 450.f; // 与服务器占区完全相同的厘米半径。
    UPROPERTY(ReplicatedUsing=OnRep_State) EDemoControlStatus State = EDemoControlStatus::Inactive; // 当前激活/占区状态。
    UPROPERTY(ReplicatedUsing=OnRep_State) float RemainingSeconds = 0.f; // 十分之一秒量化的剩余值，最多4Hz更新。
    UPROPERTY(ReplicatedUsing=OnRep_State) bool bDone = false; // 已完成点只显示，不可再次计时。
};
