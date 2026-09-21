#include "Player/DemoPlayerProfile.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"

void UDemoPlayerProfile::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL();
    Super::Initialize(Collection);
    // 所有现有自动化入口隔离存档；每次进程/PIE GI独立，OpenLevel仍复用同一子系统。
    bTestSlot = FParse::Param(FCommandLine::Get(), TEXT("DemoSessionTest")) || FParse::Param(FCommandLine::Get(), TEXT("DemoArmoryTest")) || FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponTest"))
        || FParse::Param(FCommandLine::Get(), TEXT("DemoUIValidation")) || FParse::Param(FCommandLine::Get(), TEXT("DemoSmokeTest"))
        // 敌人攻击/导航专项在非Editor打包验证时也必须隔离，不能加载正式武器解锁档案。
        || FParse::Param(FCommandLine::Get(), TEXT("DemoCampaignTest")) || FParse::Param(FCommandLine::Get(), TEXT("DemoEnemyAttackTest"))
        || FParse::Param(FCommandLine::Get(), TEXT("DemoAmmoTest")); // 弹药打包回归也使用临时武器档案，退出精确删除本GI文件。
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
    else CommitChange();
    UE_LOG(LogFPSDemo, Log, TEXT("PROFILE_LOAD slot=%s status=%s readonly=%d"), *SlotName, *Status, bReadOnly);
}
bool UDemoPlayerProfile::ReloadFromDisk()
{
    DEMO_LOG_CALL();
    if (bNeedsSave) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE reload rejected: unsaved in-memory changes")); return false; }
    UDemoProfileSave* Loaded = Cast<UDemoProfileSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)); // 临时加载快照，通过校验才替换内存。
    if (Loaded && Loaded->Data.SchemaVersion > 2)
    {
        bReadOnly = true;
        Status = TEXT("存档版本较新，已保护原文件；请使用对应版本");
        UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE future schema protected"));
        return false;
    }
    if (Loaded && Normalize(Loaded->Data))
    { Profile = Loaded->Data; bReadOnly = false; Status = TEXT("本地存档已载入"); return true; }
    UDemoProfileSave* Backup = Cast<UDemoProfileSave>(UGameplayStatics::LoadGameFromSlot(SlotName + TEXT("_Backup"), 0)); // 主档丢失也先恢复备份，绝不直接创建新档覆盖它。
    if (Backup && Normalize(Backup->Data))
    { Profile = Backup->Data; bReadOnly = false; Status = TEXT("主档异常，已从备份恢复"); bNeedsSave = true; UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE restored backup")); return true; }
    bReadOnly = true;
    Status = TEXT("存档读取失败，已保护原文件；本次进度暂不能保存");
    UE_LOG(LogFPSDemo, Error, TEXT("PROFILE primary/backup invalid; write protected"));
    return false;
}
void UDemoPlayerProfile::Deinitialize()
{
    DEMO_LOG_CALL();
    // 即将丢弃的试玩数据无需在退出时补写；正式游戏仍保留原来的失败重试行为。
    if (!bEditorSession && !bTestSlot && bNeedsSave && !bReadOnly) SaveProfile();
    if (bEditorSession || bTestSlot)
    {
        for (int32 Backup = 0; Backup < 2; ++Backup) // 主档和上次有效备份都属于当前GI，不扫描其他实例文件。
        {
            const FString OwnedSlot = SlotName + (Backup ? TEXT("_Backup") : TEXT("")); // 稳定前缀来自Initialize生成的随机GUID。
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
    if ((Data.SchemaVersion != 1 && Data.SchemaVersion != 2) || !FGuid::Parse(Data.ProfileId, ParsedId) || !ParsedId.IsValid()
        || Data.LocalRevision < 0 || Data.SyncedRevision < 0 || Data.SyncedRevision > Data.LocalRevision)
    { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE invalid schema/identity/revision")); return false; }
    Data.UnlockedWeaponIds = { TEXT("pistol") };
    if (Data.SchemaVersion == 1) { Data.SchemaVersion = 2; Data.PreferenceRevision = Data.LastSelectedPrimary.IsNone() ? 0 : Data.LocalRevision; } // 老档记录完整保留，首次联网再按RunId幂等迁移。
    if (Data.PreferenceRevision < 0 || Data.PreferenceRevision > Data.LocalRevision || Data.SyncedPreferenceRevision < 0 || Data.SyncedPreferenceRevision > Data.LocalRevision || Data.CloudClearedDifficulties.Num() > 3 || Data.RecentAcceptedRuns.Num() > 128) return false;
    for (const FString& Difficulty : Data.CloudClearedDifficulties) // 云端基线也要验证，磁盘字段不是可信授权。
    {
        if (Difficulty != TEXT("easy") && Difficulty != TEXT("normal") && Difficulty != TEXT("hard")) return false;
        Data.UnlockedWeaponIds.AddUnique(Difficulty == TEXT("easy") ? FName(TEXT("rifle")) : Difficulty == TEXT("normal") ? FName(TEXT("shotgun")) : FName(TEXT("sniper")));
    }
    FDateTime ParsedUtc; // 校验时间格式，协议中只允许UTC ISO8601字符串。
    if (!Data.UpdatedUtc.IsEmpty() && !FDateTime::ParseIso8601(*Data.UpdatedUtc, ParsedUtc)) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE invalid update timestamp")); return false; }
    TSet<FString> Seen; // 本次加载的幂等记录集合，不跨调用保存。
    for (const FDemoClearRecord& Clear : Data.Clears) // 档案拥有记录；只读借用进行验证。
    {
        FGuid Run; // 每条凭据必须有有效且唯一的通关GUID。
        if (!FGuid::Parse(Clear.RunId, Run) || !Run.IsValid() || Seen.Contains(Clear.RunId)
            || !FDateTime::ParseIso8601(*Clear.CompletedUtc, ParsedUtc)
            || (Clear.DifficultyId != TEXT("easy") && Clear.DifficultyId != TEXT("normal") && Clear.DifficultyId != TEXT("hard")))
        { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE invalid completion record")); return false; }
        Seen.Add(Clear.RunId);
        Data.UnlockedWeaponIds.AddUnique(Clear.DifficultyId == TEXT("easy") ? FName(TEXT("rifle")) : Clear.DifficultyId == TEXT("normal") ? FName(TEXT("shotgun")) : FName(TEXT("sniper")));
    }
    if (!Data.UnlockedWeaponIds.Contains(Data.LastSelectedPrimary) || Data.LastSelectedPrimary == TEXT("pistol")) Data.LastSelectedPrimary = NAME_None;
    return true;
}
const FDemoPlayerProfileData& UDemoPlayerProfile::GetData() const { DEMO_LOG_TICK(); return Profile; }
bool UDemoPlayerProfile::IsUnlocked(FName Id) const { DEMO_LOG_TICK(); return DemoWeaponCatalog::IndexOf(Id) != INDEX_NONE && Profile.UnlockedWeaponIds.Contains(Id); }
const FString& UDemoPlayerProfile::GetStatus() const { DEMO_LOG_TICK(); return Status; }
const FString& UDemoPlayerProfile::GetSlotName() const { DEMO_LOG_TICK(); return SlotName; }
bool UDemoPlayerProfile::RecordVictory(const FString& RunId, EDemoDifficulty Difficulty)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 当前权威世界的真实胜利状态。
    FGuid ParsedRun; // 通关幂等标识，不能接受空GUID或非法难度。
    if (!GetWorld() || !GetWorld()->GetAuthGameMode() || !State || State->Phase != EDemoPhase::Victory || State->LevelNumber != DemoCombatConfig::LevelCount
        || State->Difficulty != Difficulty || static_cast<uint8>(Difficulty) > 2 || !FGuid::Parse(RunId, ParsedRun) || !ParsedRun.IsValid())
    { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE victory rejected: invalid final state/run")); return false; }
    for (const FDemoClearRecord& Existing : Profile.Clears) // 仅同局幂等，不将重复回调计为另一次通关。
        if (Existing.RunId == RunId) { UE_LOG(LogFPSDemo, Log, TEXT("PROFILE duplicate victory ignored")); return true; }
    if (Profile.RecentAcceptedRuns.Contains(RunId)) { UE_LOG(LogFPSDemo, Log, TEXT("PROFILE already confirmed victory ignored")); return true; }
    FDemoClearRecord Record; // 即将追加的持久化值，不包含World引用。
    Record.RunId = RunId;
    Record.DifficultyId = Difficulty == EDemoDifficulty::Easy ? TEXT("easy") : Difficulty == EDemoDifficulty::Normal ? TEXT("normal") : TEXT("hard");
    Record.CompletedUtc = FDateTime::UtcNow().ToIso8601();
    Profile.Clears.Add(Record);
    Normalize(Profile);
    CommitChange();
    UE_LOG(LogFPSDemo, Log, TEXT("PROFILE victory recorded difficulty=%s; unlock only, no equip"), *Record.DifficultyId);
    return true;
}
void UDemoPlayerProfile::RememberPrimary(FName Id)
{
    DEMO_LOG_CALL();
    if (!IsUnlocked(Id) || Id == TEXT("pistol")) { UE_LOG(LogFPSDemo, Warning, TEXT("PROFILE selection rejected: locked/non-primary")); return; }
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
    if (Previous && Normalize(Previous->Data) && !UGameplayStatics::SaveGameToSlot(Previous, SlotName + TEXT("_Backup"), 0))
    { Status = TEXT("备份写入失败，进度暂存内存，请重试保存"); UE_LOG(LogFPSDemo, Error, TEXT("PROFILE backup save failed")); bNeedsSave = true; return false; }
    UDemoProfileSave* Snapshot = Cast<UDemoProfileSave>(UGameplayStatics::CreateSaveGameObject(UDemoProfileSave::StaticClass())); // 同步落盘期间临时拥有的数据快照。
    Snapshot->Data = Profile;
    bNeedsSave = !UGameplayStatics::SaveGameToSlot(Snapshot, SlotName, 0);
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
        || !Cloud->TryGetStringField(TEXT("lastSelectedPrimary"), Primary) || !Cloud->TryGetArrayField(TEXT("clearedDifficulties"), Difficulties) || Difficulties->Num() > 3) return false;
    FDemoPlayerProfileData Candidate = Profile; // 先完整验证，失败不污染当前游戏内数据。
    if (bAckPreference) Candidate.SyncedPreferenceRevision = FMath::Max(Candidate.SyncedPreferenceRevision, AckRevision); // 只有实际携带偏好变化的请求才确认它。
    if (NewRevision >= OldRevision)
    {
        Candidate.CloudClearedDifficulties.Empty();
        for (const TSharedPtr<FJsonValue>& Value : *Difficulties) // 值由响应持有，立即复制到候选档案。
        { FString Difficulty; /* 严格读取字符串，不将类型错误当空值。 */ if (!Value->TryGetString(Difficulty)) return false; Candidate.CloudClearedDifficulties.AddUnique(Difficulty); }
        Candidate.ServerRevision = Revision;
        if (Candidate.PreferenceRevision <= Candidate.SyncedPreferenceRevision) Candidate.LastSelectedPrimary = Primary.IsEmpty() ? NAME_None : FName(*Primary);
    }
    for (const FString& Id : AcceptedRuns) // 仅移除本次回执明确确认的通关，新产生记录保留。
    {
        // 同步谓词只捕获本轮Id引用，RemoveAll在本调用内完成，不逃逸到异步回调。
        Candidate.Clears.RemoveAll([&Id](const FDemoClearRecord& Record) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] acknowledged run predicate")); return Record.RunId == Id; });
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
    TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>(); // USTRUCT转换的数据对象，无UObject资源路径。
    if (!FJsonObjectConverter::UStructToJsonObject(FDemoPlayerProfileData::StaticStruct(), &Profile, Data, 0, 0)) return false;
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
