// 职责：发布八方向移动和连续步相的游戏线程缓存；供可编辑BlendSpace图及FP层读取，依赖实际角色速度和DA_PlayerLocomotion。
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/DemoLocomotionSet.h"
#include "Characters/DemoCharacter.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h" // 完整姿态路径显式拒绝增量序列，避免根骨缩放坍缩。
#include "GameFramework/GameStateBase.h" // 闪避动作跨端共享服务器时钟。
#include "Debug/DemoLog.h"

void UDemoPlayerAnimInstance::UpdateLocomotion(float DeltaSeconds,const UDemoPlayerAppearance* Appearance)
{
    DEMO_LOG_TICK();
    const UDemoLocomotionSet* Set=Appearance?Appearance->Locomotion.Get():nullptr; // 外观资产保活，只在游戏线程读取。
    const FDemoLocomotionProfile* Profile=Set?Set->Profiles.Find(UpperWeaponId):nullptr; // 武器身份来自同帧上身选择。
    const bool WasEnabled=bDirectionalLocomotion; // 配置启停仅边沿输出日志。
    GroundBlendSpace=Profile && Profile->Ground?Profile->Ground.Get():Set?Set->DefaultGround.Get():nullptr;
    bDirectionalLocomotion=Set && GroundBlendSpace && !Set->Strides.IsEmpty() && Character.IsValid();
    if (WasEnabled!=bDirectionalLocomotion)
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_DIRECTIONAL_MOVEMENT enabled=%d weapon=%s"),bDirectionalLocomotion,*UpperWeaponId.ToString());
    if (!bDirectionalLocomotion)
    { GroundPoseWeight=0.f; BodyMotionWeight=0.f; BodyMotionState=0; ActionOverlayWeight=1.f; GroundVelocity=FVector::ZeroVector; GroundRight=0.f; GroundForward=0.f; return; }
    const float Step=FMath::Max(0.f,DeltaSeconds); // 暂停不推进时钟。
    const FVector Local=Character->GetActorTransform().InverseTransformVectorNoScale(Character->GetVelocity()); // 与Direction相同身体空间。
    const FVector Desired=Character->IsCoopEliminated()?FVector::ZeroVector:FVector(Local.Y,Local.X,0.f).GetClampedToMaxSize(Set->RunSpeed); // X右/Y前，图坐标饱和不削减Gameplay速度。
    GroundVelocity=FMath::VInterpTo(GroundVelocity,Desired,Step,1.f/FMath::Max(.01f,Set->DirectionBlendSeconds));
    if (Speed<1.f && GroundVelocity.SizeSquared()<1.f) GroundVelocity=FVector::ZeroVector;
    GroundRight=GroundVelocity.X; GroundForward=GroundVelocity.Y;
    const FDemoLocomotionProfile* MotionProfile=Profile?Profile:Set->Profiles.Find(TEXT("rifle")); // 近战用默认Manny下身，上身仍由自身动作层覆盖。
    const bool Complete=MotionProfile && MotionProfile->BodyJumpStart && MotionProfile->BodyJumpLoop && MotionProfile->BodyLand && MotionProfile->BodyDashes.Num()==4; // 缺整组明确回退，不按单帧猜资源。
    const FDemoDashPresentation& Dash=Character->GetDashPresentation(); // 复制凭据归Pawn，只借用本帧。
    const AGameStateBase* State=GetWorld()->GetGameState(); // 同步时钟由World提供，模拟端不从Tag到达时刻重新计时。
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float DashElapsed=static_cast<float>(Now-Dash.ServerStartTime); // 秒，负值表示包先于时钟到达，暂不启动。
    const bool DashActive=Complete && DashElapsed>=0.f && DashElapsed<Set->DashSeconds && !Character->IsCoopEliminated();
    const int32 PreviousState=BodyMotionState; // 进入空中时由地面层混入，不能重放上一次跳跃尾部。
    BodyMotionState=DashActive?6:LocomotionState>=3 && LocomotionState<=5?LocomotionState:0;
    UAnimSequence* NextMotion=nullptr; // 当前非地面完整握枪姿态，缺资源不进入完整姿态通道。
    BodyMotionPhase=0.f;
    if (Complete)
    {
        if (BodyMotionState==6)
        {
            const int32 DashIndex=(FMath::FloorToInt((Dash.LocalDirectionDegrees+45.f)/90.f)+4)%4; // 最邻近四向，斜向不在两招中途来回切换。
            NextMotion=MotionProfile->BodyDashes[DashIndex]; BodyMotionPhase=FMath::Clamp(DashElapsed/FMath::Max(.2f,Set->DashSeconds),0.f,1.f);
        }
        else if (BodyMotionState==3) { NextMotion=MotionProfile->BodyJumpStart; BodyMotionPhase=FMath::Clamp(AirSeconds/.18f,0.f,1.f); }
        else if (BodyMotionState==4) { NextMotion=MotionProfile->BodyJumpLoop; BodyMotionPhase=FMath::Fmod(FMath::Max(0.f,AirSeconds-.18f)/NextMotion->GetPlayLength(),1.f); }
        else if (BodyMotionState==5) { NextMotion=MotionProfile->BodyLand; BodyMotionPhase=FMath::Clamp(1.f-LandSeconds/FMath::Max(.05f,Set->LandingSeconds),0.f,1.f); }
        else if (!BodyMotionPose || BodyMotionWeaponId!=UpperWeaponId) NextMotion=MotionProfile->BodyLand; // 地面权重为1时仅准备安全资源，不会显示落地首帧。
    }
    if (NextMotion && NextMotion->IsValidAdditive())
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_FULL_POSE_REJECT_ADDITIVE asset=%s"),*GetNameSafe(NextMotion)); NextMotion=nullptr; BodyMotionWeight=0.f; }
    if (NextMotion)
    {
        if (NextMotion!=BodyMotionPose || BodyMotionWeaponId!=UpperWeaponId)
        {
            const bool BlendOld=BodyMotionPose && BodyMotionWeaponId==UpperWeaponId && PreviousState!=0; // 同枪连续空中片段才保留旧端点。
            PreviousBodyMotionPose=BlendOld?BodyMotionPose.Get():NextMotion;
            PreviousBodyMotionTime=BlendOld?BodyMotionTime:NextMotion->GetPlayLength()*BodyMotionPhase;
            BodyMotionBlendSeconds=BlendOld?0.f:.08f; BodyMotionPose=NextMotion; BodyMotionWeaponId=UpperWeaponId;
            UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_BODY_MOTION state=%d weapon=%s sequence=%s"),BodyMotionState,*UpperWeaponId.ToString(),*GetNameSafe(NextMotion));
        }
        BodyMotionTime=NextMotion->GetPlayLength()*BodyMotionPhase;
    }
    BodyMotionBlendSeconds=FMath::Min(.08f,BodyMotionBlendSeconds+Step);
    BodyMotionBlendAlpha=FMath::SmoothStep(0.f,1.f,BodyMotionBlendSeconds/.08f);
    BodyMotionWeight=Complete && BodyMotionPose && !BodyMotionPose->IsValidAdditive()?1.f:0.f;
    const bool GroundCycle=Complete?BodyMotionState==0:(!bFalling || bSprinting) && LocomotionState!=5; // 新配置全程用已组合资源，不再接入旧增量Land。
    if (GroundCycle && Speed>1.f && !Character->IsCoopEliminated())
        GaitPhase=FMath::Fmod(GaitPhase+Step*Speed/Set->GetStride(Direction,Speed),1.f); // 后退同样正向推进，方向切换保持相位。
    GroundPoseWeight=FMath::FInterpConstantTo(GroundPoseWeight,GroundCycle?1.f:0.f,Step,1.f/.10f);
    const float CurrentAction=UpperBodyAction==TEXT("Hold")?0.f:1.f; // 枪械操作已在已有权威动作时钟中决定。
    const float PreviousAction=PreviousUpperBodyAction==TEXT("Hold")?0.f:1.f; // 淡出期间保持同一换弹/开火混合。
    const float ActionWeight=FMath::Lerp(PreviousAction,CurrentAction,UpperBodyBlendAlpha);
    ActionOverlayWeight=Profile && Profile->Ground?1.f-FMath::Max(GroundPoseWeight,BodyMotionWeight)*(1.f-ActionWeight):1.f;
}
