// 职责：七核心统一GAS事件与旧三核心行为；B04～B07委托独立瞬态策略。原Talent授予PS ASC，派生伤害来源防递归。
#pragma once
#include "CoreMinimal.h"
#include "GAS/Talents/DemoTalentEventAbility.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "NativeGameplayTags.h"
#include "RunBuildAbility.generated.h"
class UDemoRunBuildComponent;
class UDemoCombatSession;
class ADemoEnemy;
class ADemoWeaponBase;
class ADemoCharacter;
class UDemoBuildRuntime;

namespace DemoRunBuildTags { FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action); FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Exposed); }

/** 同步事件类型；不序列化、不接受客户端直接构造。 */
enum class EDemoRunBuildAction : uint8 { PreviewShot,CommitShot,BeforeHit,AfterHit,DashStarted,Melee,ReloadStarted,ReloadPressed,ReloadFinished,WeaponChanged,ShotResolved,PingAccepted,PlayerDamaged,DashFinished,MarkedTargetKilled };

/** 同步GAS事件参数/结果容器，调用栈强持有，所有Actor仅弱引用。 */
UCLASS(Transient)
class FPSDEMO_API UDemoRunBuildEventContext : public UObject
{
    GENERATED_BODY()
public:
    EDemoRunBuildAction Action=EDemoRunBuildAction::PreviewShot; // 调用入口决定，网络端不能注入。
    FDemoRunBuildShotSnapshot Shot; // 发射快照输入或Preview输出。
    FGuid ShotId; // 枪械唯一去重键。
    TWeakObjectPtr<ADemoEnemy> Enemy; // 当前真实受害者，设施为空。
    TWeakObjectPtr<ADemoWeaponBase> Weapon; // 原发射武器，回收不能赠给换枪。
    TWeakObjectPtr<ADemoCharacter> Pawn; // Dash原Avatar。
    FVector Direction=FVector::ZeroVector; // 世界平面击退方向。
    float Damage=0.f; // AfterHit为实际HP；Melee为未乘目标倍率源端基数。
    float Duration=0.f; // Dash实际RootMotion时长秒。
    float Multiplier=1.f; // BeforeHit输出的额外直击乘区。
    bool bWeakPoint=false; // 真实骨骼弱点，不是随机暴击。
    bool bKilled=false; // 权威Health从存活到死亡。
    bool bFirstEnemy=false; // 同枪尚未有有效敌人直击。
    bool bSameFirstEnemy=false; // 多弹丸同枪命中同一名首敌（不含穿透第二敌），供散弹弱点适配。
    bool bFirstTarget=false; // 同枪同目标首次有效直击。
    bool bSecondary=false; // 弹射/派生，不积层、不消耗直击标记。
    bool bFrozenBefore=false; // 近战扣血前冻结快照。
    bool bAccepted=false; // 近战返回是否接管普通击退。
    float ActualDamage=0.f; // 近战真实HP损失，不替代旧冰裂的源伤害基数。
    int32 Sequence=0; // 整个Reload GA会话的初始序号，逐颗装填不重建会话。
    int64 Serial=0; // 世界游戏线程全序动作号，用于跨玩家动作先后比较。
    FVector Location=FVector::ZeroVector; // 服务器接受的G标点位置。
    FVector HitLocation=FVector::ZeroVector; // 实际碰撞或主要受击世界坐标cm。
    float Distance=0.f; // 已验证冲刺平面位移cm，失败为0。
    float HitDistance=0.f; // 发射相机到实际受击点距离cm。
    float OverkillDamage=0.f; // 致死直击超出目标剩余HP的过量伤害。
    int32 TargetPelletsHit=0; // 本枪对当前Enemy累计造成真实HP伤害的弹丸数。
    int32 ShotPelletsHit=0; // 本枪累计造成真实HP伤害的弹丸总数。
    float ShotTotalDamage=0.f; // 本枪所有弹丸累计真实HP伤害总和。
    bool bShotHadWeakPoint=false; // 本枪是否至少有一颗弹丸命中真实弱点。
    bool bSuccess=false; // 装填全流程自然完成，取消不授予收益。
    bool bHadEnemyHit=false; // 整枪所有弹丸已终结后的真实敌人HP命中事实。
    TWeakObjectPtr<ADemoEnemy> SourceEnemy; // 受到合法当前敌人伤害的来源。
};

/** 每玩家/目标冰裂计数，弱引用Map由GA拥有，离场清空。 */
struct FDemoRunBuildIceChain
{
    int32 Count=0; // 有效不同ShotId计数。
    float LastTime=0.f; // 最后有效命中的World秒。
};
/** 等待一次击退结果的破冰快照；不把伤害缓存在共享GE CDO。 */
struct FDemoRunBuildBurst
{
    float Damage=0.f; // 源端近战基数已乘核心比率的HP，不包含原受害者部位倍率。
    float Radius=0.f; // 本次范围厘米。
    bool bSlow=false; // 已冻结C分支。
    bool bHeal=false; // 已冻结D分支。
    TWeakObjectPtr<UDemoCombatSession> Session; // 原会话撤销后不能爆裂。
};

UCLASS()
class FPSDEMO_API UDemoRunBuildAbility : public UDemoTalentEventAbility
{
    GENERATED_BODY()
public:
    /** 单PS服务器事件GA，核心不进入原随机奖池。 */
    UDemoRunBuildAbility();
    /** 核心授权来自SourceObject组件而非原15天赋权限标签，仍保留GAS死亡与权威门禁。 */
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,const FGameplayTagContainer* SourceTags=nullptr,const FGameplayTagContainer* TargetTags=nullptr,FGameplayTagContainer* OptionalRelevantTags=nullptr) const override;
    /** 同步事件分发；Rank固定1，数值从集中Settings读取并冻结，不改旧天赋等级。 */
    virtual bool OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank) override;
    /** 高频每弹前置/后置事件使用限时VeryVerbose入口日志；同步完成并End，不继承普通低频事件的逐次Log输出。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
    /** Flow开启战斗后按需初始化主动计时策略（如B09首枪蓄势），其他核心保持按事件惰性创建。 */
    void OnEncounterStarted(UDemoRunBuildComponent* Build);
    /** Flow离场显式清理，个人死亡不删除核心选择；回调弱绑定，清理不刷新冲刺/治疗冷却。 */
    void ResetRuntime();
private:
    /** 本同步事件的SourceObject解析；返回原PS组件，不持有额外ASC。 */
    UDemoRunBuildComponent* GetBuild() const;
    /** 充能或计数过期按World秒清理，并发布一次OwnerOnly视图。 */
    void RefreshView();
    /** 冻结/提交整枪窗口，Prepare不耗层，Commit只在真正发射执行。 */
    void HandleShot(UDemoRunBuildEventContext& Event);
    /** 直伤前查询乘区、直伤后确认计数与标记；不提前消费无敌命中。 */
    void HandleHit(UDemoRunBuildEventContext& Event);
    /** 实际近战冻结命中生成一次破冰，Boss完全拒绝位移。 */
    void HandleMelee(UDemoRunBuildEventContext& Event);
    /** RootMotion预计结束时一次测量真实XY位移；原Avatar/Session失效立即退出。 */
    void FinishDash();
    /** 击退组件唯一终结回调；仅到终点/阻挡/落脚终点触发爆裂，取消/死亡不触发。 */
    void OnKnockbackEnded(ADemoEnemy* Enemy,FName Reason);
    /** 源目标/值快照同步借用，最多6个有效敌人，原受害者不二次受伤。 */
    void Explode(ADemoEnemy* Origin,const FDemoRunBuildBurst& Burst);
    /** 自身有限时移动GE，先撤上次句柄，不叠层、不改变基础速度。 */
    void ApplyAssaultMove();
    TMap<TWeakObjectPtr<ADemoEnemy>,FDemoRunBuildIceChain> IceChains; // 仅当前战斗，失效目标定期按事件清理。
    TMap<TWeakObjectPtr<ADemoEnemy>,FDemoRunBuildBurst> PendingBursts; // 每目标最多一个，击退替换/取消删除。
    TWeakObjectPtr<ADemoCharacter> DashPawn; // 原Avatar，重生后的新Pawn不能继承冲刺结果。
    TWeakObjectPtr<UDemoCombatSession> DashSession; // 同场凭据，Hub/Intermission不能充能。
    FVector DashStart=FVector::ZeroVector; // 服务器开始位置厘米。
    FTimerHandle DashTimer; // 单次游戏线程UObject回调；Reset/撤销时清。
    int32 PrecisionStacks=0; // 本人的首次有效敌人弱点链。
    float PrecisionLastTime=0.f; // 最后弱点World秒。
    int32 ChargedShots=0; // 突击0..2，精准0..1。
    float ChargeEndTime=0.f; // World秒截止，不使用墙钟。
    int32 ChargeGeneration=0; // 每次成功冲刺递增，飞行中旧枪保留自己的批次。
    TSet<int32> SpeedRewardedGenerations; // 本场已触发移速的冲刺批次，长场有界修剪。
    TSet<FGuid> PrecisionRewardedShots; // 多弹丸精准强化枪每枪最多兑现一次C返弹/D暴露。
    float NextShatterTime=0.f; // 个人破冰CD秒。
    float NextHealTime=0.f; // 个人回温CD秒。
    FActiveGameplayEffectHandle AssaultMove; // 仅此GA授予的移速GE句柄，离场精确清理。
    UPROPERTY(Transient) TObjectPtr<UDemoBuildRuntime> Expansion; // 每场原GA持有的独立核心策略，Reset后销毁，不复制/存盘。
};
