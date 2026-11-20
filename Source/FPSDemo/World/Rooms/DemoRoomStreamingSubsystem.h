// 职责：每World拥有的有界房间实例与流送生命周期。由Preparation/PC调用，不决定战斗、保存或金钱；依赖UE流送、房间锚点、服务器导航。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Containers/Ticker.h"
#include "World/Rooms/DemoRoomStreamingTypes.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "DemoRoomStreamingSubsystem.generated.h"
class ULevelStreamingDynamic; // 实例由World持有，本服务只管理自己创建的至多两个实例。
class APlayerController; // 仅服务器核对该连接对准备关卡的引擎可见性ACK。
class ADemoRoomAnchor; // 子关卡拥有的唯一作者锚点。

UCLASS()
class FPSDEMO_API UDemoRoomStreamingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 仅游戏/PIE World创建，不在编辑器资产预览加载关卡。 */
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    /** World退出精确撤销Ticker和本服务实例，不发送网络请求。 */
    virtual void Deinitialize() override;
    /** 开始异步加载；同代同请求幂等，旧代/不同请求/未卸完旧实例拒绝，Error返回原因。 */
    bool BeginPrepare(const FDemoRoomStreamRequest& Request, FString& Error);
    /** 10Hz由上层准备流程轮询；Ready提供世界锚点，Pending保留旧位置，Failed必须取消。 */
    EDemoRoomLoadStatus PollPrepared(FDemoAreaSnapshot& OutArea, FString& Error);
    /** 服务器检查远端引擎level visibility ACK；Listen本机只需服务端关卡就绪。 */
    bool IsConnectionReady(APlayerController* Controller) const;
    /** 将已就绪Pending设为Active；旧Active等相关Pawn离开其实际碰撞占地后异步卸载，位置复制迟到时继续保留。 */
    bool CommitPrepared();
    /** 仅取消本次Pending；Active继续有效，取消实例卸载完之前阻止新请求。 */
    void CancelPrepared();
    /** 上层已传送Hub后调用，释放服务拥有的所有房间；旧地图资产不受影响。 */
    void ReleaseRooms();
    /** 兼容旧区域查询：仅匹配Active.ArenaIndex时返回真实房间冻结快照。 */
    bool GetActiveArea(int32 ArenaIndex, FDemoAreaSnapshot& OutArea, FString& Error) const;
    /** 是否存在已提交活动房间；不代表Pending已就绪。 */
    bool HasActiveRoom() const;
    /** 返回本次准备的加载Level供诊断/引擎ACK，不转移所有权。 */
    class ULevel* GetPreparedLevel() const;
    /** 本机只读Level身份/槽及是否曾提交；退役/Release中保留已提交视觉到Level卸载，候选取消仍false，不改变玩法提交。 */
    bool GetRoomRenderContext(const class ULevel* Level, FName& OutRoomId, FTransform& OutTransform, bool& bOutCommitted) const;
private:
    UPROPERTY(Transient) TObjectPtr<ULevelStreamingDynamic> Pending; // 准备实例，和Active合计最多两个。
    UPROPERTY(Transient) TObjectPtr<ULevelStreamingDynamic> Active; // 当前玩家所在实例，取消不得卸载。
    UPROPERTY(Transient) TObjectPtr<ULevelStreamingDynamic> Retiring; // 被替换/取消实例，未真实移除时阻止新的Begin。
    FDemoRoomStreamRequest PendingRequest; // Pending的不可变值条件。
    FDemoRoomStreamRequest ActiveRequest; // Active的身份与旧区域索引。
    FDemoRoomStreamRequest RetiringRenderRequest; // 仅本地退役视觉身份，不复制/保存、不用于门禁或改变回收决定。
    bool bRetiringWasCommitted = false; // 旧Active退役才true；取消Pending不能获得视觉激活资格。
    FDemoAreaSnapshot PendingArea; // 成功准备后冻结，提交不会重新读作者数据。
    FDemoAreaSnapshot ActiveArea; // 活动房间世界几何快照。
    FTSTicker::FDelegateHandle CleanupTicker; // 只有待卸载期间存在，10Hz，不依赖暂停的游戏时间。
    int32 LastGeneration = 0; // 已接收最高代次，取消后也不复用名字。
    bool bPreparedReady = false; // 权威导航/客户端令牌和场景验证已通过。
    bool bReleasing = false; // Hub批量释放中，全部真实移除后解除。
    bool bRetireRequested = false; // 旧实例已提交卸载，避免重复设置状态。
    bool bStopping = false; // World销毁后禁止再添加Ticker/加载请求。
    FBox PendingCollisionBounds = FBox(ForceInit); // Pending真实阻挡几何世界AABB；未显示时暂用可信安装包络，取消亦保留安全范围。
    FBox ActiveCollisionBounds = FBox(ForceInit); // 通过准备的本实例真实阻挡几何，不能用别的地图/编辑器图标bounds替代。
    FBox RetiringBounds = FBox(ForceInit); // 退役实例实际XY占地；相关Pawn尚未离开时不卸脚下地面。
    double CleanupDeadline = 0.; // 单次回收观察最多30真实秒；卡住保留有界实例并拒绝新加载，重试会再次观察。
    /** 验证唯一锚点/标签和地面，服务器另检查必要导航路径；不扫描其他World或地图。 */
    EDemoRoomLoadStatus InspectPrepared(ADemoRoomAnchor*& OutAnchor, FDemoAreaSnapshot& OutArea, FBox& OutCollisionBounds, FString& Error) const;
    /** 只扫描给定Level的可阻挡Pawn的StaticMesh组件，输出真实世界占地；不含Billboard/灯球/Nav派生显示bounds。 */
    bool ReadCollisionBounds(const class ULevel* Level, FBox& OutBounds, FString& Error) const;
    /** 服务器导航通过时返回true；尚在构建/缺路径给原因并由上层超时，不偷偷跳过。 */
    bool IsNavigationReady(const FDemoAreaSnapshot& Area, FString& Error) const;
    /** 检查相关Pawn均已离开旧实例实际XY占地及250cm余量；换Pawn空窗仍保留地面。 */
    bool CanRetirePrevious() const;
    /** 启动本对象弱绑定Ticker；已经启动则幂等。 */
    void StartCleanupTicker();
    /** 10Hz移除已离开的旧房间并等待World真正移除Streaming对象，完成后关闭Ticker。 */
    bool PollCleanup(float DeltaSeconds);
    /** 仅对本服务记录的实例发出非阻塞卸载请求。 */
    void RequestUnload(ULevelStreamingDynamic* Instance);
    /** World不再持有实例且Level已空才确认回收，避免反复取消积压实例。 */
    bool IsRemoved(const ULevelStreamingDynamic* Instance) const;
};
