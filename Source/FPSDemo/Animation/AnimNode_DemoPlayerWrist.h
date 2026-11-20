// 职责：在最终动画/IK后求前臂蒙皮骨；FourView使用原UE两段twist，HumanRig四段roll，旧腕桥仅兼容旧资产；不修改握点、指骨或Gameplay。
#pragma once
#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_DemoPlayerWrist.generated.h"

/** 共享FP/TP/近战的腕部后处理；配置在PostProcess AnimBP节点，工作线程只使用当前Pose与缓存参考骨。 */
USTRUCT(BlueprintInternalUseOnly)
struct FPSDEMO_API FAnimNode_DemoPlayerWrist : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()
    /** 构造默认UE人体双腕链；不加载资源或访问World。 */
    FAnimNode_DemoPlayerWrist();
    // FourView01显式启用原UE lowerarm_twist_02/01，分配1/3和2/3轴转；专属后处理图保存，禁止因旧共享Skeleton也含这些骨而自动误启用。
    UPROPERTY(EditAnywhere, Category="Wrist") bool bUseOriginalMannequinTwist = false;
    // 前臂承担手掌相对轴转的比例[0,1]；当前校准0.1，手掌最终组件矩阵始终保持。
    UPROPERTY(EditAnywhere, Category="Wrist", meta=(ClampMin="0",ClampMax="1")) float TwistShare=.1f;
    // 近端腕带辅助骨的前臂→手掌旋转比例；与蒙皮权重一起校准，默认1/3。
    UPROPERTY(EditAnywhere, Category="Wrist", meta=(ClampMin="0",ClampMax="1")) float NearRotationFraction=1.f/3.f;
    // 远端腕带辅助骨旋转比例，默认2/3；不能把它当作手腕角度限制。
    UPROPERTY(EditAnywhere, Category="Wrist", meta=(ClampMin="0",ClampMax="1")) float FarRotationFraction=2.f/3.f;
    // 右手局部厘米修形量，仅偏移近端腕带中心线；远端乘0.6，手掌/手指/枪挂点保持不变。
    // 配套Revision19腕部蒙皮校准值；改变前需检查全部换弹与极限俯仰，不能用来挪动握点。
    UPROPERTY(EditAnywhere, Category="Wrist") FVector RightHelperOffset=FVector(-.2,-.97,.16);
    // 两侧依次为lowerarm、hand、wrist_inner、wrist_outer；支持在图节点中检查骨名，不复制网络状态。
    UPROPERTY(EditAnywhere, Category="Wrist") TArray<FBoneReference> Chains;
    /** RequiredBones必须包含双腕全部驱动骨，缩减LOD缺骨则保留输入姿态。 */
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton,const FBoneContainer& RequiredBones) override;
    /** Output是最终组件姿态；一次提交全部骨，防止前臂旋转自动传给本应固定的手/指。 */
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms) override;
    /** 只显示节点名并向输入传播调试，不读游戏线程对象。 */
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;
private:
    /** LOD/骨容器变化时缓存参考组件矩阵及前臂支系归属，不修改共享Skeleton。 */
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
    // 当前RequiredBones对应的参考组件变换；由节点实例拥有，工作线程只读。
    TArray<FTransform> Reference;
    // 0不受前臂轴转影响；1左前臂支系、2右前臂支系，手掌及其后代排除。
    TArray<uint8> ForearmSide;
    // 每侧lowerarm、hand加两根原UE twist或四根HumanRig roll；仅当前LOD缓存，不复制、不保存用户配置。
    TArray<FBoneReference> ModelMatchedChains;
    // 检测到四段前臂布局时使用绝对扭转求值，不能同时执行旧腕桥偏移。
    bool bModelMatched = false;
    /** Output为最终主图姿势，仅输出四根twist或八根roll；采用与Blender相同的绝对分配，覆盖已烘焙值而不叠加。 */
    void EvaluateModelMatched(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms);
};
