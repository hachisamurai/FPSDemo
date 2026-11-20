#pragma once
#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/DemoPlayerSole.h"
#include "AnimNode_DemoPlayerGrounding.generated.h"

/** 在完整混合Pose之后修复鞋底低于真实地面的问题；只求解原骨长双腿，不移动胶囊、骨盆或Actor。 */
USTRUCT(BlueprintInternalUseOnly)
struct FPSDEMO_API FAnimNode_DemoPlayerGrounding : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()
    /** 设置固定小白人腿骨名；实际骨索引在RequiredBones变化时缓存。 */
    FAnimNode_DemoPlayerGrounding();
    // 由游戏线程CharacterMovement缓存，本帧真实行走且Floor可走时启用；腾空/疾冲不吸附地面。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground", meta=(PinShownByDefault)) bool bGrounded = false;
    // 真实CurrentFloor碰撞面上一点，已经转换至身体组件空间厘米；工作线程不查World或发射射线。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground", meta=(PinShownByDefault)) FVector FloorPoint = FVector::ZeroVector;
    // 身体组件空间的单位地面法线，与FloorPoint同一游戏线程快照。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground", meta=(PinShownByDefault)) FVector FloorNormal = FVector::UpVector;
    // 左鞋实际角点的序列化蒙皮值；图编译后只读，不从工作线程加载资产。
    UPROPERTY(EditAnywhere, Category="Ground") TArray<FDemoPlayerSolePoint> LeftSole;
    // 右鞋实际角点，必须由相同Body资源提取，不能跨网格复用猜测包围盒。
    UPROPERTY(EditAnywhere, Category="Ground") TArray<FDemoPlayerSolePoint> RightSole;
    // 单帧最大向上修正cm；超过12说明资产/地面错误，记录失败并限制外观突变，不拉长腿。
    UPROPERTY(EditAnywhere, Category="Ground") float MaximumCorrection = 12.f;
    // 浮点边界余量cm，0.02cm不改变视觉站立高度，避免压缩误差重复切换。
    UPROPERTY(EditAnywhere, Category="Ground") float SurfaceMargin = .02f;
    /** Skeleton/RequiredBones为当前图拥有的只读骨容器；缺任何脚/脚趾骨或鞋底数据时明确拒绝。 */
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
    /** Output为已经混合后的组件Pose，OutBoneTransforms返回原骨长IK结果；工作线程只读取本节点值。 */
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
    /** DebugData由引擎动画调试器拥有，本节点只附加可读名称。 */
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;
private:
    /** RequiredBones变化时缓存四条末端骨引用，腿上节从真实父链推导，不能硬编码Compact索引。 */
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
    /** Pose/Out为当前工作线程本帧值；Foot/Ball/Sole为单侧只读契约，解完保留脚旋转和脚趾局部姿态。 */
    void CorrectFoot(FComponentSpacePoseContext& Pose, const FBoneReference& Foot, const FBoneReference& Ball,
        const TArray<FDemoPlayerSolePoint>& Sole, TArray<FBoneTransform>& Out);
    // 本侧末端骨及脚趾骨；InitializeBoneReferences随LOD重建有效索引。
    UPROPERTY() FBoneReference LeftFoot;
    UPROPERTY() FBoneReference LeftBall;
    // 右侧引用独立，不能假设左右骨索引连续。
    UPROPERTY() FBoneReference RightFoot;
    UPROPERTY() FBoneReference RightBall;
};
