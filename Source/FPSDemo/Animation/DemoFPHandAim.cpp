// 职责：FP-HANDAIM-20261120 手部瞄准代理实现。主图（含左手TwoBoneIK、开火/换弹Slot）求值完成后，在组件空间：
// ①按枪口相对枪挂接骨（新四枪ik_hand_gun、旧枪hand_r）的刚性变换求“绕握点转多少能让枪管延长线穿过准星目标”；
// ②限幅+极短半衰期平滑（低滞后）；③转挂接骨（即转枪），双臂两骨IK让双手保持与枪的作者相对关系。PostProcess AnimBP（腕部twist）仍在之后运行。
#include "Animation/DemoFPHandAim.h"
#include "Animation/DemoFPAnimInstance.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"
#include "TwoBoneIK.h"
#include "Debug/DemoLog.h"

FDemoFPHandAimProxy::FDemoFPHandAimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); // 非UObject代理不能使用依赖this->GetName的宏。
}

void FDemoFPHandAimProxy::PreUpdate(UAnimInstance* InAnimInstance, float InDeltaSeconds)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    FAnimInstanceProxy::PreUpdate(InAnimInstance,InDeltaSeconds);
    // 游戏线程：实例由引擎保证存活；只复制纯值，工作线程不再触碰UObject。
    if (const UDemoFPAnimInstance* FP=Cast<UDemoFPAnimInstance>(InAnimInstance)) Input=FP->GetFirstPersonHandAim();
    else Input=FDemoFPHandAimInput();
    DeltaSeconds=FMath::IsFinite(InDeltaSeconds)?FMath::Max(InDeltaSeconds,0.f):0.f;
}

bool FDemoFPHandAimProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    EvaluateAnimationNode_WithRoot(Output,InRootNode); // 先完整求值原图，保证未激活时输出与旧行为逐位一致。
    const float TargetWeight=Input.bActive?1.f:0.f;
    Weight=FMath::FInterpConstantTo(Weight,TargetWeight,DeltaSeconds,FMath::Max(Input.BlendSpeed,.1f));
    if (Weight<=KINDA_SMALL_NUMBER)
    {
        Weight=0.f; SmoothedCorrection=FQuat::Identity; // 完全淡出后清状态，下次启用从作者姿态开始。
        return true;
    }
    if (!ApplyHandAim(Output)) { Weight=0.f; SmoothedCorrection=FQuat::Identity; }
    return true;
}

bool FDemoFPHandAimProxy::ApplyHandAim(FPoseContext& Output)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
    // 每次按名称解析紧凑索引：LOD/RequiredBones变化时无需额外缓存失效逻辑，成本为少量名称查找。
    auto Resolve=[&Bones](FName Name)->FCompactPoseBoneIndex // 按值捕获无；引用Bones仅本函数内使用。
    {
        if (Name.IsNone()) return FCompactPoseBoneIndex(INDEX_NONE);
        FBoneReference Ref(Name); Ref.Initialize(Bones);
        return Ref.IsValidToEvaluate(Bones)?Ref.GetCompactPoseIndex(Bones):FCompactPoseBoneIndex(INDEX_NONE);
    };
    auto Fail=[this](const TCHAR* Why)->bool // 捕获this仅用于去重日志字段，同步调用。
    {
        const FName Reason(Why);
        if (Reason!=LastFailure) { UE_LOG(LogFPSDemo,Warning,TEXT("FP_HAND_AIM_SKIP %s"),Why); LastFailure=Reason; }
        return false;
    };
    const FCompactPoseBoneIndex Anchor=Resolve(Input.AnchorBone);
    if (!Anchor.IsValid()) return Fail(TEXT("missing gun anchor bone"));
    const FCompactPoseBoneIndex Hand=Resolve(Input.HandBone);
    if (!Hand.IsValid()) return Fail(TEXT("missing hand bone"));
    const bool bAnchorIsHand=Anchor==Hand; // 旧枪socket挂在hand_r：转手即转枪；新枪挂独立ik_hand_gun：先转锚骨，再让双手跟随。
    const FCompactPoseBoneIndex Lower=Bones.GetParentBoneIndex(Hand);
    const FCompactPoseBoneIndex Upper=Lower.IsValid()?Bones.GetParentBoneIndex(Lower):FCompactPoseBoneIndex(INDEX_NONE);
    if (!Lower.IsValid() || !Upper.IsValid()) return Fail(TEXT("hand bone has no two-bone parent chain"));
    const FCompactPoseBoneIndex Support=Resolve(Input.SupportHandBone);
    const FCompactPoseBoneIndex SupportLower=Support.IsValid()?Bones.GetParentBoneIndex(Support):FCompactPoseBoneIndex(INDEX_NONE);
    const FCompactPoseBoneIndex SupportUpper=SupportLower.IsValid()?Bones.GetParentBoneIndex(SupportLower):FCompactPoseBoneIndex(INDEX_NONE);
    const bool bSupport=SupportUpper.IsValid();

    FCSPose<FCompactPose> CS; CS.InitPose(Output.Pose);
    const FTransform AnchorCS=CS.GetComponentSpaceTransform(Anchor); // 枪刚性挂接骨的作者姿态。
    const FTransform HandCS=CS.GetComponentSpaceTransform(Hand); // 右手作者姿态；修改锚骨前先取，避免缓存失效后读到新值。
    const FTransform GripCS=Input.GripInHand*AnchorCS;
    const FTransform MuzzleCS=Input.MuzzleInHand*AnchorCS;
    const FVector Grip=GripCS.GetLocation();
    const FVector Axis=MuzzleCS.GetUnitAxis(EAxis::X); // 已标定枪口环法向。
    const FVector GripToMuzzle=MuzzleCS.GetLocation()-Grip;
    const FVector GripToAim=Input.TargetCS-Grip;
    // 与旧MUZZLE-AIM相同的解析式：在原枪管上找到“到握点距离=握点到目标距离”的前向点，绕握点转到目标方向。
    const double Axial=FVector::DotProduct(GripToMuzzle,Axis);
    const double Disc=GripToAim.SizeSquared()-GripToMuzzle.SizeSquared()+Axial*Axial;
    const double Travel=Disc>0.?FMath::Sqrt(Disc)-Axial:-1.;
    if (Travel<=0. || FVector::DotProduct(GripToAim,Axis)<=0.) return Fail(TEXT("target behind or inside barrel"));
    FQuat Desired=FQuat::FindBetweenNormals((GripToMuzzle+Axis*Travel).GetSafeNormal(),GripToAim.GetSafeNormal());
    // 限幅：超过上限时沿同一旋转轴截断，保持方向一致。
    FVector RotAxis; float Angle; Desired.ToAxisAndAngle(RotAxis,Angle);
    StatMaxDesiredDeg=FMath::Max(StatMaxDesiredDeg,FMath::RadiansToDegrees(Angle));
    const float MaxRad=FMath::DegreesToRadians(FMath::Clamp(Input.MaxDegrees,0.f,30.f));
    if (Angle>MaxRad) Desired=FQuat(RotAxis,MaxRad);
    // 低滞后平滑：半衰期默认15ms（60fps约一帧收敛一半以上），仅抹平深度跳变；0为直接跟随。
    const float Alpha=Input.SmoothingHalfLife>0.f&&DeltaSeconds>0.f?1.f-FMath::Exp2(-DeltaSeconds/Input.SmoothingHalfLife):1.f;
    SmoothedCorrection=FQuat::Slerp(SmoothedCorrection,Desired,Alpha).GetNormalized();
    const FQuat Applied=FQuat::Slerp(FQuat::Identity,SmoothedCorrection,Weight).GetNormalized();

    // 绕握点刚性旋转：锚骨（枪）、右手、左手都保持与枪的作者相对关系，等价于把“枪+双手”整体绕握点转Applied。
    auto RotateAboutGrip=[&Applied,&Grip](const FTransform& T) // 引用捕获本函数局部值，同步调用。
    {
        FTransform R=T;
        R.SetRotation((Applied*T.GetRotation()).GetNormalized());
        R.SetTranslation(Grip+Applied.RotateVector(T.GetLocation()-Grip));
        return R;
    };
    const FTransform NewAnchor=RotateAboutGrip(AnchorCS);
    const FTransform NewHand=RotateAboutGrip(HandCS);
    const FTransform NewSupport=bSupport?RotateAboutGrip(CS.GetComponentSpaceTransform(Support)):FTransform::Identity;

    // 两骨IK：极向量取“肘相对肩-手中点的外侧”，保持作者肘部朝向；不拉伸骨长。
    auto SolveArm=[&CS](FCompactPoseBoneIndex U,FCompactPoseBoneIndex L,FCompactPoseBoneIndex H,const FTransform& Goal) // 引用CS仅本函数同步使用。
    {
        FTransform UT=CS.GetComponentSpaceTransform(U), LT=CS.GetComponentSpaceTransform(L), HT=CS.GetComponentSpaceTransform(H);
        const FVector Pole=LT.GetLocation()+(LT.GetLocation()-(UT.GetLocation()+HT.GetLocation())*.5);
        AnimationCore::SolveTwoBoneIK(UT,LT,HT,Pole,Goal.GetLocation(),false,1.,1.);
        HT.SetRotation(Goal.GetRotation());
        TArray<FBoneTransform,TInlineAllocator<3>> Out; // 父到子顺序，满足SafeSet排序要求。
        Out.Add(FBoneTransform(U,UT)); Out.Add(FBoneTransform(L,LT)); Out.Add(FBoneTransform(H,HT));
        Out.Sort(FCompareBoneTransformIndex());
        CS.SafeSetCSBoneTransforms(Out);
    };
    if (!bAnchorIsHand)
    {
        // 独立锚骨（ik_hand_gun）：枪挂在此骨上，直接写新姿态即转枪；其子骨随之更新。
        TArray<FBoneTransform,TInlineAllocator<1>> AnchorOut; AnchorOut.Add(FBoneTransform(Anchor,NewAnchor));
        CS.SafeSetCSBoneTransforms(AnchorOut);
    }
    SolveArm(Upper,Lower,Hand,NewHand);
    if (bSupport) SolveArm(SupportUpper,SupportLower,Support,NewSupport);
    FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS),Output.Pose);
    LastFailure=NAME_None;
    // 5秒汇总诊断：UE_LOG线程安全；只输出纯值，判断实机修正量级。
    StatMaxAppliedDeg=FMath::Max(StatMaxAppliedDeg,FMath::RadiansToDegrees(Applied.GetAngle())); ++StatFrames; StatSeconds+=DeltaSeconds;
    if (StatSeconds>=5.f)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("FP_HAND_AIM_STATS frames=%d maxDesiredDeg=%.2f maxAppliedDeg=%.2f limitDeg=%.1f weight=%.2f targetDistCm=%.0f"),
            StatFrames,StatMaxDesiredDeg,StatMaxAppliedDeg,Input.MaxDegrees,Weight,GripToAim.Size());
        StatSeconds=0.f; StatMaxDesiredDeg=0.f; StatMaxAppliedDeg=0.f; StatFrames=0;
    }
    return true;
}

FAnimInstanceProxy* UDemoFPAnimInstance::CreateAnimInstanceProxy()
{
    DEMO_LOG_CALL();
    return new FDemoFPHandAimProxy(this); // 引擎拥有生命周期，DestroyAnimInstanceProxy在任务结束后唯一释放。
}

void UDemoFPAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    DEMO_LOG_CALL();
    delete static_cast<FDemoFPHandAimProxy*>(InProxy); // 只接收Create返回的本实例代理。
}

void UDemoFPAnimInstance::SetFirstPersonHandAim(const FDemoFPHandAimInput& Input)
{
    DEMO_LOG_TICK();
    if (Input.bActive!=bLoggedHandAimActive)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("FP_HAND_AIM_STATE active=%d anchor=%s hand=%s support=%s maxDeg=%.1f halfLife=%.3f"),Input.bActive,
            *Input.AnchorBone.ToString(),*Input.HandBone.ToString(),*Input.SupportHandBone.ToString(),Input.MaxDegrees,Input.SmoothingHalfLife);
        bLoggedHandAimActive=Input.bActive;
    }
    HandAimInput=Input;
}
