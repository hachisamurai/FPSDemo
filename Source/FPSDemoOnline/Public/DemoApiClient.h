#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "DemoApiClient.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDemoOnline, Log, All);
// 回调参数依次为传输成功、HTTP状态、响应正文；只在游戏线程执行，禁止日志打印正文。
DECLARE_DELEGATE_ThreeParams(FDemoApiReply, bool, int32, const FString&);

/** GI拥有HTTP客户端；串行发送，退出解绑取消，不持有游戏场景对象。 */
UCLASS(Config=Game)
class FPSDEMOONLINE_API UDemoApiClient : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    /** Outer为GI外部对象；WITH_EDITOR构建永不创建网络子系统，PIE/Editor Standalone均只用本地数据。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** Collection为GI容器；读取配置但不自动联网。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** GI退出时取消异步请求，防止旧世界收到回调。 */
    virtual void Deinitialize() override;
    /** Verb/Path为代码定义的协议路由；Body/Token只在内存中，请勿打印。Reply须弱绑定UObject。 */
    bool Send(const FString& Verb, const FString& Path, const FString& Body, const FString& Token, FDemoApiReply Reply);
    /** 停止当前HTTP，不调用上层回调；退出或显式重连使用。 */
    void Cancel();
    /** 读取或首次创建DPAPI身份；OutId/OutSecret仅供匿名认证，Secret不得进入SaveGame。 */
    bool LoadIdentity(FString& OutId, FString& OutSecret);
    /** 最近一次凭据失败的脱敏中文原因，供UI区分读盘、解密和首次创建失败；成功后为空。 */
    const FString& GetIdentityError() const;
    /** 返回配置开关；关闭后保持本地游戏，不发HTTP。 */
    bool IsEnabled() const;
private:
    /** Stage为固定日志键，Message为用户提示，Path为非秘密文件路径，SystemError为Win32错误码或0；不输出凭据内容。 */
    bool IdentityFailure(const TCHAR* Stage, const TCHAR* Message, const FString& Path, uint32 SystemError = 0);
    /** Request/Response由HTTP模块共享拥有；此UObject绑定在游戏线程处理结束，bSucceeded为传输结果。 */
    void Complete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded);
    // 默认仅访问本机独立后端；远端必须HTTPS，绝不是MongoDB连接串。
    UPROPERTY(Config) FString BaseUrl = TEXT("http://127.0.0.1:5087");
    // 请求总超时秒数，Initialize约束为2..30；失败由业务同步器指数退避。
    UPROPERTY(Config) float TimeoutSeconds = 8.f;
    // 可在离线发行版关闭；默认自动同步，后端不可用不阻塞游戏。
    UPROPERTY(Config) bool bEnabled = true;
    // 单个在途HTTP，由GI持有到回调/取消；从不复制到Actor。
    FHttpRequestPtr ActiveRequest;
    // 当前请求的弱UObject回调，HTTP结束前有效，Deinitialize主动解绑。
    FDemoApiReply PendingReply;
    // 本GI最新凭据错误；不持久化、不包含账号秘密，用户可从日志定位实际Saved路径。
    FString IdentityError;
};
