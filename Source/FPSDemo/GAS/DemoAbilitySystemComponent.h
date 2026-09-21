#pragma once
#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "DemoAbilitySystemComponent.generated.h"
class USkeletalMeshComponent;

/** 集中处理技能授予与失败日志，保持 Pawn 输入层不直接执行战斗逻辑。 */
UCLASS()
class FPSDEMO_API UDemoAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
public:
	/** 配置可复制 ASC；单人 Demo 使用服务器技能执行策略。 */
	UDemoAbilitySystemComponent();
	/** 由 PlayerState 在 Avatar 初始化后调用；重复调用不会重复授予。 */
	void GrantStartupAbilities();
	/** AbilityClass 是要请求的原生技能类；返回实际激活是否成功并记录拒绝。 */
	bool ActivateDemoAbility(TSubclassOf<UGameplayAbility> AbilityClass);
	/** AbilityClass指定需取消的技能；切枪只取消装填/瞄准，不移除其他冷却或已授予能力。 */
	void CancelDemoAbility(TSubclassOf<UGameplayAbility> AbilityClass);
	/** Arms为Avatar拥有的Mesh1P；显式选择主实例，避免ACharacter默认Mesh抢占GAS Montage上下文。 */
	bool SetFirstPersonAnimationMesh(USkeletalMeshComponent* Arms);
	/** Tag 为单个冷却标签；返回剩余秒数，无冷却为 0，HUD 只读使用。 */
	float GetCooldownRemaining(FGameplayTag Tag) const;
private:
	// 每个 PlayerState 生命周期只发放一次默认技能；不复制，由权威端管理。
	bool bStartupGranted = false;
};
