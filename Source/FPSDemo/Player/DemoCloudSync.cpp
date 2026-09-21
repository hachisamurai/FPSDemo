#include "Player/DemoCloudSync.h"
#include "Player/DemoPlayerProfile.h"
#include "Save/DemoRunSave.h"
#include "DemoApiClient.h"
#include "Debug/DemoLog.h"
#include "Game/DemoGameState.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"

namespace DemoCloud
{
    /** Json为独立值DTO，同步序列化用于发件箱/快照指纹，不记录正文。 */
    FString Encode(const TSharedPtr<FJsonObject>& Json)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] Cloud Encode"));
        FString Text; // 返回值拥有其缓冲，调用者可跨HTTP保存。
        if (Json) FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        return Text;
    }
    /** Text仅在游戏线程解析；无效正文返回null，不能以空值继续确认进度。 */
    TSharedPtr<FJsonObject> Decode(const FString& Text)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] Cloud Decode"));
        TSharedPtr<FJsonObject> Json; // 解析器写入的共享值对象，无UObject引用。
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    }
    /** Snapshot为本地固定字段顺序导出；MD5仅检测本地改变，不用作认证。 */
    FString Fingerprint(const TSharedPtr<FJsonObject>& Snapshot)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] Cloud Fingerprint"));
        return Snapshot ? FMD5::HashAnsiString(*Encode(Snapshot)) : FString();
    }
}
bool UDemoCloudSync::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    UE_LOG(LogFPSDemo, Log, TEXT("CLOUD_DISABLED_EDITOR: local saves only"));
    return false; // 独立Editor进程、PIE、多PIE和Editor-Cmd -game全部命中此分支。
#else
    return Super::ShouldCreateSubsystem(Outer);
#endif
}
void UDemoCloudSync::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL();
    Super::Initialize(Collection);
    Collection.InitializeDependency<UDemoPlayerProfile>();
    Collection.InitializeDependency<UDemoRunSaves>();
    Collection.InitializeDependency<UDemoApiClient>();
    Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>();
    Runs = GetGameInstance()->GetSubsystem<UDemoRunSaves>();
    Api = GetGameInstance()->GetSubsystem<UDemoApiClient>();
    // 旧测试只验证本地玩法，默认不能向开发者真实云账号写测试通关。
    const FString Command = FCommandLine::Get(); // 只检测固定开关，不将任意命令行作为路径。
    bDisabled = !Api->IsEnabled() || Command.Contains(TEXT("DemoSessionTest")) || Command.Contains(TEXT("DemoArmoryTest")) || Command.Contains(TEXT("DemoWeaponTest"))
        || Command.Contains(TEXT("DemoUIValidation")) || Command.Contains(TEXT("DemoSmokeTest")) || Command.Contains(TEXT("DemoCampaignTest")) || Command.Contains(TEXT("DemoEnemyAttackTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoAmmoTest")) /* 弹药专项不使用正式账号或存档。 */;
    if (bDisabled) { Status = TEXT("云存档已关闭（本地模式）"); return; }
#if !UE_BUILD_SHIPPING
    FString CloudTest; FGuid CloudId; // 同一GUID用于测试账号、永久档案和检查点的两阶段恢复，不影响正式槽。
    if (FParse::Value(FCommandLine::Get(), TEXT("DemoCloudTestId="), CloudTest) && FGuid::Parse(CloudTest, CloudId) && CloudId.IsValid()) SaveSlot = TEXT("DemoCloudTest_") + CloudId.ToString(EGuidFormats::Digits) + TEXT("_Cloud");
#endif
    State = Cast<UDemoCloudState>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
    if (State && State->Version > 1) { Fail(TEXT("同步状态版本较新，已保护文件"), true); return; }
    if (!State) State = Cast<UDemoCloudState>(UGameplayStatics::LoadGameFromSlot(SaveSlot + TEXT("_Backup"), 0));
    if (!State && (UGameplayStatics::DoesSaveGameExist(SaveSlot, 0) || UGameplayStatics::DoesSaveGameExist(SaveSlot + TEXT("_Backup"), 0)))
    { Fail(TEXT("同步状态损坏，已保护原文件"), true); return; }
    if (!State) State = NewObject<UDemoCloudState>(this);
    if (State->Version != 1 || (!State->SlotHashes.IsEmpty() && State->SlotHashes.Num() != 3) || (!State->SlotRevisions.IsEmpty() && State->SlotRevisions.Num() != 3))
    { Fail(TEXT("同步状态格式不兼容"), true); return; }
    State->SlotHashes.SetNum(3); State->SlotRevisions.SetNum(3);
    bReady = true; // 退出重试只能操作完成版本和数组长度校验的状态。
    for (FString& Revision : State->SlotRevisions) if (Revision.IsEmpty()) Revision = TEXT("0"); // 初次连接槽版本为零。
    NextAttempt = FPlatformTime::Seconds() + 2;
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDemoCloudSync::Tick), 2.f);
}
void UDemoCloudSync::Deinitialize()
{
    DEMO_LOG_CALL();
    if (Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if (Api && !bDisabled) Api->Cancel();
    Token.Empty(); Conflict.Reset();
    Super::Deinitialize();
}
const FString& UDemoCloudSync::GetStatus() const
{
    DEMO_LOG_TICK();
    if (Profile && Runs && Status == TEXT("永久进度与战役检查点已同步"))
    {
        const FDemoPlayerProfileData& Data = Profile->GetData(); // 只读内存版本，不在DrawHUD序列化或读盘。
        if (!Data.Clears.IsEmpty() || Data.LocalRevision > Data.SyncedRevision || Data.PreferenceRevision > Data.SyncedPreferenceRevision || CheckedRunSerial != Runs->GetChangeSerial()) return PendingStatus;
    }
    return Status;
}
bool UDemoCloudSync::HasConflict() const { DEMO_LOG_TICK(); return Conflict.IsValid(); }
void UDemoCloudSync::Fail(const FString& Error, bool bStop)
{
    DEMO_LOG_CALL();
    bBusy = false; bStopped = bStop; Status = Error;
    // 退出期间首次失败交给玩家决定；后台原有退避补传仍可继续，但不能自动关闭失败弹窗。
    if (ExitState == EDemoCloudExitState::Pending) ExitState = EDemoCloudExitState::Failed;
    Attempts = FMath::Min(Attempts + 1, 6);
    NextAttempt = FPlatformTime::Seconds() + FMath::Min(60.f, FMath::Pow(2.f, Attempts));
    UE_LOG(LogFPSDemo, Warning, TEXT("CLOUD_FAIL stopped=%d reason=%s"), bStop, *Error);
}
bool UDemoCloudSync::Persist()
{
    DEMO_LOG_CALL();
    if (!State) return false;
    UDemoCloudState* Previous = Cast<UDemoCloudState>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)); // 上一个可用状态，只在同版本时备份。
    if (Previous && Previous->Version == 1 && !UGameplayStatics::SaveGameToSlot(Previous, SaveSlot + TEXT("_Backup"), 0)) { Fail(TEXT("同步队列备份失败"), true); return false; }
    if (!UGameplayStatics::SaveGameToSlot(State, SaveSlot, 0)) { Fail(TEXT("同步队列保存失败，未发送新请求"), true); return false; }
    return true;
}
bool UDemoCloudSync::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (bDisabled || bBusy || bStopped || !State || FPlatformTime::Seconds() < NextAttempt) return true;
    if (!Profile->CanSync() && !Profile->SaveProfile()) { Fail(TEXT("请先恢复本地存档写入"), true); return true; }
    if (FPlatformTime::Seconds() >= NextPull) bNeedPull = true; // 不依赖World定时器，暂停页也能刷新远端版本。
    if (Token.IsEmpty()) Login();
    else if (!State->Outbox.IsEmpty() || bNeedPull) Prepare();
    else Upload();
    return true;
}
void UDemoCloudSync::Retry()
{
    DEMO_LOG_CALL();
    if (bDisabled || bBusy || !bReady || !State || Conflict) { UE_LOG(LogFPSDemo, Log, TEXT("Cloud retry rejected: disabled/busy/invalid/conflict")); return; }
    bStopped = false; Attempts = 0; bNeedPull = true; NextAttempt = 0;
    Profile->SaveProfile();
}
void UDemoCloudSync::BeginExitSync()
{
    DEMO_LOG_CALL();
    ExitState = bDisabled ? EDemoCloudExitState::LocalOnly : EDemoCloudExitState::Pending;
    ExitDeadline = FPlatformTime::Seconds() + 30.0;
    if (bDisabled) { UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SYNC local-only configuration")); return; }
    if (!bReady || !State || Conflict)
    {
        ExitState = EDemoCloudExitState::Failed;
        UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SYNC blocked: %s"), *Status);
        return;
    }
    // 在途请求保持原回调；它确认的是旧快照时，Upload仍会继续发现最新本地改动。
    bStopped = false; Attempts = 0; bNeedPull = true; NextAttempt = 0;
    if (!bBusy && !Persist()) return; // 在途请求结束后自会落盘；不能在此写盘失败时提前解除其bBusy保护。
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SYNC waiting busy=%d outbox=%d"), bBusy, !State->Outbox.IsEmpty());
}
EDemoCloudExitState UDemoCloudSync::PollExitSync()
{
    DEMO_LOG_TICK();
    if (ExitState == EDemoCloudExitState::Pending)
    {
        if (FPlatformTime::Seconds() >= ExitDeadline)
        {
            ExitState = EDemoCloudExitState::Failed;
            Status = TEXT("云端保存超时；本地进度已保留，可重试或稍后补传");
            UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SYNC timeout; durable outbox retained"));
        }
        else Tick(0.f); // 同游戏线程且受bBusy/NextAttempt约束，暂停不依赖World定时器。
    }
    return ExitState;
}
void UDemoCloudSync::CancelExitSync()
{
    DEMO_LOG_CALL();
    ExitState = EDemoCloudExitState::Idle;
    ExitDeadline = 0;
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SYNC cancelled; background synchronization retained"));
}
void UDemoCloudSync::Request(const FString& Verb, const FString& Path, const FString& Body, int32 CurrentStep)
{
    DEMO_LOG_CALL();
    Step = CurrentStep; bBusy = true;
    if (!Api->Send(Verb, Path, Body, Token, FDemoApiReply::CreateUObject(this, &UDemoCloudSync::Reply))) Fail(TEXT("网络请求未启动，将自动重试"));
}
void UDemoCloudSync::Login()
{
    DEMO_LOG_CALL();
    FString Id; // 本机身份GUID，只参与匿名注册/认证。
    FString Secret; // 短生命周期明文，绝不保存到发件箱/日志。
    if (!Api->LoadIdentity(Id, Secret)) { Fail(Api->GetIdentityError(), true); return; } // 保留具体失败阶段；首次创建失败不能误报为读取旧账号失败。
    TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>(); // 仅发给API的认证DTO。
    Body->SetStringField(TEXT("installationId"), Id); Body->SetStringField(TEXT("secret"), Secret);
    Status = TEXT("正在连接云存档");
    Request(TEXT("POST"), TEXT("/v1/auth/session"), DemoCloud::Encode(Body), 1);
}
void UDemoCloudSync::Prepare()
{
    DEMO_LOG_CALL();
    if (!State->Outbox.IsEmpty()) { SendOutbox(); return; }
    if (State->ClientProfileId != Profile->GetData().ProfileId || State->BindingId.IsEmpty())
    {
        if (State->ClientProfileId != Profile->GetData().ProfileId || State->Epoch.IsEmpty()) State->Epoch = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        State->ClientProfileId = Profile->GetData().ProfileId;
        State->BindingId.Empty();
        if (!Persist()) return;
        TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>(); // 幂等绑定值，不能选择服务器账号。
        Body->SetStringField(TEXT("clientProfileId"), State->ClientProfileId); Body->SetStringField(TEXT("epoch"), State->Epoch);
        Request(TEXT("POST"), TEXT("/v1/me/profile-bindings"), DemoCloud::Encode(Body), 2);
        return;
    }
    Pull();
}
void UDemoCloudSync::Pull() { DEMO_LOG_CALL(); Request(TEXT("GET"), TEXT("/v1/me/profile"), FString(), 3); }
bool UDemoCloudSync::CanImport() const
{
    DEMO_LOG_CALL();
    const ADemoGameState* Game = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 当前世界借用，跨地图不缓存。
    return Game && Game->Phase == EDemoPhase::Lobby;
}
bool UDemoCloudSync::MergeSlots(const TSharedPtr<FJsonObject>& Remote, int32 Resolution)
{
    DEMO_LOG_CALL();
    const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr; // 云端对象拥有，函数内只读。
    if (!Remote->TryGetArrayField(TEXT("slots"), Slots) || Slots->Num() > 3) { Fail(TEXT("云端检查点列表格式无效"), true); return false; }
    // 第一遍只发现冲突，不能先覆盖某槽再发现另一槽需要用户选择。
    for (const TSharedPtr<FJsonValue>& Value : *Slots)
    {
        const TSharedPtr<FJsonObject> Slot = Value->AsObject(); // 单槽稳定协议对象。
        int32 Index = -1; FString Revision; // 数组位置不能替代明确槽号/版本。
        if (!Slot || !Slot->TryGetNumberField(TEXT("slotIndex"), Index) || Index < 0 || Index > 2 || !Slot->TryGetStringField(TEXT("serverRevision"), Revision)) { Fail(TEXT("云端槽号无效"), true); return false; }
        if (Revision == State->SlotRevisions[Index] && Runs->GetSlot(Index)) continue; // 本地槽被删除时，即使云版本未变化也要重新下载。
        const FString LocalHash = DemoCloud::Fingerprint(Runs->ExportCloud(Index)); // 空槽为空字符串，不把云端新建视为冲突。
        if (Resolution == 0 && !LocalHash.IsEmpty() && LocalHash != State->SlotHashes[Index])
        { Conflict = Remote; Fail(TEXT("检查点冲突：回大厅后选择保留本机或使用云端"), true); return false; }
        if (Resolution != 1 && !CanImport()) { bNeedPull = true; Fail(TEXT("云端有新检查点，回大厅后下载")); return false; }
    }
    for (const TSharedPtr<FJsonValue>& Value : *Slots) // 验证通过后顺序落盘；每个成功槽记录版本，失败可重入恢复。
    {
        const TSharedPtr<FJsonObject> Slot = Value->AsObject(); // 已在上轮验证槽号与版本。
        const int32 Index = Slot->GetIntegerField(TEXT("slotIndex")); // UI索引0..2。
        const FString Revision = Slot->GetStringField(TEXT("serverRevision")); // 该槽独立CAS版本。
        if (Revision == State->SlotRevisions[Index] && Runs->GetSlot(Index)) continue; // 同版本缺失槽也属于需要恢复的文件。
        const TSharedPtr<FJsonObject>* Snapshot = nullptr; // 云端快照只在Import内部拷贝/验证。
        if (!Slot->TryGetObjectField(TEXT("snapshot"), Snapshot)) { Fail(TEXT("云端检查点缺少快照"), true); return false; }
        if (Resolution != 1)
        {
            if (!Runs->ImportCloud(Index, *Snapshot)) { Fail(TEXT("检查点下载写盘失败，保留云端数据"), true); return false; }
            State->SlotHashes[Index] = DemoCloud::Fingerprint(Runs->ExportCloud(Index));
        }
        State->SlotRevisions[Index] = Revision;
        if (!Persist()) return false;
    }
    return true;
}
void UDemoCloudSync::Upload()
{
    DEMO_LOG_CALL();
    if (!Profile->CanSync()) { Fail(TEXT("本地进度尚未落盘"), true); return; }
    const FDemoPlayerProfileData& Data = Profile->GetData(); // 游戏线程只读借用，序列化完成即释放引用。
    TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>(); // 发件箱快照；发出后不再改变字段。
    Body->SetNumberField(TEXT("protocolVersion"), 2);
    Body->SetStringField(TEXT("requestId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
    Body->SetStringField(TEXT("bindingId"), State->BindingId);
    Body->SetNumberField(TEXT("localRevision"), Data.LocalRevision);
    Body->SetStringField(TEXT("baseServerRevision"), Data.ServerRevision.IsEmpty() ? TEXT("0") : Data.ServerRevision);
    TArray<TSharedPtr<FJsonValue>> Clears; // 一批最多64条，先迁移通关，再同步依赖这些解锁的装备快照。
    for (int32 Index = 0; Index < FMath::Min(64, Data.Clears.Num()); ++Index) // Index仅在本调用内借用记录。
    {
        const FDemoClearRecord& Clear = Data.Clears[Index]; // 本GI待确认事实。
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>(); // 独立通关协议值。
        Item->SetStringField(TEXT("runId"), Clear.RunId); Item->SetStringField(TEXT("difficultyId"), Clear.DifficultyId); Item->SetStringField(TEXT("completedUtc"), Clear.CompletedUtc);
        Clears.Add(MakeShared<FJsonValueObject>(Item));
    }
    Body->SetArrayField(TEXT("clears"), Clears);
    const bool bLastBatch = Data.Clears.Num() <= 64; // 避免早于相关解锁上传高级武器检查点/偏好。
    Body->SetBoolField(TEXT("hasPreferenceChange"), bLastBatch && Data.PreferenceRevision > Data.SyncedPreferenceRevision);
    Body->SetStringField(TEXT("lastSelectedPrimary"), Data.LastSelectedPrimary.IsNone() ? TEXT("") : Data.LastSelectedPrimary.ToString());
    TArray<TSharedPtr<FJsonValue>> Slots; // 只上传发生改变的槽，独立CAS避免覆盖其他设备进度。
    if (bLastBatch) for (int32 Index = 0; Index < 3; ++Index)
    {
        TSharedPtr<FJsonObject> Snapshot = Runs->ExportCloud(Index); // 即时复制持久缓存，后续本地保存不改变发件箱。
        if (!Snapshot || DemoCloud::Fingerprint(Snapshot) == State->SlotHashes[Index]) continue;
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>(); // 单槽写操作。
        Item->SetNumberField(TEXT("slotIndex"), Index); Item->SetStringField(TEXT("baseSlotRevision"), State->SlotRevisions[Index]); Item->SetObjectField(TEXT("snapshot"), Snapshot);
        Slots.Add(MakeShared<FJsonValueObject>(Item));
    }
    Body->SetArrayField(TEXT("slots"), Slots);
    if (Clears.IsEmpty() && Slots.IsEmpty() && Data.LocalRevision <= Data.SyncedRevision && Data.PreferenceRevision <= Data.SyncedPreferenceRevision)
    {
        CheckedRunSerial = Runs->GetChangeSerial(); Status = TEXT("永久进度与战役检查点已同步"); NextAttempt = FPlatformTime::Seconds() + 2;
        // 旧回执不是退出条件：必须再次比较最新三槽指纹、永久修订与分批通关队列。
        if (ExitState == EDemoCloudExitState::Pending && State->Outbox.IsEmpty())
        {
            ExitState = EDemoCloudExitState::Complete;
            UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SYNC complete: latest local snapshots confirmed"));
        }
        return;
    }
    State->Outbox = DemoCloud::Encode(Body);
    State->OutboxProfileId = Data.ProfileId;
    if (Persist()) SendOutbox();
}
void UDemoCloudSync::SendOutbox() { DEMO_LOG_CALL(); Status = TEXT("正在同步永久进度与检查点"); Request(TEXT("POST"), TEXT("/v1/me/profile/sync"), State->Outbox, 4); }
void UDemoCloudSync::Reply(bool bSuccess, int32 Code, const FString& Body)
{
    DEMO_LOG_CALL();
    bBusy = false;
    if (!bSuccess || Code >= 500 || Code == 429) { Fail(TEXT("云服务暂不可用，进度保留本地并自动重试")); return; }
    TSharedPtr<FJsonObject> Json = DemoCloud::Decode(Body); // 不将HTTP正文写日志，认证响应可能包含Bearer。
    if (!Json) { Fail(TEXT("服务器响应格式无效"), true); return; }
    FString Error; // 稳定协议错误码，不含异常堆栈/密码。
    Json->TryGetStringField(TEXT("code"), Error);
    if (Code == 401) { Token.Empty(); Fail(TEXT("云账号会话无效，正在重新认证"), Step == 1); return; }
    if (Code == 409 && Step == 4 && (Error == TEXT("revision_conflict") || Error == TEXT("binding_stale") || Error == TEXT("slot_conflict")))
    {
        // 明确拒绝表示事务未提交，此时才可丢弃旧请求ID并重新拉取；网络超时绝不能走这里。
        State->Outbox.Empty(); State->OutboxProfileId.Empty();
        if (Error == TEXT("binding_stale")) { State->BindingId.Empty(); State->Epoch.Empty(); }
        bNeedPull = true;
        if (Persist()) Prepare();
        return;
    }
    if (Code < 200 || Code >= 300) { UE_LOG(LogFPSDemo, Warning, TEXT("Cloud protocol rejection HTTP=%d code=%s"), Code, *Error); Fail(TEXT("云同步请求被拒绝，请查看协议错误日志"), true); return; }
    Attempts = 0;
    if (Step == 1)
    {
        FString Player; // 登录返回的服务器唯一身份，必须与既有绑定一致。
        if (!Json->TryGetStringField(TEXT("accessToken"), Token) || Token.Len() != 64 || !Json->TryGetStringField(TEXT("playerId"), Player)) { Fail(TEXT("认证响应不完整"), true); return; }
        if (!State->PlayerId.IsEmpty() && State->PlayerId != Player) { Token.Empty(); Fail(TEXT("账号与本地同步档案不一致，已阻止合并"), true); return; }
        State->PlayerId = Player;
        if (Persist()) Prepare();
        return;
    }
    if (Step == 2)
    {
        if (!Json->TryGetStringField(TEXT("bindingId"), State->BindingId) || State->BindingId.IsEmpty()) { Fail(TEXT("绑定响应不完整"), true); return; }
        if (Persist()) Pull();
        return;
    }
    const TSharedPtr<FJsonObject>* Nested = nullptr; // 上传回执内的完整投影，下载直接使用根对象。
    TSharedPtr<FJsonObject> Remote = Json;
    if (Step == 4) { if (!Json->TryGetObjectField(TEXT("profile"), Nested)) { Fail(TEXT("同步回执缺少档案"), true); return; } Remote = *Nested; }
    FString Owner; // 对端身份必须匹配当前认证账号，避免错误服务配置导入他人数据。
    if (!Remote->TryGetStringField(TEXT("serverProfileId"), Owner) || Owner != State->PlayerId) { Fail(TEXT("云端档案身份校验失败"), true); return; }
    if (Step == 3)
    {
        if (!Profile->ApplyCloud(Remote, {}, 0)) { Fail(TEXT("云端永久进度无法保存到本地"), true); return; }
        if (!MergeSlots(Remote)) return;
        bNeedPull = false; NextPull = FPlatformTime::Seconds() + 60; Upload(); return;
    }
    TSharedPtr<FJsonObject> Sent = DemoCloud::Decode(State->Outbox); // 回执必须匹配持久化请求，不接受裸服务器修订。
    FString RequestId; FString Binding; int32 Revision = 0; // HTTP回执的三项匹配字段。
    const TArray<TSharedPtr<FJsonValue>>* Accepted = nullptr; // 服务端明确确认的通关ID集合。
    if (!Sent || !Json->TryGetStringField(TEXT("requestId"), RequestId) || RequestId != Sent->GetStringField(TEXT("requestId"))
        || !Json->TryGetStringField(TEXT("bindingId"), Binding) || Binding != Sent->GetStringField(TEXT("bindingId"))
        || !Json->TryGetNumberField(TEXT("acceptedLocalRevision"), Revision) || Revision != Sent->GetIntegerField(TEXT("localRevision")) || !Json->TryGetArrayField(TEXT("acceptedRunIds"), Accepted))
    { Fail(TEXT("上传回执与本地请求不匹配"), true); return; }
    TArray<FString> AcceptedIds; // 值拷贝，后续删除队列只能针对明确接受的记录。
    for (const TSharedPtr<FJsonValue>& Value : *Accepted) AcceptedIds.Add(Value->AsString());
    const bool bSameProfile = State->OutboxProfileId == Profile->GetData().ProfileId; // 删除本地档案后旧回执不能确认新修订。
    if (!Profile->ApplyCloud(Remote, bSameProfile ? AcceptedIds : TArray<FString>(), bSameProfile ? Revision : 0, bSameProfile && Sent->GetBoolField(TEXT("hasPreferenceChange")))) { Fail(TEXT("确认回执写盘失败，保留请求以便重放"), true); return; }
    const TArray<TSharedPtr<FJsonValue>>* RemoteSlots = nullptr; // 回执内各槽提交后的版本。
    if (!Remote->TryGetArrayField(TEXT("slots"), RemoteSlots)) { Fail(TEXT("回执缺少检查点版本"), true); return; }
    for (const TSharedPtr<FJsonValue>& Value : Sent->GetArrayField(TEXT("slots")))
    {
        const TSharedPtr<FJsonObject> Item = Value->AsObject(); // 持久化发件箱中的旧快照，不是正在玩的新进度。
        const int32 Index = Item->GetIntegerField(TEXT("slotIndex")); // 由本机生成的0..2槽索引。
        for (const TSharedPtr<FJsonValue>& RemoteValue : *RemoteSlots)
        {
            const TSharedPtr<FJsonObject> RemoteSlot = RemoteValue->AsObject(); // 当前回执提交的相应槽。
            if (RemoteSlot->GetIntegerField(TEXT("slotIndex")) != Index) continue;
            State->SlotRevisions[Index] = RemoteSlot->GetStringField(TEXT("serverRevision"));
            // 使用本地导出字段顺序保存的原请求快照算hash，上传期间新本地修改仍保持dirty。
            State->SlotHashes[Index] = DemoCloud::Fingerprint(Item->GetObjectField(TEXT("snapshot")));
        }
    }
    State->Outbox.Empty(); State->OutboxProfileId.Empty();
    if (!Persist()) return;
    bNeedPull = true; Status = TEXT("云存档已确认保存"); NextAttempt = FPlatformTime::Seconds() + 2;
    UE_LOG(LogFPSDemo, Log, TEXT("CLOUD_ACK localRevision=%d"), Revision);
}
void UDemoCloudSync::ResolveConflict(bool bKeepLocal)
{
    DEMO_LOG_CALL();
    if (!Conflict || bBusy) return;
    if (!CanImport()) { Status = TEXT("请先返回大厅，再处理检查点冲突"); return; }
    TSharedRef<FJsonObject> Archive = MakeShared<FJsonObject>(); // 两份值快照保留诊断/人工恢复，永不含认证数据。
    Archive->SetObjectField(TEXT("remote"), Conflict);
    TArray<TSharedPtr<FJsonValue>> Local; // 三槽按索引保存，可空，不影响正式槽。
    for (int32 Index = 0; Index < 3; ++Index)
    { TSharedPtr<FJsonObject> Snapshot = Runs->ExportCloud(Index); /* 当前GI值对象立即拷贝为JSON。 */ if (Snapshot) Local.Add(MakeShared<FJsonValueObject>(Snapshot)); else Local.Add(MakeShared<FJsonValueNull>()); }
    Archive->SetArrayField(TEXT("local"), Local);
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Cloud/Conflicts")); // 冲突副本独立目录，不覆盖玩家已有文件。
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = FPaths::Combine(Directory, FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json")); // 每次选择唯一副本。
    if (!FFileHelper::SaveStringToFile(DemoCloud::Encode(Archive), *Path)) { Status = TEXT("冲突备份写入失败，未覆盖任何检查点"); return; }
    if (!MergeSlots(Conflict, bKeepLocal ? 1 : 2)) return;
    Conflict.Reset(); bStopped = false; bNeedPull = true; NextAttempt = 0;
    Status = bKeepLocal ? TEXT("已保留本机检查点，等待上传") : TEXT("已采用云端检查点");
}
