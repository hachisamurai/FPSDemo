#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "DemoBossDiveVFX.generated.h"

/** 一次原子传输完整模式/位置，避免网络到达顺序导致短暂错误落点。 */
USTRUCT()
struct FDemoDiveVisualSnapshot
{
    GENERATED_BODY()
    UPROPERTY() int32 Mode=0; // 0隐藏、1蓄力、2预警、3冲击，只表达视觉。
    UPROPERTY() FVector_NetQuantize Location=FVector::ZeroVector; // 世界厘米，Mode切换时服务器锁定。
    UPROPERTY() float Radius=300.f; // 地面圈半径厘米，不参与客户端伤害。
    UPROPERTY() float StartedAt=0.f; // 服务器World秒，网络到达延迟不延长前摇视觉。
};

/** 蓝色悬停扩散壳/落点圈/冲击圈，仅表现，绝不参与伤害或导航。 */
UCLASS()
class FPSDEMO_API ADemoBossDiveVFX : public AActor
{
    GENERATED_BODY()
public:
    /** 创建三个错峰扩散壳和独立落点标记，硬引用Editor创建材质供Cook。 */
    ADemoBossDiveVFX();
    /** OutLifetimeProps注册纯表现状态；此Actor不复制或执行GAS能力。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    /** Center为悬停中心cm；只在Hovering开始调用，3秒内连续向外扩散。 */
    void ShowCharge(const FVector& Center);
    /** Center为已锁定Boss落点中心cm，Radius为伤害半径cm；在俯冲开始显示。 */
    void ShowTarget(const FVector& Center,float Radius);
    /** 落地后由标记扩大产生一次冲击反馈，具体伤害由GA结算。 */
    void ShowImpact();
    /** DeltaSeconds为World帧秒；暂停时扩散同步冻结。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** 复制模式/位置后应用本机组件状态；服务器也直接调用，视觉路径唯一。 */
    UFUNCTION() void OnRep_VisualSnapshot();
    /** 写入Mode/Radius/位置及时间后提交快照，不发送伤害指令。 */
    void PublishVisualSnapshot();
    UPROPERTY(ReplicatedUsing=OnRep_VisualSnapshot) FDemoDiveVisualSnapshot VisualSnapshot; // 支持当前相关客户端及迟到复制。
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Shells; // Actor强持有三层纯表现球壳。
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Marker; // 展平球壳形成地面边缘，不依赖DrawDebug。
    UPROPERTY() TObjectPtr<class UPointLightComponent> Light; // 蓝色环境光，只在蓄力期间开启。
    float Age=0.f; // 当前表现阶段秒数，改变模式归零。
    float TargetRadius=300.f; // 落点预警与实际半径相同，单位cm。
    int32 Mode=0; // 本机已应用模式；权威来源为VisualSnapshot，不允许客户端驱动技能。
};
