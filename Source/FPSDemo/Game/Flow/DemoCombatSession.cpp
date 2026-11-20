#include "Game/Flow/DemoCombatSession.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoRunFlowComponent.h"
bool UDemoCombatSession::Initialize(UWorld* World, const FString& RunId, int32 Level)
{
    DEMO_LOG_CALL();
    if (!World || World->GetNetMode() == NM_Client || RunId.IsEmpty() || Level < 1 || LevelNumber != 0)
    { UE_LOG(LogFPSDemo, Warning, TEXT("Combat session rejected: authority/identity/lifetime")); return false; }
    SessionWorld = World; CampaignId = RunId; LevelNumber = Level; bActive = true;
    return true;
}
void UDemoCombatSession::Invalidate()
{
    DEMO_LOG_CALL();
    if (!bActive) return;
    bActive = false; // 先撤销所有伤害凭据，清理回调即使同步触发碰撞也不能再扣血。
    TMap<TWeakObjectPtr<UObject>, FSimpleDelegate> Pending = MoveTemp(Cleanups); // 回调重入不能再次执行同一注册表。
    Cleanups.Reset();
    for (auto& Entry : Pending) // 委托仅在游戏线程此同步栈借用对象，销毁后的目标自动跳过。
        if (Entry.Key.IsValid()) Entry.Value.ExecuteIfBound();
}
bool UDemoCombatSession::RegisterCleanup(UObject* Owner, FSimpleDelegate Cleanup)
{
    DEMO_LOG_CALL();
    if (!IsValid(Owner) || !Cleanup.IsBoundToObject(Owner)) { UE_LOG(LogFPSDemo, Warning, TEXT("COMBAT_CLEANUP rejected unowned delegate")); return false; }
    if (!IsActive()) { Cleanup.ExecuteIfBound(); UE_LOG(LogFPSDemo, Log, TEXT("COMBAT_CLEANUP late registration cancelled")); return false; }
    if ((++Registrations % 128) == 0)
        for (auto It = Cleanups.CreateIterator(); It; ++It) // 周期清理已销毁Actor条目，不扫描World、不引用具体攻击类。
            if (!It.Key().IsValid()) It.RemoveCurrent();
    Cleanups.Add(Owner, MoveTemp(Cleanup)); return true;
}
UDemoCombatSession* UDemoCombatSession::FindActive(UWorld* World)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    const AFPSDemoGameMode* Mode = World ? World->GetAuthGameMode<AFPSDemoGameMode>() : nullptr; // 权威组合根仅提供会话，不让伤害依赖具体阶段策略。
    UDemoCombatSession* Session = Mode ? Mode->GetRunFlow()->GetCombatSession() : nullptr; // Flow拥有，调用方仅注册弱回调或弱持有。
    return Session && Session->IsActive() ? Session : nullptr;
}
bool UDemoCombatSession::IsActive() const { DEMO_LOG_TICK(); return bActive && SessionWorld.IsValid(); }
bool UDemoCombatSession::Matches(const FString& RunId, int32 Level) const
{ DEMO_LOG_TICK(); return IsActive() && CampaignId == RunId && LevelNumber == Level; }
