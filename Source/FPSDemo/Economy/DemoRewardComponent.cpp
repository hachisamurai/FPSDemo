// 职责：战斗奖励账本和协作击杀通知。使用：刷怪服务去重后调用，先提交每人的经济，再发GAS TeamKill，不包含天赋ID分支。
#include "Economy/DemoRewardComponent.h"
#include "AbilitySystemComponent.h"
#include "GAS/Talents/DemoTalentEvents.h"
// 对应头文件先包含，确保UBT独立编译检查本类头文件自包含；功能逻辑不变。
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/DemoPlayerState.h"
#include "Progression/Summary/DemoRunSummaryComponent.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerController.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"
UDemoRewardComponent::UDemoRewardComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
void UDemoRewardComponent::InitializeSpawner(UDemoEnemySpawnComponent* SpawnerService)
{
    DEMO_LOG_CALL();
    if (!SpawnerService || SpawnerService->GetOwner() != GetOwner()) { UE_LOG(LogFPSDemo, Error, TEXT("REWARD_BIND missing/foreign spawn service")); return; }
    if (Spawner.IsValid()) Spawner->OnEnemyDefeated.RemoveAll(this);
    Spawner = SpawnerService;
    Spawner->OnEnemyDefeated.AddUObject(this, &UDemoRewardComponent::HandleEnemyDefeated); // 同World游戏线程UObject委托，EndPlay精确解绑。
}
void UDemoRewardComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{ DEMO_LOG_CALL(); if (Spawner.IsValid()) Spawner->OnEnemyDefeated.RemoveAll(this); Super::EndPlay(EndPlayReason); }
void UDemoRewardComponent::HandleEnemyDefeated(ADemoEnemy* Enemy)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 公共阶段不再拥有个人钱包。
    const AFPSDemoGameMode* Mode = Cast<AFPSDemoGameMode>(GetOwner()); // 同Owner组合根，仅服务器存在。
    if (!Mode || !GetOwner()->HasAuthority() || !State || State->Phase != EDemoPhase::Combat || !Enemy || Enemy->IsAlive() || Enemy->GetCoinReward() < 0)
    { UE_LOG(LogFPSDemo, Warning, TEXT("REWARD_KILL rejected authority/phase/enemy")); return; }
    for (ADemoCharacter* Player : Mode->GetCoopRoster()->GetPlayers()) // 已阵亡但仍参战的观战者同样获得完整银币。
    {
        UDemoPlayerProgressComponent* Progress = Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 此人独立账本。
        FDemoPlayerProgressSnapshot Next = Progress->GetSnapshot(); // 先扩宽再钳制，不能溢出存档范围。
        Next.SilverCoins = static_cast<int32>(FMath::Min<int64>(100000000, int64(Next.SilverCoins) + Enemy->GetCoinReward()));
        Next.Kills = FMath::Min(10000000, Next.Kills + 1); Progress->Commit(Next);
        FGameplayEventData Event; // 奖励账本已提交，响应由独立GA决定，不在经济模块枚举技能。
        Event.EventTag=DemoTalentEvents::TeamKill; Event.Target=Enemy; Event.Instigator=Player;
        if(!Player->IsCoopEliminated()) Player->GetAbilitySystemComponent()->HandleGameplayEvent(Event.EventTag,&Event);
    }
    UE_LOG(LogFPSDemo, Log, TEXT("KILL level=%d rewardPerPlayer=%d"), Enemy->GetMonsterLevel(), Enemy->GetCoinReward());
}
bool UDemoRewardComponent::GrantVictory(const FString& RunId, const FDemoDifficultyRow& Difficulty)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 整轮十关结算，一次Run只提交一次。
    const AFPSDemoGameMode* Mode = Cast<AFPSDemoGameMode>(GetOwner()); // 房主权威目录。
    if (!Mode || !GetOwner()->HasAuthority() || !State || State->Phase != EDemoPhase::Combat || State->bEndless || State->LevelNumber != DemoCombatConfig::LevelCount
        || !Mode->GetRunFlow() || !Mode->GetRunFlow()->HasResolvedEncounter(RunId,State->LevelNumber) || RunId.IsEmpty() || LastVictoryRun == RunId || Difficulty.VictoryGoldReward < 0 || Difficulty.VictoryGoldReward > 1000000)
    { UE_LOG(LogFPSDemo, Warning, TEXT("REWARD_VICTORY rejected phase/clear/duplicate/config")); return false; }
    LastVictoryRun = RunId; // 先防止同步回调重复提交。
    for (ADemoCharacter* Player : Mode->GetCoopRoster()->GetPlayers()) // 每人完整通关金币，购买互不影响。
    {
        UDemoPlayerProgressComponent* Progress = Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 本人 PS 持有永久部分。
        const int32 PreviousGold = Progress->GetSnapshot().Coins; // 发奖前冻结余额，摘要只记录实际Commit后的增量，不能直接抄配置奖励。
        FDemoPlayerProgressSnapshot Next = Progress->GetSnapshot(); // 原成长保留至统一结束清理。
        Next.Coins = static_cast<int32>(FMath::Min<int64>(MAX_int32, int64(Next.Coins) + Difficulty.VictoryGoldReward)); Progress->Commit(Next);
        Player->GetPlayerState<ADemoPlayerState>()->GetRunSummaryComponent()->RecordVictoryCredit(RunId, FMath::Max(0, Progress->GetSnapshot().Coins - PreviousGold));
    }
    UE_LOG(LogFPSDemo, Display, TEXT("VICTORY_GOLD difficulty=%d rewardPerPlayer=%d run=%s"), int32(State->Difficulty), Difficulty.VictoryGoldReward, *RunId); return true;
}
void UDemoRewardComponent::RecordVictoryUnlocks(const FString& RunId)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 冻结完成事实后再通知各本地Profile，不查询房主GI替队友解锁。
    const AFPSDemoGameMode* Mode = Cast<AFPSDemoGameMode>(GetOwner()); // 权威服务器组合根。
    if (!Mode || !State || State->Phase != EDemoPhase::Victory || LastVictoryRun != RunId) { UE_LOG(LogFPSDemo, Warning, TEXT("REWARD_UNLOCK rejected")); return; }
    for (ADemoCharacter* Player : Mode->GetCoopRoster()->GetPlayers())
    {
        UDemoPlayerProgressComponent* Progress = Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 先更新房主保存的个人权限。
        FDemoPlayerProgressSnapshot Next = Progress->GetSnapshot(); // 不清手枪资格，完成事实需要原值。
        Next.UnlockedWeaponIds=Next.Ownership.EffectiveIds(); // 新通关只保事实/模式/金币，枪权来自冻结Legacy和本槽已提交购买。
        if (State->Difficulty == EDemoDifficulty::Hard && Next.PistolChallenge == 1) Next.bHellUnlocked = true;
        if (State->Difficulty == EDemoDifficulty::Hell) Next.bEndlessUnlocked = true;
        Progress->Commit(Next);
        FDemoRunCompletion Completion; // 仅服务器从完成状态构建，Client RPC 不接受客户端上传的通关声明。
        Completion.RunId = RunId; Completion.Difficulty = State->Difficulty; Completion.CompletedLevel = State->LevelNumber;
        Completion.PistolChallenge = Next.PistolChallenge; Completion.bEndless = State->bEndless;
        if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(Player->GetController())) PC->ClientRunCompletion(Completion);
    }
}
bool UDemoRewardComponent::GrantAbilityChoice(const FString& RunId, int32 Choice)
{ DEMO_LOG_CALL(); return GrantAbilityChoice(RunId, Choice, Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0))); }
bool UDemoRewardComponent::GrantAbilityChoice(const FString& RunId, int32 Choice, ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 全队Reward阶段，但个人领取标记分别保存。
    ADemoPlayerState* PS = Player ? Player->GetPlayerState<ADemoPlayerState>() : nullptr; // RPC拥有者本人。
    if (!GetOwner()->HasAuthority() || !State || !PS || !Player->GetDemoAttributes() || State->Phase != EDemoPhase::Reward || Choice < 0 || Choice > 2 || RunId.IsEmpty()
        || PS->GetProgressComponent()->GetSnapshot().LastRewardLevel >= State->LevelNumber)
    { UE_LOG(LogFPSDemo, Log, TEXT("REWARD_CHOICE rejected player/phase/duplicate")); return false; }
    FDemoPlayerProgressSnapshot Next = PS->GetProgressComponent()->GetSnapshot(); // 先消费本人资格，阻止同步重入。
    Next.LastRewardLevel = State->LevelNumber;
    const FDemoPlayerProgressSnapshot Before = PS->GetProgressComponent()->GetSnapshot(); // 配置/效果失败时不消耗本关个人奖励资格。
    if (!PS->GetProgressComponent()->Commit(Next)) return false;
    if (!Player->ApplyAbilityReward(Choice)) { PS->GetProgressComponent()->Commit(Before); return false; }
    UE_LOG(LogFPSDemo, Log, TEXT("REWARD_CHOICE level=%d choice=%d player=%s"), State->LevelNumber, Choice, *GetNameSafe(Player)); return true;
}
