// 职责：玩家方向请求、Lyra式RootMotionSource闪避与GAS躲避窗口；权威速度/冷却不接受客户端修改。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Characters/DemoCharacter.h"
#include "Characters/DemoDashSettings.h"
#include "VFX/DemoDashTrailComponent.h"
#include "GAS/DemoTags.h"
#include "GAS/Gambling/DemoCombatTalentComponent.h" // 成功提交后登记一次冲刺补弹凭据。
#include "Progression/Builds/DemoRunBuildComponent.h" // 仅Combat核心记录真实冲刺位移，Hub动作不能带入充能。
#include "Debug/DemoLog.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h" // Lyra的ApplyRootMotionConstantForce使用同一引擎源；这里保留既有立即结束GA与天赋到期契约。
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h" // 跨端表现时钟，不使用本机墙钟推断技能起点。
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoEffects.h"
#include "GAS/Abilities/DemoPlayerAbilityTypes.h"
#include "InputAction.h"
#include "Templates/UnrealTemplate.h" // UE5.4的TGuardValue定义，限定方向意图只在同步GAS激活调用栈存活。

void ADemoCharacter::DashPressed()
{
    DEMO_LOG_CALL();
    RequestDash();
}
FVector ADemoCharacter::ResolveDashDirection() const
{
    DEMO_LOG_CALL();
    const APlayerController* PC=Cast<APlayerController>(Controller); // 本函数同步借用；服务器远端PC没有本机PlayerInput。
    const UEnhancedPlayerInput* Input=PC?Cast<UEnhancedPlayerInput>(PC->PlayerInput):nullptr; // UE先求Enhanced Action再执行旧Key委托，因此同帧Shift可读到起步/松键/反向。
    FVector Direction=FVector::ZeroVector; // 世界空间单位方向；始终丢弃俯仰，不从惯性速度推断按键。
    const FRotator View=GetMovementViewRotation(); // 相机水平坐标系，头身分离时与MoveDemo保持一致；空输入也沿视线冲刺。
    if (IsLocallyControlled() && Input && MoveInputAction)
    {
        const FVector2D Axis=Input->GetActionValue(MoveInputAction).Get<FVector2D>(); // 使用实际映射后的当前轴，支持手柄和重映射，零值必须立即回前向而非复用旧输入。
        Direction=View.Vector()*Axis.Y+View.RotateVector(FVector::RightVector)*Axis.X; // 仍以当帧按键快照决定方向，不读取惯性速度。
    }
    else
    {
        // 无按键意图的直接服务器GA调用保留兼容；ServerMove传的是Acceleration，并不会消费APawn的LastControlInputVector。
        Direction=GetCharacterMovement()->GetCurrentAcceleration();
        if (IsLocallyControlled() && !GetPendingMovementInputVector().IsNearlyZero()) Direction=GetPendingMovementInputVector(); // 无Enhanced输入的程序化本机移动优先尚未消费的输入。
    }
    Direction=Direction.GetSafeNormal2D();
    return Direction.IsNearlyZero()?View.Vector():Direction; // 无键向相机正前方，不向停留在阈值另一侧的身体前方。
}
void ADemoCharacter::RequestDash()
{
    DEMO_LOG_CALL();
    if (!IsLocallyControlled() || !BoundASC.IsValid() || !CanUsePlayerSkills())
    { UE_LOG(LogFPSDemo,Log,TEXT("DASH_REQUEST_REJECTED pawn=%s local=%d ASC=%d phase/menu/life"),*GetName(),IsLocallyControlled(),BoundASC.IsValid()); return; }
    const FVector Direction=ResolveDashDirection(); // 按下瞬间冻结，后续移动包或转向不能改变本次技能意图。
    UE_LOG(LogFPSDemo,Display,TEXT("DASH_REQUEST pawn=%s authority=%d direction=%s lastInput=%s acceleration=%s"),*GetName(),HasAuthority(),*Direction.ToString(),*GetLastMovementInputVector().ToString(),*GetCharacterMovement()->GetCurrentAcceleration().ToString());
    if (HasAuthority()) ActivateRequestedDash(Direction);
    else ServerRequestDash(Direction); // 方向与激活在同一个拥有者RPC内，避免PS的GAS激活先于Character移动包到达。
}
void ADemoCharacter::ServerRequestDash_Implementation(FVector_NetQuantizeNormal Direction)
{
    DEMO_LOG_CALL();
    ActivateRequestedDash(Direction); // RPC拥有权由UE检查；速度、GE、升级和冷却仍完全由服务器读取/提交。
}
void ADemoCharacter::ActivateRequestedDash(const FVector& Direction)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !BoundASC.IsValid() || BoundASC->GetAvatarActor()!=this || !CanUsePlayerSkills())
    { UE_LOG(LogFPSDemo,Log,TEXT("DASH_AUTHORITY_REJECTED pawn=%s authority/ASC/avatar/skill gate"),*GetName()); return; }
    if (Direction.ContainsNaN() || FMath::Abs(Direction.Z)>.01 || Direction.IsNearlyZero())
    { UE_LOG(LogFPSDemo,Warning,TEXT("DASH_DIRECTION_REJECTED pawn=%s direction=%s"),*GetName(),*Direction.ToString()); return; }
    const TGuardValue<FVector> DirectionScope(ActiveDashDirection,Direction.GetSafeNormal2D()); // 同步GA只借用这一归一化方向；函数返回/拒绝后自动恢复旧值，不给包体新增逐帧复制。
    const TGuardValue<bool> IntentScope(bHasActiveDashDirection,true); // 与方向同寿命；冷却失败或嵌套回调退出也不污染后续激活。
    const bool Activated=BoundASC->ActivateDemoAbility(UDemoDashAbility::StaticClass()); // 原CanActivate/Commit负责4秒冷却及生命/阶段，不在RPC里直接Launch。
    UE_LOG(LogFPSDemo,Display,TEXT("DASH_AUTHORITY_RESULT pawn=%s activated=%d direction=%s"),*GetName(),Activated,*ActiveDashDirection.ToString());
}
void ADemoCharacter::PerformDash()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !BoundASC.IsValid() || !CanUsePlayerSkills())
    { UE_LOG(LogFPSDemo,Log,TEXT("Dash action rejected: authority/ASC/skill phase/menu")); return; }
    const FVector Direction=bHasActiveDashDirection?ActiveDashDirection:ResolveDashDirection(); // 正常按键优先当前请求，直接程序化GA沿服务器可用输入回退。
    const FVector DashVelocity=Direction*DashSpeed*(DemoGambleEffects::Has(BoundASC.Get(),DemoGamble::Relic(3))?1.f-GetDefault<UDemoGambleSettings>()->DashDistanceReduction:1.f); // 世界水平cm/s；归一化避免斜向加速，不额外制造跳跃。
    UCharacterMovementComponent* Movement=GetCharacterMovement(); // Avatar拥有；RootMotionSource由CMC管理和复制，不逐帧SetActorLocation。
    const float Configured=GetDefault<UDemoDashSettings>()->ForceSeconds; // 项目时长与技能成长独立，保护非法配置。
    const float Duration=FMath::IsFinite(Configured)?FMath::Clamp(Configured,.1f,.5f):.3f;
    DemoEffects::Apply(BoundASC.Get(),BoundASC.Get(),UDemoDashEvadeEffect::StaticClass(),0.f); // GA已经Commit；保留独立0.45秒躲避窗口，不能把整个4秒冷却当免伤。
    const FVector LocalDirection=GetActorTransform().InverseTransformVectorNoScale(Direction); // 冻结触发时身体空间，转向不重选动画。
    const AGameStateBase* State=GetWorld()->GetGameState(); // World拥有，仅本次同步借用。
    DashPresentation.ServerStartTime=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    DashPresentation.LocalDirectionDegrees=FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.Y,LocalDirection.X));
    DashPresentation.WorldDirection=Direction; DashPresentation.ForceSpeed=DashVelocity.Size2D(); DashPresentation.ForceSeconds=Duration;
    ApplyApprovedDashMovement(0.f); // 与拥有者使用同一种可匹配的源，避免只在服务器瞬移纠正客户端位置。
    if(UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(this)) Build->NotifyDashStarted(this,Duration); // 记录服务器当前起点，结束时按实际XY距离确认，不按设定速度假算成功。
    ForceNetUpdate(); // 与本次推进及时发布，客户端按已过去时间取样而非到包时重播；仍不宣称本地预测。
    FGameplayCueParameters Cue; // 瞬时表现事件由ASC发送；完整时钟/方向仍以DashPresentation原子复制为准，避免两套参数分叉。
    Cue.Normal=Direction; Cue.RawMagnitude=Duration;
    BoundASC->ExecuteGameplayCue(DemoTags::DashCue,Cue);
    if (DashTrailComponent) DashTrailComponent->RefreshApprovedDash(); // Listen Server兜底与Cue共用起点去重，无资产Cue也不会漏播。
    if(UDemoCombatTalentComponent* Talents=FindComponentByClass<UDemoCombatTalentComponent>()) Talents->ArmDash(); // 0.45秒GAS标签到期才转移弹药，不在按键时提前补给。
    UE_LOG(LogFPSDemo,Display,TEXT("DASH_EXECUTE pawn=%s local=%d intent=%d direction=%s velocity=%s duration=%.3f lastInput=%s acceleration=%s"),*GetName(),IsLocallyControlled(),bHasActiveDashDirection,*Direction.ToString(),*DashVelocity.ToString(),Duration,*GetLastMovementInputVector().ToString(),*Movement->GetCurrentAcceleration().ToString());
}

void ADemoCharacter::OnRep_DashPresentation()
{
    DEMO_LOG_CALL();
    if (DashTrailComponent) DashTrailComponent->RefreshApprovedDash(); // 远端模拟代理同样需要特效；Cue丢包或先到时由此补齐。
    if(!IsLocallyControlled() || HasAuthority() || IsCoopEliminated()) return; // 观察者的RootMotion由CMC标准模拟复制驱动，不能再加一份。
    const AGameStateBase* State=GetWorld()->GetGameState(); // 时钟服务由World持有。
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    ApplyApprovedDashMovement(FMath::Max(0.f,static_cast<float>(Now-DashPresentation.ServerStartTime)));
}

void ADemoCharacter::ApplyApprovedDashMovement(float Elapsed)
{
    DEMO_LOG_CALL();
    UCharacterMovementComponent* Movement=GetCharacterMovement(); // 当前Avatar拥有的CMC，源在其预测/重演系统内运行。
    if(!Movement || DashPresentation.ForceSpeed<=0.f || DashPresentation.ForceSeconds<=0.f || Elapsed>=DashPresentation.ForceSeconds)
    { UE_LOG(LogFPSDemo,Log,TEXT("DASH_SOURCE_REJECT expired/invalid elapsed=%.3f duration=%.3f"),Elapsed,DashPresentation.ForceSeconds); return; }
    TSharedPtr<FRootMotionSource_ConstantForce> Force=MakeShared<FRootMotionSource_ConstantForce>(); // 两端保持同名、同完整时长/速度，让原生网络修正可匹配源。
    Force->InstanceName=TEXT("DemoDash"); Force->Priority=1000;
    Force->AccumulateMode=ERootMotionAccumulateMode::Override; // 不叠加旧输入速度，方向以服务器批准的快照为准。
    Force->Force=FVector(DashPresentation.WorldDirection)*DashPresentation.ForceSpeed;
    Force->Duration=DashPresentation.ForceSeconds; Force->SetTime(Elapsed); // 已经过的网络时间不再重放，不把剩余时长改成另一种源配置。
    Force->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate); // 保留物理重力/跳跃与碰撞，地面不人为起跳。
    Force->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::ClampVelocity;
    Force->FinishVelocityParams.ClampVelocity=Movement->GetMaxSpeed();
    Movement->RemoveRootMotionSource(TEXT("DemoDash"));
    const uint16 SourceId=Movement->ApplyRootMotionSource(Force); // CMC拥有并自动在Duration结束时移除。
    UE_LOG(LogFPSDemo,Log,TEXT("DASH_SOURCE_APPLIED pawn=%s authority=%d source=%u elapsed=%.3f speed=%.1f"),*GetName(),HasAuthority(),SourceId,Elapsed,DashPresentation.ForceSpeed);
}

const FDemoDashPresentation& ADemoCharacter::GetDashPresentation() const
{
    DEMO_LOG_TICK(); return DashPresentation;
}

void ADemoCharacter::HandleGameplayCue(UObject* Self,FGameplayTag Tag,EGameplayCueEvent::Type Event,const FGameplayCueParameters& Parameters)
{
    DEMO_LOG_CALL();
    if (Tag==DemoTags::DashCue)
    {
        if (Event==EGameplayCueEvent::Executed && DashTrailComponent) DashTrailComponent->RefreshApprovedDash();
        return; // 此Cue只由本项目表现组件处理，不能再执行第二份Lyra Notify。
    }
    IGameplayCueInterface::HandleGameplayCue(Self,Tag,Event,Parameters);
}
