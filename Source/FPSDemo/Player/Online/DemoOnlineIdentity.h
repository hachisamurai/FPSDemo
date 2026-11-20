#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Containers/Ticker.h"
#include "DemoOnlineIdentity.generated.h"

/** 本机连接方式；GI跨地图保留，绝不根据客户端URL修改服务器模式。 */
enum class EDemoRoomNetwork : uint8 { LAN, PublicEOS };

/** EOS登录与提供者选择。编辑器只能LAN；不拥有战斗Actor，不替代云存档API。 */
UCLASS(Config=Game)
class FPSDEMO_API UDemoOnlineIdentity : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    /** Collection为本GI容器；只装配状态并记录Editor诊断边界，不在启动时创建EOS或发起登录。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** GI退出解绑弱UObject委托；Editor记录解绑和释放边界且不输出账号，不等待浏览器或网络。 */
    virtual void Deinitialize() override;
    /** Network是UI选项；仅离线大厅可切换，房间服务先确认没有命名会话或在途操作。 */
    bool SelectNetwork(EDemoRoomNetwork Network);
    /** 显式打开Account Portal；成功后由房间服务继续搜索，禁止重复登录。 */
    bool Login();
    /** 仅无会话大厅可退出账号；不清除已有存档，不在退出前切换恢复身份。 */
    bool Logout();
    /** Tick驱动状态只读；未完成配置时返回可操作的原因，不打印平台凭据。 */
    bool IsPublic() const;
    bool IsReady() const;
    bool IsBusy() const;
    const FString& GetStatus() const;
    /** Region用于目录筛选；配置为地区代码，不承诺已测得网络延迟。 */
    const FString& GetRegion() const;
    /** 当前选定提供者的会话接口；EOS未就绪返回空，不静默退回NULL。 */
    IOnlineSessionPtr GetSessions();
    /** 本人在线UniqueId；仅Public且登录有效时返回，LocalPlayer用于UE握手。 */
    FUniqueNetIdPtr GetLocalNetId() const;
    /** EOS PUID规范化为GUID格式，复用固定队伍存档字段；空值禁止恢复。 */
    FString GetOnlinePlayerKey() const;
    /** Id为EOS SDK生成的UniqueId；输出仅是关联键，远端还必须验证传输身份。 */
    static FString PlayerKeyFromNetId(const FUniqueNetId& Id);
    /** Address必须是引擎从P2P连接取得的远端地址，不能来自客户端自报字段。 */
    static bool MatchTransportIdentity(const FString& Address, const FUniqueNetId& Id);
    /** Address为OSS解析的目标；拒绝URL选项、任意地图和不规范EOS通道。 */
    static bool IsEOSAddress(const FString& Address);
private:
    /** 检查本地开关和选定Artifact必填项；失败保留明确UI提示，不创建EOS。 */
    bool CheckConfiguration();
    /** DeltaSeconds为真实时间；有界登录等待，无法取消的SDK请求超时后隔离至回调。 */
    bool TickIdentity(float DeltaSeconds);
    /** OSS在游戏线程触发；参数为本地用户、结果、身份及脱敏前错误，禁止打印Error正文。 */
    void OnLoginComplete(int32 LocalUser, bool bSuccess, const FUniqueNetId& Id, const FString& Error);
    /** LocalUser/bSuccess为本机注销结果；错误时拒绝继续使用残留登录。 */
    void OnLogoutComplete(int32 LocalUser, bool bSuccess);
    /** LocalUser/Old/New/Id来自OSS；凭据过期立即撤销Ready，由房间服务断开。 */
    void OnLoginChanged(int32 LocalUser, ELoginStatus::Type Old, ELoginStatus::Type New, const FUniqueNetId& Id);

    UPROPERTY(Config) bool bEnablePublicRooms = false; // 填好EOS配置后显式启用；默认无平台凭据也能单人/LAN。
    UPROPERTY(Config) FString Region = TEXT("Asia"); // 本产品自定义地区筛选，1..16个ASCII字母/数字/连字符。
    IOnlineIdentityPtr Identity; // 选定EOS实例持有的接口；Deinitialize显式解绑后释放。
    EDemoRoomNetwork SelectedNetwork = EDemoRoomNetwork::LAN; // 默认LAN，防止编辑器意外创建外部服务。
    FString Status; // 脱敏的本机提示，不复制到其他玩家。
    FString OnlineKey; // 登录成功后的稳定PUID关联键；注销/身份丢失时清空。
    double Deadline = 0.; // 单调秒时间；登录120秒、注销15秒，超时不启动重叠请求。
    bool bLoggingIn = false; // SDK没有Login取消接口；超时仍等原回调，防止串号。
    bool bLoggingOut = false; // 注销未完成时不允许重新登录。
    bool bAbandoned = false; // 超时或离开公网页后迟到登录必须注销，不能恢复旧意图。
    bool bStopping = false; // GI销毁后任何异步通知都不能变更UI/旅行。
    FDelegateHandle LoginHandle, LogoutHandle, ChangedHandle; // 本GI拥有的OSS弱绑定委托。
    FTSTicker::FDelegateHandle Ticker; // 游戏线程真实时钟，独立于World暂停。
};
