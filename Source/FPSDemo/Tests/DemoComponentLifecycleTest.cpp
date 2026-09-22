#include "Tests/DemoEnemyAttackTest.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Interaction/DemoInteractable.h"
#include "Progression/DemoChallengeComponent.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Economy/DemoShopComponent.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"

bool ADemoEnemyAttackTest::CheckComponentLifecycles()
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 生产组件由本World的GM拥有。
    ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0)); // 只读本地输入拥有者。
    ADemoCharacter* Player = PC ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr; // 真实GAS Avatar，不替换其初始化路径。
    UDemoEncounterAreaSubsystem* Areas = GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>(); // World负责空间索引。
    if (!Check(Mode && PC && Player && Areas && Mode->GetRunFlow()->GetOwner() == Mode
        && Mode->GetTerminalSystem()->GetOwner() == Mode && Mode->GetChallengeSystem()->GetOwner() == Mode
        && PC->GetMenuFlow()->GetOwner() == PC, TEXT("component ownership follows world/run/local player lifetime"))) return false;
    if (!Check(Areas->DoesSupportWorldType(EWorldType::Game) && Areas->DoesSupportWorldType(EWorldType::PIE)
        && !Areas->DoesSupportWorldType(EWorldType::Editor) && !Areas->DoesSupportWorldType(EWorldType::EditorPreview), TEXT("area service excludes editor preview worlds"))) return false;
    const FString RunId = Mode->GetRunFlow()->GetRunId(); // 重复启动必须保持本轮身份。
    const ADemoWeaponBase* Weapon = Player->GetWeaponComponent()->GetActiveWeapon(); // 借用现有武器检查未被重复初始化替换。
    Mode->GetRunFlow()->BeginStartup(TEXT("ReturnToHub=1"));
    Mode->GetRunFlow()->BindAvatar(Player); Player->OnDemoAvatarReady.Broadcast(); // 已就绪后重复通知，不假定首次BeginPlay顺序。
    if (!Check(Mode->GetRunFlow()->GetRunId() == RunId && Player->GetWeaponComponent()->GetActiveWeapon() == Weapon
        && GetWorld()->GetGameState<ADemoGameState>()->Phase == EDemoPhase::Hub, TEXT("duplicate startup/avatar notification preserves active run and inventory"))) return false;
    FDemoAreaSnapshot Hub; FString Error; // 冻结真实安全区锚点，后续重建不依赖硬编码终端位置。
    if (!Check(Areas->ResolveArea(-1, Hub, Error), TEXT("hub snapshot available"))) return false;
    UDemoTerminalComponent* Terminals = Mode->GetTerminalSystem(); // 同步借用，下面显式改变其代数。
    PC->OnRunReady();
    Player->SetActorLocation(Terminals->GetShopTerminal()->GetActorLocation() + FVector(150, 0, 20));
    PC->OpenUpgradeMenu(false);
    const uint64 OldGeneration = PC->GetMenuTerminalGeneration(); // 保存打开窗口时版本，旧交易不得复用新Actor。
    if (!Check(PC->IsUpgradeMenuOpen() && Mode->GetShopSystem()->GetTerminalBlockReason(Player).IsEmpty(), TEXT("fresh shop interaction accepted"))) return false;
    if (!Check(Terminals->CreateTerminals(Hub) && Terminals->GetGeneration() != OldGeneration
        && !Mode->GetShopSystem()->GetTerminalBlockReason(Player).IsEmpty()
        && !Mode->GetShopSystem()->PurchaseUpgrade(0, Player), TEXT("rebuilt terminals reject stale open shop before realtime cleanup"))) return false;
    PC->GetMenuFlow()->UpdateRealtime(0.f);
    if (!Check(!PC->IsUpgradeMenuOpen(), TEXT("stale shop window releases input"))) return false;
    // 制造第一只成功、第二只无地面的真实部分失败，必须完整销毁本次成对创建物。
    FDemoAreaSnapshot Broken = Hub; // 故障仅在值副本，不修改地图或配置资产。
    Broken.NextAnchor += FVector(0, 0, 50000);
    UE_LOG(LogFPSDemo, Display, TEXT("EXPECTED_TERMINAL_FAILURE_BEGIN second anchor has no ground"));
    const bool bCreatedBrokenPair = Terminals->CreateTerminals(Broken); // 记录真实结果后再断言，不把业务调用放入可裁剪日志。
    UE_LOG(LogFPSDemo, Display, TEXT("EXPECTED_TERMINAL_FAILURE_END"));
    if (!Check(!bCreatedBrokenPair && !Terminals->GetShopTerminal() && !Terminals->GetNextLevelTerminal()
        && Terminals->CreateTerminals(Hub), TEXT("partial terminal creation rolls back and permits clean retry"))) return false;
    PC->OpenUpgradeMenu(false);
    if (!Check(PC->IsUpgradeMenuOpen() && Mode->GetShopSystem()->GetTerminalBlockReason(Player).IsEmpty(), TEXT("new terminal session accepted after retry"))) return false;
    PC->CloseUpgradeMenu();
    // 独立远方锚点验证空间旋转与卸载，不改变安全区地形。区号2只在本专项使用。
    ADemoEnemySpawnArea* Anchor = GetWorld()->SpawnActor<ADemoEnemySpawnArea>(FVector(40000, 0, 10000), FRotator(0, 90, 0)); // World持有，销毁即触发注销。
    if (!Check(Anchor != nullptr, TEXT("create area lifecycle fixture"))) return false;
    Anchor->ArenaIndex = 2; Areas->RegisterArea(Anchor); // 运行时测试改字段后显式重注册，实际地图在BeginPlay前配置。
    FDemoAreaSnapshot Rotated; // 无Actor引用的值快照，旧快照可安全读取但新请求不能使用卸载锚点。
    if (!Check(Areas->ResolveArea(2, Rotated, Error)
        && Rotated.PreparedStart.Equals(Anchor->GetActorTransform().TransformPosition(Anchor->PreparedPlayerOffset)), TEXT("player entry and terminal anchors use authored rotation"))) return false;
    Anchor->Destroy();
    if (!Check(!Areas->ResolveArea(2, Rotated, Error), TEXT("unloaded authored area rejects silent whitebox fallback"))) return false;
    UE_LOG(LogFPSDemo, Display, TEXT("DEMO_COMPONENT_LIFECYCLE_SUCCESS ownership, ready idempotence, terminal expiry/rollback, area unload"));
    return true;
}

bool ADemoEnemyAttackTest::CheckComponentShutdown()
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前World已完成所有刷怪回归。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 停止不能由迟到命令改变阶段。
    FDemoAreaSnapshot Hub; FString Error; // 先采集可创建的真实区域，排除无效坐标导致的伪拒绝。
    if (!Check(GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>()->ResolveArea(-1, Hub, Error), TEXT("shutdown fixture has valid hub"))) return false;
    const EDemoPhase Before = State->Phase; // Shutdown保持只读显示态，World卸载负责清场。
    Mode->GetRunFlow()->Shutdown(); Mode->GetRunFlow()->Shutdown();
    Mode->StartNextLevel(); Mode->NotifyPlayerDied();
    if (!Check(State->Phase == Before && !Mode->StartRun() && !Mode->SaveCheckpoint()
        && !Mode->GetChallengeSystem()->RestoreProgress(Mode->GetRunFlow()->GetRunId(), 1)
        && !Mode->GetTerminalSystem()->CreateTerminals(Hub) && !Mode->GetShopTerminal() && !Mode->GetNextLevelTerminal(), TEXT("shutdown is idempotent and rejects late flow/save/challenge/terminal requests"))) return false;
    UE_LOG(LogFPSDemo, Display, TEXT("DEMO_COMPONENT_SHUTDOWN_SUCCESS"));
    return true;
}
