#include "DemoApiClient.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY(LogDemoOnline);
/** 独立网络模块生命周期仅记录，不在模块启动时访问玩家/数据库。 */
class FDemoOnlineModule : public IModuleInterface
{
public:
    virtual void StartupModule() override { UE_LOG(LogDemoOnline, Log, TEXT("[CALL] StartupModule")); }
    virtual void ShutdownModule() override { UE_LOG(LogDemoOnline, Log, TEXT("[CALL] ShutdownModule")); }
};
IMPLEMENT_MODULE(FDemoOnlineModule, FPSDemoOnline)

bool UDemoApiClient::ShouldCreateSubsystem(UObject* Outer) const
{
    UE_LOG(LogDemoOnline, Log, TEXT("[CALL] ShouldCreateSubsystem"));
#if WITH_EDITOR
    UE_LOG(LogDemoOnline, Log, TEXT("Editor build: local saves only; HTTP subsystem not created"));
    return false; // 编译目标隔离，不用GIsEditor；Editor二进制的-game也不能连云。
#else
    return Super::ShouldCreateSubsystem(Outer);
#endif
}

void UDemoApiClient::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogDemoOnline, Log, TEXT("[CALL] Initialize"));
    Super::Initialize(Collection);
    BaseUrl.TrimStartAndEndInline(); // 容忍配置编辑时的空白，URL在ini中需加引号以保留双斜杠。
    BaseUrl = BaseUrl.TrimQuotes();
    BaseUrl.RemoveFromEnd(TEXT("/"));
    TimeoutSeconds = FMath::Clamp(TimeoutSeconds, 2.f, 30.f);
    // 不允许通过明文远端HTTP发送凭据；本机调试仅放行明确的loopback主机。
    const bool bLocal = BaseUrl.StartsWith(TEXT("http://127.0.0.1:")) || BaseUrl.StartsWith(TEXT("http://localhost:")); // 本机端口仍由下面URI校验限制。
    if ((!BaseUrl.StartsWith(TEXT("https://")) && !bLocal) || BaseUrl.Contains(TEXT("@")) || BaseUrl.Contains(TEXT("?")) || BaseUrl.Contains(TEXT("#")))
    { bEnabled = false; UE_LOG(LogDemoOnline, Error, TEXT("API disabled: invalid base URL (length=%d)"), BaseUrl.Len()); }
}
bool UDemoApiClient::IsEnabled() const { UE_LOG(LogDemoOnline, VeryVerbose, TEXT("[CALL] IsEnabled")); return bEnabled; }
void UDemoApiClient::Deinitialize() { UE_LOG(LogDemoOnline, Log, TEXT("[CALL] Deinitialize")); Cancel(); Super::Deinitialize(); }
void UDemoApiClient::Cancel()
{
    UE_LOG(LogDemoOnline, Log, TEXT("[CALL] Cancel"));
    PendingReply.Unbind();
    if (ActiveRequest) { ActiveRequest->OnProcessRequestComplete().Unbind(); ActiveRequest->CancelRequest(); ActiveRequest.Reset(); }
}
bool UDemoApiClient::Send(const FString& Verb, const FString& Path, const FString& Body, const FString& Token, FDemoApiReply Reply)
{
    UE_LOG(LogDemoOnline, Log, TEXT("[CALL] Send %s %s"), *Verb, *Path);
    if (!bEnabled || ActiveRequest || !IsInGameThread() || !Path.StartsWith(TEXT("/v1/")))
    { UE_LOG(LogDemoOnline, Warning, TEXT("HTTP rejected: disabled/busy/thread/path")); return false; }
    ActiveRequest = FHttpModule::Get().CreateRequest();
    PendingReply = MoveTemp(Reply);
    ActiveRequest->SetURL(BaseUrl + Path);
    ActiveRequest->SetVerb(Verb);
    ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    if (!Token.IsEmpty()) ActiveRequest->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
    ActiveRequest->SetContentAsString(Body);
    ActiveRequest->SetTimeout(TimeoutSeconds);
    ActiveRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    ActiveRequest->OnProcessRequestComplete().BindUObject(this, &UDemoApiClient::Complete);
    if (!ActiveRequest->ProcessRequest()) { Cancel(); return false; }
    return true;
}
void UDemoApiClient::Complete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
    UE_LOG(LogDemoOnline, Log, TEXT("[CALL] Complete HTTP=%d success=%d"), Response ? Response->GetResponseCode() : 0, bSucceeded);
    if (Request != ActiveRequest) return; // 取消后的过期回调不能消费下一次请求的委托。
    FDemoApiReply Reply = MoveTemp(PendingReply); // 移出委托后再释放请求，允许业务回调发出下一步HTTP。
    ActiveRequest.Reset();
    Reply.ExecuteIfBound(bSucceeded && Response.IsValid(), Response ? Response->GetResponseCode() : 0, Response ? Response->GetContentAsString() : FString());
}
const FString& UDemoApiClient::GetIdentityError() const { UE_LOG(LogDemoOnline, VeryVerbose, TEXT("[CALL] GetIdentityError")); return IdentityError; }
bool UDemoApiClient::IdentityFailure(const TCHAR* Stage, const TCHAR* Message, const FString& Path, uint32 SystemError)
{
    UE_LOG(LogDemoOnline, Warning, TEXT("[CALL] IdentityFailure IDENTITY_FAILURE stage=%s win32=%u path=%s"), Stage, SystemError, *Path);
    IdentityError = FString::Printf(TEXT("%s [%s/%u]"), Message, Stage, SystemError);
    return false;
}
bool UDemoApiClient::LoadIdentity(FString& OutId, FString& OutSecret)
{
    UE_LOG(LogDemoOnline, Log, TEXT("[CALL] LoadIdentity (redacted)"));
    OutId.Empty(); OutSecret.Empty();
    IdentityError.Empty(); // 上次失败不应污染下一次成功的登录提示。
#if PLATFORM_WINDOWS
    FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Cloud")); // 凭据独立于可删除的游戏存档，不进入Content打包。
#if !UE_BUILD_SHIPPING
    FString CloudTest; FGuid CloudId; // 自动化身份隔离，GUID规范化后才能参与路径组成。
    if (FParse::Value(FCommandLine::Get(), TEXT("DemoCloudTestId="), CloudTest) && FGuid::Parse(CloudTest, CloudId) && CloudId.IsValid()) Directory = FPaths::Combine(Directory, TEXT("Tests"), CloudId.ToString(EGuidFormats::Digits));
#endif
    Directory = FPaths::ConvertRelativePathToFull(Directory); // 日志记录真实绝对路径，跨电脑/工作目录排查不再依赖猜测Saved位置。
    const FString Path = FPaths::Combine(Directory, TEXT("Identity.bin")); // DPAPI绑定当前Windows用户，移动存档不会自动转移账号。
    UE_LOG(LogDemoOnline, Log, TEXT("IDENTITY_PATH existing=%d path=%s"), IFileManager::Get().FileExists(*Path), *Path); // 路径不是凭据，可供远端用户定位写权限。
    TArray<uint8> Bytes; // 磁盘密文；禁止写日志。
    if (IFileManager::Get().FileExists(*Path))
    {
        const int64 Size = IFileManager::Get().FileSize(*Path); // 先限制大小再读盘，拒绝空文件或异常体积。
        if (Size <= 0 || Size > 16384) return IdentityFailure(TEXT("file_size"), TEXT("账号文件为空或格式异常，已保留原文件"), Path);
        if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return IdentityFailure(TEXT("read"), TEXT("账号文件读取失败，请检查文件权限或占用"), Path);
        DATA_BLOB Input = { static_cast<DWORD>(Bytes.Num()), Bytes.GetData() }; // DPAPI借用密文，仅本同步调用有效。
        DATA_BLOB Output = {}; // Windows分配明文缓冲，使用后清零并LocalFree。
        if (!CryptUnprotectData(&Input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Output))
            return IdentityFailure(TEXT("decrypt"), TEXT("Windows无法解密账号文件；请检查是否来自其他电脑或已损坏"), Path, GetLastError());
        FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Output.pbData), Output.cbData); // 带长度转换，不依赖明文末尾零字符。
        const FString Plain(Decoded.Length(), Decoded.Get()); // 临时认证文本，只在当前线程拆分。
        const bool bSplit = Plain.Split(TEXT("\n"), &OutId, &OutSecret); // 格式固定为GUID和64位十六进制随机秘密。
        SecureZeroMemory(Output.pbData, Output.cbData); LocalFree(Output.pbData);
        FGuid Id; // 验证损坏身份文件；失败时不得静默注册另一账号覆盖旧档。
        bool bValid = bSplit && FGuid::Parse(OutId, Id) && Id.IsValid() && OutSecret.Len() == 64; // 除长度外还验证十六进制，不把损坏凭据发给后端。
        for (TCHAR Character : OutSecret) bValid = bValid && FChar::IsHexDigit(Character); // 同步校验当前短字符串，无捕获/异步生命周期。
        if (!bValid) { OutId.Empty(); OutSecret.Empty(); return IdentityFailure(TEXT("payload"), TEXT("账号内容校验失败，已保留原文件"), Path); }
        UE_LOG(LogDemoOnline, Log, TEXT("IDENTITY_READY source=existing"));
        return true;
    }
    OutId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    uint8 Random[32] = {}; // 由操作系统CSPRNG提供256位秘密，不使用游戏随机数。
    HCRYPTPROV Provider = 0; // Windows加密提供者仅在此函数内持有。
    if (!CryptAcquireContext(&Provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return IdentityFailure(TEXT("random_provider"), TEXT("Windows随机数服务不可用，未创建云账号"), Path, GetLastError());
    const bool bRandom = CryptGenRandom(Provider, sizeof(Random), Random) != 0; // 失败时不创建可猜测凭据。
    const uint32 RandomError = bRandom ? 0 : GetLastError(); // 必须在Release之前捕获错误，避免被后续Windows调用覆盖。
    CryptReleaseContext(Provider, 0);
    if (!bRandom) return IdentityFailure(TEXT("random"), TEXT("生成账号安全凭据失败"), Path, RandomError);
    OutSecret = BytesToHex(Random, sizeof(Random));
    SecureZeroMemory(Random, sizeof(Random));
    FTCHARToUTF8 Plain(*(OutId + TEXT("\n") + OutSecret)); // 临时明文，先持久化密文成功才能发认证请求。
    DATA_BLOB Input = { static_cast<DWORD>(Plain.Length()), reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(Plain.Get())) }; // DPAPI同步借用UTF8缓冲。
    DATA_BLOB Output = {}; // Windows输出密文，需要LocalFree释放。
    if (!CryptProtectData(&Input, L"FPSDemo guest identity", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Output)) return IdentityFailure(TEXT("encrypt"), TEXT("Windows账号加密失败，请使用正常登录的Windows用户运行"), Path, GetLastError());
    Bytes.Append(Output.pbData, Output.cbData); LocalFree(Output.pbData);
    if (!IFileManager::Get().DirectoryExists(*Directory) && !IFileManager::Get().MakeDirectory(*Directory, true))
        return IdentityFailure(TEXT("mkdir"), TEXT("无法创建账号目录，请将游戏完整解压到当前用户可写位置"), Path);
    // 临时文件+同目录移动避免断电留下半份身份；已有身份不会走到此分支。
    const FString TemporaryPath = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp"); // 独立临时名避免旧只读.tmp或并发首次启动互相覆盖。
    if (!FFileHelper::SaveArrayToFile(Bytes, *TemporaryPath)) return IdentityFailure(TEXT("write"), TEXT("账号写入失败，请检查磁盘空间和目录写入权限"), Path);
    if (!IFileManager::Get().Move(*Path, *TemporaryPath, false, false))
    {
        IFileManager::Get().Delete(*TemporaryPath, false, false); // 仅清理本次产生的密文临时文件，不删除既有身份或存档。
        return IdentityFailure(TEXT("commit"), TEXT("账号文件落盘失败，请关闭其他游戏进程后重试"), Path);
    }
    UE_LOG(LogDemoOnline, Log, TEXT("IDENTITY_READY source=created"));
    return true;
#else
    return IdentityFailure(TEXT("platform"), TEXT("此平台尚未实现安全账号存储"), FString());
#endif
}
