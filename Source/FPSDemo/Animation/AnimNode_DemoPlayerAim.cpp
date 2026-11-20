// 职责：对已混合全身Pose应用头部/武器瞄准；显式近战沿真实脊柱链作FK分配，枪械保留旧/作者路径。
// 用法：最终Aim图节点消费本帧角度pins及PreUpdate纯值；不访问World，不改镜头、移动、源动画或权威伤害。
#include "Animation/AnimNode_DemoPlayerAim.h"
#include "Animation/AnimInstanceProxy.h"
#include "TwoBoneIK.h" // 只解原臂长，公共握枪框架不能通过移动锁骨根或拉长胳膊保持接触。
#include "Debug/DemoLog.h"

FAnimNode_DemoPlayerAim::FAnimNode_DemoPlayerAim()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    Alpha=1.f; Spine.BoneName=TEXT("spine_01"); Neck.BoneName=TEXT("head"); // 颈环同时混合neck_01/02，整段颈部和衣口保留基础姿态；头骨绕自身关节转动，避免拖动领内皮肤。
    LeftClavicle.BoneName=TEXT("clavicle_l"); RightClavicle.BoneName=TEXT("clavicle_r");
    HandRoot.BoneName=TEXT("ik_hand_root"); Pelvis.BoneName=TEXT("pelvis");
    for (const TCHAR* Name:{TEXT("upperarm_l"),TEXT("lowerarm_l"),TEXT("hand_l"),TEXT("upperarm_r"),TEXT("lowerarm_r"),TEXT("hand_r")}) // 固定语义顺序，实际Compact索引后续解析。
    { FBoneReference Bone; Bone.BoneName=Name; Arms.Add(Bone); } // 节点独占骨引用值，不能保存外部临时指针。
}
void FAnimNode_DemoPlayerAim::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (Neck.BoneName==TEXT("neck_01") || Neck.BoneName==TEXT("neck_02")) // 兼容已经保存的旧图节点数据；不要求用户手工改BP或重新烘全部动画。
    { Neck.BoneName=TEXT("head"); UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_AIM_NECK_MIGRATED neck/collar preserved; head joint follows view")); }
    for (FBoneReference* Bone:{&Spine,&Neck,&LeftClavicle,&RightClavicle,&HandRoot,&Pelvis}) Bone->Initialize(RequiredBones); // 当前节点自己的六个引用，无外部所有权。
    AuthoredIndices.Reset(); MappedProfile=nullptr; // LOD即便骨数相同也重建作者骨映射。
    bIdleReferenceValid=false; // 同骨数的LOD/网格重绑也不能沿用旧腕/胸参考；等待新中立姿态。
    Groups.Init(0,RequiredBones.GetCompactPoseNumBones());
    Segments.Init(0,RequiredBones.GetCompactPoseNumBones());
    for (FBoneReference& Bone:Arms) Bone.Initialize(RequiredBones); // 原肩肘腕链全部重绑当前LOD。
    const FCompactPoseBoneIndex ChestRoot=LeftClavicle.IsValidToEvaluate(RequiredBones)?RequiredBones.GetParentBoneIndex(LeftClavicle.GetCompactPoseIndex(RequiredBones)):FCompactPoseBoneIndex(INDEX_NONE); // 两锁骨共同父骨作为整块上胸模式的旋转根；缺骨交给正式有效性检查拒绝。
    MeleeSpineChain.Reset(); MeleeSpineSteps.Init(INDEX_NONE,RequiredBones.GetCompactPoseNumBones()); // LOD/网格重建必须重新解析真实父链。
    if (Pelvis.IsValidToEvaluate(RequiredBones) && Spine.IsValidToEvaluate(RequiredBones) && ChestRoot!=INDEX_NONE)
    {
        const FCompactPoseBoneIndex Hip=Pelvis.GetCompactPoseIndex(RequiredBones); // 当前LOD实际骨盆，只作闭合父链边界，不参与旋转。
        FCompactPoseBoneIndex Ancestor=ChestRoot;
        while (Ancestor!=INDEX_NONE && Ancestor!=Hip) { MeleeSpineChain.Add(Ancestor); Ancestor=RequiredBones.GetParentBoneIndex(Ancestor); }
        if (Ancestor!=Hip || MeleeSpineChain.IsEmpty() || MeleeSpineChain.Last()!=Spine.GetCompactPoseIndex(RequiredBones)) MeleeSpineChain.Reset(); // 缺失/不连通时保留旧安全路径，不猜一条脊柱。
        else
        {
            for (int32 Left=0,Right=MeleeSpineChain.Num()-1;Left<Right;++Left,--Right) MeleeSpineChain.Swap(Left,Right); // 从骨盆向上父先子后，不假设骨名编号连续。
            for (int32 Step=0;Step<MeleeSpineChain.Num();++Step) MeleeSpineSteps[MeleeSpineChain[Step].GetInt()]=Step;
        }
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_DISTRIBUTED_AIM_CHAIN compact_bones=%d spine_bones=%d"),RequiredBones.GetCompactPoseNumBones(),MeleeSpineChain.Num()); // 仅LOD缓存重建，不逐帧输出。
    }
    for (int32 Index=0;Index<Groups.Num();++Index) // 真实Compact序号仅用于数组索引，骨名只在初始化查询。
    {
        FCompactPoseBoneIndex Ancestor(Index); // 向上找最近分支，手指/扭转骨自动随所属手臂旋转。
        while (Ancestor!=INDEX_NONE)
        {
            if (Ancestor==LeftClavicle.GetCompactPoseIndex(RequiredBones)) { Groups[Index]=1; break; }
            if (Ancestor==RightClavicle.GetCompactPoseIndex(RequiredBones)) { Groups[Index]=2; break; }
            if (Ancestor==Neck.GetCompactPoseIndex(RequiredBones)) { Groups[Index]=3; break; }
            if (Ancestor==ChestRoot) { Groups[Index]=4; break; } // 上胸及颈部使用同一刚体框架，避免领口和武器分别旋转。
            if (Ancestor==Spine.GetCompactPoseIndex(RequiredBones)) { Groups[Index]=6; break; } // 腰至上胸以下单独分类；整块上胸模式保持这些骨的原位置/旋转。
            if (Ancestor==HandRoot.GetCompactPoseIndex(RequiredBones)) { Groups[Index]=5; break; }
            Ancestor=RequiredBones.GetParentBoneIndex(Ancestor);
        }
        Ancestor=FCompactPoseBoneIndex(Index);
        while (Ancestor!=INDEX_NONE && !Segments[Index]) // 最近段祖先优先，使手指走腕映射、扭转骨走其真实父臂段。
        {
            for (int32 Segment=0;Segment<Arms.Num();++Segment) if (Ancestor==Arms[Segment].GetCompactPoseIndex(RequiredBones)) { Segments[Index]=Segment+1; break; } // 仅初始化扫描，非逐帧查名字。
            Ancestor=RequiredBones.GetParentBoneIndex(Ancestor);
        }
    }
}
bool FAnimNode_DemoPlayerAim::IsValidToEvaluate(const USkeleton* Skeleton,const FBoneContainer& RequiredBones)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    for (const FBoneReference* Bone:{&Spine,&Neck,&LeftClavicle,&RightClavicle,&HandRoot,&Pelvis}) // 缺少任何契约骨都保留输入Pose，不访问INDEX_NONE。
        if (!Bone->IsValidToEvaluate(RequiredBones)) return false;
    for (const FBoneReference& Bone:Arms) if (!Bone.IsValidToEvaluate(RequiredBones)) return false; // 不能在缩减LOD缺手骨时访问无效Pose。
    return RequiredBones.GetParentBoneIndex(LeftClavicle.GetCompactPoseIndex(RequiredBones))==RequiredBones.GetParentBoneIndex(RightClavicle.GetCompactPoseIndex(RequiredBones));
}
void FAnimNode_DemoPlayerAim::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!bEnabled && FMath::IsNearlyZero(HeadYaw)) return; // 无装备时也支持纯转头，不修改完整移动Pose的其他骨。
    if (!FMath::IsFinite(Pitch) || !FMath::IsFinite(HeadYaw) || RightAxis.ContainsNaN() || RightAxis.IsNearlyZero() || UpAxis.ContainsNaN() || UpAxis.IsNearlyZero())
    { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_AIM_POSE_INVALID pitch=%g headYaw=%g right=%s up=%s"),Pitch,HeadYaw,*RightAxis.ToString(),*UpAxis.ToString()); return; }
    const FBoneContainer& Bones=Output.Pose.GetPose().GetBoneContainer(); // 当前工作线程Pose只读骨映射。
    if (bEnabled && bUseDistributedMeleeAim && TorsoFraction>=.99f && EvaluateDistributedMeleeAim(Output,OutBoneTransforms)) return; // 当前有效近战优先，不让上一帧枪Profile覆盖新装备；枪械从不进入此分支。
    if (bEnabled && AuthoredProfile && EvaluateAuthoredAim(Output,OutBoneTransforms)) return; // 已验收多方向姿态独占瞄准，禁止与旧整胸旋转叠加。
    const FVector Axis=RightAxis.GetSafeNormal(); // 配置轴归一化，不将缩放传入四元数。
    const float Angle=bEnabled?FMath::Clamp(Pitch,-75.f,75.f):0.f; // 无武器只转头；正常俯仰已由相机共用配置限制。
    const bool bRigidChest=TorsoFraction>=.99f; // 显式完整上胸模式供散弹枪使用；其余0..0.35仍是旧腰部小角度+肩臂IK模式。
    const float TorsoAngle=Angle*(bRigidChest?1.f:FMath::Clamp(TorsoFraction,0.f,.35f)); // 整块上胸与枪共用完整角度，握姿不再被两个独立框架撕开。
    const FVector SpinePivot=Output.Pose.GetComponentSpaceTransform(bRigidChest?Bones.GetParentBoneIndex(LeftClavicle.GetCompactPoseIndex(Bones)):Spine.GetCompactPoseIndex(Bones)).GetLocation(); // 完整模式绕固定上胸关节旋转，腰/骨盆不跟随；保留所有局部骨长。
    FQuat TorsoRotation(Axis,FMath::DegreesToRadians(-TorsoAngle)); // 旧资产的有界俯仰；有中性枪管标定则从本帧混合胸部重新解算。
    float AppliedYaw=0.f; // 度；头部只补剩余角，避免枪胸跟转后再次叠加完整头身Yaw。
    if (bEnabled && bRigidChest && bCalibratedBore && !BoreInChest.IsNearlyZero() && !TargetDirection.IsNearlyZero())
    {
        const FTransform& Chest=Output.Pose.GetComponentSpaceTransform(Bones.GetParentBoneIndex(LeftClavicle.GetCompactPoseIndex(Bones))); // 本次输入Pose，而非上一帧求值输出。
        const FVector Up=UpAxis.GetSafeNormal(); // 真实世界竖轴在网格空间，用于先Yaw后Pitch，避免最短弧引入不必要的Roll。
        const FVector From=Chest.TransformVectorNoScale(BoreInChest).GetSafeNormal(),To=TargetDirection.GetSafeNormal(); // 校正移动/跳跃骨盆带来的枪管中性方向差。
        const FVector FromFlat=FVector::VectorPlaneProject(From,Up).GetSafeNormal(),ToFlat=FVector::VectorPlaneProject(To,Up).GetSafeNormal();
        if (!FromFlat.IsNearlyZero() && !ToFlat.IsNearlyZero())
        {
            const float Yaw=FMath::Atan2(FVector::DotProduct(Up,FVector::CrossProduct(FromFlat,ToFlat)),FVector::DotProduct(FromFlat,ToFlat)); // 弧度，跨180取最短方向。
            const float PitchDelta=FMath::Asin(FMath::Clamp(FVector::DotProduct(To,Up),-1.,1.))-FMath::Asin(FMath::Clamp(FVector::DotProduct(From,Up),-1.,1.));
            const FQuat YawRotation(Up,Yaw),PitchRotation(FVector::CrossProduct(Up,ToFlat).GetSafeNormal(),-PitchDelta); // 同一个公共框架旋转胸/肩/腕/枪，骨长与握点不变。
            const float Weight=FMath::Clamp(AlignmentWeight,0.f,1.f); // 本帧只读游戏线程动作权重。
            TorsoRotation=FQuat::Slerp(FQuat::Identity,PitchRotation*YawRotation,Weight).GetNormalized();
            AppliedYaw=FMath::RadiansToDegrees(Yaw)*Weight;
        }
    }
    const FTransform TorsoTransform(TorsoRotation,SpinePivot-TorsoRotation.RotateVector(SpinePivot)); // 公共刚体映射，子骨相对距离不变。
    const FVector ChestPivot=TorsoTransform.TransformPosition(Output.Pose.GetComponentSpaceTransform(Bones.GetParentBoneIndex(LeftClavicle.GetCompactPoseIndex(Bones))).GetLocation()); // 共同胸部父骨是两臂枢轴；锁骨局部骨长不变。
    const FQuat ArmRotation(Axis,FMath::DegreesToRadians(-(Angle-TorsoAngle))); // 同一个世界俯仰轴驱动枪和两臂，侧身持枪也保持枪口正确朝向。
    const FTransform ArmTransform(ArmRotation,ChestPivot-ArmRotation.RotateVector(ChestPivot)); // 整枪与握枪双手共享，禁止单独转枪造成脱手。
    const FQuat LeftRotation=FQuat::Slerp(FQuat::Identity,ArmRotation,FMath::Clamp(SupportWeight,0.f,1.f)); // 自然单手手枪不抬左臂，装填中段恢复双手同步。
    const FTransform LeftTransform(LeftRotation,ChestPivot-LeftRotation.RotateVector(ChestPivot)); // 支撑手过渡仅改变肩臂层权重，不重写指握姿。
    const FVector NeckPivot=TorsoTransform.TransformPosition(Output.Pose.GetComponentSpaceTransform(Neck.GetCompactPoseIndex(Bones)).GetLocation()); // 头关节位置固定，两节颈骨只继承躯干和原动画；喉片/颈环/衣口不被额外转头拖入翻领。
    const float HeadTurnClearance=FMath::SmoothStep(30.f,45.f,FMath::Abs(HeadYaw)); // 水平转头末段轻微收下巴，保留混合head权重的上颈环与固定衣领间隙；不减少45度观察范围。
    const float HeadPitchDegrees=FMath::Lerp(FMath::Clamp((Angle-TorsoAngle)*.45f,-2.f,2.f),-2.f,HeadTurnClearance); // 度；极限转头收下巴2度，中段平滑衔接原俯仰，头关节/骨长不平移。
    const FQuat HeadPitch(Axis,FMath::DegreesToRadians(-HeadPitchDegrees)); // 只改变头部附加姿态，相机、枪口和双臂保持原俯仰范围。
    const FQuat HeadRotation=FQuat(TorsoRotation.RotateVector(UpAxis.GetSafeNormal()),FMath::DegreesToRadians(FMath::Clamp(HeadYaw-AppliedYaw,-60.f,60.f)))*HeadPitch; // 只补公共胸部转向后剩余头角，不重复应用自由观察Yaw。
    const FTransform HeadTransform(HeadRotation,NeckPivot-HeadRotation.RotateVector(NeckPivot)); // 头骨关节保持原位置，两节颈骨保留基础姿态，眼球通过head父链自动继承。
    const FTransform CurrentPelvis=Output.Pose.GetComponentSpaceTransform(Pelvis.GetCompactPoseIndex(Bones)); // 移动Pose真实骨盆，只有独立IK支系需要补此运动。
    const FVector LeftPivot=TorsoTransform.TransformPosition(Output.Pose.GetComponentSpaceTransform(LeftClavicle.GetCompactPoseIndex(Bones)).GetLocation()); // 实际锁骨根不能随公共胸枢轴发生本地平移。
    const FVector RightPivot=TorsoTransform.TransformPosition(Output.Pose.GetComponentSpaceTransform(RightClavicle.GetCompactPoseIndex(Bones)).GetLocation()); // 两侧锁骨根各自固定在躯干Pose的位置。
    const FTransform LeftClavicleTransform(LeftRotation,LeftPivot-LeftRotation.RotateVector(LeftPivot)); // 只转锁骨，腕部之后由IK重新贴回公共枪框架。
    const FTransform RightClavicleTransform(ArmRotation,RightPivot-ArmRotation.RotateVector(RightPivot)); // 原本地骨偏移不变，消除极限角度肩部平移。
    TArray<FTransform> Transforms; Transforms.SetNum(Bones.GetCompactPoseNumBones()); // 本帧值数组，骨数小；不在工作线程持有场景对象。
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex()) // 父骨优先输出所有受影响子骨，避免手指仍保留旧CS姿势。
    {
        const uint8 Group=Groups.IsValidIndex(Index.GetInt())?Groups[Index.GetInt()]:0; // LOD变化极端边界保留未分类骨。
        FTransform Result=Output.Pose.GetComponentSpaceTransform(Index); // 本帧原混合值副本，不在原Pose上累计旋转。
        if (!Group || (!bEnabled && Group!=3) || (bRigidChest && Group==6)) { Transforms[Index.GetInt()]=Result; continue; } // 纯转头不做骨盆/枪/臂映射；完整上胸模式不移动下方脊柱。
        if (Group==5 && !bAnchorAlreadyAligned) Result=Result.GetRelativeTransform(ReferencePelvis)*CurrentPelvis; // 旧图在这里映射；新图已在上身分层之前映射，不能重复。
        Result=Result*TorsoTransform;
        if (Group==1) Result=Result*LeftClavicleTransform;
        else if (Group==2) Result=Result*RightClavicleTransform;
        else if (Group==5) { Result=Result*ArmTransform; Result.AddToTranslation(GripOffset); }
        else if (Group==3) Result=Result*HeadTransform;
        Result.NormalizeRotation(); Transforms[Index.GetInt()]=Result;
    }
    for (int32 Side=0;bEnabled && !bRigidChest && Side<2;++Side) // 完整上胸模式保留原始握姿/肘部并共享刚体旋转，不重复IK破坏已对齐的枪与手；原肩臂模式继续IK。
    {
        FTransform Target=Output.Pose.GetComponentSpaceTransform(Arms[Side*3+2].GetCompactPoseIndex(Bones))*TorsoTransform*(Side==0?LeftTransform:ArmTransform); // 原腕->躯干->共同瞄准，保存原手指相对握柄关系。
        Target.AddToTranslation(GripOffset*(Side==0?FMath::Clamp(SupportWeight,0.f,1.f):1.f)); // 自然下垂左手不被低头避让偏移带走。
        if (Side==0) Target.AddToTranslation(-Axis*6.f*(1.f-FMath::Clamp(SupportWeight,0.f,1.f))); // 手枪自然左手/取消过渡向外留6cm袖口与衣身间隙，完整装填接触段及长枪不偏移。
        SolveArm(Transforms,Side,Target,Bones);
    }
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex()) // 所有IK后的CS按骨序提交，父子一次完成，不分帧改手和枪。
        if (Groups[Index.GetInt()] && (bEnabled || Groups[Index.GetInt()]==3)) OutBoneTransforms.Add(FBoneTransform(Index,Transforms[Index.GetInt()])); // 无装备纯转头仅提交head子树，两节颈骨不覆盖其他控制层。
}

bool FAnimNode_DemoPlayerAim::EvaluateDistributedMeleeAim(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const FBoneContainer& Bones=Output.Pose.GetPose().GetBoneContainer(); // 仅本次引擎任务借用，所有角色状态已由GT发布。
    const int32 Count=Bones.GetCompactPoseNumBones();
    if (MeleeSpineChain.IsEmpty() || MeleeSpineSteps.Num()!=Count || Groups.Num()!=Count)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_DISTRIBUTED_AIM_FALLBACK incomplete LOD chain")); return false; }
    const FCompactPoseBoneIndex Chest=MeleeSpineChain.Last(),Head=Neck.GetCompactPoseIndex(Bones); // 已通过本节点有效性检查。
    const FVector Up=UpAxis.GetSafeNormal(),Right=RightAxis.GetSafeNormal(); // 单位轴属于实际Mesh组件空间，不能直接当骨局部轴。
    const float TotalPitch=FMath::Clamp(Pitch,-30.f,30.f); // 原DA仍默认20且Validate上限30；本路径不扩大配置的总俯仰。
    const float TorsoYaw=FMath::Clamp(HeadYaw,-60.f,60.f)*.5f; // 待机胸腰分担一半原自由观察Yaw，头只补剩余角；不旋转Root/胶囊。
    const FQuat TotalRotation=(FQuat(Up,FMath::DegreesToRadians(TorsoYaw))*FQuat(Right,FMath::DegreesToRadians(-TotalPitch))).GetNormalized();
    const FQuat StepRotation=FQuat::Slerp(FQuat::Identity,TotalRotation,1.f/MeleeSpineChain.Num()).GetNormalized(); // 以总四元数的N次根分摊，避免Yaw/Pitch各自累乘产生额外总角。
    TArray<FTransform> Input,Locals,Result; Input.SetNum(Count); Locals.SetNum(Count); Result.SetNum(Count); // 本次任务的纯值暂存，不写原输入或跨帧反馈。
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex())
        Input[Index.GetInt()]=Output.Pose.GetComponentSpaceTransform(Index);
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex())
    {
        Locals[Index.GetInt()]=Output.Pose.GetLocalSpaceTransform(Index); // 读取真实本地T/Q/S，避免对CS位置强行旋转导致拉伸原臂长/骨长。
    }
    const float HeadTurnClearance=FMath::SmoothStep(30.f,45.f,FMath::Abs(HeadYaw)); // 保留旧极限转头收下巴2度的有界衣领间隙策略。
    const FQuat HeadResidual=(FQuat(TotalRotation.RotateVector(Up),FMath::DegreesToRadians(HeadYaw-TorsoYaw))
        *FQuat(TotalRotation.RotateVector(Right),FMath::DegreesToRadians(2.f*HeadTurnClearance))).GetNormalized(); // 头只补胸腰未承担的Yaw，不重复完整观察角。
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex()) // 引擎CompactPose遍历父先子后，先提交到纯值数组，全部成功后才发布Out。
    {
        const int32 I=Index.GetInt();
        const FCompactPoseBoneIndex Parent=Bones.GetParentBoneIndex(Index);
        FTransform Current=Input[I]; // Root/骨盆/腿保持原组件空间结果，既不做补偿也不修改其局部姿态。
        if (Groups[I] && Groups[I]!=5)
        {
            Current=Parent!=INDEX_NONE?Locals[I]*Result[Parent.GetInt()]:Locals[I];
            if (MeleeSpineSteps[I]!=INDEX_NONE)
                Current.SetRotation((StepRotation*Current.GetRotation()).GetNormalized()); // 在组件空间左乘；等价父框转换后的local delta，平移/缩放不变。
            if (Index==Head)
                Current.SetRotation((HeadResidual*Current.GetRotation()).GetNormalized()); // 只改头关节旋转，后代用原locals继承，不逐骨重复施加。
        }
        Current.NormalizeRotation();
        if (Current.ContainsNaN()) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_DISTRIBUTED_AIM_FALLBACK invalid transform index=%d"),I); return false; }
        Result[I]=Current;
    }
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex()) // 独立支系第二遍：不假定HandRoot在骨索引中排在Chest之后。
    {
        const int32 I=Index.GetInt();
        if (Groups[I]!=5) continue;
        const FCompactPoseBoneIndex Parent=Bones.GetParentBoneIndex(Index);
        Result[I]=Parent==INDEX_NONE || Groups[Parent.GetInt()]!=5
            ?Input[I].GetRelativeTransform(Input[Chest.GetInt()])*Result[Chest.GetInt()]
            :Locals[I]*Result[Parent.GetInt()]; // 独立IK支系根与世界刀/真手共用新胸框架；子骨继续沿原locals继承。
        Result[I].NormalizeRotation();
        if (Result[I].ContainsNaN()) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_DISTRIBUTED_AIM_FALLBACK invalid IK transform index=%d"),I); return false; }
    }
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex())
        if (Groups[Index.GetInt()]) OutBoneTransforms.Add(FBoneTransform(Index,Result[Index.GetInt()])); // 单次按骨序提交完整上身；不走枪械IK/枪管校准，不覆盖Root/骨盆/腿。
    return true;
}

void FAnimNode_DemoPlayerAim::SolveArm(TArray<FTransform>& Transforms,int32 Side,const FTransform& Target,const FBoneContainer& Bones)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const int32 Upper=Arms[Side*3].GetCompactPoseIndex(Bones).GetInt(),Lower=Arms[Side*3+1].GetCompactPoseIndex(Bones).GetInt(),Hand=Arms[Side*3+2].GetCompactPoseIndex(Bones).GetInt(); // 已验证完整臂链的CS数组索引。
    const FTransform Before[3]={Transforms[Upper],Transforms[Lower],Transforms[Hand]}; // 本次IK前局部关系基准，扭转骨/手指必须跟随对应段修正。
    FTransform After[3]={Before[0],Before[1],Before[2]}; // 独立求解值，肩位置固定，腕旋转仍取原握枪目标。
    const FVector Direction=(Before[2].GetLocation()-Before[0].GetLocation()).GetSafeNormal(); // 臂纵向轴，保持本姿势的肘弯曲面。
    const FVector Elbow=Before[1].GetLocation()-Before[0].GetLocation(); // 当前肘位置在臂纵向正交平面的分量。
    FVector Bend=(Elbow-Direction*FVector::DotProduct(Elbow,Direction)).GetSafeNormal(); // 不在快转时固定任意极点导致翻肘。
    if (Bend.IsNearlyZero()) Bend=FVector::CrossProduct(Direction,RightAxis).GetSafeNormal(); // 极少数直臂退回稳定参考平面。
    const float HighSupport=FMath::SmoothStep(0.f,50.f,Pitch)*FMath::Clamp(SupportWeight,0.f,1.f); // 从抬枪起同步展开两肘，不能等25度才避让而在换弹过渡碰枪托；单手权重为0、换弹Pitch归零。
    const FVector Forward=FVector::CrossProduct(RightAxis,UpAxis).GetSafeNormal(); // 游戏线程已提供真实组件轴，右×上为角色前向，不假设导入姿势。
    const FVector ClearancePole=Side==0?Forward*100.f-RightAxis.GetSafeNormal()*25.f:RightAxis.GetSafeNormal()*100.f+Forward*20.f; // 左肘前展避头，右肘外展避枪托；两者仅为解算极点，不修改骨位置/长度契约。
    const FVector JointTarget=FMath::Lerp(Before[1].GetLocation()+Bend*30.f,Before[0].GetLocation()+ClearancePole,HighSupport); // 保留原低角度肘面，连续过渡到高举安全平面。
    AnimationCore::SolveTwoBoneIK(After[0],After[1],After[2],JointTarget,Target.GetLocation(),false,1.f,1.f);
    After[2].SetRotation(Target.GetRotation());
    const float Error=FVector::Dist(After[2].GetLocation(),Target.GetLocation()); // 真实不可达距离，绝不通过拉长骨或移动锁骨根掩盖。
    if (Error>.05f) UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_AIM_UNREACHABLE side=%d pitch=%.2f error=%.3f"),Side,Pitch,Error);
    for (int32 Index=0;Index<Transforms.Num();++Index) // 最近段继承保证Twist与五指保持各自局部旋转/平移。
    {
        const int32 Segment=static_cast<int32>(Segments[Index])-1-Side*3; // 当前侧0/1/2有效，另一侧和躯干不改变。
        if (Segment>=0 && Segment<3) Transforms[Index]=Transforms[Index].GetRelativeTransform(Before[Segment])*After[Segment];
    }
}
void FAnimNode_DemoPlayerAim::GatherDebugData(FNodeDebugData& DebugData)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    DebugData.AddDebugItem(DebugData.GetNodeName(this)); ComponentPose.GatherDebugData(DebugData);
}
