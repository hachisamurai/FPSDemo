#include "Game/FPSDemoGameMode.h"
#include "Save/DemoRunSave.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"

bool AFPSDemoGameMode::SaveCheckpoint()
{
    DEMO_LOG_CALL();
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI拥有同步存档服务。
    const ADemoGameState* State = GetGameState<ADemoGameState>(); // 当前安全阶段值视图。
    if (!Saves || !State) { UE_LOG(LogFPSDemo, Error, TEXT("SaveCheckpoint missing service/state")); return false; }
    // 实际死亡必须落盘清空银币/临时成长并保留永久成长；失败时禁止离开页面，系统错误仍保留上次检查点。
    if (State->Phase == EDemoPhase::Defeat && State->bReturnToHubOnRestart) return Saves->ResetActive(State->Difficulty, State->Coins, State->UpgradeProgress);
    if (Saves->GetActiveSlot() == INDEX_NONE || State->Phase == EDemoPhase::Lobby || State->Phase == EDemoPhase::Combat || State->Phase == EDemoPhase::Defeat) return true;
    const ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 本次同步借用Avatar。
    const UDemoAttributeSet* Attributes = Player ? Player->GetDemoAttributes() : nullptr; // 只存永久与当局成长的合计基础值，排除瞬时状态。
    if (!Attributes) { UE_LOG(LogFPSDemo, Error, TEXT("SaveCheckpoint missing Avatar/attributes")); return false; }
    UDemoRunSave* Snapshot = NewObject<UDemoRunSave>(this); // Store会复制值并持久化，不延长GameMode生命周期。
    Snapshot->CreatedLocal = FDateTime::Now(); // Store替换为原槽创建时间，先提供合法值用于校验。
    Snapshot->RunId = CampaignRunId;
    Snapshot->Phase = State->Phase; Snapshot->Difficulty = State->Difficulty; Snapshot->CompletedLevel = State->LevelNumber;
    Snapshot->Coins = State->Coins; Snapshot->Kills = State->TotalKills; Snapshot->Purchases = State->Purchases;
    Snapshot->SilverCoins = State->SilverCoins; Snapshot->GoldPurchases = State->GoldPurchases; Snapshot->SilverPurchases = State->SilverPurchases; // 双币余额与独立价格作为同一检查点提交。
    Snapshot->UpgradeProgress = State->UpgradeProgress; // 永久来源与逐项次数必须和GAS合计原子保存。
    Snapshot->Health = Attributes->GetHealth(); Snapshot->MaxHealth = Attributes->GetMaxHealth();
    Snapshot->DamageBonus = Attributes->GetWeaponDamageBonus(); Snapshot->MagazineBonus = Attributes->GetMagazineBonus();
    Snapshot->HealAmount = Player->GetHealAmount(); Snapshot->DashSpeed = Player->GetDashSpeed();
    Player->GetWeaponComponent()->CaptureLoadout(*Snapshot);
    return Saves->Store(Snapshot);
}
bool AFPSDemoGameMode::RestoreCheckpoint(const UDemoRunSave& Data)
{
    DEMO_LOG_CALL();
    if (!Data.Validate() || !StartRun()) { UE_LOG(LogFPSDemo, Error, TEXT("RestoreCheckpoint rejected: data/config/startup failure")); return false; }
    ADemoGameState* State = GetGameState<ADemoGameState>(); // StartRun已创建新安全区和干净角色状态。
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 本World新Avatar。
    CampaignRunId = Data.RunId;
    State->Difficulty = Data.Difficulty; State->LevelNumber = Data.CompletedLevel;
    State->Coins = Data.Coins; State->TotalKills = Data.Kills; State->Purchases = Data.Purchases;
    State->SilverCoins = Data.SilverCoins; State->GoldPurchases = Data.GoldPurchases; State->SilverPurchases = Data.SilverPurchases; // GI在读盘时已完成旧格式到V3迁移。
    State->UpgradeProgress = Data.UpgradeProgress; // 读档只恢复账本，不重放金币GE，避免永久加成叠加两次。
    Player->RestoreRunProgress(Data);
    if (!Player->GetWeaponComponent()->RestoreLoadout(Data))
    { State->FailureReason = TEXT("存档武器无法恢复，检查解锁与资源；原文件未覆盖"); UE_LOG(LogFPSDemo, Error, TEXT("%s"), *State->FailureReason); SetPhase(EDemoPhase::Lobby); return false; }
    if (Data.Phase == EDemoPhase::Reward || Data.Phase == EDemoPhase::Intermission)
    {
        FDemoLevelRow Level; // 已完成关卡的竞技场配置值副本。
        FDemoDifficultyRow Difficulty; // 完整读取验证，不能直接信任存档的物理坐标。
        FString Error; // 配置错误回大厅，不覆盖原检查点。
        if (!GetLevelConfig(Data.CompletedLevel, Level, Difficulty, Error) || !SpawnTerminals(GetAreaCenter(Level.ArenaIndex)))
        { State->FailureReason = TEXT("检查点区域无法重建，请检查关卡配置"); UE_LOG(LogFPSDemo, Error, TEXT("%s %s"), *State->FailureReason, *Error); SetPhase(EDemoPhase::Lobby); return false; }
        Player->SetActorLocation(GetAreaCenter(Level.ArenaIndex)+FVector(-650,0,100), false, nullptr, ETeleportType::TeleportPhysics);
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
void AFPSDemoGameMode::ResetCompletedRunGrowth()
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetGameState<ADemoGameState>(); // 权威成长与钱包，金币保持不变。
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
bool AFPSDemoGameMode::ContinueAfterVictory()
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetGameState<ADemoGameState>(); // 本World权威状态，只有完整第十关胜利允许结算后回安全区。
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 保留现有Avatar及ASC，不创建默认角色覆盖数据。
    ADemoPlayerController* PC = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 清除结算/暂停输入状态的拥有者。
    UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 永久解锁依据已完成的旧RunId幂等补记。
    if (!State || !Player || !PC || !Profile || !bAreasReady || bRestartRequested || !ActiveEnemies.IsEmpty()
        || State->Phase != EDemoPhase::Victory || State->LevelNumber != DemoCombatConfig::LevelCount)
    { UE_LOG(LogFPSDemo, Warning, TEXT("VICTORY_CONTINUE rejected: incomplete victory/runtime/travel")); return false; }
    // 必须先提交旧战役凭据再创建新GUID；旧Victory文件反复读入不能产生重复解锁记录。
    if (!Profile->RecordVictory(CampaignRunId, State->Difficulty))
    { State->FailureReason = TEXT("通关记录未能确认，请检查日志后重试"); UE_LOG(LogFPSDemo, Error, TEXT("VICTORY_CONTINUE completion record rejected")); return false; }
    if (!SpawnTerminals(GetAreaCenter(-1)))
    { State->FailureReason = TEXT("安全区终端创建失败，请重试并检查场景碰撞"); UE_LOG(LogFPSDemo, Error, TEXT("VICTORY_CONTINUE hub terminals failed")); return false; }
    // 兼容旧Victory存档里残留的成长/银币；金币与解锁保留，当前库存按基础容量加金币永久容量重新补给。
    ResetCompletedRunGrowth();
    State->LevelNumber = 0;
    State->EnemiesRemaining = 0;
    State->bReturnToHubOnRestart = false;
    State->FailureReason.Empty();
    CampaignRunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    GetWorldTimerManager().ClearTimer(FinishLevelTimer);
    SetPhase(EDemoPhase::Hub);
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    PlacePlayerInHub(Player, false);
    UGameplayStatics::SetGamePaused(this, false); // 也允许从Victory上的Esc暂停页返回安全区。
    PC->OnRunReady();
    // 保存失败仍保留可操作的Hub和内存金币/解锁；HUD显示错误，下一次出发会先重试保存，旧Victory档也仍可继续。
    if (!SaveCheckpoint()) UE_LOG(LogFPSDemo, Warning, TEXT("VICTORY_CONTINUE hub checkpoint pending; progress retained in memory"));
    UE_LOG(LogFPSDemo, Log, TEXT("VICTORY_CONTINUE Hub coins=%d purchases=%d kills=%d nextRun=%s"), State->Coins, State->Purchases, State->TotalKills, *CampaignRunId);
    return true;
}
bool AFPSDemoGameMode::HasRunUpgrades() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 免费技能奖励也算成长，不只判断购买次数。
    const UDemoAttributeSet* Attributes = Player ? Player->GetDemoAttributes() : nullptr; // 当前属性只读借用，未初始化不视为已有成长。
    const ADemoGameState* State = GetGameState<ADemoGameState>(); // 只提示将损失的临时成长，纯永久金币升级不需要放弃确认。
    return Attributes && State && (Attributes->GetWeaponDamageBonus() > State->UpgradeProgress.PermanentDamage
        || Attributes->GetMagazineBonus() > State->UpgradeProgress.PermanentMagazine || Attributes->GetMaxHealth() > 100.f + State->UpgradeProgress.PermanentHealth
        || Player->GetHealAmount() > 35 || Player->GetDashSpeed() > 1300);
}
bool AFPSDemoGameMode::ReturnToSafeHub(bool bConfirmed)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetGameState<ADemoGameState>(); // 重新校验成长，不能信任先前UI结果。
    // 通关返回统一清空临时成长/银币、保留金币成长与武器解锁；所有返回入口共用同一规则。
    if (State && State->Phase == EDemoPhase::Victory) return ContinueAfterVictory();
    if (!State || State->Phase == EDemoPhase::Lobby || bRestartRequested || (HasRunUpgrades() && !bConfirmed)) { UE_LOG(LogFPSDemo, Warning, TEXT("ReturnToSafeHub rejected: state/travel pending/unconfirmed growth")); return false; }
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 永久解锁与本次重置完全分开。
    if (!Saves->ResetActive(State->Difficulty, State->Coins, State->UpgradeProgress)) return false;
    TravelToRun(true); // 有槽和无槽开发World统一消费GI值快照，不通过URL丢失永久属性。
    return true;
}
void AFPSDemoGameMode::TravelToRun(bool bResume)
{
    DEMO_LOG_CALL();
    if (bRestartRequested) { UE_LOG(LogFPSDemo, Log, TEXT("Travel duplicate ignored")); return; }
    bRestartRequested = true;
    UGameplayStatics::SetGamePaused(this, false); // 防止新World继承暂停意图，旧Timer随World销毁。
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this,true)), true, bResume ? TEXT("ResumeRun=1") : TEXT(""));
}
