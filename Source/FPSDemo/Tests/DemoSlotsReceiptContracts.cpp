// 职责：已保存老虎机事实的领域/可选V10 JSON纯合同；不启动World/HTTP/云，不生成获奖或假正式资产。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "Save/Codecs/DemoCheckpointJsonCodec.h"
#include "Save/DemoRunSave.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Debug/DemoLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoSlotsReceiptContract,"FPSDemo.Casino.SavedSlotsReceipt.DomainAndJson",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoSlotsReceiptContract::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] Casino.SavedSlotsReceipt.DomainAndJson"));
    FDemoGambleProgress Empty; // 原生缺字段的默认领域值；不是根据新规则生成期望。
    TestTrue(TEXT("Old missing receipt is strict empty"),Empty.ValidateLastSlotsReceipt());
    Empty.LastSlotsReceipt.Serial=0; TestFalse(TEXT("Empty receipt cannot retain serial zero"),Empty.ValidateLastSlotsReceipt());
    Empty.LastSlotsReceipt=FDemoCasinoResult(); Empty.LastSlotsReceipt.Number=1;
    TestFalse(TEXT("Empty receipt cannot smuggle other mode fields"),Empty.ValidateLastSlotsReceipt());
    FDemoGambleProgress Saved; Saved.Serial=10; Saved.LastSlotsReceipt.Mode=1; Saved.LastSlotsReceipt.Serial=4; Saved.LastSlotsReceipt.Symbols={0,2,4}; Saved.LastSlotsReceipt.Paid=12;
    TestTrue(TEXT("Legal saved fact is valid"),Saved.ValidateLastSlotsReceipt());
    Saved.ResetRun(); TestTrue(TEXT("ResetRun preserves serial and saved slot fact"),Saved.Serial==10 && Saved.LastSlotsReceipt.Serial==4 && Saved.LastSlotsReceipt.Symbols==TArray<int32>({0,2,4}) && Saved.ValidateLastSlotsReceipt());
    FDemoGambleProgress Bad=Saved; Bad.LastSlotsReceipt.bBomb=true; TestFalse(TEXT("Mine flag cannot enter saved slot receipt"),Bad.ValidateLastSlotsReceipt());
    Bad=Saved; Bad.LastSlotsReceipt.Battle={1}; TestFalse(TEXT("Arena data cannot enter slot receipt"),Bad.ValidateLastSlotsReceipt());
    for (int32 Symbol:{-1,5}) { Bad=Saved; Bad.LastSlotsReceipt.Symbols[1]=Symbol; TestFalse(TEXT("Symbol outside five canonical faces rejected"),Bad.ValidateLastSlotsReceipt()); }
    for (int32 Serial:{0,11}) { Bad=Saved; Bad.LastSlotsReceipt.Serial=Serial; TestFalse(TEXT("Receipt must reference a real already committed serial"),Bad.ValidateLastSlotsReceipt()); }
    Bad=Saved; Bad.LastSlotsReceipt.Paid=-1; TestFalse(TEXT("Negative payout rejected"),Bad.ValidateLastSlotsReceipt());
    UDemoRunSave* Source=NewObject<UDemoRunSave>(); // 最小合法纯检查点，固定测试GUID不关联真实账户。
    Source->CreatedLocal=Source->SavedUtc=FDateTime(2026,11,15); Source->RunId=TEXT("00000000-0000-0000-0000-000000000101");
    Source->SlotStorageKey=TEXT("00000000-0000-0000-0000-000000000102"); Source->OwnerPlayerKey=TEXT("00000000-0000-0000-0000-000000000103");
    // 原权限合同要求旧基线显式含免费pistol；不能用空权限绕过真实V10合法性前置。
    TestTrue(TEXT("Test uses the real frozen ownership initializer"),Source->Ownership.FreezeLegacy({FName(TEXT("pistol"))},TEXT("00000000-0000-0000-0000-000000000104"),0));
    Source->Gambling=Saved;
    FString Error; // 只读错误原因，坏字段不写入真实存档或GI。
    TSharedPtr<FJsonObject> Json=DemoCheckpointJsonCodec::Encode(*Source,Error);
    if (!TestTrue(TEXT("Minimal current checkpoint encodes through production codec"),Json.IsValid())) return false;
    UDemoRunSave* Restored=NewObject<UDemoRunSave>();
    TestTrue(TEXT("Typed symbols survive actual V10 JSON roundtrip"),DemoCheckpointJsonCodec::Decode(Json,*Restored,Error) && Restored->Gambling.LastSlotsReceipt.Symbols==Saved.LastSlotsReceipt.Symbols && Restored->Gambling.LastSlotsReceipt.Paid==12);
    TSharedPtr<FJsonObject> Gamble=Json->GetObjectField(TEXT("gambling"));
    TSharedPtr<FJsonObject> Receipt=Gamble->GetObjectField(TEXT("lastSlotsReceipt"));
    Receipt->SetNumberField(TEXT("number"),2);
    TestFalse(TEXT("JSON rejects unknown other-mode fields"),DemoCheckpointJsonCodec::Decode(Json,*Restored,Error));
    Receipt->RemoveField(TEXT("number")); Receipt->SetNumberField(TEXT("serial"),11);
    TestFalse(TEXT("JSON rejects receipt ahead of parent serial"),DemoCheckpointJsonCodec::Decode(Json,*Restored,Error));
    Receipt->SetNumberField(TEXT("serial"),4);
    Receipt->SetArrayField(TEXT("symbols"),{MakeShared<FJsonValueNumber>(0),MakeShared<FJsonValueNumber>(1.5),MakeShared<FJsonValueNumber>(4)});
    TestFalse(TEXT("JSON rejects fractional symbol without truncation"),DemoCheckpointJsonCodec::Decode(Json,*Restored,Error));
    Gamble->SetField(TEXT("lastSlotsReceipt"),MakeShared<FJsonValueNull>());
    TestFalse(TEXT("Present null is not legacy missing"),DemoCheckpointJsonCodec::Decode(Json,*Restored,Error));
    Gamble->RemoveField(TEXT("lastSlotsReceipt"));
    TestTrue(TEXT("Old V10 without optional field restores empty fact"),DemoCheckpointJsonCodec::Decode(Json,*Restored,Error) && Restored->Gambling.LastSlotsReceipt.Mode==INDEX_NONE && Restored->Gambling.LastSlotsReceipt.Symbols.IsEmpty());
    Source->Gambling.LastSlotsReceipt=FDemoCasinoResult();
    Json=DemoCheckpointJsonCodec::Encode(*Source,Error);
    TestTrue(TEXT("Empty current receipt does not add legacy JSON key"),Json.IsValid() && !Json->GetObjectField(TEXT("gambling"))->HasField(TEXT("lastSlotsReceipt")));
    Receipt->SetNumberField(TEXT("mode"),-1); Receipt->SetNumberField(TEXT("serial"),-1); Receipt->SetNumberField(TEXT("paid"),0); Receipt->SetArrayField(TEXT("symbols"),{});
    Json->GetObjectField(TEXT("gambling"))->SetObjectField(TEXT("lastSlotsReceipt"),Receipt);
    TestTrue(TEXT("Explicit strict empty receipt is accepted without inventing symbols"),DemoCheckpointJsonCodec::Decode(Json,*Restored,Error) && Restored->Gambling.LastSlotsReceipt.Mode==INDEX_NONE);
    Source->Version=9; Source->SlotStorageKey.Empty(); Source->OwnerPlayerKey.Empty(); Source->Ownership=FDemoWeaponOwnership(); Source->Gambling=Saved;
    Json=DemoCheckpointJsonCodec::Encode(*Source,Error);
    if (!TestTrue(TEXT("V9 writer preserves original field shape"),Json.IsValid() && !Json->GetObjectField(TEXT("gambling"))->HasField(TEXT("lastSlotsReceipt")))) return false;
    Receipt->SetNumberField(TEXT("mode"),1); Receipt->SetNumberField(TEXT("serial"),4); Receipt->SetNumberField(TEXT("paid"),12);
    Receipt->SetArrayField(TEXT("symbols"),{MakeShared<FJsonValueNumber>(0),MakeShared<FJsonValueNumber>(2),MakeShared<FJsonValueNumber>(4)});
    Json->GetObjectField(TEXT("gambling"))->SetObjectField(TEXT("lastSlotsReceipt"),Receipt); // 内容本身有效，确保此负例真正只测旧版本门。
    TestFalse(TEXT("Old V9 cannot inject a new receipt object"),DemoCheckpointJsonCodec::Decode(Json,*Restored,Error));
    return !HasAnyErrors();
}
#endif
