// 职责：目标共享核心控制门禁与公开易伤标记。使用：首次合法核心命中时由服务器添加到Enemy；不保存玩家计数，Boss不冻结/不击退。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "RunBuildTargetState.generated.h"
class ADemoEnemy;
class UDemoRunBuildComponent;
UCLASS()
class FPSDEMO_API UDemoRunBuildTargetState : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 事件驱动无Tick；仅公开标记截止复制，来源身份与控制门禁不向客户端开放。 */
    UDemoRunBuildTargetState();
    /** Enemy由World持有；Create只允许权威，已有组件直接借用。 */
    static UDemoRunBuildTargetState* Find(ADemoEnemy* Enemy,bool bCreate);
    /** 满计数调用；小怪短冻不延长已有冻结，Boss仅给Source独立易碎；目标CD共享。 */
    bool TryIceTrigger(UDemoRunBuildComponent* Source);
    /** 查询Boss专属易碎与公开标记乘区；ShotId排除制造标记的原枪，Consume仅在实际扣血后true。 */
    float ResolveMarks(UDemoRunBuildComponent* Source,const FGuid& ShotId,bool bConsume);
    /** 精准强化弱点实际扣血后发布D标记；串行覆盖旧来源/截止，不叠加。 */
    void Expose(UDemoRunBuildComponent* Source,const FGuid& ShotId);
    /** B08-D震颤破甲与B09-D猎杀标定共用有时限公开易伤；不随命中消费、刷新期限，不改变现有网络复制结构。 */
    void ApplyTimedVulnerability(UDemoRunBuildComponent* Source,const FGuid& ShotId,float Seconds,float Bonus,bool bNotifyOnKill);
    /** 目标真实死亡后触发：若挂有B09-D标定且击杀者合法（队友或单人本人），通知标定者满蓄并补弹。 */
    void NotifyKilled(UDemoRunBuildComponent* Killer);
    /** 冰裂C实际命中后给存活小怪减速；替换同核心旧GE，不多层叠加。 */
    void ApplyBurstSlow(UDemoRunBuildComponent* Source);
    /** 到期移除公开标记GE，Source/Timer属于本World，游戏暂停不推进。 */
    void ExpireExpose();
    /** 有时限易伤到期清理，不干扰仍在生效的单次精准暴露。 */
    void ExpireTeamDebuff();
    /** 目标销毁时清Timer和本组件持有的GE/弱引用。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 只复制公开标记截止，不复制个人Boss标记或冻结计数。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Demo|Build") float ExposedEndTime=0.f; // World服务器游戏秒，0表示无公开标记。
private:
    /** 同步刷新公开ExposedEndTime及Tag容器，兼顾单次精准暴露与有时限易伤。 */
    void SyncPublicExposeVisual(UDemoRunBuildComponent* Source);
    float NextIceTime=0.f; // 同一敌人全队共享冻结/易碎触发CD。
    TMap<TWeakObjectPtr<UDemoRunBuildComponent>,float> BossMarks; // 每个玩家独立2秒标记，目标共享门禁限制轮番触发。
    float SingleExposeEndTime=0.f; // B03-D单次消费精准暴露截止World秒。
    TWeakObjectPtr<UDemoRunBuildComponent> ExposeSource; // 标记制造者，团队模式不能自己消费。
    FGuid ExposeShot; // 制造标记的整枪不能消费自身派生标记。
    float TeamDebuffEndTime=0.f; // B08-D/B09-D有时限公开易伤截止World秒，不随命中消费。
    float TeamDebuffBonus=0.f; // 有时限公开易伤增伤比例。
    TWeakObjectPtr<UDemoRunBuildComponent> TeamDebuffSource; // 施加者Build组件，用于B09-D击杀回调。
    FGuid TeamDebuffShot; // 施加标记的原枪不重复享受自己刚挂上的易伤。
    bool bTeamDebuffNotifyKill=false; // true仅由B09-D开启，击杀时通知施加者。
    FActiveGameplayEffectHandle ExposeEffect; // 动态公开Tag由原Enemy ASC复制。
    FActiveGameplayEffectHandle SlowEffect; // 精确替换冰裂减速，不移除弹药/其他天赋效果。
    FTimerHandle ExposeTimer; // B03-D单次回调，EndPlay清理。
    FTimerHandle TeamDebuffTimer; // 有时限易伤到期回调，EndPlay清理。
};
