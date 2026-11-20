#include "Tests/DemoPlayerAnimationTest.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Characters/DemoCharacter.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h" // 正式关卡/换枪会恢复玩家镜头，每阶段重新设置验收观察相机。
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

bool ADemoPlayerAnimationTest::BeginPistolRunArmStage()
{
    DEMO_LOG_CALL();
    static const TCHAR* Names[]={TEXT("Pistol_Run"),TEXT("Pistol_Stop"),TEXT("Pistol_RunTactical"),TEXT("Pistol_RunEmpty"),TEXT("Pistol_RunCancel"),TEXT("Pistol_RunFire"),TEXT("Rifle_Run"),TEXT("Pistol_RunJump"),TEXT("Pistol_Strafe"),TEXT("Pistol_Backwards"),TEXT("Pistol_Walk")}; // 独立输入计划，阶段1保留前一阶段速度以检查自然刹停。
    StageName=Names[Stage];
    Player->GetWeaponComponent()->CancelActions();
    if ((Stage==6 && !EquipWeapon(1)) || (Stage==7 && !EquipWeapon(0))) return false;
    CastChecked<ADemoPlayerController>(Player->GetController())->SetViewTarget(Camera.Get()); // 使用正常外部观察镜头显示完整角色，不改OwnerNoSee或网格可见性。
    if (Stage!=1)
    {
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->ConsumeMovementInputVector();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics);
    }
    StageStart=GetWorld()->GetTimeSeconds(); StageDuration=2.f;
    StageSamples=0; MaximumSpeed=0.f; MaximumFootTravel=0.f; ObservedStates.Reset();
    bCancelled=false; bSawFalling=false; bRunArmActionStarted=false; RunArmActionStart=0.f;
    NextSample=StageStart+.05f; NextCapture=StageStart+.05f;
    if (Stage>=2 && Stage<=5)
    {
        StartAmmo=Stage==3?0:1; ExpectedAmmo=StartAmmo+2;
        Weapon->RestoreAmmo(StartAmmo,2); // 两发备用弹验证真实结算；空仓仍只允许GAS结束时入仓。
        StageDuration=6.f; // 动作发起后按真实装填时长覆盖；此初始上限避免开始前提前结束。
    }
    UE_LOG(LogFPSDemo,Display,TEXT("PISTOL_RUN_STAGE_BEGIN stage=%s"),*StageName);
    return true;
}

void ADemoPlayerAnimationTest::TickPistolRunArmAudit(float Now)
{
    DEMO_LOG_TICK();
    if (Stage<0)
    {
        if (!InitializeFixture()) return;
        Stage=0; if (!BeginPistolRunArmStage()) return;
    }
    if (!Player.IsValid() || !Weapon.IsValid()) { Check(false,TEXT("run arm pawn/weapon missing")); return; }
    const float Elapsed=Now-StageStart; // 当前阶段World秒，动画和GAS沿真实World推进。
    if (Stage!=1)
        Player->AddMovementInput(Stage==8?FVector::RightVector:Stage==9?-FVector::ForwardVector:FVector::ForwardVector,Stage==10?.25f:1.f); // 前跑、横跑、后跑和慢走均使用输入；不写动画缓存。
    if (!bRunArmActionStarted && Elapsed>=.6f)
    {
        bRunArmActionStarted=true; RunArmActionStart=Now;
        if (Stage>=2 && Stage<=4)
        {
            Player->GetWeaponComponent()->RequestReload();
            if (!Check(Weapon->IsReloading(),TEXT("reload starts after real run warmup"))) return;
            StageDuration=Elapsed+Weapon->GetReloadDuration()+.7f;
        }
        if (Stage==5)
        {
            Player->GetWeaponComponent()->StartFire(); Player->GetWeaponComponent()->StopFire();
            StageDuration=Elapsed+1.f;
        }
        if (Stage==7) Player->Jump();
    }
    if (Stage==4 && bRunArmActionStarted && !bCancelled && Now>=RunArmActionStart+.1f)
    { Player->GetWeaponComponent()->CancelActions(); bCancelled=true; }
    if (Stage==7 && bRunArmActionStarted && Now>=RunArmActionStart+.12f) Player->StopJumping();
    MaximumSpeed=FMath::Max(MaximumSpeed,static_cast<float>(Player->GetVelocity().Size2D()));
    bSawFalling|=Player->GetCharacterMovement()->IsFalling();
    if (const UDemoPlayerAnimInstance* Animation=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance())) // 只读本帧真实图选择。
        ObservedStates.Add(Animation->LocomotionState);
    const FVector Target=Player->GetActorLocation()+FVector(0,0,8); // 完整角色中部跟拍，画面包含肩、腕和脚。
    const FVector CameraPosition=Target+FVector(310,-330,115); // 左前侧观察自然摆臂与装填手。
    Camera->SetActorLocationAndRotation(CameraPosition,(Target-CameraPosition).Rotation());
    if (Now+.0001f>=NextSample) { RecordFrame(); NextSample=Now+1.f/60.f; }
    if (bCapture && FApp::CanEverRender() && Now>=NextCapture && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Image=FPaths::ProjectSavedDir()/TEXT("PlayerPistolRunAcceptance/Frames")/FString::Printf(TEXT("%02d_%s_%05d.png"),Stage,*StageName,ScreenshotIndex++); // 与所有旧美术证据目录隔离。
        FScreenshotRequest::RequestScreenshot(Image,false,false);
        NextCapture=Now+1.f/30.f-.0001f;
    }
    if (Elapsed<StageDuration) return;
    if (!Check(StageSamples>=20 && FindPresentationWeapon(),TEXT("actual full-body and weapon poses sampled"))) return;
    if (Stage!=1 && Stage!=10 && !Check(MaximumSpeed>500.f && MaximumFootTravel>4.f,TEXT("running movement and articulated legs observed"))) return;
    if ((Stage==2 || Stage==3) && !Check(!Weapon->IsReloading() && Weapon->GetAmmo()==ExpectedAmmo && Weapon->GetReserveAmmo()==0,TEXT("moving reload settles two rounds once"))) return;
    if (Stage==4 && !Check(bCancelled && !Weapon->IsReloading() && Weapon->GetAmmo()==StartAmmo,TEXT("moving early cancel preserves ammo"))) return;
    if (Stage==5 && !Check(Weapon->GetAmmo()==StartAmmo-1,TEXT("moving fire consumes one round"))) return;
    if (Stage==7 && !Check(bSawFalling && !Player->GetCharacterMovement()->IsFalling(),TEXT("running jump lands normally"))) return;
    if (Stage==10 && !Check(ObservedStates.Contains(1),TEXT("walk exclusion exercised"))) return;
    ++Stage;
    if (Stage==11) SaveAndExit(true);
    else BeginPistolRunArmStage();
}
