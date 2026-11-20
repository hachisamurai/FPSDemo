// 职责：经济账本到天赋组件的桥接与遗物保护。用法：Commit/Avatar初始化时同步，天赋句柄归PS管理器，遗物经Health执行入口。
#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "DemoGambleEffects.generated.h"
class UAbilitySystemComponent;
struct FGameplayEffectModCallbackData;
/** 无限GE仅授予遗物权限Tag；天赋权限与射速使用各项独立GE。 */
UCLASS()
class FPSDEMO_API UDemoGambleLoadoutEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 每人最多一份，仅遗物权限变化时替换，不因天赋升级重建。 */
    UDemoGambleLoadoutEffect();
};
/** 致死保护独立于冲刺躲避，避免误触发Boss躲避回血。 */
UCLASS()
class FPSDEMO_API UDemoGambleProtectionEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 时长由每次Spec写入，不修改CDO。 */
    UDemoGambleProtectionEffect();
};
namespace DemoGambleEffects
{
    /** ASC为本人借用，Id为固定天赋/遗物白名单；可在客户端只读复制Tag。 */
    FPSDEMO_API bool Has(const UAbilitySystemComponent* ASC, FName Id);
    /** ASC为当前玩家借用，Id为天赋ID；要求真实GE的Tag存在，再从本人复制账本读等级，缺失返回0。 */
    FPSDEMO_API int32 Rank(const UAbilitySystemComponent* ASC,FName Id);
    /** ASC/Id同Rank；返回此天赋的等级效果系数，未拥有为0。 */
    FPSDEMO_API float Strength(const UAbilitySystemComponent* ASC,FName Id);
    /** ASC是PS持有的权威实例，Data为已验证账本；转交天赋差异同步，再更新变化的遗物权限。 */
    FPSDEMO_API void Sync(UAbilitySystemComponent* ASC, const FDemoGambleProgress& Data);
    /** Data是正在结算的Health GE，Health为旧HP；返回false拒绝伤害，可修改致死伤害为保留1HP。 */
    FPSDEMO_API bool GuardHealth(FGameplayEffectModCallbackData& Data, float Health);
}
