// 职责：Editor隔离World复现俯仰/水平组合，记录两种实际显示枪的方向与位置；不修改正式图、动画或网络规则。
#include "Tests/DemoPlayerAnimationTest.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Weapons/DemoWeaponBase.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h" // 独立记录最终POV，区别Pawn组件基础眼位与装饰相机反馈。
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HighResScreenshot.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Debug/DemoLog.h"
#include "Combat/DemoEnemyHitZones.h" // 独立焦点射线与正式伤害通道相同；不调用生产瞄准结果作为预期。
#include "Engine/World.h"
#include "Animation/DemoPlayerAimSettings.h"

namespace DemoAimContract
{
    /** Value为世界单位向量或cm位置；仅复制数值到本帧JSON，不保留场景对象。 */
    TArray<TSharedPtr<FJsonValue>> Vector(const FVector& Value)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        return {MakeShared<FJsonValueNumber>(Value.X),MakeShared<FJsonValueNumber>(Value.Y),MakeShared<FJsonValueNumber>(Value.Z)};
    }
    /** 两个单位方向的三维夹角，度；反余弦夹紧浮点误差，不把位置视差当作角度误差。 */
    double Error(const FVector& Left,const FVector& Right)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Left.GetSafeNormal(),Right.GetSafeNormal()),-1.,1.)));
    }
}

void ADemoPlayerAnimationTest::RecordAimContractState(TSharedPtr<FJsonObject> Frame)
{
    DEMO_LOG_TICK();
    USkeletalMeshComponent* TP=FindPresentationWeapon(); // 同一Pawn的最终展示枪，Tick已等待Presentation。
    USkeletalMeshComponent* FP=Weapon->GetWeaponSkeletalMesh(); // 当前库存Actor拥有的第一人称机械网格。
    UCameraComponent* OwnerCamera=Player->GetFirstPersonCameraComponent(); // 外部观察轮可能非活动相机，必须在报告标识。
    if (!TP || !FP || !TP->DoesSocketExist(Weapon->Config.MuzzleSocket) || !FP->DoesSocketExist(Weapon->Config.MuzzleSocket))
    { Check(false,TEXT("aim contract requires both actual muzzle sockets")); return; }
    const FTransform TPSocket=TP->GetSocketTransform(Weapon->Config.MuzzleSocket,RTS_World);
    const FTransform FPSocket=FP->GetSocketTransform(Weapon->Config.MuzzleSocket,RTS_World);
    const FVector Target=Player->GetBaseAimRotation().Vector(); // 输入真值，不读取外部测试相机。
    const FVector TPBore=TPSocket.GetRotation().GetForwardVector(),FPBore=FPSocket.GetRotation().GetForwardVector(); // 两个真实枪管X轴，来自正式枪口Socket。
    const TSharedPtr<FJsonObject> Data=MakeShared<FJsonObject>(); // 本帧纯值证据，采集后不会再更改。
    Data->SetBoolField(TEXT("owner_camera_active"),CastChecked<APlayerController>(Player->GetController())->GetViewTarget()==Player.Get());
    Data->SetArrayField(TEXT("control_direction"),DemoAimContract::Vector(Target));
    Data->SetArrayField(TEXT("camera_direction"),DemoAimContract::Vector(OwnerCamera->GetForwardVector()));
    Data->SetArrayField(TEXT("camera_origin"),DemoAimContract::Vector(OwnerCamera->GetComponentLocation()));
    Data->SetArrayField(TEXT("shared_view_origin"),DemoAimContract::Vector(Player->GetSharedViewLocation()));
    if(const APlayerController* PC=Cast<APlayerController>(Player->GetController())) if(PC->PlayerCameraManager)
    {
        const FRotator FinalRotation=PC->PlayerCameraManager->GetCameraRotation(); // GT最终相机实际值，外部观察轮仍标owner_camera_active=false。
        Data->SetArrayField(TEXT("final_camera_origin"),DemoAimContract::Vector(PC->PlayerCameraManager->GetCameraLocation()));
        Data->SetArrayField(TEXT("final_camera_rotation_pyr"),DemoAimContract::Vector(FVector(FinalRotation.Pitch,FinalRotation.Yaw,FinalRotation.Roll)));
        Data->SetNumberField(TEXT("final_camera_fov"),PC->PlayerCameraManager->GetFOVAngle());
    }
    Data->SetBoolField(TEXT("animated_head_policy"),Player->CaptureFullBodyViewFrame().bUseAnimatedHead);
    Data->SetArrayField(TEXT("fp_bore"),DemoAimContract::Vector(FPBore));
    Data->SetArrayField(TEXT("tp_bore"),DemoAimContract::Vector(TPBore));
    Data->SetArrayField(TEXT("fp_origin"),DemoAimContract::Vector(FPSocket.GetLocation()));
    Data->SetArrayField(TEXT("tp_origin"),DemoAimContract::Vector(TPSocket.GetLocation()));
    Data->SetNumberField(TEXT("fp_to_control_deg"),DemoAimContract::Error(FPBore,Target));
    Data->SetNumberField(TEXT("tp_to_control_deg"),DemoAimContract::Error(TPBore,Target));
    Data->SetNumberField(TEXT("fp_to_tp_deg"),DemoAimContract::Error(FPBore,TPBore));
    Data->SetNumberField(TEXT("camera_to_control_deg"),DemoAimContract::Error(OwnerCamera->GetForwardVector(),Target));
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview")))
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoGunElbowFocus),false,Player.Get()); // 真实视线/碰撞，无假板或目标锁定，不改动画焦点。
        Query.AddIgnoredActor(Weapon.Get());
        FHitResult Hit; // 本帧独立查询，只读其接触点，非动画节点快照。
        const FVector Origin=Player->GetSharedViewLocation(); // 当前真正拥有者眼位，外部摄影机不作为焦点。
        GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Target*Weapon->Config.Range,DemoEnemyHitZones::TraceChannel,Query);
        const FVector Focus=Hit.bBlockingHit?Hit.ImpactPoint:Origin+Target*Weapon->Config.Range;
        Data->SetNumberField(TEXT("independent_focus_error_degrees"),DemoAimContract::Error(TPBore,Focus-TPSocket.GetLocation()));
        Data->SetNumberField(TEXT("independent_focus_distance_cm"),FVector::Distance(Origin,Focus));
        Data->SetBoolField(TEXT("focus_precision_applicable"),FVector::Distance(Origin,Focus)>=GetDefault<UDemoPlayerAimSettings>()->MuzzleFocusMinDistance); // 近于2m的夹限案只记录，不把旧纯视线1度当现行验收。
    }
    Data->SetNumberField(TEXT("body_yaw"),Player->GetActorRotation().Yaw);
    Data->SetNumberField(TEXT("control_yaw"),Player->GetBaseAimRotation().Yaw);
    Data->SetNumberField(TEXT("control_pitch"),Player->GetBaseAimRotation().Pitch);
    if (const UDemoPlayerAnimInstance* Anim=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()))
    { Data->SetNumberField(TEXT("torso_fraction"),Anim->AimTorsoFraction); Data->SetNumberField(TEXT("alignment_weight"),Anim->AimAlignmentWeight); }
    Frame->SetObjectField(TEXT("aim_contract"),Data);
}

void ADemoPlayerAnimationTest::TickAimContractAudit(float Now)
{
    DEMO_LOG_TICK();
    TArray<FRotator> Views={FRotator(0,0,0),FRotator(0,40,0),FRotator(0,-40,0),
        FRotator(30,0,0),FRotator(45,0,0),FRotator(55,0,0),FRotator(30,40,0),
        FRotator(45,40,0),FRotator(55,40,0),FRotator(55,-40,0),FRotator(-60,0,0)}; // 覆盖单轴及身体阈值内的组合角，不使身体先转到相机方向而掩盖问题。
    const bool bLowAim=FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimAudit")); // 补齐低头×侧转组合，默认旧44案仍可重现历史结果。
    if(bLowAim)
        for(float LowPitch:{-5.f,-15.f,-30.f,-45.f,-60.f}) // 角度覆盖中立过渡与上下格之间，不只验端点。
            for(float Yaw:{-45.f,-22.5f,0.f,22.5f,45.f})
                if(LowPitch!=-60.f || Yaw!=0.f) Views.Add(FRotator(LowPitch,Yaw,0.f)); // 原第10案已包含-60/0。
    const int32 Count=Views.Num(); // 每把枪同一计划；扩展轮35案×4枪。
    if (Stage<0)
    {
        if (!InitializeFixture()) return;
        Report->SetStringField(TEXT("diagnostic_scope"),FString::Printf(TEXT("%d view combinations; collection success is not visual acceptance or a fix"),Count*4));
        Stage=0; StageStart=Now; StageName=TEXT("pistol_aim_00"); NextSample=NextCapture=Now+.2f;
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics);
    }
    if (!Player.IsValid() || !Weapon.IsValid()) { Check(false,TEXT("aim contract fixture lost pawn/weapon")); return; }
    const FVector Target=Player->GetActorLocation()+FVector(0,0,40); // 前侧近景，显示胸颈接缝及完整双手。
    const FVector View=Target+FVector(245,-230,65);
    Camera->SetActorLocationAndRotation(View,(Target-View).Rotation());
    CastChecked<APlayerController>(Player->GetController())->SetViewTarget(bFirstPersonCapture?static_cast<AActor*>(Player.Get()):static_cast<AActor*>(Camera.Get())); // 终端装备会恢复玩家ViewTarget，显式恢复本轮观察点，不能把FP图片标作TP。
    if (Now>=NextSample) { RecordFrame(); NextSample=Now+1.f/20.f; }
    if (bCapture && FApp::CanEverRender() && Now>=NextCapture && Now-StageStart<.85f && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Path=GetReviewRoot()/TEXT("Frames")/FString::Printf(TEXT("%02d_%s_%05d.png"),Stage,*StageName,ScreenshotIndex++);
        FScreenshotRequest::RequestScreenshot(Path,false,false); NextCapture=Now+(bLowAim?.3f:.125f); // 扩展静态轮每案保留三个实际帧；旧轮频率不变。
    }
    if (Now-StageStart<1.f) return;
    if (++Stage==Count*4) { SaveAndExit(true); return; }
    if (Stage%Count==0 && !EquipWeapon(Stage/Count)) return;
    Player->GetCharacterMovement()->StopMovementImmediately(); Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics);
    Player->SetActorRotation(FRotator::ZeroRotator); // 每案同一身体朝向，避免前案身体偏移改变实际头身差。
    ADemoPlayerController* PC=CastChecked<ADemoPlayerController>(Player->GetController()); // 游戏线程同步借用。
    PC->SetControlRotation(Views[Stage%Count]); PC->UpdateRotation(0.f); // 真正生产相机限制，不修改动画参数。
    AimRequestedPitch=Views[Stage%Count].Pitch;
    StageName=FString::Printf(TEXT("%s_aim_%02d"),*Weapon->GetWeaponId().ToString(),Stage%Count);
    StageStart=Now; NextSample=NextCapture=Now+.2f;
    UE_LOG(LogFPSDemo,Log,TEXT("AIM_CONTRACT_STAGE %s view=%s"),*StageName,*Views[Stage%Count].ToString());
}
