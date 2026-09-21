#include "Player/DemoPlayerState.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"

ADemoPlayerState::ADemoPlayerState()
{
	DEMO_LOG_CALL();
	AbilitySystem = CreateDefaultSubobject<UDemoAbilitySystemComponent>(TEXT("AbilitySystem"));
	Attributes = CreateDefaultSubobject<UDemoAttributeSet>(TEXT("Attributes"));
	NetUpdateFrequency = 100.f;
}
UAbilitySystemComponent* ADemoPlayerState::GetAbilitySystemComponent() const { DEMO_LOG_TICK(); return AbilitySystem; }
UDemoAbilitySystemComponent* ADemoPlayerState::GetDemoASC() const { DEMO_LOG_TICK(); return AbilitySystem; }
const UDemoAttributeSet* ADemoPlayerState::GetAttributes() const { DEMO_LOG_TICK(); return Attributes; }
