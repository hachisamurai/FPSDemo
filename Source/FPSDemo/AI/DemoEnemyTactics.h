#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoEnemyTactics.generated.h"

/** 自动混编按出生序号分配，固定角色可用于关卡单独制作；Boss始终使用专用行为。 */
UENUM(BlueprintType)
enum class EDemoEnemyRole : uint8 { Mixed, Chaser, Ranged, Flanker };

/** 标准压迫感参数；实例复制配置，不在战斗中修改共享DataTable。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoEnemyTacticsSettings
{
    GENERATED_BODY()
    // 关闭后恢复旧追击/攻击节奏，便于配置比较；不是存档字段。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") bool bEnabled = true;
    // Mixed以追击/远程/侧翼循环混编，避免每次读档随机改变阵容。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") EDemoEnemyRole Role = EDemoEnemyRole::Mixed;
    // 远程保持的理想水平距离cm；近于MinimumRange撤离，远于MaximumRange接近。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float PreferredRange = 850.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float MinimumRange = 550.f; // cm，回退进入阈值，必须小于理想距离。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float MaximumRange = 1100.f; // cm，接近进入阈值，必须大于理想距离。
    // 重新选择侧移/后退目标的World秒间隔[1,6]，避免每帧改变路径导致抖动。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float RepositionInterval = 2.5f;
    // 侧翼接近的目标环半径cm[300,1500]，相对当前位置偏转65度。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float FlankRange = 650.f;
    // 追击兵/侧翼移动倍率[1,2]，再乘GAS移速；远程保持模板原速度。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float PressureSpeedMultiplier = 1.25f;
    // 弹道预判上限秒[0,0.6]；只快照发射时速度，最多提前180cm，不追踪冲刺。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float PredictionSeconds = .3f;
    // Boss生命比例降到此值进入一次性第二阶段；[0.1,0.9]，治疗不退出。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float RageHealthFraction = .5f;
    // 第二阶段攻击间隔倍率[0.5,1]；保留0.9/2秒预警和各技能最低间隔。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float RageIntervalMultiplier = .75f;
    // 第二阶段移动倍率[1,1.5]，和GAS减速相乘，不移除冰冻/减速。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tactics") float RageSpeedMultiplier = 1.15f;
    /** 验证有限值、距离顺序和枚举；蓝图编辑Clamp不能替代JSON运行时校验。 */
    bool IsValid() const;
};

/** 无独立Tick的决策层；Enemy在Combat守卫之后调用，施法期间不会偷偷移动。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoEnemyTactics : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 创建只保存本次生成状态的组件，不注册异步任务。 */
    UDemoEnemyTactics();
    /** Settings为值快照；Slot为本关出生序号；bEnabled允许直接生成的测试/旧调用者显式保留旧行为。 */
    void Configure(const FDemoEnemyTacticsSettings& Settings, int32 Slot, bool bEnabled);
    /** Player是本帧目标，Speed为已经应用GAS的cm/s，DeltaSeconds为World秒；只在允许移动时调用。 */
    void Move(class ADemoCharacter* Player, float Speed, float DeltaSeconds);
    /** Health/Maximum是最新GAS健康值；Boss跨阈值只进入一次第二阶段，返回是否本次触发。 */
    bool UpdateRage(float Health, float Maximum);
    /** Player为当前目标、ProjectileSpeed为cm/s；输出有限提前量的瞄准点，不缓存玩家指针。 */
    FVector AimPoint(const class ADemoCharacter* Player, float ProjectileSpeed) const;
    /** Base为原间隔秒、Minimum为下限；只有Boss第二阶段缩短，不刷新已开始的冷却。 */
    float AttackInterval(float Base, float Minimum) const;
    /** 返回出生序号决定的额外延迟秒；保证最早首发仍不早于2秒。 */
    float OpeningDelay() const;
    /** 提供调试/测试只读角色与二阶段状态，不允许外部修改。 */
    EDemoEnemyRole GetRole() const;
    bool IsEnraged() const;
    /** Cancel/非Combat清除临时战术目标，保留本敌人的角色与已触发二阶段。 */
    void ResetMovement();
private:
    /** NewState为稳定诊断键；只在切换时输出Log，高频入口仍记录TICK。 */
    void SetState(FName NewState);
    FDemoEnemyTacticsSettings Config; // 本敌人冻结配置，无表引用、无跨World生命周期。
    EDemoEnemyRole Role = EDemoEnemyRole::Chaser; // Mixed解析后的确定角色，Boss不使用此角色移动。
    bool bActive = false; // 关闭时保持旧行为，配置无效也回退关闭并记录。
    bool bRage = false; // 单次Boss生命周期内锁存，死亡销毁，不写检查点。
    int32 FormationSlot = 0; // 非负出生序号，用于左右分工及错开发射，不是全局计数器。
    FVector Goal = FVector::ZeroVector; // 本次侧移目标世界cm，路径由Navigation拥有。
    float NextDecision = 0.f; // World秒，暂停冻结；换阶段重置。
    FName State = NAME_None; // 当前战术诊断，和Navigation具体寻路原因分开。
};
