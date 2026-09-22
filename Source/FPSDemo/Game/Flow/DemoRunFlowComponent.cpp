#include "Game/Flow/DemoRunFlowComponent.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Progression/DemoChallengeComponent.h"
#include "Save/DemoCheckpointAssembler.h"
#include "Engine/World.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyProjectile.h"
#include "Characters/DemoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Debug/DemoLog.h"
#include "Economy/DemoRewardComponent.h"
#include "Economy/DemoShopComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "Game/DemoEncounterRules.h" // 固定战役与无尽共用兵种/Boss周期。
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/DemoInteractable.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerState.h"
#include "Save/DemoRunSave.h"
#include "Spawning/DemoEnemySpawnComponent.h" // 只读剩余数量/取消旅行，不再访问GameMode私有生成状态。
#include "TimerManager.h"
#include "UI/DemoHUD.h"
#include "Weapons/DemoWeaponComponent.h"
bool UDemoRunFlowComponent::SelectDifficulty(EDemoDifficulty Difficulty)
{
	DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW command rejected during shutdown")); return false; }
	// State 是本局权威状态；枚举必须验证以拒绝非法脚本或旧 UI 请求。
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	if (!State || State->Phase != EDemoPhase::Hub || State->LevelNumber != 0 || DemoCombatConfig::DifficultyName(Difficulty).IsNone())
	{
		UE_LOG(LogFPSDemo, Warning, TEXT("Difficulty change rejected outside initial hub or invalid enum"));
		return false;
	}
	if (Difficulty == EDemoDifficulty::Hell && !GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()->IsHellUnlocked())
	{ UE_LOG(LogFPSDemo, Warning, TEXT("Hell locked: complete hard using pistol only")); return false; }
	State->bEndless = false; // 普通难度选择明确退出无尽模式，保留最高纪录。
	State->Difficulty = Difficulty;
	UE_LOG(LogFPSDemo, Log, TEXT("DIFFICULTY %s"), *DemoCombatConfig::DifficultyName(Difficulty).ToString());
	return true;
}
bool UDemoRunFlowComponent::StartRun()
{
	DEMO_LOG_CALL();
	// 当前单人 Pawn/State 仅调用期借用，所有配置在生成交互物前完整校验。
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!bInitialized || bStopping || bRestartRequested || !IsPlayerReady() || !State || !Player || State->Phase != EDemoPhase::Lobby) { UE_LOG(LogFPSDemo, Warning, TEXT("StartRun rejected: invalid phase/player")); return false; }
	FString Error; // 展示和日志共用校验错误，留在大厅可修复配置后重试。
	if (!DemoCombatConfig::Validate(EnemyTable, DifficultyTable, LevelTable, Error))
	{
		State->FailureReason = Error;
		UE_LOG(LogFPSDemo, Error, TEXT("Combat config rejected: %s"), *Error);
		return false;
	}
	if (Areas.IsValid() && !Areas->IsReady()) Areas->PrepareAreas();
	if ((!Areas.IsValid() || !Areas->IsReady()))
	{
		State->FailureReason = TEXT("Arena initialization failed; check map and log");
		UE_LOG(LogFPSDemo, Error, TEXT("StartRun rejected: %s"), *State->FailureReason);
		return false;
	}
	// 每局首次进入安全区时创建终端；清关后的终端由 FinishLevel 在当前竞技场创建。
	if (!SpawnTerminals(-1))
	{
		State->FailureReason = TEXT("Safe hub terminals could not be created; check collision and log");
		return false;
	}
	State->FailureReason.Empty();
	SetPhase(EDemoPhase::Hub);
	CampaignRunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); // 本次完整战役的唯一通关凭据标识。
	ChallengeSystem->BeginRun(CampaignRunId);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	PlacePlayerInHub(Player);
	return true;
}
bool UDemoRunFlowComponent::GetLevelConfig(int32 Number, FDemoLevelRow& OutLevel, FDemoDifficultyRow& OutDifficulty, FString& Error) const
{
	DEMO_LOG_CALL();
	if (!DemoCombatConfig::Validate(EnemyTable, DifficultyTable, LevelTable, Error)) return false;
	// 本次查询借用表行，返回值复制；表热改不能改变已经生成的敌人。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	const FDemoLevelRow* Level = LevelTable->FindRow<FDemoLevelRow>(DemoCombatConfig::LevelName(State && State->bEndless ? 1 : Number), TEXT("SpawnLevel"), false);
	const FDemoDifficultyRow* Difficulty = State ? DifficultyTable->FindRow<FDemoDifficultyRow>(DemoCombatConfig::DifficultyName(State->Difficulty), TEXT("SpawnDifficulty"), false) : nullptr;
	if (!Level || !Difficulty) { Error = TEXT("Missing requested level or difficulty row"); UE_LOG(LogFPSDemo, Error, TEXT("%s"), *Error); return false; }
	OutLevel = *Level;
	OutDifficulty = *Difficulty;
	if (State->bEndless)
	{
		FDemoEndlessRow Growth; FString Reason; // 表值副本与验证错误，只在本次公式解析借用。
		if (Number < 1 || Number >= MAX_int32 || !GetEndlessConfig(Growth, Reason)) { Error = Reason; return false; }
		// 在log域限制指数，避免高关数pow溢出；达到数值保护上限仍可继续关卡，不触发胜利。
		OutLevel.MonsterLevel = Number; OutLevel.ArenaIndex = (Number-1)%3;
		OutLevel.HealthMultiplier = FMath::Min(1.e12f, Level->HealthMultiplier * FMath::Exp(FMath::Min(27.6f,(Number-1)*FMath::Loge(Growth.HealthGrowth))));
		OutLevel.AttackMultiplier = FMath::Min(1.e12f, Level->AttackMultiplier * FMath::Exp(FMath::Min(27.6f,(Number-1)*FMath::Loge(Growth.DamageGrowth))));
		OutLevel.EnemyCount = FMath::Min(1000000, FMath::RoundToInt(Level->EnemyCount * FMath::Exp(FMath::Min(12.f,(Number-1)*FMath::Loge(Growth.CountGrowth)))));
		if (Number % DemoEncounterRules::RangedInterval == 0) OutLevel.EnemyCount = FMath::Max(3, OutLevel.EnemyCount); // 极低数量基数也必须给三兵种各留一个名额。
		OutLevel.EnemyCoinReward = FMath::Min(1000000,FMath::RoundToInt(Level->EnemyCoinReward * FMath::Exp(FMath::Min(12.f,(Number-1)*FMath::Loge(Growth.SilverGrowth)))));
		const FDemoLevelRow* BossLevel = LevelTable->FindRow<FDemoLevelRow>(DemoCombatConfig::LevelName(10),TEXT("EndlessBoss")); // 第五关改为三兵种小怪，Boss模板/银币基数改用第十关。
		OutLevel.BossRow = Number % DemoEncounterRules::BossInterval == 0 ? BossLevel->BossRow : NAME_None;
		OutLevel.BossCoinReward = FMath::Min(1000000,FMath::RoundToInt(BossLevel->BossCoinReward * FMath::Exp(FMath::Min(12.f,(Number-1)*FMath::Loge(Growth.SilverGrowth)))));
	}
	return true;
}
bool UDemoRunFlowComponent::GetSpawnStats(int32 Number, bool bBoss, FDemoEnemySpawnStats& OutStats, FString& Error) const
{
	DEMO_LOG_CALL();
	// 行快照仅属于这次调用；完整验证避免生成一半才发现 Boss 引用无效。
	FDemoLevelRow Level; // 本关成长、怪物模板引用及金币快照。
	FDemoDifficultyRow Difficulty; // 当前难度的血量和攻击倍率快照。
	if (!GetLevelConfig(Number, Level, Difficulty, Error)) return false;
	const FDemoEnemyRow* Enemy = EnemyTable->FindRow<FDemoEnemyRow>(bBoss ? Level.BossRow : Level.EnemyRow, TEXT("SpawnEnemy"), false); // 借用模板。
	if (!Enemy) { Error = TEXT("Requested enemy template missing"); UE_LOG(LogFPSDemo, Error, TEXT("%s"), *Error); return false; }
	OutStats = DemoCombatConfig::Resolve(*Enemy, Difficulty, Level);
	return true;
}
void UDemoRunFlowComponent::PlacePlayerInHub(ADemoCharacter* Player, bool bRefill)
{
	DEMO_LOG_CALL();
	if (!Player) { UE_LOG(LogFPSDemo, Warning, TEXT("PlacePlayerInHub rejected: missing Avatar")); return; }
	FDemoAreaSnapshot Area; FString Error; // 本次安全区完整快照，错误不得传送到默认错误位置。
    if (!Areas.IsValid() || !Areas->ResolveArea(-1, Area, Error)) { UE_LOG(LogFPSDemo, Warning, TEXT("HUB_PLACEMENT failed %s"), *Error); return; }
    Player->StopFiring();
	Player->GetCharacterMovement()->StopMovementImmediately();
	Player->SetActorLocation(Area.PreparedStart, false, nullptr, ETeleportType::TeleportPhysics);
	if (Player->GetController()) Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
	// 新建/死亡重开沿用补给；通关续玩不覆盖保存下来的当前生命和独立弹药。
	if (bRefill) RefillPlayer(Player);
}
void UDemoRunFlowComponent::RefillPlayer(ADemoCharacter* Player)
{
	DEMO_LOG_CALL();
	if (!Player) { UE_LOG(LogFPSDemo, Warning, TEXT("Refill rejected: missing player")); return; }
	// 将补给与传送拆开，沿用清关恢复规则，玩家无需回安全区才能获得补给。
	if (const UDemoAttributeSet* Attributes = Player->GetDemoAttributes())
	{
		DemoEffects::Apply(Player->GetDemoASC(), Player->GetDemoASC(), UDemoHealthEffect::StaticClass(), Attributes->GetMaxHealth());
		// 弹药唯一真值在武器实例，清关同时补给主武器库与副武器。
		Player->GetWeaponComponent()->RefillAll();
	}
}
void UDemoRunFlowComponent::SetPhase(EDemoPhase NewPhase)
{
	DEMO_LOG_CALL();
	if (ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>())
	{
		// 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
		UE_LOG(LogFPSDemo, Display, TEXT("PHASE %d -> %d level=%d coins=%d"), static_cast<int32>(State->Phase), static_cast<int32>(NewPhase), State->LevelNumber, State->Coins);
		State->Phase = NewPhase;
		// 在阶段切换的同一调用栈撤销攻击，不等待下一帧；终局、奖励与安全区不会遗留减速或弹道。
		if (NewPhase != EDemoPhase::Combat)
		{
            ADemoProjectileBase::CancelAll(GetWorld()); // 与敌人攻击同栈撤销，GE触发清关后不能继续穿透。
			// It只借用本World敌人，不修改敌人注册表；清理回调不会产生伤害或死亡。
			for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) { It->CancelPendingAttacks(); It->FindComponentByClass<UDemoAmmoStatus>()->Clear(); } // 非战斗同栈清除DOT，防止死亡后残留伤害/奖励。
			// It的Destroy只标记延迟释放，Actor迭代器可安全前进。
			for (TActorIterator<ADemoEnemyProjectile> It(GetWorld()); It; ++It) It->Destroy();
			// Player仅调用期间借用；初始化阶段无Avatar时无需清理。
			if (ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
			{
				Player->GetWeaponComponent()->CancelActions();
				Player->ClearAttackStatuses();
			}
		}
	}
}
void UDemoRunFlowComponent::StartNextLevel()
{
	DEMO_LOG_CALL();
	// 状态和 Pawn 均为权威本局对象，UI 不能指定任意关卡跳过进度。
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const ADemoPlayerController* PC = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 借用输入状态；商店/确认框打开时禁止直接推进，确认后先消费请求。
	if (bStopping || bRestartRequested || !IsPlayerReady() || !State || !Player || !PC || PC->IsUpgradeMenuOpen() || PC->IsNextLevelConfirmationOpen() || PC->HasBlockingOverlay() || (State->Phase != EDemoPhase::Hub && State->Phase != EDemoPhase::Intermission)
		|| (!State->bEndless && State->LevelNumber >= DemoCombatConfig::LevelCount) || State->LevelNumber >= MAX_int32-1 || (!Areas.IsValid() || !Areas->IsReady()))
	{
		UE_LOG(LogFPSDemo, Warning, TEXT("StartNextLevel rejected: state/player/menu/area invalid"));
		return;
	}
	// 解锁在最后出发点再次检查；菜单开关或直接恢复不能绕过账号永久条件。
	const UDemoPlayerProfile* Progress = GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 当前GI的通关事实，只在此同步借用。
	if (!Progress || (State->Difficulty==EDemoDifficulty::Hell && !Progress->IsHellUnlocked()) || (State->bEndless && !Progress->IsEndlessUnlocked()))
	{ UE_LOG(LogFPSDemo,Warning,TEXT("Departure blocked: challenge mode locked")); return; }
	// 先完成本关全部数据解析再传送/生成，禁止部分使用旧的硬编码数据。
	FDemoLevelRow Level; // 下一关的布局、数量及等级配置。
	FDemoDifficultyRow Difficulty; // GetLevelConfig 的输出，用于确认该难度行合法。
	FDemoSpawnPlan Plan; // 数值/兵种由计划层解析，执行器不再读取GameMode或DataTable。
	FDemoEndlessRow Endless; // 仅无尽需要表中并发预算，普通关仍默认一次生成全部。
	FDemoAreaSnapshot Area; // 本关场景区域的值快照，Actor销毁不遗留引用。
	FString Error; // 配置错误进入可重开的失败页，避免无敌人而卡关。
	const int32 NextLevel = State->LevelNumber + 1; // 只有成功解析才提交进度。
	if (!GetLevelConfig(NextLevel, Level, Difficulty, Error) || (State->bEndless && !GetEndlessConfig(Endless, Error))
		|| !DemoSpawnPlan::Build(Level, Difficulty, EnemyTable, SpawnSystem->Settings, CampaignRunId, State->bEndless,
			State->bEndless ? Endless.MaxAlive : Level.EnemyCount + (Level.BossRow.IsNone() ? 0 : 1), Plan, Error)
		|| !Areas->ResolveArea(Level.ArenaIndex, Area, Error)) { FailRun(Error); return; }
	if (!SaveCheckpoint()) { UE_LOG(LogFPSDemo, Warning, TEXT("Departure blocked: checkpoint save failed")); return; } // 出发前先持久化，战斗内退出回到此状态。
	ClearTerminals(); // 清除安全区/上一清关区的一对物体，复用场景时不会遗留可交互的旧终端。
	++State->LevelNumber;
	SetPhase(EDemoPhase::Combat);
	Player->StopFiring();
	Player->GetCharacterMovement()->StopMovementImmediately();
	// 入场在竞技场左侧，敌人围绕中心分布，避免进入即贴脸。
	// 本关玩家与怪物共享同一快照，区域旋转不会只影响其中一方。
	Player->SetActorLocation(Area.CombatStart, false, nullptr, ETeleportType::TeleportPhysics);
	if (Player->GetController()) Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
	// 两种玩法提交同一执行器；生成/计数/重试/失败均由组件处理，GameMode只推进关卡。
	if (!SpawnSystem->StartEncounter(Plan, Area.Transform, Area.Geometry, Error)) { FailRun(Error); return; }
	// 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
	UE_LOG(LogFPSDemo, Display, TEXT("LEVEL START %d monsterLevel=%d arena=%d minions=%d boss=%d"), State->LevelNumber, Level.MonsterLevel, Level.ArenaIndex, Level.EnemyCount, !Level.BossRow.IsNone());
}
void UDemoRunFlowComponent::HandleSpawnRemaining(int32 Remaining)
{
	DEMO_LOG_CALL();
	if (ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>()) State->EnemiesRemaining = Remaining; // HUD唯一读取GameState，不访问组件内部队列。
}
void UDemoRunFlowComponent::FinishLevel()
{
	DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW command rejected during shutdown")); return; }
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!State || State->Phase != EDemoPhase::Combat || !SpawnSystem->IsEncounterComplete() || !Player) return;
	Player->StopFiring();
	Player->GetDemoASC()->CancelAllAbilities();
	FDemoLevelRow Completed; // 本次真实清关配置；中途关卡不再发金币。
	FDemoDifficultyRow CompletedDifficulty; // 最终胜利金币来自当前锁定难度的显式奖励，不乘属性倍率。
	FString ClearError; // 配置错误不可假装正常通关发奖。
	if (!GetLevelConfig(State->LevelNumber, Completed, CompletedDifficulty, ClearError)) { FailRun(ClearError); return; }
	if (State->bEndless)
	{
		State->BestEndlessLevel = FMath::Max(State->BestEndlessLevel,State->LevelNumber);
		UE_LOG(LogFPSDemo,Log,TEXT("ENDLESS_CLEARED level=%d best=%d"),State->LevelNumber,State->BestEndlessLevel);
	}
	if (!State->bEndless && State->LevelNumber == DemoCombatConfig::LevelCount)
	{
		if (!RewardSystem->GrantVictory(CampaignRunId, CompletedDifficulty)) { FailRun(TEXT("Victory reward rejected")); return; }
		SetPhase(EDemoPhase::Victory); // 奖励服务负责幂等发放，玩法层再推进阶段/重置临时成长/保存。
		RewardSystem->RecordVictoryUnlocks(CampaignRunId); // Profile只接受真实Victory；保持原解锁时序与资格判定。
		ResetCompletedRunGrowth(); // 胜利形成当帧就清空银币和临时成长，保留金币成长，结算时退出也不能恢复这些临时收益。
		SaveCheckpoint(); // 结算前退出也安全；Victory检查点下次读入自动转可继续挑战的Hub。
		if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(Player->GetController()))
		{
			PC->ShowEndScreen();
			if (State->Difficulty==EDemoDifficulty::Hell) PC->ShowEndlessUnlockTip(); // 先建立结算输入，再覆盖独立提示。
		}
	}
	else
	{
		// 本关物理区域由表决定，不以逻辑关号推导；玩家保持原位置和视角。
		FDemoLevelRow Level; // 借用表数据的值快照，提供当前 ArenaIndex。
		FDemoDifficultyRow Difficulty; // 校验该关配置时的输出，不用于修改玩家补给。
		FString Error; // 解析失败走可恢复终局，而不是让玩家困在没有入口的场景。
		if (!GetLevelConfig(State->LevelNumber, Level, Difficulty, Error)) { FailRun(Error); return; }
		Player->GetCharacterMovement()->StopMovementImmediately();
		if (!SpawnTerminals(Level.ArenaIndex)) { FailRun(TEXT("Cleared arena terminals could not be created")); return; }
		SetPhase(EDemoPhase::Reward);
		RefillPlayer(Player); // 保留原有满血/满弹奖励，但不会发生传送。
		SaveCheckpoint(); // 奖励未选也保存，恢复后仍需领取一次。
		if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(Player->GetController())) PC->OpenUpgradeMenu(true);
	}
}
bool UDemoRunFlowComponent::ChooseReward(int32 Choice)
{
	DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW command rejected during shutdown")); return false; }
	if (!RewardSystem->GrantAbilityChoice(CampaignRunId, Choice)) return false; // 权限、能力发放与同关防重统一归奖励服务。
	SetPhase(EDemoPhase::Intermission); // 关间留在已清关竞技场；Hub用于初始、死亡重开与通关续玩。
	SaveCheckpoint(); // 领取后的阶段与成长一起保存，避免重复免费奖励。
	return true;
}
void UDemoRunFlowComponent::NotifyPlayerDied()
{
	DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW command rejected during shutdown")); return; }
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 仅权威状态决定是否是玩家死亡重开。
	if (!State || State->Phase == EDemoPhase::Lobby || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("Player death ignored: no active run or already ended"));
		return;
	}
	State->bReturnToHubOnRestart = true;
	State->SilverCoins = 0; // 死亡立即清空银币；新Hub检查点保留金币永久成长，不能读回临时收益。
	FailRun(TEXT("You were eliminated"));
	if (!SaveResetCheckpoint()) // 死亡当帧保存装备；直接退出/回大厅同样可以恢复主武器。
		UE_LOG(LogFPSDemo, Warning, TEXT("DEATH_RESET save pending; leaving death screen will retry"));
}
void UDemoRunFlowComponent::FailRun(const FString& Reason)
{
	DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW failure ignored after shutdown")); return; } // 旧生成回调不能覆盖新旅行意图。
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	if (!State || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat) return;
	State->FailureReason = Reason;
	SetPhase(EDemoPhase::Defeat);
	SpawnSystem->CancelEncounter(false); // 先撤销生成和下一帧清场回调，保留Actor到World回收以避免GAS栈内销毁。
	ClearTerminals(); // 死亡/异常终局不能保留可继续购买或进入下一关的旧入口。
	// 终局阻止新伤害，保留对象到重开统一销毁，避免在属性回调中递归释放 ASC。
	if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0))) PC->ShowEndScreen();
	UE_LOG(LogFPSDemo, Warning, TEXT("RUN FAILED: %s"), *Reason);
}
void UDemoRunFlowComponent::RestartDemo()
{
	DEMO_LOG_CALL();
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	if (!State || bRestartRequested || (State->Phase != EDemoPhase::Victory && State->Phase != EDemoPhase::Defeat))
	{
		UE_LOG(LogFPSDemo, Log, TEXT("Restart rejected: invalid phase or travel already requested"));
		return;
	}
    // 胜利继续同一角色库存但清空临时成长/银币；死亡重载恢复本槽主副武器选择，均保留金币与解锁。
    if (State->Phase == EDemoPhase::Victory) { ContinueAfterVictory(); return; }
    // 死亡沿用活动槽落盘保留永久成长、清空临时成长，系统错误保留上次检查点返回大厅。
    UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>();
    // 真实死亡包括无槽开发World均走相同成长重置，GI传递永久账本而非仅传金币URL。
    if (State->bReturnToHubOnRestart) { ReturnToSafeHub(true); return; }
    if (Saves->GetActiveSlot() != INDEX_NONE)
    {
        TravelToRun(false); // 剩余分支只有系统错误，不覆盖上次有效检查点。
        return;
    }
	bRestartRequested = true;
    Shutdown();
	// 无活动槽且系统错误，只回干净大厅；真实死亡已走上面的GI永久账本恢复。
	UE_LOG(LogFPSDemo, Log, TEXT("RESTART destination=Lobby system error"));
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)), true);
}
bool UDemoRunFlowComponent::SaveCheckpoint()
{
    DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW save rejected after shutdown")); return false; } // 旅行前保存，旅行后不再允许旧UI覆盖GI活动槽。
    UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI拥有同步存档服务。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前安全阶段值视图。
    if (!Saves || !State) { UE_LOG(LogFPSDemo, Error, TEXT("SaveCheckpoint missing service/state")); return false; }
    // 实际死亡必须落盘清空银币/临时成长并保留永久成长；失败时禁止离开页面，系统错误仍保留上次检查点。
    if (State->Phase == EDemoPhase::Defeat && State->bReturnToHubOnRestart) return SaveResetCheckpoint(); // 同一入口保留死亡时的主枪和槽位。
    // 战斗中只重试资格元数据写入，不采集半场战利品；磁盘失败不能通过退出重进恢复手枪资格。
    if (State->Phase==EDemoPhase::Combat) return Saves->StoreChallenge(State->PistolChallenge,State->BestEndlessLevel);
    if (Saves->GetActiveSlot() == INDEX_NONE || State->Phase == EDemoPhase::Lobby || State->Phase == EDemoPhase::Defeat) return true;
    const ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 本次同步借用Avatar。
    const UDemoAttributeSet* Attributes = Player ? Player->GetDemoAttributes() : nullptr; // 只存永久与当局成长的合计基础值，排除瞬时状态。
    if (!Attributes) { UE_LOG(LogFPSDemo, Error, TEXT("SaveCheckpoint missing Avatar/attributes")); return false; }
    UDemoRunSave* Snapshot = NewObject<UDemoRunSave>(this); // Store会复制值并持久化，不延长GameMode生命周期。
    if (!DemoCheckpointAssembler::Capture(CampaignRunId, *State, *Player, *Snapshot)) return false;
    return Saves->Store(Snapshot);
}
bool UDemoRunFlowComponent::SaveResetCheckpoint()
{
    DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW reset save rejected after shutdown")); return false; } // 与普通保存共用停止边界。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 权威经济与永久来源，不沿用死亡前的临时属性。
    const ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)); // 死亡Pawn仍持有装备，直到后续OpenLevel才销毁。
    UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI负责原子提交小型值快照，跨World有效。
    if (!GetOwner()->HasAuthority() || !State || !Saves || !Player || !Player->GetWeaponComponent())
    { UE_LOG(LogFPSDemo, Warning, TEXT("Reset checkpoint rejected: missing authority/state/player/equipment/save service")); return false; }
    UDemoRunSave* Loadout = NewObject<UDemoRunSave>(this); // 只供本次同步采集，ResetActive只复制装备字段，不保存Actor引用。
    Loadout->MagazineBonus = State->UpgradeProgress.PermanentMagazine; // 去掉银币容量后按永久容量补满，与重开的GAS基线一致。
    Player->GetWeaponComponent()->CaptureLoadout(*Loadout, true);
    return Saves->ResetActive(State->Difficulty, State->Coins, State->UpgradeProgress, Loadout);
}
bool UDemoRunFlowComponent::RestoreCheckpoint(const UDemoRunSave& Data)
{
    DEMO_LOG_CALL();
    const UDemoPlayerProfile* Profile=GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 模式解锁跨槽共享，读档不能绕过账号条件。
    if (!Profile || (Data.Difficulty==EDemoDifficulty::Hell && !Profile->IsHellUnlocked()) || (Data.bEndless && !Profile->IsEndlessUnlocked()))
    { UE_LOG(LogFPSDemo,Warning,TEXT("Restore rejected: challenge mode locked")); return false; }
    if (!Data.Validate() || !StartRun()) { UE_LOG(LogFPSDemo, Error, TEXT("RestoreCheckpoint rejected: data/config/startup failure")); return false; }
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // StartRun已创建新安全区和干净角色状态。
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 本World新Avatar。
    CampaignRunId = Data.RunId;
    FString RestoreError; // 完整值恢复失败保留原文件，并撤销新建终端。
    if (!DemoCheckpointAssembler::Apply(Data, *State, *Player, RestoreError) || !ChallengeSystem->RestoreProgress(CampaignRunId, Data.PistolChallenge))
    { ClearTerminals(); State->FailureReason = RestoreError; SetPhase(EDemoPhase::Lobby); return false; }
    if (Data.Phase == EDemoPhase::Reward || Data.Phase == EDemoPhase::Intermission)
    {
        FDemoLevelRow Level; // 已完成关卡的竞技场配置值副本。
        FDemoDifficultyRow Difficulty; // 完整读取验证，不能直接信任存档的物理坐标。
        FString Error; // 配置错误回大厅，不覆盖原检查点。
        if (!GetLevelConfig(Data.CompletedLevel, Level, Difficulty, Error) || !SpawnTerminals(Level.ArenaIndex))
        { ClearTerminals(); State->FailureReason = TEXT("检查点区域无法重建，请检查关卡配置"); UE_LOG(LogFPSDemo, Error, TEXT("%s %s"), *State->FailureReason, *Error); SetPhase(EDemoPhase::Lobby); return false; }
        FDemoAreaSnapshot Area; // 从同一区域服务取得备战入场位置，包含地图旋转。
        if (!Areas->ResolveArea(Level.ArenaIndex, Area, Error)) { ClearTerminals(); SetPhase(EDemoPhase::Lobby); return false; }
        Player->SetActorLocation(Area.PreparedStart, false, nullptr, ETeleportType::TeleportPhysics);
    }
    SetPhase(Data.Phase);
    // 兼容已经存在的第10关Victory档：先还原检查点，再走统一结算清理入口，不能再次卡在结算页。
    if (Data.Phase == EDemoPhase::Victory)
    {
        if (ContinueAfterVictory()) return true;
        SetPhase(EDemoPhase::Lobby); // 终端/解锁恢复失败仍返回可重新选择的大厅，保留原存档。
        return false;
    }
    Player->GetCharacterMovement()->StopMovementImmediately();
    if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(Player->GetController())) PC->OnRunReady(); // 借用新控制器恢复正确UI。
    UE_LOG(LogFPSDemo, Log, TEXT("RUN_RESTORED phase=%d level=%d coins=%d"), static_cast<int32>(Data.Phase), Data.CompletedLevel, Data.Coins);
    return true;
}
void UDemoRunFlowComponent::ResetCompletedRunGrowth()
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 权威成长与钱包，金币保持不变。
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 本World Avatar，胜利回安全区继续使用其库存。
    if (!State || !Player || (State->Phase != EDemoPhase::Victory && State->Phase != EDemoPhase::Hub))
    { UE_LOG(LogFPSDemo, Warning, TEXT("RESET_GROWTH rejected outside completed run/hub")); return; }
    UDemoRunSave* Baseline = NewObject<UDemoRunSave>(this); // 统一复用新存档基线，避免重置数值与新档默认值分叉。
    Baseline->CreatedLocal = FDateTime::Now(); Baseline->RunId = CampaignRunId;
    Baseline->UpgradeProgress = State->UpgradeProgress;
    Baseline->ResetTemporaryGrowth(); // 新基线含永久金币属性及逐项价格；只移除银币与免费奖励。
    Player->RestoreRunProgress(*Baseline); // GAS设置合计基础值，永久增量不重复施加。
    Player->GetWeaponComponent()->RefillAll(); // 容量加成归零后按各枪基础容量补给，不能遗留超容量弹药。
    State->SilverCoins = 0; State->SilverPurchases = 0;
    State->UpgradeProgress = Baseline->UpgradeProgress;
    State->Purchases = State->GoldPurchases = Baseline->GoldPurchases;
    UE_LOG(LogFPSDemo, Log, TEXT("RESET_GROWTH gold=%d permanentDamage=%.0f permanentHealth=%.0f permanentMagazine=%.0f; temporary growth/silver cleared"),
        State->Coins, State->UpgradeProgress.PermanentDamage, State->UpgradeProgress.PermanentHealth, State->UpgradeProgress.PermanentMagazine);
}
bool UDemoRunFlowComponent::ContinueAfterVictory()
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 本World权威状态，只有完整第十关胜利允许结算后回安全区。
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 保留现有Avatar及ASC，不创建默认角色覆盖数据。
    ADemoPlayerController* PC = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 清除结算/暂停输入状态的拥有者。
    UDemoPlayerProfile* Profile = GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 永久解锁依据已完成的旧RunId幂等补记。
    if (!State || !Player || !PC || !Profile || (!Areas.IsValid() || !Areas->IsReady()) || bRestartRequested || SpawnSystem->GetRemaining() != 0
        || State->Phase != EDemoPhase::Victory || State->LevelNumber != DemoCombatConfig::LevelCount)
    { UE_LOG(LogFPSDemo, Warning, TEXT("VICTORY_CONTINUE rejected: incomplete victory/runtime/travel")); return false; }
    // 必须先提交旧战役凭据再创建新GUID；旧Victory文件反复读入不能产生重复解锁记录。
    if (!Profile->RecordVictory(CampaignRunId, State->Difficulty))
    { State->FailureReason = TEXT("通关记录未能确认，请检查日志后重试"); UE_LOG(LogFPSDemo, Error, TEXT("VICTORY_CONTINUE completion record rejected")); return false; }
    if (!SpawnTerminals(-1))
    { State->FailureReason = TEXT("安全区终端创建失败，请重试并检查场景碰撞"); UE_LOG(LogFPSDemo, Error, TEXT("VICTORY_CONTINUE hub terminals failed")); return false; }
    // 兼容旧Victory存档里残留的成长/银币；金币与解锁保留，当前库存按基础容量加金币永久容量重新补给。
    ResetCompletedRunGrowth();
    State->LevelNumber = 0;
    State->bEndless = false; // 新挑战重新选择模式，账号永久解锁与最高纪录不清除。
    State->EnemiesRemaining = 0;
    State->bReturnToHubOnRestart = false;
    State->FailureReason.Empty();
    CampaignRunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    ChallengeSystem->BeginRun(CampaignRunId);
    SpawnSystem->CancelEncounter(false); // 通关回Hub清理本轮组件状态，不再由GameMode保留推进Timer。
    SetPhase(EDemoPhase::Hub);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    PlacePlayerInHub(Player, false);
    UGameplayStatics::SetGamePaused(this, false); // 也允许从Victory上的Esc暂停页返回安全区。
    PC->OnRunReady();
    // 保存失败仍保留可操作的Hub和内存金币/解锁；HUD显示错误，下一次出发会先重试保存，旧Victory档也仍可继续。
    if (!SaveCheckpoint()) UE_LOG(LogFPSDemo, Warning, TEXT("VICTORY_CONTINUE hub checkpoint pending; progress retained in memory"));
    // 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
    UE_LOG(LogFPSDemo, Display, TEXT("VICTORY_CONTINUE Hub coins=%d purchases=%d kills=%d nextRun=%s"), State->Coins, State->Purchases, State->TotalKills, *CampaignRunId);
    return true;
}
bool UDemoRunFlowComponent::HasRunUpgrades() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 免费技能奖励也算成长，不只判断购买次数。
    const UDemoAttributeSet* Attributes = Player ? Player->GetDemoAttributes() : nullptr; // 当前属性只读借用，未初始化不视为已有成长。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 只提示将损失的临时成长，纯永久金币升级不需要放弃确认。
    return Attributes && State && (Attributes->GetWeaponDamageBonus() > State->UpgradeProgress.PermanentDamage
        || Attributes->GetMagazineBonus() > State->UpgradeProgress.PermanentMagazine || Attributes->GetMaxHealth() > 100.f + State->UpgradeProgress.PermanentHealth
        || Player->GetHealAmount() > 35 || Player->GetDashSpeed() > 1300);
}
bool UDemoRunFlowComponent::ReturnToSafeHub(bool bConfirmed)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 重新校验成长，不能信任先前UI结果。
    // 通关返回统一清空临时成长/银币、保留金币成长与武器解锁；所有返回入口共用同一规则。
    if (State && State->Phase == EDemoPhase::Victory) return ContinueAfterVictory();
    if (!State || State->Phase == EDemoPhase::Lobby || bRestartRequested || (HasRunUpgrades() && !bConfirmed)) { UE_LOG(LogFPSDemo, Warning, TEXT("ReturnToSafeHub rejected: state/travel pending/unconfirmed growth")); return false; }
    if (!SaveResetCheckpoint()) return false; // 主动放弃与死亡重开共用装备保留规则。
    TravelToRun(true); // 有槽和无槽开发World统一消费GI值快照，不通过URL丢失永久属性。
    return true;
}
void UDemoRunFlowComponent::TravelToRun(bool bResume)
{
    DEMO_LOG_CALL();
    if (bStopping || bRestartRequested) { UE_LOG(LogFPSDemo, Log, TEXT("Travel duplicate/stopped ignored")); return; } // 只允许存活World首次提交旅行。
    bRestartRequested = true;
    Shutdown(); // 阻止旧World在OpenLevel生效前继续处理交易与结算。
    SpawnSystem->CancelEncounter(false); // 提交旅行时立即取消，不能在OpenLevel生效前的旧World继续补怪或结算。
    UGameplayStatics::SetGamePaused(this, false); // 防止新World继承暂停意图，旧Timer随World销毁。
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this,true)), true, bResume ? TEXT("ResumeRun=1") : TEXT(""));
}
bool UDemoRunFlowComponent::GetEndlessConfig(FDemoEndlessRow& OutConfig, FString& Error) const
{
    DEMO_LOG_CALL();
    const FDemoEndlessRow* Row = EndlessTable && EndlessTable->GetRowStruct() == FDemoEndlessRow::StaticStruct()
        ? EndlessTable->FindRow<FDemoEndlessRow>(TEXT("Default"),TEXT("Endless"),false) : nullptr; // 只借用正确原生类型的Default行。
    const FDemoLevelRow* Boss = LevelTable ? LevelTable->FindRow<FDemoLevelRow>(DemoCombatConfig::LevelName(10),TEXT("EndlessBoss"),false) : nullptr; // Boss每10关出现，复用第十关模板与银币基数。
    if (!Row || !Boss || Boss->BossRow.IsNone() || Row->MaxAlive < 4 || Row->MaxAlive > 16)
    { Error=TEXT("无尽配置缺失、并发数量不在4到16之间或第十关Boss模板无效"); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; } // Boss关必须同时容纳三种小怪与Boss。
    for (const float Rate : {Row->HealthGrowth,Row->DamageGrowth,Row->CountGrowth,Row->SilverGrowth}) // 值快照，拒绝NaN/无限倍率。
        if (!FMath::IsFinite(Rate) || Rate < 1.f || Rate > 3.f)
        { Error=TEXT("无尽成长倍率必须在1到3之间"); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; }
    OutConfig=*Row;
    return true;
}
bool UDemoRunFlowComponent::SelectEndless()
{
    DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW command rejected during shutdown")); return false; }
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 仅权威初始安全区允许修改模式。
    UDemoPlayerProfile* Profile=GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 账号永久通关事实，不读取当前武器。
    FDemoEndlessRow Config; FString Error; // 先验证配置再提交选择，避免进入不可生成的模式。
    if (!State || State->Phase!=EDemoPhase::Hub || State->LevelNumber!=0 || !Profile || !Profile->IsEndlessUnlocked() || !GetEndlessConfig(Config,Error))
    { UE_LOG(LogFPSDemo,Warning,TEXT("ENDLESS select rejected: locked/phase/config %s"),*Error); return false; }
    State->bEndless=true; State->Difficulty=EDemoDifficulty::Hell;
    UE_LOG(LogFPSDemo,Log,TEXT("ENDLESS selected best=%d"),State->BestEndlessLevel);
    return true;
}
