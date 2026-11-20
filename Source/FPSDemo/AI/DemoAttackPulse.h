// 职责：GC/Boss攻击的无伤害可复制球壳表现。使用：设置EndRadius与Tint，World在短寿命内扩展并回收；不得用于命中判定。
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
    /** OutLifetimeProps只复制最终视觉半径，实际伤害由服务器释放逻辑处理。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    // 世界厘米半径，Boss保持9000；弹药GC可显式缩小为局部爆炸，纯表现不改变伤害范围。
    UPROPERTY(EditAnywhere,Replicated,Category="VFX") float EndRadius = 9000.f;
    /** DeltaSeconds为世界帧间秒数，只更新外观，不参与伤害或躲避判断。 */
    virtual void Tick(float DeltaSeconds) override;
    /** Color为仅表现的线性RGB；服务器设置后复制给相关客户端，GC本地实例也可使用。 */
    void SetTint(FLinearColor Color);
private:
    /** RepNotify刷新本Actor的动态材质；只改显示，不改变攻击归属或范围。 */
    UFUNCTION() void OnRep_Tint();
    /** 默认红色保持Boss旧表现；冰爆GC设置为蓝色。 */
    UPROPERTY(ReplicatedUsing=OnRep_Tint) FLinearColor Tint=FLinearColor(1.f,.04f,.02f);
    // Actor持有的无碰撞球壳；服务器产生复制Actor，客户端只有缩放表现。
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Shell;
    /** Editor创建的可变色材质硬引用；只有SetTint/复制颜色回调替换，原Boss红光保持旧材质。 */
    UPROPERTY() TObjectPtr<class UMaterialInterface> TintMaterial;
    // 累计世界秒数，暂停时不增长；寿命与视觉曲线保持一致。
    float Age = 0.f;
};
