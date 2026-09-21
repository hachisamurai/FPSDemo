#include "Save/DemoRunSave.h"
// 对应头文件先于依赖，保证UE独立编译能检查本类声明自包含。
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Debug/DemoLog.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"

bool UDemoRunSave::Validate() const
{
    DEMO_LOG_CALL();
    FGuid Id; // 本次临时解析，不依赖本地时间唯一性。
    if ((Version < 1 || Version > 5) || CreatedLocal.GetTicks() <= 0 || !FGuid::Parse(RunId, Id) || !Id.IsValid()
        || static_cast<uint8>(Difficulty) > 3 || PistolChallenge < 0 || PistolChallenge > 2 || BestEndlessLevel < 0 || (bEndless && Difficulty != EDemoDifficulty::Hell) || Coins < 0 || Coins > 100000000 || Kills < 0 || Kills > 10000000 || Purchases < 0 || Purchases > 100000
        || SilverCoins < 0 || SilverCoins > 100000000 || GoldPurchases < 0 || GoldPurchases > 100000 || SilverPurchases < 0 || SilverPurchases > 100000
        || (Version >= 2 && Purchases != GoldPurchases + SilverPurchases) // V1无分币种次数，先校验旧字段再迁移。
        || !FMath::IsFinite(MaxHealth) || MaxHealth < 100 || MaxHealth > 10000000 || !FMath::IsFinite(Health) || Health <= 0 || Health > MaxHealth
        || !FMath::IsFinite(DamageBonus) || DamageBonus < 0 || DamageBonus > 10000 || !FMath::IsFinite(MagazineBonus) || MagazineBonus < 0 || MagazineBonus > 10000
        || !FMath::IsFinite(HealAmount) || HealAmount < 35 || HealAmount > 100000 || !FMath::IsFinite(DashSpeed) || DashSpeed < 1300 || DashSpeed > 100000
        || (ActiveSlot != 1 && ActiveSlot != 2) || Weapons.Num() > 4)
    { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE invalid fields/version")); return false; }
    // V1..V4无挑战证明；拒绝伪装为旧格式携带新模式，V5无尽纪录不得低于已完成关数。
    if ((Version<5 && (bEndless || BestEndlessLevel!=0 || PistolChallenge!=0 || Difficulty==EDemoDifficulty::Hell))
        || (bEndless && (Phase==EDemoPhase::Reward || Phase==EDemoPhase::Intermission) && BestEndlessLevel<CompletedLevel))
    { UE_LOG(LogFPSDemo,Warning,TEXT("RUN_SAVE inconsistent challenge version/record")); return false; }
    if(Version>=4)
    {
        TSet<FName> AmmoIds; // 本槽稳定ID白名单与唯一性检查，拒绝伪造未解锁装备。
        for(FName AmmoId:UnlockedAmmoIds) { if(UDemoAmmoCatalog::IndexOf(AmmoId)<0||AmmoIds.Contains(AmmoId))return false; AmmoIds.Add(AmmoId); }
        if(!AmmoIds.Contains(TEXT("normal"))||!AmmoIds.Contains(SelectedAmmoId))return false;
    }
    // V3总属性必须包含永久部分，防止结算时凭空增加属性或丢失已购买来源。
    if (Version >= 3 && (!UpgradeProgress.Validate() || GoldPurchases != UpgradeProgress.Total(true) || SilverPurchases != UpgradeProgress.Total(false)
        || DamageBonus < UpgradeProgress.PermanentDamage || MaxHealth < 100.f + UpgradeProgress.PermanentHealth || MagazineBonus < UpgradeProgress.PermanentMagazine))
    { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE inconsistent permanent progression")); return false; }
    if (!((Phase == EDemoPhase::Hub && CompletedLevel == 0)
        || ((Phase == EDemoPhase::Reward || Phase == EDemoPhase::Intermission) && CompletedLevel >= 1 && (bEndless ? CompletedLevel < MAX_int32 : CompletedLevel < 10))
        || (Phase == EDemoPhase::Victory && CompletedLevel == 10 && !bEndless)))
    { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE invalid checkpoint phase/level")); return false; }
    TSet<FName> Seen; // 校验重复ID，阻止不一致库存。
    for (const FDemoSavedWeapon& Weapon : Weapons) // 数值快照由存档持有，只读借用。
    {
        if (DemoWeaponCatalog::IndexOf(Weapon.Id) < 0 || Seen.Contains(Weapon.Id) || Weapon.Ammo < 0 || Weapon.Ammo > 11000 || Weapon.Reserve < 0 || Weapon.Reserve > 100000)
        { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE invalid/duplicate weapon %s"), *Weapon.Id.ToString()); return false; }
        Seen.Add(Weapon.Id);
    }
    // 主槽不能指向未持有武器，当前槽为1时必须有可恢复主武器。
    const bool bValidLoadout = (PrimaryId.IsNone() || (DemoWeaponCatalog::IndexOf(PrimaryId) > 0 && Seen.Contains(PrimaryId))) && (ActiveSlot != 1 || !PrimaryId.IsNone());
    if (!bValidLoadout) UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE primary/active slot mismatch"));
    return bValidLoadout;
}
bool UDemoRunSave::UpgradeLegacy()
{
    DEMO_LOG_CALL();
    if (!Validate()) { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE migration rejected: invalid source")); return false; }
    if (Version < 3)
    {
        // V2只有币种总数，V1连币种也未知；V1只在Hub沿用历史兼容约定估算金币购买数。
        const int32 RefundCount = Version == 2 ? GoldPurchases : (Phase == EDemoPhase::Hub ? Purchases : 0); // 不把关间来源不明的V1成长转永久。
        const int64 Refund = 20LL * RefundCount + 5LL * RefundCount * (RefundCount - 1); // 旧共享价格20+30+...，64位避免极端旧档溢出。
        Coins = static_cast<int32>(FMath::Min<int64>(100000000, static_cast<int64>(Coins) + Refund));
        if (Version == 1) SilverCoins = 0;
        UpgradeProgress = FDemoUpgradeProgress(); // 来源不可还原，不伪造逐项等级；原GAS/技能合计留到本轮结束。
        Purchases = GoldPurchases = SilverPurchases = 0;
        Version = 3;
        UE_LOG(LogFPSDemo, Log, TEXT("RUN_SAVE migrated to V3 refund=%lld gold=%d; legacy growth temporary"), Refund, Coins);
    }
    if(Version<4){UnlockedAmmoIds={FName(TEXT("normal"))};SelectedAmmoId=TEXT("normal");Version=4;UE_LOG(LogFPSDemo,Log,TEXT("RUN_SAVE migrated V4 ammo normal"));}
    if (Version < 5) { PistolChallenge = CompletedLevel > 0 ? 2 : 0; bEndless = false; BestEndlessLevel = 0; Version = 5; UE_LOG(LogFPSDemo,Log,TEXT("RUN_SAVE migrated V5; old active runs cannot prove pistol challenge")); }
    return true;
}
void UDemoRunSave::ResetTemporaryGrowth()
{
    DEMO_LOG_CALL();
    UpgradeProgress.ResetTemporary();
    GoldPurchases = UpgradeProgress.Total(true); SilverPurchases = 0; Purchases = GoldPurchases;
    DamageBonus = UpgradeProgress.PermanentDamage; MagazineBonus = UpgradeProgress.PermanentMagazine;
    MaxHealth = 100.f + UpgradeProgress.PermanentHealth; Health = MaxHealth; // 重开/通关补满永久生命上限。
    HealAmount = 35.f; DashSpeed = 1300.f; // 免费关卡技能奖励仍属于本轮临时成长。
}
void UDemoRunSaves::ReportStatus(const FString& Message, bool bError)
{
    DEMO_LOG_CALL();
    Status = Message;
    // UE日志宏包含内部控制结构，分支使用花括号避免宏展开影响else匹配。
    if (bError) { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE %s"), *Status); }
    else { UE_LOG(LogFPSDemo, Log, TEXT("RUN_SAVE %s"), *Status); }
}
void UDemoRunSaves::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL();
    Super::Initialize(Collection);
    // 自动化入口统一隔离；测试随GI而非静态变量，跨OpenLevel仍使用同一前缀。
    bTest = FString(FCommandLine::Get()).Contains(TEXT("DemoSessionTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoSmokeTest"))
        || FString(FCommandLine::Get()).Contains(TEXT("DemoCampaignTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoWeaponTest"))
        || FString(FCommandLine::Get()).Contains(TEXT("DemoArmoryTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoUIValidation")) || FString(FCommandLine::Get()).Contains(TEXT("DemoEnemyAttackTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoAmmoTest"))
        || FString(FCommandLine::Get()).Contains(TEXT("DemoWeaponAnimationTest")); // 动画专项通过真实终端存档，必须沿用本GI临时前缀。
    Prefix = bTest ? TEXT("DemoRunTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) : TEXT("DemoRun");
#if WITH_EDITOR
    // 不能使用GIsEditor判断：UnrealEditor -game的GIsEditor为false，也必须遵守临时本地规则。
    bEditorSession = true;
    // PID供父Editor在强制结束Standalone子进程后清理，GUID区分同一进程内多个PIE GI。
    Prefix = FString::Printf(TEXT("DemoEditorRun_%u_%s"), FPlatformProcess::GetCurrentProcessId(), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    UE_LOG(LogFPSDemo, Log, TEXT("EDITOR_SESSION run saves=%s; local only, delete on GI shutdown"), *Prefix);
#elif !UE_BUILD_SHIPPING
    FString CloudTest; FGuid CloudId; // 两阶段真实HTTP回归只允许GUID命名的独立槽，不使用玩家文件。
    if (FParse::Value(FCommandLine::Get(), TEXT("DemoCloudTestId="), CloudTest) && FGuid::Parse(CloudTest, CloudId) && CloudId.IsValid())
    { Prefix = TEXT("DemoCloudTest_") + CloudId.ToString(EGuidFormats::Digits) + TEXT("_Run"); bTest = false; }
#endif
    RefreshSlots();
}
void UDemoRunSaves::Deinitialize()
{
    DEMO_LOG_CALL();
    if (bEditorSession || bTest)
    {
        for (int32 Index = 0; Index < 3; ++Index) // 仅删除本GI拥有的三个槽，不能按通配符清理其他PIE实例。
        {
            for (int32 Backup = 0; Backup < 2; ++Backup) // 0主档、1备份；备份也必须移除，防止下次恢复旧数据。
            {
                const FString OwnedSlot = SlotName(Index) + (Backup ? TEXT("_Backup") : TEXT("")); // 固定槽名，不接受外部路径。
                if (!UGameplayStatics::DoesSaveGameExist(OwnedSlot, 0)) continue;
                const bool bDeleted = UGameplayStatics::DeleteGameInSlot(OwnedSlot, 0); // 平台SaveGame API负责实际文件删除。
                if (bDeleted) { UE_LOG(LogFPSDemo, Log, TEXT("SESSION_SAVE_CLEANUP %s"), *OwnedSlot); }
                else { UE_LOG(LogFPSDemo, Error, TEXT("SESSION_SAVE_CLEANUP failed %s"), *OwnedSlot); }
            }
        }
    }
    // 清空值对象引用和跨World恢复意图；后续GI永远重新创建，不能继承这次试玩的内存状态。
    Slots.Empty(); Exists.Empty(); DetachedRestart = nullptr; ActiveSlot = INDEX_NONE; bPending = false; // 同步销毁无槽开发重开的永久账本。
    Super::Deinitialize();
}
FString UDemoRunSaves::SlotName(int32 Index) const { DEMO_LOG_TICK(); return FString::Printf(TEXT("%s_%d"), *Prefix, Index); }
void UDemoRunSaves::RefreshSlots()
{
    DEMO_LOG_CALL();
    Slots.SetNum(3);
    Exists.SetNum(3);
    for (int32 Index = 0; Index < 3; ++Index) // 每槽独立失败，不阻止其他有效槽选择。
    {
        Exists[Index] = UGameplayStatics::DoesSaveGameExist(SlotName(Index), 0) || UGameplayStatics::DoesSaveGameExist(SlotName(Index)+TEXT("_Backup"), 0);
        Slots[Index] = Exists[Index] ? Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(SlotName(Index), 0)) : nullptr;
        if (Slots[Index] && Slots[Index]->Version > 5) { Slots[Index] = nullptr; ReportStatus(TEXT("存档版本较新，原文件已保护"), true); continue; }
        if (Exists[Index] && (!Slots[Index] || !Slots[Index]->UpgradeLegacy()))
        {
            Slots[Index] = Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(SlotName(Index)+TEXT("_Backup"), 0));
            if (!Slots[Index] || !Slots[Index]->UpgradeLegacy()) { Slots[Index] = nullptr; ReportStatus(TEXT("存档损坏，已保护原文件；请选择其他栏位"), true); }
            else ReportStatus(TEXT("主存档异常，已恢复有效备份"), true);
        }
    }
}
const UDemoRunSave* UDemoRunSaves::GetSlot(int32 Index) const { DEMO_LOG_TICK(); return Slots.IsValidIndex(Index) ? Slots[Index] : nullptr; }
bool UDemoRunSaves::SlotExists(int32 Index) const { DEMO_LOG_TICK(); return Exists.IsValidIndex(Index) && Exists[Index]; }
int32 UDemoRunSaves::GetActiveSlot() const { DEMO_LOG_TICK(); return ActiveSlot; }
const FString& UDemoRunSaves::GetStatus() const { DEMO_LOG_TICK(); return Status; }
uint64 UDemoRunSaves::GetChangeSerial() const { DEMO_LOG_TICK(); return ChangeSerial; }
bool UDemoRunSaves::CreateSlot(int32 Index)
{
    DEMO_LOG_CALL();
    RefreshSlots();
    if (!Slots.IsValidIndex(Index) || SlotExists(Index)) { ReportStatus(TEXT("栏位无效或已存在，不能覆盖创建"), true); return false; }
    UDemoRunSave* Fresh = NewObject<UDemoRunSave>(this); // 新文件值对象，创建成功前不改变当前槽。
    Fresh->CreatedLocal = FDateTime::Now();
    Fresh->SavedUtc = FDateTime::UtcNow();
    Fresh->RunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    if (!UGameplayStatics::SaveGameToSlot(Fresh, SlotName(Index), 0)) { ReportStatus(TEXT("创建存档失败，请检查磁盘空间/写入权限"), true); return false; }
    Slots[Index] = Fresh; Exists[Index] = true; ActiveSlot = Index; bPending = true;
    ++ChangeSerial; // 成功写盘即显示待同步，不等下一次HTTP轮询才更新提示。
    ReportStatus(TEXT("新存档已创建"));
    return true;
}
bool UDemoRunSaves::SelectSlot(int32 Index)
{
    DEMO_LOG_CALL();
    RefreshSlots();
    if (!GetSlot(Index)) { ReportStatus(TEXT("存档不可读取，未进入游戏"), true); return false; }
    ActiveSlot = Index; bPending = true;
    return true;
}
const UDemoRunSave* UDemoRunSaves::ConsumePending()
{
    DEMO_LOG_CALL();
    if (!bPending) { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE no pending resume request")); return nullptr; }
    bPending = false;
    return ActiveSlot == INDEX_NONE ? DetachedRestart.Get() : GetSlot(ActiveSlot); // 开发World的无槽重开也恢复同样的永久账本。
}
bool UDemoRunSaves::Store(UDemoRunSave* Snapshot)
{
    DEMO_LOG_CALL();
    if (ActiveSlot == INDEX_NONE) return true; // 无槽仅供原有独立World测试，不生成玩家档。
    if (!Snapshot || !Snapshot->Validate() || !GetSlot(ActiveSlot)) { ReportStatus(TEXT("检查点校验失败，未覆盖存档"), true); return false; }
    Snapshot->CreatedLocal = Slots[ActiveSlot]->CreatedLocal;
    Snapshot->SavedUtc = FDateTime::UtcNow();
    // 仅已验证的主档轮换到备份，损坏主档不能覆盖有效备份。
    UDemoRunSave* Previous = Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(SlotName(ActiveSlot), 0)); // 本次写盘前同步借用的旧有效值对象。
    if (Previous && Previous->Validate() && !UGameplayStatics::SaveGameToSlot(Previous, SlotName(ActiveSlot)+TEXT("_Backup"), 0))
    { ReportStatus(TEXT("检查点备份保存失败，请重试"), true); return false; }
    if (!UGameplayStatics::SaveGameToSlot(Snapshot, SlotName(ActiveSlot), 0)) { ReportStatus(TEXT("检查点保存失败，请重试后离开"), true); return false; }
    Slots[ActiveSlot] = DuplicateObject<UDemoRunSave>(Snapshot, this);
    ++ChangeSerial; // 区分上传期间产生的新检查点，旧回执不能把它误报为已同步。
    ReportStatus(TEXT("检查点已自动保存"));
    // 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
    UE_LOG(LogFPSDemo, Display, TEXT("RUN_SAVE slot=%d phase=%d completed=%d"), ActiveSlot, static_cast<int32>(Snapshot->Phase), Snapshot->CompletedLevel);
    return true;
}
bool UDemoRunSaves::ResetActive(EDemoDifficulty Difficulty, int32 Gold, const FDemoUpgradeProgress& Progress, const UDemoRunSave* Loadout)
{
    DEMO_LOG_CALL();
    if (!Progress.Validate()) { ReportStatus(TEXT("永久成长账本无效，取消重置"), true); return false; }
    if (Gold < 0 || Gold > 100000000) { ReportStatus(TEXT("金币余额无效，取消重置"), true); return false; }
    if (ActiveSlot != INDEX_NONE && !GetSlot(ActiveSlot)) { ReportStatus(TEXT("当前存档不可读取，取消重置"), true); return false; } // 外部损坏不能导致空指针或静默覆写。
    UDemoRunSave* Fresh = NewObject<UDemoRunSave>(this); // 新局快照，保留同一栏位创建时间。
    Fresh->CreatedLocal = ActiveSlot == INDEX_NONE ? FDateTime::Now() : Slots[ActiveSlot]->CreatedLocal;
    Fresh->RunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    Fresh->Difficulty = Difficulty;
    if(const UDemoRunSave* Previous=GetSlot(ActiveSlot)) { Fresh->UnlockedAmmoIds=Previous->UnlockedAmmoIds; Fresh->SelectedAmmoId=Previous->SelectedAmmoId; } // 已提交购买/装配随同槽保留。
    if (const UDemoRunSave* Previous = GetSlot(ActiveSlot)) { Fresh->bEndless = Previous->bEndless; Fresh->BestEndlessLevel = Previous->BestEndlessLevel; } // 重开保留模式与已通关纪录，手枪资格回到0。
    Fresh->Coins = Gold; // 新快照只清空银币与关卡，不丢失金币余额或永久来源。
    Fresh->UpgradeProgress = Progress;
    Fresh->ResetTemporaryGrowth();
    // 重开仅重置战斗成长；装备由同槽快照或当前Pawn提供，不能用跨槽的账号偏好覆盖实际装备。
    const UDemoRunSave* Equipment = Loadout ? Loadout : GetSlot(ActiveSlot); // 同步借用，不保留旧World/UObject引用。
    if (Equipment)
    {
        Fresh->Weapons = Equipment->Weapons;
        Fresh->PrimaryId = Equipment->PrimaryId;
        Fresh->ActiveSlot = Equipment->ActiveSlot;
        Fresh->UnlockedAmmoIds = Equipment->UnlockedAmmoIds;
        Fresh->SelectedAmmoId = Equipment->SelectedAmmoId;
    }
    if (!Fresh->Validate()) { ReportStatus(TEXT("重开装备快照无效，已保留原存档"), true); return false; } // 无活动槽也必须校验。
    if (!Store(Fresh)) return false;
    UE_LOG(LogFPSDemo, Log, TEXT("RUN_RESET_LOADOUT primary=%s active=%d weapons=%d"), *Fresh->PrimaryId.ToString(), Fresh->ActiveSlot, Fresh->Weapons.Num());
    if (ActiveSlot == INDEX_NONE) DetachedRestart = Fresh; // 只由GI持有直到重开/结束试玩，不创建匿名磁盘档。
    bPending = true;
    return true;
}

bool UDemoRunSaves::StoreChallenge(int32 ChallengeStatus, int32 Best)
{
    DEMO_LOG_CALL();
    if (ChallengeStatus<0 || ChallengeStatus>2 || Best<0) { UE_LOG(LogFPSDemo,Warning,TEXT("Challenge checkpoint rejected: range")); return false; }
    const UDemoRunSave* Previous=GetSlot(ActiveSlot); // 借用出发前快照，不采集半场金币/生命/敌人。
    if (ActiveSlot==INDEX_NONE) return true; // 无槽开发World只保留GameState内存。
    if (!Previous) { UE_LOG(LogFPSDemo,Warning,TEXT("Challenge checkpoint missing")); return false; }
    if (Previous->PistolChallenge==ChallengeStatus && Previous->BestEndlessLevel>=Best) return true;
    UDemoRunSave* Snapshot=DuplicateObject<UDemoRunSave>(Previous,this); // Store另存值副本，原检查点不被未落盘写入污染。
    Snapshot->PistolChallenge=FMath::Max(Previous->PistolChallenge,ChallengeStatus); Snapshot->BestEndlessLevel=FMath::Max(Previous->BestEndlessLevel,Best);
    return Store(Snapshot);
}

TSharedPtr<FJsonObject> UDemoRunSaves::ExportCloud(int32 Index) const
{
    DEMO_LOG_CALL();
    const UDemoRunSave* Snapshot = GetSlot(Index); // GI拥有缓存，导出期间不跨线程借用。
    if (!Snapshot || !Snapshot->Validate()) return nullptr;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>(); // 独立值DTO，不含UObject身份或蓝图路径。
    if (!FJsonObjectConverter::UStructToJsonObject(UDemoRunSave::StaticClass(), Snapshot, Json, 0, 0)) return nullptr;
    // 时间/枚举/None明确规范化，避免UE版本序列化差异成为网络协议。
    Json->SetStringField(TEXT("createdLocal"), Snapshot->CreatedLocal.ToIso8601());
    Json->SetStringField(TEXT("savedUtc"), Snapshot->SavedUtc.ToIso8601());
    Json->SetStringField(TEXT("difficulty"), Snapshot->Difficulty == EDemoDifficulty::Hell ? TEXT("hell") : Snapshot->Difficulty == EDemoDifficulty::Easy ? TEXT("easy") : Snapshot->Difficulty == EDemoDifficulty::Normal ? TEXT("normal") : TEXT("hard"));
    Json->SetStringField(TEXT("primaryId"), Snapshot->PrimaryId.IsNone() ? TEXT("") : Snapshot->PrimaryId.ToString());
    // 非Editor的FName不保留输入大小写，normal可能复用先注册的Normal（难度名）。协议ID必须显式规范化，不能依赖Editor中的ToString表现。
    TArray<TSharedPtr<FJsonValue>> AmmoIds; // 当前导出独占的JSON值数组，与金币共同进入本次请求快照。
    for(const FName Id : Snapshot->UnlockedAmmoIds)AmmoIds.Add(MakeShared<FJsonValueString>(Id.ToString().ToLower())); // Validate已限制为稳定白名单，输出统一小写。
    Json->SetArrayField(TEXT("unlockedAmmoIds"),AmmoIds);
    Json->SetStringField(TEXT("selectedAmmoId"),Snapshot->SelectedAmmoId.ToString().ToLower());
    return Json;
}
bool UDemoRunSaves::ImportCloud(int32 Index, const TSharedPtr<FJsonObject>& Data)
{
    DEMO_LOG_CALL();
    if (Index < 0 || Index > 2 || !Data) return false;
    UDemoRunSave* Candidate = NewObject<UDemoRunSave>(this); // 临时值对象，验证成功之前不影响游戏或本地槽。
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>(*Data); // 不修改同步器持有的远端原始快照。
    FString Difficulty; // 协议小写稳定键映射回UE枚举。
    FString Primary; // 空字符串明确代表没有装备主枪。
    FString Created; // 时间必须手工严格检查，损坏云端数据不能覆盖本地。
    FString Saved; // UTC时间仅作展示，冲突处理使用服务器版本。
    if (!Json->TryGetStringField(TEXT("difficulty"), Difficulty) || !Json->TryGetStringField(TEXT("primaryId"), Primary)
        || !Json->TryGetStringField(TEXT("createdLocal"), Created) || !Json->TryGetStringField(TEXT("savedUtc"), Saved)
        || (Difficulty != TEXT("easy") && Difficulty != TEXT("normal") && Difficulty != TEXT("hard") && Difficulty != TEXT("hell"))) return false;
    Json->SetStringField(TEXT("difficulty"), Difficulty == TEXT("hell") ? TEXT("Hell") : Difficulty == TEXT("easy") ? TEXT("Easy") : Difficulty == TEXT("normal") ? TEXT("Normal") : TEXT("Hard"));
    Json->SetStringField(TEXT("primaryId"), Primary.IsEmpty() ? TEXT("None") : Primary);
    if (!FJsonObjectConverter::JsonObjectToUStruct(Json, UDemoRunSave::StaticClass(), Candidate, 0, 0)
        || !FDateTime::ParseIso8601(*Created, Candidate->CreatedLocal) || !FDateTime::ParseIso8601(*Saved, Candidate->SavedUtc) || !Candidate->UpgradeLegacy())
    { ReportStatus(TEXT("云端检查点格式无效，已保留本地存档"), true); return false; }
    RefreshSlots();
    if (SlotExists(Index) && !GetSlot(Index)) { ReportStatus(TEXT("本地损坏/未来版本存档受保护，未覆盖"), true); return false; }
    if (GetSlot(Index) && !UGameplayStatics::SaveGameToSlot(Slots[Index], SlotName(Index) + TEXT("_Backup"), 0)) return false;
    if (!UGameplayStatics::SaveGameToSlot(Candidate, SlotName(Index), 0)) { ReportStatus(TEXT("云端检查点写入本地失败"), true); return false; }
    Slots[Index] = Candidate; Exists[Index] = true;
    ++ChangeSerial; // 导入同样改变本地缓存，同步器完成哈希确认后再显示已同步。
    ReportStatus(TEXT("云端检查点已下载，选择栏位后继续"));
    return true;
}
