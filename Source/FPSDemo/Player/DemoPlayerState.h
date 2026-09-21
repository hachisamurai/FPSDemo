#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "DemoPlayerState.generated.h"

class UDemoAbilitySystemComponent;
class UDemoAttributeSet;

/** 参考 Lyra：玩家 ASC/属性由 PlayerState 拥有，Pawn 仅作为当前 Avatar。 */
UCLASS()
class FPSDEMO_API ADemoPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	/** 创建 ASC 与属性默认子对象，生命周期与本局 PlayerState 一致。 */
	ADemoPlayerState();
	/** GAS 标准接口，返回此 PlayerState 持有的 ASC。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	/** 只读引用入口，调用者不接管所有权。 */
	UDemoAbilitySystemComponent* GetDemoASC() const;
	/** 属性对象由 PlayerState 保活，调用者不得手动销毁。 */
	const UDemoAttributeSet* GetAttributes() const;
private:
	// 玩家持久能力容器，复制模式 Mixed；Avatar 可重新绑定。
	UPROPERTY(VisibleAnywhere, Category="Demo|GAS")
	TObjectPtr<UDemoAbilitySystemComponent> AbilitySystem;
	// 复制属性集合，作为默认子对象被 ASC 自动注册。
	UPROPERTY()
	TObjectPtr<UDemoAttributeSet> Attributes;
};
