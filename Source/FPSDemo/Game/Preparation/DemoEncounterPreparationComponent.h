// 职责：GM拥有的入场资源握手屏障。使用Flow在传送前EnsureReady，冻结队伍/准备版本并等PC回执；只协调加载，不生成敌人、不发奖、不保存。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Player/Coop/Preparation/DemoEncounterPreparationTypes.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "DemoEncounterPreparationComponent.generated.h"
class ADemoPlayerController;
class APawn;
struct FDemoSpawnPlan;
struct FDemoAreaSnapshot;

UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoEncounterPreparationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无常规Tick，只有等待客户端时10Hz真实时间看门狗；挂载GM，状态只投影到GS。 */
    UDemoEncounterPreparationComponent();
    /** Key/Plan冻结本关；RoomId为空走旧ArenaIndex，否则服务器先加载地图；全员就绪才输出Area允许原Flow提交。 */
    bool EnsureReady(const FString& Key, const FDemoSpawnPlan& Plan, int32 ArenaIndex, FName RoomId, FDemoAreaSnapshot& Area, FString& Error);
    /** Player为拥有者RPC真实连接；仅接受当前代数/冻结成员一次回执，客户端不提交位置、资源或传送权限。 */
    void ReportReady(ADemoPlayerController* Player, int32 Generation, bool bReady, FName Code);
    /** 结束旧准备并撤销客户端回调，Reason为有限公开原因；不清任何账本或写检查点。 */
    void Cancel(const FString& Reason);
    /** Flow真正提交Combat前消费屏障；保留本地加载资源，旧回执不能再触发第二次出发。 */
    bool Consume();
    /** 仅等待期间拒绝新加入；人数/Avatar变化由看门狗取消，不自动重算新阵容。 */
    bool IsPending() const;
    /** World卸载取消Ticker与请求，不复活GS或在销毁期间触发出发。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** DeltaSeconds为CoreTicker秒；校验原阶段/准备修订/队伍/终端，30秒超时，全部就绪后最多一次调用Flow重验。 */
    bool Poll(float DeltaSeconds);
    /** 更新有限公开人数和状态，事件驱动复制，不逐帧强制网络更新。 */
    void Publish(const FString& Status);
    /** 只收集冻结敌人组/现有武器的/Game入口，最多64项；失败不提交任何部分请求。 */
    bool CollectResources(const FDemoSpawnPlan& Plan, TArray<FSoftObjectPath>& Out, FString& Error) const;
    /** 只驱动本次服务器房间准备；ready后一次发送各PC请求，Pending继续现有看门狗，Failed取消本次事务。 */
    EDemoRoomLoadStatus AdvanceServerPreparation(FString& Error);
    /** 全员业务ACK以及引擎Level可见性ACK同时通过；旧区域不要求流送连接ACK。 */
    bool AreAllClientsReady() const;
    UPROPERTY(Transient) TArray<FSoftObjectPath> PendingResources; // 冻结资源入口，等待服务器地图期间保留，最多64项。
    UPROPERTY(Transient) FDemoRoomStreamRequest RoomRequest; // 本次服务器批准的地图身份，不能由客户端或计时器重选。
    FDemoAreaSnapshot PreparedArea; // 准备完成后冻结真实地图/旧区域快照供Flow提交。
    bool bRequestsSent = false; // 服务器显示/导航/锚点通过后才发送一次客户端加载，防MissingLevelPackage。
    FString ActiveKey; // 本次内容/关号键，仅权威World，不复制或写存档。
    TMap<TWeakObjectPtr<ADemoPlayerController>, bool> Members; // 冻结最多4人及本次资源确认，Actor离线不被强持有。
    TMap<TWeakObjectPtr<ADemoPlayerController>, TWeakObjectPtr<APawn>> Avatars; // 同连接换Pawn也取消旧准备，不继承旧碰撞确认。
    TSet<TWeakObjectPtr<ADemoPlayerController>> RequiredTerminals; // 开始时已使用出发终端者，关闭/离开后取消；保留底层开发入口兼容。
    FTSTicker::FDelegateHandle Watchdog; // 弱UObject绑定，取消/Consume/EndPlay准确移除。
    double Deadline = 0.; // 单调真实秒，服务器等待最多30秒，不因暂停卡住。
    int32 Generation = 0; // World内单调正代数，耗尽拒绝新请求防复用实例名；旅行重建，无跨World意义。
    int32 Stage = 0; // 当前准备的目标关号。
    int32 ReadyRevision = 0; // TeamReady开始时版本；交易/核心/模式变化后拒绝自动出发。
    uint32 PhaseRevision = 0; // World阶段版本，旧关握手不能带入下一关。
    bool bActive = false; // true表示等待或全员确认待提交，Consume后旧回执失效。
    bool bReadyToCommit = false; // 只在全部回执后设置，出发仍由Flow完整重验。
    bool bStopping = false; // EndPlay后拒绝新请求，不向销毁中的World写UI。
    FName PendingFailure; // 客户端同步失败先排队，下次Ticker统一取消，避免发送请求的调用栈内重入清空成员。
};
