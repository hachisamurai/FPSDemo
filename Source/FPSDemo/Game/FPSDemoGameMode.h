#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Game/DemoGameState.h"
#include "FPSDemoGameMode.generated.h"

class ADemoCharacter;
class ADemoEnemy;
class ADemoInteractable;

/** 大厅、十关战役与无尽权威状态机；逻辑关卡复用三个物理区域，生成计划来自表，刷怪、奖励和商店由独立组件执行。 */
UCLASS()
class FPSDEMO_API AFPSDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** 创建三个系统子组件、默认类和四张配置资产引用，无需新建蓝图。 */
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
private:
	/** OutConfig复制Default行并验证增长倍率与并发上限，Error返回缺失/越界原因。 */
	bool GetEndlessConfig(FDemoEndlessRow& OutConfig, FString& Error) const;
	UPROPERTY() TObjectPtr<UDataTable> EndlessTable; // GI不持有；当前GameMode强引用配置资产。
	UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoEnemySpawnComponent> SpawnSystem; // 权威World生成服务，独占队列/注册表/Timer。
	UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoRewardComponent> RewardSystem; // 击杀、通关、能力领奖，不处理阶段与UI。
	UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoShopComponent> ShopSystem; // 终端交易校验与购买；库存/GAS仍各有唯一所有者。
	/** Remaining为刷怪器统计快照；此处只同步GameState供HUD读取。 */
	void HandleSpawnRemaining(int32 Remaining);
	// 当前挑战GUID用于通关幂等；死亡/新局及胜利回Hub续玩生成新值，普通检查点读档恢复旧值。
	FString CampaignRunId;
	// 仅当前 World 启动时解析重开 URL；显式初次运行仍为大厅，不做跨局静态状态缓存。
	bool bStartInHub = false;
	// 死亡重开保留的合法难度；输入 URL 非法时回退普通并记录。
	EDemoDifficulty RetryDifficulty = EDemoDifficulty::Normal;
	// 无活动存档的独立World测试重开用金币快照0..1亿；正式玩家通过SaveGame恢复，不依赖URL持久化。
	int32 RetryGold = 0;
	/** 仅Victory/Hub恢复永久金币GAS基线，重置临时技能/银币价格并补给，不触碰金币、击杀统计、武器解锁。 */
	void ResetCompletedRunGrowth();
	// 防止 Enter/按钮同帧多次提交 OpenLevel；新 World 自动重置。
	bool bRestartRequested = false;
	// 本 World 持有已加载表，防止软引用载入后被 GC；不会在怪物上保留行裸指针。
	UPROPERTY() TObjectPtr<UDataTable> EnemyTable;
	UPROPERTY() TObjectPtr<UDataTable> DifficultyTable;
	UPROPERTY() TObjectPtr<UDataTable> LevelTable;
	/** 优先使用四个带 DemoAuthoredBlockout/区号标签的 Blender 场景网格；模板地图回退到基础几何。 */
	void BuildAreas();
	/** Player为借用角色；停止并传送安全区，bRefill默认补给，新一轮胜利续玩为false以保留检查点原值。 */
	void PlacePlayerInHub(ADemoCharacter* Player, bool bRefill = true);
	/** Player 为本次借用角色；仅补满生命弹药，不传送、不清除局内升级或冷却。 */
	void RefillPlayer(ADemoCharacter* Player);
	/** Center 为当前备战区地面世界坐标 cm；清理旧终端后生成一对，任一失败则全部回滚。 */
	bool SpawnTerminals(const FVector& Center);
	/** 进入战斗/失败时销毁当前终端并清空引用，阻止旧区域交互和物体累积。 */
	void ClearTerminals();
	/** 清场后下一帧执行；固定战役前九关/无尽每关原地备战；只有固定战役第十关胜利。 */
	void FinishLevel();
	/** NewPhase 为目标阶段，记录旧/新状态。 */
	void SetPhase(EDemoPhase NewPhase);
	/** Reason 是可读失败原因；系统异常也可终局重试。 */
	void FailRun(const FString& Reason);
	// 地面就绪后才能传送，防止登录早于 StartPlay。
	bool bAreasReady = false;
	// World 拥有当前升级终端；仅备战和 Reward 时存在，切换战斗前销毁，不复制。
	UPROPERTY() TObjectPtr<ADemoInteractable> ShopTerminal;
	// 与 ShopTerminal 成对创建/清理；不保留已经清关的旧区域入口。
	UPROPERTY() TObjectPtr<ADemoInteractable> NextLevelTerminal;
};
