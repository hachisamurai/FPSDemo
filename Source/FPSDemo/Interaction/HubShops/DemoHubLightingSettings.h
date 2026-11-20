// 职责：HUB-INTERIOR-EXPOSURE-20261119 安全区枪械库/赌场室内本机曝光补偿配置。ADemoHubShopBlockout重建时各端读取，
// 按DA尺寸生成两店Bias组件；Hub私有消费者另在原Hub有界Volume上认领白名单字段，纯视觉、不复制、不存档。
// 使用：DefaultGame.ini [/Script/FPSDemo.DemoHubLightingSettings] 或 Project Settings → Demo Hub Lighting。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DemoHubLightingSettings.generated.h"

/** 室内曝光补偿档；只覆盖AutoExposureBias，其余曝光范围/速度/Bloom沿用Hub已接受的混合结果。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Demo Hub Lighting"))
class FPSDEMO_API UDemoHubLightingSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Hub Atmosphere") TSoftObjectPtr<class UDemoRoomAtmosphereProfile> HubAtmosphereProfile; // 空保原Hub；正式包路径由LIGHT/RENDER单一集成，不默认套房间档。
    UPROPERTY(Config, EditAnywhere, Category="Hub Atmosphere") FName HubVolumeObjectName; // G0实读原PersistentLevel PP对象短名，非label；未配置明确REJECT，不猜第一个Volume。
    UPROPERTY(Config, EditAnywhere, Category="Hub Atmosphere") FVector HubVolumeCenterCm = FVector(-5990,-200,10259.55); // 历史Brush世界中心cm；集成前核G0同版，运行容差0.05cm。
    UPROPERTY(Config, EditAnywhere, Category="Hub Atmosphere") FVector HubVolumeExtentCm = FVector(2530,2740,660.45); // 历史Brush世界AABB半径cm；漂移拒绝，不放大范围掩盖。
    UPROPERTY(Config, EditAnywhere, Category="Interior Exposure") bool bInteriorExposureEnabled = true; // false＝两店不生成补偿组件，回到原Hub观感。
    UPROPERTY(Config, EditAnywhere, Category="Interior Exposure", meta=(ClampMin="-5", ClampMax="5")) float GunShopExposureBias = 0.f; // 枪械库室内AutoExposureBias（EV，绝对值）；全局默认1，0＝亮度减半，越小越暗。
    UPROPERTY(Config, EditAnywhere, Category="Interior Exposure", meta=(ClampMin="-5", ClampMax="5")) float CasinoExposureBias = 0.f; // 赌场室内AutoExposureBias（EV，绝对值），同上。
    UPROPERTY(Config, EditAnywhere, Category="Interior Exposure", meta=(ClampMin="0", ClampMax="1000")) float BlendRadiusCm = 150.f; // 室内盒外混合带cm，出入门时平滑过渡；超过后室外完全不受影响。
    UPROPERTY(Config, EditAnywhere, Category="Interior Exposure") float Priority = 20.f; // 高于Hub SpeedDown Volume的10；两者覆盖字段不同，不互相抵消。
};
