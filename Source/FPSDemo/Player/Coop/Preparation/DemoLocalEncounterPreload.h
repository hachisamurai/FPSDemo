// 职责：PC本地资源异步预载与落脚点确认。使用拥有者可靠请求，完成后仅回传代数/成功码；依赖StreamableManager和本World碰撞，无玩法推进权限。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Player/Coop/Preparation/DemoEncounterPreparationTypes.h"
#include "DemoLocalEncounterPreload.generated.h"
struct FStreamableHandle;

UCLASS()
class FPSDEMO_API UDemoLocalEncounterPreload : public UActorComponent
{
    GENERATED_BODY()
public:
    /** PC默认子对象；只在准备期间启用真实时间Ticker，不按帧常驻工作。 */
    UDemoLocalEncounterPreload();
    /** Request为服务器冻结的合法资源白名单；替换请求前撤销旧句柄，首次完成不直接推进战斗。 */
    void BeginPreload(const FDemoEncounterPreloadRequest& Request);
    /** Generation仅取消对应旧请求；服务器取消和World卸载均精确移除回调，不向服务器补发取消完成。 */
    void CancelPreload(int32 Generation);
    /** 服务器提交代数必须匹配已成功回执；客户端激活候选地图，脚下旧地图由World服务延迟卸载。 */
    void CommitPreload(int32 Generation);
    /** 服务器全员已回Hub后通知；资源句柄释放，地图退役仍等本地位置复制避免落空。 */
    void ResetRooms();
    /** World卸载停止异步/真实时间回调，释放本PC资源保活，不清其他玩家或全局资源。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** 游戏线程Streamable完成，Generation为绑定时冻结代数；旧请求即使迟到也不能确认新请求。 */
    void HandleAssetsLoaded(int32 Generation);
    /** 10Hz真实时间等待资源和本地地面，暂停不冻结超时；false自动撤销Ticker。 */
    bool PollReadiness(float DeltaSeconds);
    /** 只发送一次拥有者回执，Code只允许本模块固定错误名，不发送资源路径或磁盘信息。 */
    void Report(bool bReady, FName Code);
    /** 统一释放本实例拥有的句柄/Ticker；不会调用完成RPC，不能清全局AssetManager。 */
    void Stop();
    FDemoEncounterPreloadRequest ActiveRequest; // 当前本地有限请求，World卸载清理，不写档。
    TSharedPtr<FStreamableHandle> LoadHandle; // 保活当前场资源到下一次预载/卸载，避免异步完成立即被GC。
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> RetainedResources; // 现成资源快速路径同样强保活，最多64个入口；不依赖裸ResolveObject指针活过GC。
    FTSTicker::FDelegateHandle PollTicker; // 仅当前准备活跃，绑定弱UObject并在Stop移除。
    double Deadline = 0.; // 单调真实秒，最多30秒，不能因单人暂停永久等待。
    bool bAssetsLoaded = false; // 完成回调已运行且所有资源入口ResolveObject有效。
    bool bReported = false; // 每代数只回报一次，迟到/重复回调无网络洪泛。
    bool bReadyReported = false; // 只有成功ACK才能消费commit；失败回执不能被误认作入场就绪。
    bool bCommitted = false; // 已激活地图不再由Stop取消，改由World房间服务管理退役。
};
