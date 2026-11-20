// 职责：Editor网络生命周期与按需系统快照观察。使用：Editor模块安装/卸载，依赖CoreTicker和只读Win32采样；EEE故障结案后重采样默认关闭，避免阻塞游戏线程。
#include "Diagnostics/DemoEditorNetworkDiagnostics.h"
#include "Diagnostics/DemoNetworkSystemSnapshot.h"
#include "Debug/DemoNetworkDiagnostics.h"
#include "Debug/DemoLog.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"

namespace
{
    TAutoConsoleVariable<int32> SystemSampling(TEXT("Demo.NetDiag.SystemSampling"), 0,
        TEXT("Editor-only passive Windows table sampling (default off). 0 keeps lifecycle/lightweight world snapshots only.")); // EEE问题已结案；曾观察单次606ms游戏线程占用，仅专项排障时显式开启，不改变UE网络行为。
    TAutoConsoleVariable<float> SampleSeconds(TEXT("Demo.NetDiag.SampleSeconds"), 5.0f,
        TEXT("Passive snapshot interval in seconds (clamped to 1..60).")); // 默认5秒只取轻量World状态；系统表查询必须另开SystemSampling。

    /** 模块唯一观察器；全部回调在游戏线程，AddRaw不延长寿命，必须在析构前由Stop解除所有绑定。 */
    class FNetworkObserver final
    {
    public:
        /** 模块启动时安装观察；已有GEngine才订阅失败事件，不创建OnlineSubsystem。 */
        void Start()
        {
            UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::Start"));
            PreBegin = FEditorDelegates::PreBeginPIE.AddRaw(this, &FNetworkObserver::OnPreBegin);
            Begin = FEditorDelegates::BeginPIE.AddRaw(this, &FNetworkObserver::OnBegin);
            Started = FEditorDelegates::PostPIEStarted.AddRaw(this, &FNetworkObserver::OnStarted);
            PreEnd = FEditorDelegates::PrePIEEnded.AddRaw(this, &FNetworkObserver::OnPreEnd);
            End = FEditorDelegates::EndPIE.AddRaw(this, &FNetworkObserver::OnEnd);
            Finished = FEditorDelegates::ShutdownPIE.AddRaw(this, &FNetworkObserver::OnFinished);
            WorldInit = FWorldDelegates::OnPostWorldInitialization.AddRaw(this, &FNetworkObserver::OnWorldInit);
            WorldTearDown = FWorldDelegates::OnWorldBeginTearDown.AddRaw(this, &FNetworkObserver::OnWorldTearDown);
            WorldCleanup = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FNetworkObserver::OnWorldCleanup);
            WorldCleaned = FWorldDelegates::OnPostWorldCleanup.AddRaw(this, &FNetworkObserver::OnWorldCleaned);
            EngineExit = FCoreDelegates::OnEnginePreExit.AddRaw(this, &FNetworkObserver::OnEngineExit);
            if (GEngine)
            {
                NetworkFailure = GEngine->OnNetworkFailure().AddRaw(this, &FNetworkObserver::OnNetworkFailure);
                TravelFailure = GEngine->OnTravelFailure().AddRaw(this, &FNetworkObserver::OnTravelFailure);
            }
            SnapshotCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("Demo.NetDiag.Snapshot"), TEXT("Write a passive diagnostic snapshot now."),
                FConsoleCommandDelegate::CreateRaw(this, &FNetworkObserver::OnSnapshot), ECVF_Default);
            Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FNetworkObserver::Tick), 1.0f);
            DEMO_NET_DIAG(TEXT("Observer.Start"), nullptr, TEXT("schema=1 passive=1"));
            OnSnapshot(); // 启动即记录基线，不能等用户停止PIE才首次采样。
        }

        /** 模块卸载时先记录边界，再移除所有原生委托、Ticker与命令；不等待网络或访问GI子系统。 */
        void Stop()
        {
            UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::Stop"));
            DEMO_NET_DIAG(TEXT("Observer.Stop"), nullptr, TEXT("removeCallbacks=1"));
            FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
            if (SnapshotCommand) IConsoleManager::Get().UnregisterConsoleObject(SnapshotCommand, false);
            SnapshotCommand = nullptr;
            FEditorDelegates::PreBeginPIE.Remove(PreBegin); FEditorDelegates::BeginPIE.Remove(Begin);
            FEditorDelegates::PostPIEStarted.Remove(Started); FEditorDelegates::PrePIEEnded.Remove(PreEnd);
            FEditorDelegates::EndPIE.Remove(End); FEditorDelegates::ShutdownPIE.Remove(Finished);
            FWorldDelegates::OnPostWorldInitialization.Remove(WorldInit); FWorldDelegates::OnWorldBeginTearDown.Remove(WorldTearDown);
            FWorldDelegates::OnWorldCleanup.Remove(WorldCleanup); FWorldDelegates::OnPostWorldCleanup.Remove(WorldCleaned);
            FCoreDelegates::OnEnginePreExit.Remove(EngineExit);
            if (GEngine) { GEngine->OnNetworkFailure().Remove(NetworkFailure); GEngine->OnTravelFailure().Remove(TravelFailure); }
        }

    private:
        /** Event固定事件名，bSimulating是Editor传入的SIE标记；不保存World，单轮编号辅助关联多实例PIE。 */
        void RecordPIE(const TCHAR* Event, bool bSimulating)
        {
            UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::RecordPIE"));
            DEMO_NET_DIAG(Event, nullptr, FString::Printf(TEXT("play=%u simulate=%d"), PlaySerial, bSimulating));
        }
        /** bSimulating由PreBeginPIE提供；开始一轮新编号，之后各事件沿用，World另有pie实例编号。 */
        void OnPreBegin(bool bSimulating) { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnPreBegin")); ++PlaySerial; RecordPIE(TEXT("PIE.PreBegin"), bSimulating); OnSnapshot(); }
        /** bSimulating来自BeginPIE；只记录即将创建试玩World的边界。 */
        void OnBegin(bool bSimulating) { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnBegin")); RecordPIE(TEXT("PIE.Begin"), bSimulating); }
        /** bSimulating来自PostPIEStarted；只标记试玩初始化完成。 */
        void OnStarted(bool bSimulating) { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnStarted")); RecordPIE(TEXT("PIE.Started"), bSimulating); }
        /** bSimulating来自PrePIEEnded；退出前采样，不参与或改变退出顺序。 */
        void OnPreEnd(bool bSimulating) { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnPreEnd")); RecordPIE(TEXT("PIE.PreEnd"), bSimulating); OnSnapshot(); }
        /** bSimulating来自EndPIE；该委托本身不等于Socket已关闭，须结合引擎主日志。 */
        void OnEnd(bool bSimulating) { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnEnd")); RecordPIE(TEXT("PIE.End"), bSimulating); }
        /** bSimulating来自ShutdownPIE；随后仍保留模块级采样直到Editor退出。 */
        void OnFinished(bool bSimulating) { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnFinished")); RecordPIE(TEXT("PIE.Shutdown"), bSimulating); OnSnapshot(); }

        /** World是委托期间借用对象；Values为引擎初始化选项，本诊断不改变这些选项。 */
        void OnWorldInit(UWorld* World, const UWorld::InitializationValues Values)
        { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnWorldInit")); if (World && World->IsGameWorld()) DEMO_NET_DIAG(TEXT("World.Initialized"), World, TEXT("")); }
        /** World来自开始拆除委托，切图和停止PIE均可能到达，不能仅用此事件判断玩家主动退出。 */
        void OnWorldTearDown(UWorld* World)
        { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnWorldTearDown")); if (World && World->IsGameWorld()) DEMO_NET_DIAG(TEXT("World.BeginTearDown"), World, TEXT("")); }
        /** World短期借用；bSessionEnded/bCleanupResources由引擎提供，记录真实清理语义，不自行推断。 */
        void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
        { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnWorldCleanup")); if (World && World->IsGameWorld()) DEMO_NET_DIAG(TEXT("World.Cleanup"), World, FString::Printf(TEXT("sessionEnded=%d resources=%d"), bSessionEnded, bCleanupResources)); }
        /** World可能已失去GI/驱动；仅读取仍有效对象，两个标记保持与引擎委托一致。 */
        void OnWorldCleaned(UWorld* World, bool bSessionEnded, bool bCleanupResources)
        { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnWorldCleaned")); if (World && World->IsGameWorld()) DEMO_NET_DIAG(TEXT("World.Cleaned"), World, FString::Printf(TEXT("sessionEnded=%d resources=%d"), bSessionEnded, bCleanupResources)); }
        /** 整个Editor退出的时间标记，不关停任何游戏或系统网络服务。 */
        void OnEngineExit() { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnEngineExit")); DEMO_NET_DIAG(TEXT("Engine.PreExit"), nullptr, TEXT("")); }

        /** World/Driver只在委托期间借用；Type是引擎枚举；Error可能含地址或凭据，只记录长度。 */
        void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
        { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnNetworkFailure")); DEMO_NET_DIAG(TEXT("Engine.NetworkFailure"), World, FString::Printf(TEXT("type=%d failedDriver=%u errorChars=%d"), static_cast<int32>(Type), IsValid(Driver) ? Driver->GetUniqueID() : 0, Error.Len())); }
        /** World借用；Type只记录枚举值，Error仅长度，禁止把包含旅行Options的正文写入诊断文件。 */
        void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
        { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnTravelFailure")); DEMO_NET_DIAG(TEXT("Engine.TravelFailure"), World, FString::Printf(TEXT("type=%d errorChars=%d"), static_cast<int32>(Type), Error.Len())); }

        /** 手工命令和生命周期调用；读取当前游戏World，不保存裸指针，不创建GI/OSS/驱动。 */
        void OnSnapshot()
        {
            UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FNetworkObserver::OnSnapshot"));
            if (!DemoNetworkDiagnostics::IsEnabled()) return;
            DEMO_NET_DIAG(TEXT("Snapshot"), nullptr, FString::Printf(TEXT("play=%u systemSampling=%d intervalSeconds=%.1f"), PlaySerial,
                SystemSampling.GetValueOnGameThread(), FMath::Clamp(SampleSeconds.GetValueOnGameThread(), 1.0f, 60.0f)));
            if (GEngine)
                for (const FWorldContext& Context : GEngine->GetWorldContexts()) // 引擎持有的只读上下文，迭代期间不执行旅行或异步工作。
                    if (Context.World() && Context.World()->IsGameWorld()) DEMO_NET_DIAG(TEXT("World.Snapshot"), Context.World(), TEXT(""));
            if (SystemSampling.GetValueOnGameThread() != 0) DemoNetworkSystemSnapshot::Capture();
            LastSample = FPlatformTime::Seconds();
        }
        /** DeltaSeconds为CoreTicker增量；使用单调时间限频，暂停World/结束PIE后仍能采样，返回true保持观察。 */
        bool Tick(float DeltaSeconds)
        {
            UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] FNetworkObserver::Tick"));
            if (DemoNetworkDiagnostics::IsEnabled() && FPlatformTime::Seconds() - LastSample >= FMath::Clamp(SampleSeconds.GetValueOnGameThread(), 1.0f, 60.0f)) OnSnapshot();
            return true;
        }

        uint32 PlaySerial = 0; // 本Editor进程中开始PIE的轮次；各World仍通过pie/wid区分，不是玩家身份。
        double LastSample = 0; // 上次完整采样的单调秒数；不使用受暂停/时间倍率影响的World时间。
        FDelegateHandle PreBegin, Begin, Started, PreEnd, End, Finished; // Editor PIE委托句柄，Stop逐个解绑。
        FDelegateHandle WorldInit, WorldTearDown, WorldCleanup, WorldCleaned; // World生命周期委托，不持有World引用。
        FDelegateHandle NetworkFailure, TravelFailure, EngineExit; // Engine失败/退出观察句柄，不拦截事件。
        FTSTicker::FDelegateHandle Ticker; // CoreTicker游戏线程回调，模块卸载前移除。
        IConsoleObject* SnapshotCommand = nullptr; // ConsoleManager拥有命令对象，本类只持注册句柄，Stop注销。
    };

    TUniquePtr<FNetworkObserver> Observer; // Editor模块唯一所有者；Shutdown先Stop再释放，避免AddRaw悬空。
}

void DemoEditorNetworkDiagnostics::Startup()
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoEditorNetworkDiagnostics::Startup"));
    if (Observer) { UE_LOG(LogFPSDemo, Log, TEXT("NETDIAG observer already installed")); return; }
    Observer = MakeUnique<FNetworkObserver>(); Observer->Start();
}

void DemoEditorNetworkDiagnostics::Shutdown()
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoEditorNetworkDiagnostics::Shutdown"));
    if (!Observer) return;
    Observer->Stop(); Observer.Reset();
}
