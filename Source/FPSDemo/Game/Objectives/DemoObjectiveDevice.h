// 职责：设施目标的独立GAS生命和复制表现。由目标协调器准备/激活/回收；真实弹体或近战经IDemoDamageableTarget进入，不是Enemy、不发奖励。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"
#include "Combat/DemoDamageableTarget.h"
#include "DemoObjectiveDevice.generated.h"

class UAbilitySystemComponent;
class UDemoAttributeSet;
class UDemoCombatSession;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
struct FOnAttributeChangeData;

/** 本帧HUD借用的纯值视图；不包含服务端Session或Actor引用，不复制第二套生命。 */
struct FDemoObjectiveDeviceView
{
    FVector Anchor = FVector::ZeroVector; // 当前网格包围盒上方的世界cm位置，供各本地相机投影。
    float Health = 0.f; // 来自同一组复制展示HP，不由HUD递减。
    float MaxHealth = 1.f; // 当前关配置倍率后的上限，显示时防御零值。
    bool bDestroyed = false; // 设施破坏事实，不能当作敌人死亡或奖励资格。
};

/** 击碎事实同步通知协调器；不在Actor内推进关卡/派奖。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FDemoObjectiveDeviceBroken, class ADemoObjectiveDevice*);

/** 设施专属火DOT模板，无敌人控制/阈值事件；会话结束时由设施整体撤销。 */
UCLASS()
class FPSDEMO_API UDemoObjectiveBurnEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** CDO固定周期加法Health模板；实际时长/频率/伤害来自发射快照，禁止修改CDO。 */
    UDemoObjectiveBurnEffect();
};

UCLASS(Blueprintable)
class FPSDEMO_API ADemoObjectiveDevice : public AActor, public IAbilitySystemInterface, public IDemoDamageableTarget
{
    GENERATED_BODY()
public:
    /** 创建独立ASC/属性/白模立方体，构造期硬引用基础形状，命中时不读盘。 */
    ADemoObjectiveDevice();
    /** 服务器在Prepare时初始化会话/HP，始终先保持免伤；InSession须同World当前有效凭据。 */
    bool PrepareDevice(UDemoCombatSession* InSession, float MaxHealth);
    /** 协调器Activate后才允许损血；撤销时关伤害并移除本设施持有的DOT。 */
    void SetObjectiveActive(bool bActive);
    /** GAS标准接口；本Actor独占ASC，不能拿Enemy/Player ASC替代。 */
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    /** 真实服务器命中请求；无弱点/控制/击退/奖励，只有实际HP损失返回数值。 */
    virtual FDemoTargetDamageResult ApplyTargetDamage(const FDemoTargetDamageRequest& Request) override;
    /** 返回本场是否已经正常击碎；异常Destroy不冒充完成。 */
    bool IsBroken() const;
    /** 每帧只读复制状态与网格包围盒；文字由各玩家HUD投影，Actor不旋转去追踪某个玩家。 */
    FDemoObjectiveDeviceView GetStatusView() const;
    /** 初始化ASC Avatar和复制文本；服务器属性委托只绑定本Actor。 */
    virtual void BeginPlay() override;
    /** 仅本地闪光存活期间启用；DeltaSeconds为World秒，淡出后停Tick，专服不运行表现。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 解除Health委托并撤销DOT；Reason为引擎卸载原因，不通知击碎。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** OutLifetimeProps注册公共健康/破坏状态；属性也由ASC复制但UI不依赖其到达顺序。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    FDemoObjectiveDeviceBroken OnBroken; // 协调器弱UObject订阅，同步事实，回收前解绑。
private:
    /** Data为ASC委托；冻结实际健康，首次到零先闩锁再发事实，防GE同步重入。 */
    void HandleHealthChanged(const FOnAttributeChangeData& Data);
    /** 客户端复制或服务器属性改变时触发损血短闪/关闭破坏碰撞，破坏边沿播摧毁爆炸；HP文字由HUD消费，不负责扣血。 */
    UFUNCTION() void OnRep_Presentation();
    /** DeltaSeconds为同World秒；只改本设施MID，销毁时立即恢复，不改变碰撞或任务完成时序。 */
    void UpdateHitFlash(float DeltaSeconds);
    /** 本机观察到破坏边沿时调用（OnRep_Presentation，Listen房主同步）：在网格中心播摧毁爆炸3D声（当前为V8实录）；目录缺失静默跳过，专服不调用，不影响完成事实。 */
    void PlayDestroyExplosion();
    /** 会话撤销统一关闭伤害并移除DOT；UObject绑定，不捕获裸指针。 */
    void HandleSessionEnded();
    UPROPERTY(VisibleAnywhere, Category="Objective") TObjectPtr<UStaticMeshComponent> Body; // 设施可配置网格；唯一真实阻挡体，Actor拥有。
    UPROPERTY(VisibleAnywhere, Category="Objective") TObjectPtr<UTextRenderComponent> Label; // 保留旧组件名兼容引用，但隐藏世界文字；每位玩家HUD独立显示，避免背面反字。
    UPROPERTY(EditDefaultsOnly, Category="Objective|Feedback", meta=(ClampMin="0", ClampMax="0.4")) float HitFlashSeconds = .16f; // 与敌人同节奏的本地受击脉冲秒数，0禁用。
    UPROPERTY(EditDefaultsOnly, Category="Objective|Feedback") FName HitFlashParameter = TEXT("EnemyTint"); // 默认复用敌人发光材质参数；自定义网格材质需提供同名向量。
    UPROPERTY(EditDefaultsOnly, Category="Objective|Feedback") FLinearColor IdleTint = FLinearColor(.08f, .12f, .16f); // 白模设施基础颜色，每个MID独立，短闪后恢复。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlashMaterial; // BeginPlay创建一次，Actor持有；射击不反复分配MID。
    FLinearColor FlashPeakTint = FLinearColor::White; // 当前已确认损血的HDR色，普通冷白、破坏暖红，与敌人共用算法。
    float FlashRemaining = 0.f; // 当前本地脉冲剩余World秒，不复制、不写存档。
    float FlashDuration = .16f; // 本次脉冲冻结总时长；更新配置不造成除零。
    float LastPresentedHealth = 0.f; // 上次消费的复制生命，仅判断下降边沿，不用于GAS计算。
    bool bHasPresentedHealth = false; // 首次复制快照只建基线，迟加入不能把已有损伤当刚击中。
    bool bPresentedBroken = false; // 本机上次消费的破坏状态，仅判断破坏边沿播一次爆炸；不复制、不写存档。
    UPROPERTY(VisibleAnywhere, Category="Objective") TObjectPtr<UAbilitySystemComponent> AbilitySystem; // Actor拥有的复制ASC，Minimal效果复制。
    UPROPERTY() TObjectPtr<UDemoAttributeSet> Attributes; // Actor强持有生命属性，扣血只经GE。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) float VisibleHealth = 0.f; // 当前HP纯值，供复制展示。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) float VisibleMaxHealth = 1.f; // 本场已乘倍率HP上限。
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bBroken = false; // 正常击碎闩锁，不代表被外部Destroy。
    bool bObjectiveActive = false; // 仅服务器门禁；准备/结束阶段不能吃伤害，不写存档。
    TWeakObjectPtr<UDemoCombatSession> Session; // Flow持有，设施不延长旧战斗寿命。
    FDelegateHandle HealthBinding; // ASC属性事件准确解绑，防重复初始化。
};
