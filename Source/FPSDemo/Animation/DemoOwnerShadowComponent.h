// 职责：本机独立第一人称的正常世界尺度阴影代理。使用：DemoCharacter默认子对象；仅读取已求值的FP/TP/下身骨姿。
// 依赖Presentation单一隐藏投影门；不复制、不参与碰撞/ASC/伤害，不改真实动画、相机或其他客户端。场景捕获/分屏/HWRT有显式边界。
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PoseableMeshComponent.h" // 仅身体子类覆盖保守bounds；头仍复用该身体Leader bounds。
#include "DemoOwnerShadowComponent.generated.h"

class ADemoCharacter;
class UDemoPlayerPresentationComponent;
class UPoseableMeshComponent;
class UPrimitiveComponent;
class UMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkinnedMeshComponent;
class UStaticMeshComponent;

/** 只读最终CS骨点扩展原生参考bounds；避免正常尺度前伸手臂被Game静态参考包围盒裁掉，不做CPU蒙皮。 */
UCLASS()
class FPSDEMO_API UDemoOwnerShadowMeshComponent : public UPoseableMeshComponent
{
    GENERATED_BODY()
public:
    /** LocalToWorld为引擎本组件世界框；合并Super bounds与实际骨点AABB+20cm保守蒙皮余量。
     * 仅读已求值缓存，初始化无有效点时保Super；此值仅供剔除，不是精确网格碰撞/蒙皮接触证据。 */
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};

/** 正常骨长的本机投影：保下身/自然TP躯干，将FP最终上臂世界朝向和兼容子骨旋转接到真实肩部。 */
UCLASS(ClassGroup=(Demo), meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoOwnerShadowComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 创建非复制PostUpdate协调器；渲染子组件仅在本机资源/视角/参考骨架就绪后按需创建。 */
    UDemoOwnerShadowComponent();
    /** 绑定本Pawn现有Presentation/Melee/WeaponAnimation求值顺序；不加载或保存任何资产。 */
    virtual void BeginPlay() override;
    /** DeltaTime为帧秒，Tick参数来自引擎；读取完成后的骨缓存并更新本机投影，不重新求值源动画。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    /** Pawn/World结束先恢复原隐藏投影门，再销毁本组件独有渲染件；不销毁任何源组件。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** 只读本帧是否已成功构造完整正常尺度身体与当前武器代理；不作为任何玩法权限。 */
    bool IsProxyActive() const;
    /** 借用本机正常尺度身体，供窄范围渲染验收；未就绪/非本机时为空，调用者不得写姿态。 */
    UPoseableMeshComponent* GetBodyProxy() const;
    /** 借用当前有效的静态或骨武器代理；仅渲染验收，不是物理枪口或近战轨迹。 */
    UPrimitiveComponent* GetWeaponProxy() const;
    // 本Pawn本机表现开关；未配置旧资源使用默认true，但不兼容/分屏/非本人视角自动回退，不复制或存档。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|OwnerShadow") bool bEnabled=true;
private:
    /** Body/Arms/Lower为已解析源；仅指针/LOD边沿更新Tick依赖，避免同组调度依赖注册偶然顺序。 */
    void RefreshSourcePrerequisites(USkeletalMeshComponent* Body, USkeletalMeshComponent* Arms, USkeletalMeshComponent* Lower);
    /** Mesh或FP LOD变化时验证骨名、父骨及参考局部坐标；失败缓存到下次资源边沿，不逐帧报警。 */
    bool RefreshBoneMaps(USkeletalMeshComponent* Body, USkeletalMeshComponent* Arms, USkeletalMeshComponent* Lower);
    /** 仅读源组件最终CS缓存构造局部变换；父先子后，不强制完成或重跑动画任务。 */
    bool ReadLocalPose(USkeletalMeshComponent* Source, TArray<FTransform>& OutLocal) const;
    /** 批量合并正常下身/TP躯干与兼容FP手臂姿态；bMelee区分近战双臂FK与枪械隐藏左臂过滤/双手对枪定长IK，局部平移/缩放来自正常物理基底。 */
    bool BuildBodyPose(USkeletalMeshComponent* Body, USkeletalMeshComponent* Arms, USkeletalMeshComponent* Lower, bool bMelee);
    /** 只为通过验证的源创建/换Mesh，并复用源材质引用；注册前关闭投影，提交完整帧后再启用。 */
    bool PrepareBodyMeshes(ADemoCharacter* Pawn, USkeletalMeshComponent* Body, USkeletalMeshComponent* Head);
    /** SourceHand为真实FP右手世界框；将FP当前刀/枪机械Pose映射到正常代理右手，抵消相机空间比例。 */
    bool UpdateWeaponMeshes(ADemoCharacter* Pawn, USkeletalMeshComponent* Arms, const FTransform& SourceHand, bool bMelee);
    /** 统一配置仅投影渲染件；隐藏但投动态影，禁碰撞/主pass/捕获/HWRT/距离场和间接照明，不复制。 */
    void ConfigureShadowPrimitive(UPrimitiveComponent* Primitive);
    /** Source/Target均为本Pawn持有的渲染件；仅不同槽位引用变化时赋值，不创建MID或修改共享材质。 */
    void MirrorMaterials(UMeshComponent* Source, UMeshComponent* Target);
    /** 边沿切换所有自有caster并调用Presentation唯一门；Reason为稳定拒绝原因，重复Tick不刷日志。 */
    void SetProxyState(bool bNewActive, FName Reason);

    // 下列代理由本组件强持有、Pawn注册；不进入网络/保存，EndPlay显式销毁，材质借用源对象引用。
    UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> BodyProxy;
    UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> HeadProxy;
    UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> SkeletalWeaponProxy;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> StaticWeaponProxy;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> OpticProxy;
    TWeakObjectPtr<UDemoPlayerPresentationComponent> Presentation; // 同Pawn已有组件，弱引用不延长Avatar寿命。
    TWeakObjectPtr<USkeletalMeshComponent> PrerequisiteBody, PrerequisiteArms, PrerequisiteLower; // 仅识别依赖边沿。
    TWeakObjectPtr<USkeletalMesh> CachedBodyMesh, CachedArmsMesh, CachedLowerMesh; // 骨映射对应资产；GC/换Mesh使缓存失效。
    TArray<int32> ArmBoneMap, LowerBoneMap; // Body骨下标→源Ref骨下标；INDEX_NONE表示该骨保TP基底。
    TArray<uint8> ArmKinds; // 0非手臂，1左上臂根，2左臂子骨，3右上臂根，4右臂子骨。
    int32 SpineBoneIndex=INDEX_NONE, LeftUpperArmIndex=INDEX_NONE, LeftLowerArmIndex=INDEX_NONE, LeftHandIndex=INDEX_NONE, RightHandIndex=INDEX_NONE; // Body参考骨架缓存下标，随RefreshBoneMaps更新。
    TArray<FTransform> BodyLocalPose, ArmsLocalPose, LowerLocalPose, MergedLocalPose, MergedComponentPose; // 复用本帧纯值缓冲，不保活源动画实例。
    int32 CachedArmsLOD=INDEX_NONE; // FP预测LOD变化触发参考映射复核；不把LOD裁骨误认为Skeleton变化。
    bool bMapsValid=false; // 只缓存静态参考兼容性；运行Pose晚到仍每帧重试，不永久禁用。
    bool bActive=false; // 已提交的本机渲染状态；不是装备/生命或复制真值。
    bool bUsesSkeletalWeapon=false; // 当前成功提交的武器代理种类，inactive时不表示网格可见。
    bool bUsesOptic=false; // 当前枪具有匹配镜体资源；近战或无镜体时为false。
    FName LastReason=NAME_None; // 仅边沿业务日志，不逐帧重复输出正常重试原因。
};
