// 职责：Editor隔离World录制当前近战正式配置的地面连续动画。启动-DemoMeleeTest -DemoMeleeTrialRecording -UseFixedTimeStep -FPS=60；依赖原GAS/动画图/RHI，只写Saved帧与CSV，不改游戏资产或摄像机配置。
#include "Tests/DemoMeleeTest.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoMeleeAnimInstance.h" // 候选动态分层审查：同序列同相位与完整FP图对照，不把作者渲染代替UE。
#include "Animation/AnimSequence.h" // 记录实际资源路径需完整AnimSequence类型，不修改动画。
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h" // 专用双手录像标志只改变Editor验收夹具，不改变正式输入或玩法。
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "DrawDebugHelpers.h" // 独立候选距离框，不参与伤害、碰撞或动画求解。
#include "Debug/DemoLog.h"

void ADemoMeleeTest::TickTrialRecording()
{
    DEMO_LOG_TICK();
#if !WITH_EDITOR
    Check(false,TEXT("recording fixture requires Editor target")); return;
#else
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeRunningRecording"))) { TickRunningRecording(); return; } // 显式附加模式独立分发，旧录制的步骤/时间不改变。
    const double Now=GetWorld()->GetTimeSeconds(); // 真实引擎游戏时钟；渲染较慢时固定时间步仍保持每帧1/60秒。
    if (Now<NextTime) return;
    if (Now>40.) { Check(false,TEXT("recording exceeds bounded game time")); return; }
    const bool bTwoHand=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTwoHandRecording")); // 本次录制模式：10秒真实FP，完整左右各一次，不跳过回位。
    const bool bPowerCandidate=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleePowerCandidate")) || FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeParentCandidate")) || FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeCoordinatedCandidate")) || FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeAnatomyCandidate")); // 031～034共用真实90cm目标与动态层检查，仅Editor夹具内有效。
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 只借用当前隔离World真实模式。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 唯一本地拥有者。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 不持有跨旅行Pawn。
    UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 原组件驱动攻击，录制不直接写动画相位。
    if (!Mode || !PC || !Player || !Melee || !Melee->GetDefinition()) { Check(false,TEXT("recording runtime services unavailable")); return; }
    if (Step==0)
    {
        if (!Check(FApp::UseFixedTimeStep() && FMath::IsNearlyEqual(FApp::GetFixedDeltaTime(),1./60.,.00001),TEXT("recording fixed 60 Hz required"))) return;
        if (!Check(Mode->StartRun(),TEXT("recording opens real temporary hub"))) return;
        PC->OnRunReady();
        const float FloorZ=20000.f; // 临时平台顶面世界厘米，远离常驻地图避免遮挡；不是Flying或强制骨姿。
        AStaticMeshActor* Platform=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,FloorZ-10),FRotator::ZeroRotator); // World拥有，不保存到地图。
        if (!Check(Platform!=nullptr,TEXT("recording floor spawned"))) return;
        Platform->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Platform->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Platform->SetActorScale3D(FVector(20,20,.2)); // 2000×2000×20厘米，平台与角色接触通过真实Movement地面检测。
        Player->SetActorLocation(FVector(0,0,FloorZ+Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->ConsumeMovementInputVector();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        PC->SetControlRotation(FRotator::ZeroRotator); Player->SetActorRotation(FRotator::ZeroRotator);
        Melee->SetEquipped(true);
        Camera=GetWorld()->SpawnActor<ACameraActor>();
        if (!Check(Camera.IsValid(),TEXT("recording observer camera ready"))) return;
        Camera->GetCameraComponent()->SetFieldOfView(55.f); // 仅观察镜头，保留玩家FP相机90度。
        ADirectionalLight* Fill=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-35,140,0)); // 临时中性补光便于观察肘腕，不改材质或骨骼。
        if (!Check(Fill!=nullptr,TEXT("recording fill light ready"))) return;
        Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable); Fill->GetLightComponent()->SetIntensity(3.f);
        AddTickPrerequisiteComponent(Melee); // 等原近战组件手臂求值后采样，下一帧截图跟随同一运行图。
        TrialRecordingDir=FPaths::ProjectSavedDir()/TEXT("MeleeAcceptance/TrialRecording")/FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
        if (!Check(IFileManager::Get().MakeDirectory(*TrialRecordingDir,true),TEXT("recording evidence folder ready"))) return;
        TrialRecordingRows.Add(TEXT("frame,game_seconds,view,attacking,serial,progress,grounded,fullbody_weight,speed"));
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RECORD_BEGIN directory=%s definition=%s fp=%s tp=%s"),*TrialRecordingDir,*Melee->GetDefinition()->GetPathName(),*Melee->GetDefinition()->FirstPersonIdle->GetPathName(),*Melee->GetDefinition()->ThirdPersonIdle->GetPathName());
        Step=1; NextTime=Now+.8; return; // 给真实Walking、落地混合和装备过渡留出时间，开始前不录冻结帧。
    }
    UDemoPlayerAnimInstance* Anim=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 读取实际图输出的状态，不注入全身权重。
    if (Step==1)
    {
        if (!Check(Anim && Anim->bGroundedForFeet && Player->GetCharacterMovement()->IsMovingOnGround(),TEXT("recording pawn truly grounded"))) return;
        const int32 View=bTwoHand?0:TrialRecordingFrame/216; // 专项保持真实FP；旧夹具保留三镜头。
        if (View==0) PC->SetViewTarget(Player);
        else
        {
            const FVector Offset=View==1?FVector(330,-440,90):FVector(35,-550,65); // cm，相对胶囊中心，足部避开底部HUD。
            Camera->SetActorLocation(Player->GetActorLocation()+Offset);
            Camera->SetActorRotation((Player->GetActorLocation()-FVector(0,0,5)-Camera->GetActorLocation()).Rotation());
            PC->SetViewTarget(Camera.Get());
        }
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RECORD_VIEW index=%d ground=1"),View);
        Step=2; NextTime=Now+.3; return; // 相机管理器与曝光先稳定，热身不混入成片。
    }
    if (Step==2)
    {
        const int32 LocalFrame=bTwoHand?TrialRecordingFrame:TrialRecordingFrame%216; // 新版3秒待机，左扫回位后第5秒右扫，余下时间检查循环。
        if (bTwoHand?(LocalFrame==180 || (LocalFrame==300 && Melee->GetDefinition()->Attacks.IsValidIndex(1))):(LocalFrame==36 || LocalFrame==120)) // 单招式诊断不假播旧右扫冒充新稿。
        {
            if (bTwoHand && LocalFrame==300) Melee->RequestAttack(1); // 与当前右键近战入口相同，不直接写动画姿势。
            else Player->GetWeaponComponent()->StartFire();
            if (!Check(Melee->IsAttacking(),TEXT("recording primary input accepted"))) return;
        }
        float PoseSeconds=0.f,Progress=0.f; FName Action; // 当前只读表现相位；不把视频时间写回攻击计时。
        Melee->GetPose(false,PoseSeconds,Progress,Action);
        if (bPowerCandidate)
        {
            TInlineComponentArray<USkeletalMeshComponent*> ReviewMeshes(Player); // 当前帧组件快照，查找真正FP臂的组件空间。
            for (USkeletalMeshComponent* ReviewMesh:ReviewMeshes)
            {
                if (ReviewMesh->GetFName()!=TEXT("MeleeFirstPersonArms")) continue;
                // Blender(-Y前)经固定Yaw=-90导入后，UE组件+Y为前、-X为作者侧向。
                // 以真实胶囊前向90cm为主验收面；固定FP组件原点在胶囊前28cm，不能把作者90误称为胶囊90。
                FVector ReviewTarget=Player->GetActorLocation()+Player->GetActorForwardVector()*90.f; // World厘米，随当前玩家基准计算。
                ReviewTarget.Z=ReviewMesh->GetComponentTransform().TransformPosition(FVector(0,62,145)).Z; // 高度沿作者目标，水平基准为真实胶囊。
                DrawDebugBox(GetWorld(),ReviewTarget,FVector(6,15,22),FColor::Orange,false,0.f,0,1.f); // 检查框无碰撞，无伪造命中反馈。
                if (Melee->IsAttacking())
                {
                    TInlineComponentArray<UStaticMeshComponent*> ReviewWeapons(Player); // 只读实际FP棍组件，避免用作者轨迹代替真实挂接结果。
                    for (UStaticMeshComponent* ReviewWeapon:ReviewWeapons)
                    {
                        if (ReviewWeapon->GetFName()!=TEXT("MeleeFirstPersonBat")) continue;
                        double MinDistance=DBL_MAX; // 中上段局部Z52..78中心线到目标AABB的最短距离cm；棍半径按作者检查2.8cm。
                        for (int32 BarrelZ=52;BarrelZ<=78;++BarrelZ)
                        {
                            const FVector Delta=ReviewWeapon->GetComponentTransform().TransformPosition(FVector(0,0,BarrelZ))-ReviewTarget; // 统一World坐标，实际挂接Transform只应用一次。
                            const FVector Outside(FMath::Max(FMath::Abs(Delta.X)-6.,0.),FMath::Max(FMath::Abs(Delta.Y)-15.,0.),FMath::Max(FMath::Abs(Delta.Z)-22.,0.)); // 固定目标半深/半宽/半高，与Blender一致。
                            MinDistance=FMath::Min(MinDistance,Outside.Size());
                        }
                        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_POWER_CONTACT frame=%d pose_seconds=%.6f distance_cm=%.6f"),TrialRecordingFrame,PoseSeconds,MinDistance);
                    }
                }
                if (TrialRecordingFrame==0)
                {
                    const float CapsuleForward=FVector::DotProduct(ReviewTarget-Player->GetActorLocation(),Player->GetActorForwardVector()); // 胶囊原点到检查平面的前向距离cm。
                    const float CameraForward=FVector::DotProduct(ReviewTarget-PC->PlayerCameraManager->GetCameraLocation(),PC->PlayerCameraManager->GetCameraRotation().Vector()); // 镜头到同一平面cm。
                    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_POWER_TARGET rig_cm=62 capsule_forward_cm=%.3f camera_forward_cm=%.3f target=%s"),CapsuleForward,CameraForward,*ReviewTarget.ToString());
                }
            }
        }
        if (bTwoHand && (bPowerCandidate || FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeRelaxedCandidate"))) && TrialRecordingFrame%6==0)
        {
            USkeletalMeshComponent* Actual=nullptr; // 仅当前Avatar真实近战双臂，避免误审枪械Mesh1P。
            TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player); // 当前同步调用内有效。
            for (USkeletalMeshComponent* Mesh:Meshes) if (Mesh->GetFName()==TEXT("MeleeFirstPersonArms")) Actual=Mesh;
            UDemoMeleeAnimInstance* FP=Actual?Cast<UDemoMeleeAnimInstance>(Actual->GetAnimInstance()):nullptr; // 图发布的实际序列时间。
            if (!Check(FP && FP->Pose,TEXT("candidate recording has actual FP graph"))) return;
            if (FP->BlendAlpha>.999f) // 正常过渡本来混合两段，不能拿单段基准误报重复驱动。
            {
                USkeletalMeshComponent* Pure=NewObject<USkeletalMeshComponent>(this); // 临时只读基准，检查后立即销毁。
                Pure->SetSkeletalMesh(Actual->GetSkeletalMeshAsset()); Pure->SetVisibility(false); Pure->SetCollisionEnabled(ECollisionEnabled::NoCollision); Pure->RegisterComponent();
                Pure->SetAnimationMode(EAnimationMode::AnimationSingleNode); Pure->PlayAnimation(FP->Pose,false); Pure->SetPosition(FP->PoseTime,false); Pure->TickAnimation(0.f,false); Pure->RefreshBoneTransforms();
                const TArray<FTransform>& Full=Actual->GetComponentSpaceTransforms(); // 两份都在组件空间比较，排除World位移。
                const TArray<FTransform>& Single=Pure->GetComponentSpaceTransforms(); // 同骨架、同序列和相位。
                double MaxPosition=0.,MaxAngle=0.; // 全骨位置cm与旋转度，不只检查手腕。
                const bool bSameCount=Full.Num()==Single.Num() && Full.Num()>0; // 禁止索引错位。
                if (bSameCount) for (int32 Bone=0;Bone<Full.Num();++Bone)
                {
                    MaxPosition=FMath::Max(MaxPosition,(Full[Bone].GetLocation()-Single[Bone].GetLocation()).Size());
                    MaxAngle=FMath::Max(MaxAngle,FMath::RadiansToDegrees(Full[Bone].GetRotation().AngularDistance(Single[Bone].GetRotation())));
                }
                Pure->DestroyComponent();
                UE_LOG(LogFPSDemo,Display,TEXT("MELEE_DYNAMIC_LAYERS frame=%d source_seconds=%.6f position_cm=%.6f angle_deg=%.6f"),TrialRecordingFrame,FP->PoseTime,MaxPosition,MaxAngle);
                if (!Check(bSameCount && MaxPosition<.05 && MaxAngle<.2,TEXT("candidate complete FP graph agrees with pure animation"))) return;
            }
        }
        TrialRecordingRows.Add(FString::Printf(TEXT("%d,%.8f,%d,%d,%u,%.6f,%d,%.6f,%.6f"),TrialRecordingFrame,Now,TrialRecordingFrame/216,Melee->IsAttacking(),Melee->GetAttackSerial(),Progress,Anim && Anim->bGroundedForFeet,Anim?Anim->MeleeFullBodyWeight:0.f,Player->GetVelocity().Size()));
        const FString FramePath=TrialRecordingDir/FString::Printf(TEXT("Frame-%05d.png"),TrialRecordingFrame); // RHI帧末真实视口像素，不以作者渲染替代游戏画面。
        FScreenshotRequest::RequestScreenshot(FramePath,false,false);
        ++TrialRecordingFrame;
        if (TrialRecordingFrame>=(bTwoHand?600:648)) { Step=3; NextTime=Now+.2; } // 等最后一帧写盘后退出，避免末帧缺失。
        else if (!bTwoHand && TrialRecordingFrame%216==0) { Step=1; NextTime=Now+.1; }
        return;
    }
    if (Step==3)
    {
        const uint32 ExpectedAttacks=Melee->GetDefinition()->Attacks.IsValidIndex(1)?2u:1u; // 明确部分诊断和完整双招式录制边界。
        if (bTwoHand && !Check(!Melee->IsAttacking() && Melee->GetAttackSerial()==ExpectedAttacks,TEXT("configured complete attacks returned to idle without restart"))) return;
        if (!Check(FFileHelper::SaveStringArrayToFile(TrialRecordingRows,*(TrialRecordingDir/TEXT("frames.csv"))),TEXT("recording timing saved"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RECORD_SUCCESS frames=%d fixed_fps=60 directory=%s"),TrialRecordingFrame,*TrialRecordingDir);
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
    }
#endif
}
