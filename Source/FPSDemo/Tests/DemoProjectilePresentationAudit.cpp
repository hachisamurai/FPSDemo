// 职责：Editor独立-game检验实体弹FP接续/HP，或显式-DemoMovingMuzzleAudit核四枪出膛前后三帧的口心、枪管与准星投影，可附加-DemoFixedMuzzleAudit / -DemoMuzzleWeapon。
// 使用：-DemoProjectileTest -DemoProjectilePresentationAudit，RHI Standalone；正式StartRun/核心/准备进入Combat，不直接写Phase/HP，不调用伤害/命中接口。
#include "Tests/DemoProjectileTest.h"
#if WITH_EDITOR
#include "Tests/DemoProjectileFixtures.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Save/DemoRunSave.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Interaction/HubShops/DemoShopFocusComponent.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "AI/DemoEnemy.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/Projectiles/DemoProjectileTracer.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Interaction/DemoInteractable.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/BlueprintGeneratedClass.h" // 临时CDO差异必须同步原生PostConstruction复制表；不是改引擎或绕过BP构造。
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Debug/DemoLog.h"

void ADemoProjectileTest::RefreshPresentationAuditDefaults()
{
    DEMO_LOG_CALL();
    if (ProjectileDefaults)
        if (UBlueprintGeneratedClass* Class=Cast<UBlueprintGeneratedClass>(ProjectileDefaults->GetClass()))
            Class->UpdateCustomPropertyListForPostConstruction(); // GameThread同步：UE5.4只复制相对native默认有差异的字段，动态改CDO后必须重建，退出还原也重建。
}
void ADemoProjectileTest::RestorePresentationAudit()
{
    DEMO_LOG_CALL();
    if (bPresentationRestored) return;
    bPresentationRestored=true;
    FWorldDelegates::OnWorldPreActorTick.RemoveAll(this); // 仅注销本夹具的移动开火取样。
    if (!bPresentationScopeReady) return; // 误启动的PIE/普通Editor只留失败文本，不取消其弹体或更改相机。
    if (ADemoCharacter* Player=PresentationPawn.Get())
    {
        Player->GetWeaponComponent()->StopFire(); Player->GetWeaponComponent()->StopAim();
        if (APlayerController* PC=Cast<APlayerController>(Player->GetController())) PC->SetViewTarget(Player);
        if (bPresentationProfileFlagSaved)
            if (UDemoPlayerProfile* Profile=Player->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Profile->SetDebugTestFlag(0,bPresentationOriginalInfiniteGold);
    }
    if (bPresentationSavedConfig)
    {
        if (ProjectileDefaults) { ProjectileDefaults->Config=PresentationOriginalProjectile; RefreshPresentationAuditDefaults(); }
        if (Pistol.IsValid()) Pistol->Config=PresentationOriginalWeapon;
    }
    if (GetWorld()) ADemoProjectileBase::CancelAll(GetWorld()); // 仅独立显式测试Actor的退出流程，不影响任何其他进程。
    if (Wall.IsValid()) { Wall->Destroy(); Wall.Reset(); }
    if (PresentationCamera.IsValid()) { PresentationCamera->Destroy(); PresentationCamera.Reset(); }
    if (Target.IsValid()) { Target->SetActorEnableCollision(false); Target->Destroy(); Target.Reset(); }
}
void ADemoProjectileTest::FinishPresentationAudit(bool bSuccess,const FString& Reason)
{
    DEMO_LOG_CALL();
    if (bPresentationFinished) return;
    bPresentationFinished=true; SetActorTickEnabled(false);
    int32 Captured=0; // 统计实际存在的PNG，不能拿截图请求数冒充渲染成功。
    for (const FString& Path:PresentationCaptures) if (IFileManager::Get().FileExists(*Path)) ++Captured;
    RestorePresentationAudit();
    FString Restored; // 原CDO由UStruct文本按值导出，不比较带内存地址的ToString。
    if (ProjectileDefaults) FDemoProjectileConfig::StaticStruct()->ExportText(Restored,&ProjectileDefaults->Config,nullptr,ProjectileDefaults,PPF_None,nullptr);
    const bool bRestored=!bPresentationSavedConfig || Restored==PresentationOriginalProjectileText;
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("status"),bSuccess&&bRestored&&Captured>0?TEXT("TECHNICAL_RUNTIME_PASS"):TEXT("FAILED"));
    Report->SetStringField(TEXT("reason"),Reason);
    Report->SetStringField(TEXT("scope"),TEXT("Editor Standalone seven original Fire GA Mesh/HP/scope/switch/wall/view cases plus separate reset-checkpoint disk readback and production session Shutdown; original full ReturnHub/World travel not verified; LAN and art acceptance not tested"));
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMovingMuzzleAudit")))
        Report->SetStringField(TEXT("scope"),TEXT("local Editor Standalone selected firearm; raw socket vs first tracer rear, forward axis and bore/reticle intersection at traced target depth, before firing and for three shot frames; no LAN/art acceptance"));
    Report->SetNumberField(TEXT("cases_completed"),PresentationCase); Report->SetNumberField(TEXT("real_damage_cases"),PresentationDamageCases);
    Report->SetNumberField(TEXT("original_cases_completed"),FMath::Min(PresentationCase,7)); // 新会话撤销案不能冒充已失败的原ReturnHub/保存/旅行案。
    Report->SetBoolField(TEXT("production_session_shutdown_verified"),bSuccess&&PresentationCase>=8);
    Report->SetBoolField(TEXT("reset_checkpoint_real_disk_readback_verified"),bPresentationResetCheckpointVerified);
    Report->SetBoolField(TEXT("reset_input_nonzero_gold"),bPresentationResetHadCoins);
    Report->SetBoolField(TEXT("reset_input_nonzero_silver"),bPresentationResetHadSilver);
    Report->SetBoolField(TEXT("reset_input_nonzero_permanent_growth"),bPresentationResetHadPermanentGrowth);
    Report->SetBoolField(TEXT("reset_input_nonzero_temporary_growth"),bPresentationResetHadTemporaryGrowth);
    Report->SetBoolField(TEXT("original_returnhub_save_travel_verified"),false);
    Report->SetStringField(TEXT("original_returnhub_evidence"),TEXT("Historical ProjectileFP_20261114_204858_30684/result.json: ReturnToSafeHub rejected by reset checkpoint validation; this run only retests reset checkpoint before Shutdown, not the full ReturnHub/World travel"));
    Report->SetBoolField(TEXT("actual_live_mesh_observed"),bPresentationSawLiveMesh); Report->SetBoolField(TEXT("actual_handoff_observed"),bPresentationSawHandoff);
    Report->SetBoolField(TEXT("cdo_config_restored"),bRestored); Report->SetBoolField(TEXT("phase_or_health_written"),false);
    Report->SetNumberField(TEXT("frames"),PresentationFrame); Report->SetNumberField(TEXT("rhi_png_files"),Captured);
    Report->SetBoolField(TEXT("asset_saved"),false); Report->SetBoolField(TEXT("multiplayer_tested"),false);
    FString Json;
    bool bSaved=!PresentationDirectory.IsEmpty() && FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    if (bSaved)
    {
        bSaved=FFileHelper::SaveStringToFile(Json,*(PresentationDirectory/TEXT("result.json")));
        bSaved=FFileHelper::SaveStringArrayToFile(PresentationRows,*(PresentationDirectory/TEXT("frames.csv"))) && bSaved;
        bSaved=FFileHelper::SaveStringArrayToFile(PresentationEvents,*(PresentationDirectory/TEXT("events.csv"))) && bSaved;
        bSaved=FFileHelper::SaveStringArrayToFile(PresentationCaptures,*(PresentationDirectory/TEXT("captures.txt"))) && bSaved;
    }
    const bool bPassed=bSuccess&&bRestored&&bSaved&&Captured>0;
    UE_LOG(LogFPSDemo,Display,TEXT("PROJECTILE_PRESENTATION_AUDIT_%s reason=%s cases=%d real_hp_cases=%d frames=%d png=%d restored=%d directory=%s"),
        bPassed?TEXT("SUCCESS"):TEXT("FAILED"),*Reason,PresentationCase,PresentationDamageCases,PresentationFrame,Captured,bRestored,*PresentationDirectory);
    if (FParse::Param(FCommandLine::Get(),TEXT("game")) && FParse::Param(FCommandLine::Get(),TEXT("DemoProjectileTest"))
        && FParse::Param(FCommandLine::Get(),TEXT("DemoProjectilePresentationAudit")) && GetWorld()
        && GetWorld()->WorldType==EWorldType::Game && GetNetMode()==NM_Standalone)
        FPlatformMisc::RequestExitWithStatus(false,bPassed?0:1); // 不能终止用户Editor/PIE/联机进程。
}
bool ADemoProjectileTest::BeginPresentationAuditShot()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player=PresentationPawn.Get();
    ADemoPlayerController* PC=Player?Cast<ADemoPlayerController>(Player->GetController()):nullptr;
    if (!Player || !PC || !Pistol.IsValid() || !Target.IsValid()) { FinishPresentationAudit(false,TEXT("case dependencies lost")); return false; }
    if (Player->GetWeaponComponent()->GetActiveWeapon()!=Pistol.Get())
    {
        if (!Player->GetWeaponComponent()->EquipSlot(2)) { FinishPresentationAudit(false,TEXT("production pistol equip rejected")); return false; }
        Step=3; NextTime=GetWorld()->GetTimeSeconds()+.1f; return true; // 等Body/TP枪/FP层真实更新，不在切枪同帧借错误TP源。
    }
    Player->GetWeaponComponent()->StopAim(); PC->SetViewTarget(Player);
    DemoHitZoneTest::Aim(Target.Get(),Player,BodyPoint,700.f); // 真实物理骨靶表面，不人工构造Hit。
    if (Wall.IsValid()) { Wall->Destroy(); Wall.Reset(); }
    ProjectileDefaults->Config.bEnableTracer=PresentationCase==1;
    RefreshPresentationAuditDefaults(); // 每案Tracer开关也属原native默认字段，不能只改CDO而使用过期复制表。
    Pistol->Config.bSupportsScope=PresentationCase==2;
    Pistol->Config.bUnscopeAfterShot=PresentationCase==2;
    Pistol->Config.ScopeFOV=45.f; Pistol->Config.DeepScopeFOV=20.f; Pistol->Config.ScopedSpreadHalfAngle=0.f;
    if (PresentationCase==2)
    {
        Player->GetWeaponComponent()->CycleAim();
        if (Player->GetWeaponComponent()->GetScopeLevel()==0) { FinishPresentationAudit(false,TEXT("real Scope GA rejected")); return false; }
    }
    if (PresentationCase==5)
    {
        const FVector Eye=Player->GetSharedViewLocation(),Aim=PC->GetControlRotation().Vector(); // 以实际压缩后的FP枪口构造遮挡，旧TP半距墙可能仍在FP前方而不需要降级。
        const float PhysicalDepth=static_cast<float>(FVector::DotProduct(Pistol->GetMuzzleLocation()-Eye,Aim));
        const float VisualDepth=static_cast<float>(FVector::DotProduct(Pistol->GetVisualMuzzleLocation()-Eye,Aim));
        const float WallDepth=FMath::Min(PhysicalDepth,VisualDepth)*.5f; // 必须真隔在Eye与两枪口之间，而非把安全的FP前方桥硬判失败。
        if (WallDepth<=2.5f) { FinishPresentationAudit(false,TEXT("near-wall fixture has no positive clear camera-to-muzzle depth")); return false; }
        Wall=SpawnWall(Player,WallDepth);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoProjectileAuditNearWall),false,Player); Query.AddIgnoredActor(Pistol.Get());
        FHitResult Witness; // 独立真实视线见证：只有创建的墙确实挡住FP路径，才要求world降级。
        if (!Wall.IsValid() || !GetWorld()->LineTraceSingleByChannel(Witness,Eye,Pistol->GetVisualMuzzleLocation(),ECC_GameTraceChannel3,Query) || Witness.GetActor()!=Wall.Get())
        { FinishPresentationAudit(false,TEXT("near-wall fixture did not actually obstruct compressed FP muzzle")); return false; }
        PresentationEvents.Add(FString::Printf(TEXT("%d,%.9f,near_wall_eye_to_fp_obstructed depth=%.6f fp_depth=%.6f tp_depth=%.6f"),PresentationCase,GetWorld()->GetTimeSeconds(),WallDepth,VisualDepth,PhysicalDepth));
    }
    if (PresentationCase==6)
    {
        PresentationCamera=GetWorld()->SpawnActor<ACameraActor>(Player->GetActorLocation()+FVector(0,300,200),FRotator::ZeroRotator);
        if (!PresentationCamera.IsValid()) { FinishPresentationAudit(false,TEXT("external camera spawn failed")); return false; }
        PC->SetViewTarget(PresentationCamera.Get());
    }
    PresentationHPBefore=Target->GetHealth(); PresentationHPAfter=PresentationHPBefore;
    PresentationVisualOrigin=Pistol->GetVisualMuzzleLocation(); // Fire之前的真实FP机械枪，不改变Muzzle Socket。
    PresentationShotFrames=0; bPresentationCaseHandoff=false;
    const int32 AmmoBefore=Pistol->GetAmmo();
    UE_LOG(LogFPSDemo,Log,TEXT("PROJECTILE_PRESENTATION_PREFIRE case=%d speed=%.6f life=%.6f range=%.6f class=%s"),
        PresentationCase,ProjectileDefaults->Config.InitialSpeed,ProjectileDefaults->Config.MaxLifeSeconds,Pistol->Config.Range,*GetNameSafe(Pistol->Config.ProjectileClass)); // 核实际GA输入，不用fixture预设值解释构造后失败。
    DemoHitZoneTest::Fire(Player); // 真正Fire GA，禁止调用Prepare/ResolveImpact/伤害接口伪造结果。
    PresentationShotStart=GetWorld()->GetTimeSeconds();
    PresentationBullet.Reset();
    for (TActorIterator<ADemoProjectileBase> It(GetWorld());It;++It)
        if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetShotContext() && It->GetShotContext()->GetSourcePawn()==Player) PresentationBullet=*It;
    if (!PresentationBullet.IsValid())
    {
        if (PresentationCase==5 && Pistol->GetAmmo()==AmmoBefore && Target->GetHealth()==PresentationHPBefore)
        {
            PresentationEvents.Add(FString::Printf(TEXT("%d,%.9f,close_wall_safely_rejected"),PresentationCase,PresentationShotStart));
            Step=5; NextTime=PresentationShotStart+.2f; return true; // 正式准备拒绝是合法降级，不能把它计为真实命中。
        }
        FinishPresentationAudit(false,TEXT("actual Fire GA did not commit exactly one projectile")); return false;
    }
    ADemoProjectileBase* Bullet=PresentationBullet.Get();
    if (!FDemoProjectileConfig::StaticStruct()->CompareScriptStruct(&Bullet->Config,&ProjectileDefaults->Config,PPF_None))
    { FinishPresentationAudit(false,TEXT("actual spawned BP projectile configuration differs from isolated CDO fixture")); return false; } // 原BP仍走完整构造和Prepare，配置不一致不能冒充TracerOff/慢弹。
    const FDemoProjectilePresentationLaunch& Launch=Bullet->GetPresentationLaunchForAudit();
    PresentationShot=Launch.ShotId; PresentationPhysicalOrigin=Bullet->GetActorLocation();
    const FVector RenderRoot=Bullet->GetCosmeticLocationForAudit(); // 真正Mesh的父层，不是计算器目标值。
    const UStaticMeshComponent* Mesh=Bullet->FindComponentByClass<UStaticMeshComponent>();
    if (DemoProjectileFixtures::CountLive(GetWorld())!=1 || Pistol->GetAmmo()!=AmmoBefore-Pistol->Config.AmmoPerShot
        || !Launch.ShotId.IsValid() || Launch.Pellet!=0 || !PresentationPhysicalOrigin.Equals(FVector(Launch.Origin),.02)
        || Target->GetHealth()!=PresentationHPBefore || !Mesh || !Mesh->IsVisible() || Mesh->bHiddenInGame)
    { FinishPresentationAudit(false,TEXT("committed single root/identity/ammo/live Mesh/HP-before-flight invariant failed")); return false; }
    bPresentationSawLiveMesh=true;
    PresentationEvents.Add(FString::Printf(TEXT("%d,%.9f,initial shot=%s P0=[%s] F0=[%s] Render=[%s] Mesh=[%s] hp=%.6f"),
        PresentationCase,PresentationShotStart,*Launch.ShotId.ToString(),*PresentationPhysicalOrigin.ToString(),*PresentationVisualOrigin.ToString(),*RenderRoot.ToString(),*Mesh->GetComponentLocation().ToString(),PresentationHPBefore));
    const bool bExpectedBridge=PresentationCase!=2 && PresentationCase!=5 && PresentationCase!=6;
    const FVector Expected=bExpectedBridge?PresentationVisualOrigin:PresentationPhysicalOrigin;
    if (!RenderRoot.Equals(Expected,.03)) { FinishPresentationAudit(false,TEXT("actual Mesh render parent is not correct FP or safe world initial origin")); return false; }
    const float ExpectedScale=bExpectedBridge && Player->GetPlayerPresentation()?Player->GetPlayerPresentation()->GetFirstPersonRenderScale():1.f; // 同次发射策略，Scope/外部相机始终world1。
    PresentationPreviousScale=Bullet->GetCosmeticScaleForAudit();
    if (!FMath::IsNearlyEqual(PresentationPreviousScale,ExpectedScale,1.e-5f) || !Bullet->GetActorScale3D().Equals(FVector::OneVector,1.e-6))
    { FinishPresentationAudit(false,TEXT("actual cosmetic initial scale / invariant physical root scale failed")); return false; }
    if (PresentationCase==0 && FVector::Distance(PresentationVisualOrigin,PresentationPhysicalOrigin)<2.)
    { FinishPresentationAudit(false,TEXT("fixture has no meaningful FP/TP parallax to verify")); return false; }
    PresentationPreviousCorrection=static_cast<float>(FVector::Distance(RenderRoot,PresentationPhysicalOrigin));
    if (PresentationCase==2 && (!Launch.bScoped || Player->GetWeaponComponent()->GetScopeLevel()!=0))
    { FinishPresentationAudit(false,TEXT("Scope-at-fire snapshot / real automatic unscope failed")); return false; }
    if (PresentationCase==3 && !Player->GetWeaponComponent()->EquipSlot(1))
    { FinishPresentationAudit(false,TEXT("production in-flight weapon switch rejected")); return false; }
    if (PresentationCase==4) Wall=SpawnWall(Player,300.f); // 开火瞄准查询之后才插薄墙，验证实际飞行碰撞阻断。
    if (PresentationCase==7)
    {
        AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
        UDemoRunFlowComponent* Flow=Mode?Mode->GetRunFlow():nullptr; // 独立验证ResetCheckpoint磁盘结果与旅行前生产撤销入口；不声称执行完整ReturnHub/OpenLevel。
        if (!Flow || !Flow->GetCombatSession() || !Flow->GetCombatSession()->IsActive())
        { FinishPresentationAudit(false,TEXT("production session missing before Shutdown cancellation")); return false; }
        UDemoRunSaves* Saves=Player->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 只读取本夹具真实创建的GI临时槽，不能选择/覆盖用户正式槽。
        const ADemoPlayerState* PlayerState=Player->GetPlayerState<ADemoPlayerState>();
        const UDemoRunSave* Previous=Saves?Saves->GetSlot(Saves->GetActiveSlot()):nullptr;
        if (!Previous || !Previous->Validate() || !PlayerState || !PlayerState->GetProgressComponent())
        { FinishPresentationAudit(false,TEXT("actual reset checkpoint original slot/progress invalid")); return false; }
        const FString SlotKey=Previous->SlotStorageKey,OwnerKey=Previous->OwnerPlayerKey; // 保存前同槽稳定键；RunId允许本轮重置换代。
        const FDemoPlayerProgressSnapshot Personal=PlayerState->GetProgressComponent()->GetSnapshot(); // 同步本栈值快照，不写钱包/成长或借旧缓存造权限。
        bPresentationResetHadCoins=Personal.Coins!=0; bPresentationResetHadSilver=Personal.SilverCoins!=0;
        bPresentationResetHadPermanentGrowth=Personal.UpgradeProgress.Total(true)!=0 || Personal.UpgradeProgress.PermanentDamage!=0.f || Personal.UpgradeProgress.PermanentHealth!=0.f || Personal.UpgradeProgress.PermanentMagazine!=0.f;
        bPresentationResetHadTemporaryGrowth=Personal.UpgradeProgress.Total(false)!=0;
        if (!Flow->SaveResetCheckpoint()) { FinishPresentationAudit(false,TEXT("production reset checkpoint rejected before session Shutdown")); return false; }
        const UDemoRunSave* Readback=Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(Saves->SlotName(Saves->GetActiveSlot()),0)); // 真实磁盘反序列化，不能用内存缓存或成功UI代替耐久结果。
        FDemoUpgradeProgress ExpectedGrowth=Personal.UpgradeProgress; ExpectedGrowth.ResetTemporary(); // 以原来源账本比较永久值/逐项金币价格，银币价格应归0。
        if (!Readback || !Readback->Validate() || Readback->SlotStorageKey!=SlotKey || Readback->OwnerPlayerKey!=OwnerKey
            || Readback->Phase!=EDemoPhase::Hub || Readback->CompletedLevel!=0 || Readback->Coins!=Personal.Coins || Readback->SilverCoins!=0 || Readback->SilverPurchases!=0
            || !FDemoUpgradeProgress::StaticStruct()->CompareScriptStruct(&Readback->UpgradeProgress,&ExpectedGrowth,PPF_None)
            || !FDemoWeaponOwnership::StaticStruct()->CompareScriptStruct(&Readback->Ownership,&Personal.Ownership,PPF_None)
            || Readback->PrimaryId!=TEXT("rifle") || Readback->ActiveSlot!=2 || Readback->Weapons.Num()!=2
            || !Flow->GetCombatSession()->IsActive() || DemoProjectileFixtures::CountLive(GetWorld())!=1 || Target->GetHealth()!=PresentationHPBefore)
        { FinishPresentationAudit(false,TEXT("actual reset checkpoint disk identity/permanent ownership/growth or unconsumed live shot invariant failed")); return false; }
        bPresentationResetCheckpointVerified=true;
        PresentationEvents.Add(FString::Printf(TEXT("%d,%.9f,reset_checkpoint_disk_readback_valid same_slot=1 same_owner=1 gold=%d silver_before=%d permanent_nonzero=%d temporary_nonzero=%d weapons=%d"),
            PresentationCase,GetWorld()->GetTimeSeconds(),Personal.Coins,Personal.SilverCoins,bPresentationResetHadPermanentGrowth,bPresentationResetHadTemporaryGrowth,Readback->Weapons.Num()));
        Flow->Shutdown();
        PresentationEvents.Add(FString::Printf(TEXT("%d,%.9f,production_session_shutdown_after_reset_checkpoint_no_returnhub_or_world_travel"),PresentationCase,GetWorld()->GetTimeSeconds()));
    }
    PresentationEvents.Add(FString::Printf(TEXT("%d,%.9f,real_fire_%s"),PresentationCase,PresentationShotStart,*PresentationShot.ToString()));
    Step=4; NextTime=PresentationShotStart+.05f;
    return true;
}

// 单枪专项的正式武器ID，默认步枪；仅显式Editor测试读取，选择真实陈列/装备与默认弹速。
static FName MuzzleAuditGunId()
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] MuzzleAuditGunId"));
    FString Id;
    return FParse::Value(FCommandLine::Get(),TEXT("DemoMuzzleWeapon="),Id) ? FName(*Id) : FName(TEXT("rifle"));
}

void ADemoProjectileTest::SampleMovingMuzzle(UWorld* World,ELevelTick TickType,float DeltaSeconds)
{
    DEMO_LOG_CALL();
    if (World!=GetWorld() || bPresentationFinished) return;
    ADemoCharacter* Player=PresentationPawn.Get();
    ADemoWeaponBase* Weapon=Player?Player->GetWeaponComponent()->GetActiveWeapon():nullptr;
    APlayerController* PC=Player?Cast<APlayerController>(Player->GetController()):nullptr;
    UPrimitiveComponent* Gun=Weapon?Weapon->GetMuzzleFlashComponent(false):nullptr;
    if (!Gun || !PC || Weapon->GetWeaponId()!=MuzzleAuditGunId()) { FinishPresentationAudit(false,TEXT("expected equipped gun or muzzle unavailable")); return; }
    const bool bFixed=FParse::Param(FCommandLine::Get(),TEXT("DemoFixedMuzzleAudit")); // 附加到枪械专项，保留静止相机。
    const FVector Socket=Gun->GetSocketLocation(Weapon->Config.MuzzleFlashSocket); // 独立真枪组件，绝不调用生产校准函数计算期望。
    // 从刚渲染的视口中心反投影；这个见证不借用生产的TraceAimPoint和构图数学。
    int32 ScreenX=0,ScreenY=0;
    PC->GetViewportSize(ScreenX,ScreenY);
    const FVector2D Reticle(ScreenX*.5,ScreenY*.5);
    FVector RayStart,Ray;
    if (!PC->DeprojectScreenPositionToWorld(Reticle.X,Reticle.Y,RayStart,Ray))
    { FinishPresentationAudit(false,TEXT("reticle deprojection failed")); return; }
    FVector Eye; FRotator View;
    PC->GetPlayerViewPoint(Eye,View);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoAuditReticle),false,Player);
    Query.AddIgnoredActor(Weapon);
    FHitResult Hit;
    const FVector End=Eye+Ray*Weapon->Config.Range;
    const bool bHit=World->LineTraceSingleByChannel(Hit,RayStart,End,DemoEnemyHitZones::TraceChannel,Query);
    const FVector Aim=bHit ? Hit.ImpactPoint : End;
    const FVector Bore=Gun->GetSocketTransform(Weapon->Config.MuzzleFlashSocket).GetUnitAxis(EAxis::X);
    const double Forward=FVector::DotProduct(Bore,Ray);
    const double Along=Forward>UE_SMALL_NUMBER ? FVector::DotProduct(Aim-Socket,Ray)/Forward : -1.;
    FVector2D Intercept;
    const bool bForward=Along>0. && PC->ProjectWorldLocationToScreen(Socket+Along*Bore,Intercept);
    const double ErrorPx=bForward ? FVector2D::Distance(Intercept,Reticle) : 1.e6;
    UE_LOG(LogFPSDemo,Display,TEXT("RETICLE_BORE_WITNESS gun=%s step=%d hit=%d depthCm=%.3f centerErrorPx=%.5f"),
        *Weapon->GetWeaponId().ToString(),Step,bHit,FVector::DotProduct(Aim-Eye,Ray),ErrorPx);
    if (ErrorPx>.5 || (Wall.IsValid() && bFixed && Hit.GetActor()!=Wall.Get()))
    { FinishPresentationAudit(false,TEXT("barrel does not intersect center at independently traced target depth")); return; }
    if (Step==82)
    {
        MuzzleEarlySocket=Socket; MuzzleShotFrame=GFrameCounter; MuzzlePawnStart=Player->GetActorLocation();
        const int32 BeforeShot=Weapon->GetAmmo();
        DemoHitZoneTest::Fire(Player); // 正常GA提交；姿态变化特意发生在同一帧的输入处理与相机求值之间。
        if (Weapon->GetAmmo()!=BeforeShot-Weapon->Config.AmmoPerShot) { FinishPresentationAudit(false,TEXT("fire GA did not pay and commit")); return; }
        if (!bFixed)
        {
            PC->SetControlRotation(PC->GetControlRotation()+FRotator(6,18,0));
            Player->GetCharacterMovement()->Velocity=FVector(0,480,0); // 本夹具设置玩家横移初速cm/s，随后由生产Movement推进；不改弹速。
            Player->AddMovementInput(FVector::RightVector,1.f,true);
        }
        const FString Capture=PresentationDirectory/(MuzzleAuditGunId().ToString()+TEXT("LaunchFrame.png"));
        FScreenshotRequest::RequestScreenshot(Capture,false,false); PresentationCaptures.Add(Capture);
        Step=83; return;
    }
    if (Step<83) return;
    const uint64 Age=GFrameCounter-MuzzleShotFrame; // 在下一次输入前复核刚渲染的三个画面，首段寿命两后续帧。
    if (Age>=3) FWorldDelegates::OnWorldPreActorTick.RemoveAll(this);
    double BestError=TNumericLimits<double>::Max();
    FVector Rear=FVector::ZeroVector, Front=FVector::ZeroVector; // 在实际渲染Cube局部两端独立测朝向。
    for (TActorIterator<ADemoProjectileTracer> It(World); It; ++It)
    {
        if (It->IsHidden() || It->IsActorBeingDestroyed()) continue;
        const FVector Candidate=It->GetActorTransform().TransformPosition(FVector(-50,0,0)); // 生产100cm Cube的真实近端。
        const double Error=FVector::Distance(Candidate,Socket);
        if (Error<BestError) { BestError=Error; Rear=Candidate; Front=It->GetActorTransform().TransformPosition(FVector(50,0,0)); }
    }
    FVector2D Pixel=FVector2D::ZeroVector; // 与刚渲染的PC视图共用投影；记录物理口心所在屏幕像素以供逐帧核图。
    FVector2D FrontPixel;
    const bool bProjected=PC->ProjectWorldLocationToScreen(Socket,Pixel) && PC->ProjectWorldLocationToScreen(Front,FrontPixel);
    const double AxisDot=FVector::DotProduct(Bore,(Front-Rear).GetSafeNormal());
    UE_LOG(LogFPSDemo,Display,TEXT("MUZZLE_FLASH_AXIS_WITNESS fixed=%d projected=%d socketY=%.4f reticleY=%.4f frontY=%.4f axisDot=%.8f socketX=%.2f frontX=%.2f"),
        bFixed,bProjected,Pixel.Y,ScreenY*.5,FrontPixel.Y,AxisDot,Pixel.X,FrontPixel.X);
    PresentationEvents.Add(FString::Printf(TEXT("socketPixel=%.2f %.2f"),Pixel.X,Pixel.Y));
    const double Travel=FVector::Distance(MuzzlePawnStart,Player->GetActorLocation());
    const double EarlyError=FVector::Distance(MuzzleEarlySocket,Socket);
    const FString Witness=FString::Printf(TEXT("shotFrame=%llu readFrame=%llu pawnTravel=%.6f earlyError=%.6f finalError=%.6f early=[%s] socket=[%s] tracerRear=[%s]"),
        MuzzleShotFrame,GFrameCounter,Travel,EarlyError,BestError,*MuzzleEarlySocket.ToString(),*Socket.ToString(),*Rear.ToString());
    PresentationEvents.Add(Witness); UE_LOG(LogFPSDemo,Display,TEXT("MOVING_MUZZLE_WITNESS %s"),*Witness);
    const double Side=Pixel.X-ScreenX*.5; // 左/右握位均保留准星一侧；另核出射线在真实画面有横向投影。
    const double Remaining=FMath::Abs(Side)>2. ? (FrontPixel.X-ScreenX*.5)/Side : 1.;
    if (Remaining<=0. || Remaining>1.001 || (Step==83 && (Age!=1 || (!bFixed && (Travel<.5 || EarlyError<1.)))) || BestError>.03
        || !bProjected || AxisDot<.9999)
    { FinishPresentationAudit(false,TEXT("missing same-render-frame moving muzzle alignment")); return; }
    // 同帧新组件可能等待首次PSO；保留随后三帧的真实RHI图，不用端点数学替代可见性。
    if (Step==83) { Step=84; NextTime=0.f; }
}

void ADemoProjectileTest::TickPresentationAudit(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (bPresentationFinished) return;
    const double Now=GetWorld()->GetTimeSeconds(),WallNow=FPlatformTime::Seconds();
    if (PresentationWallStart==0.)
    {
        PresentationWallStart=WallNow; PresentationGameStart=Now;
        PresentationDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("CameraClipFix20261114")/(TEXT("ProjectileFP_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+FString::Printf(TEXT("_%u"),FPlatformProcess::GetCurrentProcessId())));
        IFileManager::Get().MakeDirectory(*PresentationDirectory,true);
        PresentationRows.Add(TEXT("frame,engine_frame,seconds,delta,case,step,shot,hp,actual_distance,root_x,root_y,root_z,render_x,render_y,render_z,mesh_x,mesh_y,mesh_z,fp_x,fp_y,fp_z,frozen_fp_x,frozen_fp_y,frozen_fp_z,correction,active_slot,scope,tracer_enabled,render_scale,physical_root_scale_x,mesh_scale_x"));
        PresentationEvents.Add(TEXT("case,seconds,event"));
        if (!FParse::Param(FCommandLine::Get(),TEXT("game")) || !FParse::Param(FCommandLine::Get(),TEXT("DemoProjectileTest"))
            || GetWorld()->WorldType!=EWorldType::Game || GetNetMode()!=NM_Standalone || FParse::Param(FCommandLine::Get(),TEXT("nullrhi")))
        { FinishPresentationAudit(false,TEXT("requires explicit Editor -game Standalone RHI -DemoProjectileTest")); return; }
        bPresentationScopeReady=true;
        UE_LOG(LogFPSDemo,Display,TEXT("PROJECTILE_PRESENTATION_AUDIT_BEGIN directory=%s"),*PresentationDirectory);
    }
    if (WallNow-PresentationWallStart>90. || Now-PresentationGameStart>35.)
    { FinishPresentationAudit(false,TEXT("bounded 90 wall / 35 game seconds exceeded")); return; }
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    if (!Mode || !State || !Player || !Player->GetWeaponComponent()) return;
    PresentationPawn=Player;
    if (Step>=3)
    {
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It)
            if (*It!=Target.Get()) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 天然怪仍登记存活，不杀怪或伪造清关。
        ADemoProjectileBase* Bullet=PresentationBullet.Get();
        if (Bullet && !Bullet->IsActorBeingDestroyed())
        {
            const FVector Root=Bullet->GetActorLocation(),Render=Bullet->GetCosmeticLocationForAudit(),FP=Pistol->GetVisualMuzzleLocation();
            const UStaticMeshComponent* Mesh=Bullet->FindComponentByClass<UStaticMeshComponent>();
            const FVector MeshLocation=Mesh?Mesh->GetComponentLocation():FVector::ZeroVector;
            const float Correction=static_cast<float>(FVector::Distance(Root,Render));
            const float Distance=Bullet->GetActualDistanceForAudit();
            const float RenderScale=Bullet->GetCosmeticScaleForAudit(); // 真正component值，不以计算函数输出冒充运行变换。
            PresentationRows.Add(FString::Printf(TEXT("%d,%llu,%.9f,%.9f,%d,%d,%s,%.6f,%.6f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.6f,%d,%d,%d,%.9f,%.9f,%.9f"),PresentationFrame,GFrameCounter,Now,DeltaSeconds,PresentationCase,Step,*PresentationShot.ToString(),Target.IsValid()?Target->GetHealth():-1.f,Distance,
                Root.X,Root.Y,Root.Z,Render.X,Render.Y,Render.Z,MeshLocation.X,MeshLocation.Y,MeshLocation.Z,FP.X,FP.Y,FP.Z,PresentationVisualOrigin.X,PresentationVisualOrigin.Y,PresentationVisualOrigin.Z,Correction,Player->GetWeaponComponent()->GetActiveSlot(),Player->GetWeaponComponent()->GetScopeLevel(),ProjectileDefaults->Config.bEnableTracer,RenderScale,Bullet->GetActorScale3D().X,Mesh?Mesh->GetComponentScale().X:0.));
            ++PresentationFrame; ++PresentationShotFrames;
            if (Correction>PresentationPreviousCorrection+.05f || !Mesh || Mesh->bHiddenInGame)
            { FinishPresentationAudit(false,TEXT("actual live Mesh disappeared or frozen correction grew")); return; }
            PresentationPreviousCorrection=Correction;
            if (RenderScale<PresentationPreviousScale-1.e-5f || RenderScale>1.f+1.e-5f || !Bullet->GetActorScale3D().Equals(FVector::OneVector,1.e-6))
            { FinishPresentationAudit(false,TEXT("visible size reversed merge or physical root was scaled")); return; }
            PresentationPreviousScale=RenderScale;
            if (Distance>=ProjectileDefaults->Config.OwnerMuzzleBlendDistance+.5f)
            {
                if (!Render.Equals(Root,.03) || !FMath::IsNearlyEqual(RenderScale,1.f,1.e-5f)) { FinishPresentationAudit(false,TEXT("actual Mesh failed to join authoritative position and size")); return; }
                bPresentationSawHandoff=true; bPresentationCaseHandoff=true;
            }
            if (PresentationShotFrames==1 || PresentationShotFrames==3 || PresentationShotFrames==6 || PresentationShotFrames==12 || PresentationShotFrames==24)
            {
                const FString Path=PresentationDirectory/FString::Printf(TEXT("C%d_F%04d.png"),PresentationCase,PresentationFrame);
                FScreenshotRequest::RequestScreenshot(Path,false,false); PresentationCaptures.Add(Path);
            }
        }
    }
    if (Now<NextTime) return;
    if (Step==0)
    {
        if (PC->GetMenuFlow())
        {
            PC->GetMenuFlow()->UpdateRealtime(0.f);
            if (PC->GetMenuFlow()->GetMenuPage()==EDemoMenuPage::FirstRunGuide) PC->GetMenuFlow()->DispatchUIAction(EDemoUIAction::OverlayBack);
        }
        UDemoRunSaves* Saves=Player->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 当前GI临时slot0，枪权事务必须真实耐久，不能借旧无槽夹具。
        if (!Saves || !Saves->CreateSlot(0) || !Mode->StartRun()) { FinishPresentationAudit(false,TEXT("isolated slot / production StartRun rejected")); return; }
        Saves->ConsumePending(); // 当前World已初始化，避免下一次阶段误消费新槽恢复意图。
        PC->OnRunReady(); Step=1; NextTime=Now+.2; return;
    }
    if (Step==1)
    {
        UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
        const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr;
        if (!Definition || State->Phase!=EDemoPhase::Hub) { NextTime=Now+.1; return; }
        ADemoGunDisplay* RifleDisplay=nullptr; // 从当前实际可信目录匹配，不硬编码旧GunShop布局/Actor名。
        for (const FDemoHubDisplayBinding& Binding:Definition->Displays)
            if (const FDemoHubWeaponOffer* Offer=Definition->FindOffer(Binding.OfferId))
                if (Offer->WeaponId==(FParse::Param(FCommandLine::Get(),TEXT("DemoMovingMuzzleAudit")) && MuzzleAuditGunId()!=TEXT("pistol") ? MuzzleAuditGunId() : FName(TEXT("rifle")))) { RifleDisplay=Registry->FindDisplay(Binding.DisplayId); break; }
        if (!RifleDisplay || !RifleDisplay->IsDisplayReady()) { NextTime=Now+.1; return; }
        UDemoPlayerProfile* Profile=Player->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>();
        if (!Profile) { FinishPresentationAudit(false,TEXT("isolated Editor profile missing")); return; }
        bPresentationOriginalInfiniteGold=Profile->IsDebugTestFlagEnabled(0); bPresentationProfileFlagSaved=true;
        Profile->SetDebugTestFlag(0,true); // 既有本GI免扣规则，报价/本槽拥有权/真实0实付收据仍由原枪墙事务提交，不伪造余额。
        const FVector Focus=RifleDisplay->GetFocusPoint();
        const FVector DesiredEye=Focus+RifleDisplay->GetActorTransform().TransformVectorNoScale(FVector(0,-180,0)); // 当前北墙陈列的屋内侧，不挪动任何展示或DA。
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorLocation(Player->GetActorLocation()+DesiredEye-Player->GetSharedViewLocation(),false,nullptr,ETeleportType::TeleportPhysics);
        PC->SetControlRotation((Focus-Player->GetSharedViewLocation()).Rotation()); PC->SetViewTarget(Player);
        PresentationShopStarted=Now; Step=11; NextTime=Now+.5; return; // 给真实Focus扫描/稳定时间/服务端报价和相机各自Tick，不自行签发SlotEpoch。
    }
    if (Step==11)
    {
        if (Player->GetWeaponComponent()->GetActiveSlot()!=1)
        {
            if (Now-PresentationShopStarted>3.) { FinishPresentationAudit(false,TEXT("real gun-wall focus/quote/purchase timed out")); return; }
            UDemoShopFocusComponent* Focus=PC->FindComponentByClass<UDemoShopFocusComponent>();
            if (!Focus) { FinishPresentationAudit(false,TEXT("real gun-wall focus component missing")); return; }
            Focus->TryConsumeInteraction(); // 真实本人焦点/查询/购买RPC；服务端再次核距离/LOS/报价/slotEpoch/耐久。
            NextTime=Now+.25; return;
        }
        if (!Player->GetWeaponComponent()->EquipSlot(2)) { FinishPresentationAudit(false,TEXT("production purchased rifle to pistol switch failed")); return; }
        Step=12; NextTime=Now+.15; return; // 等实际FP/TP两套武器层切回手枪，再冻结测试配置。
    }
    if (Step==12)
    {
        Pistol=Player->GetWeaponComponent()->GetActiveWeapon();
        if (!Pistol.IsValid() || !Pistol->Config.ProjectileClass) { FinishPresentationAudit(false,TEXT("formal pistol projectile missing")); return; }
        ProjectileDefaults=Pistol->Config.ProjectileClass->GetDefaultObject<ADemoProjectileBase>();
        if (!FParse::Param(FCommandLine::Get(),TEXT("DemoMovingMuzzleAudit"))) // 原8案需要慢弹，四枪出膛专项使用正式配置。
        {
            PresentationOriginalProjectile=ProjectileDefaults->Config; PresentationOriginalWeapon=Pistol->Config; bPresentationSavedConfig=true;
            FDemoProjectileConfig::StaticStruct()->ExportText(PresentationOriginalProjectileText,&ProjectileDefaults->Config,nullptr,ProjectileDefaults,PPF_None,nullptr);
            ProjectileDefaults->Config.InitialSpeed=300.f; ProjectileDefaults->Config.MaxLifeSeconds=15.f; ProjectileDefaults->Config.bEnableTracer=false;
            RefreshPresentationAuditDefaults(); // 保原3000cm射程与寿命覆盖门；修复fixture更新CDO却遗漏BP初始化缓存的根因。
            Pistol->Config.Range=3000.f; Pistol->Config.FalloffStart=2500.f; Pistol->Config.BaseDamage=20.f; Pistol->Config.SpreadHalfAngle=0.f; Pistol->Config.RecoilPitch=0.f;
        }
        UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Player);
        if (!Build || (!Build->HasSelectedCore() && !Build->TrySelectCore(TEXT("B01")))) { FinishPresentationAudit(false,TEXT("production B01 core selection failed")); return; }
        Mode->StartNextLevel(); Step=2; NextTime=Now+.2; return;
    }
    if (Step==2)
    {
        if (State->Phase!=EDemoPhase::Combat) { NextTime=Now+.1; return; } // 原全队/流送准备屏障，不直接写Phase。
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); }
        Player->SetActorLocation(FVector(0,0,20000),false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately();
        Player->SetActorRotation(FRotator::ZeroRotator); PC->SetControlRotation(FRotator::ZeroRotator); PC->SetViewTarget(Player);
        if (!Player->GetPlayerPresentation() || !Player->GetPlayerPresentation()->UsesIndependentFirstPersonWeapons())
        { FinishPresentationAudit(false,TEXT("current candidate is not independent FP; use explicit preloaded Appearance candidate")); return; }
        Target=SpawnTarget(Player,700.f);
        if (!Target.IsValid()) { FinishPresentationAudit(false,TEXT("real rig-check PhysicsAsset target failed")); return; }
        Target->SetActorTickEnabled(false); DemoHitZoneTest::Ammo(Player,TEXT("normal"));
        float WallDistance=0.f; // 独立固定近/远实物深度，移动旧靶以免抢先挡住远处准星。
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoMovingMuzzleAudit")) && FParse::Value(FCommandLine::Get(),TEXT("DemoMuzzleAimWall="),WallDistance))
        {
            Target->AddActorWorldOffset(FVector(0,3000,0));
            Wall=SpawnWall(Player,WallDistance);
            if (!Wall.IsValid()) { FinishPresentationAudit(false,TEXT("aim-depth wall spawn failed")); return; }
        }
        AddTickPrerequisiteComponent(Player->GetMesh()); AddTickPrerequisiteComponent(Player->GetMesh1P());
        AddTickPrerequisiteComponent(Player->GetPlayerPresentation()); AddTickPrerequisiteComponent(Pistol->GetWeaponSkeletalMesh());
        Step=FParse::Param(FCommandLine::Get(),TEXT("DemoMovingMuzzleAudit"))?80:3; NextTime=Now+.5; return;
    }
    if (Step==80)
    {
        // 只在显式新专项执行；原有八案不改变定时和成功条件。选择本案实际装备。
        if (MuzzleAuditGunId()!=TEXT("pistol") && !Player->GetWeaponComponent()->EquipSlot(1)) { FinishPresentationAudit(false,TEXT("live gun equip rejected")); return; }
        Step=81; NextTime=Now+.3f; return;
    }
    if (Step==81)
    {
        FWorldDelegates::OnWorldPreActorTick.AddUObject(this,&ADemoProjectileTest::SampleMovingMuzzle);
        Step=82; return;
    }
    if (Step>=84 && Step<=86)
    {
        const FString Capture=PresentationDirectory/FString::Printf(TEXT("%sLaunchPlus%d.png"),*MuzzleAuditGunId().ToString(),Step-83);
        FScreenshotRequest::RequestScreenshot(Capture,false,false); PresentationCaptures.Add(Capture);
        ++Step; return;
    }
    if (Step==87) { FinishPresentationAudit(true,TEXT("reticle at target depth before fire; muzzle and flash axis on launch and two following frames")); return; }
    if (Step==3) { BeginPresentationAuditShot(); return; }
    if (Step==4)
    {
        if (PresentationCase==7)
        {
            const UDemoCombatSession* Session=Mode->GetRunFlow()->GetCombatSession(); // 同一World生产Shutdown实际失效，不能把不旅行的测试写成Hub已恢复。
            if (!Session || Session->IsActive() || DemoProjectileFixtures::CountLive(GetWorld())!=0)
            { FinishPresentationAudit(false,TEXT("real session Shutdown retained active session or old projectile")); return; }
            if (Target.IsValid() && Target->GetHealth()!=PresentationHPBefore) { FinishPresentationAudit(false,TEXT("cancelled projectile damaged target")); return; }
            Step=5; NextTime=Now+.2; return;
        }
        if (DemoProjectileFixtures::CountLive(GetWorld())>0)
        {
            if (Target->GetHealth()<PresentationHPBefore-.01f) { FinishPresentationAudit(false,TEXT("HP dropped before projectile was consumed at body contact")); return; }
            if (Now-PresentationShotStart>4.) { FinishPresentationAudit(false,TEXT("actual slow projectile failed to resolve within physical deadline")); return; }
            return;
        }
        PresentationHPAfter=Target->GetHealth();
        const bool bWall=PresentationCase==4 || PresentationCase==5;
        if ((bWall && PresentationHPAfter!=PresentationHPBefore) || (!bWall && PresentationHPAfter>=PresentationHPBefore-.01f))
        { FinishPresentationAudit(false,TEXT("real body contact or inserted-wall HP contract failed")); return; }
        if (!bWall) ++PresentationDamageCases;
        if (PresentationCase==0 && !bPresentationCaseHandoff) { FinishPresentationAudit(false,TEXT("Tracer-off case had no observed live Mesh handoff")); return; }
        PresentationEvents.Add(FString::Printf(TEXT("%d,%.9f,resolved_hp_%.6f_to_%.6f"),PresentationCase,Now,PresentationHPBefore,PresentationHPAfter));
        Step=5; NextTime=Now+.2; return;
    }
    if (Step==5)
    {
        if (Wall.IsValid()) { Wall->Destroy(); Wall.Reset(); }
        PC->SetViewTarget(Player);
        if (PresentationCamera.IsValid()) { PresentationCamera->Destroy(); PresentationCamera.Reset(); }
        ++PresentationCase;
        if (PresentationCase>=8) { FinishPresentationAudit(bPresentationSawLiveMesh&&bPresentationSawHandoff&&PresentationDamageCases>=5&&bPresentationResetCheckpointVerified,TEXT("seven original real GA Mesh/body/scope/switch/wall/view cases plus separate reset checkpoint disk readback and production session Shutdown completed; full ReturnHub/World travel remains unverified")); return; }
        Step=3; NextTime=Now+.2;
    }
}
#endif
