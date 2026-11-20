// 职责：B06战术锚点及A/B/C/D分支的服务器瞬态策略。使用：原RunBuildAbility持有并同步投递事实；依赖基类GAS结算、CombatSession、导航和纯表现Actor。
#pragma once
#include "CoreMinimal.h"
#include "GAS/Talents/Builds/DemoBuildRuntime.h"
#include "DemoAnchorBuildRuntime.generated.h"

class ADemoTacticalAnchorActor;
class ADemoCharacter;
class UDemoRunBuildComponent;
class UDemoRunBuildEventContext;

UCLASS(Transient)
class FPSDEMO_API UDemoAnchorBuildRuntime : public UDemoBuildRuntime
{
    GENERATED_BODY()
public:
    /** 接受正常G标点成功事实和本人有效直击；每次重新验证原Avatar/会话，绝不影响正常标点结果。 */
    virtual void Handle(UDemoRunBuildEventContext& Event) override;
    /** 最终撤销入口：先清锚点/唯一维护Timer/迁移凭据，再清基类的原Avatar与会话引用。 */
    virtual void ResetRuntime() override;
    /** ActorBuild是本次真实射手；所有核心直击后调用一次，仅D锚点接受队友贡献，固定预算不乘队伍人数。 */
    static void NotifyTeamHit(UDemoRunBuildComponent* ActorBuild, const UDemoRunBuildEventContext& Event);
private:
    /** 验证固定地面落点；Point是服务器正常Ping命中点，OutGround为近邻NavMesh+实际地面+视线均通过的地点。 */
    bool TryGroundPoint(const FVector& Point, FVector& OutGround) const;
    /** 部署本人唯一锚点；合法C迁移可以绕过一次部署冷却，坏落点不消耗该凭据。 */
    void Deploy(const FVector& Point);
    /** 接受本人或D队友的实际HP直击；同ShotId只计一次，B兑现只允许本人且必须是充满后的下一枪。 */
    void ReceiveHit(UDemoRunBuildComponent* ActorBuild, const UDemoRunBuildEventContext& Event);
    /** 0.1秒只查本人位置/窗口/寿命，不扫描敌人；进入后离开由A/基础兑现或B作废。 */
    void Maintain();
    /** 发布充能和迁移/冷却状态，并让纯表现Actor与服务器计数一致。 */
    void RefreshPresentation();
    /** 消费锚点后做一次有界非元素脉冲；Direction为B本人当次服务器朝向，零方向为A/基础整圆。 */
    void Release(const FVector& Direction);
    /** 清理当前锚点但保留运行实例/冷却；Reason仅日志，不能在普通部署链切换时调用基类Reset。 */
    void ClearAnchor(FName Reason);
    /** Character仅同步借用；区域为水平半径+300cm高度容差，防止不同楼层共享充能。 */
    bool IsInside(const ADemoCharacter* Character) const;
    /** 创建或复用唯一UObject定时回调；策略由GA保活，撤销时显式清理。 */
    void EnsureMaintenance();
    TWeakObjectPtr<ADemoTacticalAnchorActor> Anchor; // 仅当前活跃Actor，释放/取消先清引用，不保存。
    FVector Center = FVector::ZeroVector; // 固定地面锚点世界坐标厘米，不跟随标记敌人移动。
    FTimerHandle MaintenanceTimer; // 本策略唯一0.1秒游戏线程Timer，暂停跟随World。
    TMap<FGuid, int32> ShotCharges; // 当前锚点已计ShotId及已贡献格数（1或2），硬上限256，防止同枪重复计数或同枪触发B06-B。
    int32 Charge = 0; // 一档0..配置阈值，D共享同一个计数。
    float ChargeEndTime = 0.f; // 第一枪开始的固定充能窗口，秒；新命中不延期。
    float AnchorEndTime = 0.f; // 本锚点绝对游戏时间寿命，不因G/进出/充能而刷新。
    float NextDeployTime = 0.f; // 部署冷却截止，取消/离开不返还冷却。
    float RelayEndTime = 0.f; // C完整伤敌后可用迁移凭据的截止秒，不持久化。
    float ActiveRadius = 350.f; // 当前部署冻结的厘米半径，与公开Actor保持一致，配置编辑只影响下次部署。
    float ChargeWindowSeconds = 8.f; // 当前部署冻结的固定充能窗口秒数。
    float PulseDamage = 55.f; // 当前部署冻结的非元素派生HP请求值，实际扣血由基类返回。
    float PulseConeCos = .5f; // 当前部署冻结的兼容半角余弦。
    float DirectedRange = 1000.f; // 当前部署冻结的B06-B中远程定向冲击射程cm。
    float DirectedConeCos = .85f; // 当前部署冻结的B06-B中远程窄锥余弦。
    int32 PelletDoubleThreshold = 5; // 多弹丸单枪命中达到此值贡献第2格充能。
    float RelayWindowSeconds = 3.f; // 当前部署冻结的C一次迁移凭据秒数。
    int32 ChargeLimit = 4; // 当前部署冻结的一档阈值，不随运行时设置编辑而改变。
    int32 PulseTargetLimit = 6; // 当前部署冻结的最多实际查询/结算目标数。
    bool bEntered = false; // 在前方部署可先接近；只有曾进入后离开才判撤离/失败。
    bool bRelayPlacement = false; // 当前是否已消费该链唯一迁移；第二锚点绝不再产迁移凭据。
};
