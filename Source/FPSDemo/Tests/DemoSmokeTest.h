#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoSmokeTest.generated.h"

class ADemoEnemy;

/** 仅在非 Shipping 的 -DemoSmokeTest 启动时生成；在真实 Standalone 世界验证完整流程。 */
UCLASS(NotBlueprintable, Transient)
class ADemoSmokeTest : public AActor
{
	GENERATED_BODY()
public:
	/** 开启低频状态机，测试成功/失败以进程退出码返回脚本。 */
	ADemoSmokeTest();
	/** DeltaSeconds 为帧间秒数；用等待阶段验证真实 AbilityTask/GE 到期。 */
	virtual void Tick(float DeltaSeconds) override;
private:
	/** EventName 为 Weapon.Fire/Weapon.Hit 标签，统计当前世界仍播放的组件；只供 -DemoAudioValidation 使用。 */
	int32 CountWeaponVoices(FName EventName) const;
	/** bCondition 为断言结果，Message 是日志用例名；失败停止测试并退出 1。 */
	bool Check(bool bCondition, const TCHAR* Message);
	/** Next 为测试步骤号，Delay 是至少等待的世界秒数。 */
	void Advance(int32 Next, float Delay = 0.1f);
	/** 使用 GAS 伤害清除本关；验证死亡去重与金币，不模拟未实现的控制台作弊。 */
	void ClearCurrentLevel();
	/** Name 为截图基名，仅 -DemoCapture 时调用引擎截图，不改变系统窗口。 */
	void Capture(const FString& Name);
	// 当前测试步骤只在单个 World 中有效，重载后从 0 开始。
	int32 Step = 0;
	// 下一步骤最早执行的 World 时间（秒），用于等待 Task 和下帧清场。
	float NextStepTime = 2.f;
	// 失败后停止本 Actor Tick，等待引擎退出。
	bool bFailed = false;
	// 测试射线命中的目标，不延长敌人生命周期。
	TWeakObjectPtr<ADemoEnemy> ShotTarget;
};
