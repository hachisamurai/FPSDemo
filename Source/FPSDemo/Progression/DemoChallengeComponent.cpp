#include "Progression/DemoChallengeComponent.h"
#include "Game/DemoGameState.h"
#include "Save/DemoRunSave.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"

UDemoChallengeComponent::UDemoChallengeComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
void UDemoChallengeComponent::BeginRun(const FString& RunId)
{
    DEMO_LOG_CALL();
    if (RunId == ActiveRunId) { UE_LOG(LogFPSDemo, Log, TEXT("CHALLENGE_BEGIN duplicate ignored")); return; }
    RestoreProgress(RunId, 0);
}
bool UDemoChallengeComponent::RestoreProgress(const FString& RunId, int32 Progress)
{
    DEMO_LOG_CALL();
    FGuid Parsed; // 只接受稳定轮次身份，避免空ID让旧请求复用资格。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前World显示投影。
    if (!GetOwner()->HasAuthority() || !State || !FGuid::Parse(RunId, Parsed) || !Parsed.IsValid() || Progress < 0 || Progress > 2 || bStopped)
    { UE_LOG(LogFPSDemo, Warning, TEXT("CHALLENGE_RESTORE rejected identity/progress/lifecycle")); return false; }
    ActiveRunId = RunId; Qualification = Progress; State->PistolChallenge = Qualification;
    return true;
}
bool UDemoChallengeComponent::TryCommitWeaponShot(const FString& RunId, FName WeaponId)
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 重新验证阶段，窗口或旧Pawn不能重用开火授权。
    UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI持久化资格，不保存半场战利品。
    if (bStopped || !GetOwner()->HasAuthority() || !State || !Saves || State->Phase != EDemoPhase::Combat
        || ActiveRunId.IsEmpty() || RunId != ActiveRunId || DemoWeaponCatalog::IndexOf(WeaponId) == INDEX_NONE)
    { UE_LOG(LogFPSDemo, Log, TEXT("CHALLENGE_SHOT rejected phase/run/weapon/lifecycle")); return false; }
    const int32 Next = WeaponId == TEXT("pistol") ? FMath::Max(1, Qualification) : 2; // 空弹匣/失败射击不进入；散弹按一次开火记录。
    if (Next != Qualification) UE_LOG(LogFPSDemo, Log, TEXT("PISTOL_CHALLENGE %d -> %d weapon=%s"), Qualification, Next, *WeaponId.ToString());
    Qualification = Next; State->PistolChallenge = Qualification;
    return Saves->StoreChallenge(Qualification, State->BestEndlessLevel); // 保存失败拒绝本次伤害，内存失格保留以待重试。
}
void UDemoChallengeComponent::Stop() { DEMO_LOG_CALL(); bStopped = true; }
