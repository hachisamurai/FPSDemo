// 职责：按Blender同一swing-twist规则驱动原UE两段twist或HumanRig四段roll；旧腕桥仅兼容旧模型；保持手和武器目标，不依赖Pawn/网络。
#include "Animation/AnimNode_DemoPlayerWrist.h"
#include "Debug/DemoLog.h"

FAnimNode_DemoPlayerWrist::FAnimNode_DemoPlayerWrist()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); // 动画节点是值结构，沿用项目FAnimNode入口日志形式。
    for (const TCHAR* Side : {TEXT("l"),TEXT("r")})
        for (const TCHAR* Prefix : {TEXT("lowerarm_"),TEXT("hand_"),TEXT("wrist_inner_"),TEXT("wrist_outer_")})
        { FBoneReference Bone; Bone.BoneName=FName(FString(Prefix)+Side); Chains.Add(Bone); } // 仅初始化本节点持有的骨引用。
}

void FAnimNode_DemoPlayerWrist::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    bModelMatched=RequiredBones.GetReferenceSkeleton().FindBoneIndex(TEXT("forearm_roll_01_l"))!=INDEX_NONE;
    ModelMatchedChains.Reset();
    if (bModelMatched || bUseOriginalMannequinTwist)
    {
        const TArray<FString> Prefixes=bUseOriginalMannequinTwist
            ? TArray<FString>{TEXT("lowerarm_"),TEXT("hand_"),TEXT("lowerarm_twist_02_"),TEXT("lowerarm_twist_01_")}
            : TArray<FString>{TEXT("lowerarm_"),TEXT("hand_"),TEXT("forearm_roll_01_"),TEXT("forearm_roll_02_"),TEXT("forearm_roll_03_"),TEXT("forearm_roll_04_")}; // 原骨架02靠肘、01靠腕，顺序与新蒙皮相同。
        for (const TCHAR* Side : {TEXT("l"),TEXT("r")})
            for (const FString& Prefix : Prefixes)
            {
                FBoneReference Bone; // 当前LOD持有的骨映射，完整性由IsValid检查。
                Bone.BoneName=FName(Prefix+Side); Bone.Initialize(RequiredBones); ModelMatchedChains.Add(Bone);
            }
        UE_LOG(LogFPSDemo,Log,TEXT("WRIST_MODE %s"),bUseOriginalMannequinTwist?TEXT("original_mannequin_two_twists"):TEXT("model_matched_distributed_roll"));
    }
    else for (FBoneReference& Bone : Chains) Bone.Initialize(RequiredBones);
    const int32 Count=RequiredBones.GetCompactPoseNumBones(); // 当前LOD所需骨数，不假定完整骨架索引。
    Reference.SetNum(Count); ForearmSide.Init(0,Count);
    for (int32 Index=0;Index<Count;++Index)
    {
        const FCompactPoseBoneIndex Compact(Index); // 本容器内合法紧凑索引。
        const FCompactPoseBoneIndex Parent=RequiredBones.GetParentBoneIndex(Compact);
        Reference[Index]=RequiredBones.GetRefPoseTransform(Compact);
        if (Parent.GetInt()!=INDEX_NONE) Reference[Index]=Reference[Index]*Reference[Parent.GetInt()];
    }
    if (!IsValidToEvaluate(nullptr,RequiredBones))
    { UE_LOG(LogFPSDemo,Warning,TEXT("WRIST_INIT_REJECTED missing chain bones; input pose preserved")); return; }
    if (bModelMatched || bUseOriginalMannequinTwist) return; // 分配骨不转动主前臂，不需要旧腕桥后代归属表。
    for (int32 Index=0;Index<Count;++Index)
        for (int32 Side=0;Side<2;++Side)
        {
            FCompactPoseBoneIndex Ancestor(Index); // 父链遍历止于手或前臂，手指不继承额外轴转。
            const FCompactPoseBoneIndex Lower=Chains[Side*4].GetCompactPoseIndex(RequiredBones);
            const FCompactPoseBoneIndex Hand=Chains[Side*4+1].GetCompactPoseIndex(RequiredBones);
            while (Ancestor.GetInt()!=INDEX_NONE && Ancestor!=Lower && Ancestor!=Hand) Ancestor=RequiredBones.GetParentBoneIndex(Ancestor);
            if (Ancestor==Lower) ForearmSide[Index]=Side+1;
        }
}

bool FAnimNode_DemoPlayerWrist::IsValidToEvaluate(const USkeleton* Skeleton,const FBoneContainer& RequiredBones)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if (bModelMatched || bUseOriginalMannequinTwist)
    {
        if (ModelMatchedChains.Num()!=(bUseOriginalMannequinTwist?8:12) || Reference.Num()!=RequiredBones.GetCompactPoseNumBones()) return false;
        for (const FBoneReference& Bone : ModelMatchedChains)
            if (!Bone.IsValidToEvaluate(RequiredBones))
            { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("WRIST_ROLL_MISSING %s"),*Bone.BoneName.ToString()); return false; }
        return true;
    }
    if (Chains.Num()!=8 || !FMath::IsFinite(TwistShare) || !FMath::IsFinite(NearRotationFraction) || !FMath::IsFinite(FarRotationFraction) || RightHelperOffset.ContainsNaN())
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("WRIST_EVALUATION_REJECTED invalid chain count or numeric configuration")); return false; }
    for (const FBoneReference& Bone : Chains)
        if (!Bone.IsValidToEvaluate(RequiredBones))
        { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("WRIST_EVALUATION_REJECTED missing bone %s"),*Bone.BoneName.ToString()); return false; }
    const bool bReferenceReady=Reference.Num()==RequiredBones.GetCompactPoseNumBones(); // 缓存必须对应当前LOD，未初始化则保持输入。
    if (!bReferenceReady) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("WRIST_EVALUATION_REJECTED stale reference cache"));
    return bReferenceReady;
}

void FAnimNode_DemoPlayerWrist::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if (bModelMatched || bUseOriginalMannequinTwist) { EvaluateModelMatched(Output,OutBoneTransforms); return; }
    const FBoneContainer& Bones=Output.Pose.GetPose().GetBoneContainer(); // 当前求值线程的不可变骨映射。
    TArray<FTransform> Result; Result.SetNum(Reference.Num()); // 每次从输入重建，不能逐帧累积扭转。
    for (FCompactPoseBoneIndex Index : Output.Pose.GetPose().ForEachBoneIndex()) Result[Index.GetInt()]=Output.Pose.GetComponentSpaceTransform(Index);
    for (int32 Side=0;Side<2;++Side)
    {
        const int32 Lower=Chains[Side*4].GetCompactPoseIndex(Bones).GetInt(); // 同侧四个有效组件索引。
        const int32 Hand=Chains[Side*4+1].GetCompactPoseIndex(Bones).GetInt();
        const FTransform OriginalLower=Result[Lower],OriginalHand=Result[Hand]; // 原握点完整保存，包括指骨目标。
        const FVector Axis=(Reference[Hand].GetLocation()-Reference[Lower].GetLocation()).GetSafeNormal(); // 绑定空间前臂长轴。
        const FVector CurrentAxis=(OriginalHand.GetLocation()-OriginalLower.GetLocation()).GetSafeNormal();
        if (Axis.IsNearlyZero() || CurrentAxis.IsNearlyZero())
        { UE_LOG(LogFPSDemo,Warning,TEXT("WRIST_DEGENERATE_CHAIN side=%d"),Side); continue; }
        const FQuat ForeDeform=OriginalLower.GetRotation()*Reference[Lower].GetRotation().Inverse();
        const FQuat HandDeform=OriginalHand.GetRotation()*Reference[Hand].GetRotation().Inverse();
        const FQuat Relative=(ForeDeform.Inverse()*HandDeform).GetNormalized(); // 分解相对旋转，不能把整体角度当作前臂轴转。
        const float Projection=FVector::DotProduct(FVector(Relative.X,Relative.Y,Relative.Z),Axis);
        FQuat Twist(Axis.X*Projection,Axis.Y*Projection,Axis.Z*Projection,Relative.W); // swing-twist分解，仅取沿长轴的虚部分量。
        Twist.Normalize(); if (Twist.W<0.f) Twist=Twist*-1.f;
        const float Angle=2.f*FMath::Atan2(FVector::DotProduct(FVector(Twist.X,Twist.Y,Twist.Z),Axis),Twist.W);
        const FQuat Roll(CurrentAxis,Angle*FMath::Clamp(TwistShare,0.f,1.f)); // 只绕肘腕连线，主关节位置不改变。
        for (int32 Index=0;Index<Result.Num();++Index)
            if (ForearmSide[Index]==Side+1)
            {
                Result[Index].SetLocation(OriginalLower.GetLocation()+Roll.RotateVector(Result[Index].GetLocation()-OriginalLower.GetLocation()));
                Result[Index].SetRotation((Roll*Result[Index].GetRotation()).GetNormalized());
            }
        const FQuat CorrectedFore=Result[Lower].GetRotation()*Reference[Lower].GetRotation().Inverse();
        for (int32 Helper=0;Helper<2;++Helper)
        {
            const int32 Index=Chains[Side*4+2+Helper].GetCompactPoseIndex(Bones).GetInt(); // 现有蒙皮辅助骨；不替换手骨或附着武器。
            const float Fraction=FMath::Clamp(Helper==0?NearRotationFraction:FarRotationFraction,0.f,1.f);
            const FQuat Deform=FQuat::Slerp(CorrectedFore,HandDeform,Fraction).GetNormalized(); // 逐层旋转插值减少LBS在大角度处塌陷。
            Result[Index].SetRotation((Deform*Reference[Index].GetRotation()).GetNormalized());
            Result[Index].SetLocation(OriginalHand.GetLocation()+Deform.RotateVector(Reference[Index].GetLocation()-Reference[Hand].GetLocation()));
            if (Side==1)
            {
                // 腕带自身中心线修形给枪托留净空；局部偏移随手转动，不改任何手/指目标或骨架参考。
                Result[Index].AddToTranslation(OriginalHand.GetRotation().RotateVector(RightHelperOffset)*(Helper==0?1.f:.6f));
            }
        }
    }
    for (FCompactPoseBoneIndex Index : Output.Pose.GetPose().ForEachBoneIndex())
        OutBoneTransforms.Add(FBoneTransform(Index,Result[Index.GetInt()])); // 手/指原CS也显式提交，抵消父前臂新增轴转。
}

void FAnimNode_DemoPlayerWrist::EvaluateModelMatched(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const FBoneContainer& Bones=Output.Pose.GetPose().GetBoneContainer(); // 当前工作线程骨缓存，不访问UObject状态。
    const int32 Segments=bUseOriginalMannequinTwist?2:4; // 本配置对应蒙皮段数，不改变参考或主骨长。
    const int32 Stride=Segments+2; // 每侧缓存依次为主肘、手、分配骨。
    for (int32 Side=0;Side<2;++Side)
    {
        const FCompactPoseBoneIndex Lower=ModelMatchedChains[Side*Stride].GetCompactPoseIndex(Bones);
        const FCompactPoseBoneIndex Hand=ModelMatchedChains[Side*Stride+1].GetCompactPoseIndex(Bones);
        const FTransform& ElbowPose=Output.Pose.GetComponentSpaceTransform(Lower); // 主图/IK最终肘，不移动它。
        const FTransform& HandPose=Output.Pose.GetComponentSpaceTransform(Hand); // 最终掌姿，手指和武器不受本节点干扰。
        const FTransform& ElbowBind=Reference[Lower.GetInt()];
        const FTransform& HandBind=Reference[Hand.GetInt()];
        const FVector Axis=(HandBind.GetLocation()-ElbowBind.GetLocation()).GetSafeNormal(); // 参考空间前臂轴，避免猜FBX局部X/Y。
        const FQuat Fore=(ElbowPose.GetRotation()*ElbowBind.GetRotation().Inverse()).GetNormalized();
        const FQuat Palm=(HandPose.GetRotation()*HandBind.GetRotation().Inverse()).GetNormalized();
        const FQuat Delta=(Fore.Inverse()*Palm).GetNormalized();
        const double Projection=FVector::DotProduct(FVector(Delta.X,Delta.Y,Delta.Z),Axis);
        FQuat Twist(Axis.X*Projection,Axis.Y*Projection,Axis.Z*Projection,Delta.W); // swing-twist，仅分配旋前/旋后。
        if (Axis.IsNearlyZero() || Twist.SizeSquared()<SMALL_NUMBER)
        { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("WRIST_ROLL_DEGENERATE side=%d"),Side); continue; }
        Twist.Normalize(); if (Twist.W<0.) Twist=Twist*-1.;
        const double Angle=2.*FMath::Atan2(FVector::DotProduct(FVector(Twist.X,Twist.Y,Twist.Z),Axis),Twist.W);
        for (int32 Segment=0;Segment<Segments;++Segment)
        {
            const FCompactPoseBoneIndex Index=ModelMatchedChains[Side*Stride+2+Segment].GetCompactPoseIndex(Bones);
            const FTransform& Bind=Reference[Index.GetInt()]; // 蒙皮与参考位置由同一导入网格提供。
            const double Fraction=double(Segment+1)/(Segments+1); // 原UE为1/3、2/3；HumanRig为20/40/60/80%，与Blender作者一致。
            FTransform Result=Bind; // 绝对重建，不读取上一次/动画已烘焙的roll旋转。
            Result.SetLocation(ElbowPose.GetLocation()+Fore.RotateVector(Bind.GetLocation()-ElbowBind.GetLocation()));
            Result.SetRotation((Fore*FQuat(Axis,Angle*Fraction)*Bind.GetRotation()).GetNormalized());
            Result.SetScale3D(ElbowPose.GetScale3D());
            OutBoneTransforms.Add(FBoneTransform(Index,Result));
        }
    }
    // 骨控制基类要求输出按紧凑索引升序；纯值比较，无捕获、不越过本次工作线程求值生命周期。
    OutBoneTransforms.Sort([](const FBoneTransform& A,const FBoneTransform& B){return A.BoneIndex<B.BoneIndex;});
}

void FAnimNode_DemoPlayerWrist::GatherDebugData(FNodeDebugData& DebugData)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    DebugData.AddDebugItem(DebugData.GetNodeName(this)); ComponentPose.GatherDebugData(DebugData);
}
