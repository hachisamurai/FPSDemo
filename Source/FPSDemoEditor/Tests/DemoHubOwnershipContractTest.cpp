// 职责：HUB-SHOPS来源/JSON与本地耐久失败纯合同；Editor Automation运行，不创建World、不PIE、不访问HTTP，临时GUID文件结束清理。
#include "Misc/AutomationTest.h"
#include "Contracts/DemoWeaponOwnership.h"
#include "Save/Codecs/DemoOwnershipJson.h"
#include "Save/DemoDurableSave.h"
#include "Save/DemoRunSave.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoStaticLog.h" // 非UObject测试/静态helper使用统一入口宏，默认Editor可见。
#include "Dom/JsonObject.h"
#include "Player/DemoPlayerProfile.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace
{
    /** 检查差分fixture确实没有目标ASCII属性tag，不将显式旧版本值误当缺省兼容证明。 */
    bool HasAsciiTag(const TArray<uint8>& Bytes,const ANSICHAR* Tag)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        const int32 Length=FCStringAnsi::Strlen(Tag); // UE字符串代理对ASCII属性名保存ANSI字节。
        for (int32 Index=0;Index+Length<=Bytes.Num();++Index) if (FMemory::Memcmp(Bytes.GetData()+Index,Tag,Length)==0) return true;
        return false;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoHubOwnershipContractTest,"FPSDemo.HubShops.OwnershipAndDurableSave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDemoHubOwnershipContractTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    TArray<uint8> OldRunBytes,OldProfileBytes; // 模拟旧CDO9/3与原UObject差分writer，不调用本轮完整Serialize写法。
    {
        TGuardValue<int32> OldDefault(GetMutableDefault<UDemoRunSave>()->Version,9); // 同步测试临时CDO值，出作用域恢复10。
        UDemoRunSave* Old=NewObject<UDemoRunSave>(); Old->Version=9; Old->Coins=73;
        FMemoryWriter Writer(OldRunBytes,true); FObjectAndNameAsStringProxyArchive Archive(Writer,false);
        Old->USaveGame::Serialize(Archive); // 明确绕开本轮覆写，复现UE5.4 SaveGameToMemory原差分语义。
    }
    TestFalse(TEXT("Old V9 fixture omits Version default tag"),HasAsciiTag(OldRunBytes,"Version"));
    UDemoRunSave* OldRunLoaded=NewObject<UDemoRunSave>();
    { FMemoryReader Reader(OldRunBytes,true); FObjectAndNameAsStringProxyArchive Archive(Reader,true); OldRunLoaded->Serialize(Archive); }
    TestTrue(TEXT("Missing old Version tag loads as 9, preserves wallet"),OldRunLoaded->Version==9&&OldRunLoaded->Coins==73);
    {
        TGuardValue<int32> OldDefault(GetMutableDefault<UDemoProfileSave>()->Data.SchemaVersion,3); // 同步且精确恢复当前Profile4 CDO。
        UDemoProfileSave* Old=NewObject<UDemoProfileSave>(); Old->Data.SchemaVersion=3; Old->Data.ProfileId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); Old->Data.LocalRevision=7;
        FMemoryWriter Writer(OldProfileBytes,true); FObjectAndNameAsStringProxyArchive Archive(Writer,false); Old->USaveGame::Serialize(Archive);
    }
    TestFalse(TEXT("Old Profile3 fixture omits SchemaVersion default tag"),HasAsciiTag(OldProfileBytes,"SchemaVersion"));
    UDemoProfileSave* OldProfileLoaded=NewObject<UDemoProfileSave>();
    { FMemoryReader Reader(OldProfileBytes,true); FObjectAndNameAsStringProxyArchive Archive(Reader,true); OldProfileLoaded->Serialize(Archive); }
    TestTrue(TEXT("Missing old schema tag loads as 3, keeps pending revision"),OldProfileLoaded->Data.SchemaVersion==3&&OldProfileLoaded->Data.LocalRevision==7);
    TestEqual(TEXT("Run CDO retains new default"),GetDefault<UDemoRunSave>()->Version,10);
    TestEqual(TEXT("Profile CDO retains new default"),GetDefault<UDemoProfileSave>()->Data.SchemaVersion,4);
    TArray<uint8> NewRunBytes; // 新默认也必须显式保存Version=10，下一版CDO不能重新解释。
    UDemoRunSave* NewRun=NewObject<UDemoRunSave>(); UGameplayStatics::SaveGameToMemory(NewRun,NewRunBytes);
    TestTrue(TEXT("V10 writer emits explicit default version tag"),HasAsciiTag(NewRunBytes,"Version"));
    UDemoRunSave* NewRunLoaded=Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromMemory(NewRunBytes)); TestTrue(TEXT("Explicit V10 loads as V10"),NewRunLoaded&&NewRunLoaded->Version==10);
    FDemoWeaponOwnership Value; // 与Profile冻结源无关的隔离测试候选。
    const FString Migration=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    TestTrue(TEXT("Freeze empty legacy still grants pistol"),Value.FreezeLegacy({FName(TEXT("pistol"))},Migration,7)&&Value.Contains(TEXT("pistol")));
    TestTrue(TEXT("Repeated freeze does not infer new clear rights"),Value.FreezeLegacy({FName(TEXT("pistol")),FName(TEXT("rifle"))},Migration,7)&&!Value.Contains(TEXT("rifle")));
    TestTrue(TEXT("Editor zero charged purchase accepted"),Value.AddPurchased(TEXT("rifle"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens),0,1));
    TestFalse(TEXT("Owned weapon cannot be added twice"),Value.AddPurchased(TEXT("rifle"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens),500,1));
    FDemoWeaponShopReceipt Receipt; // 成功收据包含实际扣款0，水位仍消费，不能下一次又执行。
    Receipt.RequestId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); Receipt.PayloadHash=TEXT("sha1:0000000000000000000000000000000000000000"); Receipt.CommandSerial=1; Receipt.WeaponId=TEXT("rifle"); Receipt.OwnershipRevision=Value.Revision; Receipt.bBought=true;
    Value.ShopSerial=1; Value.ShopReceipts.Add(Receipt); TestTrue(TEXT("Zero charged receipt with consumed serial validates"),Value.Validate(true));
    FDemoWeaponOwnership Copy; // JSON必须保留完整来源和高水位。
    const TSharedRef<FJsonObject> Json=DemoOwnershipJson::Encode(Value);
    TestTrue(TEXT("Ownership strict JSON roundtrip"),DemoOwnershipJson::Decode(Json,Copy)&&Copy.ShopSerial==1&&Copy.Contains(TEXT("rifle"))&&Copy.Entries[0].bInitialSupplyGranted);
    Json->SetNumberField(TEXT("unknownFutureField"),1); TestFalse(TEXT("Unknown ownership field rejected"),DemoOwnershipJson::Decode(Json,Copy));
    FDemoWeaponOwnership Invalid=Value; Invalid.ShopSerial=0; TestFalse(TEXT("Receipt cannot exceed high water"),Invalid.Validate(true));
    Invalid=Value; Invalid.Entries[0].bInitialSupplyGranted=false; TestFalse(TEXT("Paid source must consume one-time supply"),Invalid.Validate(true));
    const FString Slot=TEXT("DemoHubOwnershipContract_")+FGuid::NewGuid().ToString(EGuidFormats::Digits); // 测试独占文件，不加载/修改真实玩家槽。
    UDemoRunSave* Snapshot=NewObject<UDemoRunSave>(); // 只用于验证SaveGame耐久字节；没有World、Player或云服务。
    Snapshot->Coins=111;
    const bool bInitial=DemoDurableSave::Write(Snapshot,Slot); TestTrue(TEXT("Initial durable local write"),bInitial);
    if (bInitial)
    {
        Snapshot->Coins=222;
        for (int32 Failure=1;Failure<=2;++Failure) // 分别注入Flush前和rename前失败，正式文件必须仍是111。
        {
            DemoDurableSave::SetFailureForTest(Failure); TestFalse(TEXT("Injected stage/replace failure"),DemoDurableSave::Write(Snapshot,Slot)); DemoDurableSave::SetFailureForTest(0);
            UDemoRunSave* Loaded=Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(Slot,0)); // 通过正常UE加载正式文件，未采纳.pending。
            TestTrue(TEXT("Old complete committed state survives failure"),Loaded&&Loaded->Coins==111);
        }
        TestTrue(TEXT("Retry commits exact new local state"),DemoDurableSave::Write(Snapshot,Slot));
        const UDemoRunSave* Loaded=Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(Slot,0)); TestTrue(TEXT("Normal loader sees new complete state"),Loaded&&Loaded->Coins==222);
    }
    DemoDurableSave::SetFailureForTest(0); DemoDurableSave::RemoveStaging(Slot); UGameplayStatics::DeleteGameInSlot(Slot,0);
    return !HasAnyErrors(); // 只证明上述合同，绝非实际购买/多人/游戏/Cook验收。
}
