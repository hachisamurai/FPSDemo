// 职责：玩家受击表现可调数据，组件初始化加载一次；不包含伤害倍率/存档/输入权限。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoPlayerDamageFeedbackProfile.generated.h"
class USoundWave;
UCLASS(BlueprintType)
class FPSDEMO_API UDemoPlayerDamageFeedbackProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 初始化数字默认；Wave由Editor导入器写入真实数据资产，不在战斗中搜索目录。 */
    UDemoPlayerDamageFeedbackProfile();
    /** 检查有限值/阈值/容量；声音缺失可降级，不阻止玩家战斗。 */
    bool Validate() const;
    UPROPERTY(EditAnywhere,Category="Timing") float IndicatorSeconds=.7f; // 来向弧有效秒。
    UPROPERTY(EditAnywhere,Category="Timing") float EdgeSeconds=.22f; // 外缘脉冲秒。
    UPROPERTY(EditAnywhere,Category="Timing") float TrailHoldSeconds=.12f; // 血条残影等待秒。
    UPROPERTY(EditAnywhere,Category="Timing") float TrailFadeSeconds=.35f; // 残影收敛秒。
    UPROPERTY(EditAnywhere,Category="Strength") float LightFraction=.1f; // MaxHealth损失比例中击阈值。
    UPROPERTY(EditAnywhere,Category="Strength") float HeavyFraction=.25f; // 重击阈值。
    UPROPERTY(EditAnywhere,Category="Strength") float LightEdge=.1f; // 外缘透明度。
    UPROPERTY(EditAnywhere,Category="Strength") float MediumEdge=.16f; // 中击外缘透明度，上限.25。
    UPROPERTY(EditAnywhere,Category="Strength") float HeavyEdge=.22f; // 重击外缘透明度，上限.25。
    UPROPERTY(EditAnywhere,Category="Camera") float LightRoll=.12f; // 视觉侧倾度，不改ControlRotation。
    UPROPERTY(EditAnywhere,Category="Camera") float HeavyRoll=.3f; // 重击侧倾度，上限.4，不改逻辑瞄准。
    UPROPERTY(EditAnywhere,Category="Camera") float RollSeconds=.18f; // Roll包络淡出秒。
    UPROPERTY(EditAnywhere,Category="LowHealth") float LowEnter=.3f; // 比例，滞回进入/退出。
    UPROPERTY(EditAnywhere,Category="LowHealth") float LowExit=.35f; // 退出低血比例，大于LowEnter。
    UPROPERTY(EditAnywhere,Category="LowHealth") float CriticalEnter=.15f; // 进入濒危比例。
    UPROPERTY(EditAnywhere,Category="LowHealth") float CriticalExit=.2f; // 退出濒危比例，大于CriticalEnter。
    UPROPERTY(EditAnywhere,Category="Audio") float ContactVolume=.7f; // 战斗SFX线性增益。
    UPROPERTY(EditAnywhere,Category="Audio") float VoiceVolume=.45f; // 非语言痛哼线性增益。
    UPROPERTY(EditAnywhere,Category="Audio") float PainChance=.65f; // 中/重击尝试痛哼的本机概率0..1，仍受1秒CD限制。
    UPROPERTY(EditAnywhere,Category="Audio") float LowHealthVolume=.2f; // 低血呼吸线性增益。
    UPROPERTY(EditAnywhere,Category="Audio") TArray<TObjectPtr<USoundWave>> Light; // 3个轻受伤源，数据资产保活。
    UPROPERTY(EditAnywhere,Category="Audio") TArray<TObjectPtr<USoundWave>> Heavy; // 3个重受伤源。
    UPROPERTY(EditAnywhere,Category="Audio") TArray<TObjectPtr<USoundWave>> Pain; // 3个非语言声。
    UPROPERTY(EditAnywhere,Category="Audio") TObjectPtr<USoundWave> LowHealth; // 唯一无缝呼吸循环。
};
