// 职责：集中配置八向步态、步长和第一人称移动资源；DA_PlayerLocomotion供动画实例只读，不驱动CharacterMovement或GAS。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoLocomotionSet.generated.h"

class UBlendSpace;
class UAnimSequence;

/** 同一武器的全身握持移动和独立FP表现；仅保存已在Blender验收的资源，不包含玩法状态。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoLocomotionProfile
{
    GENERATED_BODY()
    // 全身二维移动，X为局部右向速度、Y为前向速度（cm/s），序列均烘焙为一个归一化周期。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") TObjectPtr<UBlendSpace> Ground;
    // 已联合烘焙当前枪握姿的完整TP起跳；禁止配置Additive序列。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ThirdPerson") TObjectPtr<UAnimSequence> BodyJumpStart;
    // TP空中循环，去掉root位移但保留骨盆/腿运动；物理控制实际高度。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ThirdPerson") TObjectPtr<UAnimSequence> BodyJumpLoop;
    // TP接地恢复，不重新播放源Fall_Land的下落前段。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ThirdPerson") TObjectPtr<UAnimSequence> BodyLand;
    // TP四向闪避，固定0前/1右/2后/3左；按权威冻结方向选择，不随速度噪声切换。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ThirdPerson") TArray<TObjectPtr<UAnimSequence>> BodyDashes;
    // FP行走循环；与全身共享归一化步相，不叠加旧正弦组件摆动。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FirstPerson") TObjectPtr<UAnimSequence> Walk;
    // 短GAS躲避时使用的FP降低持枪循环；不增加持续奔跑模式或修改输入。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FirstPerson") TObjectPtr<UAnimSequence> Dash;
    // FP起跳段，按当前全身起跳窗口归一化采样。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FirstPerson") TObjectPtr<UAnimSequence> JumpStart;
    // FP空中循环；落地边沿退出。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FirstPerson") TObjectPtr<UAnimSequence> JumpLoop;
    // FP落地恢复段，按当前落地窗口归一化采样。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FirstPerson") TObjectPtr<UAnimSequence> Land;
};

/** 实测一个左右脚完整周期的行进距离；同方向步长插值，不通过倒放前进模拟后退。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoGaitStride
{
    GENERATED_BODY()
    // 相对角色前向角度[-180,180]，正值向右，按角度升序配置并环绕插值。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait") float Direction = 0.f;
    // 慢走单周期的实际厘米数；来自源轨道测量，非播放时长。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait", meta=(ClampMin="1")) float WalkCentimeters = 200.f;
    // 跑步单周期实际厘米数，后退和横移允许不同，避免所有方向使用前进步长。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gait", meta=(ClampMin="1")) float RunCentimeters = 300.f;
};

/** 正式外观持有的移动配置，运行时只读；增加武器只添加Profile，不改C++武器枚举分支。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoLocomotionSet : public UDataAsset
{
    GENERATED_BODY()
public:
    // 稳定武器ID映射；近战使用默认腿部BlendSpace并继续自己的全上身动作。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") TMap<FName, FDemoLocomotionProfile> Profiles;
    // 没有专用全身配置时的基础腿部资源，仍由现有上身层提供该武器动作。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") TObjectPtr<UBlendSpace> DefaultGround;
    // 升序方向表，由离线作者填入实际步长；运行时只用于计算连续步相。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") TArray<FDemoGaitStride> Strides;
    // BlendSpace慢走环的厘米/秒坐标，需与资源采样位置一致。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="1")) float WalkSpeed = 250.f;
    // BlendSpace跑步环坐标，超过此值仅提高步频，不修改真实移动速度。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="1")) float RunSpeed = 650.f;
    // 速度输入平滑时间秒；只影响视觉，保留角色输入响应。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.01",ClampMax="0.5")) float DirectionBlendSeconds = .10f;
    // TP/FP共享落地恢复秒数，仅表现，不阻塞移动或再次起跳。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.05",ClampMax="0.5")) float LandingSeconds=.30f;
    // 闪避含收势的表现长度；GAS的0.45秒躲避窗口独立，不延长免伤。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.2",ClampMax="1.5")) float DashSeconds=.65f;
    /** 游戏线程按方向和速度插值周期步长(cm)；空表明确回退300，不改变任何Gameplay状态。 */
    float GetStride(float DirectionDegrees, float Speed) const;
};
