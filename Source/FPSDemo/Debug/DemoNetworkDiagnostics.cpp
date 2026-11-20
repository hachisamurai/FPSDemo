#include "Debug/DemoNetworkDiagnostics.h"

#if WITH_EDITOR
#include "Debug/DemoLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// 独立类别仅编译进Editor；文件不依赖LogFPSDemo过滤级别，方便从大量玩法埋点中提取时间线。
DEFINE_LOG_CATEGORY_STATIC(LogDemoNetDiag, Log, All);

namespace
{
    TAutoConsoleVariable<int32> Enabled(TEXT("Demo.NetDiag.Enabled"), 1,
        TEXT("Editor-only passive network diagnostics. 0 disables collection and file output.")); // 默认启用本轮排障；游戏线程读取，不向包体暴露。
    FString FileStem; // 本进程首次事件生成的固定UTC/PID文件名前缀，不含用户输入，不跨进程共享。
    uint64 Sequence = 0; // 游戏线程独占的递增序号，用于跨切图串联；不会按PIE轮次归零。
    int32 Part = 0; // 当前轮转槽0..3，每槽8MiB；同一进程最多保留32MiB独立诊断。
    int64 BytesWritten = 0; // 当前槽估算UTF-8字节数；成功落盘后递增。
    bool bFileFailed = false; // 磁盘写失败后只警告一次并停止重试，主日志仍继续输出事件。
    constexpr int64 PartLimit = 8 * 1024 * 1024; // 每槽8MiB，避免长时间开Editor造成独立日志无限增长。

    /** World只在游戏线程同步借用；不通过GetSubsystem/GetOnlineSubsystem创建服务，不读取旅行URL或玩家身份。 */
    FString DescribeWorld(const UWorld* World)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] DemoNetworkDiagnostics::DescribeWorld"));
        if (!IsValid(World)) return TEXT("world=none");
        const UGameInstance* Instance = World->GetGameInstance(); // 可为空或正在退出；仅有效时取进程内对象编号，不访问其子系统集合。
        const UNetDriver* Driver = World->GetNetDriver(); // 当前World直接持有的驱动，禁止创建新驱动或解析地址。
        const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr; // 只读已有上下文，清理尾部可不存在。
        return FString::Printf(TEXT("world=%s wid=%u pie=%d type=%d tearing=%d gi=%u driver=%u driverClass=%s driverName=%s clients=%d serverConnection=%d"),
            *World->GetName(), World->GetUniqueID(), Context ? Context->PIEInstance : INDEX_NONE, static_cast<int32>(World->WorldType), World->bIsTearingDown,
            IsValid(Instance) ? Instance->GetUniqueID() : 0, IsValid(Driver) ? Driver->GetUniqueID() : 0,
            IsValid(Driver) ? *Driver->GetClass()->GetName() : TEXT("none"), IsValid(Driver) ? *Driver->NetDriverName.ToString() : TEXT("none"),
            IsValid(Driver) ? Driver->ClientConnections.Num() : 0, IsValid(Driver) && Driver->ServerConnection != nullptr);
    }
}

bool DemoNetworkDiagnostics::IsEnabled()
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] DemoNetworkDiagnostics::IsEnabled"));
    return IsInGameThread() && Enabled.GetValueOnGameThread() != 0;
}

void DemoNetworkDiagnostics::Record(const TCHAR* Event, const UWorld* World, const FString& Details)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] DemoNetworkDiagnostics::Record"));
    if (!IsEnabled()) return; // 非游戏线程不读取UObject；调用者须保持游戏线程边界，禁止在异步完成线程直接传World。
    const FDateTime Utc = FDateTime::UtcNow(); // UTC绝对时间与Windows事件对齐，显示北京时间时加8小时。
    const uint32 ProcessId = FPlatformProcess::GetCurrentProcessId(); // 区分Editor及Standalone子进程，不收集其他进程身份。
    if (FileStem.IsEmpty())
    {
        const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Logs/NetworkDiagnostics")); // 固定项目内诊断目录，不读取配置路径。
        bFileFailed = !IFileManager::Get().MakeDirectory(*Directory, true);
        FileStem = Directory / FString::Printf(TEXT("NetDiag-%s-P%u"), *Utc.ToString(TEXT("%Y%m%d-%H%M%S-%s")), ProcessId);
        UE_LOG(LogDemoNetDiag, Display, TEXT("NETDIAG_FILE %s-0.log (UTC, passive, editor-only)"), *FileStem);
        if (bFileFailed) UE_LOG(LogDemoNetDiag, Warning, TEXT("NETDIAG file directory unavailable; main log remains enabled"));
    }
    FString SafeDetails = Details.Left(2048); // 固定上限防止误传大块内容；换行归一防止单条状态污染时间线。
    SafeDetails.ReplaceInline(TEXT("\r"), TEXT(" ")); SafeDetails.ReplaceInline(TEXT("\n"), TEXT(" "));
    const FString Line = FString::Printf(TEXT("[NETDIAG] utc=%s mono=%.6f pid=%u seq=%llu event=%s %s %s\n"),
        *Utc.ToIso8601(), FPlatformTime::Seconds(), ProcessId, ++Sequence, Event, *DescribeWorld(World), *SafeDetails); // 单调时间计算耗时，UTC仅用于跨日志对照。
    UE_LOG(LogDemoNetDiag, Log, TEXT("%s"), *Line.TrimEnd());
    if (bFileFailed) return;
    const int64 LineBytes = FTCHARToUTF8(*Line).Length(); // UTF-8无BOM，每次写入关闭句柄，崩溃/断网现场尽量保留此前完成的记录。
    bool bAppend = true; // 首次写新槽自动创建；达到上限时只覆盖本进程命名的下一个轮转槽。
    if (BytesWritten + LineBytes > PartLimit) { Part = (Part + 1) % 4; BytesWritten = 0; bAppend = false; }
    const FString Filename = FString::Printf(TEXT("%s-%d.log"), *FileStem, Part); // 文件名不含用户路径或网络地址。
    if (!FFileHelper::SaveStringToFile(Line, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
        &IFileManager::Get(), FILEWRITE_AllowRead | (bAppend ? FILEWRITE_Append : 0)))
    {
        bFileFailed = true;
        UE_LOG(LogDemoNetDiag, Warning, TEXT("NETDIAG file write failed; independent file disabled for this process"));
        return;
    }
    BytesWritten += LineBytes;
}
#endif
