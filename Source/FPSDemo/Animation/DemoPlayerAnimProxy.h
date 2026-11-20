// 职责：把本帧游戏线程瞄准输入交给最终动画节点；由PlayerAnimInstance创建/销毁，任务完成前引擎禁止重写。依赖UE5.4原生FAnimInstanceProxy，不查询World或修改资产。
#pragma once
#include "Animation/AnimInstanceProxy.h"
#include "Characters/DemoSharedViewFrame.h" // 和真正相机共用眼位/胶囊夹限，任务中不读取场景对象。

class UDemoPlayerAnimInstance;
class UDemoAuthoredAimProfile;

/** 当前游戏线程意图与同帧基础姿态；前者在派发任务前写，后者由WeaponAnchor写、最终Aim顺序读，绝不访问World。 */
struct FDemoIdleMotionFrame
{
    // 当前武器身份/意图；切换时丢弃上一枪中立参考，不跨Avatar共享。
    FName Weapon=NAME_None;
    float Weight=0.f; // 0正常瞄准、1完整原生待机差量，共同控制腕/枪。
    bool bCaptureReference=false; // 仅静稳且原生IdleBreak完全退出时更新中立参考。
    bool bPoseValid=false; // 每帧游戏线程清零，本次WeaponAnchor完成后才有效。
    bool bReferenceValid=false; // 原生中立Pose已采集；缺失时最终节点回退正常持枪。
    FName ReferenceWeapon=NAME_None; // 参考所属武器，防止换枪沿用错误骨姿。
    TArray<FTransform> Pose; // 本帧基础组件空间Pose，cm；按当前LOD紧凑骨序，容量随LOD改变。
    TArray<FTransform> Reference; // 同骨序中立基础Pose，不是绝对枪模型朝向，不保存UObject。
};

#if WITH_EDITOR
/** 隔离待机诊断的同帧纯值；单个动画任务写、PostUpdate测试在引擎求值屏障后读，不改变任何输出Pose。 */
struct FDemoIdleBreakPoseAudit
{
    // 仅显式DemoIdleBreakAudit启用，构造时在游戏线程冻结；正式游戏不包含此结构。
    bool bEnabled=false;
    // 三组组件空间变换均按胸、右腕、左腕、枪锚顺序；cm/四元数，不持有UObject。
    FTransform NativeBase[4];
    FTransform UpperAction[4];
    FTransform BeforeAim[4];
    FTransform AfterAim[4];
    // R04只读姿态诊断：真实root/pelvis/spine01..05在Anchor前的组件框，默认audit关闭不采集。
    // 名称与数组等长，任务写入后测试在游戏线程求值屏障后复制，不驱动任何骨骼。
    TArray<FName> PostureNames;
    TArray<FTransform> NativeBasePosture;
    TArray<FTransform> UpperPosture;
    // R04注视回归另分kind保存真实头颈输入，保持既有7骨Base/Upper格式不变；仅audit开启采集。
    TArray<FName> ViewPostureNames;
    TArray<FTransform> NativeBaseViewPosture;
    TArray<FTransform> UpperViewPosture;
    // 锚点节点预测映射后的真实枪骨组件空间变换，用于区分基础姿态与静态Hold覆盖。
    FTransform MappedGun=FTransform::Identity;
    // 当前进程实际求值次数；NoAim对照允许AimCount为0，不能把未执行阶段当作有数据。
    uint32 AnchorCount=0;
    uint32 AimCount=0;
    float IdleApplied=0.f; // 最终节点实际应用的随机待机权重，区别于只发布未执行的缓存。
    float ReachErrors[2]={0.f,0.f}; // 左/右腕到实际IK目标的位置误差cm，诊断不允许靠拉伸通过。
    // 仅-DemoFireArmJitterAudit开启的全骨阶段快照。GT每帧清空，单一动画任务按图依赖写，
    // 测试Actor在求值屏障后复制；组件空间cm/单位四元数，绝不反向驱动Pose。
    bool bFirePoseEnabled=false;
    TArray<FName> FirePoseNames; // 当前Compact骨序对应的真实名称，可能包含虚拟骨；不得当网格161索引。
    TArray<FTransform> FireBase; // 原Lyra基础输出，Anchor映射前。
    TArray<FTransform> FireUpper; // 当前操作混合输出，Anchor映射前。
    TArray<FTransform> FireBeforeAim; // 作者瞄准求解器本帧实际输入；未执行则为空。
    TArray<FTransform> FireAfterAim; // 作者求解器本帧Result，最终蒙皮仍须另读PostProcess后的组件。
    TArray<FTransform> FireAimLibrary; // Editor显式诊断：本帧实际双线性库姿态CS。
    TArray<FTransform> FireArmSeed; // Editor显式诊断：胸部/动作差量后、二骨IK前的实际CS初值。
    uint32 FireAnchorCount=0; // 本实例累计真实Anchor采集次数；不将多次求值误计为多次射击。
    uint32 FireAimCount=0; // 本实例累计真实作者Aim采集次数，NoAim可为0。
};
#endif

/** 仅纯值的本帧桥接；UE5.4节点PreUpdate早于NativeUpdateAnimation，因此不能在那里读取上一帧焦点。 */
struct FDemoPlayerAnimProxy : public FAnimInstanceProxy
{
    /** Instance为拥有本代理的动画实例；在游戏线程构造，代理销毁前实例与节点任务均有效。 */
    explicit FDemoPlayerAnimProxy(UAnimInstance* Instance);
    /** Source为本帧已更新的实例；NativeUpdateAnimation末尾在游戏线程调用一次，工作线程开始后只读。 */
    void CaptureMuzzleFocus(const UDemoPlayerAnimInstance& Source);
    // Mesh空间焦点cm；不保存场景Actor，跨任务只读。
    FVector FocusPoint=FVector::ZeroVector;
    // 真实机械Socket在枪锚局部的平移cm，不是上一帧世界枪口。
    FVector MuzzleOffset=FVector::ZeroVector;
    // 查询时的Mesh空间基础眼位/视线；稳定策略直接用此值，旧Head策略才同帧重新投影，不含CameraModifier。
    FVector ViewOrigin=FVector::ZeroVector;
    FVector ViewDirection=FVector::ForwardVector;
    // 本帧表面平面快照；无命中时法线为0，禁止对远处空视线伪造碰撞。
    FVector SurfacePoint=FVector::ZeroVector;
    FVector SurfaceNormal=FVector::ZeroVector;
    // 冻结的Mesh到世界变换与原眼位纯值规则，任务中只做空间变换。
    FTransform MeshToWorld=FTransform::Identity;
    FDemoSharedViewFrame ViewFrame;
    // 显式AnimatedHead策略才用当前head重算；显示全身与眼位来源分开，稳定策略即使有Body也保持此值false。
    bool bEvaluatedEye=false;
    // 焦点距离合法范围cm，近墙/无命中仍受原预算限制。
    FVector2D DistanceLimits=FVector2D(200,10000);
    // 同一帧合法修正预算度数，最终节点仍有防御夹限。
    float MaxDegrees=15.f;
    // 资源与当前武器匹配才开启，初始化/无Pawn时立即关闭。
    bool bFocusActive=false;
    // 仅作本帧资产身份比较，不拥有；实例/Appearance强引用保活，任何异步任务期间不卸载。
    const UDemoAuthoredAimProfile* ProfileIdentity=nullptr;
    // 所属实例的同帧随机待机通道；WeaponAnchor与最终Aim按图依赖顺序访问，游戏线程只在任务屏障外发布意图。
    FDemoIdleMotionFrame IdleMotion;
    // 新逐枪增量后坐策略，本帧NativeUpdate后发布；只有启用且未被Reload/近战接管才为true。
    // 最终Aim用它保留相对已验证Hold的肩肘动态，旧枪和旧资产默认false，绝不读取上帧骨姿。
    bool bPreserveGunRecoilArmDelta=false;
#if WITH_EDITOR
    // 诊断值不参与动作选择/校准；引擎任务完成前游戏线程不读取。
    FDemoIdleBreakPoseAudit IdleAudit;
#endif
};
