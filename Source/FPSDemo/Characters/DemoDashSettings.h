// 职责：配置Lyra式RootMotionSource闪避推进窗口；DefaultGame.ini统一各端参数，GAS继续管理成本、冷却和免伤。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoDashSettings.generated.h"

/** 项目级移动配置；不是玩家成长或存档，速度仍取角色权威DashSpeed及遗物修正。 */
UCLASS(Config=Game,DefaultConfig)
class FPSDEMO_API UDemoDashSettings : public UObject
{
    GENERATED_BODY()
public:
    // 恒定水平推进秒数；运行读取时钳制[0.1,0.5]，默认0.30秒，动画恢复和0.45秒免伤独立。
    UPROPERTY(Config,EditAnywhere,Category="Dash",meta=(ClampMin="0.1",ClampMax="0.5")) float ForceSeconds=.30f;
};
