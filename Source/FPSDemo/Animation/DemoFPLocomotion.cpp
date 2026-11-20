// 职责：第一人称移动层的纯值采样；复用同Pawn全身步相和空中状态，不生成正弦假动作，不发网络RPC。
#include "Animation/DemoFPAnimInstance.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoLocomotionSet.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/AnimSequence.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"

void UDemoFPAnimInstance::UpdateMovement(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwningActor()); // 本实例拥有者，预览可空。
    const UDemoPlayerAppearance* Appearance=Character && Character->GetPlayerPresentation()?Character->GetPlayerPresentation()->GetAppearance():nullptr; // 资产由组件保活。
    const UDemoLocomotionSet* Set=Appearance?Appearance->Locomotion.Get():nullptr;
    const ADemoWeaponBase* Weapon=Character && Character->GetWeaponComponent()?Character->GetWeaponComponent()->GetActiveWeapon():nullptr; // 借用实际当前枪。
    const FName WeaponId=Weapon?Weapon->GetWeaponId():NAME_None;
    const FDemoLocomotionProfile* Profile=Set?Set->Profiles.Find(WeaponId):nullptr;
    const UDemoPlayerAnimInstance* Body=Character?Cast<UDemoPlayerAnimInstance>(Character->GetMesh()->GetAnimInstance()):nullptr; // 同Avatar已发布动画缓存，不直接执行另一个图。
    if (!Profile || !Body || !Body->bDirectionalLocomotion || !IdlePose)
    { MovementPose=IdlePose; PreviousMovementPose=IdlePose; MovementWeight=0.f; MovementTime=0.f; return; }
    UAnimSequence* Next=Profile->Walk; // 地面走跑保持一致握姿，速率由实际步长连续驱动。
    float Phase=Body->GaitPhase; // 0..1左右脚周期，方向切换不归零。
    float TargetWeight=FMath::Clamp(Body->Speed/120.f,0.f,1.f); // 缓慢起停渐入，零速回原Idle。
    if (Body->BodyMotionWeight>0.f && Body->BodyMotionState!=0)
    {
        Next=Body->BodyMotionState==6?Profile->Dash.Get():Body->BodyMotionState==3?Profile->JumpStart.Get():Body->BodyMotionState==5?Profile->Land.Get():Profile->JumpLoop.Get();
        Phase=Body->BodyMotionPhase; TargetWeight=1.f; // 使用同一跳跃/接地/闪避时钟，FP动作本身仍保留当前枪械资源。
    }
    else if (Body->bSprinting && Body->BodyMotionWeight<=0.f) { Next=Profile->Dash; TargetWeight=1.f; }
    else if (Body->LocomotionState==3 || Body->LocomotionState==5)
    {
        Next=Body->LocomotionState==3?Profile->JumpStart.Get():Profile->Land.Get();
        const UAnimSequence* Baseline=Appearance->MovementClips.IsValidIndex(Body->LocomotionState)?Appearance->MovementClips[Body->LocomotionState].Get():nullptr; // 现有空中状态片段契约。
        Phase=Baseline?FMath::Clamp(Body->LocomotionTime/FMath::Max(.01f,Baseline->GetPlayLength()),0.f,1.f):0.f;
        TargetWeight=1.f;
    }
    else if (Body->bFalling) { Next=Profile->JumpLoop; Phase=FMath::Fmod(Body->LocomotionTime,1.f); TargetWeight=1.f; }
    if (!Next) Next=IdlePose;
    if (WeaponId!=MovementWeaponId || Next!=MovementPose)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("FP_MOVEMENT_POSE weapon=%s sequence=%s"),*WeaponId.ToString(),*GetNameSafe(Next));
        const bool SameWeapon=WeaponId==MovementWeaponId && MovementPose; // 跨枪切换直接使用当前新枪，不保留旧手指。
        PreviousMovementPose=SameWeapon?MovementPose.Get():Next;
        PreviousMovementTime=SameWeapon?MovementTime:Next->GetPlayLength()*Phase;
        MovementBlendSeconds=SameWeapon?0.f:.1f;
        MovementPose=Next; MovementWeaponId=WeaponId;
    }
    MovementTime=Next->GetPlayLength()*Phase;
    MovementBlendSeconds=FMath::Min(.1f,MovementBlendSeconds+FMath::Max(0.f,DeltaSeconds));
    MovementBlendAlpha=FMath::SmoothStep(0.f,1.f,MovementBlendSeconds/.1f);
    MovementWeight=FMath::FInterpConstantTo(MovementWeight,Character->IsCoopEliminated()?0.f:TargetWeight*(Snapshot.bAiming?.15f:1.f),DeltaSeconds,10.f);
}
