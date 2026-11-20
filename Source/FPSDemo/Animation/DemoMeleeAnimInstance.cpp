// 职责：游戏线程发布FP近战姿态/Idle与恢复配置；旧图双路混合，新惯性图独占Idle边，已审连续连招仍直接取样。无技能或伤害回调。
#include "Animation/DemoMeleeAnimInstance.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Characters/DemoCharacter.h" // 仅GT读取当前Avatar真实视线；AnyThread节点只消费下方纯值。
#include "Animation/DemoPlayerEyesComponent.h" // 复用本Pawn输入方向；观战/审片Camera不能反向驱动持握。
#include "GameFramework/Actor.h"
#include "Debug/DemoLog.h"
void UDemoMeleeAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::NativeUpdateAnimation(DeltaSeconds);
    const UDemoMeleeComponent* Component=GetOwningActor()?GetOwningActor()->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 游戏线程借用，不放入AnyThread节点。
    float NextTime=0.f; // 新资源归一化时钟对应的源秒数，仅缓存表现。
    UAnimSequence* Next=Component?Component->GetPose(true,NextTime,Progress,Action):nullptr; // 本地拥有者可能预测；资源独立，但同一作者相位映射。
    const uint32 Serial=Component?Component->GetAttackSerial():0; // 同源动画重播也必须平滑衔接，不依赖资源指针变化。
    const UDemoMeleeDefinition* Definition=Component?Component->GetDefinition():nullptr; // 组件保活配置，只在GT借用，不加载或写资源。
    const ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwningActor()); // 只发布本Avatar状态，不借用观战ViewTarget。
    const bool bNextIdle=Next && Definition && Character && !Character->IsCoopEliminated() && Next==Definition->FirstPersonIdle && Action==TEXT("MeleeHold"); // 取消后允许再次合法Hold，不能用历史取消Serial永久禁Idle。
    const bool bIdleBoundary=bIdlePresentation!=bNextIdle; // 新图只在Idle/Attack分支切换时请求一次引擎惯性处理。
    const bool bTransition=Next!=Pose || Serial!=PreviousSerial; // 同资源新刀也有自己的过渡身份。
    const float Duration=Definition?Definition->FirstPersonBlendSeconds:.1f; // 资产校验过的有限过渡秒数。
    const float Step=FMath::IsFinite(DeltaSeconds)?FMath::Max(0.f,DeltaSeconds):0.f; // 坏/负dt不污染表现权重。
    RecoveryBlendSeconds=Definition && FMath::IsFinite(Definition->MeleeRecoveryBlendSeconds)?FMath::Clamp(Definition->MeleeRecoveryBlendSeconds,.08f,.4f):.18f; // 与DA原生校验范围一致，热改坏值不能进入惯性节点。
    if (bTransition)
    {
        const bool bContinuous=Component && Component->IsContinuousPoseTransition(true,Pose.Get(),PreviousSerial); // 仅配套前缀或父Serial相邻Entry；取消/回Idle不得跳混合。
        const bool bInertialBoundary=bUseRecoveryInertialGraph && bIdleBoundary; // 新图的Idle边只有一套引擎混合，禁止旧双路混合重复叠权。
        const bool bDirect=bContinuous || bInertialBoundary;
        PreviousPose=bDirect?Next:(Pose?Pose.Get():Next);
        PreviousTime=bDirect?NextTime:(Pose?PoseTime:NextTime);
        BlendElapsed=bDirect || !Pose?Duration:0.f; // 普通旧图首帧从0开始，不预吃本帧dt；连续源/新惯性边由各自唯一策略处理。
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_FP_TRANSITION from=%s to=%s duration=%.3f continuous=%d inertial_boundary=%d idle=%d"),*GetNameSafe(Pose),*GetNameSafe(Next),bDirect?0.f:Duration,bContinuous,bInertialBoundary,bNextIdle);
    }
    else BlendElapsed=FMath::Min(Duration,BlendElapsed+Step); // 新转场首帧保留完整起点；下一实际求值帧才推进，低帧不在发现边沿时先跳过大半段。
    BlendAlpha=FMath::SmoothStep(0.f,1.f,BlendElapsed/Duration); Pose=Next; PoseTime=NextTime;
    bIdlePresentation=bNextIdle;
    PreviousSerial=Serial; // 确认不换序号，所以不会因RPC往返重新混合/退回蓄势。
    if (!Pose) { PoseTime=0.f; Progress=0.f; Action=NAME_None; }
    const UAimOffsetBlendSpace1D* PreviousLook=LookOffset.Get(); // 只做本帧资源边沿比较，不保活或跨线程捕获。
    LookOffset=Definition?Definition->FirstPersonLookOffset.Get():nullptr;
    const bool bLookIdle=LookOffset && bIdlePresentation;
    const float LookSeconds=Definition && FMath::IsFinite(Definition->FirstPersonLookBlendSeconds)?FMath::Clamp(Definition->FirstPersonLookBlendSeconds,.03f,.3f):.1f; // 已装备预检，仍限有界正数防热改异常。
    LookWeight=bUseRecoveryInertialGraph?(bLookIdle?1.f:0.f):LookOffset?FMath::FInterpConstantTo(LookWeight,bLookIdle?1.f:0.f,Step,1.f/LookSeconds):0.f; // 新图Idle分支包含完整AO，攻击分支无AO；惯性化完整输出，不再独立线性重开持握层。
    LookPitch=LookOffset && Character?FMath::Clamp(UDemoPlayerEyesComponent::ReadAimDirection(Character).Rotation().Pitch,LookOffset->GetBlendParameter(0).Min,LookOffset->GetBlendParameter(0).Max):0.f;
    if (bWasLookIdle!=bLookIdle || PreviousLook!=LookOffset.Get())
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_FP_LOOK_STATE idle=%d offset=%s blend_seconds=%.3f"),bLookIdle,*GetNameSafe(LookOffset),LookSeconds); // 仅状态边沿，不能每帧刷屏。
    bWasLookIdle=bLookIdle;
}
