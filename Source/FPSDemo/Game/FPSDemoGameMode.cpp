#include "Game/FPSDemoGameMode.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Economy/DemoRewardComponent.h"
#include "Economy/DemoShopComponent.h"
#include "Game/DemoEncounterRules.h" // 固定战役与无尽共用兵种/Boss周期。
// 对应头文件先于依赖，保证UE独立编译能检查本类声明自包含。
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "Tests/DemoAmmoTest.h"
#include "Tests/DemoProjectileTest.h"
#include "Save/DemoRunSave.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Player/DemoPlayerProfile.h"
#include "Engine/GameInstance.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerController.h"
#include "UI/DemoHUD.h"
#include "AI/DemoEnemy.h"
#include "Interaction/DemoInteractable.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoEffects.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Tests/DemoSmokeTest.h"
#include "Tests/DemoSessionTest.h"
#include "Tests/DemoWeaponTest.h"
#include "Tests/DemoWeaponAnimationTest.h" // 显式命令行启动的四枪动画/音效专项，不影响正常关卡流程。
#include "Tests/DemoArmoryTest.h"
#include "Tests/DemoCloudTest.h"
#include "Tests/DemoCampaignTest.h"
#include "AI/DemoEnemyProjectile.h"
#include "Tests/DemoEnemyAttackTest.h"
#include "Misc/CommandLine.h"
#include "EngineUtils.h"

#include "Game/Flow/DemoRunFlowComponent.h"
#include "Progression/DemoChallengeComponent.h"
#include "Interaction/DemoTerminalComponent.h"
#include "World/DemoEncounterAreaSubsystem.h"
AFPSDemoGameMode::AFPSDemoGameMode()
{
	DEMO_LOG_CALL();
	SpawnSystem = CreateDefaultSubobject<UDemoEnemySpawnComponent>(TEXT("SpawnSystem")); // 当前权威World拥有生成调度。
	RewardSystem = CreateDefaultSubobject<UDemoRewardComponent>(TEXT("RewardSystem")); // 通过事件订阅去重后的击杀。
	ShopSystem = CreateDefaultSubobject<UDemoShopComponent>(TEXT("ShopSystem")); // 属性/弹药交易的唯一业务入口。
    TerminalSystem = CreateDefaultSubobject<UDemoTerminalComponent>(TEXT("TerminalSystem")); // 按阶段生成交互物。
    RunFlow = CreateDefaultSubobject<UDemoRunFlowComponent>(TEXT("RunFlow")); // 统一依赖绑定与流程提交。
    ChallengeSystem = CreateDefaultSubobject<UDemoChallengeComponent>(TEXT("ChallengeSystem")); // 与Pawn生命周期分离。
	DefaultPawnClass = ADemoCharacter::StaticClass();
	PlayerControllerClass = ADemoPlayerController::StaticClass();
	PlayerStateClass = ADemoPlayerState::StaticClass();
	GameStateClass = ADemoGameState::StaticClass();
	HUDClass = ADemoHUD::StaticClass();
	// 资产由 import_combat_tables.py 创建，缺失时大厅给出明确错误，不退回硬编码属性。
	EndlessTableAsset = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Endless.DT_Endless"))); // Editor创建资产，Cook由/Game/Data目录规则收集。
	EnemyTableAsset = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Enemies.DT_Enemies")));
	DifficultyTableAsset = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Difficulties.DT_Difficulties")));
	LevelTableAsset = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/DT_Levels.DT_Levels")));
}
void AFPSDemoGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	DEMO_LOG_CALL();
	// 原模板地图指定 BP_GameMode；旧 BP 序列化的类默认值不能覆盖新 GAS 启动链。
	DefaultPawnClass = ADemoCharacter::StaticClass();
	PlayerControllerClass = ADemoPlayerController::StaticClass();
	PlayerStateClass = ADemoPlayerState::StaticClass();
	GameStateClass = ADemoGameState::StaticClass();
	HUDClass = ADemoHUD::StaticClass();
	Super::InitGame(MapName, Options, ErrorMessage);
}
void AFPSDemoGameMode::StartPlay()
{
    DEMO_LOG_CALL();
    UDataTable* Enemies = EnemyTableAsset.LoadSynchronous(); // 同步借用加载结果，Flow初始化后强引用。
    UDataTable* Difficulties = DifficultyTableAsset.LoadSynchronous(); // 四档难度配置。
    UDataTable* Levels = LevelTableAsset.LoadSynchronous(); // 十关模板。
    UDataTable* Endless = EndlessTableAsset.LoadSynchronous(); // 无尽公式配置。
    Super::StartPlay(); // 生命周期完成并不隐式启动刷怪；以下显式安装全部依赖。
    RunFlow->InitializeServices(Enemies, Difficulties, Levels, Endless, SpawnSystem, RewardSystem, ShopSystem, TerminalSystem, ChallengeSystem);
    RunFlow->BeginStartup(OptionsString);
#if !UE_BUILD_SHIPPING
	FString CloudTestMode; // 真实云测试只有显式模式且测试Actor再次验证隔离GUID才可推进。
	if (FParse::Value(FCommandLine::Get(), TEXT("DemoCloudTest="), CloudTestMode)) GetWorld()->SpawnActor<ADemoCloudTest>();
	// 测试入口只有显式命令行参数才启用；普通 PIE/游戏不生成测试 Actor。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoSmokeTest"))) GetWorld()->SpawnActor<ADemoSmokeTest>();
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoSessionTest"))) GetWorld()->SpawnActor<ADemoSessionTest>(); // 显式测试才覆盖暂停/旅行输入。
	// 武器专用测试通过真实InputKey进入映射链路，仅显式参数开启。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponTest"))) GetWorld()->SpawnActor<ADemoWeaponTest>();
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponAnimationTest"))) GetWorld()->SpawnActor<ADemoWeaponAnimationTest>(); // 独立进程验证真实LinkedLayer、双网格与取消清理。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoArmoryTest"))) GetWorld()->SpawnActor<ADemoArmoryTest>(); // 非Shipping显式隔离回归，不影响正常游玩。
	// 三难度/十等级独立回归入口，普通游戏及 Shipping 不自动执行。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoCampaignTest"))) GetWorld()->SpawnActor<ADemoCampaignTest>();
	// 新敌人攻击测试独立进程运行，避免与完整战役测试同时驱动World。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoProjectileTest"))) GetWorld()->SpawnActor<ADemoProjectileTest>(); // 显式隔离的真实飞行/碰撞专项。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoAmmoTest"))) GetWorld()->SpawnActor<ADemoAmmoTest>(); // 显式隔离回归入口。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoEnemyAttackTest"))) GetWorld()->SpawnActor<ADemoEnemyAttackTest>();
#endif
}
void AFPSDemoGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    DEMO_LOG_CALL(); Super::HandleStartingNewPlayer_Implementation(NewPlayer);
    if (RunFlow && NewPlayer) RunFlow->BindAvatar(Cast<ADemoCharacter>(NewPlayer->GetPawn())); // 玩家可先于StartPlay到达；Flow先登记再检查。
}
void AFPSDemoGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{ DEMO_LOG_CALL(); RunFlow->Shutdown(); Super::EndPlay(EndPlayReason); }
bool AFPSDemoGameMode::SelectDifficulty(EDemoDifficulty Difficulty)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->SelectDifficulty(Difficulty);
}
bool AFPSDemoGameMode::StartRun()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->StartRun();
}
bool AFPSDemoGameMode::GetLevelConfig(int32 Number, FDemoLevelRow& OutLevel, FDemoDifficultyRow& OutDifficulty, FString& Error) const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->GetLevelConfig(Number, OutLevel, OutDifficulty, Error);
}
bool AFPSDemoGameMode::GetSpawnStats(int32 Number, bool bBoss, FDemoEnemySpawnStats& OutStats, FString& Error) const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->GetSpawnStats(Number, bBoss, OutStats, Error);
}
void AFPSDemoGameMode::StartNextLevel()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	RunFlow->StartNextLevel();
}
bool AFPSDemoGameMode::ChooseReward(int32 Choice)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->ChooseReward(Choice);
}
void AFPSDemoGameMode::NotifyPlayerDied()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	RunFlow->NotifyPlayerDied();
}
void AFPSDemoGameMode::RestartDemo()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	RunFlow->RestartDemo();
}
bool AFPSDemoGameMode::SaveCheckpoint()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->SaveCheckpoint();
}
bool AFPSDemoGameMode::SaveResetCheckpoint()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->SaveResetCheckpoint();
}
bool AFPSDemoGameMode::RestoreCheckpoint(const UDemoRunSave& Data)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->RestoreCheckpoint(Data);
}
bool AFPSDemoGameMode::ContinueAfterVictory()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->ContinueAfterVictory();
}
bool AFPSDemoGameMode::HasRunUpgrades() const
{
	DEMO_LOG_TICK(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->HasRunUpgrades();
}
bool AFPSDemoGameMode::ReturnToSafeHub(bool bConfirmed)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->ReturnToSafeHub(bConfirmed);
}
void AFPSDemoGameMode::TravelToRun(bool bResume)
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	RunFlow->TravelToRun(bResume);
}
bool AFPSDemoGameMode::SelectEndless()
{
	DEMO_LOG_CALL(); // 兼容入口；业务与状态唯一属于分类服务。
	return RunFlow->SelectEndless();
}
void AFPSDemoGameMode::NotifyEnemyKilled(ADemoEnemy* Enemy)
{
	DEMO_LOG_CALL();
	SpawnSystem->NotifyEnemyDefeated(Enemy); // 保留旧测试/调用兼容；真实敌人已通过OnDefeated直达组件。
}
void AFPSDemoGameMode::NotifyEnemyLost(ADemoEnemy* Enemy)
{
	DEMO_LOG_CALL();
	SpawnSystem->NotifyEnemyLost(Enemy); // 只转发；GameMode不再维护第二份注册表。
}
UDemoEnemySpawnComponent* AFPSDemoGameMode::GetSpawnSystem() const
{ DEMO_LOG_TICK(); return SpawnSystem; }
UDemoRewardComponent* AFPSDemoGameMode::GetRewardSystem() const
{ DEMO_LOG_TICK(); return RewardSystem; }
UDemoShopComponent* AFPSDemoGameMode::GetShopSystem() const
{ DEMO_LOG_TICK(); return ShopSystem; }
int32 AFPSDemoGameMode::GetUpgradeCost(int32 Choice) const
{
	DEMO_LOG_TICK();
	// 兼容既有UI/测试入口；业务和钱包修改由Shop组件唯一处理。
	return ShopSystem->GetUpgradeCost(Choice);
}
bool AFPSDemoGameMode::PurchaseUpgrade(int32 Choice, ADemoCharacter* Purchaser)
{
	DEMO_LOG_CALL();
	// 兼容既有UI/测试入口；业务和钱包修改由Shop组件唯一处理。
	return ShopSystem->PurchaseUpgrade(Choice, Purchaser);
}
FString AFPSDemoGameMode::GetPurchaseBlockReason(ADemoCharacter* Purchaser, int32 Choice) const
{
	DEMO_LOG_TICK();
	// 兼容既有UI/测试入口；业务和钱包修改由Shop组件唯一处理。
	return ShopSystem->GetPurchaseBlockReason(Purchaser, Choice);
}
FString AFPSDemoGameMode::GetTerminalBlockReason(ADemoCharacter* Player) const
{
	DEMO_LOG_TICK();
	// 兼容既有UI/测试入口；业务和钱包修改由Shop组件唯一处理。
	return ShopSystem->GetTerminalBlockReason(Player);
}
FVector AFPSDemoGameMode::GetAreaCenter(int32 Index) const
{ DEMO_LOG_TICK(); return GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>()->GetAreaCenter(Index); }
ADemoInteractable* AFPSDemoGameMode::GetShopTerminal() const { DEMO_LOG_TICK(); return TerminalSystem->GetShopTerminal(); }
ADemoInteractable* AFPSDemoGameMode::GetNextLevelTerminal() const { DEMO_LOG_TICK(); return TerminalSystem->GetNextLevelTerminal(); }
UDemoTerminalComponent* AFPSDemoGameMode::GetTerminalSystem() const { DEMO_LOG_TICK(); return TerminalSystem; }
UDemoRunFlowComponent* AFPSDemoGameMode::GetRunFlow() const { DEMO_LOG_TICK(); return RunFlow; }
UDemoChallengeComponent* AFPSDemoGameMode::GetChallengeSystem() const { DEMO_LOG_TICK(); return ChallengeSystem; }
bool AFPSDemoGameMode::RegisterWeaponShot(FName WeaponId)
{ DEMO_LOG_CALL(); return ChallengeSystem->TryCommitWeaponShot(RunFlow->GetRunId(), WeaponId); }
