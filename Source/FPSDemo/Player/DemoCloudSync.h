#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "DemoCloudSync.generated.h"

/** 退出屏障的类型化结果；Complete只由最新快照全部确认并落盘后的空队列检查产生。 */
enum class EDemoCloudExitState : uint8 { Idle, LocalOnly, Pending, Complete, Failed };

/** 不含秘密的耐久同步状态；在HTTP发出前先保存发件箱，超时/重启重放同一请求。 */
UCLASS()
class FPSDEMO_API UDemoCloudState : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() int32 Version = 1; // 协议状态版本，未知版本不覆盖。
    UPROPERTY() FString PlayerId; // 已认证服务器账号，用来阻止跨账号自动合并。
    UPROPERTY() FString ClientProfileId; // 当前本地档案GUID，删除重建时重新绑定。
    UPROPERTY() FString Epoch; // 本地版本序列代号，备份回退后重新生成。
    UPROPERTY() FString BindingId; // 服务器返回的绑定ID，不是认证凭据。
    UPROPERTY() FString Outbox; // 不可变JSON请求；只含进度，绝不含Bearer或数据库密码。
    UPROPERTY() FString OutboxProfileId; // 回执所属档案，避免旧请求确认新建的本地版本。
    UPROPERTY() TArray<FString> SlotRevisions; // 三槽最后已处理云版本，字符串Int64。
    UPROPERTY() TArray<FString> SlotHashes; // 对应已处理本地快照指纹，用于发现离线修改。
};

/** 单人GI级同步编排；永久进度合并，检查点CAS冲突必须显式选择。 */
UCLASS()
class FPSDEMO_API UDemoCloudSync : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    /** Outer为所属GI；仅不含编辑器的游戏目标创建同步器，禁止编辑器误上传真实进度。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** Collection提供依赖；旧自动化标记禁用联网，绝不污染真实账号。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** 退出移除核心Ticker/取消HTTP；未确认发件箱继续留在磁盘。 */
    virtual void Deinitialize() override;
    /** 用户重试：先保留本地进度，解除普通错误退避；冲突仍需明确选择。 */
    void Retry();
    /** 玩家退出时启动最长30秒的同步屏障；复用在途请求和耐久发件箱，不更换请求ID。 */
    void BeginExitSync();
    /** 暂停时由控制器轮询，驱动同步并检查真实时间超时；不根据显示文案判断成功。 */
    EDemoCloudExitState PollExitSync();
    /** 取消退出意图，不删除发件箱、不取消后台补传；晚到HTTP不能触发退出。 */
    void CancelExitSync();
    /** HUD只读反馈，不会在绘制时执行网络操作。 */
    const FString& GetStatus() const;
    /** 是否需要显示两种冲突处理按钮。 */
    bool HasConflict() const;
    /** bKeepLocal选择本机或云端检查点；仅大厅允许，先保留两份冲突归档。 */
    void ResolveConflict(bool bKeepLocal);
private:
    /** DeltaSeconds来自CoreTicker；暂停游戏仍能同步，在游戏线程串行执行。 */
    bool Tick(float DeltaSeconds);
    /** 开始/刷新匿名会话，秘密由Online模块从本机安全存储读取。 */
    void Login();
    /** Token准备后先重放旧发件箱，再绑定/拉取，不能提前覆盖待确认操作。 */
    void Prepare();
    /** 拉取当前账号投影以恢复存档并获得CAS版本。 */
    void Pull();
    /** 收集持久化本地修改，生成新的耐久请求；无修改则闲置。 */
    void Upload();
    /** 发送固定Outbox，不根据超时重建RequestId。 */
    void SendOutbox();
    /** Verb/Path/Body为内部协议数据；CurrentStep指定本回调语义，正文不记录日志。 */
    void Request(const FString& Verb, const FString& Path, const FString& Body, int32 CurrentStep);
    /** HTTP弱UObject回调；bSuccess/Code/Body为传输结果，始终游戏线程。 */
    void Reply(bool bSuccess, int32 Code, const FString& Body);
    /** Error为可公开中文提示；bStop表示需要用户处理而不是无限重试。 */
    void Fail(const FString& Error, bool bStop = false);
    /** 云端快照Remote合并检查点；Resolution=0自动/1保留本地/2采用云端。 */
    bool MergeSlots(const TSharedPtr<class FJsonObject>& Remote, int32 Resolution = 0);
    /** 同步状态先备份再落盘；失败时禁止发送尚未持久化请求。 */
    bool Persist();
    /** 当前是否处于无活动战役的大厅，允许替换磁盘检查点。 */
    bool CanImport() const;
    UPROPERTY() TObjectPtr<UDemoCloudState> State; // GI持有持久化值，不持有世界Actor。
    UPROPERTY() TObjectPtr<class UDemoApiClient> Api; // 同GI网络客户端，HTTP取消由其负责。
    UPROPERTY() TObjectPtr<class UDemoPlayerProfile> Profile; // 永久进度依赖，无跨线程访问。
    UPROPERTY() TObjectPtr<class UDemoRunSaves> Runs; // 三槽本地缓存，下载不自动进入游戏。
    FTSTicker::FDelegateHandle Ticker; // 必须在Deinitialize移除的核心定时器句柄。
    FString Token; // 两小时Bearer仅内存存在，禁止日志/SaveGame。
    FString Status = TEXT("云存档等待连接"); // HUD用户反馈，不作为服务器状态依据。
    FString PendingStatus = TEXT("进度已保存本地，等待云端同步"); // 已同步提示失效后立即返回的只读文案。
    uint64 CheckedRunSerial = 0; // 最近完成本地槽指纹比较的序列，防轮询间隔内误显示已同步。
    FString SaveSlot = TEXT("DemoCloudState"); // 同步状态固定文件，凭据位于独立DPAPI文件。
    TSharedPtr<class FJsonObject> Conflict; // 当前冲突远端快照，归档后才能显式覆盖。
    double NextAttempt = 0; // FPlatformTime单调秒数，不受游戏暂停/时间缩放影响。
    double NextPull = 0; // 最长60秒刷新云版本，允许另一设备的进度在大厅被发现。
    int32 Attempts = 0; // 连续失败次数，退避上限60秒。
    int32 Step = 0; // 1登录/2绑定/3拉取/4上传；只有一条HTTP在途。
    bool bBusy = false; // 当前HTTP请求尚未结束，不允许并发同步。
    bool bStopped = false; // 配置/协议/磁盘问题需显式重试，游戏仍可离线运行。
    bool bDisabled = false; // 用户配置或旧自动化测试禁用本GI所有联网。
    bool bNeedPull = true; // 初次/重连/版本冲突后必须重新下载版本基线。
    bool bReady = false; // 仅结构校验完成后置true，损坏/未来版本状态不能经退出重试绕过保护。
    EDemoCloudExitState ExitState = EDemoCloudExitState::Idle; // GI游戏线程持有，HTTP只改变结果，不操作退出UI。
    double ExitDeadline = 0; // 单调时钟秒数；最长30秒等待包含登录、旧队列重放及新快照上传。
};
