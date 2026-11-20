// 职责：定义权限与纯射速成长的可逆GE。用法：管理组件按每项句柄授予/更新/移除，禁止直接修改永久基础属性。
#pragma once
#include "GameplayEffect.h"
#include "DemoTalentPassiveEffects.generated.h"
UCLASS()
class FPSDEMO_API UDemoTalentPermissionEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 构造无限非叠层权限GE；每Spec的DynamicGrantedTags来自已验证定义。 */
    UDemoTalentPermissionEffect();
};
UCLASS(Blueprintable)
class FPSDEMO_API UDemoTalentFireRateEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 使用Demo.Magnitude乘法修饰FireRateMultiplier；等级由定义策略写入私有Spec。 */
    UDemoTalentFireRateEffect();
};
