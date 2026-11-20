// 职责：两端真实WASD/Shift网络回归；正常UI准备后等真实Combat/已提交房间，DemoDashFlowHub留安全区。仅Editor临时局，不代替远端选核心/准备，不绕过出发门禁。
#include "Tests/DemoDashNetworkTest.h"
#include "Tests/DemoNetworkProbeSetup.h" // 共用只读准备门，等待正式状态，不自动驱动业务意图。
#include "Characters/DemoCharacter.h"
#include "Animation/DemoPlayerAnimInstance.h" // 仅测试读取真实已求值的头角，不写姿态值。
#include "Animation/DemoPlayerPresentationComponent.h" // 最终TP机械网格，方向验证不读取生产标定值作为期望。
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AI/DemoEnemy.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "World/DemoEncounterAreaSubsystem.h" // 优先解析当前流送房间快照，不把新场景回退到旧白模世界坐标。
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h" // FInputKeyParams完整定义；独立非Unity编译不依赖其他测试的间接include。
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Player/DemoPlayerController.h"
#include "Serialization/JsonSerializer.h"

ADemoDashNetworkTest::ADemoDashNetworkTest()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    bReplicates=true; bAlwaysRelevant=true;
#if WITH_EDITOR
    SetupWaitSeconds=DemoNetworkProbeSetup::ReadTimeoutSeconds(); // 两端独立读本地参数，仅影响准备等待，不放宽动作/几何断言。
#endif
    bYawAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerYawNetworkTest")); // 复用本轮显式专项开关，不在普通客户端增加测试行为。
    bMovementViewAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerMoveYawTest")); // 独立输出新契约，旧方向测试继续保留。
    bLocomotionAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoLocomotionNetwork")); // 两端独立启用，不以服务器代替客户端观察。
    bLyraAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoLyraNetwork")); bLocomotionAudit|=bLyraAudit;
    bAimContractAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoAimContractNetwork")) || FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimNetwork")); bLyraAudit|=bAimContractAudit; bLocomotionAudit|=bAimContractAudit; // 低头专项共用真实联网输入，不改变生产姿态。
    bShoulderAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderCandidate")) || FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderFormal")) || FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderLegacy"));
    bShoulderAudit|=FParse::Param(FCommandLine::Get(),TEXT("DemoReadyAcceptance")); // 本轮独立肩部证据，不能覆盖首轮报告。
}
ADemoCharacter* ADemoDashNetworkTest::Subject() const
{
    DEMO_LOG_TICK();
    return Step<9 || Step>=18?RemotePawn.Get():HostPawn.Get();
}
FVector2D ADemoDashNetworkTest::ExpectedAxis() const
{
    DEMO_LOG_TICK();
    const FVector2D Axes[]={FVector2D(-1,0),FVector2D(1,0),FVector2D(0,-1),FVector2D(0,1),FVector2D(-1,-1),FVector2D(1,-1),FVector2D(-1,1),FVector2D(1,1),FVector2D::ZeroVector}; // 左右后前、四斜向、无键；期望来自测试计划。
    if (Step==18) return FVector2D(-1,0);
    if (Step==19) return FVector2D::ZeroVector;
    if (Step==20) return FVector2D(1,0);
    if (Step==21) return FVector2D(-1,-1);
    return Axes[Step%9];
}
void ADemoDashNetworkTest::Key(FKey KeyValue, bool Down)
{
    DEMO_LOG_CALL();
    APlayerController* PC=UGameplayStatics::GetPlayerController(this,0); // 当前端唯一拥有者输入，绝不取另一玩家的Controller模拟请求。
    if (PC) PC->InputKey(FInputKeyParams(KeyValue,Down?IE_Pressed:IE_Released,Down?1.0:0.0));
    else UE_LOG(LogFPSDemo,Warning,TEXT("DASH_TEST_INPUT_MISSING"));
}
void ADemoDashNetworkTest::MoveKeys(FVector2D Axis)
{
    DEMO_LOG_CALL();
    Key(EKeys::A,Axis.X<0); Key(EKeys::D,Axis.X>0); Key(EKeys::W,Axis.Y>0); Key(EKeys::S,Axis.Y<0);
}
void ADemoDashNetworkTest::ResetStep(int32 NextStep)
{
    DEMO_LOG_CALL();
    if(NextStep==0) StartedAt=FPlatformTime::Seconds(); // 服务器与客户端均从真正开始首场观察计算原220秒动作上限。
    Step=NextStep; StepStart=GetWorld()->GetTimeSeconds(); bLocalDone=bRemoteDone=false;
    bJumpRequested=bJumpReleased=false; BoreSamples=JumpPoseSamples=LandPoseSamples=DashPoseSamples=0; MaximumBoreError=MaximumRootScaleError=0.f;
    NativeDashSamples=0;
    ShoulderStartSamples=0;
    bMoved=bPressed=bReleased=bRepeated=bRepeatReleased=false; bSawEvade=bWasEvading=false;
    EvadeStarts=Samples=0; PeakSpeed=0; CooldownAtTwoSeconds=-1; PeakDirection=DashStart=DashEnd=FVector::ZeroVector;
    bCooldownSampled=bCooldownTagAtTwoSeconds=false; // 每场景独立锁存；非拥有者的-1不再被误认为尚未采样。
    PeakWalkSpeed=0.f; WalkDirection=FVector::ZeroVector; // 新场景独立保留WASD运动证据，不能沿用冲刺方向。
    MovementPoseSamples=0; MovementFootTravel=0.f; FirstMovementFoot=FVector::ZeroVector; // 每方向重新测量最终骨运动。
    bStopLookApplied=false; MovingYawSamples=StoppedYawSamples=0; MaximumMovingBodyError=MaximumStoppedBodyError=MaximumStoppedHeadError=0.f; // 每个场景重新开始，不能继承上一个已对齐的结果。
    MoveKeys(FVector2D::ZeroVector); Key(EKeys::LeftShift,false);
    if (ADemoCharacter* Driver=Subject(); Driver && Driver->IsLocallyControlled()) // 当前场景只让真正拥有者调整其真实ControlRotation。
    {
        Driver->GetController()->SetControlRotation(FRotator(0,Step==21?67.f:bYawAudit && Step==20?20.f:0.f,0)); // 水平专项20度在阈值内，67度跨阈值，两种都必须按视线移动。
        if (bAimContractAudit) CastChecked<APlayerController>(Driver->GetController())->SetViewTarget(Driver); // 结束上一案远端观察相机，拥有者FP必须真实活动。
    }
    UE_LOG(LogFPSDemo,Display,TEXT("DASH_NETWORK_STEP endpoint=%s step=%d driver=%s axis=%s yaw=%.0f"),HasAuthority()?TEXT("host"):TEXT("client"),Step,Step<9 || Step>=18?TEXT("remote"):TEXT("host"),*ExpectedAxis().ToString(),Step==21?67.f:bYawAudit && Step==20?20.f:0.f);
}
void ADemoDashNetworkTest::BeginStep(int32 NextStep)
{
    DEMO_LOG_CALL();
    Step=NextStep;
    const bool bFlowHub=FParse::Param(FCommandLine::Get(),TEXT("DemoDashFlowHub")); // Hub专项使用安全区，普通场景只读取当前真实关卡配置。
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前权威World借用，不跨旅行缓存。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 关号来自服务器公开状态，不按探针步号生成。
    UDemoEncounterAreaSubsystem* Areas=GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>(); // 同生产流送/旧区域统一路由。
    FDemoLevelRow Level; FDemoDifficultyRow Difficulty; FString AreaError; // 同步关卡配置与失败原因，未通过时不使用默认位置。
    if(!Mode||!State||!Areas||(!bFlowHub&&!Mode->GetLevelConfig(State->LevelNumber,Level,Difficulty,AreaError)))
    { const FString Message=TEXT("current fixture area config unavailable: ")+AreaError; ClientFinish(false,Message); Finish(false,Message); return; }
    FDemoAreaSnapshot Area; // 只读当前已提交快照；ResolveArea会优先使用流送ActiveArea，并拒绝异常回退。
    if(!Areas->ResolveArea(bFlowHub?-1:Level.ArenaIndex,Area,AreaError))
    { const FString Message=TEXT("current fixture area unresolved: ")+AreaError; ClientFinish(false,Message); Finish(false,Message); return; }
    for (ADemoCharacter* Pawn:{HostPawn.Get(),RemotePawn.Get()}) // 仅复位隔离场景的站位，保留自然经过的技能冷却与GAS状态。
    {
        const FVector Probe=Area.Transform.TransformPosition(FVector(Pawn==Subject()?0.f:1300.f,0,500)); // 当前区域地面原点上500cm，非测试玩家仍隔1300cm；不改变World空间输入方向期望。
        FHitResult Floor; // 必须实测WorldStatic地面，不在空中伪造已接地。
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DashTestFloor),true,Pawn); // 同步只读，忽略本Pawn。
        if (!GetWorld()->LineTraceSingleByObjectType(Floor,Probe,Probe-FVector(0,0,1000),FCollisionObjectQueryParams(ECC_WorldStatic),Query))
        { ClientFinish(false,TEXT("real fixture floor missing")); Finish(false,TEXT("real fixture floor missing")); return; }
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Pawn->GetCharacterMovement()->ClearAccumulatedForces();
        Pawn->SetActorLocation(Floor.ImpactPoint+FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3),false,nullptr,ETeleportType::TeleportPhysics);
        Pawn->ForceNetUpdate();
    }
    ResetStep(NextStep); ClientBegin(NextStep,HostPawn,RemotePawn);
}
void ADemoDashNetworkTest::ClientBegin_Implementation(int32 NextStep, ADemoCharacter* Host, ADemoCharacter* Remote)
{
    DEMO_LOG_CALL();
    if (!FParse::Param(FCommandLine::Get(),TEXT("DemoDashNetworkClient"))) { ServerResult(NextStep,false,TEXT("explicit client flag missing")); return; }
    HostPawn=Host; RemotePawn=Remote;
#if WITH_EDITOR
    if(NextStep==0&&(!HostPawn||!RemotePawn||!DemoNetworkProbeSetup::CanStartLocally(this,FParse::Param(FCommandLine::Get(),TEXT("DemoDashFlowHub")))))
    { bInitialStepPending=true; UE_LOG(LogFPSDemo,Log,TEXT("DASH_NETWORK_WAIT_LOCAL_PHASE initial plan pending")); return; } // 不假定本测试RPC与GS/UI状态同序，保留准备总时限。
#endif
    bInitialStepPending=false; ResetStep(NextStep);
}
void ADemoDashNetworkTest::Drive(float Elapsed)
{
    DEMO_LOG_TICK();
    ADemoCharacter* Driver=Subject(); // 只从本机玩家真实键盘路径触发Shift；服务器不代替远端调用GA。
    if (!Driver || !Driver->IsLocallyControlled()) return;
    if (!bMoved && Elapsed>=.4f)
    { bMoved=true; MoveKeys(Step==18?FVector2D::ZeroVector:Step==19 || Step==20?FVector2D(-1,0):ExpectedAxis()); }
    if (!bPressed && Elapsed>=.85f)
    {
        bPressed=true;
        if (Step>=18) MoveKeys(ExpectedAxis()); // 同一输入帧起步/松开/反向，必须读到当前Enhanced Action而非上帧LastInput。
        Key(EKeys::LeftShift,true);
        UE_LOG(LogFPSDemo,Display,TEXT("DASH_NETWORK_PRESS step=%d last=%s acceleration=%s"),Step,*Driver->GetLastMovementInputVector().ToString(),*Driver->GetCharacterMovement()->GetCurrentAcceleration().ToString());
    }
    if (!bReleased && Elapsed>=.98f) { bReleased=true; MoveKeys(FVector2D::ZeroVector); Key(EKeys::LeftShift,false); }
    if (!bRepeated && Elapsed>=1.55f) { bRepeated=true; Key(EKeys::LeftShift,true); } // 第一次窗口结束后、冷却未完时重按，不能二次启动或重置冷却。
    if (!bRepeatReleased && Elapsed>=1.68f) { bRepeatReleased=true; Key(EKeys::LeftShift,false); }
    if (bMovementViewAudit && Step>=20 && !bStopLookApplied && Elapsed>=2.5f)
    { bStopLookApplied=true; Driver->GetController()->SetControlRotation(FRotator(0,(Step==21?67.f:20.f)+20.f,0)); } // 真实刹停后只改相机输入，身体应保留之前对齐方向，头部独立转20度。
    if (bLyraAudit && !bJumpRequested && Elapsed>=2.6f)
    {
        bJumpRequested=true;
        const float Pitch=FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimNetwork"))?(Step%3==0?-60.f:Step%3==1?-30.f:55.f):bAimContractAudit?(Step%2?45.f:55.f):(Step%2?-30.f:35.f); // 低头轮包含两档低头与仰头回归；输入和断言独立计算同一计划。
        const float YawOffset=(Step%2?-1.f:1.f)*(bAimContractAudit?40.f:30.f); // 两侧均保持身体自由观察阈值内。
        Driver->GetController()->SetControlRotation(FRotator(Pitch,Driver->GetActorRotation().Yaw+YawOffset,0)); // 阈值内身体不转，枪必须跟随；站立跳检验空中姿态。
        Key(EKeys::SpaceBar,true);
    }
    if (bLyraAudit && bJumpRequested && !bJumpReleased && Elapsed>=2.72f) { bJumpReleased=true; Key(EKeys::SpaceBar,false); }
}
void ADemoDashNetworkTest::Observe()
{
    DEMO_LOG_TICK();
    ADemoCharacter* Pawn=Subject(); // 两端同一真实Actor，实际速度由原生移动网络链路产生。
    if (!Pawn || !Pawn->GetDemoASC()) return;
    const float Elapsed=ScenarioElapsed(); // 测试计划秒；渲染专项起步延长采集，但真实World/冷却时间从不修改。
    if (bShoulderAudit) ObserveShoulder(Pawn,GetWorld()->GetTimeSeconds()-StepStart); // 骨证据始终记录实际经过秒。
    if (bAimContractAudit) ObserveAimContract(Pawn,Elapsed);
    if (bLyraAudit && Elapsed>.3f)
    {
        const UDemoPlayerAnimInstance* Animation=Cast<UDemoPlayerAnimInstance>(Pawn->GetMesh()->GetAnimInstance()); // 两端自己的最终实例，不能用权威结果冒充模拟代理。
        MaximumRootScaleError=FMath::Max(MaximumRootScaleError,static_cast<float>((Pawn->GetMesh()->GetSocketTransform(TEXT("root"),RTS_Component).GetScale3D()-FVector::OneVector).GetAbsMax()));
        if (Animation)
        {
            if(Animation->GetCurrentActiveMontage()) ++NativeDashSamples; // 在专用移动夹具中仅Dash允许播放Montage，其他射击操作使用独立序列。
            DashPoseSamples+=Animation->BodyMotionState==6 && Animation->BodyMotionWeight>0.f;
            JumpPoseSamples+=Animation->BodyMotionState==3 || Animation->BodyMotionState==4;
            LandPoseSamples+=Animation->BodyMotionState==5;
            const ADemoWeaponBase* Weapon=Pawn->GetWeaponComponent()->GetActiveWeapon(); // 当前实际库存武器，非硬编码Socket。
            const USkeletalMeshComponent* Gun=Pawn->GetPlayerPresentation()->GetThirdPersonWeaponMesh();
            if (Elapsed>2.9f && Animation->bCalibratedBoreAim && Animation->UpperBodyAction==TEXT("Hold") && Gun && Weapon)
            {
                const double Dot=FVector::DotProduct(Gun->GetSocketRotation(Weapon->Config.MuzzleSocket).Vector(),Pawn->GetBaseAimRotation().Vector());
                MaximumBoreError=FMath::Max(MaximumBoreError,static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot,-1.,1.))))); ++BoreSamples;
            }
        }
    }
    if (bMovementViewAudit && Step>=20)
    {
        const float ExpectedBody=Step==21?67.f:20.f; // 手写输入计划，横移/后退身体也必须朝视线，不能朝速度方向。
        const float BodyError=FMath::Abs(FMath::FindDeltaAngleDegrees(ExpectedBody,static_cast<float>(Pawn->GetActorRotation().Yaw))); // 真胶囊Yaw，服务端和拥有者分别检查。
        if (Elapsed>=.65f && Elapsed<1.35f && Pawn->GetVelocity().Size2D()>80.f)
        { ++MovingYawSamples; MaximumMovingBodyError=FMath::Max(MaximumMovingBodyError,BodyError); }
        if (Elapsed>=3.f && Pawn->GetVelocity().Size2D()<3.f)
        {
            const UDemoPlayerAnimInstance* Animation=Cast<UDemoPlayerAnimInstance>(Pawn->GetMesh()->GetAnimInstance()); // 已求值实例，缺失不能默认为头角正确。
            ++StoppedYawSamples; MaximumStoppedBodyError=FMath::Max(MaximumStoppedBodyError,BodyError);
            MaximumStoppedHeadError=FMath::Max(MaximumStoppedHeadError,Animation?FMath::Abs(Animation->HeadYaw-20.f):999.f);
        }
    }
    if (bYawAudit && Step>=20 && Elapsed>=.55f && Elapsed<.8f && Pawn->GetVelocity().Size2D()>PeakWalkSpeed)
    { PeakWalkSpeed=Pawn->GetVelocity().Size2D(); WalkDirection=Pawn->GetVelocity().GetSafeNormal2D(); } // 按Shift之前实测WASD，不能将Dash输入快照当普通移动通过。
    const bool Evading=Pawn->IsDashEvading(); // 实际复制GE窗口，不由测试置Tag。
    if (Evading && !bWasEvading) { ++EvadeStarts; if (!bSawEvade) DashStart=Pawn->GetActorLocation(); bSawEvade=true; }
    bWasEvading=Evading;
    if (Elapsed>=.85f && Elapsed<2.2f)
    {
        ++Samples;
        const FVector Velocity=Pawn->GetVelocity(); // 实际世界cm/s，不能用生产请求参数作为结果。
        if (bLocomotionAudit && Velocity.Size2D()>80.f)
        {
            const UDemoPlayerAnimInstance* Animation=Cast<UDemoPlayerAnimInstance>(Pawn->GetMesh()->GetAnimInstance()); // PostUpdateWork只读本端真实求值缓存。
            if (Animation && Animation->bDirectionalLocomotion && Animation->GroundBlendSpace)
            {
                const FVector Foot=Pawn->GetMesh()->GetSocketTransform(TEXT("foot_l"),RTS_Component).GetLocation(); // 包含最终地面适配，去掉网络Actor位移。
                if (MovementPoseSamples++==0) FirstMovementFoot=Foot;
                MovementFootTravel=FMath::Max(MovementFootTravel,static_cast<float>(FVector::Distance(Foot,FirstMovementFoot)));
            }
        }
        if (Velocity.Size2D()>PeakSpeed) { PeakSpeed=Velocity.Size2D(); PeakDirection=Velocity.GetSafeNormal2D(); }
        if (bSawEvade) DashEnd=Pawn->GetActorLocation();
    }
    if (Elapsed>=2.2f && !bCooldownSampled)
    {
        bCooldownSampled=true;
        bCooldownTagAtTwoSeconds=Pawn->GetDemoASC()->HasMatchingGameplayTag(DemoTags::DashCooldown);
        // ASC使用Mixed复制：权威和拥有者检查时长，其他观察者检查Tag与实际窗口数，不要求未复制的GE数据。
        if (HasAuthority() || Pawn->IsLocallyControlled()) CooldownAtTwoSeconds=Pawn->GetDemoASC()->GetCooldownRemaining(DemoTags::DashCooldown);
    }
}
bool ADemoDashNetworkTest::CheckStep(FString& Summary)
{
    DEMO_LOG_CALL();
    const FVector2D Axis=ExpectedAxis(); // 独立测试表，不调用角色的方向解算器。
    const FRotator View(0,Step==21?67.f:bYawAudit && Step==20?20.f:0.f,0); // 独立计划的相机Yaw，不读取生产身体或方向函数。
    const FVector Expected=View.RotateVector(Axis.IsNearlyZero()?FVector::ForwardVector:FVector(Axis.Y,Axis.X,0).GetSafeNormal()); // 斜向归一化，无键向相机前方。
    const FVector Displacement=DashEnd-DashStart; // 从本端观察到第一次真实GE开始后的实际位移。
    const float DirectionDot=FVector::DotProduct(PeakDirection,Expected),TravelDot=FVector::DotProduct(Displacement.GetSafeNormal2D(),Expected); // 区分仅输入走路与冲刺，两个方向证据都检查。
    const float Speed=Subject()?Subject()->GetDashSpeed():0; // 服务器配置/拥有者成长，不让期望依赖测试固定1300。
    const bool DurationVisible=HasAuthority() || (Subject() && Subject()->IsLocallyControlled()); // Mixed复制的GE时长只对权威及当前Pawn拥有者开放。
    const bool CooldownPassed=bCooldownSampled && bCooldownTagAtTwoSeconds && (!DurationVisible || (CooldownAtTwoSeconds>2.3f && CooldownAtTwoSeconds<3.1f)); // 两端都必须观察到冷却；有时长权限的端额外检查未被重复请求刷新。
    const FVector ExpectedWalk=View.RotateVector(Step==20?FVector(0,-1,0):FVector(Axis.Y,Axis.X,0).GetSafeNormal()); // 第20步先左移后同帧反向冲刺；第21步按计划斜后走。
    const float WalkDot=FVector::DotProduct(WalkDirection,ExpectedWalk); // 与正式WASD产生的速度比较，不依赖Dash参数。
    const bool WalkPassed=!bYawAudit || Step<20 || (PeakWalkSpeed>80.f && WalkDot>.98f); // 只对有明确前置移动窗口的两个转头案例增加断言。
    const bool ViewPassed=!bMovementViewAudit || Step<20 || (MovingYawSamples>=3 && StoppedYawSamples>=12
        && MaximumMovingBodyError<.2f && MaximumStoppedBodyError<.2f && MaximumStoppedHeadError<2.f); // 稳定网络样本；不放宽旧速度/冷却/方向断言。
    // 本端通过条件同时约束真实运动与GAS状态；输入成功或服务器接受请求本身不能作为冲刺通过证据。
    const bool Passed=Samples>=12 && bSawEvade && EvadeStarts==1 && PeakSpeed>Speed*.65f && PeakSpeed<=Speed*1.02f
        && DirectionDot>.98f && Displacement.Size2D()>120.f && TravelDot>.90f && CooldownPassed && WalkPassed && ViewPassed
        && (!bLocomotionAudit || (MovementPoseSamples>=5 && MovementFootTravel>4.f))
        && (!(FParse::Param(FCommandLine::Get(),TEXT("DemoLyraNativeCandidate")) || bShoulderAudit) || NativeDashSamples>=3)
        && (!bShoulderAudit || Step==8 || Step==17 || Step>=18 || ShoulderStartSamples>=3)
        && (!bLyraAudit || (BoreSamples>=20 && JumpPoseSamples>=3 && LandPoseSamples>=3 && DashPoseSamples>=3 && MaximumBoreError<1.5f && MaximumRootScaleError<.001f));
    Summary=FString::Printf(TEXT("endpoint=%s step=%d passed=%d samples=%d windows=%d speed=%.3f direction=%s dot=%.6f travel=%s travelDot=%.6f cooldownTag=%d durationVisible=%d cooldownAt2.2=%.3f"),HasAuthority()?TEXT("host"):TEXT("client"),Step,Passed,Samples,EvadeStarts,PeakSpeed,*PeakDirection.ToString(),DirectionDot,*Displacement.ToString(),TravelDot,bCooldownTagAtTwoSeconds,DurationVisible,CooldownAtTwoSeconds);
    if (bYawAudit) Summary+=FString::Printf(TEXT(" walkSpeed=%.3f walkDot=%.6f"),PeakWalkSpeed,WalkDot); // 原始WASD值与Dash值分开保存。
    if (bLocomotionAudit) Summary+=FString::Printf(TEXT(" movementPoseSamples=%d footTravelCm=%.3f"),MovementPoseSamples,MovementFootTravel);
    if (bLyraAudit) Summary+=FString::Printf(TEXT(" boreSamples=%d muzzleErrorDeg=%.3f jump=%d land=%d dash=%d rootScaleError=%.6f"),BoreSamples,MaximumBoreError,JumpPoseSamples,LandPoseSamples,DashPoseSamples,MaximumRootScaleError);
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoLyraNativeCandidate"))) Summary+=FString::Printf(TEXT(" nativeMontageSamples=%d"),NativeDashSamples);
    if(bShoulderAudit) Summary+=FString::Printf(TEXT(" shoulderStartSamples=%d nativeMontageSamples=%d"),ShoulderStartSamples,NativeDashSamples);
    if (bMovementViewAudit) Summary+=FString::Printf(TEXT(" movingSamples=%d stoppedSamples=%d movingBodyError=%.3f stoppedBodyError=%.3f stoppedHeadError=%.3f"),MovingYawSamples,StoppedYawSamples,MaximumMovingBodyError,MaximumStoppedBodyError,MaximumStoppedHeadError); // 保留两端真实误差，避免仅报告输入调用成功。
    Results.Add(Summary); UE_LOG(LogFPSDemo,Display,TEXT("DASH_NETWORK_OBSERVATION %s"),*Summary);
    return Passed;
}
void ADemoDashNetworkTest::ServerResult_Implementation(int32 Index, bool Passed, const FString& Summary)
{
    DEMO_LOG_CALL();
    if (Index!=Step || bRemoteDone || bFinished) { UE_LOG(LogFPSDemo,Warning,TEXT("DASH_NETWORK_STALE_RESULT step=%d received=%d"),Step,Index); return; }
    Results.Add(Summary); bRemoteDone=true;
    if (!Passed) { ClientFinish(false,Summary); Finish(false,Summary); }
}
void ADemoDashNetworkTest::ClientFinish_Implementation(bool Passed, const FString& Message) { DEMO_LOG_CALL(); Finish(Passed,Message); }
void ADemoDashNetworkTest::Finish(bool Passed, const FString& Message)
{
    DEMO_LOG_CALL();
    if (bFinished) return;
    bFinished=true; bFinalPassed=Passed;
    MoveKeys(FVector2D::ZeroVector); Key(EKeys::LeftShift,false);
    const FString Endpoint=HasAuthority()?TEXT("host"):TEXT("client"),Directory=FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimNetwork"))?(FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimCandidate"))?TEXT("LowAimReview/Candidate/Network"):TEXT("LowAimReview/Formal/Network")):bAimContractAudit?(FParse::Param(FCommandLine::Get(),TEXT("DemoAuthoredAimCandidate"))?TEXT("AimContractAudit/AuthoredCandidate/Network"):TEXT("AimContractAudit/Network")):FParse::Param(FCommandLine::Get(),TEXT("DemoReadyAcceptance"))?TEXT("LyraResidualMotion/Network"):bShoulderAudit?(FParse::Param(FCommandLine::Get(),TEXT("DemoShoulderNetworkCapture"))?TEXT("LyraShoulderAcceptance/NetworkRendered"):TEXT("LyraShoulderAcceptance/NetworkDash")):FParse::Param(FCommandLine::Get(),TEXT("DemoLyraNativeCandidate"))?TEXT("LyraNativeAcceptance/NetworkDash"):FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerMoveYawTest"))?TEXT("PlayerMovementViewAcceptance/Dash"):bYawAudit?TEXT("PlayerYawAcceptance/Dash"):TEXT("DashAcceptance")); // 诊断目录独立；只改变证据路径，不开启生产策略。
    IFileManager::Get().MakeDirectory(*Directory,true);
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 同步值报告，不保留World对象。
    Report->SetBoolField(TEXT("passed"),Passed); Report->SetStringField(TEXT("message"),Message); Report->SetStringField(TEXT("endpoint"),Endpoint);
    Report->SetNumberField(TEXT("setup_contract_version"),2); Report->SetStringField(TEXT("setup_mode"),TEXT("manual_formal_ui")); // 准备由真实两端UI完成，不能按旧无人值守脚本解释此报告。
    Report->SetNumberField(TEXT("setup_wait_seconds"),SetupWaitSeconds); // 单调准备秒，和原220秒动作上限分别记录。
    Report->SetStringField(TEXT("scope"),TEXT("Two real Editor LAN processes; actual InputKey W/A/S/D/Shift, 22 direction/neutral/same-frame/release/reverse/rotated-view cases; authoritative GAS cooldown/evade and observed movement; no prediction/Internet/four-player claim"));
    Report->SetBoolField(TEXT("dash_flow_hub"),FParse::Param(FCommandLine::Get(),TEXT("DemoDashFlowHub"))); // 明确本次是否只验证安全区技能，不将其宣称为Combat入口验证。
    TArray<TSharedPtr<FJsonValue>> Rows; // 每场景原文本保留单位和所有失败值，不只保存pass个数。
    for (const FString& Result:Results) Rows.Add(MakeShared<FJsonValueString>(Result));
    Report->SetArrayField(TEXT("observations"),Rows);
    if(bShoulderAudit) Report->SetArrayField(TEXT("shoulder_frames"),ShoulderFrames);
    if(bAimContractAudit) Report->SetArrayField(TEXT("aim_contract_frames"),AimContractFrames);
    FString Json; // 在当前游戏线程完成序列化，退出前写盘。
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json); // 无异步捕获，不跨World持有Writer。
    if (!FJsonSerializer::Serialize(Report,Writer) || !FFileHelper::SaveStringToFile(Json,*(Directory/(Endpoint+TEXT(".json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bFinalPassed=false;
    UE_LOG(LogFPSDemo,Display,TEXT("DASH_NETWORK_%s endpoint=%s %s"),bFinalPassed?TEXT("SUCCESS"):TEXT("FAILED"),*Endpoint,*Message);
    ExitAt=FPlatformTime::Seconds()+(HasAuthority()?2.:.2); // 远端先排空回执再退出，避免主机先断开触发旅行销毁测试Actor。
}
void ADemoDashNetworkTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (bFinished) { if (FPlatformTime::Seconds()>=ExitAt) FPlatformMisc::RequestExitWithStatus(false,bFinalPassed?0:1); return; }
#if !WITH_EDITOR
    Finish(false,TEXT("requires isolated Editor temporary progress")); return;
#else
    if (!StartedAt) StartedAt=FPlatformTime::Seconds();
    const double Limit=Step<0?SetupWaitSeconds:220.; // 两端准备与观察独立计时，合法异步加载不再触发旧动作超时。
    if (FPlatformTime::Seconds()-StartedAt>Limit)
    { const FString Message=Step<0?TEXT("setup timeout: complete formal two-player UI/phase and close menus"):TEXT("action probe timeout"); if (HasAuthority()) ClientFinish(false,Message); Finish(false,Message); return; }
    if (GetWorld()->GetTimeSeconds()<1.f) return;
    if (Step<0)
    {
        if (!HasAuthority())
        {
            if(bInitialStepPending&&HostPawn&&RemotePawn&&DemoNetworkProbeSetup::CanStartLocally(this,FParse::Param(FCommandLine::Get(),TEXT("DemoDashFlowHub"))))
            { bInitialStepPending=false; ResetStep(0); } // 本端GS和菜单已收敛才启动独立动作时钟；不代替服务端确认出发。
            return;
        }
        AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 正式流程创建队伍/战斗，不伪造登记。
        ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 等真实大厅及安全区就绪。
        UDemoRoomSubsystem* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // 同正式LAN房间服务，Editor没有云子系统。
        if (!Mode || !State || !Rooms) return;
        if (Rooms->IsPublic()) { Finish(false,TEXT("LAN only")); return; }
        if (GetNetMode()==NM_Standalone)
        { if (!bHostRequested && State->Phase==EDemoPhase::Lobby) { bHostRequested=true; if (!Rooms->HostSlot(0,true)) Finish(false,TEXT("HostSlot rejected")); } return; }
        if (GetNetMode()!=NM_ListenServer || Rooms->GetState()!=EDemoRoomState::Hosting) return;
        if (!bHostRequested) { bHostRequested=true; UE_LOG(LogFPSDemo,Display,TEXT("DASH_NETWORK_HOST_READY: both endpoints select core, open Next, Ready; host starts first KillAll via normal UI; Hub-only closes menus")); }
        const TArray<ADemoCharacter*> Members=Mode->GetCoopRoster()->GetPlayers(); // 正式握手成功的两位真实参战者。
        if(Members.Num()>2) { ClientFinish(false,TEXT("requires exactly two registered players")); Finish(false,TEXT("requires exactly two registered players")); return; }
        if (Members.Num()!=2) return;
        for (ADemoCharacter* Member:Members) if (Member->IsLocallyControlled()) HostPawn=Member; else RemotePawn=Member;
        if (!HostPawn || !RemotePawn || !HostPawn->GetDemoASC() || !RemotePawn->GetDemoASC()) return;
        ADemoPlayerController* RemotePC=Cast<ADemoPlayerController>(RemotePawn->GetController()); // 拥有测试Actor的真实连接。
        if (!RemotePC || !RemotePC->IsCoopRegistered()) return;
        if(GetOwner()!=RemotePC) { SetOwner(RemotePC); ForceNetUpdate(); } // 只在真实拥有者改变时更新通道，不按每Tick重发。
        const bool bFlowHub=FParse::Param(FCommandLine::Get(),TEXT("DemoDashFlowHub")); // 特效专项留真Hub；默认通过正式UI创建真实战斗会话。
        FString SetupReason; // 共用门返回实际待办/失败，不把异步StartLevel即时返回当最终结果。
        const DemoNetworkProbeSetup::EResult Setup=DemoNetworkProbeSetup::Poll(this,bFlowHub,FPlatformTime::Seconds()-StartedAt,SetupWaitSeconds,SetupReason);
        if(Setup==DemoNetworkProbeSetup::EResult::Waiting) return;
        if(Setup==DemoNetworkProbeSetup::EResult::Failed) { ClientFinish(false,SetupReason); Finish(false,SetupReason); return; }
        if(!DemoNetworkProbeSetup::CanStartLocally(this,bFlowHub)) return; // 房主也等本机菜单/Phase收敛，HubOnly不抢占仍打开的提示或终端。
        UE_LOG(LogFPSDemo,Display,TEXT("DASH_NETWORK_SETUP_READY contract=2 mode=manual_formal_ui"));
        BeginStep(0); return;
    }
    if (HasAuthority()) for (TActorIterator<ADemoEnemy> Enemy(GetWorld()); Enemy; ++Enemy) { Enemy->SetActorTickEnabled(false); Enemy->SetActorEnableCollision(false); } // 仅隔离回归避免AI伤害/挡路，不保存World或改资产。
    const float Elapsed=ScenarioElapsed(); // 普通场景5.25秒；渲染专项仅延长前置起步，4秒冷却仍自然结束。
    if (!bLocalDone)
    {
        Drive(Elapsed); Observe();
        if (Elapsed>=5.25f)
        {
            FString Summary; // 本端真实结果，同时用于失败报告与远端回执。
            const bool Passed=CheckStep(Summary); bLocalDone=true; // 锁存本端断言一次，再向权威回执；不能因下一帧状态变化重复改判。
            if (!HasAuthority()) ServerResult(Step,Passed,Summary);
            else if (!Passed) { ClientFinish(false,Summary); Finish(false,Summary); return; }
        }
    }
    if (HasAuthority() && bLocalDone && bRemoteDone)
    { if (Step==21) { ClientFinish(true,TEXT("all 22 dash scenarios passed")); Finish(true,TEXT("all 22 dash scenarios passed")); } else BeginStep(Step+1); }
#endif
}
