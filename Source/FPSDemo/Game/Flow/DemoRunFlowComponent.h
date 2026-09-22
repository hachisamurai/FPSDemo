#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Game/DemoGameState.h"
#include "DemoRunFlowComponent.generated.h"
class ADemoCharacter;
class UDemoEnemySpawnComponent;
class UDemoRewardComponent;
class UDemoShopComponent;
class UDemoTerminalComponent;
class UDemoChallengeComponent;
class UDemoEncounterAreaSubsystem;
class UDemoRunSave;

/** 当前权威World唯一流程协调器；GM只转发，GI独占持久数据，不创建第二份阶段/钱包。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoRunFlowComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无常规Tick，只有启动等待期间启用有界真实时间检查。 */
    UDemoRunFlowComponent();
    /** 四张Table为GM加载的资源；各System为同Owner默认组件，只绑定依赖、不开始刷怪。 */
    void InitializeServices(UDataTable* Enemies, UDataTable* Difficulties, UDataTable* Levels, UDataTable* Endless,
        UDemoEnemySpawnComponent* Spawn, UDemoRewardComponent* Reward, UDemoShopComponent* Shop, UDemoTerminalComponent* Terminal, UDemoChallengeComponent* Challenge);
    /** Options为本World URL；消费一次恢复意图，等待当前Avatar有效后执行，不等待云端。 */
    void BeginStartup(const FString& Options);
    /** Player为刚登录或替换的Avatar；先订阅再查询状态，兼容通知先到/晚到，不重复授予能力。 */
    void BindAvatar(ADemoCharacter* Player);
    /** 旅行/EndPlay共用幂等停止，先作废请求，再撤销Timer/委托/终端。 */
    void Shutdown();
    /** EndPlayReason为卸载原因，独立清理而不假定GM先调用Shutdown。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** 只读当前轮次身份，武器资格和通关幂等使用同一值。 */
    const FString& GetRunId() const;
	/** Difficulty为终端选择的四档枚举，地狱需先解锁；仅第0关安全区允许修改，大厅/战斗/关间拒绝。 */
	bool SelectDifficulty(EDemoDifficulty Difficulty);
	/** 存档恢复/新局初始化的底层入口；校验完整配置后创建干净安全区，失败留大厅。 */
	bool StartRun();
	/** Number为正关数，固定战役1..10、无尽公式生成；OutLevel/OutDifficulty 为复制出的行，Error 返回失败原因，不泄漏表指针。 */
	bool GetLevelConfig(int32 Number, FDemoLevelRow& OutLevel, FDemoDifficultyRow& OutDifficulty, FString& Error) const;
	/** Number为正关数，bBoss 选择模板；OutStats 输出生成值，Error 返回缺行/校验错误。 */
	bool GetSpawnStats(int32 Number, bool bBoss, FDemoEnemySpawnStats& OutStats, FString& Error) const;
	/** 权威推进入口；终端经控制器确认后调用，仅 Hub/Intermission 且所有模态菜单关闭时允许进入，先清理两个终端。 */
	void StartNextLevel();
	/** Choice 范围 0..2，伤害/治疗/冲刺；前九关 Reward 时免费选一次。 */
	bool ChooseReward(int32 Choice);
	/** 玩家归零/掉出地图触发死亡结算，标记重开时直达安全区并保留难度。 */
	void NotifyPlayerDied();
	/** 终局主按钮；胜利/死亡清空临时成长银币、保留金币成长回安全区，系统错误返回大厅。 */
	void RestartDemo();
	/** 导出安全阶段检查点到当前槽；战斗/失败只保留上次检查点，不写半场状态。 */
	bool SaveCheckpoint();
	/** 死亡/主动放弃及离开死亡页重试时，采集当前装备与永久弹匣补给后保存新Hub；失败保留原槽，禁止旅行。 */
	bool SaveResetCheckpoint();
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
	/** OutConfig复制Default行并验证增长倍率与并发上限，Error返回缺失/越界原因。 */
	bool GetEndlessConfig(FDemoEndlessRow& OutConfig, FString& Error) const;
	/** 无尽已解锁且处于初始安全区才允许选择；选择后由现有终端确认出发。 */
	bool SelectEndless();
private:
	/** Player为借用角色；停止并传送安全区，bRefill默认补给，新一轮胜利续玩为false以保留检查点原值。 */
	void PlacePlayerInHub(ADemoCharacter* Player, bool bRefill = true);
	/** Player 为本次借用角色；仅补满生命弹药，不传送、不清除局内升级或冷却。 */
	void RefillPlayer(ADemoCharacter* Player);
	/** NewPhase 为目标阶段，记录旧/新状态。 */
	void SetPhase(EDemoPhase NewPhase);
	/** Remaining为刷怪器统计快照；此处只同步GameState供HUD读取。 */
	void HandleSpawnRemaining(int32 Remaining);
	/** 清场后下一帧执行；固定战役前九关/无尽每关原地备战；只有固定战役第十关胜利。 */
	void FinishLevel();
	/** Reason 是可读失败原因；系统异常也可终局重试。 */
	void FailRun(const FString& Reason);
	/** 仅Victory/Hub恢复永久金币GAS基线，重置临时技能/银币价格并补给，不触碰金币、击杀统计、武器解锁。 */
	void ResetCompletedRunGrowth();
    /** 任一依赖变化后检查完整就绪；只允许本World当前Avatar，无固定延迟假设。 */
    bool IsPlayerReady() const;
    /** 玩家就绪委托，同游戏线程且不携带旧Pawn，重复通知幂等。 */
    void HandleAvatarReady();
    /** 完成一次启动/恢复；返回true代表完成或明确失败，false表示依赖仍在准备。 */
    bool TryCompleteStartup();
    /** DeltaSeconds为核心Ticker间隔，仅启动等待时检查30秒真实时间上限，暂停不冻结。 */
    bool PollStartup(float DeltaSeconds);
    /** AreaIndex为-1安全区/0..2战斗区，解析一次值快照后交给终端服务。 */
    bool SpawnTerminals(int32 AreaIndex);
    /** 业务切换前撤销当前终端与交互版本。 */
    void ClearTerminals();
    /** Index为-1..2兼容位置查询，正式入场使用经验证的完整区域快照。 */
    FVector GetAreaCenter(int32 Index) const;
    UPROPERTY() TObjectPtr<UDataTable> EnemyTable; // 资源强引用，生命周期限本World。
    UPROPERTY() TObjectPtr<UDataTable> DifficultyTable; // 不在生成实例上保存行裸指针。
    UPROPERTY() TObjectPtr<UDataTable> LevelTable; // 关卡值在提交生成前冻结。
    UPROPERTY() TObjectPtr<UDataTable> EndlessTable; // 无尽配置按模式读取验证。
    UPROPERTY() TObjectPtr<UDemoEnemySpawnComponent> SpawnSystem; // 同Owner服务，队列唯一归Spawn。
    UPROPERTY() TObjectPtr<UDemoRewardComponent> RewardSystem; // 同Owner服务，统一发奖与去重。
    UPROPERTY() TObjectPtr<UDemoShopComponent> ShopSystem; // 停止时撤销保存委托，旧UI不能在旅行窗口继续交易。
    UPROPERTY() TObjectPtr<UDemoTerminalComponent> TerminalSystem; // 同Owner服务，管理场景交互对象。
    UPROPERTY() TObjectPtr<UDemoChallengeComponent> ChallengeSystem; // 本轮挑战资格，Profile仍负责永久事实。
    TWeakObjectPtr<UDemoEncounterAreaSubsystem> Areas; // World拥有，释放次序不作为业务前提。
    TWeakObjectPtr<ADemoCharacter> Avatar; // 当前Pawn，替换后立即解绑旧实例。
    FDelegateHandle AvatarReadyHandle; // 当前Pawn的就绪订阅，精确解绑。
    FTSTicker::FDelegateHandle StartupTicker; // 仅等待期间存在，不随World暂停。
    UPROPERTY() TObjectPtr<UDemoRunSave> PendingCheckpoint; // 启动等待时强持有已消费的值快照，不持有旧World。
    FString CampaignRunId; // 本轮GUID，存档恢复旧值，新轮生成新值。
    double StartupDeadline = 0; // 单调真实秒数，0代表未开始等待。
    bool bInitialized = false; // 依赖安装完成才允许业务启动。
    bool bStartupRequested = false; // BeginStartup幂等门控，不重复消费恢复请求。
    bool bStartupComplete = false; // 完成/失败后忽略晚到就绪通知。
    bool bStopping = false; // 旅行/卸载后拒绝旧World新请求。
    bool bRestartRequested = false; // 当前World只提交一次OpenLevel。
    bool bStartInHub = false; // 兼容旧无槽重开URL，仅本World使用。
    EDemoDifficulty RetryDifficulty = EDemoDifficulty::Normal; // URL校验后四档值。
    int32 RetryGold = 0; // 旧无槽入口余额0..1亿，正式槽使用检查点。
};
