// 职责：角色头身分离、共享稳定第一人称眼位、视线复制与本机清晰度。相机/瞄准/动画代理共用纯值策略，装饰反馈不回写基础视点。
#include "Characters/DemoCharacter.h"
#include "Weapons/Melee/DemoMeleeComponent.h" // 挥击时短暂对齐身体，保证动画与权威轨迹方向一致。
#include "Animation/DemoPlayerAimSettings.h"
#include "Animation/DemoPlayerPresentationComponent.h" // 相机只读取已加载外观配置，不逐帧加载资产。
#include "Animation/DemoPlayerAppearance.h"
#include "Debug/DemoLog.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h" // 仅当前本机Pawn视点采用per-view near，外相机/观战沿其自身POV。
#include "Camera/CameraComponent.h" // 起落边沿记录组件位置，不对相机增加滞后插值。
#include "Components/SkeletalMeshComponent.h" // 全身模式读取同一Body求值后的head位置，不驱动任何骨骼。
#include "Components/CapsuleComponent.h" // 将眼位限制在碰撞胶囊内，不能随弯腰动画穿到墙后。
#include "GameFramework/CharacterMovementComponent.h" // 实际移动状态决定头身策略；不把按键或GAS表现Tag当作移动真值。
#include "EngineGlobals.h" // GFrameCounter只用于避免同一Pawn同帧重复插值，不参与网络时钟或存档。
#include "Engine/World.h" // BeginPlay验证隔离Editor GameWorld；基础眼位不访问网络服务。
#if WITH_EDITOR
#include "Misc/CommandLine.h" // 只在BeginPlay解析隔离AB，不逐帧读参数或进入包体。
#include "Misc/Parse.h"
#endif

void ADemoCharacter::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
#if WITH_EDITOR
    const bool bRequestedLegacy=FParse::Param(FCommandLine::Get(),TEXT("DemoViewLegacyHeadAB")); // 显式只读对照意图。
    const bool bAllowedFixture=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeComboRegression")) || FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerAnimationTest")); // 仅既有动画夹具。
    bEditorLegacyHeadViewAB=bRequestedLegacy && bAllowedFixture && FParse::Param(FCommandLine::Get(),TEXT("game"))
        && GetWorld()->WorldType==EWorldType::Game && GetNetMode()==NM_Standalone;
    if(bRequestedLegacy) UE_LOG(LogFPSDemo,Log,TEXT("VIEW_LEGACY_HEAD_AB requested=1 accepted=%d"),bEditorLegacyHeadViewAB); // 每Avatar一次，不开放CVar/编辑器联网开关。
#endif
    const FDemoSharedViewFrame InitialView=CaptureFullBodyViewFrame(); // 仅记录实际策略，不缓存将来会移动的眼位。
    UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_VIEW_POLICY pawn=%s stable=%d eye=%s"),*GetName(),!InitialView.bUseAnimatedHead,*GetSharedViewLocation().ToCompactString());
    OnCharacterMovementUpdated.AddUniqueDynamic(this,&ADemoCharacter::OnViewMovementUpdated); // UObject委托只绑定本Pawn，游戏线程移动完成后调用，EndPlay精确解绑。
}

void ADemoCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    DEMO_LOG_TICK();
    GetFirstPersonCameraComponent()->SetWorldLocation(GetSharedViewLocation()); // 原相机继续管理FOV与控制旋转；不附到head继承翻滚。
    Super::CalcCamera(DeltaTime,OutResult);
    const APlayerController* ViewController=Cast<APlayerController>(GetController()); // 游戏线程借用，本机控制权还不足以证明当前ViewTarget是自己。
    if (IsLocallyControlled() && ViewController && ViewController->GetViewTarget()==this
        && GetPlayerPresentation() && GetPlayerPresentation()->GetFirstPersonRenderScale()<1.f)
    {
        OutResult.PerspectiveNearClipPlane=GetPlayerPresentation()->GetFirstPersonNearClipCm(); // UE5.4原生POV字段：影响这一个完整view，绝不改GNear。
    }
    const UDemoPlayerAppearance* ViewAppearance=GetPlayerPresentation()?GetPlayerPresentation()->GetAppearance():nullptr; // 已加载配置只读，不写共享资产或全局渲染CVar。
    if (IsLocallyControlled() && ViewAppearance && ViewAppearance->bDisableFirstPersonMotionBlur)
    {
        // LOD保护修复缺骨变形；这里独立消除高速转头的相机模糊。保持所有骨姿/相机位置/FOV和时间AA，不靠模糊掩盖皮肤异常。
        OutResult.PostProcessSettings.bOverride_MotionBlurAmount=true;
        OutResult.PostProcessSettings.MotionBlurAmount=0.f;
        OutResult.PostProcessBlendWeight=1.f; // 当前相机原权重即1；确保该相机的明确关闭不被关卡Volume默认量重新混入。
    }
}

FVector ADemoCharacter::GetSharedViewLocation() const
{
    DEMO_LOG_TICK();
    const FDemoSharedViewFrame Frame=CaptureFullBodyViewFrame(); // 所有消费者用同一快照公式，不查询CameraComponent的上一帧位置。
    return Frame.Resolve(Frame.bUseAnimatedHead?GetMesh()->GetSocketLocation(TEXT("head")):FVector::ZeroVector); // 稳定模式不读取任何骨骼，避免动画/相机循环依赖。
}

FDemoSharedViewFrame ADemoCharacter::CaptureFullBodyViewFrame() const
{
    DEMO_LOG_TICK();
    const UDemoPlayerAppearance* Appearance=GetPlayerPresentation()?GetPlayerPresentation()->GetAppearance():nullptr; // 当前Pawn只读配置，返回数据不保存对象。
    FVector EyeOffset=Appearance && !Appearance->FullBodyEyeOffset.ContainsNaN()?Appearance->FullBodyEyeOffset:FVector(4,0,10); // cm；沿用原全身眼位缺省。
    const float Forward=Appearance && FMath::IsFinite(Appearance->LowLookForwardOffset)?FMath::Clamp(Appearance->LowLookForwardOffset,0.f,20.f):10.f; // 原低头前移安全界限。
    EyeOffset.X+=Forward*FMath::SmoothStep(15.f,60.f,static_cast<float>(-FRotator::NormalizeAxis(GetBaseAimRotation().Pitch)));
    FDemoSharedViewFrame Frame; // 本帧值快照，可复制到动画代理，不持有World引用。
    Frame.Center=GetActorLocation(); Frame.EyeOffset=GetBaseAimRotation().RotateVector(EyeOffset);
    Frame.Radius=FMath::Max(1.f,GetCapsuleComponent()->GetScaledCapsuleRadius()-8.f);
    Frame.CapsuleHalfHeight=FMath::Max(Frame.Radius,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-5.f);
    Frame.HeightLimits=FVector2D(-GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+30.f,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-5.f);
    FVector StableOffset=Appearance && !Appearance->FirstPersonViewOffset.ContainsNaN()?Appearance->FirstPersonViewOffset:FVector(8,0,75); // 世界上轴眼高，沿用已存在稳定配置，初始化缺资产同默认。
    StableOffset.X+=Forward*FMath::SmoothStep(15.f,60.f,static_cast<float>(-FRotator::NormalizeAxis(GetBaseAimRotation().Pitch))); // 同一低头前探，仅限安全胶囊。
    Frame.StableEye=Frame.Center+GetMovementViewRotation().RotateVector(StableOffset);
    bool bStable=!Appearance || Appearance->bUseStableFirstPersonView; // 共享数据配置，不以本机/远端、模型创建成功与否决定默认策略。
#if WITH_EDITOR
    if(bEditorLegacyHeadViewAB) bStable=false; // 已在BeginPlay校验的私有只读闸；无逐帧命令行解析。
#endif
    Frame.bUseAnimatedHead=!bStable && GetPlayerPresentation() && GetPlayerPresentation()->UsesFullBodyFirstPerson() && GetMesh()->DoesSocketExist(TEXT("head"));
    return Frame;
}

FVector FDemoSharedViewFrame::Resolve(const FVector& Head) const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    FVector Relative=(bUseAnimatedHead?Head+EyeOffset:StableEye)-Center; // 稳定眼位与当前head无关；旧策略留作兼容/同DLL对照。
    Relative.Z=FMath::Clamp(Relative.Z,HeightLimits.X,HeightLimits.Y);
    const double EndCap=bUseAnimatedHead?0.:FMath::Max(0.,FMath::Abs(Relative.Z)-FMath::Max(0.,static_cast<double>(CapsuleHalfHeight-Radius))); // 真胶囊顶部半球，不能把圆柱角点误当安全体内。
    const double SafeRadius=bUseAnimatedHead?Radius:FMath::Sqrt(FMath::Max(0.,FMath::Square(static_cast<double>(Radius))-FMath::Square(EndCap)));
    const FVector Horizontal=FVector(Relative.X,Relative.Y,0.f).GetClampedToMaxSize(SafeRadius);
    Relative.X=Horizontal.X; Relative.Y=Horizontal.Y;
    return Center+Relative;
}

void ADemoCharacter::OnJumped_Implementation()
{
    DEMO_LOG_CALL(); Super::OnJumped_Implementation();
    UE_LOG(LogFPSDemo,Log,TEXT("JUMP_VIEW_BEGIN pawn=%s role=%d local=%d z=%.3f cameraZ=%.3f velocityZ=%.3f dt=%.4f"),*GetName(),static_cast<int32>(GetLocalRole()),IsLocallyControlled(),GetActorLocation().Z,GetFirstPersonCameraComponent()->GetComponentLocation().Z,GetVelocity().Z,GetWorld()->GetDeltaSeconds()); // 仅真正起跳时输出一次，便于区分输入/物理/帧耗时问题。
}
void ADemoCharacter::Landed(const FHitResult& Hit)
{
    DEMO_LOG_CALL(); Super::Landed(Hit);
    UE_LOG(LogFPSDemo,Log,TEXT("JUMP_VIEW_LAND pawn=%s role=%d local=%d z=%.3f cameraZ=%.3f normalZ=%.3f dt=%.4f"),*GetName(),static_cast<int32>(GetLocalRole()),IsLocallyControlled(),GetActorLocation().Z,GetFirstPersonCameraComponent()->GetComponentLocation().Z,Hit.ImpactNormal.Z,GetWorld()->GetDeltaSeconds()); // Hit只在本回调借用，不缓存场景碰撞对象。
}

void ADemoCharacter::OnViewMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
    DEMO_LOG_TICK();
    if (Controller && GetLocalRole()!=ROLE_SimulatedProxy) FaceRotation(Controller->GetControlRotation(),DeltaSeconds); // ServerMove本次加速度在PC转向之后更新，此回调让主机/自主端起步同帧对齐；模拟端只消费复制。
}

void ADemoCharacter::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
    DEMO_LOG_TICK();
    // 本机PC与服务器处理ServerMove都调用此入口；模拟代理只消费ReplicatedMovement，不能独立追着视线再转一次。
    if (GetLocalRole()==ROLE_SimulatedProxy || bCoopEliminated) { bTurningBodyToView=false; return; } // 代理/死亡不得保留本地转身任务。
    if (NewControlRotation.ContainsNaN())
    { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_HEAD_YAW_INVALID pawn=%s rotation=%s"),*GetName(),*NewControlRotation.ToString()); return; }
    const UDemoPlayerAimSettings* Settings=GetDefault<UDemoPlayerAimSettings>(); // 同构建配置只在游戏线程借用。
    const float Limit=Settings->HeadYawLimit; // 已校验项目配置，单位度；恰好45度仍属于自由观察。
    const UCharacterMovementComponent* Movement=GetCharacterMovement(); // Pawn拥有的移动组件，仅当前游戏线程借用。
    const bool bMoving=Movement && Movement->MovementMode!=MOVE_None
        && (GetVelocity().SizeSquared2D()>9.f || Movement->GetCurrentAcceleration().SizeSquared2D()>1.f); // 速度超过3cm/s或有实际加速度视为移动；刹停保留跟随，只有垂直原地跳跃仍可自由转头。
    FRotator Body=GetActorRotation(); // 当前真实胶囊朝向；保留俯仰/翻滚，不把头部角度写回控制器。
    const float Delta=FMath::FindDeltaAngleDegrees(static_cast<float>(Body.Yaw),static_cast<float>(NewControlRotation.Yaw)); // 最短水平角，跨±180度仍连续。
    const UDemoMeleeComponent* Melee=FindComponentByClass<UDemoMeleeComponent>(); // 只读本Pawn动作，移动与攻击均使用真实复制时钟。
    float Excess=0.f; // 本次实际身体增量度数，始终走最短角；不把控制器视线写回身体结果。
    if (bMoving || (Melee && Melee->IsAttacking()))
    {
        Excess=Delta; bTurningBodyToView=false; // 保留移动/挥击即时对齐契约，伤害轨迹不等待站立转身。
    }
    else
    {
        if (!bTurningBodyToView && FMath::Abs(Delta)>Limit+.01f)
        {
            bTurningBodyToView=true;
            UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_BODY_TURN_BEGIN pawn=%s authority=%d view=%.2f body=%.2f delta=%.2f"),*GetName(),HasAuthority(),NewControlRotation.Yaw,Body.Yaw,Delta);
        }
        if (bTurningBodyToView && LastBodyTurnFrame!=GFrameCounter)
        {
            LastBodyTurnFrame=GFrameCounter;
            const float Step=FMath::IsFinite(DeltaTime)?FMath::Clamp(DeltaTime,0.f,.1f):0.f; // 游戏秒；暂停/无效Dt不推进，长卡帧不瞬间转完整个身体。
            Excess=Delta*(1.f-FMath::Exp(-Settings->BodyTurnFollowRate*Step)); // 身体与双手/枪共同转动，不单独偏转枪而脱手。
            if (FMath::Abs(Delta-Excess)<=.1f)
            {
                Excess=Delta; bTurningBodyToView=false;
                UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_BODY_TURN_ALIGNED pawn=%s authority=%d view=%.2f"),*GetName(),HasAuthority(),NewControlRotation.Yaw);
            }
        }
    }
    if (!FMath::IsNearlyZero(Excess,.0001f))
    { Body.Yaw=FRotator::NormalizeAxis(Body.Yaw+Excess); SetActorRotation(Body); }
    if (HasAuthority()) RemoteViewYaw=FRotator::CompressAxisToShort(NewControlRotation.Yaw); // 纯表现Yaw沿Actor属性复制，拥有者保持即时本机视线。
    if (bMoving!=bLoggedMovingView)
    { UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_VIEW_MOVE_MODE pawn=%s authority=%d moving=%d speed=%.2f view=%.2f body=%.2f"),*GetName(),HasAuthority(),bMoving,GetVelocity().Size2D(),NewControlRotation.Yaw,Body.Yaw); bLoggedMovingView=bMoving; }
    const int32 Band=FMath::Abs(Delta)>=Limit-.01f?(Delta<0.f?-1:1):0; // 只记录极限方向变化，连续拖动仍保留上方高频入口埋点。
    if (Band!=HeadYawLogBand)
    { UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_HEAD_YAW_LIMIT pawn=%s authority=%d band=%d view=%.2f body=%.2f excess=%.2f"),*GetName(),HasAuthority(),Band,NewControlRotation.Yaw,Body.Yaw,Excess); HeadYawLogBand=Band; }
}

FRotator ADemoCharacter::GetBaseAimRotation() const
{
    DEMO_LOG_TICK();
    if (Controller) return Controller->GetControlRotation(); // 本Pawn的输入真值，不能读取外部观战ViewTarget。
    FRotator Aim=Super::GetBaseAimRotation(); // 无Controller的原生分支保留RemoteViewPitch及引擎播放/复制处理。
    Aim.Yaw=FRotator::DecompressAxisFromShort(RemoteViewYaw); // 身体Yaw已不等于相机Yaw，必须读取独立复制的绝对视线。
    return Aim;
}

FRotator ADemoCharacter::GetMovementViewRotation() const
{
    DEMO_LOG_TICK();
    return FRotator(0.f,GetBaseAimRotation().Yaw,0.f); // 去掉上下视角，抬头不会减少地面移动/冲刺速度。
}
