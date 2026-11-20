#include "Tests/DemoPlayerEyesTest.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/DemoPlayerEyesComponent.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/DemoLog.h"
// 几何验收报告记录实际资产 UObject 路径，需完整类型以安全调用 GetPathNameSafe。
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/HUD.h" // 隔离夹具隐藏自身Canvas HUD，避免中央安全区提示遮挡眼鼻；生产HUD不变。
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

ADemoPlayerEyesTest::ADemoPlayerEyesTest()
{
    DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
}
bool ADemoPlayerEyesTest::InitializeFixture()
{
    DEMO_LOG_CALL();
#if !WITH_EDITOR
    Finish(false,TEXT("requires isolated WITH_EDITOR runtime")); return false;
#else
    Controller=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前正式GameMode，只启动安全区不伪造战斗阶段。
    if (!Controller.IsValid() || !Mode || !Mode->StartRun()) { Finish(false,TEXT("formal hub startup failed")); return false; }
    Controller->OnRunReady(); Player=Cast<ADemoCharacter>(Controller->GetPawn());
    if (!Player.IsValid() || !Player->GetPlayerPresentation()->GetEyes() || !Player->GetPlayerPresentation()->GetEyes()->GetEyeMesh(true)
        || !Player->GetPlayerPresentation()->GetEyes()->GetEyeMesh(false))
    { Finish(false,TEXT("both configured eyes must exist after real player initialization")); return false; }
    AddTickPrerequisiteComponent(Player->GetPlayerPresentation()); // 已包含Body和Eyes依赖，实际同帧骨/组件值。
    Camera=GetWorld()->SpawnActor<ACameraActor>();
    if (!Camera) { Finish(false,TEXT("independent inspection camera missing")); return false; }
    Camera->GetCameraComponent()->SetFieldOfView(InspectionFov); // 拉宽并拉远测试取景，避免EyesCapture01中头顶/下巴裁切。
    Controller->SetViewTarget(Camera); // 眼神仍必须由Controller自身输入决定，不能跟此摄影机反向看回头部。
    if (AHUD* TestHUD=Controller->GetHUD()) // 借用当前隔离World的HUD实例；本专项退出进程，不修改类默认值或生产绘制逻辑。
    {
        TestHUD->bShowHUD=false;
        UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_EYES_CAPTURE_HUD hidden for isolated face inspection"));
    }
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("PlayerAcceptance/FaceFrames")),true);
    return true;
#endif
}
void ADemoPlayerEyesTest::BeginStage()
{
    DEMO_LOG_CALL();
    const FRotator Controls[]={FRotator::ZeroRotator,FRotator(0,-15,0),FRotator(0,15,0),FRotator(35,0,0),FRotator(-35,0,0),FRotator::ZeroRotator,FRotator(35,0,0),FRotator(-35,0,0)}; // 原六阶段后增加斜侧上/下视；镜头偏航不混入真实玩家输入。
    static_assert(UE_ARRAY_COUNT(Controls)==TestStageCount,"Every eye capture stage needs an explicit real input");
    DesiredControl=Controls[Stage]; Controller->SetControlRotation(DesiredControl);
    CameraOrbitYaw=Stage==6?35.f:(Stage==7?-35.f:0.f); // 仅摄影机围绕角色正前方转动；角色仍由真实ControlRotation更新身体Yaw。
    StageStart=NextSample=GetWorld()->GetTimeSeconds(); NextCapture=StageStart+.05f; StableSamples=0; // 新镜头至少跨过一个真实视图更新再请求图，避免把上一阶段缓存视点标成斜侧阶段。
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_EYES_CAPTURE_STAGE index=%d control=%s camera_orbit_yaw=%.1f"),Stage,*DesiredControl.ToString(),CameraOrbitYaw);
}
TSharedRef<FJsonObject> ADemoPlayerEyesTest::TransformJson(const FTransform& Value)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>(); // 本帧独立值对象，JSON持有不引用组件。
    const FMatrix Matrix=Value.ToMatrixWithScale().GetTransposed(); // 转列向量约定，与已有几何验收工具兼容。
    TArray<TSharedPtr<FJsonValue>> Entries; // 4x4按行flatten，厘米位置位于列3。
    for (int32 Row=0;Row<4;++Row)
        for (int32 Column=0;Column<4;++Column) Entries.Add(MakeShared<FJsonValueNumber>(Matrix.M[Row][Column]));
    Result->SetArrayField(TEXT("matrix4x4_column_vector"),Entries);
    return Result;
}
bool ADemoPlayerEyesTest::RecordFrame(float Elapsed)
{
    DEMO_LOG_TICK();
    UDemoPlayerEyesComponent* Eyes=Player->GetPlayerPresentation()->GetEyes(); // 同Pawn当前真实展示，不注入动画或眼神数据。
    const UDemoPlayerAppearance* Settings=Player->GetPlayerPresentation()->GetAppearance(); // 实际角限与rest配置。
    const FRotator ActualControl=Controller->GetControlRotation(); // 与独立ViewTarget并存的玩家输入。
    const FVector Aim=Eyes->GetAimDirection(); // 生产组件上一Tick刚更新的世界方向。
    const AHUD* TestHUD=Controller->GetHUD(); // 本帧只读借用，断言取景HUD没有被其它流程重新开启。
    const bool bHUDHidden=!TestHUD || !TestHUD->bShowHUD; // 没有HUD也满足无遮挡条件，不创建额外UI替代。
    const float AimCameraDot=Aim|Camera->GetActorForwardVector(); // 仅检验镜头仍朝脸，不作为眼神方向真值。
    TSharedRef<FJsonObject> Frame=MakeShared<FJsonObject>(); // 本次完整独立数据值。
    Frame->SetNumberField(TEXT("stage"),Stage); Frame->SetNumberField(TEXT("seconds"),Elapsed);
    Frame->SetObjectField(TEXT("head_world"),TransformJson(Player->GetMesh()->GetSocketTransform(TEXT("head"),RTS_World)));
    Frame->SetObjectField(TEXT("control_world"),TransformJson(FTransform(ActualControl)));
    Frame->SetObjectField(TEXT("desired_control_world"),TransformJson(FTransform(DesiredControl))); // 独立记录本阶段预定输入，避免只比较两个同样错误的派生方向。
    Frame->SetObjectField(TEXT("camera_world"),TransformJson(Camera->GetActorTransform()));
    Frame->SetObjectField(TEXT("body_world"),TransformJson(Player->GetMesh()->GetComponentTransform()));
    Frame->SetNumberField(TEXT("aim_x"),Aim.X); Frame->SetNumberField(TEXT("aim_y"),Aim.Y); Frame->SetNumberField(TEXT("aim_z"),Aim.Z);
    Frame->SetNumberField(TEXT("camera_orbit_yaw"),CameraOrbitYaw); Frame->SetNumberField(TEXT("aim_camera_dot"),AimCameraDot); Frame->SetBoolField(TEXT("test_hud_hidden"),bHUDHidden);
    bool bValid=Player->bUseControllerRotationYaw && Aim.Equals(ActualControl.Vector(),.001f) && bHUDHidden; // 不冻结身体制造眼珠左右旋转，也不从摄影机读反向凝视。
    for (bool bLeft:{true,false}) // 两眼逐一检查并输出真实矩阵。
    {
        UStaticMeshComponent* Eye=Eyes->GetEyeMesh(bLeft); // 配置EyeMesh已在初始化验证，持续消失应失败。
        if (!Eye) return false;
        const FRotator Current=Eyes->GetCurrentRotation(bLeft), Target=Eyes->GetTargetRotation(bLeft); // 相对各自rest的角，单位度。
        TSharedRef<FJsonObject> EyeData=TransformJson(Eye->GetComponentTransform()); // 实际渲染组件世界矩阵。
        EyeData->SetObjectField(TEXT("relative"),TransformJson(Eye->GetRelativeTransform()));
        EyeData->SetNumberField(TEXT("yaw"),Current.Yaw); EyeData->SetNumberField(TEXT("pitch"),Current.Pitch);
        EyeData->SetNumberField(TEXT("target_yaw"),Target.Yaw); EyeData->SetNumberField(TEXT("target_pitch"),Target.Pitch);
        Frame->SetObjectField(bLeft?TEXT("left_eye"):TEXT("right_eye"),EyeData);
        bValid&=Eye->bOwnerNoSee && !Eye->bOnlyOwnerSee && !Eye->bHiddenInGame && Eye->GetCollisionEnabled()==ECollisionEnabled::NoCollision
            && FMath::Abs(Current.Yaw)<=Settings->EyeMaxYaw+.001f && FMath::Abs(Current.Pitch)<=Settings->EyeMaxPitch+.001f;
        if (Elapsed>.6f) bValid&=FMath::Abs(Current.Yaw-Target.Yaw)<.2f && FMath::Abs(Current.Pitch-Target.Pitch)<.2f; // 固定静止瞄准下真实平滑应已收敛，而不是仅发出请求。
    }
    if (Elapsed>.6f)
    {
        const float MaximumAimCameraDot=Stage>=6?-.6f:-.8f; // 斜侧35度且控制Pitch±35度时反向点积约-0.67；旧正面-0.8只适用于原六阶段。
        bValid&=ActualControl.Equals(DesiredControl,.01f) && Aim.Equals(DesiredControl.Vector(),.001f) && AimCameraDot<MaximumAimCameraDot; // 真值来自实际预定控制输入；摄影机只单独断言在对面，防止ViewTarget方向被误当Aim。
        if (bValid) ++StableSamples;
    }
    Frame->SetBoolField(TEXT("valid"),bValid); Frames.Add(MakeShared<FJsonValueObject>(Frame));
    return bValid || Elapsed<=.6f; // 输入/身体初始转动允许稳定窗口，仍记录过渡帧所有数据。
}
void ADemoPlayerEyesTest::Finish(bool bSuccess,const FString& Reason)
{
    DEMO_LOG_CALL(); if (bFinished) return;
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 专属短眼神测试报告，不覆盖808suite历史证据。
    Report->SetBoolField(TEXT("runtime_passed"),bSuccess); Report->SetStringField(TEXT("reason"),Reason);
    const UDemoPlayerPresentationComponent* Presentation=Player.IsValid()?Player->GetPlayerPresentation():nullptr; // 结束时读取真正被采样Pawn的资产身份，几何导出不能用当前DA猜采集资产。
    const UDemoPlayerAppearance* Appearance=Presentation?Presentation->GetAppearance():nullptr; // 只读当前外观闭包；缺失以空路径明确记录，导出器必须拒绝。
    Report->SetStringField(TEXT("body_mesh"),Player.IsValid()?GetPathNameSafe(Player->GetMesh()->GetSkeletalMeshAsset()):FString());
    Report->SetStringField(TEXT("eye_mesh"),Presentation && Presentation->GetEyes() && Presentation->GetEyes()->GetEyeMesh(true)?GetPathNameSafe(Presentation->GetEyes()->GetEyeMesh(true)->GetStaticMesh()):FString());
    Report->SetStringField(TEXT("appearance_asset"),GetPathNameSafe(Appearance)); // 包SHA由root在捕获前后独立核验并绑定runtime JSON哈希，测试不读取作者源。
    Report->SetStringField(TEXT("scope"),TEXT("eight local real ControlRotation stages: six frontal plus +/-35 degree oblique cameras with real pitch +/-35; body yaw remains enabled; isolated HUD hidden; independent 36 degree FOV camera at 130cm; snapshots require separate visual review; LAN uses the separate nineteen-stage network probe"));
    Report->SetNumberField(TEXT("stage_count"),TestStageCount); Report->SetNumberField(TEXT("inspection_fov_degrees"),InspectionFov); Report->SetNumberField(TEXT("inspection_distance_cm"),InspectionDistance); // 取景参数进入报告，不能用旧六阶段截图冒充新版可读取景。
    Report->SetNumberField(TEXT("screenshot_requests"),ScreenshotIndex); Report->SetArrayField(TEXT("frames"),Frames);
    FString Json; // 游戏线程同步序列化，结束前完成写入。
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json); // 仅当前栈有效，无异步捕获。
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir()/TEXT("PlayerAcceptance")),true);
    if (!FJsonSerializer::Serialize(Report,Writer) || !FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("PlayerAcceptance/eyes_capture.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bSuccess=false;
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_EYES_CAPTURE_%s %s"),bSuccess?TEXT("SUCCESS"):TEXT("FAILED"),*Reason);
    bFinished=true; Stage=bSuccess?100:101; ExitAt=FPlatformTime::Seconds()+2.0; // 允许最后一张RHI截图完成后安全退出本测试进程。
}
void ADemoPlayerEyesTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (bFinished) { if (FPlatformTime::Seconds()>=ExitAt) FPlatformMisc::RequestExitWithStatus(false,Stage==100?0:1); return; }
    const float Now=GetWorld()->GetTimeSeconds(); // 只用当前World单调时钟，初始化前等正式服务建立。
    if (Now<1.f) return;
    if (Stage<0) { if (!InitializeFixture()) return; Stage=0; BeginStage(); }
    if (!Player.IsValid() || !Controller.IsValid() || !Camera) { Finish(false,TEXT("test pawn/controller/camera disappeared")); return; }
    UDemoPlayerEyesComponent* Eyes=Player->GetPlayerPresentation()->GetEyes(); // 真实组件位置作为取景中心，不手工猜head高度。
    const FVector Focus=(Eyes->GetEyeMesh(true)->GetComponentLocation()+Eyes->GetEyeMesh(false)->GetComponentLocation())*.5+FVector(0,0,-3.f); // 双眼以下3cm作为完整头脸构图中心，不改变头/眼组件位置。
    const FVector OrbitDirection=Player->GetActorForwardVector().RotateAngleAxis(CameraOrbitYaw,FVector::UpVector); // 观察者方位随真正身体Yaw移动；±35度提供三分之四侧视，不冻结或改写身体。
    const FVector Position=Focus+OrbitDirection*InspectionDistance+FVector(0,0,2.f); // 130cm水平视距和2cm观察高度差，只改变本测试Camera。
    Camera->SetActorLocationAndRotation(Position,(Focus-Position).Rotation());
    if (Now+.0001f>=NextSample)
    { if (!RecordFrame(Now-StageStart)) { Finish(false,TEXT("eye gaze/visibility/clamp/smoothing contract failed")); return; } NextSample=Now+1.f/30.f; }
    if (FApp::CanEverRender() && Now+.0001f>=NextCapture && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Path=FPaths::ProjectSavedDir()/TEXT("PlayerAcceptance/FaceFrames")/FString::Printf(TEXT("%02d_Eyes_%05d.png"),Stage,ScreenshotIndex++); // 8Hz实际采样，内部编号不受外部路径输入控制。
        FScreenshotRequest::RequestScreenshot(Path,false,false); NextCapture=Now+.125f;
    }
    if (Now-StageStart>=1.6f)
    {
        if (StableSamples<12) { Finish(false,TEXT("too few stable real-eye observations")); return; }
        if (++Stage>=TestStageCount) Finish(true,TEXT("eight actual aim stages passed including oblique up/down; readable RHI visual review pending")); else BeginStage();
    }
}
