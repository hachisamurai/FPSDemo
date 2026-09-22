#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Game/DemoGameState.h"
#include "FPSDemoGameMode.generated.h"

class ADemoCharacter;
class ADemoEnemy;
class ADemoInteractable;

/** 权威玩法组合入口；生命周期转交RunFlow，公开方法为旧UI/测试兼容转发，不持有第二份流程状态。 */
UCLASS()
class FPSDEMO_API AFPSDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** 创建六个系统子组件、默认类和四张配置资产引用，无需新建蓝图。 */
	AFPSDemoGameMode();
	/** MapName/Options 是引擎 URL，ErrorMessage 返回初始化错误；覆盖旧蓝图存储的默认类。 */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	/** 加载配置并显示大厅；死亡重开 URL 会恢复难度并直接初始化安全区，其余入口等待开始游戏。 */
	virtual void StartPlay() override;
	/** NewPlayer 为登录控制器；支持开局及编辑器重生。 */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	/** EndPlayReason 为卸载原因，负责清除延迟推进回调。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 存档恢复/新局初始化的底层入口；校验完整配置后创建干净安全区，失败留大厅。 */
	bool StartRun();
	/** 导出安全阶段检查点到当前槽；战斗/失败只保留上次检查点，不写半场状态。 */
	bool SaveCheckpoint();
	/** 已校验Data借用到恢复结束，重建后恢复备战/领奖；旧Victory转续玩Hub，失败留大厅显示原因。 */
	bool RestoreCheckpoint(const class UDemoRunSave& Data);
	/** 完整Victory后回安全区；保留金币/武器解锁与库存，清空银币/临时成长并保留金币成长，更新RunId并自动保存。 */
	bool ContinueAfterVictory();
	/** 是否存在将被清空的临时属性/技能成长（不含永久金币成长）；返回安全区前用于决定是否必须确认。 */
	bool HasRunUpgrades() const;
	/** 未通关有成长时bConfirmed必须为true才可清空；Victory清空临时成长/银币并保留金币成长回Hub，不影响永久解锁。 */
	bool ReturnToSafeHub(bool bConfirmed);
	/** 选择存档/返回大厅统一旅行；bResume需有GI待恢复槽，请求只消费一次。 */
	void TravelToRun(bool bResume);
	/** Difficulty为终端选择的四档枚举，地狱需先解锁；仅第0关安全区允许修改，大厅/战斗/关间拒绝。 */
	bool SelectDifficulty(EDemoDifficulty Difficulty);
	/** 无尽已解锁且处于初始安全区才允许选择；选择后由现有终端确认出发。 */
	bool SelectEndless();
	/** WeaponId为实际成功开火的稳定目录ID；更新整轮手枪资格并立即保存，失败阻止本次射击。 */
	bool RegisterWeaponShot(FName WeaponId);
	/** Number为正关数，固定战役1..10、无尽公式生成；OutLevel/OutDifficulty 为复制出的行，Error 返回失败原因，不泄漏表指针。 */
	bool GetLevelConfig(int32 Number, FDemoLevelRow& OutLevel, FDemoDifficultyRow& OutDifficulty, FString& Error) const;
	/** Number为正关数，bBoss 选择模板；OutStats 输出生成值，Error 返回缺行/校验错误。 */
	bool GetSpawnStats(int32 Number, bool bBoss, FDemoEnemySpawnStats& OutStats, FString& Error) const;
	/** 权威推进入口；终端经控制器确认后调用，仅 Hub/Intermission 且所有模态菜单关闭时允许进入，先清理两个终端。 */
	void StartNextLevel();
	/** Enemy 是确认死亡的注册敌人；只处理一次，避免重复金币。 */
	void NotifyEnemyKilled(ADemoEnemy* Enemy);
	/** Enemy 是异常销毁的活敌人；失败而不是无限等待剩余计数。 */
	void NotifyEnemyLost(ADemoEnemy* Enemy);
	/** 玩家归零/掉出地图触发死亡结算，标记重开时直达安全区并保留难度。 */
	void NotifyPlayerDied();
	/** 死亡/主动放弃及离开死亡页重试时，采集当前装备与永久弹匣补给后保存新Hub；失败保留原槽，禁止旅行。 */
	bool SaveResetCheckpoint();
	/** Choice 范围 0..2，伤害/治疗/冲刺；前九关 Reward 时免费选一次。 */
	bool ChooseReward(int32 Choice);
	/** Choice 范围 0..2，伤害/上限生命/弹匣；Purchaser 必须在当前备战终端旁。 */
	bool PurchaseUpgrade(int32 Choice, ADemoCharacter* Purchaser);
	/** Choice=0伤害/1生命/2弹匣；当前区域币种每项20起，该项每购一次+10，非法项返回INDEX_NONE。 */
	int32 GetUpgradeCost(int32 Choice = 0) const;
	/** Purchaser为借用角色，Choice=0..2；返回该项交易阻塞原因，UI与交易共用校验。 */
	FString GetPurchaseBlockReason(ADemoCharacter* Purchaser, int32 Choice = 0) const;
	/** Player为借用当前玩家；只校验备战终端/拥有者/生命/距离，不要求金币，供武器与商店共用。 */
	FString GetTerminalBlockReason(ADemoCharacter* Player) const;
	/** 终局主按钮；胜利/死亡清空临时成长银币、保留金币成长回安全区，系统错误返回大厅。 */
	void RestartDemo();
	/** Index=-1 返回安全区、0..2 返回战斗区地面中心，单位厘米。 */
	FVector GetAreaCenter(int32 Index) const;
	/** 返回当前升级终端借用引用；安全区与已清关区域复用接口，战斗期间为 null。 */
	ADemoInteractable* GetShopTerminal() const;
	/** 返回当前下一关终端借用引用；战斗、失败、最终胜利时不存在。 */
	ADemoInteractable* GetNextLevelTerminal() const;
	/** 返回当前World的系统服务，仅借用；旧GameMode交易/死亡接口保留为转发适配。 */
	class UDemoEnemySpawnComponent* GetSpawnSystem() const;
	class UDemoRewardComponent* GetRewardSystem() const;
	class UDemoShopComponent* GetShopSystem() const;
	// 四张软引用配置表；运行时同步载入一次。原生默认路径由 DefaultGame.ini 的 /Game/Data 显式 Cook 规则保障，换目录需同步规则。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Config") TSoftObjectPtr<UDataTable> EnemyTableAsset;
	UPROPERTY(EditDefaultsOnly, Category="Demo|Config") TSoftObjectPtr<UDataTable> DifficultyTableAsset;
	UPROPERTY(EditDefaultsOnly, Category="Demo|Config") TSoftObjectPtr<UDataTable> LevelTableAsset;
	UPROPERTY(EditDefaultsOnly, Category="Demo|Config") TSoftObjectPtr<UDataTable> EndlessTableAsset; // /Game/Data/DT_Endless的Default行。
    /** 返回同World的终端/流程/挑战服务，仅借用，不跨地图缓存。 */
    class UDemoTerminalComponent* GetTerminalSystem() const;
    class UDemoRunFlowComponent* GetRunFlow() const;
    class UDemoChallengeComponent* GetChallengeSystem() const;
private:
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoEnemySpawnComponent> SpawnSystem; // 权威生成执行器。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoRewardComponent> RewardSystem; // 权威收益结算。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoShopComponent> ShopSystem; // 属性/弹药交易。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoTerminalComponent> TerminalSystem; // 终端生命周期与公共交互校验。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoRunFlowComponent> RunFlow; // 唯一流程与RunId所有者。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoChallengeComponent> ChallengeSystem; // 本轮资格及检查点元数据。
};
