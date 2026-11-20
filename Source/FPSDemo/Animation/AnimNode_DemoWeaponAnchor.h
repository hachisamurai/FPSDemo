// 职责：统一移动与武器的空间契约；近战DA可显式选择实际Idle为脊柱动态旋转参考，避免静态弯背重复叠Lyra。依赖同帧两输入Pose及游戏线程纯值快照，旧DA保持原公式。
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "AnimNode_DemoWeaponAnchor.generated.h"

/** 在外层分层混合前建立一致的腰背/双臂/枪框架；不修改源动画、骨长、指骨或Actor。 */
USTRUCT(BlueprintInternalUseOnly)
struct FPSDEMO_API FAnimNode_DemoWeaponAnchor : public FAnimNode_Base
{
    GENERATED_BODY()
    // 同帧基础移动Pose；Lyra脊柱及其横移补偿始终作为基底，外层LayeredBoneBlend保留root和骨盆。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links) FPoseLink Base;
    // 待叠加的上身动作，包括已经混合的换弹取消Pose。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Links) FPoseLink Upper;
    // 仅新移动图启用，旧资产继续由最终瞄准节点执行原映射。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault)) bool bEnabled=false;
    /** 初始化两输入链，所有状态属于当前AnimInstance代理。 */
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    /** 缓存当前LOD骨索引，LOD变化重新解析，不保留骨容器裸引用。 */
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    /** 更新两输入和暴露引脚；不读取World、角色或共享资产可变状态。 */
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    /** 要求引擎在游戏线程缓存实例配置，不能在工作线程Cast/读取可变UObject。 */
    virtual bool HasPreUpdate() const override;
    /** 游戏线程读取实例/当前近战DA策略并复制Idle参考旋转；仅发布本节点拥有的纯值快照，不在工作线程访问UObject。 */
    virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
    /** 默认Lyra模式复制原生脊柱；近战按选定参考叠动态旋转并重建Chest。Idle参考模式保留Base颈部及头部注视朝向，最终View Aim仍由原节点执行。无骨长/缩放修正。 */
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    /** 动画调试树只输出节点与输入，不改变运行时状态。 */
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;
private:
    bool bIdleBonesChanged=true; // CacheBones后首个任务使原生中立参考失效，包含骨数相同但映射改变的情况。
    // PreUpdate确认Demo代理类型；工作线程只处理纯值的待机Pose和Editor诊断，不查询UObject。
    bool bUsesDemoProxy=false;
    // LOD骨引用，初始化时采用固定骨语义；不跨实例共享。
    FBoneReference Pelvis;
    // 枪/双手IK子树的唯一根，改变它的局部变换即可带动整个独立支系。
    FBoneReference HandRoot;
    // Manny双锁骨的父骨；当前正式骨架的spine_05，不用肩腕补偿替代腰背动作。
    FBoneReference Chest;
    // 当前LOD从胸到骨盆之间的完整脊柱索引；CacheBones重建，不跨LOD持有引用。
    TArray<FCompactPoseBoneIndex> SpineChain;
    // 当前LOD从head到Chest的真实父链；Idle中立武器保留移动注视，避免旧全身源抬头补偿随去弯背重复继承。
    TArray<FCompactPoseBoneIndex> ViewChain;
    // PreUpdate发布的实例级策略；工作线程仅读，不访问角色/World。
    bool bPreserveSpine=false;
    // 当前装备请求作者相对参考旋转叠加时为true；含待机/取消混合，不随IsAttacking跳变；与默认保留移动脊柱策略独立。
    bool bUseAuthoredMeleeSpine=false;
    // 显式候选DA请求Idle中立基准；false时逐项保留旧Ref公式，不能自动改变所有Montage武器。
    bool bUseWeaponIdleSpineReference=false;
    // PreUpdate对当前DA完整参考的校验结果；失败时保留Base脊柱，不混合部分Idle/Ref数据。
    bool bSpineReferenceValid=false;
    // 实际Skeleton骨名→父骨局部单位旋转；游戏线程每帧发布当前五骨纯值，工作线程只读。
    TMap<FName,FQuat> SpineReferenceRotations;
    // 上一次参考资产身份/修订及拒绝原因，仅用于状态变化日志，避免无效配置逐帧刷Warning。
    FString SpineReferenceIdentity;
    FString SpineReferenceError;
};
