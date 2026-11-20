#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h" // 真实按键夹具需要完整FKey定义。
#include "DemoPlayerStatusTest.generated.h"

/** -DemoPlayerStatusTest显式Editor单人回归；跨帧验证真实Tab/暂停/HUD分页与等级持久化。 */
UCLASS(Transient,NotBlueprintable)
class ADemoPlayerStatusTest : public AActor
{
    GENERATED_BODY()
public:
    /** 使用真实时间推进，暂停时仍Tick；普通游戏不会创建此Actor。 */
    ADemoPlayerStatusTest();
    /** DeltaSeconds为游戏帧间隔；断言与键盘事件按真实秒分帧，避免同帧按下/松开被合并。 */
    virtual void Tick(float DeltaSeconds) override;
private:
#if WITH_EDITOR
    /** Condition/Message为断言，失败终止隔离进程并记录原因。 */
    bool Check(bool Condition,const TCHAR* Message);
    /** Key为真实输入键，bDown表示按下/松开；仅本测试World本机PC。 */
    void Key(FKey Key,bool bDown);
    int32 Step=0; // 隔离测试阶段，只在断言成功后推进。
    double Next=0.; // 真实秒截止；不使用会被冻结的World计时器。
    double Started=0.; // 超时起点，防止输入或暂停故障使进程无限等待。
    bool bFailed=false; // 首个失败后停止进一步改状态。
#endif
};
