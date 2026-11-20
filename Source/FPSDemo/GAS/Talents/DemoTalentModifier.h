// 职责：无状态天赋数值/射击策略。用法：独立原生或蓝图派生类由Definition组合，CDO只读；依赖快照和GAS Spec，不拥有玩家状态。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayEffectTypes.h"
#include "GAS/Talents/DemoTalentModifiers.h"
#include "DemoTalentModifier.generated.h"

/** 纯计算扩展点；不得在共享CDO保存玩家、计时器、随机结果或修改World。 */
UCLASS(Abstract,Blueprintable,BlueprintType)
class FPSDEMO_API UDemoTalentModifier : public UObject
{
    GENERATED_BODY()
public:
    /** 获得/开火时同步聚合；Rank为正等级，Modifiers为本次值快照，不能保留引用。 */
    UFUNCTION(BlueprintNativeEvent,Category="Talent") void BuildModifiers(int32 Rank,UPARAM(ref) FDemoTalentModifiers& Modifiers) const;
    /** Rank为当前/预览等级；生成与本策略公式一致的说明，不读取玩家状态。 */
    UFUNCTION(BlueprintNativeEvent,Category="Talent") FText Describe(int32 Rank) const;
    /** 仅服务器获得/升阶时调用；Rank为账本等级，Spec为当前私有Spec，写SetByCaller而不施加GE。 */
    UFUNCTION(BlueprintNativeEvent,Category="Talent") void ConfigureEffect(int32 Rank,const FGameplayEffectSpecHandle& Spec) const;
};
