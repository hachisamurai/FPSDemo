// 职责：两端实际Pawn/骨姿/开火及观战观测；准备阶段只读正式UI结果，不代替核心选择、Ready或房间ACK。
#include "Tests/DemoPlayerNetworkTest.h"
#include "Tests/DemoNetworkProbeSetup.h"
#include "AI/DemoEnemy.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoPlayerAimSettings.h" // 当前视角阈值取同一只读配置，联网测试不再请求已禁止的60度抬头。
#include "Animation/DemoPlayerAppearance.h" // 眼球限制从真实DA读取，不在测试复制另一套默认值。
#include "Animation/DemoPlayerEyesComponent.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Camera/CameraActor.h" // 可选LAN截图只使用独立观察Actor，不改变Pawn相机或控制方向。
#include "Camera/CameraComponent.h" // 本测试相机FOV固定50度，不写生产相机配置。
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h" // 胶囊世界变换是真实移动基准，不能拿Mesh自身位置当期望。
#include "Components/StaticMeshComponent.h" // 实际眼球组件父骨、可见性及碰撞断言。
#include "GameFramework/Controller.h" // 测试输入进入真实ControlRotation/Movement复制，不改RemoteViewPitch。
#include "Debug/DemoLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h" // 鞋底逐点查询本World真实静态地面，不使用测试写入的地面Z。
#include "EngineUtils.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoEffects.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h" // 显式截图模式必须有真实RHI，NullRHI只能执行原数值专项。
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/Coop/DemoCoopSpectatorCamera.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Player/DemoPlayerController.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

ADemoPlayerNetworkTest::ADemoPlayerNetworkTest()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    bReplicates=true; bAlwaysRelevant=true; NetUpdateFrequency=30.f;
    bYawAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerYawNetworkTest")); // 两端均显式运行水平专项时才扩展计划，不改普通游戏。
#if WITH_EDITOR
    SetupWaitSeconds=DemoNetworkProbeSetup::ReadTimeoutSeconds(); // 仅探针准备预算；实际加载仍受生产30秒门禁控制。
#endif
}
bool ADemoPlayerNetworkTest::ReadTint(ADemoCharacter* Pawn, FLinearColor& Tint)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 静态只读函数没有UObject this。
    if (!Pawn || !Pawn->GetMesh()) return false;
    const int32 Slot=Pawn->GetMesh()->GetMaterialIndex(TEXT("OutfitPrimaryColor")); // 槽名是导入契约，不假定材质数组序号。
    UMaterialInstanceDynamic* Material=Slot>=0?Cast<UMaterialInstanceDynamic>(Pawn->GetMesh()->GetMaterial(Slot)):nullptr; // 本Pawn独有MID，绝不写共享材质。
    if (!Material) return false;
    Tint=Material->K2_GetVectorParameterValue(TEXT("OutfitPrimaryColor"));
    return true;
}
ADemoCharacter* ADemoPlayerNetworkTest::GetSubject() const
{
    DEMO_LOG_TICK();
    return (Stage>=4 && Stage<=6) || (Stage>=14 && Stage<=18) || (Stage>=29 && Stage<=38)?RemotePawn.Get():HostPawn.Get(); // 新增29..38为远端拥有者水平阈值序列。
}
FRotator ADemoPlayerNetworkTest::GetEyeStageAim() const
{
    DEMO_LOG_TICK();
    if (bYawAudit && Stage>=19 && Stage<=38) return GetYawStageAim(); // 新专项保留前十九阶段，追加真实头身分离边界。
    const int32 Direction=(Stage-9)%5; // 9..13/14..18各同一组方向，不复用本地相机朝向作为期望。
    if (Direction==1) return FRotator(0,-60,0);
    if (Direction==2) return FRotator(0,60,0);
    if (Direction==3) return FRotator(GetDefault<UDemoPlayerAimSettings>()->LookUpDegrees,0,0);
    if (Direction==4) return FRotator(-GetDefault<UDemoPlayerAimSettings>()->LookDownDegrees,0,0);
    return FRotator::ZeroRotator;
}
bool ADemoPlayerNetworkTest::ValidateEyeSolver() const
{
    DEMO_LOG_CALL();
    if (!HostPawn || !HostPawn->GetPlayerPresentation()->GetAppearance()) return false;
    const UDemoPlayerAppearance* Settings=HostPawn->GetPlayerPresentation()->GetAppearance(); // 本次真实资产的角限值。
    const FTransform Rest(FRotator(13,37,9),FVector(7,11,19)); // 故意用非标准参考轴验证逆空间，而非只检查单位框架。
    const FRotator Inputs[]={FRotator::ZeroRotator,FRotator(0,-80,0),FRotator(0,80,0),FRotator(80,0,0),FRotator(-80,0,0),FRotator(70,-75,0)}; // 前/左右/上下/组合的真实向量输入。
    const FRotator Expected[]={FRotator::ZeroRotator,FRotator(0,-Settings->EyeMaxYaw,0),FRotator(0,Settings->EyeMaxYaw,0),FRotator(Settings->EyeMaxPitch,0,0),FRotator(-Settings->EyeMaxPitch,0,0),FRotator(Settings->EyeMaxPitch,-Settings->EyeMaxYaw,0)}; // 边界值来自配置，独立于函数输出。
    for (int32 Index=0;Index<UE_ARRAY_COUNT(Inputs);++Index) // 同步纯值检查，不影响动画/瞄准状态。
        if (!UDemoPlayerEyesComponent::SolveTarget(Rest,Rest.TransformVectorNoScale(Inputs[Index].Vector()),Settings->EyeMaxYaw,Settings->EyeMaxPitch).Equals(Expected[Index],.001f)) return false;
    return true;
}
TSharedRef<FJsonObject> ADemoPlayerNetworkTest::PlacementTransformJson(const FTransform& Value)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>(); // 独立值对象，归报告持有，不延长组件生命周期。
    const FVector Location=Value.GetLocation(),Scale=Value.GetScale3D(); // 世界/局部空间由调用字段名明确，位置单位cm。
    const FQuat Rotation=Value.GetRotation(); // 原xyzw四元数，避免欧拉角跨±180产生误导。
    TArray<TSharedPtr<FJsonValue>> PositionRows,RotationRows,ScaleRows; // 紧凑数值数组，支持独立复算Mesh相对胶囊变换。
    for (double Number:{Location.X,Location.Y,Location.Z}) PositionRows.Add(MakeShared<FJsonValueNumber>(Number)); // 当前分量立即复制到JSON。
    for (double Number:{Rotation.X,Rotation.Y,Rotation.Z,Rotation.W}) RotationRows.Add(MakeShared<FJsonValueNumber>(Number)); // xyzw顺序保持UE数值。
    for (double Number:{Scale.X,Scale.Y,Scale.Z}) ScaleRows.Add(MakeShared<FJsonValueNumber>(Number)); // 单位缩放也显式记录。
    Result->SetArrayField(TEXT("translation_cm"),PositionRows); Result->SetArrayField(TEXT("rotation_xyzw"),RotationRows); Result->SetArrayField(TEXT("scale"),ScaleRows);
    return Result;
}
bool ADemoPlayerNetworkTest::MeasureSoleFloor(ADemoCharacter* Subject, bool bLeft, FDemoNetworkSoleMeasurement& Out)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const UDemoPlayerAppearance* Settings=Subject->GetPlayerPresentation()->GetAppearance(); // 只借用当前实际模型导入时提取的Shoe最低顶点及完整双骨权重。
    USkeletalMeshComponent* Body=Subject->GetMesh(); // 必须使用网络平滑之后真正的组件世界变换。
    const TArray<FDemoPlayerSolePoint>& Points=bLeft?Settings->LeftSole:Settings->RightSole; // 左右分别验，不用足骨或胶囊底代替鞋底。
    const FName FootName=bLeft?TEXT("foot_l"):TEXT("foot_r"),BallName=bLeft?TEXT("ball_l"):TEXT("ball_r"); // 固定真实小白人骨名。
    if (Points.Num()<4 || Body->GetBoneIndex(FootName)==INDEX_NONE || Body->GetBoneIndex(BallName)==INDEX_NONE) return false;
    const FFindFloorResult& Support=Subject->GetCharacterMovement()->CurrentFloor; // 原生胶囊的真实支撑面，绝不由测试射线写回或凭胶囊底推造。
    const FVector SupportPoint=Support.HitResult.ImpactPoint,SupportNormal=Support.HitResult.ImpactNormal.GetSafeNormal(); // 与脚下射线不同位置的平面，按每个鞋点XY解出真实支撑Z。
    if (!Support.bBlockingHit || !Support.IsWalkableFloor() || !Support.HitResult.bBlockingHit || !Support.HitResult.GetComponent()
        || Support.HitResult.GetComponent()->GetCollisionObjectType()!=ECC_WorldStatic || SupportPoint.ContainsNaN() || SupportNormal.ContainsNaN() || SupportNormal.Z<=.95f) return false; // 本专项只接受实际近水平静态支撑，缺失不能凭高度常量补偿。
    const FTransform Foot=Body->GetSocketTransform(FootName,RTS_World),Ball=Body->GetSocketTransform(BallName,RTS_World); // 包含最终骨Pose与整个Mesh世界偏移，正是旧探针遗漏的一层。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PlayerNetworkSole),true,Subject); // 同步只读复杂射线，忽略自身且不修改碰撞设置。
    const FCollisionObjectQueryParams Objects(ECC_WorldStatic); // 本专项白模静态地面；排除其它玩家/展示枪成为伪造支撑面。
    Out=FDemoNetworkSoleMeasurement(); Out.GroundHeight=TNumericLimits<float>::Max(); // 每次只保留本次真实测量，不沿用上一帧表面差。
    for (const FDemoPlayerSolePoint& Point:Points) // 所有真实最低鞋底点都测，不能只选恰好贴地的单一足骨。
    {
        if (!FMath::IsFinite(Point.FootWeight) || Point.FootWeight<0.f || Point.FootWeight>1.f || Point.FootLocalPosition.ContainsNaN() || Point.BallLocalPosition.ContainsNaN()) return false;
        const FVector WorldPoint=Foot.TransformPosition(Point.FootLocalPosition)*Point.FootWeight+Ball.TransformPosition(Point.BallLocalPosition)*(1.f-Point.FootWeight); // 与真实鞋双骨LBS一致，含整个Mesh变换。
        const FVector Start(WorldPoint.X,WorldPoint.Y,Subject->GetCapsuleComponent()->GetComponentLocation().Z+20.f); // 从真实胶囊上方测地面，错误悬空Mesh不会把射线起点一起抬走。
        FHitResult Hit; // 本鞋点的真实World碰撞结果，函数结束后不持有地面组件。
        if (!Subject->GetWorld()->LineTraceSingleByObjectType(Hit,Start,Start-FVector(0,0,500.f),Objects,Query) || !Hit.bBlockingHit || Hit.ImpactNormal.GetSafeNormal().Z<=.95f) return false; // 支撑和脚下两面都须近水平，不能把台阶侧面当作合法高度补偿。
        const float Height=static_cast<float>(WorldPoint.Z-Hit.ImpactPoint.Z); // 世界厘米高度，负值为真正鞋底穿地。
        const double SupportZ=SupportPoint.Z-(SupportNormal.X*(WorldPoint.X-SupportPoint.X)+SupportNormal.Y*(WorldPoint.Y-SupportPoint.Y))/SupportNormal.Z; // 同一个真实鞋点XY的支撑平面Z，非水平面也不使用常量ImpactPoint.Z冒充。
        const float SurfaceDelta=static_cast<float>(SupportZ-Hit.ImpactPoint.Z),SupportHeight=static_cast<float>(WorldPoint.Z-SupportZ); // 同点恒等式GroundHeight=SupportHeight+SurfaceDelta，保留全部真实高度。
        if (!FMath::IsFinite(Height) || !FMath::IsFinite(SurfaceDelta) || !FMath::IsFinite(SupportHeight)) return false;
        Out.MaximumAbsoluteSurfaceDelta=FMath::Max(Out.MaximumAbsoluteSurfaceDelta,FMath::Abs(SurfaceDelta)); // 全部角点均约束，不只挑地形高差最小的好点。
        if (Height<Out.GroundHeight) { Out.GroundHeight=Height; Out.SupportHeight=SupportHeight; Out.SurfaceDelta=SurfaceDelta; Out.LowestWorld=WorldPoint; Out.GroundWorld=Hit.ImpactPoint; }
    }
    return true;
}
TSharedRef<FJsonObject> ADemoPlayerNetworkTest::PlacementHitJson(const FHitResult& Hit)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>(); // 只冻结真实碰撞结果数值与对象路径，不保活地面Actor。
    const UPrimitiveComponent* Component=Hit.GetComponent(); // 当前游戏线程借用；射线没命中时允许为空。
    Result->SetBoolField(TEXT("blocking_hit"),Hit.bBlockingHit); Result->SetBoolField(TEXT("start_penetrating"),Hit.bStartPenetrating);
    Result->SetStringField(TEXT("impact_point_cm"),Hit.ImpactPoint.ToString()); Result->SetStringField(TEXT("impact_normal"),Hit.ImpactNormal.ToString()); Result->SetStringField(TEXT("location_cm"),Hit.Location.ToString());
    Result->SetStringField(TEXT("component"),GetPathNameSafe(Component)); Result->SetStringField(TEXT("actor"),GetPathNameSafe(Hit.GetActor()));
    Result->SetNumberField(TEXT("object_type"),Component?static_cast<int32>(Component->GetCollisionObjectType()):-1); Result->SetNumberField(TEXT("face_index"),Hit.FaceIndex);
    Result->SetNumberField(TEXT("distance_cm"),Hit.Distance); Result->SetNumberField(TEXT("penetration_depth_cm"),Hit.PenetrationDepth);
    return Result;
}
TSharedRef<FJsonObject> ADemoPlayerNetworkTest::PlacementFloorJson(ADemoCharacter* Subject, bool bSoleMeasured, const FVector& LeftWorld, const FVector& RightWorld)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const UCapsuleComponent* Capsule=Subject->GetCapsuleComponent(); // 调用前已验证存在，胶囊是实际Movement支撑形状。
    const UCharacterMovementComponent* Movement=Subject->GetCharacterMovement(); // 只读本端原生地面缓存，不用测试射线替换CurrentFloor。
    const FFindFloorResult& Floor=Movement->CurrentFloor; // 复制前本Tick有效的碰撞结果，保留扫掠/直线判定差异。
    TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>(); // 此对象仅在10Hz采样路径生成，不增加每Tick对比射线成本。
    Result->SetObjectField(TEXT("current_floor_hit"),PlacementHitJson(Floor.HitResult)); Result->SetBoolField(TEXT("current_floor_blocking"),Floor.bBlockingHit);
    Result->SetBoolField(TEXT("current_floor_walkable"),Floor.IsWalkableFloor()); Result->SetBoolField(TEXT("current_floor_line_trace"),Floor.bLineTrace);
    Result->SetNumberField(TEXT("floor_dist_cm"),Floor.FloorDist); Result->SetNumberField(TEXT("line_dist_cm"),Floor.LineDist);
    Result->SetNumberField(TEXT("capsule_scaled_half_height_cm"),Capsule->GetScaledCapsuleHalfHeight()); Result->SetNumberField(TEXT("capsule_scaled_radius_cm"),Capsule->GetScaledCapsuleRadius());
    Result->SetNumberField(TEXT("capsule_unscaled_half_height_cm"),Capsule->GetUnscaledCapsuleHalfHeight()); Result->SetStringField(TEXT("capsule_bottom_world_cm"),(Capsule->GetComponentLocation()-Capsule->GetUpVector()*Capsule->GetScaledCapsuleHalfHeight()).ToString());
    const FVector Points[]={Capsule->GetComponentLocation(),LeftWorld,RightWorld}; // 胶囊中心和两脚同XY对比可区分胶囊跨在窄凸条上与碰撞模式高度差。
    const TCHAR* Names[]={TEXT("capsule_center"),TEXT("left_sole"),TEXT("right_sole")}; // 与Points固定同序，不能把中心射线高度冒充鞋底高度。
    TArray<TSharedPtr<FJsonValue>> Probes; // 每点保留simple/complex两份实际命中，失败射线也归档。
    const FCollisionObjectQueryParams Objects(ECC_WorldStatic); // 与鞋底原断言相同WorldStatic集合，不改变碰撞或增加支撑物。
    for (int32 PointIndex=0;PointIndex<(bSoleMeasured?3:1);++PointIndex) // 鞋点未成功时只记录真实胶囊中心，避免用零向量发伪造查询。
    {
        const FVector Start(Points[PointIndex].X,Points[PointIndex].Y,Capsule->GetComponentLocation().Z+20.f),End=Start-FVector(0,0,500.f); // 两种查询共享相同实际World射线。
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PlayerNetworkFloorCompare),false,Subject); // 同步查询只改变局部参数，不改变组件CollisionTraceFlag。
        Query.bReturnFaceIndex=true; // 三角面索引便于独立确认白模网格线；simple命中允许INDEX_NONE。
        FHitResult SimpleHit,ComplexHit; // 同点的两种真实结果，保留不命中而不推算默认地面。
        const bool bSimple=Subject->GetWorld()->LineTraceSingleByObjectType(SimpleHit,Start,End,Objects,Query); // 初始bTraceComplex=false。
        Query.bTraceComplex=true;
        const bool bComplex=Subject->GetWorld()->LineTraceSingleByObjectType(ComplexHit,Start,End,Objects,Query); // 唯一差别为复杂查询标志。
        TSharedRef<FJsonObject> Probe=MakeShared<FJsonObject>(); // 当前点结果在返回报告中独立保留，不写入角色。
        Probe->SetStringField(TEXT("point"),Names[PointIndex]); Probe->SetStringField(TEXT("anchor_world_cm"),Points[PointIndex].ToString());
        Probe->SetObjectField(TEXT("simple"),PlacementHitJson(SimpleHit)); Probe->SetObjectField(TEXT("complex"),PlacementHitJson(ComplexHit));
        Probe->SetBoolField(TEXT("simple_returned_hit"),bSimple); Probe->SetBoolField(TEXT("complex_returned_hit"),bComplex);
        if (bSimple && bComplex) Probe->SetNumberField(TEXT("simple_minus_complex_z_cm"),SimpleHit.ImpactPoint.Z-ComplexHit.ImpactPoint.Z); // 同点差值解释碰撞模式差异，不改通过门槛。
        if (bSimple) Probe->SetNumberField(TEXT("anchor_above_simple_cm"),Points[PointIndex].Z-SimpleHit.ImpactPoint.Z);
        if (bComplex) Probe->SetNumberField(TEXT("anchor_above_complex_cm"),Points[PointIndex].Z-ComplexHit.ImpactPoint.Z);
        Probes.Add(MakeShared<FJsonValueObject>(Probe));
    }
    Result->SetArrayField(TEXT("ground_probes"),Probes);
    return Result;
}
bool ADemoPlayerNetworkTest::BeginCaptureStage()
{
    DEMO_LOG_CALL();
    if (!bCaptureRequested || Stage<9 || Stage>18) return true; // 0..8严格保留原ViewTarget，先完整执行生产死亡观战和清关复活。
    const bool bExplicitEndpoint=FParse::Param(FCommandLine::Get(),HasAuthority()?TEXT("DemoPlayerNetworkTest"):TEXT("DemoPlayerNetworkClient")); // 每端需有自己的显式专项开关，不能借复制Actor在普通进程截图。
    if (!bExplicitEndpoint || (GetNetMode()!=NM_ListenServer && GetNetMode()!=NM_Client) || !FApp::CanEverRender() || FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))
    { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_CAPTURE_REJECTED stage=%d requires explicit LAN endpoint with real RHI"),Stage); return false; }
    ADemoPlayerController* Local=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 只改本端LocalPC ViewTarget，不访问远端私有PC。
    if (!Local || !Local->IsLocalController() || !HostPawn || !RemotePawn || !HostPawn->GetCapsuleComponent() || !RemotePawn->GetCapsuleComponent())
    { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_CAPTURE_REJECTED stage=%d local controller or real capsule missing"),Stage); return false; }
    // 清场复活完成后仍有真实必选奖励UI；通过本人正式命令领取第0项再拍摄，不能隐藏HUD或改菜单字段伪造无UI画面。
    // 仅可选Capture的阶段9执行，奖励归当前Editor临时局；不影响先前射击/换弹/死亡/复活断言，也不修改角色朝向或骨姿。
    if (Stage==9 && Local->IsRewardMenu()) { UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_NETWORK_CAPTURE_REWARD endpoint=%s choice=0"),HasAuthority()?TEXT("host"):TEXT("client")); Local->SelectUpgrade(0); }
    if (!CaptureCamera)
    {
        FActorSpawnParameters Parameters; // 无Owner、无复制的瞬时观察Actor归World管理，避免OwnerNoSee被伪造或资产被保存。
        Parameters.ObjectFlags|=RF_Transient; Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        CaptureCamera=GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),FTransform::Identity,Parameters);
    }
    if (!CaptureCamera) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_CAPTURE_REJECTED stage=%d camera spawn failed"),Stage); return false; }
    const FVector Center=(HostPawn->GetCapsuleComponent()->GetComponentLocation()+RemotePawn->GetCapsuleComponent()->GetComponentLocation())*.5; // 两位真实Pawn当前胶囊中心的世界平均值，不移动任何Pawn。
    const FVector Position=Center+FVector(400,600,120),Focus=Center+FVector(0,0,25); // 厘米；同时容纳双方Body和脚下地面，观察方向不参与眼神/朝向真值。
    CaptureCamera->SetActorLocationAndRotation(Position,(Focus-Position).Rotation()); CaptureCamera->GetCameraComponent()->SetFieldOfView(50.f);
    Local->SetViewTarget(CaptureCamera); // 只改变测试摄影机，真实ControlRotation、Movement、GAS和所有网格可见性旗标保持生产行为。
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("PlayerAcceptance/NetworkFrames"); // 固定测试输出目录，两端文件名互不覆盖。
    if (!IFileManager::Get().MakeDirectory(*Directory,true)) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_CAPTURE_REJECTED cannot create %s"),*Directory); return false; }
    StageCapturePath=Directory/FString::Printf(TEXT("%s_stage_%02d.png"),HasAuthority()?TEXT("host"):TEXT("client"),Stage);
    bCaptureStageReady=true;
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_NETWORK_CAPTURE_CAMERA endpoint=%s stage=%d location=%s target=%s fov=50"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,*Position.ToString(),*Focus.ToString());
    return true;
}
bool ADemoPlayerNetworkTest::CaptureIfReady(float Elapsed)
{
    DEMO_LOG_TICK();
    if (!bCaptureRequested || Stage<9 || Stage>18) return true;
    if (!bCaptureStageReady && !BeginCaptureStage()) return false;
    ADemoPlayerController* Local=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 读本端真实ViewTarget，意外被生产流程切走必须报告而不是每Tick强行改回。
    if (!Local || Local->GetViewTarget()!=CaptureCamera) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_CAPTURE_REJECTED stage=%d inspection ViewTarget lost"),Stage); return false; }
    const bool bBothSettled=HostPawn->GetCharacterMovement()->bNetworkSmoothingComplete && RemotePawn->GetCharacterMovement()->bNetworkSmoothingComplete
        && HostPawn->GetVelocity().Size()<1.f && RemotePawn->GetVelocity().Size()<1.f && StablePlacementSamples[0]>=12 && StablePlacementSamples[1]>=12; // 与原观测共享真实完成证据，不宣布网络平滑完成或修改姿态。
    if (Elapsed<2.1f || bStageScreenshotRequested || !bBothSettled || Local->IsUpgradeMenuOpen() || FScreenshotRequest::IsScreenshotRequested()) return true; // 等本人奖励真实响应关闭UI，超时缺图仍失败，不靠隐藏Canvas遮掩问题。
    StageCapturePreviousTimestamp=IFileManager::Get().GetTimeStamp(*StageCapturePath); StageCaptureRequestedAt=FDateTime::UtcNow(); // 保存旧文件时间后才请求，避免同名旧PNG误通过。
    FScreenshotRequest::RequestScreenshot(StageCapturePath,false,false); bStageScreenshotRequested=true;
    TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>(); // 仅截图请求/实际相机元数据；不创建自动视觉通过标记。
    Row->SetNumberField(TEXT("stage"),Stage); Row->SetNumberField(TEXT("seconds"),Elapsed); Row->SetStringField(TEXT("path"),StageCapturePath); Row->SetStringField(TEXT("request_utc"),StageCaptureRequestedAt.ToIso8601());
    Row->SetStringField(TEXT("previous_file_timestamp_utc"),StageCapturePreviousTimestamp.ToIso8601()); // 仅识别本次是否真的更新文件，不据文件mtime推算动画时基。
    Row->SetObjectField(TEXT("camera_world"),PlacementTransformJson(CaptureCamera->GetActorTransform())); Row->SetNumberField(TEXT("fov_degrees"),CaptureCamera->GetCameraComponent()->FieldOfView);
    Row->SetStringField(TEXT("view_target"),GetPathNameSafe(Local->GetViewTarget())); Row->SetStringField(TEXT("control_rotation"),Local->GetControlRotation().ToString());
    CaptureRecords.Add(MakeShared<FJsonValueObject>(Row));
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_NETWORK_CAPTURE_REQUEST endpoint=%s stage=%d elapsed=%.3f path=%s"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,Elapsed,*StageCapturePath);
    return true;
}
bool ADemoPlayerNetworkTest::ObserveBodyPlacement(ADemoCharacter* Subject, int32 PawnIndex)
{
    DEMO_LOG_TICK();
    const float Now=GetWorld()->GetTimeSeconds(),Elapsed=Now-StageStart; // 两端使用本World阶段时钟，不比较跨进程绝对时间。
    const UDemoPlayerAppearance* Settings=Subject && Subject->GetPlayerPresentation()?Subject->GetPlayerPresentation()->GetAppearance():nullptr; // 实际Pawn配置，不能由测试重新指定BodyOffset。
    USkeletalMeshComponent* Body=Subject?Subject->GetMesh():nullptr; // 真正渲染Body，而不是Local足骨的替代数据。
    const UCapsuleComponent* Capsule=Subject?Subject->GetCapsuleComponent():nullptr; // CharacterMovement真正移动的根组件。
    const UCharacterMovementComponent* Movement=Subject?Subject->GetCharacterMovement():nullptr; // 原生平滑状态仅观察，从不禁用或手动宣布完成。
    if (!Settings || !Body || !Capsule || !Movement || Body->GetAttachParent()!=Capsule)
    {
        if (Elapsed>.75f && ++InvalidPlacementSamples[PawnIndex]==1) UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_PLACEMENT_MISSING endpoint=%s stage=%d pawn=%d"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,PawnIndex);
        return false;
    }
    const FTransform Expected=Settings->BodyOffset; // 资产声明的相对胶囊空间位置/朝向，保持原生产配置。
    const FTransform Actual=Body->GetComponentTransform().GetRelativeTransform(Capsule->GetComponentTransform()); // 明确从两个真实世界变换复算，抓住整Mesh被SmoothClientPosition重置。
    const FTransform Cached(Subject->GetBaseRotationOffset(),Subject->GetBaseTranslationOffset(),Expected.GetScale3D()); // 只读原生Character平滑基线；不调用CacheInitialMeshOffset修饰测试结果。
    const float BaseLocationError=static_cast<float>(FVector::Dist(Cached.GetLocation(),Expected.GetLocation())); // 缓存和DA的厘米偏差，移动期间也不应改变。
    const float BaseRotationError=static_cast<float>(FMath::RadiansToDegrees(Cached.GetRotation().AngularDistance(Expected.GetRotation()))); // 度，四元数角距离避免Yaw换界误报。
    const float MeshLocationError=static_cast<float>(FVector::Dist(Actual.GetLocation(),Expected.GetLocation())); // 当前视觉网格的相对偏差，平滑中仅记录不按静止阈值拒绝。
    const float MeshRotationError=static_cast<float>(FMath::RadiansToDegrees(Actual.GetRotation().AngularDistance(Expected.GetRotation()))); // 真正最终Mesh旋转偏差。
    const FVector MeshForward=Body->GetComponentTransform().TransformVectorNoScale(FVector::RightVector).GetSafeNormal2D(); // 已验证UE模型参考+Y为人物前向，不能拿ActorForward冒充人物朝向。
    const FVector CapsuleForward=Capsule->GetForwardVector().GetSafeNormal2D(); // 真实胶囊的水平前向。
    const float FacingError=static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(MeshForward,CapsuleForward),-1.,1.)))); // 整人物相对移动方向的角差。
    const float SettledAfter=bPistolRunAudit && (Stage==3 || Stage==6)?4.85f:1.75f; // 新专项4.2秒才释放输入，沿用原夹具释放后0.65秒稳定窗口；不能把撞墙瞬间速度0的跑步过渡当作站立。
    const bool bSettled=Elapsed>SettledAfter && !Subject->IsCoopEliminated() && Subject->GetVelocity().Size()<1.f && Movement->bNetworkSmoothingComplete; // 仍检查真实静止和平滑完成；新阶段末强制要求稳定样本，不跳过鞋底检查。
    bool bValid=BaseLocationError<=.1f && BaseRotationError<=.1f && Movement->NetworkSmoothingMode!=ENetworkSmoothingMode::Disabled && !Subject->bIsCrouched; // 禁止通过关闭平滑通过；此19阶段没有蹲伏场景。
    bool bSoleMeasured=false; // 缺真实鞋底/地面不能当作0高度。
    FDemoNetworkSoleMeasurement LeftSole,RightSole; // 两脚各自保留真实脚下高度、同点支撑高度及测得表面差，不把1.15cm实际高度改写成零。
    float LeftAllowedHeight=1.f,RightAllowedHeight=1.f,ControlYawError=0.f; // cm/度；仅有效测量后给脚下上限增加真实正表面差，支撑面上限仍1cm。
    if (bSettled)
    {
        ++StablePlacementSamples[PawnIndex];
        const bool bLeft=MeasureSoleFloor(Subject,true,LeftSole),bRight=MeasureSoleFloor(Subject,false,RightSole); // 两脚都执行，不能短路漏记另一侧。
        bSoleMeasured=bLeft && bRight;
        bValid&=Movement->IsMovingOnGround() && Actual.GetScale3D().Equals(Expected.GetScale3D(),.001f) && MeshLocationError<=.25f && MeshRotationError<=.5f && FacingError<=.5f;
        LeftAllowedHeight=1.f+FMath::Max(0.f,LeftSole.SurfaceDelta); RightAllowedHeight=1.f+FMath::Max(0.f,RightSole.SurfaceDelta); // 只有同点测得的支撑面高于脚下地面才增加上限，没有任意加大常数。
        bValid&=bSoleMeasured && LeftSole.GroundHeight>=-.15f && RightSole.GroundHeight>=-.15f && LeftSole.SupportHeight<=1.f && RightSole.SupportHeight<=1.f
            && LeftSole.MaximumAbsoluteSurfaceDelta<=2.f && RightSole.MaximumAbsoluteSurfaceDelta<=2.f && LeftSole.GroundHeight<=LeftAllowedHeight && RightSole.GroundHeight<=RightAllowedHeight; // 保留实际穿地底线及1cm支撑悬空上限，窄网格线高差只能来自≤2cm真实面差。
        if (Stage>=9 && Stage<=18 && Subject==GetSubject())
        {
            const float Separation=FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(MeshForward.Rotation().Yaw),static_cast<float>(GetEyeStageAim().Yaw))); // 头身分离后允许配置内夹角，不能仍要求模型完全朝镜头。
            ControlYawError=FMath::Max(0.f,Separation-GetDefault<UDemoPlayerAimSettings>()->HeadYawLimit); // 此字段新版含义是超出合法头身角的部分；19..38另验精确身体轨迹。
            bValid&=ControlYawError<=2.f;
        }
        MaximumMeshLocationError[PawnIndex]=FMath::Max(MaximumMeshLocationError[PawnIndex],MeshLocationError); MaximumMeshRotationError[PawnIndex]=FMath::Max(MaximumMeshRotationError[PawnIndex],MeshRotationError);
        MaximumFacingError[PawnIndex]=FMath::Max(MaximumFacingError[PawnIndex],FacingError); MaximumControlYawError[PawnIndex]=FMath::Max(MaximumControlYawError[PawnIndex],ControlYawError);
        if (bSoleMeasured)
        {
            if (!GroundPlacementSamples[PawnIndex]) // 两类高度分别初始化，不把实际1.15cm减去地形后再覆盖原记录。
            {
                MinimumSoleHeight[PawnIndex]=FMath::Min(LeftSole.GroundHeight,RightSole.GroundHeight); MaximumSoleHeight[PawnIndex]=FMath::Max(LeftSole.GroundHeight,RightSole.GroundHeight);
                MinimumSupportHeight[PawnIndex]=FMath::Min(LeftSole.SupportHeight,RightSole.SupportHeight); MaximumSupportHeight[PawnIndex]=FMath::Max(LeftSole.SupportHeight,RightSole.SupportHeight);
            }
            ++GroundPlacementSamples[PawnIndex]; MinimumSoleHeight[PawnIndex]=FMath::Min(MinimumSoleHeight[PawnIndex],FMath::Min(LeftSole.GroundHeight,RightSole.GroundHeight)); MaximumSoleHeight[PawnIndex]=FMath::Max(MaximumSoleHeight[PawnIndex],FMath::Max(LeftSole.GroundHeight,RightSole.GroundHeight));
            MinimumSupportHeight[PawnIndex]=FMath::Min(MinimumSupportHeight[PawnIndex],FMath::Min(LeftSole.SupportHeight,RightSole.SupportHeight)); MaximumSupportHeight[PawnIndex]=FMath::Max(MaximumSupportHeight[PawnIndex],FMath::Max(LeftSole.SupportHeight,RightSole.SupportHeight));
            MaximumSurfaceDelta[PawnIndex]=FMath::Max(MaximumSurfaceDelta[PawnIndex],FMath::Max(LeftSole.MaximumAbsoluteSurfaceDelta,RightSole.MaximumAbsoluteSurfaceDelta));
        }
        if (StablePlacementSamples[PawnIndex]==1) UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_NETWORK_PLACEMENT_SETTLED endpoint=%s stage=%d pawn=%d basecm=%.3f basedeg=%.3f meshcm=%.3f meshdeg=%.3f facing=%.3f sole=%d groundcm=%.3f/%.3f supportcm=%.3f/%.3f surfacecm=%.3f/%.3f allowedgroundcm=%.3f/%.3f"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,PawnIndex,BaseLocationError,BaseRotationError,MeshLocationError,MeshRotationError,FacingError,bSoleMeasured,LeftSole.GroundHeight,RightSole.GroundHeight,LeftSole.SupportHeight,RightSole.SupportHeight,LeftSole.SurfaceDelta,RightSole.SurfaceDelta,LeftAllowedHeight,RightAllowedHeight);
    }
    if (Elapsed>.75f)
    {
        ++PlacementSamples[PawnIndex]; MaximumBaseLocationError[PawnIndex]=FMath::Max(MaximumBaseLocationError[PawnIndex],BaseLocationError); MaximumBaseRotationError[PawnIndex]=FMath::Max(MaximumBaseRotationError[PawnIndex],BaseRotationError);
        if (!bValid && ++InvalidPlacementSamples[PawnIndex]==1) UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_PLACEMENT_INVALID endpoint=%s stage=%d pawn=%d role=%d locallycontrolled=%d basecm=%.3f basedeg=%.3f meshcm=%.3f meshdeg=%.3f facing=%.3f settled=%d smoothing=%d sole=%d groundcm=%.3f/%.3f supportcm=%.3f/%.3f maxsurfacecm=%.3f/%.3f allowedgroundcm=%.3f/%.3f"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,PawnIndex,static_cast<int32>(Subject->GetLocalRole()),Subject->IsLocallyControlled(),BaseLocationError,BaseRotationError,MeshLocationError,MeshRotationError,FacingError,bSettled,Movement->bNetworkSmoothingComplete,bSoleMeasured,LeftSole.GroundHeight,RightSole.GroundHeight,LeftSole.SupportHeight,RightSole.SupportHeight,LeftSole.MaximumAbsoluteSurfaceDelta,RightSole.MaximumAbsoluteSurfaceDelta,LeftAllowedHeight,RightAllowedHeight);
    }
    if (Now>=NextPlacementRecord[PawnIndex])
    {
        NextPlacementRecord[PawnIndex]=Now+.1f;
        TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>(); // 当前游戏线程数值快照，报告10Hz归档不改变逐Tick检查覆盖。
        Row->SetNumberField(TEXT("stage"),Stage); Row->SetNumberField(TEXT("seconds"),Elapsed); Row->SetNumberField(TEXT("pawn_index"),PawnIndex); Row->SetStringField(TEXT("pawn"),Subject->GetPathName());
        Row->SetNumberField(TEXT("local_role"),static_cast<int32>(Subject->GetLocalRole())); Row->SetBoolField(TEXT("locally_controlled"),Subject->IsLocallyControlled()); Row->SetBoolField(TEXT("eliminated"),Subject->IsCoopEliminated());
        Row->SetObjectField(TEXT("expected_body_offset"),PlacementTransformJson(Expected)); Row->SetObjectField(TEXT("cached_base_offset"),PlacementTransformJson(Cached)); Row->SetObjectField(TEXT("mesh_to_capsule"),PlacementTransformJson(Actual));
        Row->SetObjectField(TEXT("mesh_world"),PlacementTransformJson(Body->GetComponentTransform())); Row->SetObjectField(TEXT("capsule_world"),PlacementTransformJson(Capsule->GetComponentTransform()));
        Row->SetNumberField(TEXT("base_location_error_cm"),BaseLocationError); Row->SetNumberField(TEXT("base_rotation_error_deg"),BaseRotationError); Row->SetNumberField(TEXT("mesh_location_error_cm"),MeshLocationError); Row->SetNumberField(TEXT("mesh_rotation_error_deg"),MeshRotationError);
        Row->SetNumberField(TEXT("facing_error_deg"),FacingError); Row->SetNumberField(TEXT("control_yaw_error_deg"),ControlYawError); Row->SetNumberField(TEXT("speed_cm_s"),Subject->GetVelocity().Size());
        Row->SetNumberField(TEXT("smoothing_mode"),static_cast<int32>(Movement->NetworkSmoothingMode)); Row->SetBoolField(TEXT("smoothing_complete"),Movement->bNetworkSmoothingComplete); Row->SetBoolField(TEXT("settled_checked"),bSettled); Row->SetBoolField(TEXT("sole_measured"),bSoleMeasured);
        if (bSoleMeasured)
        {
            Row->SetNumberField(TEXT("left_sole_height_cm"),LeftSole.GroundHeight); Row->SetNumberField(TEXT("right_sole_height_cm"),RightSole.GroundHeight); // 旧字段继续表示实际脚下离地，不改变历史含义。
            Row->SetNumberField(TEXT("left_sole_above_support_cm"),LeftSole.SupportHeight); Row->SetNumberField(TEXT("right_sole_above_support_cm"),RightSole.SupportHeight);
            Row->SetNumberField(TEXT("left_support_minus_ground_cm"),LeftSole.SurfaceDelta); Row->SetNumberField(TEXT("right_support_minus_ground_cm"),RightSole.SurfaceDelta);
            Row->SetNumberField(TEXT("left_max_abs_surface_delta_cm"),LeftSole.MaximumAbsoluteSurfaceDelta); Row->SetNumberField(TEXT("right_max_abs_surface_delta_cm"),RightSole.MaximumAbsoluteSurfaceDelta);
            Row->SetNumberField(TEXT("left_allowed_sole_height_cm"),LeftAllowedHeight); Row->SetNumberField(TEXT("right_allowed_sole_height_cm"),RightAllowedHeight);
            Row->SetStringField(TEXT("left_lowest_world_cm"),LeftSole.LowestWorld.ToString()); Row->SetStringField(TEXT("left_ground_world_cm"),LeftSole.GroundWorld.ToString()); Row->SetStringField(TEXT("right_lowest_world_cm"),RightSole.LowestWorld.ToString()); Row->SetStringField(TEXT("right_ground_world_cm"),RightSole.GroundWorld.ToString());
        }
        Row->SetObjectField(TEXT("floor_telemetry"),PlacementFloorJson(Subject,bSoleMeasured,LeftSole.LowestWorld,RightSole.LowestWorld)); // 保留原生地面与同XY双射线诊断，不能把新增支撑高度覆盖实际脚下高度。
        Row->SetBoolField(TEXT("valid"),bValid); PlacementFrames.Add(MakeShared<FJsonValueObject>(Row));
    }
    return bValid;
}
bool ADemoPlayerNetworkTest::ObserveEyes(ADemoCharacter* Subject)
{
    DEMO_LOG_TICK();
    const UDemoPlayerAppearance* Settings=Subject->GetPlayerPresentation()->GetAppearance(); // 当前Pawn加载的真实眼部配置。
    UDemoPlayerEyesComponent* Eyes=Subject->GetPlayerPresentation()->GetEyes(); // Presentation依赖Eyes，因此为同帧读值。
    if (!Settings || !Settings->EyeMesh || !Eyes || !Subject->bUseControllerRotationYaw) return false;
    for (bool bLeft:{true,false}) // 两个组件都必须存在，不能只用一枚眼球通过。
    {
        UStaticMeshComponent* Eye=Eyes->GetEyeMesh(bLeft); // Pawn拥有的实际渲染组件，不是测试替身。
        if (!Eye || Eye->GetStaticMesh()!=Settings->EyeMesh || Eye->GetAttachParent()!=Subject->GetMesh() || Eye->GetAttachSocketName()!=TEXT("head")
            || !Eye->bOwnerNoSee || Eye->bOnlyOwnerSee || Eye->GetCollisionEnabled()!=ECollisionEnabled::NoCollision || Eye->GetGenerateOverlapEvents()
            || Eye->IsVisible()!=Subject->GetMesh()->IsVisible() || Eye->bHiddenInGame!=Subject->GetMesh()->bHiddenInGame) return false; // 独立组件同时继承死亡Visibility和整体HiddenInGame。
        const FRotator Current=Eyes->GetCurrentRotation(bLeft), Target=Eyes->GetTargetRotation(bLeft); // 当前真实显示及未平滑目标，均为相对rest角。
        MaximumEyeYaw=FMath::Max(MaximumEyeYaw,static_cast<float>(FMath::Abs(Current.Yaw)));
        MaximumEyePitch=FMath::Max(MaximumEyePitch,static_cast<float>(FMath::Abs(Current.Pitch)));
        if (FMath::Abs(Current.Yaw)>Settings->EyeMaxYaw+.001f || FMath::Abs(Current.Pitch)>Settings->EyeMaxPitch+.001f
            || FMath::Abs(Target.Yaw)>Settings->EyeMaxYaw+.001f || FMath::Abs(Target.Pitch)>Settings->EyeMaxPitch+.001f) return false;
        const FTransform& Rest=bLeft?Settings->LeftEyeRest:Settings->RightEyeRest; // 配置位置不能被跟随旋转拖动。
        if (!Eye->GetRelativeLocation().Equals(Rest.GetLocation(),.001f)
            || !Eye->GetRelativeRotation().Quaternion().Equals((Rest.GetRotation()*Current.Quaternion()).GetNormalized(),.0001f)) return false;
    }
    const FVector ExpectedDirection=UDemoPlayerEyesComponent::ReadAimDirection(Subject); // 源链一致性另以下方实际预定ControlRotation作独立断言。
    if (!Eyes->GetAimDirection().Equals(ExpectedDirection,.001f)) return false;
    if (Stage>=9 && Stage<=18 && GetWorld()->GetTimeSeconds()-StageStart>.75f)
    {
        const float Error=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(Eyes->GetAimDirection()|GetEyeStageAim().Vector()),-1.f,1.f))); // 两端世界方向角差，RemoteViewPitch字节误差保留真实容差。
        MaximumAimError=FMath::Max(MaximumAimError,Error);
        if (Error>2.f) return false;
        const UDemoPlayerAnimInstance* Aim=Cast<UDemoPlayerAnimInstance>(Subject->GetMesh()->GetAnimInstance()); // 实际全身实例，仅读取已经复制方向驱动的缓存。
        const USkeletalMeshComponent* Gun=Subject->GetPlayerPresentation()->GetThirdPersonWeaponMesh(); // 真实队友展示枪，不能用请求方向代替最终求值结果。
        const ADemoWeaponBase* Weapon=Subject->GetWeaponComponent()->GetActiveWeapon(); // 读取当前枪真实枪口Socket契约。
        if (!Aim || !Aim->bAimWeaponActive || !Gun || !Weapon || Aim->UpperBodyAction!=TEXT("Hold")) return false;
        const float PoseError=FMath::Abs(Aim->AimPitch-GetEyeStageAim().Pitch); // 复制量化/平滑收敛后应在2度内，不能只让眼球跟随。
        const FVector MuzzleDirection=Gun->GetSocketRotation(Weapon->Config.MuzzleSocket).Vector(); // 实际骨枪枪口世界轴。
        const FVector GunExpected=FRotator(GetEyeStageAim().Pitch,Subject->GetActorRotation().Yaw,0).Vector(); // 水平仅转头时TP枪仍随身体，俯仰完整保留上一功能；真实射击仍按相机。
        const float MuzzleError=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(MuzzleDirection|GunExpected),-1.f,1.f))); // 检查真实枪轴，新增专项再以独立手工身体轨迹约束Yaw。
        MaximumAimPoseError=FMath::Max(MaximumAimPoseError,PoseError); MaximumMuzzleAimError=FMath::Max(MaximumMuzzleAimError,MuzzleError);
        if (PoseError>2.f || MuzzleError>2.5f) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_AIM_INVALID stage=%d pose=%.3f muzzle=%.3f"),Stage,PoseError,MuzzleError); return false; }
        ++EyeAimSamples;
    }
    return true;
}
void ADemoPlayerNetworkTest::ResetObservation(int32 NextStage, float Duration)
{
    DEMO_LOG_CALL();
    Stage=NextStage; StageDuration=Duration; StageStart=GetWorld()->GetTimeSeconds();
    if(NextStage==0) { StartedAt=FPlatformTime::Seconds(); bInitialStagePending=false; } // 两端各自从实际首阶段起算动作总预算，排除UI准备时间。
    bPistolRunAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoPistolRunArmNetworkTest")); // 两端显式选择相同专项，报告隔离保存。
    RunArmSamples=ReloadArmSamples=ResumedRunArmSamples=0; RunningHandTravel=0.f; FirstRunningHand=FVector::ZeroVector; // 每阶段独立收集真实运动证据。
    bCaptureRequested=FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerNetworkCapture")); bCaptureStageReady=false; bStageScreenshotRequested=false; StageCapturePath.Reset(); // 每端独立可选，不复制截图开关；相机跨9..18复用但每阶段重新取真实双人中心。
    bLocalDone=false; bRemoteDone=false; Samples=0; InvalidSamples=0; FirstGunPose.Reset(); MaximumGunTravel=0.f;
    MaximumFootTravel=MaximumHandTravel=MaximumSpeed=0.f;
    bSawFire=bSawReload=bSawSpectator=bSawRevived=false; FireRequests=0; bReloadRequested=false; bDeathApplied=false;
    EyeAimSamples=0; MaximumEyeYaw=MaximumEyePitch=MaximumAimError=MaximumAimPoseError=MaximumMuzzleAimError=0.f; bSawIndependentEyeSpectator=false; // 新阶段不沿用眼球/上身/实际枪口的旧方向证据。
    YawSamples=0; MaximumYawBodyError=MaximumYawHeadError=MaximumYawGunError=0.f; // 每阶段都清零水平实际骨/Actor统计，不能继承上一边界通过结果。
    for (int32 PawnIndex=0;PawnIndex<2;++PawnIndex) // 仅重置本阶段统计；PlacementFrames跨阶段保留以排查首次平滑及死亡/复活。
    {
        PlacementSamples[PawnIndex]=StablePlacementSamples[PawnIndex]=GroundPlacementSamples[PawnIndex]=InvalidPlacementSamples[PawnIndex]=0;
        MaximumBaseLocationError[PawnIndex]=MaximumBaseRotationError[PawnIndex]=0.f;
        MaximumMeshLocationError[PawnIndex]=MaximumMeshRotationError[PawnIndex]=MaximumFacingError[PawnIndex]=MaximumControlYawError[PawnIndex]=0.f;
        MinimumSoleHeight[PawnIndex]=MaximumSoleHeight[PawnIndex]=0.f; MinimumSupportHeight[PawnIndex]=MaximumSupportHeight[PawnIndex]=MaximumSurfaceDelta[PawnIndex]=0.f; NextPlacementRecord[PawnIndex]=StageStart; // 两组高度及实际地形差均按阶段独立统计。
    }
    ADemoCharacter* Subject=GetSubject(); // 两端同阶段观察同一网络Pawn。
    ADemoWeaponBase* Weapon=Subject?Subject->GetWeaponComponent()->GetActiveWeapon():nullptr; // 引用可能比Pawn晚到，Observe继续等待。
    InitialShot=MaximumShot=Weapon?Weapon->GetShotPresentationState().Sequence:0;
    InitialReload=MaximumReload=Weapon?Weapon->GetReloadSequence():0;
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_NETWORK_STAGE endpoint=%s stage=%d duration=%.2f"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,Duration);
}
void ADemoPlayerNetworkTest::BeginStage(int32 NextStage)
{
    DEMO_LOG_CALL();
    const float Duration=NextStage==3 || NextStage==6?6.f:NextStage==8?8.f:2.5f; // 装填/生成队列留真实时钟裕量，其余包含三次扳机的可观测窗口。
    if (NextStage==19) { HostYawBaseline=HostPawn->GetActorRotation().Yaw; RemoteYawBaseline=RemotePawn->GetActorRotation().Yaw; } // 先冻结两实际初态，再发计划；不写身体朝向以伪造一致性。
    ResetObservation(NextStage,Duration);
    ClientBeginStage(NextStage,Duration,HostPawn,RemotePawn,ExpectedHostColor,ExpectedRemoteColor,ExpectedHostTint,ExpectedRemoteTint,HostYawBaseline,RemoteYawBaseline);
}
void ADemoPlayerNetworkTest::ClientBeginStage_Implementation(int32 NextStage, float Duration, ADemoCharacter* Host,
    ADemoCharacter* Remote, uint8 HostColor, uint8 RemoteColor, FLinearColor HostTint, FLinearColor RemoteTint, float HostYawBase, float RemoteYawBase)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if (!FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerNetworkClient")))
    { ServerObservation(NextStage,false,TEXT("client requires explicit -DemoPlayerNetworkClient")); return; }
    HostPawn=Host; RemotePawn=Remote; ExpectedHostColor=HostColor; ExpectedRemoteColor=RemoteColor;
    ExpectedHostTint=HostTint; ExpectedRemoteTint=RemoteTint;
    HostYawBaseline=HostYawBase; RemoteYawBaseline=RemoteYawBase; // 测试服务器冻结值只进入断言计划，不改变生产控制器或Actor。
    if (HostPawn) AddTickPrerequisiteComponent(HostPawn->GetPlayerPresentation());
    if (RemotePawn) AddTickPrerequisiteComponent(RemotePawn->GetPlayerPresentation());
    if(NextStage==0&&(!HostPawn||!RemotePawn||!DemoNetworkProbeSetup::CanStartLocally(this,false)))
    { bInitialStagePending=true; PendingInitialDuration=Duration; return; } // 跨Actor复制无同序保证，先等本人真实Phase与菜单状态，不抢跑输入时钟。
    ResetObservation(NextStage,Duration);
#endif
}
void ADemoPlayerNetworkTest::DriveLocalPawn(float Elapsed)
{
    DEMO_LOG_TICK();
    ADemoCharacter* Driver=GetSubject(); // 客户端阶段只能由客户端自身提交动作。
    if (!Driver || !Driver->IsLocallyControlled()) return;
    if ((Stage==1 || Stage==4) && Elapsed<1.1f) Driver->AddMovementInput(Driver->GetActorForwardVector(),bPistolRunAudit?1.f:.25f); // 专项全速跑、原夹具慢走；仍由自主代理真实输入复制。
    if (bPistolRunAudit && (Stage==3 || Stage==6) && Elapsed<4.2f) Driver->AddMovementInput(Driver->GetActorForwardVector(),1.f); // 从跑步进入装填，完成后继续跑再自然刹停。
    if ((Stage==2 || Stage==5) && FireRequests<3 && Elapsed>=.5f+.4f*FireRequests)
    {
        ++FireRequests; Driver->GetWeaponComponent()->StartFire(); Driver->GetWeaponComponent()->StopFire(); // 正式拥有者组件RPC->GAS->扣弹->公开时钟。
    }
    if ((Stage==3 || Stage==6) && !bReloadRequested && Elapsed>=.5f)
    { bReloadRequested=true; Driver->GetWeaponComponent()->RequestReload(); }
    if (Stage>=9 && Stage<=(bYawAudit?38:18)) Driver->GetController()->SetControlRotation(GetEyeStageAim()); // 只写拥有者控制输入；身体阈值、RemoteViewYaw及头骨全部由生产流程求值。
    if (Stage==7) Driver->GetController()->SetControlRotation(FRotator(40,-20,0)); // 观战目标看向上方；客户端后侧Camera看向下方，应保持不同眼神来源。
}
bool ADemoPlayerNetworkTest::Observe()
{
    DEMO_LOG_TICK();
    if (!HostPawn || !RemotePawn) return false;
    const bool bHostPlacement=ObserveBodyPlacement(HostPawn,0),bRemotePlacement=ObserveBodyPlacement(RemotePawn,1); // 两个真实Pawn分别观测，不因第一位失败短路或中止原有动作/眼神证据。
    FLinearColor HostTint,RemoteTint; // 当前两Pawn材质实例值，跨进程比较服务器冻结的精确线性色。
    if (!ReadTint(HostPawn,HostTint) || !ReadTint(RemotePawn,RemoteTint)) return false;
    if (HostPawn->GetOutfitColorIndex()!=ExpectedHostColor || RemotePawn->GetOutfitColorIndex()!=ExpectedRemoteColor
        || !HostTint.Equals(ExpectedHostTint,.0001f) || !RemoteTint.Equals(ExpectedRemoteTint,.0001f)) return false;
    ADemoCharacter* Subject=GetSubject(); // 实际被观察的同一Pawn，不能以本地替身代替复制Actor。
    if (!ObserveEyes(Subject) || !ObserveYaw()) return false; // 新专项失败同样计入实际坏帧，不能只在报告里记录而不拒绝。
    if ((Stage==7 || Stage==8) && !ObserveEyes(RemotePawn)) return false; // 对真实死亡/复活目标另验双眼可见性，不能仅检查活着的被观战Host。
    USkeletalMeshComponent* Body=Subject->GetMesh(); // PostUpdateWork最终图输出。
    USkeletalMeshComponent* Gun=Subject->GetPlayerPresentation()->GetThirdPersonWeaponMesh(); // 每端独立创建的队友展示枪。
    ADemoWeaponBase* Weapon=Subject->GetWeaponComponent()->GetActiveWeapon(); // 服务器复制的稳定武器Actor。
    const UDemoPlayerAnimInstance* Animation=Cast<UDemoPlayerAnimInstance>(Body->GetAnimInstance()); // 真实运行缓存，不接受测试注入状态。
    const bool bHideOwnerGun=!Subject->GetPlayerPresentation()->UsesSharedBodyWeaponDisplay()
        || (Subject->IsLocallyControlled() && Subject->GetWeaponComponent()->GetScopeLevel()>0); // 渲染策略影响Owner过滤，远端仍必须显示同一把TP枪；不以旧双臂假设拒绝全身模式。
    if (!Weapon || !Body->GetSkeletalMeshAsset() || !Animation || !Gun || !Gun->GetSkeletalMeshAsset() || Gun->bHiddenInGame
        || !Body->bOwnerNoSee || Gun->bOwnerNoSee!=bHideOwnerGun || !Subject->GetMesh1P()->bOnlyOwnerSee || !Weapon->GetWeaponSkeletalMesh()->bOnlyOwnerSee) return false;
    const FVector Foot=Body->GetSocketTransform(TEXT("foot_l"),RTS_Component).GetLocation(); // 排除根Actor网络位移。
    const FVector Hand=Body->GetSocketTransform(TEXT("hand_l"),RTS_Component).GetLocation(); // 换弹必须真的驱动上身骨。
    if (bPistolRunAudit)
    {
        if (Animation->Speed>500.f && !Weapon->IsReloading() && Animation->UpperBodyAction==TEXT("Hold") && Animation->UpperBodyBlendAlpha>.999f)
        {
            if (Animation->RunLeftArmWeight<.99f) { UE_LOG(LogFPSDemo,Warning,TEXT("PISTOL_NETWORK_RUN_ARM_MISSING stage=%d weight=%.3f"),Stage,Animation->RunLeftArmWeight); return false; }
            const FVector RelativeHand=Hand-Body->GetSocketTransform(TEXT("upperarm_l"),RTS_Component).GetLocation(); // 同一真实骨架、同帧组件坐标，排除Actor与骨盆位移。
            if (!RunArmSamples) FirstRunningHand=RelativeHand;
            RunningHandTravel=FMath::Max(RunningHandTravel,static_cast<float>(FVector::Distance(RelativeHand,FirstRunningHand)));
            ++RunArmSamples; if (bSawReload) ++ResumedRunArmSamples;
        }
        if (Weapon->IsReloading() && Animation->UpperBodyAction==TEXT("Reload") && Animation->UpperBodyBlendAlpha>.999f)
        {
            if (Animation->RunLeftArmWeight>.001f) { UE_LOG(LogFPSDemo,Warning,TEXT("PISTOL_NETWORK_RELOAD_ARM_CONFLICT stage=%d weight=%.3f"),Stage,Animation->RunLeftArmWeight); return false; }
            ++ReloadArmSamples;
        }
    }
    if (!Samples) { FirstFoot=Foot; FirstHand=Hand; }
    const TArray<FTransform>& GunPose=Gun->GetComponentSpaceTransforms(); // 去掉展示枪整个组件的跟随位移，只比较内部弹匣等机械骨。
    if (FirstGunPose.IsEmpty()) FirstGunPose=GunPose;
    if (FirstGunPose.Num()==GunPose.Num())
        for (int32 Index=0; Index<GunPose.Num(); ++Index) // 骨序由同一枪网格拥有，本阶段没有换枪。
            MaximumGunTravel=FMath::Max(MaximumGunTravel,static_cast<float>(FVector::Dist(FirstGunPose[Index].GetLocation(),GunPose[Index].GetLocation())));
    MaximumFootTravel=FMath::Max(MaximumFootTravel,static_cast<float>(FVector::Dist(FirstFoot,Foot)));
    MaximumHandTravel=FMath::Max(MaximumHandTravel,static_cast<float>(FVector::Dist(FirstHand,Hand)));
    MaximumSpeed=FMath::Max(MaximumSpeed,static_cast<float>(Subject->GetVelocity().Size2D()));
    MaximumShot=FMath::Max(MaximumShot,Weapon->GetShotPresentationState().Sequence);
    MaximumReload=FMath::Max(MaximumReload,Weapon->GetReloadSequence());
    bSawFire|=Animation->UpperBodyAction==TEXT("Fire"); bSawReload|=Weapon->IsReloading() && Animation->UpperBodyAction==TEXT("Reload");
    ++Samples;
    ADemoPlayerController* Local=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 只取当前进程拥有的PC，不找远端私有控制器。
    if (!HasAuthority() && Local && Stage==7)
    {
        ADemoCoopSpectatorCamera* Camera=Cast<ADemoCoopSpectatorCamera>(Local->GetViewTarget()); // 正式拥有者RPC实际创建的相机。
        if (Camera && Camera->GetObservedCharacter()==HostPawn && Local->GetPawn()==RemotePawn && RemotePawn->IsCoopEliminated()
            && !HostPawn->IsOwnedBy(Camera))
        {
            ObservedCamera=Camera; bSawSpectator=true;
            const FVector EyeDirection=HostPawn->GetPlayerPresentation()->GetEyes()->GetAimDirection(); // 实际远端host眼神，不取本地死亡Pawn的控制器。
            bSawIndependentEyeSpectator|=(EyeDirection|Camera->GetActorForwardVector())<.95f
                && (EyeDirection|FRotator(40,-20,0).Vector())>.999f; // 字节Pitch的≤1.42度误差仍允许；必须与观战画面明显不同。
        }
    }
    if (Stage==8 && Local)
    {
        if (HasAuthority()) bSawRevived|=!RemotePawn->IsCoopEliminated();
        else bSawRevived|=!RemotePawn->IsCoopEliminated() && Local->GetPawn()==RemotePawn && Local->GetViewTarget()==RemotePawn && !ObservedCamera.IsValid();
    }
    return bHostPlacement && bRemotePlacement; // 原有局部骨/眼Aim合格不能覆盖整Mesh世界变换失败。
}
bool ADemoPlayerNetworkTest::FinishObservation(FString& Summary)
{
    DEMO_LOG_CALL();
    bool bSuccess=Samples>=12 && InvalidSamples==0 && ExpectedHostColor<4 && ExpectedRemoteColor<4 && ExpectedHostColor!=ExpectedRemoteColor; // 两端均验证真实色号/MID与可见性旗标，不能丢弃坏帧。
    if (Stage==1 || Stage==4) bSuccess&=MaximumSpeed>80.f && MaximumFootTravel>4.f;
    if (Stage==2 || Stage==5) bSuccess&=MaximumShot>=InitialShot+2 && bSawFire;
    if (Stage==3 || Stage==6) bSuccess&=MaximumReload>InitialReload && bSawReload && MaximumHandTravel>4.f && MaximumGunTravel>2.f;
    if (bPistolRunAudit && (Stage==1 || Stage==4 || Stage==3 || Stage==6)) bSuccess&=RunArmSamples>=6 && RunningHandTravel>8.f; // 两端均须实际看到循环，不接受只复制到动作枚举。
    if (bPistolRunAudit && (Stage==3 || Stage==6)) bSuccess&=ReloadArmSamples>=8 && ResumedRunArmSamples>=6; // 装填独占与结束恢复都必须发生。
    if (Stage==7) bSuccess&=HasAuthority()?RemotePawn->IsCoopEliminated() && !HostPawn->IsCoopEliminated():bSawSpectator && bSawIndependentEyeSpectator;
    if (Stage==8) bSuccess&=bSawRevived;
    if (Stage>=9 && Stage<=18) bSuccess&=EyeAimSamples>=12;
    if (Stage>=19 && Stage<=38) bSuccess&=bYawAudit && YawSamples>=12; // 真实头骨稳定样本不足同样不能通过。
    const bool bRequireSettled=Stage==0 || Stage==1 || Stage==4 || (Stage>=9 && Stage<=38) || (bPistolRunAudit && (Stage==3 || Stage==6)); // 移动装填阶段末新增必须稳定落地要求，不能仅通过延迟窗口掩盖持久异常。
    for (int32 PawnIndex=0;PawnIndex<2;++PawnIndex) // 每端都须检查host/remote两位，不能用自主代理通过掩盖模拟代理失败。
    {
        bSuccess&=PlacementSamples[PawnIndex]>=12 && InvalidPlacementSamples[PawnIndex]==0;
        if (bRequireSettled) bSuccess&=StablePlacementSamples[PawnIndex]>=12 && GroundPlacementSamples[PawnIndex]>=12; // 一直处于未完成平滑或未测得地面不能跳过断言而通过。
    }
    bSuccess&=ValidateEyeSolver(); // 实际显示与纯边界解算分别验收，不以发出输入冒充眼球达标。
    if (bCaptureRequested && Stage>=9 && Stage<=18)
    {
        const FDateTime RequestSecond(StageCaptureRequestedAt.GetYear(),StageCaptureRequestedAt.GetMonth(),StageCaptureRequestedAt.GetDay(),StageCaptureRequestedAt.GetHour(),StageCaptureRequestedAt.GetMinute(),StageCaptureRequestedAt.GetSecond()); // UE5.4 Windows GetTimeStamp截断到秒，必须同精度比较，不能把新文件误判成比毫秒请求时间早。
        const FDateTime ImageTime=IFileManager::Get().GetTimeStamp(*StageCapturePath); // 本次同步读到的实际文件写入时间，不复用旧时间。
        const bool bNewImage=bStageScreenshotRequested && IFileManager::Get().FileSize(*StageCapturePath)>0 && ImageTime>StageCapturePreviousTimestamp && ImageTime>=RequestSecond; // 新PNG必须更新且不早于请求所在秒；存在不等于视觉合格。
        bSuccess&=bNewImage;
        if (!bNewImage) UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK_CAPTURE_MISSING endpoint=%s stage=%d requested=%d path=%s"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,bStageScreenshotRequested,*StageCapturePath);
    }
    Summary=FString::Printf(TEXT("endpoint=%s stage=%d passed=%d samples=%d invalid=%d colors=%d/%d speed=%.2f foot=%.2f hand=%.2f mechanical=%.2f shot=%d->%d reload=%d->%d Fire=%d Reload=%d spectator=%d revived=%d eyesamples=%d eyeyaw=%.3f eyepitch=%.3f aimerror=%.3f eyeSpectator=%d"),
        HasAuthority()?TEXT("host"):TEXT("client"),Stage,bSuccess,Samples,InvalidSamples,ExpectedHostColor,ExpectedRemoteColor,MaximumSpeed,MaximumFootTravel,MaximumHandTravel,MaximumGunTravel,
        InitialShot,MaximumShot,InitialReload,MaximumReload,bSawFire,bSawReload,bSawSpectator,bSawRevived,EyeAimSamples,MaximumEyeYaw,MaximumEyePitch,MaximumAimError,bSawIndependentEyeSpectator);
    Summary+=FString::Printf(TEXT(" upperAimError=%.3f muzzleAimError=%.3f"),MaximumAimPoseError,MaximumMuzzleAimError); // 新字段保留实际数值，不改变历史眼球/脚底字段语义。
    if (bPistolRunAudit) Summary+=FString::Printf(TEXT(" runArm=%d reloadArm=%d resumedArm=%d handSwingCm=%.2f"),RunArmSamples,ReloadArmSamples,ResumedRunArmSamples,RunningHandTravel); // 两端分别输出真实左腕与优先级证据。
    if (bYawAudit) Summary+=FString::Printf(TEXT(" yawSamples=%d bodyYawError=%.3f headYawError=%.3f gunYawError=%.3f"),YawSamples,MaximumYawBodyError,MaximumYawHeadError,MaximumYawGunError); // 实物结果与动画缓存分开，包含两端各自最坏值。
    for (int32 PawnIndex=0;PawnIndex<2;++PawnIndex) // 两组简短统计保持测试RPC的2048字符上限，详细世界矩阵仅保存在各端本地JSON。
        Summary+=FString::Printf(TEXT(" placement%d[n=%d stable=%d ground=%d invalid=%d basecm=%.3f basedeg=%.3f meshcm=%.3f meshdeg=%.3f facingdeg=%.3f controldeg=%.3f solecm=%.3f..%.3f supportcm=%.3f..%.3f maxsurfacecm=%.3f]"),PawnIndex,
            PlacementSamples[PawnIndex],StablePlacementSamples[PawnIndex],GroundPlacementSamples[PawnIndex],InvalidPlacementSamples[PawnIndex],MaximumBaseLocationError[PawnIndex],MaximumBaseRotationError[PawnIndex],
            MaximumMeshLocationError[PawnIndex],MaximumMeshRotationError[PawnIndex],MaximumFacingError[PawnIndex],MaximumControlYawError[PawnIndex],MinimumSoleHeight[PawnIndex],MaximumSoleHeight[PawnIndex],MinimumSupportHeight[PawnIndex],MaximumSupportHeight[PawnIndex],MaximumSurfaceDelta[PawnIndex]);
    Summaries.Add(Summary); UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_NETWORK_OBSERVATION %s"),*Summary);
    return bSuccess;
}
void ADemoPlayerNetworkTest::ServerObservation_Implementation(int32 StageIndex, bool bSuccess, const FString& Summary)
{
    DEMO_LOG_CALL();
    if (bFinished || StageIndex!=Stage || bRemoteDone) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_NETWORK stale/duplicate report stage=%d expected=%d"),StageIndex,Stage); return; }
    Summaries.Add(Summary.Left(2048)); // 测试回执有界；该文字只进入报告，不执行代码或决定业务权限。
    if (!bSuccess) { Fail(TEXT("remote observation failed: ")+Summary.Left(512)); return; }
    bRemoteDone=true;
}
void ADemoPlayerNetworkTest::Fail(const FString& Message)
{
    DEMO_LOG_CALL();
    if (bFinished) return;
    if (HasAuthority()) ClientFinish(false,Message); else ServerObservation(Stage,false,Message);
    Finish(false,Message);
}
void ADemoPlayerNetworkTest::ClientFinish_Implementation(bool bSuccess, const FString& Message)
{
    DEMO_LOG_CALL(); Finish(bSuccess,Message);
}
void ADemoPlayerNetworkTest::Finish(bool bSuccess, const FString& Message)
{
    DEMO_LOG_CALL();
    if (bFinished) return;
    bFinished=true;
    const FString Endpoint=HasAuthority()?TEXT("host"):TEXT("client"); // 文件名来自内部固定枚举，不接收网络路径。
    const FString Directory=FPaths::ProjectSavedDir()/(bPistolRunAudit?TEXT("PlayerPistolRunAcceptance"):FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerMoveYawTest"))?TEXT("PlayerMovementViewAcceptance"):bYawAudit?TEXT("PlayerYawAcceptance"):FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerAimNetworkTest"))?TEXT("PlayerAimAcceptance"):TEXT("PlayerAcceptance")); // 各专项独立保留真实双端证据。
    IFileManager::Get().MakeDirectory(*Directory,true);
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 只序列化值，不保存跨World Actor引用。
    Report->SetBoolField(TEXT("passed"),bSuccess); Report->SetStringField(TEXT("endpoint"),Endpoint); Report->SetStringField(TEXT("message"),Message);
    Report->SetNumberField(TEXT("setup_contract_version"),2); Report->SetStringField(TEXT("setup_mode"),TEXT("manual_formal_ui")); // 新版报告不能被当作旧无人交互脚本或完整M1验收。
    Report->SetBoolField(TEXT("yaw_audit"),bYawAudit); Report->SetNumberField(TEXT("planned_stages"),bYawAudit?39:19); // 扩展计划只增加默认45度头身边界/换界，不扩大成所有武器或四人范围。
    Report->SetBoolField(TEXT("pistol_run_arm_audit"),bPistolRunAudit); // true明确1/4全速跑、3/6移动装填，并验证实际左腕行程和动画优先级。
    Report->SetStringField(TEXT("scope"),TEXT("two real LAN processes, two outfit MIDs, pistol TP movement/fire/reload, client death spectator and real clear revive, nineteen stages including own/remote eye aim and independent spectator gaze; both Pawns actual mesh/capsule world transform, native smoothing baseline, settled model facing and skinned soles over real static floor; no public internet, four-player, slopes or all-gun network claim"));
    if (bYawAudit) Report->SetStringField(TEXT("scope"),TEXT("two real LAN processes; original nineteen movement/pistol/eyes/death/revive/placement stages plus twenty head-only yaw, threshold, reverse and wrap stages; actual head bone, actor, view and TP muzzle, default 45 degrees; no Internet/four-player/all-gun network claim")); // 明确39阶段新增范围，不把旧十九阶段摘要当完整水平验收。
    Report->SetArrayField(TEXT("placement_frames"),PlacementFrames); // 本进程两真实Pawn的10Hz完整变换证据；逐Tick判定统计见阶段摘要。
    Report->SetBoolField(TEXT("capture_requested"),bCaptureRequested); Report->SetArrayField(TEXT("capture_records"),CaptureRecords);
    Report->SetStringField(TEXT("capture_scope"),TEXT("optional explicit LAN real-RHI stages 9..18; independent local camera only after spectator/revive stages; each endpoint writes 10 stable PNGs; image existence and values do not establish visual acceptance"));
    TSharedRef<FJsonObject> Contract=MakeShared<FJsonObject>(); // 本轮版本3定义完整整网格/两表面契约，缺字段或旧版本不能冒充本轮范围。
    Contract->SetNumberField(TEXT("version"),4); Contract->SetNumberField(TEXT("replication_grace_seconds"),.75); Contract->SetNumberField(TEXT("settled_after_seconds"),1.75); // 版本4的controldeg为超出头身阈值的余量；整Mesh与地面门槛不变。
    Contract->SetNumberField(TEXT("head_body_yaw_limit_deg"),GetDefault<UDemoPlayerAimSettings>()->HeadYawLimit); // 记录真实配置，不能用旧版身体等于控制Yaw契约解读新报告。
    Contract->SetNumberField(TEXT("stationary_speed_below_cm_s"),1.); Contract->SetNumberField(TEXT("minimum_samples_per_pawn"),12); Contract->SetNumberField(TEXT("record_interval_seconds"),.1);
    Contract->SetNumberField(TEXT("base_location_tolerance_cm"),.1); Contract->SetNumberField(TEXT("base_rotation_tolerance_deg"),.1);
    Contract->SetNumberField(TEXT("mesh_location_tolerance_cm"),.25); Contract->SetNumberField(TEXT("mesh_rotation_tolerance_deg"),.5); Contract->SetNumberField(TEXT("mesh_scale_tolerance"),.001);
    Contract->SetNumberField(TEXT("model_capsule_facing_tolerance_deg"),.5); Contract->SetNumberField(TEXT("model_stage_control_yaw_tolerance_deg"),2.);
    Contract->SetNumberField(TEXT("minimum_sole_height_cm"),-.15); Contract->SetNumberField(TEXT("maximum_sole_above_support_cm"),1.); Contract->SetNumberField(TEXT("maximum_absolute_surface_delta_cm"),2.); Contract->SetNumberField(TEXT("minimum_both_surface_normal_z_exclusive"),.95);
    Contract->SetStringField(TEXT("maximum_ground_height_rule"),TEXT("1cm + max(0, CurrentFloor support plane Z at the same lowest shoe point XY minus actual traced ground Z); every shoe sample absolute surface delta <=2cm, both normals Z>0.95"));
    Contract->SetStringField(TEXT("required_settled_stages"),TEXT("0,1,4,9,10,11,12,13,14,15,16,17,18")); Contract->SetStringField(TEXT("model_reference_forward"),TEXT("local +Y"));
    Contract->SetStringField(TEXT("settled_gate"),TEXT("native bNetworkSmoothingComplete, alive, stationary after 1.75 seconds; smoothing remains enabled; every available settled frame checked in all stages"));
    if (bPistolRunAudit) Contract->SetStringField(TEXT("pistol_run_settled_gate"),TEXT("stages 3/6 keep movement input until 4.2s; stationary placement starts at 4.85s (same 0.65s after input release as original move stages), require >=12 stable/floor samples for BOTH pawns; all geometric tolerances unchanged")); // 新动作计划与旧站立时间假设分开记录。
    Contract->SetStringField(TEXT("sole_method"),TEXT("all configured imported shoe-minimum points, real final world foot/ball transforms and complete two-bone weights; each foot minimum Z over WorldStatic trace floor"));
    Report->SetObjectField(TEXT("placement_contract"),Contract);
    TArray<TSharedPtr<FJsonValue>> Rows; // 双方每阶段摘要，客户端文件只含其自身证据。
    for (const FString& Row:Summaries) Rows.Add(MakeShared<FJsonValueString>(Row));
    Report->SetArrayField(TEXT("observations"),Rows);
    FString Json; // 同步最终字符串，退出前完成文件写入。
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json); // 游戏线程内存写入，不持异步UObject捕获。
    if (!FJsonSerializer::Serialize(Report,Writer) || !FFileHelper::SaveStringToFile(Json,*(Directory/(TEXT("network_")+Endpoint+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bSuccess=false;
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_NETWORK_%s endpoint=%s %s"),bSuccess?TEXT("SUCCESS"):TEXT("FAILED"),*Endpoint,*Message);
    ExitAt=FPlatformTime::Seconds()+(HasAuthority()?2.0:.2); Stage=bSuccess?100:101; // 客户端先退出、房主后退出；成功ClientFinish已证明host收到最后回执，避免host先断线触发客户端旅行并销毁退出Tick。
}
void ADemoPlayerNetworkTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (bFinished) { if (FPlatformTime::Seconds()>=ExitAt) FPlatformMisc::RequestExitWithStatus(false,Stage==100?0:1); return; }
#if !WITH_EDITOR
    Fail(TEXT("requires isolated WITH_EDITOR LAN runtime")); return;
#else
    if (!StartedAt) StartedAt=FPlatformTime::Seconds();
    if (FPlatformTime::Seconds()-StartedAt>(Stage<0?SetupWaitSeconds:(bYawAudit?240.0:150.0)))
    { Fail(Stage<0?TEXT("LAN setup timeout: complete formal UI core/ready/departure in both processes"):TEXT("LAN observation timeout")); return; } // 准备/动作分别有界，不放宽逐阶段精度。
    if (GetWorld()->GetTimeSeconds()<1.f) return;
    if (Stage<0)
    {
        if (!HasAuthority())
        {
            if(bInitialStagePending&&HostPawn&&RemotePawn&&DemoNetworkProbeSetup::CanStartLocally(this,false)) ResetObservation(0,PendingInitialDuration);
            return; // 客户端只消费服务器首计划，GS/UI尚未收敛时不能自行出发。
        }
        AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 权威阶段/队伍只从当前World获取。
        ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 等真正启动就绪，不伪造阶段字段。
        UDemoRoomSubsystem* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // 同一正式LAN房间服务。
        if (!Mode || !State || !Rooms) return;
        if (Rooms->IsPublic()) { Fail(TEXT("network probe permits LAN only")); return; }
        if (GetNetMode()==NM_Standalone)
        {
            if (!bHostRequested && State->Phase==EDemoPhase::Lobby)
            { bHostRequested=true; if (!Rooms->HostSlot(0,true)) Fail(TEXT("formal HostSlot(0,true) rejected")); }
            return;
        }
        if (GetNetMode()!=NM_ListenServer || Rooms->GetState()!=EDemoRoomState::Hosting) return;
        if (!bHostRequested) { bHostRequested=true; UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_NETWORK_HOST_READY JoinCoop 127.0.0.1:7777; both players use formal UI: core -> next terminal -> ready -> host starts first KillAll")); }
        const TArray<ADemoCharacter*> Members=Mode->GetCoopRoster()->GetPlayers(); // 必须已走真正连接/PC握手，不能只数生成Pawn。
        if(Members.Num()>2) { Fail(TEXT("probe requires exactly two registered players")); return; }
        if (Members.Num()!=2) return;
        for (ADemoCharacter* Member:Members)
            if (Member->IsLocallyControlled()) HostPawn=Member; else RemotePawn=Member;
        if (!HostPawn || !RemotePawn || !ReadTint(HostPawn,ExpectedHostTint) || !ReadTint(RemotePawn,ExpectedRemoteTint)) return;
        ADemoPlayerController* RemoteController=Cast<ADemoPlayerController>(RemotePawn->GetController()); // 真正网络连接拥有者，测试RPC仅发此客户端。
        if (!RemoteController || !RemoteController->IsCoopRegistered()) return;
        if(GetOwner()!=RemoteController) { SetOwner(RemoteController); ForceNetUpdate(); } // UI等待可能持续数分钟，只在实际拥有者变化时更新测试通道。
        ExpectedHostColor=HostPawn->GetOutfitColorIndex(); ExpectedRemoteColor=RemotePawn->GetOutfitColorIndex();
        AddTickPrerequisiteComponent(HostPawn->GetPlayerPresentation()); AddTickPrerequisiteComponent(RemotePawn->GetPlayerPresentation());
        FString SetupReason; // 只读生产阶段/会话/目标；Hub异步准备不是测试失败，不直接调用任何出发业务。
        const DemoNetworkProbeSetup::EResult Setup=DemoNetworkProbeSetup::Poll(this,false,FPlatformTime::Seconds()-StartedAt,SetupWaitSeconds,SetupReason);
        if(Setup==DemoNetworkProbeSetup::EResult::Failed) { Fail(SetupReason); return; }
        if(Setup==DemoNetworkProbeSetup::EResult::Waiting||!DemoNetworkProbeSetup::CanStartLocally(this,false)) return;
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_NETWORK_SETUP contract=2 mode=manual_formal_ui ready first CombatSession/KillAll"));
        BeginStage(0); return;
    }
    if (HasAuthority())
    {
        for (TActorIterator<ADemoEnemy> Enemy(GetWorld()); Enemy; ++Enemy) // 仅测试World冻结AI，保留实际生成、死亡和清关事件。
        {
            Enemy->SetActorTickEnabled(false);
            if (Stage==8 && Enemy->GetHealth()>0.f) DemoEffects::Apply(HostPawn->GetDemoASC(),Enemy->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-100000.f); // 正式死亡GE触发清场及全队复活，不直接调用FinishLevel。
        }
        if (Stage==7 && !bDeathApplied && GetWorld()->GetTimeSeconds()-StageStart>.5f)
        { bDeathApplied=true; DemoEffects::Apply(HostPawn->GetDemoASC(),RemotePawn->GetDemoASC(),UDemoHealthEffect::StaticClass(),-100000.f); }
    }
    const float Elapsed=GetWorld()->GetTimeSeconds()-StageStart; // 本地观测时长，服务器只等远端回执，不同步写时钟。
    if (!bLocalDone)
    {
        DriveLocalPawn(Elapsed);
        if (!Observe() && Elapsed>.75f) ++InvalidSamples; // 允许首次跨Actor引用复制短暂就绪，不掩盖稳定阶段的错误样本。
        if (!CaptureIfReady(Elapsed)) { Fail(TEXT("optional LAN RHI capture setup failed")); return; } // 独立摄影流程失败必须报告，但不能用它改原网络姿态/数值判定。
        if (Elapsed>=StageDuration)
        {
            FString Summary; // 本阶段独立断言输出，失败也保留具体采样数据。
            const bool bSuccess=FinishObservation(Summary); // 实际状态覆盖不足必须失败，不能按已发送动作计成功。
            bLocalDone=true;
            if (!bSuccess) { Fail(Summary); return; }
            if (!HasAuthority()) ServerObservation(Stage,true,Summary);
        }
    }
    if (HasAuthority() && bLocalDone && bRemoteDone)
    {
        if (Stage==(bYawAudit?38:18)) { const FString Message=bYawAudit?TEXT("all thirty-nine LAN stages passed"):TEXT("all nineteen LAN stages passed"); ClientFinish(true,Message); Finish(true,Message); } // 全部新增阶段也必须双方回执成功才结束。
        else BeginStage(Stage+1);
    }
#endif
}
