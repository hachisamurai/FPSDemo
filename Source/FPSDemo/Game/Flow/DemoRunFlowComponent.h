// 职责：服务器战局编排，组合目标/生成/奖励/区域；个人成长仍归PlayerState。
// 职责：当前权威World的战役流程、阶段和检查点协调；运行期整轮属性重置与严格持久恢复分离，依赖本人PS/Avatar及领域服务。
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
struct FDemoSpawnPlan;
enum class EDemoRunOutcome : uint8; // 只用于冻结个人战报，不新增战局阶段。

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
    /** 当前战斗可撤销凭据，只在权威 World 存在；调用者仅弱持有。 */
    class UDemoCombatSession* GetCombatSession() const;
    /** 服务器准入门禁：依赖及读档完成、当前Hub且未提交旅行时才能接纳新连接。 */
    bool CanAcceptConnections() const;
	/** Difficulty为终端选择的四档枚举，地狱需先解锁；仅第0关安全区允许修改，大厅/战斗/关间拒绝。 */
	bool SelectDifficulty(EDemoDifficulty Difficulty);
	/** 存档恢复/新局初始化的底层入口；校验完整配置后创建干净安全区，失败留大厅。 */
	bool StartRun();
	/** Number为正关数，固定战役1..10、无尽公式生成；OutLevel/OutDifficulty 为复制出的行，Error 返回失败原因，不泄漏表指针。 */
	bool GetLevelConfig(int32 Number, FDemoLevelRow& OutLevel, FDemoDifficultyRow& OutDifficulty, FString& Error) const;
	/** Number为正关数，bBoss 选择模板；OutStats 输出生成值，Error 返回缺行/校验错误。 */
	bool GetSpawnStats(int32 Number, bool bBoss, FDemoEnemySpawnStats& OutStats, FString& Error) const;
	/** 权威推进入口；仅Hub/Intermission，合作需全队有效准备；先完成配置、保存和目标预检，成功提交才清理终端并关闭确认页。 */
	void StartNextLevel();
	/** Choice 范围 0..2，伤害/治疗/冲刺；前九关 Reward 时免费选一次。 */
	bool ChooseReward(int32 Choice);
	/** Player 为实际选择者；全部已连接成员领完后才进入 Intermission。 */
	bool ChooseReward(int32 Choice, ADemoCharacter* Player);
	/** 奖励领取/离线/读档后重新评估当前队伍；无人待领时进入Intermission，避免离线成员卡死。 */
	void ReconcileRewardReadiness();
	/** 玩家归零/掉出地图触发死亡结算，标记重开时直达安全区并保留难度。 */
	void NotifyPlayerDied();
	/** 终局主按钮；胜利/死亡清空临时成长银币、保留金币成长回安全区，系统错误返回大厅。 */
	void RestartDemo();
	/** 导出安全阶段检查点到当前槽；战斗/失败只保留上次检查点，不写半场状态。 */
	bool SaveCheckpoint();
	/** 死亡/主动放弃及离开死亡页重试时，保留当前槽V10身份、采集装备与永久弹匣补给后保存新Hub；不生成新槽身份，失败保留原槽并禁止旅行。 */
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
    /** 只读当前内容模式，旧中途存档和无尽不自动启用新任务。 */
    bool UsesDemoCampaign() const;
    /** 奖励服务查询已完成的同轮同关裁决；仅Ending同栈窗口有效，独立于已撤销刷怪器的自然清空标记。 */
    bool HasResolvedEncounter(const FString& RunId, int32 Level) const;
    /** Player为拥有者、Choice=0..2；安全区终端选核心，命令入口统一保存并返回状态，拒绝跨阶段选择。 */
    bool SelectRunCore(ADemoCharacter* Player, int32 Choice);
private:
    /** 新轮从已指定资产冻结内容并建立seed；缺失/非法配置返回false并保留旧内容，不修改钱包。 */
    bool BeginCampaignContent();
    /** 读取GM覆盖或项目设置资产，验证并复制为不可变运行定义；输出JSON/指纹仅在成功后供检查点使用。 */
    class UDemoCampaignDefinition* FreezeConfiguredCampaign(FString& Json, FString& Fingerprint, FString& Error);
    /** 预检完成后一次提交内容引用和公开身份；Definition=null为旧模式，Json/指纹/Seed必须为空/0，不访问磁盘或资源。 */
    void CommitCampaignContent(class UDemoCampaignDefinition* Definition, const FString& Json, const FString& Fingerprint, int32 Seed);
    /** 准备本场稳定内容和原生成计划，任务/人数数量只舍入一次，失败不提交Combat。 */
    bool PrepareCampaignPlan(int32 Stage, FDemoLevelRow& Level, FDemoDifficultyRow& Difficulty, FDemoSpawnPlan& Plan, FString& Error);
    /** 生成清空只是事实，非歼灭目标不会因此结束。 */
    void HandleSpawnerCleared();
    /** Snapshot来自当前World目标组件，复制给GS；客户端不查询GM。 */
    void HandleObjectiveSnapshot(const FDemoObjectiveSnapshot& Snapshot);
    /** Session须为本场凭据；候选排队后重查全员生命，防回调次序抢胜。 */
    void HandleObjectiveCompleted(class UDemoCombatSession* Session);
    /** 关键目标丢失保留原检查点，不伪造通关。 */
    void HandleObjectiveError(class UDemoCombatSession* Session, const FString& Error);
    /** 下帧裁决完成候选，同帧团灭优先，结果最多提交一次。 */
    void ResolveObjectiveOutcome();
    UPROPERTY() TObjectPtr<class UDemoObjectiveCoordinatorComponent> ObjectiveSystem; // 同GM服务。
    UPROPERTY() TObjectPtr<class UDemoCampaignDefinition> CampaignDefinition; // 本轮从资产/检查点解析的独立冻结副本，不持有可被策划热改的作者对象。
    FString ContentSnapshot; // V9规范JSON，仅本轮冻结一次，保存沿用原字节，禁止每次按当前资产重建。
    FString ContentFingerprint; // 规范JSON内容一致性摘要；不作为身份认证或权限凭据。
    FName ContentId; // None旧战役，steam_demo新内容。
    int32 ContentRevision=0; // 作者内容修订，正整数；兼容性由冻结快照schema决定，不要求等于当前资产修订。
    int32 ContentSeed=0; // 非负稳定seed，随检查点保存。
    FDemoCampaignPlan PreparedCampaign; // 下一场的冻结值，提交成功才发布。
    bool bObjectiveCandidate=false; // 一次完成意图，退出Combat撤销。
    bool bEndingEncounter=false; // 结果闩锁，防重入发奖。
    FTimerHandle OutcomeTimer; // UObject绑定；Shutdown取消。
    FTimerHandle TerminalRetryTimer; // 仅本World清关后两种摆放均失败时有效，离开备战时撤销。
    /** 权威定时回调。ClearedLevel为已完成的关号，AreaIndex为同一房间区号，只补终端，不重复领奖/存档。 */
    void RetryClearedTerminals(int32 ClearedLevel,int32 AreaIndex);
    // 每次入场创建新对象；离开 Combat 先失效再清理子弹和异步攻击。
    UPROPERTY() TObjectPtr<class UDemoCombatSession> CombatSession;
	/** Player为借用角色；停止并传送安全区，bRefill默认补给，新一轮胜利续玩为false以保留检查点原值。 */
	void PlacePlayerInHub(ADemoCharacter* Player, bool bRefill = true);
	/** Player 为本次借用角色；仅补满生命弹药，不传送、不清除局内升级或冷却。 */
	void RefillPlayer(ADemoCharacter* Player);
	/** NewPhase为权威目标阶段；实际变化推进PhaseRevision并撤销旧备战动作，同阶段重复发布不创建新攻击代次。 */
	void SetPhase(EDemoPhase NewPhase);
	/** Remaining为刷怪器统计快照；此处只同步GameState供HUD读取。 */
	void HandleSpawnRemaining(int32 Remaining);
	/** 清场后下一帧执行；固定战役前九关/无尽每关原地备战；只有固定战役第十关胜利。 */
	void FinishLevel();
	/** Reason 是可读失败原因；系统异常也可终局重试。 */
	void FailRun(const FString& Reason);
	/** 胜利形成/胜利回Hub/全员阵亡时逐人撤销本轮策略、清临时账本，再按本人永久来源恢复活体属性并补给；不以临时SaveGame绕过持久验证。 */
	void ResetCompletedRunGrowth();
    /** Outcome/Reason为已确认结束；临时成长清理前逐人冻结摘要并推送拥有者，幂等且不发奖或写档。 */
    void CaptureRunSummaries(EDemoRunOutcome Outcome, const FString& Reason);
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
    bool bRestoringCheckpoint = false; // 读档同步事务期间拒绝保存/准入，完成后才发布可操作阶段。
    bool bRestartRequested = false; // 当前World只提交一次OpenLevel。
    bool bStartInHub = false; // 兼容旧无槽重开URL，仅本World使用。
    EDemoDifficulty RetryDifficulty = EDemoDifficulty::Normal; // URL校验后四档值。
    int32 RetryGold = 0; // 旧无槽入口余额0..1亿，正式槽使用检查点。
};
