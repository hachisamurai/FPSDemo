#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Debug/DemoLog.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    /** 队列拥有测试子进程；超时也清理句柄并终止仅本测试创建的进程。 */
    class FStandaloneSaveProbeCommand final : public IAutomationLatentCommand
    {
    public:
        /** InTest为自动化队列拥有的有效对象；Handle转交查询/停止职责，Pid是刚创建的子进程。 */
        FStandaloneSaveProbeCommand(FAutomationTestBase* InTest, FProcHandle Handle, uint32 Pid)
            : Test(InTest), Process(Handle), ProcessId(Pid)
        { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FStandaloneSaveProbeCommand pid=%u"), ProcessId); }

        /** 自动化释放命令时关闭句柄；失败时也不能遗留本测试启动的游戏窗口/进程。 */
        virtual ~FStandaloneSaveProbeCommand() override
        {
            UE_LOG(LogFPSDemo, Log, TEXT("[CALL] ~FStandaloneSaveProbeCommand"));
            if (Process.IsValid() && FPlatformProcess::IsProcRunning(Process)) FPlatformProcess::TerminateProc(Process);
            FPlatformProcess::CloseProc(Process);
        }

        /** 真实子进程产生存档后使用与UE Stop相同的TerminateProc，等待父Editor补做清理。 */
        virtual bool Update() override
        {
            UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] FStandaloneSaveProbeCommand::Update"));
            if (StartedAt == 0) StartedAt = FPlatformTime::Seconds();
            if (FPlatformTime::Seconds() - StartedAt > 60) { Test->AddError(TEXT("Standalone cleanup timeout")); return true; }
            TArray<FString> Files; // 仅枚举该真实子进程的存档，测试自身不删除文件来掩盖清理缺陷。
            TArray<FString> ProfileFiles; // 解锁档案与检查点一起进入验证集。
            const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames")); // 当前项目固定输出目录。
            IFileManager::Get().FindFiles(Files, *(Directory / FString::Printf(TEXT("DemoEditorRun_%u_*.sav"), ProcessId)), true, false);
            IFileManager::Get().FindFiles(ProfileFiles, *(Directory / FString::Printf(TEXT("DemoEditorProfile_%u_*.sav"), ProcessId)), true, false);
            if (!bStopped)
            {
                if (!FPlatformProcess::IsProcRunning(Process)) { Test->AddError(TEXT("Standalone exited before forced-stop fixture ready")); return true; }
                if (Files.Num() < 2 || ProfileFiles.IsEmpty()) return false; // 等待真实检查点主档/备份和解锁档案实际落盘。
                Files.Append(ProfileFiles);
                FileCount = Files.Num();
                UE_LOG(LogFPSDemo, Display, TEXT("EDITOR_STANDALONE_FORCE_STOP pid=%u files=%d"), ProcessId, FileCount);
                FPlatformProcess::TerminateProc(Process); // 仅终止本测试拥有的子进程；故意跳过GI Deinitialize以重现编辑器Stop。
                bStopped = true;
                StartedAt = FPlatformTime::Seconds();
                return false;
            }
            if (FPlatformProcess::IsProcRunning(Process) || !Files.IsEmpty() || !ProfileFiles.IsEmpty()) return false;
            Test->TestTrue(TEXT("Real forced-stop fixture included main, backup and profile"), FileCount >= 3);
            UE_LOG(LogFPSDemo, Display, TEXT("EDITOR_STANDALONE_SAVE_CLEANUP_SUCCESS: parent editor removed %d child-owned files after TerminateProc"), FileCount);
            return true;
        }
    private:
        FAutomationTestBase* Test; // 自动化框架拥有，命令结束前有效，只在游戏线程访问。
        FProcHandle Process; // 本命令拥有，析构关闭；该进程仅由本测试启动。
        uint32 ProcessId; // 真实子进程ID，用于限定测试文件枚举范围。
        bool bStopped = false; // 只强制终止一次，随后等待正式清理Ticker。
        int32 FileCount = 0; // 终止前实际落盘文件数量，避免空样本假通过。
        double StartedAt = 0; // 启动/清理阶段真实时间上限秒，不使用World时间。
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoEditorStandaloneSaveTest, "FPSDemo.Editor.StandaloneTemporarySaves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters为未使用的自动化参数；专属隐藏子进程验证独立运行被Stop终止后清理。 */
bool FDemoEditorStandaloneSaveTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FDemoEditorStandaloneSaveTest::RunTest"));
    if (!GEditor || GEditor->PlayWorld) { AddError(TEXT("Test requires idle editor")); return false; }
    const FString Executable = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/Win64/UnrealEditor-Cmd.exe")); // 当前UE5.4 Windows编辑器程序，不调用shell。
    const FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()); // 本项目路径独立加引号以支持空格。
    const FString Log = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Logs/EditorStandaloneSaveChild.log")); // 子进程诊断日志保留，不属于玩家数据。
    const FString Args = FString::Printf(TEXT("\"%s\" /Game/Whitebox/Maps/L_ThreeSector_Whitebox -game -nullrhi -unattended -nosound -nosplash -DemoSessionTest -DemoVictoryContinuationTest -abslog=\"%s\" -LogCmds=\"LogFPSDemo Log\""), *Project, *Log); // 复用真实新建存档入口，在通关前强制停止。
    uint32 ProcessId = 0; // CreateProc填写的实际子进程ID；不是手工构造的清理授权。
    FProcHandle Handle = FPlatformProcess::CreateProc(*Executable, *Args, false, true, true, &ProcessId, 0, nullptr, nullptr); // 隐藏测试进程，无交互窗口；成功后由latent命令拥有。
    if (!Handle.IsValid()) { AddError(TEXT("Cannot launch editor standalone test child")); return false; }
    FEditorDelegates::BeginStandaloneLocalPlay.Broadcast(ProcessId); // 与真实UE启动同一委托，验证模块已注册而非直接调用删除实现。
    ADD_LATENT_AUTOMATION_COMMAND(FStandaloneSaveProbeCommand(this, Handle, ProcessId));
    return true;
}
#endif
