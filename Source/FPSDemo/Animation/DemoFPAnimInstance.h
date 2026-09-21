#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DemoFPAnimInstance.generated.h"

class UAnimSequence;
class UAnimMontage;
class USoundBase;
class USkeletalMesh;
class UStaticMesh;

/** 游戏时钟归一化换弹事件；只播放声音，不允许驱动弹药结算。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoReloadSoundEvent
{
    GENERATED_BODY()
    // 相对本次ReloadSeconds的位置[0,1]，按升序配置；跨帧越过一次只触发一次。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload", meta=(ClampMin="0", ClampMax="1")) float NormalizedTime = 0.f;
    // CDO强引用的短机械声；每次动作生成的AudioComponent由表现组件停止/销毁。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") TObjectPtr<USoundBase> Sound;
    // 非负线性音量；使用游戏SoundClass设备音量，不绕过用户静音设置。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload", meta=(ClampMin="0", ClampMax="2")) float VolumeMultiplier = 1.f;
    // 声音采样音高倍率[0.5,2]；动作提速只改变事件时刻，不强制扭曲机械音色。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload", meta=(ClampMin="0.5", ClampMax="2")) float PitchMultiplier = 1.f;
};

/** 同一时间轴导出的双网格动作；手臂由主实例Montage播放，枪械按相同phase取样。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoReloadAnimationPair
{
    GENERATED_BODY()
    // 共享第一人称骨架、FPAction Slot的单段Montage，唯一完整手臂动作来源。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") TObjectPtr<UAnimMontage> ArmsMontage;
    // 对应本枪Skeleton的非循环动作，根保持固定，仅驱动枪械内部零件。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") TObjectPtr<UAnimSequence> WeaponSequence;
    // 有序动作声音，归一化时刻匹配Blender阶段；为空允许无声预览但资产专项需验证完整性。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") TArray<FDemoReloadSoundEvent> SoundEvents;
    // 可选手持替身启用时，枪内弹匣隐藏的时刻[0,1]；当前四枪未配替身，真实弹匣骨始终可见。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") float MagazineDetachPhase = .2f;
    // 新弹匣回枪时刻[Detach,1]，中断时不依赖此事件而强制复原。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") float MagazineAttachPhase = .6f;
};

/** 配置子AnimBP的只读CDO数据；不得写入本次技能计时、武器实例或弹药。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoWeaponAnimationSet
{
    GENERATED_BODY()
    // 共享手臂Skeleton的持枪姿势，公共层图从此字段读取，不复制四套图。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pose") TObjectPtr<UAnimSequence> IdlePose;
    // 短开火Montage；可空时仍保留声音/弹道，换弹必需动作由下方两项提供。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pose") TObjectPtr<UAnimMontage> FireMontage;
    // 开始时弹匣Ammo>0使用，AmmoPerShot不足但非零仍选择此动作。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") FDemoReloadAnimationPair TacticalReload;
    // 开始时Ammo==0使用；当前没有独立膛内弹或容量+1规则。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") FDemoReloadAnimationPair EmptyReload;
    // 可选弹匣视觉StaticMesh，需以手部抓握点为原点；当前四枪留空，使用真实magazine骨，无碰撞/拾取能力。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Magazine") TObjectPtr<UStaticMesh> MagazinePresentationMesh;
    // 第一人称手臂上的左手挂点；手臂片段负责让手先到弹匣再离开。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Magazine") FName MagazineHandSocket = TEXT("hand_l");
    // 弹匣相对手部挂点的局部厘米/度变换，资源导入器根据共享手臂握点标定。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Magazine") FTransform MagazineHandOffset = FTransform::Identity;
    // 新武器接入后C++向下4cm举枪偏移的恢复秒数[0,1]；图层惯性混合另由公共图配置，不产生玩法冷却。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Blend", meta=(ClampMin="0", ClampMax="1")) float EquipBlendSeconds = .16f;
    // 移动时整个手臂组件的轻摆振幅，厘米[0,2]，双手与武器锚点一起移动。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pose", meta=(ClampMin="0", ClampMax="2")) float MoveSwayCentimeters = .35f;
    // 支撑左手腕在稳定ik_hand_gun骨空间的位置，厘米；必须由资源标定，禁止反馈读取手部最终姿势。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pose") FVector SupportHandGrip = FVector::ZeroVector;
    // 待机支撑手IK权重[0,1]；换弹已烘焙双手轨道，运行时强制0避免覆盖装填动作。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pose", meta=(ClampMin="0", ClampMax="1")) float SupportHandIKAlpha = 1.f;
    /** ArmsMesh/WeaponMesh为装备预检借用资产，Error返回明确失败原因；不改变共享CDO。 */
    bool Validate(const USkeletalMesh* ArmsMesh, const USkeletalMesh* WeaponMesh, FString& Error) const;
};

/** 主线程动画更新边界发布的纯值快照；图和Linked Layer不在工作线程访问World/ASC/Actor。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoFPAnimationSnapshot
{
    GENERATED_BODY()
    // 水平速度厘米/秒，来自Character当前速度；不包含根运动写回。
    UPROPERTY(BlueprintReadOnly, Category="State") float Speed = 0.f;
    // 装填归一化进度[0,1]，来自统一游戏时钟；非装填时为0。
    UPROPERTY(BlueprintReadOnly, Category="State") float ReloadProgress = 0.f;
    // 装填状态只反映实际锁定的武器实例；不拥有另一个玩法状态机。
    UPROPERTY(BlueprintReadOnly, Category="State") bool bReloading = false;
    // 狙击瞄准状态供姿势图读取，ASC/装备组件仍掌握权限。
    UPROPERTY(BlueprintReadOnly, Category="State") bool bAiming = false;
    // 本次开始时冻结的空仓版本，不在中途跟随Ammo变化。
    UPROPERTY(BlueprintReadOnly, Category="State") bool bEmptyReload = false;
    // 装备过渡[0,1]，只影响视觉，不阻塞武器输入。
    UPROPERTY(BlueprintReadOnly, Category="State") float EquipBlendAlpha = 1.f;
};

/** 固定Mesh1P主实例；只在UE游戏线程NativeUpdateAnimation采样。 */
UCLASS(Transient, Blueprintable)
class FPSDEMO_API UDemoFPAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** 初始化/重建时清空旧快照；配置与链接由Pawn拥有的动画组件统一恢复。 */
    virtual void NativeInitializeAnimation() override;
    /** DeltaSeconds为本次动画更新时间秒数；UE随后将成员复制给图求值。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    // 图唯一状态来源，在主线程更新边界写入，工作线程只读值。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") FDemoFPAnimationSnapshot Snapshot;
    // 主图未链接时的安全持枪姿势，由动画组件发布当前配置，不写CDO。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") TObjectPtr<UAnimSequence> IdlePose;
    // 主图TwoBoneIK的ik_hand_gun骨空间目标，游戏线程复制配置值，不建立手枪循环依赖。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") FVector SupportHandGrip = FVector::ZeroVector;
    // 换弹期/隐藏左臂期为0，否则取配置权重；由主图Slot之后的IK节点使用。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") float SupportHandIKAlpha = 0.f;
protected:
    /** DeltaSeconds为引擎本帧动画秒数；在引擎推进Montage之前注入统一时钟终点，保留正常权重和Notify处理。 */
    virtual void PreUpdateAnimation(float DeltaSeconds) override;
};

/** 公共武器层父类，四个子AnimBP仅配置AnimSet；主图Slot仍属于主实例。 */
UCLASS(Transient, Blueprintable)
class FPSDEMO_API UDemoWeaponLayerAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** 初始化拷贝只读资源到图输入，不在层实例绑定第二套ASC委托。 */
    virtual void NativeInitializeAnimation() override;
    /** DeltaSeconds为UE本帧动画秒数；复制已采样主实例状态，禁止重新查询Gameplay对象。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    // 四个配置子类的唯一资源表，引用由CDO和实例保活，运行中不改共享数据。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Demo Animation") FDemoWeaponAnimationSet AnimSet;
    // 公共WeaponBasePose层图直接读取此资源，SequencePlayer不查询Actor。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") TObjectPtr<UAnimSequence> IdlePose;
    // 从主实例复制的纯值状态，不是独立的动画玩法状态机。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") FDemoFPAnimationSnapshot Snapshot;
};
