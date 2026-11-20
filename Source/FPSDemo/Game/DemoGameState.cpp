// 职责：注册公共战局复制和本地菜单通知，不在OnRep发奖或推进规则。
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Net/UnrealNetwork.h"
#include "Player/DemoPlayerController.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Engine/World.h"
#include "Player/Coop/Ping/DemoTeamPingComponent.h"

ADemoGameState::ADemoGameState()
{
    DEMO_LOG_CALL(); TeamPings = CreateDefaultSubobject<UDemoTeamPingComponent>(TEXT("TeamPings"));
}

UDemoTeamPingComponent* ADemoGameState::GetTeamPingComponent() const
{
    DEMO_LOG_TICK(); return TeamPings;
}

void ADemoGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	DEMO_LOG_CALL();
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADemoGameState, Phase);
	DOREPLIFETIME(ADemoGameState, PhaseRevision);
    DOREPLIFETIME(ADemoGameState, CampaignPlan);
    DOREPLIFETIME(ADemoGameState, Objective);
    DOREPLIFETIME(ADemoGameState, TeamReady);
    DOREPLIFETIME(ADemoGameState, Preparation);
	// UI 和实例生成共用同一个权威难度，客户端不能自行修改。
	DOREPLIFETIME(ADemoGameState, Difficulty);
	// 模式/本槽最高纪录/手枪资格由权威维护，复制只供HUD展示。
	DOREPLIFETIME(ADemoGameState, bEndless);
	// BestEndlessLevel 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。
	// PistolChallenge 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。
	DOREPLIFETIME(ADemoGameState, LevelNumber);
	DOREPLIFETIME(ADemoGameState, EnemiesRemaining);
	// TotalKills 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。
	// Coins 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。
	// 双币和各自购买次数跟随权威状态展示；不允许客户端自行交易。
	// SilverCoins 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。
	// GoldPurchases 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。
	// UpgradeProgress 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。 // 整个值账本由权威GameMode修改并随UI状态复制。
	// SilverPurchases 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。
	// Purchases 旧字段不再复制；本人数据由 PlayerState.Progress 的 COND_OwnerOnly 提供。
	DOREPLIFETIME(ADemoGameState, FailureReason);
	// 终局按钮以权威死亡原因决定目的地，不能靠本地文案推断重开语义。
	DOREPLIFETIME(ADemoGameState, bReturnToHubOnRestart);
}
void ADemoGameState::OnRep_RunView()
{
    DEMO_LOG_CALL();
    UWorld* World = GetWorld(); // 同步借用当前World；销毁期间通知不得访问下一World或发送攻击请求。
    if (!World) { UE_LOG(LogFPSDemo, Log, TEXT("RUN_VIEW ignored: World unavailable")); return; }
    const bool bPhaseChanged = !bObservedRunPhase || LastObservedPhaseRevision != PhaseRevision || LastObservedPhase != Phase; // 版本覆盖同一枚举的多次往返，枚举补强属性分次到达。
    LastObservedPhaseRevision = PhaseRevision; LastObservedPhase = Phase; bObservedRunPhase = true;
    if (bPhaseChanged && GetNetMode() == NM_Client)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("RUN_VIEW_PHASE revision=%u phase=%d"), PhaseRevision, static_cast<int32>(Phase));
        ADemoProjectileBase::ReconcilePhase(World); // 仅协调本机外观；保留匹配的新阶段实体，不反发CancelActions误取消服务器已开始的新动作。
    }
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It) // 关号/菜单通知即使没有换阶段也必须刷新本World各本地玩家UI。
        if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(It->Get()); PC && PC->IsLocalController() && PC->GetMenuFlow()) PC->GetMenuFlow()->RefreshFromReplicatedState();
}
