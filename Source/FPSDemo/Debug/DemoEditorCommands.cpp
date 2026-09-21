// 整个注册和回调仅在Editor目标编译；不依赖可被更改的运行时bool或ECVF_Cheat来保护包体。
#if WITH_EDITOR
#include "Debug/DemoLog.h"
#include "Player/DemoPlayerProfile.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

namespace
{
    /** Args必须为空；World由当前控制台传入，不搜索其他PIE实例；Output只在同步调用内借用。 */
    void UnlockAll(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoEditorCommands::UnlockAll"));
        UGameInstance* Instance = World ? World->GetGameInstance() : nullptr; // 只处理命令所属试玩，不访问编辑器预览World或其他会话。
        UDemoPlayerProfile* Profile = Instance ? Instance->GetSubsystem<UDemoPlayerProfile>() : nullptr; // GI持有直到本次试玩停止。
        if (!Args.IsEmpty() || !World || !World->IsGameWorld() || World->GetNetMode() != NM_Standalone || !Profile)
        {
            UE_LOG(LogFPSDemo, Warning, TEXT("DEBUG_UNLOCK_ALL rejected: requires standalone editor game world and no arguments"));
            Output.Log(TEXT("Demo.Debug.UnlockAll: 请在单人编辑器试玩的游戏控制台输入，不带参数。"));
            return;
        }
        Profile->DebugUnlockAllForSession();
        Output.Log(TEXT("已解锁全部枪械和弹药；前往终端装配。仅本次编辑器试玩有效，停止后清空。"));
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice UnlockAllCommand( // 模块静态注册，卸载时自动注销；静态函数无对象捕获和异步生命周期。
        TEXT("Demo.Debug.UnlockAll"), TEXT("Editor only: unlock all weapons and ammo for the current standalone play session."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&UnlockAll));
}
#endif
