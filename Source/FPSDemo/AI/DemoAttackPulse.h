#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoAttackPulse.generated.h"
class UStaticMeshComponent;

/** 从Boss释放位置扩散的纯表现冲击壳，伤害已在释放时结算；没有碰撞或重复伤害。 */
UCLASS()
class FPSDEMO_API ADemoAttackPulse : public AActor
{
    GENERATED_BODY()
public:
    /** 创建使用红色发光材质的球壳，0.4秒内扩展覆盖当前战斗区域并自动销毁。 */
    ADemoAttackPulse();
    // 世界厘米半径，Boss保持9000；弹药GC可显式缩小为局部爆炸，纯表现不改变伤害范围。
    UPROPERTY(EditAnywhere,Category="VFX") float EndRadius = 9000.f;
    /** DeltaSeconds为世界帧间秒数，只更新外观，不参与伤害或躲避判断。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    // Actor持有的无碰撞球壳；仅本地单人表现，不声称实现联机特效广播。
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Shell;
    // 累计世界秒数，暂停时不增长；寿命与视觉曲线保持一致。
    float Age = 0.f;
};
