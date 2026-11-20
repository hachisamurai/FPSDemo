// 职责：真实子弹/霰弹/近战扣血、无损血拒绝、接触点与10000次爆发验证；使用显式Editor进程，资源通过生产DA加载，压力事件不伪造Gameplay伤害。
#include "Tests/DemoMetalImpactTest.h"
#include "Tests/DemoProjectileFixtures.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Combat/Feedback/DemoHitFeedbackComponent.h"
#include "Combat/Feedback/DemoMetalImpactSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Debug/DemoLog.h"

ADemoMetalImpactTest::ADemoMetalImpactTest()
{ DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork; }
bool ADemoMetalImpactTest::Check(bool Value,const TCHAR* Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Display,TEXT("METAL_TEST %s step=%d %s"),Value?TEXT("PASS"):TEXT("FAIL"),Step,Message);
    if (!Value) { bFailed=true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,1); }
    return Value;
}
void ADemoMetalImpactTest::Advance(float Delay) { DEMO_LOG_CALL(); ++Step; NextAt=GetWorld()->GetTimeSeconds()+Delay; }
void ADemoMetalImpactTest::Capture(const TCHAR* Name)
{
    DEMO_LOG_CALL();
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMetalImpactCapture")))
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("MetalImpactAcceptance")/Name,false,false);
        for (TActorIterator<AActor> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("DemoMetalImpactPool"))) // 同时核对真正注册组件状态，避免只凭准入计数声称画面正确。
        {
            TInlineComponentArray<UInstancedStaticMeshComponent*> Meshes(*It); // 本次截图同步借用两个ISM。
            for (UInstancedStaticMeshComponent* Mesh : Meshes) // 只记录可见实例，不输出全部312个空槽。
            {
                int32 Visible=0; // 当前非零缩放实例数，实际CPU缓冲。
                for (int32 Index=0;Index<Mesh->GetInstanceCount();++Index)
                {
                    FTransform Transform; Mesh->GetInstanceTransform(Index,Transform,true); // 世界坐标验证高空测试没有变回原点。
                    if (Transform.GetScale3D().IsNearlyZero()) continue;
                    ++Visible;
                    UE_LOG(LogFPSDemo,Log,TEXT("METAL_CAPTURE_INSTANCE mesh=%s point=%s scale=%s fade=%.3f"),*Mesh->GetName(),*Transform.GetLocation().ToCompactString(),*Transform.GetScale3D().ToCompactString(),Mesh->PerInstanceSMCustomData.IsValidIndex(Index)?Mesh->PerInstanceSMCustomData[Index]:-1.f);
                }
                UE_LOG(LogFPSDemo,Display,TEXT("METAL_CAPTURE %s mesh=%s visible=%d hidden=%d actorhidden=%d bounds=%s"),Name,*Mesh->GetName(),Visible,Mesh->bHiddenInGame,It->IsHidden(),*Mesh->Bounds.Origin.ToCompactString());
            }
        }
    }
}
void ADemoMetalImpactTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (bFailed) return;
#if WITH_EDITOR
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoHitFxReel")))
    {
        TickHitFxReel();
        return;
    }
#endif
    const double Now=GetWorld()->GetTimeSeconds(); // 游戏秒，逐帧观察实际事件不阻塞。
    if (Now>30.) { Check(false,TEXT("bounded runtime")); return; }
    UDemoMetalImpactSubsystem* Pool=GetWorld()->GetSubsystem<UDemoMetalImpactSubsystem>(); // 此World池，指针不跨帧保存。
    if ((Step==2 || Step==6) && Pool && Pool->GetStats().Played>Before.Played && !bCaptured)
    {
        if (ImpactAt<0.) ImpactAt=Now; // 首帧回执已经到达，等待2帧让渲染线程消费实例数据。
        if (Now-ImpactAt>=.035) { Capture(Step==2?TEXT("01-BulletMetalImpact.png"):TEXT("02-MeleeMetalImpact.png")); bCaptured=true; }
    }
    if (Now<NextAt) return;
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本机生产控制器。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 真实Avatar，禁止伪造ASC。
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 独立单人权威进程。
    if (!Check(Pool && Player && Mode,TEXT("production world and pool initialized"))) return;
    UDemoMeleeComponent* Melee=Player->FindComponentByClass<UDemoMeleeComponent>(); // 默认生产组件，本帧借用。
    switch (Step)
    {
    case 0:
    {
        if (!Check(Mode->StartRun(),TEXT("isolated run hub"))) return;
        PC->OnRunReady();
        if (!Check(DemoProjectileFixtures::EquipPrimary(Player,1) && Player->GetWeaponComponent()->EquipSlot(2),TEXT("prepare real shotgun/pistol in hub"))) return;
        Mode->StartNextLevel();
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 保留战役计数，仅隔离天然靶。
        Player->SetActorLocation(FVector(0,0,20000)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator::ZeroRotator); Player->SetActorRotation(FRotator::ZeroRotator);
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 高空测试位置不受关卡挡墙影响。
        Target=GetWorld()->SpawnActor<ADemoEnemy>(FVector(1000,0,20000),FRotator::ZeroRotator,Params); // 初始探针前后各500cm，必须远离玩家，防查询先撞到玩家胶囊。
        FDemoEnemySpawnStats Spawn; Spawn.Health=5000.f; Spawn.AttackPower=0.f; // 不改伤害算法，仅靶高生命、无反击。
        Target->Configure(Spawn); Target->SetActorTickEnabled(false);
        USkeletalMeshComponent* Mesh=DemoHitZoneTest::Pose(Target.Get(),TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f); // 生产骨架真实碰撞体。
        if (!Check(DemoHitZoneTest::FindRegion(Mesh,Target->GetHitProfile(),EDemoEnemyHitRegion::Body,BodyPoint,TEXT("core")),TEXT("real chest surface probe, chest core is normal body damage"))) return; // 胸口正面接触点，避免脚底/推进器的视角遮挡掩盖火花。
        ADirectionalLight* Light=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-40,130,0)); // 仅空中验收补光，不保存地图。
        Light->GetLightComponent()->SetMobility(EComponentMobility::Movable); Light->GetLightComponent()->SetIntensity(3.f);
        Advance(.25f); break;
    }
    case 1:
    {
        ADemoWeaponBase* Weapon=Player->GetWeaponComponent()->GetActiveWeapon(); // 仅修改此测试实例以获得稳定可重复弹道。
        Weapon->Config.SpreadHalfAngle=0.f; Weapon->Config.RecoilPitch=0.f; Weapon->Config.BaseDamage=20.f; Weapon->Config.MuzzleSocket=NAME_None;
        DemoHitZoneTest::Aim(Target.Get(),Player,BodyPoint,280.f);
        Before=Pool->GetStats(); BeforeHealth=Target->GetHealth(); DemoHitZoneTest::Fire(Player);
        Advance(.65f); break;
    }
    case 2:
        if (!Check(FMath::IsNearlyEqual(BeforeHealth-Target->GetHealth(),20.f,.01f) && Pool->GetStats().Played==Before.Played+1,TEXT("real projectile HP damage creates exactly one spark burst"))) return;
        if (!Check(FVector::Distance(Pool->GetStats().LastPoint,Target->GetActorLocation())<150.f && FVector::Distance(Pool->GetStats().LastPoint,Player->GetActorLocation())>150.f,TEXT("effect uses target surface, not shooter origin"))) return;
        if (!Check(FVector::Distance(Pool->GetStats().LastPoint,Target->FindComponentByClass<USkeletalMeshComponent>()->GetComponentTransform().TransformPosition(BodyPoint))<10.f,TEXT("spark origin matches probed real body contact within swept projectile radius"))) return;
        Player->GetWeaponComponent()->EquipSlot(1); Advance(.25f); break;
    case 3:
    {
        ADemoWeaponBase* Weapon=Player->GetWeaponComponent()->GetActiveWeapon(); // 生产霰弹，多弹丸同向命中一只真怪，检查真实聚合。
        Weapon->Config.SpreadHalfAngle=0.f; Weapon->Config.RecoilPitch=0.f; Weapon->Config.MuzzleSocket=NAME_None;
        Before=Pool->GetStats(); BeforeHealth=Target->GetHealth(); DemoHitZoneTest::Fire(Player);
        Advance(.7f); break;
    }
    case 4:
        if (!Check(Target->GetHealth()<BeforeHealth && Pool->GetStats().Played==Before.Played+1 && Pool->GetStats().Deduplicated>Before.Deduplicated,TEXT("real shotgun multiple pellets damage, one target burst"))) return;
        Melee->SetEquipped(true); Target->SetActorLocation(Player->GetActorLocation()+FVector(95,0,0));
        DemoHitZoneTest::Pose(Target.Get(),TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f);
        Advance(.25f); break;
    case 5:
        Before=Pool->GetStats(); BeforeHealth=Target->GetHealth(); bCaptured=false; ImpactAt=-1.;
        Melee->RequestAttack(0);
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoMetalImpactCapture")))
        {
            PreviewCamera=GetWorld()->SpawnActor<ACameraActor>(Player->GetActorLocation()+FVector(-140,210,100),FRotator::ZeroRotator); // 从接触面同侧观察，不能隔着怪物头部检验受深度测试的火花。
            PreviewCamera->SetActorRotation((Player->GetActorLocation()+FVector(60,0,40)-PreviewCamera->GetActorLocation()).Rotation());
            PreviewCamera->GetCameraComponent()->SetFieldOfView(52.f); PC->SetViewTarget(PreviewCamera.Get());
        }
        Advance(.9f); break;
    case 6:
        if (!Check(Target->GetHealth()<BeforeHealth && Pool->GetStats().Played==Before.Played+1 && Pool->GetStats().AudioPlayed==Before.AudioPlayed+1,TEXT("real melee damage emits surface sparks and pooled metal sound"))) return;
        Before=Pool->GetStats(); Target->SetActorEnableCollision(false); Melee->RequestAttack(0); Advance(.9f); break;
    case 7:
    {
        if (!Check(Pool->GetStats().Played==Before.Played && Pool->GetStats().AudioPlayed==Before.AudioPlayed,TEXT("missed melee creates neither monster spark nor metal sound"))) return;
        FDemoHitFeedbackEvent Zero; Zero.ShotId=FGuid::NewGuid(); Zero.TargetId=42; Zero.ImpactPoint=Player->GetActorLocation(); // 明确非法零伤害回执，生产入口必须拒绝。
        Player->FindComponentByClass<UDemoHitFeedbackComponent>()->Confirm(Zero);
        if (!Check(Pool->GetStats().Accepted==Before.Accepted,TEXT("zero HP receipt rejected before pool"))) return;
        Before=Pool->GetStats();
        const FGuid Same=FGuid::NewGuid(); // 压力用纯表现标识，不进入GAS或扣血。
        for (int32 Index=0;Index<1000;++Index) Pool->Admit(Same,900000,true); // 相同挥击目标1000次，只有一次准入。
        if (!Check(Pool->GetStats().Accepted==Before.Accepted+1 && Pool->GetStats().Deduplicated==Before.Deduplicated+999,TEXT("1000 repeated callbacks collapse to one"))) return;
        for (int32 Index=0;Index<10000;++Index) Pool->Admit(FGuid::NewGuid(),900001+Index,false); // 同帧10000个不同弹击，测全场突发桶而非只测同目标限流。
        if (!Check(Pool->GetStats().Accepted<=Before.Accepted+9 && Pool->GetStats().RateDropped>=Before.RateDropped+9992,TEXT("10000 unique bullet callbacks bounded by global burst budget"))) return;
        if (!Check(Pool->Admit(FGuid::NewGuid(),999999,true),TEXT("melee admission survives exhausted bullet budget"))) return;
        for (TActorIterator<AActor> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("DemoMetalImpactPool"))) PoolComponents=It->GetComponents().Num(); // 实际World组件数，与内部统计交叉验证。
        Before=Pool->GetStats();
        FDemoMetalImpactEvent Event; Event.Point=Player->GetActorLocation()+FVector(120,0,50); Event.Normal=FVector(-1,0,0); // 压力直接驱动本地池，证明即使网络异常爆发也不会创建对象。
        for (int32 Index=0;Index<10000;++Index) Pool->Play(Event); // 超出视觉容量只丢表现。
        Event.bMelee=true; Pool->Play(Event); // 公共槽满后必须仍有近战预留槽。
        if (!Check(Pool->GetStats().Played==Before.Played+21 && Pool->GetStats().PoolDropped==Before.PoolDropped+9980 && Pool->GetStats().PeakActive<=24,TEXT("20 bullet slots plus reserved melee, 9980 bursts omitted"))) return;
        for (TActorIterator<AActor> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("DemoMetalImpactPool"))) // 无新Actor/组件/ISM实例增长。
        {
            if (!Check(It->GetComponents().Num()==PoolComponents,TEXT("actual pool component count unchanged after 10000 events"))) return;
            TInlineComponentArray<UInstancedStaticMeshComponent*> Meshes(*It); // 测试栈枚举实际ISM实例，不用内部期望值冒充实测。
            int32 Instances=0; for (UInstancedStaticMeshComponent* Mesh : Meshes) Instances+=Mesh->GetInstanceCount(); // 全场实例数固定312。
            if (!Check(Instances==312,TEXT("actual preallocated ISM instance count stays 312"))) return;
        }
        Advance(.6f); break;
    }
    case 8:
    {
        if (!Check(!Pool->IsTickable(),TEXT("expired pool stops ticking"))) return;
        Before=Pool->GetStats();
        FDemoMetalImpactEvent Far; Far.Point=Player->GetActorLocation()+FVector(30000,0,0); Far.bMelee=true; // 距离300m，火花/声音均应剔除。
        Pool->Play(Far);
        if (!Check(Pool->GetStats().DistanceDropped==Before.DistanceDropped+1 && Pool->GetStats().Played==Before.Played && Pool->GetStats().AudioPlayed==Before.AudioPlayed,TEXT("far visual and audio cull"))) return;
        Target->SetActorHiddenInGame(true); Capture(TEXT("03-MetalBatMaterial.png")); // 避免近身靶遮住金属棒身，镜头保留可见真实角色握姿。
        UE_LOG(LogFPSDemo,Display,TEXT("METAL_IMPACT_SUCCESS actual_projectile=1 shotgun_dedup=1 melee_audio=1 stress=10000 instances=312 components=%d peak=%d"),PoolComponents,Pool->GetStats().PeakActive);
        Advance(.25f); break;
    }
    default: FPlatformMisc::RequestExitWithStatus(false,0); SetActorTickEnabled(false); break;
    }
}
