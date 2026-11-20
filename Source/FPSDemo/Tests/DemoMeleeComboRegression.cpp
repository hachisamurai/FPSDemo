// 职责：显式Editor -game单机近战连砍复现；真实Hub、Key3装备/每.2秒离散LMB，PostUpdate记录FP4秒/跟随TP4秒的生产时钟、AnimBP、实际骨与刀。
// 使用：UnrealEditor.exe FPSDemo.uproject -game -DemoMeleeComboRegression [-DemoMeleeCapture] [-DemoMeleeComboGracefulTail] -ExecCmds="t.MaxFPS 60"；仅新Saved证据，不改DA/源动画/配置/正式存档，不进入Combat。
// GracefulTail在5.4秒停止新按下，并继续记录至8秒及断言最后已回待机，避免只截到末刀半段；不改变无此参数的历史输入计划。
// 依赖：生产StartRun/OnRunReady、GAS/EnhancedInput、当前FP/TP图与真实RHI；夹具通过只说明采样和输入成功，不是艺术/联机验收。
// World最长30秒，普通采样墙钟30秒，逐帧PNG录制墙钟90秒；只允许该独立进程退出，截图开销不代表游戏帧率。
#include "Tests/DemoMeleeTest.h"
#if WITH_EDITOR
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Weapons/Melee/DemoMeleeDefinition.h"
#include "Animation/DemoMeleeAnimInstance.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoLyraAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Debug/DemoLog.h"

namespace DemoMeleeComboRegression
{
    /** 只读完整当前反射配置；包含Attacks、声音和未知新增属性，作为本轮结束保护，不从历史快照恢复。 */
    FString Configuration(const UDemoMeleeDefinition* Definition)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoMeleeComboRegression::Configuration"));
        if (!Definition) return TEXT("none");
        FString Result; // 本次同步值导出，不持有FProperty或UObject跨帧引用。
        for (TFieldIterator<FProperty> Property(Definition->GetClass());Property;++Property)
        {
            FString Value; Property->ExportText_InContainer(0,Value,Definition,nullptr,const_cast<UDemoMeleeDefinition*>(Definition),PPF_None);
            Result+=Property->GetName()+TEXT("=")+Value+TEXT("\n");
        }
        return Result;
    }
    /** 当前最终Transform按cm/xyzw/scale展开；高频只写CSV，不开启默认VeryVerbose调用输出。 */
    FString TransformCells(const FTransform& Transform)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoMeleeComboRegression::TransformCells"));
        const FVector P=Transform.GetLocation(),S=Transform.GetScale3D(); const FQuat Q=Transform.GetRotation(); // 实测值，空间由CSV kind标识。
        return FString::Printf(TEXT("%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f"),P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z);
    }
    /** 只读求值已结束的两套组件关键骨；不TickAnimation/强制LOD/重求IK，失败保留已采数据。 */
    bool AppendMesh(USkeletalMeshComponent* Mesh,const TCHAR* Label,int32 Frame,double Now,TArray<FString>& Rows)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoMeleeComboRegression::AppendMesh"));
        if (!Mesh || !Mesh->GetSkeletalMeshAsset()) return false;
        static const FName Names[]={TEXT("root"),TEXT("pelvis"),TEXT("spine_01"),TEXT("spine_03"),TEXT("spine_04"),TEXT("spine_05"),TEXT("neck_01"),TEXT("head"),TEXT("clavicle_r"),TEXT("upperarm_r"),TEXT("lowerarm_r"),TEXT("hand_r"),TEXT("clavicle_l"),TEXT("upperarm_l"),TEXT("lowerarm_l"),TEXT("hand_l")}; // 实际Manny语义，FP/TP各自查索引。
        const TArray<FTransform>& Poses=Mesh->GetComponentSpaceTransforms(); // 本PostUpdate帧最终组件空间值，只在同步循环借用。
        for (FName Name:Names)
        {
            const int32 Index=Mesh->GetBoneIndex(Name); // 各Mesh独立索引，绝不按另一Mesh数组位置猜骨。
            if (!Poses.IsValidIndex(Index) || Poses[Index].ContainsNaN()) return false;
            Rows.Add(FString::Printf(TEXT("%d,%.9f,%s,component,%s,%s"),Frame,Now,Label,*Name.ToString(),*TransformCells(Poses[Index])));
        }
        Rows.Add(FString::Printf(TEXT("%d,%.9f,%s,world,Component,%s"),Frame,Now,Label,*TransformCells(Mesh->GetComponentTransform())));
        return true;
    }
}

void ADemoMeleeTest::RestoreComboRegression()
{
    DEMO_LOG_CALL();
    if (ADemoPlayerController* PC=ComboRegressionController.Get())
    {
        PC->InputKey(FInputKeyParams(EKeys::LeftMouseButton,IE_Released,0.)); PC->InputKey(FInputKeyParams(EKeys::Three,IE_Released,0.));
        if (ADemoCharacter* Player=ComboRegressionPawn.Get()) PC->SetViewTarget(Player);
    }
    bComboRegressionButtonDown=false;
    if (ComboRegressionCamera.IsValid()) ComboRegressionCamera->Destroy();
    if (ComboRegressionPlatform.IsValid()) ComboRegressionPlatform->Destroy();
    if (ComboRegressionFill.IsValid()) ComboRegressionFill->Destroy();
    ComboRegressionCamera.Reset(); ComboRegressionPlatform.Reset(); ComboRegressionFill.Reset();
}

void ADemoMeleeTest::FinishComboRegression(bool bSuccess,const FString& Reason)
{
    DEMO_LOG_CALL();
    if (bComboRegressionFinished) return;
    bComboRegressionFinished=true; SetActorTickEnabled(false);
    const ADemoCharacter* Player=ComboRegressionPawn.Get(); const UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 最后一次只读当前配置，不保留新跨World引用。
    const FString After=DemoMeleeComboRegression::Configuration(Melee?Melee->GetDefinition():nullptr); // 与实际本轮起始值比较，绝不采用历史DA。
    const bool bConfigurationUnchanged=!ComboRegressionConfig.IsEmpty() && After==ComboRegressionConfig;
    const bool bDirectory=!ComboRegressionDir.IsEmpty(); bool bSaved=bDirectory; // 初始化失败也写具体失败；不能把无目录当写盘成功。
    if (bDirectory)
    {
        bSaved=FFileHelper::SaveStringArrayToFile(ComboRegressionFrames,*(ComboRegressionDir/TEXT("frames.csv"))) && bSaved;
        bSaved=FFileHelper::SaveStringArrayToFile(ComboRegressionBones,*(ComboRegressionDir/TEXT("bones.csv"))) && bSaved;
        bSaved=FFileHelper::SaveStringArrayToFile(ComboRegressionEvents,*(ComboRegressionDir/TEXT("events.csv"))) && bSaved;
        bSaved=FFileHelper::SaveStringToFile(After,*(ComboRegressionDir/TEXT("definition_after.txt"))) && bSaved;
        const FString Result=FString::Printf(TEXT("status=%s\nreason=%s\nframes=%d\nclicks=%d\nfp_attack_frames=%d\ntp_attack_frames=%d\ndefinition_unchanged=%d\nasset_saved=0\ncombat_entered=0\nscope=actual discrete input, clocks, FP/TP final bone and RHI capture; not art or multiplayer acceptance\n"),bSuccess&&bSaved&&bConfigurationUnchanged?TEXT("CAPTURE_COMPLETED_PENDING_REVIEW"):TEXT("FAILED"),*Reason,ComboRegressionFrame,ComboRegressionPressCount,ComboRegressionAttackFrames[0],ComboRegressionAttackFrames[1],bConfigurationUnchanged);
        bSaved=FFileHelper::SaveStringToFile(Result,*(ComboRegressionDir/TEXT("result.txt"))) && bSaved;
    }
    RestoreComboRegression();
    const bool bPassed=bSuccess && bSaved && bConfigurationUnchanged; // 数据采集门，不用跳变阈值伪称视觉通过。
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_COMBO_REGRESSION_%s reason=%s frames=%d clicks=%d config_unchanged=%d saved=%d directory=%s"),bPassed?TEXT("CAPTURE_COMPLETE"):TEXT("FAILED"),*Reason,ComboRegressionFrame,ComboRegressionPressCount,bConfigurationUnchanged,bSaved,*ComboRegressionDir);
    if (FParse::Param(FCommandLine::Get(),TEXT("game")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeComboRegression")) && GetWorld() && GetWorld()->WorldType==EWorldType::Game && GetNetMode()==NM_Standalone)
        FPlatformMisc::RequestExitWithStatus(false,bPassed?0:1); // 双显式独立-game门；不能结束Root/用户普通Editor或PIE。
    else UE_LOG(LogFPSDemo,Error,TEXT("MELEE_COMBO_REGRESSION_REJECT process exit forbidden outside isolated explicit Editor -game"));
}

void ADemoMeleeTest::TickComboRegression(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (bComboRegressionFinished) return;
    const double Now=GetWorld()->GetTimeSeconds(),WallNow=FPlatformTime::Seconds(); // 实际World秒和墙钟，禁止固定时间步假设或写生产动作时钟。
    if (ComboRegressionWallStart==0.)
    {
        ComboRegressionWallStart=WallNow;
        ComboRegressionDir=FPaths::ProjectSavedDir()/TEXT("MeleeAcceptance/ComboRegression20261114")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+FString::Printf(TEXT("_%u"),FPlatformProcess::GetCurrentProcessId()));
        if (!IFileManager::Get().MakeDirectory(*ComboRegressionDir,true)) { FinishComboRegression(false,TEXT("new evidence directory could not be created")); return; }
        ComboRegressionFrames.Add(TEXT("frame,engine_frame,game_seconds,delta_seconds,wall_seconds,view,view_seconds,attacking,serial,fp_business_sequence,fp_business_time,fp_business_progress,fp_business_action,tp_business_sequence,tp_business_time,tp_business_progress,tp_business_action,fp_actual_sequence,fp_actual_time,fp_previous_sequence,fp_previous_time,fp_blend_alpha,tp_actual_sequence,tp_actual_time,tp_background_sequence,tp_background_time,tp_previous_sequence,tp_previous_time,tp_blend_alpha,tp_fullbody_weight,montage,montage_active,montage_position,montage_weight,montage_instance_id,fp_lod,tp_lod,camera_x,camera_y,camera_z,camera_pitch,camera_yaw,camera_roll,camera_fov,speed,grounded,phase,menu_page"));
        ComboRegressionFrames.Last()+=TEXT(",actor_x,actor_y,actor_z,component_camera_x,component_camera_y,component_camera_z,shared_eye_x,shared_eye_y,shared_eye_z,head_x,head_y,head_z,animated_head_policy"); // 基础/最终POV分列，不能拿组件位置证明Modifier已执行。
        ComboRegressionBones.Add(TEXT("frame,game_seconds,component,space,bone,x,y,z,qx,qy,qz,qw,sx,sy,sz"));
        ComboRegressionEvents.Add(TEXT("engine_frame,game_seconds,event,serial,detail"));
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_COMBO_REGRESSION_BEGIN directory=%s real_time_step=1 capture=%d"),*ComboRegressionDir,FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeCapture")));
    }
    if (!FParse::Param(FCommandLine::Get(),TEXT("game")) || GetWorld()->WorldType!=EWorldType::Game || GetNetMode()!=NM_Standalone) { FinishComboRegression(false,TEXT("requires explicit isolated Editor -game Standalone World")); return; }
    const double WallBudget=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeCapture"))?90.:30.; // PNG落盘可能慢于固定模拟步；只扩大显式录制墙钟，不延长World或攻击时钟。
    if (WallNow-ComboRegressionWallStart>WallBudget || Now>30.) { FinishComboRegression(false,FString::Printf(TEXT("bounded test timeout wall_budget=%.0f game_budget=30"),WallBudget)); return; }
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本World当前规则/本地输入。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr;
    if (!Mode || !PC || !Player || !Melee || !Melee->GetDefinition()) { if (Now>5.) FinishComboRegression(false,TEXT("actual game mode/local Pawn/melee definition unavailable after five seconds")); return; }
    ComboRegressionPawn=Player; ComboRegressionController=PC;
    UCharacterMovementComponent* Move=Player->GetCharacterMovement(); // 原Walking地面与姿态运行，不切Flying/禁用生产动画。
    if (Step==0)
    {
        if (Now<1.) return; // 等ASC/菜单初始化，不绕过原输入资格。
        ComboRegressionConfig=DemoMeleeComboRegression::Configuration(Melee->GetDefinition());
        FFileHelper::SaveStringToFile(ComboRegressionConfig,*(ComboRegressionDir/TEXT("definition_before.txt")));
        if (!Mode->StartRun()) { FinishComboRegression(false,TEXT("production StartRun did not enter temporary Hub")); return; }
        PC->OnRunReady();
        const float FloorZ=20000.f; AStaticMeshActor* Platform=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,FloorZ-10),FRotator::ZeroRotator); // World临时地面避开真实Hub设施，不保存地图。
        if (!Platform) { FinishComboRegression(false,TEXT("temporary Walking floor spawn failed")); return; }
        ComboRegressionPlatform=Platform; Platform->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable); Platform->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))); Platform->SetActorScale3D(FVector(20,20,.2));
        Player->SetActorLocation(FVector(0,0,FloorZ+Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2),false,nullptr,ETeleportType::TeleportPhysics);
        Move->StopMovementImmediately(); Player->ConsumeMovementInputVector(); Move->SetMovementMode(MOVE_Walking); Player->SetActorRotation(FRotator::ZeroRotator); PC->SetControlRotation(FRotator::ZeroRotator);
        ComboRegressionCamera=GetWorld()->SpawnActor<ACameraActor>();
        if (!ComboRegressionCamera.IsValid()) { FinishComboRegression(false,TEXT("TP observer camera unavailable")); return; }
        ComboRegressionCamera->GetCameraComponent()->SetFieldOfView(60.f); // 只改观察机；原FP相机FOV/变换全部保留并逐帧记录。
        ADirectionalLight* Fill=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-35,140,0));
        if (Fill) { ComboRegressionFill=Fill; Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable); Fill->GetLightComponent()->SetIntensity(3.f); }
        AddTickPrerequisiteComponent(Melee); AddTickPrerequisiteComponent(Player->GetMesh()); // TG_PostUpdateWork读取原组件/Body求值后结果，不主动Tick动画。
        PC->InputKey(FInputKeyParams(EKeys::Three,IE_Pressed,1.)); ComboRegressionEvents.Add(FString::Printf(TEXT("%llu,%.9f,Key3Pressed,0,equip"),GFrameCounter,Now));
        Step=1; NextTime=Now+.10; return;
    }
    if (Step==1)
    {
        if (Now<NextTime) return;
        PC->InputKey(FInputKeyParams(EKeys::Three,IE_Released,0.));
        if (!Melee->IsEquipped()) { FinishComboRegression(false,TEXT("real Key3 Started did not equip current default melee")); return; }
        Step=2; NextTime=Now+.75; return; // 原装备/落地混合完整热身，不将其当连招边界。
    }
    if (Now<NextTime) return;
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 全录制严格Hub，不能靠开Combat绕过输入门禁。
    if (!State || State->Phase!=EDemoPhase::Hub || !Move->IsMovingOnGround()) { FinishComboRegression(false,TEXT("recording must remain truly grounded in production Hub")); return; }
    if (Step==2)
    {
        ComboRegressionCaptureStart=Now; ComboRegressionNextPress=Now+.2; PC->SetViewTarget(Player); Step=3;
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_COMBO_REGRESSION_VIEW view=FP now=%.9f"),Now);
    }
    const double Elapsed=Now-ComboRegressionCaptureStart; const int32 View=Elapsed<4.?0:1; // 实际World秒，不把截图帧数当时钟。
    const bool bGracefulTail=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeComboGracefulTail")); // 仅本隔离采样的停止输入计划；仍走生产冷却/收刀，不主动EndAttack。
    if (Elapsed>=8.)
    {
        FinishComboRegression(ComboRegressionAttackFrames[0]>0 && ComboRegressionAttackFrames[1]>0 && Melee->GetAttackSerial()>=4 && (!bGracefulTail || !Melee->IsAttacking()),
            bGracefulTail?TEXT("eight actual game seconds sampled with released input and final recovery observed; inspect sequence/time/final-bone evidence")
                         :TEXT("eight actual game seconds sampled; inspect sequence/time/final-bone boundary evidence")); return;
    }
    if (View==1)
    {
        ComboRegressionCamera->SetActorLocation(Player->GetActorLocation()+FVector(390,-180,95));
        ComboRegressionCamera->SetActorRotation((Player->GetActorLocation()+FVector(0,0,15)-ComboRegressionCamera->GetActorLocation()).Rotation());
        if (PC->GetViewTarget()!=ComboRegressionCamera.Get()) { PC->SetViewTarget(ComboRegressionCamera.Get()); UE_LOG(LogFPSDemo,Display,TEXT("MELEE_COMBO_REGRESSION_VIEW view=TP now=%.9f"),Now); }
    }
    USkeletalMeshComponent* FPArms=nullptr; TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player); // 真实Pawn当前组件，不假设旧枪械Mesh1P就是近战FP。
    for (USkeletalMeshComponent* Mesh:Meshes) if (Mesh->GetFName()==TEXT("MeleeFirstPersonArms")) FPArms=Mesh;
    UDemoMeleeAnimInstance* FP=FPArms?Cast<UDemoMeleeAnimInstance>(FPArms->GetAnimInstance()):nullptr; UDemoPlayerAnimInstance* TP=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance());
    if (!FP || !TP || !PC->PlayerCameraManager) { FinishComboRegression(false,TEXT("actual independent FP/TP AnimInstances or final camera missing")); return; }
    float FPTime=0.f,FPProgress=0.f,TPTime=0.f,TPProgress=0.f; FName FPAction,TPAction; // 业务取样与实际图缓存分列，允许真实一帧发布差异。
    const UAnimSequence* FPSequence=Melee->GetPose(true,FPTime,FPProgress,FPAction); const UAnimSequence* TPSequence=Melee->GetPose(false,TPTime,TPProgress,TPAction);
    const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(TP); const UAnimMontage* Montage=Native?Native->CurrentMeleeMontage.Get():nullptr;
    FAnimMontageInstance* Instance=Montage?TP->GetActiveInstanceForMontage(Montage):nullptr; // 只读真实Slot播放器权重/位置，不创建/重启/改变Montage。
    const FVector CameraPosition=PC->PlayerCameraManager->GetCameraLocation(); const FRotator CameraRotation=PC->PlayerCameraManager->GetCameraRotation();
    ComboRegressionFrames.Add(FString::Printf(TEXT("%d,%llu,%.9f,%.9f,%.9f,%s,%.9f,%d,%u,%s,%.9f,%.9f,%s,%s,%.9f,%.9f,%s,%s,%.9f,%s,%.9f,%.9f,%s,%.9f,%s,%.9f,%s,%.9f,%.9f,%.9f,%s,%d,%.9f,%.9f,%d,%d,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%d,%d,%d"),
        ComboRegressionFrame,GFrameCounter,Now,DeltaSeconds,WallNow,View==0?TEXT("FP"):TEXT("TP"),Elapsed-View*4.,Melee->IsAttacking(),Melee->GetAttackSerial(),
        *GetPathNameSafe(FPSequence),FPTime,FPProgress,*FPAction.ToString(),*GetPathNameSafe(TPSequence),TPTime,TPProgress,*TPAction.ToString(),
        *GetPathNameSafe(FP->Pose.Get()),FP->PoseTime,*GetPathNameSafe(FP->PreviousPose.Get()),FP->PreviousTime,FP->BlendAlpha,
        *GetPathNameSafe(TP->MeleeAttackSequence.Get()),TP->MeleeAttackTime,*GetPathNameSafe(TP->UpperBodySequence.Get()),TP->UpperBodyTime,
        *GetPathNameSafe(TP->PreviousUpperBodySequence.Get()),TP->PreviousUpperBodyTime,TP->UpperBodyBlendAlpha,TP->MeleeFullBodyWeight,
        *GetPathNameSafe(Montage),Instance!=nullptr,Instance?Instance->GetPosition():0.f,Instance?Instance->GetWeight():0.f,Instance?Instance->GetInstanceID():INDEX_NONE,
        FPArms->GetPredictedLODLevel(),Player->GetMesh()->GetPredictedLODLevel(),CameraPosition.X,CameraPosition.Y,CameraPosition.Z,CameraRotation.Pitch,CameraRotation.Yaw,CameraRotation.Roll,PC->PlayerCameraManager->GetFOVAngle(),Player->GetVelocity().Size2D(),Move->IsMovingOnGround(),static_cast<int32>(State->Phase),static_cast<int32>(PC->GetMenuPage())));
    const FVector ActorPosition=Player->GetActorLocation(),ComponentEye=Player->GetFirstPersonCameraComponent()->GetComponentLocation(),SharedEye=Player->GetSharedViewLocation(),HeadPosition=Player->GetMesh()->GetSocketLocation(TEXT("head")); // 逐帧实际测量，不从烘焙源/目标值代替最终镜头。
    const bool bAnimatedHead=Player->CaptureFullBodyViewFrame().bUseAnimatedHead;
    ComboRegressionFrames.Last()+=FString::Printf(TEXT(",%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%d"),ActorPosition.X,ActorPosition.Y,ActorPosition.Z,ComponentEye.X,ComponentEye.Y,ComponentEye.Z,SharedEye.X,SharedEye.Y,SharedEye.Z,HeadPosition.X,HeadPosition.Y,HeadPosition.Z,bAnimatedHead);
    if(View==0 && !bAnimatedHead && (!ComponentEye.Equals(SharedEye,.001) || !CameraPosition.Equals(SharedEye,.001) || FMath::Abs(CameraRotation.Roll)>.6501))
    { FinishComboRegression(false,TEXT("stable base/component/final eye mismatch or melee roll budget exceeded")); return; } // 只断言新基础契约，原艺术/连砍检查仍独立。
    if (!DemoMeleeComboRegression::AppendMesh(FPArms,TEXT("FP"),ComboRegressionFrame,Now,ComboRegressionBones) || !DemoMeleeComboRegression::AppendMesh(Player->GetMesh(),TEXT("TP"),ComboRegressionFrame,Now,ComboRegressionBones)) { FinishComboRegression(false,TEXT("actual final key bone transform missing or nonfinite")); return; }
    TInlineComponentArray<UStaticMeshComponent*> Weapons(Player); // 实际FP/TP刀组件与端点世界坐标，不用作者烘焙轨迹代替可见刀。
    for (UStaticMeshComponent* Weapon:Weapons) if (Weapon->GetFName()==TEXT("MeleeFirstPersonBat") || Weapon->GetFName()==TEXT("MeleeThirdPersonBat"))
    {
        const FTransform World=Weapon->GetComponentTransform(); const UDemoMeleeDefinition* Definition=Melee->GetDefinition();
        ComboRegressionBones.Add(FString::Printf(TEXT("%d,%.9f,%s,world,Weapon,%s"),ComboRegressionFrame,Now,*Weapon->GetName(),*DemoMeleeComboRegression::TransformCells(World)));
        for (int32 Endpoint=0;Endpoint<2;++Endpoint)
        {
            const FVector Position=World.TransformPosition(Endpoint==0?Definition->TraceBase:Definition->TraceTip); // 配置的真实刀本地端点，只变换一次。
            ComboRegressionBones.Add(FString::Printf(TEXT("%d,%.9f,%s,world,%s,%s"),ComboRegressionFrame,Now,*Weapon->GetName(),Endpoint==0?TEXT("TraceBase"):TEXT("TraceTip"),*DemoMeleeComboRegression::TransformCells(FTransform(Position))));
        }
    }
    if (Melee->IsAttacking()) ++ComboRegressionAttackFrames[View];
    if (Melee->GetAttackSerial()!=ComboRegressionPreviousSerial) { ComboRegressionPreviousSerial=Melee->GetAttackSerial(); ComboRegressionEvents.Add(FString::Printf(TEXT("%llu,%.9f,SerialChanged,%u,observed_after_animation"),GFrameCounter,Now,ComboRegressionPreviousSerial)); }
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeCapture"))) FScreenshotRequest::RequestScreenshot(ComboRegressionDir/FString::Printf(TEXT("Frame-%05d.png"),ComboRegressionFrame),false,false); // 帧末真实RHI；本帧输入只影响随后游戏输入处理，时间并列保存。
    ++ComboRegressionFrame;
    if (bComboRegressionButtonDown && Now>=ComboRegressionReleaseAt)
    {
        PC->InputKey(FInputKeyParams(EKeys::LeftMouseButton,IE_Released,0.)); bComboRegressionButtonDown=false;
        ComboRegressionEvents.Add(FString::Printf(TEXT("%llu,%.9f,LMBReleased,%u,real_input"),GFrameCounter,Now,Melee->GetAttackSerial()));
    }
    else if (!bComboRegressionButtonDown && Now>=ComboRegressionNextPress && (!bGracefulTail || Elapsed<5.4))
    {
        PC->InputKey(FInputKeyParams(EKeys::LeftMouseButton,IE_Pressed,1.)); bComboRegressionButtonDown=true; ComboRegressionReleaseAt=Now+.04; ComboRegressionNextPress=Now+.2; ++ComboRegressionPressCount;
        if (ComboRegressionPressCount==1 && Melee->GetDefinition()->bRestartPrimarySequenceAfterRecovery)
            ComboRegressionNextPress=Now+Melee->GetDefinition()->Attacks[0].Duration+.25; // 新组策略先单击并等完整收招，验证下一组仍正手；随后才连续离散点击测试两刀组。旧baseline策略保持原输入。
        ComboRegressionEvents.Add(FString::Printf(TEXT("%llu,%.9f,LMBPressed,%u,click_%d"),GFrameCounter,Now,Melee->GetAttackSerial(),ComboRegressionPressCount)); // 不直接RequestAttack，不长帧补发；触发原EnhancedInput Started/Completed。
    }
}
#endif
