#include "Save/DemoEditorSessionCleanup.h"
#include "Editor.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"

namespace
{
    TMap<uint32, FProcHandle> WatchedProcesses; // 父Editor拥有查询句柄，保留进程对象身份以避免PID重用误判退出。
    FDelegateHandle BeginHandle; // 模块生命周期内的Editor启动委托，Shutdown移除。
    FTSTicker::FDelegateHandle TickHandle; // CoreTicker游戏线程轮询，不依赖当前World或暂停状态。

    /** ProcessId是已退出的被监视子进程；仅删除项目目录下符合完整临时槽语法的主档/备份。 */
    void CleanupExitedProcess(uint32 ProcessId)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] CleanupExitedProcess pid=%u"), ProcessId);
        // 极少数PID已被新进程重用时保守跳过，不能清理可能仍在使用的数据。
        if (FPlatformProcess::IsApplicationRunning(ProcessId)) { UE_LOG(LogFPSDemo, Warning, TEXT("EDITOR_CHILD_CLEANUP skipped reused pid=%u"), ProcessId); return; }
        TArray<FString> Files; // 固定SaveGames根目录的直接子文件，不递归，不接受命令行路径。
        const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames")); // 当前项目的明确目录边界。
        IFileManager::Get().FindFiles(Files, *(Directory / TEXT("DemoEditor*.sav")), true, false);
        for (const FString& File : Files) // 基本文件名来自固定目录枚举，不传入跨目录删除操作。
        {
            const FString Slot = FPaths::GetBaseFilename(File); // UE SaveGame API接受的槽名，不含路径/扩展名。
            FString BareSlot = Slot; // 去掉合法备份后缀后验证完整语法。
            BareSlot.RemoveFromEnd(TEXT("_Backup"));
            TArray<FString> Parts; // Profile_PID_GUID或Run_PID_GUID_Index，拒绝任何其他命名。
            BareSlot.ParseIntoArray(Parts, TEXT("_"), false);
            if ((Parts.Num() != 3 && Parts.Num() != 4) || Parts[1] != FString::FromInt(ProcessId)) continue;
            const bool bProfile = Parts[0] == TEXT("DemoEditorProfile") && Parts.Num() == 3; // 解锁档案精确格式。
            const bool bRun = Parts[0] == TEXT("DemoEditorRun") && Parts.Num() == 4 && (Parts[3] == TEXT("0") || Parts[3] == TEXT("1") || Parts[3] == TEXT("2")); // 仅三个战役栏。
            FGuid Id; // 必须是32位Digits GUID，避免删到名称相似的人工备份。
            if ((!bProfile && !bRun) || !FGuid::ParseExact(Parts[2], EGuidFormats::Digits, Id) || !Id.IsValid()) continue;
            const bool bDeleted = UGameplayStatics::DeleteGameInSlot(Slot, 0); // 平台API删除精确槽；不使用通配符删除。
            if (bDeleted) { UE_LOG(LogFPSDemo, Log, TEXT("EDITOR_CHILD_CLEANUP deleted %s"), *Slot); }
            else { UE_LOG(LogFPSDemo, Error, TEXT("EDITOR_CHILD_CLEANUP failed %s"), *Slot); }
        }
    }

    /** DeltaSeconds为CoreTicker真实秒增量；每秒检查退出状态，返回true保持模块级轮询。 */
    bool TickProcesses(float DeltaSeconds)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] TickProcesses"));
        for (auto It = WatchedProcesses.CreateIterator(); It; ++It) // 迭代器仅在本次同步调用持有，不跨帧。
        {
            if (FPlatformProcess::IsProcRunning(It.Value())) continue;
            CleanupExitedProcess(It.Key());
            FPlatformProcess::CloseProc(It.Value());
            It.RemoveCurrent();
        }
        return true;
    }
}

void DemoEditorSessionCleanup::Startup()
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoEditorSessionCleanup::Startup"));
    // Editor可执行文件的-game进程只负责自身GI清理，不充当父监视器。
    if (!GIsEditor) { UE_LOG(LogFPSDemo, Log, TEXT("EDITOR_CHILD_CLEANUP watcher disabled in game process")); return; }
    BeginHandle = FEditorDelegates::BeginStandaloneLocalPlay.AddStatic(&WatchProcess);
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickProcesses), 1.f);
}

void DemoEditorSessionCleanup::WatchProcess(uint32 ProcessId)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoEditorSessionCleanup::WatchProcess pid=%u"), ProcessId);
    if (!GIsEditor || !ProcessId || ProcessId == FPlatformProcess::GetCurrentProcessId() || WatchedProcesses.Contains(ProcessId))
    { UE_LOG(LogFPSDemo, Log, TEXT("EDITOR_CHILD_CLEANUP watch rejected: non-editor/invalid/self/duplicate pid=%u"), ProcessId); return; }
    FProcHandle Handle = FPlatformProcess::OpenProcess(ProcessId); // 父进程拥有查询句柄；关闭句柄不终止游戏。
    if (!Handle.IsValid()) { UE_LOG(LogFPSDemo, Warning, TEXT("EDITOR_CHILD_CLEANUP unable to watch pid=%u"), ProcessId); return; }
    WatchedProcesses.Add(ProcessId, Handle);
}

void DemoEditorSessionCleanup::Shutdown()
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoEditorSessionCleanup::Shutdown"));
    FEditorDelegates::BeginStandaloneLocalPlay.Remove(BeginHandle);
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    TickProcesses(0);
    for (auto& Entry : WatchedProcesses) FPlatformProcess::CloseProc(Entry.Value); // 活跃子进程只关闭监视句柄，自身正常退出仍会清理。
    WatchedProcesses.Empty();
}
