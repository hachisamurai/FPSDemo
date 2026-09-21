#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoUIValidation.generated.h"

/** 显式 -DemoUIValidation 的真实渲染回归；独立于玩法 Smoke，检查热区、菜单状态及截图。 */
UCLASS(NotBlueprintable, Transient)
class FPSDEMO_API ADemoUIValidation : public AActor
{
	GENERATED_BODY()
public:
	/** 低频推进，每一步至少留一帧用于 HUD 重建热区和完成截图。 */
	ADemoUIValidation();
	/** DeltaSeconds 为帧间秒数；只在专用测试进程修改本局，不接触编辑器资产。 */
	virtual void Tick(float DeltaSeconds) override;
private:
	/** 显式-DemoEnemyStatusTest的独立UI分支；参数均为本帧借用，使用真实ASC标签夹具和渲染截图，不实现弹药效果。 */
	void TickEnemyStatus(class ADemoPlayerController* PC, class AFPSDemoGameMode* Mode, class ADemoCharacter* Player, class ADemoHUD* HUD);
	// 测试敌人归World持有，弱引用防止死亡/换图后的悬空访问；不写正式配置或存档。
	TWeakObjectPtr<class ADemoEnemy> StatusEnemy;
	// 初始头顶可见位置与视角仅为当前测试World恢复取景，世界坐标厘米/角度。
	FVector StatusEnemyLocation = FVector::ZeroVector;
	FRotator StatusViewRotation = FRotator::ZeroRotator;
	// 测试遮挡盒归World持有，结束遮挡断言后立即销毁。
	TWeakObjectPtr<AActor> StatusOccluder;
	/** Condition 是预期结果，Message 为可搜索用例名；失败以退出码 1 结束测试进程。 */
	bool Check(bool Condition, const TCHAR* Message);
	/** Name 是固定截图基名；保存到 Saved/Screenshots/DemoUI，不触碰用户截图。 */
	void Capture(const TCHAR* Name);
	/** Next 是下一步编号；延迟 0.3 秒保证渲染与输入热区完成刷新。 */
	void Advance(int32 Next);
	/** 使用真实 GE 击杀当前存活敌人；只验证流程，不替代玩家射击或难度验收。 */
	void ClearEnemies();
	/** Offset 是相对设计锚点的偏移，Expected 为预期热区；bLobbyAnchor 使用左侧43%高度锚点，否则用视口中心。 */
	bool ClickMenuPoint(FVector2D Offset, FName Expected, bool bLobbyAnchor = false);
	// 本 World 的步骤和调度时间（秒）；重载后重新初始化，不持有旧 World 指针。
	int32 Step = 0;
	float NextTime = 2.f;
	// 断言失败后阻止后续步骤继续修改测试世界。
	bool bFailed = false;
};
