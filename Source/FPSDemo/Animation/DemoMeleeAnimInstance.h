// 职责：近战动画图的主线程值缓存与普通/连续切换策略。使用：ABP_Melee_FP及MeleePose层继承，工作线程只读Sequence/Time，不访问World或ASC。
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DemoMeleeAnimInstance.generated.h"
class UAnimSequence;
class UAimOffsetBlendSpace1D;
UCLASS(Transient,Blueprintable)
class FPSDEMO_API UDemoMeleeAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** DeltaSeconds为引擎秒，发布FP姿态及Idle状态；新图由引擎惯性节点处理Idle边，已审连招边不冻结混合，权威相位仍由组件维护。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee") TObjectPtr<UAnimSequence> Pose; // DA资源由组件强引用保活，此缓存同步发布给图。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee") float PoseTime=0.f; // 源序列秒数，不驱动命中。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee") float Progress=0.f; // 招式0..1，供设计师额外表现层读取。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee") FName Action; // 稳定招式键，配置调试可见。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee") TObjectPtr<UAnimSequence> PreviousPose; // 取消/换招时冻结的上一表现资源，不复制。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee") float PreviousTime=0.f; // 上一已显示姿态的源秒数，混出期间不继续攻击。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee") float BlendAlpha=1.f; // 图将旧/新完整姿态混合，手和握柄始终同图驱动。
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Melee|Recovery") bool bUseRecoveryInertialGraph=false; // 仅新标准Inertial图类默认开启；旧BP默认保原路径，不因原生类改动自动改图。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee|Recovery") bool bIdlePresentation=false; // 本帧合法MeleeHold资源；无Pawn/死亡/无Pose时false，不授权攻击或代表服务器阶段。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee|Recovery") float RecoveryBlendSeconds=.18f; // DA配置的收招惯性秒数，本机纯表现；不延长GA/冷却/伤害或连招期限。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee|Look") TObjectPtr<UAimOffsetBlendSpace1D> LookOffset; // Definition保活的可选同骨架加成，工作线程只读，不加载资源。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee|Look") float LookPitch=0.f; // 当前控制俯仰度，夹在资产X轴；不是再旋转相机的角度。
    UPROPERTY(BlueprintReadOnly,Transient,Category="Melee|Look") float LookWeight=0.f; // [0,1]仅Idle渐入，攻击/收起/死亡淡出，旧无look图保持0。
private:
    uint32 PreviousSerial=0; // 本机表现序号，同一序列经输入缓冲连续重播也触发过渡；预测确认不改变此号。
    float BlendElapsed=0.f; // 本实例本地过渡秒数；不作为伤害或冷却时钟。
    bool bWasLookIdle=false; // 只在look进入/退出Idle或资源变化时打印业务事件，避免默认逐帧刷日志。
};
