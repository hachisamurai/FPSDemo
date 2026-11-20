// 职责：Editor隔离夹具验证四枪连续八方向移动及操作衔接；依赖正式Movement/GAS和最终已求值骨姿，不写动画变量。
#include "Tests/DemoPlayerAnimationTest.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoFPAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Characters/DemoCharacter.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

bool ADemoPlayerAnimationTest::BeginDirectionalStage()
{
    DEMO_LOG_CALL();
    const int32 Step=Stage%13; // 每枪Idle、八方向、慢走、停止、移动装填、移动取消。
    if (Step==0)
    {
        Player->GetWeaponComponent()->CancelActions();
        if (!EquipWeapon(Stage/13)) return false;
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->ConsumeMovementInputVector();
        Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics);
        PreviousGaitPhase=-1.f; bHoldMeasured=false;
    }
    CastChecked<ADemoPlayerController>(Player->GetController())->SetViewTarget(bFirstPersonCapture?static_cast<AActor*>(Player.Get()):static_cast<AActor*>(Camera.Get()));
    StageName=FString::Printf(TEXT("%s_Move_%02d"),*Weapon->GetWeaponId().ToString(),Step);
    StageStart=GetWorld()->GetTimeSeconds(); StageDuration=Step==9?2.f:1.2f;
    StageSamples=0; MaximumSpeed=0.f; MaximumFootTravel=0.f; MaximumContactDrift=0.f;
    bRunArmActionStarted=false; bCancelled=false;
    NextSample=StageStart; NextCapture=StageStart;
    if (Step>=11) { Weapon->RestoreAmmo(0,2); StartAmmo=0; StageDuration=8.f; } // 正式动作开始后读取冻结时长；空闲GetReloadDuration返回0。
    UE_LOG(LogFPSDemo,Display,TEXT("DIRECTIONAL_STAGE_BEGIN %s"),*StageName);
    return true;
}

void ADemoPlayerAnimationTest::TickDirectionalAudit(float Now,float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (Stage<0) { if (!InitializeFixture()) return; Stage=0; if (!BeginDirectionalStage()) return; }
    if (bDirectionalStagePending)
    {
        bDirectionalStagePending=false;
        if (++Stage==52) { SaveAndExit(true); return; }
        if (!BeginDirectionalStage()) return;
    }
    if (!Player.IsValid() || !Weapon.IsValid()) { Check(false,TEXT("directional fixture lost pawn/weapon")); return; }
    const int32 Step=Stage%13; // 外部输入计划与动画状态解耦。
    const float Elapsed=Now-StageStart;
    const float Angles[]={0.f,45.f,90.f,135.f,180.f,-135.f,-90.f,-45.f}; // 依次穿越所有方向边界，不强停或重置动画。
    if (Step>=1 && Step<=8) Player->AddMovementInput(FRotator(0,Angles[Step-1],0).Vector(),1.f);
    else if (Step==9 || Step>=11) Player->AddMovementInput(FVector::ForwardVector,Step==9?.25f:1.f);
    if (Step>=11 && !bRunArmActionStarted && Elapsed>.35f)
    {
        bRunArmActionStarted=true; RunArmActionStart=Now;
        Player->GetWeaponComponent()->RequestReload();
        if (!Check(Weapon->IsReloading(),TEXT("moving weapon operation started"))) return;
        StageDuration=Elapsed+Weapon->GetReloadDuration()+.7f;
    }
    if (Step==12 && bRunArmActionStarted && !bCancelled && Now-RunArmActionStart>.1f)
    { Player->GetWeaponComponent()->CancelActions(); bCancelled=true; }
    const UDemoPlayerAnimInstance* Body=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 本帧正式图缓存。
    const UDemoFPAnimInstance* Arms=Cast<UDemoFPAnimInstance>(Player->GetMesh1P()->GetAnimInstance()); // 独立第一人称图缓存。
    if (!Body || !Body->bDirectionalLocomotion || !Body->GroundBlendSpace || !Arms)
    { Check(false,TEXT("directional graph/config inactive")); return; }
    if (PreviousGaitPhase>=0.f && DeltaSeconds<.1f && Step!=0)
    {
        const float Advance=FMath::Fmod(Body->GaitPhase-PreviousGaitPhase+1.f,1.f); // 允许自然1->0环绕，拒绝换向倒放/复位。
        if (Advance>.35f) { Check(false,TEXT("gait discontinuity across actual direction transition")); return; }
    }
    PreviousGaitPhase=Body->GaitPhase;
    if (Step>=1 && Step<11 && Elapsed>.2f && Arms->MovementPose)
    {
        const float PhaseDifference=FMath::Abs(Arms->MovementTime/FMath::Max(.01f,Arms->MovementPose->GetPlayLength())-Body->GaitPhase); // 独立读取两个实际图缓存，验证Tick先后而非假定注册顺序。
        if (FMath::Min(PhaseDifference,1.f-PhaseDifference)>.001f) { Check(false,TEXT("FP movement uses stale body gait phase")); return; }
    }
    USkeletalMeshComponent* Mesh=Player->GetMesh(); // 已在PostUpdateWork求值完成。
    const FTransform Anchor=Mesh->GetSocketTransform(TEXT("ik_hand_gun"),RTS_Component);
    const FTransform Left=Mesh->GetSocketTransform(TEXT("hand_l"),RTS_Component).GetRelativeTransform(Anchor);
    const FTransform Right=Mesh->GetSocketTransform(TEXT("hand_r"),RTS_Component).GetRelativeTransform(Anchor);
    if (Step==0 && Elapsed>.6f && !bHoldMeasured) { HoldLeft=Left; HoldRight=Right; bHoldMeasured=true; }
    if (bHoldMeasured && Step<11)
        MaximumContactDrift=FMath::Max(MaximumContactDrift,static_cast<float>(FMath::Max(FVector::Distance(Left.GetLocation(),HoldLeft.GetLocation()),FVector::Distance(Right.GetLocation(),HoldRight.GetLocation()))));
    MaximumSpeed=FMath::Max(MaximumSpeed,static_cast<float>(Player->GetVelocity().Size2D()));
    const FVector Target=Player->GetActorLocation()+FVector(0,0,8); // 外部相机跟随真实位置，显示完整头脚。
    const FVector CameraPosition=Target+FVector(310,-330,115);
    Camera->SetActorLocationAndRotation(CameraPosition,(Target-CameraPosition).Rotation());
    if (Now>=NextSample) { RecordFrame(); NextSample=Now+1.f/15.f; }
    if (bCapture && FApp::CanEverRender() && Now>=NextCapture && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Path=GetReviewRoot()/TEXT("PlayerLocomotionV2Acceptance")/(bFirstPersonCapture?TEXT("FramesFP"):TEXT("Frames"))/FString::Printf(TEXT("%02d_%s_%05d.png"),Stage,*StageName,ScreenshotIndex++); // 只写独立验收图。
        FScreenshotRequest::RequestScreenshot(Path,false,false); NextCapture=Now+1.f/15.f;
    }
    if (Elapsed<StageDuration) return;
    if (!Check(StageSamples>=8 && FindPresentationWeapon(),TEXT("evaluated body, arms and actual gun present"))) return;
    if (!Check(!Player->GetCharacterMovement()->IsFalling(),TEXT("continuous movement remains on real fixture floor"))) return;
    if (Step>=1 && Step<=8 && !Check(MaximumSpeed>500.f && MaximumFootTravel>4.f && Arms->MovementWeight>.95f,TEXT("real directional run drives legs and authored FP movement"))) return;
    if (Step==10 && !Check(Player->GetVelocity().Size2D()<1.f && Arms->MovementWeight<.01f,TEXT("stop returns to neutral hold"))) return;
    if (Step<11 && !Check(MaximumContactDrift<1.f,FString::Printf(TEXT("hold contact drift %.4f cm"),MaximumContactDrift))) return;
    if (Step==11 && !Check(!Weapon->IsReloading() && Weapon->GetAmmo()==2 && Weapon->GetReserveAmmo()==0,TEXT("moving reload settles once"))) return;
    if (Step==12 && !Check(bCancelled && !Weapon->IsReloading() && Weapon->GetAmmo()==0,TEXT("moving cancel retains ammo"))) return;
    bDirectionalStagePending=true; // 先让本帧截图和当前姿态完成；下一真实Tick推进阶段。
}
