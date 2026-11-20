#include "Tests/DemoPlayerSpectatorTest.h"
#include "Player/Coop/DemoCoopSpectatorCamera.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "UnrealClient.h"
#include "Debug/DemoLog.h"

ADemoPlayerSpectatorTest::ADemoPlayerSpectatorTest()
{
    DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .05f; PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}
bool ADemoPlayerSpectatorTest::Check(bool Condition, const TCHAR* Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Display, TEXT("PLAYER_SPECTATOR_TEST %s step=%d %s"), Condition ? TEXT("PASS") : TEXT("FAIL"), Step, Message);
    if (!Condition) { bFailed = true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 1); }
    return Condition;
}
void ADemoPlayerSpectatorTest::Advance(int32 Next, float Delay)
{
    DEMO_LOG_CALL(); Step = Next; NextTime = GetWorld()->GetTimeSeconds() + Delay;
}
void ADemoPlayerSpectatorTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
    if (GetWorld()->GetTimeSeconds() > 20.f) { Check(false, TEXT("timed out")); return; }
#if !WITH_EDITOR
    Check(false, TEXT("requires isolated WITH_EDITOR runtime")); return;
#else
    switch (Step)
    {
    case 0:
    {
        Controller = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
        LocalPawn = Controller.IsValid() ? Cast<ADemoCharacter>(Controller->GetPawn()) : nullptr;
        if (!Check(Controller.IsValid() && Controller->IsLocalController() && LocalPawn.IsValid(), TEXT("real local controller and pawn ready"))) return;
        OriginalOutfit = LocalPawn->GetOutfitColorIndex();
        FActorSpawnParameters Spawn; // 无控制器的瞬时远端替身；只验证本地观察，不伪造网络连接或GAS权限。
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Spawn.ObjectFlags |= RF_Transient;
        FirstTarget = GetWorld()->SpawnActor<ADemoCharacter>(ADemoCharacter::StaticClass(), FVector(0, 0, 20000), FRotator::ZeroRotator, Spawn);
        SecondTarget = GetWorld()->SpawnActor<ADemoCharacter>(ADemoCharacter::StaticClass(), FVector(1200, 150, 20000), FRotator::ZeroRotator, Spawn);
        if (!Check(FirstTarget.IsValid() && SecondTarget.IsValid(), TEXT("two independent full body targets spawned"))) return;
        FirstTarget->GetCharacterMovement()->DisableMovement(); SecondTarget->GetCharacterMovement()->DisableMovement();
        LocalPawn->SetCoopEliminated(true); // 走正式本人生死入口，不解除Possess/经济关系。
        Controller->ClientSpectateTeammate(FirstTarget.Get()); // Standalone调用真实拥有者RPC实现；传输覆盖需另行双进程测试。
        Camera = Cast<ADemoCoopSpectatorCamera>(Controller->GetViewTarget());
        if (!Check(Camera.IsValid() && Camera->GetObservedCharacter() == FirstTarget.Get() && !Camera->GetIsReplicated(), TEXT("local camera is distinct ViewTarget and observes first teammate"))) return;
        if (!Check(FirstTarget->GetMesh()->bOwnerNoSee && FirstTarget->GetMesh1P()->bOnlyOwnerSee
            && !FirstTarget->IsOwnedBy(Camera.Get()), TEXT("ViewActor ownership selects full body and hides private FP mesh"))) return;
        InitialCameraLocation = Camera->GetActorLocation();
        FirstTarget->AddActorWorldOffset(FVector(500, 0, 0));
        Advance(1); break;
    }
    case 1:
    {
        if (!Check(Camera.IsValid() && FVector::Dist(Camera->GetActorLocation(), InitialCameraLocation) > 450.f, TEXT("camera followed actual target translation"))) return;
        const FVector Focus = FirstTarget->GetActorLocation() + FVector(0, 0, 48); // 与预期胸肩点比较光轴，未手调正式相机Tick。
        if (!Check(FVector::DotProduct(Camera->GetActorForwardVector(), (Focus - Camera->GetActorLocation()).GetSafeNormal()) > .999f, TEXT("camera faces teammate rather than unrelated controller aim"))) return;
        AStaticMeshActor* Blocker = GetWorld()->SpawnActor<AStaticMeshActor>(Focus + FVector(-150, 52.5f, 44), FRotator::ZeroRotator); // 后置相机路径中点上的真实阻挡墙。
        if (!Check(Blocker != nullptr, TEXT("camera blocker spawned"))) return;
        Blocker->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Blocker->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Blocker->SetActorScale3D(FVector(.2f, 3, 3));
        Blocker->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Blocker->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
        Wall = Blocker; Advance(2); break;
    }
    case 2:
    {
        const FVector Focus = FirstTarget->GetActorLocation() + FVector(0, 0, 48); // 独立检查Camera真实位置已被墙面约束。
        if (!Check(Camera.IsValid() && FVector::Dist(Camera->GetActorLocation(), Focus) < 200.f, TEXT("camera sphere sweep stopped before wall"))) return;
        if (Wall.IsValid()) Wall->Destroy();
        Controller->ClientSpectateTeammate(SecondTarget.Get());
        if (!Check(Controller->GetViewTarget() == Camera.Get() && Camera->GetObservedCharacter() == SecondTarget.Get(), TEXT("authoritative retarget reuses local camera"))) return;
        Advance(3); break;
    }
    case 3:
    {
        if (!Check(Camera.IsValid() && Camera->GetActorLocation().X > 800.f, TEXT("camera follows replacement teammate"))) return;
        if (FParse::Param(FCommandLine::Get(), TEXT("DemoSpectatorCapture")) && FApp::CanEverRender())
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("PlayerAcceptance/Spectator.png"), false, false);
        SecondTarget->Destroy(); Advance(4); break;
    }
    case 4:
    {
        if (!Check(Camera.IsValid() && Camera->GetObservedCharacter() == nullptr && Controller->GetViewTarget() == Camera.Get(), TEXT("lost target keeps safe TP view while waiting for server retarget"))) return;
        Controller->ClientSpectateTeammate(FirstTarget.Get());
        if (!Check(Camera->GetObservedCharacter() == FirstTarget.Get(), TEXT("camera accepts valid target after previous target destruction"))) return;
        LocalPawn->SetCoopEliminated(false); Controller->SetViewTarget(LocalPawn.Get()); // 既有清关复活恢复镜头路径，不调用观战专用清理来掩盖泄漏。
        Advance(5); break;
    }
    case 5:
        if (!Check(!Camera.IsValid() && Controller->GetViewTarget() == LocalPawn.Get() && !LocalPawn->IsCoopEliminated(), TEXT("revival restores owner view and destroys spectator camera"))) return;
        if (!Check(LocalPawn->GetOutfitColorIndex() == OriginalOutfit && Controller->GetPawn() == LocalPawn.Get(), TEXT("spectating preserves pawn ownership and outfit"))) return;
        if (FirstTarget.IsValid()) FirstTarget->Destroy();
        UE_LOG(LogFPSDemo, Display, TEXT("PLAYER_SPECTATOR_ACCEPTANCE_SUCCESS scope=local-world not-network-transport"));
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 0); break;
    default: Check(false, TEXT("unknown test stage")); break;
    }
#endif
}
