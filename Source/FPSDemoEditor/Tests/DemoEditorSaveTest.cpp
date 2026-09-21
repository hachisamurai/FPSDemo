#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Save/DemoRunSave.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoCloudSync.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h" // V4测试直接检查协议字符串，避免仅UStruct往返掩盖大小写错误。

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    /** 队列共享值，不持有GI/World；跨两次真实PIE检查临时文件和正式档保护。 */
    struct FEditorSaveProbe
    {
        TArray<FString> OwnedSlots; // 当前测试PIE创建的精确槽名，停止后逐个验证主档/备份已删除。
        FString PreviousRunSlot; // 第一轮GUID槽名，第二轮必须不同。
        FString PreviousProfileId; // 第一轮档案ID，第二轮不得继承解锁身份。
        TArray<FString> FormalSlots; // 正式游戏的固定槽及备份名，只读，不创建/删除。
        TArray<TArray<uint8>> FormalBytes; // 测试前的原始数据，仅验证未被Editor读写流程改变。
        TArray<bool> FormalExists; // 区分不存在与空文件，防止新增正式文件也被忽略。
    };

    /** 每帧等待PIE启动/关闭；无Lambda，队列持有共享值，借用Test直到自动化队列结束。 */
    class FEditorSaveProbeCommand final : public IAutomationLatentCommand
    {
    public:
        /** InTest为队列期间有效的测试对象；InProbe共享值；InStage=1写入/2清理/3新会话/4清理。 */
        FEditorSaveProbeCommand(FAutomationTestBase* InTest, TSharedRef<FEditorSaveProbe> InProbe, int32 InStage)
            : Test(InTest), Probe(InProbe), Stage(InStage)
        { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FEditorSaveProbeCommand stage=%d"), Stage); }

        /** 游戏线程轮询真实Editor PlayWorld；完成或30秒超时才推进自动化队列。 */
        virtual bool Update() override
        {
            UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] FEditorSaveProbeCommand::Update stage=%d"), Stage);
            if (StartedAt == 0) StartedAt = FPlatformTime::Seconds();
            if (FPlatformTime::Seconds() - StartedAt > 30)
            { Test->AddError(FString::Printf(TEXT("PIE lifecycle timeout stage=%d"), Stage)); return true; }
            if (Stage == 2 || Stage == 4)
            {
                if (GEditor->PlayWorld) return false;
                for (const FString& Slot : Probe->OwnedSlots) // 只检查当前GI创建的槽，不触碰其他试玩实例。
                    Test->TestFalse(FString::Printf(TEXT("Stopped PIE deletes %s"), *Slot), UGameplayStatics::DoesSaveGameExist(Slot, 0));
                for (int32 Index = 0; Index < Probe->FormalSlots.Num(); ++Index) // 原始正式存档字节只读比较。
                {
                    TArray<uint8> CurrentBytes; // 本帧同步读取，无World生命周期依赖。
                    const FString& Slot = Probe->FormalSlots[Index]; // 共享测试值中的固定槽名。
                    Test->TestEqual(TEXT("Formal slot existence unchanged"), UGameplayStatics::DoesSaveGameExist(Slot, 0), Probe->FormalExists[Index]);
                    if (Probe->FormalExists[Index]) UGameplayStatics::LoadDataFromSlot(CurrentBytes, Slot, 0);
                    Test->TestTrue(TEXT("Formal slot bytes untouched"), CurrentBytes == Probe->FormalBytes[Index]);
                }
                UE_LOG(LogFPSDemo, Display, TEXT("EDITOR_TEMPORARY_SAVES stopped stage=%d files=%d"), Stage, Probe->OwnedSlots.Num());
                if (Stage == 4 && !Test->HasAnyErrors()) UE_LOG(LogFPSDemo, Display, TEXT("EDITOR_TEMPORARY_SAVES_SUCCESS: real PIE twice, local reload, all owned files removed, formal saves untouched"));
                return true;
            }
            UGameInstance* GI = GEditor->PlayWorld ? GEditor->PlayWorld->GetGameInstance() : nullptr; // 只在本帧借用，停止时不保留悬空指针。
            if (!GI || !GEditor->PlayWorld->HasBegunPlay()) return false;
            UDemoRunSaves* Saves = GI->GetSubsystem<UDemoRunSaves>(); // 本轮临时检查点服务。
            UDemoPlayerProfile* Profile = GI->GetSubsystem<UDemoPlayerProfile>(); // 本轮临时武器解锁服务。
            if (!Test->TestNotNull(TEXT("PIE save service"), Saves) || !Test->TestNotNull(TEXT("PIE profile service"), Profile)) return true;
            Test->TestNull(TEXT("PIE never creates cloud sync"), GI->GetSubsystem<UDemoCloudSync>());
            Test->TestTrue(TEXT("Editor run prefix"), Saves->SlotName(0).StartsWith(TEXT("DemoEditorRun_")));
            Test->TestTrue(TEXT("Editor profile prefix"), Profile->GetSlotName().StartsWith(TEXT("DemoEditorProfile_")));
            for (int32 Index = 0; Index < 3; ++Index) // 每次新PIE均是空槽，不继承上一次试玩进度。
                Test->TestFalse(TEXT("Fresh PIE has no checkpoint"), Saves->SlotExists(Index));
            Test->TestTrue(TEXT("Fresh PIE only unlocks pistol"), Profile->IsUnlocked(TEXT("pistol")) && !Profile->IsUnlocked(TEXT("rifle")) && Profile->GetData().Clears.IsEmpty());
            if (Stage == 3)
            {
                Test->TestTrue(TEXT("New PIE uses different run prefix"), Saves->SlotName(0) != Probe->PreviousRunSlot);
                Test->TestTrue(TEXT("New PIE uses different profile identity"), Profile->GetData().ProfileId != Probe->PreviousProfileId);
            }
            Probe->PreviousRunSlot = Saves->SlotName(0);
            Probe->PreviousProfileId = Profile->GetData().ProfileId;
            Probe->OwnedSlots.Empty();
            // 写满三个槽及备份，保证停止验证覆盖真实文件，而非不存在的空路径。
            for (int32 Index = 0; Index < 3; ++Index)
            {
                if (!Test->TestTrue(TEXT("Create local checkpoint"), Saves->CreateSlot(Index))) return true;
                UDemoRunSave* Snapshot = DuplicateObject<UDemoRunSave>(Saves->GetSlot(Index), GI); // 同步测试拥有的值副本，不修改内部缓存。
                Snapshot->Coins = 42 + Index; Snapshot->DamageBonus = 10;
                Snapshot->SilverCoins = 71; Snapshot->GoldPurchases = 1; Snapshot->SilverPurchases = 2; Snapshot->Purchases = 3; // 两钱包和两处价格必须原样读回。
                Snapshot->UpgradeProgress.GoldLevels = {1,0,0}; Snapshot->UpgradeProgress.SilverLevels = {0,1,1}; // 非对称价格能发现数组项错位。
                Snapshot->UpgradeProgress.PermanentDamage = 5; // 10合计伤害中仅5永久，读档不能丢来源或重复施加。
                Snapshot->UnlockedAmmoIds={TEXT("normal"),TEXT("fire"),TEXT("frost"),TEXT("piercing")}; Snapshot->SelectedAmmoId=TEXT("frost"); // V4值协议覆盖四种解锁，而非仅默认普通弹。
                Test->TestTrue(TEXT("Store checkpoint and backup"), Saves->Store(Snapshot));
                Test->TestTrue(TEXT("Select existing local checkpoint"), Saves->SelectSlot(Index));
                Test->TestTrue(TEXT("Local reload keeps both currencies, prices and growth"), Saves->GetSlot(Index)->Coins == 42 + Index && Saves->GetSlot(Index)->DamageBonus == 10
                    && Saves->GetSlot(Index)->SilverCoins == 71 && Saves->GetSlot(Index)->GoldPurchases == 1 && Saves->GetSlot(Index)->SilverPurchases == 2);
                Probe->OwnedSlots.Add(Saves->SlotName(Index));
                Probe->OwnedSlots.Add(Saves->SlotName(Index) + TEXT("_Backup"));
            }
            // 值协议专项不发送HTTP：同时检查后端区分大小写的ID，再实际导出/导入相同栏。
            const TSharedPtr<FJsonObject> AmmoJson=Saves->ExportCloud(2); // 本调用拥有JSON快照，不含认证数据。
            Test->TestTrue(TEXT("V4 canonical lowercase ammo IDs"),AmmoJson && AmmoJson->GetStringField(TEXT("selectedAmmoId"))==TEXT("frost") && AmmoJson->GetArrayField(TEXT("unlockedAmmoIds"))[0]->AsString()==TEXT("normal"));
            Test->TestTrue(TEXT("V4 checkpoint JSON round trip"), Saves->ImportCloud(2, AmmoJson));
            Test->TestTrue(TEXT("V4 JSON preserves ammo unlocks and selection"),Saves->GetSlot(2)->UnlockedAmmoIds.Num()==4 && Saves->GetSlot(2)->SelectedAmmoId==TEXT("frost"));
            Test->TestTrue(TEXT("V3 JSON preserves wallets, permanent source and per-attribute prices"), Saves->GetSlot(2)->SilverCoins == 71
                && Saves->GetSlot(2)->UpgradeProgress.PermanentDamage == 5 && Saves->GetSlot(2)->UpgradeProgress.Cost(0,true) == 30
                && Saves->GetSlot(2)->UpgradeProgress.Cost(1,true) == 20 && Saves->GetSlot(2)->UpgradeProgress.Cost(0,false) == 20
                && Saves->GetSlot(2)->UpgradeProgress.Cost(1,false) == 30);
            UDemoRunSave* Legacy = DuplicateObject<UDemoRunSave>(Saves->GetSlot(2), GI); // 独立夹具，拒绝分支不可污染原缓存。
            Legacy->Version = 1; Legacy->Phase = EDemoPhase::Intermission; Legacy->CompletedLevel = 1; Legacy->GoldPurchases = Legacy->SilverPurchases = Legacy->SilverCoins = 0;
            Test->TestTrue(TEXT("V1 arena keeps legacy stats temporarily without inventing permanent sources"), Legacy->UpgradeLegacy() && Legacy->Version == 4
                && Legacy->Coins == 44 && Legacy->DamageBonus == 10 && Legacy->SilverCoins == 0 && Legacy->GoldPurchases == 0 && Legacy->SilverPurchases == 0 && Legacy->UpgradeProgress.PermanentDamage == 0);
            Legacy->Version = 2; Legacy->GoldPurchases = 3; Legacy->SilverPurchases = 1; Legacy->Purchases = 4; // 模拟V2只有币种总次数。
            Test->TestTrue(TEXT("V2 refunds 20+30+40 gold once and preserves legacy totals temporarily"), Legacy->UpgradeLegacy() && Legacy->Coins == 134 && Legacy->DamageBonus == 10 && Legacy->Purchases == 0);
            Test->TestTrue(TEXT("V3 migration is idempotent"), Legacy->UpgradeLegacy() && Legacy->Coins == 134);
            Legacy->ResetTemporaryGrowth();
            Test->TestTrue(TEXT("Migrated legacy temporary stats clear at run end"), Legacy->DamageBonus == 0 && Legacy->Coins == 134);
            Legacy->SilverCoins = -1;
            Test->TestFalse(TEXT("Negative silver rejected"), Legacy->Validate());
            Legacy->SilverCoins = 0; Legacy->GoldPurchases = 1;
            Test->TestFalse(TEXT("Inconsistent currency purchase totals rejected"), Legacy->Validate());
            Legacy->GoldPurchases = 0; Legacy->UpgradeProgress.GoldLevels = {0,0};
            Test->TestFalse(TEXT("Malformed per-attribute array rejected"), Legacy->Validate());
            Legacy->UpgradeProgress.GoldLevels = {0,0,0}; Legacy->UpgradeProgress.PermanentDamage = 5;
            Test->TestFalse(TEXT("Permanent source larger than total rejected"), Legacy->Validate());
            Legacy->UpgradeProgress.PermanentDamage = 0; Legacy->Version = 5; // V4已支持，V5才是未知未来版本。
            Test->TestFalse(TEXT("Future checkpoint version rejected"), Legacy->UpgradeLegacy());
            UDemoProfileSave* Unlock = NewObject<UDemoProfileSave>(GI); // 仅本测试的UE序列化夹具，用于验证解锁在下次PIE清空。
            Unlock->Data = Profile->GetData();
            FDemoClearRecord Clear; // 合法离线通关凭据夹具；游戏通关逻辑另由Victory专项验证。
            Clear.RunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
            Clear.DifficultyId = TEXT("easy"); Clear.CompletedUtc = FDateTime::UtcNow().ToIso8601();
            Unlock->Data.Clears.Add(Clear);
            Test->TestTrue(TEXT("Seed editor-only unlock"), UGameplayStatics::SaveGameToSlot(Unlock, Profile->GetSlotName(), 0));
            Test->TestTrue(TEXT("Reload local unlock"), Profile->ReloadFromDisk() && Profile->IsUnlocked(TEXT("rifle")));
            Test->TestTrue(TEXT("Create profile backup"), Profile->SaveProfile());
            Probe->OwnedSlots.Add(Profile->GetSlotName());
            Probe->OwnedSlots.Add(Profile->GetSlotName() + TEXT("_Backup"));
            for (const FString& Slot : Probe->OwnedSlots) // 停止前确认所有被测文件实际存在。
                Test->TestTrue(TEXT("Fixture file exists before stop"), UGameplayStatics::DoesSaveGameExist(Slot, 0));
            UE_LOG(LogFPSDemo, Display, TEXT("EDITOR_TEMPORARY_SAVES seeded stage=%d files=%d"), Stage, Probe->OwnedSlots.Num());
            return true;
        }
    private:
        FAutomationTestBase* Test; // 自动化框架拥有，整个latent队列期间有效；仅游戏线程访问。
        TSharedRef<FEditorSaveProbe> Probe; // 队列命令共同持有值快照，最后一条命令销毁后释放。
        int32 Stage; // 固定检查阶段1..4，构造后不变。
        double StartedAt = 0; // 当前命令第一次Update的真实秒数，不受游戏暂停影响。
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoEditorTemporarySavesTest, "FPSDemo.Editor.TemporarySaves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters为未使用的自动化参数；显式测试启动两次PIE，拒绝接管用户已运行的试玩。 */
bool FDemoEditorTemporarySavesTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FDemoEditorTemporarySavesTest::RunTest"));
    if (!GEditor || GEditor->PlayWorld) { AddError(TEXT("Test requires an idle editor without active PIE")); return false; }
    TSharedRef<FEditorSaveProbe> Probe = MakeShared<FEditorSaveProbe>(); // 只在本次自动化队列共享，不用静态变量跨测试保留GI状态。
    Probe->FormalSlots = { TEXT("DemoPlayerProfile"), TEXT("DemoPlayerProfile_Backup"), TEXT("DemoRun_0"), TEXT("DemoRun_0_Backup"), TEXT("DemoRun_1"), TEXT("DemoRun_1_Backup"), TEXT("DemoRun_2"), TEXT("DemoRun_2_Backup") };
    for (const FString& Slot : Probe->FormalSlots) // 正式档案只读建立基线，测试失败也不删除或恢复写入。
    {
        Probe->FormalExists.Add(UGameplayStatics::DoesSaveGameExist(Slot, 0));
        TArray<uint8>& Bytes = Probe->FormalBytes.AddDefaulted_GetRef(); // 容器拥有的字节副本，无借用文件句柄。
        if (Probe->FormalExists.Last() && !UGameplayStatics::LoadDataFromSlot(Bytes, Slot, 0)) { AddError(TEXT("Cannot read formal save baseline")); return false; }
    }
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FEditorSaveProbeCommand(this, Probe, 1));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FEditorSaveProbeCommand(this, Probe, 2));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FEditorSaveProbeCommand(this, Probe, 3));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    ADD_LATENT_AUTOMATION_COMMAND(FEditorSaveProbeCommand(this, Probe, 4));
    return true;
}
#endif
