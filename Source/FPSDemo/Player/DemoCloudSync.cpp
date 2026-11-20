// 职责：非Editor云同步编排与不可变Outbox；本地整槽提交独立成功，HTTP3不兼容/旧权冲突停止上传并保留材料。
#include "Player/DemoCloudSync.h"
#include "Player/Cloud/DemoCloudProtocol.h"
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
        || Command.Contains(TEXT("DemoUIValidation")) || Command.Contains(TEXT("DemoSmokeTest")) || Command.Contains(TEXT("DemoCampaignTest")) || Command.Contains(TEXT("DemoEnemyAttackTest")) || FString(FCommandLine::Get()).Contains(TEXT("DemoProjectileTest")) /* 实体子弹测试禁用正式存档与云同步。 */ || FString(FCommandLine::Get()).Contains(TEXT("DemoAmmoTest")) /* 弹药专项不使用正式账号或存档。 */;
    if (bDisabled) { StatusCode = EDemoCloudStatus::Disabled; Status = TEXT("云存档已关闭（本地模式）"); return; }
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
    if (Profile && Runs && StatusCode == EDemoCloudStatus::Synchronized)
    {
        const FDemoPlayerProfileData& Data = Profile->GetData(); // 只读内存版本，不在DrawHUD序列化或读盘。
        if (!Data.Clears.IsEmpty() || Data.LocalRevision > Data.SyncedRevision || Data.PreferenceRevision > Data.SyncedPreferenceRevision || CheckedRunSerial != Runs->GetChangeSerial()) return PendingStatus;
    }
    return Status;
}
EDemoCloudStatus UDemoCloudSync::GetStatusCode() const
{
    DEMO_LOG_TICK();
    if(StatusCode==EDemoCloudStatus::Synchronized&&Profile&&Runs)
    {
        const FDemoPlayerProfileData& Data=Profile->GetData(); // 只查修订/序列，不在HUD调用中读盘或生成JSON。
        if(!Data.Clears.IsEmpty()||Data.LocalRevision>Data.SyncedRevision||Data.PreferenceRevision>Data.SyncedPreferenceRevision||CheckedRunSerial!=Runs->GetChangeSerial())return EDemoCloudStatus::Pending;
    }
    return StatusCode;
}
bool UDemoCloudSync::HasConflict() const { DEMO_LOG_TICK(); return Conflict.IsValid(); }
void UDemoCloudSync::Fail(const FString& Error, bool bStop)
{
    DEMO_LOG_CALL();
    bBusy = false; bStopped = bStop; StatusCode = Conflict ? EDemoCloudStatus::Conflict : EDemoCloudStatus::Failed; Status = Error;
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
            StatusCode = EDemoCloudStatus::Failed; Status = TEXT("云端保存超时；本地进度已保留，可重试或稍后补传");
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
void UDemoCloudSync::Request(const FString& Verb, const FString& Path, const FString& Body, EDemoCloudStep CurrentStep)
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
    StatusCode = EDemoCloudStatus::Connecting; Status = TEXT("正在连接云存档");
    Request(TEXT("POST"), TEXT("/v1/auth/session"), DemoCloudProtocol::Encode(Body), EDemoCloudStep::Login);
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
        Request(TEXT("POST"), TEXT("/v1/me/profile-bindings"), DemoCloudProtocol::Encode(Body), EDemoCloudStep::Bind);
        return;
    }
    Pull();
}
void UDemoCloudSync::Pull() { DEMO_LOG_CALL(); Request(TEXT("GET"), TEXT("/v1/me/profile"), FString(), EDemoCloudStep::Pull); }
bool UDemoCloudSync::CanImport() const
{
    DEMO_LOG_CALL();
    const ADemoGameState* Game = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 当前世界借用，跨地图不缓存。
    return Game && Game->Phase == EDemoPhase::Lobby;
}
bool UDemoCloudSync::MergeSlots(const TSharedPtr<FJsonObject>& Remote, int32 Resolution)
{
    DEMO_LOG_CALL();
    TArray<FDemoCloudSlot> Slots; // Codec先验证所有槽字段，任何文件写入前拒绝重复/缺字段/非法版本。
    if (!DemoCloudProtocol::ReadSlots(Remote,Slots)) { Fail(TEXT("云端检查点列表格式无效"),true); return false; }
    for (const FDemoCloudSlot& Slot:Slots) // 第一遍只检查冲突和导入许可，不写本地文件。
    {
        if (Slot.Revision==State->SlotRevisions[Slot.Index]&&Runs->GetSlot(Slot.Index))continue;
        const FString LocalHash=DemoCloudProtocol::Fingerprint(Runs->ExportCloud(Slot.Index)); // 空槽不构成本地冲突。
        if (Resolution==0&&!LocalHash.IsEmpty()&&LocalHash!=State->SlotHashes[Slot.Index])
        { Conflict=Remote; Fail(TEXT("检查点冲突：回大厅后选择保留本机或使用云端"),true); return false; }
        if(Resolution!=1&&!CanImport()){ bNeedPull=true; Fail(TEXT("云端有新检查点，回大厅后下载"));return false; }
    }
    for(const FDemoCloudSlot& Slot:Slots) // 每槽提交后保留CAS水位，部分磁盘失败仍可幂等重入。
    {
        if(Slot.Revision==State->SlotRevisions[Slot.Index]&&Runs->GetSlot(Slot.Index))continue;
        if(Resolution!=1)
        {
            if(!Runs->ImportCloud(Slot.Index,Slot.Snapshot)){ Fail(TEXT("检查点下载写盘失败，保留云端数据"),true);return false; }
            State->SlotHashes[Slot.Index]=DemoCloudProtocol::Fingerprint(Runs->ExportCloud(Slot.Index));
        }
        State->SlotRevisions[Slot.Index]=Slot.Revision;
        if(!Persist())return false;
    }
    return true;
}
void UDemoCloudSync::Upload()
{
    DEMO_LOG_CALL();
    if (!Profile->CanSync()) { Fail(TEXT("本地进度尚未落盘"), true); return; }
    const FDemoPlayerProfileData& Data = Profile->GetData(); // 游戏线程只读借用，序列化完成即释放引用。
    TArray<FDemoCloudSlot> Slots; // 同步冻结变更槽，编解码器只处理值，不读取GI或文件。
    if(Data.Clears.Num()<=64)for(int32 Index=0;Index<3;++Index) // 最后一批通关才附带依赖解锁的检查点。
    {
        const TSharedPtr<FJsonObject> Snapshot=Runs->ExportCloud(Index); // Save Codec提供稳定白名单。
        if(!Snapshot && Runs->GetSlot(Index)) { Fail(TEXT("本地检查点无法编码，尚未上传；请保留本地存档并检查版本"),true); return; } // 有效本地槽不能因编码失败伪装成空槽/已同步。
        if(!Snapshot||DemoCloudProtocol::Fingerprint(Snapshot)==State->SlotHashes[Index])continue;
        FDemoCloudSlot Slot; // 此次请求独占的CAS写操作。
        Slot.Index=Index; Slot.Revision=State->SlotRevisions[Index]; Slot.Snapshot=Snapshot; Slots.Add(MoveTemp(Slot));
    }
    if (Data.Clears.IsEmpty() && Slots.IsEmpty() && Data.LocalRevision <= Data.SyncedRevision && Data.PreferenceRevision <= Data.SyncedPreferenceRevision)
    {
        CheckedRunSerial = Runs->GetChangeSerial(); StatusCode = EDemoCloudStatus::Synchronized; Status = TEXT("永久进度与战役检查点已同步"); NextAttempt = FPlatformTime::Seconds() + 2;
        // 旧回执不是退出条件：必须再次比较最新三槽指纹、永久修订与分批通关队列。
        if (ExitState == EDemoCloudExitState::Pending && State->Outbox.IsEmpty())
        {
            ExitState = EDemoCloudExitState::Complete;
            // 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
            UE_LOG(LogFPSDemo, Display, TEXT("EXIT_SYNC complete: latest local snapshots confirmed"));
        }
        return;
    }
    const FString RequestId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); // 仅创建新耐久请求时分配，重放绝不替换。
    State->Outbox = DemoCloudProtocol::Encode(DemoCloudProtocol::BuildSync(Data,State->BindingId,RequestId,Slots));
    State->OutboxProfileId = Data.ProfileId;
    if (Persist()) SendOutbox();
}
void UDemoCloudSync::SendOutbox() { DEMO_LOG_CALL(); StatusCode = EDemoCloudStatus::Pending; Status = TEXT("正在同步永久进度与检查点"); Request(TEXT("POST"), TEXT("/v1/me/profile/sync"), State->Outbox, EDemoCloudStep::Upload); }
void UDemoCloudSync::Reply(bool bSuccess, int32 Code, const FString& Body)
{
    DEMO_LOG_CALL();
    bBusy = false;
    if (!bSuccess || Code >= 500 || Code == 429) { Fail(TEXT("云服务暂不可用，进度保留本地并自动重试")); return; }
    TSharedPtr<FJsonObject> Json = DemoCloudProtocol::Decode(Body); // 不将HTTP正文写日志，认证响应可能包含Bearer。
    if (!Json) { Fail(TEXT("服务器响应格式无效"), true); return; }
    FString Error; // 稳定协议错误码，不含异常堆栈/密码。
    Json->TryGetStringField(TEXT("code"), Error);
    if (Code == 401) { Token.Empty(); Fail(TEXT("云账号会话无效，正在重新认证"), Step == EDemoCloudStep::Login); return; }
    if (Code == 409 && Step == EDemoCloudStep::Upload && (Error == TEXT("revision_conflict") || Error == TEXT("binding_stale") || Error == TEXT("slot_conflict")))
    {
        // 明确拒绝表示事务未提交，此时才可丢弃旧请求ID并重新拉取；网络超时绝不能走这里。
        State->Outbox.Empty(); State->OutboxProfileId.Empty();
        if (Error == TEXT("binding_stale")) { State->BindingId.Empty(); State->Epoch.Empty(); }
        bNeedPull = true;
        if (Persist()) Prepare();
        return;
    }
    if (Code < 200 || Code >= 300)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("Cloud protocol rejection HTTP=%d code=%s"), Code, *Error);
        const bool bCoopUpload=Step==EDemoCloudStep::Upload && State->Outbox.Contains(TEXT("\"teamMembers\"")); // 只识别当前耐久请求的扩展标识，不打印正文或重新生成请求ID。
        const bool bOwnershipUpload=Step==EDemoCloudStep::Upload && State->Outbox.Contains(TEXT("\"hub_weapon_ownership_v1\"")); // 不改不可变Outbox，只识别已持久协议能力。
        const bool bLegacyConflict=Error==TEXT("legacy_migration_conflict")||Error==TEXT("legacy_migration_unproven"); // 不自动以远端旧权覆盖本地已购槽。
        Fail(bLegacyConflict?TEXT("旧武器权迁移存在冲突或缺少证明；本地进度与待同步材料已保留，停止自动上传")
            :bOwnershipUpload?TEXT("枪权检查点尚未同步：云服务需支持HTTP3拥有权合同；本地购买已保留，请升级或修复服务后重试")
            :bCoopUpload?TEXT("合作检查点尚未同步：云服务未支持该数据或校验失败；本地存档和待上传队列已保留，请升级云服务后重试")
            :TEXT("云同步请求被拒绝，请查看协议错误日志"),true);
        if (bLegacyConflict) StatusCode=EDemoCloudStatus::Conflict; // 仅提示冲突，不开放丢弃本地Legacy的自动解决入口。
        return;
    }
    Attempts = 0;
    if (Step == EDemoCloudStep::Login)
    {
        FString Player; // 登录返回的服务器唯一身份，必须与既有绑定一致。
        if (!Json->TryGetStringField(TEXT("accessToken"), Token) || Token.Len() != 64 || !Json->TryGetStringField(TEXT("playerId"), Player)) { Fail(TEXT("认证响应不完整"), true); return; }
        if (!State->PlayerId.IsEmpty() && State->PlayerId != Player) { Token.Empty(); Fail(TEXT("账号与本地同步档案不一致，已阻止合并"), true); return; }
        State->PlayerId = Player;
        if (Persist()) Prepare();
        return;
    }
    if (Step == EDemoCloudStep::Bind)
    {
        if (!Json->TryGetStringField(TEXT("bindingId"), State->BindingId) || State->BindingId.IsEmpty()) { Fail(TEXT("绑定响应不完整"), true); return; }
        if (Persist()) Pull();
        return;
    }
    const TSharedPtr<FJsonObject>* Nested = nullptr; // 上传回执内的完整投影，下载直接使用根对象。
    TSharedPtr<FJsonObject> Remote = Json;
    if (Step == EDemoCloudStep::Upload) { if (!Json->TryGetObjectField(TEXT("profile"), Nested)) { Fail(TEXT("同步回执缺少档案"), true); return; } Remote = *Nested; }
    FString Owner; // 对端身份必须匹配当前认证账号，避免错误服务配置导入他人数据。
    if (!Remote->TryGetStringField(TEXT("serverProfileId"), Owner) || Owner != State->PlayerId) { Fail(TEXT("云端档案身份校验失败"), true); return; }
    if (Step == EDemoCloudStep::Pull)
    {
        if (!Remote->HasTypedField<EJson::Object>(TEXT("legacyMigration"))) { Fail(TEXT("云服务未提供HTTP3旧权迁移合同；本地购买已保留，请升级服务"),true); return; }
        if (!Profile->ApplyCloud(Remote, {}, 0)) { Fail(TEXT("云端永久进度无法保存到本地"), true); return; }
        if (!MergeSlots(Remote)) return;
        bNeedPull = false; NextPull = FPlatformTime::Seconds() + 60; Upload(); return;
    }
    TSharedPtr<FJsonObject> Sent = DemoCloudProtocol::Decode(State->Outbox); // 回执必须匹配持久化请求，不接受裸服务器修订。
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
            State->SlotHashes[Index] = DemoCloudProtocol::Fingerprint(Item->GetObjectField(TEXT("snapshot")));
        }
    }
    State->Outbox.Empty(); State->OutboxProfileId.Empty();
    if (!Persist()) return;
    bNeedPull = true; StatusCode = EDemoCloudStatus::Pending; Status = TEXT("云存档已确认保存"); NextAttempt = FPlatformTime::Seconds() + 2;
    // 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
    UE_LOG(LogFPSDemo, Display, TEXT("CLOUD_ACK localRevision=%d"), Revision);
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
    if (!FFileHelper::SaveStringToFile(DemoCloudProtocol::Encode(Archive), *Path)) { Status = TEXT("冲突备份写入失败，未覆盖任何检查点"); return; }
    if (!MergeSlots(Conflict, bKeepLocal ? 1 : 2)) return;
    Conflict.Reset(); bStopped = false; bNeedPull = true; NextAttempt = 0;
    StatusCode = EDemoCloudStatus::Pending;
    Status = bKeepLocal ? TEXT("已保留本机检查点，等待上传") : TEXT("已采用云端检查点");
}
