#include "Player/DemoPlayerController.h"
#include "Player/Coop/DemoCoopSpectatorCamera.h"
#include "Characters/DemoCharacter.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

void ADemoPlayerController::ClientSpectateTeammate_Implementation(ADemoCharacter* Target)
{
    DEMO_LOG_CALL();
    if (!IsLocalController() || !IsValid(Target) || Target == GetPawn())
    { UE_LOG(LogFPSDemo, Warning, TEXT("COOP_SPECTATOR_REJECT invalid target/local controller")); return; }
    ADemoCoopSpectatorCamera* Camera = Cast<ADemoCoopSpectatorCamera>(GetViewTarget()); // 同一死者换目标时复用本地Camera，不改队友视角。
    const bool bCreated = Camera == nullptr; // 失败时仅销毁本次创建的对象，不销毁现有有效观战画面。
    if (!Camera)
    {
        FActorSpawnParameters Spawn; // 本机瞬时Camera归PC，不复制给其他玩家。
        Spawn.Owner = this; Spawn.ObjectFlags |= RF_Transient;
        Camera = GetWorld()->SpawnActor<ADemoCoopSpectatorCamera>(ADemoCoopSpectatorCamera::StaticClass(), FTransform::Identity, Spawn);
    }
    if (!Camera || !Camera->Observe(this, Target))
    {
        if (bCreated && Camera) Camera->Destroy();
        UE_LOG(LogFPSDemo, Error, TEXT("COOP_SPECTATOR_REJECT camera unavailable")); return;
    }
    SetViewTarget(Camera); // 不能直接用目标Pawn，OwnerNoSee按ViewActor判定，会隐藏TP并暴露未同步的远端FP层。
}
