// 职责：PlayerState核心选择/互斥分支/复制与行为入口。使用：Flow显式Begin/End，UI仅请求选择；依赖本人既有ASC/Talent组件，不拥有钱包或推进关卡。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "DemoRunBuildComponent.generated.h"
class ADemoCharacter;
class ADemoEnemy;
class ADemoWeaponBase;
class UAbilitySystemComponent;
class UDemoRunBuildAbility;
class UDemoRunBuildEventContext;

UCLASS(ClassGroup=(Demo),meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoRunBuildComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 创建无Tick复制组件；必须作为PS默认子对象，不挂易销毁Pawn。 */
    UDemoRunBuildComponent();
    /** CoreId只允许中央七核心目录；权威Hub、未锁定时替换，清旧分支，不自动保存/推进。 */
    bool TrySelectCore(FName CoreId);
    /** Stage只接受已完成3/7，Flow负责成功事实；幂等拒绝已领/不合法顺序，不自发新奖励。 */
    bool OfferBranch(int32 Stage);
    /** 完整BranchId必须属于当前待选组；服务器成功后清资格并增加修订号。 */
    bool TrySelectBranch(FName BranchId);
    /** 出发前依赖预检，Error为失败原因；不要求CombatSession、不锁核心/清计数/激活GA，可幂等授予原ASC事件GA，失败保留Hub/关间状态。 */
    bool PrepareEncounter(FString& Error);
    /** Prepare成功且CombatSession/Combat阶段已提交后调用，锁核心并开启事件GA；旧模式不要调用，未选中拒绝。 */
    void BeginEncounter();
    /** 离开战斗清临时计数/Timer/GE，保留选择和基础技能冷却，普通个人死亡不要调用。 */
    void EndEncounter();
    /** 整轮结束/团灭/放弃清全部核心临时成长；不改永久成长、弹药或金币。 */
    void ResetRun();
    /** 读取检查点选择值，只借用至本同步调用结束；不含战斗层数。 */
    const FDemoRunBuildSnapshot& GetSnapshot() const;
    /** 权威检查点恢复；严格验证后应用，清战斗瞬时状态，等待Flow显式Begin。 */
    bool RestoreSnapshot(const FDemoRunBuildSnapshot& Value);
    /** UI/准备门禁的纯只读状态；无隐式默认。 */
    bool HasSelectedCore() const;
    /** 当前服务器是否启用了新内容行为；不能把Hub窗口带进Combat。 */
    bool IsEncounterEnabled() const;
    /** 返回本人HUD确认状态；客户端依赖OwnerOnly复制，不自行裁决充能。 */
    const FDemoRunBuildRuntimeView& GetRuntimeView() const;
    /** 服务器行为GA发布本人计数快照，低频事件调用，不每帧复制。 */
    void SetRuntimeView(const FDemoRunBuildRuntimeView& Value);
    /** Pawn为同步借用；从它真实PlayerState寻找本人组件，绝不使用Player0。 */
    static UDemoRunBuildComponent* FindForPawn(const ADemoCharacter* Pawn);
    /** 查询原PS ASC，返回借用指针，禁止替换或另建ASC。 */
    UAbilitySystemComponent* GetASC() const;
    /** 准备整枪时只冻结窗口，FiringWeapon为原准备枪借用；最终扣弹并确定发射才CommitShot。 */
    FDemoRunBuildShotSnapshot PreviewShot(ADemoWeaponBase* FiringWeapon=nullptr);
    /** 成功发射消费一次充能并写回全序号；ShotId为整枪GUID，FiringWeapon只借用原枪；失败准备/空仓不调用。 */
    void CommitShot(FDemoRunBuildShotSnapshot& Shot,const FGuid& ShotId,ADemoWeaponBase* FiringWeapon);
    /** 真实伤害前查询核心乘区，Enemy空表示设施；HitDistance为发射相机到命中点cm，bSameFirstEnemy兼容多弹丸同首敌，派生必须bSecondary。 */
    float GetDirectMultiplier(const FDemoRunBuildShotSnapshot& Shot,const FGuid& ShotId,ADemoEnemy* Enemy,bool bWeakPoint,bool bFirstEnemy,bool bSameFirstEnemy,float HitDistance,bool bSecondary);
    /** 实际扣血后提交；Damage为实际HP，OverkillDamage为致死过量伤害，HitPoint/HitDistance/弹丸计数用于多弹丸与B08/B09结算。 */
    void NotifyDirectHit(const FDemoRunBuildShotSnapshot& Shot,const FGuid& ShotId,ADemoEnemy* Enemy,float Damage,float OverkillDamage,const FVector& HitPoint,float HitDistance,bool bWeakPoint,bool bKilled,bool bFirstEnemy,bool bSameFirstEnemy,bool bFirstTarget,int32 TargetPelletsHit,int32 ShotPelletsHit,bool bSecondary,ADemoWeaponBase* FiringWeapon);
    /** 权威冲刺已提交后记录起点和Duration秒；结束比较实际平面位移，同一Avatar/会话才充能。 */
    void NotifyDashStarted(ADemoCharacter* Pawn,float Duration);
    /** 近战扣血后调用；RawDamage为源HP基数，ActualDamage为实际HP，FrozenBefore为命中前快照，Direction世界推力；true时接管普通击退。 */
    bool NotifyMeleeHit(ADemoEnemy* Enemy,float RawDamage,float ActualDamage,bool bFrozenBefore,bool bKilled,const FVector& Direction);
    /** 整个装填GA的起始/二次按键/最终完成事实，Sequence为首次装填序号；中断无收益。 */
    void NotifyReloadStarted(ADemoWeaponBase* Weapon,int32 SessionSequence);
    bool NotifyReloadPressed(ADemoWeaponBase* Weapon);
    void NotifyReloadFinished(ADemoWeaponBase* Weapon,int32 SessionSequence,bool bSuccess);
    /** 实际换装提交后通知；原武器可空，不把旧弹收益赠新枪。 */
    void NotifyWeaponChanged(ADemoWeaponBase* OldWeapon,ADemoWeaponBase* NewWeapon);
    /** 所有已准备弹丸终结，整枪仅一次；传递整枪累计实伤/弱点/弹丸数供B03/B04/B08结算。 */
    void NotifyShotResolved(const FDemoRunBuildShotSnapshot& Shot,const FGuid& ShotId,bool bHadEnemyHit,bool bHadWeakPoint,int32 TotalPelletsHit,float TotalDamage,ADemoEnemy* PrimaryEnemy,const FVector& PrimaryImpact,ADemoWeaponBase* FiringWeapon);
    /** 正常G标点成功后附加部署请求，失败不能吞原标点。 */
    void NotifyPingAccepted(FVector Location);
    /** Health GE确认真实HP损失且玩家仍活时调用；只接受合法敌人来源。 */
    void NotifyPlayerDamaged(ADemoEnemy* SourceEnemy,float ActualDamage);
    /** 目标身上由本玩家施加的B09-D猎杀标定被合法击杀时调用，回满蓄势并从备弹补1发。 */
    void NotifyMarkedTargetKilled(ADemoEnemy* Enemy,UDemoRunBuildComponent* Killer);
    /** 返回当前开镜移速惩罚抵消比例0..1（仅B09-C蓄势>=50%生效），服务端与拥有者客户端共用复制投影，无额外网络字段。 */
    float GetAimMoveMitigation() const;
    /** 个人死亡即时撤销瞬态策略/锚点/计数，保留本轮核心与分支选择以及会话启用资格。 */
    void NotifyAvatarDefeated();
    /** 公开仅核心ID，钱包/分支/计数仍仅拥有者；引擎复制注册。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    /** 销毁时取消行为与Timer，随后由原Talent组件撤销自己授予的句柄。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(Replicated,BlueprintReadOnly,Category="Demo|Build") FName PublicCoreId; // 队伍只读公开身份，无个人数据。
private:
    /** 确保由原Talent组件授予行为GA并取得当前PS实例；无第二句柄管理器。 */
    UDemoRunBuildAbility* EnsureBehavior();
    /** 在本调用栈向原ASC发送同步事件，Event对象由调用方强持有；行为GA写回结果。 */
    void Dispatch(UDemoRunBuildEventContext* Event);
    /** 借用当前同步深度的事件对象并重置字段；最多8层防回调递归，无异步持有，不每颗弹丸分配UObject。 */
    UDemoRunBuildEventContext* AcquireEvent();
    /** 只在选择/恢复发生变更时推送复制，不广播每帧日志。 */
    void PublishSelection();
    /** OwnerOnly视图到达客户端时同步刷新开镜移速（B09-C），不增加复制字段。 */
    UFUNCTION() void OnRep_RuntimeView();
    UPROPERTY(Replicated) FDemoRunBuildSnapshot Snapshot; // OwnerOnly完整选择；保存通过显式快照API。
    UPROPERTY(ReplicatedUsing=OnRep_RuntimeView) FDemoRunBuildRuntimeView RuntimeView; // OwnerOnly确认的计数和截止秒。
    bool bEncounterEnabled=false; // 仅服务器Flow写入，旧流程默认关闭；不持久化。
    TWeakObjectPtr<UDemoRunBuildAbility> Behavior; // ASC保活的事件实例，组件不接管GA句柄，EndEncounter不额外授予。
    TWeakObjectPtr<class UDemoCombatSession> EnabledSession; // 当前显式启用会话，重复Begin幂等，不刷新已经运行的计数。
    UPROPERTY(Transient) TArray<TObjectPtr<UDemoRunBuildEventContext>> EventPool; // PS保活的最多8个同步事件容器，避免高射速GC压力。
    int32 EventDepth=0; // 游戏线程RAII同步调用深度；不是异步队列，退出栈自动回退。
};
