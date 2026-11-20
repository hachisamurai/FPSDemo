// 职责：射手HUD回执及相关客户端空间接触批次。使用：Pawn默认复制组件，实际扣血后提交；依赖World特效池/拥有者RPC，无伤害或存档权限。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "Combat/Feedback/DemoMetalImpactSubsystem.h"
#include "Combat/Feedback/DemoCombatFeelSettings.h"
#include "DemoHitFeedbackComponent.generated.h"

/** 服务器确认的纯表现值；不带目标Actor引用，尸体销毁或最后一杀切阶段也能送达。 */
USTRUCT()
struct FDemoHitFeedbackEvent
{
    GENERATED_BODY()
    UPROPERTY() FGuid ShotId; // 一次开火的全局唯一标识，散弹/穿透共享，不能使用当前装备替代。
    UPROPERTY() uint32 TargetId = 0; // 服务器目标实例ID，仅供本Pawn短期聚合，不解析为对象、不持久化。
    UPROPERTY() FVector_NetQuantize ImpactPoint = FVector::ZeroVector; // 真实碰撞点世界厘米，供射手投影，不作为客户端伤害输入。
    UPROPERTY() FVector_NetQuantizeNormal ImpactNormal = FVector::UpVector; // 真实接触面外法线，世界空间；池沿此方向喷出火花。
    UPROPERTY() bool bMelee = false; // 近战使用DA指定的3D接触声及独立池预算，不混入拥有者旧2D肉体声。
    UPROPERTY() bool bMetalTarget = false; // 设施直击使用空间金属声；与bMelee独立，枪弹仍受原弹击预算限制。
    UPROPERTY() bool bRedesigned = false; // 发射冻结的首批AB选择，最后一杀切阶段仍可交付实际扣血回执。
    UPROPERTY() EDemoImpactSurface Surface = EDemoImpactSurface::Armor; // 目标显式表面；不影响实际伤害倍率或权限。
    UPROPERTY() FName WeaponId; // 原发射武器稳定身份，切枪后不读取当前装备。
    UPROPERTY() float Damage = 0.f; // 实际损失HP之和，排除过量伤害、DOT和后续元素爆炸。
    UPROPERTY() bool bCore = false; // 保留网络协议字段名：本枪对此目标至少一颗实际命中 head 弱点；旧胸部 core 不再触发，不是随机暴击。
    UPROPERTY() bool bKilled = false; // 该直击使HP归零；金属设施显示“摧毁”，仅表现，不授予敌人击杀奖励。
    // 服务器端专用：设施本次扣血后的剩余HP比例0..1，供Confirm内SelectImpact选V7分档命中声。非UPROPERTY，不进入RPC序列化，网络协议不变；客户端收到恒为默认1。
    float DeviceHealthFraction = 1.f;
};

/** 本机显示缓存只含值，随Pawn销毁；HUD按年龄隐藏，不拥有任何敌人。 */
struct FDemoHitFeedbackDisplay
{
    FDemoHitFeedbackEvent Hit; // 同枪同目标累计值，网络分批到达仍合并。
    double StartedAt = 0.0; // 本地World秒；首次回执起算，连续弹丸不能无限延长飘字。
};

UCLASS(ClassGroup=(Demo), meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoHitFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** Pawn构造时建立拥有者RPC通道及肉体声硬引用；不Tick、不加载网络/存档服务。 */
    UDemoHitFeedbackComponent();
    /** Hit为实际扣血快照，ImpactSound为可选近战资源；记录近战冻结声源后由世界池限流，HUD独立合并，下一帧批量发出。 */
    void Confirm(const FDemoHitFeedbackEvent& Hit, class USoundBase* ImpactSound = nullptr);
    /** HUD同步借用有限缓存；不得跨帧保存引用。过期项由HUD隐藏，下次回执时移除。 */
    const TArray<FDemoHitFeedbackDisplay>& GetHits() const;
    /** 返回本地标记年龄秒；负值表示尚无回执，暂停跟随World时钟。 */
    float GetMarkerAge() const;
    /** 返回普通0/头部弱点1/致死或摧毁2；仅表现优先级，不参与伤害计算，设施不计敌人击杀。 */
    int32 GetMarkerKind() const;
    /** 当前最高优先级标记是否为设施摧毁；不读当前装备或目标Actor，避免目标销毁后误写“击杀”。 */
    bool IsDestructionMarker() const;
    /** EndPlayReason为Pawn/World卸载原因；撤销待发回调与本组件创建的声音，不跨地图遗留。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // 原生默认覆盖所有角色；单位秒，短于0.05无法阅读，长于1会遮挡连续射击。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback", meta=(ClampMin="0.05", ClampMax="1.0")) float HitMarkerSeconds = .24f;
    // 击杀稍长以便确认清怪；不因后续普通命中提前降级。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback", meta=(ClampMin="0.05", ClampMax="1.0")) float KillMarkerSeconds = .42f;
    // 飘字首次出现后的存活秒数；同枪累加但不反复重置，最多同时显示12条。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback", meta=(ClampMin="0.1", ClampMax="2.0")) float DamageNumberSeconds = .65f;
    // 可按角色BP关闭飘字，头部/击杀准星与服务器伤害不受影响。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback") bool bShowDamageNumbers = true;
    // 拥有者近耳肉体确认层线性音量；设施/近战使用空间接触池而跳过此层，0关闭补充层。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback", meta=(ClampMin="0", ClampMax="1")) float ConfirmationVolume = .28f;
private:
    /** UObject绑定的下一帧回调；先消费队列再发RPC，最后一杀切阶段不丢失已确认伤害。 */
    void FlushPending();
    /** Hits只由服务器发送；客户端合并显示及限流声音，丢包仅损失短期反馈。 */
    UFUNCTION(Client, Unreliable) void ClientConfirmHits(const TArray<FDemoHitFeedbackEvent>& Hits);
    /** Events为服务器准入且最多8条的短期批次；所有相关观众本地取池播放，丢包不补发。 */
    UFUNCTION(NetMulticast, Unreliable) void MulticastMetalImpacts(const TArray<FDemoMetalImpactEvent>& Events);
    /** Hit为本机已接收回执，Now是本地World秒；同枪仅一次，最多三声并发。 */
    void PlayConfirmation(const FDemoHitFeedbackEvent& Hit, double Now);
    /** Hit为冻结的新目录回执，Now为本地World秒；先消费最高等级账本（最多32枪），再按bPlayConfirmationSounds播射手专属层（普通OwnerBody、头部OwnerHead在命中点3D随距离衰减；击杀V5为2D）；同枪只升档，静音不影响HUD。 */
    void PlayRedesignedConfirmation(const FDemoHitFeedbackEvent& Hit, double Now);
    /** Wave由CDO保活；复用原3活动+1淡出预算，设置非UI后播放，不在命中加载资产。
     * SpatialPoint非空时在该世界点（cm）以Attenuation做3D播放（射手专属普通/头部层，用户2026-11-17要求“随距离变小”）；空则2D（击杀确认）。 */
    void PlayConfirmationWave(class USoundWave* Wave, float Volume, double Now, const FVector* SpatialPoint = nullptr, class USoundAttenuation* Attenuation = nullptr);
    // 硬引用已有三个肉体SoundWave，绕过3D Cue衰减，仅拥有者播放低音量确认层；Cook可追踪。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback") TArray<TObjectPtr<class USoundWave>> ConfirmationSounds;
    TArray<FDemoHitFeedbackEvent> Pending; // 服务器瞬时批次，上限16条，不含UObject引用。
    UPROPERTY() TArray<FDemoMetalImpactEvent> PendingImpacts; // 权威表现队列上限8；仅资源强引用，无目标Actor。
    FTimerHandle FlushTimer; // 本组件拥有的下一帧定时器，EndPlay精确撤销。
    TArray<FDemoHitFeedbackDisplay> Displays; // 本地最多12条，按首次接收时间过期。
    TArray<FGuid> HeardShots; // 本地最近32枪声音闩锁，跨目标/跨帧散弹不叠加近耳声音。
    TArray<FGuid> GradedShots; // 新候选最近32枪，顺序有界；与Grade数组同下标，不解析目标Actor。
    TArray<uint8> HeardGrades; // 已消费最高普通/弱点/致死档，后续降档或重复不再播主体。
    TArray<TWeakObjectPtr<class UAudioComponent>> PlayingSounds; // World自动销毁音频；只弱跟踪以限声部和卸载停止。
    TWeakObjectPtr<class UAudioComponent> FadingSound; // 最多一路25ms退场尾音；卸载同样停止，避免从活动数组移出后漏清理。
    double LastSoundAt = -100.0; // 本地World秒；至少75ms一次确认声，自动武器不会堆积低频。
    double MarkerStartedAt = -100.0; // 当前优先级的显示起点，普通命中不能刷新并延长击杀标记。
    int32 MarkerKind = 0; // 普通0/头部弱点1/致死或摧毁2，仅本机值，不复制、不保存。
    bool bMarkerDestruction = false; // 与当前MarkerKind同一事件更新；设施摧毁不被后续普通命中改成敌人击杀。
};
