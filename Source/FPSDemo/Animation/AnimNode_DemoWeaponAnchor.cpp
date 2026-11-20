// 职责：同帧移动/武器映射；近战显式Idle中立脊柱并保留Base注视，避免源全身弯背/抬头补偿重复继承。旧DA不变，工作线程只处理Pose与配置纯值。
#include "Animation/AnimNode_DemoWeaponAnchor.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/DemoLyraAnimInstance.h"
#include "Animation/DemoPlayerAnimProxy.h" // Editor诊断只写本帧纯值快照，绝不修改基础/上身姿态。
#include "Weapons/Melee/DemoMeleeComponent.h" // 仅PreUpdate游戏线程读取当前装备及其配置，不由动画工作线程查询组件。
#include "Weapons/Melee/DemoMeleeDefinition.h" // 作者脊柱为单个近战DA显式选择，旧配置默认关闭。
#include "GameFramework/Actor.h"
#include "Debug/DemoLog.h"

void FAnimNode_DemoWeaponAnchor::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); FAnimNode_Base::Initialize_AnyThread(Context); Base.Initialize(Context); Upper.Initialize(Context); Pelvis.BoneName=TEXT("pelvis"); HandRoot.BoneName=TEXT("ik_hand_root"); Chest.BoneName=TEXT("spine_05"); }

void FAnimNode_DemoWeaponAnchor::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    Base.CacheBones(Context); Upper.CacheBones(Context);
    bIdleBonesChanged=true; // 同骨数LOD切换也重新采集中立，不用数组长度猜测骨序相同。
    const FBoneContainer& Bones=Context.AnimInstanceProxy->GetRequiredBones(); // 仅本次缓存借用，索引随LOD重建。
    Pelvis.Initialize(Bones); HandRoot.Initialize(Bones); Chest.Initialize(Bones);
    SpineChain.Reset(); ViewChain.Reset();
    if (Chest.IsValidToEvaluate(Bones) && Pelvis.IsValidToEvaluate(Bones))
    {
        FCompactPoseBoneIndex Ancestor=Chest.GetCompactPoseIndex(Bones); // 从真实父链取脊柱，不假定索引连续。
        while (Ancestor!=INDEX_NONE && Ancestor!=Pelvis.GetCompactPoseIndex(Bones))
        { SpineChain.Add(Ancestor); Ancestor=Bones.GetParentBoneIndex(Ancestor); }
        if (Ancestor==INDEX_NONE)
        { SpineChain.Reset(); UE_LOG(LogFPSDemo,Warning,TEXT("WEAPON_ANCHOR_SPINE_INVALID chest is not below pelvis; retain legacy mapping")); }
        const int32 HeadMeshIndex=Bones.GetReferenceSkeleton().FindBoneIndex(TEXT("head")); // 实际Mesh骨序，不将Skeleton索引直接当Compact索引。
        Ancestor=HeadMeshIndex!=INDEX_NONE?Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(HeadMeshIndex)):FCompactPoseBoneIndex(INDEX_NONE);
        while (Ancestor!=INDEX_NONE && Ancestor!=Chest.GetCompactPoseIndex(Bones))
        { ViewChain.Add(Ancestor); Ancestor=Bones.GetParentBoneIndex(Ancestor); }
        if (Ancestor==INDEX_NONE) { ViewChain.Reset(); UE_LOG(LogFPSDemo,VeryVerbose,TEXT("WEAPON_ANCHOR_VIEW_FALLBACK missing head-to-chest chain at current LOD")); }
    }
}

bool FAnimNode_DemoWeaponAnchor::HasPreUpdate() const
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return true; }

void FAnimNode_DemoWeaponAnchor::PreUpdate(const UAnimInstance* InAnimInstance)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(InAnimInstance); // 引擎保证游戏线程、实例存活至本次动画任务完成。
    bUsesDemoProxy=Native!=nullptr; // 构造同一自定义代理的实例，允许任务顺序传递基础Pose差量。
    const AActor* Owner=Native?Native->GetOwningActor():nullptr; // 本次调用借用；预览/销毁期间允许无Owner。
    const UDemoMeleeComponent* Melee=Owner?Owner->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 组件由角色拥有，不跨线程保存。
    const UDemoMeleeDefinition* Definition=Melee && Melee->IsEquipped()?Melee->GetDefinition():nullptr; // 卸下后恢复原策略；在同装备取消/回待机期间保持策略不变。
    const bool bNextAuthoredMeleeSpine=Definition && (Definition->bUseAuthoredSpine || Definition->bUseUpperBodyMontage || (Native && Native->bUseMeleeUpperBodyMontage)); // DA直接参与避免PreUpdate先于NativeUpdate时首帧读到旧策略；专用上身Montage只叠一次作者脊柱旋转。
    if (bNextAuthoredMeleeSpine!=bUseAuthoredMeleeSpine)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("WEAPON_ANCHOR_AUTHORED_SPINE owner=%s definition=%s enabled=%d"),*GetNameSafe(Owner),*GetNameSafe(Definition),bNextAuthoredMeleeSpine);
        bUseAuthoredMeleeSpine=bNextAuthoredMeleeSpine;
    }
    const bool bNextIdleReference=Definition && Definition->bUseWeaponIdleSpineReference; // 只有显式选择的新候选改变中立策略。
    FString NextError; // 本帧校验原因，不持有资产或跨线程借用引用。
    SpineReferenceRotations.Reset(); // 卸下/失败时不能残留上一武器的参考。
    const bool bNextReferenceValid=bNextIdleReference && Definition->CopySpineReference(SpineReferenceRotations,NextError);
    const FString NextIdentity=bNextIdleReference?Definition->GetPathName()+TEXT("|")+Definition->SpineReferenceRevision:FString(); // 只用于变化日志，不参与骨骼计算。
    if (bNextIdleReference!=bUseWeaponIdleSpineReference || bNextReferenceValid!=bSpineReferenceValid || NextIdentity!=SpineReferenceIdentity || NextError!=SpineReferenceError)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("WEAPON_ANCHOR_SPINE_REFERENCE owner=%s definition=%s idle_reference=%d valid=%d revision=%s reason=%s"),
            *GetNameSafe(Owner),*GetNameSafe(Definition),bNextIdleReference,bNextReferenceValid,
            Definition?*Definition->SpineReferenceRevision:TEXT(""),*NextError);
    }
    bUseWeaponIdleSpineReference=bNextIdleReference; bSpineReferenceValid=bNextReferenceValid;
    SpineReferenceIdentity=NextIdentity; SpineReferenceError=NextError; // 本节点拥有的数据，在动画任务派发前完成写入。
    // 作者delta作用于整套上身（包括取消混合），不能关闭移动补偿；枪械与未选择的旧近战仍走原恢复分支。
    bPreserveSpine=Native && Native->bPreserveLocomotionSpine;
}

void FAnimNode_DemoWeaponAnchor::Update_AnyThread(const FAnimationUpdateContext& Context)
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); GetEvaluateGraphExposedInputs().Execute(Context); Base.Update(Context); Upper.Update(Context); }

void FAnimNode_DemoWeaponAnchor::Evaluate_AnyThread(FPoseContext& Output)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    Upper.Evaluate(Output);
    const FBoneContainer& Bones=Output.Pose.GetBoneContainer(); // 输入Pose拥有，仅本次求值借用。
    if (!bEnabled || !Pelvis.IsValidToEvaluate(Bones) || !HandRoot.IsValidToEvaluate(Bones)) return;
    FPoseContext BaseOutput(Output); // 本次求值的独立临时Pose，不跨线程保存。
    Base.Evaluate(BaseOutput);
    FCSPose<FCompactPose> BaseCS; BaseCS.InitPose(BaseOutput.Pose); // 局部转组件，真实骨盆可包含跳跃和转向混合。
    FCSPose<FCompactPose> UpperCS; UpperCS.InitPose(Output.Pose);
    if (bUsesDemoProxy)
    {
        FDemoIdleMotionFrame& Frame=static_cast<FDemoPlayerAnimProxy*>(Output.AnimInstanceProxy)->IdleMotion; // 本图依赖保证先写基础Pose，最终Aim后读；不与游戏线程并发。
        const int32 Count=Bones.GetCompactPoseNumBones(); // 当前LOD，两个输入/最终Aim共用同一RequiredBones。
        if (bIdleBonesChanged || Frame.ReferenceWeapon!=Frame.Weapon || Frame.Reference.Num()!=Count) { Frame.bReferenceValid=false; Frame.ReferenceWeapon=Frame.Weapon; }
        bIdleBonesChanged=false;
        if (!Frame.Weapon.IsNone() && (Frame.bCaptureReference || Frame.Weight>0.f))
        {
            Frame.Pose.SetNum(Count);
            for (FCompactPoseBoneIndex Index:BaseOutput.Pose.ForEachBoneIndex()) Frame.Pose[Index.GetInt()]=BaseCS.GetComponentSpaceTransform(Index);
            Frame.bPoseValid=true;
            if (Frame.bCaptureReference) { Frame.Reference=Frame.Pose; Frame.bReferenceValid=true; }
        }
    }
    const FCompactPoseBoneIndex Hip=Pelvis.GetCompactPoseIndex(Bones); // 当前LOD合法索引。
    const FCompactPoseBoneIndex Hand=HandRoot.GetCompactPoseIndex(Bones); // 独立支系根索引。
    const FCompactPoseBoneIndex Parent=Bones.GetParentBoneIndex(Hand); // 通常为root；保留骨架层级通用性。
    const bool bUseChest=(bPreserveSpine || bUseWeaponIdleSpineReference) && !SpineChain.IsEmpty(); // 显式Idle参考失败时也安全保留Base脊柱，不恢复静态Ref差量。
    if(bPreserveSpine && !bUseChest) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("WEAPON_ANCHOR_SPINE_FALLBACK missing valid LOD spine chain")); // 高频失败路径保留埋点，不默认逐帧刷屏。
    bool bCompleteIdleReference=!bUseWeaponIdleSpineReference || bSpineReferenceValid; // 完整五骨同一参考，不能在同一胸链混用两种基准。
    if (bUseWeaponIdleSpineReference && bCompleteIdleReference)
        for (const FCompactPoseBoneIndex Bone:SpineChain)
        {
            const FName BoneName=Bones.GetReferenceSkeleton().GetBoneName(Bones.MakeMeshPoseIndex(Bone).GetInt()); // 名称映射不依赖Compact连续索引。
            if (!SpineReferenceRotations.Contains(BoneName))
            { bCompleteIdleReference=false; UE_LOG(LogFPSDemo,VeryVerbose,TEXT("WEAPON_ANCHOR_SPINE_REFERENCE_LOD_REJECT bone=%s; preserve Base spine"),*BoneName.ToString()); break; }
        }
    const bool bApplyAuthoredSpine=bUseAuthoredMeleeSpine && !SpineChain.IsEmpty() && bCompleteIdleReference; // 默认false的参考策略及所有旧DA分支保持不变。
    FTransform Mapped; // 最终独立IK支系的组件空间变换；两条分支各写一次，避免root/yaw重复映射。
    if (bApplyAuthoredSpine)
    {
        const FTransform UpperHand=UpperCS.GetComponentSpaceTransform(Hand),UpperChest=UpperCS.GetComponentSpaceTransform(Chest.GetCompactPoseIndex(Bones)); // 修改Output前冻结作者共同框架，手/枪相对Chest关系保持一致。
        FTransform FinalChest=BaseCS.GetComponentSpaceTransform(Hip); // 实际移动Root及骨盆已经组合在此，只从这里构建一次最终脊柱。
        for (int32 Index=SpineChain.Num()-1;Index>=0;--Index) // CacheBones按Chest向上缓存；此处反序使父骨先于子骨。
        {
            const FCompactPoseBoneIndex Bone=SpineChain[Index]; // 当前LOD紧凑索引；GetRefPoseTransform处理LOD和RefPoseOverride。
            FQuat Neutral= Bones.GetRefPoseTransform(Bone).GetRotation(); // 旧资产明确保留Skeleton参考，不自动切换新策略。
            if (bUseWeaponIdleSpineReference)
            {
                const FName BoneName=Bones.GetReferenceSkeleton().GetBoneName(Bones.MakeMeshPoseIndex(Bone).GetInt()); // 上方已整体校验此LOD链。
                Neutral=SpineReferenceRotations.FindChecked(BoneName); // GT快照的父骨局部旋转；不读取动画资产或叠加组件旋转。
            }
            const FQuat Delta=(Output.Pose[Bone].GetRotation()*Neutral.Inverse()).GetNormalized(); // UE5.4顺序：Upper * Neutral^-1；Idle时动态差量为单位。
            FTransform Mixed=BaseOutput.Pose[Bone]; // 平移与缩放完整保留移动基底，不把动画位移/伸长混入普通脊柱。
            Mixed.SetRotation((Delta*Mixed.GetRotation()).GetNormalized()); // UE5.4 AccumulateWithAdditiveScale同序：Delta * Base，不是Base * Delta。
            Output.Pose[Bone]=Mixed;
            FinalChest=Mixed*FinalChest; // UE Transform是Local * Parent；Lyra横移Root旋转及脊柱反向补偿不再分离。
            FinalChest.NormalizeRotation();
        }
        Mapped=UpperHand.GetRelativeTransform(UpperChest)*FinalChest; // 独立IK支系随最终Chest；不再额外乘Upper/参考骨盆或Root。
        if (bUseWeaponIdleSpineReference && !ViewChain.IsEmpty())
        {
            // 全身源的颈/头曾用于抵消原弯背；移除静态弯背后继续照搬会平视时仰头约18度。
            // 武器层只拥有胸肩/双臂动作，注视仍由Base和后续原有View Aim拥有。保留颈部Base locals，
            // 头部只抵消新胸框带来的朝向改变；位置沿真实颈链继承，T/S不改，不增加手臂IK或相机偏移。
            FTransform FinalViewParent=FinalChest; // 本次任务值，父先子后重建真实颈链。
            for (int32 Index=ViewChain.Num()-1;Index>=0;--Index)
            {
                const FCompactPoseBoneIndex Bone=ViewChain[Index]; // head为ViewChain[0]，其余均为真实颈部祖先。
                FTransform ViewLocal=BaseOutput.Pose[Bone]; // 颈/头局部平移缩放保留Base，不拉长颈部。
                if (Index==0)
                    ViewLocal.SetRotation((FinalViewParent.GetRotation().Inverse()*BaseCS.GetComponentSpaceTransform(Bone).GetRotation()).GetNormalized());
                Output.Pose[Bone]=ViewLocal;
                FinalViewParent=ViewLocal*FinalViewParent; FinalViewParent.NormalizeRotation();
            }
        }
    }
    else
    {
        const FCompactPoseBoneIndex Anchor=bUseChest?Chest.GetCompactPoseIndex(Bones):Hip; // 原策略：保留移动脊柱时按胸部映射，否则按骨盆。
        Mapped=UpperCS.GetComponentSpaceTransform(Hand).GetRelativeTransform(UpperCS.GetComponentSpaceTransform(Anchor))*BaseCS.GetComponentSpaceTransform(Anchor);
        if (bUseChest) for (const FCompactPoseBoneIndex Bone:SpineChain) Output.Pose[Bone]=BaseOutput.Pose[Bone]; // 旧DA及所有枪械公式/顺序保持不变。
    }
    // 分层混合保留Base的root。Lyra OrientationWarping会旋转该root；若以Upper的静止root转局部，
    // 下游混合会再次继承Base root，枪发生二次旋转而双手只旋转一次。以最终保留的父骨转局部。
    Output.Pose[Hand]=Parent!=INDEX_NONE?Mapped.GetRelativeTransform(BaseCS.GetComponentSpaceTransform(Parent)):Mapped;
    Output.Pose[Hand].NormalizeRotation();
#if WITH_EDITOR
    if (bUsesDemoProxy)
    {
        FDemoIdleBreakPoseAudit& Audit=static_cast<FDemoPlayerAnimProxy*>(Output.AnimInstanceProxy)->IdleAudit; // 已由PreUpdate确认类型，任务独占写，PostUpdate读之前引擎等待任务结束。
        if (Audit.bEnabled)
        {
            if (Audit.bFirePoseEnabled)
            {
                Audit.FirePoseNames.Reset(); Audit.FireBase.Reset(); Audit.FireUpper.Reset();
                for (const FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
                {
                    Audit.FirePoseNames.Add(Bones.GetReferenceSkeleton().GetBoneName(Bones.MakeMeshPoseIndex(Index).GetInt()));
                    Audit.FireBase.Add(BaseCS.GetComponentSpaceTransform(Index));
                    Audit.FireUpper.Add(UpperCS.GetComponentSpaceTransform(Index));
                }
                ++Audit.FireAnchorCount; // 只读冻结的两组输入，不读取正在改写的Output来冒充源姿态。
            }
            Audit.PostureNames.Reset(); Audit.NativeBasePosture.Reset(); Audit.UpperPosture.Reset(); // 只读同帧输入，不将混合后的值当作Base证据。
            const FName PostureNames[]={TEXT("root"),TEXT("pelvis"),TEXT("spine_01"),TEXT("spine_02"),TEXT("spine_03"),TEXT("spine_04"),TEXT("spine_05")};
            for (const FName Name:PostureNames)
            {
                const int32 MeshIndex=Bones.GetReferenceSkeleton().FindBoneIndex(Name);
                const FCompactPoseBoneIndex Index=MeshIndex!=INDEX_NONE?Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)):FCompactPoseBoneIndex(INDEX_NONE);
                if (Index!=INDEX_NONE)
                {
                    Audit.PostureNames.Add(Name); Audit.NativeBasePosture.Add(BaseCS.GetComponentSpaceTransform(Index));
                    Audit.UpperPosture.Add(UpperCS.GetComponentSpaceTransform(Index));
                }
            }
            Audit.ViewPostureNames.Reset(); Audit.NativeBaseViewPosture.Reset(); Audit.UpperViewPosture.Reset(); // 独立注视输入证据，绝不反写姿态。
            for (const FCompactPoseBoneIndex Index:ViewChain)
            {
                Audit.ViewPostureNames.Add(Bones.GetReferenceSkeleton().GetBoneName(Bones.MakeMeshPoseIndex(Index).GetInt()));
                Audit.NativeBaseViewPosture.Add(BaseCS.GetComponentSpaceTransform(Index));
                Audit.UpperViewPosture.Add(UpperCS.GetComponentSpaceTransform(Index));
            }
            const FName Names[4]={TEXT("spine_05"),TEXT("hand_r"),TEXT("hand_l"),TEXT("ik_hand_gun")}; // 真实骨名，按当前LOD重建Compact索引。
            bool bValid=true; // 缺骨不发布计数，测试不能把默认恒等矩阵当真实样本。
            for (int32 I=0;I<4;++I)
            {
                const int32 MeshIndex=Bones.GetReferenceSkeleton().FindBoneIndex(Names[I]); // 当前网格骨索引，不能当作Compact索引。
                const FCompactPoseBoneIndex Index=MeshIndex!=INDEX_NONE?Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)):FCompactPoseBoneIndex(INDEX_NONE);
                if (Index==INDEX_NONE) { bValid=false; break; }
                Audit.NativeBase[I]=BaseCS.GetComponentSpaceTransform(Index); Audit.UpperAction[I]=UpperCS.GetComponentSpaceTransform(Index);
                if (I==3) Audit.MappedGun=UpperCS.GetComponentSpaceTransform(Index).GetRelativeTransform(UpperCS.GetComponentSpaceTransform(Hand))*Mapped;
            }
            if (bValid) ++Audit.AnchorCount;
        }
    }
#endif
}

void FAnimNode_DemoWeaponAnchor::GatherDebugData(FNodeDebugData& DebugData)
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); DebugData.AddDebugItem(DebugData.GetNodeName(this)); Base.GatherDebugData(DebugData); Upper.GatherDebugData(DebugData); }
