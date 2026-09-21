#include "Economy/DemoRewardComponent.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Game/DemoGameState.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerProfile.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"

UDemoRewardComponent::UDemoRewardComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
void UDemoRewardComponent::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    Spawner = GetOwner()->FindComponentByClass<UDemoEnemySpawnComponent>();
    if (Spawner.IsValid()) Spawner->OnEnemyDefeated.AddUObject(this, &UDemoRewardComponent::HandleEnemyDefeated);
    else UE_LOG(LogFPSDemo, Error, TEXT("REWARD_BIND missing spawn component"));
}
void UDemoRewardComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); if (Spawner.IsValid()) Spawner->OnEnemyDefeated.RemoveAll(this); Super::EndPlay(EndPlayReason);
}
void UDemoRewardComponent::HandleEnemyDefeated(ADemoEnemy* Enemy)
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前权威钱包；库存/存档不保存第二份余额。
    if (!GetOwner()->HasAuthority() || !State || State->Phase != EDemoPhase::Combat || !Enemy || Enemy->IsAlive() || Enemy->GetCoinReward() < 0)
    { UE_LOG(LogFPSDemo, Warning, TEXT("REWARD_KILL rejected authority/phase/enemy")); return; }
    State->SilverCoins = static_cast<int32>(FMath::Min<int64>(100000000, int64(State->SilverCoins) + Enemy->GetCoinReward())); // 先扩宽再饱和，与存档范围一致。
    State->TotalKills = FMath::Min(10000000, State->TotalKills + 1);
    UE_LOG(LogFPSDemo, Log, TEXT("KILL Lv%d silverReward=%d silver=%d gold=%d remaining=%d"), Enemy->GetMonsterLevel(), Enemy->GetCoinReward(), State->SilverCoins, State->Coins, State->EnemiesRemaining);
}
bool UDemoRewardComponent::GrantVictory(const FString& RunId, const FDemoDifficultyRow& Difficulty)
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前关卡状态借用，仅普通十关完整胜利符合条件。
    if (!GetOwner()->HasAuthority() || !State || State->Phase != EDemoPhase::Combat || State->bEndless || State->LevelNumber != DemoCombatConfig::LevelCount
        || !Spawner.IsValid() || !Spawner->IsEncounterComplete() || RunId.IsEmpty() || LastVictoryRun == RunId || Difficulty.VictoryGoldReward < 0 || Difficulty.VictoryGoldReward > 1000000)
    { UE_LOG(LogFPSDemo, Warning, TEXT("REWARD_VICTORY rejected phase/clear/duplicate/config")); return false; }
    LastVictoryRun = RunId; // 先消费本轮结算身份，后续广播或重复调用不能再次加币。
    State->Coins = static_cast<int32>(FMath::Min<int64>(100000000, int64(State->Coins) + Difficulty.VictoryGoldReward));
    // 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
    UE_LOG(LogFPSDemo, Display, TEXT("VICTORY_GOLD difficulty=%d reward=%d gold=%d run=%s"), static_cast<int32>(State->Difficulty), Difficulty.VictoryGoldReward, State->Coins, *RunId);
    return true;
}
void UDemoRewardComponent::RecordVictoryUnlocks(const FString& RunId)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 必须先由玩法协调器进入Victory，不能降低Profile原有阶段约束。
    if (!GetOwner()->HasAuthority() || !State || State->Phase != EDemoPhase::Victory || LastVictoryRun != RunId)
    { UE_LOG(LogFPSDemo, Warning, TEXT("REWARD_UNLOCK rejected phase/unsettled run")); return; }
    UDemoPlayerProfile* Profile = GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // GI负责永久武器/地狱/无尽解锁与幂等事实。
    if (!Profile || !Profile->RecordVictory(RunId, State->Difficulty)) UE_LOG(LogFPSDemo, Warning, TEXT("REWARD_UNLOCK pending; victory continuation retries profile save"));
}
bool UDemoRewardComponent::GrantAbilityChoice(const FString& RunId, int32 Choice)
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // Reward阶段是持久化领取资格，读入Intermission不能再领。
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)); // 能力仍交给角色/GAS现有应用接口。
    if (!GetOwner()->HasAuthority() || !State || !Player || State->Phase != EDemoPhase::Reward || Choice < 0 || Choice > 2 || RunId.IsEmpty()
        || (LastChoiceRun == RunId && LastChoiceLevel == State->LevelNumber))
    { UE_LOG(LogFPSDemo, Warning, TEXT("REWARD_CHOICE rejected authority/phase/choice/duplicate")); return false; }
    LastChoiceRun = RunId; LastChoiceLevel = State->LevelNumber;
    Player->ApplyAbilityReward(Choice);
    UE_LOG(LogFPSDemo, Log, TEXT("REWARD_CHOICE run=%s level=%d choice=%d"), *RunId, State->LevelNumber, Choice);
    return true;
}
