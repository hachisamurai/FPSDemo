// 职责：天赋独立计时移速GE默认模板。用法：Spec设置Demo.Magnitude加法增益和持续秒数，可用Blueprint子类替换；不共享叠层时钟。
#pragma once
#include "GameplayEffect.h"
#include "DemoTalentMoveEffect.generated.h"
/** StackingType=None，每份GE独立到期；移速为1基线上的增量。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoTalentMoveEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 默认5秒；本次Spec覆盖持续时间，支持正向加速和负向减速。 */
    UDemoTalentMoveEffect();
};
