// 职责：金属命中表现预算与资源；使用DA_MetalImpact配置，由World池CDO硬引用进入Cook。依赖引擎网格/材质/空间音效，不决定伤害。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoMetalImpactSettings.generated.h"

UCLASS(BlueprintType)
class FPSDEMO_API UDemoMetalImpactSettings : public UDataAsset
{
    GENERATED_BODY()
public:
    /** CDO/资产实例构造只埋点；预算由本地World初始化时冻结，运行中不重建池。 */
    UDemoMetalImpactSettings();
    // 全场火花簇并发，1..32；每簇至多12根火花及一个瞬闪，预分配后不增长。
    UPROPERTY(EditAnywhere, Category="Budget", meta=(ClampMin="1",ClampMax="32")) int32 ImpactSlots = 24;
    // 保留给近战的槽位；枪弹不能占用，避免高射速掩盖近战确认。
    UPROPERTY(EditAnywhere, Category="Budget", meta=(ClampMin="0",ClampMax="8")) int32 MeleeReservedSlots = 4;
    // 全场服务器弹击事件每秒补充量；突发桶容量8，限制RPC和远端解码工作。
    UPROPERTY(EditAnywhere, Category="Budget", meta=(ClampMin="1",ClampMax="120")) float BulletEventsPerSecond = 48.f;
    // 单目标连续弹击最短间隔秒；同枪同目标无论间隔仍只出现一簇。
    UPROPERTY(EditAnywhere, Category="Budget", meta=(ClampMin="0.02",ClampMax="0.5")) float TargetInterval = .06f;
    // 最大可见/可听距离cm；中距离按该值的32%/64%降低火花数，不影响真实伤害。
    UPROPERTY(EditAnywhere, Category="Budget", meta=(ClampMin="100",ClampMax="10000")) float MaxDistance = 2500.f;
    // 复用音频组件数量1..8；声音全部忙时丢弃新声音，不能创建临时组件。
    UPROPERTY(EditAnywhere, Category="Budget", meta=(ClampMin="1",ClampMax="8")) int32 AudioVoices = 6;
    // 以下资源强引用进入Cook；使用引擎100cm Cube/Sphere，仅渲染，无碰撞/阴影/导航。
    UPROPERTY(EditAnywhere, Category="Resources") TObjectPtr<class UStaticMesh> SparkMesh;
    UPROPERTY(EditAnywhere, Category="Resources") TObjectPtr<class UStaticMesh> FlashMesh;
    // Additive Unlit材质，PerInstanceCustomData[0]控制寿命；不使用动态灯。
    UPROPERTY(EditAnywhere, Category="Resources") TObjectPtr<class UMaterialInterface> SparkMaterial;
    UPROPERTY(EditAnywhere, Category="Resources") TObjectPtr<class UMaterialInterface> FlashMaterial;
    // 三变体原创金属撞击Cue，自带空间衰减；近战/设施枪弹实际扣血后共用，字段名保留以兼容已保存DA。
    UPROPERTY(EditAnywhere, Category="Resources") TObjectPtr<class USoundBase> MeleeSound;
};
