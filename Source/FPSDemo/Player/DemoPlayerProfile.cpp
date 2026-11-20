// 职责：加载/保存通关档案并派生权限。使用：GI跨地图持有，Editor停止清理；依赖SaveGame、武器目录和云协议，调试覆盖不写通关事实。
#include "Player/DemoPlayerProfile.h"
#include "Player/Cloud/DemoCloudProtocol.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Save/DemoDurableSave.h" // Legacy冻结先本地耐久，再允许新槽导入。

void UDemoProfileSave::Serialize(FArchive& Ar)
{
    DEMO_LOG_CALL();
    if (HasAnyFlags(RF_ClassDefaultObject)) { Super::Serialize(Ar); return; } // CDO仍为4，不能把后续新档默认降成3。
    if (Ar.IsLoading()) Data.SchemaVersion=3; // 旧V3默认tag可被省略，不能按当前CDO的4误认完成冻结。
    const bool bPrevious=Ar.ArNoDelta; // 本次同步保存临时选项，完成后恢复调用者归档状态。
    if (Ar.IsSaving()) Ar.ArNoDelta=true;
    Super::Serialize(Ar); Ar.ArNoDelta=bPrevious;
}
void UDemoPlayerProfile::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL();
    Super::Initialize(Collection);
    // 所有现有自动化入口隔离存档；每次进程/PIE GI独立，OpenLevel仍复用同一子系统。
    bTestSlot = FParse::Param(FCommandLine::Get(), TEXT("DemoSessionTest")) || FParse::Param(FCommandLine::Get(), TEXT("DemoArmoryTest")) || FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponTest"))
        || FParse::Param(FCommandLine::Get(), TEXT("DemoUIValidation")) || FParse::Param(FCommandLine::Get(), TEXT("DemoSmokeTest"))
        // 敌人攻击/导航专项在非Editor打包验证时也必须隔离，不能加载正式武器解锁档案。
        || FParse::Param(FCommandLine::Get(), TEXT("DemoCampaignTest")) || FParse::Param(FCommandLine::Get(), TEXT("DemoEnemyAttackTest"))
        || FParse::Param(FCommandLine::Get(), TEXT("DemoProjectileTest")) // 实体子弹专项同样使用隔离档案。
        || FParse::Param(FCommandLine::Get(), TEXT("DemoAmmoTest"))
        || FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponAnimationTest")); // 动画夹具会解锁四枪，任何目标均隔离临时档案，退出精确清理。
    SlotName = bTestSlot ? TEXT("DemoProfileTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) : TEXT("DemoPlayerProfile");
#if WITH_EDITOR
    // 编辑器试玩不读取正式解锁，也不沿用云端测试身份；Standalone -game同样使用临时档。
    bEditorSession = true;
    // 父Editor可根据确切子进程PID补做强制停止清理；同PID的多个GI仍以GUID隔离。
    SlotName = FString::Printf(TEXT("DemoEditorProfile_%u_%s"), FPlatformProcess::GetCurrentProcessId(), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    UE_LOG(LogFPSDemo, Log, TEXT("EDITOR_SESSION profile=%s; local only, delete on GI shutdown"), *SlotName);
#elif !UE_BUILD_SHIPPING
    FString CloudTest; FGuid CloudId; // 显式真实云回归的跨进程GUID，非法参数绝不作为文件路径。
    if (FParse::Value(FCommandLine::Get(), TEXT("DemoCloudTestId="), CloudTest) && FGuid::Parse(CloudTest, CloudId) && CloudId.IsValid())
    { SlotName = TEXT("DemoCloudTest_") + CloudId.ToString(EGuidFormats::Digits) + TEXT("_Profile"); bTestSlot = false; } // 两阶段回归需要保留到脚本显式清理。
#endif
    Profile.ProfileId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    Profile.UnlockedWeaponIds = { TEXT("pistol") };
    if (UGameplayStatics::DoesSaveGameExist(SlotName, 0) || UGameplayStatics::DoesSaveGameExist(SlotName + TEXT("_Backup"), 0)) ReloadFromDisk();
    else { Normalize(Profile); CommitChange(); } // 新档也冻结空旧权基线，后续胜利不会补主枪。
    UE_LOG(LogFPSDemo, Log, TEXT("PROFILE_LOAD slot=%s status=%s readonly=%d"), *SlotName, *Status, bReadOnly);
}
bool UDemoPlayerProfile::ReloadFromDisk()
{
    DEMO_LOG_CALL();
    if (bNeedsSave) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE reload rejected: unsaved in-memory changes")); return false; }
    UDemoProfileSave* Loaded = Cast<UDemoProfileSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)); // 临时加载快照，通过校验才替换内存。
    if (Loaded && Loaded->Data.SchemaVersion > 4)
    {
        bReadOnly = true;
        Status = TEXT("存档版本较新，已保护原文件；请使用对应版本");
        UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE future schema protected"));
        return false;
    }
    const bool bLegacy = Loaded && Loaded->Data.SchemaVersion < 4; // 冻结必须先持久，不能在每次重开后重新取水位。
    if (Loaded && Normalize(Loaded->Data))
    { Profile = Loaded->Data; bReadOnly = false; Status = TEXT("本地存档已载入"); if (bLegacy) { bNeedsSave=true; return SaveProfile(); } return true; }
    UDemoProfileSave* Backup = Cast<UDemoProfileSave>(UGameplayStatics::LoadGameFromSlot(SlotName + TEXT("_Backup"), 0)); // 主档丢失也先恢复备份，绝不直接创建新档覆盖它。
    if (Backup && Normalize(Backup->Data))
    { Profile = Backup->Data; bReadOnly = false; Status = TEXT("主档异常，已从备份恢复"); bNeedsSave = true; UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE restored backup")); return SaveProfile(); }
    bReadOnly = true;
    Status = TEXT("存档读取失败，已保护原文件；本次进度暂不能保存");
    UE_LOG(LogFPSDemo, Error, TEXT("PROFILE primary/backup invalid; write protected"));
    return false;
}
void UDemoPlayerProfile::Deinitialize()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    DebugTestFlags=0; bDebugUnlockAllForSession=false; bDebugUnlockAllModesForSession=false; // Stop PIE清除包括模式在内的全部GI覆盖，外部暂存引用也不再授权。
#endif
    // 即将丢弃的试玩数据无需在退出时补写；正式游戏仍保留原来的失败重试行为。
    if (!bEditorSession && !bTestSlot && bNeedsSave && !bReadOnly) SaveProfile();
    if (bEditorSession || bTestSlot)
    {
        for (int32 Backup = 0; Backup < 4; ++Backup) // 主/备份及迁移原始字节都属于当前GI，不扫描其他实例。
        {
            const FString OwnedSlot = SlotName + (Backup==0?TEXT(""):Backup==1?TEXT("_Backup"):Backup==2?TEXT("_LegacyRaw"):TEXT("_LegacyBackupRaw")); // 稳定前缀来自Initialize生成的随机GUID。
            DemoDurableSave::RemoveStaging(OwnedSlot); // 未提交冻结也属于本GI，不留到下一次试玩。
            if (!UGameplayStatics::DoesSaveGameExist(OwnedSlot, 0)) continue;
            const bool bDeleted = UGameplayStatics::DeleteGameInSlot(OwnedSlot, 0); // 仅通过UE平台SaveGame接口清理。
            if (bDeleted) { UE_LOG(LogFPSDemo, Log, TEXT("SESSION_PROFILE_CLEANUP %s"), *OwnedSlot); }
            else { UE_LOG(LogFPSDemo, Error, TEXT("SESSION_PROFILE_CLEANUP failed %s"), *OwnedSlot); }
        }
        Profile = FDemoPlayerProfileData(); // 值字段全部回到默认，无UObject/World引用需要跨会话保留。
        bNeedsSave = false;
    }
    Super::Deinitialize();
}
bool UDemoPlayerProfile::Normalize(FDemoPlayerProfileData& Data) const
{
    DEMO_LOG_CALL();
    FGuid ParsedId; // 解析后的临时GUID，拒绝空标识或损坏文件。
    if (Data.SchemaVersion < 1 || Data.SchemaVersion > 4 || !FGuid::Parse(Data.ProfileId, ParsedId) || !ParsedId.IsValid()
        || Data.LocalRevision < 0 || Data.SyncedRevision < 0 || Data.SyncedRevision > Data.LocalRevision)
    { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE invalid schema/identity/revision")); return false; }
    const bool bFreeze = Data.SchemaVersion < 4 || (Data.LegacyMigrationId.IsEmpty() && Data.LocalRevision == 0 && Data.Clears.IsEmpty()); // 只在升级或全新档取一次基线。
    TArray<FName> LegacyIds = { FName(TEXT("pistol")) }; // 旧规则仅在bFreeze期间求值，不能受后续通关影响。
    if (Data.SchemaVersion == 1) { Data.SchemaVersion = 2; Data.PreferenceRevision = Data.LastSelectedPrimary.IsNone() ? 0 : Data.LocalRevision; } // 老档记录完整保留，首次联网再按RunId幂等迁移。
    if (Data.PreferenceRevision < 0 || Data.PreferenceRevision > Data.LocalRevision || Data.SyncedPreferenceRevision < 0 || Data.SyncedPreferenceRevision > Data.LocalRevision || Data.CloudClearedDifficulties.Num() > 5 || Data.RecentAcceptedRuns.Num() > 128) return false;
    for (const FString& Difficulty : Data.CloudClearedDifficulties) // 云端基线也要验证，磁盘字段不是可信授权。
    {
        if (Difficulty != TEXT("easy") && Difficulty != TEXT("normal") && Difficulty != TEXT("hard") && Difficulty != TEXT("hard_pistol") && Difficulty != TEXT("hell")) return false;
        if (bFreeze && !DemoWeaponCatalog::AppendUnlocksForClear(Difficulty, LegacyIds)) return false; // 冻结后难度仅用于模式准入。
    }
    FDateTime ParsedUtc; // 校验时间格式，协议中只允许UTC ISO8601字符串。
    if (!Data.UpdatedUtc.IsEmpty() && !FDateTime::ParseIso8601(*Data.UpdatedUtc, ParsedUtc)) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE invalid update timestamp")); return false; }
    TSet<FString> Seen; // 本次加载的幂等记录集合，不跨调用保存。
    for (const FDemoClearRecord& Clear : Data.Clears) // 档案拥有记录；只读借用进行验证。
    {
        FGuid Run; // 每条凭据必须有有效且唯一的通关GUID。
        if (!FGuid::Parse(Clear.RunId, Run) || !Run.IsValid() || Seen.Contains(Clear.RunId)
            || !FDateTime::ParseIso8601(*Clear.CompletedUtc, ParsedUtc)
            || (Clear.DifficultyId != TEXT("easy") && Clear.DifficultyId != TEXT("normal") && Clear.DifficultyId != TEXT("hard") && Clear.DifficultyId != TEXT("hard_pistol") && Clear.DifficultyId != TEXT("hell")))
        { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE invalid completion record")); return false; }
        Seen.Add(Clear.RunId);
        if (bFreeze && !DemoWeaponCatalog::AppendUnlocksForClear(Clear.DifficultyId, LegacyIds)) return false;
    }
    if (bFreeze)
    {
        Data.LegacyMigrationId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); Data.LegacySourceRevision=Data.LocalRevision; Data.LegacyWeaponIds=LegacyIds;
        Data.LegacyRunIds.Reset(); for (const FDemoClearRecord& Clear:Data.Clears) Data.LegacyRunIds.Add(Clear.RunId);
        UE_LOG(LogFPSDemo, Log, TEXT("PROFILE_LEGACY frozen revision=%d rights=%d"), Data.LegacySourceRevision, Data.LegacyWeaponIds.Num());
    }
    Data.SchemaVersion=4;
    FDemoWeaponOwnership Frozen; // 用同一来源验证器核账号旧权，不含本槽购买/调试。
    if (!Frozen.FreezeLegacy(Data.LegacyWeaponIds,Data.LegacyMigrationId,Data.LegacySourceRevision) || Data.LegacyRunIds.Num()>4096) return false;
    TSet<FString> FrozenRuns; // 只验证已冻结水位，绝不从新Clears追加。
    for (const FString& Run:Data.LegacyRunIds) { FGuid Id; if (!FGuid::Parse(Run,Id)||!Id.IsValid()||FrozenRuns.Contains(Run)) return false; FrozenRuns.Add(Run); }
    Data.UnlockedWeaponIds=Frozen.EffectiveIds();
    if (DemoWeaponCatalog::IndexOf(Data.LastSelectedPrimary)<=0) Data.LastSelectedPrimary = NAME_None; // 偏好可指某槽已买主枪，但本身不授跨槽权。
    return true;
}
const FDemoPlayerProfileData& UDemoPlayerProfile::GetData() const { DEMO_LOG_TICK(); return Profile; }
bool UDemoPlayerProfile::MakeLegacyOwnership(FDemoWeaponOwnership& Out) const
{
    DEMO_LOG_CALL();
    if (bReadOnly || bNeedsSave || Profile.SchemaVersion != 4) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE_LEGACY rejected uncommitted/protected")); return false; }
    return Out.FreezeLegacy(Profile.LegacyWeaponIds, Profile.LegacyMigrationId, Profile.LegacySourceRevision);
}
bool UDemoPlayerProfile::IsHellUnlocked() const
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (IsDebugUnlockAllModesForSession()) return true; // 仅本次单人试玩覆盖，不向Clears追加hard_pistol事实。
#endif
    if (Profile.CloudClearedDifficulties.Contains(TEXT("hard_pistol"))) return true;
    for (const FDemoClearRecord& Clear : Profile.Clears) if (Clear.DifficultyId==TEXT("hard_pistol")) return true; // 待上传的本地事实同样立即解锁。
    return false;
}
bool UDemoPlayerProfile::IsEndlessUnlocked() const
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (IsDebugUnlockAllModesForSession()) return true; // 与地狱同步开放，不模拟地狱胜利或发金币。
#endif
    if (Profile.CloudClearedDifficulties.Contains(TEXT("hell"))) return true;
    for (const FDemoClearRecord& Clear : Profile.Clears) if (Clear.DifficultyId==TEXT("hell")) return true; // 幂等记录跨地图有效。
    return false;
}
bool UDemoPlayerProfile::IsUnlocked(FName Id) const
{
    DEMO_LOG_TICK();
    if (DemoWeaponCatalog::IndexOf(Id) == INDEX_NONE) return false; // 调试也不能授权未知武器ID。
#if WITH_EDITOR
    if (bDebugUnlockAllForSession) return true; // 编辑器权限覆盖与通关事实分离，避免误解锁难度/领取奖励。
#endif
    return Profile.UnlockedWeaponIds.Contains(Id);
}
#if WITH_EDITOR
void UDemoPlayerProfile::DebugUnlockAllForSession()
{
    DEMO_LOG_CALL();
    bDebugUnlockAllForSession = true; // 幂等开关；不改档案修订、钱包或当前装备，跨地图由GI自然保留。
    UE_LOG(LogFPSDemo, Display, TEXT("DEBUG_UNLOCK_ALL all weapons/ammo unlocked for this editor session"));
}
bool UDemoPlayerProfile::IsDebugUnlockAllForSession() const { DEMO_LOG_TICK(); return bDebugUnlockAllForSession; }
void UDemoPlayerProfile::DebugUnlockAllModesForSession()
{
    DEMO_LOG_CALL();
    if (!GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->GetNetMode() != NM_Standalone)
    { UE_LOG(LogFPSDemo, Warning, TEXT("DEBUG_UNLOCK_MODES rejected world/net mode")); return; }
    bDebugUnlockAllModesForSession = true; // 幂等授权，不修改档案修订、已选模式、最高纪录、钱包或通关凭据。
    UE_LOG(LogFPSDemo, Display, TEXT("DEBUG_UNLOCK_MODES Hell/Endless unlocked for this editor session"));
}
bool UDemoPlayerProfile::IsDebugUnlockAllModesForSession() const
{
    DEMO_LOG_TICK();
    // 即使同一GI随后旅行到Listen/Client，也不将单人调试权限作为联机账号资格。
    return bDebugUnlockAllModesForSession && GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() == NM_Standalone;
}
void UDemoPlayerProfile::SetDebugTestFlag(int32 Flag,bool bEnabled)
{
    DEMO_LOG_CALL();
    if(Flag<0||Flag>2){UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_TEST_FLAG rejected index=%d"),Flag);return;}
    const uint8 Bit=static_cast<uint8>(1<<Flag); // 仅当前GI内三位，不写Profile.Data或存档。
    if(bEnabled) DebugTestFlags|=Bit; else DebugTestFlags&=static_cast<uint8>(~Bit);
    UE_LOG(LogFPSDemo,Display,TEXT("DEBUG_TEST_FLAG flag=%d enabled=%d"),Flag,bEnabled);
}
bool UDemoPlayerProfile::IsDebugTestFlagEnabled(int32 Flag) const
{ DEMO_LOG_TICK(); return Flag>=0&&Flag<3&&(DebugTestFlags&(1<<Flag))!=0; }
#endif
const FString& UDemoPlayerProfile::GetStatus() const { DEMO_LOG_TICK(); return Status; }
const FString& UDemoPlayerProfile::GetSlotName() const { DEMO_LOG_TICK(); return SlotName; }
bool UDemoPlayerProfile::RecordVictory(const FString& RunId, EDemoDifficulty Difficulty)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 当前权威世界的真实胜利状态。
    FGuid ParsedRun; // 通关幂等标识，不能接受空GUID或非法难度。
    if (!GetWorld() || !GetWorld()->GetAuthGameMode() || !State || State->Phase != EDemoPhase::Victory || State->LevelNumber != DemoCombatConfig::LevelCount
        || State->Difficulty != Difficulty || static_cast<uint8>(Difficulty) > 3 || State->bEndless || !FGuid::Parse(RunId, ParsedRun) || !ParsedRun.IsValid())
    { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE victory rejected: invalid final state/run")); return false; }
    FDemoRunCompletion Completion; // 旧单机入口只负责将已验证World状态转换为不持有World的值事实。
    Completion.RunId = RunId;
    Completion.Difficulty = Difficulty;
    Completion.CompletedLevel = State->LevelNumber;
    Completion.PistolChallenge = State->PistolChallenge;
    Completion.bEndless = State->bEndless;
    return RecordCompletion(Completion);
}
bool UDemoPlayerProfile::RecordCompletion(const FDemoRunCompletion& Completion)
{
    DEMO_LOG_CALL();
    FGuid ParsedRun; // 仅验证事实形状和幂等键；网络可信来源由拥有者RPC路由保证，LAN身份不代表互联网认证。
    if (Completion.bEndless || Completion.CompletedLevel != DemoCombatConfig::LevelCount
        || static_cast<uint8>(Completion.Difficulty) > 3 || Completion.PistolChallenge < 0 || Completion.PistolChallenge > 2
        || !FGuid::Parse(Completion.RunId, ParsedRun) || !ParsedRun.IsValid())
    { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE completion rejected: invalid fact")); return false; }
    const FString& RunId = Completion.RunId; // 同步执行期间借用，跨地图不保留事实引用。
    const EDemoDifficulty Difficulty = Completion.Difficulty; // 稳定难度只用于派生永久通关凭据。
    for (const FDemoClearRecord& Existing : Profile.Clears) // 仅同局幂等，不将重复回调计为另一次通关。
        if (Existing.RunId.Equals(RunId,ESearchCase::IgnoreCase)) { UE_LOG(LogFPSDemo, Log, TEXT("PROFILE duplicate victory ignored")); return true; }
    for (const FString& Accepted:Profile.RecentAcceptedRuns) if (Accepted.Equals(RunId,ESearchCase::IgnoreCase)) { UE_LOG(LogFPSDemo, Log, TEXT("PROFILE already confirmed victory ignored")); return true; } // .NET回执小写与UE生成大写为同一GUID。
    FDemoClearRecord Record; // 即将追加的持久化值，不包含World引用。
    Record.RunId = RunId;
    // 手枪挑战作为独立的幂等通关事实，上传后仍能从云端基线推导解锁，不只依赖本机bool。
    Record.DifficultyId = Difficulty == EDemoDifficulty::Hell ? TEXT("hell") : Difficulty == EDemoDifficulty::Hard && Completion.PistolChallenge == 1 ? TEXT("hard_pistol") : Difficulty == EDemoDifficulty::Easy ? TEXT("easy") : Difficulty == EDemoDifficulty::Normal ? TEXT("normal") : TEXT("hard");
    Record.CompletedUtc = FDateTime::UtcNow().ToIso8601();
    Profile.Clears.Add(Record);
    Normalize(Profile);
    CommitChange();
    // 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
    UE_LOG(LogFPSDemo, Display, TEXT("PROFILE victory recorded difficulty=%s; unlock only, no equip"), *Record.DifficultyId);
    return true;
}
void UDemoPlayerProfile::RememberPrimary(FName Id)
{
    DEMO_LOG_CALL();
    if (DemoWeaponCatalog::IndexOf(Id)<=0) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE selection rejected: unknown/non-primary")); return; } // 偏好不是授权；真正装备已由槽来源验证。
    if (Profile.LastSelectedPrimary == Id) return;
    Profile.LastSelectedPrimary = Id;
    Profile.PreferenceRevision = Profile.LocalRevision < MAX_int32 ? Profile.LocalRevision + 1 : MAX_int32; // 标记主动选择，后续仅通关上传不覆盖别的设备偏好。
    CommitChange();
}
void UDemoPlayerProfile::CommitChange()
{
    DEMO_LOG_CALL();
    if (Profile.LocalRevision == MAX_int32) { bReadOnly = true; Status = TEXT("存档修订号已满，需要升级存档格式"); return; }
    ++Profile.LocalRevision;
    Profile.UpdatedUtc = FDateTime::UtcNow().ToIso8601();
    bNeedsSave = true;
    SaveProfile();
}
bool UDemoPlayerProfile::SaveProfile()
{
    DEMO_LOG_CALL();
    if (bReadOnly) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE save rejected: protected file")); return false; }
    UDemoProfileSave* Previous = Cast<UDemoProfileSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)); // 仅有效旧档才能轮换到备份，损坏主档不能覆盖好备份。
    if (Previous && Previous->Data.SchemaVersion<4 && (!DemoDurableSave::Archive(SlotName,SlotName+TEXT("_LegacyRaw")) || !DemoDurableSave::Archive(SlotName+TEXT("_Backup"),SlotName+TEXT("_LegacyBackupRaw"))))
    { bNeedsSave=true; Status=TEXT("旧档原始备份失败，冻结尚未发布"); return false; }
    if (Previous && Normalize(Previous->Data) && !DemoDurableSave::Write(Previous, SlotName + TEXT("_Backup")))
    { Status = TEXT("备份写入失败，进度暂存内存，请重试保存"); UE_LOG(LogFPSDemo, Error, TEXT("PROFILE backup save failed")); bNeedsSave = true; return false; }
    UDemoProfileSave* Snapshot = Cast<UDemoProfileSave>(UGameplayStatics::CreateSaveGameObject(UDemoProfileSave::StaticClass())); // 同步落盘期间临时拥有的数据快照。
    Snapshot->Data = Profile;
    bNeedsSave = !DemoDurableSave::Write(Snapshot, SlotName);
    Status = bNeedsSave ? TEXT("本地保存失败，进度暂存内存，请重试保存") : TEXT("已保存至本地");
    UE_LOG(LogFPSDemo, Log, TEXT("PROFILE_SAVE revision=%d ok=%d"), Profile.LocalRevision, !bNeedsSave);
    return !bNeedsSave;
}
bool UDemoPlayerProfile::CanSync() const { DEMO_LOG_TICK(); return !bReadOnly && !bNeedsSave; }
bool UDemoPlayerProfile::ApplyCloud(const TSharedPtr<FJsonObject>& Cloud, const TArray<FString>& AcceptedRuns, int32 AckRevision, bool bAckPreference)
{
    DEMO_LOG_CALL();
    if (!CanSync() || !Cloud || AckRevision < 0 || AckRevision > Profile.LocalRevision) return false;
    FString Revision; // 云端Int64字符串，仅作单调并发版本，不经过浮点数。
    FString Primary; // 下载偏好不产生装备，只影响终端提示。
    const TArray<TSharedPtr<FJsonValue>>* Difficulties = nullptr; // 响应对象拥有的数组，仅同步调用内借用。
    int64 NewRevision = 0; // 检查溢出/非法版本，拒绝未验证的回执。
    int64 OldRevision = 0; // 初次离线为空等同0。
    LexTryParseString(OldRevision, *Profile.ServerRevision);
    if (!Cloud->TryGetStringField(TEXT("serverRevision"), Revision) || !LexTryParseString(NewRevision, *Revision) || NewRevision < 0
        || !Cloud->TryGetStringField(TEXT("lastSelectedPrimary"), Primary) || !Cloud->TryGetArrayField(TEXT("clearedDifficulties"), Difficulties) || Difficulties->Num() > 5) return false;
    FDemoPlayerProfileData Candidate = Profile; // 先完整验证，失败不污染当前游戏内数据。
    if (bAckPreference) Candidate.SyncedPreferenceRevision = FMath::Max(Candidate.SyncedPreferenceRevision, AckRevision); // 只有实际携带偏好变化的请求才确认它。
    if (NewRevision >= OldRevision)
    {
        const TSharedPtr<FJsonObject>* Migration=nullptr; // HTTP3服务器冻结投影；旧API不能静默丢新字段。
        const TArray<TSharedPtr<FJsonValue>>* Ids=nullptr; const TArray<TSharedPtr<FJsonValue>>* Runs=nullptr; // 本响应同步借用。
        FString MigrationId; double Rules=0,Source=0; // 先检查整数/范围再转本地版本。
        if (!Cloud->TryGetObjectField(TEXT("legacyMigration"),Migration)||!(*Migration)->TryGetStringField(TEXT("migrationId"),MigrationId)
            ||!(*Migration)->TryGetNumberField(TEXT("rulesVersion"),Rules)||Rules!=1||!(*Migration)->TryGetNumberField(TEXT("sourceRevision"),Source)||!FMath::IsFinite(Source)||Source<0||Source>MAX_int32||Source!=FMath::FloorToDouble(Source)
            ||!(*Migration)->TryGetArrayField(TEXT("baselineWeaponIds"),Ids)||Ids->Num()>4||!(*Migration)->TryGetArrayField(TEXT("baselineRunIds"),Runs)||Runs->Num()>4096)
        { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE_CLOUD incompatible ownership capability")); return false; }
        TArray<FName> LegacyIds; TArray<FString> LegacyRuns; // 候选冻结列表，无法归属旧权时保护本地不免费扩权。
        for (const TSharedPtr<FJsonValue>& Value:*Ids) { FString Id; if (!Value||!Value->TryGetString(Id)||Id.Len()>32) return false; LegacyIds.Add(FName(*Id)); }
        for (const TSharedPtr<FJsonValue>& Value:*Runs) { FString Id; if (!Value||!Value->TryGetString(Id)||Id.Len()>36) return false; LegacyRuns.Add(Id); }
        FDemoWeaponOwnership RemoteOwnership; // 同一目录/GUID/去重规则，不在下载时按新Clears派枪。
        if (!RemoteOwnership.FreezeLegacy(LegacyIds,MigrationId,static_cast<int32>(Source))) return false;
        for (const FName Existing:Candidate.LegacyWeaponIds) if (!RemoteOwnership.Contains(Existing))
        { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE_CLOUD legacy migration conflict; local rights preserved")); return false; }
        Candidate.LegacyMigrationId=MigrationId; Candidate.LegacySourceRevision=static_cast<int32>(Source); Candidate.LegacyWeaponIds=LegacyIds; Candidate.LegacyRunIds=LegacyRuns;
        Candidate.CloudClearedDifficulties.Empty();
        for (const TSharedPtr<FJsonValue>& Value : *Difficulties) // 值由响应持有，立即复制到候选档案。
        { FString Difficulty; /* 严格读取字符串，不将类型错误当空值。 */ if (!Value->TryGetString(Difficulty)) return false; Candidate.CloudClearedDifficulties.AddUnique(Difficulty); }
        Candidate.ServerRevision = Revision;
        if (Candidate.PreferenceRevision <= Candidate.SyncedPreferenceRevision) Candidate.LastSelectedPrimary = Primary.IsEmpty() ? NAME_None : FName(*Primary);
    }
    for (const FString& Id : AcceptedRuns) // 仅移除本次回执明确确认的通关，新产生记录保留。
    {
        // 同步谓词只捕获本轮Id引用，RemoveAll在本调用内完成，不逃逸到异步回调。
        Candidate.Clears.RemoveAll([&Id](const FDemoClearRecord& Record) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] acknowledged run predicate")); return Record.RunId.Equals(Id,ESearchCase::IgnoreCase); });
        Candidate.RecentAcceptedRuns.AddUnique(Id);
    }
    while (Candidate.RecentAcceptedRuns.Num() > 128) Candidate.RecentAcceptedRuns.RemoveAt(0);
    Candidate.SyncedRevision = FMath::Max(Candidate.SyncedRevision, AckRevision);
    if (!Normalize(Candidate)) return false;
    Profile = MoveTemp(Candidate);
    bNeedsSave = true;
    return SaveProfile(); // 必须保存成功才能清除持久化发件箱。
}
bool UDemoPlayerProfile::BuildUploadJson(FString& OutJson) const
{
    DEMO_LOG_CALL();
    OutJson.Empty();
    if (bReadOnly) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE export rejected: protected/invalid save")); return false; }
    TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>(); // 独立协议包，未来网络层持有序列化字符串，不持有子系统引用。
    TSharedPtr<FJsonObject> Data = DemoCloudProtocol::BuildLegacyProfile(Profile); // 显式协议1兼容白名单；结构新增字段不自动进入上传。
    if (!Data) return false;
    Envelope->SetNumberField(TEXT("protocolVersion"), 1);
    Envelope->SetStringField(TEXT("requestId"), FString::Printf(TEXT("%s:%d"), *Profile.ProfileId, Profile.LocalRevision));
    Envelope->SetStringField(TEXT("baseServerRevision"), Profile.ServerRevision);
    Envelope->SetObjectField(TEXT("profile"), Data);
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson); // 同步序列化目标；函数返回前结束写入。
    return FJsonSerializer::Serialize(Envelope, Writer);
}
bool UDemoPlayerProfile::AcknowledgeUpload(const FString& ProfileId, int32 UploadedRevision, const FString& ServerRevision)
{
    DEMO_LOG_CALL();
    if (!IsInGameThread() || bReadOnly || ProfileId != Profile.ProfileId || UploadedRevision <= Profile.SyncedRevision
        || UploadedRevision > Profile.LocalRevision || ServerRevision.IsEmpty())
    { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE sync ack rejected: thread/identity/revision")); return false; }
    Profile.SyncedRevision = UploadedRevision;
    Profile.ServerRevision = ServerRevision;
    bNeedsSave = true;
    return SaveProfile(); // 不推进内容修订，上传期间的新内容保持dirty，网络适配器可再次导出。
}
