// 职责：显式服务初始化、就绪门禁与幂等退出。依赖同GM目标/生成服务，不靠BeginPlay先后隐式开战。
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Game/Preparation/DemoEncounterPreparationComponent.h"
#include "Game/Objectives/DemoObjectiveCoordinatorComponent.h"
#include "Game/Content/DemoCampaignDefinition.h"
#include "TimerManager.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Economy/DemoRewardComponent.h"
#include "Economy/DemoShopComponent.h"
#include "Interaction/DemoTerminalComponent.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Progression/DemoChallengeComponent.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"

UDemoRunFlowComponent::UDemoRunFlowComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
void UDemoRunFlowComponent::InitializeServices(UDataTable* Enemies, UDataTable* Difficulties, UDataTable* Levels, UDataTable* Endless,
    UDemoEnemySpawnComponent* Spawn, UDemoRewardComponent* Reward, UDemoShopComponent* Shop, UDemoTerminalComponent* Terminal, UDemoChallengeComponent* Challenge)
{
    DEMO_LOG_CALL();
    if (bInitialized || bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW_INIT duplicate/stopped ignored")); return; }
    if (!GetOwner()->HasAuthority() || !Spawn || !Reward || !Shop || !Terminal || !Challenge
        || Spawn->GetOwner() != GetOwner() || Reward->GetOwner() != GetOwner() || Shop->GetOwner() != GetOwner()
        || Terminal->GetOwner() != GetOwner() || Challenge->GetOwner() != GetOwner())
    { UE_LOG(LogFPSDemo, Error, TEXT("FLOW_INIT missing/foreign dependencies")); return; }
    EnemyTable = Enemies; DifficultyTable = Difficulties; LevelTable = Levels; EndlessTable = Endless;
    SpawnSystem = Spawn; RewardSystem = Reward; ShopSystem = Shop; TerminalSystem = Terminal; ChallengeSystem = Challenge;
    ObjectiveSystem=GetOwner()->FindComponentByClass<UDemoObjectiveCoordinatorComponent>();
    // 作者配置只在新轮显式加载；恢复已有轮次直接使用冻结快照，避免当前资产缺失或改动阻止旧档恢复。
    if(!ObjectiveSystem) { UE_LOG(LogFPSDemo,Error,TEXT("FLOW_INIT no objective service")); return; }
    ObjectiveSystem->OnSnapshotChanged.AddUObject(this,&UDemoRunFlowComponent::HandleObjectiveSnapshot);
    ObjectiveSystem->OnCompletionCandidate.AddUObject(this,&UDemoRunFlowComponent::HandleObjectiveCompleted);
    ObjectiveSystem->OnSystemError.AddUObject(this,&UDemoRunFlowComponent::HandleObjectiveError);
    Areas = GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>();
    if (!Areas.IsValid()) { UE_LOG(LogFPSDemo, Error, TEXT("FLOW_INIT no World area service")); return; }
    // 同游戏线程UObject委托；先安装完整事件图，再开放StartEncounter，结束时精确解绑。
    SpawnSystem->OnRemainingChanged.AddUObject(this, &UDemoRunFlowComponent::HandleSpawnRemaining);
    SpawnSystem->OnCleared.AddUObject(this, &UDemoRunFlowComponent::HandleSpawnerCleared);
    SpawnSystem->OnFailed.AddUObject(this, &UDemoRunFlowComponent::FailRun);
    Reward->InitializeSpawner(Spawn);
    Shop->InitializeServices(Terminal, FDemoSaveCheckpointRequest::CreateUObject(this, &UDemoRunFlowComponent::SaveCheckpoint));
    bInitialized = true;
}
void UDemoRunFlowComponent::BeginStartup(const FString& Options)
{
    DEMO_LOG_CALL();
    if (bStartupRequested || bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("FLOW_STARTUP duplicate/stopped ignored")); return; }
    bStartupRequested = true;
    SetPhase(EDemoPhase::Lobby);
    if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0))) PC->ShowLobby(); // UI可先显示，业务仍等待就绪。
    bStartInHub = UGameplayStatics::HasOption(Options, TEXT("ReturnToHub"));
    const int32 DifficultyIndex = UGameplayStatics::GetIntOption(Options, TEXT("RetryDifficulty"), static_cast<int32>(EDemoDifficulty::Normal)); // URL四档范围0..3。
    RetryDifficulty = DifficultyIndex >= 0 && DifficultyIndex <= 3 ? static_cast<EDemoDifficulty>(DifficultyIndex) : EDemoDifficulty::Normal;
    RetryGold = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("RetryGold"), 0), 0, MAX_int32);
    if (UGameplayStatics::HasOption(Options, TEXT("ResumeRun")))
        if (UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>()) // GI持有值快照，等待期间复制到本组件。
            if (const UDemoRunSave* Pending = Saves->ConsumePending()) PendingCheckpoint = DuplicateObject<UDemoRunSave>(Pending, this);
    StartupDeadline = FPlatformTime::Seconds() + 30.0;
    BindAvatar(Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)));
    if (!TryCompleteStartup()) StartupTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDemoRunFlowComponent::PollStartup), .1f);
}
void UDemoRunFlowComponent::BindAvatar(ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    if (bStopping) return;
    if (Avatar.Get() != Player)
    {
        if (Avatar.IsValid()) Avatar->OnDemoAvatarReady.Remove(AvatarReadyHandle);
        Avatar = Player; AvatarReadyHandle.Reset();
        if (Avatar.IsValid()) AvatarReadyHandle = Avatar->OnDemoAvatarReady.AddUObject(this, &UDemoRunFlowComponent::HandleAvatarReady);
    }
    HandleAvatarReady(); // 订阅后查询已完成状态，不能等待一个已经广播过的事件。
}
bool UDemoRunFlowComponent::IsPlayerReady() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Player = Avatar.Get(); // 当前Avatar弱引用，绝不借用旧World的Pawn。
    return bInitialized && !bStopping && Player && Player == UGameplayStatics::GetPlayerPawn(this, 0)
        && Player->GetDemoASC() && Player->GetDemoASC()->GetAvatarActor() == Player && Player->GetDemoAttributes()
        && Player->GetWeaponComponent() && Player->GetWeaponComponent()->GetActiveWeapon() && Player->GetController();
}
void UDemoRunFlowComponent::HandleAvatarReady() { DEMO_LOG_CALL(); if (bStartupRequested && !bStartupComplete) TryCompleteStartup(); }
bool UDemoRunFlowComponent::TryCompleteStartup()
{
    DEMO_LOG_CALL();
    if (bStartupComplete || bStopping) return true;
    if (!bStartupRequested || !IsPlayerReady()) return false;
    bStartupComplete = true; // 先消费启动意图，恢复中的属性/菜单同步回调不得再次恢复。
    // 房主就绪后先登记本地权限与稳定键，再恢复装备；PC握手晚到会幂等复用这一身份。
    CastChecked<AFPSDemoGameMode>(GetOwner())->RegisterCoopPlayer(CastChecked<ADemoPlayerController>(Avatar->GetController()),
        GetWorld()->GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->GetLocalPlayerKey());
    bool bSuccess = true; // 大厅无需开启战斗，只有恢复/重开需要创建区域和终端。
    if (PendingCheckpoint) { bSuccess = RestoreCheckpoint(*PendingCheckpoint); PendingCheckpoint = nullptr; }
    else if (bStartInHub)
    {
        bSuccess = StartRun();
        if (bSuccess)
        {
            SelectDifficulty(RetryDifficulty); GetWorld()->GetGameState<ADemoGameState>()->Coins = RetryGold;
            CastChecked<ADemoPlayerController>(Avatar->GetController())->OnRunReady();
        }
    }
    if (!bSuccess)
    {
        ClearTerminals(); SetPhase(EDemoPhase::Lobby);
        CastChecked<ADemoPlayerController>(Avatar->GetController())->ShowLobby();
        UE_LOG(LogFPSDemo, Warning, TEXT("FLOW_STARTUP failed; original checkpoint retained"));
    }
    else UE_LOG(LogFPSDemo, Display, TEXT("FLOW_READY world=%s restoredPhase=%d"), *GetWorld()->GetName(), static_cast<int32>(GetWorld()->GetGameState<ADemoGameState>()->Phase));
    return true;
}
bool UDemoRunFlowComponent::PollStartup(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (TryCompleteStartup()) { StartupTicker.Reset(); return false; }
    if (FPlatformTime::Seconds() < StartupDeadline) return true;
    bStartupComplete = true; PendingCheckpoint = nullptr; StartupTicker.Reset();
    if (ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>()) State->FailureReason = TEXT("玩家或玩法依赖未就绪，请重新选择存档"); // 保留大厅与原文件，不刷屏等待。
    UE_LOG(LogFPSDemo, Error, TEXT("FLOW_STARTUP timeout initialized=%d avatar=%d; no checkpoint overwritten"), bInitialized, Avatar.IsValid());
    return false;
}
bool UDemoRunFlowComponent::SpawnTerminals(int32 AreaIndex)
{
    DEMO_LOG_CALL();
    FDemoAreaSnapshot Area; FString Error; // 场景快照与错误只在本次创建期间借用。
    return !bStopping && Areas.IsValid() && Areas->ResolveArea(AreaIndex, Area, Error) && TerminalSystem->CreateTerminals(Area, AreaIndex == INDEX_NONE); // 读档也按物理Hub选外观，此时逻辑阶段可能仍为关间。
}
void UDemoRunFlowComponent::ClearTerminals() { DEMO_LOG_CALL(); if (TerminalSystem) TerminalSystem->ClearTerminals(); }
FVector UDemoRunFlowComponent::GetAreaCenter(int32 Index) const { DEMO_LOG_TICK(); return Areas.IsValid() ? Areas->GetAreaCenter(Index) : FVector::ZeroVector; }
const FString& UDemoRunFlowComponent::GetRunId() const { DEMO_LOG_TICK(); return CampaignRunId; }
void UDemoRunFlowComponent::Shutdown()
{
    DEMO_LOG_CALL();
    if (bStopping) return;
    bStopping = true; PendingCheckpoint = nullptr;
    CastChecked<AFPSDemoGameMode>(GetOwner())->GetPreparationSystem()->Cancel(TEXT("已停止当前战局的资源准备")); // 旅行先撤销加载等待，再清其他服务；不会由旧ACK重入开战。
    GetWorld()->GetTimerManager().ClearTimer(OutcomeTimer); bObjectiveCandidate=false;
    GetWorld()->GetTimerManager().ClearTimer(TerminalRetryTimer);
    if(ObjectiveSystem)
    {
        ObjectiveSystem->OnSnapshotChanged.RemoveAll(this); ObjectiveSystem->OnCompletionCandidate.RemoveAll(this); ObjectiveSystem->OnSystemError.RemoveAll(this);
        ObjectiveSystem->CancelObjective();
    }
    if (CombatSession) CombatSession->Invalidate(); // 旅行撤销先于子弹Destroy与Timer清理，旧伤害回调同步失效。
    // 已注册的武器、敌人和弹丸由会话统一撤销；World卸载负责最终Actor释放。
    if (StartupTicker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(StartupTicker); StartupTicker.Reset(); }
    if (Avatar.IsValid()) Avatar->OnDemoAvatarReady.Remove(AvatarReadyHandle);
    Avatar.Reset(); AvatarReadyHandle.Reset();
    if (ChallengeSystem) ChallengeSystem->Stop();
    if (ShopSystem) ShopSystem->Shutdown(); // 先禁止交易，随后清理终端和刷怪事件。
    if (SpawnSystem)
    {
        SpawnSystem->OnRemainingChanged.RemoveAll(this); SpawnSystem->OnCleared.RemoveAll(this); SpawnSystem->OnFailed.RemoveAll(this);
        SpawnSystem->CancelEncounter(false); // 不在死亡GAS广播栈内销毁ASC。
    }
    if (TerminalSystem) TerminalSystem->Shutdown(); // 停止后不能通过旧服务引用重新创建终端。
    Areas.Reset();
}
bool UDemoRunFlowComponent::CanAcceptConnections() const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 阶段只是条件之一，不能在旧World提交旅行后仍允许登录。
    return bStartupComplete && bInitialized && !bStopping && !bRestoringCheckpoint && State && State->Phase==EDemoPhase::Hub
        && !CastChecked<AFPSDemoGameMode>(GetOwner())->GetPreparationSystem()->IsPending(); // 开始冻结入场名单后暂时关闭加入，避免人数倍率与落点不一致。
}
void UDemoRunFlowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) { DEMO_LOG_CALL(); Shutdown(); Super::EndPlay(EndPlayReason); }
