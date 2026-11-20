// 职责：独立TP枪械机械动画缓存。薄AnimBP读取身体已选动作与同一服务器相位；不结算弹药，不创建额外时钟。
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DemoPlayerWeaponAnimInstance.generated.h"

class UAnimSequence;
class ADemoCharacter;

/** 第三人称机械枪薄图的只读缓存；与全身上身使用同一phase和过渡权重，无武器玩法所有权。 */
UCLASS(Transient, Blueprintable)
class FPSDEMO_API UDemoPlayerWeaponAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** 实例创建/换枪时重取Pawn，清空跨Skeleton源资源。 */
    virtual void NativeInitializeAnimation() override;
    /** DeltaSeconds由Presentation手动零步求值；只复制本帧身体缓存，不独立累计时间。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    // 当前机械动作，源自本枪现有动画配置；图求值不访问Actor/CDO。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") TObjectPtr<UAnimSequence> CurrentSequence;
    // 本帧当前机械动作源秒数；服务器换弹归一化phase映射而来。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float CurrentTime = 0.f;
    // 取消/动作切换前冻结的机械动作；只在同枪过渡中使用。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") TObjectPtr<UAnimSequence> PreviousSequence;
    // 与身体冻结上身姿态对应的旧机械序列时刻，过渡期间不推进。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float PreviousTime = 0.f;
    // 与身体共享的0..1混合权重，1表示新动作；不能单独平滑枪而让双手跳变。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float BlendAlpha = 1.f;
private:
    // Owner是创建展示组件的Pawn；失效后不保活旧World。
    TWeakObjectPtr<ADemoCharacter> Character;
};
