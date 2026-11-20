// 职责：权威战役/无尽阶段与检查点流程；依赖CombatSession、配置和生成服务，无尽按十关节奏复用特殊替换与稳定Boss池。
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Progression/Summary/DemoRunSummaryComponent.h"
#include "Game/Preparation/DemoEncounterPreparationComponent.h"
#include "Game/Objectives/DemoObjectiveCoordinatorComponent.h"
#include "Game/Content/DemoCampaignDefinition.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/Coop/Ready/DemoTeamReadyComponent.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"
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
	const ADemoPlayerState* LeaderState = Avatar.IsValid() ? Avatar->GetPlayerState<ADemoPlayerState>() : nullptr; // 房主的运行时权限来自其PS，不能用GI替代远端个人账本。
    if (!LeaderState || (Difficulty == EDemoDifficulty::Hell && !LeaderState->GetProgressComponent()->GetSnapshot().bHellUnlocked))
	{ UE_LOG(LogFPSDemo, Warning, TEXT("Hell locked: complete hard using pistol only")); return false; }
    const bool bChanged=State->bEndless||State->Difficulty!=Difficulty; // 同一选项不反复撤销全队确认。
    if(ContentId.IsNone() && !BeginCampaignContent()) return false; // 切回战役先准备资源，失败不能改变原模式。
	State->bEndless = false; // 普通难度选择明确退出无尽模式，保留最高纪录。
	State->Difficulty = Difficulty;
    if(bChanged) CastChecked<AFPSDemoGameMode>(GetOwner())->GetTeamReadySystem()->InvalidateAll(TEXT("难度或玩法模式已改变，请重新准备"));
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
    CampaignRunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); // 新Hub凭据；恢复事务稍后换回存档RunId。
    if(!bRestoringCheckpoint && !BeginCampaignContent()) return false; // 资源缺失留大厅；恢复已有档时不读当前作者配置。
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
	ChallengeSystem->BeginRun(CampaignRunId);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	PlacePlayerInHub(Player);
    CastChecked<AFPSDemoGameMode>(GetOwner())->GetTeamReadySystem()->InvalidateAll(TEXT("新挑战已建立，请确认配置后准备"));
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
    if(UsesDemoCampaign())
    {
        FDemoCampaignPlan ContentPlan; // 区域只依赖seed/关号，人数不参与房间随机。
        if(!CampaignDefinition || !CampaignDefinition->BuildPlan(ContentSeed,Number,1,ContentPlan,Error)) return false;
        OutLevel.ArenaIndex=ContentPlan.ArenaIndex;
        if(Number==DemoCombatConfig::LevelCount)
        { OutLevel.BossRow=CampaignDefinition->RoomBossRows.FindChecked(ContentPlan.RoomId); OutLevel.BossPool.Reset(); } // 新计划已稳定抽定Boss房间，禁止旧池再抽一次导致身份不对应。
    }
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
        OutLevel.BossPool=OutLevel.BossRow.IsNone()?TArray<FName>():BossLevel->BossPool; // 无尽每十关也稳定选择双Boss；其余关不得携带候选池。
        const FDemoLevelRow* RosterLevel=LevelTable->FindRow<FDemoLevelRow>(DemoCombatConfig::LevelName((Number-1)%DemoCombatConfig::LevelCount+1),TEXT("EndlessRoster")); // 重复十关引入节奏，只复制有限替换槽，不改变指数成长。
        OutLevel.SpecialRows=RosterLevel?RosterLevel->SpecialRows:TArray<FName>();
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
	Player->SetCoopEliminated(false); // 死亡标签在治疗前移除，复活不丢失库存和个人永久数据。
    if (ADemoPlayerController* Owner = Cast<ADemoPlayerController>(Player->GetController())) { Owner->SetViewTarget(Player); Owner->ClientSetViewTarget(Player); }
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
    if (NewPhase!=EDemoPhase::Reward && NewPhase!=EDemoPhase::Intermission)
        GetWorld()->GetTimerManager().ClearTimer(TerminalRetryTimer);
	if(NewPhase!=EDemoPhase::Combat)
    {
        GetWorld()->GetTimerManager().ClearTimer(OutcomeTimer); bObjectiveCandidate=false;
        if(ObjectiveSystem && !bEndingEncounter) ObjectiveSystem->CancelObjective();
        for(ADemoCharacter* Member:CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
            if(ADemoPlayerState* PS=Member->GetPlayerState<ADemoPlayerState>()) PS->GetRunBuildComponent()->EndEncounter();
    }
    if (NewPhase != EDemoPhase::Combat && CombatSession) CombatSession->Invalidate(); // 必须早于状态/销毁回调，阻止同步重入伤害。
	if (ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>())
	{
		const EDemoPhase PreviousPhase = State->Phase; // 本次权威提交的旧阶段，用于只撤销离开备战时的动作，不刷新原招式冷却。
		const bool bPhaseChanged = PreviousPhase != NewPhase; // 重复发布同一阶段不制造新攻击代次；客户端关号/UI刷新也不能误取消有效弹。
		// 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
		UE_LOG(LogFPSDemo, Display, TEXT("PHASE %d -> %d level=%d"), static_cast<int32>(State->Phase), static_cast<int32>(NewPhase), State->LevelNumber);
		State->Phase = NewPhase;
        if (bPhaseChanged)
        {
            State->PhaseRevision = State->PhaseRevision == MAX_uint32 ? 1u : State->PhaseRevision + 1u; // 溢出跳过未初始化的0；每个World独立，不写存档。
            if (PreviousPhase == EDemoPhase::Intermission)
                for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers()) // 备战没有CombatSession清理回调，显式撤销其连射/装填/开镜及近战动作。
                    if (Member && Member->GetWeaponComponent()) Member->GetWeaponComponent()->CancelActions();
        }
        CastChecked<AFPSDemoGameMode>(GetOwner())->GetTeamReadySystem()->InvalidateAll(TEXT("战局阶段已改变")); // 新一关不会继承上次Ready；先更新阶段再生成投影。
        // 战斗攻击由会话失效清理；备战弹每次模拟检查阶段版本，不能把旧弹或旧连射带进新关。
        State->ForceNetUpdate(); State->OnRep_RunView(); // 服务器本地UI与远端OnRep共用幂等通知。
        for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers()) // 只向每个拥有者发布个人菜单投影。
            if (ADemoPlayerController* Owner = Cast<ADemoPlayerController>(Member->GetController())) Owner->PublishMenuSnapshot();
	}
}
void UDemoRunFlowComponent::StartNextLevel()
{
	DEMO_LOG_CALL();
	// 状态和 Pawn 均为权威本局对象，UI 不能指定任意关卡跳过进度。
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const ADemoPlayerController* PC = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 房主身份只借用；保留下一关确认页，成功提交Combat后由阶段通知关闭。
	if (bStopping || bRestartRequested || !IsPlayerReady() || !State || !Player || !PC || (State->Phase != EDemoPhase::Hub && State->Phase != EDemoPhase::Intermission)
		|| (!State->bEndless && State->LevelNumber >= DemoCombatConfig::LevelCount) || State->LevelNumber >= MAX_int32-1 || (!Areas.IsValid() || !Areas->IsReady()))
	{
		if(State) State->FailureReason=TEXT("当前阶段、角色或区域尚未就绪，请稍后重试");
		UE_LOG(LogFPSDemo, Warning, TEXT("StartNextLevel rejected: state/player/area invalid"));
		return;
	}
	// 解锁在最后出发点再次检查；菜单开关或直接恢复不能绕过账号永久条件。
	const UDemoPlayerProgressComponent* Progress = Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 房主本人永久准入权限，由真实完成事实写入PS。
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) // 新连接必须完成握手和Avatar初始化后才能一起出发。
        if (ADemoPlayerController* Member = Cast<ADemoPlayerController>(It->Get()); Member && (!Member->IsCoopRegistered() || !Cast<ADemoCharacter>(Member->GetPawn())
            || !Cast<ADemoCharacter>(Member->GetPawn())->GetDemoAttributes() || !Cast<ADemoCharacter>(Member->GetPawn())->GetWeaponComponent()->GetActiveWeapon()))
        { State->FailureReason=TEXT("等待所有队员完成连接与角色初始化"); UE_LOG(LogFPSDemo, Log, TEXT("Departure waiting for member readiness")); return; }
    if (!Progress || (State->Difficulty==EDemoDifficulty::Hell && !Progress->GetSnapshot().bHellUnlocked) || (State->bEndless && !Progress->GetSnapshot().bEndlessUnlocked))
	{ State->FailureReason=TEXT("当前挑战模式尚未解锁"); UE_LOG(LogFPSDemo,Warning,TEXT("Departure blocked: challenge mode locked")); return; }
    FString ReadyError; // 出发瞬间全量重验，按钮可点与旧复制快照不能作为出发凭据。
    if(!CastChecked<AFPSDemoGameMode>(GetOwner())->GetTeamReadySystem()->ValidateDeparture(ReadyError))
    { State->FailureReason=ReadyError; return; }
	// 先完成本关全部数据解析再传送/生成，禁止部分使用旧的硬编码数据。
	FDemoLevelRow Level; // 下一关的布局、数量及等级配置。
	FDemoDifficultyRow Difficulty; // GetLevelConfig 的输出，用于确认该难度行合法。
	FDemoSpawnPlan Plan; // 数值/兵种由计划层解析，执行器不再读取GameMode或DataTable。
	FDemoEndlessRow Endless; // 无尽保留配置并发预算；非新战役的旧路径同样限制至16存活，其余排队。
	FDemoAreaSnapshot Area; // 本关场景区域的值快照，Actor销毁不遗留引用。
	FString Error; // 提交Combat之前的配置错误只阻止出发，保留安全阶段、终端和已准备意图供重试。
	const int32 NextLevel = State->LevelNumber + 1; // 只有成功解析才提交进度。
	if (!GetLevelConfig(NextLevel, Level, Difficulty, Error) || (State->bEndless && !GetEndlessConfig(Endless, Error)))
    { State->FailureReason=Error; UE_LOG(LogFPSDemo,Warning,TEXT("DEPARTURE config rejected %s"),*Error); return; }
    if(UsesDemoCampaign())
    {
        if(!PrepareCampaignPlan(NextLevel,Level,Difficulty,Plan,Error))
        { State->FailureReason=Error; UE_LOG(LogFPSDemo,Warning,TEXT("DEPARTURE preparation rejected %s"),*Error); return; }
    }
    else
    {
        Level.EnemyCount=FMath::Clamp(FMath::RoundToInt(Level.EnemyCount*Difficulty.EnemyCountMultiplier),1,1000000); // 旧/无尽也受难度数量配置影响，只缩放小怪，保持有界计数。
        if (!DemoSpawnPlan::Build(Level, Difficulty, EnemyTable, SpawnSystem->Settings, CampaignRunId, State->bEndless,
            State->bEndless ? Endless.MaxAlive : FMath::Min(16,Level.EnemyCount+(Level.BossRow.IsNone()?0:1)), Plan, Error))
        { State->FailureReason=Error; UE_LOG(LogFPSDemo,Warning,TEXT("DEPARTURE spawn plan rejected %s"),*Error); return; }
    }
    const UDemoRoomStreamingSettings* RoomSettings = GetDefault<UDemoRoomStreamingSettings>(); // 安装内容目录；旧冻结修订仍按原0..2映射，不改V9字节。
    const FName StreamedRoomId = UsesDemoCampaign() && RoomSettings->bEnabled && ContentRevision >= RoomSettings->MinimumContentRevision ? PreparedCampaign.RoomId : NAME_None; // 旧玩法/无尽/旧修订保留原区域。
    const FString PreparationKey = FString::Printf(TEXT("%s/%d/%u/%d/%d/%d/%s"), *CampaignRunId, NextLevel, State->PhaseRevision, State->TeamReady.Revision, int32(State->Difficulty), State->bEndless, *ContentFingerprint); // 绑定轮次/阶段/队伍准备/内容，不能复用旧装备或旧关ACK。
    if (!CastChecked<AFPSDemoGameMode>(GetOwner())->GetPreparationSystem()->EnsureReady(PreparationKey, Plan, Level.ArenaIndex, StreamedRoomId, Area, Error))
    { State->FailureReason=Error; return; } // 保留原位置/终端，不生成目标或刷怪；全员完成后由屏障重新调用本事务。
	if (!SaveCheckpoint()) { UE_LOG(LogFPSDemo, Warning, TEXT("Departure blocked: checkpoint save failed")); return; } // 出发前先持久化，战斗内退出回到此状态。
	// 目标完全准备成功前保留上一安全区终端，失败无需重建或丢失当前交互。
	bObjectiveCandidate=false; bEndingEncounter=false;
	CombatSession = NewObject<UDemoCombatSession>(this); // 旧引用永不重新激活，飞弹不能跨关复用。
	if (!CombatSession->Initialize(GetWorld(), CampaignRunId, NextLevel))
    { CombatSession->Invalidate(); State->FailureReason=TEXT("战斗初始化失败，请重试"); UE_LOG(LogFPSDemo,Warning,TEXT("DEPARTURE session initialization rejected")); return; }
    for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers()) // 活跃角色只注册一次本关撤销入口，新增攻击类型无需修改阶段函数。
        CombatSession->RegisterCleanup(Member, FSimpleDelegate::CreateUObject(Member, &ADemoCharacter::OnCombatSessionEnded));
	FDemoObjectiveConfig ObjectiveConfig; // 服务器白模锚点，后续可由独立房间资产提供，不接受客户端位置。
    ObjectiveConfig.SourceLevelPackage=Area.Geometry.ExplicitSourceLevelPackage; // 流送准备已验证的实际Level来源；目标近地复检不得命中上层或别的房间。
    ObjectiveConfig.Kind=UsesDemoCampaign()?PreparedCampaign.Objective:EDemoObjectiveKind::KillAll;
    if(UsesDemoCampaign())
    {
        ObjectiveConfig.DeviceHealth=CampaignDefinition->DeviceBaseHealth*Level.HealthMultiplier*Difficulty.HealthMultiplier*CampaignDefinition->Party[PreparedCampaign.PartySize-1].DeviceHealth;
        ObjectiveConfig.ControlSeconds=CampaignDefinition->ControlSeconds; ObjectiveConfig.TransitionSeconds=CampaignDefinition->TransitionSeconds; ObjectiveConfig.ControlRadius=CampaignDefinition->ControlRadius;
        if (Area.bRoomAnchors) { ObjectiveConfig.DeviceAnchors = Area.DeviceAnchors; ObjectiveConfig.ControlAnchors = Area.ControlAnchors; }
        else
        {
            for(const FVector& Local:CampaignDefinition->LocalDeviceAnchors) ObjectiveConfig.DeviceAnchors.Add(Area.Transform.TransformPosition(Local));
            for(const FVector& Local:CampaignDefinition->LocalControlAnchors) ObjectiveConfig.ControlAnchors.Add(Area.Transform.TransformPosition(Local));
        }
        if (CampaignDefinition->ContentRevision>=4 && ObjectiveConfig.Kind==EDemoObjectiveKind::RotatingControl)
        {
            if (ObjectiveConfig.ControlAnchors.Num()!=3)
            { CombatSession->Invalidate(); State->FailureReason=TEXT("单占点缺少作者三候选锚点"); UE_LOG(LogFPSDemo,Warning,TEXT("OBJECTIVE_PREPARE rejected control anchor catalogue")); return; }
            const FVector CaptureCenter=ObjectiveConfig.ControlAnchors[1]; // 当前房间中部作者点；每关只实例化此区域，三候选配置仍兼容旧schema1快照。
            ObjectiveConfig.ControlAnchors={CaptureCenter};
        }
    }
    if(!ObjectiveSystem->PrepareObjective(ObjectiveConfig,CombatSession,Error))
    {
        CombatSession->Invalidate(); State->FailureReason=Error;
        ObjectiveSystem->CancelObjective(); // 原安全终端尚未清除，留在原阶段可修复配置后重试。
        UE_LOG(LogFPSDemo,Warning,TEXT("OBJECTIVE_PREPARE blocked %s"),*Error); return;
    }
    if(UsesDemoCampaign())
        for(ADemoCharacter* Member:CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
            if(!Member->GetPlayerState<ADemoPlayerState>()->GetRunBuildComponent()->PrepareEncounter(Error))
            {
                CombatSession->Invalidate(); ObjectiveSystem->CancelObjective(); State->FailureReason=Error;
                UE_LOG(LogFPSDemo,Warning,TEXT("CORE_PREPARE blocked %s"),*Error); return; // 保留旧安全阶段及交互终端，不锁定任何核心。
            }
    if (!CastChecked<AFPSDemoGameMode>(GetOwner())->GetPreparationSystem()->Consume())
    {
        CombatSession->Invalidate(); ObjectiveSystem->CancelObjective(); State->FailureReason = TEXT("房间提交条件失效，请重新准备");
        CastChecked<AFPSDemoGameMode>(GetOwner())->GetPreparationSystem()->Cancel(State->FailureReason); return;
    } // 全部配置/GAS/目标预检成功才消费，保存失败仍保留已加载资源供重试。
    ClearTerminals();
    State->FailureReason.Empty(); State->LevelNumber=NextLevel;
    if(UsesDemoCampaign()) State->CampaignPlan=PreparedCampaign;
    else State->CampaignPlan=FDemoCampaignPlan();
    SetPhase(EDemoPhase::Combat);
    if(UsesDemoCampaign())
        for(ADemoCharacter* Member:CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
            {
                UDemoRunBuildComponent* Build=Member->GetPlayerState<ADemoPlayerState>()->GetRunBuildComponent(); // 依赖失败不能静默带空行为进入战斗。
                Build->BeginEncounter();
                if(!Build->IsEncounterEnabled()) { FailRun(TEXT("核心GAS行为初始化失败")); return; }
            }
    ObjectiveSystem->UpdatePopulation(Plan.GetTotalCount(),0);
    if(!ObjectiveSystem->ActivateObjective()) { FailRun(TEXT("目标激活失败")); return; }
	Player->StopFiring();
	Player->GetCharacterMovement()->StopMovementImmediately();
	// 入场在竞技场左侧，敌人围绕中心分布，避免进入即贴脸。
	// 本关玩家与怪物共享同一快照，区域旋转不会只影响其中一方。
	int32 MemberIndex = 0; // 固定小队入场横向间隔120cm，防止四个胶囊完全重合。
    for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
    {
        Member->StopFiring(); Member->GetCharacterMovement()->StopMovementImmediately();
        Member->SetActorLocation(Area.GetCombatLandingPoint(MemberIndex++), false, nullptr, ETeleportType::TeleportPhysics);
        if (ADemoPlayerController* Owner = Cast<ADemoPlayerController>(Member->GetController())) { Owner->SetViewTarget(Member); Owner->ClientSetViewTarget(Member); }
    }
	if (Player->GetController()) Player->GetController()->SetControlRotation(FRotator::ZeroRotator);
	// 两种玩法提交同一执行器；生成/计数/重试/失败均由组件处理，GameMode只推进关卡。
	if (!SpawnSystem->StartEncounter(Plan, Area.Transform, Area.Geometry, Error)) { FailRun(Error); return; }
    // 只有实际开战才给各参战者装填；准备重试、读档落在备战点以及商店交互均不会消费备弹。
    for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers(true))
        Member->GetWeaponComponent()->RefillAll(/*bFromReserveOnly=*/true);
	// 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
	UE_LOG(LogFPSDemo, Display, TEXT("LEVEL START %d monsterLevel=%d arena=%d minions=%d boss=%d"), State->LevelNumber, Level.MonsterLevel, Level.ArenaIndex, Level.EnemyCount, !Level.BossRow.IsNone());
}
void UDemoRunFlowComponent::HandleSpawnRemaining(int32 Remaining)
{
	DEMO_LOG_CALL();
	if(ObjectiveSystem && !bEndingEncounter) ObjectiveSystem->UpdatePopulation(FMath::Max(0,Remaining-SpawnSystem->GetAliveCount()),SpawnSystem->GetAliveCount());
    if (ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>()) State->EnemiesRemaining = Remaining; // HUD唯一读取GameState，不访问组件内部队列。
}
void UDemoRunFlowComponent::FinishLevel()
{
	DEMO_LOG_CALL();
    if (bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW command rejected during shutdown")); return; }
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!State || State->Phase != EDemoPhase::Combat || !bEndingEncounter || !Player) return;
	TerminalSystem->CaptureVictoryPlacementOrigin(); // 在复活/补给队友之前冻结真实存活参战者胜利位置，由终端系统负责本图落地与成对摆放。
	Player->StopFiring();
	for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers()) // 清场后全队停止旧攻击并复活阵亡者。
    { Member->StopFiring(); Member->GetDemoASC()->CancelAllAbilities(); RefillPlayer(Member); }
	FDemoLevelRow Completed; // 本次真实清关配置；中途关卡不再发金币。
	FDemoDifficultyRow CompletedDifficulty; // 最终胜利金币来自当前锁定难度的显式奖励，不乘属性倍率。
	FString ClearError; // 配置错误不可假装正常通关发奖。
	if (!GetLevelConfig(State->LevelNumber, Completed, CompletedDifficulty, ClearError)) { FailRun(ClearError); return; }
	if (State->bEndless)
	{
		State->BestEndlessLevel = FMath::Max(State->BestEndlessLevel,State->LevelNumber);
        for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers()) // 每人纪录跟随个人快照。
        { UDemoPlayerProgressComponent* Progress = Member->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); /* PS 拥有。 */ FDemoPlayerProgressSnapshot Next = Progress->GetSnapshot(); /* 同步值提交。 */ Next.BestEndlessLevel=FMath::Max(Next.BestEndlessLevel,State->LevelNumber); Progress->Commit(Next); }
		UE_LOG(LogFPSDemo,Log,TEXT("ENDLESS_CLEARED level=%d best=%d"),State->LevelNumber,State->BestEndlessLevel);
	}
	if (!State->bEndless && State->LevelNumber == DemoCombatConfig::LevelCount)
	{
		if (!RewardSystem->GrantVictory(CampaignRunId, CompletedDifficulty)) { FailRun(TEXT("Victory reward rejected")); return; }
		SetPhase(EDemoPhase::Victory); // 奖励服务负责幂等发放，玩法层再推进阶段/重置临时成长/保存。
		RewardSystem->RecordVictoryUnlocks(CampaignRunId); // Profile只接受真实Victory；保持原解锁时序与资格判定。
        CaptureRunSummaries(EDemoRunOutcome::Victory, FString()); // 必须先于清临时成长；金币使用实际到账、核心分支/资格保持结束前值。
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
		Player->GetCharacterMovement()->StopMovementImmediately();
		// 清关已经由目标系统确认；即使本帧摆不下终端，也必须推进关号对应的奖励检查点。
		const bool bTerminalsReady=SpawnTerminals(Completed.ArenaIndex);
		if(UsesDemoCampaign() && (State->LevelNumber==3 || State->LevelNumber==7))
            for(ADemoCharacter* Member:CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
                Member->GetPlayerState<ADemoPlayerState>()->GetRunBuildComponent()->OfferBranch(State->LevelNumber);
        SetPhase(EDemoPhase::Reward);
        if (!bTerminalsReady)
        {
            const FTimerDelegate Retry=FTimerDelegate::CreateUObject(this,&UDemoRunFlowComponent::RetryClearedTerminals,State->LevelNumber,Completed.ArenaIndex); // 按值绑定本关，服务器游戏秒计时。
            GetWorld()->GetTimerManager().SetTimer(TerminalRetryTimer,Retry,3.f,true);
            UE_LOG(LogFPSDemo,Warning,TEXT("TERMINAL_RETRY scheduled clearedLevel=%d arena=%d"),State->LevelNumber,Completed.ArenaIndex);
        }
		RefillPlayer(Player); // 清关前已全队复活补给；保留兼容调用幂等。
		SaveCheckpoint(); // 奖励未选也保存，恢复后仍需领取一次。
		if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(Player->GetController())) PC->OpenUpgradeMenu(true);
	}
}
void UDemoRunFlowComponent::RetryClearedTerminals(int32 ClearedLevel,int32 AreaIndex)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 验证原关卡仍在备战；已经离场的回调不能重建终端。
    if (bStopping || !State || State->LevelNumber!=ClearedLevel || (State->Phase!=EDemoPhase::Reward && State->Phase!=EDemoPhase::Intermission)
        || (IsValid(TerminalSystem->GetShopTerminal()) && IsValid(TerminalSystem->GetNextLevelTerminal())))
    { GetWorld()->GetTimerManager().ClearTimer(TerminalRetryTimer); return; }
    TerminalSystem->CaptureVictoryPlacementOrigin(true);
    if (SpawnTerminals(AreaIndex))
    {
        GetWorld()->GetTimerManager().ClearTimer(TerminalRetryTimer);
        UE_LOG(LogFPSDemo,Log,TEXT("TERMINAL_RETRY recovered level=%d"),ClearedLevel);
    }
}
bool UDemoRunFlowComponent::ChooseReward(int32 Choice)
{ DEMO_LOG_CALL(); return ChooseReward(Choice, Avatar.Get()); }
bool UDemoRunFlowComponent::ChooseReward(int32 Choice, ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    if(bStopping || !Player) return false;
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 本人领取当关一次奖励。
    ADemoPlayerState* PS=Player->GetPlayerState<ADemoPlayerState>();
    if(!PS||!State||State->Phase!=EDemoPhase::Reward) return false;
    UDemoRunBuildComponent* Build=PS->GetRunBuildComponent(); // 核心和旧奖励共用当关领取门禁。
    if(UsesDemoCampaign() && (State->LevelNumber==3 || State->LevelNumber==7))
    {
        if(Choice<0||Choice>1||PS->GetProgressComponent()->GetSnapshot().LastRewardLevel>=State->LevelNumber) return false;
        const FName Branch(*FString::Printf(TEXT("%s-%c"),*Build->GetSnapshot().CoreId.ToString(),(State->LevelNumber==3?'A':'C')+Choice)); // 固定互斥组，客户端不能提交任意天赋ID。
        const FDemoRunBuildSnapshot PreviousBuild=Build->GetSnapshot(); // 两个组件同栈提交，账本拒绝时必须回滚核心，避免有分支但无法出发。
        if(!Build->TrySelectBranch(Branch)) return false;
        FDemoPlayerProgressSnapshot Progress=PS->GetProgressComponent()->GetSnapshot(); // 只推进此人的领取标记，不另送免费成长。
        Progress.LastRewardLevel=State->LevelNumber;
        if(!PS->GetProgressComponent()->Commit(Progress))
        { Build->RestoreSnapshot(PreviousBuild); UE_LOG(LogFPSDemo,Warning,TEXT("BRANCH_TRANSACTION rolled back: progress rejected")); return false; }
    }
    else if(!RewardSystem->GrantAbilityChoice(CampaignRunId, Choice, Player)) return false;
    ReconcileRewardReadiness();
    if (!SaveCheckpoint()) UE_LOG(LogFPSDemo, Warning, TEXT("REWARD choice applied; checkpoint pending"));
    return true;
}
void UDemoRunFlowComponent::ReconcileRewardReadiness()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 离线队员不阻塞仍在线队伍；未领取的离线奖励不补发。
    if (bStopping || !State || State->Phase != EDemoPhase::Reward) return;
    bool bAllSelected = true; // 所有当前连接成员完成选择才统一进入备战。
    for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
        bAllSelected &= Member->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot().LastRewardLevel >= State->LevelNumber;
    if (bAllSelected) SetPhase(EDemoPhase::Intermission);
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
    CaptureRunSummaries(EDemoRunOutcome::Eliminated, TEXT("队伍全员阵亡")); // 失败关不计已完成；个人阵亡观战不会走本结束入口。
	ResetCompletedRunGrowth(); // 全员失败才结束团队挑战，逐人清银币/临时成长，永久金币与解锁保留。
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
    CaptureRunSummaries(EDemoRunOutcome::SystemError, Reason); // 已有团灭战报不会被覆盖；纯系统异常不谎称成长已清空。
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
#if WITH_EDITOR
        const UDemoRunSave* Checkpoint=Saves->GetSlot(Saves->GetActiveSlot()); // 同轮系统异常只向本次PIE的GI交接借枪，保留原检查点和正式枪权。
        if (Checkpoint && Checkpoint->RunId==CampaignRunId && Avatar.IsValid())
            Saves->CaptureEditorRestartWeapon(*Avatar->GetWeaponComponent());
#endif
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
    if (bStopping || bRestoringCheckpoint) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW save rejected during shutdown/restore")); return false; } // 旅行前保存，旅行后不再允许旧UI覆盖GI活动槽。
    UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI拥有同步存档服务。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前安全阶段值视图。
    if (!Saves || !State) { UE_LOG(LogFPSDemo, Error, TEXT("SaveCheckpoint missing service/state")); return false; }
    // 实际死亡必须落盘清空银币/临时成长并保留永久成长；失败时禁止离开页面，系统错误仍保留上次检查点。
    if (State->Phase == EDemoPhase::Defeat && State->bReturnToHubOnRestart) return SaveResetCheckpoint(); // 同一入口保留死亡时的主枪和槽位。
    // 战斗中只重试资格元数据写入，不采集半场战利品；磁盘失败不能通过退出重进恢复手枪资格。
    if (State->Phase==EDemoPhase::Combat)
    {
        bool bSaved = true; // 只保存每人资格元数据，战斗钱包仍回到上次检查点。
        UDemoCoopRosterComponent* Roster = CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster(); // 当前服务器成员身份。
        for (ADemoCharacter* Member : Roster->GetPlayers())
        { const FDemoPlayerProgressSnapshot& Data = Member->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot(); /* 本人资格。 */ bSaved &= Saves->StorePlayerChallenge(Roster->GetPlayerKey(Member),Data.PistolChallenge,Data.BestEndlessLevel); }
        return bSaved;
    }
    if (Saves->GetActiveSlot() == INDEX_NONE || State->Phase == EDemoPhase::Lobby || State->Phase == EDemoPhase::Defeat) return true;
    const ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 本次同步借用Avatar。
    const UDemoAttributeSet* Attributes = Player ? Player->GetDemoAttributes() : nullptr; // 只存永久与当局成长的合计基础值，排除瞬时状态。
    if (!Attributes) { UE_LOG(LogFPSDemo, Error, TEXT("SaveCheckpoint missing Avatar/attributes")); return false; }
    UDemoRunSave* Snapshot = NewObject<UDemoRunSave>(this); // Store会复制值并持久化，不延长GameMode生命周期。
    if (!DemoCheckpointAssembler::Capture(CampaignRunId, *State, *Player, *Snapshot)) return false;
    Snapshot->CampaignId=UsesDemoCampaign()?ContentId:NAME_None; Snapshot->ContentRevision=UsesDemoCampaign()?ContentRevision:0; Snapshot->RunSeed=UsesDemoCampaign()?ContentSeed:0;
    Snapshot->ContentSnapshot=UsesDemoCampaign()?ContentSnapshot:FString(); Snapshot->ContentFingerprint=UsesDemoCampaign()?ContentFingerprint:FString(); // 沿用整轮冻结原字节，当前作者调参不影响保存中的旧轮。
    const UDemoRunSave* Previous = Saves->GetSlot(Saves->GetActiveSlot()); // 原合作档在单人继续时仍保留离线成员。
    UDemoCoopRosterComponent* Roster=CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster(); // 同次候选/成功后缓存提交使用同一目录，不重新采集活体值。
    const bool bCaptureMembers=GetWorld()->GetNetMode()==NM_ListenServer || (Previous && !Previous->TeamMembers.IsEmpty()); // 旧合作槽单人继续也保离线成员。
    if (bCaptureMembers && (!Roster || !Roster->Capture(Snapshot->TeamMembers,false))) return false; // 仅准备，不发布重连缓存。
    if (!Saves->Store(Snapshot)) return false; // 失败保留旧SavedMembers，外层可安全回滚本次个人候选。
    if (bCaptureMembers)
    {
        const UDemoRunSave* Committed=Saves->GetSlot(Saves->GetActiveSlot()); // 读取Store实际提交的完整同次成员值，不能再读当前PS。
        if (Committed) Roster->PublishSavedMembers(Committed->TeamMembers);
        else if (Saves->GetActiveSlot()==INDEX_NONE)
        {
            Roster->PublishSavedMembers(Snapshot->TeamMembers); // 原无槽开发World的明确内存提交，不作为经济耐久依据。
            UE_LOG(LogFPSDemo, Log, TEXT("CHECKPOINT cache published memory-only without active slot"));
        }
        else UE_LOG(LogFPSDemo, Error, TEXT("CHECKPOINT cache publication missing committed slot; durable success retained")); // 已落盘后不向购买者返回可退款的保存失败。
    }
    return true;
}
bool UDemoRunFlowComponent::SaveResetCheckpoint()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 服务器公共战局。
    ADemoCharacter* Player = Avatar.Get(); // 房主仅用于旧V5根字段，所有成员另以稳定键保存。
    UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI唯一磁盘写入者。
    if (bStopping || !State || !Saves || !Player || !Player->GetPlayerState<ADemoPlayerState>()) return false;
    UDemoRunSave* Fresh = NewObject<UDemoRunSave>(this); // 不先改活体属性，写盘失败不能让确认页面偷偷清空成长。
    const UDemoRunSave* Previous=Saves->GetSlot(Saves->GetActiveSlot()); // 同槽V10身份只从已提交槽读取；也用于下方离线队员保留，不从活体或新RunId推造。
    const FDemoPlayerProgressSnapshot& Personal = Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot(); // 根字段只属于房主。
    Fresh->CreatedLocal=FDateTime::Now(); Fresh->RunId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    Fresh->Difficulty=State->Difficulty; Fresh->bEndless=State->bEndless; Fresh->BestEndlessLevel=Personal.BestEndlessLevel;
    Fresh->Gambling=Personal.Gambling; // 重开前复制永久研究，ResetTemporaryGrowth只清本轮部分。
    Fresh->Ownership=Personal.Ownership; // 同槽永久枪权和幂等水位跨死亡/放弃保留；不重新按通关导入。
    if (Previous)
    {
        Fresh->SlotStorageKey=Previous->SlotStorageKey; Fresh->OwnerPlayerKey=Previous->OwnerPlayerKey; // ResetActive会采用这个候选装备；缺键会覆盖有效槽身份并被原V10校验拒绝。
        UE_LOG(LogFPSDemo,Log,TEXT("RESET_CHECKPOINT_SLOT_IDENTITY_RETAINED slot=%d"),Saves->GetActiveSlot()); // 只记录槽号，不输出玩家稳定键；不放宽快照校验。
    }
    Fresh->Coins=Personal.Coins; Fresh->UpgradeProgress=Personal.UpgradeProgress; Fresh->ResetTemporaryGrowth();
    Player->GetWeaponComponent()->CaptureLoadout(*Fresh,true); // 使用已重置的永久容量补给，主武器ID仍保留。
    UDemoCoopRosterComponent* Roster=CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster(); // 当前在线与离线成员统一收集。
    const bool bCaptureMembers=GetWorld()->GetNetMode()==NM_ListenServer || (Previous && !Previous->TeamMembers.IsEmpty()); // 重开也只在成功后发布已重置成员。
    if (bCaptureMembers)
    {
        if (!Roster || !Roster->Capture(Fresh->TeamMembers,false)) return false; // 尚未耐久的重置/金币/枪权候选不进入重连缓存。
        for (FDemoCoopPlayerCheckpoint& Member : Fresh->TeamMembers) // 结束本轮只清此人的临时来源，永久金币/属性/解锁独立保留。
        {
            Member.Build=FDemoRunBuildSnapshot(); Member.Progress.Gambling.ResetRun(); // 统一结束全队本轮策略，永久研究不变。
            Member.Progress.SilverCoins=0; Member.Progress.Kills=0; Member.Progress.PistolChallenge=0; Member.Progress.LastRewardLevel=0;
            Member.Progress.UpgradeProgress.ResetTemporary();
            Member.MaxHealth=100.f+Member.Progress.UpgradeProgress.PermanentHealth; Member.Health=Member.MaxHealth;
            Member.DamageBonus=Member.Progress.UpgradeProgress.PermanentDamage; Member.MagazineBonus=Member.Progress.UpgradeProgress.PermanentMagazine;
            Member.HealAmount=35.f; Member.DashSpeed=1300.f; Member.Loadout.MagazineBonus=Member.MagazineBonus;
            for (ADemoCharacter* Current : Roster->GetPlayers()) // 在线成员按真实武器配置补给，离线成员保留库存并在下次入场钳制。
                if (Roster->GetPlayerKey(Current)==Member.PlayerKey) Current->GetWeaponComponent()->CaptureSnapshot(Member.Loadout,true);
        }
    }
    if (!Saves->ResetActive(Fresh->Difficulty,Fresh->Coins,Fresh->UpgradeProgress,Fresh)) return false; // 保留旧SavedMembers直到整槽重开保存成功。
#if WITH_EDITOR
    Saves->CaptureEditorRestartWeapon(*Player->GetWeaponComponent()); // 单人调试借枪仅随成功的重置进入 GI 内存。
#endif
    if (bCaptureMembers)
    {
        const UDemoRunSave* Committed=Saves->GetSlot(Saves->GetActiveSlot()); // ResetActive可能规范化成员，发布实际提交值而非原始捕获值。
        if (Committed) Roster->PublishSavedMembers(Committed->TeamMembers);
        else if (Saves->GetActiveSlot()==INDEX_NONE)
        {
            Roster->PublishSavedMembers(Fresh->TeamMembers); // 兼容无槽开发World的GI内临时重开，不作为经济耐久凭据。
            UE_LOG(LogFPSDemo, Log, TEXT("RESET_CHECKPOINT cache published memory-only without active slot"));
        }
        else UE_LOG(LogFPSDemo, Error, TEXT("RESET_CHECKPOINT cache publication missing committed slot; durable success retained"));
    }
    return true;
}
bool UDemoRunFlowComponent::RestoreCheckpoint(const UDemoRunSave& Source)
{
    DEMO_LOG_CALL();
    if (bRestoringCheckpoint || !Source.Validate() || !Avatar.IsValid() || !Avatar->GetPlayerState<ADemoPlayerState>()) return false;
    TGuardValue<bool> RestoreGuard(bRestoringCheckpoint,true); // 读档期间拒绝保存及新连接，防止回调写入半份状态。
    UDemoRunSave* Prepared=DuplicateObject<UDemoRunSave>(&Source,this); // 规范化仅改内存副本，任何预检失败都保留磁盘原档。
    const bool bCompleted=Prepared->Phase==EDemoPhase::Victory; // 旧Victory检查点先转成下一轮Hub，避免提交个人状态后再遇到区域创建失败。
    if (bCompleted)
    {
        if (!GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()->SaveProfile()) return false; // 已记录的永久通关事实先确认，不重复发金币。
        Prepared->ResetTemporaryGrowth(); Prepared->SilverCoins=0; Prepared->PistolChallenge=0;
        Prepared->CompletedLevel=0; Prepared->bEndless=false; Prepared->Phase=EDemoPhase::Hub;
        Prepared->RunId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        // 库存值暂保留，在线玩家提交后使用真实武器Config补给；禁止伪造大弹药值绕过库存配置。
        for (FDemoCoopPlayerCheckpoint& Member : Prepared->TeamMembers)
        {
            Member.Build=FDemoRunBuildSnapshot(); Member.Progress.Gambling.ResetRun(); // 统一结束全队本轮策略，永久研究不变。
            Member.Progress.SilverCoins=0; Member.Progress.UpgradeProgress.ResetTemporary(); Member.Progress.PistolChallenge=0; Member.Progress.LastRewardLevel=0;
            Member.DamageBonus=Member.Progress.UpgradeProgress.PermanentDamage; Member.MagazineBonus=Member.Progress.UpgradeProgress.PermanentMagazine;
            Member.Loadout.MagazineBonus=Member.MagazineBonus; Member.MaxHealth=100.f+Member.Progress.UpgradeProgress.PermanentHealth; Member.Health=Member.MaxHealth;
            Member.HealAmount=35.f; Member.DashSpeed=1300.f;
            // 离线成员保留原库存值，重连按新的永久容量钳制，不凭空授予高额备弹。
        }
    }
    const UDemoRunSave& Data=*Prepared; // 后续所有阶段共享同一份已规范化不可变值。
    const FDemoPlayerProgressSnapshot Permission=Data.TeamMembers.IsEmpty()?Avatar->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot():Data.TeamMembers[0].Progress; // 第0项固定为房主权限。
    if (!Data.Validate() || (Data.Difficulty==EDemoDifficulty::Hell && !Permission.bHellUnlocked) || (Data.bEndless && !Permission.bEndlessUnlocked))
    { UE_LOG(LogFPSDemo,Warning,TEXT("RESTORE rejected data/leader permission")); return false; }
    FString Error,RestoredJson,RestoredFingerprint; // 所有资源/内容预检先于个人提交，失败不覆盖原档。
    const bool bNeedsNewContent=Data.Phase==EDemoPhase::Hub && !Data.bEndless && (Data.CampaignId.IsNone()||bCompleted); // 旧中途档与无尽保持原模式；旧Victory结束后新轮使用现有资产。
    UDemoCampaignDefinition* RestoredDefinition=nullptr; // 本同步事务准备的冻结内容，马上交给Flow强持有。
    if(bNeedsNewContent) RestoredDefinition=FreezeConfiguredCampaign(RestoredJson,RestoredFingerprint,Error);
    else if(!Data.CampaignId.IsNone())
    { RestoredJson=Data.ContentSnapshot; RestoredFingerprint=Data.ContentFingerprint; RestoredDefinition=UDemoCampaignDefinition::FromSnapshot(this,RestoredJson,RestoredFingerprint,Error); }
    if((bNeedsNewContent||!Data.CampaignId.IsNone()) && !RestoredDefinition)
    {
        if(ADemoGameState* FailedState=GetWorld()->GetGameState<ADemoGameState>()) FailedState->FailureReason=Error;
        UE_LOG(LogFPSDemo,Warning,TEXT("RESTORE content rejected %s"),*Error); return false;
    }
    CampaignDefinition=RestoredDefinition; // StartRun的World准备不能让新建定义丢失强引用，恢复期间不触碰作者资产。
    if (!StartRun()) return false; // 准备World安全区，不加载个人经济和库存。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 只在此同步事务借用。
    ADemoCharacter* Player=Avatar.Get(); // 当前房主Avatar，原状态保持到个人事务开始。
    const EDemoDifficulty PreviousDifficulty=State->Difficulty; // 公共字段预检失败也必须还原。
    const bool bPreviousEndless=State->bEndless; // 不把失败检查点的模式泄漏到下次新档。
    const int32 PreviousLevel=State->LevelNumber; // StartRun的新Hub通常为0，但仍显式备份。
    const FString PreviousRunId=CampaignRunId; // 挑战凭据和公开阶段一并恢复。
    FDemoAreaSnapshot Area; // 在应用任何玩家状态前解析出生区域并创建终端。
    CommitCampaignContent(RestoredDefinition,RestoredJson,RestoredFingerprint,bNeedsNewContent?static_cast<int32>(GetTypeHash(Data.RunId)&MAX_int32):Data.RunSeed);
    State->Difficulty=Data.Difficulty; State->bEndless=Data.bEndless; // 区域解析需要模式，关号在个人事务成功后才发布。
    bool bReady=true; // 所有可失败World准备必须先于整队提交。
    FDemoCampaignPlan RestoredPlan; // 由已保存seed重建公开计划，不保存或恢复半场目标Actor。
    if(UsesDemoCampaign() && Data.CompletedLevel>0)
        bReady=CampaignDefinition && CampaignDefinition->BuildPlan(ContentSeed,Data.CompletedLevel,FMath::Clamp(Data.TeamMembers.Num(),1,4),RestoredPlan,Error);
    if (Data.Phase==EDemoPhase::Reward || Data.Phase==EDemoPhase::Intermission)
    {
        FDemoLevelRow Level; FDemoDifficultyRow Difficulty; // 已完成关卡对应的权威区域配置。
        const UDemoRoomStreamingSettings* RoomSettings = GetDefault<UDemoRoomStreamingSettings>(); // 同新轮出发门禁，V9旧修订不改变恢复区域。
        const bool bStreamedCheckpoint = UsesDemoCampaign() && RoomSettings->bEnabled && ContentRevision >= RoomSettings->MinimumContentRevision; // 实例名不写存档；关间恢复到常驻安全备战点，关号/奖励/个人数据保持原值。
        bReady=bReady && GetLevelConfig(Data.CompletedLevel,Level,Difficulty,Error)
            && Areas->ResolveArea(bStreamedCheckpoint ? -1 : Level.ArenaIndex,Area,Error) && SpawnTerminals(bStreamedCheckpoint ? -1 : Level.ArenaIndex);
    }
    else bReady=bReady && Areas->ResolveArea(-1,Area,Error);
    if (bReady) bReady=ChallengeSystem->RestoreProgress(Data.RunId,Data.PistolChallenge); // 凭据预检失败尚未修改任何个人值。
    if (bReady)
    {
        if (Data.TeamMembers.IsEmpty()) bReady=DemoCheckpointAssembler::Apply(Data,*State,*Player,Error); // 单人兼容适配器内部有完整库存事务。
        else bReady=CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->Restore(Data.TeamMembers); // 合作直接用成员数据，不先重复套用房主根字段。
    }
    if (!bReady)
    {
        State->Difficulty=PreviousDifficulty; State->bEndless=bPreviousEndless; State->LevelNumber=PreviousLevel;
        ChallengeSystem->BeginRun(PreviousRunId); CommitCampaignContent(nullptr,FString(),FString(),0); ClearTerminals(); SetPhase(EDemoPhase::Lobby);
        State->FailureReason=Error.IsEmpty()?TEXT("检查点恢复失败，原数据已保留；请检查资源配置"):Error;
        UE_LOG(LogFPSDemo,Warning,TEXT("RESTORE aborted: %s"),*State->FailureReason); return false;
    }
    // 从这里开始只有不分配关键资源的提交/投影；不会再因区域缺失而留下半队伍恢复。
    CampaignRunId=Data.RunId;
    State->CampaignPlan=FDemoCampaignPlan(); State->CampaignPlan.CampaignId=ContentId; State->CampaignPlan.ContentRevision=ContentRevision; State->CampaignPlan.RunSeed=ContentSeed;
    if(RestoredPlan.Stage>0) State->CampaignPlan=RestoredPlan; // 已清关页面保持同一房间/目标身份，下次出发重新冻结在线参战人数。
    State->LevelNumber=Data.CompletedLevel; State->BestEndlessLevel=Data.BestEndlessLevel; State->FailureReason.Empty();
    State->EnemiesRemaining=0; State->bReturnToHubOnRestart=false;
    int32 MemberIndex=0; // 安全区/关间按成员横向错位，避免四个胶囊重叠。
    for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
    {
        Member->SetActorLocation(Area.PreparedStart+FVector(0,120.f*MemberIndex++,0),false,nullptr,ETeleportType::TeleportPhysics);
        Member->GetCharacterMovement()->StopMovementImmediately(); Member->SetCoopEliminated(false);
        if (bCompleted) RefillPlayer(Member); // 旧Victory兼容与正常通关共用武器自身的容量/备弹配置。
    }
    SetPhase(Data.Phase); ReconcileRewardReadiness();
#if WITH_EDITOR
    GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>()->RestoreEditorRestartWeapon(Data.RunId,*Player->GetWeaponComponent()); // 同GI、同轮次且单机才消费；奖励/关间检查点也可恢复借枪。
#endif
    CastChecked<AFPSDemoGameMode>(GetOwner())->GetTeamReadySystem()->InvalidateAll(TEXT("检查点已恢复，准备状态需要重新确认"));
    if (ADemoPlayerController* PC=Cast<ADemoPlayerController>(Player->GetController())) PC->OnRunReady(); // 输入只有在事务成功后开放。
    bRestoringCheckpoint=false; // 此时才允许规范化后的旧Victory检查点写盘；Guard退出仍恢复原false。
    if (bCompleted && !SaveCheckpoint()) UE_LOG(LogFPSDemo,Warning,TEXT("RESTORE complete; normalized checkpoint save pending"));
    UE_LOG(LogFPSDemo,Display,TEXT("RESTORE committed phase=%d level=%d team=%d"),static_cast<int32>(State->Phase),State->LevelNumber,Data.TeamMembers.Num()); return true;
}
void UDemoRunFlowComponent::ResetCompletedRunGrowth()
{
    DEMO_LOG_CALL();
    CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->ResetTemporaryMembers(); // 同样撤销离线成员的上轮临时来源。
    for (ADemoCharacter* Player : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers()) // 每人永久成长单独保留，不能将房主基线复制给全队。
    {
        UDemoPlayerProgressComponent* Progress = Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 当前个人永久来源。
        // 先撤销本轮核心/天赋，再恢复真实永久基线；不能构造缺少V10槽身份/枪权的临时SaveGame来调用严格检查点恢复。
        Player->GetPlayerState<ADemoPlayerState>()->GetRunBuildComponent()->ResetRun();
        Progress->ResetTemporary();
        if (!Player->ResetTemporaryRunAttributes())
        { UE_LOG(LogFPSDemo,Error,TEXT("RUN_GROWTH_RESET_FAILED player=%s; permanent attributes were not applied"),*GetNameSafe(Player)); continue; }
        RefillPlayer(Player); // 只在基线已恢复后按永久上限补满，不能把残留局内生命当成新永久值保存。
    }
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
    FString NextJson,NextFingerprint,ContentError; // 下一轮资产预检必须先于清成长/改RunId，失败留原胜利页与检查点。
    UDemoCampaignDefinition* NextDefinition=FreezeConfiguredCampaign(NextJson,NextFingerprint,ContentError);
    if(!NextDefinition) { State->FailureReason=ContentError; return false; }
    // 必须先提交旧战役凭据再创建新GUID；旧Victory文件反复读入不能产生重复解锁记录。
    if (!Profile->SaveProfile())
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
    CommitCampaignContent(NextDefinition,NextJson,NextFingerprint,static_cast<int32>(GetTypeHash(CampaignRunId)&MAX_int32));
    SpawnSystem->CancelEncounter(false); // 通关回Hub清理本轮组件状态，不再由GameMode保留推进Timer。
    SetPhase(EDemoPhase::Hub);
    int32 MemberIndex = 0; // 在同一个安全区给每名成员不同站位。
    for (ADemoCharacter* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
    { PlacePlayerInHub(Member, false); Member->AddActorWorldOffset(FVector(0,120.f*MemberIndex++,0)); }
    GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>()->ReleaseRooms(); // 全队服务器位置已在常驻Hub；本地/远端也等脚下旧地面不再需要才退役。
    for (ADemoPlayerController* Member : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetControllers()) Member->ClientResetEncounterRooms();
    UGameplayStatics::SetGamePaused(this, false); // 也允许从Victory上的Esc暂停页返回安全区。
    PC->OnRunReady();
    CastChecked<AFPSDemoGameMode>(GetOwner())->GetTeamReadySystem()->InvalidateAll(TEXT("已返回安全区，请为新挑战重新准备"));
    // 保存失败仍保留可操作的Hub和内存金币/解锁；HUD显示错误，下一次出发会先重试保存，旧Victory档也仍可继续。
    if (!SaveCheckpoint()) UE_LOG(LogFPSDemo, Warning, TEXT("VICTORY_CONTINUE hub checkpoint pending; progress retained in memory"));
    // 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
    UE_LOG(LogFPSDemo, Display, TEXT("VICTORY_CONTINUE Hub players=%d nextRun=%s"), CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers().Num(), *CampaignRunId);
    return true;
}
bool UDemoRunFlowComponent::HasRunUpgrades() const
{
    DEMO_LOG_TICK();
    for (ADemoCharacter* Player : CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers()) // 全队返回会结束每个人本轮，确认必须覆盖队友临时收益。
    {
        const UDemoAttributeSet* Attributes=Player->GetDemoAttributes(); // 当前合计战斗值。
        const FDemoUpgradeProgress& Permanent=Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot().UpgradeProgress; // 此人永久来源。
        if(Player->GetPlayerState<ADemoPlayerState>()->GetRunBuildComponent()->GetSnapshot().bLocked || !Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot().Gambling.Talents.IsEmpty()) return true; // 仅有天赋也要求放弃成长确认。
        if (Attributes && (Attributes->GetWeaponDamageBonus()>Permanent.PermanentDamage || Attributes->GetMagazineBonus()>Permanent.PermanentMagazine
            || Attributes->GetMaxHealth()>100.f+Permanent.PermanentHealth || Player->GetHealAmount()>35.f || Player->GetDashSpeed()>1300.f)) return true;
    }
    return false;
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
    if (!bResume && GetWorld()->GetNetMode() == NM_ListenServer)
        GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->Leave(); // 房主回大厅关闭房间；检查点已由命令入口保存，队友收到断线提示。
    else if (GetWorld()->GetNetMode() == NM_ListenServer)
        GetWorld()->ServerTravel(UGameplayStatics::GetCurrentLevelName(this,true) + (bResume ? TEXT("?listen?ResumeRun=1") : TEXT("?listen")), false);
    else UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this,true)), true, bResume ? TEXT("ResumeRun=1") : TEXT(""));
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
    const ADemoPlayerState* LeaderState=Avatar.IsValid()?Avatar->GetPlayerState<ADemoPlayerState>():nullptr; // 房主个人运行时准入权限，不查询其他玩家GI。
    FDemoEndlessRow Config; FString Error; // 先验证配置再提交选择，避免进入不可生成的模式。
    if (!State || State->Phase!=EDemoPhase::Hub || State->LevelNumber!=0 || !LeaderState || !LeaderState->GetProgressComponent()->GetSnapshot().bEndlessUnlocked || !GetEndlessConfig(Config,Error))
    { UE_LOG(LogFPSDemo,Warning,TEXT("ENDLESS select rejected: locked/phase/config %s"),*Error); return false; }
    if(State->bEndless) return true; // 重复选择已生效模式不清已有准备，也不重复重置核心修订。
    State->bEndless=true; State->Difficulty=EDemoDifficulty::Hell;
    CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->ResetBuilds(); // 离线成员同样退出新核心，不清其他个人数据。
    CommitCampaignContent(nullptr,FString(),FString(),0); // 旧无尽模式不能携带新战役快照。
    CastChecked<AFPSDemoGameMode>(GetOwner())->GetTeamReadySystem()->InvalidateAll(TEXT("已切换无尽模式，请重新准备"));
    UE_LOG(LogFPSDemo,Log,TEXT("ENDLESS selected best=%d"),State->BestEndlessLevel);
    return true;
}
UDemoCombatSession* UDemoRunFlowComponent::GetCombatSession() const { DEMO_LOG_TICK(); return CombatSession; }
