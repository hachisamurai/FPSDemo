// 职责：双端探针的正式UI准备观察器与纯值合同；仅Editor编译，复用原Roster/Session/目标真值，无业务写入口。
#include "Tests/DemoNetworkProbeSetup.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DemoNetworkProbeSetup::EResult DemoNetworkProbeSetup::Evaluate(const FSnapshot& Snapshot,double ElapsedSeconds,double TimeoutSeconds,FString& Reason)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoNetworkProbeSetup::Evaluate")); Reason.Empty();
    if(!FMath::IsFinite(ElapsedSeconds)||ElapsedSeconds<0.||!FMath::IsFinite(TimeoutSeconds)||TimeoutSeconds<30.||TimeoutSeconds>600.)
    { Reason=TEXT("SetupInvalidClockOrLimit"); return EResult::Failed; }
    if(ElapsedSeconds>=TimeoutSeconds) { Reason=TEXT("SetupTimeout: finish formal UI preparation before retrying probe"); return EResult::Failed; }
    if(Snapshot.RegisteredPlayers<0||Snapshot.RegisteredPlayers>2) { Reason=TEXT("SetupRequiresExactlyTwoRegisteredPlayers"); return EResult::Failed; }
    if(!Snapshot.bListenServer||Snapshot.RegisteredPlayers!=2) { Reason=TEXT("WaitingForFormalListenParty"); return EResult::Waiting; }
    if(Snapshot.Phase==EPhase::Lobby) { Reason=TEXT("WaitingForSafeHub"); return EResult::Waiting; }
    if(Snapshot.Phase==EPhase::Other) { Reason=TEXT("SetupRequiresFreshHubOrFirstCombat"); return EResult::Failed; }
    if(Snapshot.bHubOnly)
    {
        if(Snapshot.Phase==EPhase::Hub&&Snapshot.Level==0) return EResult::Ready;
        Reason=TEXT("HubOnlyProbeRequiresInitialHub"); return EResult::Failed;
    }
    if(Snapshot.Phase==EPhase::Hub)
    {
        if(Snapshot.Level!=0) { Reason=TEXT("SetupRequiresFreshCheckpointSlot"); return EResult::Failed; }
        Reason=TEXT("WaitingForFormalUI: each player selects core, opens next terminal and readies; host starts and waits for streaming"); return EResult::Waiting;
    }
    if(Snapshot.Level!=1) { Reason=TEXT("SetupRequiresFirstCombatLevel"); return EResult::Failed; }
    if(!Snapshot.bCurrentSession) { Reason=TEXT("SetupCombatSessionInvalid"); return EResult::Failed; }
    if(!Snapshot.bObjectiveActive) { Reason=TEXT("WaitingForActiveObjective"); return EResult::Waiting; }
    if(!Snapshot.bKillAll) { Reason=TEXT("SetupRequiresKillAllForExistingProbePlan"); return EResult::Failed; }
    return EResult::Ready;
}

double DemoNetworkProbeSetup::ReadTimeoutSeconds()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoNetworkProbeSetup::ReadTimeoutSeconds"));
    float Seconds=180.f; // 参数只影响准备等待，不改变各探针动作预算或生产30秒加载超时。
    if(FParse::Value(FCommandLine::Get(),TEXT("DemoNetworkSetupTimeout="),Seconds)&&(!FMath::IsFinite(Seconds)||Seconds<30.f||Seconds>600.f))
    { UE_LOG(LogFPSDemo,Warning,TEXT("NETWORK_PROBE_SETUP invalid timeout; using 180 seconds")); return 180.; }
    return Seconds;
}

DemoNetworkProbeSetup::EResult DemoNetworkProbeSetup::Poll(const AActor* Probe,bool bHubOnly,double ElapsedSeconds,double TimeoutSeconds,FString& Reason)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoNetworkProbeSetup::Poll"));
    if(!Probe||!Probe->HasAuthority()||!Probe->GetWorld()) { Reason=TEXT("SetupRequiresAuthoritativeProbeWorld"); return EResult::Failed; }
    UWorld* World=Probe->GetWorld(); // 当前Tick同步借用，不保存旧World或异步捕获。
    const AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>(); // 当前服务器编排，只借用真实Roster/Flow。
    const ADemoGameState* State=World->GetGameState<ADemoGameState>(); // 当前已发布阶段/关号/目标，不写快照。
    FSnapshot Snapshot; Snapshot.bHubOnly=bHubOnly; Snapshot.bListenServer=World->GetNetMode()==NM_ListenServer;
    if(Mode&&Mode->GetCoopRoster()) Snapshot.RegisteredPlayers=Mode->GetCoopRoster()->GetPlayers().Num();
    if(State)
    {
        Snapshot.Level=State->LevelNumber;
        Snapshot.Phase=State->Phase==EDemoPhase::Hub?EPhase::Hub:State->Phase==EDemoPhase::Combat?EPhase::Combat:State->Phase==EDemoPhase::Lobby?EPhase::Lobby:EPhase::Other;
        Snapshot.bObjectiveActive=State->Objective.State==EDemoObjectiveState::Active; Snapshot.bKillAll=State->Objective.Kind==EDemoObjectiveKind::KillAll;
        const UDemoRunFlowComponent* Flow=Mode?Mode->GetRunFlow():nullptr; // 身份使用当前Flow，不能只查世界上是否有某个旧Session。
        const UDemoCombatSession* Session=Flow?Flow->GetCombatSession():nullptr;
        Snapshot.bCurrentSession=Session&&Session->Matches(Flow->GetRunId(),State->LevelNumber);
    }
    return Evaluate(Snapshot,ElapsedSeconds,TimeoutSeconds,Reason);
}

bool DemoNetworkProbeSetup::CanStartLocally(const AActor* Probe,bool bHubOnly)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoNetworkProbeSetup::CanStartLocally"));
    if(!Probe||!Probe->GetWorld()) return false;
    const ADemoGameState* State=Probe->GetWorld()->GetGameState<ADemoGameState>();
    const ADemoPlayerController* LocalPC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(Probe,0)); // 隔离双进程各只有一个LocalPlayer；不取服务器远端PC代发操作。
    const ADemoCharacter* Pawn=LocalPC?Cast<ADemoCharacter>(LocalPC->GetPawn()):nullptr;
    if(!State||!LocalPC||!LocalPC->IsLocalController()||!LocalPC->IsCoopRegistered()||!Pawn||LocalPC->GetViewTarget()!=Pawn||Pawn->IsCoopEliminated()||!Pawn->GetDemoASC()||Pawn->GetDemoASC()->GetAvatarActor()!=Pawn||!Pawn->GetDemoAttributes()||Pawn->GetDemoAttributes()->GetHealth()<=0.f
        ||LocalPC->HasBlockingOverlay()||LocalPC->IsUpgradeMenuOpen()||LocalPC->IsNextLevelConfirmationOpen()||LocalPC->IsPauseMenuOpen()) return false;
    return bHubOnly?(State->Phase==EDemoPhase::Hub&&State->LevelNumber==0):(State->Phase==EDemoPhase::Combat&&State->LevelNumber==1&&State->Objective.State==EDemoObjectiveState::Active&&State->Objective.Kind==EDemoObjectiveKind::KillAll);
}
#endif
