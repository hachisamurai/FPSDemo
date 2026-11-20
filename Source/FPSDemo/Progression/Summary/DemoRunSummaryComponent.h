// 职责：冻结每位玩家结束前的只读战报。使用：PS默认子对象，Flow在清理临时成长前Capture，拥有者菜单快照转发；依赖原账本，不发奖、不存档、不Tick。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Game/DemoCombatConfig.h"
#include "Game/Objectives/DemoObjectiveTypes.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "DemoRunSummaryComponent.generated.h"

/** 结束原因与实际清理规则分开；系统错误保留检查点，不能冒充团灭或通关。 */
UENUM()
enum class EDemoRunOutcome : uint8 { Victory, Eliminated, SystemError };

/** 仅本人菜单RPC的纯值战报；在结束前冻结，不能从清理后的钱包/核心反推，不保存半场进度。 */
USTRUCT()
struct FPSDEMO_API FDemoRunSummarySnapshot
{
    GENERATED_BODY()
    UPROPERTY() FString RunId; // 结束轮次稳定ID；空表示尚无战报，客户端不将其作为任何交易凭据。
    UPROPERTY() uint32 EndPhaseRevision = 0; // 绑定本World本次终局阶段，防公开GS先到时误显示上一轮同关战报。
    UPROPERTY() EDemoRunOutcome Outcome = EDemoRunOutcome::SystemError; // 服务器确认的结果。
    UPROPERTY() EDemoDifficulty Difficulty = EDemoDifficulty::Normal; // 结束时难度，回Hub选择新难度不改旧战报。
    UPROPERTY() bool bEndless = false; // 无尽关数无十关上限，仍明确显示实际完成数。
    UPROPERTY() int32 ReachedStage = 0; // 本次到达的逻辑关，未出发的Hub为0。
    UPROPERTY() int32 CompletedStages = 0; // 只统计已提交胜利；当前未完成Combat不计入。
    UPROPERTY() EDemoObjectiveKind LastObjective = EDemoObjectiveKind::KillAll; // 结束所在任务类型。
    UPROPERTY() int32 ObjectiveCompleted = 0; // 当前任务已完成子目标，系统错误仍展示已确认快照。
    UPROPERTY() int32 ObjectiveTotal = 0; // 子目标总数，歼灭不借此推导击杀数。
    UPROPERTY() FDemoRunBuildSnapshot Build; // 清理前核心与3/7分支，保留名称所需稳定ID。
    UPROPERTY() int32 TeamKills = 0; // 原个人账本中的团队累计击杀，不宣称个人最后一击数量。
    UPROPERTY() int32 AwardedGold = 0; // 本轮GrantVictory实际提交的钱包增量，含余额上限裁切；失败为0。
    UPROPERTY() int32 GoldBalance = 0; // 清理前金币余额，结束时不会因银币清理被扣除。
    UPROPERTY() int32 SilverRemoved = 0; // 结束时将清除的实际银币余额，系统错误为0。
    UPROPERTY() int32 PistolChallenge = 0; // 清理前资格，用于显示困难纯手枪成就，不能从重置后的0推断。
    UPROPERTY() bool bTemporaryReset = false; // 真实通关/团灭将清临时成长；系统错误为false。
    UPROPERTY() float PermanentHealth = 0.f; // 原金币健康成长保留量HP，不含100基础生命。
    UPROPERTY() float PermanentDamage = 0.f; // 原金币伤害成长保留量。
    UPROPERTY() float PermanentMagazine = 0.f; // 原金币容量成长保留量，发。
    UPROPERTY() TArray<FName> UnlockedWeapons; // 本人当前保留的解锁，不公开给队友，不称为本次全部新增。
    UPROPERTY() FString Reason; // 系统错误的有限说明，不带存档正文或账号。
};

UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoRunSummaryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** PS拥有的服务器元数据；无独立复制，唯一网络出口是本人菜单快照。 */
    UDemoRunSummaryComponent();
    /** 奖励器在钱包Commit后记录实际增量；RunId去重，Amount不接受配置奖励替代真实到账。 */
    void RecordVictoryCredit(const FString& RunId, int32 Amount);
    /** Flow结束前冻结本人账本/核心；同一RunId最多一次，State只在同步调用中借用，返回是否新生成。 */
    bool Capture(const FString& RunId, const class ADemoGameState& State, EDemoRunOutcome Outcome, const FString& Reason);
    /** 服务器构建拥有者投影时同步拷贝；客户端HUD只能读取已收到的菜单值。 */
    const FDemoRunSummarySnapshot& GetSnapshot() const;
private:
    FDemoRunSummarySnapshot Snapshot; // PS内存中最近一次结束结果，旅行销毁，不写存档或云。
    FString CreditRun; // 最近实际发放金币的轮次，单次GrantVictory幂等事实。
    int32 CreditAmount = 0; // 对应该轮实际Commit的增量，0合法，不随配置热改。
};
