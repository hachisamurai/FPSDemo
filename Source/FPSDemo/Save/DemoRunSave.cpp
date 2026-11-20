// 职责：V1..V10检查点、一次枪权冻结与本地整槽耐久提交；使用RunFlow/个人来源和固定成员，Editor临时归档停止精确清理。
#include "Save/DemoRunSave.h"
// 对应头文件先于依赖，保证UE独立编译能检查本类声明自包含。
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Debug/DemoLog.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Dom/JsonObject.h"
#include "Save/Codecs/DemoCheckpointJsonCodec.h" // 云协议字段白名单与本地SaveGame结构分离。
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "Game/Content/DemoCampaignDefinition.h" // 旧轮冻结快照自校验，不以当前作者资产重建历史配置。
#include "Player/DemoPlayerProfile.h" // Legacy只从已持久冻结Profile导入，不读取Editor调试权。
#include "Save/DemoDurableSave.h" // 暂存/刷新/原子替换；云Outbox不参与本地发布点。
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h" // Windows本地临时文件Flush后原子rename；不删除旧正式文件再写。
#endif

namespace
{
    /** 冻结旧库存的供弹消费；仅已有真实库存置true，未领旧枪保留一次资格。 */
    void MarkExistingSupply(FDemoWeaponOwnership& Ownership, const TArray<FDemoSavedWeapon>& Weapons)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] MarkExistingSupply"));
        for (FDemoWeaponOwnershipEntry& Entry:Ownership.Entries) for (const FDemoSavedWeapon& Weapon:Weapons) if (Entry.WeaponId==Weapon.Id) Entry.bInitialSupplyGranted=true;
    }
    /** 旧槽在本GI已持久Profile冻结后整体迁移；失败保留原盘，不推算根库存为全部枪权。 */
    bool FreezeSlotOwnership(UDemoRunSave& Data, UGameInstance* Instance)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FreezeSlotOwnership"));
        if (Data.Version>=10) return Data.Validate();
        const UDemoPlayerProfile* Profile=Instance?Instance->GetSubsystem<UDemoPlayerProfile>():nullptr; // 不从调试覆盖取旧权。
        const UDemoCoopSessionSubsystem* Session=Instance?Instance->GetSubsystem<UDemoCoopSessionSubsystem>():nullptr; // 原固定队伍键优先，不按RunId猜身份。
        if (!Profile || !Session || !Profile->MakeLegacyOwnership(Data.Ownership)) return false;
        for (FDemoCoopPlayerCheckpoint& Member:Data.TeamMembers) { FGuid Key; if (!FGuid::Parse(Member.PlayerKey,Key)||!Key.IsValid()) return false; Member.PlayerKey=Key.ToString(EGuidFormats::DigitsWithHyphens); } // 旧.NET小写键恢复UE规范；不改GUID语义，原字节先归档。
        if (!Data.OnlineOwnerKey.IsEmpty()) { FGuid Key; if (!FGuid::Parse(Data.OnlineOwnerKey,Key)||!Key.IsValid()) return false; Data.OnlineOwnerKey=Key.ToString(EGuidFormats::DigitsWithHyphens); }
        Data.SlotStorageKey=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        Data.OwnerPlayerKey=Data.TeamMembers.IsEmpty()?Session->GetLocalPlayerKey():Data.TeamMembers[0].PlayerKey;
        MarkExistingSupply(Data.Ownership,Data.Weapons);
        for (int32 Index=0;Index<Data.TeamMembers.Num();++Index)
        {
            FDemoCoopPlayerCheckpoint& Member=Data.TeamMembers[Index]; // 整槽候选独占，不修改活体PS。
            if (Index==0)
            {
                TSet<FName> Expected,Actual; // 旧根与房主成员冲突保护原档。
                for (const FName Id:Data.Ownership.EffectiveIds()) Expected.Add(Id);
                for (const FName Id:Member.Progress.UnlockedWeaponIds) Actual.Add(Id);
                if (Expected.Num()!=Actual.Num()) return false;
                for (const FName Id:Expected) if (!Actual.Contains(Id)) return false;
                Member.Progress.Ownership=Data.Ownership;
            }
            else if (!Member.Progress.Ownership.FreezeLegacy(Member.Progress.UnlockedWeaponIds,FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens),Member.Progress.Revision)) return false;
            for (FDemoWeaponOwnershipEntry& Entry:Member.Progress.Ownership.Entries) for (const FDemoWeaponSnapshot& Weapon:Member.Loadout.Weapons) if (Entry.WeaponId==Weapon.Id) Entry.bInitialSupplyGranted=true;
            Member.Progress.UnlockedWeaponIds=Member.Progress.Ownership.EffectiveIds();
        }
        Data.Version=10; return Data.Validate(); // 一次整体写盘后才可购物，失败整个候选丢弃。
    }
    /** Data为刚从盘读取的借用对象；未知未来结构/内容禁止回退旧备份后覆盖，不将其误判为损坏。 */
    bool HasUnsupportedContent(const UDemoRunSave* Data)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        if (!Data) return false;
        return Data->Version > 10 || (!Data->CampaignId.IsNone() && Data->CampaignId != TEXT("steam_demo"))
            || (Data->Version == 8 && Data->ContentRevision > 1)
            || (Data->Version >= 9 && UDemoCampaignDefinition::HasUnsupportedSnapshotSchema(Data->ContentSnapshot));
    }
    /** Build为只读个人值；Phase/Completed为队伍检查点，bCurrent为房主；离线成员可停留Hub未选/未锁或旧待选，不能提前领取。 */
    bool ValidateBuildCheckpoint(const FDemoRunBuildSnapshot& Build, EDemoPhase Phase, int32 Completed, bool bCurrent)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        if (!Build.Validate()) return false;
        if (Phase == EDemoPhase::Hub || Phase == EDemoPhase::Victory)
            return Build.Branch3.IsNone() && Build.Branch7.IsNone() && Build.PendingBranchStage == 0; // Hub可尚未选核心；结算已清轮次成长。
        if ((Completed < 3 && !Build.Branch3.IsNone())
            || (Completed < 7 && !Build.Branch7.IsNone()) || Build.PendingBranchStage > Completed) return false;
        if (!bCurrent) return true; // 离线成员可能在Hub未选/未锁即离开；Build.Validate保证空/未锁不夹带分支或待选，不能阻断在线队伍落盘。
        if (Build.CoreId.IsNone() || !Build.bLocked) return false; // 房主当前节点严格要求锁定核心；存档成员无连接状态，在线出发门禁由Flow负责。
        if (Build.PendingBranchStage != 0 && (Phase != EDemoPhase::Reward || Build.PendingBranchStage != Completed)) return false;
        if (Completed >= 3 && Build.Branch3.IsNone() && !(Phase == EDemoPhase::Reward && Completed == 3 && Build.PendingBranchStage == 3)) return false;
        if (Completed >= 7 && Build.Branch7.IsNone() && !(Phase == EDemoPhase::Reward && Completed == 7 && Build.PendingBranchStage == 7)) return false;
        return true;
    }
}

void UDemoRunSave::Serialize(FArchive& Ar)
{
    DEMO_LOG_CALL();
    if (HasAnyFlags(RF_ClassDefaultObject)) { Super::Serialize(Ar); return; } // 原生CDO保留当前版本10，只修真实存档对象的旧tag缺省。
    if (Ar.IsLoading()) Version=9; // 旧V9差分文件可能完全没有Version属性；显式旧/未来tag仍会覆盖本缺省。
    const bool bPrevious=Ar.ArNoDelta; // 同栈暂存归调用者所有的归档选项，不能改变后续对象保存策略。
    if (Ar.IsSaving()) Ar.ArNoDelta=true; // V10以后版本与所有来源字段都显式持久，不依赖未来CDO默认。
    Super::Serialize(Ar); Ar.ArNoDelta=bPrevious;
}
bool UDemoRunSave::Validate() const
{
    DEMO_LOG_CALL();
    FGuid OnlineOwner; // 可选在线关联键不是认证凭据；只接受V5的规范格式，云端同样校验。
    if (!OnlineOwnerKey.IsEmpty() && (Version < 5 || !FGuid::Parse(OnlineOwnerKey, OnlineOwner) || !OnlineOwner.IsValid() || !OnlineOwner.ToString(EGuidFormats::DigitsWithHyphens).Equals(OnlineOwnerKey,ESearchCase::IgnoreCase))) return false;
    if (TeamMembers.Num() > 4) { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE invalid team size")); return false; }
    TSet<FString> MemberKeys; // 整队恢复前先验证稳定身份唯一；个人校验的临时V5对象不含TeamMembers，避免递归。
    for (const FDemoCoopPlayerCheckpoint& Member : TeamMembers)
    {
        if (MemberKeys.Contains(Member.PlayerKey.ToUpper()) || !UDemoCoopRosterComponent::ValidatePlayer(Member)) { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE invalid team member")); return false; }
        MemberKeys.Add(Member.PlayerKey.ToUpper());
    }
    FGuid Id; // 本次临时解析，不依赖本地时间唯一性。
    if (!Gambling.Validate()) return false; // V6固定白名单及边界，在恢复GAS前验证。
    if ((Version < 1 || Version > 10) || CreatedLocal.GetTicks() <= 0 || !FGuid::Parse(RunId, Id) || !Id.IsValid()
        || static_cast<uint8>(Difficulty) > 3 || PistolChallenge < 0 || PistolChallenge > 2 || BestEndlessLevel < 0 || (bEndless && Difficulty != EDemoDifficulty::Hell) || Coins < 0 || Kills < 0 || Kills > 10000000 || Purchases < 0 || Purchases > 100000
        || SilverCoins < 0 || SilverCoins > 100000000 || GoldPurchases < 0 || GoldPurchases > 100000 || SilverPurchases < 0 || SilverPurchases > 100000
        || (Version >= 2 && Purchases != GoldPurchases + SilverPurchases) // V1无分币种次数，先校验旧字段再迁移。
        || !FMath::IsFinite(MaxHealth) || MaxHealth < 100 || MaxHealth > 10000000 || !FMath::IsFinite(Health) || Health <= 0 || Health > MaxHealth
        || !FMath::IsFinite(DamageBonus) || DamageBonus < 0 || DamageBonus > 10000 || !FMath::IsFinite(MagazineBonus) || MagazineBonus < 0 || MagazineBonus > 10000
        || !FMath::IsFinite(HealAmount) || HealAmount < 35 || HealAmount > 100000 || !FMath::IsFinite(DashSpeed) || DashSpeed < 1300 || DashSpeed > 100000
        || (ActiveSlot != 1 && ActiveSlot != 2) || Weapons.Num() > 4)
    { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE invalid fields/version")); return false; }
    if (Version>=10)
    {
        FGuid Storage,Owner; // 持久槽身份独立于本轮RunId；成员0为根镜像所有人。
        if (SlotStorageKey.Len()!=36 || !FGuid::Parse(SlotStorageKey,Storage)||!Storage.IsValid()||!Storage.ToString(EGuidFormats::DigitsWithHyphens).Equals(SlotStorageKey,ESearchCase::IgnoreCase)
            ||OwnerPlayerKey.Len()!=36 || !FGuid::Parse(OwnerPlayerKey,Owner)||!Owner.IsValid()||!Owner.ToString(EGuidFormats::DigitsWithHyphens).Equals(OwnerPlayerKey,ESearchCase::IgnoreCase)||!Ownership.Validate(true)) return false;
        for (const FDemoSavedWeapon& Weapon:Weapons) if (!Ownership.Contains(Weapon.Id)) return false;
        for (const FDemoCoopPlayerCheckpoint& Member:TeamMembers) if (!Member.Progress.Ownership.Validate(true)) return false;
        for (const FDemoWeaponOwnershipEntry& Entry:Ownership.Entries)
        {
            bool bHeld=false; for (const FDemoSavedWeapon& Weapon:Weapons) if (Weapon.Id==Entry.WeaponId) bHeld=true;
            if (Entry.bInitialSupplyGranted!=bHeld) return false;
        }
        if (!TeamMembers.IsEmpty())
        {
            const FDemoCoopPlayerCheckpoint& Host=TeamMembers[0]; // 不是第二个权限源；同次保存必须与根完全一致。
            if (Host.PlayerKey!=OwnerPlayerKey || Host.Progress.Coins!=Coins || Host.Progress.SilverCoins!=SilverCoins || Host.Progress.Kills!=Kills
                ||!FDemoWeaponOwnership::StaticStruct()->CompareScriptStruct(&Host.Progress.Ownership,&Ownership,0)
                ||!FDemoUpgradeProgress::StaticStruct()->CompareScriptStruct(&Host.Progress.UpgradeProgress,&UpgradeProgress,0)
                ||!FDemoGambleProgress::StaticStruct()->CompareScriptStruct(&Host.Progress.Gambling,&Gambling,0)
                ||!FDemoRunBuildSnapshot::StaticStruct()->CompareScriptStruct(&Host.Build,&Build,0)
                ||Host.Progress.PistolChallenge!=PistolChallenge || Host.Progress.BestEndlessLevel!=BestEndlessLevel
                ||Host.MaxHealth!=MaxHealth || Host.Health!=Health || Host.DamageBonus!=DamageBonus || Host.MagazineBonus!=MagazineBonus || Host.HealAmount!=HealAmount || Host.DashSpeed!=DashSpeed
                ||Host.Loadout.PrimaryId!=PrimaryId || Host.Loadout.ActiveSlot!=ActiveSlot || Host.Loadout.Weapons.Num()!=Weapons.Num() || Host.Loadout.UnlockedAmmoIds!=UnlockedAmmoIds || Host.Loadout.SelectedAmmoId!=SelectedAmmoId) return false;
            for (int32 Index=0;Index<Weapons.Num();++Index) if (Weapons[Index].Id!=Host.Loadout.Weapons[Index].Id || Weapons[Index].Ammo!=Host.Loadout.Weapons[Index].Ammo || Weapons[Index].Reserve!=Host.Loadout.Weapons[Index].Reserve) return false;
        }
    }
    // 内容身份不从关号猜测；旧轮继续原规则，未知内容不能覆盖为另一战役。
    if (!Build.Validate() || (CampaignId.IsNone() ? (ContentRevision != 0 || RunSeed != 0 || !Build.CoreId.IsNone() || !ContentSnapshot.IsEmpty() || !ContentFingerprint.IsEmpty())
        : (Version < 8 || CampaignId != TEXT("steam_demo") || ContentRevision < 1 || (Version == 8 && ContentRevision != 1) || RunSeed < 0 || bEndless)))
    { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE invalid campaign/build version")); return false; }
    if (Version < 9 && (!ContentSnapshot.IsEmpty() || !ContentFingerprint.IsEmpty()))
    { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE old format contains frozen content")); return false; }
    if (Version >= 9 && !CampaignId.IsNone())
    {
        FString ContentError; // 同步读取只生成临时候选配置，不修改资产、World或存档原字符串。
        const UDemoCampaignDefinition* Frozen = UDemoCampaignDefinition::FromSnapshot(GetTransientPackage(), ContentSnapshot, ContentFingerprint, ContentError);
        if (!Frozen || Frozen->CampaignId != CampaignId || Frozen->ContentRevision != ContentRevision)
        { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE invalid frozen campaign: %s"), *ContentError); return false; }
    }
    for (const FDemoCoopPlayerCheckpoint& Member : TeamMembers) // 离线成员也不能把新核心注入旧战役。
        if (CampaignId.IsNone() && !Member.Build.CoreId.IsNone())
        { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE legacy campaign contains member build")); return false; }
    if (!CampaignId.IsNone())
    {
        if (!ValidateBuildCheckpoint(Build, Phase, CompletedLevel, true))
        { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE build does not match checkpoint phase")); return false; }
        for (const FDemoCoopPlayerCheckpoint& Member : TeamMembers)
            if (!ValidateBuildCheckpoint(Member.Build, Phase, CompletedLevel, false))
            { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE member build ahead of campaign")); return false; }
    }
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
        Coins = static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(Coins) + Refund));
        if (Version == 1) SilverCoins = 0;
        UpgradeProgress = FDemoUpgradeProgress(); // 来源不可还原，不伪造逐项等级；原GAS/技能合计留到本轮结束。
        Purchases = GoldPurchases = SilverPurchases = 0;
        Version = 3;
        UE_LOG(LogFPSDemo, Log, TEXT("RUN_SAVE migrated to V3 refund=%lld gold=%d; legacy growth temporary"), Refund, Coins);
    }
    if(Version<4){UnlockedAmmoIds={FName(TEXT("normal"))};SelectedAmmoId=TEXT("normal");Version=4;UE_LOG(LogFPSDemo,Log,TEXT("RUN_SAVE migrated V4 ammo normal"));}
    if (Version < 5) { PistolChallenge = CompletedLevel > 0 ? 2 : 0; bEndless = false; BestEndlessLevel = 0; Version = 5; UE_LOG(LogFPSDemo,Log,TEXT("RUN_SAVE migrated V5; old active runs cannot prove pistol challenge")); }
    if(Version<6) { Gambling=FDemoGambleProgress(); for(auto& Member:TeamMembers) Member.Progress.Gambling=FDemoGambleProgress(); Version=6; UE_LOG(LogFPSDemo,Log,TEXT("RUN_SAVE migrated V6 empty research")); } // 旧存档没有可验证的研究事实。
    if(Version<7) { Gambling.Mine=FDemoMineRound(); for(auto& Member:TeamMembers) Member.Progress.Gambling.Mine=FDemoMineRound(); Version=7; UE_LOG(LogFPSDemo,Log,TEXT("RUN_SAVE migrated V7 empty casino mine")); }
    if (Version < 8)
    {
        CampaignId = NAME_None; ContentRevision = 0; RunSeed = 0; Build = FDemoRunBuildSnapshot();
        for (FDemoCoopPlayerCheckpoint& Member : TeamMembers) Member.Build = FDemoRunBuildSnapshot(); // 不补发旧轮第3/7关分支。
        Version = 8; UE_LOG(LogFPSDemo, Log, TEXT("RUN_SAVE migrated V8 preserving legacy campaign"));
    }
    if (Version < 9)
    {
        if (!CampaignId.IsNone())
        {
            FString MigrationError; // V8只有历史v1可证明；不得读取被作者修改的DataAsset/CDO值替代历史规则。
            if (!UDemoCampaignDefinition::ExportLegacyV1(ContentSnapshot, ContentFingerprint, MigrationError))
            { UE_LOG(LogFPSDemo, Warning, TEXT("RUN_SAVE V9 migration failed: %s"), *MigrationError); return false; }
        }
        Version = 9;
        UE_LOG(LogFPSDemo, Log, TEXT("RUN_SAVE migrated V9 frozen=%d"), !ContentSnapshot.IsEmpty());
    }
    return Validate(); // 迁移后重新校验整份候选，包括队伍与冻结配置；失败不能写回磁盘。
}
void UDemoRunSave::ResetTemporaryGrowth()
{
    DEMO_LOG_CALL();
    Build = FDemoRunBuildSnapshot(); // 通关/团灭/放弃清本轮核心，不改永久成长和内容身份。
    Gambling.ResetRun(); // 永久遗物、保底和装配保留；救援/天赋随新轮重置。
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
    Collection.InitializeDependency<UDemoPlayerProfile>(); // 冻结账号旧权必须先于旧槽迁移。
    Collection.InitializeDependency<UDemoCoopSessionSubsystem>(); // 新单人根也使用本人稳定键。
    // 自动化入口统一隔离；测试随GI而非静态变量，跨OpenLevel仍使用同一前缀。
    bTest = FString(FCommandLine::Get()).Contains(TEXT("DemoSessionTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoSmokeTest"))
        || FString(FCommandLine::Get()).Contains(TEXT("DemoCampaignTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoWeaponTest"))
        || FString(FCommandLine::Get()).Contains(TEXT("DemoArmoryTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoUIValidation")) || FString(FCommandLine::Get()).Contains(TEXT("DemoEnemyAttackTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoProjectileTest")) /* 实体子弹测试禁用正式存档与云同步。 */ || FString(FCommandLine::Get()).Contains(TEXT("DemoAmmoTest"))
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
            for (int32 Backup = 0; Backup < 4; ++Backup) // 主/备份/迁移原始主/备份全属于本GI，停止必须清理。
            {
                const FString OwnedSlot = SlotName(Index) + (Backup==0?TEXT(""):Backup==1?TEXT("_Backup"):Backup==2?TEXT("_LegacyRaw"):TEXT("_LegacyBackupRaw")); // 固定槽名，不接受外部路径。
                DemoDurableSave::RemoveStaging(OwnedSlot); // 只清当前GI未提交暂存，不误收购枪权。
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
        if (HasUnsupportedContent(Slots[Index])) { Slots[Index] = nullptr; ReportStatus(TEXT("存档或内容版本较新，原文件已保护"), true); continue; }
        if (Exists[Index] && (!Slots[Index] || !Slots[Index]->UpgradeLegacy()))
        {
            Slots[Index] = Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(SlotName(Index)+TEXT("_Backup"), 0));
            if (!Slots[Index] || !Slots[Index]->UpgradeLegacy()) { Slots[Index] = nullptr; ReportStatus(TEXT("存档损坏，已保护原文件；请选择其他栏位"), true); }
            else ReportStatus(TEXT("主存档异常，已恢复有效备份"), true);
        }
        if (Slots[Index] && Slots[Index]->Version<10)
        {
            UDemoRunSave* Candidate=DuplicateObject<UDemoRunSave>(Slots[Index],this); // 原内存/文件保持到全部冻结和耐久提交成功。
            if (!DemoDurableSave::Archive(SlotName(Index),SlotName(Index)+TEXT("_LegacyRaw")) || !DemoDurableSave::Archive(SlotName(Index)+TEXT("_Backup"),SlotName(Index)+TEXT("_LegacyBackupRaw"))
                || !FreezeSlotOwnership(*Candidate,GetGameInstance()) || !DemoDurableSave::Write(Slots[Index],SlotName(Index)+TEXT("_Backup")) || !DemoDurableSave::Write(Candidate,SlotName(Index)))
            { Slots[Index]=nullptr; ReportStatus(TEXT("旧武器拥有权迁移失败或来源冲突，已保护原文件"),true); }
            else { Slots[Index]=Candidate; ++ChangeSerial; }
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
    const UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 新档只继承账号冻结旧权，绝不继承别槽Paid。
    Fresh->SlotStorageKey=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); Fresh->OwnerPlayerKey=GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->GetLocalPlayerKey();
    if (!Profile || !Profile->MakeLegacyOwnership(Fresh->Ownership) || !Fresh->Validate() || !DemoDurableSave::Write(Fresh, SlotName(Index))) { ReportStatus(TEXT("创建存档失败，请检查迁移状态/磁盘写入权限"), true); return false; }
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
bool UDemoRunSaves::BindOnlineOwner(int32 Index, const FString& OwnerKey)
{
    DEMO_LOG_CALL();
    const UDemoRunSave* Previous = GetSlot(Index); // 只绑定完整可读的安全区槽，不改变其他槽。
    FGuid Parsed; // 只使用上层EOS登录结果，拒绝空键或损坏键。
    if (!Previous || Previous->Phase != EDemoPhase::Hub || !FGuid::Parse(OwnerKey, Parsed) || !Parsed.IsValid()) return false;
    if (!Previous->OnlineOwnerKey.IsEmpty())
    {
        if (Previous->OnlineOwnerKey == OwnerKey) return true;
        ReportStatus(TEXT("此存档已绑定其他在线账号，请选择该账号的存档或创建新存档"), true); return false;
    }
    if (Previous->TeamMembers.Num() > 1)
    { ReportStatus(TEXT("旧局域网多人存档无法自动关联在线队员，请为公网创建新存档"), true); return false; }
    UDemoRunSave* Bound = DuplicateObject<UDemoRunSave>(Previous, this); // 写盘失败保留原档与内存快照，不部分绑定。
    Bound->OnlineOwnerKey = OwnerKey;
    Bound->OwnerPlayerKey = OwnerKey; // 显式账号绑定事务同时更新根/房主成员镜像键。
    if (!Bound->TeamMembers.IsEmpty()) Bound->TeamMembers[0].PlayerKey = OwnerKey; // 单人槽只有本机房主可明确绑定，不改其他队员身份。
    const int32 PreviousSlot = ActiveSlot; // 绑定事务临时选择目标，失败恢复原活动槽。
    ActiveSlot = Index;
    const bool bSaved = Store(Bound); // 使用标准备份、校验及云待同步水位。
    if (!bSaved) ActiveSlot = PreviousSlot;
    return bSaved;
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
    if (!IsInGameThread() || bStoring) { ReportStatus(TEXT("整槽保存正在提交，拒绝重入"),true); return false; }
    TGuardValue<bool> Guard(bStoring,true); // 序列化/落盘同步栈持有；所有出口自动解除，不等待云响应。
    if (ActiveSlot == INDEX_NONE) return true; // 无槽仅供原有独立World测试，不生成玩家档。
    if (Snapshot && GetSlot(ActiveSlot))
    {
        const FString& OwnerKey = GetSlot(ActiveSlot)->OnlineOwnerKey; // 普通捕获/死亡/通关新建的快照必须继承已绑定身份。
        if (!OwnerKey.IsEmpty() && !Snapshot->OnlineOwnerKey.IsEmpty() && OwnerKey != Snapshot->OnlineOwnerKey)
        { ReportStatus(TEXT("拒绝更换存档的在线账号绑定"), true); return false; }
        if (Snapshot->OnlineOwnerKey.IsEmpty()) Snapshot->OnlineOwnerKey = OwnerKey;
        if (!Snapshot->SlotStorageKey.IsEmpty() && Snapshot->SlotStorageKey!=GetSlot(ActiveSlot)->SlotStorageKey) { ReportStatus(TEXT("拒绝跨栏位资产身份写入"),true); return false; }
        Snapshot->SlotStorageKey=GetSlot(ActiveSlot)->SlotStorageKey;
        if (Snapshot->OwnerPlayerKey.IsEmpty()) Snapshot->OwnerPlayerKey=GetSlot(ActiveSlot)->OwnerPlayerKey;
        if (!Snapshot->TeamMembers.IsEmpty() && Snapshot->TeamMembers[0].PlayerKey!=Snapshot->OwnerPlayerKey)
        {
            const FString LocalKey=GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->GetLocalPlayerKey(); // 原合法跨设备房主重绑定，不改变槽实例或付费来源。
            if (Snapshot->TeamMembers[0].PlayerKey!=LocalKey) { ReportStatus(TEXT("根与房主成员身份冲突"),true); return false; }
            Snapshot->OwnerPlayerKey=LocalKey;
        }
    }
    if (!Snapshot || !Snapshot->Validate() || !GetSlot(ActiveSlot)) { ReportStatus(TEXT("检查点校验失败，未覆盖存档"), true); return false; }
    Snapshot->CreatedLocal = Slots[ActiveSlot]->CreatedLocal;
    Snapshot->SavedUtc = FDateTime::UtcNow();
    // 仅已验证的主档轮换到备份，损坏主档不能覆盖有效备份。
    UDemoRunSave* Previous = Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(SlotName(ActiveSlot), 0)); // 本次写盘前同步借用的旧有效值对象。
    if (HasUnsupportedContent(Previous)) { ReportStatus(TEXT("磁盘存档或内容版本较新，已取消覆盖"), true); return false; } // 保护缓存读取后由其他版本程序写入的新档。
    if (Previous && Previous->Validate() && !DemoDurableSave::Write(Previous, SlotName(ActiveSlot)+TEXT("_Backup")))
    { ReportStatus(TEXT("检查点备份保存失败，请重试"), true); return false; }
    if (!DemoDurableSave::Write(Snapshot, SlotName(ActiveSlot))) { ReportStatus(TEXT("检查点保存失败，请重试后离开"), true); return false; }
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
    if (Gold < 0) { ReportStatus(TEXT("金币余额无效，取消重置"), true); return false; }
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
        Fresh->Ownership=Equipment->Ownership; Fresh->SlotStorageKey=Equipment->SlotStorageKey; Fresh->OwnerPlayerKey=Equipment->OwnerPlayerKey; // 换RunId不销毁本槽枪权/收据。
        // ResetActive已分配新RunId，明确代表死亡/放弃结束后的新轮；保留Fresh空内容，Flow在新Hub读取最新作者配置。
        // 普通已有Hub档不经过ResetActive，继续自身冻结内容；ResetTemporaryGrowth单独调用仍不清旧轮配置。
        Fresh->Gambling=Equipment->Gambling; Fresh->Gambling.ResetRun(); // 保留同槽个人研究，不从账号全局借用。
        Fresh->Weapons = Equipment->Weapons;
        Fresh->PrimaryId = Equipment->PrimaryId;
        Fresh->ActiveSlot = Equipment->ActiveSlot;
        Fresh->UnlockedAmmoIds = Equipment->UnlockedAmmoIds;
        Fresh->SelectedAmmoId = Equipment->SelectedAmmoId;
        Fresh->TeamMembers = Equipment->TeamMembers; // 调用者已逐成员清理本轮成长；绝不能在重开时只复制房主装备。
        for (FDemoCoopPlayerCheckpoint& Member : Fresh->TeamMembers) Member.Build = FDemoRunBuildSnapshot(); // 离线成员同样不能继承上一轮核心。
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
bool UDemoRunSaves::StorePlayerChallenge(const FString& PlayerKey, int32 ChallengeStatus, int32 Best)
{
    DEMO_LOG_CALL();
    if (ActiveSlot == INDEX_NONE) return true; // 无槽开发World不写文件。
    const UDemoRunSave* Previous = GetSlot(ActiveSlot); // 只操作出发前已提交检查点，不采集半场战利品。
    if (!Previous || ChallengeStatus < 0 || ChallengeStatus > 2 || Best < 0) return false;
    if (Previous->TeamMembers.IsEmpty()) return StoreChallenge(ChallengeStatus, Best); // 原V5单人协议保持不变。
    UDemoRunSave* Snapshot = DuplicateObject<UDemoRunSave>(Previous, this); // 不修改缓存直到整份文件保存成功。
    for (FDemoCoopPlayerCheckpoint& Member : Snapshot->TeamMembers)
        if (Member.PlayerKey == PlayerKey)
        {
            if (Member.Progress.PistolChallenge == ChallengeStatus && Member.Progress.BestEndlessLevel >= Best) return true;
            Member.Progress.PistolChallenge = FMath::Max(Member.Progress.PistolChallenge, ChallengeStatus);
            Member.Progress.BestEndlessLevel = FMath::Max(Member.Progress.BestEndlessLevel, Best);
            if (PlayerKey == GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->GetLocalPlayerKey())
            { Snapshot->PistolChallenge = Member.Progress.PistolChallenge; Snapshot->BestEndlessLevel = Member.Progress.BestEndlessLevel; }
            return Store(Snapshot);
        }
    UE_LOG(LogFPSDemo, Warning, TEXT("CHALLENGE rejected: player not in committed team checkpoint")); return false;
}

TSharedPtr<FJsonObject> UDemoRunSaves::ExportCloud(int32 Index) const
{
    DEMO_LOG_CALL();
    const UDemoRunSave* Snapshot=GetSlot(Index); // GI拥有缓存；Codec只同步借用值，不访问磁盘或World。
    if (!Snapshot) return nullptr;
    FString Error; // 具体格式拒绝原因由Codec记录，云编排器不得把有效槽导出失败当作空槽同步成功。
    return DemoCheckpointJsonCodec::Encode(*Snapshot,Error);
}
bool UDemoRunSaves::ImportCloud(int32 Index, const TSharedPtr<FJsonObject>& Data)
{
    DEMO_LOG_CALL();
    if (Index < 0 || Index > 2 || !Data) return false;
    UDemoRunSave* Candidate = NewObject<UDemoRunSave>(this); // 临时值对象，验证成功之前不影响游戏或本地槽。
    FString Error; // 完整协议/成员校验成功前禁止修改本地文件。
    if (!DemoCheckpointJsonCodec::Decode(Data,*Candidate,Error))
    { ReportStatus(Error.IsEmpty()?TEXT("云端检查点格式无效，已保留本地存档"):Error,true); return false; }
    RefreshSlots();
    if (SlotExists(Index) && !GetSlot(Index)) { ReportStatus(TEXT("本地损坏/未来版本存档受保护，未覆盖"), true); return false; }
    if (GetSlot(Index) && !GetSlot(Index)->OnlineOwnerKey.IsEmpty() && GetSlot(Index)->OnlineOwnerKey != Candidate->OnlineOwnerKey)
    { ReportStatus(TEXT("云端快照缺失或改变在线账号绑定，已保护本地检查点"), true); return false; } // 旧服务端不能静默剥离公网上线的固定队伍身份。
    if (GetSlot(Index)&&!GetSlot(Index)->TeamMembers.IsEmpty()&&Candidate->TeamMembers.IsEmpty())
    { ReportStatus(TEXT("云端快照不含队伍数据，已保护房主本地合作检查点"),true); return false; } // 旧服务端/旧档不能将合作存档静默降级成单人。
    const double SourceVersion = Data->GetNumberField(TEXT("version")); // Codec已验证为有限整型；候选UpgradeLegacy后不再保留上传原版本。
    if (GetSlot(Index) && !GetSlot(Index)->CampaignId.IsNone() && SourceVersion < 8)
    { ReportStatus(TEXT("云端旧格式缺少战役与核心，已保护本地新版检查点"), true); return false; } // 旧服务不能用缺失字段清空新核心；合法V8主动切无尽仍可恢复。
    if (GetSlot(Index) && !GetSlot(Index)->ContentSnapshot.IsEmpty() && SourceVersion < 9)
    { ReportStatus(TEXT("云端旧格式缺少冻结内容，已保护本地V9检查点"), true); return false; } // 即使旧V8可迁移，也不能用历史v1覆盖本地正在进行的新版配置。
    if (GetSlot(Index) && GetSlot(Index)->Version>=10 && SourceVersion<10) { ReportStatus(TEXT("旧云版本不能覆盖本槽新拥有权，已保护本地"),true); return false; }
    if (Candidate->Version<10 && !FreezeSlotOwnership(*Candidate,GetGameInstance())) { ReportStatus(TEXT("云旧权迁移无法证明，已保护本地"),true); return false; }
    if (GetSlot(Index) && !DemoDurableSave::Write(Slots[Index], SlotName(Index) + TEXT("_Backup"))) return false;
    if (!DemoDurableSave::Write(Candidate, SlotName(Index))) { ReportStatus(TEXT("云端检查点写入本地失败"), true); return false; }
    Slots[Index] = Candidate; Exists[Index] = true;
    ++ChangeSerial; // 导入同样改变本地缓存，同步器完成哈希确认后再显示已同步。
    ReportStatus(TEXT("云端检查点已下载，选择栏位后继续"));
    return true;
}
