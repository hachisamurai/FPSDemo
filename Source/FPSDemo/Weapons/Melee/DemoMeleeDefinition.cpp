// 职责：近战配置、连招、通知窗口及Idle脊柱数据校验。装备前/导入后Validate；起刀解析作者窗口，不在工作线程取样动画资产。
#include "Weapons/Melee/DemoMeleeDefinition.h"
#include "Weapons/Melee/DemoMeleeTraceWindow.h" // 显式通知配置是窗口唯一源，不用客户端事件授权。
#include "Animation/AnimSequence.h"
#include "Animation/AimOffsetBlendSpace1D.h" // 标准MeshSpace additive资产校验，不能把原生delta直接当完整姿态。
#include "Animation/Skeleton.h" // 骨挂点不存在时拒绝装备，不能静默落在模型原点。
#include "Engine/SkeletalMesh.h" // 独立近战手臂须与其FP动画使用相同Skeleton。
#include "Debug/DemoLog.h"

float FDemoMeleeAttack::SampleProgress(float Elapsed) const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] %hs"),__FUNCTION__); // USTRUCT不是UObject，不能使用需要GetNameSafe(this)的宏；同样仅限时逐帧追踪时输出。
    const float Time=FMath::Clamp(Elapsed/FMath::Max(Duration,.001f),0.f,1.f); // 归一化运行秒数；有效配置Duration>0，兜底避免除0。
    if (PlaybackKeys.IsEmpty()) return Time;
    for (int32 Index=1;Index<PlaybackKeys.Num();++Index) // 校验后的有界单调关键点，最多16项。
        if (Time<=PlaybackKeys[Index].X)
        {
            const FVector2D& A=PlaybackKeys[Index-1]; // 本段开始，配置由DA保活，当前栈只读。
            const FVector2D& B=PlaybackKeys[Index]; // 本段结束，X严格递增。
            return FMath::Lerp(A.Y,B.Y,FMath::Clamp((Time-A.X)/(B.X-A.X),0.,1.));
        }
    return 1.f;
}

bool FDemoMeleeAttack::ResolveHitWindow(FVector2D& OutWindow,FString& Error) const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] %hs"),__FUNCTION__); // USTRUCT无UObject this，保留低频可追踪入口。
    if (bUseAnimationNotifyHitWindow) return UDemoMeleeTraceWindow::ReadWindow(ThirdPerson,OutWindow,Error);
    OutWindow=HitWindow; Error.Reset(); // 旧资产继续使用原窗口，不修改/重存旧资源。
    return true;
}

float FDemoMeleeAttack::RuntimeAtPhase(float Phase) const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] %hs"),__FUNCTION__); // USTRUCT不使用UObject日志宏；逆映射不默认逐帧输出。
    const float Author=FMath::Clamp(Phase,0.f,1.f); // 作者比例，无单位；资源校验保证表严格单调。
    if (PlaybackKeys.IsEmpty()) return Author*Duration;
    for (int32 Index=1;Index<PlaybackKeys.Num();++Index)
        if (Author<=PlaybackKeys[Index].Y)
        {
            const FVector2D& A=PlaybackKeys[Index-1]; // 生命周期为本次只读配置访问。
            const FVector2D& B=PlaybackKeys[Index];
            return Duration*FMath::Lerp(A.X,B.X,FMath::Clamp((Author-A.Y)/(B.Y-A.Y),0.,1.));
        }
    return Duration;
}

bool UDemoMeleeDefinition::CopySpineReference(TMap<FName,FQuat>& OutRotations,FString& Error) const
{
    DEMO_LOG_TICK(); // 装备期间每帧PreUpdate可能调用，只保留限时逐帧埋点，不能默认刷业务日志。
    OutRotations.Reset(); Error.Reset(); // 失败不发布部分中立数据，避免同一胸链混用Idle与A-Pose参考。
    if (!bUseAuthoredSpine || !bUseUpperBodyMontage || !ThirdPersonIdle || SpineReferenceIdle!=ThirdPersonIdle || !SpineReferenceIdle->GetSkeleton()
        || !FMath::IsFinite(SpineReferenceTime) || SpineReferenceTime<0.f || SpineReferenceTime>SpineReferenceIdle->GetPlayLength()
        || SpineReferenceRevision.IsEmpty())
    { Error=TEXT("Idle spine reference requires authored upper-body Montage, current TP Idle identity, finite sample time and revision"); UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_SPINE_REFERENCE_REJECT %s"),*Error); return false; }
    const FReferenceSkeleton& Reference=SpineReferenceIdle->GetSkeleton()->GetReferenceSkeleton(); // 本次游戏线程借用，不跨动画任务持有。
    const int32 PelvisIndex=Reference.FindBoneIndex(TEXT("pelvis")),ChestIndex=Reference.FindBoneIndex(TEXT("spine_05")); // 当前项目实际Manny链端点。
    TSet<FName> Required; // 骨名集合，而不是Skeleton/Mesh/Compact索引混用。
    int32 Ancestor=ChestIndex; // 从真实胸部父链走到骨盆，拒绝缺骨或不相连的参考。
    while (Ancestor!=INDEX_NONE && Ancestor!=PelvisIndex)
    { Required.Add(Reference.GetBoneName(Ancestor)); Ancestor=Reference.GetParentIndex(Ancestor); }
    if (PelvisIndex==INDEX_NONE || ChestIndex==INDEX_NONE || Ancestor!=PelvisIndex || Required.IsEmpty() || SpineReferenceBones.Num()!=Required.Num())
    { Error=TEXT("Idle spine reference must cover the exact pelvis-to-spine_05 child chain"); UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_SPINE_REFERENCE_REJECT %s"),*Error); return false; }
    for (const FDemoMeleeSpineReferenceBone& Bone:SpineReferenceBones) // 配置五骨小表，按名字复制；任何坏值使整个策略失效。
    {
        if (!Required.Contains(Bone.BoneName) || OutRotations.Contains(Bone.BoneName) || Bone.LocalRotation.ContainsNaN() || !Bone.LocalRotation.IsNormalized())
        { OutRotations.Reset(); Error=TEXT("Idle spine reference contains missing/duplicate bone or invalid local quaternion"); UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_SPINE_REFERENCE_REJECT %s bone=%s"),*Error,*Bone.BoneName.ToString()); return false; }
        OutRotations.Add(Bone.BoneName,Bone.LocalRotation);
    }
    return true;
}

bool UDemoMeleeDefinition::Validate(FString& Error) const
{
    DEMO_LOG_CALL();
    if (WeaponId.IsNone() || !Mesh || !ThirdPersonIdle || !FirstPersonIdle || !FirstPersonAnimClass || HandSocket.IsNone()
        || Attacks.Num()<1 || Attacks.Num()>16 || !EnemyImpactSound || !WorldImpactSound || GripOffset.ContainsNaN() // 单主攻击合法；右键副攻击是可选配置，不能强迫保留已撤销的下劈。
        || TraceBase.ContainsNaN() || TraceTip.ContainsNaN() || !FMath::IsFinite(TraceRadius) || TraceRadius<1.f || TraceRadius>10.f
        || !FMath::IsFinite(MaxReach) || MaxReach<80.f || MaxReach>250.f || FVector::Distance(TraceBase,TraceTip)<10.f
        || AimPivot.ContainsNaN() || !FMath::IsFinite(AimPitchLimit) || AimPitchLimit<0.f || AimPitchLimit>30.f
        || !FMath::IsFinite(FullBodyFadeSpeed) || FullBodyFadeSpeed<10.f || FullBodyFadeSpeed>200.f
        || !FMath::IsFinite(FullBodyBlendSeconds) || FullBodyBlendSeconds<.03f || FullBodyBlendSeconds>.4f // 即使层关闭也拒绝坏配置，避免之后启用时传播NaN。
        || !FMath::IsFinite(InputBufferSeconds) || InputBufferSeconds<0.f || InputBufferSeconds>.3f // 输入缓冲不能成为永久挂起的攻击命令。
        || FirstPersonOffset.ContainsNaN() || !FMath::IsFinite(FirstPersonBlendSeconds) || FirstPersonBlendSeconds<.03f || FirstPersonBlendSeconds>.3f
        || !ThirdPersonIdle->GetSkeleton() || ThirdPersonIdle->GetPlayLength()<=0.f
        || FirstPersonIdle->GetSkeleton()!=ThirdPersonIdle->GetSkeleton() || FirstPersonIdle->GetPlayLength()<=0.f)
    { Error=TEXT("Missing identity/visual/animation/audio or invalid collision geometry"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    if (!FMath::IsFinite(FirstPersonLookBlendSeconds) || FirstPersonLookBlendSeconds<.03f || FirstPersonLookBlendSeconds>.3f
        || (FirstPersonLookOffset && (FirstPersonLookOffset->GetSkeleton()!=FirstPersonIdle->GetSkeleton()
            || FirstPersonLookOffset->GetBlendSamples().Num()<3
            || !FMath::IsFinite(FirstPersonLookOffset->GetBlendParameter(0).Min) || !FMath::IsFinite(FirstPersonLookOffset->GetBlendParameter(0).Max)
            || FirstPersonLookOffset->GetBlendParameter(0).Min>=0.f || FirstPersonLookOffset->GetBlendParameter(0).Max<=0.f)))
    { Error=TEXT("First-person look offset requires matching skeleton, three additive samples and finite negative/positive pitch range"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    if (!FMath::IsFinite(MeleeRecoveryBlendSeconds) || MeleeRecoveryBlendSeconds<.08f || MeleeRecoveryBlendSeconds>.4f
        || (bUseDistributedThirdPersonAim && !bAlignTraceToEvaluatedGrip))
    { Error=TEXT("Recovery must be 0.08..0.4 seconds; distributed melee Aim requires evaluated-grip authority"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    if (FirstPersonLookOffset)
        for (const FBlendSample& Sample:FirstPersonLookOffset->GetBlendSamples()) // 新资产逐样本核对加成类型，旧空配置不增加取样或动画开销。
            if (!Sample.Animation || Sample.Animation->GetSkeleton()!=FirstPersonIdle->GetSkeleton() || Sample.Animation->AdditiveAnimType!=AAT_RotationOffsetMeshSpace)
            { Error=TEXT("First-person look sample must be MeshSpace additive on current skeleton"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    if (FirstPersonArmsMesh && FirstPersonArmsMesh->GetSkeleton()!=FirstPersonIdle->GetSkeleton())
    { Error=TEXT("Dedicated melee arms mesh and FP animation skeleton mismatch"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    if (bIndependentFirstPersonDisplay && (!FirstPersonArmsMesh || !OwnerBodyWithoutArmsMesh || OwnerBodyWithoutArmsMesh->GetSkeleton()!=ThirdPersonIdle->GetSkeleton()))
    { Error=TEXT("Independent melee display requires compatible arms and owner body partition"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    if (bAlignTraceToEvaluatedGrip && !bUseUpperBodyMontage)
    { Error=TEXT("Evaluated grip trace requires the configured upper-body Montage strategy"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    if (bUseFullContextForComboContinuation && !bUseUpperBodyMontage)
    { Error=TEXT("Full-context combo continuation requires the explicit TP upper-body Montage source-seconds path"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; } // 旧Evaluator按phase重归一化，不给该未适配路径静默开放新策略。
    if (bUseWeaponIdleSpineReference)
    {
        TMap<FName,FQuat> Neutral; // 本次校验临时纯值；默认关闭的旧DA不增加参考数据要求。
        if (!CopySpineReference(Neutral,Error))
        { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT idle_spine_reference %s"),*Error); return false; }
    }
    // 两套表现必须使用当前玩家原骨架；FP错配时明确拒绝，不能静默显示TP高位动作挡住镜头。
    if (ThirdPersonIdle->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(HandSocket)==INDEX_NONE && !ThirdPersonIdle->GetSkeleton()->FindSocket(HandSocket))
    { Error=TEXT("Hand socket/bone absent from animation skeleton"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    TSet<FName> Seen; // 本次验证的稳定键集合，重复招式会导致调试身份不明确。
    for (const FDemoMeleeAttack& Attack : Attacks) // 只读资产招式，不缓存指向可编辑数组的指针。
    {
        FVector2D DamageWindow; // 启用通知时校验实际窗口，缺失不得降级旧HitWindow。
        if (!Attack.ResolveHitWindow(DamageWindow,Error))
        { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT attack=%s window=%s"),*Attack.Id.ToString(),*Error); return false; }
        if (Attack.Id.IsNone() || Seen.Contains(Attack.Id) || !Attack.ThirdPerson || !Attack.FirstPerson || !Attack.SwingSound
            || Attack.ThirdPerson->GetSkeleton()!=ThirdPersonIdle->GetSkeleton()
            || Attack.ThirdPerson->GetPlayLength()<=0.f || Attack.FirstPerson->GetPlayLength()<=0.f
            || Attack.FirstPerson->GetSkeleton()!=FirstPersonIdle->GetSkeleton()
            || !FMath::IsFinite(Attack.Duration) || Attack.Duration<.2f || Attack.Duration>5.f || !FMath::IsFinite(Attack.Damage) || Attack.Damage<0.f || Attack.Damage>10000.f
            || !FMath::IsFinite(DamageWindow.X) || !FMath::IsFinite(DamageWindow.Y) || DamageWindow.X<0.f || DamageWindow.X>=DamageWindow.Y || DamageWindow.Y>1.f
            || !FMath::IsFinite(Attack.SwingSoundPhase) || Attack.SwingSoundPhase<0.f || Attack.SwingSoundPhase>1.f || Attack.MaxTargets<1 || Attack.MaxTargets>16
            // 不信任仅编辑器Clamp；运行时拒绝非有限/越界击退，避免错误资产产生超远或无限运动。
            || !FMath::IsFinite(Attack.KnockbackDistance) || Attack.KnockbackDistance<0.f || Attack.KnockbackDistance>600.f
            || !FMath::IsFinite(Attack.KnockbackDuration) || Attack.KnockbackDuration<.05f || Attack.KnockbackDuration>2.f
            || Attack.TracePoses.Num()<2 || Attack.TracePoses.Num()>601)
        { Error=FString::Printf(TEXT("Invalid attack %s: resource/skeleton/window/value"),*Attack.Id.ToString()); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
        Seen.Add(Attack.Id);
        if (bAlignTraceToEvaluatedGrip)
        {
            if (Attack.WeaponFrames.Num()!=Attack.TracePoses.Num())
            { Error=TEXT("Evaluated grip trace requires a weapon frame per trace sample; rebake the definition"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
            for (const FTransform& Frame:Attack.WeaponFrames)
                if (Frame.ContainsNaN() || !Frame.GetRotation().IsNormalized() || Frame.GetScale3D().GetMin()<=0.f || Frame.GetLocation().Size()>MaxReach)
                { Error=TEXT("Invalid baked weapon frame"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
        }
        for (const FDemoMeleeTracePose& Pose : Attack.TracePoses) // 拒绝NaN与超远作者数据，不能把资产误配置变为远程伤害。
            if (Pose.Base.ContainsNaN() || Pose.Tip.ContainsNaN() || Pose.Base.Size()>MaxReach || Pose.Tip.Size()>MaxReach)
            { Error=TEXT("Invalid baked trace position"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    }
    for (const FDemoMeleeAttack& Attack : Attacks) // 运行时间与作者相位的映射必须双向单调，不允许倒放重复扫掠伤害窗口。
    {
        if (Attack.PlaybackKeys.IsEmpty()) continue;
        bool bValid=Attack.PlaybackKeys.Num()>=2 && Attack.PlaybackKeys.Num()<=16; // 有界表，首尾必须覆盖完整动作。
        for (int32 Index=0;bValid && Index<Attack.PlaybackKeys.Num();++Index)
        {
            const FVector2D& Key=Attack.PlaybackKeys[Index]; // 比例，无单位，禁止NaN和越界。
            bValid=FMath::IsFinite(Key.X) && FMath::IsFinite(Key.Y) && Key.X>=0. && Key.X<=1. && Key.Y>=0. && Key.Y<=1.
                && (Index==0 || (Key.X>Attack.PlaybackKeys[Index-1].X && Key.Y>Attack.PlaybackKeys[Index-1].Y));
        }
        if (!bValid || !Attack.PlaybackKeys[0].Equals(FVector2D::ZeroVector,.00001) || !Attack.PlaybackKeys.Last().Equals(FVector2D(1,1),.00001))
        { Error=TEXT("Playback keys require 2..16 strictly increasing pairs from (0,0) to (1,1)"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    }
    if (HorizontalAttackSequence.Num()>8)
    { Error=TEXT("Horizontal sequence exceeds eight entries"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    for (FName AttackId : HorizontalAttackSequence) // 循环只引用本资产已有ID；重排Attacks不靠裸下标猜动作身份。
        // 旧资产默认保留副攻槽隔离；显式许可只放开第1项的循环资格，不改变右键直选或服务器成功起招后推进的规则。
        if (AttackId.IsNone() || !Seen.Contains(AttackId) || (!bAllowSecondaryInHorizontalSequence && Attacks.IsValidIndex(1) && AttackId==Attacks[1].Id)) // 单招式先检查下标，缺省许可不能变相开放旧资产。
        { Error=TEXT("Horizontal sequence references missing attack or secondary attack without explicit opt-in"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
    for (const FDemoMeleeAttack& Attack:Attacks) // 可选连招只验证有界纯配置；完整前缀/握持由同版作者资产审查，不在运行时逐骨重采。
    {
        if (Attack.ComboTargetId.IsNone())
        {
            if (Attack.ComboFirstPerson || Attack.ComboThirdPerson || Attack.ComboBranchPhase!=0.f || Attack.ComboEndPhase!=0.f || Attack.ComboEntryPhase!=0.f)
            { Error=TEXT("Disabled combo must not retain partial transition data"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
            continue;
        }
        const FDemoMeleeAttack* Target=nullptr; // 同步有界查找，保留数组稳定引用，不建立回调生命周期。
        for (const FDemoMeleeAttack& Candidate:Attacks) if (Candidate.Id==Attack.ComboTargetId) { Target=&Candidate; break; }
        FVector2D DamageWindow,TargetWindow; // 分支/入口也校验同一实际窗口，避免作者通知与连招时钟分离。
        const bool bWindows=Attack.ResolveHitWindow(DamageWindow,Error) && Target && Target->ResolveHitWindow(TargetWindow,Error);
        const bool bPhases=FMath::IsFinite(Attack.ComboBranchPhase) && FMath::IsFinite(Attack.ComboEndPhase) && FMath::IsFinite(Attack.ComboEntryPhase)
            && bWindows && Attack.ComboBranchPhase>=DamageWindow.Y && Attack.ComboBranchPhase<Attack.ComboEndPhase && Attack.ComboEndPhase<=1.f
            && Attack.ComboEntryPhase>=0.f && Attack.ComboEntryPhase<1.f;
        if (!Target || Target==&Attack || !HorizontalAttackSequence.Contains(Attack.Id) || !HorizontalAttackSequence.Contains(Attack.ComboTargetId)
            || !bPhases || !Attack.ComboFirstPerson || !Attack.ComboThirdPerson
            || Attack.ComboFirstPerson->GetSkeleton()!=FirstPersonIdle->GetSkeleton() || Attack.ComboThirdPerson->GetSkeleton()!=ThirdPersonIdle->GetSkeleton()
            || FMath::Abs(Attack.FirstPerson->GetPlayLength()-Attack.ThirdPerson->GetPlayLength())>.001f
            || Attack.ComboFirstPerson->GetPlayLength()+.002f<Attack.FirstPerson->GetPlayLength()*Attack.ComboEndPhase
            || Attack.ComboThirdPerson->GetPlayLength()+.002f<Attack.ThirdPerson->GetPlayLength()*Attack.ComboEndPhase
            || FMath::Abs(Attack.ComboFirstPerson->GetPlayLength()-Attack.ComboThirdPerson->GetPlayLength())>.001f // 可复用包含真实后续动作的完整Combo源；只消费到指定出口，不能用资源总长改写时钟。
            || Attack.ComboEntryPhase>TargetWindow.X+KINDA_SMALL_NUMBER)
        { Error=TEXT("Invalid combo target/source/branch/end/entry contract"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s"),*Error); return false; }
        if (bUseFullContextForComboContinuation)
        {
            // 每视图用自己的标准源秒数计算完整覆盖；只校验作者上下文合同，不延长本刀GA/伤害或自动创建目标动作。
            const float RequiredFPSeconds=Attack.FirstPerson->GetPlayLength()*Attack.ComboEndPhase+Target->FirstPerson->GetPlayLength()*(1.f-Attack.ComboEntryPhase);
            const float RequiredTPSeconds=Attack.ThirdPerson->GetPlayLength()*Attack.ComboEndPhase+Target->ThirdPerson->GetPlayLength()*(1.f-Attack.ComboEntryPhase);
            if (Attack.ComboFirstPerson->GetPlayLength()+.002f<RequiredFPSeconds || Attack.ComboThirdPerson->GetPlayLength()+.002f<RequiredTPSeconds)
            { Error=TEXT("Full-context continuation requires FP/TP source through the target's complete recovery"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CONFIG_REJECT %s attack=%s required_fp_seconds=%.6f required_tp_seconds=%.6f"),*Error,*Attack.Id.ToString(),RequiredFPSeconds,RequiredTPSeconds); return false; } // .002秒沿用原UE源长度读回容差，不缩短作者阶段或放宽骨姿精度门；目标有后继不要求先收刀。
        }
    }
    Error.Reset(); return true;
}
