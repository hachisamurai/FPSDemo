// 职责：Editor抽取事务与存档回归。使用：-DemoGambleTest运行真实报价、提交与失败回滚；只操作本次GI临时进度。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Contracts/DemoGameplayContracts.h"
#include "DemoGambleTest.generated.h"
/** 仅WITH_EDITOR显式启动的隔离实机回归；不访问HTTP/账号，无正常游戏入口。 */
UCLASS(Transient,NotBlueprintable)
class ADemoGambleTest : public AActor
{
    GENERATED_BODY()
public:
    /** Editor低频状态机等待真实GE计时与页面更新；非Editor禁用Tick且不编入测试接口。 */
    ADemoGambleTest();
    /** DeltaSeconds为World秒；每步重新获取当前Pawn，不跨旅行缓存裸对象。 */
    virtual void Tick(float DeltaSeconds) override;
private:
#if WITH_EDITOR
    /** 当前Hub夹具执行三种金币正式事务、保存失败和私有棋盘回归；仅Editor调用。 */
    bool CheckCasino();
    /** Hub真实事务扩展：持续局面往返/回滚、无封顶到账及GAS训练射击。 */
    bool CheckArcade();
    /** Condition/Message为实测结果；失败立刻非零结束隔离进程。 */
    bool Check(bool Condition,const TCHAR* Message);
    /** Choice为生产白名单；Serial=-1使用当前报价，否则用于重放拒绝测试。 */
    FDemoCommandResult Place(int32 Choice,int32 Serial=INDEX_NONE);
    /** 同步UObject弱委托故障注入，只用于测试持久化失败回滚，不触碰正式文件权限。 */
    bool RejectSave();
    int32 Step=0; // 当前步骤，正常游戏永不创建。
    double Next=1; // World秒截止，等待UI帧/真实保护GE到期。
    bool bFailed=false; // 首次失败后停止后续状态修改。
#endif
};
