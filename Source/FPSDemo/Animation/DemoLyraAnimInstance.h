// 职责：为迁入的 Lyra 原生状态机提供 Demo 数据，并继承当前四枪操作/瞄准缓存；近战按DA策略在专用上身Slot同步Montage，不替换Lyra腿部，不依赖 LyraGame。
#pragma once
#include "CoreMinimal.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "GameplayEffectTypes.h"
#include "DemoLyraAnimInstance.generated.h"
class UAnimMontage;
class ADemoWeaponBase; // 按实际武器实例与原成功枪序号去重，稳定名称不能区分同ID的新库存实例。

/** 主 AnimBP 的原生适配层；移动/选片仍在可编辑 Lyra 图内，游戏线程发布外部状态。 */
UCLASS(Transient, Blueprintable)
class FPSDEMO_API UDemoLyraAnimInstance : public UDemoPlayerAnimInstance
{
    GENERATED_BODY()
public:
    /** Avatar/动画实例初始化时重置层身份；不改变角色与技能。 */
    virtual void NativeInitializeAnimation() override;
    /** DeltaSeconds 为游戏秒；发布地面距离、武器状态，工作线程仅消费缓存。 */
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    /** Presentation在主实例安装后及PostUpdateWork调用；在本帧求值结束后切层，让下帧PreUpdate先初始化属性缓存。 */
    void RefreshWeaponLayer();
    // 迁入主图读取的脚底到可碰撞地面距离 cm；在地面为0，检测不到为100000，不参与物理移动。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Lyra Adaptation|Movement") float GroundDistance=0.f;
    // 按现有稳定武器ID选择 Linked Anim Layer；资产由配置强引用，不能在工作线程加载。
    UPROPERTY(EditDefaultsOnly, Category="Lyra Adaptation|Layers") TMap<FName,TSubclassOf<UAnimInstance>> WeaponLayers;
    // 未装备/近战及缺失武器配置时使用的移动层，不能退回空图导致T Pose。
    UPROPERTY(EditDefaultsOnly, Category="Lyra Adaptation|Layers") TSubclassOf<UAnimInstance> DefaultLayer;
    // 原生腰背保留策略，已验收后默认开启；可在主AnimBP类默认值配置，由WeaponAnchor游戏线程缓存，不复制骨骼。
    UPROPERTY(EditDefaultsOnly, Category="Lyra Adaptation|Layers") bool bPreserveLocomotionSpine=true;
    // 使用原生HipFire UpperBodyMask协调举枪与移动；C++兼容默认关闭，正式主图开启并连接WeaponReadyLocomotion。
    UPROPERTY(EditDefaultsOnly, Category="Lyra Adaptation|Layers") bool bUseWeaponReadyLocomotion=false;
    // 游戏线程从实际枪械标定/存活状态发布，Linked Layer仅读；不是GAS开火或ADS状态，不影响移速/伤害。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Lyra Adaptation|Layers") bool bWeaponReadyLocomotion=false;
    // 原生IdleBreak真实状态权重与本实例平滑后的动作权重；不复制，远端沿已有视线/武器状态独立表现。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Idle") float NativeIdleBreakWeight=0.f;
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Idle") float IdleBreakMotionWeight=0.f;
    // 游戏线程发布给Ready_ItemBase.CanPlayIdleBreak的资格快照；入口timer还须AND该函数，提前退出复用原!CanPlay，不伪造ADS/Reload，不复制/存档。
    // 初始化false；无Pawn的动画预览NativeUpdate不发布资格，因此不播放大幅IdleBreak；正常Avatar首帧按真实状态计算。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Idle") bool bAllowNativeIdleBreak=false;
    // 原Lyra四向FullBody蒙太奇，固定前/右/后/左；斜向按Lyra边界选择邻近动作，不能误称八条独立资源。
    UPROPERTY(EditDefaultsOnly, Category="Lyra Adaptation|Dash") TArray<TObjectPtr<UAnimMontage>> DashMontages;
    // 动画总时长秒，与原Demo FP闪避0.65秒对应；不改变GAS免伤0.45秒和移动推进时长。
    UPROPERTY(EditDefaultsOnly, Category="Lyra Adaptation|Dash",meta=(ClampMin="0.2",ClampMax="2.0")) float DashPresentationSeconds=.65f;
    // 专用上身Slot真实播放的动态Montage，仅本实例保活；可供Editor逐帧确认实际播放而非仅查业务标识。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Melee") TObjectPtr<UAnimMontage> CurrentMeleeMontage;
    // Body专用后坐实例资产，单槽DemoGunRecoil；强引用仅维持播放/淡出生命周期，不是Mesh1P的FPAction。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") TObjectPtr<UAnimMontage> CurrentGunRecoilMontage;
    // 本次实际引擎实例ID，包含淡出期；验证和机械读实际实例，不凭业务标识声称已播放。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") int32 GunRecoilMontageInstanceID=INDEX_NONE;
    // 当前实际Montage权重0..1；连发时上一实例保持冻结Pose并按独立组跨淡，正常结束淡至0。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") float GunRecoilMontageWeight=0.f;
    // 此Avatar真正成功启动的身体后坐次数，只作表现诊断，不授予伤害或复制到网络。
    UPROPERTY(BlueprintReadOnly, Transient, Category="Player|Recoil") int32 GunRecoilMontageStartCount=0;
#if WITH_EDITOR
    // 显式动画夹具的隔离模式：Current/NoAim/Native/Ready；仅本进程诊断，不保存、不复制、不进入非Editor包体。
    FName MotionIsolationMode;
#endif
private:
    /** DeltaSeconds为游戏步长；读取真实Linked Layer权重，发布原生资格与手部差量快照；操作/视线中断后等待退出和重入，不改原计时器。 */
    void UpdateIdleBreakMotion(float DeltaSeconds);
    // 操作后的重置等待秒数；仅本Avatar，不能保存或在重生后沿用。
    float IdleBreakRearmRemaining=0.f;
    // 被中断的原生轮次必须先退出；否则鼠标停止后会从半段动作重新抬手。
    bool bIdleBreakWaitForNeutral=false;
    FName IdleBreakWeapon=NAME_None; // 用于换枪清空意图与参考。
    FQuat PreviousIdleAim=FQuat::Identity; // 上帧真实控制旋转；只检测输入变化，不反馈相机动画。
    bool bHasIdleAim=false; // 首帧只建立参考，不伪造转头事件。
    /** NativeUpdate在DA策略/业务phase更新后调用；相同源/序号只启动一次，已审连招边零混入接续；卸下/死亡/取消停止，不触发伤害或RootMotion。 */
    // NativeUpdate内同步唯一近战Slot；连续零混入新实例须先以零Delta初始化权重再发布求值快照，避免该帧回落基础姿态。
    void UpdateMeleeMontage();
    /** Base已发布Hold/Fire和服务器源时刻后调用；每枪仅启动一次同组增量，真实求值数据与机械同phase，无独立Advance。 */
    void UpdateGunRecoilMontage();
    /** 换枪/死亡/操作/菜单取消本实例专组所有活动及淡出后坐；Reason仅诊断，保留已消费序号防旧消息复活。 */
    void CancelGunRecoilMontage(const TCHAR* Reason);
    // 最后消费的真实武器/枪序号；包括已过期/已取消枪，防同一旧复制凭据重新起播，不保存或复制。
    TWeakObjectPtr<ADemoWeaponBase> GunRecoilConsumedWeapon;
    int32 GunRecoilConsumedSequence=0; // 原成功射击Sequence，不由帧数生成新时钟。
    // 每个实际库存实例的去重记录；切到另一枪再切回也不能重放旧枪，弱键不保活Actor，实例初始化清空。
    TMap<TWeakObjectPtr<ADemoWeaponBase>,int32> GunRecoilConsumedSequences;
    // 当前MG所属武器实例，取消与换枪时清空；已消费身份另存，避免切回重播旧枪。
    TWeakObjectPtr<ADemoWeaponBase> GunRecoilMontageWeapon;
    // 正常尾部只发起一次Stop，不能每帧重置混出时长；中断仍可立即收掉所有旧淡出实例。
    bool bGunRecoilStopping=false;
    // 当前已消费的视觉序号，预测确认不变时不重启；实例重建后重新消费。
    uint32 MeleeMontageSerial=0;
    // 本实例Montage正在使用的源；同序号切资源不能误续播旧片，停止时清空，不修改/复制源资产。
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> MeleeMontageSource;
    // 源的近战定义身份，仅用于同序号换武器时重建Montage，不把名称当作GameplayTag条件。
    FName MeleeMontageWeapon=NAME_None;
    /** 游戏线程PostUpdate读取已复制的服务器凭据，以已过时间启动TP专用蒙太奇；ASC的FP主实例不改绑。 */
    void UpdateDashPresentation();
    /** 将已缓存的业务布尔值写入迁入主图的同名变量；失败只首次报告，避免每帧刷屏。 */
    void PublishFlag(FName PropertyName,bool Value);
    // 当前已链接层，随Avatar实例结束；不保存或复制。
    UPROPERTY(Transient) TSubclassOf<UAnimInstance> ActiveLayer;
    // 已报告的缺失属性，仅用于诊断去重，不影响动画状态。
    TSet<FName> MissingFlags;
    // 此实例已消费的服务器闪避起点，避免同一复制凭据每帧重播；重建实例允许从已过时间恢复。
    float LastDashStartTime=-100.f;
    // 当前TP动画实例拥有的闪避资源引用；死亡时只停止该动画，不打断别的武器动画。
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> CurrentDashMontage;
};
