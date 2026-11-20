// 职责：GM拥有的权威准备门禁；公开值发布到GS，意图经PC网关，绝不保存准备状态。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/Coop/Ready/DemoTeamReadyTypes.h"
#include "DemoTeamReadyComponent.generated.h"
class ADemoPlayerController;
class ADemoCharacter;

UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoTeamReadyComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick；只有存在准备成员时以2Hz检测会话/Avatar失效，全部取消立即停。 */
    UDemoTeamReadyComponent();
    /** 阶段、模式或成员变化调用；先撤销全部意图，再重建公开投影。 */
    void InvalidateAll(const FString& Reason);
    /** 本人的交易/装备/核心/关闭终端调用，不改变其他玩家已经确认的意图。 */
    void InvalidatePlayer(ADemoPlayerController* Player, const FString& Reason);
    /** 服务初始化、恢复及交互事件后更新公开状态，不隐式设置任何人准备。 */
    void Refresh();
    /** Player为拥有者身份；bReady是明确目标，Revision/Level绑定当前备战页面，返回可读拒绝原因。 */
    bool SetReady(ADemoPlayerController* Player, bool bReady, int32 ExpectedRevision, int32 ExpectedLevel, FString& Error);
    /** 最后一道权威门禁；重新检查完整在线目录、Avatar/GAS/生命/核心/奖励/终端，拒绝原因可显示。 */
    bool ValidateDeparture(FString& Error);
    /** World卸载清Timer与弱引用，不发RPC、不访问销毁中的GS。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    /** 返回当前在线登记控制器；包含暂缺Pawn的成员，不能借此跳过准备。 */
    TArray<ADemoPlayerController*> GetMembers() const;
    /** bRequireTerminal仅多人检查已批准的下一关会话，单人保留Flow原门禁。 */
    FString GetBlockReason(ADemoPlayerController* Player, bool bRequireTerminal) const;
    /** 个人进度/核心版本和实际装配索引的服务端签名；防止绕过UI的合法旧接口保留过期Ready。 */
    FString GetPreparationFingerprint(ADemoPlayerController* Player) const;
    /** 构造值投影，不读取客户端菜单字段；bRecheck会撤销已失效的意图。 */
    void Publish(bool bRecheck);
    /** 2Hz有界看门狗，仅已有Ready且仍备战时活跃；入口只留高频埋点。 */
    void RecheckReadyMembers();
    /** 仅当前Pawn的准备意图；Pawn替换、死亡或过期会话不继承。 */
    TMap<TWeakObjectPtr<ADemoPlayerController>, TWeakObjectPtr<ADemoCharacter>> ReadyPlayers;
    TMap<TWeakObjectPtr<ADemoPlayerController>, FString> ReadyFingerprints; // 与ReadyPlayers同生命周期，不复制或保存。
    int32 Revision = 1; // World内版本，只在意图/条件改变时递增。
    FString LastReason; // 当前撤销/配置变化的公开说明，不含私有进度。
    FString ContextKey; // RunId/阶段/关号/模式/难度/在线成员集合的防漏失效指纹，仅服务器持有。
    FTimerHandle CheckTimer; // UObject绑定，EndPlay和无人准备时取消。
};
