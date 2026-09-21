#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Game/DemoCombatConfig.h"
#include "DemoRewardComponent.generated.h"

/** GameMode持有的权威奖励服务；只发放收益，阶段推进、UI与检查点由玩法协调器处理。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoRewardComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick；订阅刷怪器已确认的击杀，不扫描世界Actor。 */
    UDemoRewardComponent();
    /** 所有默认子组件已创建后绑定刷怪事件，UObject弱委托随World有效。 */
    virtual void BeginPlay() override;
    /** EndPlayReason为World卸载原因，精确解绑事件。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** RunId为当前整轮身份、Difficulty为已验证金币配置；仅真实战役清场发放一次金币，不改变玩法阶段。 */
    bool GrantVictory(const FString& RunId, const FDemoDifficultyRow& Difficulty);
    /** RunId为刚结算轮次；GameMode切到Victory后提交永久解锁，满足Profile的真实终局校验。 */
    void RecordVictoryUnlocks(const FString& RunId);
    /** RunId/Choice为当前轮和0..2能力选项；Reward阶段有效，一关只能领取一次，读档由已保存阶段去重。 */
    bool GrantAbilityChoice(const FString& RunId, int32 Choice);
private:
    /** Enemy来自刷怪器移除成功的同步通知；不允许外部直接给任意Actor发击杀奖励。 */
    void HandleEnemyDefeated(class ADemoEnemy* Enemy);
    TWeakObjectPtr<class UDemoEnemySpawnComponent> Spawner; // 同Owner组件弱引用，不延长旧World生命。
    FString LastVictoryRun; // 本World最近发金币的轮次；持久化仍使用Victory/Hub检查点和Profile通关事实。
    FString LastChoiceRun; // 本World最后领取能力的轮次，允许新Run从第一关重新领取。
    int32 LastChoiceLevel = 0; // 与RunId组合防重复，恢复Intermission不经过领奖入口。
};
