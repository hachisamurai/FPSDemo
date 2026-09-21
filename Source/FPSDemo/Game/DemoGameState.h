#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Game/DemoCombatConfig.h"
#include "Game/DemoUpgradeProgress.h"
#include "DemoGameState.generated.h"

/** Lobby大厅、Hub初始/通关续玩安全区、Intermission原地备战、Combat战斗、Reward奖励、Victory/Defeat结算。 */
UENUM(BlueprintType)
enum class EDemoPhase : uint8 { Lobby, Hub, Combat, Reward, Victory, Defeat, Intermission };

/** 单人局内展示数据；写入集中于 GameMode，复制为后续观察客户端预留。 */
UCLASS()
class FPSDEMO_API ADemoGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	// 启动先进入图片背景大厅（半透明菜单）；只有开始游戏才建立安全区交互物。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") EDemoPhase Phase = EDemoPhase::Lobby;
	// 难度只在第0关Hub终端修改，通关续玩可重新选择；复制供UI展示，不代表完整联机支持。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") EDemoDifficulty Difficulty = EDemoDifficulty::Normal;
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") bool bEndless = false; // 独立模式，使用地狱难度基线且没有第十关终局。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 BestEndlessLevel = 0; // 本存档最高已完成关数，死亡/放弃保留。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 PistolChallenge = 0; // 0尚未开火/1仅手枪/2已失格；实际成功开火更新，2不可逆。
	// 当前/已完成关卡编号，0 表示尚未进入第一关，固定战役0..10，无尽可继续递增；显示当前战斗关或最近完成关。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 LevelNumber = 0;
	// 本关未死亡敌人数量，包含 Boss；只有死亡事件才能推进清场。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 EnemiesRemaining = 0;
	// 存档累计击杀，通关续玩保留；死亡/未通关主动放弃重开归零。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 TotalKills = 0;
	// 金币：整轮十关按难度通关奖励，安全区升级扣除；同一存档通关/死亡/主动返回均保留余额。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 Coins = 0;
	// 银币：击杀奖励，关间升级扣除；通关、死亡或主动放弃清零，UI复制只用于展示。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 SilverCoins = 0;
	// 两种终端购买总次数供统计；等于GoldPurchases+SilverPurchases，重置后只剩永久金币次数。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 Purchases = 0;
	// 永久金币购买总次数，仅统计；具体属性的价格由UpgradeProgress决定。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 GoldPurchases = 0;
	// 关间银币购买次数，只影响银币升级价格，通关/死亡重置。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 SilverPurchases = 0;
	// 权威成长来源和每项价格；随当前栏位读档，复制仅用于展示，不代表完整联机授权。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") FDemoUpgradeProgress UpgradeProgress;
	// 流程错误说明，例如生成失败；终局 HUD 明确呈现。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") FString FailureReason;
	// 仅死亡终局为true；重置局内成长与银币，保留金币永久成长/余额/解锁，系统错误回大厅。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") bool bReturnToHubOnRestart = false;
	/** OutLifetimeProps 为引擎复制注册列表。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
