// 职责：冻结战役内容与检查点V9的纯值兼容夹具。使用：Automation RunTests FPSDemo.Save.CampaignCompatibility；不启动PIE、不读写玩家槽、不访问云。
#include "Misc/AutomationTest.h"
#include "Save/DemoRunSave.h"
#include "Save/Codecs/DemoCheckpointJsonCodec.h"
#include "Game/Content/DemoCampaignDefinition.h"
#include "Dom/JsonObject.h"
#include "Debug/DemoLog.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoCampaignSaveCompatibilityTest, "FPSDemo.Save.CampaignCompatibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters由Automation提供，本夹具忽略外部路径；所有候选在瞬态包独立生成，不能改动作者CDO或实际存档。 */
bool FDemoCampaignSaveCompatibilityTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FDemoCampaignSaveCompatibilityTest::RunTest"));
    FString Error, PublishedJson, PublishedFingerprint; // 历史v1由生产迁移入口导出，再与候选迁移结果比较。
    if (!TestTrue(TEXT("Published v1 exports"), UDemoCampaignDefinition::ExportLegacyV1(PublishedJson, PublishedFingerprint, Error))) return false;
    UDemoCampaignDefinition* Authored = NewObject<UDemoCampaignDefinition>(); // 独立模拟策划已改版，不影响正式DataAsset/CDO。
    Authored->ContentRevision = 7; Authored->ControlSeconds = 55.f;
    FString AuthoredJson, AuthoredFingerprint; // 模拟新轮内容；其改动不能进入旧V8轮次。
    if (!TestTrue(TEXT("Changed author config exports"), Authored->ExportSnapshot(AuthoredJson, AuthoredFingerprint, Error))) return false;
    TestNotEqual(TEXT("New authored config differs from immutable history"), AuthoredFingerprint, PublishedFingerprint);

    UDemoRunSave* Previous = NewObject<UDemoRunSave>(); // V8安全区合法档；无文件操作，金币作为已有数据保留哨兵。
    Previous->Version = 8; Previous->CampaignId = TEXT("steam_demo"); Previous->ContentRevision = 1; Previous->RunSeed = 197;
    Previous->CreatedLocal = FDateTime::Now(); Previous->SavedUtc = FDateTime::UtcNow(); Previous->RunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); Previous->Coins = 77;
    const TSharedPtr<FJsonObject> OldJson = DemoCheckpointJsonCodec::Encode(*Previous, Error); // 导出旧源版本不注入新字段，旧Outbox字节由原链路保留。
    if (!TestTrue(TEXT("V8 still exports"), OldJson.IsValid())) return false;
    TestFalse(TEXT("V8 has no frozen-content key"), OldJson->HasField(TEXT("contentSnapshot")));
    TestFalse(TEXT("V8 has no fingerprint key"), OldJson->HasField(TEXT("contentFingerprint")));
    if (!TestTrue(TEXT("V8 migrates"), Previous->UpgradeLegacy())) return false;
    TestEqual(TEXT("Migrated schema is V9"), Previous->Version, 9);
    TestEqual(TEXT("Migrated run uses published v1 JSON"), Previous->ContentSnapshot, PublishedJson);
    TestEqual(TEXT("Migrated run uses published v1 fingerprint"), Previous->ContentFingerprint, PublishedFingerprint);
    TestEqual(TEXT("Migration preserves seed"), Previous->RunSeed, 197);
    TestEqual(TEXT("Migration preserves wallet"), Previous->Coins, 77);

    UDemoRunSave* Frozen = DuplicateObject<UDemoRunSave>(Previous, GetTransientPackage()); // 新轮使用显式作者版本，后续作者改动不得改变此值。
    Frozen->ContentRevision = 7; Frozen->ContentSnapshot = AuthoredJson; Frozen->ContentFingerprint = AuthoredFingerprint;
    Authored->ControlSeconds = 120.f; // 模拟保存后策划调参；不重新导出覆盖Frozen。
    TestTrue(TEXT("Arbitrary positive content revision validates"), Frozen->Validate());
    const TSharedPtr<FJsonObject> Cloud = DemoCheckpointJsonCodec::Encode(*Frozen, Error); // 生产Codec保留根JSON原文，成员无需副本。
    UDemoRunSave* Roundtrip = NewObject<UDemoRunSave>(); // 独立解码候选，失败也不能污染Frozen。
    if (!TestTrue(TEXT("V9 JSON roundtrip succeeds"), Cloud.IsValid() && DemoCheckpointJsonCodec::Decode(Cloud, *Roundtrip, Error))) return false;
    TestEqual(TEXT("Frozen source survives roundtrip byte for byte"), Roundtrip->ContentSnapshot, AuthoredJson);
    TestEqual(TEXT("Fingerprint survives roundtrip"), Roundtrip->ContentFingerprint, AuthoredFingerprint);
    UDemoCampaignDefinition* Restored = UDemoCampaignDefinition::FromSnapshot(GetTransientPackage(), Roundtrip->ContentSnapshot, Roundtrip->ContentFingerprint, Error); // 只读回本轮值，不能读取Authored当前120秒。
    if (!TestNotNull(TEXT("Frozen content restores"), Restored)) return false;
    TestEqual(TEXT("Saved value wins over changed asset"), Restored->ControlSeconds, 55.f);

    UDemoRunSave* Invalid = DuplicateObject<UDemoRunSave>(Frozen, GetTransientPackage()); // 每种损坏保持原冻结值可重复恢复，不写真实文件。
    Invalid->ContentFingerprint = TEXT("sha1:0000000000000000000000000000000000000000");
    TestFalse(TEXT("Mismatched fingerprint rejects save"), Invalid->Validate());
    Invalid->ContentFingerprint = AuthoredFingerprint; Invalid->Version = 10;
    TestFalse(TEXT("Future save version does not migrate"), Invalid->UpgradeLegacy());
    Invalid->Version = 9; Invalid->ContentRevision = 8;
    TestFalse(TEXT("Root and content revision mismatch rejects"), Invalid->Validate());
    const FString FutureSchema = AuthoredJson.Replace(TEXT("\"schemaVersion\":1"), TEXT("\"schemaVersion\":2")); // 未知解释规则应被识别为未来内容，供文件层禁止回退覆盖。
    TestTrue(TEXT("Future snapshot schema is protected"), UDemoCampaignDefinition::HasUnsupportedSnapshotSchema(FutureSchema));
    TestFalse(TEXT("Malformed JSON is corruption, not future schema"), UDemoCampaignDefinition::HasUnsupportedSnapshotSchema(TEXT("{")));
    return true;
}
#endif
