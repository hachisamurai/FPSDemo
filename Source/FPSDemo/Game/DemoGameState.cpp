#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Net/UnrealNetwork.h"

void ADemoGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	DEMO_LOG_CALL();
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADemoGameState, Phase);
	// UI 和实例生成共用同一个权威难度，客户端不能自行修改。
	DOREPLIFETIME(ADemoGameState, Difficulty);
	DOREPLIFETIME(ADemoGameState, LevelNumber);
	DOREPLIFETIME(ADemoGameState, EnemiesRemaining);
	DOREPLIFETIME(ADemoGameState, TotalKills);
	DOREPLIFETIME(ADemoGameState, Coins);
	// 双币和各自购买次数跟随权威状态展示；不允许客户端自行交易。
	DOREPLIFETIME(ADemoGameState, SilverCoins);
	DOREPLIFETIME(ADemoGameState, GoldPurchases);
	DOREPLIFETIME(ADemoGameState, UpgradeProgress); // 整个值账本由权威GameMode修改并随UI状态复制。
	DOREPLIFETIME(ADemoGameState, SilverPurchases);
	DOREPLIFETIME(ADemoGameState, Purchases);
	DOREPLIFETIME(ADemoGameState, FailureReason);
	// 终局按钮以权威死亡原因决定目的地，不能靠本地文案推断重开语义。
	DOREPLIFETIME(ADemoGameState, bReturnToHubOnRestart);
}
