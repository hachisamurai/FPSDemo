// 职责：Editor独立六例真实开火诊断，逐帧CSV记录正式AnimGraph各阶段与最终Manny骨姿；不改动画、后坐、权重或Shot。
// 使用：-DemoPlayerAnimationTest -DemoFireArmJitterAudit -DemoMotionIsolation=Current/NoAim -UseFixedTimeStep -FPS=60；可选-DemoFireArmJitterCapture实际RHI截图。
// 可选-DemoFireRecoilOperationBoundary将两枪单发例改成真实Reload后快速开火；真实时序/旧操作复活单独登记，不代替菜单或多人验收。
// 可选-DemoGunElbowOperations -DemoReloadHoldAudit明确扩成四枪32例装填/取消/立即Fire或Bolt，输出独立证据；旧18例默认不变。
// 依赖现有PlayerAnimationTest合法战斗/装备夹具、只读FirePose快照与Presentation先决条件；输入在读帧后提交，下一次正常动画更新才读取。
#include "Tests/DemoPlayerAnimationTest.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoPlayerAimSettings.h" // 仅记录实际消费的恢复指数，支持同DLL配置AB，不由参数文本推定策略。
#include "Animation/DemoLyraAnimInstance.h" // 只读真实Body Montage实例及启动计数，不驱动动画缓存。
#include "Animation/DemoPlayerAnimProxy.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNode_StateMachine.h" // 仅读取已完成IdleSM实际状态/权重，不写状态机或播放时钟。
#include "Animation/Skeleton.h" // 只读清单记录实际骨架路径，需要完整USkeleton声明。
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h" // 只记录真实组件矩阵，与Actor/POV分别保留，不锁视角。
#include "Camera/PlayerCameraManager.h" // 记录实际ViewTarget/FOV，不把固定外部机位当拥有者视角。
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GAS/DemoAbilitySystemComponent.h" // 射速专项只改本Editor临时Avatar属性，实际射击继续独立检查GAS成本和冷却。
#include "UObject/UnrealType.h" // GT只读当前Linked Layer已完成的原生IdleBreak标量，不调用蓝图事件或改默认值。
#include "GAS/DemoAttributeSet.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h" // manifest记录本次真实启动参数，不在动画工作线程解析。
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

namespace
{
    /** Value为CSV文字单元；统一转义引号，禁止路径/动作名破坏列边界。 */
    FString FireJitterCell(const FString& Value)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        return TEXT("\"")+Value.Replace(TEXT("\""),TEXT("\"\""))+TEXT("\"");
    }

    /** Stream由本测试独占；Text仅本帧UTF-8缓冲，Serialize后立即释放，不持有全部骨帧。 */
    bool FireJitterWrite(FArchive* Stream,const FString& Text)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        if (!Stream || Stream->IsError()) return false;
        FTCHARToUTF8 Utf8(*Text); // 转码缓冲只在本次同步Serialize期间有效。
        Stream->Serialize(const_cast<ANSICHAR*>(Utf8.Get()),Utf8.Length());
        return !Stream->IsError();
    }

    /** Number为有限标量；统一保留9位小数，JSON/CSV单位由manifest说明。 */
    FString FireJitterNumber(double Number)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        return FString::Printf(TEXT("%.9f"),Number);
    }

    /** Frame/Case指已完成求值的实际帧；Kind区别输入阶段和Final，Transform为CS厘米/xyzw/缩放。 */
    FString FireJitterPoseRow(int32 Frame,int32 Case,const TCHAR* Kind,FName Bone,const FTransform& Transform)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        const FVector P=Transform.GetLocation(),S=Transform.GetScale3D(); // 本帧有限值副本，不修改骨缓冲。
        const FQuat Q=Transform.GetRotation(); // 保留实际求值四元数，不重新归一化掩盖非法输出。
        return FString::Printf(TEXT("%d,%d,%s,%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n"),
            Frame,Case,Kind,*FireJitterCell(Bone.ToString()),P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z);
    }

    /** 在真实输入调用之后写事件，Seq/Ammo为调用后的武器真值；同帧骨样本在此前已经冻结。 */
    bool FireJitterEvent(FArchive* Stream,int32 Frame,int32 Case,float Now,const TCHAR* Event,const ADemoWeaponBase* Weapon)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        return FireJitterWrite(Stream,FString::Printf(TEXT("%d,%d,%.9f,%s,%d,%d\n"),Frame,Case,Now,Event,
            Weapon?Weapon->GetShotPresentationState().Sequence:0,Weapon?Weapon->GetAmmo():0));
    }
}

bool ADemoPlayerAnimationTest::BeginFireArmJitterStage(float Now)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const bool bLongGunReview=FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview")); // 仅Editor显式诊断扩为三把长枪，不改原后坐两枪六例合同。
    const bool bOperations=FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowOperations")); // 显式操作计划18例，后三例重复切回三枪核缓存，不影响原开火计划。
    const bool bReloadHoldAudit=bOperations && FParse::Param(FCommandLine::Get(),TEXT("DemoReloadHoldAudit")); // Editor显式四枪计划，不改变原开火或18操作合同。
    const bool bScopeViewAudit=bReloadHoldAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoScopeViewAudit")); // 可选末尾狙击真实两档镜及退出，旧32/18例默认不扩展。
    const bool bScopeCase=bScopeViewAudit && Stage==32; // 只最后新增一例，不把普通持枪或动画选态当作已经Scope。
    const int32 Stride=bReloadHoldAudit?8:bOperations?5:3; // 新计划各8例；旧每枪操作5例/开火3例不变。
    const int32 PlannedCases=bScopeViewAudit?33:bReloadHoldAudit?32:bOperations?18:bLongGunReview?9:6; // Scope为单独可辨的第33例，四枪/原计划不混淆。
    if (!Player.IsValid() || Stage<0 || Stage>=PlannedCases || ReloadHoldOnlyStage<INDEX_NONE || ReloadHoldOnlyStage>=PlannedCases) return false;
    Player->GetWeaponComponent()->StopFire(); // 保证前例自动Timer已经撤销，不让换枪边沿带旧输入。
    if ((ReloadHoldOnlyStage!=INDEX_NONE || Stage%Stride==0 || (!bReloadHoldAudit && bOperations && Stage>=15)) && !EquipWeapon(bReloadHoldAudit?FMath::Min(3,Stage/Stride):bOperations && Stage>=15?Stage-14:Stage/Stride+(bLongGunReview?1:0))) return false;
    if (!Weapon.IsValid()) return false;
    if (Stage==0 && FireRecoilTestRateScale!=1.f)
    {
        if (!FMath::IsFinite(FireRecoilTestRateScale) || FireRecoilTestRateScale<.5f || FireRecoilTestRateScale>3.f || !Player->GetDemoASC())
        { FinishFireArmJitterAudit(false,TEXT("invalid isolated GAS rate scale")); return false; }
        Player->GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetFireRateMultiplierAttribute(),FireRecoilTestRateScale); // 仅本fixture一次初始化，不连乘，不改Shot.Duration凭据。
    }
    Player->GetCharacterMovement()->StopMovementImmediately(); Player->ConsumeMovementInputVector();
    Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics);
    Player->SetActorRotation(FRotator::ZeroRotator);
    const int32 Operation=bScopeCase?8:bReloadHoldAudit?Stage%8:bOperations?(Stage<15?Stage%5:0):0; // 可选8为Scope，原0..7为四枪装填衔接计划。
    const bool bCancelOperation=bReloadHoldAudit?(Operation==3 || Operation==4):Operation==3; // 两种真实取消保持弹药守恒，逐颗装填在早期入仓前取消。
    const bool bEmptyOperation=bReloadHoldAudit?(Operation==2 || Operation==4 || Operation==7):Operation==2; // 空仓选择只通过隔离库存准备，不直接写bEmptyReload。
    const float Pitch=bScopeCase?0.f:bOperations?(bEmptyOperation?45.f:Operation==1 || (bReloadHoldAudit && Operation>=5)?-45.f:bCancelOperation?-20.f:0.f):bFireRecoilAimCorners?(Stage%3==1?45.f:Stage%3==2?-55.f:0.f):0.f; // 真正大角度恢复；Scope另固定水平，避免混入输入运动。
    const float Yaw=bScopeCase?0.f:bOperations?(bReloadHoldAudit?(bEmptyOperation?-25.f:Operation>0?25.f:0.f):Operation==1?25.f:Operation==2?-25.f:Operation==3?25.f:0.f):bFireRecoilAimCorners?(Stage%3==1?35.f:Stage%3==2?-35.f:0.f):0.f; // 新专项扩角度，旧18例角度逐值保持。
    CastChecked<APlayerController>(Player->GetController())->SetControlRotation(FRotator(Pitch,Yaw,0.f)); // 同例只设置一次，不逐帧压制真实后坐或转头。
    Weapon->RestoreAmmo(Weapon->GetCapacity(),0); // 仅隔离Actor补满；不重置冷却/Shot，也不创建持久补给。
    static const TCHAR* Cases[]={TEXT("stationary_single"),TEXT("stationary_burst"),TEXT("moving_burst")}; // 每枪同样三例，单发/连发与运动分别定位。
    static const TCHAR* Operations[]={TEXT("hold_sweep"),TEXT("tactical_reload"),TEXT("empty_reload"),TEXT("cancel_reload"),TEXT("recovery_fire")}; // 当前真实操作计划，不以动画缓存决定期望。
    static const TCHAR* ReloadHoldOperations[]={TEXT("hold_reference"),TEXT("tactical_reload"),TEXT("empty_reload"),TEXT("cancel_tactical"),TEXT("cancel_empty"),TEXT("recovery_fire"),TEXT("direct_tactical_fire"),TEXT("direct_empty_fire"),TEXT("scope_cycle")}; // 每枪固定8例，Scope只在额外第33例。
    StageName=Weapon->GetWeaponId().ToString()+TEXT("_")+(bReloadHoldAudit?ReloadHoldOperations[Operation]:bOperations?(Stage>=15?TEXT("return_hold"):Operations[Operation]):Cases[Stage%3]);
    StageStart=Now; StageDuration=3.f; FireArmJitterCaseFrame=0; bNativeIdleInterruptSubmitted=bNativeIdleExitObserved=false; // 单例转头诊断每轮只提交一次并独立观察退出。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowSweep")) && Stage%3==0) StageDuration=6.f; // 本项各枪单发例增加真实连续上下/左右扫视，不向动画节点写姿态。
    FireArmJitterStartShot=FireArmJitterLastShot=Weapon->GetShotPresentationState().Sequence;
    const UDemoLyraAnimInstance* InitialAnim=Cast<UDemoLyraAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 本帧装备切换后只冻结计数，不采新姿态。
    FireRecoilStartMontageCount=InitialAnim?InitialAnim->GunRecoilMontageStartCount:0;
    FireArmJitterFireSamples=GunElbowBoltSamples=0; FireArmJitterMaxSpeed=0.f; FireArmJitterNextPress=Now+.6f;
    bFireArmJitterPressed=bFireArmJitterReleased=false;
    bFireRecoilBoundaryObservedReload=false; FireRecoilBoundaryReloadFinishedAt=FireRecoilBoundaryShotDelay=-1.f;
    FireRecoilBoundaryOldOperationSamples=0; // 每例独立，不能由前一把枪的边沿满足当前断言。
    FireRecoilBoundaryPreviousArmLocals.Empty(); bFireRecoilBoundaryPreviousAdditive=bFireRecoilBoundaryActivationObserved=false;
    FireRecoilBoundaryActivationLocalStepDegrees=FireRecoilBoundaryAdjacentLocalStepDegrees=0.f; FireRecoilBoundaryActivationSourceSeconds=-1.f;
    if (bOperations)
    {
        bGunElbowReloadObserved=bGunElbowReloadCompleted=bGunElbowReloadCancelled=bGunElbowFollowupShot=false; GunElbowReloadExitTime=-1.f;
        bReloadHoldDirectSourceObserved=false;ReloadHoldFollowupShotDelay=-1.f; // 每例清理真实输入/源状态证据，不沿用上一把枪的成功。
        ScopeViewAuditStep=0; // 可选Scope后续物理落稳再冻结位置，不能继承前一例的90/40/10。
        StageDuration=bReloadHoldAudit?(Operation==0?.6f:bCancelOperation?1.8f:Weapon->GetReloadDuration()+1.8f):Stage>=15?2.f:Operation==0?6.f:Operation==3?1.8f:Weapon->GetReloadDuration()+1.8f;
        if (bScopeCase) StageDuration=1.2f; // 正式CycleAim/StopAim各等待真实下一帧POV，不手动SetFOV。
        if (Operation>0 && !bScopeCase)
        {
            StartAmmo=bEmptyOperation?0:FMath::Max(0,Weapon->GetCapacity()-1); Weapon->RestoreAmmo(StartAmmo,1); // 临时fixture只准备缺一发/空仓条件，不伪造装填回调。
            Player->GetWeaponComponent()->RequestReload();
            if (!Weapon->IsReloading()) { FinishFireArmJitterAudit(false,TEXT("gun elbow operation real GAS reload did not begin")); return false; }
            bGunElbowReloadObserved=true; if (!bCancelOperation) StageDuration=Weapon->GetReloadDuration()+1.8f;
            ExpectedAmmo=FMath::Min(Weapon->GetCapacity(),StartAmmo+1); ExpectedReserve=1-(ExpectedAmmo-StartAmmo);
        }
    }
    if (bReloadHoldAudit && ReloadHoldOnlyStage!=INDEX_NONE && !bScopeCase && ReloadHoldTailSeconds>0.f)
        StageDuration=(Operation==0 || bCancelOperation?0.f:Weapon->GetReloadDuration())+ReloadHoldTailSeconds; // 原操作deadline不变，只改变隔离夹具何时结束采集。
    const bool bOperationBoundaryCase=bFireRecoilOperationBoundary && Stage%3==0; // 仅手枪/步枪单发阶段；另外四例保持原连发/移动覆盖。
    if (bOperationBoundaryCase)
    {
        Weapon->RestoreAmmo(FMath::Max(0,Weapon->GetCapacity()-1),1); // 临时隔离库存只缺一发，走正常GAS装填及唯一弹药结算。
        Player->GetWeaponComponent()->RequestReload();
        if (!Weapon->IsReloading()) { FinishFireArmJitterAudit(false,TEXT("operation boundary: real GAS reload did not start")); return false; }
        StageDuration=FMath::Max(3.f,Weapon->GetReloadDuration()+1.f); // 给正常装填及后坐尾部完整回位留时，不写入任何动画/Shot时钟。
        UE_LOG(LogFPSDemo,Log,TEXT("FIRE_RECOIL_BOUNDARY_RELOAD_BEGIN stage=%d weapon=%s duration=%.6f"),Stage,*Weapon->GetWeaponId().ToString(),Weapon->GetReloadDuration());
    }
    if (!FireJitterEvent(FireArmJitterEvents.Get(),FireArmJitterFrame,Stage,Now,TEXT("restore_ammo_and_begin_case"),Weapon.Get()))
    { FinishFireArmJitterAudit(false,TEXT("case event CSV write failed")); return false; }
    TSharedPtr<FJsonObject> Case=MakeShared<FJsonObject>(); // 六项小型元数据，逐帧大数组只写CSV。
    Case->SetStringField(TEXT("name"),StageName); Case->SetNumberField(TEXT("stage"),Stage);
    Case->SetNumberField(TEXT("explicit_tail_seconds"),ReloadHoldTailSeconds); Case->SetBoolField(TEXT("explicit_native_idle_look_interrupt"),bNativeIdleInterruptAudit); // 非默认输入必须在同证据清楚标注。
    Case->SetStringField(TEXT("weapon_class"),Weapon->GetClass()->GetPathName());
    Case->SetStringField(TEXT("body_mesh"),Player->GetMesh()->GetSkeletalMeshAsset()->GetPathName());
    Case->SetStringField(TEXT("body_skeleton"),Player->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton()->GetPathName());
    Case->SetStringField(TEXT("actual_anim_class"),GetPathNameSafe(Player->GetMesh()->GetAnimClass()));
    Case->SetStringField(TEXT("actual_postprocess_class"),Player->GetMesh()->GetPostProcessInstance()?Player->GetMesh()->GetPostProcessInstance()->GetClass()->GetPathName():TEXT("None"));
    Case->SetNumberField(TEXT("rounds_per_minute"),Weapon->Config.RoundsPerMinute);
    Case->SetNumberField(TEXT("actual_fire_interval_seconds"),Weapon->GetFireInterval());
    Case->SetNumberField(TEXT("recoil_pitch_config"),Weapon->Config.RecoilPitch);
    Case->SetNumberField(TEXT("initial_ammo"),Weapon->GetAmmo());
    Case->SetBoolField(TEXT("automatic"),Weapon->Config.FireMode==EDemoFireMode::Automatic);
    Case->SetNumberField(TEXT("world_start_seconds"),Now); Case->SetNumberField(TEXT("duration_seconds"),StageDuration);
    Case->SetBoolField(TEXT("operation_boundary_case"),bOperationBoundaryCase); // 报告明确区别原六例与装填边界专项。
    Case->SetBoolField(TEXT("reload_hold_audit"),bReloadHoldAudit);Case->SetNumberField(TEXT("planned_cases"),PlannedCases);
    Case->SetBoolField(TEXT("scope_view_audit"),bScopeCase); // 与装填和Direct分项，未显式运行不称Scope通过。
    Case->SetNumberField(TEXT("reload_aim_restore_exponent"),GetDefault<UDemoPlayerAimSettings>()->ReloadAimRestoreExponent); // 读取CDO实际值，不伪造AB标签。
    Case->SetNumberField(TEXT("requested_pitch_degrees"),Pitch); Case->SetNumberField(TEXT("requested_yaw_degrees"),Yaw);
    TArray<TSharedPtr<FJsonValue>> CasesJson; // 只复制现有最多五项引用，不复制任何骨帧。
    const TArray<TSharedPtr<FJsonValue>>* PreviousCases=nullptr;
    if (Report->TryGetArrayField(TEXT("cases"),PreviousCases)) CasesJson=*PreviousCases;
    CasesJson.Add(MakeShared<FJsonValueObject>(Case)); Report->SetArrayField(TEXT("cases"),CasesJson);
    UE_LOG(LogFPSDemo,Display,TEXT("FIRE_ARM_JITTER_CASE_BEGIN index=%d name=%s seconds=%.3f rpm=%.3f recoil=%.3f"),Stage,*StageName,StageDuration,Weapon->Config.RoundsPerMinute,Weapon->Config.RecoilPitch);
    return true;
#else
    return false;
#endif
}

void ADemoPlayerAnimationTest::RecordFireArmJitterFrame(float DeltaSeconds)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    USkeletalMeshComponent* Body=Player.IsValid()?Player->GetMesh():nullptr; // 测试Tick已依赖Presentation，组件骨刷新与武器展示均完成。
    const UDemoPlayerAnimInstance* Anim=Body?Cast<UDemoPlayerAnimInstance>(Body->GetAnimInstance()):nullptr;
    if (!Body || !Body->GetSkeletalMeshAsset() || !Anim || !Weapon.IsValid())
    { FinishFireArmJitterAudit(false,TEXT("actual mesh/AnimInstance/weapon unavailable")); return; }
    const FDemoIdleBreakPoseAudit Audit=Anim->ReadIdleBreakAudit(); // 只有任务结束后读取独立值，绝不写回代理或动画缓存。
    if (!Audit.bFirePoseEnabled) { FinishFireArmJitterAudit(false,TEXT("dedicated per-bone FirePose diagnostic was not enabled")); return; }
    const bool bAnchor=Audit.FireAnchorCount!=FireArmJitterLastAnchorCount && !Audit.FirePoseNames.IsEmpty()
        && Audit.FireBase.Num()==Audit.FirePoseNames.Num() && Audit.FireUpper.Num()==Audit.FirePoseNames.Num();
    const bool bAim=Audit.FireAimCount!=FireArmJitterLastAimCount && !Audit.FirePoseNames.IsEmpty()
        && Audit.FireBeforeAim.Num()==Audit.FirePoseNames.Num() && Audit.FireAfterAim.Num()==Audit.FirePoseNames.Num(); // NoAim必须记录未执行，不能生成假的单位姿态。
    FireArmJitterLastAnchorCount=Audit.FireAnchorCount; FireArmJitterLastAimCount=Audit.FireAimCount;
    const FReferenceSkeleton& Reference=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& Final=Body->GetComponentSpaceTransforms(); // 原生Manny PP后的实际Mesh骨数组，可能与Compact含虚拟骨数量不同。
    if (Final.Num()!=Reference.GetNum()) { FinishFireArmJitterAudit(false,TEXT("actual final mesh pose is incomplete")); return; }
    const AGameStateBase* State=GetWorld()->GetGameState(); // 与生产动画选片相同服务器同步World时钟。
    const double ServerNow=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const FDemoBoltCycleState Shot=Weapon->GetShotPresentationState(); // 真实成功射击状态，不从输入事件反推。
    const FRotator View=Player->GetBaseAimRotation(); const FVector Focus=Anim->MuzzleFocusPoint,Muzzle=Anim->MuzzleFocusOffset;
    const FVector SharedEye=Player->GetSharedViewLocation(); // 当前最终眼位，与上帧游戏线程焦点一起用于检查反馈，不直接用来驱动Pose。
    const FVector ActorLocation=Player->GetActorLocation();
    TArray<FString> Cells; Cells.Reserve(58); // 有限一行标量，避免printf多列类型错配。
    Cells.Add(FString::FromInt(FireArmJitterFrame)); Cells.Add(FString::FromInt(FireArmJitterCaseFrame)); Cells.Add(FString::FromInt(Stage));
    Cells.Add(FireJitterCell(StageName)); Cells.Add(FireJitterCell(Weapon->GetWeaponId().ToString()));
    Cells.Add(FireJitterNumber(GetWorld()->GetTimeSeconds())); Cells.Add(FireJitterNumber(GetWorld()->GetTimeSeconds()-StageStart));
    Cells.Add(FireJitterNumber(ServerNow)); Cells.Add(FireJitterNumber(DeltaSeconds));
    Cells.Add(FString::FromInt(Shot.Sequence)); Cells.Add(FireJitterNumber(Shot.ServerStartTime)); Cells.Add(FireJitterNumber(Shot.Duration));
    Cells.Add(FireJitterNumber(ServerNow-Shot.ServerStartTime)); Cells.Add(FireJitterCell(Anim->UpperBodyAction.ToString()));
    Cells.Add(FireJitterCell(Anim->PreviousUpperBodyAction.ToString())); Cells.Add(FireJitterCell(GetPathNameSafe(Anim->UpperBodySequence.Get())));
    Cells.Add(FireJitterNumber(Anim->ActionProgress)); Cells.Add(FireJitterNumber(Anim->UpperBodyTime)); Cells.Add(FireJitterNumber(Anim->PreviousUpperBodyTime));
    Cells.Add(FireJitterNumber(Anim->UpperBodyBlendAlpha)); Cells.Add(FireJitterNumber(Anim->AimAlignmentWeight)); Cells.Add(FireJitterNumber(Anim->ActionOverlayWeight));
    Cells.Add(FireJitterNumber(Audit.IdleApplied)); Cells.Add(FireJitterNumber(Player->GetVelocity().Size2D())); Cells.Add(FString::FromInt(Weapon->GetAmmo()));
    Cells.Add(FireJitterNumber(View.Pitch)); Cells.Add(FireJitterNumber(View.Yaw)); Cells.Add(FString::FromInt(Anim->bMuzzleFocusActive));
    for (const double Number:{Focus.X,Focus.Y,Focus.Z,Muzzle.X,Muzzle.Y,Muzzle.Z,SharedEye.X,SharedEye.Y,SharedEye.Z,ActorLocation.X,ActorLocation.Y,ActorLocation.Z}) Cells.Add(FireJitterNumber(Number)); // 各组三轴单位/空间由header和manifest声明。
    Cells.Add(FString::FromInt(bAnchor)); Cells.Add(FString::FromInt(bAim)); Cells.Add(FString::FromInt(Audit.FireAnchorCount)); Cells.Add(FString::FromInt(Audit.FireAimCount));
    Cells.Add(FString::FromInt(Audit.FirePoseNames.Num())); Cells.Add(FString::FromInt(Final.Num()));
    Cells.Add(FString::FromInt(Body->GetPostProcessInstance()!=nullptr && !Body->GetDisablePostProcessBlueprint()));
    const UDemoLyraAnimInstance* Lyra=Cast<UDemoLyraAnimInstance>(Anim); // 当前实际实例，只读，不把Slot配置存在当作实际播放。
    Cells.Add(FString::FromInt(Anim->bUseAdditiveGunRecoil)); Cells.Add(FireJitterCell(GetPathNameSafe(Anim->GunRecoilMontage.Get())));
    Cells.Add(FireJitterNumber(Anim->GunRecoilSourceTime)); Cells.Add(FireJitterNumber(Anim->GunRecoilPhase));
    Cells.Add(FString::FromInt(Lyra?Lyra->GunRecoilMontageInstanceID:INDEX_NONE));
    Cells.Add(FireJitterNumber(Lyra?Lyra->GunRecoilMontageWeight:0.f));
    Cells.Add(FString::FromInt(Lyra?Lyra->GunRecoilMontageStartCount:0));
    const USkeletalMeshComponent* OwnerSkin=Player->GetPlayerPresentation()->GetFirstPersonBodyMesh(); // 当前实际Follower，非旧Arms Mesh1P。
    Cells.Add(FString::FromInt(Body->GetPredictedLODLevel()));Cells.Add(FString::FromInt(OwnerSkin?OwnerSkin->GetPredictedLODLevel():-1));
    Cells.Add(FString::FromInt(Body->GetForcedLOD()));Cells.Add(FString::FromInt(OwnerSkin?OwnerSkin->GetForcedLOD():-1));
    Cells.Add(FString::FromInt(Anim->GetRequiredBones().GetCompactPoseNumBones()));
    Cells.Add(FString::FromInt(Body->GetPostProcessInstance()?Body->GetPostProcessInstance()->GetRequiredBones().GetCompactPoseNumBones():0));
    Cells.Add(FireJitterCell(OwnerSkin?GetPathNameSafe(OwnerSkin->GetSkeletalMeshAsset()):TEXT("none")));Cells.Add(FireJitterCell(OwnerSkin?GetPathNameSafe(OwnerSkin->LeaderPoseComponent.Get()):TEXT("none")));
    const APlayerController* ActualController=Cast<APlayerController>(Player->GetController()); // 本帧真正显示镜头，外部和Owner分别记录。
    Cells.Add(FireJitterNumber(ActualController && ActualController->PlayerCameraManager?ActualController->PlayerCameraManager->GetFOVAngle():0.f));
    Cells.Add(FireJitterCell(ActualController?GetPathNameSafe(ActualController->GetViewTarget()):TEXT("none")));
    const TSubclassOf<UAnimInstance>* ConfiguredLayer=Lyra?Lyra->WeaponLayers.Find(Anim->GetUpperWeaponId()):nullptr; // 当前真实Linked Layer类，不用固定Pistol/Rifle名字推断来源。
    UAnimInstance* NativeLayer=Lyra?Lyra->GetLinkedAnimLayerInstanceByClass(ConfiguredLayer?*ConfiguredLayer:Lyra->DefaultLayer):nullptr;
    const FAnimNode_StateMachine* IdleMachine=NativeLayer?NativeLayer->GetStateMachineInstanceFromName(TEXT("IdleSM")):nullptr; // Presentation屏障后只读实际当前求值结果。
    const FBakedAnimationStateMachine* IdleDesc=NativeLayer?NativeLayer->GetStateMachineInstanceDesc(TEXT("IdleSM")):nullptr;
    float ActualIdleBreakWeight=-1.f; // -1为来源不可用，不能伪造成未激活。
    if (IdleMachine && IdleDesc) for (int32 Index=0;Index<IdleDesc->States.Num();++Index)
        if (IdleDesc->States[Index].StateName==TEXT("IdleBreak")) ActualIdleBreakWeight=IdleMachine->GetStateWeight(Index);
    const FBoolProperty* WantsBreak=NativeLayer?FindFProperty<FBoolProperty>(NativeLayer->GetClass(),TEXT("WantsIdleBreak")):nullptr;
    const FIntProperty* BreakIndex=NativeLayer?FindFProperty<FIntProperty>(NativeLayer->GetClass(),TEXT("CurrentIdleBreakIndex")):nullptr;
    const FNumericProperty* BreakDelay=NativeLayer?FindFProperty<FNumericProperty>(NativeLayer->GetClass(),TEXT("TimeUntilNextIdleBreak")):nullptr; // 蓝图Real可能是double，旧float-only诊断的-1代表不可读，不可当真实计时。
    Cells.Add(FireJitterNumber(Lyra?Lyra->NativeIdleBreakWeight:-1.f));Cells.Add(FireJitterNumber(ActualIdleBreakWeight));
    Cells.Add(FireJitterNumber(Lyra?Lyra->IdleBreakMotionWeight:-1.f));Cells.Add(FString::FromInt(Lyra && Lyra->bWeaponReadyLocomotion));
    Cells.Add(FString::FromInt(Player->GetCharacterMovement()->IsMovingOnGround()));Cells.Add(FireJitterCell(IdleMachine?IdleMachine->GetCurrentStateName().ToString():TEXT("unavailable")));
    Cells.Add(FString::FromInt(WantsBreak?WantsBreak->GetPropertyValue_InContainer(NativeLayer):-1));
    Cells.Add(FString::FromInt(BreakIndex?BreakIndex->GetPropertyValue_InContainer(NativeLayer):-1));
    Cells.Add(FireJitterNumber(BreakDelay && BreakDelay->IsFloatingPoint()?BreakDelay->GetFloatingPointPropertyValue(BreakDelay->ContainerPtrToValuePtr<void>(NativeLayer)):-1.f));
    Cells.Add(FString::FromInt(Lyra && Lyra->bAllowNativeIdleBreak));
    const FMatrix BodyWorld=Body->GetComponentTransform().ToMatrixWithScale(),CameraWorld=Player->GetFirstPersonCameraComponent()->GetComponentTransform().ToMatrixWithScale(); // 引擎原始行向量矩阵，世界cm；不转置后冒称列向量。
    for (int32 Row=0;Row<4;++Row) for (int32 Column=0;Column<4;++Column) Cells.Add(FireJitterNumber(BodyWorld.M[Row][Column]));
    for (int32 Row=0;Row<4;++Row) for (int32 Column=0;Column<4;++Column) Cells.Add(FireJitterNumber(CameraWorld.M[Row][Column]));
    FString PoseText; PoseText.Reserve(180000); // 仅本帧最多五套骨变换，写完释放；不会随录制时长增长。
    for (int32 Index=0;Index<Reference.GetNum();++Index)
    {
        if (Final[Index].ContainsNaN()) { FinishFireArmJitterAudit(false,TEXT("non-finite final bone transform")); return; }
        PoseText+=FireJitterPoseRow(FireArmJitterFrame,Stage,TEXT("Final"),Reference.GetBoneName(Index),Final[Index]);
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview")))
    {
        const USkeletalMeshComponent* OwnerBody=Player->GetPlayerPresentation()->GetFirstPersonBodyMesh(); // 已有共享身体Follower，仅记录最终值，不驱动/修改PP。
        if (OwnerBody && OwnerBody->GetSkeletalMeshAsset())
        {
            const FReferenceSkeleton& OwnerRef=OwnerBody->GetSkeletalMeshAsset()->GetRefSkeleton(); // Follower网格真实骨序，不当作主Body骨序。
            for (int32 Index=0;Index<OwnerRef.GetNum();++Index)
                PoseText+=FireJitterPoseRow(FireArmJitterFrame,Stage,TEXT("OwnerFinal"),OwnerRef.GetBoneName(Index),OwnerBody->GetSocketTransform(OwnerRef.GetBoneName(Index),RTS_Component)); // 通过真实Follower Socket映射读取Leader结果。
        }
    }
    const TArray<FTransform>* Stages[]={&Audit.FireBase,&Audit.FireUpper,&Audit.FireBeforeAim,&Audit.FireAfterAim,&Audit.FireAimLibrary,&Audit.FireArmSeed}; // 瞬时借用值副本数组，未执行阶段不会写出残留。
    static const TCHAR* Kinds[]={TEXT("Base"),TEXT("Upper"),TEXT("BeforeAim"),TEXT("AfterAim"),TEXT("AimLibrary"),TEXT("ArmSeed")};
    for (int32 Kind=0;Kind<6;++Kind)
    {
        if (!(Kind<2?bAnchor:bAim) || Stages[Kind]->Num()!=Audit.FirePoseNames.Num()) continue; // 未执行阶段明确缺失，不补假Pose。
        for (int32 Index=0;Index<Audit.FirePoseNames.Num();++Index)
        {
            const FTransform& Pose=(*Stages[Kind])[Index];
            if (Pose.ContainsNaN()) { FinishFireArmJitterAudit(false,TEXT("non-finite diagnostic bone transform")); return; }
            PoseText+=FireJitterPoseRow(FireArmJitterFrame,Stage,Kinds[Kind],Audit.FirePoseNames[Index],Pose);
        }
    }
    if (!FireJitterWrite(FireArmJitterFrames.Get(),FString::Join(Cells,TEXT(","))+TEXT("\n")) || !FireJitterWrite(FireArmJitterPoses.Get(),PoseText))
    { FinishFireArmJitterAudit(false,TEXT("per-frame CSV write failed")); return; }
    FireArmJitterFrames->Flush(); FireArmJitterPoses->Flush(); // 窗口异常退出也保留已完成帧；无逐骨磁盘open/close。
    FireArmJitterLastShot=Shot.Sequence; FireArmJitterFireSamples+=Anim->UpperBodyAction==TEXT("Fire")?1:0;
    GunElbowBoltSamples+=Anim->UpperBodyAction==TEXT("Bolt")?1:0; // 读取原真实动作，不为测试插入狙击Fire或修改机械间隔。
    FireArmJitterMaxSpeed=FMath::Max(FireArmJitterMaxSpeed,static_cast<float>(Player->GetVelocity().Size2D()));
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowSkin")))
    {
        const bool bSelected=FireArmJitterCaseFrame==FMath::RoundToInt(.5f*FireArmJitterSampleFPS)
            || FireArmJitterCaseFrame==FMath::RoundToInt(StageDuration*.5f*FireArmJitterSampleFPS)
            || FireArmJitterCaseFrame==FMath::RoundToInt((StageDuration-.3f)*FireArmJitterSampleFPS); // 同片段进入/中段/恢复，限定帧输出当前真正LOD蒙皮；不逐帧写全顶点。
        if (bSelected)
        {
            RecordGunElbowSkin(Body,Audit,TEXT("Body"));
            if (!bFireArmJitterFinishing && OwnerSkin)RecordGunElbowSkin(const_cast<USkeletalMeshComponent*>(OwnerSkin),Audit,TEXT("Owner"));
            if (bFireArmJitterFinishing)return;
        }
    }
    if (bFireArmJitterCapture && FApp::CanEverRender() && GetWorld()->GetTimeSeconds()-StageStart<StageDuration-.1f)
    {
        if (FScreenshotRequest::IsScreenshotRequested()) { FinishFireArmJitterAudit(false,TEXT("previous RHI screenshot was not consumed before next fixed frame")); return; }
        const FString Path=FireArmJitterDirectory/TEXT("Frames")/FString::Printf(TEXT("%02d_%05d.png"),Stage,FireArmJitterFrame);
        FScreenshotRequest::RequestScreenshot(Path,false,false); // 真实帧末RHI，保留HUD和原OwnerNoSee，不伪造模型或视角。
        ++FireArmJitterScreenshots; // 最后0.1秒仍采数值但不请求截图，避免随后切例/退出令末帧画面错标。
    }
    ++FireArmJitterFrame; ++FireArmJitterCaseFrame;
#endif
}

void ADemoPlayerAnimationTest::TickFireArmJitterAudit(float DeltaSeconds)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (bFireArmJitterFinishing) return;
    const float Now=GetWorld()->GetTimeSeconds();
    const bool bAllowedFPS=FireArmJitterSampleFPS==30.f || FireArmJitterSampleFPS==60.f || FireArmJitterSampleFPS==120.f; // 仅三档实际帧率，不允许任意小步长制造伪平滑。
    if (!bAllowedFPS || !FApp::UseFixedTimeStep() || !FMath::IsNearlyEqual(FApp::GetFixedDeltaTime(),1./FireArmJitterSampleFPS,.0001)
        || !FMath::IsNearlyEqual(DeltaSeconds,1.f/FireArmJitterSampleFPS,.0001f))
    { FinishFireArmJitterAudit(false,TEXT("requires matching -UseFixedTimeStep -FPS=30/60/120 and -DemoFireArmJitterFPS=same value")); return; }
    if (Stage<0)
    {
        if (FireArmJitterDirectory.IsEmpty()) FireArmJitterDirectory=GetReviewRoot()/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)); // 每进程唯一证据目录，保留旧诊断。
        if (!InitializeFixture()) return;
        IFileManager::Get().MakeDirectory(*FireArmJitterDirectory,true);
        if (bFireArmJitterCapture) IFileManager::Get().MakeDirectory(*(FireArmJitterDirectory/TEXT("Frames")),true);
        FireArmJitterFrames.Reset(IFileManager::Get().CreateFileWriter(*(FireArmJitterDirectory/TEXT("frames.csv"))));
        FireArmJitterPoses.Reset(IFileManager::Get().CreateFileWriter(*(FireArmJitterDirectory/TEXT("poses.csv"))));
        FireArmJitterEvents.Reset(IFileManager::Get().CreateFileWriter(*(FireArmJitterDirectory/TEXT("input_events.csv"))));
        if (!FireArmJitterFrames || !FireArmJitterPoses || !FireArmJitterEvents)
        { FinishFireArmJitterAudit(false,TEXT("could not create independent CSV streams")); return; }
const bool bHeaders=FireJitterWrite(FireArmJitterFrames.Get(),TEXT("frame,case_frame,stage,case,weapon,world_seconds,case_seconds,server_seconds,delta_seconds,shot_sequence,shot_start,shot_duration,shot_elapsed,action,previous_action,source_sequence,action_progress,upper_seconds,previous_upper_seconds,blend_alpha,alignment_weight,overlay_weight,idle_applied,speed_cm_s,ammo,control_pitch,control_yaw,focus_active,focus_cs_x,focus_cs_y,focus_cs_z,muzzle_local_x,muzzle_local_y,muzzle_local_z,shared_eye_world_x,shared_eye_world_y,shared_eye_world_z,actor_world_x,actor_world_y,actor_world_z,anchor_executed,aim_executed,anchor_count,aim_count,compact_names,final_mesh_bones,postprocess_active,additive_recoil,recoil_montage,recoil_source_seconds,recoil_phase,recoil_instance_id,recoil_weight,recoil_start_count,body_lod,owner_lod,body_forced_lod,owner_forced_lod,body_required_bones,pp_required_bones,owner_mesh,owner_leader,actual_fov,actual_view_target,native_idle_break_cached,native_idle_break_current,idle_motion_weight,weapon_ready,moving_on_ground,native_idle_state,native_wants_break,native_break_index,native_break_delay,native_allow_idle_break,body_world_ue_row_m00,body_world_ue_row_m01,body_world_ue_row_m02,body_world_ue_row_m03,body_world_ue_row_m10,body_world_ue_row_m11,body_world_ue_row_m12,body_world_ue_row_m13,body_world_ue_row_m20,body_world_ue_row_m21,body_world_ue_row_m22,body_world_ue_row_m23,body_world_ue_row_m30,body_world_ue_row_m31,body_world_ue_row_m32,body_world_ue_row_m33,camera_world_ue_row_m00,camera_world_ue_row_m01,camera_world_ue_row_m02,camera_world_ue_row_m03,camera_world_ue_row_m10,camera_world_ue_row_m11,camera_world_ue_row_m12,camera_world_ue_row_m13,camera_world_ue_row_m20,camera_world_ue_row_m21,camera_world_ue_row_m22,camera_world_ue_row_m23,camera_world_ue_row_m30,camera_world_ue_row_m31,camera_world_ue_row_m32,camera_world_ue_row_m33\n"))
            && FireJitterWrite(FireArmJitterPoses.Get(),TEXT("frame,stage,kind,bone,x,y,z,qx,qy,qz,qw,sx,sy,sz\n"))
            && FireJitterWrite(FireArmJitterEvents.Get(),TEXT("last_recorded_frame_plus_one,stage,world_seconds,event,shot_sequence_after_call,ammo_after_call\n"));
        if (!bHeaders) { FinishFireArmJitterAudit(false,TEXT("CSV header write failed")); return; }
        Report->SetStringField(TEXT("scope"),TEXT("Read-only real GAS fire diagnostic, six original or nine long-gun cases; current formal animation graph; input submitted only after sampled pose; no pose, Shot or recoil writes"));
        Report->SetStringField(TEXT("motion_isolation"),MotionIsolation.IsEmpty()?TEXT("Current"):MotionIsolation);
        Report->SetStringField(TEXT("sampling_order"),TEXT("PostUpdateWork after Body and Presentation -> actual pose CSV -> input request; request appears in next normal animation update"));
        Report->SetStringField(TEXT("pose_format"),TEXT("component-space centimetres, quaternion xyzw, actual scale; Final uses mesh reference bone order, stages use explicitly named Compact bones which may include virtual bones"));
        Report->SetStringField(TEXT("rhi_tail_policy"),TEXT("No screenshots requested in last 0.1s of each 3s case; numerical samples continue. This drains the last frame request before case transition or process exit."));
        Report->SetBoolField(TEXT("actual_rhi_requested"),bFireArmJitterCapture); Report->SetBoolField(TEXT("can_render"),FApp::CanEverRender());
        Report->SetStringField(TEXT("command_line"),FCommandLine::Get());
        Report->SetNumberField(TEXT("fixed_sampling_fps"),FireArmJitterSampleFPS); Report->SetBoolField(TEXT("aim_corner_plan"),bFireRecoilAimCorners);
        Report->SetNumberField(TEXT("isolated_gas_fire_rate_scale"),FireRecoilTestRateScale);
        Stage=ReloadHoldOnlyStage==INDEX_NONE?0:ReloadHoldOnlyStage; // 显式单例仍走真实装备/GAS初始化，完整默认计划不变。
        Report->SetNumberField(TEXT("explicit_only_stage"),ReloadHoldOnlyStage); // 单例输出不能被当成全部33例覆盖。
        if (!BeginFireArmJitterStage(Now)) { if (!bFireArmJitterFinishing && !bFailed) FinishFireArmJitterAudit(false,TEXT("first case setup failed")); }
        return; // 本Tick刚切场景/装备，禁止把切换前已经求值的旧骨记作新例第一帧。
    }
    if (!Player.IsValid() || !Weapon.IsValid()) { FinishFireArmJitterAudit(false,TEXT("real fixture lost pawn or weapon")); return; }
    const FVector Target=Player->GetActorLocation()+FVector(0,0,45); // 原外部观察相机跟随真实Pawn，仅取景，不驱动主相机/骨架。
    FString ElbowView; FParse::Value(FCommandLine::Get(),TEXT("DemoGunElbowView="),ElbowView); // 只识别有限摄影机位，不读取任意场景或改本机Camera/FOV。
    const FVector CameraPosition=Target+(ElbowView==TEXT("Left")?FVector(20,-270,40):ElbowView==TEXT("Front")?FVector(270,0,40):FVector(185,-230,55)); // 解剖左侧/正前方/原斜侧，所有实例仍真实可见网格。
    Camera->SetActorLocationAndRotation(CameraPosition,(Target-CameraPosition).Rotation());
    CastChecked<APlayerController>(Player->GetController())->SetViewTarget(bFirstPersonCapture?static_cast<AActor*>(Player.Get()):static_cast<AActor*>(Camera.Get()));
    RecordFireArmJitterFrame(DeltaSeconds); // 先固定当前输出，后续输入不能被误认为已经影响本帧Pose。
    if (bFireArmJitterFinishing) return;
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowOperations"))) { TickGunElbowOperationInputs(Now); return; } // 该帧已采真实最终Pose，再提交操作；不跑旧开火输入。
    const float Elapsed=Now-StageStart;
    const int32 Variant=Stage%3; // 0单发、1静止连发、2移动连发；同一枪的初始显示条件相同。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowSweep")) && Variant==0)
    {
        const float Phase=FMath::Clamp(Elapsed/StageDuration,0.f,1.f),Wave=FMath::Sin(Phase*2.f*PI); // 6秒真实扫过±极角及中立，使用现有Controller/相机约束。
        CastChecked<APlayerController>(Player->GetController())->SetControlRotation(FRotator(Wave*(Wave>=0.f?55.f:60.f),40.f*FMath::Sin(Phase*4.f*PI),0.f)); // 本帧骨姿已记录，输入仅下一次正常图求值消费。
    }
    const bool bOperationBoundaryCase=bFireRecoilOperationBoundary && Variant==0; // 仅此flag下的两次单发采用真实Reload结束边沿。
    if (Variant==2 && Elapsed>=.15f && Elapsed<2.4f) Player->AddMovementInput(FVector::ForwardVector,1.f); // 下帧由正常Movement消费，不写速度或步态。
    UDemoWeaponComponent* Equipment=Player->GetWeaponComponent();
    if (bOperationBoundaryCase)
    {
        if (Weapon->IsReloading()) bFireRecoilBoundaryObservedReload=true;
        else if (bFireRecoilBoundaryObservedReload && FireRecoilBoundaryReloadFinishedAt<0.f)
        {
            FireRecoilBoundaryReloadFinishedAt=Now; // 采样真实状态退出，不按配置时长推算或主动结束装填。
            UE_LOG(LogFPSDemo,Log,TEXT("FIRE_RECOIL_BOUNDARY_RELOAD_END stage=%d world=%.6f"),Stage,Now);
            if (!FireJitterEvent(FireArmJitterEvents.Get(),FireArmJitterFrame,Stage,Now,TEXT("boundary_reload_complete"),Weapon.Get()))
            { FinishFireArmJitterAudit(false,TEXT("boundary reload event write failed")); return; }
        }
        if (!bFireArmJitterPressed && FireRecoilBoundaryReloadFinishedAt>=0.f && Now-FireRecoilBoundaryReloadFinishedAt>=.02f)
        {
            Equipment->StartFire(); Equipment->StopFire(); bFireArmJitterPressed=bFireArmJitterReleased=true; // 一次真实点击，不重复重试被门禁拒绝的枪。
            const FDemoBoltCycleState BoundaryShot=Weapon->GetShotPresentationState(); // 成功枪起点来自原业务流程；不由测试写入。
            if (BoundaryShot.Sequence>FireArmJitterStartShot) FireRecoilBoundaryShotDelay=static_cast<float>(BoundaryShot.ServerStartTime)-FireRecoilBoundaryReloadFinishedAt;
            UE_LOG(LogFPSDemo,Log,TEXT("FIRE_RECOIL_BOUNDARY_FIRE stage=%d sequence=%d actual_delay=%.6f"),Stage,BoundaryShot.Sequence,FireRecoilBoundaryShotDelay);
            if (!FireJitterEvent(FireArmJitterEvents.Get(),FireArmJitterFrame,Stage,Now,TEXT("boundary_start_stop_fire"),Weapon.Get()))
            { FinishFireArmJitterAudit(false,TEXT("boundary fire event write failed")); return; }
        }
        const UDemoPlayerAnimInstance* BoundaryAnim=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 只读本帧已采样Body输出，与前面的CSV处于同一更新时刻。
        const bool bOldOperation=BoundaryAnim && (BoundaryAnim->PreviousUpperBodyAction==TEXT("Reload") || BoundaryAnim->PreviousUpperBodyAction==TEXT("EmptyReload") || BoundaryAnim->PreviousUpperBodyAction==TEXT("Bolt"));
        if (BoundaryAnim && BoundaryAnim->bUseAdditiveGunRecoil && bOldOperation)
        {
            if (++FireRecoilBoundaryOldOperationSamples==1)
                UE_LOG(LogFPSDemo,Error,TEXT("FIRE_RECOIL_BOUNDARY_OPERATION_REVIVED stage=%d previous=%s alpha=%.6f"),Stage,*BoundaryAnim->PreviousUpperBodyAction.ToString(),BoundaryAnim->UpperBodyBlendAlpha);
        }
        const USkeletalMeshComponent* BoundaryBody=Player->GetMesh(); // 记录真实Final父子骨，而非用画面位置判断左右或只读控制器。
        const FReferenceSkeleton& BoundaryReference=BoundaryBody->GetSkeletalMeshAsset()->GetRefSkeleton();
        const TArray<FTransform>& BoundaryFinal=BoundaryBody->GetComponentSpaceTransforms();
        static const FName BoundaryArmBones[]={TEXT("clavicle_l"),TEXT("upperarm_l"),TEXT("lowerarm_l"),TEXT("hand_l"),TEXT("clavicle_r"),TEXT("upperarm_r"),TEXT("lowerarm_r"),TEXT("hand_r")}; // 正式Manny主链；父骨从真实RefSkeleton查询。
        TArray<FQuat> CurrentArmLocals; CurrentArmLocals.Reserve(UE_ARRAY_COUNT(BoundaryArmBones)); // 本帧有限8个值，下面仅保存至下次专项采样。
        float AdjacentStep=0.f; // 同一真实渲染步长的最大局部角差，不插值或重采样成固定帧率。
        for (const FName Bone:BoundaryArmBones)
        {
            const int32 BoneIndex=BoundaryReference.FindBoneIndex(Bone);
            const int32 ParentIndex=BoneIndex!=INDEX_NONE?BoundaryReference.GetParentIndex(BoneIndex):INDEX_NONE;
            if (!BoundaryFinal.IsValidIndex(BoneIndex) || !BoundaryFinal.IsValidIndex(ParentIndex))
            { FinishFireArmJitterAudit(false,TEXT("boundary actual arm/parent bone missing")); return; }
            const FQuat Local=BoundaryFinal[BoneIndex].GetRelativeTransform(BoundaryFinal[ParentIndex]).GetRotation().GetNormalized(); // 实际父空间旋转，骨长/缩放从未写入。
            const int32 ArmIndex=CurrentArmLocals.Add(Local);
            if (FireRecoilBoundaryPreviousArmLocals.IsValidIndex(ArmIndex))
                AdjacentStep=FMath::Max(AdjacentStep,static_cast<float>(FMath::RadiansToDegrees(Local.AngularDistance(FireRecoilBoundaryPreviousArmLocals[ArmIndex])))); // UE5大世界FQuat为double，报告字段为有限float度数。
        }
        if (FireRecoilBoundaryReloadFinishedAt>=0.f && Weapon->GetShotPresentationState().Sequence>FireArmJitterStartShot)
        {
            FireRecoilBoundaryAdjacentLocalStepDegrees=FMath::Max(FireRecoilBoundaryAdjacentLocalStepDegrees,AdjacentStep);
            if (BoundaryAnim && BoundaryAnim->bUseAdditiveGunRecoil && !bFireRecoilBoundaryPreviousAdditive && FireRecoilBoundaryPreviousArmLocals.Num()==UE_ARRAY_COUNT(BoundaryArmBones))
            {
                bFireRecoilBoundaryActivationObserved=true;
                FireRecoilBoundaryActivationLocalStepDegrees=FMath::Max(FireRecoilBoundaryActivationLocalStepDegrees,AdjacentStep);
                FireRecoilBoundaryActivationSourceSeconds=BoundaryAnim->GunRecoilSourceTime;
                UE_LOG(LogFPSDemo,Log,TEXT("FIRE_RECOIL_BOUNDARY_ACTIVATION stage=%d frame=%d local_step_degrees=%.6f source_seconds=%.6f"),Stage,FireArmJitterFrame-1,AdjacentStep,FireRecoilBoundaryActivationSourceSeconds);
            }
        }
        FireRecoilBoundaryPreviousArmLocals=MoveTemp(CurrentArmLocals); bFireRecoilBoundaryPreviousAdditive=BoundaryAnim && BoundaryAnim->bUseAdditiveGunRecoil;
    }
    else if (Elapsed>=.6f && Elapsed<2.1f)
    {
        if (!bFireArmJitterPressed || (Variant!=0 && Weapon->Config.FireMode!=EDemoFireMode::Automatic && Now+.0001f>=FireArmJitterNextPress))
        {
            Equipment->StartFire(); bFireArmJitterPressed=true;
            FireArmJitterNextPress=Now+FMath::Max(1.f/FireArmJitterSampleFPS,Weapon->GetFireInterval()); // 半自动真实点击间隔，正常GAS再次独立检查冷却。
            if (!FireJitterEvent(FireArmJitterEvents.Get(),FireArmJitterFrame,Stage,Now,TEXT("start_fire"),Weapon.Get()))
            { FinishFireArmJitterAudit(false,TEXT("fire input event write failed")); return; }
            if (Variant==0 || Weapon->Config.FireMode!=EDemoFireMode::Automatic)
            {
                Equipment->StopFire();
                if (!FireJitterEvent(FireArmJitterEvents.Get(),FireArmJitterFrame,Stage,Now,TEXT("stop_fire_click"),Weapon.Get()))
                { FinishFireArmJitterAudit(false,TEXT("stop input event write failed")); return; }
            }
        }
    }
    if (!bOperationBoundaryCase && Elapsed>=2.1f && !bFireArmJitterReleased)
    {
        Equipment->StopFire(); bFireArmJitterReleased=true;
        if (!FireJitterEvent(FireArmJitterEvents.Get(),FireArmJitterFrame,Stage,Now,TEXT("stop_fire_window_end"),Weapon.Get()))
        { FinishFireArmJitterAudit(false,TEXT("window-end event write failed")); return; }
    }
    if (Elapsed<StageDuration) return;
    const int32 Shots=FireArmJitterLastShot-FireArmJitterStartShot;
    const UDemoLyraAnimInstance* FinalAnim=Cast<UDemoLyraAnimInstance>(Player->GetMesh()->GetAnimInstance()); // 原始实例启动计数不受诊断输入缓存影响。
    const int32 MontageStarts=FinalAnim?FinalAnim->GunRecoilMontageStartCount-FireRecoilStartMontageCount:0;
    const bool bExpectedAdditive=FinalAnim && FinalAnim->bUseAdditiveGunRecoil; // 旧正式对照仍null策略，不要求新Montage。
    const float ActivationStepSanityDegrees=5.f; // 专项不连续性门禁：排除已复现的33度瞬跳，非动作质量/人体角度或所有帧率通用美术验收阈值。
    const bool bBoundaryActivationPassed=bFireRecoilBoundaryActivationObserved && FireRecoilBoundaryActivationLocalStepDegrees<=ActivationStepSanityDegrees;
    const bool bBoundaryPassed=!bOperationBoundaryCase || (bExpectedAdditive && bFireRecoilBoundaryObservedReload && FireRecoilBoundaryReloadFinishedAt>=0.f
        && FireRecoilBoundaryShotDelay>=.019f && FireRecoilBoundaryShotDelay<=.101f && FireRecoilBoundaryOldOperationSamples==0 && bBoundaryActivationPassed); // 1ms仅容忍float时钟量化，60/120Hz均真实等待至少0.02秒。
    const int32 MinimumBurstShots=FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview"))?FMath::Min(Weapon->GetCapacity(),FMath::Min(3,1+FMath::FloorToInt(1.4f/Weapon->GetFireInterval()))):3; // 真实输入窗受容量/间隔约束；旧两枪仍3，不补弹/缩短拉栓来凑数。
    const bool bObservedShotMotion=FireArmJitterFireSamples>0 || (FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview")) && GunElbowBoltSamples>0); // 显式长枪诊断允许真实Bolt，原两枪仍必须Fire。
    const bool bCasePassed=(Variant==0?Shots==1:Shots>=MinimumBurstShots) && bObservedShotMotion && FireArmJitterCaseFrame>=FMath::FloorToInt(FireArmJitterSampleFPS*2.8f)
        && (!bExpectedAdditive || MontageStarts==Shots)
        && bBoundaryPassed
        && (Variant!=2 || FireArmJitterMaxSpeed>100.f); // 仅诊断覆盖门禁，不以角度阈值伪造美术验收。
    const TArray<TSharedPtr<FJsonValue>>* Cases=nullptr;
    if (Report->TryGetArrayField(TEXT("cases"),Cases) && Cases->IsValidIndex(Stage))
    {
        const TSharedPtr<FJsonObject> Case=(*Cases)[Stage]->AsObject();
        Case->SetNumberField(TEXT("recorded_frames"),FireArmJitterCaseFrame); Case->SetNumberField(TEXT("successful_shots"),Shots);
        Case->SetNumberField(TEXT("evaluated_fire_samples"),FireArmJitterFireSamples); Case->SetNumberField(TEXT("max_speed_cm_s"),FireArmJitterMaxSpeed);
        Case->SetNumberField(TEXT("evaluated_bolt_samples"),GunElbowBoltSamples);
        Case->SetBoolField(TEXT("capture_coverage_passed"),bCasePassed);
        Case->SetBoolField(TEXT("additive_policy"),bExpectedAdditive); Case->SetNumberField(TEXT("actual_body_montage_starts"),MontageStarts);
        if (bOperationBoundaryCase)
        {
            Case->SetBoolField(TEXT("real_reload_observed"),bFireRecoilBoundaryObservedReload); Case->SetNumberField(TEXT("reload_finished_world_seconds"),FireRecoilBoundaryReloadFinishedAt);
            Case->SetNumberField(TEXT("successful_shot_delay_after_reload_seconds"),FireRecoilBoundaryShotDelay); Case->SetNumberField(TEXT("revived_operation_samples"),FireRecoilBoundaryOldOperationSamples);
            Case->SetBoolField(TEXT("operation_boundary_passed"),bBoundaryPassed);
            Case->SetBoolField(TEXT("additive_activation_observed"),bFireRecoilBoundaryActivationObserved); Case->SetBoolField(TEXT("activation_step_sanity_passed"),bBoundaryActivationPassed);
            Case->SetNumberField(TEXT("activation_local_step_degrees"),FireRecoilBoundaryActivationLocalStepDegrees); Case->SetNumberField(TEXT("activation_step_sanity_limit_degrees"),ActivationStepSanityDegrees);
            Case->SetNumberField(TEXT("post_shot_max_adjacent_local_step_degrees"),FireRecoilBoundaryAdjacentLocalStepDegrees); Case->SetNumberField(TEXT("activation_source_seconds"),FireRecoilBoundaryActivationSourceSeconds);
            UE_LOG(LogFPSDemo,Display,TEXT("FIRE_RECOIL_BOUNDARY_RESULT stage=%d observed=%d delay=%.6f revived=%d shots=%d starts=%d activation_step_degrees=%.6f source_seconds=%.6f passed=%d"),Stage,bFireRecoilBoundaryObservedReload,FireRecoilBoundaryShotDelay,FireRecoilBoundaryOldOperationSamples,Shots,MontageStarts,FireRecoilBoundaryActivationLocalStepDegrees,FireRecoilBoundaryActivationSourceSeconds,bBoundaryPassed);
        }
    }
    UE_LOG(LogFPSDemo,Display,TEXT("FIRE_ARM_JITTER_CASE_END index=%d name=%s frames=%d shots=%d fire_samples=%d max_speed=%.3f covered=%d"),
        Stage,*StageName,FireArmJitterCaseFrame,Shots,FireArmJitterFireSamples,FireArmJitterMaxSpeed,bCasePassed);
    if (!bCasePassed) { FinishFireArmJitterAudit(false,TEXT("real fire/movement sampling coverage failed; inspect recorded inputs and frames")); return; }
    const int32 PlannedCases=FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview"))?9:6; // 三枪审查保持单发/连发/移动各例；其他调用仍为原六例。
    if (++Stage==PlannedCases) { FinishFireArmJitterAudit(true,TEXT("planned real cases captured; pose diagnosis and visual acceptance pending")); return; }
    if (!BeginFireArmJitterStage(Now) && !bFireArmJitterFinishing && !bFailed) FinishFireArmJitterAudit(false,TEXT("next case setup failed"));
#endif
}

void ADemoPlayerAnimationTest::FinishFireArmJitterAudit(bool bSuccess,const FString& Reason)
{
    DEMO_LOG_CALL();
    if (bFireArmJitterFinishing) return;
    bFireArmJitterFinishing=true; SetActorTickEnabled(false);
    if (Player.IsValid())
    {
        Player->GetWeaponComponent()->StopFire(); Player->ConsumeMovementInputVector();
        Player->GetCharacterMovement()->StopMovementImmediately(); // 仅清理本隔离World输入，不取消用户其它World任务。
    }
    for (FArchive* Stream:{FireArmJitterFrames.Get(),FireArmJitterPoses.Get(),FireArmJitterEvents.Get()})
        if (Stream) { Stream->Flush(); bSuccess&=!Stream->IsError(); }
    FireArmJitterFrames.Reset(); FireArmJitterPoses.Reset(); FireArmJitterEvents.Reset();
    if (!Report) Report=MakeShared<FJsonObject>(); // 初始化门禁失败仍留下可复核原因，不丢失失败阶段。
    Report->SetStringField(TEXT("result"),bSuccess?TEXT("CAPTURE_COMPLETE_DIAGNOSIS_PENDING"):TEXT("CAPTURE_FAILED"));
    Report->SetStringField(TEXT("reason"),Reason); Report->SetNumberField(TEXT("recorded_frames"),FireArmJitterFrame);
    Report->SetNumberField(TEXT("rhi_screenshot_requests"),FireArmJitterScreenshots);
    Report->SetBoolField(TEXT("visual_accepted"),false); Report->SetBoolField(TEXT("source_or_asset_modified"),false); // 兼容旧报告字段，source只指动画制作源。
    Report->SetBoolField(TEXT("source_animation_or_content_modified"),false); // 诊断CPP埋点不是动作/Content修改，明确两种范围。
    Report->SetBoolField(TEXT("diagnostic_cpp_added"),true);
    if (FireArmJitterDirectory.IsEmpty()) FireArmJitterDirectory=GetReviewRoot()/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_failed"));
    IFileManager::Get().MakeDirectory(*FireArmJitterDirectory,true);
    FString Json; const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json); // 仅一次写小型manifest，大型骨数据已流式保存。
    if (!FJsonSerializer::Serialize(Report.ToSharedRef(),Writer)
        || !FFileHelper::SaveStringToFile(Json,*(FireArmJitterDirectory/TEXT("manifest.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bSuccess=false;
    UE_LOG(LogFPSDemo,Display,TEXT("FIRE_ARM_JITTER_CAPTURE_%s frames=%d directory=%s reason=%s"),bSuccess?TEXT("SUCCESS"):TEXT("FAILED"),FireArmJitterFrame,*FireArmJitterDirectory,*Reason);
    FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
}
