#include "Animation/AnimNode_DemoPlayerGrounding.h"
#include "Animation/AnimInstanceProxy.h"
#include "TwoBoneIK.h"
#include "Debug/DemoLog.h"

FAnimNode_DemoPlayerGrounding::FAnimNode_DemoPlayerGrounding()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    Alpha=1.f; LeftFoot.BoneName=TEXT("foot_l"); LeftBall.BoneName=TEXT("ball_l");
    RightFoot.BoneName=TEXT("foot_r"); RightBall.BoneName=TEXT("ball_r");
}
void FAnimNode_DemoPlayerGrounding::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    LeftFoot.Initialize(RequiredBones); LeftBall.Initialize(RequiredBones);
    RightFoot.Initialize(RequiredBones); RightBall.Initialize(RequiredBones);
}
bool FAnimNode_DemoPlayerGrounding::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    return LeftFoot.IsValidToEvaluate(RequiredBones) && LeftBall.IsValidToEvaluate(RequiredBones)
        && RightFoot.IsValidToEvaluate(RequiredBones) && RightBall.IsValidToEvaluate(RequiredBones)
        && LeftSole.Num()>=4 && RightSole.Num()>=4;
}
void FAnimNode_DemoPlayerGrounding::CorrectFoot(FComponentSpacePoseContext& Pose, const FBoneReference& Foot,
    const FBoneReference& Ball, const TArray<FDemoPlayerSolePoint>& Sole, TArray<FBoneTransform>& Out)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const FBoneContainer& Bones=Pose.Pose.GetPose().GetBoneContainer(); // 只读Compact映射归当前动画Pose所有。
    const FCompactPoseBoneIndex End=Foot.GetCompactPoseIndex(Bones); // 本侧踝关节，不是鞋底位置。
    const FCompactPoseBoneIndex Toe=Ball.GetCompactPoseIndex(Bones); // 本侧脚趾，鞋前掌确有20%影响。
    const FCompactPoseBoneIndex Lower=Bones.GetParentBoneIndex(End); // 原骨链脚->小腿->大腿，保持原骨长。
    const FCompactPoseBoneIndex Upper=Lower!=INDEX_NONE?Bones.GetParentBoneIndex(Lower):FCompactPoseBoneIndex(INDEX_NONE); // 无完整链时拒绝而不访问无效Pose。
    if (Lower==INDEX_NONE || Upper==INDEX_NONE) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_FOOT_CHAIN_MISSING %s"),*Foot.BoneName.ToString()); return; }
    FTransform Ankle=Pose.Pose.GetComponentSpaceTransform(End); // 解IK前保存真实脚旋转/缩放。
    const FTransform ToePose=Pose.Pose.GetComponentSpaceTransform(Toe); // 本帧最终混合脚趾Pose，不能用参考位置替代。
    const FVector Normal=FloorNormal.GetSafeNormal(); // 由游戏线程传入，但保护损坏数据造成NaN。
    float Minimum=TNumericLimits<float>::Max(); // 最低真实鞋底点与地面平面的有符号距离cm。
    for (const FDemoPlayerSolePoint& Point:Sole)
    {
        const FVector Skinned=Ankle.TransformPosition(Point.FootLocalPosition)*Point.FootWeight
            +ToePose.TransformPosition(Point.BallLocalPosition)*(1.f-Point.FootWeight); // 与当前鞋双骨LBS完全相同，保持角点分离。
        Minimum=FMath::Min(Minimum,static_cast<float>(FVector::DotProduct(Skinned-FloorPoint,Normal)));
    }
    if (Minimum>=SurfaceMargin || Normal.IsNearlyZero()) return; // 已在空中的摆脚保持原轨迹，只校正低于接触边界的脚。
    const float Correction=FMath::Min(SurfaceMargin-Minimum,MaximumCorrection); // 单向抬踝，不将腾空脚下拉或抬整个角色。
    if (SurfaceMargin-Minimum>MaximumCorrection) UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_FOOT_CORRECTION_LIMIT bone=%s penetration=%.3f limit=%.3f"),*Foot.BoneName.ToString(),-Minimum,MaximumCorrection);
    FTransform Thigh=Pose.Pose.GetComponentSpaceTransform(Upper); // 原大腿头位置保持固定，不改骨盆。
    FTransform Calf=Pose.Pose.GetComponentSpaceTransform(Lower); // 保留当帧膝盖弯曲方向，避免脚修正造成翻膝。
    const FVector Axis=(Ankle.GetLocation()-Thigh.GetLocation()).GetSafeNormal(); // 当前完整腿的纵向轴。
    FVector Bend=Calf.GetLocation()-Thigh.GetLocation(); // 膝相对髋向量，投影为现有弯曲面方向。
    Bend=(Bend-Axis*FVector::DotProduct(Bend,Axis)).GetSafeNormal();
    if (Bend.IsNearlyZero()) Bend=FVector::CrossProduct(Axis,FVector::RightVector).GetSafeNormal(); // 近乎笔直腿仅使用稳定组件方向作为极点，不伸长骨。
    const FVector Pole=Calf.GetLocation()+Bend*30.f; // 保持膝原方向的极点cm，有限偏移不影响链长。
    const FVector Target=Ankle.GetLocation()+Normal*Correction; // 最终鞋底整体沿平面法线抬到接触边界。
    const FQuat Rotation=Ankle.GetRotation(); // 只解踝位置，鞋和脚趾旋转继续来自原动作。
    AnimationCore::SolveTwoBoneIK(Thigh,Calf,Ankle,Pole,Target,false,1.f,1.f);
    Ankle.SetRotation(Rotation);
    if (FVector::Dist(Ankle.GetLocation(),Target)>.05f) UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_FOOT_IK_UNREACHABLE bone=%s error=%.3f"),*Foot.BoneName.ToString(),FVector::Dist(Ankle.GetLocation(),Target));
    Out.Add(FBoneTransform(Upper,Thigh)); Out.Add(FBoneTransform(Lower,Calf)); Out.Add(FBoneTransform(End,Ankle));
}
void FAnimNode_DemoPlayerGrounding::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!bGrounded) return; // 真实腾空/移动地面缺失时，不把脚吸回地板。
    CorrectFoot(Output,LeftFoot,LeftBall,LeftSole,OutBoneTransforms);
    CorrectFoot(Output,RightFoot,RightBall,RightSole,OutBoneTransforms);
    OutBoneTransforms.Sort(FCompareBoneTransformIndex()); // 引擎LocalBlendCSBoneTransforms要求父骨先于子骨。
}
void FAnimNode_DemoPlayerGrounding::GatherDebugData(FNodeDebugData& DebugData)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    DebugData.AddDebugItem(DebugData.GetNodeName(this)); ComponentPose.GatherDebugData(DebugData);
}
