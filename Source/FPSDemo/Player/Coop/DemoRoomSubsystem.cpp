// 职责：合作房间发现与版本筛选。使用：由本地房间界面调用，协议版本必须与加入参数和服务器门禁一致。
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Player/Coop/DemoCoopProtocol.h"
#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "Player/DemoPlayerController.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Save/DemoRunSave.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "GameFramework/PlayerState.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoNetworkDiagnostics.h" // 仅Editor记录原有会话操作边界，不为诊断调用ResolveSessions创建OSS。

namespace
{
    const FName ProtocolKey(TEXT("DEMO_PROTOCOL")); // 广播协议标识，不与OSS内置字段重名。
    const FString Protocol = FString(DemoCoopProtocol::RoomPrefix) + DemoCoopProtocol::Version; // 广播与筛选从公共版本派生，必须与登录URL和PreLogin完全一致。
    const FName RoomKey(TEXT("DEMO_ROOM")); // 房间展示名，不用于路由。
    const FName OpenKey(TEXT("DEMO_OPEN")); // 1表示安全区可加入，0表示暂时关闭。
    const FName CountKey(TEXT("DEMO_COUNT")); // 权威在线人数，搜索显示值不参与服务器校验。
    const FName RegionKey(TEXT("DEMO_REGION")); // 配置地区用于目录筛选，不代表实测延迟。
    const FName LevelKey(TEXT("DEMO_LEVEL")); // 关卡展示，不参与服务器结算。
    const FName DifficultyKey(TEXT("DEMO_DIFFICULTY")); // 难度枚举的整数展示投影。
    const FName BucketKey(TEXT("OSSEOS_BUCKET_ID_ATTRIBUTE_KEY")); // UE5.4 EOS创建与搜索使用同一Bucket。
}

void UDemoRoomSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Super::Initialize(Collection);
    Collection.InitializeDependency<UDemoCoopSessionSubsystem>();
    Status = TEXT("搜索同一局域网内的合作房间");
    NetworkHandle = GEngine->OnNetworkFailure().AddUObject(this, &UDemoRoomSubsystem::OnNetworkFailure);
    TravelHandle = GEngine->OnTravelFailure().AddUObject(this, &UDemoRoomSubsystem::OnTravelFailure);
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDemoRoomSubsystem::TickService), .5f);
    DEMO_NET_DIAG(TEXT("Room.Initialized"), GetWorld(), FString::Printf(TEXT("subsystem=%u tickerInstalled=1"), GetUniqueID())); // 实例编号在早期World尚未就绪时也可关联。
}
void UDemoRoomSubsystem::Deinitialize()
{
    DEMO_LOG_CALL();
    DEMO_NET_DIAG(TEXT("Room.Deinitialize.Begin"), GetWorld(), FString::Printf(TEXT("subsystem=%u roomState=%d sessionsValid=%d searchValid=%d"), GetUniqueID(), static_cast<int32>(State), Sessions.IsValid(), SearchRequest.IsValid()));
    bStopping = true; // 先阻止迟到回调；以下日志不访问Identity或其他可能已销毁的GI子系统。
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if (GEngine) { GEngine->OnNetworkFailure().Remove(NetworkHandle); GEngine->OnTravelFailure().Remove(TravelHandle); }
    ClearOperationDelegates();
    DEMO_NET_DIAG(TEXT("Room.Callbacks.Removed"), GetWorld(), TEXT("ticker=removed operationDelegates=removed"));
    if (Sessions.IsValid())
    {
        Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
        Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle);
        DEMO_NET_DIAG(TEXT("Room.CancelFind.Begin"), GetWorld(), TEXT(""));
        [[maybe_unused]] const bool bCancelAccepted = Sessions->CancelFindSessions(); // 原调用仅执行一次；返回值仅用于Editor证据，不把请求接受视为异步完成。
        DEMO_NET_DIAG(TEXT("Room.CancelFind.Returned"), GetWorld(), FString::Printf(TEXT("accepted=%d"), bCancelAccepted));
        if (Sessions->GetNamedSession(NAME_GameSession))
        {
            DEMO_NET_DIAG(TEXT("Room.DestroySession.Begin"), GetWorld(), TEXT("source=deinitialize"));
            [[maybe_unused]] const bool bDestroyAccepted = Sessions->DestroySession(NAME_GameSession); // 保持原来的单次销毁请求，不重试、不等待回调。
            DEMO_NET_DIAG(TEXT("Room.DestroySession.Returned"), GetWorld(), FString::Printf(TEXT("source=deinitialize accepted=%d"), bDestroyAccepted));
        }
        else DEMO_NET_DIAG(TEXT("Room.DestroySession.Skipped"), GetWorld(), TEXT("reason=noNamedSession"));
    }
    else DEMO_NET_DIAG(TEXT("Room.NetworkCleanup.Skipped"), GetWorld(), TEXT("reason=noSessionInterface"));
    DEMO_NET_DIAG(TEXT("Room.Interfaces.Reset.Begin"), GetWorld(), TEXT(""));
    Sessions.Reset(); SearchRequest.Reset(); Super::Deinitialize();
    DEMO_NET_DIAG(TEXT("Room.Deinitialize.End"), nullptr, FString::Printf(TEXT("subsystem=%u sessionsReset=1 searchReset=1"), GetUniqueID())); // Super结束后只取仍在执行的本对象编号，不再访问GI/World。
}
bool UDemoRoomSubsystem::ResolveSessions()
{
    DEMO_LOG_CALL();
    DEMO_NET_DIAG(TEXT("Room.ResolveSessions.Begin"), GetWorld(), FString::Printf(TEXT("quarantined=%d"), bSessionQuarantined)); // 仅观察原业务显式获取接口的时机，诊断自身不会走此入口。
    if (bSessionQuarantined) { Status = TEXT("上次在线会话清理失败，请重启游戏后重试"); return false; }
    UDemoOnlineIdentity* Identity = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>(); // 明确提供者并阻止编辑器创建EOS。
    Sessions = Identity->GetSessions();
    DEMO_NET_DIAG(TEXT("Room.ResolveSessions.End"), GetWorld(), FString::Printf(TEXT("sessionsValid=%d"), Sessions.IsValid()));
    if (!Sessions) { Status = IsPublic() ? Identity->GetStatus() : TEXT("局域网服务不可用，请检查NULL配置"); UE_LOG(LogFPSDemo, Warning, TEXT("ROOM provider unavailable")); }
    return Sessions.IsValid();
}
bool UDemoRoomSubsystem::IsPublic() const { DEMO_LOG_TICK(); return GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->IsPublic(); }
bool UDemoRoomSubsystem::IsReady() const { DEMO_LOG_TICK(); return !bSessionQuarantined && GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->IsReady(); }
bool UDemoRoomSubsystem::SelectNetwork(EDemoRoomNetwork Network)
{
    DEMO_LOG_CALL();
    if (bStopping || IsBusy() || bSessionQuarantined || (Sessions && Sessions->GetNamedSession(NAME_GameSession)))
    { UE_LOG(LogFPSDemo, Log, TEXT("ROOM provider switch rejected busy/session")); return false; }
    if (!GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->SelectNetwork(Network)) return false;
    ClearOperationDelegates(); Sessions.Reset(); Rooms.Reset(); Results.Reset(); ++Revision;
    bSearchAfterLogin = false; bQuickJoin = false; Status = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->GetStatus();
    if (IsReady()) Search(); return true;
}
void UDemoRoomSubsystem::LoginOrLogout()
{
    DEMO_LOG_CALL();
    const ADemoGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 账号操作只能来自离线大厅。
    if (!IsPublic() || IsBusy() || bSessionQuarantined || !GameState || GameState->Phase != EDemoPhase::Lobby || GetWorld()->GetNetMode() != NM_Standalone || (Sessions && Sessions->GetNamedSession(NAME_GameSession))) return;
    UDemoOnlineIdentity* Identity = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>(); // 浏览器与登录委托均由身份服务拥有。
    Rooms.Reset(); Results.Reset(); ++Revision; bSearchAfterLogin = false;
    if (Identity->IsReady()) Identity->Logout(); else bSearchAfterLogin = Identity->Login();
    Status = Identity->GetStatus();
}
bool UDemoRoomSubsystem::QuickJoin()
{
    DEMO_LOG_CALL(); if (!Search()) return false;
    bQuickJoin = true; return true;
}
void UDemoRoomSubsystem::RegisterMember(APlayerController* Player)
{
    DEMO_LOG_CALL();
    if (!IsPublic() || State != EDemoRoomState::Hosting || !Sessions || !Sessions->GetNamedSession(NAME_GameSession) || !Player || !Player->HasAuthority() || !Player->PlayerState) return; // 不与Create/Destroy并发修改SDK成员。
    const FUniqueNetIdRepl Id = Player->PlayerState->GetUniqueId(); // 服务器PC通过传输校验的身份，不接收UI自报ID。
    if (Id.IsValid() && !Sessions->RegisterPlayer(NAME_GameSession, *Id, false)) UE_LOG(LogFPSDemo, Warning, TEXT("EOS session member registration rejected"));
}
void UDemoRoomSubsystem::UnregisterMember(APlayerController* Player)
{
    DEMO_LOG_CALL();
    if (!IsPublic() || State != EDemoRoomState::Hosting || !Sessions || !Sessions->GetNamedSession(NAME_GameSession) || !Player || !Player->HasAuthority() || !Player->PlayerState) return; // 退出房间由Destroy整体清理，不在创建/销毁期间另发注销。
    const FUniqueNetIdRepl Id = Player->PlayerState->GetUniqueId(); // PS销毁前注销目录成员，不删除固定队伍检查点。
    if (Id.IsValid() && !Sessions->UnregisterPlayer(NAME_GameSession, *Id)) UE_LOG(LogFPSDemo, Warning, TEXT("EOS session member unregistration rejected"));
}
void UDemoRoomSubsystem::ClearOperationDelegates()
{
    DEMO_LOG_CALL();
    if (!Sessions.IsValid()) return;
    Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle); FindHandle.Reset();
    Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle); CreateHandle.Reset();
    Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle); JoinHandle.Reset();
}
const TArray<FDemoRoomView>& UDemoRoomSubsystem::GetRooms() const { DEMO_LOG_TICK(); return Rooms; }
uint32 UDemoRoomSubsystem::GetRevision() const { DEMO_LOG_TICK(); return Revision; }
EDemoRoomState UDemoRoomSubsystem::GetState() const { DEMO_LOG_TICK(); return State; }
const FString& UDemoRoomSubsystem::GetStatus() const
{
    DEMO_LOG_TICK();
    const UDemoOnlineIdentity* Identity = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>(); // 未登录时显示真实授权/配置状态。
    return IsPublic() && !Identity->IsReady() ? Identity->GetStatus() : Status;
}
bool UDemoRoomSubsystem::IsBusy() const
{
    DEMO_LOG_TICK();
    return (IsPublic() && GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->IsBusy()) || State == EDemoRoomState::Searching || State == EDemoRoomState::OpeningHost || State == EDemoRoomState::Creating
        || State == EDemoRoomState::Joining || State == EDemoRoomState::Travelling || State == EDemoRoomState::Closing;
}
bool UDemoRoomSubsystem::ConsumeBrowserReturn()
{
    DEMO_LOG_TICK(); // 本地MenuFlow逐帧查询，使用高频等级，保留源码埋点而不刷普通调用日志。
    const ADemoGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 本地大厅真正就绪后才消费失败恢复意图。
    if (!bReturnBrowser || !GameState || GameState->Phase != EDemoPhase::Lobby || GetWorld()->GetNetMode() != NM_Standalone || IsBusy()) return false;
    bReturnBrowser = false; return true;
}
bool UDemoRoomSubsystem::Search()
{
    DEMO_LOG_CALL();
    const ADemoGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 仅主大厅可以浏览，局内不会自动断线。
    if (bStopping || IsBusy() || !GameState || GameState->Phase != EDemoPhase::Lobby || GetWorld()->GetNetMode() != NM_Standalone)
    { UE_LOG(LogFPSDemo, Log, TEXT("ROOM_SEARCH rejected state/world")); return false; }
    if (!ResolveSessions()) return false;
    if (Sessions->GetNamedSession(NAME_GameSession)) { Status = TEXT("旧连接正在清理，请稍后刷新"); CloseSession(); return false; }
    Rooms.Reset(); Results.Reset(); ++Revision; bDiscardPending = false; bQuickJoin = false;
    SearchRequest = MakeShared<FOnlineSessionSearch>();
    SearchRequest->bIsLanQuery = !IsPublic(); SearchRequest->MaxSearchResults = 64; SearchRequest->TimeoutInSeconds = IsPublic() ? 20.f : 8.f; // 公网查询目录，LAN查询广播。
    if (IsPublic())
    {
        SearchRequest->QuerySettings.Set(BucketKey, Protocol, EOnlineComparisonOp::Equals);
        SearchRequest->QuerySettings.Set(ProtocolKey, Protocol, EOnlineComparisonOp::Equals);
        SearchRequest->QuerySettings.Set(RegionKey, GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->GetRegion(), EOnlineComparisonOp::Equals);
    }
    FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &UDemoRoomSubsystem::OnFound));
    State = EDemoRoomState::Searching; Deadline = FPlatformTime::Seconds() + (IsPublic() ? 25. : 12.); Status = IsPublic() ? TEXT("正在搜索公网房间……") : TEXT("正在搜索局域网房间……");
    DEMO_NET_DIAG(TEXT("Room.FindSessions.Begin"), GetWorld(), FString::Printf(TEXT("lan=%d"), SearchRequest->bIsLanQuery)); // 不输出查询区域/房间名/地址。
    const bool bFindAccepted = Sessions->FindSessions(0, SearchRequest.ToSharedRef()); // 原单次请求返回值，记录后仍执行原来的失败处理。
    DEMO_NET_DIAG(TEXT("Room.FindSessions.Returned"), GetWorld(), FString::Printf(TEXT("accepted=%d"), bFindAccepted));
    if (!bFindAccepted) { State = EDemoRoomState::Idle; Fail(TEXT("搜索未能启动，请稍后刷新"), false); return false; }
    return true;
}
void UDemoRoomSubsystem::CancelSearch()
{
    DEMO_LOG_CALL();
    bSearchAfterLogin = false; bQuickJoin = false; // 关闭页面撤销授权后的自动搜索意图。
    if (State != EDemoRoomState::Searching) return;
    if (IsPublic())
    { bDiscardPending = true; Rooms.Reset(); Results.Reset(); ++Revision; Status = TEXT("已取消，等待在线查询结束……"); return; } // 不清空SDK共享CurrentSearch，等待原回调后再允许新查询。
    ClearOperationDelegates();
    if (Sessions.IsValid()) Sessions->CancelFindSessions();
    SearchRequest.Reset(); Rooms.Reset(); Results.Reset(); ++Revision;
    Deadline = 0; State = EDemoRoomState::Idle; Status = TEXT("搜索已取消");
}
void UDemoRoomSubsystem::OnFound(bool bSuccess)
{
    DEMO_LOG_CALL();
    if (bStopping || State != EDemoRoomState::Searching) return;
    ClearOperationDelegates(); Deadline = 0;
    if (bDiscardPending) { bDiscardPending = false; SearchRequest.Reset(); State = EDemoRoomState::Idle; return; } // 迟到结果不能更新已关闭页面或自动加入。
    if (!bSuccess || !SearchRequest) { State = EDemoRoomState::Idle; Fail(TEXT("搜索失败，请刷新重试"), false); return; }
    // 无捕获比较器，仅本次游戏线程StableSort同步执行；Left/Right借用查询数组元素，按毫秒延时排序，不跨回调保存引用。
    SearchRequest->SearchResults.StableSort([](const FOnlineSessionSearchResult& Left, const FOnlineSessionSearchResult& Right)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ROOM ping comparator"));
        return Left.PingInMs < Right.PingInMs;
    });
    // UE默认SortSearchResults为空；本服务显式排序并再次筛选协议，不能依赖NULL执行自定义查询过滤。
    for (const FOnlineSessionSearchResult& Found : SearchRequest->SearchResults)
    {
        FString FoundProtocol; // 只接受本项目明确标记的房间，不能加入同网段其他UE游戏。
        if (!Found.IsValid() || !Found.Session.SessionSettings.Get(ProtocolKey, FoundProtocol) || FoundProtocol != Protocol) continue;
        FDemoRoomView View; // 与Results同步添加，UI索引只在当前Revision中有效。
        int32 Open = 0; // 广告缺字段时安全默认不可加入。
        Found.Session.SessionSettings.Get(RoomKey, View.Name); View.Name = View.Name.Left(32);
        Found.Session.SessionSettings.Get(OpenKey, Open); Found.Session.SessionSettings.Get(CountKey, View.Players);
        if (View.Players < 1 || View.Players > 4 || View.Name.IsEmpty()) continue;
        View.PingMs = IsPublic() ? INDEX_NONE : FMath::Clamp(Found.PingInMs, 0, 9999); // EOS目录没有实测Ping。
        View.bJoinable = Open == 1 && View.Players < 4;
        View.Stage = Open == 1 ? TEXT("安全区") : TEXT("挑战中 · 等待返回安全区");
        if (IsPublic())
        {
            FString FoundRegion; // 再次过滤远端元数据，不能只依赖目录查询。
            int32 Level = 0, Difficulty = 0; // 显示字段不能改变本机战局状态。
            Found.Session.SessionSettings.Get(RegionKey, FoundRegion);
            if (FoundRegion != GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->GetRegion()) continue;
            Found.Session.SessionSettings.Get(LevelKey, Level); Found.Session.SessionSettings.Get(DifficultyKey, Difficulty);
            static const TCHAR* DifficultyNames[] = { TEXT("简单"), TEXT("普通"), TEXT("困难"), TEXT("地狱") }; // 与四种难度枚举对应。
            View.Stage += FString::Printf(TEXT(" · %s · 第%d关 · %s"), DifficultyNames[FMath::Clamp(Difficulty,0,3)], FMath::Clamp(Level,0,100000), *FoundRegion);
        }
        Rooms.Add(MoveTemp(View)); Results.Add(Found);
    }
    SearchRequest.Reset(); State = EDemoRoomState::Idle;
    Status = Rooms.IsEmpty() ? (IsPublic() ? TEXT("未找到同地区同版本房间，可以创建房间") : TEXT("未找到兼容房间；请确认房主已开房且位于同一局域网")) : TEXT("选择房间加入；固定队伍已满时仅原成员可重连");
    UE_LOG(LogFPSDemo, Display, TEXT("ROOM_SEARCH complete compatible=%d"), Rooms.Num());
    if (bQuickJoin)
    {
        bQuickJoin = false;
        for (int32 Index = 0; Index < Rooms.Num(); ++Index) // 首个开放兼容房间；固定队伍拒绝后由玩家选择其他房间，不偷偷循环连接。
            if (Rooms[Index].bJoinable) { Join(Index, Revision); return; }
        Status = TEXT("没有可快速加入的房间，请创建房间或稍后刷新");
    }
}
bool UDemoRoomSubsystem::Join(int32 Index, uint32 ExpectedRevision)
{
    DEMO_LOG_CALL();
    if (bStopping || !IsReady() || IsBusy() || GetWorld()->GetNetMode() != NM_Standalone || ExpectedRevision != Revision || !Rooms.IsValidIndex(Index)
        || !Results.IsValidIndex(Index) || !Rooms[Index].bJoinable || !Sessions.IsValid())
    { Status = TEXT("房间列表已变化或房间不可加入，请刷新"); UE_LOG(LogFPSDemo, Log, TEXT("ROOM_JOIN rejected stale/unavailable")); return false; }
    if (Sessions->GetNamedSession(NAME_GameSession)) { Status = TEXT("请等待旧房间退出完成"); CloseSession(); return false; }
    LobbyMap = UGameplayStatics::GetCurrentLevelName(this, true);
    JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &UDemoRoomSubsystem::OnJoined));
    State = EDemoRoomState::Joining; Deadline = FPlatformTime::Seconds() + 15.; Status = TEXT("正在加入房间……");
    if (!Sessions->JoinSession(0, NAME_GameSession, Results[Index])) { State = EDemoRoomState::Idle; Fail(TEXT("房间已不可用，请刷新后重试"), false); return false; }
    return true;
}
const FString& UDemoRoomSubsystem::GetDirectAddressDraft() const { DEMO_LOG_TICK(); return DirectAddressDraft; }
void UDemoRoomSubsystem::SetDirectAddressDraft(const FString& InAddress)
{
    DEMO_LOG_TICK();
    DirectAddressDraft = InAddress.Left(253); // 与底层受限直连最大长度一致，避免缓存超长文本。
}
bool UDemoRoomSubsystem::ValidateDirectAddress(const FString& RawAddress, FString& OutNormalized, FString& OutError)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    OutNormalized = RawAddress.TrimStartAndEnd();
    OutError.Empty();
    if (OutNormalized.IsEmpty())
    {
        OutError = TEXT("请输入房主的域名或 IP 地址（例如 play.example.com:7777）");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected empty address"));
        return false;
    }
    if (OutNormalized.Len() > 253)
    {
        OutError = TEXT("地址过长，请检查域名或 IP 输入");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected oversized address len=%d"), OutNormalized.Len());
        return false;
    }
    if (OutNormalized.StartsWith(TEXT("EOS:"), ESearchCase::IgnoreCase))
    {
        OutError = TEXT("公网 EOS 地址不能通过直连模式加入，请切换至公网页签");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected EOS scheme"));
        return false;
    }
    for (const TCHAR Character : OutNormalized) // 与DemoCoopSessionSubsystem::JoinCoop字符白名单一致，禁止协议头、路径、查询参数与空白。
    {
        if (Character > 127 || (!FChar::IsAlnum(Character) && Character != TEXT('.') && Character != TEXT('-') && Character != TEXT(':') && Character != TEXT('[') && Character != TEXT(']')))
        {
            OutError = TEXT("地址格式无效：请直接填写域名或 IP（可附带 :端口），不要包含 http://、路径或空格");
            UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected invalid character"));
            return false;
        }
    }
    // 校验端口字符串必须是1..65535纯ASCII数字，防止引擎静默忽略或解析越界端口。
    auto IsValidPort = [](const FString& PortText) -> bool
    {
        if (PortText.IsEmpty() || PortText.Len() > 5) return false;
        for (const TCHAR Digit : PortText) if (Digit < TEXT('0') || Digit > TEXT('9')) return false;
        const int32 PortValue = FCString::Atoi(*PortText);
        return PortValue >= 1 && PortValue <= 65535;
    };
    if (OutNormalized.StartsWith(TEXT("[")))
    {
        int32 CloseBracket = INDEX_NONE;
        if (!OutNormalized.FindChar(TEXT(']'), CloseBracket) || CloseBracket <= 1)
        {
            OutError = TEXT("IPv6 地址格式无效，示例：[2001:db8::1]:7777");
            UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected malformed ipv6 bracket"));
            return false;
        }
        const FString HostPart = OutNormalized.Mid(1, CloseBracket - 1);
        for (const TCHAR Ch : HostPart)
            if (!FChar::IsHexDigit(Ch) && Ch != TEXT(':') && Ch != TEXT('.'))
            {
                OutError = TEXT("IPv6 地址包含非法字符");
                UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected invalid ipv6 host char"));
                return false;
            }
        const FString Tail = OutNormalized.Mid(CloseBracket + 1);
        if (!Tail.IsEmpty() && (!Tail.StartsWith(TEXT(":")) || !IsValidPort(Tail.Mid(1))))
        {
            OutError = TEXT("端口无效：请使用 1～65535 之间的数字（留空默认 7777）");
            UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected invalid ipv6 port"));
            return false;
        }
        return true;
    }
    if (OutNormalized.Contains(TEXT("[")) || OutNormalized.Contains(TEXT("]")))
    {
        OutError = TEXT("地址格式无效：括号仅用于 IPv6 地址");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected stray bracket"));
        return false;
    }
    int32 ColonCount = 0;
    int32 FirstColon = INDEX_NONE;
    for (int32 Index = 0; Index < OutNormalized.Len(); ++Index)
        if (OutNormalized[Index] == TEXT(':')) { if (FirstColon == INDEX_NONE) FirstColon = Index; ++ColonCount; }
    if (ColonCount >= 2)
    {
        for (const TCHAR Ch : OutNormalized) // 裸IPv6字面量不含端口；只允许十六进制数字、冒号和点。
            if (!FChar::IsHexDigit(Ch) && Ch != TEXT(':') && Ch != TEXT('.'))
            {
                OutError = TEXT("IPv6 地址格式无效，如附带端口请使用 [IPv6]:端口 格式");
                UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected invalid bare ipv6"));
                return false;
            }
        return true;
    }
    const FString HostPart = ColonCount == 1 ? OutNormalized.Left(FirstColon) : OutNormalized;
    if (HostPart.IsEmpty() || HostPart.StartsWith(TEXT(".")) || HostPart.EndsWith(TEXT(".")) || HostPart.StartsWith(TEXT("-")) || HostPart.EndsWith(TEXT("-")) || HostPart.Contains(TEXT("..")))
    {
        OutError = TEXT("请输入有效的主机域名或 IP 地址（例如 play.example.com:7777）");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected malformed host"));
        return false;
    }
    if (ColonCount == 1 && !IsValidPort(OutNormalized.Mid(FirstColon + 1)))
    {
        OutError = TEXT("端口无效：请使用 1～65535 之间的数字（留空默认 7777）");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected invalid port"));
        return false;
    }
    return true;
}
bool UDemoRoomSubsystem::JoinDirect(const FString& RawAddress, FString& OutError)
{
    DEMO_LOG_CALL();
    OutError.Empty();
    const UWorld* World = GetWorld(); // 仅在本地单机大厅允许发起直连，局内或重入一律拒绝。
    const ADemoGameState* GameState = World ? World->GetGameState<ADemoGameState>() : nullptr;
    if (bStopping || !World || World->GetNetMode() != NM_Standalone || !GameState || GameState->Phase != EDemoPhase::Lobby)
    {
        OutError = TEXT("当前不在本地大厅，无法发起直连");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected non-lobby world"));
        return false;
    }
    if (bSessionQuarantined)
    {
        OutError = TEXT("上次在线会话清理失败，请重启游戏后重试");
        Status = OutError;
        UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_DIRECT rejected quarantined session"));
        return false;
    }
    if (IsPublic())
    {
        OutError = TEXT("域名/IP 直连仅用于局域网/直连模式，请先切换到左侧「局域网 / 直连」页签");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected in public EOS mode"));
        return false;
    }
    if (IsBusy() && State != EDemoRoomState::Searching)
    {
        OutError = TEXT("当前正在处理其他连接操作，请稍候或先取消连接");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT rejected busy state=%d"), static_cast<int32>(State));
        return false;
    }
    FString NormalizedAddress;
    if (!ValidateDirectAddress(RawAddress, NormalizedAddress, OutError)) return false;
    DirectAddressDraft = NormalizedAddress;
    // 刚进入房间页时会自动搜索LAN；直连发起前先同步取消LAN搜索，避免用户空等搜索超时。
    if (State == EDemoRoomState::Searching) CancelSearch();
    if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
    {
        OutError = TEXT("请等待旧房间清理完成后再直连");
        Status = OutError;
        CloseSession();
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT deferred for existing named session"));
        return false;
    }
    LobbyMap = UGameplayStatics::GetCurrentLevelName(this, true);
    State = EDemoRoomState::Travelling;
    Deadline = FPlatformTime::Seconds() + 40.;
    Status = TEXT("正在直连目标主机并恢复队伍数据……");
    if (!GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->JoinCoop(NormalizedAddress))
    {
        Deadline = 0;
        State = EDemoRoomState::Idle;
        OutError = TEXT("无法发起直连，请检查域名/IP 格式和网络状态");
        Status = OutError;
        UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_DIRECT JoinCoop rejected"));
        return false;
    }
    UE_LOG(LogFPSDemo, Display, TEXT("ROOM_DIRECT travel requested"));
    return true;
}
void UDemoRoomSubsystem::OnJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    DEMO_LOG_CALL();
    if (bStopping || SessionName != NAME_GameSession || State != EDemoRoomState::Joining) return;
    ClearOperationDelegates();
    if (bDiscardPending) { bDiscardPending = false; State = EDemoRoomState::Idle; CloseSession(); return; } // 取消后的Join成功只销毁会话，不旅行。
    State = EDemoRoomState::Idle; // 原SDK操作已经完成，后续失败可以安全销毁会话。
    FString Address; // 仅采用OSS解析地址，不解析房间显示名作为命令。
    if (Result != EOnJoinSessionCompleteResult::Success || !Sessions->GetResolvedConnectString(SessionName, Address))
    { Fail(TEXT("加入失败：房间已满、已关闭或地址不可用"), false); return; }
    State = EDemoRoomState::Travelling; Deadline = FPlatformTime::Seconds() + 40.; Status = TEXT("正在连接房主并恢复队伍数据……");
    if (!GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->JoinCoop(Address)) Fail(TEXT("无法连接此房间"), true);
}
bool UDemoRoomSubsystem::HostSlot(int32 Slot, bool bCreate)
{
    DEMO_LOG_CALL();
    const ADemoGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 不能在连接中的游戏覆盖本机恢复意图。
    if (bStopping || IsBusy() || !GameState || GameState->Phase != EDemoPhase::Lobby || GetWorld()->GetNetMode() != NM_Standalone || Slot < 0 || Slot > 2 || !ResolveSessions()) return false;
    if (Sessions->GetNamedSession(NAME_GameSession)) { Status = TEXT("请等待旧房间清理完成"); CloseSession(); return false; }
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 新建/选槽仍使用同一存档服务，绝不覆盖已有槽。
    if (bCreate && !Saves->CreateSlot(Slot)) { Status = Saves->GetStatus(); return false; }
    const UDemoRunSave* Data = Saves->GetSlot(Slot); // 只从安全区开房，关间检查点请先以单人继续后返回安全区。
    if (!Data || Data->Phase != EDemoPhase::Hub) { Status = TEXT("请选安全区存档；关间存档需先单人继续并返回安全区"); return false; }
    if (!IsPublic() && !Data->OnlineOwnerKey.IsEmpty()) { Status = TEXT("该存档已绑定在线队伍，请选择公网开房；仍可单人继续"); return false; } // 避免把PUID队员当作可信LAN GUID接受。
    if (IsPublic() && !Saves->BindOnlineOwner(Slot, GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->GetOnlinePlayerKey()))
    { Status = Saves->GetStatus(); return false; } // 在旅行前落盘身份绑定，换账号不能占用同一固定队伍。
    if (!Saves->SelectSlot(Slot)) { Status = Saves->GetStatus(); return false; }
    LobbyMap = UGameplayStatics::GetCurrentLevelName(this, true);
    State = EDemoRoomState::OpeningHost; Deadline = FPlatformTime::Seconds() + 40.; Status = TEXT("正在恢复安全区并创建房间……");
    UGameplayStatics::OpenLevel(this, FName(*LobbyMap), true, TEXT("listen?ResumeRun=1")); return true;
}
bool UDemoRoomSubsystem::HostCurrentHub()
{
    DEMO_LOG_CALL();
    const ADemoGameState* CurrentState = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 在绑定存档前先拒绝战斗/客户端开房意图，避免失败操作改变身份元数据。
    if (!CurrentState || CurrentState->Phase != EDemoPhase::Hub || GetWorld()->GetNetMode() != NM_Standalone)
    { Status = TEXT("请先进入单人安全区再开房"); UE_LOG(LogFPSDemo, Log, TEXT("ROOM_HOST rejected non-standalone hub")); return false; }
    if (bStopping || IsBusy() || !ResolveSessions() || Sessions->GetNamedSession(NAME_GameSession)) return false;
    const UDemoRunSaves* CurrentSaves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 控制台入口同样禁止在线队伍降级LAN身份。
    const UDemoRunSave* CurrentSlot = CurrentSaves->GetSlot(CurrentSaves->GetActiveSlot()); // 仅同步借用槽快照。
    if (!IsPublic() && CurrentSlot && !CurrentSlot->OnlineOwnerKey.IsEmpty()) { Status = TEXT("在线队伍存档请从公网入口开房"); return false; }
    if (IsPublic())
    {
        UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 安全区控制台开房同样执行绑定，不能绕过UI门禁。
        if (!Saves->BindOnlineOwner(Saves->GetActiveSlot(), GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->GetOnlinePlayerKey())) { Status = Saves->GetStatus(); return false; }
    }
    LobbyMap = UGameplayStatics::GetCurrentLevelName(this, true);
    State = EDemoRoomState::OpeningHost; Deadline = FPlatformTime::Seconds() + 40.; Status = TEXT("正在准备合作房间……"); // 身份服务决定LAN/EOS传输。
    if (!GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->HostCoop()) { Fail(TEXT("请先进入单人安全区再开房"), false); return false; }
    return true;
}
void UDemoRoomSubsystem::OnCreated(FName SessionName, bool bSuccess)
{
    DEMO_LOG_CALL();
    if (bStopping || SessionName != NAME_GameSession || State != EDemoRoomState::Creating) return;
    ClearOperationDelegates();
    State = EDemoRoomState::Idle; // 收到最终结果后才可以销毁；不能和Create重叠。
    if (bDiscardPending) { bDiscardPending = false; CloseSession(); return; }
    if (!bSuccess) { Fail(TEXT("创建房间失败，请检查在线服务和产品权限后重试"), true); return; }
    Deadline = 0; State = EDemoRoomState::Hosting; Status = TEXT("房间已公开 · 最多4人 · 仅安全区允许加入");
    AdvertisedPhase = AdvertisedPlayers = AdvertisedLevel = AdvertisedDifficulty = INDEX_NONE;
    RegisterMember(GetGameInstance()->GetFirstLocalPlayerController()); UpdateAdvertisement();
    UE_LOG(LogFPSDemo, Display, TEXT("ROOM_HOST advertised public=%d"), IsPublic());
}
void UDemoRoomSubsystem::UpdateAdvertisement()
{
    DEMO_LOG_TICK();
    const ADemoGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 房主World权威公开状态。
    AFPSDemoGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSDemoGameMode>() : nullptr; // UE5.4的GetNumPlayers非const；只查询在线人数，不修改战局。
    FNamedOnlineSession* Session = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr; // OSS拥有，仅本次同步借用。
    if (!GameState || !Mode || !Session || bUpdating || FPlatformTime::Seconds() < NextAdvertiseAt) return; // EOS更新必须等待最终回调，失败退避。
    const int32 Phase = Mode->GetRunFlow()->CanAcceptConnections() ? static_cast<int32>(GameState->Phase) : -1; // 把初始化/旅行窗口也纳入广告变化，旧Hub不等于可登录。
    const int32 Players = FMath::Clamp(Mode->GetNumPlayers(), 1, 4); // PreLogin还有独立上限校验。
    if (Phase == AdvertisedPhase && Players == AdvertisedPlayers && GameState->LevelNumber == AdvertisedLevel && static_cast<int32>(GameState->Difficulty) == AdvertisedDifficulty) return;
    FOnlineSessionSettings Settings = Session->SessionSettings; // 完整保留原生OSS配置和版本键。
    Settings.Set(OpenKey, Phase == static_cast<int32>(EDemoPhase::Hub) ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing); // 显示准入与服务器门禁一致。
    Settings.Set(CountKey, Players, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(LevelKey, GameState->LevelNumber, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    Settings.Set(DifficultyKey, static_cast<int32>(GameState->Difficulty), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
    PendingPhase = Phase; PendingPlayers = Players; PendingLevel = GameState->LevelNumber; PendingDifficulty = static_cast<int32>(GameState->Difficulty);
    bUpdating = true; UpdateDeadline = FPlatformTime::Seconds() + 15.;
    UpdateHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(FOnUpdateSessionCompleteDelegate::CreateUObject(this, &UDemoRoomSubsystem::OnUpdated));
    if (!Sessions->UpdateSession(NAME_GameSession, Settings)) OnUpdated(NAME_GameSession, false);
}
void UDemoRoomSubsystem::OnUpdated(FName SessionName, bool bSuccess)
{
    DEMO_LOG_CALL();
    if (bStopping || SessionName != NAME_GameSession || !bUpdating || !Sessions) return;
    Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle); UpdateHandle.Reset();
    bUpdating = false; UpdateDeadline = 0; NextAdvertiseAt = FPlatformTime::Seconds() + (bSuccess ? 3. : 10.);
    if (bCloseAfterUpdate)
    { bCloseAfterUpdate = false; bSessionQuarantined = false; State = EDemoRoomState::Idle; CloseSession(); return; } // 最终更新回调已消费，才可销毁或解除超时隔离。
    if (bSuccess) { AdvertisedPhase = PendingPhase; AdvertisedPlayers = PendingPlayers; AdvertisedLevel = PendingLevel; AdvertisedDifficulty = PendingDifficulty; }
    else UE_LOG(LogFPSDemo, Warning, TEXT("ROOM directory update failed; retry delayed"));
}
bool UDemoRoomSubsystem::TickService(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (bStopping) return false;
    if (bSearchAfterLogin && !GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->IsBusy())
    { bSearchAfterLogin = false; if (IsReady()) Search(); } // 只有本次尚未取消的登录意图可以继续搜索。
    if (IsPublic() && !IsReady() && (State == EDemoRoomState::Hosting || State == EDemoRoomState::Connected || State == EDemoRoomState::Travelling))
    { Fail(TEXT("在线身份已失效，请重新登录"), true); return true; }
    if (bUpdating && UpdateDeadline > 0 && FPlatformTime::Seconds() >= UpdateDeadline)
    { UpdateDeadline = 0; UE_LOG(LogFPSDemo, Warning, TEXT("ROOM directory update timed out; waiting for final callback")); } // 不叠加新更新，服务器门禁仍立即生效。
    if (Deadline > 0 && FPlatformTime::Seconds() >= Deadline)
    {
        if (IsPublic() && (State == EDemoRoomState::Searching || State == EDemoRoomState::Creating || State == EDemoRoomState::Joining))
        {
            bDiscardPending = true; bQuickJoin = false; Deadline = 0;
            Status = TEXT("在线操作超时，等待服务清理；可返回大厅，未恢复时重启游戏");
            UE_LOG(LogFPSDemo, Warning, TEXT("ROOM EOS operation timed out; quarantined until original callback"));
            if (State == EDemoRoomState::Creating) { bReturnBrowser = true; UGameplayStatics::OpenLevel(this,FName(*LobbyMap),true); }
            return true;
        }
        if (State == EDemoRoomState::Closing)
        {
            Deadline = 0; State = EDemoRoomState::Failed;
            if (IsPublic()) { bSessionQuarantined = true; Status = TEXT("在线会话清理超时，请重启游戏后重试"); } // 不能在SDK回调仍在途时RemoveNamedSession再创建同名会话。
            else { Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle); DestroyHandle.Reset(); Sessions->RemoveNamedSession(NAME_GameSession); }
            UE_LOG(LogFPSDemo, Warning, TEXT("ROOM session cleanup timed out public=%d"), IsPublic());
        }
        else Fail(TEXT("连接操作超时，请确认房主仍在线后重试"), State != EDemoRoomState::Searching && State != EDemoRoomState::Joining);
        return true;
    }
    UWorld* World = GetWorld(); // GI旅行期间World可能尚未就绪，不能保留旧World指针。
    const ADemoGameState* GameState = World ? World->GetGameState<ADemoGameState>() : nullptr; // 明确Hub才公开房间。
    const ADemoPlayerController* Player = Cast<ADemoPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()); // 当前地图的本地拥有者。
    if (State == EDemoRoomState::OpeningHost && World && World->GetNetMode() == NM_ListenServer && GameState && GameState->Phase == EDemoPhase::Hub && Player && Player->IsCoopRegistered())
    {
        if (!ResolveSessions()) { Fail(TEXT("房间服务未就绪"), true); return true; }
        FOnlineSessionSettings Settings; // 统一房间语义，EOS使用Sessions目录，不引入第二套Lobby生命周期。
        Settings.bIsLANMatch = !IsPublic(); Settings.NumPublicConnections = 4; Settings.bShouldAdvertise = true;
        Settings.bAllowJoinInProgress = true; Settings.bUsesPresence = false; Settings.bAllowInvites = false;
        Settings.Set(ProtocolKey, Protocol, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
        Settings.bUseLobbiesIfAvailable = false; // 房主离开即销毁战局，不承诺EOS Lobby所有权迁移可以迁移UE服务器。
        if (IsPublic())
        {
            Settings.Set(BucketKey, Protocol, EOnlineDataAdvertisementType::ViaOnlineService);
            Settings.Set(RegionKey, GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->GetRegion(), EOnlineDataAdvertisementType::ViaOnlineService);
            Settings.Set(LevelKey, GameState->LevelNumber, EOnlineDataAdvertisementType::ViaOnlineService);
            Settings.Set(DifficultyKey, static_cast<int32>(GameState->Difficulty), EOnlineDataAdvertisementType::ViaOnlineService);
        }
        Settings.Set(RoomKey, FString::Printf(TEXT("合作房间 %s"), *GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->GetLocalPlayerKey().Left(6)), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
        Settings.Set(OpenKey, 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing); Settings.Set(CountKey, 1, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
        CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &UDemoRoomSubsystem::OnCreated));
        State = EDemoRoomState::Creating; Deadline = FPlatformTime::Seconds() + 15.;
        if (!Sessions->CreateSession(0, NAME_GameSession, Settings)) { State = EDemoRoomState::Idle; Fail(TEXT("房间广播创建失败"), true); } // SDK拒绝启动，无在途Create需要等待。
    }
    else if (State == EDemoRoomState::Travelling && World && World->GetNetMode() == NM_Client && Player && Player->IsCoopRegistered())
    { State = EDemoRoomState::Connected; Deadline = 0; Status = TEXT("已加入合作房间"); UE_LOG(LogFPSDemo, Display, TEXT("ROOM_JOIN registered")); }
    else if (State == EDemoRoomState::Hosting) UpdateAdvertisement();
    return true;
}
void UDemoRoomSubsystem::CloseSession()
{
    DEMO_LOG_CALL();
    if (State == EDemoRoomState::Closing) return; // 重复失败/退出回调不能清掉正在等待的销毁超时。
    if (IsPublic() && (State == EDemoRoomState::Searching || State == EDemoRoomState::Creating || State == EDemoRoomState::Joining))
    { bDiscardPending = true; return; } // 保留原回调，成功/失败都只进入清理，绝不同时Create/Destroy同名会话。
    if (IsPublic() && bUpdating)
    { bCloseAfterUpdate = true; State = EDemoRoomState::Closing; Deadline = FPlatformTime::Seconds() + 15.; return; } // 不只Create/Join，Update同样持有SDK命名会话引用。
    ClearOperationDelegates(); Deadline = 0;
    if (Sessions) Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle);
    UpdateHandle.Reset(); bUpdating = false; UpdateDeadline = 0;
    if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession)) { State = EDemoRoomState::Idle; return; }
    if (DestroyHandle.IsValid()) return;
    DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &UDemoRoomSubsystem::OnDestroyed));
    State = EDemoRoomState::Closing; Deadline = FPlatformTime::Seconds() + (IsPublic() ? 15. : 5.); // 公网清理不能沿用LAN瞬时完成假设。
    DEMO_NET_DIAG(TEXT("Room.DestroySession.Begin"), GetWorld(), TEXT("source=closeSession"));
    const bool bDestroyAccepted = Sessions->DestroySession(NAME_GameSession); // 单次调用可能同步触发OnDestroyed，日志允许Callback先于Returned。
    DEMO_NET_DIAG(TEXT("Room.DestroySession.Returned"), GetWorld(), FString::Printf(TEXT("source=closeSession accepted=%d"), bDestroyAccepted));
    if (!bDestroyAccepted) OnDestroyed(NAME_GameSession, false);
}
void UDemoRoomSubsystem::OnDestroyed(FName SessionName, bool bSuccess)
{
    DEMO_LOG_CALL();
    DEMO_NET_DIAG(TEXT("Room.DestroySession.Callback"), bStopping ? nullptr : GetWorld(), FString::Printf(TEXT("subsystem=%u gameSession=%d success=%d stopping=%d sessionsValid=%d"), GetUniqueID(), SessionName == NAME_GameSession, bSuccess, bStopping, Sessions.IsValid())); // 停止后的迟到回调不再查询GI/World，不输出目录会话ID。
    if (SessionName != NAME_GameSession || bStopping || !Sessions.IsValid()) return;
    Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle); DestroyHandle.Reset();
    if (!bSuccess)
    {
        if (IsPublic()) { bSessionQuarantined = true; Status = TEXT("在线会话清理失败，请重启游戏后重试"); }
        else Sessions->RemoveNamedSession(NAME_GameSession);
        UE_LOG(LogFPSDemo, Warning, TEXT("ROOM session destruction failed public=%d"), IsPublic());
    }
    else bSessionQuarantined = false; // 超时后迟到的最终成功可以解除隔离。
    Deadline = 0; State = bSessionQuarantined ? EDemoRoomState::Failed : EDemoRoomState::Idle;
}
void UDemoRoomSubsystem::Fail(const FString& Message, bool bReturnToLobby)
{
    DEMO_LOG_CALL();
    if (bStopping) return;
    if (State == EDemoRoomState::Searching) CancelSearch();
    Status = Message; UE_LOG(LogFPSDemo, Warning, TEXT("ROOM failure: %s"), *Message);
    CloseSession();
    if (bReturnToLobby && !LobbyMap.IsEmpty())
    {
        bReturnBrowser = true;
        if (GEngine) GEngine->CancelPending(GetWorld());
        UGameplayStatics::OpenLevel(this, FName(*LobbyMap), true);
    }
}
void UDemoRoomSubsystem::Leave()
{
    DEMO_LOG_CALL();
    CancelSearch(); Status = TEXT("已离开房间"); CloseSession();
    if (LobbyMap.IsEmpty()) LobbyMap = UGameplayStatics::GetCurrentLevelName(this, true);
    if (GEngine) GEngine->CancelPending(GetWorld());
    UGameplayStatics::OpenLevel(this, FName(*LobbyMap), true);
}
void UDemoRoomSubsystem::RegistrationFailed(const FString& Reason)
{
    DEMO_LOG_CALL();
    if (!GetWorld() || GetWorld()->GetNetMode()!=NM_Client) return;
    if (LobbyMap.IsEmpty()) LobbyMap=UGameplayStatics::GetCurrentLevelName(this,true); // 控制台直连同样有本地退路。
    Fail(Reason,true); // 不等待外层40秒超时，也不把失败客户端继续算作房间成员。
}
void UDemoRoomSubsystem::OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
    DEMO_LOG_CALL();
    if (bStopping || !World || World->GetGameInstance() != GetGameInstance()) return;
    if (World->GetNetMode() == NM_ListenServer && (Type == ENetworkFailure::ConnectionLost || Type == ENetworkFailure::ConnectionTimeout))
    { UE_LOG(LogFPSDemo, Log, TEXT("ROOM peer disconnected; host keeps running")); return; } // 单个连接失败交给Logout，不能关闭整个房间。
    if (LobbyMap.IsEmpty()) LobbyMap = UGameplayStatics::GetCurrentLevelName(this, true); // 兼容控制台直连，也保留返回路径。
    UE_LOG(LogFPSDemo, Warning, TEXT("ROOM network failure type=%d reason=%s"), static_cast<int32>(Type), *Error);
    Fail(TEXT("连接失败或房主已离线，请返回房间列表重试"), true);
}
void UDemoRoomSubsystem::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
    DEMO_LOG_CALL();
    if (bStopping || !World || World->GetGameInstance() != GetGameInstance() || (State != EDemoRoomState::Travelling && State != EDemoRoomState::OpeningHost)) return;
    UE_LOG(LogFPSDemo, Warning, TEXT("ROOM travel failure type=%d reason=%s"), static_cast<int32>(Type), *Error);
    Fail(TEXT("加载房间失败，请检查两端版本和地图一致"), true);
}
