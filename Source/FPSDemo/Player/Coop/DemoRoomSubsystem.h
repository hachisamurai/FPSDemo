// 职责：合作房间发现与版本筛选。使用：由本地房间界面调用，协议版本必须与加入参数和服务器门禁一致。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Player/Online/DemoOnlineIdentity.h" // UI仅选择提供者，EOS登录由独立GI服务拥有。
#include "DemoRoomSubsystem.generated.h"

/** 本机异步连接状态；发现与服务器战局阶段分离，不复制。 */
enum class EDemoRoomState : uint8 { Idle, Searching, OpeningHost, Creating, Joining, Travelling, Hosting, Connected, Closing, Failed };

/** UI只读房间投影，不暴露连接地址或OSS对象；刷新后旧Revision立即失效。 */
struct FDemoRoomView
{
    FString Name; // 房主提供的显示名，截断后显示，不能作为命令执行。
    FString Stage; // 当前安全区/战斗状态的人类可读摘要。
    int32 Players = 0; // 广播时在线人数，最终以服务器PreLogin为准。
    int32 PingMs = INDEX_NONE; // LAN实测毫秒；EOS目录没有实测Ping时为-1，UI显示未测。
    bool bJoinable = false; // 安全区且还有连接名额；固定成员资格由服务器复核。
};

/** GI拥有房间发现/连接生命周期；身份服务选择NULL/EOS，不负责经济或战斗。 */
UCLASS()
class FPSDEMO_API UDemoRoomSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    /** Collection为GI容器；绑定弱UObject回调和真实时钟，不受暂停影响；Editor记录初始化World，诊断不创建会话。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** GI结束解绑所有OSS/引擎委托；Editor分步记录取消/销毁的请求返回，不能视为异步完成；不等待网络或启动旅行。 */
    virtual void Deinitialize() override;
    /** 仅单机大厅搜索兼容版本的房间；Public还需登录，操作期间拒绝重入；Editor记录搜索请求是否受理，不输出查询身份。 */
    bool Search();
    /** Network为UI明确选择；仅无会话且无未完成SDK操作时切换，清空旧列表。 */
    bool SelectNetwork(EDemoRoomNetwork Network);
    /** 公网页登录后自动搜索；无会话时才能退出账号，取消页面会撤销自动搜索意图。 */
    void LoginOrLogout();
    /** 搜索后尝试首个兼容且开放的房间；不自动创建房间或覆盖存档。 */
    bool QuickJoin();
    /** PC由权威GameMode提供；同步真实EOS会话成员，LAN不依赖云注册。 */
    void RegisterMember(class APlayerController* Player);
    void UnregisterMember(class APlayerController* Player);
    /** 当前网络类型和可操作性供UI只读；Ready不表示已有游戏连接。 */
    bool IsPublic() const;
    bool IsReady() const;
    /** Index/Revision来自当前列表；旧列表、战斗房间或满房不能发起加入。 */
    bool Join(int32 Index, uint32 Revision);
    /** RawAddress为玩家输入的域名或IP（可选:端口）；仅非公网模式下在单机大厅发起直连，复用受限IP旅行与40秒超时回收。 */
    bool JoinDirect(const FString& RawAddress, FString& OutError);
    /** 校验并规范化直连主机地址（支持域名、IPv4、[IPv6]:端口与裸IPv6），拒绝EOS地址、协议头、路径、查询参数与非法端口。 */
    static bool ValidateDirectAddress(const FString& RawAddress, FString& OutNormalized, FString& OutError);
    /** 本次运行内存中保留的最近一次直连地址输入；跨列表刷新保留，不写盘、不复制。 */
    const FString& GetDirectAddressDraft() const;
    void SetDirectAddressDraft(const FString& InAddress);
    /** Slot为0..2；bCreate仅明确确认空槽时使用，已有非安全区检查点不能开房。 */
    bool HostSlot(int32 Slot, bool bCreate);
    /** 当前单机安全区转换为所选提供者的房间；保留选中检查点。 */
    bool HostCurrentHub();
    /** 关闭浏览器取消搜索；连接中不可静默关闭，应调用Leave回大厅。 */
    void CancelSearch();
    /** 断开本人并清理本机Session；房主调用会关闭房间，不删除检查点。 */
    void Leave();
    /** Reason为拥有者握手最终失败/超时；退出失败连接并在大厅恢复可操作的房间页。 */
    void RegistrationFailed(const FString& Reason);
    /** UI无副作用只读接口；Revision用于防止刷新后的旧行点击。 */
    const TArray<FDemoRoomView>& GetRooms() const;
    uint32 GetRevision() const;
    EDemoRoomState GetState() const;
    const FString& GetStatus() const;
    bool IsBusy() const;
    /** 连接失败回到本地大厅后，MenuFlow消费一次以重新打开房间页。 */
    bool ConsumeBrowserReturn();
private:
    /** 当前World解析OSS，PIE必须按World隔离；已有操作持有接口直到完成；Editor只记录显式业务触发，不主动预热OSS。 */
    bool ResolveSessions();
    /** 清理一次性委托，取消后的迟到回执不能复活页面或再次旅行。 */
    void ClearOperationDelegates();
    /** Message为玩家提示；完整连接原因由调用方写日志，不在UI泄漏地址。 */
    void Fail(const FString& Message, bool bReturnToLobby);
    /** 销毁本地命名会话，完成前禁止新搜索/开房/加入；Editor区分请求返回和真正回调，允许NULL同步完成。 */
    void CloseSession();
    /** DeltaSeconds为真实时钟间隔；超时、Host就绪和广告刷新统一在游戏线程执行。 */
    bool TickService(float DeltaSeconds);
    /** OSS回调参数为对应会话名/结果；均使用CreateUObject弱绑定，无裸PC捕获。 */
    void OnFound(bool bSuccess);
    void OnCreated(FName SessionName, bool bSuccess);
    void OnJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
    /** SessionName/bSuccess来自OSS结果；Editor连被拒绝的迟到回调也记录状态位，停止后不再通过GI读取World。 */
    void OnDestroyed(FName SessionName, bool bSuccess);
    /** 广告更新的最终异步结果；失败延后重试，不能把请求已受理当成已发布。 */
    void OnUpdated(FName SessionName, bool bSuccess);
    /** World/Driver/Type/Error为引擎失败通知，只处理本GI，避免多个PIE实例串扰。 */
    void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
    /** World/Type/Error为引擎旅行失败；回到可操作大厅并保留提示。 */
    void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
    /** 房主权威阶段/人数变化才更新广播；预登录仍是最终准入依据。 */
    void UpdateAdvertisement();

    IOnlineSessionPtr Sessions; // 当前World对应的OSS接口，GI退出时显式释放。
    TSharedPtr<FOnlineSessionSearch> SearchRequest; // OSS异步使用的查询，取消前不能释放。
    TArray<FOnlineSessionSearchResult> Results; // 与Rooms一一对应的本轮兼容结果，不持有Actor。
    TArray<FDemoRoomView> Rooms; // UI投影只在搜索完成或清空时更新。
    uint32 Revision = 0; // 每次刷新/取消递增，防止点击过期行。
    EDemoRoomState State = EDemoRoomState::Idle; // 本机操作状态，独立于GS战局阶段。
    FString Status; // UI摘要，连接错误后保持至下一次明确操作。
    FString DirectAddressDraft; // 本次运行内存中的域名/IP直连输入草稿，不写磁盘、不跨进程持久化。
    FString LobbyMap; // 本次离开大厅前记录的本地地图名，绝不来自远端广播。
    double Deadline = 0; // 单调时间秒；0表示没有等待中的操作。
    bool bReturnBrowser = false; // 失败旅行完成后的一次性本地页面恢复意图。
    bool bStopping = false; // Deinitialize之后回调不再触发旅行。
    bool bSearchAfterLogin = false; // 用户本次登录意图；关闭页面必须清除，迟到授权不能搜索。
    bool bQuickJoin = false; // 本次搜索完成后自动选择房间；取消后清除。
    bool bDiscardPending = false; // EOS请求不可安全中断，取消后等待原回调清理，禁止同类重入。
    bool bSessionQuarantined = false; // EOS销毁失败时不伪造成功；重启进程前禁止复用残留会话。
    bool bUpdating = false; // 最多一个目录更新在途，真实完成后才确认已发布快照。
    bool bCloseAfterUpdate = false; // EOS目录更新也必须排空后再Destroy，防止旧回调写入同名新会话。
    double NextAdvertiseAt = 0.; // 目录更新最少间隔3秒；失败时退避，避免逐帧请求云服务。
    double UpdateDeadline = 0.; // 单次目录更新15秒；超时仍等待原回调，不发重叠更新。
    int32 PendingPhase = INDEX_NONE, PendingPlayers = INDEX_NONE; // 在途广告快照，回调成功才提交。
    int32 AdvertisedLevel = INDEX_NONE, AdvertisedDifficulty = INDEX_NONE; // 展示元数据改变也需要发布。
    int32 PendingLevel = INDEX_NONE, PendingDifficulty = INDEX_NONE; // 在途关卡/难度元数据。
    int32 AdvertisedPhase = INDEX_NONE; // 上次成功广播的公开阶段，用于避免每帧UpdateSession。
    int32 AdvertisedPlayers = INDEX_NONE; // 上次成功广播人数，最多4人。
    FDelegateHandle FindHandle, CreateHandle, JoinHandle, DestroyHandle; // 本GI拥有的一次性OSS委托句柄。
    FDelegateHandle UpdateHandle; // 会话更新完成委托，销毁前解绑。
    FDelegateHandle NetworkHandle, TravelHandle; // 引擎全局委托，回调内还需检查GI归属。
    FTSTicker::FDelegateHandle Ticker; // 0.5秒真实时间调度，GI销毁前移除。
};
