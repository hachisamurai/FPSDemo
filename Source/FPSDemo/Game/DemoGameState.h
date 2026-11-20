// 职责：公开战局、内容计划和目标快照，服务器Flow提交，客户端HUD只读；个人成长仍归PS。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Game/DemoCombatConfig.h"
#include "Game/DemoUpgradeProgress.h"
#include "Game/Content/DemoCampaignDefinition.h"
#include "Player/Coop/Ready/DemoTeamReadyTypes.h"
#include "Player/Coop/Preparation/DemoEncounterPreparationTypes.h"
#include "DemoGameState.generated.h"

class UDemoTeamPingComponent;

/** Lobby大厅、Hub初始/通关续玩安全区、Intermission原地备战、Combat战斗、Reward奖励、Victory/Defeat结算。 */
UENUM(BlueprintType)
enum class EDemoPhase : uint8 { Lobby, Hub, Combat, Reward, Victory, Defeat, Intermission };

/** 全队公开战局复制；个人经济归PlayerState。旧经济字段仅供历史接口迁移，不参与联机授权或复制。 */
UCLASS()
class FPSDEMO_API ADemoGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
    /** 建立全队公开标记子对象；只持有复制表现值，不接管个人输入或经济。 */
    ADemoGameState();
    /** HUD只读当前World的标记组件；权威请求必须经拥有者PC的无参数RPC。 */
    UDemoTeamPingComponent* GetTeamPingComponent() const;
    UPROPERTY(Replicated) FDemoTeamReadySnapshot TeamReady; // 公开准备目录，服务器出发瞬间重验，不写档。
    UPROPERTY(Replicated) FDemoEncounterPreparationView Preparation; // 本场资源与地面握手状态，独立于玩家出发意图，不写检查点。
    UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo|Campaign") FDemoCampaignPlan CampaignPlan; // 公开稳定内容计划，不含钱包或Actor。
    UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo|Objective") FDemoObjectiveSnapshot Objective; // 目标状态改变或进度最多4Hz更新。
	// 启动先进入图片背景大厅（半透明菜单）；只有开始游戏才建立安全区交互物。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_RunView, Category="Demo") EDemoPhase Phase = EDemoPhase::Lobby; // UI 从复制状态恢复，不能假定 RPC 比属性晚到。
	UPROPERTY(ReplicatedUsing=OnRep_RunView) uint32 PhaseRevision = 0; // 服务器在实际阶段变化时递增，0为World初始状态；绑定开火/弹迹时序，不持久化或跨World复用。
	// 难度只在第0关Hub终端修改，通关续玩可重新选择；复制供UI展示，不代表完整联机支持。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") EDemoDifficulty Difficulty = EDemoDifficulty::Normal;
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") bool bEndless = false; // 独立模式，使用地狱难度基线且没有第十关终局。
	UPROPERTY(BlueprintReadOnly, Category="Demo") int32 BestEndlessLevel = 0; // 本存档最高已完成关数，死亡/放弃保留。 // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	UPROPERTY(BlueprintReadOnly, Category="Demo") int32 PistolChallenge = 0; // 0尚未开火/1仅手枪/2已失格；实际成功开火更新，2不可逆。 // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	// 当前/已完成关卡编号，0 表示尚未进入第一关，固定战役0..10，无尽可继续递增；显示当前战斗关或最近完成关。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_RunView, Category="Demo") int32 LevelNumber = 0; // 阶段和关号可能分次到达，通知必须幂等。
	// 本关未死亡敌人数量，包含 Boss；只有死亡事件才能推进清场。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") int32 EnemiesRemaining = 0;
	// 存档累计击杀，通关续玩保留；死亡/未通关主动放弃重开归零。
	UPROPERTY(BlueprintReadOnly, Category="Demo") int32 TotalKills = 0; // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	// 金币：整轮十关按难度通关奖励，安全区升级扣除；同一存档通关/死亡/主动返回均保留余额。
	UPROPERTY(BlueprintReadOnly, Category="Demo") int32 Coins = 0; // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	// 银币：击杀奖励，关间升级扣除；通关、死亡或主动放弃清零，UI复制只用于展示。
	UPROPERTY(BlueprintReadOnly, Category="Demo") int32 SilverCoins = 0; // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	// 两种终端购买总次数供统计；等于GoldPurchases+SilverPurchases，重置后只剩永久金币次数。
	UPROPERTY(BlueprintReadOnly, Category="Demo") int32 Purchases = 0; // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	// 永久金币购买总次数，仅统计；具体属性的价格由UpgradeProgress决定。
	UPROPERTY(BlueprintReadOnly, Category="Demo") int32 GoldPurchases = 0; // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	// 关间银币购买次数，只影响银币升级价格，通关/死亡重置。
	UPROPERTY(BlueprintReadOnly, Category="Demo") int32 SilverPurchases = 0; // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	// 权威成长来源和每项价格；随当前栏位读档，复制仅用于展示，不代表完整联机授权。
	UPROPERTY(BlueprintReadOnly, Category="Demo") FDemoUpgradeProgress UpgradeProgress; // 兼容旧反射字段；业务已迁至个人Progress，不向队友复制。
	// 流程错误说明，例如生成失败；终局 HUD 明确呈现。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") FString FailureReason;
	// 仅死亡终局为true；重置局内成长与银币，保留金币永久成长/余额/解锁，系统错误回大厅。
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Demo") bool bReturnToHubOnRestart = false;
	/** OutLifetimeProps 为引擎复制注册列表。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 本机 UI 的复制就绪通知；阶段/版本变化才清理过期弹迹，重复菜单/关号通知保持幂等，不驱动权威规则。 */
	UFUNCTION() void OnRep_RunView();
private:
    uint32 LastObservedPhaseRevision = 0; // 本机上次已处理的阶段版本，只抑制重复表现清理，不复制或保存。
    EDemoPhase LastObservedPhase = EDemoPhase::Lobby; // Phase与版本分次复制时补充识别阶段变更，不用关号刷新误清新弹。
    bool bObservedRunPhase = false; // 首次复制也必须协调已先到达的弹体；World销毁后随GS重置。
    UPROPERTY(VisibleAnywhere, Category="Demo|Coop") TObjectPtr<UDemoTeamPingComponent> TeamPings; // GS拥有并公开复制，World结束销毁，不写检查点。
};
