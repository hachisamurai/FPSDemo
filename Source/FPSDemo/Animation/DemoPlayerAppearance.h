// 职责：全身/第一人称手臂模型、TP动画与眼位的配置闭包。使用DA_PlayerAppearance；两视角同骨架独立动画，近战FP姿态由武器DA配置，不保存玩家状态。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/DemoPlayerSole.h" // 实际鞋底角点蒙皮契约供最终Pose适配，不猜测足骨离地高度。
#include "DemoPlayerAppearance.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimMontage; // 可选身体后坐增量，单独Slot/组；与隐藏Mesh1P的FPAction不共享实例。
class USkeleton; // 运行时只核对资源身份，不修改骨架或插槽。
class UDemoAuthoredAimProfile; // 每枪离线制作/验收的多方向姿态，由Appearance强引用并进入Cook。
class UDemoPlayerAnimInstance;
class UDemoOwnerLowerBodyAnimInstance; // 本人下身独立窄移动图；空配置继续旧LeaderPose，完整TP/影子不改。
class UDemoPlayerWeaponAnimInstance;
class UDemoLocomotionSet; // 集中移动资源与实测步长，由外观保活并进入Cook依赖。
class UStaticMesh; // 独立眼球使用静态网格，不向共享小白人Skeleton加骨。

/** 独立第三人称上身动画：腕/指沿用已验证枪局部接触，肩、肘在正常全身骨架重解。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoPlayerWeaponPoses
{
    GENERATED_BODY()
    // 所有序列强引用同一个玩家Skeleton；不包含第一人称浮动肩部或玩法Notify。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<UAnimSequence> Hold;
    // 可选已验收瞄准库；有值时使用分布式腰背/颈部姿态及完整枪框架，空值保留旧资产兼容路径。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Aim") TObjectPtr<UDemoAuthoredAimProfile> AuthoredAim;
    // 旧资产的完整上胸兼容策略；AuthoredAim有值时由多方向姿态独占最终瞄准，本标志仅供回退路径。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Aim") bool bRigidChestAim=false;
    // 启用独立作者骨盆；旧资产false继续使用Appearance的共同参考值。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Aim") bool bOverrideReferencePelvis=false;
    // 本枪TP源Idle的组件空间pelvis，厘米；独立IK枪支系映射到移动骨盆时使用。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Aim") FTransform ReferencePelvis=FTransform::Identity;
    // TP待机与实际移动骨盆混合后枪口的静止俯仰（度）；作者从真实两组序列计算，整体瞄准减去此角，不改握点/骨长。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Aim", meta=(ClampMin="-20",ClampMax="20")) float NeutralAimPitch=0.f;
    // 当前Hold与配对机械Idle实测的枪管方向，位于spine_05局部；单位向量。零值为旧静态俯仰校准兼容路径。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Aim") FVector BoreDirectionInChest=FVector::ZeroVector;
    // 可选跑动左臂循环，同玩家Skeleton；只混入upperarm_l子树，保持当前右手持枪。当前仅手枪配置，换弹优先覆盖。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<UAnimSequence> RunLeftArm;
    // 旧绝对姿态开火兼容源；RecoilMontage为空时继续使用，狙击仍用Bolt，不叠第二次开火。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<UAnimSequence> Fire;
    // 可选身体唯一后坐Montage，要求当前Hold匹配的MeshSpace增量、DemoGunRecoil独立组/槽；为空完整回退旧Fire，资产由外观保活。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Recoil") TObjectPtr<UAnimMontage> RecoilMontage;
    // 作者源秒到表现游戏秒的比例[0.25,4]；不改变GAS射速，新射击通过同组跨淡重入而非把整段挤进冷却。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Recoil",meta=(ClampMin="0.25",ClampMax="4")) float RecoilPlayRate=1.f;
    // 首次开火混入秒数[0,0.2]；仅此Montage实例，不改变基础持枪或摄像机。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Recoil",meta=(ClampMin="0",ClampMax="0.2")) float RecoilBlendInSeconds=.02f;
    // 前一发仍有权重时，新枪序号从其实际姿态跨淡的秒数[0.005,0.2]，不重复触发同一序号。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Recoil",meta=(ClampMin="0.005",ClampMax="0.2")) float RecoilRetriggerBlendSeconds=.035f;
    // 正常尾部回中性的混出秒数[0,0.3]；换枪/死亡/阻塞取消立即撤销，不能把旧枪动作带入新姿态。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Recoil",meta=(ClampMin="0",ClampMax="0.3")) float RecoilBlendOutSeconds=.06f;
    /** 游戏线程预检可选后坐闭包；ExpectedSkeleton为当前身体骨架，Error输出拒绝原因；空Montage是合法旧策略，不检查作者美术观感。 */
    bool ValidateRecoilMontage(const USkeleton* ExpectedSkeleton,FString& Error) const;
    /** 返回配置后坐的游戏秒数；空/非法播放率返回0，只用于同一已提交枪时钟的表现取样。 */
    float GetRecoilPresentationSeconds() const;
    // 普通装填序列，与现有配对机械动画使用相同归一化phase。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<UAnimSequence> Reload;
    // 空仓首周期版本；霰弹逐颗换弹每个服务器Sequence独立取样。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<UAnimSequence> EmptyReload;
    // 可选逐发拉栓版本；只表现已提交的服务器射击时钟。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<UAnimSequence> Bolt;
    // 本枪Skeleton专用薄图，与全身共享取消/动作混合缓存；四枪不可共用一个不兼容的编译Skeleton。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TSubclassOf<UDemoPlayerWeaponAnimInstance> MechanicalAnimClass;
    // 可选独立TP机械组；与上述身体动作同相位烘焙。全部为空时保留旧资产的FP机械兼容路径。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Mechanical") TObjectPtr<UAnimSequence> MechanicalHold;
    // TP开火机械运动；为空时保持MechanicalHold，不借用另一视角的动作。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Mechanical") TObjectPtr<UAnimSequence> MechanicalFire;
    // TP普通换弹机械轨道，和Reload共享0..1相位及取消混合。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Mechanical") TObjectPtr<UAnimSequence> MechanicalReload;
    // TP空仓换弹机械轨道，与EmptyReload配套。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Mechanical") TObjectPtr<UAnimSequence> MechanicalEmptyReload;
    // TP逐发拉栓机械轨道，与Bolt配套。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Mechanical") TObjectPtr<UAnimSequence> MechanicalBolt;
};

/** 玩家模型、动画、肤色服装的Cook依赖闭包；运行时只读，不修改共享CDO或Skeleton。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoPlayerAppearance : public UDataAsset
{
    GENERATED_BODY()
public:
    // 全枪/Scope/近战共享稳定基础眼位；默认true令旧资产使用下方FirstPersonViewOffset，false仅兼容旧头骨相机。
    // 共享玩法配置，不能放进本机舒适度设置或依据OwnerOnly模型是否创建改变；不含相机反馈/ControlRotation后坐。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") bool bUseStableFirstPersonView=true;
    // 本人LeaderPose显示分区开关，不决定眼位来源或要求渲染整个TP躯干；Mesh1P保留GAS动画/Notify。false回退旧独立双臂。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") bool bFullBodyFirstPerson=false;
    // 本人枪械使用现有相机空间Mesh1P及FP专用动作，队友仍TP；true时FirstPersonBodyMesh只配下身，不得再含双臂。
    // 共享外观配置，默认false兼容历史资产；不决定服务器物理枪口、伤害或基础眼位，不复制每帧骨姿。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") bool bUseIndependentFirstPersonWeapons=false;
    // 本人独立FP相机子件的等比深度压缩[0.1,1]；同FOV保持投影角，1兼容旧资产。仅渲染，TP/腿/碰撞/物理枪口不缩。
    // UE5.4项目侧子集，非新版原生FP渲染器；非零世界WPO/平移镜头修饰需另审，不复制此本机显示值。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View", meta=(ClampMin="0.1",ClampMax="1")) float FirstPersonRenderScale=1.f;
    // 压缩FP时当前本人整个透视view的near厘米[0.5,10]，含世界/下身；不改全局GNear，非FP专用近裁面。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View", meta=(ClampMin="0.5",ClampMax="10")) float FirstPersonNearClipCm=1.f;
    // 可选本人下身窄移动实例；仅本机独立FP表示脱离TP LeaderPose，复用已有基础移动/跳落资源，不接TP Dash/攻击槽。
    // 默认空兼容历史；无骨/碰撞缩放，不影响队友、影子、GAS或服务器移动；完整动作观感须单独验收。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") TSubclassOf<UDemoOwnerLowerBodyAnimInstance> OwnerLowerBodyAnimClass;
    // 本机第一人称关闭相机运动模糊，避免快速转头的长拖尾；不关TSR/世界阴影，不改变角色骨姿，可在外观配置独立恢复。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") bool bDisableFirstPersonMotionBlur=true;
    // 本人渲染分区；独立FP武器模式只显示已适配的闭合下身，双臂来自ArmsMesh；历史共享武器模式可含双臂。
    // 必须与Body同Skeleton/参考，禁止缩骨隐藏；远端仍用完整BodyMesh，不能把历史开放腰口候选作为正式下身。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") TObjectPtr<USkeletalMesh> FirstPersonBodyMesh;
    // 显式旧头部策略相对head的眼位厘米，沿视线前X/右Y/上Z；稳定策略忽略本项，不继承动画头部旋转。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") FVector FullBodyEyeOffset=FVector(4,0,10);
    // 远端/第三人称完整蒙皮网格，厘米、缩放1；拥有者使用ArmsMesh，隐藏本体仍投射完整影子。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<USkeletalMesh> BodyMesh;
    // 可选头部渲染分区，同一Skeleton/LeaderPose；原版Manny头部已在BodyMesh中，本项为空，不独立播放动画。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") TObjectPtr<USkeletalMesh> HeadMesh;
    // 第一人称完整手臂网格；当前恢复原版SK_Mannequin_Arms，换新网格必须验证四枪和近战，不继承卡通腕部后处理。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TObjectPtr<USkeletalMesh> ArmsMesh;
    // 新资源按其Blender相机标定完整FP组件变换；旧资产false保留模板组件，不能以此修补单手/骨骼扭曲。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") bool bOverrideArmsTransform=false;
    // 相机局部厘米/度；整体肩肘腕和枪一起变换，制作器记录对应Blender眼位/FOV。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") FTransform ArmsTransform=FTransform(FRotator::ZeroRotator,FVector(-10,0,-147));
    // 原生缓存状态驱动的薄AnimBP；不把GAS ASC改绑到全身Mesh。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TSubclassOf<UDemoPlayerAnimInstance> AnimClass;
    // 0..6保留Idle/Walk/Run/JumpStart/JumpLoop/JumpLand/Sprint，追加7/8左/右横移；作者/运行时共用固定顺序。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TArray<TObjectPtr<UAnimSequence>> MovementClips;
    // 可选八方向移动配置；为空保留旧资产路径，正式新配置不再倒放前进或混用旧横移动作。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Movement") TObjectPtr<UDemoLocomotionSet> Locomotion;
    // 作者直线支撑相位实际退脚速度cm/s；导入器从manifest填入，不凭游戏MaxWalkSpeed猜测步频。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player", meta=(ClampMin="1")) float AuthoredWalkSpeed = 120.f;
    // 跑步短支撑+腾空的作者速度cm/s，当前约636，运行时按650/该值调整，避免六倍快抖。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player", meta=(ClampMin="1")) float AuthoredRunSpeed = 636.36f;
    // 独立左右横移支撑脚退步速度cm/s，作者清单实测，默认为200。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player", meta=(ClampMin="1")) float AuthoredStrafeSpeed = 200.f;
    // 走/跑显示切换速度cm/s；230以下允许模拟量慢走，游戏真实速度仍由CharacterMovement掌握。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player", meta=(ClampMin="1")) float RunThreshold = 230.f;
    // 武器稳定身份pistol/rifle/shotgun/sniper映射；不使用槽位或本地化名称。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TMap<FName, FDemoPlayerWeaponPoses> WeaponPoses;
    // Body相对胶囊中心厘米/角度；-90度令参考+Y朝角色+X。旧卡通默认Z=-99，Manny资产配置为-96。应用后必须CacheInitialMeshOffset。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") FTransform BodyOffset = FTransform(FRotator(0,-90,0),FVector(0,0,-99));
    // 稳定眼位相对胶囊中心厘米，X沿视线水平前方、Y右、Z世界上；默认眼前8/眼高75，按安全胶囊夹限。
    // bUseStableFirstPersonView开启时全枪/Scope/近战共用，不以每次装备时的动画head重新标定；蹲伏跟实际胶囊变化。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View") FVector FirstPersonViewOffset = FVector(8,0,75);
    // 低头15..60度平滑向前探视的最大厘米[0,20]，默认10让视点越过衣领前沿；只移动视点，不移动碰撞体/骨或武器。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|View", meta=(ClampMin="0",ClampMax="20")) float LowLookForwardOffset=10.f;
    // 离线TP基础姿势的pelvis组件空间变换；运行时将枪锚点带入移动骨盆，消除步态bob手枪脱离。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") FTransform ReferencePelvis = FTransform::Identity;
    // 卡通Body真实Shoe槽的最低点（4..32）；Manny为空以停用旧鞋底修正，绝不借用旧模型样本。
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Player") TArray<FDemoPlayerSolePoint> LeftSole;
    // 对应右脚，换模型/鞋型后必须重新提取与烘焙AnimBP，不能只换BodyMesh。
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Player") TArray<FDemoPlayerSolePoint> RightSole;
    // 每个Pawn独立MID的配色参数名；卡通资产使用OutfitPrimaryColor，原版Manny使用已有Tint。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") FName OutfitParameter = TEXT("OutfitPrimaryColor");
    // 服务器复制的0..3外观索引映射到本机MID；Manny使用Tint参数与灰白/青/红/黄，旧资产默认保留工装配色。不复制颜色数组。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player") TArray<FLinearColor> OutfitPalette = {
        FLinearColor(.105f,.285f,.305f), FLinearColor(.32f,.125f,.095f),
        FLinearColor(.42f,.265f,.085f), FLinearColor(.235f,.145f,.285f)};
    // 双眼共用闭合眼球（局部+X前/+Z上），Cook强引用；缺失时只禁用眼球表现，既有玩法继续。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Eyes") TObjectPtr<UStaticMesh> EyeMesh;
    // 左眼静止位置/朝向，真实head骨局部厘米；由作者绑定矩阵计算，所有端共用资产，不逐帧复制。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Eyes") FTransform LeftEyeRest = FTransform::Identity;
    // 右眼可有不对称高度；不通过镜像骨缩放生成，保持网格绕序和瞳孔方向一致。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Eyes") FTransform RightEyeRest = FTransform::Identity;
    // 相对每眼静止框架的左右角度上限，度；作者须对完整眼眶检验极限，不能暴露球后部。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Eyes", meta=(ClampMin="0",ClampMax="45")) float EyeMaxYaw = 18.f;
    // 上下转动对称上限，度；半眯眼皮保持在Body上，仅眼球旋转。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Eyes", meta=(ClampMin="0",ClampMax="30")) float EyeMaxPitch = 10.f;
    // 指数跟随速率1/秒，默认12（约0.25秒到95%）；只平滑本地表现，不增加网络RPC或GAS标签。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Player|Eyes", meta=(ClampMin="0.1",ClampMax="60")) float EyeFollowSpeed = 12.f;
};
