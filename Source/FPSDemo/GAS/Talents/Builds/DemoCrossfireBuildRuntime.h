// 职责：正式B05交叉火力及ABCD分支。使用：原RunBuild GA拥有策略；团队桥只转发服务器事实，弱目标凭据有界且先消费后GE。
#pragma once
#include "CoreMinimal.h"
#include "GAS/Talents/Builds/DemoBuildRuntime.h"
#include "DemoCrossfireBuildRuntime.generated.h"
class ADemoWeaponBase;

/** 每来源最多一个临时破势标记；不复制私有预算、不保存Actor，不延长目标/会话生命周期。 */
struct FDemoCrossfireMark
{
    TWeakObjectPtr<ADemoEnemy> Target; // 同目标兑现，禁止切换敌人挪用标记。
    TWeakObjectPtr<ADemoWeaponBase> Gun; // B反向路线起手原枪，C回弹只给这把枪。
    TWeakObjectPtr<class UDemoCombatSession> Session; // 原Combat凭据；新关即使同Actor也不能消费。
    int64 Serial=0; // 全服务器动作全序；先前飞行弹不能兑现后来近战标记。
    float EndTime=0.f; // World秒到期，一次性Timer及时撤HUD。
    float Budget=0.f; // 冻结近战/直击源乘率并夹上限后的总冲击HP预算。
    float Radius=0.f; // A范围厘米，创建时冻结。
    int32 Targets=0; // A其他敌人数上限，创建时冻结。
    int32 Rounds=0; // C从原枪备用弹回弹匣的发数，不生成弹药。
    bool bReverse=false; // B为枪起手→近战兑现，其他为近战→枪。
    bool bArea=false; // A把固定总预算分给主目标与遮挡检查后的其他敌人。
    bool bShared=false; // D只有下一位合法队友兑现；真单人回退自身。
};

UCLASS(Transient)
class FPSDEMO_API UDemoCrossfireBuildRuntime : public UDemoBuildRuntime
{
    GENERATED_BODY()
public:
    /** 正式核心自己的动作入口；原枪/近战伤害始终正常，只在真实HP命中后附加独立冲击。 */
    virtual void Handle(UDemoRunBuildEventContext& Event) override;
    /** GA撤销/死亡/离场先清标记、Timer、弱注册，再撤销基类会话。 */
    virtual void ResetRuntime() override;
    /** 所有参战者有效枪击后由共同桥调用；ActorBuild为出手者，最多原子消费一个D来源标记。 */
    static void NotifyTeamGun(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event);
    /** 所有参战者实际近战扣血后调用；支持B反向+D协同，来源预算只消费一次。 */
    static void NotifyTeamMelee(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event);
private:
    /** 游戏线程弱注册仅供本World团队hook查找；每World最多4个来源，无World Tick或强全局UObject。 */
    void RegisterRuntime();
    /** bGun指枪起手；只有真实存活目标可以创建，起手不会阻止/修改原伤害。 */
    void CreateMark(const UDemoRunBuildEventContext& Event,bool bGun);
    /** 只读验证ActorBuild/原会话/同目标/后发动作及D队友规则，不在遍历中施加GE。 */
    bool CanConsume(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event,bool bGun) const;
    /** 先移除凭据再扣血，防GE同步回调重入；原攻击已杀死目标时只消费，不攻击尸体。 */
    bool Consume(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event,bool bGun);
    /** 同World另有存活、已参战的Avatar时D必须让给别人；非观战者人数以原Build/ASC为准。 */
    bool HasOtherParticipant() const;
    /** Timer只撤销当前到期标记，替换后的新截止不会被陈旧回调清除。 */
    UFUNCTION() void OnMarkExpired();
    /** 撤销当前标记，可选择发布已确认Idle；Reset时不再发布旧场状态。 */
    void ClearMark(bool bPublish);
    /** 静态团队hook共用确定顺序选择器，遍历只读，最早Serial来源优先消费一次。 */
    static void NotifyTeam(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event,bool bGun);
    FDemoCrossfireMark Mark; // 每拥有者仅一个标记，四人同目标最多四来源。
    FTimerHandle MarkExpiryTimer; // 当前标记唯一UObject Timer，替换/Reset显式撤销。
    static TArray<TWeakObjectPtr<UDemoCrossfireBuildRuntime>> Registered; // 跨World弱表；失效/撤销及时修剪，不持久化。
};
