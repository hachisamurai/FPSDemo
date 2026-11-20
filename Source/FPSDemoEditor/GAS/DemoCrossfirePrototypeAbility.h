// 职责：B05交叉火力的Editor独立GAS候选。使用：当前单人Editor Combat显式启用；只观察真实近战/枪械伤害并追加一次非元素冲击。
// 依赖：原PS ASC、CombatSession、Health GE和Editor同步事实桥；不属于正式核心目录，不写V9/API/网络字段。
#pragma once
#include "CoreMinimal.h"
#include "GAS/Talents/DemoTalentAbility.h"
#include "Debug/DemoEditorCombatPrototype.h"
#include "DemoCrossfirePrototypeAbility.generated.h"

class ADemoCharacter;
class ADemoEnemy;
class UDemoCombatSession;
struct FOnAttributeChangeData;

/** Editor模块类型不进入非Editor目标；普通枪械和近战始终独立正常伤害，破势只提供额外冲击。 */
UCLASS(Transient,NotBlueprintable)
class FPSDEMOEDITOR_API UDemoCrossfirePrototypeAbility : public UDemoTalentAbility
{
    GENERATED_BODY()
public:
    /** 单PS服务器实例；激活期间监听本人的既有扣血事实，不授予正式天赋/核心权限。 */
    UDemoCrossfirePrototypeAbility();
    /** 仅当前Standalone游戏World、存活本机Avatar、有效CombatSession与本人PS来源允许激活。 */
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags=nullptr,const FGameplayTagContainer* TargetTags=nullptr,FGameplayTagContainer* OptionalRelevantTags=nullptr) const override;
    /** 激活后停止本人的正式RunBuild临时行为，保留选择；会话撤销/死亡/Avatar切换时结束原型，不在旧场自动恢复正式行为。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
    /** 新Avatar不得继承旧Avatar的候选/目标；引擎调用时只在已活跃且真实换人后结束。 */
    virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo,const FGameplayAbilitySpec& Spec) override;
    /** 精确解绑本人事实/Health委托、Timer和读取器；不移除其它GA/GE，不重置原技能冷却。 */
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled) override;
    /** Pawn只借用；显式Editor测试/控制台可调用，成功返回原ASC中的临时实例句柄，不写持久数据。 */
    static bool EnableForPlayer(ADemoCharacter* Pawn,FString& Error);
    /** 仅取消并移除本人当前候选Spec；没有候选时幂等成功，不影响其它玩家/能力。 */
    static bool DisableForPlayer(ADemoCharacter* Pawn,FString& Error);
    /** 本机HUD读取纯值；失效会话/Avatar/死亡返回空状态，过期凭据不显示为可消费。 */
    FDemoEditorCrossfireView ReadView() const;

    UPROPERTY(EditDefaultsOnly,Category="Prototype",meta=(ClampMin="0.1",ClampMax="10")) float WindowSeconds=3.f; // 同目标破势窗口秒；再次合法近战替换当前凭据。
    UPROPERTY(EditDefaultsOnly,Category="Prototype",meta=(ClampMin="0",ClampMax="2")) float ImpactRatio=.6f; // 近战源端基数的非元素冲击比例，不含原受害者部位倍率。
    UPROPERTY(EditDefaultsOnly,Category="Prototype",meta=(ClampMin="0",ClampMax="100")) float MaximumImpactDamage=60.f; // 每次冲击HP硬预算，不因天赋倍率再倍乘。
private:
    /** 高频只读门禁：同World/Pawn/PS/Session、存活、Combat、当前关一致；不访问文件或找玩家0。 */
    bool IsRuntimeValid() const;
    /** Source/Enemy为同步扣血事实；只接受本人有效近战且目标存活，将源端值冻结为一份有界凭据。 */
    void OnMeleeConfirmed(ADemoCharacter* Source,ADemoEnemy* Enemy,float RawDamage,bool bKilled);
    /** 原射击事务成功后登记ShotId；只在已有破势的有效窗口内接收，最多128个，旧飞行弹或准备失败不占资格。 */
    void OnShotCommitted(ADemoCharacter* Source,const FGuid& ShotId);
    /** 原枪伤已先结算；本人同目标/有效ShotId/非派生直击才消费，再经原Health GE追加冲击，不能拦截普通射击。 */
    void OnDirectConfirmed(ADemoCharacter* Source,ADemoEnemy* Enemy,const FGuid& ShotId,float ActualDamage,bool bKilled,bool bSecondary,bool bFirstTarget);
    /** Health变化为原ASC同步回调；真实阵亡立即结束，不依赖下一帧HUD查询。 */
    void OnSourceHealthChanged(const FOnAttributeChangeData& Data);
    /** 原CombatSession同步撤销回调，取消本次候选与旧目标。 */
    void OnCombatEnded();
    /** World游戏时间到期只清破势，候选仍可通过下一次近战重新开始。 */
    void ExpireCharge();
    /** 消费/到期/退出共用清理；Timer属于同World，目标仅弱引用，无迟到结算。 */
    void ClearCharge();
    TWeakObjectPtr<ADemoCharacter> SourcePawn; // 原激活Avatar；ASC持有GA，GA不延长Pawn生命周期。
    TWeakObjectPtr<UDemoCombatSession> Session; // 本次正式Flow会话，撤销后永不复用。
    TWeakObjectPtr<ADemoEnemy> ChargedTarget; // 当前唯一破势对象，死亡/销毁不转移给其它敌人。
    float ChargeExpiresAt=0.f; // 服务器World游戏秒，暂停不流逝，不写存档。
    float FrozenImpactDamage=0.f; // 近战成功时冻结的有界预算HP，不取飞行弹命中时的新配置。
    int32 EncounterLevel=0; // 激活时关号，异步/人工改阶段不允许跨关结算。
    int32 ConsumedCharges=0; // 本次激活调试计数，不复制/保存/发奖。
    int32 ConfirmedImpacts=0; // 原Health GE实际额外HP损失次数，不把免疫当作命中成功。
    bool bStopping=false; // 同步GE/死亡/会话回调可能重入，只执行一次退出。
    FTimerHandle ChargeTimer; // World拥有的UObject到期回调；EndAbility精确清理。
    FDelegateHandle MeleeBinding; // 仅本候选实例的全局弱委托句柄。
    FDelegateHandle ShotBinding; // 成功发射时序监听，结束精确解绑。
    FDelegateHandle DirectBinding; // 仅本候选实例的原始直击监听句柄。
    FDelegateHandle HealthBinding; // 原ASC生命值变化绑定，结束时移除。
    TSet<FGuid> EligibleShots; // 当前唯一3秒凭据之后成功发射的ShotId，硬上限128，消费/替换/到期/退出清空。
};
