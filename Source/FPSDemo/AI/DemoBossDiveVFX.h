#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoBossDiveVFX.generated.h"

/** 蓝色悬停扩散壳/落点圈/冲击圈，仅表现，绝不参与伤害或导航。 */
UCLASS()
class FPSDEMO_API ADemoBossDiveVFX : public AActor
{
    GENERATED_BODY()
public:
    /** 创建三个错峰扩散壳和独立落点标记，硬引用Editor创建材质供Cook。 */
    ADemoBossDiveVFX();
    /** Center为悬停中心cm；只在Hovering开始调用，3秒内连续向外扩散。 */
    void ShowCharge(const FVector& Center);
    /** Center为已锁定Boss落点中心cm，Radius为伤害半径cm；在俯冲开始显示。 */
    void ShowTarget(const FVector& Center,float Radius);
    /** 落地后由标记扩大产生一次冲击反馈，具体伤害由GA结算。 */
    void ShowImpact();
    /** DeltaSeconds为World帧秒；暂停时扩散同步冻结。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Shells; // Actor强持有三层纯表现球壳。
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Marker; // 展平球壳形成地面边缘，不依赖DrawDebug。
    UPROPERTY() TObjectPtr<class UPointLightComponent> Light; // 蓝色环境光，只在蓄力期间开启。
    float Age=0.f; // 当前表现阶段秒数，改变模式归零。
    float TargetRadius=300.f; // 落点预警与实际半径相同，单位cm。
    int32 Mode=0; // 0隐藏、1蓄力、2预警、3落地波；不复制，当前单人表现。
};
