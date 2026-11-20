// 职责：FP主图/Linked Layers的只读动画配置与游戏线程快照；玩法与服务器时钟由武器/GAS组件拥有。
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
    // 新弹匣就位相位(Detach,1]。服务器整匣在此转移弹药，表现可继续回握；管式另读ShellInsertPhase。
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
    // 可选枪械闭锁待机，原包机械参考姿势不一定闭锁；为空保留旧枪参考Pose。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pose") TObjectPtr<UAnimSequence> WeaponIdlePose;
    // 可选空仓后锁待机；为空沿用WeaponIdlePose，Ammo真值由权威武器决定。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pose") TObjectPtr<UAnimSequence> WeaponEmptyPose;
    // 与FireMontage同相位的短枪机/套筒运动；使用实际Montage位置采样，无独立逐帧计时。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") TObjectPtr<UAnimSequence> FireWeaponSequence;
    // 最后一发可选机械动作，开始时Ammo==0冻结选择；结束进入WeaponEmptyPose。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") TObjectPtr<UAnimSequence> LastShotWeaponSequence;
    // 开始时弹匣Ammo>0使用，AmmoPerShot不足但非零仍选择此动作。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") FDemoReloadAnimationPair TacticalReload;
    // 开始时Ammo==0使用；当前没有独立膛内弹或容量+1规则。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reload") FDemoReloadAnimationPair EmptyReload;
    // 逐发拉栓的手臂/枪机/声音配对；复用动作容器但不使用弹匣显隐字段，不执行补弹。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") FDemoReloadAnimationPair BoltCycle;
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

/** FP-HANDAIM-20261120：本人FP枪臂“手部瞄准+双臂IK”的游戏线程纯值输入（参考UE5.8 FirstPerson Shooter模板Ctrl_HandAdjusment思路，
 *  不复制其资产/代码）。由表现组件每帧写入主实例，代理PreUpdate复制给工作线程；不复制、不参与玩法/弹道。 */
struct FDemoFPHandAimInput
{
    bool bActive=false; // 本帧是否允许手部瞄准；false时代理按BlendSpeed淡出到作者原姿。
    FVector TargetCS=FVector::ZeroVector; // 准星首个阻挡点（已按最小距离外推），Mesh1P组件空间cm（不含渲染缩放）。
    FTransform MuzzleInHand=FTransform::Identity; // 枪口socket相对AnchorBone骨的刚性变换；X轴为已标定枪管方向。
    FTransform GripInHand=FTransform::Identity; // 握点（枪挂接点）相对AnchorBone骨的变换；旋转支点。挂接点本身是骨时为Identity。
    FName AnchorBone=NAME_None; // 枪实际刚性挂接的骨：新四枪为ik_hand_gun（独立于手臂链），旧枪为GripPoint socket所属hand_r。代理旋转此骨即转枪。
    FName HandBone=NAME_None; // 右手骨（通常hand_r）；其父/祖父作为右臂两骨IK链，跟随枪保持原相对关系。等于AnchorBone时直接IK到新锚点。
    FName SupportHandBone=NAME_None; // 对侧手骨（通常hand_l）；无此骨时只做右手。
    float MaxDegrees=1.5f; // 单帧修正角上限（度），避免近距离/极端深度时手腕大幅折转。
    float SmoothingHalfLife=.015f; // 修正量的指数平滑半衰期（秒）；用户要求低滞后，0为不平滑。
    float BlendSpeed=10.f; // 启用/停用权重每秒变化量（1/秒），换弹或隐藏时约0.1秒淡出，避免跳变。
};

/** 固定Mesh1P主实例；只在UE游戏线程NativeUpdateAnimation采样。 */
UCLASS(Transient, Blueprintable)
class FPSDEMO_API UDemoFPAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** 游戏线程由表现组件每帧调用，写入下一次动画求值使用的手部瞄准输入；只存纯值，不访问World。 */
    void SetFirstPersonHandAim(const FDemoFPHandAimInput& Input);
    /** 代理PreUpdate在游戏线程读取当前输入；返回引用仅在调用期间有效。 */
    const FDemoFPHandAimInput& GetFirstPersonHandAim() const { return HandAimInput; }
    /** 初始化/重建时清空旧快照；配置与链接由Pawn拥有的动画组件统一恢复。 */
    virtual void NativeInitializeAnimation() override;
    /** DeltaSeconds为本次动画更新时间秒数；UE随后将成员复制给图求值。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    // 图唯一状态来源，在主线程更新边界写入，工作线程只读值。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") FDemoFPAnimationSnapshot Snapshot;
    // 主图未链接时的安全持枪姿势，由动画组件发布当前配置，不写CDO。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") TObjectPtr<UAnimSequence> IdlePose;
    // Blender已烘焙的当前FP移动Pose；资源来自外观移动配置，与真实枪握点一致。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation|Movement") TObjectPtr<UAnimSequence> MovementPose;
    // 当前移动序列取样秒数；地面与TP共用GaitPhase，空中使用真实跳跃边沿。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation|Movement") float MovementTime=0.f;
    // 动作切换前冻结的FP移动Pose，用于短过渡，不在换枪时混入旧枪握姿。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation|Movement") TObjectPtr<UAnimSequence> PreviousMovementPose;
    // 上述冻结序列的取样秒数，只控制本地表现。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation|Movement") float PreviousMovementTime=0.f;
    // 当前/上一移动Pose之间的0..1过渡，默认完成状态。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation|Movement") float MovementBlendAlpha=1.f;
    // Idle与移动Pose的混合；瞄准时降低步态摆动，Slot中的开火/换弹拥有最终优先级。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation|Movement") float MovementWeight=0.f;
    // 主图TwoBoneIK的ik_hand_gun骨空间目标，游戏线程复制配置值，不建立手枪循环依赖。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") FVector SupportHandGrip = FVector::ZeroVector;
    // 换弹Gameplay/隐藏左臂时为0；结束或取消后按实际换弹Montage淡出权重恢复配置值，主图Slot后使用。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Demo Animation") float SupportHandIKAlpha = 0.f;
protected:
    /** DeltaSeconds为当前动画帧；游戏线程读取同角色移动缓存，工作线程仅读取上述纯值/资源引用。 */
    void UpdateMovement(float DeltaSeconds);
    // 识别跨枪械切换，不能让旧枪移动握姿污染新机械网格。
    FName MovementWeaponId=NAME_None;
    // 当前Pose过渡累计游戏秒，暂停不推进。
    float MovementBlendSeconds=.1f;
    /** DeltaSeconds为引擎本帧动画秒数；在引擎推进Montage之前注入统一时钟终点，保留正常权重和Notify处理。 */
    virtual void PreUpdateAnimation(float DeltaSeconds) override;
    /** 创建FP手部瞄准代理（FDemoFPHandAimProxy）；引擎拥有生命周期，所有子AnimBP继承。 */
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    /** 释放Create返回的同类代理；动画任务结束后由引擎调用。 */
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
private:
    FDemoFPHandAimInput HandAimInput; // 最新一次游戏线程输入；初始化时清为未激活，不复制。
    bool bLoggedHandAimActive=false; // 仅用于状态切换日志的上一帧激活值。
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
