// 职责：游戏线程读取移动/GAS/武器时钟并提供全身AnimBP缓存；工作线程只读这些值，不在图中驱动玩法或网络。
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DemoPlayerAnimInstance.generated.h"

class UAnimSequence;
class UAnimMontage; // 后坐配置由Appearance强引用，Body缓存只供本帧唯一Montage驱动。
class UBlendSpace; // 外观配置拥有的全身二维移动，工作线程只读取缓存引用。
class ADemoCharacter;
class UDemoPlayerAppearance; // 只在游戏线程同步读取外观，动画骨控制节点只接收值。
class UDemoAuthoredAimProfile; // 已导入的不可变枪械瞄准库。
#if WITH_EDITOR
struct FDemoIdleBreakPoseAudit; // 仅诊断值，接口在游戏线程等待动画任务后返回副本，不暴露原生代理。
#endif

/** 游戏线程读取真实CharacterMovement/GAS/复制武器时钟，AnimGraph只读缓存值。 */
UCLASS(Transient, Blueprintable)
class FPSDEMO_API UDemoPlayerAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    // 类默认值兼容旧图/Editor候选；每帧另与当前近战DA.bUseUpperBodyMontage合并。
    // 只在已安装DemoMeleeUpperBody Slot的主图使用；运行值表示本帧策略，Evaluator只提供待机，不复制伤害时钟。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Player|Melee") bool bUseMeleeUpperBodyMontage=false;
    // 本帧真实TP攻击及源秒数，供Montage同步和审计；不是下方待机Evaluator，也不参与伤害授权。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Melee") TObjectPtr<UAnimSequence> MeleeAttackSequence;
    // 作者动作源秒数，由近战已有预测/复制时钟驱动，Montage不独立累计另一份进度。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Melee") float MeleeAttackTime=0.f;
    // 仅有效optional身体后坐策略且Hold/Fire阶段为true；作者Aim据此保留输入相对Hold的肩肘旋转差，旧枪/换弹/近战/死亡保持false。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") bool bUseAdditiveGunRecoil=false;
    // 当前后坐配置；身体基础Evaluator仍固定Hold，实际增量只由Lyra主实例专用Slot播放。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") TObjectPtr<UAnimMontage> GunRecoilMontage;
    // 同一已提交服务器枪起点得到的本帧源秒数/归一化phase；机械必须读此值，不能读固定为0的UpperBodyTime。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") float GunRecoilSourceTime=0.f;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") float GunRecoilPhase=0.f; // [0,1]，不复制、不参与射击冷却。
    // 该帧策略选出的原武器成功枪序号；仅实例去重，不创建新RPC或伤害身份。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") int32 GunRecoilShotSequence=0;
    /** 返回最后已发布的上身武器身份；Presentation用它避免切换当帧把新枪挂到尚未更新的近战骨姿，仅读游戏线程缓存。 */
    FName GetUpperWeaponId() const;
    /** 网格创建/重建时重置局部姿态缓存，ASC仍由第一人称主实例管理。 */
    virtual void NativeInitializeAnimation() override;
    /** DeltaSeconds为本帧游戏秒；只选择表现，不驱动移动、技能或弹药。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    /** 游戏线程为本实例创建专用纯值代理，沿用原Lyra图；原生任务生命周期由引擎管理。 */
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    /** InProxy仅属于本实例；引擎等待所有动画任务完成后销毁，不跨实例释放。 */
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
#if WITH_EDITOR
    /** 仅Editor隔离诊断在游戏线程调用；等待引擎求值完成并返回阶段快照副本，不允许测试修改代理或姿态。 */
    FDemoIdleBreakPoseAudit ReadIdleBreakAudit() const;
#endif
    // 相机俯仰减作者中性枪口角后的TP整体瞄准叠加角（度）；换弹回源姿态，远端平滑，不新增复制。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") float AimPitch=0.f;
    // 本帧枪械瞄准配置；游戏线程发布，最终节点PreUpdate复制，切近战/死亡清空。
    UPROPERTY(Transient) TObjectPtr<UDemoAuthoredAimProfile> AuthoredAimProfile;
    // 当前角色自己的准星焦点，Mesh组件空间cm；游戏线程射线冻结，工作线程只读，不使用旁观摄影机或复制每骨状态。
    UPROPERTY(Transient) FVector MuzzleFocusPoint=FVector::ZeroVector;
    // 实际展示枪Socket在ik_hand_gun锚点局部的平移cm；由机械网格组件空间取得，不反馈上一帧世界枪口。
    UPROPERTY(Transient) FVector MuzzleFocusOffset=FVector::ZeroVector;
    // 本次查询真实表面点/法线，Mesh空间cm/单位向量；供本帧新head眼位重投影，不保存命中Actor或跨帧锁定目标。
    UPROPERTY(Transient) FVector MuzzleFocusSurfacePoint=FVector::ZeroVector;
    UPROPERTY(Transient) FVector MuzzleFocusSurfaceNormal=FVector::ZeroVector;
    // 当前武器/作者库/Socket全部匹配才开启；切近战、阵亡或资源无效立即清理。
    UPROPERTY(Transient) bool bMuzzleFocusActive=false;
    // 本次游戏线程快照的修正角上限度数，供最终节点有界解算；默认与项目配置一致。
    UPROPERTY(Transient) float MuzzleFocusMaxDegrees=15.f;
    // 视线相对身体的水平夹角（度），只额外作用head子树并保留颈骨基础姿态；受HeadYawLimit限制，远端平滑，不随装填归零。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") float HeadYaw=0.f;
    // 世界竖轴转换到实际Mesh组件空间；动画工作线程不读取Actor，水平转头不使用模型硬编码轴。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") FVector AimUpAxis=FVector::UpVector;
    // 当前角色右方向在身体组件空间的单位向量，修正模型-90度及网络平滑的坐标差。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") FVector AimRightAxis=FVector::RightVector;
    // 本Pawn真实视线转换为实际Mesh组件空间；含网络平滑，不能用Actor前向代替。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") FVector AimTargetDirection=FVector::ForwardVector;
    // 当前武器相对spine_05的中性枪管方向，来自联合源姿态标定，不逐帧读展示枪形成反馈回路。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") FVector AimBoreInChest=FVector::ZeroVector;
    // 有有效中性枪管标定时启用最终三维方向匹配；旧资产/近战保留原有策略。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") bool bCalibratedBoreAim=false;
    // 枪械换弹时暂退回作者动作的权重0..1，和既有动作混合时间一致。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") float AimAlignmentWeight=0.f;
    // 组件空间握枪避让位移cm；肩臂模式保持肩位并解双臂，散弹枪完整上胸模式固定为0以保留源握姿关系。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") FVector AimGripOffset=FVector::ZeroVector;
    // 通常读取配置中的0..0.35躯干比例；散弹枪抬头时使用1启用完整上胸刚体模式，低头仍用原比例，工作线程只读取缓存。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") float AimTorsoFraction=.18f;
    // 当前枪作者骨盆的组件空间变换；只读快照传给最终瞄准节点，不修改源骨架。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") FTransform AimReferencePelvis=FTransform::Identity;
    // 手枪装填抬左手权重；同一动作混合时钟平滑，长枪为1。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") float AimSupportWeight=1.f;
    // 仅有效装备且存活时启动最终瞄准层，死亡/无装备保留基础全身姿势。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Aim") bool bAimWeaponActive=false;
    // 水平速度cm/s；来自实际Character速度，远端使用已有移动复制而非每帧骨RPC。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float Speed = 0.f;
    // 相对角色朝向的平面移动角度(-180,180]度；新移动配置使用专用后退步态，旧配置才保留倒放兼容。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float Direction = 0.f;
    // 是否具备完整新移动配置；图和最终枪锚点映射共用同一开关。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") bool bDirectionalLocomotion=false;
    // 当前武器全身移动资源，近战可用DefaultGround后覆盖自己的上身。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") TObjectPtr<UBlendSpace> GroundBlendSpace;
    // 地面相对速度cm/s，X为向右、Y为向前；短平滑防止输入边界瞬跳。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") FVector GroundVelocity=FVector::ZeroVector;
    // 二维图X轴的平滑右向速度cm/s，与GroundVelocity.X相同，直接绑定原生图引脚。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float GroundRight=0.f;
    // 二维图Y轴的平滑前向速度cm/s，与GroundVelocity.Y相同。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float GroundForward=0.f;
    // 连续0..1左右脚周期；换方向、换枪均不重置，停下只冻结步相。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float GaitPhase=0.f;
    // 新地面Pose与既有跳跃段之间的混合，空中继续沿原起跳/落地契约。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float GroundPoseWeight=0.f;
    // 当前完整TP非地面动作；资源来自本枪配置，不允许增量动画走完整Pose路径。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") TObjectPtr<UAnimSequence> BodyMotionPose;
    // 切换前冻结的片段，仅同武器混合，保留原握点。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") TObjectPtr<UAnimSequence> PreviousBodyMotionPose;
    // 当前动作取样秒数，Evaluator不提取RootMotion或Notify。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float BodyMotionTime=0.f;
    // 旧片段冻结时刻，不在新状态开始时复位旧姿势。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float PreviousBodyMotionTime=0.f;
    // 当前与旧片段之间的0..1短过渡权重。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float BodyMotionBlendAlpha=1.f;
    // 当前配置提供完整非地面姿态时为1，否则使用明确的兼容路径。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float BodyMotionWeight=0.f;
    // TP/FP共用非地面动作进度0..1，从物理边沿/权威闪避时钟计算。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float BodyMotionPhase=0.f;
    // 0地面/3起跳/4空中/5接地/6闪避；表现收势不延长技能权限。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") int32 BodyMotionState=0;
    // 新握枪移动已含完整上身，只有开火/装填/近战/空中叠加原上身动作；保持原UpperBodyWeight诊断契约。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Movement") float ActionOverlayWeight=1.f;
    // 0Idle、1Walk、2Run、3JumpStart、4JumpLoop、5JumpLand、6Sprint、7StrafeLeft、8StrafeRight；只读验收契约。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") int32 LocomotionState = 0;
    // 本次状态取样秒数；SequenceEvaluator关闭Notify/root motion，跳跃首尾不循环。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float LocomotionTime = 0.f;
    // 每状态保留独立时刻，混出旧状态不会因新状态归零而突然跳脚；全部单位为游戏秒。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float IdleTime = 0.f;
    // Walk退出后冻结最近时刻，重新进入按新状态开始取样。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float WalkTime = 0.f;
    // Run的独立时刻，按作者支撑速度调整步频。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float RunTime = 0.f;
    // 起跳片段映射至实际起跳窗口，不在切到空中前截断一半动作。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float JumpStartTime = 0.f;
    // 空中循环片段时间，不依赖实际高度逆推角色位移。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float JumpLoopTime = 0.f;
    // 落地片段映射至短恢复窗口，不影响真实移动响应。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float JumpLandTime = 0.f;
    // 当前短Launch技能腾空疾冲的循环取样时刻。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float SprintTime = 0.f;
    // 左横移独立循环秒数，避免使用前向脚步进行纯侧向滑动。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float StrafeLeftTime = 0.f;
    // 右横移独立循环秒数，左右交替动作分别由作者提供。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float StrafeRightTime = 0.f;
    // 实际移动模式；Dash的短Launch可为true，但Sprint状态优先，不能误播起跳。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") bool bFalling = false;
    // GAS躲避窗口或远端高速移动的只读表现；没有独立玩法速度/冷却。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") bool bSprinting = false;
    // 当前CharacterMovement已确认可走地面且非腾空时为true，仅控制最终足底表现，不改变移动模式。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") bool bGroundedForFeet = false;
    // 当前Floor.ImpactPoint转换至身体组件空间厘米；动画工作线程只读此缓存值，不查询碰撞World。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") FVector FootFloorPoint = FVector::ZeroVector;
    // 与上述同帧地面法线，已转换为身体组件空间单位向量。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") FVector FootFloorNormal = FVector::UpVector;
    // 当前独立TP上身资源；强引用来自角色Appearance，动画线程不查询武器。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") TObjectPtr<UAnimSequence> UpperBodySequence;
    // 与服务器换弹/成功射击时钟对齐的源序列秒数，非权威时间来源。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float UpperBodyTime = 0.f;
    // 没有有效武器或完整动画资产时为0，保留完整移动姿势并明确日志。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float UpperBodyWeight = 0.f;
    // 0..1：静止近战时采用同一攻击序列的骨盆和双腿；移动/腾空/死亡退出，不复制骨或新增动作时钟。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Melee") float MeleeFullBodyWeight=0.f;
    // Hold/Fire/Reload/EmptyReload/Bolt，仅本地诊断；不另建复制状态机。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") FName UpperBodyAction = NAME_None;
    // 本帧归一化动作进度0..1，展示枪机械序列读取相同缓存。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float ActionProgress = 0.f;
    // 动作切换前的TP序列/时刻冻结值，0.12秒混合归位，不能快照成另一Skeleton资源。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") TObjectPtr<UAnimSequence> PreviousUpperBodySequence;
    // 上述冻结序列的源秒数，只用于视觉过渡，不是Gameplay时钟。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float PreviousUpperBodyTime = 0.f;
    // 对应冻结动作，机械网格依据同一个标识选择旧弹匣/枪机姿态。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") FName PreviousUpperBodyAction = NAME_None;
    // 对应冻结归一化phase，机械序列使用它而不是继续推进被取消的动作。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float PreviousActionProgress = 0.f;
    // 当前新动作权重0..1；身体与TP机械薄图逐帧共享，避免手先柔化而弹匣瞬移。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player") float UpperBodyBlendAlpha = 1.f;
    // 可配置的单手跑动左臂循环，DA强引用；图只覆盖左上臂后代，不修改右手/枪锚点/腿。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|RunArm") TObjectPtr<UAnimSequence> RunLeftArmSequence;
    // 与当前跑步/横移腿部归一化步频一致的源秒数；停下时冻结以平滑混出，不另建网络时钟。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|RunArm") float RunLeftArmTime = 0.f;
    // 跑动进入/退出与换弹优先级合成的0..1权重；完整装填阶段严格为0。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|RunArm") float RunLeftArmWeight = 0.f;
protected:
    // 每次实例初始化从类默认值恢复，旧Editor候选可显式强制；不从上帧DA合成值回读，避免换武器后开关粘住。
    bool bMeleeUpperBodyMontageFallback=false;
private:
    // 当前非地面过渡计时，只在本实例使用，不复制骨姿。
    float BodyMotionBlendSeconds=.08f;
    // 防止跨枪混合另一武器的手指和握点。
    FName BodyMotionWeaponId=NAME_None;
    /** 本帧DeltaSeconds与Appearance只读；缓存二维速度/连续步相/动作覆盖，不发RPC、不改变移动或武器数值。 */
    void UpdateLocomotion(float DeltaSeconds,const UDemoPlayerAppearance* Appearance);
    /** DeltaSeconds为游戏秒，Appearance提供步频源；仅手枪地面跑动/快速横移启用，装填按已有上身混合权重让出左臂。 */
    void UpdateRunLeftArm(float DeltaSeconds, const UDemoPlayerAppearance* Appearance);
    // 本实例0..1移动混合，0.12秒进入/退出；不复制，换枪/死亡清零，不参与Gameplay。
    float RunLeftArmMovementWeight = 0.f;
    /** DeltaSeconds为当前帧游戏秒，Appearance为同步借用配置，bPistol选择自然单手规则；只缓存数值供动画线程。 */
    void UpdateAim(float DeltaSeconds,const UDemoPlayerAppearance* Appearance,bool bPistol);
    /** DeltaSeconds为本帧游戏秒；按当前源混合取得0..1装填影响，抢入不同动作时继续原释放截止时间，不修改动作/Shot/机械时钟。 */
    float UpdateReloadAimInfluence(float DeltaSeconds);
    /** 初始化、Avatar/配置失效或换枪时清除本实例恢复所有权；Reason为同步日志原因，不跨帧保存。 */
    void ResetReloadAimRecovery(const TCHAR* Reason);
    /** DeltaSeconds为游戏步长秒；游戏线程沿本Pawn准星求焦点、只平滑距离，发布纯值供作者瞄准节点，无伤害或客户端命中权限。 */
    void UpdateMuzzleFocus(float DeltaSeconds);
    // 本动画实例的距离平滑历史，cm；只属于当前Avatar/当前武器，不写存档、不进行网络复制。
    float MuzzleFocusDistance=0.f;
    // 距离历史的武器稳定ID；切枪时丢弃旧值，不能让长枪沿用手枪的近距离焦点过渡。
    FName MuzzleFocusWeapon=NAME_None;
    // 首次有效瞄准直接取当前角度，避免新复制角色从0度缓慢抬枪；实例重建时清理。
    bool bAimInitialized=false;
    // 仅本AnimInstance的散弹枪换弹后抬枪权重0..1；握姿混合完成后用0.12秒恢复，不复制、不改GAS装弹时钟，其他枪不使用。
    float ShotgunAimRecovery=1.f;
    // 原装填退出混合尚余游戏秒/总秒数；仅GT本实例使用，第三个动作不能抹掉或重启其截止时间，不复制。
    float ReloadAimReleaseRemaining=0.f;
    float ReloadAimReleaseDuration=0.f;
    FName ReloadAimReleaseWeapon=NAME_None; // 释放时钟所属武器ID，换枪/新Avatar不得继承。
    bool bReloadAimReleaseInterrupted=false; // 仅标记本轮抢入日志已发，非Gameplay门禁，下一次源装填释放时清理。
    // -1向下/0水平/1向上，仅用于边沿日志，不驱动姿势或网络。
    int32 AimLogBand=0;
    // 本网格拥有者弱引用，重建/销毁不会保活旧Pawn。
    TWeakObjectPtr<ADemoCharacter> Character;
    // 上帧实际falling，用于区分刚落地和静止；本地表现生命周期。
    bool bWasFalling = false;
    // 起跳/坠落持续秒，用于从起跳到空中循环；传送后由movement状态收敛。
    float AirSeconds = 0.f;
    // 刚落地的缓冲秒，最多0.16；不影响真实移动或跳跃输入。
    float LandSeconds = 0.f;
    // 上身过渡累计游戏秒数，暂停时不推进；只同枪动作之间过渡。
    float UpperBlendSeconds = .12f;
    // 当前视觉混合长度，短后坐用0.03秒，装填/取消仍0.12秒；只改变显示，不参与射速。
    float UpperBlendDuration = .12f;
    // 当前上身记录的成功射击序号，重复复制不重启；连射新序号可从当前后坐平滑切入新一发。
    int32 UpperShotSequence = 0;
    // 同一非法optional资源只报告一次；空/有效配置清空，避免每帧警告，弱引用不保活旧候选。
    TWeakObjectPtr<UAnimMontage> RejectedGunRecoilMontage;
    // 最近姿态所属武器稳定身份，切枪不能把旧枪的机械姿势用于新Skeleton。
    FName UpperWeaponId = NAME_None;
};
