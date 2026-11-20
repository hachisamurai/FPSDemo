// 职责：近战真实输入/时序/伤害/遮挡与双视角验收实现。仅Editor显式启动；单横扫先选真实核心并等待战斗准备，WideTest仍为历史双攻击夹具；依赖生产GAS、准备屏障及真实骨骼碰撞。
#include "Tests/DemoMeleeTest.h"
#include "Tests/DemoProjectileFixtures.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Weapons/Melee/DemoMeleeAbility.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h" // 单横扫夹具必须确认真正进入Combat，不能在Hub无伤害时误报动画范围错误。
#include "Progression/Builds/DemoRunBuildComponent.h" // 新战役出发前通过既有权威接口选择本轮核心。
#include "AI/DemoEnemy.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoMeleeAnimInstance.h" // 新Idle专项只读最终GT快照，与已求值骨矩阵同时取证。
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Animation/AnimSequence.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformProcess.h"
#include "Animation/DemoPlayerPresentationComponent.h" // 显示策略可选共享全身，旧双臂断言不能误报新契约。
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h" // FInputKeyParams完整类型，按键验收走真实输入链。
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/Abilities/DemoFireAbility.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h" // 视觉验收必须记录最终生效的相机/FOV，而非仅相信配置眼位。
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h" // 截图同时保存实际组件骨矩阵，定位离线序列与AnimBP运行结果的差异。
#include "HAL/FileManager.h" // 新全身第一人称证据使用独立目录，保留旧图。
#include "Serialization/JsonSerializer.h"
#include "Engine/SkeletalMesh.h"
#include "Debug/DemoLog.h"

namespace
{
    /** Value为当前骨组件空间变换；生成列向量矩阵，仅供离线变形审计，不参与玩法复制。 */
    TSharedPtr<FJsonValue> MeleeAuditMatrix(const FTransform& Value)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] MeleeAuditMatrix"));
        const FVector X=Value.TransformVector(FVector::ForwardVector),Y=Value.TransformVector(FVector::RightVector),Z=Value.TransformVector(FVector::UpVector),P=Value.GetTranslation(); // 三基向量含缩放，P单位cm。
        const double Cells[]={X.X,Y.X,Z.X,P.X,X.Y,Y.Y,Z.Y,P.Y,X.Z,Y.Z,Z.Z,P.Z,0,0,0,1}; // 按行展平的列向量矩阵。
        TArray<TSharedPtr<FJsonValue>> Result; // 本次同步序列化所有权，完成后由JSON报告持有。
        for (double Cell : Cells) Result.Add(MakeShared<FJsonValueNumber>(Cell));
        return MakeShared<FJsonValueArray>(Result);
    }
}

ADemoMeleeTest::ADemoMeleeTest() { DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.015f; PrimaryActorTick.TickGroup=TG_PostUpdateWork; /* 在动画求值后采样实际骨姿，避免用前帧混合状态冒充当前画面。 */ }
bool ADemoMeleeTest::Check(bool Value,const TCHAR* Message)
{
    DEMO_LOG_CALL(); UE_LOG(LogFPSDemo,Display,TEXT("MELEE_TEST %s step=%d %s"),Value?TEXT("PASS"):TEXT("FAIL"),Step,Message);
    if (!Value) { bFailed=true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,1); } return Value;
}
void ADemoMeleeTest::Advance(float Seconds) { DEMO_LOG_CALL(); ++Step; NextTime=GetWorld()->GetTimeSeconds()+Seconds; }
void ADemoMeleeTest::Capture(const TCHAR* Name)
{
    DEMO_LOG_CALL();
    if (!FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeCapture"))) return;
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeMotionCapture")) && !FString(Name).StartsWith(TEXT("Motion-"))) return; // 连续录制时每帧只留一个请求，避免命名关键帧覆盖帧序列导致跳帧。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoNativeIdleAudit")) && NativeIdleEvidenceDir.IsEmpty())
        NativeIdleEvidenceDir=FPaths::ProjectSavedDir()/TEXT("MeleeIdleAim20261115/BodyViews")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+FString::Printf(TEXT("_%u"),FPlatformProcess::GetCurrentProcessId())); // 每次运行独立，失败图也保留。
    const FString Review=!NativeIdleEvidenceDir.IsEmpty()?NativeIdleEvidenceDir:FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("DemoFullBodyChecks"))?TEXT("FullBodyView20261029/FormalMelee"):TEXT("MeleeAcceptance")); // 不覆盖旧独立双臂验收图。
    IFileManager::Get().MakeDirectory(*Review,true);
    FScreenshotRequest::RequestScreenshot(Review/Name,false,false);
    ADemoCharacter* Player=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 当前World短期借用，不跨旅行持有。
    if (!Player) return;
    TArray<TSharedPtr<FJsonValue>> Samples; // 同截图时刻各可见骨组件，JSON只落Saved。
    TInlineComponentArray<USkeletalMeshComponent*> Components(Player); // 同时记录实际FP/TP骨姿，避免以作者文件替代运行图结果。
    for (USkeletalMeshComponent* Component : Components)
    {
        if (Component->GetFName()!=TEXT("MeleeFirstPersonArms") && Component!=Player->GetMesh()) continue;
        TSharedPtr<FJsonObject> Sample=MakeShared<FJsonObject>(),Bones=MakeShared<FJsonObject>(); // 当前组件快照，不保留UObject引用。
        Sample->SetStringField(TEXT("component"),Component->GetName()); Sample->SetStringField(TEXT("mesh"),Component->GetSkeletalMeshAsset()->GetPathName());
        Sample->SetField(TEXT("world"),MeleeAuditMatrix(Component->GetComponentTransform())); // 两套视角坐标不同，离线审计必须使用各自实际组件变换。
        Sample->SetNumberField(TEXT("lod"),Component->GetPredictedLODLevel());
        for (int32 Index=0;Index<Component->GetNumBones();++Index) Bones->SetField(Component->GetBoneName(Index).ToString(),MeleeAuditMatrix(Component->GetComponentSpaceTransforms()[Index])); // 实际运行全骨，不重求IK。
        Sample->SetObjectField(TEXT("bones"),Bones); Samples.Add(MakeShared<FJsonValueObject>(Sample));
    }
    TSharedPtr<FJsonObject> Report=MakeShared<FJsonObject>(); Report->SetArrayField(TEXT("components"),Samples); // 每截图单独文件，失败截图也能回溯。
    if (!NativeIdleEvidenceDir.IsEmpty())
    {
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player); // 当前已求值的真实FP实例，不另行Tick或采样资源。
        for (USkeletalMeshComponent* Mesh:Meshes) if (const UDemoMeleeAnimInstance* Anim=Cast<UDemoMeleeAnimInstance>(Mesh->GetAnimInstance()))
        {
            Report->SetStringField(TEXT("fp_actual_pose"),GetPathNameSafe(Anim->Pose)); Report->SetNumberField(TEXT("fp_actual_time"),Anim->PoseTime);
            Report->SetStringField(TEXT("fp_look_offset"),GetPathNameSafe(Anim->LookOffset)); Report->SetNumberField(TEXT("fp_look_pitch"),Anim->LookPitch); Report->SetNumberField(TEXT("fp_look_weight"),Anim->LookWeight);
        }
        TArray<TSharedPtr<FJsonValue>> Weapons; TInlineComponentArray<UStaticMeshComponent*> Statics(Player); // 真实世界刀及相机刀各记投影/显隐，不以标志冒充地面视觉通过。
        for (UStaticMeshComponent* Mesh:Statics) if (Mesh->GetName().Contains(TEXT("Melee")))
        {
            TSharedPtr<FJsonObject> Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("component"),Mesh->GetName()); Value->SetStringField(TEXT("mesh"),GetPathNameSafe(Mesh->GetStaticMesh()));
            Value->SetBoolField(TEXT("cast_shadow"),Mesh->CastShadow); Value->SetBoolField(TEXT("cast_hidden_shadow"),Mesh->bCastHiddenShadow); Value->SetBoolField(TEXT("owner_no_see"),Mesh->bOwnerNoSee); Value->SetBoolField(TEXT("hidden_in_game"),Mesh->bHiddenInGame); Weapons.Add(MakeShared<FJsonValueObject>(Value));
        }
        Report->SetArrayField(TEXT("melee_world_and_fp_weapons"),Weapons);
        UE_LOG(LogFPSDemo,Log,TEXT("NATIVE_MELEE_VIEW_CAPTURE directory=%s name=%s"),*Review,Name);
    }
    const ADemoPlayerController* PC=Cast<ADemoPlayerController>(Player->GetController()); // 本测试只有一个本地观察者，PC相机管理器是最终视图真值。
    if (PC && PC->PlayerCameraManager)
    {
        Report->SetField(TEXT("body_world"),MeleeAuditMatrix(Player->GetMesh()->GetComponentTransform()));
        Report->SetField(TEXT("camera_world"),MeleeAuditMatrix(FTransform(PC->PlayerCameraManager->GetCameraRotation(),PC->PlayerCameraManager->GetCameraLocation())));
        Report->SetNumberField(TEXT("camera_fov"),PC->PlayerCameraManager->GetFOVAngle());
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_VIEW_CAPTURE name=%s camera=%s sharedEye=%s fov=%.2f"),Name,*PC->PlayerCameraManager->GetCameraLocation().ToString(),*Player->GetSharedViewLocation().ToString(),PC->PlayerCameraManager->GetFOVAngle()); // 每张截图一次诊断事件，便于排除错相机/旧图。
    }
    FString Encoded; // UTF-8 JSON待写内容，序列化完成前不改旧报告。
    if (FJsonSerializer::Serialize(Report.ToSharedRef(),TJsonWriterFactory<>::Create(&Encoded))) FFileHelper::SaveStringToFile(Encoded,*(Review/FPaths::ChangeExtension(Name,TEXT("json")))); // 骨姿与真实图同目录。
}
void ADemoMeleeTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeCameraFeedbackTest"))) { TickMeleeCameraFeedbackAudit(DeltaSeconds); return; } // 独立真实HP回执专项优先，旧测试分发与配置完全保留。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeComboRegression"))) { TickComboRegression(DeltaSeconds); return; } // 独立-game专用入口，不能落入旧时长断言或自动退出其他模式。
#endif
    if (!bFailed && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeRigAudit"))) { TickRigAudit(); return; } // 静态诊断优先，避免进入历史伤害夹具。
    if (!bFailed && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTrialRecording"))) { TickTrialRecording(); return; } // 独立实机录制，不进入Flying伤害靶或历史双攻击计划。
    // 单招式DA使用独立验收，不把已撤销的下劈当作必需项；仍保留旧多武器测试入口。
    if (!bFailed && FParse::Param(FCommandLine::Get(),TEXT("DemoApexMeleeTest"))) { TickApexSweep(); return; }
    if (!bFailed && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeMotionCapture")) && ((Step>=9 && Step<=13) || (Step>=16 && Step<=19))
        && !((Step==13 || Step==19) && GetWorld()->GetTimeSeconds()>=NextTime)) // 只录两段固定观察角的完整动作；镜头切换/后续换枪另由关键帧专项验收，不混入同一动画预览。
    {
        const bool bFP=Step<14; // 第一段FP横挥/纵劈，第二段TP横挥；含蓄势、完整回收和短待机。
        int32& Frame=bFP?FirstPersonMotionFrame:ThirdPersonMotionFrame; // 测试Actor本地编号，不写生产状态；每次运行从0开始。
        const FString Name=FString::Printf(TEXT("Motion-%s-%05d.png"),bFP?TEXT("FP"):TEXT("TP"),Frame++); // 配合固定60FPS导出连续预览，非游戏录制服务。
        Capture(*Name);
    }
    if (bFailed || GetWorld()->GetTimeSeconds()<NextTime) return;
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoBodyViewTest"))) { TickSharedBodyViews(); return; } // 同一夹具的独立视觉流程，不让伤害靶阻断眼位验收。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeWideTest"))) { TickWideSweep(); return; } // 新范围专项使用独立计划，避免原用例阶段条件掩盖侧面漏击。
    if (!Check(GetWorld()->GetTimeSeconds()<40.f,TEXT("bounded runtime"))) return;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前权威规则，仅本帧借用。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 独立测试进程本机控制器。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 不跨World缓存玩家。
    UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 生产默认组件。
    if (!Check(Mode && PC && Player && Melee && Melee->GetDefinition(),TEXT("runtime assets loaded"))) return;
    if (!Check(Melee->GetDefinition()->Attacks.IsValidIndex(1),TEXT("two authored attacks configured"))) return;
    const float HorizontalDuration=Melee->GetDefinition()->Attacks[0].Duration; // 生产DA秒数，避免提速后仍在旧时间截图而漏掉实际击打姿态。
    const float VerticalDuration=Melee->GetDefinition()->Attacks[1].Duration; // 纵劈单独时基，测试不覆盖或修改正式时长。
    switch (Step)
    {
    case 0:
        if (!Check(Mode->StartRun(),TEXT("real run enters hub"))) return;
        PC->OnRunReady();
        PC->InputKey(FInputKeyParams(EKeys::Three,IE_Pressed,1.)); Advance(.12f); break;
    case 1:
        PC->InputKey(FInputKeyParams(EKeys::Three,IE_Released,0.));
        if (!Check(Melee->IsEquipped(),TEXT("key 3 equips default bat without purchase"))) return;
        Melee->RequestAttack(0);
        if (!Check(Melee->IsAttacking(),TEXT("hub allows melee swing without combat damage authority"))) return; // 新规则允许安全区挥击；伤害窗口仍必须绑定Combat会话。
        Mode->StartNextLevel();
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 隔离天然敌人，保留注册避免空关结算。
        Player->SetActorLocation(FVector(0,0,20000)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator::ZeroRotator); Player->SetActorRotation(FRotator::ZeroRotator);
        {
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 靶夹具不受测试平台天然碰撞影响。
            ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),Player->GetActorLocation()+FVector(95,0,0),FRotator(0,180,0),Params); // 正前近距离真实骨骼靶。
            FDemoEnemySpawnStats Stats; Stats.Health=1000.f; Stats.AttackPower=0.f; // 高生命避免意外死亡中断时序。
            Enemy->Configure(Stats); Enemy->SetActorTickEnabled(false); Target=Enemy;
            DemoHitZoneTest::Pose(Enemy,TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f);
            ADirectionalLight* Light=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-40,130,0)); // 临时补光只用于验收截图，不保存地图。
            Light->GetLightComponent()->SetMobility(EComponentMobility::Movable); Light->GetLightComponent()->SetIntensity(3.f);
        }
        BeforeAmmo=Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo();
        Advance(FMath::Max(.2f,HorizontalDuration+.1f)); break; // 开关卡取消Hub动作仍保留后摇截止，等待后再测战斗输入，不能让测试绕过生产冷却。
    case 2:
        Capture(TEXT("01-FirstPersonIdle.png"));
        BeforeHealth=Target->GetHealth(); Player->GetWeaponComponent()->StartFire(); Serial=Melee->GetAttackSerial();
        if (!Check(Melee->IsAttacking(),TEXT("left click routes to horizontal GAS"))) return;
        Melee->RequestAttack(1); Melee->RequestAttack(-1); Melee->RequestAttack(999);
        if (!Check(Melee->GetAttackSerial()==Serial,TEXT("reentrant and invalid attacks rejected"))) return;
        Advance(HorizontalDuration*.12f); break; // 12%仍在25%命中窗口前，并为15FPS长帧保留余量。
    case 3:
        if (!Check(Target->GetHealth()==BeforeHealth,TEXT("windup cannot damage"))) return;
        Advance(HorizontalDuration*.31f); break; // 从前摇检查到约43%实际触击，不绑定旧0.85秒资源长度。
    case 4:
        Capture(TEXT("02-FirstPersonHorizontal.png")); Advance(.6f); break;
    case 5:
        if (!Check(Target->GetHealth()<BeforeHealth && BeforeHealth-Target->GetHealth()<=70.01f,TEXT("horizontal sweep hits once using bone multiplier"))) return;
        if (!Check(Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo()==BeforeAmmo,TEXT("melee preserves firearm ammo"))) return;
        BeforeHealth=Target->GetHealth(); Player->GetWeaponComponent()->CycleAim(); Advance(VerticalDuration*.46f); break; // 纵劈取样接近触击峰值。
    case 6:
        Capture(TEXT("03-FirstPersonVertical.png")); Advance(.75f); break;
    case 7:
        if (!Check(Target->GetHealth()<BeforeHealth && BeforeHealth-Target->GetHealth()<=110.01f,TEXT("right click vertical damages once"))) return;
        BeforeHealth=Target->GetHealth(); Melee->RequestAttack(0); Player->GetWeaponComponent()->EquipSlot(2); Melee->SetEquipped(true); Melee->RequestAttack(0);
        if (!Check(!Melee->IsAttacking(),TEXT("switch cancels and preserves recovery deadline"))) return;
        Advance(1.f); break;
    case 8:
        if (!Check(Target->GetHealth()==BeforeHealth,TEXT("cancelled attack has no late damage"))) return;
        {
            AActor* Blocker=GetWorld()->SpawnActor<AActor>(); UBoxComponent* Box=NewObject<UBoxComponent>(Blocker); // 真实薄墙覆盖胸口至靶的可见路径。
            Blocker->SetRootComponent(Box); Box->SetBoxExtent(FVector(2,160,180)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
            Blocker->SetActorLocation(Player->GetActorLocation()+FVector(48,0,0)); Wall=Blocker;
        }
        Melee->RequestAttack(0); Advance(1.f); break;
    case 9:
        if (!Check(Target->GetHealth()==BeforeHealth,TEXT("wall blocks melee through-wall damage"))) return;
        Wall->Destroy(); Target->SetActorEnableCollision(false); Target->SetActorHiddenInGame(true); // TP验收隔离靶遮挡，碰撞断言已经完成。
        Camera=GetWorld()->SpawnActor<ACameraActor>(Player->GetActorLocation()+FVector(280,-320,80),FRotator::ZeroRotator);
        Camera->SetActorRotation((Player->GetActorLocation()+FVector(0,0,12)-Camera->GetActorLocation()).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(55.f);
        PC->SetViewTarget(Camera.Get()); Advance(.2f); break;
    case 10:
        Capture(TEXT("04-ThirdPersonIdle.png")); Melee->RequestAttack(0); Advance(HorizontalDuration*.43f); break; // TP与FP同一相位检查握持。
    case 11:
        Capture(TEXT("05-ThirdPersonHorizontal.png"));
        if (const UDemoPlayerAnimInstance* Anim=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance()))
            if (!Check(Anim->UpperBodyAction==TEXT("BatHorizontal"),TEXT("existing full body layer consumes melee DA sequence"))) return;
        Advance(.7f); break;
    case 12:
        Melee->RequestAttack(1); Advance(VerticalDuration*.28f); break; // 蓄力最高点，检查前臂与脸部净空。
    case 13:
        Capture(TEXT("06-ThirdPersonVertical.png")); Advance(1.f); break;
    case 14:
        PC->SetViewTarget(Player); PC->InputKey(FInputKeyParams(EKeys::Two,IE_Pressed,1.)); Advance(.1f); break;
    case 15:
        PC->InputKey(FInputKeyParams(EKeys::Two,IE_Released,0.));
        if (!Check(!Melee->IsEquipped() && Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo()==BeforeAmmo,TEXT("key 2 restores gun with preserved ammunition"))) return;
        Capture(TEXT("07-ReturnToPistol.png")); Advance(.2f); break;
    case 16:
        Melee->SetEquipped(true); PC->SetControlRotation(FRotator(55,0,0)); Advance(.3f); break;
    case 17:
        Capture(TEXT("08-LookUpIdle.png")); Advance(.15f); break;
    case 18:
        Melee->RequestAttack(0); Advance(HorizontalDuration*.43f); break; // 极限抬头下仍捕捉实际横挥。
    case 19:
        Capture(TEXT("09-LookUpHorizontal.png")); Advance(.65f); break;
    case 20:
        PC->SetControlRotation(FRotator(-60,0,0)); Advance(.3f); break;
    case 21:
        Capture(TEXT("10-LookDownIdle.png")); Advance(.15f); break;
    case 22:
        Melee->RequestAttack(1); Advance(VerticalDuration*.43f); break; // 极限低头检查，跟随DA提速。
    case 23:
        Capture(TEXT("11-LookDownVertical.png")); Advance(.8f); break;
    case 24:
        PC->SetControlRotation(FRotator::ZeroRotator);
        Player->GetDemoASC()->ActivateDemoAbility(UDemoFireAbility::StaticClass()); // 恶意绕过输入直接请求枪GA，也不能使用隐藏枪械。
        if (!Check(Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo()==BeforeAmmo,TEXT("direct firearm GAS blocked while bat equipped"))) return;
        Player->GetWeaponComponent()->EquipSlot(2); Player->GetWeaponComponent()->StartFire(); Advance(.15f); break;
    case 25:
        Player->GetWeaponComponent()->StopFire();
        if (!Check(Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo()<BeforeAmmo,TEXT("restored firearm still fires via GAS"))) return;
        Advance(.2f); break;
    default:
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_TEST_SUCCESS steps=26")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
}

void ADemoMeleeTest::TickApexSweep()
{
    DEMO_LOG_TICK();
    const double Now=GetWorld()->GetTimeSeconds(); // 本World游戏秒，兼容固定/低帧率测试。
    if (Target.IsValid() && ApexFirstHit<0. && Target->GetHealth()<BeforeHealth) ApexFirstHit=Now-ApexAttackStart;
    if (Now<NextTime) return;
    if (!Check(Now<90.,TEXT("single sweep bounded runtime"))) return;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前同步夹具的真实模式。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 唯一本地测试控制器。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 旅行后重新解析，不缓存旧Pawn。
    UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 正式武器逻辑，不模拟命中函数。
    if (!Check(Mode && Player && Melee && Melee->GetDefinition(),TEXT("single sweep runtime assets"))) return;
    const UDemoMeleeDefinition* Definition=Melee->GetDefinition(); // 只读配置，用实际时长驱动等待。
    if (!Check(Definition->Attacks.Num()==1,TEXT("only accepted horizontal attack configured"))) return;
    const FDemoMeleeAttack& Attack=Definition->Attacks[0]; // 不覆盖伤害/时长/检测半径以伪造通过。
    if (Step==0)
    {
        if (!Check(Mode->StartRun(),TEXT("single sweep enters hub"))) return;
        PC->OnRunReady(); Melee->SetEquipped(true); Melee->RequestAttack(1);
        if (!Check(!Melee->IsAttacking(),TEXT("removed secondary input rejected"))) return;
        Melee->RequestAttack(0);
        if (!Check(Melee->IsAttacking(),TEXT("hub supports horizontal presentation"))) return;
        Melee->CancelAttack(); Advance(Attack.Duration+.15f); return;
    }
    if (Step==1)
    {
        ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 同World当前权威阶段，异步准备完成后才布置靶。
        if (!Check(State!=nullptr,TEXT("single sweep authoritative game state"))) return;
        if (State->Phase!=EDemoPhase::Combat)
        {
            UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Player); // 本次Editor隔离PS的临时核心，不改任何持久档或跳过生产权限。
            if (!Check(Build && (Build->HasSelectedCore() || Build->TrySelectCore(TEXT("B01"))),TEXT("single sweep fixture selects real core"))) return;
            Mode->StartNextLevel(); // 正式入口可能等待房间资源/地面ACK；下一Tick重新确认，不伪造Combat枚举。
            if (State->Phase!=EDemoPhase::Combat)
            {
                UE_LOG(LogFPSDemo,Log,TEXT("MELEE_FIXTURE_WAIT_COMBAT phase=%d reason=%s"),int32(State->Phase),*State->FailureReason);
                NextTime=Now+.1; return; // 复用本专项90秒总上界，避免失败后在安全区继续测伤害。
            }
        }
        if (!Check(State->LevelNumber==1,TEXT("single sweep real first-level combat active"))) return;
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 隔离天然AI，保留会话和关卡有效。
        Player->SetActorLocation(FVector(0,0,20000)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator::ZeroRotator); Player->SetActorRotation(FRotator::ZeroRotator);
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 专项靶生成不受浮空夹具碰撞挤开。
        ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),Player->GetActorLocation()+FVector(95,0,0),FRotator(0,180,0),Params); // 使用真实骨架/PhysicsAsset。
        FDemoEnemySpawnStats Stats; Stats.Health=10000.f; Stats.AttackPower=0.f; // 高生命保留整组用例会话，无敌人攻击干扰。
        Enemy->Configure(Stats); Enemy->SetActorTickEnabled(false); Target=Enemy;
        DemoHitZoneTest::Pose(Enemy,TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f);
        ADirectionalLight* Light=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-40,130,0)); // 只照亮临时验收画面，不保存地图。
        Light->GetLightComponent()->SetMobility(EComponentMobility::Movable); Light->GetLightComponent()->SetIntensity(3.f);
        BeforeAmmo=Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo(); Advance(.2f); return;
    }
    if (Step==2)
    {
        // 单位cm：近/中/中远、左右偏移、超距、墙后、取消。每轮使用同一真实敌人姿势。
        const FVector Positions[]={FVector(60,0,0),FVector(95,0,0),FVector(130,0,0),FVector(95,-40,0),FVector(95,40,0),FVector(260,0,0),FVector(95,0,0),FVector(95,0,0)};
        Target->SetActorLocation(Player->GetActorLocation()+Positions[ApexCase]);
        if (ApexCase==6)
        {
            AActor* Blocker=GetWorld()->SpawnActor<AActor>(); // World持有的薄墙，不引用任何正式地图资源。
            UBoxComponent* Box=NewObject<UBoxComponent>(Blocker); // 当前Actor根碰撞，销毁墙时一起释放。
            Blocker->SetRootComponent(Box); Box->SetBoxExtent(FVector(2,160,180)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
            Blocker->SetActorLocation(Player->GetActorLocation()+FVector(48,0,0)); Wall=Blocker;
        }
        BeforeHealth=Target->GetHealth(); ApexFirstHit=-1.; ApexAttackStart=Now;
        Player->GetWeaponComponent()->StartFire(); Serial=Melee->GetAttackSerial();
        if (!Check(Melee->IsAttacking(),TEXT("primary input starts accepted sweep"))) return;
        Melee->RequestAttack(0); Melee->RequestAttack(1); Melee->RequestAttack(999);
        if (!Check(Melee->GetAttackSerial()==Serial,TEXT("reentry/secondary/out of bounds cannot restart"))) return;
        if (ApexCase==7) Melee->CancelAttack();
        Advance(.1f); return;
    }
    if (Step==3)
    {
        if (!Check(Target->GetHealth()==BeforeHealth,TEXT("windup cannot damage early"))) return;
        if (ApexCase==0) Capture(TEXT("Apex-FP-Windup.png"));
        Advance(.28f); return;
    }
    if (Step==4)
    {
        if (ApexCase==0) Capture(TEXT("Apex-FP-Sweep.png"));
        Advance(Attack.Duration+.15f); return;
    }
    if (Step==5)
    {
        UE_LOG(LogFPSDemo,Display,TEXT("APEX_CONTACT case=%d elapsed=%.3f damage=%.2f"),ApexCase,ApexFirstHit,BeforeHealth-Target->GetHealth());
        const double LatencyLimit=Attack.PlaybackKeys.IsEmpty()?.65:.30; // 新时序必须在300ms内实际扣血，旧资产保持原基线，不能仅测启动动画。
        if (!Check(ApexCase<5?(ApexFirstHit>=0. && ApexFirstHit<=LatencyLimit && BeforeHealth-Target->GetHealth()<=Attack.Damage*2.f+.01f):Target->GetHealth()==BeforeHealth,TEXT("coverage / latency / dedup / wall / range / cancel"))) return;
        if (Wall.IsValid()) Wall->Destroy();
        if (++ApexCase<8) { Step=2; NextTime=Now+.1; return; }
        if (!Check(Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo()==BeforeAmmo,TEXT("sweep preserves gun ammunition"))) return;
        Target->SetActorHiddenInGame(true); Target->SetActorEnableCollision(false);
        Camera=GetWorld()->SpawnActor<ACameraActor>(Player->GetActorLocation()+FVector(280,-320,80),FRotator::ZeroRotator);
        Camera->SetActorRotation((Player->GetActorLocation()+FVector(0,0,12)-Camera->GetActorLocation()).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(55.f);
        // 先在靶隐藏后验收无遮挡FP，避免60cm近靶本身占满画面，误判为手臂/模型穿模。
        Advance(.2f); return;
    }
    if (Step==6) { Capture(TEXT("Apex-FP-ClearIdle.png")); Advance(.1f); return; }
    if (Step==7) { Melee->RequestAttack(0); Advance(Attack.PlaybackKeys.IsEmpty()?.32f:.21f); return; } // 同一触击姿态在新映射下更早到达，截图跟随实际窗口。
    if (Step==8) { Capture(TEXT("Apex-FP-ClearContact.png")); Advance(Attack.Duration+.1f); return; }
    if (Step==9) { PC->SetViewTarget(Camera.Get()); Advance(.2f); return; }
    if (Step==10) { Capture(TEXT("Apex-TP-Idle.png")); Advance(.1f); return; }
    if (Step==11) { Melee->RequestAttack(0); Advance(Attack.PlaybackKeys.IsEmpty()?.4f:.29f); return; } // 新时序0.29秒对应原0.40秒侧向随挥。
    if (Step==12) { Capture(TEXT("Apex-TP-Sweep.png")); Advance(Attack.Duration+.1f); return; }
    if (Step==13)
    {
        Melee->RequestAttack(0);
        if (!Check(Melee->IsAttacking(),TEXT("subsequent primary repeats horizontal"))) return;
        Player->GetWeaponComponent()->EquipSlot(2);
        if (!Check(!Melee->IsEquipped() && !Melee->IsAttacking(),TEXT("switch to firearm cancels melee"))) return;
        Advance(Attack.Duration+.2f); return;
    }
    if (Step==14) { Melee->SetEquipped(true); Melee->RequestAttack(0); Serial=Melee->GetAttackSerial(); Advance(Attack.Duration-Definition->InputBufferSeconds*.5f); return; }
    if (Step==15)
    {
        Melee->RequestAttack(0); Melee->RequestAttack(0); // 收招末尾连续两次点击最多缓存一次，不能立即重置攻击时钟。
        if (!Check(Melee->GetAttackSerial()==Serial,TEXT("buffer cannot shorten active attack"))) return;
        Advance(.2f); return;
    }
    if (Step==16)
    {
        if (!Check(Melee->GetAttackSerial()==Serial+1 && Melee->IsAttacking(),TEXT("tail input consumed exactly once at recovery end"))) return;
        Melee->CancelAttack(); Serial=Melee->GetAttackSerial(); Advance(Attack.Duration+.2f); return;
    }
    if (Step==17)
    {
        if (!Check(Melee->GetAttackSerial()==Serial && !Melee->IsAttacking(),TEXT("buffer does not become automatic repeat"))) return;
        Melee->RequestAttack(0); Serial=Melee->GetAttackSerial(); Advance(Attack.Duration-Definition->InputBufferSeconds*.5f); return;
    }
    if (Step==18) { Melee->RequestAttack(0); Melee->CancelAttack(); Advance(Attack.Duration+.2f); return; }
    if (Step==19)
    {
        if (!Check(Melee->GetAttackSerial()==Serial && !Melee->IsAttacking(),TEXT("cancel clears queued tail input"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("APEX_MELEE_TEST_SUCCESS cases=8 buffer=passed")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

void ADemoMeleeTest::TickWideSweep()
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前临时World权威规则，测试仍走真实战斗会话。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 当前本机输入所有者。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 只借用当前Pawn，不跨重生持有。
    UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 使用正式组件，不能以假伤害代理代替GAS。
    if (!Check(GetWorld()->GetTimeSeconds()<45.f && Mode && PC && Player && Melee && Melee->GetDefinition(),TEXT("wide sweep fixture ready within timeout"))) return;
    if (Step==0)
    {
        if (!Check(Mode->StartRun(),TEXT("wide test starts real run"))) return;
        PC->OnRunReady(); Mode->StartNextLevel(); Melee->SetEquipped(true);
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 天然敌人保留注册以防空关结算，但不干扰独立靶。
        Player->SetActorLocation(FVector(0,0,20000)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator::ZeroRotator); Player->SetActorRotation(FRotator::ZeroRotator);
        ADirectionalLight* Light=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-40,130,0)); // 真实RHI截图临时补光，不保存关卡。
        Light->GetLightComponent()->SetMobility(EComponentMobility::Movable); Light->GetLightComponent()->SetIntensity(3.f);
        Advance(.3f); return;
    }
    const int32 Scenario=(Step-1)/4,Part=(Step-1)%4; // 每场景准备/前摇/随挥截图/结果四步，独立靶避免击退影响下一攻击。
    if (Scenario>=8)
    {
        // 范围断言之后追加真实待机/极限视角/手枪截图；不能再用只显示侧面挥击的图片验收共享人物腕桥。
        switch (Step-33)
        {
        case 0:
            Target->SetActorEnableCollision(false); Target->SetActorHiddenInGame(true); PC->SetViewTarget(Player); Advance(.25f); return;
        case 1:
            // 分离后的两套资源必须独立；共用的是权威状态/时钟，不能再用旧别名断言回滚用户要求。
            if (!Check(Melee->GetDefinition()->FirstPersonIdle!=Melee->GetDefinition()->ThirdPersonIdle && Melee->GetDefinition()->Attacks[0].FirstPerson!=Melee->GetDefinition()->Attacks[0].ThirdPerson && Melee->GetDefinition()->Attacks[1].FirstPerson!=Melee->GetDefinition()->Attacks[1].ThirdPerson,TEXT("FP and TP use independent authored sequences"))) return;
            Capture(TEXT("Skin-FP-Bat-Idle.png")); Advance(.2f); return;
        case 2:
            Camera->SetActorLocation(Player->GetActorLocation()+FVector(240,-90,45)); Camera->SetActorRotation((Player->GetActorLocation()+FVector(0,0,30)-Camera->GetActorLocation()).Rotation()); PC->SetViewTarget(Camera.Get()); Advance(.25f); return;
        case 3:
            Capture(TEXT("Skin-TP-Bat-Idle.png")); Advance(.1f); return; // 截图实际在帧末执行，本帧不更换装备或视角。
        case 4:
            Player->GetWeaponComponent()->EquipSlot(2); Advance(.3f); return;
        case 5:
            Capture(TEXT("Skin-TP-Pistol-Idle.png")); Advance(.1f); return;
        case 6:
            Melee->SetEquipped(true); PC->SetViewTarget(Player); PC->SetControlRotation(FRotator(55,0,0)); Advance(.3f); return;
        case 7:
            Capture(TEXT("Skin-FP-Bat-LookUp.png")); Advance(.1f); return;
        case 8:
            PC->SetControlRotation(FRotator(-60,0,0)); Advance(.3f); return;
        case 9:
            Capture(TEXT("Skin-FP-Bat-LookDown.png")); Advance(.15f); return;
        default:
            UE_LOG(LogFPSDemo,Display,TEXT("MELEE_WIDE_TEST_SUCCESS scenarios=8 split_pose=1 visual_states=5")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); return;
        }
    }
    const FVector Positions[]={FVector(95,0,0),FVector(0,-95,0),FVector(0,80,20),FVector(95,0,0),FVector(0,-95,0),FVector(-95,0,0),FVector(0,-95,0),FVector(95,0,0)}; // cm：中心、新左右侧、纵劈中心/侧面、身后、侧面遮挡、TP视觉。
    const bool bExpectedHit=Scenario==0 || Scenario==1 || Scenario==2 || Scenario==3 || Scenario==7; // 后方及墙后拒绝，纵劈不继承横挥新增覆盖。
    const int32 AttackIndex=(Scenario==3 || Scenario==4)?1:0; // 输入仍为左横挥/右纵劈，测试不改DA配置。
    const FDemoMeleeAttack& Attack=Melee->GetDefinition()->Attacks[AttackIndex]; // 只读实际时长/伤害，支持当前配置。
    if (Part==0)
    {
        if (Target.IsValid()) Target->Destroy();
        if (Wall.IsValid()) Wall->Destroy();
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 靶位置明确，不让Spawn校正改变范围断言。
        ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),Player->GetActorLocation()+Positions[Scenario],FRotator(0,180,0),Params); // 每攻击一只全新真实骨骼靶。
        if (!Check(Enemy!=nullptr,TEXT("wide target spawned"))) return;
        FDemoEnemySpawnStats Stats; Stats.Health=1000.f; Stats.AttackPower=0.f; // 高血量不触发清关，禁用其AI tick只隔离主动行为。
        Enemy->Configure(Stats); Enemy->SetActorTickEnabled(false); Target=Enemy;
        DemoHitZoneTest::Pose(Enemy,TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f);
        if (Scenario==6)
        {
            AActor* Blocker=GetWorld()->SpawnActor<AActor>(); UBoxComponent* Box=NewObject<UBoxComponent>(Blocker); // 横向薄墙隔开玩家和新增左侧靶。
            Blocker->SetRootComponent(Box); Box->SetBoxExtent(FVector(160,2,180)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
            Blocker->SetActorLocation(Player->GetActorLocation()+FVector(0,-48,0)); Wall=Blocker;
        }
        if (Scenario==7)
        {
            Camera=GetWorld()->SpawnActor<ACameraActor>(Player->GetActorLocation()+FVector(280,-320,80),FRotator::ZeroRotator); // 外部镜头检查转体及双手连续握持。
            Camera->SetActorRotation((Player->GetActorLocation()+FVector(0,0,12)-Camera->GetActorLocation()).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(55.f); PC->SetViewTarget(Camera.Get());
        }
        BeforeHealth=Enemy->GetHealth(); Melee->RequestAttack(AttackIndex); Serial=Melee->GetAttackSerial();
        if (!Check(Melee->IsAttacking(),TEXT("wide scenario entered real GAS attack"))) return;
        Melee->RequestAttack(AttackIndex); // 同招式重入不能使同一敌人在宽窗口内反复结算。
        if (!Check(Melee->GetAttackSerial()==Serial,TEXT("wide attack rejects reentry"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_WIDE_SCENARIO index=%d attack=%d target=%s expectHit=%d"),Scenario,AttackIndex,*Positions[Scenario].ToString(),bExpectedHit);
        Advance(Attack.Duration*.12f);
    }
    else if (Part==1)
    {
        if (!Check(Target->GetHealth()==BeforeHealth,TEXT("wide attack windup has no damage"))) return;
        Capture(Scenario==7?TEXT("Wide-TP-Windup.png"):TEXT("Wide-FP-Windup.png"));
        Advance(Attack.Duration*.43f); // 约55%时显示新增随挥弧度，避免只在腰部零转角的43%中心帧验收。
    }
    else if (Part==2)
    {
        const FString ImageName=FString::Printf(TEXT("Wide-%s-%d-Contact.png"),Scenario==7?TEXT("TP"):TEXT("FP"),Scenario); // 场景独立图片，最后一项不覆盖左右侧挥动证据。
        Capture(*ImageName); Advance(Attack.Duration*.6f+.1f); // 等待全攻击及恢复，不用截图请求当作伤害完成信号。
    }
    else
    {
        const float Lost=BeforeHealth-Target->GetHealth(); // 单次实际ASC扣血，2倍是现有头部上限，不新增AOE伤害。
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_WIDE_RESULT index=%d lost=%.3f"),Scenario,Lost);
        if (!Check(bExpectedHit?(Lost>0.f && Lost<=Attack.Damage*2.f+.01f):Lost==0.f,TEXT("wide coverage, single hit and exclusion match expectation"))) return;
        Advance(.15f);
    }
}

void ADemoMeleeTest::TickSharedBodyViews()
{
    DEMO_LOG_TICK();
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前临时World正式模式。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本机真实控制器。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 不保留跨World引用。
    UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 正式复制/动画路径。
    if (!Check(Mode && PC && Player && Melee && GetWorld()->GetTimeSeconds()<40.f,TEXT("shared body visual fixture ready"))) return;
    switch (Step)
    {
    case 0:
        if (!Check(Mode->StartRun(),TEXT("shared body starts hub"))) return;
        PC->OnRunReady(); PC->SetControlRotation(FRotator::ZeroRotator); Player->SetActorRotation(FRotator::ZeroRotator); Melee->SetEquipped(true); Advance(.5f); return;
    case 1: Capture(TEXT("Body-FP-Idle.png")); Advance(.1f); return;
    case 2: PC->SetControlRotation(FRotator(-60,0,0)); Advance(.3f); return;
    case 3: Capture(TEXT("Body-FP-LookDown.png")); Advance(.1f); return;
    case 4: PC->SetControlRotation(FRotator(55,0,0)); Advance(.3f); return;
    case 5: Capture(TEXT("Body-FP-LookUp.png")); Advance(.1f); return;
    case 6: PC->SetControlRotation(FRotator(0,45,0)); Advance(.3f); return;
    case 7: Capture(TEXT("Body-FP-HeadRight.png")); Advance(.1f); return;
    case 8: PC->SetControlRotation(FRotator::ZeroRotator); Melee->RequestAttack(0); Advance(Melee->GetDefinition()->Attacks[0].Duration*.30f); return; // 新视频动作在.25..42蓄势，取真实混合完成后的中间相位。
    case 9: Capture(TEXT("Body-FP-Horizontal-Windup.png")); Advance(Melee->GetDefinition()->Attacks[0].Duration*.24f); return;
    case 10: Capture(TEXT("Body-FP-Horizontal-Hit.png")); Advance(.8f); return;
    case 11:
        if (Melee->GetDefinition()->Attacks.IsValidIndex(1)) { Melee->RequestAttack(1); Advance(Melee->GetDefinition()->Attacks[1].Duration*.23f); }
        else { UE_LOG(LogFPSDemo,Log,TEXT("BODY_VIEW_OPTIONAL_SECOND_ATTACK_ABSENT")); Advance(.1f); } // 单横扫配置合法，禁止历史夹具越界访问副攻击。
        return;
    case 12: Capture(TEXT("Body-FP-Vertical-Windup.png")); Advance(.9f); return;
    case 13:
        Camera=GetWorld()->SpawnActor<ACameraActor>(Player->GetActorLocation()+FVector(320,-170,20),FRotator::ZeroRotator); // 后撤包含髋/膝/脚，不用胸部特写掩盖下半身静止。
        Camera->SetActorRotation((Player->GetActorLocation()+FVector(0,0,10)-Camera->GetActorLocation()).Rotation()); Camera->GetCameraComponent()->SetFieldOfView(60.f); PC->SetViewTarget(Camera.Get()); Advance(.3f); return;
    case 14:
        Capture(TEXT("Body-TP-Idle.png"));
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeRecoveryR02"))) // 仅显式新候选专项多看TP输入方向；观察机不反向驱动Pawn，常规历史13态计划不变。
        { PC->SetControlRotation(FRotator(-60,0,0)); Step=140; NextTime=GetWorld()->GetTimeSeconds()+.25f; return; }
        Advance(.1f); return;
    case 140:
        Capture(TEXT("Body-TP-LookDown-R02.png")); PC->SetControlRotation(FRotator(55,0,0)); Step=141; NextTime=GetWorld()->GetTimeSeconds()+.25f; return;
    case 141:
        Capture(TEXT("Body-TP-LookUp-R02.png")); PC->SetControlRotation(FRotator(0,35,0)); Step=142; NextTime=GetWorld()->GetTimeSeconds()+.25f; return;
    case 142:
        Capture(TEXT("Body-TP-LookRight-R02.png")); PC->SetControlRotation(FRotator::ZeroRotator); Step=15; NextTime=GetWorld()->GetTimeSeconds()+.25f; return; // 恢复正常方向后继续真实挥刀/回收/切枪。
    case 15: Melee->RequestAttack(0); Advance(Melee->GetDefinition()->Attacks[0].Duration*.30f); return;
    case 16:
        if (Melee->GetDefinition()->bStationaryFullBody && !Check(Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance())->MeleeFullBodyWeight>.95f,TEXT("configured stationary full-body attack includes pelvis and legs"))) return;
        Capture(TEXT("Body-TP-Horizontal-Windup.png")); Advance(Melee->GetDefinition()->Attacks[0].Duration*.24f); return;
    case 17: Capture(TEXT("Body-TP-Horizontal-Hit.png")); Advance(.8f); return;
    case 18: Capture(TEXT("Body-TP-Recovered.png")); Advance(.1f); return;
    case 19: Player->GetWeaponComponent()->EquipSlot(2); Advance(.3f); return;
    case 20: Capture(TEXT("Body-TP-Pistol.png")); Advance(.1f); return;
    case 21: PC->SetViewTarget(Player); Advance(.3f); return;
    case 22: Capture(TEXT("Body-FP-Pistol.png")); Advance(.1f); return;
    default:
        if (!Check(Player->GetMesh1P()->bHiddenInGame==Player->GetPlayerPresentation()->UsesSharedBodyWeaponDisplay() && Player->GetMesh()->bOwnerNoSee && !Player->GetMesh()->bHiddenInGame,TEXT("gun restored using configured shared-body or independent FP rendering while owner legs stay separate"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("SPLIT_BODY_VIEW_TEST_SUCCESS states=13")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); return;
    }
}
