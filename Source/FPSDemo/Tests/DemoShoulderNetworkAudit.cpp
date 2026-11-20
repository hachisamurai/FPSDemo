// 职责：显式双进程肩部回归，读取每端最终脊柱/肩/手/枪数据，可选截图。依赖现有真实按键网络夹具，不写生产骨姿或网络平滑参数。
#include "Tests/DemoDashNetworkTest.h"
#include "Animation/DemoLyraAnimInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Animation/DemoPlayerPresentationComponent.h" // 读取本端实际展示枪，不从生产胸部标定反推。
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"

void ADemoDashNetworkTest::ObserveAimContract(ADemoCharacter* Pawn,float Elapsed)
{
    DEMO_LOG_TICK();
    if (!Pawn || Elapsed<3.f) return; // 等真实网络输入到达和一次骨求值，仍保留跳落过渡。
    const ADemoWeaponBase* Weapon=Pawn->GetWeaponComponent()->GetActiveWeapon(); // 当前Pawn库存拥有，本回调借用。
    const USkeletalMeshComponent* TP=Pawn->GetPlayerPresentation()->GetThirdPersonWeaponMesh();
    if (!Weapon || !TP || !TP->DoesSocketExist(Weapon->Config.MuzzleSocket)) { UE_LOG(LogFPSDemo,Warning,TEXT("AIM_CONTRACT_NETWORK_MISSING_WEAPON")); return; }
    const FVector Aim=Pawn->GetBaseAimRotation().Vector(); // 本端输入或本端收到的复制值。
    const float PlannedPitch=FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimNetwork"))?(Step%3==0?-60.f:Step%3==1?-30.f:55.f):(Step%2?45.f:55.f); // 低头专项独立固定计划，包含向上回归。
    const FVector Expected=FRotator(PlannedPitch,(Step==21?67.f:0.f)+(Step%2?-40.f:40.f),0).Vector(); // 独立固定计划，不能用同一生产方向给自身作证。
    const FVector Bore=TP->GetSocketRotation(Weapon->Config.MuzzleSocket).Vector(); // 本端最后求值的TP枪口。
    const TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>(); // 本帧纯值，不保存UObject。
    Row->SetNumberField(TEXT("step"),Step); Row->SetNumberField(TEXT("seconds"),Elapsed);
    Row->SetBoolField(TEXT("locally_controlled"),Pawn->IsLocallyControlled());
    Row->SetNumberField(TEXT("actual_pitch"),Pawn->GetBaseAimRotation().Pitch);
    Row->SetNumberField(TEXT("actual_yaw"),Pawn->GetBaseAimRotation().Yaw);
    Row->SetNumberField(TEXT("view_plan_error_deg"),FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Aim,Expected),-1.,1.))));
    Row->SetNumberField(TEXT("tp_view_error_deg"),FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Aim,Bore),-1.,1.))));
    const FVector ReceiverUp=TP->GetSocketTransform(TEXT("Root"),RTS_World).GetRotation().GetUpVector(); // 真实机匣上向，枪管一致仍可能有侧倾。
    const FVector ReferenceUp=FVector::VectorPlaneProject(FVector::UpVector,Aim).GetSafeNormal();
    const FVector ProjectedUp=FVector::VectorPlaneProject(ReceiverUp,Aim).GetSafeNormal();
    Row->SetNumberField(TEXT("tp_cant_deg"),FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Aim,FVector::CrossProduct(ReferenceUp,ProjectedUp)),FVector::DotProduct(ReferenceUp,ProjectedUp))));
    if (Pawn->IsLocallyControlled())
    {
        const FVector FP=Weapon->GetWeaponSkeletalMesh()->GetSocketRotation(Weapon->Config.MuzzleSocket).Vector(); // 只信任拥有者活动FP相机，远端隐藏FP不作为渲染证据。
        const FVector FPRootUp=FVector::VectorPlaneProject(Weapon->GetWeaponSkeletalMesh()->GetSocketTransform(TEXT("Root"),RTS_World).GetRotation().GetUpVector(),Aim).GetSafeNormal(); // 只使用本地真实FP网格。
        Row->SetNumberField(TEXT("fp_cant_deg"),FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Aim,FVector::CrossProduct(ReferenceUp,FPRootUp)),FVector::DotProduct(ReferenceUp,FPRootUp))));
        Row->SetBoolField(TEXT("owner_camera_active"),CastChecked<APlayerController>(Pawn->GetController())->GetViewTarget()==Pawn);
        Row->SetNumberField(TEXT("fp_view_error_deg"),FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Aim,FP),-1.,1.))));
    }
    AimContractFrames.Add(MakeShared<FJsonValueObject>(Row));
    if (!FApp::CanEverRender() || Pawn->IsLocallyControlled()) return; // 房主只展示真实远端，拥有者继续使用FP相机。
    if (!ShoulderCamera)
    {
        ShoulderCamera=GetWorld()->SpawnActor<ACameraActor>();
        if (!ShoulderCamera) { UE_LOG(LogFPSDemo,Warning,TEXT("AIM_CONTRACT_CAMERA_FAILED")); return; }
        ShoulderCamera->GetCameraComponent()->SetFieldOfView(60.f);
    }
    const FVector Target=Pawn->GetActorLocation()+FVector(0,0,40),View=Target+FVector(245,-230,65); // 与本地组合姿态专项采用相同前侧近景。
    ShoulderCamera->SetActorLocationAndRotation(View,(Target-View).Rotation());
    UGameplayStatics::GetPlayerController(this,0)->SetViewTarget(ShoulderCamera);
    if (Elapsed>3.4f && Elapsed<4.5f && GetWorld()->GetTimeSeconds()>=NextShoulderCapture && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Path=FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimNetwork"))?(FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimCandidate"))?TEXT("LowAimReview/Candidate/Network/HostFrames"):TEXT("LowAimReview/Formal/Network/HostFrames")):FParse::Param(FCommandLine::Get(),TEXT("DemoAuthoredAimCandidate"))?TEXT("AimContractAudit/AuthoredCandidate/Network/HostFrames"):TEXT("AimContractAudit/Network/HostFrames"))/FString::Printf(TEXT("%02d_%05d.png"),Step,ShoulderScreenshot++); // 当前用例仅房主渲染；端点明确，避免覆盖本地组合图。
        FScreenshotRequest::RequestScreenshot(Path,false,false); NextShoulderCapture=GetWorld()->GetTimeSeconds()+.2f;
    }
}

float ADemoDashNetworkTest::ScenarioElapsed() const
{
    DEMO_LOG_TICK();
    const float Actual=GetWorld()->GetTimeSeconds()-StepStart; // 本端真实经过秒，不用于跨进程减法。
    const float Extension=bShoulderAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoShoulderNetworkCapture"))?1.f:0.f; // 截图可能阻塞数帧，延长真实移动来获得起步样本，不能放宽采样断言。
    return Actual-FMath::Clamp(Actual-.6f,0.f,Extension); // 只让输入计划在0.6秒保持WASD；GAS与动画继续按真实World时间执行。
}

void ADemoDashNetworkTest::ObserveShoulder(ADemoCharacter* Pawn,float Elapsed)
{
    DEMO_LOG_TICK();
    const float Extension=FParse::Param(FCommandLine::Get(),TEXT("DemoShoulderNetworkCapture"))?1.f:0.f; // 与计划的真实起步延长一致；报告seconds仍是真时间。
    if(!Pawn || Elapsed<.2f || Elapsed>2.5f+Extension) return;
    const USkeletalMeshComponent* Mesh=Pawn->GetMesh(); // 本端求值后的组件，包含本端网络平滑，非服务器注入结果。
    const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(Mesh->GetAnimInstance()); // 游戏线程只读当前策略。
    if(!Native) return;
    const TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>(); // 当前端当前帧值对象，序列化前由夹具数组拥有。
    Row->SetNumberField(TEXT("step"),Step); Row->SetNumberField(TEXT("seconds"),Elapsed);
    Row->SetBoolField(TEXT("locally_controlled"),Pawn->IsLocallyControlled());
    Row->SetBoolField(TEXT("preserve_spine"),Native->bPreserveLocomotionSpine);
    Row->SetBoolField(TEXT("weapon_ready_policy"),Native->bUseWeaponReadyLocomotion); // 实际实例配置，不以命令行冒充正式生效。
    Row->SetBoolField(TEXT("weapon_ready_active"),Native->bWeaponReadyLocomotion); // 闪避等状态应暂退回原生动作。
    Row->SetNumberField(TEXT("speed_cm_s"),Pawn->GetVelocity().Size2D());
    Row->SetNumberField(TEXT("actor_yaw"),Pawn->GetActorRotation().Yaw);
    Row->SetNumberField(TEXT("mesh_relative_yaw"),Mesh->GetRelativeRotation().Yaw);
    const FTransform Hip=Mesh->GetSocketTransform(TEXT("pelvis"),RTS_Component); // 同帧骨盆框架用于检验腰背不是固定Hold刚性链。
    for(const TCHAR* Bone:{TEXT("pelvis"),TEXT("spine_01"),TEXT("spine_03"),TEXT("spine_05"),TEXT("clavicle_l"),TEXT("clavicle_r"),TEXT("hand_l"),TEXT("hand_r"),TEXT("ik_hand_root")})
    {
        const FVector Position=Mesh->GetSocketTransform(Bone,RTS_Component).GetLocation(); // cm，去除Actor整体运动，不掩盖脊柱局部摆动。
        TArray<TSharedPtr<FJsonValue>> XYZ; // JSON三元素数组，每个坐标来自实际骨姿。
        for(double Value:{Position.X,Position.Y,Position.Z}) XYZ.Add(MakeShared<FJsonValueNumber>(Value));
        Row->SetArrayField(Bone,XYZ);
    }
    const FVector ChestInHip=Mesh->GetSocketTransform(TEXT("spine_05"),RTS_Component).GetRelativeTransform(Hip).GetLocation();
    TArray<TSharedPtr<FJsonValue>> Relative; // 原始值用于独立离线检查，生产代码不消费此数据。
    for(double Value:{ChestInHip.X,ChestInHip.Y,ChestInHip.Z}) Relative.Add(MakeShared<FJsonValueNumber>(Value));
    Row->SetArrayField(TEXT("chest_in_pelvis"),Relative); ShoulderFrames.Add(MakeShared<FJsonValueObject>(Row));
    if(Elapsed>.5f && Elapsed<.8f+Extension && Pawn->GetVelocity().Size2D()>80.f) ++ShoulderStartSamples;
    if(!FParse::Param(FCommandLine::Get(),TEXT("DemoShoulderNetworkCapture")) || !FApp::CanEverRender()) return;
    if(!ShoulderCamera)
    {
        ShoulderCamera=GetWorld()->SpawnActor<ACameraActor>();
        if(!ShoulderCamera) { UE_LOG(LogFPSDemo,Warning,TEXT("SHOULDER_NETWORK_CAMERA_FAILED")); return; }
        ShoulderCamera->GetCameraComponent()->SetFieldOfView(60.f);
        UGameplayStatics::GetPlayerController(this,0)->SetViewTarget(ShoulderCamera); // 仅隔离夹具观察点，真实输入仍由原Controller驱动。
    }
    const FVector Target=Pawn->GetActorLocation()+FVector(0,0,10),View=Target+FVector(310,-330,115); // 跟随同一观测对象，完整显示肩与下肢。
    ShoulderCamera->SetActorLocationAndRotation(View,(Target-View).Rotation());
    if(Elapsed<1.f+Extension && GetWorld()->GetTimeSeconds()>=NextShoulderCapture && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString File=FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("DemoReadyAcceptance"))?TEXT("LyraResidualMotion/Network"):TEXT("LyraShoulderAcceptance/NetworkRendered"))/(HasAuthority()?TEXT("HostFrames"):TEXT("ClientFrames"))/FString::Printf(TEXT("%02d_%05d.png"),Step,ShoulderScreenshot++); // 本端和本轮截图独立，不覆盖旧修复证据。
        FScreenshotRequest::RequestScreenshot(File,false,false); NextShoulderCapture=GetWorld()->GetTimeSeconds()+.12f;
    }
}
