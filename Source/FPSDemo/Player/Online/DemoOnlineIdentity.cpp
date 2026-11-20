#include "Player/Online/DemoOnlineIdentity.h"
#include "Game/DemoGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSubsystemEOSTypesPublic.h"
#include "EOSSettings.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoNetworkDiagnostics.h" // 身份退出仅记录状态位，不输出OnlineKey/账号/Token。

void UDemoOnlineIdentity::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Super::Initialize(Collection);
    Status = TEXT("局域网无需在线账号");
    Region = Region.Left(16);
    for (const TCHAR Character : Region) // 目录字段只允许短ASCII代码，不把自由文本作为查询表达式。
        if (Character > 127 || (!FChar::IsAlnum(Character) && Character != TEXT('-'))) { Region = TEXT("Asia"); break; }
    if (Region.IsEmpty()) Region = TEXT("Asia");
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDemoOnlineIdentity::TickIdentity), .5f);
    DEMO_NET_DIAG(TEXT("Identity.Initialized"), GetWorld(), FString::Printf(TEXT("subsystem=%u tickerInstalled=1"), GetUniqueID())); // 不同PIE的身份服务各自编号。
}
void UDemoOnlineIdentity::Deinitialize()
{
    DEMO_LOG_CALL();
    DEMO_NET_DIAG(TEXT("Identity.Deinitialize.Begin"), GetWorld(), FString::Printf(TEXT("subsystem=%u identityValid=%d loggingIn=%d loggingOut=%d network=%d"), GetUniqueID(), Identity.IsValid(), bLoggingIn, bLoggingOut, static_cast<int32>(SelectedNetwork)));
    bStopping = true; // 保持原停止顺序；诊断不调用Logout、不查找在线实例、不改变SDK生命周期。
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if (Identity)
    {
        Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
        Identity->ClearOnLogoutCompleteDelegate_Handle(0, LogoutHandle);
        Identity->ClearOnLoginStatusChangedDelegate_Handle(0, ChangedHandle);
    }
    DEMO_NET_DIAG(TEXT("Identity.Callbacks.Removed"), GetWorld(), TEXT("ticker=removed delegates=removed"));
    Identity.Reset(); OnlineKey.Empty(); Super::Deinitialize();
    DEMO_NET_DIAG(TEXT("Identity.Deinitialize.End"), nullptr, FString::Printf(TEXT("subsystem=%u identityReset=1"), GetUniqueID())); // Super之后只取本对象编号，不借用GI/World。
}
bool UDemoOnlineIdentity::IsPublic() const { DEMO_LOG_TICK(); return SelectedNetwork == EDemoRoomNetwork::PublicEOS; }
bool UDemoOnlineIdentity::IsReady() const
{
    DEMO_LOG_TICK();
    return !IsPublic() || (Identity && !IsBusy() && !bAbandoned && !OnlineKey.IsEmpty() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn);
}
bool UDemoOnlineIdentity::IsBusy() const { DEMO_LOG_TICK(); return bLoggingIn || bLoggingOut; }
const FString& UDemoOnlineIdentity::GetStatus() const { DEMO_LOG_TICK(); return Status; }
const FString& UDemoOnlineIdentity::GetRegion() const { DEMO_LOG_TICK(); return Region; }
bool UDemoOnlineIdentity::SelectNetwork(EDemoRoomNetwork Network)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 连接过程中不可替换身份提供者。
    if (bStopping || !State || State->Phase != EDemoPhase::Lobby || GetWorld()->GetNetMode() != NM_Standalone)
    { UE_LOG(LogFPSDemo, Log, TEXT("ONLINE_SELECT rejected outside local lobby")); return false; }
    SelectedNetwork = Network;
    if (!IsPublic()) { if (bLoggingIn) bAbandoned = true; Status = TEXT("局域网无需在线账号"); return true; }
    if (!CheckConfiguration()) return true; // 允许显示公网页与缺配置提示，不创建虚假服务。
    Status = IsReady() ? TEXT("在线账号已登录") : TEXT("请登录在线账号后搜索或创建房间");
    return true;
}
bool UDemoOnlineIdentity::CheckConfiguration()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    Status = TEXT("编辑器仅使用本地数据；公网请使用打包版本"); UE_LOG(LogFPSDemo, Log, TEXT("EOS disabled in WITH_EDITOR target")); return false;
#else
    if (!bEnablePublicRooms) { Status = TEXT("公网尚未配置：请按公网接入文档填写EOS配置并启用"); UE_LOG(LogFPSDemo, Log, TEXT("EOS public rooms disabled in config")); return false; }
    const FEOSSettings Settings = UEOSSettings::GetSettings(); // 本地配置只在栈上检查，不输出ClientSecret。
    // UE5.4中EOS不是默认OSS时，PersistentAuth路径会在无缓存凭据时打开AccountPortal；否则会误走外部平台令牌登录。
    if (!Settings.bUseEAS || !Settings.bUseEOSConnect || !Settings.bUseEOSSessions || !Settings.bPreferPersistentAuth)
    { Status = TEXT("EOS配置缺少账号、Connect、Sessions或PersistentAuth开关"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS required feature flags missing")); return false; }
    for (const FEOSArtifactSettings& Artifact : Settings.Artifacts) // 本项目使用DefaultArtifact；部署切换应通过本地发行配置完成。
        if (Artifact.ArtifactName == Settings.DefaultArtifactName && !Artifact.ArtifactName.IsEmpty()
            && !Artifact.ClientId.IsEmpty() && !Artifact.ClientSecret.IsEmpty() && !Artifact.ProductId.IsEmpty()
            && !Artifact.SandboxId.IsEmpty() && !Artifact.DeploymentId.IsEmpty()) return true;
    Status = TEXT("EOS产品配置不完整：请填写DefaultArtifact及对应平台参数"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS default artifact incomplete (values redacted)")); return false;
#endif
}
bool UDemoOnlineIdentity::Login()
{
    DEMO_LOG_CALL();
    if (bStopping || !IsPublic() || IsBusy() || !CheckConfiguration()) { UE_LOG(LogFPSDemo, Log, TEXT("EOS_LOGIN rejected mode/busy/config")); return false; }
#if !WITH_EDITOR
    if (IsReady()) return true;
    IOnlineSubsystem* Backend = Online::GetSubsystem(GetWorld(), FName(TEXT("EOS"))); // 仅显式公网登录时创建EOS实例，默认OSS仍为NULL。
    if (!Backend || !(Identity = Backend->GetIdentityInterface())) { Status = TEXT("EOS服务初始化失败，请检查产品配置"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS identity service unavailable")); return false; }
    if (!LoginHandle.IsValid()) LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0, FOnLoginCompleteDelegate::CreateUObject(this, &UDemoOnlineIdentity::OnLoginComplete));
    if (!LogoutHandle.IsValid()) LogoutHandle = Identity->AddOnLogoutCompleteDelegate_Handle(0, FOnLogoutCompleteDelegate::CreateUObject(this, &UDemoOnlineIdentity::OnLogoutComplete));
    if (!ChangedHandle.IsValid()) ChangedHandle = Identity->AddOnLoginStatusChangedDelegate_Handle(0, FOnLoginStatusChangedDelegate::CreateUObject(this, &UDemoOnlineIdentity::OnLoginChanged));
    bLoggingIn = true; bAbandoned = false; Deadline = FPlatformTime::Seconds() + 120.;
    Status = TEXT("等待浏览器登录授权……");
    const FOnlineAccountCredentials Credentials(TEXT("accountportal"), FString(), FString()); // 配合PersistentAuth缓存恢复；首次由浏览器授权，不收集密码或把令牌传入URL。
    if (!Identity->Login(0, Credentials)) { bLoggingIn = false; Deadline = 0; Status = TEXT("登录未能启动，请重试"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS login request rejected")); return false; }
    return true;
#else
    return false;
#endif
}
bool UDemoOnlineIdentity::Logout()
{
    DEMO_LOG_CALL();
    if (!Identity || IsBusy() || bStopping) { UE_LOG(LogFPSDemo, Log, TEXT("EOS_LOGOUT rejected busy/unavailable")); return false; }
    OnlineKey.Empty(); bLoggingOut = true; Deadline = FPlatformTime::Seconds() + 15.; Status = TEXT("正在退出在线账号……");
    if (!Identity->Logout(0)) { bLoggingOut = false; bAbandoned = true; Deadline = 0; Status = TEXT("账号退出失败，请重启游戏后登录"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS logout request rejected")); return false; }
    return true;
}
void UDemoOnlineIdentity::OnLoginComplete(int32 LocalUser, bool bSuccess, const FUniqueNetId& Id, const FString& Error)
{
    DEMO_LOG_CALL();
    if (bStopping || LocalUser != 0 || !bLoggingIn) return;
    bLoggingIn = false; Deadline = 0;
    if (bAbandoned || !IsPublic()) { OnlineKey.Empty(); if (bSuccess) Logout(); return; }
    OnlineKey = bSuccess ? PlayerKeyFromNetId(Id) : FString();
    Status = OnlineKey.IsEmpty() ? TEXT("登录失败或已取消，请检查测试账号权限后重试") : TEXT("在线账号已登录，可以搜索或创建房间");
    UE_LOG(LogFPSDemo, Display, TEXT("EOS_LOGIN success=%d"), !OnlineKey.IsEmpty()); // 不记录账号、Token或平台错误正文。
}
void UDemoOnlineIdentity::OnLogoutComplete(int32 LocalUser, bool bSuccess)
{
    DEMO_LOG_CALL(); if (bStopping || LocalUser != 0) return;
    bLoggingOut = false; Deadline = 0; OnlineKey.Empty(); bAbandoned = !bSuccess;
    Status = bSuccess ? TEXT("已退出账号，可以重新登录") : TEXT("退出失败，请重启游戏后登录");
    UE_LOG(LogFPSDemo, Display, TEXT("EOS_LOGOUT success=%d"), bSuccess);
}
void UDemoOnlineIdentity::OnLoginChanged(int32 LocalUser, ELoginStatus::Type Old, ELoginStatus::Type New, const FUniqueNetId& Id)
{
    DEMO_LOG_CALL(); if (bStopping || LocalUser != 0 || New == ELoginStatus::LoggedIn) return;
    OnlineKey.Empty();
    if (!IsBusy()) { Status = TEXT("在线身份已失效，请重新登录"); UE_LOG(LogFPSDemo, Warning, TEXT("EOS identity no longer authenticated")); } // 主动注销属于正常结果，不额外刷警告。
}
bool UDemoOnlineIdentity::TickIdentity(float DeltaSeconds)
{
    DEMO_LOG_TICK(); if (bStopping) return false;
    if (Deadline > 0 && FPlatformTime::Seconds() >= Deadline)
    {
        Deadline = 0; bAbandoned = true; OnlineKey.Empty();
        Status = TEXT("账号操作超时，请关闭授权窗口；若未恢复请重启游戏");
        UE_LOG(LogFPSDemo, Warning, TEXT("EOS account operation timed out; quarantining pending callback"));
    }
    return true;
}
IOnlineSessionPtr UDemoOnlineIdentity::GetSessions()
{
    DEMO_LOG_CALL();
    if (!IsReady()) return nullptr;
#if WITH_EDITOR
    if (IsPublic()) return nullptr;
#endif
    IOnlineSubsystem* Backend = Online::GetSubsystem(GetWorld(), IsPublic() ? FName(TEXT("EOS")) : FName(TEXT("NULL"))); // World隔离的明确提供者，禁止失败后跨提供者回退。
    return Backend ? Backend->GetSessionInterface() : nullptr;
}
FUniqueNetIdPtr UDemoOnlineIdentity::GetLocalNetId() const
{
    DEMO_LOG_TICK(); return IsPublic() && IsReady() ? Identity->GetUniquePlayerId(0) : nullptr;
}
FString UDemoOnlineIdentity::GetOnlinePlayerKey() const { DEMO_LOG_TICK(); return IsPublic() && IsReady() ? OnlineKey : FString(); }
FString UDemoOnlineIdentity::PlayerKeyFromNetId(const FUniqueNetId& Id)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
#if WITH_EOS_SDK
    if (!Id.IsValid() || Id.GetType() != FName(TEXT("EOS"))) return FString();
    const IUniqueNetIdEOS& EOSId = static_cast<const IUniqueNetIdEOS&>(Id); // 类型白名单通过后使用引擎EOS接口，不解析显示名。
    char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {}; // 仅关联用PUID，不是认证Token。
    int32 Length = UE_ARRAY_COUNT(Buffer); // SDK输出容量包含终止符。
    if (EOS_ProductUserId_ToString(EOSId.GetProductUserId(), Buffer, &Length) != EOS_EResult::EOS_Success) return FString();
    FGuid Parsed; // PUID为32位十六进制，规范化保持V5团队字段兼容；不是随机生成的新身份。
    if (FGuid::ParseExact(UTF8_TO_TCHAR(Buffer), EGuidFormats::Digits, Parsed) && Parsed.IsValid()) return Parsed.ToString(EGuidFormats::DigitsWithHyphens);
#endif
    return FString();
}
bool UDemoOnlineIdentity::IsEOSAddress(const FString& Address)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    TArray<FString> Parts; // 只接受SDK格式EOS:PUID:Socket:Channel，绝不允许旅行选项。
    Address.ParseIntoArray(Parts, TEXT(":"), false);
    if (Parts.Num() != 4 || Parts[0] != TEXT("EOS") || Parts[1].Len() != 32 || Parts[2] != TEXT("GameNetDriver") || Parts[3].IsEmpty() || Parts[3].Len() > 3) return false;
    for (const TCHAR Character : Parts[1]) if (!FChar::IsHexDigit(Character)) return false; // PUID限定ASCII十六进制。
    for (const TCHAR Character : Parts[3]) if (Character < TEXT('0') || Character > TEXT('9')) return false; // 通道必须0..255。
    return FCString::Atoi(*Parts[3]) <= 255;
}
bool UDemoOnlineIdentity::MatchTransportIdentity(const FString& Address, const FUniqueNetId& Id)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    TArray<FString> Parts; // 地址来自NetConnection，P2P远端身份由EOS传输提供，URL自报身份不能替代。
    Address.ParseIntoArray(Parts, TEXT(":"), false);
    if (!IsEOSAddress(Parts.Num() == 3 ? Address + TEXT(":0") : Address)) return false; // UE PreLogin使用不含通道的远端地址，RPC复核使用完整地址。
    FGuid Parsed; // 与保存格式相同的规范关联键，大小写差异只在此规范化。
    return FGuid::ParseExact(Parts[1], EGuidFormats::Digits, Parsed) && Parsed.ToString(EGuidFormats::DigitsWithHyphens) == PlayerKeyFromNetId(Id);
}
