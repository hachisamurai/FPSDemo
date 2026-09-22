#include "Tests/DemoProjectileFixtures.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Game/FPSDemoGameMode.h"
#include "Interaction/DemoInteractable.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"

int32 DemoProjectileFixtures::CountLive(UWorld* World)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectileFixtures::CountLive"));
    int32 Count=0; // 当前World内未进入Destroy的玩家弹数，用于有界等待而非只等固定一帧。
    if (!World) return Count;
    for (TActorIterator<ADemoProjectileBase> It(World);It;++It) // 迭代引用只在同步调用内使用。
        if (IsValid(*It)&&!It->IsActorBeingDestroyed()) ++Count;
    return Count;
}

bool DemoProjectileFixtures::FireAndWait(ADemoCharacter* Player,bool& bPending,float& Deadline)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectileFixtures::FireAndWait"));
    if (!bPending)
    {
        DemoHitZoneTest::Fire(Player); bPending=true; Deadline=Player->GetWorld()->GetTimeSeconds()+1.5f;
        return false; // 至少让出一帧，不能把开火调用栈内的即时扣血误判为飞行命中。
    }
    if (CountLive(Player->GetWorld())==0) { bPending=false; return true; }
    if (Player->GetWorld()->GetTimeSeconds()>Deadline)
    {
        UE_LOG(LogFPSDemo,Error,TEXT("PROJECTILE_WAIT_FAIL: player bullets did not resolve within 1.5 seconds"));
        FPlatformMisc::RequestExitWithStatus(false,1);
    }
    return false;
}

bool DemoProjectileFixtures::EquipPrimary(ADemoCharacter* Player,int32 Index)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] ProjectileFixtures::EquipPrimary index=%d"),Index);
    ADemoPlayerController* PC=Player?Cast<ADemoPlayerController>(Player->GetController()):nullptr; // 当前测试控制器，不跨World缓存。
    AFPSDemoGameMode* Mode=Player?Player->GetWorld()->GetAuthGameMode<AFPSDemoGameMode>():nullptr; // 正式终端权限入口。
    ADemoGameState* State=Player?Player->GetWorld()->GetGameState<ADemoGameState>():nullptr; // 恢复夹具阶段所需的借用状态。
    UDemoPlayerProfile* Profile=Player?Player->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>():nullptr; // 必须由专项启动参数隔离的GI档案。
    if (!PC||!Mode||!State||!Profile||!Mode->GetShopTerminal()) return false;
    const EDemoPhase PreviousPhase=State->Phase; // 终端装配结束后恢复原测试Combat阶段。
    const int32 PreviousLevel=State->LevelNumber; // 解锁测试不改变正在验证的关号。
    const EDemoDifficulty PreviousDifficulty=State->Difficulty; // 不把夹具通关难度带入后续弹道。
    const FVector PreviousLocation=Player->GetActorLocation(); // 隔离空中靶场位置，终端需要真实距离检查。
    const EDemoDifficulty UnlockDifficulty=Index==0?EDemoDifficulty::Easy:EDemoDifficulty::Hard; // 生产规则：步枪仍需简单通关；困难只解锁散弹与狙击，不能把难度视为全武器递进。
    State->Phase=EDemoPhase::Victory; State->LevelNumber=10; State->Difficulty=UnlockDifficulty;
    const bool bUnlocked=Profile->RecordVictory(FGuid::NewGuid().ToString(),UnlockDifficulty); // 只提交本测试GI的合法解锁事实，不绕过终端/装备权限。
    State->Phase=EDemoPhase::Hub; State->LevelNumber=PreviousLevel; State->Difficulty=PreviousDifficulty;
    if (bUnlocked)
    {
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetShopTerminal()->Interact(Player); PC->SetTerminalWeaponPage(true);
        PC->InspectWeapon(Index+1); PC->EquipInspectedWeapon(); PC->CloseWeaponTip(); PC->CloseUpgradeMenu();
    }
    State->Phase=PreviousPhase; Player->SetActorLocation(PreviousLocation);
    return bUnlocked&&Player->GetWeaponComponent()->GetPrimaryIndex()==Index&&Player->GetWeaponComponent()->GetActiveSlot()==1;
}
