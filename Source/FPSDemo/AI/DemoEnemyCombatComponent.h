#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoEnemyCombatComponent.generated.h"

/** 权威端单一仲裁入口；沿用近战/俯冲执行器，表现只消费它输出的方向。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoEnemyCombatComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 不独立Tick，由Enemy初始化完成后的Actor Tick按顺序驱动。 */
    UDemoEnemyCombatComponent();
    /** DeltaSeconds为World帧秒数；仅服务器选择活目标、推进一个互斥攻击及移动。 */
    void Update(float DeltaSeconds);
    /** Reason为诊断键；先清意图和计时器再取消GA，允许EndAbility同步回调但禁止重入启动。 */
    void Cancel(FName Reason);
    /** 返回本帧有效活目标；服务器首次直接调用技能时按距离解析，不拥有Pawn。 */
    class ADemoCharacter* ResolveTarget();
    /** 取消栈内拒绝所有新技能，用于抵御同步GAS结束回调重入。 */
    bool IsCancelling() const;
private:
    TWeakObjectPtr<class ADemoCharacter> Target; // 仅当前World存活战斗目标，绝不持有跨旅行Pawn。
    bool bCancelling=false; // 游戏线程取消栈重入保护，不复制或保存。
};
