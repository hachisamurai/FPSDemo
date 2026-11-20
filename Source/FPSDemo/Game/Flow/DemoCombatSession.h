#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoCombatSession.generated.h"

/** 单次服务器战斗的撤销凭据；伤害上下文只弱持有，不再查询具体 GameMode。 */
UCLASS()
class FPSDEMO_API UDemoCombatSession : public UObject
{
    GENERATED_BODY()
public:
    /** World/RunId/Level 为已提交战斗的身份；只能初始化一次，客户端禁止激活。 */
    bool Initialize(UWorld* World, const FString& RunId, int32 Level);
    /** 阶段离开 Combat 前同栈撤销，晚到的 Timer、弹丸和 DOT 立即失效。 */
    void Invalidate();
    /** Owner为弱UObject拥有者；Cleanup必须CreateUObject绑定本人，单会话每对象一个幂等撤销入口。 */
    bool RegisterCleanup(UObject* Owner, FSimpleDelegate Cleanup);
    /** World为当前权威World；仅在创建攻击时查询一次，不在逐帧伤害路径查GameMode。 */
    static UDemoCombatSession* FindActive(UWorld* World);
    /** 高频只读检查，不拥有 World，不将同地图重开的旧战斗视为有效。 */
    bool IsActive() const;
    /** RunId/Level 为冻结身份，只能匹配仍有效的本次战斗。 */
    bool Matches(const FString& RunId, int32 Level) const;
private:
    TWeakObjectPtr<UWorld> SessionWorld; // 不延长旧 World 生命周期。
    FString CampaignId; // 本次初始化冻结的战役身份。
    int32 LevelNumber = 0; // 正关号，0 表示未初始化。
    bool bActive = false; // 只从初始化变 true，撤销后不可重用。
    TMap<TWeakObjectPtr<UObject>, FSimpleDelegate> Cleanups; // 弱绑定不延长弹丸/敌人寿命；撤销时先移走表，防止同步重入。
    uint32 Registrations = 0; // 每128次注册清理已销毁对象，长无尽关不积累历史弹丸弱条目。
};
