// 职责：创建个人运行容器和连接语音子对象并提供借用接口。用法：GAS/Voice由PS持有；Avatar更换不重授天赋，语音数据只活在内存中。
#include "Player/DemoPlayerState.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Summary/DemoRunSummaryComponent.h"
#include "Player/Voice/DemoVoiceComponent.h"
#include "GAS/Talents/DemoTalentComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Player/DemoPlayerProgressComponent.h"

ADemoPlayerState::ADemoPlayerState()
{
	DEMO_LOG_CALL();
	RunBuild = CreateDefaultSubobject<UDemoRunBuildComponent>(TEXT("RunBuild")); // 核心不占旧天赋槽，不另建ASC。
    RunSummary = CreateDefaultSubobject<UDemoRunSummaryComponent>(TEXT("RunSummary")); // 清理临时成长前保留只读结果，不复制给队友。
	Voice = CreateDefaultSubobject<UDemoVoiceComponent>(TEXT("Voice")); // 拥有者RPC复用PS连接，不把本机设备句柄复制给队友。
	AbilitySystem = CreateDefaultSubobject<UDemoAbilitySystemComponent>(TEXT("AbilitySystem"));
	Attributes = CreateDefaultSubobject<UDemoAttributeSet>(TEXT("Attributes"));
	Progress = CreateDefaultSubobject<UDemoPlayerProgressComponent>(TEXT("Progress")); // 私有经济跟随 PS，不能存入共享 GameState。
	Talents = CreateDefaultSubobject<UDemoTalentComponent>(TEXT("Talents")); // 运行句柄归PS，不能放共享Catalog/CDO。
	NetUpdateFrequency = 100.f;
}
UAbilitySystemComponent* ADemoPlayerState::GetAbilitySystemComponent() const { DEMO_LOG_TICK(); return AbilitySystem; }
UDemoAbilitySystemComponent* ADemoPlayerState::GetDemoASC() const { DEMO_LOG_TICK(); return AbilitySystem; }
const UDemoAttributeSet* ADemoPlayerState::GetAttributes() const { DEMO_LOG_TICK(); return Attributes; }
UDemoPlayerProgressComponent* ADemoPlayerState::GetProgressComponent() const { DEMO_LOG_TICK(); return Progress; }
UDemoTalentComponent* ADemoPlayerState::GetTalentComponent() const { DEMO_LOG_TICK(); return Talents; }
UDemoVoiceComponent* ADemoPlayerState::GetVoiceComponent() const { DEMO_LOG_TICK(); return Voice; }
UDemoRunBuildComponent* ADemoPlayerState::GetRunBuildComponent() const { DEMO_LOG_TICK(); return RunBuild; }
UDemoRunSummaryComponent* ADemoPlayerState::GetRunSummaryComponent() const { DEMO_LOG_TICK(); return RunSummary; }
