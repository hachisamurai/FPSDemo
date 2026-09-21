#include "Tests/DemoCampaignTest.h"
#include "Game/FPSDemoGameMode.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "AI/DemoEnemy.h"
#include "Interaction/DemoInteractable.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include <limits>

namespace
{
	// 专用测试进程跨 OpenLevel 保留难度索引，0/1/2 对应 Easy/Normal/Hard。
	int32 CampaignRun = 0;
}
ADemoCampaignTest::ADemoCampaignTest()
{
	DEMO_LOG_CALL();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	// 与 -DemoCampaignTest 搭配，隔离菜单/进度验收，不把当前仍在改造的攻击行为混入专项通过结论。
	bConfirmationOnly = FParse::Param(FCommandLine::Get(), TEXT("DemoNextLevelConfirmationTest"));
	UE_LOG(LogFPSDemo, Log, TEXT("Campaign validation scope: %s"), bConfirmationOnly ? TEXT("confirmation; no AI damage probe") : TEXT("full campaign including AI damage"));
}
bool ADemoCampaignTest::Check(bool Condition, const TCHAR* Message)
{
	DEMO_LOG_CALL();
	UE_LOG(LogFPSDemo, Display, TEXT("CAMPAIGN %s: %s"), Condition ? TEXT("PASS") : TEXT("FAIL"), Message);
	if (!Condition) { bFailed = true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 1); }
	return Condition;
}
bool ADemoCampaignTest::ValidateBadTables()
{
	DEMO_LOG_CALL();
	// 源表由同步加载缓存，副本由本 Actor 的 Outer 及本同步调用保持有效；不跨 Tick 使用。
	UDataTable* Enemies = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Enemies.DT_Enemies"));
	UDataTable* Difficulties = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Difficulties.DT_Difficulties"));
	UDataTable* Levels = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Levels.DT_Levels"));
	FString Error; // 验证器必须返回可读原因。
	if (!Check(DemoCombatConfig::Validate(Enemies, Difficulties, Levels, Error), TEXT("authored tables validate"))) return false;
	if (!Check(!DemoCombatConfig::Validate(nullptr, Difficulties, Levels, Error) && !Error.IsEmpty(), TEXT("missing table rejected with reason"))) return false;
	UDataTable* RewardCopy = DuplicateObject<UDataTable>(Difficulties, this); // 仅内存副本验证通关奖励边界，不保存策划资产。
	FDemoDifficultyRow* RewardRow = RewardCopy->FindRow<FDemoDifficultyRow>(TEXT("Hard"), TEXT("VictoryRewardValidation")); // 本次同步借用困难行。
	RewardRow->VictoryGoldReward = -1;
	if (!Check(!DemoCombatConfig::Validate(Enemies, RewardCopy, Levels, Error), TEXT("negative or unmigrated victory reward rejected"))) return false;
	RewardRow->VictoryGoldReward = 1000001;
	if (!Check(!DemoCombatConfig::Validate(Enemies, RewardCopy, Levels, Error), TEXT("oversized victory reward rejected"))) return false;
	RewardRow->VictoryGoldReward = 0;
	if (!Check(DemoCombatConfig::Validate(Enemies, RewardCopy, Levels, Error), TEXT("zero victory reward allowed"))) return false;
	UDataTable* Copy = DuplicateObject<UDataTable>(Levels, this); // 只修改内存副本。
	FDemoLevelRow* Row = Copy->FindRow<FDemoLevelRow>(TEXT("Level05"), TEXT("InvalidRowTest")); // 副本行借用。
	const FDemoLevelRow Original = *Row; // 每个用例恢复完整行，避免错误互相掩盖。
	Row->EnemyCoinReward = -1;
	if (!Check(!DemoCombatConfig::Validate(Enemies, Difficulties, Copy, Error), TEXT("negative level coins rejected"))) return false;
	*Row = Original;
	Row->MonsterLevel = 4;
	if (!Check(!DemoCombatConfig::Validate(Enemies, Difficulties, Copy, Error), TEXT("monster level must match stage"))) return false;
	*Row = Original;
	Row->HealthMultiplier = std::numeric_limits<float>::quiet_NaN();
	if (!Check(!DemoCombatConfig::Validate(Enemies, Difficulties, Copy, Error), TEXT("nonfinite growth rejected"))) return false;
	*Row = Original;
	Row->BossRow = TEXT("Drone");
	if (!Check(!DemoCombatConfig::Validate(Enemies, Difficulties, Copy, Error), TEXT("Boss reference type mismatch rejected"))) return false;
	*Row = Original;
	Row->EnemyCoinReward = 0;
	if (!Check(DemoCombatConfig::Validate(Enemies, Difficulties, Copy, Error), TEXT("zero coins are a legal configuration"))) return false;
	Copy->RemoveRow(TEXT("Level10"));
	return Check(!DemoCombatConfig::Validate(Enemies, Difficulties, Copy, Error), TEXT("missing tenth level rejected"));
}
void ADemoCampaignTest::Tick(float DeltaSeconds)
{
	DEMO_LOG_TICK();
	Super::Tick(DeltaSeconds);
	if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
	if (!Check(GetWorld()->GetTimeSeconds() < 90.f, TEXT("campaign world timeout"))) return;
	// 每一步重新借用运行对象，旧 World 卸载后没有存活的裸指针成员。
	AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	ADemoCharacter* Player = PC ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr;
	if (!Check(Mode && State && PC && Player && Player->GetDemoAttributes(), TEXT("campaign runtime objects ready"))) return;
	const UDemoAttributeSet* Attributes = Player->GetDemoAttributes(); // 本帧只读 GAS。
	FDemoLevelRow Level; // 用于本关独立人数/金币预期。
	FDemoDifficultyRow Difficulty; // 本局难度倍率快照。
	FString Error; // 表错误诊断。
	NextTime = GetWorld()->GetTimeSeconds() + 0.3f;
	if (Step == 0)
	{
		if (!Check(State->Phase == EDemoPhase::Lobby && State->Coins == 0 && !Mode->GetShopTerminal(), TEXT("lobby has no enemies, wallet or shop"))) return;
		if (!Check(!Mode->SelectDifficulty(static_cast<EDemoDifficulty>(255)), TEXT("invalid difficulty rejected"))) return;
		if (CampaignRun == 0 && !ValidateBadTables()) return;
        // 战役数值专项直接建立隔离新局；存档/设置UI由Session测试覆盖。
        if (!Check(Mode->StartRun(), TEXT("fresh fixture starts safe hub"))) return;
        PC->OnRunReady();
        if (!Check(Mode->SelectDifficulty(static_cast<EDemoDifficulty>(CampaignRun)),TEXT("difficulty selects in initial hub"))) return;
        Mode->StartNextLevel();
        if (!Check(!Mode->SelectDifficulty(EDemoDifficulty::Hard) && static_cast<int32>(State->Difficulty) == CampaignRun,TEXT("difficulty locks after departure"))) return;
		Step = 1;
		return;
	}
	if (!Check(Mode->GetLevelConfig(State->LevelNumber, Level, Difficulty, Error), TEXT("current level configuration available"))) return;
	if (Step == 1)
	{
		if (!Check(!Mode->GetShopTerminal() && !Mode->GetNextLevelTerminal() && !TActorIterator<ADemoInteractable>(GetWorld()), TEXT("combat has no stale terminals"))) return;
		// 直接读模板计算独立预期，避免只拿 Resolve 自己的结果测试自己。
		const UDataTable* Templates = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Enemies.DT_Enemies"));
		int32 Count = 0; // 本次活怪数量，不计延迟销毁的上一关尸体。
		ADemoEnemy* AttackProbe = nullptr; // 本步骤借用，选首关小怪和 5/10 Boss 做真实伤害。
		for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 当前 World 活怪遍历。
		{
			if (!It->IsAlive()) continue;
			++Count;
			It->SetActorTickEnabled(false);
			const FDemoEnemyRow* Template = Templates->FindRow<FDemoEnemyRow>(It->IsBoss() ? Level.BossRow : Level.EnemyRow, TEXT("IndependentExpectedStats")); // 借用模板行。
			if (!Check(Template && It->GetMonsterLevel() == State->LevelNumber
				&& FMath::IsNearlyEqual(It->GetMaxHealth(), FMath::Max(1.f, Template->BaseHealth * Level.HealthMultiplier * Difficulty.HealthMultiplier), .001f)
				&& FMath::IsNearlyEqual(It->GetAttackPower(), Template->BaseAttackPower * Level.AttackMultiplier * Difficulty.AttackMultiplier, .001f)
				&& It->GetCoinReward() == (It->IsBoss() ? Level.BossCoinReward : Level.EnemyCoinReward), TEXT("spawned GAS attributes, level and coins match data"))) return;
			// 专项仍逐个校验真实 GAS 属性/等级/金币，但不依赖近战/Boss 的旧攻击时序。
			if (!bConfirmationOnly && ((State->LevelNumber == 1 && !AttackProbe) || It->IsBoss())) AttackProbe = *It;
		}
		if (!Check(Count == Level.EnemyCount + (Level.BossRow.IsNone() ? 0 : 1) && State->EnemiesRemaining == Count, TEXT("spawn count includes configured Boss"))) return;
		ExpectedKills += Count;
		ExpectedCoins += State->LevelNumber == DemoCombatConfig::LevelCount ? Difficulty.VictoryGoldReward : 0; // 仅最终胜利按难度发放金币，中途清场为0。
		ExpectedSilver += Level.EnemyCount * Level.EnemyCoinReward + (Level.BossRow.IsNone() ? 0 : Level.BossCoinReward); // 怪物逐只发银币。
		if (AttackProbe)
		{
			Player->GetCharacterMovement()->DisableMovement();
			AttackProbe->SetActorLocation(Player->GetActorLocation() + FVector(AttackProbe->IsBoss() ? 500.f : 160.f, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
			HealthBeforeAttack = Attributes->GetHealth();
			ExpectedDamage = AttackProbe->GetAttackPower();
			AttackProbe->SetActorTickEnabled(true);
			NextTime = GetWorld()->GetTimeSeconds() + (AttackProbe->IsBoss() ? 2.9f : 2.f);
			Step = 2;
		}
		else Step = 3;
		return;
	}
	if (Step == 2)
	{
		if (!Check(FMath::IsNearlyEqual(Attributes->GetHealth(), HealthBeforeAttack - ExpectedDamage, .01f), TEXT("real melee or Boss GE damage uses level then difficulty"))) return;
		Step = 3;
	}
	if (Step == 3)
	{
		// 冻结测试 Pawn 的重力以便精确断言原地清关；不改变生产清场流程。
		Player->GetCharacterMovement()->DisableMovement();
		// 第 2/3 关刻意站在两个默认终端出生点；生成需微调终端，不能把玩家传走或卡死。
		if (State->LevelNumber == 2 || State->LevelNumber == 3)
			Player->SetActorLocation(Mode->GetAreaCenter(Level.ArenaIndex) + FVector(0, State->LevelNumber == 2 ? -300.f : 300.f, 100.f));
		PositionBeforeClear = Player->GetActorLocation();
		RotationBeforeClear = PC->GetControlRotation();
		for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 实际 GE 死亡，注册集合下一帧推进。
		{
			if (!It->IsAlive()) continue;
			DemoEffects::Apply(Player->GetDemoASC(), It->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -It->GetMaxHealth());
			const int32 AfterDeath = State->SilverCoins; // 去重基准，不能再发一次等级奖励。
			Mode->NotifyEnemyKilled(*It);
			if (!Check(State->SilverCoins == AfterDeath, TEXT("duplicate death grants no additional level coins"))) return;
		}
		Step = 4;
		return;
	}
	if (Step == 4)
	{
		if (!Check(State->Coins == ExpectedCoins && State->SilverCoins == (State->LevelNumber == 10 ? 0 : ExpectedSilver) && State->TotalKills == ExpectedKills && State->EnemiesRemaining == 0, TEXT("level payout exact and no stale enemies"))) return;
		if (State->LevelNumber < DemoCombatConfig::LevelCount)
		{
			if (!Check(State->Phase == EDemoPhase::Reward, TEXT("first nine levels offer reward"))) return;
			if (!Check(Player->GetActorLocation().Equals(PositionBeforeClear, 1.f) && PC->GetControlRotation().Equals(RotationBeforeClear, .01f), TEXT("clear preserves player location and facing"))) return;
			// 中心放置范围用 600cm 上界容纳碰撞微调；活跃终端必须恰好一对。
			ADemoInteractable* Shop = Mode->GetShopTerminal(); // World 持有，仅本步骤交互。
			ADemoInteractable* Portal = Mode->GetNextLevelTerminal(); // 生命周期直到下一次 StartNextLevel。
			int32 Terminals = 0; // 当前 World 活终端数量，不计 Destroy 的对象。
			for (TActorIterator<ADemoInteractable> It(GetWorld()); It; ++It) ++Terminals;
			if (!Check(Shop && Portal && Terminals == 2 && FVector::Dist2D(Shop->GetActorLocation(), Mode->GetAreaCenter(Level.ArenaIndex)) <= 600.f
				&& FVector::Dist2D(Portal->GetActorLocation(), Mode->GetAreaCenter(Level.ArenaIndex)) <= 600.f, TEXT("cleared arena center has exactly two terminals"))) return;
			Player->SetActorLocation(Portal->GetActorLocation() + FVector(-180,0,20));
			Portal->Interact(Player);
			if (!Check(State->Phase == EDemoPhase::Reward, TEXT("next terminal cannot skip pending reward"))) return;
			Player->SetActorLocation(PositionBeforeClear);
			PC->SelectUpgrade(0);
			if (!Check(State->Phase == EDemoPhase::Intermission && Player->GetActorLocation().Equals(PositionBeforeClear, 1.f) && !Mode->ChooseReward(0), TEXT("reward leaves player in arena and cannot repeat"))) return;
			Player->SetActorLocation(Shop->GetActorLocation() + FVector(-180,0,20));
			Shop->Interact(Player);
			if (!Check(PC->IsUpgradeMenuOpen() && Mode->GetPurchaseBlockReason(Player).IsEmpty(), TEXT("arena shop usable after clearing"))) return;
			Mode->StartNextLevel(); // 商店打开时任何入口都不能直接推进。
			if (!Check(State->Phase == EDemoPhase::Intermission, TEXT("open shop blocks next level"))) return;
			PC->CloseUpgradeMenu();
			Player->SetActorLocation(Mode->GetAreaCenter(Level.ArenaIndex) + FVector(-1100,0,100));
			Portal->Interact(Player);
			if (!Check(State->Phase == EDemoPhase::Intermission, TEXT("next terminal rejects distant interaction"))) return;
			Player->SetActorLocation(Portal->GetActorLocation() + FVector(-180,0,20));
			const int32 CompletedLevel = State->LevelNumber; // 打开确认前的进度，取消/重复点击均不能提前改变。
			const FVector ConfirmationPosition = Player->GetActorLocation(); // 取消后应原地恢复控制，单位 cm。
			Portal->Interact(Player); // 真实终端只打开本地确认，不能立即销毁物体或推进。
			Mode->StartNextLevel(); // 旧入口在确认框显示时也必须拒绝。
			PC->OpenUpgradeMenu(false); // 确认框不能被另一个菜单覆盖。
			Portal->Interact(Player); // 重复交互不得消费请求。
			if (!Check(PC->IsNextLevelConfirmationOpen() && !PC->IsUpgradeMenuOpen() && State->Phase == EDemoPhase::Intermission
				&& State->LevelNumber == CompletedLevel && PC->bShowMouseCursor && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored(), TEXT("next terminal waits for explicit confirmation and locks modal input"))) return;
			PC->CancelNextLevelConfirmation();
			PC->ConfirmNextLevel(); // 残留热区点击不等于新的确认。
			if (!Check(!PC->IsNextLevelConfirmationOpen() && !PC->bShowMouseCursor && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored()
				&& State->LevelNumber == CompletedLevel && State->Coins == ExpectedCoins && IsValid(Portal)
				&& Player->GetActorLocation().Equals(ConfirmationPosition), TEXT("No keeps location, coins, progress and terminals and restores input"))) return;
			Portal->Interact(Player);
			Player->SetActorLocation(Portal->GetActorLocation() + FVector(-1000,0,20)); // 模拟外部传送，确认时必须重新验证距离。
			PC->ConfirmNextLevel();
			if (!Check(!PC->IsNextLevelConfirmationOpen() && State->LevelNumber == CompletedLevel && State->Phase == EDemoPhase::Intermission, TEXT("confirmation rechecks range and discards expired request"))) return;
			Player->SetActorLocation(ConfirmationPosition);
			Portal->Interact(Player);
			PC->ConfirmNextLevel();
			PC->ConfirmNextLevel(); // 请求已消费，连点“是”只能前进一关。
			if (!Check(State->Phase == EDemoPhase::Combat && State->LevelNumber == CompletedLevel + 1 && !PC->IsNextLevelConfirmationOpen()
				&& !PC->IsMoveInputIgnored() && !IsValid(Shop) && !IsValid(Portal), TEXT("Yes starts exactly one stage and destroys old pair"))) return;
			Step = 1;
		}
		else
		{
			if (!Check(State->Phase == EDemoPhase::Victory && !Mode->GetShopTerminal() && !Mode->GetNextLevelTerminal(), TEXT("tenth clear is victory without an eleventh-level terminal"))) return;
			if (++CampaignRun < 3)
			{
				Mode->RestartDemo();
				if (!Check(State->Phase==EDemoPhase::Hub && State->LevelNumber==0 && State->Coins==ExpectedCoins,TEXT("victory returns playable hub preserving campaign earnings"))) return;
				Mode->TravelToRun(false); // 各难度数值探针使用独立零成长夹具；生产续玩保留数据另由Session专项验证。
			}
			else
			{
				// 显式块避免 UE_LOG 宏展开后干扰 if/else 配对；两种范围保持不同成功标记。
				if (bConfirmationOnly)
				{
					UE_LOG(LogFPSDemo, Display, TEXT("DEMO_CONFIRMATION_SUCCESS: 30 stages, Yes/No, input locks, range revalidation, duplicate requests and terminal cleanup; AI damage not tested"));
				}
				else
				{
					UE_LOG(LogFPSDemo, Display, TEXT("DEMO_CAMPAIGN_SUCCESS: 30 stages, 3 difficulties, real damage/coins, in-place rewards, arena terminal interaction/cleanup and lobby restart"));
				}
				FPlatformMisc::RequestExitWithStatus(false, 0);
			}
			SetActorTickEnabled(false);
		}
	}
}
