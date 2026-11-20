// 职责：本人受击的最终视图Roll叠加器。使用：由PC的受击组件安装，逐帧只读组件包络，不拥有伤害或计时状态。
// 依赖：原CameraManager与本机受击组件；不改变输入、ControlRotation、位置、Pitch/Yaw、FOV或武器后坐力。
#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "DemoDamageCameraModifier.generated.h"

UCLASS()
class FPSDEMO_API UDemoDamageCameraModifier : public UCameraModifier
{
    GENERATED_BODY()
public:
    /** 创建非独占Modifier；生命周期由本人PC受击组件管理，不创建Tick或Timer。 */
    UDemoDamageCameraModifier();
    /** CameraManager求最终视图时调用；DeltaTime为本帧秒，InOutPOV只允许叠加±0.4度Roll，返回false保留后续Modifier。 */
    virtual bool ModifyCamera(float DeltaTime,FMinimalViewInfo& InOutPOV) override;
};
