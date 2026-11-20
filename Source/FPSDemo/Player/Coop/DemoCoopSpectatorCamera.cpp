#include "Player/Coop/DemoCoopSpectatorCamera.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

ADemoCoopSpectatorCamera::ADemoCoopSpectatorCamera()
{
    DEMO_LOG_CALL();
    bReplicates = false; // 每个观察者位置独立，服务器不需要复制相机Actor。
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    GetCameraComponent()->SetFieldOfView(70.f);
}

bool ADemoCoopSpectatorCamera::Observe(ADemoPlayerController* Observer, ADemoCharacter* Target)
{
    DEMO_LOG_CALL();
    if (!IsValid(Observer) || !Observer->IsLocalController() || !IsValid(Target)
        || Observer->GetWorld() != GetWorld() || Target->GetWorld() != GetWorld() || Target == Observer->GetPawn())
    { UE_LOG(LogFPSDemo, Warning, TEXT("COOP_SPECTATOR_REJECT invalid local observer/teammate")); return false; }
    ObservingController = Observer; ObservedCharacter = Target; bLoggedMissingTarget = false;
    SetOwner(Observer); // ViewActor仍是本Camera，既不是目标Pawn也不是目标PC，不会触发其OwnerNoSee。
    UpdateView();
    UE_LOG(LogFPSDemo, Log, TEXT("COOP_SPECTATOR_TARGET target=%s"), *GetNameSafe(Target));
    return true;
}

void ADemoCoopSpectatorCamera::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (!ObservingController.IsValid() || ObservingController->GetViewTarget() != this)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("COOP_SPECTATOR_END view replaced or controller unloaded"));
        Destroy(); return; // 现有清关/复活ClientSetViewTarget(Pawn)即可结束，不依赖Pawn与PC跨通道复制先后。
    }
    UpdateView();
}

void ADemoCoopSpectatorCamera::UpdateView()
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Target = ObservedCharacter.Get(); // 同步借用当前目标；不访问它的远端Controller。
    if (!IsValid(Target))
    {
        if (!bLoggedMissingTarget) UE_LOG(LogFPSDemo, Log, TEXT("COOP_SPECTATOR_WAIT target gone; keeping last view until authoritative retarget"));
        bLoggedMissingTarget = true; return;
    }
    const FVector Focus = Target->GetActorLocation() + FVector(0, 0, 48.f); // 胶囊中心上48cm的胸肩点，不依赖骨骼LOD。
    const FRotator Heading(0.f, Target->GetActorRotation().Yaw, 0.f); // 只跟随复制角色Yaw，不猜测未复制的远端相机Pitch。
    const FVector Desired = Focus + Heading.RotateVector(FVector(-300.f, 105.f, 88.f)); // 后侧第三人称视角，单位厘米。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoCoopSpectator), false, Target); // 忽略被观察者胶囊；展示枪本身无碰撞。
    Query.AddIgnoredActor(this);
    if (ObservingController.IsValid()) Query.AddIgnoredActor(ObservingController->GetPawn()); // 死者胶囊即使复制稍晚也不挤压镜头。
    FHitResult Hit; // 本帧本地相机小球扫掠，不参与伤害或移动校正。
    const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Focus, Desired, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.f), Query);
    const FVector Position = bBlocked ? Hit.Location : Desired; // Hit.Location已是小球中心接触点，无需再次扣半径。
    SetActorLocationAndRotation(Position, (Focus - Position).Rotation());
}

ADemoCharacter* ADemoCoopSpectatorCamera::GetObservedCharacter() const
{ DEMO_LOG_TICK(); return ObservedCharacter.Get(); }
