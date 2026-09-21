#include "Tests/DemoArmoryTest.h"
#include "Debug/DemoLog.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Game/FPSDemoGameMode.h"
#include "Interaction/DemoInteractable.h"
#include "UI/DemoHUD.h"
#include "AI/DemoEnemy.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/HUDHitBox.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Engine/Texture2D.h"

namespace
{
    // 仅专用测试进程跨OpenLevel：0..2依次完整通关三难度，3装备检查，4死亡重载检查。
    int32 ArmoryRun = 0;
}
ADemoArmoryTest::ADemoArmoryTest() { DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = .1f; }
bool ADemoArmoryTest::Check(bool Condition, const TCHAR* Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Display, TEXT("ARMORY_TEST %s run=%d step=%d %s"), Condition ? TEXT("PASS") : TEXT("FAIL"), ArmoryRun, Step, Message);
    if (!Condition) { bFailed = true; FPlatformMisc::RequestExitWithStatus(false, 1); }
    return Condition;
}
void ADemoArmoryTest::Advance(int32 Next) { DEMO_LOG_CALL(); Step = Next; NextTime = GetWorld()->GetTimeSeconds() + .3f; }
void ADemoArmoryTest::Capture(const TCHAR* Name)
{
    DEMO_LOG_CALL();
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Armory") / FString(Name) + TEXT(".png"), false, false);
}
void ADemoArmoryTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds() < 90.f, TEXT("bounded test world"))) return;
    ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0)); // 本步借用当前World对象，OpenLevel后重取。
    ADemoCharacter* Player = PC ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr; // 真实Avatar，用于范围/生命检查。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 真实权威战役流程。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前关号/难度，不直接授予解锁。
    UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 隔离测试槽，跨World保留。
    ADemoHUD* HUD = PC ? PC->GetHUD<ADemoHUD>() : nullptr; // 真实Canvas热区。
    if (!Check(Player && Mode && State && Profile && HUD, TEXT("runtime dependencies ready"))) return;
    UDemoWeaponComponent* Equipment = Player->GetWeaponComponent(); // 本步借用当前装备，不跨地图缓存。
    switch (Step)
    {
    case 0:
    {
        int32 Count = 0; // 当前实际武器Actor数，验证没有隐藏的未装备主武器。
        for (TActorIterator<ADemoWeaponBase> It(GetWorld()); It; ++It) ++Count; // World只读迭代，不修改Actor。
        if (ArmoryRun == 4)
        {
            if (!Check(State->Phase == EDemoPhase::Hub && Profile->GetData().UnlockedWeaponIds.Num() == 4
                && Profile->GetData().LastSelectedPrimary == TEXT("sniper") && State->Coins == 0
                && Count == 4 && Equipment->GetPrimaryIndex() == 0 && Equipment->GetActiveSlot() == 1
                && Equipment->GetActiveWeapon()->GetAmmo() == Equipment->GetActiveWeapon()->GetCapacity(), TEXT("death retains real rifle loadout, refills ammo and ignores different account preference"))) return; // 无活动槽开发World也必须跨旅行保留装备值。
            UE_LOG(LogFPSDemo, Display, TEXT("DEMO_ARMORY_SUCCESS: three full difficulty clears, death equipment retention, terminal equip, locks, save roundtrip, upload contract, modal hitboxes"));
            FPlatformMisc::RequestExitWithStatus(false, 0);
            SetActorTickEnabled(false);
            return;
        }
        // 真正新档/普通大厅不凭账号解锁或偏好自动发枪；死亡恢复在上方使用真实装备快照。
        if (!Check(Count == 1 && Equipment->GetActiveSlot() == 2 && Equipment->GetPrimaryIndex() == INDEX_NONE, TEXT("fresh world without resume owns pistol only"))) return;
        if (!Check(!Equipment->EquipSlot(1), TEXT("empty primary slot rejected"))) return;
        // 大厅新增存档选择由Session测试覆盖；本专项直接建立隔离新局，仍使用权威难度接口。
        if (!Check(Mode->StartRun(), TEXT("armory fixture starts fresh run"))) return;
        PC->OnRunReady();
        if (ArmoryRun < 3 && !Check(Mode->SelectDifficulty(static_cast<EDemoDifficulty>(ArmoryRun)), TEXT("authoritative initial-hub difficulty selected"))) return;
        if (!Check(State->Phase == EDemoPhase::Hub, TEXT("start enters safe hub"))) return;
        if (FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponIconTest"))) // 仅额外测试参数修改运行时CDO，不保存或修改任何蓝图资产。
        {
            for (int32 Index = 0; Index < 4; ++Index) // 同图验证彩色/去色；空路径和无效路径验证两个回退分支。
            {
                ADemoWeaponBase* Definition = Equipment->GetCatalogWeapon(Index)->GetClass()->GetDefaultObject<ADemoWeaponBase>(); // 测试进程中的CDO副本，退出进程即丢弃。
                Definition->Config.Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Index < 2 ? TEXT("/Game/StarterContent/Textures/T_Brick_Clay_Old_D.T_Brick_Clay_Old_D")
                    : Index == 2 ? TEXT("") : TEXT("/Game/Weapons/Icons/TestMissingIcon.TestMissingIcon")));
            }
        }
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation() + FVector(-180,0,20));
        Mode->GetShopTerminal()->Interact(Player);
        PC->SetTerminalWeaponPage(true);
        Advance(1);
        break;
    }
    case 1:
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponIconTest"))) // 在真实UI异步完成后验证，不用同步加载使测试失真。
        {
            if ((!HUD->GetWeaponIcon(0) || !HUD->GetWeaponIcon(1)) && GetWorld()->GetTimeSeconds() < NextTime + 8.f) return; // 限时等待流送，超时走断言。
            if (!Check(HUD->GetWeaponIcon(0) && HUD->GetWeaponIcon(0) == HUD->GetWeaponIcon(1)
                && HUD->GetWeaponIcon(0)->GetPathName() == TEXT("/Game/StarterContent/Textures/T_Brick_Clay_Old_D.T_Brick_Clay_Old_D")
                && !HUD->GetWeaponIcon(2) && !HUD->GetWeaponIcon(3), TEXT("configured soft icons loaded/shared; null and invalid icons fallback"))) return;
        }
        if (!Check(PC->IsWeaponMenuOpen() && HUD->GetHitBoxWithName(TEXT("Weapon0")) && HUD->GetHitBoxWithName(TEXT("Weapon3")), TEXT("terminal renders four clickable items"))) return;
        int32 Width = 0; // 实际视口尺寸，用于验证卡片坐标与生产缩放一致。
        int32 Height = 0; // 非16:9时锚点仍在视口中心。
        PC->GetViewportSize(Width, Height);
        const float Scale = FMath::Min(Width/1280.f, Height/720.f); // Canvas设计坐标比例。
        const FHUDHitBox* Hit = HUD->GetHitBoxAtCoordinates(FVector2D(Width,Height)/2.f + FVector2D(-110,-20)*Scale, true); // 第二卡片中心。
        if (!Check(Hit && Hit->GetName() == TEXT("Weapon1"), TEXT("scaled point resolves rifle card"))) return;
        if (ArmoryRun == 0)
        {
            if (!Check(Profile->GetData().UnlockedWeaponIds.Num() == 1 && !Equipment->SelectPrimary(0), TEXT("new save only pistol and direct locked equip rejected"))) return;
            Capture(TEXT("00-LockedCatalog"));
        }
        if (ArmoryRun == 3) { Capture(TEXT("02-UnlockedCatalog")); Advance(10); break; }
        Advance(2);
        break;
    }
    case 2:
        HUD->NotifyHitBoxClick(TEXT("Weapon3"));
        Advance(3);
        break;
    case 3:
        if (!Check(PC->GetInspectedWeapon() == 3 && !HUD->GetHitBoxWithName(TEXT("WeaponEquip"))
            && !HUD->GetHitBoxWithName(TEXT("Weapon0")), TEXT("locked tip disables equip and blocks background hitboxes"))) return;
        PC->EquipInspectedWeapon();
        if (!Check(Equipment->GetPrimaryIndex() == INDEX_NONE, TEXT("forged locked equip leaves loadout unchanged"))) return;
        if (ArmoryRun == 0) Capture(TEXT("01-LockedTip"));
        Advance(4);
        break;
    case 4:
        PC->CloseWeaponTip();
        PC->CloseUpgradeMenu();
        Mode->StartNextLevel();
        Advance(5);
        break;
    case 5:
        if (!Check(State->Phase == EDemoPhase::Combat, TEXT("real campaign combat before clear"))) return;
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 真实GE触发注册敌人死亡和FinishLevel，不直接写胜利或解锁。
            if (It->IsAlive()) DemoEffects::Apply(Player->GetDemoASC(), It->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -100000.f);
        Advance(6);
        break;
    case 6:
        if (State->Phase == EDemoPhase::Reward)
        {
            if (!Check(Profile->GetData().UnlockedWeaponIds.Num() == ArmoryRun + 1, TEXT("partial campaign never unlocks next weapon"))) return;
            PC->SetTerminalWeaponPage(true);
            if (!Check(!PC->IsWeaponMenuOpen(), TEXT("reward cannot switch to weapon page"))) return;
            PC->SelectUpgrade(0);
            Mode->StartNextLevel();
            Advance(5);
        }
        else
        {
            if (!Check(State->Phase == EDemoPhase::Victory && State->LevelNumber == 10
                && Profile->IsUnlocked(DemoWeaponCatalog::IdAt(ArmoryRun + 1)) && Profile->GetData().UnlockedWeaponIds.Num() == ArmoryRun + 2
                && Equipment->GetPrimaryIndex() == INDEX_NONE, TEXT("full corresponding difficulty unlocks exactly one weapon without granting it"))) return;
            const int32 Revision = Profile->GetData().LocalRevision; // 胜利重复回调前版本，应保持不变。
            Profile->RecordVictory(Profile->GetData().Clears.Last().RunId, State->Difficulty);
            UDemoProfileSave* Loaded = Cast<UDemoProfileSave>(UGameplayStatics::LoadGameFromSlot(Profile->GetSlotName(), 0)); // 从磁盘读回的独立对象。
            if (!Check(Profile->GetData().LocalRevision == Revision && Loaded && Loaded->Data.Clears.Num() == ArmoryRun + 1
                && Loaded->Data.UnlockedWeaponIds == Profile->GetData().UnlockedWeaponIds, TEXT("duplicate victory idempotent and disk serialization roundtrip"))) return;
            ++ArmoryRun;
            Mode->RestartDemo();
            if (!Check(State->Phase==EDemoPhase::Hub && State->LevelNumber==0 && Mode->GetShopTerminal(),TEXT("clear returns to usable armory hub"))) return;
            Mode->TravelToRun(false); // 每档默认手枪夹具显式新建World，正常胜利续玩的库存不再被清空。
            SetActorTickEnabled(false);
        }
        break;
    case 10:
        HUD->NotifyHitBoxClick(TEXT("Weapon1"));
        Advance(11);
        break;
    case 11:
        if (!Check(HUD->GetHitBoxWithName(TEXT("WeaponEquip")) && !HUD->GetHitBoxWithName(TEXT("TerminalStats")), TEXT("unlocked tip exposes explicit equip and blocks tabs"))) return;
        // 同一帧打开/关闭暂停避免测试Tick被暂停；底层Tips与组件均必须拒绝旧装备意图。
        PC->EscapePressed();
        PC->EquipInspectedWeapon();
        if (!Check(PC->HasBlockingOverlay() && Equipment->GetPrimaryIndex() == INDEX_NONE && !Equipment->SelectPrimary(0), TEXT("pause overlay blocks controller and direct component equip"))) return;
        PC->CloseOverlay();
        HUD->NotifyHitBoxClick(TEXT("WeaponEquip"));
        if (!Check(Equipment->GetPrimaryIndex() == 0 && Equipment->GetActiveSlot() == 1 && State->Coins == 0, TEXT("terminal equips rifle without requiring coins"))) return;
        Equipment->GetActiveWeapon()->ModifyAmmo(-1); // 控制测试库存，随后切回不得刷新弹匣。
        Capture(TEXT("03-EquippedTip"));
        Advance(12);
        break;
    case 12:
        PC->CloseWeaponTip();
        PC->InspectWeapon(2);
        PC->EquipInspectedWeapon();
        if (!Check(Equipment->GetActiveWeapon()->IsA<ADemoShotgunWeapon>(), TEXT("shotgun equipped from terminal"))) return;
        PC->CloseWeaponTip();
        PC->InspectWeapon(3);
        PC->EquipInspectedWeapon();
        if (!Check(Equipment->GetActiveWeapon()->IsA<ADemoSniperWeapon>(), TEXT("sniper equipped from terminal"))) return;
        PC->CloseWeaponTip();
        PC->InspectWeapon(1);
        PC->EquipInspectedWeapon();
        if (!Check(Equipment->GetActiveWeapon()->GetAmmo() == 11, TEXT("reselect reuses inventory without free reload"))) return;
        Equipment->CyclePrimary();
        if (!Check(Equipment->GetPrimaryIndex() == 0, TEXT("old B shortcut cannot change model"))) return;
        Player->SetActorLocation(Player->GetActorLocation() + FVector(800,0,0));
        PC->CloseWeaponTip();
        PC->InspectWeapon(3);
        PC->EquipInspectedWeapon();
        if (!Check(Equipment->GetPrimaryIndex() == 0 && !Equipment->SelectPrimary(2), TEXT("range revalidated at equip in controller and component"))) return;
        Advance(13);
        break;
    case 13:
    {
        FString Json; // 纯数据网络边界，测试不启动HTTP或上传玩家文件。
        if (!Check(Profile->BuildUploadJson(Json) && Json.Contains(TEXT("protocolVersion")) && Json.Contains(TEXT("baseServerRevision"))
            && Json.Contains(TEXT("rifle")) && !Json.Contains(TEXT("/Game/")), TEXT("upload JSON uses stable ids and versioned envelope"))) return;
        const int32 Uploaded = Profile->GetData().LocalRevision; // 模拟网络飞行期间的旧快照版本。
        Profile->RememberPrimary(TEXT("sniper")); // 新本地内容不能被较旧服务器确认回滚。
        if (!Check(!Profile->AcknowledgeUpload(TEXT("wrong-profile"), Uploaded, TEXT("server-1"))
            && Profile->AcknowledgeUpload(Profile->GetData().ProfileId, Uploaded, TEXT("server-1"))
            && Profile->GetData().SyncedRevision < Profile->GetData().LocalRevision && Profile->GetData().LastSelectedPrimary == TEXT("sniper")
            && !Profile->AcknowledgeUpload(Profile->GetData().ProfileId, Uploaded, TEXT("stale")), TEXT("sync acknowledgements reject wrong/stale identity and preserve newer local mutation"))) return;
        // 只操作本次GUID测试槽：完整重载、未知未来版本保护、坏主档从备份恢复都走生产读取路径。
        UDemoProfileSave* Good = Cast<UDemoProfileSave>(UGameplayStatics::CreateSaveGameObject(UDemoProfileSave::StaticClass())); // 保存当前期望快照，最后还原隔离文件。
        Good->Data = Profile->GetData();
        if (!Check(Profile->ReloadFromDisk() && Profile->GetData().LocalRevision == Good->Data.LocalRevision
            && Profile->GetData().Clears.Num() == 3, TEXT("production reload restores progression and revision from local file"))) return;
        UDemoProfileSave* Invalid = Cast<UDemoProfileSave>(UGameplayStatics::CreateSaveGameObject(UDemoProfileSave::StaticClass())); // 刻意构造的版本/数据损坏夹具。
        Invalid->Data = Good->Data;
        Invalid->Data.SchemaVersion = 99;
        UGameplayStatics::SaveGameToSlot(Invalid, Profile->GetSlotName(), 0);
        if (!Check(!Profile->ReloadFromDisk() && !Profile->SaveProfile(), TEXT("future version is protected from downgrade overwrite"))) return;
        Invalid->Data.SchemaVersion = 1;
        Invalid->Data.ProfileId = TEXT("broken-guid");
        UGameplayStatics::SaveGameToSlot(Invalid, Profile->GetSlotName(), 0);
        if (!Check(Profile->ReloadFromDisk() && Profile->GetData().UnlockedWeaponIds.Num() == 4 && Profile->SaveProfile(), TEXT("invalid main file restores valid backup and repairs safely"))) return;
        UGameplayStatics::SaveGameToSlot(Good, Profile->GetSlotName(), 0);
        if (!Check(Profile->ReloadFromDisk(), TEXT("test fixture restored before death persistence check"))) return;
        PC->CloseWeaponTip();
        PC->CloseUpgradeMenu();
        if (!Check(Equipment->EquipSlot(2) && Equipment->EquipSlot(1), TEXT("numeric slot API uses chosen primary and pistol after menu"))) return;
        if (!Check(!Equipment->SelectPrimary(2), TEXT("outside-terminal model selection rejected"))) return;
        Mode->NotifyPlayerDied();
        ++ArmoryRun;
        Mode->RestartDemo();
        SetActorTickEnabled(false);
        break;
    }
    }
}
