// 职责：管理Pawn本人可见分区及FP/TP武器和原世界表示的阴影授权；正常尺度影子代理只借用姿态，玩法与复制仍归原组件。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoPlayerPresentationComponent.generated.h"

class UDemoPlayerAppearance;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ADemoWeaponBase;
class UAnimSequence;
class UDemoPlayerEyesComponent; // 双眼独立局部表现，不占用原AnimBP或Skeleton骨。
class UDemoOwnerLowerBodyAnimInstance;
class UDemoLocomotionSet;

/** 角色拥有的全身/独立展示枪；不拥有库存、弹药或ASC，只读取已复制的运动和动作。 */
UCLASS(ClassGroup=(Demo), meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoPlayerPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 加载独立Appearance依赖；PostUpdate Tick在身体骨骼求值后放置第三人称展示枪。 */
    UDemoPlayerPresentationComponent();
    /** Possess/复制初始化可能早于BeginPlay；共用Editor候选解析后应用真实FP网格和标定，幂等且不创建TP组件。 */
    bool PrepareFirstPersonMesh();
    /** 身体/FP手臂就绪后建立动画与材质；头部分区和眼球可选，保持Mesh1P的GAS所有权，必需资源缺失记录错误。 */
    virtual void BeginPlay() override;
    /** DeltaTime为游戏秒；TickType/TickFunction为引擎当前Tick，仅读取装备和动作缓存。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    /** EndPlayReason为卸载原因；清理Pawn拥有的两个展示组件，不销毁真实武器Actor。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** 当前Appearance只读借用引用；供AnimInstance/验收读取，可能在资产缺失时为空。 */
    UDemoPlayerAppearance* GetAppearance() const;
    /** 返回经资源预检的本人身体分区是否就绪；不表示本人枪械也使用TP骨姿，不决定权威枪口来源。 */
    bool UsesFullBodyFirstPerson() const;
    /** 返回共享外观配置中的独立FP枪械策略；调用者另核观察者/装备，不能把它作为服务器物理枪口选择条件。 */
    bool UsesIndependentFirstPersonWeapons() const;
    /** 游戏线程返回本人独立FP深度比例[0.1,1]；有限越界钳制，非本机/旧策略/非有限配置为1，不能供服务器物理枪口或伤害使用。 */
    float GetFirstPersonRenderScale() const;
    /** 当前外观配置的本人全view近裁面厘米[0.5,10]；仅CalcCamera确认压缩及当前本人视点后采用，不修改全局。 */
    float GetFirstPersonNearClipCm() const;
    /** PrepareFirstPersonMesh后返回未压缩的相机局部作者基线；装备重建不得从已缩组件位置再采基线。 */
    FTransform GetAuthoredFirstPersonArmsTransform() const;
    /** 历史本人TP双臂/枪显示是否启用；独立FP模式为false，下身分区仍可存在。 */
    bool UsesSharedBodyWeaponDisplay() const;
    /** 本机可见分区，可跟随Body或用独立基础下身图；供显示/验收借用，绝不作为权威动作、物理枪口或伤害来源。 */
    USkeletalMeshComponent* GetFirstPersonBodyMesh() const;
    /** 原TP可选头部分区借用引用，影子协调器读取资源；不改变嘴型或添加动画实例。 */
    USkeletalMeshComponent* GetOwnerShadowHeadMesh() const;
    /** 仅本机正常尺度代理提交成功才true；所有原表示共用，不从本地控制权推测激活。 */
    bool IsOwnerShadowProxyActive() const;
    /** 影子协调器完整就绪后提交或撤销；立即刷新原世界表示，防首帧/切枪重复投影，不复制/存档。 */
    void SetOwnerShadowProxyActive(bool bActive);
    /** PaletteIndex来自角色服务器复制的0..3颜色；仅更新每Pawn独立MID，不写共享材质。 */
    void ApplyOutfitColor(uint8 PaletteIndex);
    /** 返回世界骨枪组件借用引用；静态枪可为空。权威枪口调用方须另核当前装备/资源，不能退回相机空间FP枪。 */
    USkeletalMeshComponent* GetThirdPersonWeaponMesh() const;
    /** 当前世界静态枪显示组件；武器接口须核当前装备/资源匹配，不能退到相机空间枪作为物理源。 */
    UStaticMeshComponent* GetThirdPersonStaticWeaponMesh() const;
    /** 原世界镜体借用引用，仅用于正常尺度投影映射，不授予开镜/装备/枪口权限。 */
    UStaticMeshComponent* GetThirdPersonOpticMesh() const;
    /** 返回本帧实际第三人称枪世界锚点，仅用于表现/几何验收，不作为弹道源。 */
    FTransform GetWeaponAnchorTransform() const;
    /** 返回同Pawn拥有的双眼表现组件；原版Manny不配置独立眼球时为空，验收仅读不注入姿态。 */
    UDemoPlayerEyesComponent* GetEyes() const;
    // CDO只记录软路径形成Cook依赖；制作器首次启动尚无资产时不触发构造错误，BeginPlay再同步解析。
    UPROPERTY(EditDefaultsOnly, Category="Player") TSoftObjectPtr<UDemoPlayerAppearance> AppearanceAsset;
    /** 只读：本人FP枪管是否被旋转到对准准星（旧MUZZLE-AIM）。武器表现层据此决定是否把首段亮条贴枪管轴；不复制、无副作用。 */
    bool AimsFirstPersonBarrelAtCrosshair() const;
private:
    /** PostUpdate按最终本人视点令枪口轴指向准星实际深度；Weapon借用当前装备，隐藏/装填时恢复原构图。
     *  2026-11-19 OWNER-CONVERGE：默认关闭（见bAimFirstPersonBarrelAtCrosshair），按CS2方式保持作者右下持枪构图。 */
    void AimFirstPersonMuzzle(ADemoWeaponBase* Weapon);
    // 是否把本人FP枪臂绕握点旋转使枪管延长线穿过准星目标（旧MUZZLE-AIM）。默认false：参考CS2，枪模型保持作者姿态与视线平行，
    // 只有纯表现弹道从可见枪口收敛到准星命中点；旋转会把枪口拉到准星旁并被左手遮挡（2026-11-19用户实机反馈“从屏幕中心发射”）。仅本机表现，不复制。
    UPROPERTY(EditDefaultsOnly, Category="FirstPerson") bool bAimFirstPersonBarrelAtCrosshair = false;
    // CS2 viewmodel_offset式本人FP枪臂整体平移，相机局部cm：X前/Y右/Z上。只在不旋转枪管时使用，把枪口移到准星右下让弹道可见地从枪口飞向准星。
    // 仅本机渲染构图，不改相机、物理弹起点、TP模型或网络；Editor可用Demo.Feel.ViewmodelOffset临时覆盖调试。
    // 实机标定：FP枪臂缩放渲染，Y每1cm约令枪在1600px宽画面右移约140px；(0,0.8,-0.6)使手枪枪口约落在画面63%/63%处（初值待用户试看）。
    UPROPERTY(EditDefaultsOnly, Category="FirstPerson") FVector FirstPersonViewmodelOffset = FVector(0.f, .8f, -.6f);
    UPROPERTY(Transient) TObjectPtr<USceneComponent> AimPivot; // Pawn拥有的相机子件，只整体变换本机枪臂，不复制。
    /** FP-HANDAIM-20261120：每帧把准星目标、枪口/握点相对握枪手的变换写入Mesh1P主实例；条件不满足（隐藏/换弹/非本人FP/旧刚体模式）发布未激活。
     *  Weapon借用当前装备可为空；只影响本机表现骨姿，不改弹道/相机/网络。 */
    void PublishFirstPersonHandAim(ADemoWeaponBase* Weapon);
    // 是否启用模板式“手部瞄准+双臂IK”：握枪手绕握点小角度转向准星目标，左手保持贴枪。用户2026-11-20要求按UE5.8模板重做IK版；
    // 与旧刚体MUZZLE-AIM（bAimFirstPersonBarrelAtCrosshair）互斥，后者为true时本项不生效。Editor可用Demo.Feel.HandAim 0/1临时A/B。
    UPROPERTY(EditDefaultsOnly, Category="FirstPerson|HandAim") bool bAimFirstPersonHandsAtCrosshair = true;
    // 手部修正角上限（度，0..30）：限制近距离/大深度差时手腕折转，保持右下构图。默认1.5°由用户2026-11-20实机调参确定（原10°）。
    UPROPERTY(EditDefaultsOnly, Category="FirstPerson|HandAim", meta=(ClampMin="0",ClampMax="30")) float FirstPersonHandAimMaxDegrees = 1.5f;
    // 修正量平滑半衰期（秒，0..0.1）：用户要求不要模板那样明显滞后，默认15ms只抹平深度跳变；0为不平滑。
    UPROPERTY(EditDefaultsOnly, Category="FirstPerson|HandAim", meta=(ClampMin="0",ClampMax="0.1")) float FirstPersonHandAimHalfLife = .015f;
    // 目标最小距离（cm）：贴墙时目标沿视线外推到此距离，避免手腕急转。
    UPROPERTY(EditDefaultsOnly, Category="FirstPerson|HandAim", meta=(ClampMin="20",ClampMax="1000")) float FirstPersonHandAimMinDistance = 150.f;
    FName LastHandAimReason; // FP-HANDAIM：上次发布结果原因（Active或具体拒绝门），仅游戏线程本机诊断去重，不复制、不持久化。
    /** 视角惯性半衰期（秒）：瞄准方向追随真实视角的指数平滑半衰期，“落后→真实”增量再施加一次使转视角时枪口朝转动方向领先（A09，用户确认方向）。
     *  0=无惯性；默认.03秒明显低于模板弹簧拖拽。Editor可用Demo.Feel.HandAimLag实时覆盖。A10/A11弹簧摇摆经用户评审回退。 */
    UPROPERTY(EditDefaultsOnly, Category="FirstPerson|HandAim", meta=(ClampMin="0",ClampMax="0.2")) float FirstPersonHandAimViewLagHalfLife = .03f;
    FQuat LaggedAimView=FQuat::Identity; // 游戏线程跨帧的平滑世界视角朝向；未激活时重置，下次激活从真实视角起步避免跳变。不复制。
    bool bLaggedAimViewValid=false; // LaggedAimView是否已用真实视角初始化。
    /** Tick或代理边沿更新Body/Head/TP枪/镜隐藏投影的唯一策略；外部视图仍使用CastShadow世界表示。 */
    void RefreshWorldShadowPolicy();
    bool bOwnerShadowProxyActive=false; // 本Pawn瞬态非复制状态；来源缺失或退出本人视角时由代理撤销。
#if WITH_EDITOR
    /** Possess前准备和BeginPlay共同调用：只接受统一白名单覆盖；路径变化撤销旧强引用，无覆盖时保持配置，不加载或保存资产。 */
    void ApplyEditorAppearanceOverride();
#endif
    // Pawn拥有的仅本机身体分区；默认LeaderPose，可选独立基础下身窄图；始终无碰撞/影子，不作为玩法骨姿来源。
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> FirstPersonBody;
    /** 初始化、换Mesh或控制权变化时配置Owner姿态源；只在边沿重建实例，普通Tick不重新求骨/不创建资源。 */
    void RefreshOwnerBodyPosePolicy();
    TWeakObjectPtr<UDemoOwnerLowerBodyAnimInstance> ConfiguredOwnerLowerInstance; // 当前已配置实例；换Mesh/失去本机控制后失效，不保活旧实例。
    TWeakObjectPtr<UDemoLocomotionSet> ConfiguredOwnerLowerSet; // 已提交给窄实例的只读资源闭包，Appearance另强持有。
    bool bOwnerLowerPoseActive=false; // 姿态策略边沿，不以可能被换Mesh销毁的弱实例是否有效决定是否恢复旧策略。
    bool bSavedOwnerPostProcessDisabled=false; // 独立窄图前的组件PP开关，退出精确恢复，不写共享Mesh/AnimBP。
    bool bFullBodyViewReady=false; // BeginPlay校验配置和参考Skeleton后锁存；资源失败回退旧显示并明确报错。
    FTransform AuthoredArmsTransform=FTransform::Identity; // 当前Pawn未缩的模板/Appearance基线，Prepare时确定；纯本地值，不复制。
    bool bCachedAuthoredArmsTransform=false; // 保证重复Possess/装备初始化不将已缩或轻摆输出再次当作作者输入。
    /** DeltaTime为本帧秒；读取本人PS的实际语音幅度并驱动头部Morph，不给GAS/身体骨骼添加写入者。 */
    void UpdateSpeechMouth(float DeltaTime);
    // 作者头部形变名，默认MouthOpen；新人物需提供同名Morph或在组件默认值修改，缺失仅禁用嘴型。
    UPROPERTY(EditDefaultsOnly, Category="Player|Voice") FName MouthMorphName = TEXT("MouthOpen");
    float MouthOpenAmount = 0.f; // 本Pawn本地平滑值0..1，不复制；重生从闭嘴开始。
    bool bHasMouthMorph = false; // BeginPlay检查一次，防缺资源时每帧报警/寻找Morph。
    // BeginPlay解析后的强引用保活外观依赖，运行时只读；缺失时为空并明确日志。
    UPROPERTY(Transient) TObjectPtr<UDemoPlayerAppearance> Appearance;
    // Pawn拥有的眼球跟随组件，Presentation保活并随World显式清理，无网络组件复制。
    UPROPERTY(Transient) TObjectPtr<UDemoPlayerEyesComponent> Eyes;
    // 原TP头部使用Body LeaderPose且本人不可见；默认/回退投隐藏影，代理成功时由中央门撤销原隐藏影，卸载销毁。
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Head;
    /** Weapon为当前库存借用实例；换枪时重建展示Mesh，支持骨枪与兼容静态枪。 */
    void RefreshWeapon(ADemoWeaponBase* Weapon);
    /** 初始化或本人几何分区改变时按该网格自己的材质建立/复用MID；不得用TP槽覆盖Owner专属近身裁剪，缓存仅存本Pawn生命周期。 */
    void RefreshOwnerPartitionMaterials();
    // Pawn拥有的第三人称骨枪，OwnerNoSee，无碰撞/Gameplay/网络Actor；卸载显式DestroyComponent。
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> ThirdPersonWeapon;
    // 兼容已有StaticMesh配置，无碰撞，只在第三人称可见，与骨枪互斥显示。
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ThirdPersonStaticWeapon;
    // 可选镜体挂在真实TP机械枪Root，随枪动作移动；只渲染，不参与瞄准权限或复制。
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ThirdPersonOptic;
    // 当前显示的库存弱引用，不阻止切枪后库存销毁。
    TWeakObjectPtr<ADemoWeaponBase> PresentedWeapon;
    // 本Pawn身体、双臂及已使用Owner分区的MID缓存；按原材质Parent复用，装备边沿不重复分配，EndPlay统一释放，不与别的Pawn共享。
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> OutfitMaterials;
    // PostUpdate计算后的真实展示锚点；初始化前Identity，不复制。
    FTransform WeaponAnchor = FTransform::Identity;
};
