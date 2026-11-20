// 职责：Editor隔离游戏中复现准星与可见枪管视差；复用真实战斗/装备夹具，移动同一遮挡板并记录四枪近远/侧转。依赖全身Presentation与Physics查询，不修改动画资产。
#include "Tests/DemoPlayerAnimationTest.h"
#include "Characters/DemoCharacter.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/DemoPlayerAnimInstance.h" // 专项仅记录游戏线程焦点快照与最终枪口的差，区分求解误差和相机/骨姿时序。
#include "Animation/DemoLyraAnimInstance.h" // 只读随机待机让权状态，验证真实输入中断与准星汇聚并存。
#include "Camera/CameraActor.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/DemoLog.h"
#include "DrawDebugHelpers.h" // 录制前半段可见真实Socket轴，纯一帧调试绘制，不改变枪姿/伤害。
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h" // 只在录制模式订阅本World PostActorTick，所有UObject读取保持游戏线程。
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"

namespace
{
    /** A/B为有限非零方向；返回度数，用独立几何比较而非生产节点内部的校准结果。 */
    double FocusAngle(const FVector& A,const FVector& B)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(),B.GetSafeNormal()),-1.,1.)));
    }
}

void ADemoPlayerAnimationTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if (MuzzleRecordingHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(MuzzleRecordingHandle);
    MuzzleRecordingHandle.Reset();
    Super::EndPlay(EndPlayReason);
}

void ADemoPlayerAnimationTest::TickMuzzleFocusRecording(float Now)
{
    DEMO_LOG_TICK();
    if (Stage<0)
    {
        const bool bReady=InitializeFixture(); // 仍等待真实战斗/装备初始化，不伪造可见身体或游戏进度。
        if (Player.IsValid()) RemoveTickPrerequisiteComponent(Player->GetPlayerPresentation()); // 原静态测试的末帧依赖不适用于PrePhysics输入，实际抓帧由下方World委托保证时序。
        if (!bReady) return;
        if (!Check(FApp::UseFixedTimeStep() && FMath::IsNearlyEqual(FApp::GetFixedDeltaTime(),1./60.,.0001),TEXT("recording requires actual 60Hz fixed World step"))) return;
        AStaticMeshActor* Wall=GetWorld()->SpawnActor<AStaticMeshActor>(); // World拥有的同一真实碰撞板，两次进程执行同一计划。
        if (!Check(Wall!=nullptr,TEXT("recording real obstruction ready"))) return;
        MuzzleFocusWall=Wall;
        Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Wall->SetActorScale3D(FVector(.05f,8.f,8.f)); // 同32案板，不加视频专用假枪/假准星。
        Player->AddTickPrerequisiteActor(this); Player->GetCharacterMovement()->AddTickPrerequisiteActor(this); Player->GetMesh()->AddTickPrerequisiteActor(this); // 输入先于Movement/身体；Presentation仍沿原身体先决条件。
        MuzzleRecordingHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&ADemoPlayerAnimationTest::CaptureMuzzleFocusRecording); // 弱UObject委托仅游戏线程触发，本实例EndPlay解除，无Lambda裸捕获。
        Stage=0; StageStart=-1.f; ScreenshotIndex=0;
        Report->SetStringField(TEXT("scope"),TEXT("Actual 60Hz RHI recording, four weapons x 360 frames, same controlled depth/view plan; first 3s each weapon draws actual bore guide"));
        IFileManager::Get().MakeDirectory(*(GetReviewRoot()/TEXT("Frames")),true);
    }
    if (!Player.IsValid() || !Weapon.IsValid()) { Check(false,TEXT("recording lost actual pawn/weapon")); return; }
    if (ScreenshotIndex>=1440)
    { UE_LOG(LogFPSDemo,Display,TEXT("MUZZLE_FOCUS_RECORD_SUCCESS frames=1440 rate=60")); SaveAndExit(true); return; } // 上一帧末截图已完成，下一Tick才退出。
    const int32 WantedStage=ScreenshotIndex/360; // 每枪准确6秒真实帧；不按磁盘写入速度改变播放时间。
    if (StageStart<0.f || WantedStage!=Stage)
    {
        Stage=WantedStage;
        if (Stage>0 && !EquipWeapon(Stage)) return;
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics); Player->SetActorRotation(FRotator::ZeroRotator);
        CastChecked<APlayerController>(Player->GetController())->SetViewTarget(Player.Get());
        StageName=Weapon->GetWeaponId().ToString(); StageStart=Now; MuzzleRecordingWarmup=0;
    }
    const float Time=MuzzleRecordingWarmup<60?0.f:static_cast<float>(ScreenshotIndex%360)/60.f; // 预热不入影片；采集时间来自真实固定World步长。
    const float LookPhase=FMath::Clamp(Time-2.f,0.f,2.f); // 2..4秒平滑左右/上下扫视，两端角度均回零。
    const FRotator View(12.f*FMath::Sin(LookPhase*PI),25.f*FMath::Sin(LookPhase*PI),0.f); // 正常Controller输入，不旋转枪组件或覆盖动画结果。
    APlayerController* Controller=CastChecked<APlayerController>(Player->GetController()); // 当前拥有者控制器，仅本回调同步借用。
    Controller->SetControlRotation(View); Controller->UpdateRotation(0.f);
    const float Distance=Time<1.f?600.f:Time<2.f?FMath::Lerp(600.f,200.f,FMath::SmoothStep(1.f,2.f,Time)):Time<4.f?200.f:Time<5.f?FMath::Lerp(200.f,1500.f,FMath::SmoothStep(4.f,5.f,Time)):FMath::Lerp(1500.f,350.f,FMath::SmoothStep(5.f,6.f,Time)); // cm；同一受控板近远切换，非目标锁定。
    MuzzleFocusWall->SetActorLocationAndRotation(Player->GetSharedViewLocation()+View.Vector()*(Distance+2.5f),View);
    ++MuzzleRecordingWarmup;
}

void ADemoPlayerAnimationTest::CaptureMuzzleFocusRecording(UWorld* World,ELevelTick Type,float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (World!=GetWorld() || bFailed || Stage<0 || MuzzleRecordingWarmup<60 || ScreenshotIndex>=1440) return; // 其他World/预热不产生媒体，Actor结束后弱绑定自然失效。
    if (!Check(FMath::IsNearlyEqual(DeltaSeconds,1.f/60.f,.0001f) && !FScreenshotRequest::IsScreenshotRequested(),TEXT("one real RHI frame for each fixed World step"))) return;
    USkeletalMeshComponent* Gun=FindPresentationWeapon(); // 此时原Body和Presentation均已更新，不从源FBX或旧FP枪伪造画面。
    if (!Check(Gun && Gun->DoesSocketExist(Weapon->Config.MuzzleSocket),TEXT("recording reads actual shared muzzle"))) return;
    const FTransform Muzzle=Gun->GetSocketTransform(Weapon->Config.MuzzleSocket,RTS_World); // 本帧真实世界Socket位置/枪管X轴。
    if (ScreenshotIndex%360<180)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoMuzzleVideoGuide),false,Player.Get()); // 导向线单独只读查询，不参与瞄准或任何伤害。
        Query.AddIgnoredActor(Weapon.Get());
        FHitResult Hit; // 真实枪管轴碰撞结果，不取准星目标来伪造正确方向。
        const FVector End=Muzzle.GetLocation()+Muzzle.GetRotation().GetForwardVector()*2000.f; // 最长20m导向，实际板遮挡时截止。
        GetWorld()->LineTraceSingleByChannel(Hit,Muzzle.GetLocation(),End,DemoEnemyHitZones::TraceChannel,Query);
        DrawDebugLine(GetWorld(),Muzzle.GetLocation(),Hit.bBlockingHit?Hit.ImpactPoint:End,FColor::Orange,false,-1.f,0,.5f);
        if (Hit.bBlockingHit) DrawDebugSphere(GetWorld(),Hit.ImpactPoint,1.5f,8,FColor::Orange,false,-1.f,0,.4f);
    }
    TSharedPtr<FJsonObject> Frame=MakeShared<FJsonObject>(); // 一帧纯值索引，媒体与World时钟可一一核对。
    Frame->SetNumberField(TEXT("frame"),ScreenshotIndex); Frame->SetStringField(TEXT("weapon"),StageName);
    Frame->SetNumberField(TEXT("world_seconds"),GetWorld()->GetTimeSeconds()); Frame->SetNumberField(TEXT("fixed_delta"),DeltaSeconds);
    Frame->SetNumberField(TEXT("control_pitch"),Player->GetBaseAimRotation().Pitch); Frame->SetNumberField(TEXT("control_yaw"),Player->GetBaseAimRotation().Yaw);
    Frame->SetBoolField(TEXT("bore_guide"),ScreenshotIndex%360<180); Frames.Add(MakeShared<FJsonValueObject>(Frame));
    FScreenshotRequest::RequestScreenshot(GetReviewRoot()/TEXT("Frames")/FString::Printf(TEXT("Frame-%05d.png"),ScreenshotIndex),false,false);
    ++ScreenshotIndex;
}

void ADemoPlayerAnimationTest::TickMuzzleFocusAudit(float Now)
{
    DEMO_LOG_TICK();
    const bool bBaseline=FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusBaseline")); // 仅改变验收预期和输出根，不切换生产代码。
    const int32 Cases=8; // 每枪独立覆盖近/中/远、无命中、贴墙、上下侧转、焦点距离切换。
    if (Stage<0)
    {
        if (!InitializeFixture()) return;
        AStaticMeshActor* Wall=GetWorld()->SpawnActor<AStaticMeshActor>(); // 测试World拥有，所有阶段复用一次分配。
        if (!Check(Wall!=nullptr,TEXT("focus audit obstruction allocated"))) return;
        MuzzleFocusWall=Wall;
        Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Wall->SetActorScale3D(FVector(.05f,8.f,8.f)); // 5cm厚、8m宽高，独立碰撞目标，不以敌人胶囊作焦点。
        Stage=0; StageStart=-1.f;
        Report->SetStringField(TEXT("scope"),TEXT("32 actual whole-body weapon focus cases; baseline records geometry, formal checks convergence; no animation source replacement"));
    }
    if (!Player.IsValid() || !Weapon.IsValid()) { Check(false,TEXT("focus audit lost production pawn/weapon")); return; }
    const int32 Case=Stage%Cases; // 当前枪的独立用例，不从生产瞄准状态推导计划。
    if (StageStart<0.f)
    {
        if (Case==0 && Stage>0 && !EquipWeapon(Stage/Cases)) return;
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics);
        Player->SetActorRotation(FRotator::ZeroRotator);
        APlayerController* Controller=CastChecked<APlayerController>(Player->GetController()); // 当前正式控制器，只输入真实控制旋转。
        FRotator View=Case==5?FRotator(35,30,0):Case==6?FRotator(-40,-30,0):FRotator::ZeroRotator; // 阈值内转头+俯仰，不能靠整身转向掩盖枪偏。
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakFixReview"))) View.Yaw+=(Stage%2==0?.3f:-.3f); // 每1.3秒真实转动视线，维持主动瞄准意图；不关待机配置、不改状态机或放宽精度阈值。
        Controller->SetControlRotation(View); Controller->UpdateRotation(0.f);
        Controller->SetViewTarget(bFirstPersonCapture?static_cast<AActor*>(Player.Get()):static_cast<AActor*>(Camera.Get()));
        MuzzleFocusWall->SetActorEnableCollision(Case!=3);
        MuzzleFocusWall->SetActorHiddenInGame(Case==3);
        StageName=FString::Printf(TEXT("%s_focus_%d"),*Weapon->GetWeaponId().ToString(),Case);
        StageStart=Now; NextSample=Now+.4f; NextCapture=Now+.9f;
        UE_LOG(LogFPSDemo,Display,TEXT("MUZZLE_FOCUS_CASE %s baseline=%d"),*StageName,bBaseline);
    }
    const FVector ViewOrigin=Player->GetSharedViewLocation(),ViewDirection=Player->GetBaseAimRotation().Vector(); // 同正式实体弹的稳定眼位/控制方向，不读取旁观摄影机。
    const float Distance=Case==0?200.f:Case==1?500.f:Case==2?1500.f:Case==4?30.f:Case==7?(Now-StageStart<.45f?1500.f:300.f):350.f; // cm；切换用例保留同一角色/武器，检查焦点状态不会锁死。
    MuzzleFocusWall->SetActorLocationAndRotation(ViewOrigin+ViewDirection*(Distance+2.5f),ViewDirection.Rotation()); // 板前表面位于给定距离，碰撞查询仍独立实测。
    const FVector BodyCenter=Player->GetActorLocation()+FVector(0,0,40); // 外部视角显示两手/枪/肩肘，Owner轮继续真正玩家镜头。
    const FVector CameraLocation=BodyCenter+FVector(150,-200,60);
    Camera->SetActorLocationAndRotation(CameraLocation,(BodyCenter-CameraLocation).Rotation());
    if (Now>=NextSample)
    {
        USkeletalMeshComponent* Gun=FindPresentationWeapon(); // Presentation已求值的实际共享展示枪。
        if (!Check(Gun && Gun->DoesSocketExist(Weapon->Config.MuzzleSocket),TEXT("focus audit actual shared muzzle available"))) return;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoMuzzleFocusAudit),false,Player.Get()); // 本轮独立求交；不读取生产节点目标作为预期。
        Query.AddIgnoredActor(Weapon.Get());
        FHitResult Hit; // 只采集真实表面接触，不调用伤害或发射。
        GetWorld()->LineTraceSingleByChannel(Hit,ViewOrigin,ViewOrigin+ViewDirection*Weapon->Config.Range,DemoEnemyHitZones::TraceChannel,Query);
        const FVector Target=Hit.bBlockingHit?Hit.ImpactPoint:ViewOrigin+ViewDirection*Weapon->Config.Range; // 无命中使用当前枪射程，不伪造命中。
        const FTransform Muzzle=Gun->GetSocketTransform(Weapon->Config.MuzzleSocket,RTS_World); // 世界厘米/完整旋转来自同帧渲染骨姿。
        const double Error=FocusAngle(Muzzle.GetRotation().GetForwardVector(),Target-Muzzle.GetLocation()); // 真正枪管到准星交点角差。
        const double ViewError=FocusAngle(Muzzle.GetRotation().GetForwardVector(),ViewDirection); // 修正角应有界；与射线交点误差分开。
        TSharedPtr<FJsonObject> Frame=MakeShared<FJsonObject>(); // 当前帧数值随报告保存，不持有World对象。
        Frame->SetStringField(TEXT("stage"),StageName); Frame->SetNumberField(TEXT("seconds"),Now-StageStart);
        Frame->SetNumberField(TEXT("focus_error_degrees"),Error); Frame->SetNumberField(TEXT("view_offset_degrees"),ViewError);
        Frame->SetNumberField(TEXT("hit_distance_cm"),FVector::Distance(ViewOrigin,Target)); Frame->SetBoolField(TEXT("obstruction_hit"),Hit.bBlockingHit);
        if (const UDemoPlayerAnimInstance* Anim=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance())) // 已求值实例只读，不把内部目标作为独立验收预期。
        {
            const FVector Snapshot=Player->GetMesh()->GetComponentTransform().TransformPosition(Anim->MuzzleFocusPoint); // 将任务前快照还原到当前世界用于定位时序。
            Frame->SetNumberField(TEXT("snapshot_error_degrees"),FocusAngle(Muzzle.GetRotation().GetForwardVector(),Snapshot-Muzzle.GetLocation()));
            Frame->SetNumberField(TEXT("snapshot_vs_ray_cm"),FVector::Distance(Snapshot,Target));
            Frame->SetNumberField(TEXT("alignment_weight"),Anim->AimAlignmentWeight);
            Frame->SetStringField(TEXT("upper_action"),Anim->UpperBodyAction.ToString());
            if (const UDemoLyraAnimInstance* Main=Cast<UDemoLyraAnimInstance>(Anim)) // 回归明确记录待机让权，不能用被待机偏移的Pose当作瞄准误差。
            {
                Frame->SetNumberField(TEXT("idle_intent_weight"),Main->IdleBreakMotionWeight);
                if (FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakFixReview")) && Now-StageStart>1.f && !Check(Main->IdleBreakMotionWeight<.001f,TEXT("real view input keeps cosmetic idle yielded during focus audit"))) return;
            }
        }
        Frame->SetNumberField(TEXT("root_scale_error"),(Player->GetMesh()->GetSocketTransform(TEXT("root"),RTS_Component).GetScale3D()-FVector::OneVector).GetAbsMax());
        for (const FName Hand:{FName(TEXT("hand_l")),FName(TEXT("hand_r"))}) // 同帧双腕相对实际枪框架；跨距离比较可检出IK不可达造成的脱握，不能只测枪管。
        {
            const FTransform Grip=Player->GetMesh()->GetSocketTransform(Hand,RTS_World).GetRelativeTransform(Gun->GetComponentTransform()); // 世界变换消去后的位置cm和旋转四元数。
            const FVector Position=Grip.GetLocation(); // 保存纯值，不持有骨姿缓冲引用。
            const FQuat Rotation=Grip.GetRotation(); // 同时记录腕方向，避免只验证位置漏掉手腕旋转。
            Frame->SetArrayField(Hand.ToString(),{MakeShared<FJsonValueNumber>(Position.X),MakeShared<FJsonValueNumber>(Position.Y),MakeShared<FJsonValueNumber>(Position.Z),MakeShared<FJsonValueNumber>(Rotation.X),MakeShared<FJsonValueNumber>(Rotation.Y),MakeShared<FJsonValueNumber>(Rotation.Z),MakeShared<FJsonValueNumber>(Rotation.W)});
        }
        Frames.Add(MakeShared<FJsonValueObject>(Frame));
        if (!Check(Hit.bBlockingHit==(Case!=3),TEXT("focus ray reaches planned actual obstruction/empty space"))) return;
        if (!bBaseline && Case!=4 && Now-StageStart>1.f && !Check(Error<.3,TEXT("settled visible muzzle converges to independent crosshair ray hit"))) return; // 2m长枪也必须通过，不能把固定点不收敛误当角度限幅。
        if (!bBaseline && !Check(ViewError<15.2,TEXT("near-wall correction stays bounded"))) return;
        NextSample=Now+.1f;
    }
    if (bCapture && FApp::CanEverRender() && Now>=NextCapture && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Directory=GetReviewRoot()/TEXT("Images"); // 每轮独立输出，不覆盖其他功能审片。
        IFileManager::Get().MakeDirectory(*Directory,true);
        FScreenshotRequest::RequestScreenshot(Directory/(StageName+TEXT(".png")),false,false); NextCapture=Now+10.f;
    }
    if (Now-StageStart<1.3f) return;
    if (++Stage==Cases*4)
    { UE_LOG(LogFPSDemo,Display,TEXT("MUZZLE_FOCUS_AUDIT_SUCCESS baseline=%d cases=32"),bBaseline); SaveAndExit(true); return; }
    StageStart=-1.f; // 下一帧再换枪/场景，确保本阶段截图已经完成。
}
