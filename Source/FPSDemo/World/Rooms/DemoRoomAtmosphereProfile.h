// 职责：房间本机氛围数据及受限PP白名单；由StreamingSettings选择、Anchor消费，不复制、不写存档。
// 使用：创建DataAsset，Overrides仅支持文档列出的字段；依赖UE5.4公开Scene/LocalFogVolume接口，无全局CVar。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Scene.h"
#include "DemoRoomAtmosphereProfile.generated.h"

/** 一个均匀缩放的球形雾域，必须完整包含于本房间几何盒；纯本地数据，不创建全局高度雾。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoRoomLocalFogSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog") bool bEnabled = false; // 默认无雾；艺术标定后显式启用。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog") FVector CenterOffsetCm = FVector::ZeroVector; // 相对房间几何AABB中心的世界轴偏移cm，不是锚点局部轴。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog", meta=(ClampMin="10", ClampMax="3000", Units="cm")) float RadiusCm = 300.f; // 球半径cm；运行时还核完整包含，不夹值掩盖越界。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog", meta=(ClampMin="0", ClampMax="0.2")) float RadialExtinction = 0.02f; // UE局部球规范化径向消光系数，非全局FogDensity。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog", meta=(ClampMin="0", ClampMax="0.2")) float HeightExtinction = 0.f; // 规范化高度消光，默认关闭底部积雾。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog", meta=(ClampMin="1", ClampMax="5000")) float HeightFalloff = 1000.f; // UE规范化高度衰减，至少1避免地平线伪影。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog", meta=(ClampMin="-2", ClampMax="2")) float HeightOffset = 0.f; // 相对球中心的规范化高度偏移，无世界单位。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog", meta=(ClampMin="0", ClampMax="0.9")) float PhaseG = 0.2f; // 散射方向性，不承担玩法预警。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog") FLinearColor Albedo = FLinearColor(0.7f,0.75f,0.8f); // 线性反照率RGB [0,1]；无发光以免抬白室内。
};

/** LIGHTING-REFACTOR-20261119/RENDER：严格有界的艺术覆写；不允许任意Blendables/渲染方法/描边字段。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoRoomAtmosphereProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 初始化兼容旧曝光与克制后处理候选；不读取World或加载其他资产。 */
    UDemoRoomAtmosphereProfile();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Atmosphere") bool bEnabled = true; // 关闭为显式不应用，不回退其他Profile。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Atmosphere") FPostProcessSettings Overrides; // UE原生override勾选语义；运行只接收BuildSettings白名单，其他勾选拒绝。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds", meta=(ClampMin="0", ClampMax="2000", Units="cm")) float BoundsMarginCm = 300.f; // 几何盒扩展cm，连同混合带须通过槽位隔离。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds", meta=(ClampMin="0", ClampMax="2000", Units="cm")) float BlendRadiusCm = 300.f; // 有界PP的盒外混合带cm，不影响雾半径。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds", meta=(ClampMin="0", ClampMax="9")) float Priority = 1.f; // 低于Hub 10；范围隔离为主要门禁。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fog") FDemoRoomLocalFogSettings LocalFog; // 每Anchor至多一个Transient球，跨区域分屏保守停雾。
    /** Anchor创建前调用；验证所有启用字段、有限数和艺术上限，成功输出全新白名单PP，失败不输出部分配置。 */
    bool BuildSettings(FPostProcessSettings& Out, FString& Error) const;
    /** Hub只在GT用BuildSettings成功输出认领启用字段；不复制非白名单/Blendables，Target其他字段完全保留。 */
    static bool ApplyOwnedOverrides(const FPostProcessSettings& Validated, FPostProcessSettings& Target);
    /** Hub撤销认领字段：只在当前值/override仍等Applied时恢复Baseline，外部写入冲突保留且返回false。 */
    static bool RestoreOwnedOverrides(const FPostProcessSettings& Applied, const FPostProcessSettings& Baseline, FPostProcessSettings& Target);
};
