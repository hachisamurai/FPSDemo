// 职责：在最终瞄准节点采样已验收的Blender姿态库，保留Lyra腰背及源动作握点；工作线程只读配置和Pose。
#include "Animation/AnimNode_DemoPlayerAim.h"
#include "Animation/DemoAuthoredAimProfile.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Characters/DemoCharacter.h" // PreUpdate游戏线程只取当前Melee策略；工作线程不保存Pawn/组件/Definition裸指针。
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Animation/DemoPlayerAnimProxy.h" // UE5.4本帧NativeUpdate之后的纯值，不从PreUpdate复制上一帧焦点。
#include "Animation/AnimInstanceProxy.h"
#include "Math/RotationMatrix.h"
#include "TwoBoneIK.h"
#include "Debug/DemoLog.h"

namespace
{
    /** 在递增角度轴中取包围格及0..1权重；端点钳位，不外推极限姿态。 */
    int32 AimGridCell(const TArray<float>& Axis,float Value,float& Weight)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        int32 Cell=0; // 下界格，最多倒数第二项。
        while (Cell+2<Axis.Num() && Value>Axis[Cell+1]) ++Cell;
        Weight=FMath::Clamp((Value-Axis[Cell])/FMath::Max(.001f,Axis[Cell+1]-Axis[Cell]),0.f,1.f);
        return Cell;
    }
    /** 局部刚体混合：只插值旋转/平移，强制单位缩放，禁止借缩放修复骨长。 */
    FTransform AimBlend(const FTransform& A,const FTransform& B,float Weight)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        return FTransform(FQuat::Slerp(A.GetRotation(),B.GetRotation(),Weight).GetNormalized(),FMath::Lerp(A.GetLocation(),B.GetLocation(),Weight));
    }

}

bool FAnimNode_DemoPlayerAim::HasPreUpdate() const
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return true; }

void FAnimNode_DemoPlayerAim::PreUpdate(const UAnimInstance* InAnimInstance)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const UDemoPlayerAnimInstance* Player=Cast<UDemoPlayerAnimInstance>(InAnimInstance); // 引擎同步屏障之前借用，不保存实例裸指针。
    AuthoredProfile=Player?Player->AuthoredAimProfile.Get():nullptr;
    const ADemoCharacter* Pawn=Player?Cast<ADemoCharacter>(Player->GetOwningActor()):nullptr; // PreUpdate先于NativeUpdate，直接取本GT当前装备事实，避免切回枪仍用上一帧近战flag。
    const UDemoMeleeComponent* Melee=Pawn?Pawn->FindComponentByClass<UDemoMeleeComponent>():nullptr;
    const UDemoMeleeDefinition* Definition=Melee && Melee->IsEquipped()?Melee->GetDefinition():nullptr;
    const bool bNextDistributed=Pawn && !Pawn->IsCoopEliminated() && Definition && Definition->bUseDistributedThirdPersonAim && Definition->bAlignTraceToEvaluatedGrip;
    if (bUseDistributedMeleeAim!=bNextDistributed)
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_DISTRIBUTED_AIM_POLICY pawn=%s definition=%s enabled=%d"),*GetNameSafe(Pawn),*GetNameSafe(Definition),bNextDistributed); // 装备/阵亡边沿才打印，不每帧刷业务日志。
    bUseDistributedMeleeAim=bNextDistributed; // 动画任务屏障之前发布纯值，Pitch/HeadYaw仍走本帧原图pins。
}

bool FAnimNode_DemoPlayerAim::EvaluateAuthoredAim(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const UDemoAuthoredAimProfile& Profile=*AuthoredProfile; // PreUpdate强引用保活；导入完成之后禁止运行时改共享资产。
    const FBoneContainer& Bones=Output.Pose.GetPose().GetBoneContainer(); // 本次求值只读骨映射。
    const int32 Count=Bones.GetCompactPoseNumBones(),Columns=Profile.Yaws.Num(); // 紧凑骨数及格列数。
    if (Columns<2 || Profile.Pitches.Num()<2 || Profile.Samples.Num()!=Columns*Profile.Pitches.Num() || Profile.ReferencePose.Num()!=Profile.BoneNames.Num())
    { UE_LOG(LogFPSDemo,Warning,TEXT("AUTHORED_AIM_INVALID_GRID profile=%s"),*Profile.GetName()); return false; }
    if (MappedProfile!=&Profile || AuthoredIndices.Num()!=Count)
    {
        AuthoredIndices.SetNum(Count);
        for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex())
            AuthoredIndices[Index.GetInt()]=Profile.BoneNames.IndexOfByKey(Bones.GetReferenceSkeleton().GetBoneName(Bones.MakeMeshPoseIndex(Index).GetInt()));
        MappedProfile=&Profile;
        UE_LOG(LogFPSDemo,Log,TEXT("AUTHORED_AIM_PROFILE profile=%s path=%s bones=%d samples=%d"),*Profile.GetName(),*Profile.GetPathName(),Count,Profile.Samples.Num()); // 记录本节点真正映射的完整身份，不用同名DA/样本数替代候选消费证明。
    }
    const int32 Gun=Profile.BoneNames.IndexOfByKey(TEXT("ik_hand_gun")); // 作者骨名索引，不与Compact混用。
    const FCompactPoseBoneIndex Chest=Bones.GetParentBoneIndex(LeftClavicle.GetCompactPoseIndex(Bones));
    const int32 GunCompact=AuthoredIndices.IndexOfByKey(Gun); // 当前LOD的真实枪锚点。
    if (Gun==INDEX_NONE || GunCompact==INDEX_NONE)
    { UE_LOG(LogFPSDemo,Warning,TEXT("AUTHORED_AIM_MISSING_ANCHOR profile=%s"),*Profile.GetName()); return false; }
    const FVector Up=UpAxis.GetSafeNormal(),Forward=FVector::CrossProduct(RightAxis,Up).GetSafeNormal(); // Mesh平滑后的真实角色轴。
    const FVector Direction=TargetDirection.GetSafeNormal(); // 已受相机范围限制的当前瞄准方向。
    const float ViewPitch=FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Direction,Up),-1.,1.)));
    const float ViewYaw=FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Direction,RightAxis.GetSafeNormal()),FVector::DotProduct(Direction,Forward)));
    float X=0.f,Y=0.f; // 双线性权重，分别表示水平/俯仰格内位置。
    const int32 Column=AimGridCell(Profile.Yaws,ViewYaw,X),Row=AimGridCell(Profile.Pitches,ViewPitch,Y);
    const int32 Cells[4]={Row*Columns+Column,Row*Columns+Column+1,(Row+1)*Columns+Column,(Row+1)*Columns+Column+1};
    for (int32 Cell:Cells) if (Profile.Samples[Cell].LocalPose.Num()!=Profile.BoneNames.Num())
    { UE_LOG(LogFPSDemo,Warning,TEXT("AUTHORED_AIM_INVALID_SAMPLE profile=%s cell=%d"),*Profile.GetName(),Cell); return false; }
    const float Weight=FMath::Clamp(AlignmentWeight,0.f,1.f); // 和换弹/取消同一动作权重，0时完整保留源动作。
#if WITH_EDITOR
    FDemoIdleBreakPoseAudit& FrameAudit=static_cast<FDemoPlayerAnimProxy*>(Output.AnimInstanceProxy)->IdleAudit; // 当前任务只写自身诊断；禁用瞄准时不能留下上一帧Idle应用值。
    FrameAudit.IdleApplied=0.f; FrameAudit.ReachErrors[0]=FrameAudit.ReachErrors[1]=0.f;
#endif
    if (Weight<=KINDA_SMALL_NUMBER)
    {
#if WITH_EDITOR
        if (FrameAudit.bFirePoseEnabled && FrameAudit.FirePoseNames.Num()==Count)
        {
            FrameAudit.FireBeforeAim.SetNum(Count);
            for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex()) FrameAudit.FireBeforeAim[Index.GetInt()]=Output.Pose.GetComponentSpaceTransform(Index);
            FrameAudit.FireAfterAim=FrameAudit.FireBeforeAim; FrameAudit.FireAimLibrary.Reset(); FrameAudit.FireArmSeed.Reset(); ++FrameAudit.FireAimCount; // 真正执行但权重0的透传帧；库/IK未执行明确为空，不用旧数组伪造装填中数据。
        }
#endif
        return true;
    }
    TArray<FTransform> Input,Reference,Sample,Result; // 全部属于本次工作线程栈上的动态数组，无跨帧反馈。
    Input.SetNum(Count); Reference.SetNum(Count); Sample.SetNum(Count); Result.SetNum(Count);
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex())
    {
        const int32 I=Index.GetInt(),Author=AuthoredIndices[I]; // 当前Compact及其作者索引。
        const FCompactPoseBoneIndex Parent=Bones.GetParentBoneIndex(Index);
        Input[I]=Output.Pose.GetComponentSpaceTransform(Index);
        FTransform Local=Parent!=INDEX_NONE?Input[I].GetRelativeTransform(Input[Parent.GetInt()]):Input[I]; // 原输入的本地变换。
        FTransform RefLocal=Local,SampleLocal=Local; // 虚拟骨/缩减LOD缺作者骨时保留输入。
        if (Author!=INDEX_NONE)
        {
            RefLocal=Profile.ReferencePose[Author];
            SampleLocal=AimBlend(AimBlend(Profile.Samples[Cells[0]].LocalPose[Author],Profile.Samples[Cells[1]].LocalPose[Author],X),
                AimBlend(Profile.Samples[Cells[2]].LocalPose[Author],Profile.Samples[Cells[3]].LocalPose[Author],X),Y);
            const uint8 Group=Groups[I]; // 腰背/头颈保留移动局部并叠作者差分；手臂先建立作者肘平面。
            if (Group==3 || Group==4 || Group==6)
            {
                const FQuat Delta=(SampleLocal.GetRotation()*RefLocal.GetRotation().Inverse()).GetNormalized();
                Local.SetRotation((FQuat::Slerp(FQuat::Identity,Delta,Weight)*Local.GetRotation()).GetNormalized());
            }
            else if (Group==1 || Group==2)
            {
                FQuat AimArmRotation=SampleLocal.GetRotation(); // 原策略只提供作者中性肘面；旧枪/装填/近战沿原分支。
                const FDemoPlayerAnimProxy& ActionInput=static_cast<const FDemoPlayerAnimProxy&>(*Output.AnimInstanceProxy); // 当前任务纯值；实例和Profile强引用由引擎任务屏障保活。
                if (ActionInput.bPreserveGunRecoilArmDelta)
                {
                    // 后坐Slot在本节点前。ReferencePose已经由导入器逐骨校验为本枪真实Hold，
                    // 因此保留的是连续肩肘动态而非旧Fire那套错误静态构型。只分配q差，不改骨长/T/S；
                    // 后面仍以同一枪框架的双腕目标运行原有定长求解，不新增第二套IK或腕部补偿。
                    const FQuat ArmAction=(Local.GetRotation()*RefLocal.GetRotation().Inverse()).GetNormalized();
                    AimArmRotation=(ArmAction*AimArmRotation).GetNormalized();
                }
                Local.SetRotation(FQuat::Slerp(Local.GetRotation(),AimArmRotation,Weight).GetNormalized()); // 肘面及动作让权使用同一AlignmentWeight，0仍完整保留源操作。
            }
        }
        Reference[I]=Parent!=INDEX_NONE?RefLocal*Reference[Parent.GetInt()]:RefLocal;
        Sample[I]=Parent!=INDEX_NONE?SampleLocal*Sample[Parent.GetInt()]:SampleLocal;
        Result[I]=Parent!=INDEX_NONE?Local*Result[Parent.GetInt()]:Local;
    }
    // 作者枪相对作者胸部映射到真实Lyra胸部；仅校准该中性框架的完整朝向。
    FTransform Neutral=Sample[GunCompact].GetRelativeTransform(Sample[Chest.GetInt()])*Result[Chest.GetInt()];
    const FVector FromBore=Neutral.TransformVectorNoScale(Profile.BoreInAnchor).GetSafeNormal();
    const FVector FromUp=Neutral.TransformVectorNoScale(Profile.UpInAnchor).GetSafeNormal();
    const FVector DesiredUp=FQuat(Direction,FMath::DegreesToRadians(Profile.CantDegrees)).RotateVector(FVector::VectorPlaneProject(Up,Direction).GetSafeNormal());
    const FQuat Correction=(FRotationMatrix::MakeFromXZ(Direction,DesiredUp).ToQuat()*FRotationMatrix::MakeFromXZ(FromBore,FromUp).ToQuat().Inverse()).GetNormalized();
    Neutral.SetRotation((Correction*Neutral.GetRotation()).GetNormalized()); // 仅腕/枪框架，不再把校准角强塞进spine_05。
    const FDemoPlayerAnimProxy& Focus=static_cast<const FDemoPlayerAnimProxy&>(*Output.AnimInstanceProxy); // AuthoredProfile仅由PlayerAnimInstance发布，保证代理类型；只读引擎冻结的本帧输入。
    if (Focus.bFocusActive && Focus.ProfileIdentity==AuthoredProfile.Get() && !Focus.FocusPoint.ContainsNaN() && !Focus.MuzzleOffset.ContainsNaN())
    {
        FVector FocusPoint=Focus.FocusPoint; // 本次任务可修正的纯值焦点，不回写代理或实例。
        if (Focus.bEvaluatedEye)
        {
            const int32 Head=Neck.GetCompactPoseIndex(Bones).GetInt(); // 当前节点Neck字段已迁移为head；取本帧作者姿态最终头位置。
            const FVector Eye=Focus.MeshToWorld.InverseTransformPosition(Focus.ViewFrame.Resolve(Focus.MeshToWorld.TransformPosition(Result[Head].GetLocation()))); // 仅旧AnimatedHead策略进入；稳定视点使用完整GT焦点快照，不把它替回头骨。
            float Distance=FVector::DotProduct(Focus.FocusPoint-Focus.ViewOrigin,Focus.ViewDirection); // 已平滑距离cm，不能取消原深度过渡。
            const float PlaneDot=FVector::DotProduct(Focus.ViewDirection,Focus.SurfaceNormal); // 冻结表面上的新视线求交，近乎平行或无命中时不除零。
            if (FMath::Abs(PlaneDot)>.01f) Distance-=FVector::DotProduct(Eye-Focus.ViewOrigin,Focus.SurfaceNormal)/PlaneDot;
            FocusPoint=Eye+Focus.ViewDirection*FMath::Clamp(static_cast<double>(Distance),Focus.DistanceLimits.X,Focus.DistanceLimits.Y); // 同帧头位置变化不再让枪追上一帧眼位；只重投影平面，不新增射线/锁定目标。
        }
        const FQuat ViewRotation=Neutral.GetRotation(); // 原作者/完整枪身方向基准，角度上限始终相对它，不在迭代中累计超限。
        const int32 PrimaryHand=Arms[5].GetCompactPoseIndex(Bones).GetInt(); // 原右腕为主握点；只用本帧输入骨姿，不采集上次IK结果。
        const FVector PivotLocal=(Focus.bPreserveGunRecoilArmDelta?Reference[PrimaryHand].GetRelativeTransform(Reference[GunCompact]):Input[PrimaryHand].GetRelativeTransform(Input[GunCompact])).GetLocation(); // 新增量合同已验证双手对枪为刚体：枢轴取真实Hold握点，避免MeshSpace混合的毫米漂移进入焦点解析；其它操作仍取本帧作者握点。
        const FVector Pivot=Neutral.TransformPosition(PivotLocal); // 固定原作者右腕目标位置；围绕武器原点转动曾让手枪低头双臂超出可达域约1.29cm。
        const float Limit=FMath::DegreesToRadians(FMath::IsFinite(Focus.MaxDegrees)?FMath::Clamp(Focus.MaxDegrees,1.f,20.f):15.f); // 防御非法快照，合法配置仍由游戏线程验证。
        const FQuat LocalFrame=FRotationMatrix::MakeFromXZ(Profile.BoreInAnchor,Profile.UpInAnchor).ToQuat(); // 真实枪管/机匣局部基底，不假定所有枪Socket均沿角色X。
        // 对枢轴到枪口的侧向/上向偏置作解析求交；旧固定点迭代在2m长枪上不满足收敛条件，增加步数仍会振荡，不能当作角度限幅残差放过。
        const FVector Lever=LocalFrame.UnrotateVector(Focus.MuzzleOffset-PivotLocal); // 枪管基底下的枢轴→枪口cm，X沿枪管，Y横向，Z向上。
        const double Cant=FMath::DegreesToRadians(Profile.CantDegrees); // 原作者机匣侧倾弧度，保持完整枪框架。
        const double Lateral=Lever.Y*FMath::Cos(Cant)-Lever.Z*FMath::Sin(Cant),Vertical=Lever.Y*FMath::Sin(Cant)+Lever.Z*FMath::Cos(Cant); // 侧倾后横/竖偏置cm。
        const FVector ToFocus=FocusPoint-Pivot; // 本帧主握点到最终眼位焦点的组件空间向量cm。
        const double AxialSquared=ToFocus.SizeSquared()-FMath::Square(Lateral)-FMath::Square(Vertical); // |目标|²=(枪口前向偏置+飞行距离)²+横偏²+竖偏²。
        if (AxialSquared>1. && FMath::Sqrt(AxialSquared)>Lever.X+1.) // 焦点必须在真实枪口前；不可达/身后情况保留原框架，不翻枪。
        {
            const double Along=FMath::Sqrt(AxialSquared); // 枢轴到焦点在最终枪管轴上的正向投影cm。
            const double PitchAngle=FMath::Asin(FMath::Clamp(FVector::DotProduct(ToFocus,Up)/FMath::Sqrt(FMath::Square(Along)+FMath::Square(Vertical)),-1.,1.))-FMath::Atan2(Vertical,Along); // F·Up=Along*sin(Pitch)+Vertical*cos(Pitch)。
            const double Horizontal=Along*FMath::Cos(PitchAngle)-Vertical*FMath::Sin(PitchAngle); // 扣除枪口竖向偏置后的水平投影cm。
            const double YawAngle=FMath::Atan2(FVector::DotProduct(ToFocus,RightAxis.GetSafeNormal()),FVector::DotProduct(ToFocus,Forward))-FMath::Atan2(Lateral,Horizontal); // 扣除侧偏后枪管的水平角，弧度。
            const FVector FocusDirection=((Forward*FMath::Cos(YawAngle)+RightAxis.GetSafeNormal()*FMath::Sin(YawAngle))*FMath::Cos(PitchAngle)+Up*FMath::Sin(PitchAngle)).GetSafeNormal(); // 同组件空间单位方向，不依赖近距离迭代。
            const FVector FocusUp=FQuat(FocusDirection,FMath::DegreesToRadians(Profile.CantDegrees)).RotateVector(FVector::VectorPlaneProject(Up,FocusDirection).GetSafeNormal()); // 保持原机匣侧倾，不引入绕枪管漂移。
            const FQuat Wanted=(FRotationMatrix::MakeFromXZ(FocusDirection,FocusUp).ToQuat()*LocalFrame.Inverse()).GetNormalized(); // 焦点对应的完整枪框架。
            const float Angle=ViewRotation.AngularDistance(Wanted); // 相对原视角框架的完整角，非每次迭代增量。
            Neutral.SetRotation(FQuat::Slerp(ViewRotation,Wanted,Angle>Limit?Limit/Angle:1.f).GetNormalized());
            Neutral.SetTranslation(Pivot-Neutral.GetRotation().RotateVector(PivotLocal)); // 枪与双腕共同绕主握点微调，保持右臂源路径/骨长，左手继续按同一枪框架解算。
        }
        else UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MUZZLE_FOCUS_REJECT focus behind/unreachable from muzzle"));
    }
    const FTransform Action=Input[GunCompact].GetRelativeTransform(Input[Chest.GetInt()]).GetRelativeTransform(Reference[GunCompact].GetRelativeTransform(Reference[Chest.GetInt()])); // 本帧源动作相对中性Hold的后坐/恢复，不取消源动作。
    FTransform GunTarget=AimBlend(Input[GunCompact],Action*Neutral,Weight); // 正常操作目标；随机待机通过下方统一动作通道让权，不能覆盖Fire/Reload时钟。
    const FDemoIdleMotionFrame& Idle=Focus.IdleMotion; // 同帧WeaponAnchor已写完的基础Pose，原生参考由中立态建立。
    const int32 RightHand=Arms[5].GetCompactPoseIndex(Bones).GetInt(),LeftHand=Arms[2].GetCompactPoseIndex(Bones).GetInt(); // 当前LOD腕索引。
    if (IdleReferenceWeapon!=Idle.Weapon || IdleReferenceBones!=Count) { bIdleReferenceValid=false; IdleReferenceWeapon=Idle.Weapon; IdleReferenceBones=Count; }
    const float IdleWeight=bIdleReferenceValid && Idle.bPoseValid && Idle.bReferenceValid && Idle.Pose.Num()==Count && Idle.Reference.Num()==Count && IdlePoseReference.Num()==Count?FMath::Clamp(Idle.Weight,0.f,1.f):0.f; // 缺失/换枪/LOD数据不得使用旧参考。
    for (int32 I=0;I<Count;++I) if (Groups[I]==5) Result[I]=Input[I].GetRelativeTransform(Input[GunCompact])*GunTarget;
#if WITH_EDITOR
    TArray<FTransform> ArmSeed; // 仅显式分层诊断保存初值，不参与生产姿态计算。
    if (Focus.IdleAudit.bFirePoseEnabled) ArmSeed=Result;
#endif
    for (int32 Side=0;Side<2;++Side)
    {
        const int32 Upper=Arms[Side*3].GetCompactPoseIndex(Bones).GetInt(),Lower=Arms[Side*3+1].GetCompactPoseIndex(Bones).GetInt(),Hand=Arms[Side*3+2].GetCompactPoseIndex(Bones).GetInt();
        const FTransform GripInGun=Focus.bPreserveGunRecoilArmDelta?Reference[Hand].GetRelativeTransform(Reference[GunCompact]):Input[Hand].GetRelativeTransform(Input[GunCompact]); // 仅连续后坐采用已验Hold完整位置/旋转握点；移动躯干上的MeshSpace混合不再成为新的滑手目标，装填/旧枪仍保留源握点。
        const FTransform GripTarget=GripInGun*GunTarget; // 同一个枪框架驱动既有定长双腕求解，保留上方连续肩肘初值，不新增IK/骨长补偿。
        const FTransform Target=GripTarget; // 先完成正常瞄准/操作；待机在下方按局部Pose统一混入，不叠加可能不可达的腕位置。
        FTransform After[3]={Result[Upper],Result[Lower],Result[Hand]}; // 作者肘平面作为解算初值，绝不启用拉伸。
        const FVector Pole=Result[Lower].GetLocation()+(Result[Lower].GetLocation()-Result[Upper].GetLocation()).GetSafeNormal()*30.f;
        AnimationCore::SolveTwoBoneIK(After[0],After[1],After[2],Pole,Target.GetLocation(),false,1.f,1.f);
        After[2].SetRotation(Target.GetRotation());
        const float Error=FVector::Dist(After[2].GetLocation(),Target.GetLocation());
#if WITH_EDITOR
        if (Focus.IdleAudit.bEnabled) static_cast<FDemoPlayerAnimProxy*>(Output.AnimInstanceProxy)->IdleAudit.ReachErrors[Side]=Error;
#endif
        if(Error>.1f) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("AUTHORED_AIM_REACH profile=%s side=%d pitch=%.2f yaw=%.2f error=%.3f"),*Profile.GetName(),Side,ViewPitch,ViewYaw,Error);
        for (int32 I=0;I<Count;++I)
        {
            const int32 Segment=static_cast<int32>(Segments[I])-1-Side*3;
            if (Segment>=0 && Segment<3)
            {
                const int32 Original=Arms[Side*3+Segment].GetCompactPoseIndex(Bones).GetInt();
                Result[I]=Input[I].GetRelativeTransform(Input[Original])*After[Segment]; // 扭转骨/手指沿用输入段内局部姿态。

            }
        }
    }
    if (Idle.bCaptureReference && Idle.bReferenceValid && Idle.bPoseValid)
    { IdlePoseReference=Result; bIdleReferenceValid=true; } // 只保存已解算且真实握枪的中立姿态，不能保存IK前的作者肘平面。
    if (IdleWeight>0.f)
    {
        const TArray<FTransform> Normal=Result; // 冻结正常输出；重建父链时不得从已修改的父骨反算原局部姿态。
        const FTransform RightGrip=Input[RightHand].GetRelativeTransform(Input[GunCompact]); // 当前FPS枪的真实握柄，而非Lyra枪模型轴。
        for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex())
        {
            const int32 I=Index.GetInt(),Parent=Bones.GetParentBoneIndex(Index).GetInt(); // 引擎紧凑骨序父先子后，肩至指尖统一重建。
            if (Groups[I]!=1 && Groups[I]!=2) continue;
            const FTransform CurrentLocal=Normal[I].GetRelativeTransform(Normal[Parent]); // 正常动作局部Pose，开火/换弹时沿相同权重恢复。
            FTransform IdleLocal=IdlePoseReference[I].GetRelativeTransform(IdlePoseReference[Parent]); // 当前枪械中立局部骨长/手型。
            if (I==RightHand || Segments[I]!=6) // 右手指保留原枪握持手型；肩肘腕和左手指使用原生动作差量。
            {
                const FQuat NativeLocal=Idle.Pose[I].GetRelativeTransform(Idle.Pose[Parent]).GetRotation(); // 原生当前局部旋转。
                const FQuat NativeNeutral=Idle.Reference[I].GetRelativeTransform(Idle.Reference[Parent]).GetRotation(); // 同轮原生中立局部旋转。
                IdleLocal.SetRotation((NativeLocal*NativeNeutral.Inverse()*IdleLocal.GetRotation()).GetNormalized());
            }
            Result[I]=AimBlend(CurrentLocal,IdleLocal,IdleWeight)*Result[Parent]; // 只混局部旋转，保留已标定平移/单位缩放，固定骨长并天然保证腕可达。
        }
        GunTarget=RightGrip.Inverse()*Result[RightHand]; // 最终右腕唯一驱动枪，待机不再被准星方向校准覆盖。
        for (int32 I=0;I<Count;++I) if (Groups[I]==5) Result[I]=Input[I].GetRelativeTransform(Input[GunCompact])*GunTarget; // 枪/附属IK支系同框架，后续Presentation读取实际结果。
    }
    for (FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex())
        if (Groups[Index.GetInt()]) { Result[Index.GetInt()].NormalizeRotation(); OutBoneTransforms.Add(FBoneTransform(Index,Result[Index.GetInt()])); }
#if WITH_EDITOR
    FDemoIdleBreakPoseAudit& Audit=static_cast<FDemoPlayerAnimProxy*>(Output.AnimInstanceProxy)->IdleAudit; // AuthoredProfile保证Demo代理类型；只复制诊断值，输出姿态已计算完成。
    if (Audit.bEnabled)
    {
        if (Audit.bFirePoseEnabled && Audit.FirePoseNames.Num()==Count)
        {
            // Anchor与本节点共享同一RequiredBones；按名称再次核对，缺LOD/节点则不发布伪造对应。
            bool bSameBoneOrder=true; // 本任务局部校验，不参与任何求解或Pose输出。
            for (const FCompactPoseBoneIndex Index:Output.Pose.GetPose().ForEachBoneIndex())
                bSameBoneOrder&=Audit.FirePoseNames[Index.GetInt()]==Bones.GetReferenceSkeleton().GetBoneName(Bones.MakeMeshPoseIndex(Index).GetInt());
            if (bSameBoneOrder)
            {
                Audit.FireBeforeAim=Input; Audit.FireAfterAim=Result; Audit.FireAimLibrary=Sample; Audit.FireArmSeed=ArmSeed; ++Audit.FireAimCount;
            }
            else UE_LOG(LogFPSDemo,VeryVerbose,TEXT("FIRE_POSE_AUDIT_REJECT compact order changed before Aim"));
        }
        Audit.IdleApplied=IdleWeight; // 记录实际使用值，而非仅有游戏线程意图。
        const int32 Indices[4]={Chest.GetInt(),Arms[5].GetCompactPoseIndex(Bones).GetInt(),Arms[2].GetCompactPoseIndex(Bones).GetInt(),GunCompact}; // 胸、右腕、左腕、枪骨的本帧有效Compact索引。
        for (int32 I=0;I<4;++I) { Audit.BeforeAim[I]=Input[Indices[I]]; Audit.AfterAim[I]=Result[Indices[I]]; }
        ++Audit.AimCount;
    }
#endif
    return true;
}
