// 职责：拥有者PC的入场资源握手RPC适配。使用GM发送有限请求、本地组件异步预载；服务端回执从真实PC解析身份，不接受客户端资源或落点。
#include "Player/DemoPlayerController.h"
#include "Player/Coop/Preparation/DemoLocalEncounterPreload.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Preparation/DemoEncounterPreparationComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

void ADemoPlayerController::ClientPrepareEncounter_Implementation(const FDemoEncounterPreloadRequest& Request)
{
    DEMO_LOG_CALL();
    if (IsLocalController()) if (UDemoLocalEncounterPreload* Preload = FindComponentByClass<UDemoLocalEncounterPreload>()) Preload->BeginPreload(Request);
}

void ADemoPlayerController::ClientCancelEncounterPreload_Implementation(int32 Generation)
{
    DEMO_LOG_CALL();
    if (IsLocalController()) if (UDemoLocalEncounterPreload* Preload = FindComponentByClass<UDemoLocalEncounterPreload>()) Preload->CancelPreload(Generation);
}

void ADemoPlayerController::ServerReportEncounterPreload_Implementation(int32 Generation, bool bReady, FName Code)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSDemoGameMode>() : nullptr; // 真实拥有者可靠RPC，不能传入其他Player。
    if (!Mode || !IsCoopRegistered() || Generation <= 0) { UE_LOG(LogFPSDemo, Log, TEXT("PRELOAD_REPORT rejected registration/world/generation")); return; }
    Mode->GetPreparationSystem()->ReportReady(this, Generation, bReady, Code);
}

void ADemoPlayerController::ClientCommitEncounterPreload_Implementation(int32 Generation)
{
    DEMO_LOG_CALL();
    if (IsLocalController()) if (UDemoLocalEncounterPreload* Preload = FindComponentByClass<UDemoLocalEncounterPreload>()) Preload->CommitPreload(Generation);
}

void ADemoPlayerController::ClientResetEncounterRooms_Implementation()
{
    DEMO_LOG_CALL();
    if (IsLocalController()) if (UDemoLocalEncounterPreload* Preload = FindComponentByClass<UDemoLocalEncounterPreload>()) Preload->ResetRooms();
}
