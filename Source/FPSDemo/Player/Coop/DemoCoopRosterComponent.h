// 职责：房主固定队伍目录与每人检查点事务。使用：GameMode/RunFlow协调登记和恢复；真实在线状态仍归PS组件。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Contracts/DemoGameplayContracts.h"
#include "DemoCoopRosterComponent.generated.h"
class ADemoCharacter;
class ADemoPlayerController;

/** 房主 World 的固定队伍目录；个人真值在 PS，离线成员只持检查点值。 */
UCLASS()
class FPSDEMO_API UDemoCoopRosterComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无 Tick；登录、离线和检查点时显式调用。 */
    UDemoCoopRosterComponent();
    /** Player/Key 为拥有者握手；最多四个固定成员，重复键不能顶替在线玩家。 */
    bool RegisterPlayer(ADemoPlayerController* Player, const FString& Key);
    /** Key为LAN连接声明的稳定键；创建Pawn之前拒绝在线重复身份和非固定成员满队请求。 */
    bool CanAdmit(const FString& Key) const;
    /** Player 离开时冻结当前安全值；战斗离线不保存半场收益。 */
    void UnregisterPlayer(ADemoPlayerController* Player);
    /** bAliveOnly 排除阵亡观战者；返回本 World 同步借用列表，不跨帧保存。 */
    TArray<ADemoCharacter*> GetPlayers(bool bAliveOnly = false) const;
    /** 在线登记控制器含暂缺Pawn者；准备门禁禁止过滤初始化中的成员。 */
    TArray<ADemoPlayerController*> GetControllers() const;
    /** Player 是本机房主才可操作队伍流程；远端客户端不能通过索引伪装。 */
    bool IsLeader(const ADemoPlayerController* Player) const;
    /** Key 查本次已登记身份，空值代表尚未握手。 */
    FString GetPlayerKey(const ADemoCharacter* Player) const;
    /** Out收集在线/离线完整候选；bPublishCache=false仅准备磁盘事务，不修改重连缓存。默认true保留Hub离线即时冻结。 */
    bool Capture(TArray<FDemoCoopPlayerCheckpoint>& Out, bool bPublishCache = true);
    /** 整槽成功后发布同次已提交成员值，不重读PS/库存或发Gameplay事件；无槽开发兼容须由调用者明确标为内存提交。 */
    void PublishSavedMembers(const TArray<FDemoCoopPlayerCheckpoint>& Members);
    /** Members为整队检查点；全部预生成装备后提交，任一失败回滚在线成员，成功才替换离线缓存。 */
    bool Restore(const TArray<FDemoCoopPlayerCheckpoint>& Members);
    /** 战役结束时清理缓存中离线成员的临时来源；在线值随后由 PS 采集覆盖。 */
    void ResetTemporaryMembers();
    /** 安全区切换到旧/无尽模式时清在线PS及全部缓存核心；不修改钱包、遗物、属性或装备，恢复事务期间拒绝。 */
    void ResetBuilds();
    /** Player/Key 为同步借用身份；Out 不含 Actor 或网络对象。 */
    static bool CapturePlayer(const ADemoCharacter* Player, const FString& Key, FDemoCoopPlayerCheckpoint& Out);
    /** Player/Data 为服务器已校验恢复值；自身校验并在失败时恢复旧个人值。 */
    static bool ApplyPlayer(ADemoCharacter* Player, const FDemoCoopPlayerCheckpoint& Data);
    /** Data 完整字段验证，保存/恢复共用；不访问可变世界。 */
    static bool ValidatePlayer(const FDemoCoopPlayerCheckpoint& Data);
private:
    // 控制器仅弱引用；固定身份不依赖登录顺序、PlayerId 或房主 Player0。
    TMap<TWeakObjectPtr<ADemoPlayerController>, FString> Connections;
    // 已提交/Hub离线即时冻结的重连值；磁盘候选捕获不得提前覆盖，避免保存失败后留下未提交枪权。
    UPROPERTY() TArray<FDemoCoopPlayerCheckpoint> SavedMembers;
    bool bRestoring = false; // 同步整队事务锁，属性/装备回调不得重入注册、保存或第二次恢复。
};
