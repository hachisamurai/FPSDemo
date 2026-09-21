#include "Game/FPSDemoGameMode.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Economy/DemoRewardComponent.h"
#include "Economy/DemoShopComponent.h"
#include "Game/DemoEncounterRules.h" // 固定战役与无尽共用兵种/Boss周期。
// 对应头文件先于依赖，保证UE独立编译能检查本类声明自包含。
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "Tests/DemoAmmoTest.h"
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

AFPSDemoGameMode::AFPSDemoGameMode()
{
	DEMO_LOG_CALL();
	SpawnSystem = CreateDefaultSubobject<UDemoEnemySpawnComponent>(TEXT("SpawnSystem")); // 当前权威World拥有生成调度。
	RewardSystem = CreateDefaultSubobject<UDemoRewardComponent>(TEXT("RewardSystem")); // 通过事件订阅去重后的击杀。
	ShopSystem = CreateDefaultSubobject<UDemoShopComponent>(TEXT("ShopSystem")); // 属性/弹药交易的唯一业务入口。
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
	// 死亡重开使用 URL 标志，避免静态变量污染不同 PIE World 或普通返回大厅。
	bStartInHub = UGameplayStatics::HasOption(Options, TEXT("ReturnToHub"));
	const int32 DifficultyIndex = UGameplayStatics::GetIntOption(Options, TEXT("RetryDifficulty"), static_cast<int32>(EDemoDifficulty::Normal)); // URL 整数合法范围 0..2。
	if (DifficultyIndex < 0 || DifficultyIndex > 2) UE_LOG(LogFPSDemo, Warning, TEXT("Invalid retry difficulty=%d; using Normal"), DifficultyIndex);
	RetryDifficulty = DifficultyIndex >= 0 && DifficultyIndex <= 2 ? static_cast<EDemoDifficulty>(DifficultyIndex) : EDemoDifficulty::Normal;
	RetryGold = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("RetryGold"), 0), 0, 100000000); // 无活动槽的重开也保留余额。
}

FVector AFPSDemoGameMode::GetAreaCenter(int32 Index) const
{
	DEMO_LOG_TICK();
	// 四区相距 60m，抬高 100m 避免模板地图几何与原生闯关区域重叠。
	return FVector(Index * 6000.f, 0.f, 10000.f);
}
ADemoInteractable* AFPSDemoGameMode::GetShopTerminal() const { DEMO_LOG_TICK(); return ShopTerminal; }
ADemoInteractable* AFPSDemoGameMode::GetNextLevelTerminal() const { DEMO_LOG_TICK(); return NextLevelTerminal; }

void AFPSDemoGameMode::BuildAreas()
{
	DEMO_LOG_CALL();
	// 白模地图在编辑器持有四个带区号标签的网格；完整匹配才跳过旧运行时地板/墙体。
	int32 AuthoredAreaCount = 0;
	// AreaIndex 与 GetAreaCenter 共用 -1..2 编号，防止只加载部分区域仍被当作完整地图。
	for (int32 AreaIndex = -1; AreaIndex < 3; ++AreaIndex)
	{
		// AreaTag 仅在本次查找中使用；每个区域最多计数一次。
		const FName AreaTag(*FString::Printf(TEXT("DemoArea%d"), AreaIndex));
		// It 只遍历当前 World 网格，引用由 World 持有，不缓存跨关卡指针。
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("DemoAuthoredBlockout")) && It->ActorHasTag(AreaTag) && It->GetStaticMeshComponent()->GetStaticMesh())
			{
				++AuthoredAreaCount;
				break;
			}
		}
	}
	if (AuthoredAreaCount != 0 && AuthoredAreaCount != 4)
	{
		UE_LOG(LogFPSDemo, Error, TEXT("Incomplete authored whitebox: expected 4 areas, found %d"), AuthoredAreaCount);
		bAreasReady = false;
		return;
	}
	UE_LOG(LogFPSDemo, Log, TEXT("BuildAreas uses %s geometry"), AuthoredAreaCount == 4 ? TEXT("Blender authored") : TEXT("procedural fallback"));
	// 共享 Cube 资产由组件持有；构建时加载，打包时 Engine 基础形状同样可用。
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* FloorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	// Area=-1 安全区、0..2 战斗区；每区一块地板和四面 5m 高边界。
	for (int32 Area = -1; AuthoredAreaCount == 0 && Area < 3; ++Area)
	{
		const FVector Center = GetAreaCenter(Area);
		// Piece=0 地板，1..4 四面墙；变换用厘米/基础 Cube 100cm 换算。
		for (int32 Piece = 0; Piece < 5; ++Piece)
		{
			FVector Offset(0,0,-50);
			FVector Scale(34,34,1);
			if (Piece == 1) { Offset = FVector(1700,0,250); Scale = FVector(1,35,5); }
			if (Piece == 2) { Offset = FVector(-1700,0,250); Scale = FVector(1,35,5); }
			if (Piece == 3) { Offset = FVector(0,1700,250); Scale = FVector(35,1,5); }
			if (Piece == 4) { Offset = FVector(0,-1700,250); Scale = FVector(35,1,5); }
			// 动态组件先设为 Movable 再赋网格，避免 BeginPlay 后设置 Static 网格被引擎拒绝。
			AStaticMeshActor* Geometry = GetWorld()->SpawnActor<AStaticMeshActor>(Center + Offset, FRotator::ZeroRotator);
			if (!Geometry || !Cube) { UE_LOG(LogFPSDemo, Error, TEXT("Area geometry spawn failed")); continue; }
			Geometry->SetMobility(EComponentMobility::Movable);
			Geometry->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Geometry->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Geometry->SetActorScale3D(Scale);
			if (FloorMaterial) Geometry->GetStaticMeshComponent()->SetMaterial(0, FloorMaterial);
		}
	}
	// 地形与终端分开管理：地形整局复用，两个交互物只属于当前备战区域。
	bAreasReady = Cube != nullptr;
}
bool AFPSDemoGameMode::SpawnTerminals(const FVector& Center)
{
	DEMO_LOG_CALL();
	ClearTerminals();
	// Spawn 允许引擎在中心附近微调，避免清关时玩家正好站在终端位置被物体包住。
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	// 分别检测实际地面；白模安全区有约 5cm 高的台面，固定 Z=80 会穿入台面而被拒绝生成。
	for (int32 Index = 0; Index < 2; ++Index) // 0 升级，1 下一关；由下方成功检查保证成对有效。
	{
		const FVector Anchor = Center + FVector(0, Index == 0 ? -300.f : 300.f, 0); // 当前终端期望的地面 XY。
		FHitResult GroundHit; // 垂直射线仅用于定位地面，不把玩家胶囊当作支撑台。
		FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(DemoTerminalGround), false);
		GroundQuery.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(this, 0));
		if (!GetWorld()->LineTraceSingleByChannel(GroundHit, Anchor + FVector(0,0,400), Anchor - FVector(0,0,100), ECC_Visibility, GroundQuery))
		{
			UE_LOG(LogFPSDemo, Error, TEXT("No ground below terminal index=%d center=%s"), Index, *Center.ToString());
			ClearTerminals();
			return false;
		}
		const FVector Location = GroundHit.ImpactPoint + FVector(0,0,81); // 根碰撞半高 80cm，再留 1cm 接触容差。
		ADemoInteractable* Terminal = GetWorld()->SpawnActor<ADemoInteractable>(ADemoInteractable::StaticClass(), Location, FRotator::ZeroRotator, Spawn); // World 持有，立即登记便于失败回滚。
		if (Index == 0) ShopTerminal = Terminal;
		else NextLevelTerminal = Terminal;
	}
	if (!IsValid(ShopTerminal) || !IsValid(NextLevelTerminal))
	{
		UE_LOG(LogFPSDemo, Error, TEXT("Terminals spawn failed near %s; rolling back pair"), *Center.ToString());
		ClearTerminals();
		return false;
	}
	ShopTerminal->Configure(true);
	NextLevelTerminal->Configure(false);
	UE_LOG(LogFPSDemo, Log, TEXT("TERMINALS READY center=%s shop=%s next=%s"), *Center.ToString(), *ShopTerminal->GetActorLocation().ToString(), *NextLevelTerminal->GetActorLocation().ToString());
	return true;
}
void AFPSDemoGameMode::ClearTerminals()
{
	DEMO_LOG_CALL();
	// Actor::Destroy 延迟释放 UObject；当前 Interact 调用栈可以安全返回，但旧终端已不再有效。
	if (IsValid(ShopTerminal)) ShopTerminal->Destroy();
	if (IsValid(NextLevelTerminal)) NextLevelTerminal->Destroy();
	ShopTerminal = nullptr;
	NextLevelTerminal = nullptr;
}
void AFPSDemoGameMode::StartPlay()
{
	DEMO_LOG_CALL();
	// 同World UObject委托，EndPlay精确解绑；组件负责Timer，不在GameMode复制生成状态。
	SpawnSystem->OnRemainingChanged.AddUObject(this, &AFPSDemoGameMode::HandleSpawnRemaining);
	SpawnSystem->OnCleared.AddUObject(this, &AFPSDemoGameMode::FinishLevel);
	SpawnSystem->OnFailed.AddUObject(this, &AFPSDemoGameMode::FailRun);
	// 预先载入但不生成敌人或交互物；大厅遮罩下冻结角色避免玩家提前进入战斗。
	EnemyTable = EnemyTableAsset.LoadSynchronous();
	EndlessTable = EndlessTableAsset.LoadSynchronous(); // 无尽配置只在选择/生成无尽时校验。
	DifficultyTable = DifficultyTableAsset.LoadSynchronous();
	LevelTable = LevelTableAsset.LoadSynchronous();
	Super::StartPlay();
	SetPhase(EDemoPhase::Lobby);
	// PC 仅本次借用；控制器负责光标和输入，GameMode 不持有 UI 状态。
	if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0))) PC->ShowLobby();
	// 先消费GI中的一次读档请求，普通返回大厅不会自动载入上次槽。
    if (UGameplayStatics::HasOption(OptionsString, TEXT("ResumeRun")))
    {
        if (const UDemoRunSave* Pending = GetGameInstance()->GetSubsystem<UDemoRunSaves>()->ConsumePending()) // GI强持有，本次只借用。
        {
            if (!RestoreCheckpoint(*Pending))
            {
                if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->ShowLobby(); // 失败仍允许重新选择其他槽。
            }
        }
    }
    else if (bStartInHub)
	{
		// 新World的PS/ASC/角色已经重新建立；带回难度与金币，其余局内进度归零。
		if (StartRun())
        {
            SelectDifficulty(RetryDifficulty);
            GetGameState<ADemoGameState>()->Coins = RetryGold; // 新World只恢复允许跨挑战保留的金币，成长/银币保持默认。
            if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->OnRunReady(); // 新局返回不走存档选择页。
        }
	}
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
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoAmmoTest"))) GetWorld()->SpawnActor<ADemoAmmoTest>(); // 显式隔离回归入口。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoEnemyAttackTest"))) GetWorld()->SpawnActor<ADemoEnemyAttackTest>();
#endif
}
bool AFPSDemoGameMode::SelectDifficulty(EDemoDifficulty Difficulty)
{
	DEMO_LOG_CALL();
	// State 是本局权威状态；枚举必须验证以拒绝非法脚本或旧 UI 请求。
	ADemoGameState* State = GetGameState<ADemoGameState>();
	if (!State || State->Phase != EDemoPhase::Hub || State->LevelNumber != 0 || DemoCombatConfig::DifficultyName(Difficulty).IsNone())
	{
		UE_LOG(LogFPSDemo, Warning, TEXT("Difficulty change rejected outside initial hub or invalid enum"));
		return false;
	}
	if (Difficulty == EDemoDifficulty::Hell && !GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()->IsHellUnlocked())
	{ UE_LOG(LogFPSDemo, Warning, TEXT("Hell locked: complete hard using pistol only")); return false; }
	State->bEndless = false; // 普通难度选择明确退出无尽模式，保留最高纪录。
	State->Difficulty = Difficulty;
	UE_LOG(LogFPSDemo, Log, TEXT("DIFFICULTY %s"), *DemoCombatConfig::DifficultyName(Difficulty).ToString());
	return true;
}
bool AFPSDemoGameMode::StartRun()
{
	DEMO_LOG_CALL();
	// 当前单人 Pawn/State 仅调用期借用，所有配置在生成交互物前完整校验。
	ADemoGameState* State = GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!State || !Player || State->Phase != EDemoPhase::Lobby) { UE_LOG(LogFPSDemo, Warning, TEXT("StartRun rejected: invalid phase/player")); return false; }
	FString Error; // 展示和日志共用校验错误，留在大厅可修复配置后重试。
	if (!DemoCombatConfig::Validate(EnemyTable, DifficultyTable, LevelTable, Error))
	{
		State->FailureReason = Error;
		UE_LOG(LogFPSDemo, Error, TEXT("Combat config rejected: %s"), *Error);
		return false;
	}
	if (!bAreasReady) BuildAreas();
	if (!bAreasReady)
	{
		State->FailureReason = TEXT("Arena initialization failed; check map and log");
		UE_LOG(LogFPSDemo, Error, TEXT("StartRun rejected: %s"), *State->FailureReason);
		return false;
	}
	// 每局首次进入安全区时创建终端；清关后的终端由 FinishLevel 在当前竞技场创建。
	if (!SpawnTerminals(GetAreaCenter(-1)))
	{
		State->FailureReason = TEXT("Safe hub terminals could not be created; check collision and log");
		return false;
	}
	State->FailureReason.Empty();
	SetPhase(EDemoPhase::Hub);
	CampaignRunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); // 本次完整战役的唯一通关凭据标识。
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	PlacePlayerInHub(Player);
	return true;
}
bool AFPSDemoGameMode::GetLevelConfig(int32 Number, FDemoLevelRow& OutLevel, FDemoDifficultyRow& OutDifficulty, FString& Error) const
{
	DEMO_LOG_CALL();
	if (!DemoCombatConfig::Validate(EnemyTable, DifficultyTable, LevelTable, Error)) return false;
	// 本次查询借用表行，返回值复制；表热改不能改变已经生成的敌人。
	const ADemoGameState* State = GetGameState<ADemoGameState>();
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
bool AFPSDemoGameMode::GetSpawnStats(int32 Number, bool bBoss, FDemoEnemySpawnStats& OutStats, FString& Error) const
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
void AFPSDemoGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	DEMO_LOG_CALL();
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	if (bAreasReady && NewPlayer) PlacePlayerInHub(Cast<ADemoCharacter>(NewPlayer->GetPawn()));
}
void AFPSDemoGameMode::PlacePlayerInHub(ADemoCharacter* Player, bool bRefill)
{
	DEMO_LOG_CALL();
	if (!Player) { UE_LOG(LogFPSDemo, Warning, TEXT("PlacePlayerInHub rejected: missing Avatar")); return; }
	Player->StopFiring();
	Player->GetCharacterMovement()->StopMovementImmediately();
	Player->SetActorLocation(GetAreaCenter(-1) + FVector(-650,0,100), false, nullptr, ETeleportType::TeleportPhysics);
	if (Player->GetController()) Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
	// 新建/死亡重开沿用补给；通关续玩不覆盖保存下来的当前生命和独立弹药。
	if (bRefill) RefillPlayer(Player);
}
void AFPSDemoGameMode::RefillPlayer(ADemoCharacter* Player)
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

void AFPSDemoGameMode::SetPhase(EDemoPhase NewPhase)
{
	DEMO_LOG_CALL();
	if (ADemoGameState* State = GetGameState<ADemoGameState>())
	{
		// 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
		UE_LOG(LogFPSDemo, Display, TEXT("PHASE %d -> %d level=%d coins=%d"), static_cast<int32>(State->Phase), static_cast<int32>(NewPhase), State->LevelNumber, State->Coins);
		State->Phase = NewPhase;
		// 在阶段切换的同一调用栈撤销攻击，不等待下一帧；终局、奖励与安全区不会遗留减速或弹道。
		if (NewPhase != EDemoPhase::Combat)
		{
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

void AFPSDemoGameMode::StartNextLevel()
{
	DEMO_LOG_CALL();
	// 状态和 Pawn 均为权威本局对象，UI 不能指定任意关卡跳过进度。
	ADemoGameState* State = GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const ADemoPlayerController* PC = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 借用输入状态；商店/确认框打开时禁止直接推进，确认后先消费请求。
	if (!State || !Player || !PC || PC->IsUpgradeMenuOpen() || PC->IsNextLevelConfirmationOpen() || PC->HasBlockingOverlay() || (State->Phase != EDemoPhase::Hub && State->Phase != EDemoPhase::Intermission)
		|| (!State->bEndless && State->LevelNumber >= DemoCombatConfig::LevelCount) || State->LevelNumber >= MAX_int32-1 || !bAreasReady)
	{
		UE_LOG(LogFPSDemo, Warning, TEXT("StartNextLevel rejected: state/player/menu/area invalid"));
		return;
	}
	// 解锁在最后出发点再次检查；菜单开关或直接恢复不能绕过账号永久条件。
	const UDemoPlayerProfile* Progress = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 当前GI的通关事实，只在此同步借用。
	if (!Progress || (State->Difficulty==EDemoDifficulty::Hell && !Progress->IsHellUnlocked()) || (State->bEndless && !Progress->IsEndlessUnlocked()))
	{ UE_LOG(LogFPSDemo,Warning,TEXT("Departure blocked: challenge mode locked")); return; }
	// 先完成本关全部数据解析再传送/生成，禁止部分使用旧的硬编码数据。
	FDemoLevelRow Level; // 下一关的布局、数量及等级配置。
	FDemoDifficultyRow Difficulty; // GetLevelConfig 的输出，用于确认该难度行合法。
	FDemoSpawnPlan Plan; // 数值/兵种由计划层解析，执行器不再读取GameMode或DataTable。
	FDemoEndlessRow Endless; // 仅无尽需要表中并发预算，普通关仍默认一次生成全部。
	FTransform AreaTransform; FDemoSpawnGeometry Geometry; // 本关场景区域的值快照，Actor销毁不遗留引用。
	FString Error; // 配置错误进入可重开的失败页，避免无敌人而卡关。
	const int32 NextLevel = State->LevelNumber + 1; // 只有成功解析才提交进度。
	if (!GetLevelConfig(NextLevel, Level, Difficulty, Error) || (State->bEndless && !GetEndlessConfig(Endless, Error))
		|| !DemoSpawnPlan::Build(Level, Difficulty, EnemyTable, SpawnSystem->Settings, CampaignRunId, State->bEndless,
			State->bEndless ? Endless.MaxAlive : Level.EnemyCount + (Level.BossRow.IsNone() ? 0 : 1), Plan, Error)
		|| !ADemoEnemySpawnArea::Resolve(GetWorld(), Level.ArenaIndex, GetAreaCenter(Level.ArenaIndex), AreaTransform, Geometry, Error)) { FailRun(Error); return; }
	if (!SaveCheckpoint()) { UE_LOG(LogFPSDemo, Warning, TEXT("Departure blocked: checkpoint save failed")); return; } // 出发前先持久化，战斗内退出回到此状态。
	ClearTerminals(); // 清除安全区/上一清关区的一对物体，复用场景时不会遗留可交互的旧终端。
	++State->LevelNumber;
	SetPhase(EDemoPhase::Combat);
	Player->StopFiring();
	Player->GetCharacterMovement()->StopMovementImmediately();
	// 入场在竞技场左侧，敌人围绕中心分布，避免进入即贴脸。
	const FVector Center = GetAreaCenter(Level.ArenaIndex);
	Player->SetActorLocation(Center + FVector(-1100,0,100), false, nullptr, ETeleportType::TeleportPhysics);
	if (Player->GetController()) Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
	// 两种玩法提交同一执行器；生成/计数/重试/失败均由组件处理，GameMode只推进关卡。
	if (!SpawnSystem->StartEncounter(Plan, AreaTransform, Geometry, Error)) { FailRun(Error); return; }
	// 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
	UE_LOG(LogFPSDemo, Display, TEXT("LEVEL START %d monsterLevel=%d arena=%d minions=%d boss=%d"), State->LevelNumber, Level.MonsterLevel, Level.ArenaIndex, Level.EnemyCount, !Level.BossRow.IsNone());
}
void AFPSDemoGameMode::HandleSpawnRemaining(int32 Remaining)
{
	DEMO_LOG_CALL();
	if (ADemoGameState* State = GetGameState<ADemoGameState>()) State->EnemiesRemaining = Remaining; // HUD唯一读取GameState，不访问组件内部队列。
}
UDemoEnemySpawnComponent* AFPSDemoGameMode::GetSpawnSystem() const { DEMO_LOG_TICK(); return SpawnSystem; }
UDemoRewardComponent* AFPSDemoGameMode::GetRewardSystem() const { DEMO_LOG_TICK(); return RewardSystem; }
UDemoShopComponent* AFPSDemoGameMode::GetShopSystem() const { DEMO_LOG_TICK(); return ShopSystem; }
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
void AFPSDemoGameMode::FinishLevel()
{
	DEMO_LOG_CALL();
	ADemoGameState* State = GetGameState<ADemoGameState>();
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
		if (!SpawnTerminals(GetAreaCenter(Level.ArenaIndex))) { FailRun(TEXT("Cleared arena terminals could not be created")); return; }
		SetPhase(EDemoPhase::Reward);
		RefillPlayer(Player); // 保留原有满血/满弹奖励，但不会发生传送。
		SaveCheckpoint(); // 奖励未选也保存，恢复后仍需领取一次。
		if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(Player->GetController())) PC->OpenUpgradeMenu(true);
	}
}
bool AFPSDemoGameMode::ChooseReward(int32 Choice)
{
	DEMO_LOG_CALL();
	if (!RewardSystem->GrantAbilityChoice(CampaignRunId, Choice)) return false; // 权限、能力发放与同关防重统一归奖励服务。
	SetPhase(EDemoPhase::Intermission); // 关间留在已清关竞技场；Hub用于初始、死亡重开与通关续玩。
	SaveCheckpoint(); // 领取后的阶段与成长一起保存，避免重复免费奖励。
	return true;
}
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
void AFPSDemoGameMode::NotifyPlayerDied()
{
	DEMO_LOG_CALL();
	ADemoGameState* State = GetGameState<ADemoGameState>(); // 仅权威状态决定是否是玩家死亡重开。
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
void AFPSDemoGameMode::FailRun(const FString& Reason)
{
	DEMO_LOG_CALL();
	ADemoGameState* State = GetGameState<ADemoGameState>();
	if (!State || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat) return;
	State->FailureReason = Reason;
	SetPhase(EDemoPhase::Defeat);
	SpawnSystem->CancelEncounter(false); // 先撤销生成和下一帧清场回调，保留Actor到World回收以避免GAS栈内销毁。
	ClearTerminals(); // 死亡/异常终局不能保留可继续购买或进入下一关的旧入口。
	// 终局阻止新伤害，保留对象到重开统一销毁，避免在属性回调中递归释放 ASC。
	if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0))) PC->ShowEndScreen();
	UE_LOG(LogFPSDemo, Warning, TEXT("RUN FAILED: %s"), *Reason);
}
void AFPSDemoGameMode::RestartDemo()
{
	DEMO_LOG_CALL();
	const ADemoGameState* State = GetGameState<ADemoGameState>();
	if (!State || bRestartRequested || (State->Phase != EDemoPhase::Victory && State->Phase != EDemoPhase::Defeat))
	{
		UE_LOG(LogFPSDemo, Log, TEXT("Restart rejected: invalid phase or travel already requested"));
		return;
	}
    // 胜利继续同一角色库存但清空临时成长/银币；死亡重载恢复本槽主副武器选择，均保留金币与解锁。
    if (State->Phase == EDemoPhase::Victory) { ContinueAfterVictory(); return; }
    // 死亡沿用活动槽落盘保留永久成长、清空临时成长，系统错误保留上次检查点返回大厅。
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>();
    // 真实死亡包括无槽开发World均走相同成长重置，GI传递永久账本而非仅传金币URL。
    if (State->bReturnToHubOnRestart) { ReturnToSafeHub(true); return; }
    if (Saves->GetActiveSlot() != INDEX_NONE)
    {
        TravelToRun(false); // 剩余分支只有系统错误，不覆盖上次有效检查点。
        return;
    }
	bRestartRequested = true;
	// 无活动槽且系统错误，只回干净大厅；真实死亡已走上面的GI永久账本恢复。
	UE_LOG(LogFPSDemo, Log, TEXT("RESTART destination=Lobby system error"));
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)), true);
}
void AFPSDemoGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
	SpawnSystem->OnRemainingChanged.RemoveAll(this); SpawnSystem->OnCleared.RemoveAll(this); SpawnSystem->OnFailed.RemoveAll(this); // 先解除Owner回调再卸载组件。
	SpawnSystem->CancelEncounter(false); // 先撤销生成和下一帧清场回调，保留Actor到World回收以避免GAS栈内销毁。
	Super::EndPlay(EndPlayReason);
}
