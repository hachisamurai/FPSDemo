// 职责：天赋内容定义，与GA执行方式解耦。用法：Editor创建DataAsset并注册到Catalog，存档仅保存稳定ID/等级；依赖GAS与无状态策略。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayEffectTypes.h"
#include "ScalableFloat.h" // 定义中可配置等级曲线，头文件必须自包含，不能依赖Unity编译的间接包含。
#include "GAS/Talents/DemoTalentModifiers.h"
#include "DemoTalentDefinition.generated.h"
class UDemoTalentAbility;
class UDemoTalentModifier;
class UGameplayEffect;
class UTexture2D;

/** 一项成长可组合零到多个GA、GE和修正策略；定义资产只读共享，不保存运行句柄。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoTalentDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    /** 稳定存档/协议ID；覆盖内置项时不能改变，客户端不能提交资产路径。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent") FName TalentId;
    /** 权限由本项专属无限GE授予；必须预注册且在目录内唯一。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent") FGameplayTag TalentTag;
    /** 奖池/Tab使用的本地化名字，不参与权威判定。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent") FText DisplayName;
    /** 可选图标硬引用，Cook随定义资产收集；空值由现有UI采用文字展示。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent") TObjectPtr<UTexture2D> Icon;
    /** 可选固定说明；空时组合策略/GA自身的等级说明，防止UI重复实现数值公式。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent") FText Description;
    /** -1全部弹药；0普通/1火/2冰/3穿透，用于权威抽取池过滤。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent",meta=(ClampMin="-1",ClampMax="3")) int32 RequiredAmmo=-1;
    /** 按数组顺序合成的只读策略CDO；纯数值天赋无需填写Abilities。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent|Implementation") TArray<TSubclassOf<UDemoTalentModifier>> Modifiers;
    /** 真正需要执行生命周期的GA；服务器按项记录句柄，重复升级不重新授予。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent|Implementation") TArray<TSubclassOf<UDemoTalentAbility>> Abilities;
    /** 持有期间的无限、非叠层GE；持续/瞬时效果应由事件GA施加，避免重建重复触发收益。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent|Implementation") TArray<TSubclassOf<UGameplayEffect>> PassiveEffects;
    /** 被动GE的SetByCaller等级曲线；每个私有Spec独立取值，策略可继续覆盖以兼容旧INI。 */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Talent|Implementation") TMap<FGameplayTag,FScalableFloat> EffectMagnitudes;

    /** Catalog加载时验证身份和实现引用；返回false时回退内置定义/拒绝附加项，不部分授予。 */
    UFUNCTION(BlueprintCallable,Category="Talent") bool Validate() const;
    /** Rank为正等级，Output为本次聚合结果；依次调用策略，不产生世界副作用。 */
    void BuildModifiers(int32 Rank,FDemoTalentModifiers& Output) const;
    /** Rank为展示等级；优先静态说明，否则组合每个实现自己的公式说明。 */
    FText Describe(int32 Rank) const;
    /** Rank为账本等级，Spec为尚未施加的私有效果；校验最终SetByCaller有限，失败不提交。 */
    bool ConfigureEffect(int32 Rank,const FGameplayEffectSpecHandle& Spec) const;
};
