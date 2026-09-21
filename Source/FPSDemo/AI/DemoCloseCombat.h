#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoCloseCombat.generated.h"

/** 两类纯近身敌人的配置；攻击力仍从GAS读取，不引入第二份健康/伤害真值。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoCloseCombatSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float MeleeWindup=.45f; // 秒[0.2,2]，挥击前摇可走位躲避。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float MeleeReach=190.f; // 三维距离cm[150,260]，起手/命中均检查。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float MeleeHalfAngle=65.f; // 命中扇形半角度[20,90]，朝向起手时锁定。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float Recovery=.65f; // 命中/落空/撞墙硬直秒[0.2,2]。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float ApproachMultiplier=1.5f; // 导航追击速度倍率[1,2.5]，再乘GAS移速。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float ChargeWindup=.8f; // 冲刺预警秒[0.3,2]，不随难度缩短。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float ChargeMinRange=350.f; // 起手最小水平距离cm[250,600]，近处用有前摇近战。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float ChargeMaxRange=1100.f; // 最大起手距离cm，必须大于Min且≤1800。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float ChargeSpeed=1300.f; // cm/s[600,2500]，实际突进再乘GAS减速。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float ChargeDistance=1400.f; // 每次最大直线行程cm[400,2000]，必须≥最大起手距离。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float ChargeCooldown=6.f; // Recovery结束后等待秒[1,20]，难度只缩短冷却。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CloseCombat") float ChargeDamageMultiplier=1.5f; // 乘实时GAS攻击力[0.5,3]，不重复计算关卡倍率。
    /** 拒绝NaN、越界与矛盾距离；生产表失败阻止开局，直接调用可安全禁用。 */
    bool IsValid() const;
};

/** 无独立Tick的纯近身战斗状态，Enemy统一驱动，暂停/冻结/死亡可集中撤销。 */
UENUM()
enum class EDemoClosePhase : uint8 { Idle, MeleeWindup, ChargeWindup, Charging, Recovery };

UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoCloseCombat : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 禁独立Tick，创建配置默认值，实际配置来自出生快照。 */
    UDemoCloseCombat();
    /** Settings为值快照；bEnabled/bCharger由解析角色决定；MeleeInterval为已难度缩放秒数、OpeningDelay为首发错峰秒。 */
    void Configure(const FDemoCloseCombatSettings& Settings,bool bEnabled,bool bCharger,float MeleeInterval,float OpeningDelay);
    /** Player为本帧目标；Speed已经乘GAS导航速度，DeltaSeconds为World秒；返回true表示接管纯近身行为。 */
    bool Update(class ADemoCharacter* Player,float Speed,float DeltaSeconds);
    /** 死亡/切阶段/冻结调用；Reason为诊断键，丢弃旧目标方向并关闭预警，不保留后台任务。 */
    void Cancel(FName Reason);
    /** 只读状态，攻击入口据此拒绝给纯近身怪发射飞行物。 */
    bool IsEnabled() const;
    EDemoClosePhase GetPhase() const;
    /** 动作显示锁定方向，避免冲刺路线不追踪但模型还朝玩家转动。 */
    bool HasLockedFacing() const;
    FVector GetLockedDirection() const;
private:
    /** Next为阶段，记录World时刻并同步预警；每次攻击只由一次阶段转换触发结算。 */
    void SetPhase(EDemoClosePhase Next);
    /** Player为当前目标，bCharge选择伤害倍率；通过GE造成一次伤害，不在碰撞多回调里重复应用。 */
    void Damage(class ADemoCharacter* Player,bool bCharge);
    /** 创建无碰撞的自身蓄力壳与直线地面预警；所有权归Enemy组件，取消时隐藏。 */
    void CreateTelegraph();
    FDemoCloseCombatSettings Config; // 单次生成冻结，不保存DataTable裸指针。
    bool bActive=false; // 仅启用Melee/Charger角色；旧直接生成保持旧行为。
    bool bChargeRole=false; // Charger可突进并近距离挥击，Melee只有挥击。
    EDemoClosePhase Phase=EDemoClosePhase::Idle; // 当前本地权威阶段，不保存/不宣称联机预测。
    float PhaseStart=0.f; // World秒，暂停冻结。
    float NextMelee=0.f; // 下一次近战可开始的World秒，出生至少2秒缓冲。
    float NextCharge=0.f; // 下一次冲刺可开始的World秒，恢复结束后完整冷却。
    float MeleeCooldown=1.2f; // 冻结难度修正后的普通攻击间隔秒。
    float DistanceLeft=0.f; // 本次尚可冲刺cm，碰撞/导航边缘立即终止。
    FVector Direction=FVector::ForwardVector; // 起手时锁定方向，前摇/突进期间不跟踪玩家。
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Shell; // 自身短时红色壳，复用已有可Cook材质。
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Lane; // 无碰撞地面预警带，显示固定冲刺方向。
    UPROPERTY() TObjectPtr<class UStaticMesh> SphereAsset; // CDO硬引用供Cook，NewObject组件不会漏资源。
    UPROPERTY() TObjectPtr<class UStaticMesh> CubeAsset; // 地面带基础网格，归资产系统持有。
    UPROPERTY() TObjectPtr<class UMaterialInterface> GlowAsset; // 复用Editor已创建红光材质。
};
