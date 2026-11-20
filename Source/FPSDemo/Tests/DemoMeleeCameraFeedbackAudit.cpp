// 职责：Editor隔离实机验证近战真实HP损失→本人命中镜头反馈，以及实际挥击后的取消清零。
// 使用：UnrealEditor 项目 -game -DemoMeleeTest -DemoMeleeCameraFeedbackTest；仅Standalone Game World。
// 依赖：生产StartRun/核心/准备屏障/CombatSession/GAS/PhysicsAsset及CameraManager；不改DA，不直接调用任何反馈入口。
#include "Tests/DemoMeleeTest.h"
#if WITH_EDITOR
#include "Tests/DemoProjectileFixtures.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "AI/DemoEnemy.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Weapons/Melee/DemoMeleeCameraModifier.h"
#include "Weapons/Melee/DemoMeleeAbility.h" // 第三刀真实ASC取消入口，专门绕开组件CancelAttack以验证GA EndAbility中心撤销。
#include "GAS/DemoAbilitySystemComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h" // 独立进程ID仅用于证据目录去重，不启动或控制其它进程。
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Debug/DemoLog.h"

void ADemoMeleeTest::FinishMeleeCameraFeedbackAudit(bool bSuccess,const FString& Reason)
{
    DEMO_LOG_CALL();
    if(bCameraAuditFinished) return;
    bCameraAuditFinished=true; SetActorTickEnabled(false);
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 同步证据对象，只存数值和路径，不保有World对象。
    Report->SetStringField(TEXT("status"),bSuccess?TEXT("TECHNICAL_RUNTIME_PASS"):TEXT("FAILED"));
    Report->SetStringField(TEXT("reason"),Reason); Report->SetStringField(TEXT("scope"),TEXT("Standalone actual GAS/HP/owner camera/cancel only; not multiplayer or art acceptance"));
    Report->SetBoolField(TEXT("real_health_loss"),bCameraAuditSawDamage); Report->SetBoolField(TEXT("real_modifier_accepted_hit"),bCameraAuditSawHitFeedback);
    Report->SetBoolField(TEXT("component_cancel_verified"),bCameraAuditComponentCancelVerified); Report->SetBoolField(TEXT("external_asc_cancel_verified"),bCameraAuditASCCancelVerified);
    Report->SetNumberField(TEXT("accepted_hit_count"),CameraAuditHitCount); Report->SetNumberField(TEXT("maximum_final_roll_degrees"),CameraAuditMaximumRoll);
    Report->SetNumberField(TEXT("initial_target_hp"),CameraAuditInitialHealth); Report->SetNumberField(TEXT("post_hit_target_hp"),CameraAuditPostHitHealth);
    Report->SetNumberField(TEXT("frames"),CameraAuditFrames); Report->SetNumberField(TEXT("last_step"),Step);
    Report->SetBoolField(TEXT("asset_saved"),false); Report->SetBoolField(TEXT("feedback_directly_triggered"),false);
    FString Json; // 本次结果UTF8文本；失败保留已采样证据，不覆盖历史目录。
    bool bSaved=!CameraAuditDirectory.IsEmpty() && FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    if(bSaved)
    {
        bSaved=FFileHelper::SaveStringToFile(Json,*(CameraAuditDirectory/TEXT("result.json")));
        bSaved=FFileHelper::SaveStringArrayToFile(CameraAuditRows,*(CameraAuditDirectory/TEXT("frames.csv"))) && bSaved;
    }
    if(ADemoCharacter* Player=CameraAuditPawn.Get())
    {
        if(UDemoMeleeComponent* Melee=Player->FindComponentByClass<UDemoMeleeComponent>()) Melee->CancelAttack(); // 退出夹具不留未结束GA；不影响任何正式资产。
        if(Player->GetWeaponComponent()) Player->GetWeaponComponent()->StopFire();
    }
    if(Target.IsValid()) { Target->SetActorEnableCollision(false); Target->Destroy(); Target.Reset(); }
    const bool bPassed=bSuccess && bSaved; // 写盘失败不能报运行证据完整。
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_CAMERA_FEEDBACK_TEST_%s reason=%s hp_loss=%d hit_feedback=%d hits=%u roll_peak=%.9f frames=%d saved=%d directory=%s"),
        bPassed?TEXT("SUCCESS"):TEXT("FAILED"),*Reason,bCameraAuditSawDamage,bCameraAuditSawHitFeedback,CameraAuditHitCount,CameraAuditMaximumRoll,CameraAuditFrames,bSaved,*CameraAuditDirectory);
    if(FParse::Param(FCommandLine::Get(),TEXT("game")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest"))
        && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeCameraFeedbackTest")) && GetWorld() && GetWorld()->WorldType==EWorldType::Game && GetNetMode()==NM_Standalone)
        FPlatformMisc::RequestExitWithStatus(false,bPassed?0:1); // 禁止终止普通Editor/PIE/联网进程。
}

void ADemoMeleeTest::TickMeleeCameraFeedbackAudit(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if(bCameraAuditFinished) return;
    const double Now=GetWorld()->GetTimeSeconds(),WallNow=FPlatformTime::Seconds(); // 两种预算仅由本专项读取，不修改World时间。
    if(CameraAuditWallStart==0.)
    {
        CameraAuditWallStart=WallNow; CameraAuditGameStart=Now;
        CameraAuditDirectory=FPaths::ProjectSavedDir()/TEXT("CameraReloadFix20261114")/(TEXT("HitFeedback_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+FString::Printf(TEXT("_%u"),FPlatformProcess::GetCurrentProcessId()));
        IFileManager::Get().MakeDirectory(*CameraAuditDirectory,true);
        CameraAuditRows.Add(TEXT("frame,engine_frame,game_seconds,delta_seconds,step,attacking,serial,hp,modifier_hit_count,final_x,final_y,final_z,pitch,yaw,roll,component_x,component_y,component_z,shared_x,shared_y,shared_z,head_x,head_y,head_z,ammo"));
        if(!FParse::Param(FCommandLine::Get(),TEXT("game")) || !FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest"))
            || GetWorld()->WorldType!=EWorldType::Game || GetNetMode()!=NM_Standalone)
        { FinishMeleeCameraFeedbackAudit(false,TEXT("requires explicit Editor -game Standalone -DemoMeleeTest")); return; }
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_CAMERA_FEEDBACK_TEST_BEGIN directory=%s"),*CameraAuditDirectory);
    }
    if(WallNow-CameraAuditWallStart>90. || Now-CameraAuditGameStart>35.)
    { FinishMeleeCameraFeedbackAudit(false,TEXT("bounded 90 wall/35 game seconds exceeded")); return; }
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前真实权威Mode，不手写Phase或绕过准备。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 唯一Standalone本地控制器。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    UDemoMeleeComponent* Melee=Player?Player->FindComponentByClass<UDemoMeleeComponent>():nullptr;
    if(!Mode || !Player || !Melee || !Melee->GetDefinition() || !PC->PlayerCameraManager)
    {
        if(Now-CameraAuditGameStart>8.) FinishMeleeCameraFeedbackAudit(false,TEXT("Avatar/GAS/definition/camera initialization timeout"));
        return;
    }
    CameraAuditPawn=Player;
    const UDemoMeleeDefinition* Definition=Melee->GetDefinition(); // 全流程只读当前正式DA，绝不覆盖轨迹/伤害/反馈参数。
    if(!Definition->Attacks.IsValidIndex(0) || !Definition->bIndependentFirstPersonDisplay || !Definition->CameraFeedback.bEnabled)
    { FinishMeleeCameraFeedbackAudit(false,TEXT("current independent FP melee/feedback configuration unavailable")); return; }
    const FDemoMeleeAttack& Attack=Definition->Attacks[0];
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    UDemoMeleeCameraModifier* Modifier=Cast<UDemoMeleeCameraModifier>(PC->PlayerCameraManager->FindCameraModifierByClass(UDemoMeleeCameraModifier::StaticClass())); // 只读Manager已安装的真实实例，不创建或触发效果。
    if(Step>=3 && Target.IsValid())
    {
        const FVector Final=PC->PlayerCameraManager->GetCameraLocation(),Component=Player->GetFirstPersonCameraComponent()->GetComponentLocation(),Shared=Player->GetSharedViewLocation(),Head=Player->GetMesh()->GetSocketLocation(TEXT("head"));
        const FRotator Rotation=PC->PlayerCameraManager->GetCameraRotation(); // CameraManager最终POV是观察事实，Component/Shared仅为基础眼位。
        const uint32 Hits=Modifier?Modifier->GetHitFeedbackCountForAudit():0;
        const int32 Ammo=Player->GetWeaponComponent()->GetActiveWeapon()?Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo():INDEX_NONE;
        CameraAuditMaximumRoll=FMath::Max(CameraAuditMaximumRoll,static_cast<float>(FMath::Abs(Rotation.Roll)));
        CameraAuditRows.Add(FString::Printf(TEXT("%d,%llu,%.9f,%.9f,%d,%d,%u,%.6f,%u,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%d"),CameraAuditFrames,GFrameCounter,Now,DeltaSeconds,Step,Melee->IsAttacking(),Melee->GetAttackSerial(),Target->GetHealth(),Hits,
            Final.X,Final.Y,Final.Z,Rotation.Pitch,Rotation.Yaw,Rotation.Roll,Component.X,Component.Y,Component.Z,Shared.X,Shared.Y,Shared.Z,Head.X,Head.Y,Head.Z,Ammo));
        ++CameraAuditFrames;
        if(Target->GetHealth()<CameraAuditInitialHealth-.01f) bCameraAuditSawDamage=true;
        if(Hits>0) { bCameraAuditSawHitFeedback=true; CameraAuditHitCount=FMath::Max(CameraAuditHitCount,Hits); }
        if(!Component.Equals(Shared,.001) || !Final.Equals(Shared,.001) || !FMath::IsFinite(Rotation.Roll) || FMath::Abs(Rotation.Roll)>.6501 || Ammo!=CameraAuditAmmo)
        { FinishMeleeCameraFeedbackAudit(false,TEXT("stable eye / finite final roll budget / hidden gun ammunition violated")); return; }
    }
    if(Now<NextTime) return;
    if(Step==0)
    {
        if(!Mode->StartRun()) { FinishMeleeCameraFeedbackAudit(false,TEXT("production StartRun rejected")); return; }
        PC->OnRunReady(); Step=1; NextTime=Now+.15; return;
    }
    if(Step==1)
    {
        if(!State) { FinishMeleeCameraFeedbackAudit(false,TEXT("missing authoritative game state")); return; }
        if(State->Phase!=EDemoPhase::Combat)
        {
            UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Player); // 真实本人临时核心，不伪造存档或准备ACK。
            if(!Build || (!Build->HasSelectedCore() && !Build->TrySelectCore(TEXT("B01"))))
            { FinishMeleeCameraFeedbackAudit(false,TEXT("production B01 core selection rejected")); return; }
            Mode->StartNextLevel(); NextTime=Now+.15; return; // 流送/准备未完成继续等待，总时限仍有效。
        }
        for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 不杀怪、不清除真实CombatSession，天然AI留原Spawner登记。
        Player->SetActorLocation(FVector(0,0,20000),false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Player->SetActorRotation(FRotator::ZeroRotator); PC->SetControlRotation(FRotator::ZeroRotator); PC->SetViewTarget(Player);
        if(!Melee->SetEquipped(true)) { FinishMeleeCameraFeedbackAudit(false,TEXT("production melee equip rejected")); return; }
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // World拥有孤立真实敌人，未生成替代碰撞体。
        ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),Player->GetActorLocation()+FVector(85,0,0),FRotator(0,180,0),Params);
        if(!Enemy) { FinishMeleeCameraFeedbackAudit(false,TEXT("real enemy spawn failed")); return; }
        FDemoEnemySpawnStats Stats; Stats.Health=10000.f; Stats.AttackPower=0.f; // 仅本实例的高血/无反击夹具；生产攻击数值和命中轨迹不变。
        Enemy->Configure(Stats); Enemy->SetActorTickEnabled(false); Target=Enemy;
        if(!DemoHitZoneTest::Pose(Enemy,TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f))
        { FinishMeleeCameraFeedbackAudit(false,TEXT("real PhysicsAsset rig-check pose unavailable")); return; }
        CameraAuditInitialHealth=Enemy->GetHealth();
        if(!Player->GetWeaponComponent()->GetActiveWeapon()) { FinishMeleeCameraFeedbackAudit(false,TEXT("underlying firearm missing")); return; }
        CameraAuditAmmo=Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo();
        AddTickPrerequisiteComponent(Melee); AddTickPrerequisiteComponent(Player->GetMesh());
        Step=2; NextTime=Now+.5; return; // 真实装备/AnimBP/CameraManager热身，不把同帧旧POV记为反馈失败。
    }
    if(!State || State->Phase!=EDemoPhase::Combat || !Target.IsValid())
    { FinishMeleeCameraFeedbackAudit(false,TEXT("real Combat session/target lost")); return; }
    if(Step==2)
    {
        if(!Modifier) { FinishMeleeCameraFeedbackAudit(false,TEXT("production owner camera modifier not installed")); return; }
        Melee->RequestAttack(0); CameraAuditAttackStarted=Now;
        if(!Melee->IsAttacking()) { FinishMeleeCameraFeedbackAudit(false,TEXT("real melee GAS attack did not start")); return; }
        Step=3; NextTime=Now+Attack.Duration+.35; return;
    }
    if(Step==3)
    {
        CameraAuditPostHitHealth=Target->GetHealth();
        if(!bCameraAuditSawDamage || !bCameraAuditSawHitFeedback || CameraAuditHitCount!=1 || Melee->IsAttacking())
        { FinishMeleeCameraFeedbackAudit(false,TEXT("first real blade must lose target HP, accept exactly one hit feedback and finish")); return; }
        Target->SetActorLocation(Player->GetActorLocation()+FVector(2000,0,0),false,nullptr,ETeleportType::TeleportPhysics); // 第二刀远靶仍真实存活，不能再次命中。
        Melee->RequestAttack(0); CameraAuditAttackStarted=Now;
        if(!Melee->IsAttacking()) { FinishMeleeCameraFeedbackAudit(false,TEXT("second real blade did not start after original cooldown")); return; }
        CameraAuditCancelAt=Now+FMath::Min(Attack.Duration*.9f,Attack.RuntimeAtPhase(Attack.SwingSoundPhase)+Definition->CameraFeedback.SwingSeconds*.5f);
        Step=4; NextTime=CameraAuditCancelAt; return;
    }
    if(Step==4)
    {
        if(!Melee->IsAttacking() || Target->GetHealth()!=CameraAuditPostHitHealth || !Modifier || Modifier->GetHitFeedbackCountForAudit()!=CameraAuditHitCount || FMath::Abs(PC->PlayerCameraManager->GetCameraRotation().Roll)<.00001)
        { FinishMeleeCameraFeedbackAudit(false,TEXT("second blade must actually reach swing feedback before cancel, with no far-target HP/hit")); return; }
        Melee->CancelAttack(); CameraAuditCancelFrame=GFrameCounter; Step=5; NextTime=Now; return;
    }
    if(Step==5)
    {
        if(GFrameCounter<=CameraAuditCancelFrame+1) return; // 取消发生PostUpdate，至少等待下一完整CameraManager求值。
        if(Melee->IsAttacking() || Target->GetHealth()!=CameraAuditPostHitHealth || FMath::Abs(PC->PlayerCameraManager->GetCameraRotation().Roll)>.00001)
        { FinishMeleeCameraFeedbackAudit(false,TEXT("cancel must clear actual next-frame roll and cannot damage far target")); return; }
        Step=6; NextTime=Now+Attack.Duration+.25; return; // 继续跨旧攻击截止，排查迟到反馈/伤害复活。
    }
    if(Step==6)
    {
        if(Melee->IsAttacking() || Target->GetHealth()!=CameraAuditPostHitHealth || FMath::Abs(PC->PlayerCameraManager->GetCameraRotation().Roll)>.00001 || (Modifier && Modifier->GetHitFeedbackCountForAudit()!=0))
        { FinishMeleeCameraFeedbackAudit(false,TEXT("late camera hit/HP revived after explicit cancel")); return; }
        bCameraAuditComponentCancelVerified=true;
        Melee->RequestAttack(0); CameraAuditAttackStarted=Now; // 第二刀旧冷却已完整等待；第三刀仍由原GA启动，不直接BeginAttack。
        if(!Melee->IsAttacking() || !Player->GetDemoASC()) { FinishMeleeCameraFeedbackAudit(false,TEXT("third real blade/ASC did not start after cooldown")); return; }
        CameraAuditCancelAt=Now+FMath::Min(Attack.Duration*.9f,Attack.RuntimeAtPhase(Attack.SwingSoundPhase)+Definition->CameraFeedback.SwingSeconds*.5f);
        Step=7; NextTime=CameraAuditCancelAt; return;
    }
    if(Step==7)
    {
        if(!Melee->IsAttacking() || Target->GetHealth()!=CameraAuditPostHitHealth || !Modifier || Modifier->GetHitFeedbackCountForAudit()!=0 || FMath::Abs(PC->PlayerCameraManager->GetCameraRotation().Roll)<.00001)
        { FinishMeleeCameraFeedbackAudit(false,TEXT("third blade must reach real swing feedback without far-target HP/hit")); return; }
        Player->GetDemoASC()->CancelDemoAbility(UDemoMeleeAbility::StaticClass()); // 真实外部GAS取消，绝不调用组件CancelAttack或表现清理函数。
        CameraAuditCancelFrame=GFrameCounter; Step=8; NextTime=Now; return;
    }
    if(Step==8)
    {
        if(GFrameCounter<=CameraAuditCancelFrame+1) return;
        if(Melee->IsAttacking() || Target->GetHealth()!=CameraAuditPostHitHealth || FMath::Abs(PC->PlayerCameraManager->GetCameraRotation().Roll)>.00001)
        { FinishMeleeCameraFeedbackAudit(false,TEXT("external ASC cancel must clear next-frame roll without damage")); return; }
        Step=9; NextTime=Now+Attack.Duration+.25; return; // 跨第三刀原截止，确认GA取消不会重新建立旧镜头包络。
    }
    if(Step==9)
    {
        if(Melee->IsAttacking() || Target->GetHealth()!=CameraAuditPostHitHealth || FMath::Abs(PC->PlayerCameraManager->GetCameraRotation().Roll)>.00001 || (Modifier && Modifier->GetHitFeedbackCountForAudit()!=0))
        { FinishMeleeCameraFeedbackAudit(false,TEXT("late camera hit/HP revived after external ASC cancel")); return; }
        bCameraAuditASCCancelVerified=true;
        FinishMeleeCameraFeedbackAudit(true,TEXT("real HP-confirmed owner feedback, component/ASC swing cancels, roll budget and hidden gun ammo preserved"));
    }
}
#endif
