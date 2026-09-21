#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoCampaignTest.generated.h"

class ADemoEnemy;

/** 显式 -DemoCampaignTest 运行真实三难度十关回归；非 Shipping 入口才会生成。 */
UCLASS(NotBlueprintable, Transient)
class ADemoCampaignTest : public AActor
{
	GENERATED_BODY()
public:
	/** 低频状态机，等待真实清场定时器和 AI 攻击，不伪造胜利状态。 */
	ADemoCampaignTest();
	/** DeltaSeconds 为引擎帧间秒数；每次重新取 World 对象，重载不保留旧引用。 */
	virtual void Tick(float DeltaSeconds) override;
private:
	// 显式 -DemoNextLevelConfirmationTest 只跳过旧 AI 伤害探针；默认完整测试不变，专项使用不同成功标记。
	bool bConfirmationOnly = false;
	/** Condition 为断言，Message 为可搜索用例名；失败退出码 1。 */
	bool Check(bool Condition, const TCHAR* Message);
	/** 对原表副本做缺失/非法配置测试，不写磁盘、不修改运行配置。 */
	bool ValidateBadTables();
	// 每个 World 的步骤：0 大厅，1 生成与实例断言，2 真伤害断言，3 清场，4 奖励/终局。
	int32 Step = 0;
	// 下次步骤的世界秒数；AI 伤害测试等待攻击预警实际结束。
	float NextTime = 1.f;
	// 当前局预期收入和击杀，按每关表值独立累计；此测试不购买属性。
	int32 ExpectedCoins = 0; // 清关金币累计预期。
	int32 ExpectedSilver = 0; // 击杀银币累计预期，最终通关清空。
	int32 ExpectedKills = 0;
	// 真实攻击前生命及期望单次伤害，单位 HP。
	float HealthBeforeAttack = 0.f;
	float ExpectedDamage = 0.f;
	// 清场前的位置/视角快照，测试清关与领取奖励均不把玩家传回安全区。
	FVector PositionBeforeClear = FVector::ZeroVector;
	FRotator RotationBeforeClear = FRotator::ZeroRotator;
	// 失败保护；等待进程退出期间不再发起新操作。
	bool bFailed = false;
};
