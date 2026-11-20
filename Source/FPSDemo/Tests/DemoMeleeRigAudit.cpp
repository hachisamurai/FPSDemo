// 职责：Editor实际第一人称静态握持分层诊断。使用-DemoMeleeTest -DemoMeleeRigAudit -DemoMeleeAuditPose=Baseline/01/03/07/IdleRelaxed；附加-DemoMeleeCapture截图，或-DemoMeleeRigRecording -UseFixedTimeStep -FPS=60录3秒真实RHI。DemoMeleeAuditViews额外观察同一FP组件侧面，不改变玩家相机/组件变换。依赖候选DA/AnimBP，仅写Saved证据。
#include "Tests/DemoMeleeTest.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Animation/DemoMeleeAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h" // 录制必须固定1/60秒游戏步长，不能将渲染耗时冒充动画速度。
#include "HAL/FileManager.h"
#include "UnrealClient.h" // 真实游戏视口RHI截图，而非重复既有图片。
#include "Serialization/JsonSerializer.h"
#include "Debug/DemoLog.h"

void ADemoMeleeTest::TickRigAudit()
{
    DEMO_LOG_TICK();
#if !WITH_EDITOR
    Check(false,TEXT("rig audit requires Editor target")); return;
#else
    const double Now=GetWorld()->GetTimeSeconds(); // 游戏秒；有界状态机，不阻塞游戏线程。
    const bool bRecording=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeRigRecording")); // 仅Editor静态诊断视频，不触发攻击或更改正式引用。
    const bool bAuditViews=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeAuditViews")); // 单独侧视/骨骼审查，不与连续录制混用。
    const FString EvidenceRoot=bAuditViews?TEXT("IdleRelaxed20261026/UE"):TEXT("GripFix20261026/UE"); // 新审查不覆盖旧证据。
    if (Now<NextTime) return;
    if (Now>30.) { Check(false,TEXT("rig audit timed out")); return; }
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前独立临时World。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本机真实视角。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 当前Avatar，不跨旅行保存。
    UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 只通过原装备流程创建FP组件。
    if (!Mode || !Player || !Melee || !Melee->GetDefinition()) { Check(false,TEXT("rig audit services unavailable")); return; }
    FString PoseId; FParse::Value(FCommandLine::Get(),TEXT("DemoMeleeAuditPose="),PoseId); // BeginPlay已经验证固定白名单。
    if (Step==0)
    {
        if (!Check(!(bRecording && bAuditViews),TEXT("audit views and recording are separate runs"))) return;
        IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("MeleeAcceptance")/EvidenceRoot),true);
        if (bRecording && !Check(FApp::UseFixedTimeStep() && FMath::IsNearlyEqual(FApp::GetFixedDeltaTime(),1./60.,.00001),TEXT("rig recording requires fixed 60 Hz"))) return;
        if (!Check(Mode->StartRun(),TEXT("rig audit temporary hub"))) return;
        PC->OnRunReady();
        const float FloorZ=20000.f; // 与已用录制平台相同的世界厘米；不修改实际FP眼位或FOV。
        AStaticMeshActor* Floor=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,FloorZ-10),FRotator::ZeroRotator); // World拥有临时地面。
        if (!Check(Floor!=nullptr,TEXT("rig audit floor"))) return;
        Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->SetActorScale3D(FVector(20,20,.2));
        Player->SetActorLocation(FVector(0,0,FloorZ+Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->ConsumeMovementInputVector();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        PC->SetControlRotation(FRotator::ZeroRotator); Player->SetActorRotation(FRotator::ZeroRotator); PC->SetViewTarget(Player);
        if (!Check(Melee->SetEquipped(true),TEXT("rig audit real equip"))) return;
        ADirectionalLight* Light=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-35,140,0)); // 仅隔离平台补光，保留资源原材质。
        if (Light) { Light->GetLightComponent()->SetMobility(EComponentMobility::Movable); Light->GetLightComponent()->SetIntensity(3.f); }
        AddTickPrerequisiteComponent(Melee); // 实际图完成求值后再读取，不把上一帧当作当前状态。
        Advance(1.f); return;
    }
    if (Step==1)
    {
        USkeletalMeshComponent* Actual=nullptr; // Pawn拥有真实近战双臂；不要误用枪械Mesh1P。
        TInlineComponentArray<USkeletalMeshComponent*> Components(Player); // 同步枚举，引用仅本调用有效。
        for (USkeletalMeshComponent* C:Components) if (C->GetFName()==TEXT("MeleeFirstPersonArms")) Actual=C;
        UDemoMeleeAnimInstance* Anim=Actual?Cast<UDemoMeleeAnimInstance>(Actual->GetAnimInstance()):nullptr; // 完整ABP缓存的本次序列时间。
        if (!Check(Actual && Anim && Anim->Pose && Anim->BlendAlpha>.999f,TEXT("rig audit actual graph settled"))) return;
        USkeletalMeshComponent* Pure=NewObject<USkeletalMeshComponent>(this); // 临时纯动画基准组件，无碰撞/可见输出，不改变玩家图。
        Pure->SetSkeletalMesh(Actual->GetSkeletalMeshAsset()); Pure->SetVisibility(false);
        Pure->SetCollisionEnabled(ECollisionEnabled::NoCollision); Pure->RegisterComponent();
        Pure->SetAnimationMode(EAnimationMode::AnimationSingleNode); Pure->PlayAnimation(Anim->Pose,false);
        Pure->SetPosition(Anim->PoseTime,false); Pure->TickAnimation(0.f,false); Pure->RefreshBoneTransforms();
        const TArray<FTransform>& A=Actual->GetComponentSpaceTransforms(); // 完整图组件空间，禁止和世界矩阵直接相减。
        const TArray<FTransform>& B=Pure->GetComponentSpaceTransforms(); // 同骨架/同时间/同组件空间的纯序列。
        if (!Check(A.Num()==B.Num() && A.Num()>0,TEXT("rig audit bone count"))) { Pure->DestroyComponent(); return; }
        double MaxPosition=0.,MaxAngle=0.,MaxScale=0.; // 厘米、度、单位缩放差，覆盖所有骨而非只两个腕。
        TArray<TSharedPtr<FJsonValue>> Rows; // JSON逐骨证据，数组持有到序列化完成。
        for (int32 I=0;I<A.Num();++I)
        {
            const double Position=(A[I].GetLocation()-B[I].GetLocation()).Size(); // 组件空间位置差。
            const double Angle=FMath::RadiansToDegrees(A[I].GetRotation().AngularDistance(B[I].GetRotation())); // 四元数最短角差。
            const double Scale=(A[I].GetScale3D()-B[I].GetScale3D()).GetAbsMax(); // 不忽略意外缩放。
            MaxPosition=FMath::Max(MaxPosition,Position); MaxAngle=FMath::Max(MaxAngle,Angle); MaxScale=FMath::Max(MaxScale,Scale);
            TSharedPtr<FJsonObject> Row=MakeShared<FJsonObject>(); // 本骨只读数值，不保存UObject指针。
            Row->SetStringField(TEXT("bone"),Actual->GetBoneName(I).ToString()); Row->SetNumberField(TEXT("position_cm"),Position);
            Row->SetNumberField(TEXT("angle_deg"),Angle); Row->SetNumberField(TEXT("scale_error"),Scale); Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        TSharedPtr<FJsonObject> Report=MakeShared<FJsonObject>(); // 同批次实际图/纯序列差异。
        Report->SetStringField(TEXT("pose"),PoseId); Report->SetStringField(TEXT("sequence"),Anim->Pose->GetPathName());
        Report->SetNumberField(TEXT("source_seconds"),Anim->PoseTime); Report->SetArrayField(TEXT("bones"),Rows);
        Report->SetNumberField(TEXT("max_position_cm"),MaxPosition); Report->SetNumberField(TEXT("max_angle_deg"),MaxAngle); Report->SetNumberField(TEXT("max_scale_error"),MaxScale);
        FString Encoded; // UTF8报告内容，只写Saved，不保存编辑器包。
        FJsonSerializer::Serialize(Report.ToSharedRef(),TJsonWriterFactory<>::Create(&Encoded));
        FFileHelper::SaveStringToFile(Encoded,*(FPaths::ProjectSavedDir()/TEXT("MeleeAcceptance")/EvidenceRoot/(TEXT("layers_")+PoseId+TEXT(".json"))));
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RIG_LAYERS pose=%s position_cm=%.6f angle_deg=%.6f scale=%.8f"),*PoseId,MaxPosition,MaxAngle,MaxScale);
        Pure->DestroyComponent();
        if (!Check(MaxPosition<.05 && MaxAngle<.2 && MaxScale<.001,TEXT("pure sequence and complete AnimBP agree"))) return;
        if (bRecording)
        {
            TrialRecordingDir=FPaths::ProjectSavedDir()/TEXT("MeleeAcceptance/GripFix20261026/UERecording")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+PoseId);
            if (!Check(IFileManager::Get().MakeDirectory(*TrialRecordingDir,true),TEXT("rig recording folder ready"))) return;
            TrialRecordingRows.Add(TEXT("frame,game_seconds,pose,sequence_seconds,attacking"));
            UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RIG_RECORD_BEGIN pose=%s directory=%s"),*PoseId,*TrialRecordingDir);
        }
        else Capture(*(EvidenceRoot/TEXT("Game_")+PoseId+TEXT(".png")));
        Advance(.5f); return; // 等RHI截图写盘，不在请求同帧退出。
    }
    if (bAuditViews && Step>=2 && Step<=5)
    {
        USkeletalMeshComponent* Actual=nullptr; // 始终使用真实FP图输出，不用另一个SingleNode替身摆拍侧面。
        UStaticMeshComponent* Bat=nullptr; // 实际hand_r挂接的同一根棍。
        TInlineComponentArray<UPrimitiveComponent*> Primitives(Player); // 只修改隔离World可见性，不改变换/骨姿/相机眼位。
        for (UPrimitiveComponent* C:Primitives)
        {
            if (C->GetFName()==TEXT("MeleeFirstPersonArms")) Actual=Cast<USkeletalMeshComponent>(C);
            if (C->GetFName()==TEXT("MeleeFirstPersonBat")) Bat=Cast<UStaticMeshComponent>(C);
        }
        if (!Check(Actual && Bat,TEXT("side audit uses actual FP components"))) return;
        if (Step==2)
        {
            for (UPrimitiveComponent* C:Primitives) if (C!=Actual && C!=Bat) C->SetRenderInMainPass(false); // 隐藏观察相机下额外的TP身体，生产动画继续求值。
            Actual->SetOnlyOwnerSee(false); Bat->SetOnlyOwnerSee(false); // 允许临时观察相机看同一个FP组件。
            const FTransform Component=Actual->GetComponentTransform(); // UE组件空间是作者空间的Y反射，位置只转换一次。
            const FVector Eye=Component.TransformPosition(FVector(-280,70,160)); // 固定侧视机位，与Blender侧面一致。
            const FVector Focus=Component.TransformPosition(FVector(0,30,135)); // 固定观察点，不随姿态自动构图。
            Camera=GetWorld()->SpawnActor<ACameraActor>(Eye,(Focus-Eye).Rotation());
            if (!Check(Camera.IsValid(),TEXT("side camera created"))) return;
            Camera->GetCameraComponent()->FieldOfView=50.f; PC->SetViewTarget(Camera.Get());
            Advance(.5f); return;
        }
        if (Step==3)
        {
            Capture(*(EvidenceRoot/(TEXT("Side_")+PoseId+TEXT(".png")))); Advance(.3f); return;
        }
        if (Step==4)
        {
            Actual->SetRenderInMainPass(false); // 骨骼检查隐藏表面以显示实际关节，不改变姿态或组件变换。
            for (const FName Side:{FName(TEXT("r")),FName(TEXT("l"))})
            {
                const FString Suffix=TEXT("_")+Side.ToString(); // 明确骨骼身份，禁止依屏幕左右猜手别。
                const FColor Color=Side==TEXT("r")?FColor::Orange:FColor::Cyan; // 橙右/青左，持续到下一帧截图。
                const FVector Shoulder=Actual->GetBoneLocation(FName(*(TEXT("upperarm")+Suffix))); // 实际肩关节World厘米。
                const FVector Elbow=Actual->GetBoneLocation(FName(*(TEXT("lowerarm")+Suffix))); // 实际肘关节World厘米。
                const FVector Wrist=Actual->GetBoneLocation(FName(*(TEXT("hand")+Suffix))); // 实际腕关节World厘米。
                const FVector Grip=Bat->GetComponentTransform().TransformPosition(FVector(0,0,Side==TEXT("r")?16:6)); // 固定真实柄部中心，世界cm。
                // ForegroundLineBatcher每帧清理，不能跨帧截图；World批次按2秒生命周期保留。
                DrawDebugLine(GetWorld(),Shoulder,Elbow,Color,false,2.f,SDPG_World,1.5f);
                DrawDebugLine(GetWorld(),Elbow,Wrist,Color,false,2.f,SDPG_World,1.5f);
                for (const FVector Point:{Shoulder,Elbow,Wrist}) DrawDebugSphere(GetWorld(),Point,.8f,10,Color,false,2.f,SDPG_World,1.f);
                DrawDebugLine(GetWorld(),Wrist,Grip,FColor::Yellow,false,2.f,SDPG_World,1.f);
                DrawDebugSphere(GetWorld(),Grip,1.f,12,FColor::Yellow,false,2.f,SDPG_World,1.f);
            }
            Advance(.1f); return;
        }
        Capture(*(EvidenceRoot/(TEXT("DebugSide_")+PoseId+TEXT(".png")))); Advance(.3f); return;
    }
    if (Step==2 && bRecording)
    {
        float SequenceTime=0.f,Progress=0.f; FName Action; // 读取实际动画相位，录像不回写骨骼或相机。
        UAnimSequence* Sequence=Melee->GetPose(true,SequenceTime,Progress,Action); // FP序列身份应仍为静态候选。
        if (!Sequence || Melee->IsAttacking()) { Check(false,TEXT("rig recording must remain a static pose")); return; }
        TrialRecordingRows.Add(FString::Printf(TEXT("%d,%.8f,%s,%.8f,0"),TrialRecordingFrame,Now,*PoseId,SequenceTime));
        const FString FramePath=TrialRecordingDir/FString::Printf(TEXT("Frame-%05d.png"),TrialRecordingFrame); // 独立路径保留所有实际渲染帧。
        FScreenshotRequest::RequestScreenshot(FramePath,false,false);
        ++TrialRecordingFrame;
        if (TrialRecordingFrame==180) Advance(.3f); // 留出末帧写盘时间；180个60Hz帧恰好3秒。
        return;
    }
    if (bRecording)
    {
        if (!Check(FFileHelper::SaveStringArrayToFile(TrialRecordingRows,*(TrialRecordingDir/TEXT("frames.csv"))),TEXT("rig recording timing saved"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RIG_RECORD_SUCCESS pose=%s frames=%d directory=%s"),*PoseId,TrialRecordingFrame,*TrialRecordingDir);
    }
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RIG_AUDIT_SUCCESS pose=%s"),*PoseId);
    SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
#endif
}
