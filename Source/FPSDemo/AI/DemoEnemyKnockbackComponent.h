// 职责：敌人服务器权威受击位移。使用：伤害确认后Apply，Enemy Tick在AI前Advance；依赖敌人碰撞根/战斗门禁，复用Actor移动复制，不创建物理模拟或独立Tick。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoEnemyKnockbackComponent.generated.h"
class ADemoEnemy;
/** 敌人当前位移唯一结束回执；Enemy同步借用，Reason区分Completed/Blocked/取消，不自行造成伤害。 */
DECLARE_DELEGATE_TwoParams(FDemoKnockbackFinished,ADemoEnemy*,FName);

/** 与武器/招式解耦的短时平面位移；值状态仅服务器拥有，客户端只接收宿主位置。 */
UCLASS(ClassGroup=(Demo),meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoEnemyKnockbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无独立Tick/网络状态；由Enemy显式仲裁，避免AI追击与击退同时改位置。 */
    UDemoEnemyKnockbackComponent();
    /** Direction为权威世界平面推力方向，Distance为0..600cm，Duration为0.05..2秒；Finished为可选弱UObject结束回执，成功替换旧击退、不叠加冲量。 */
    bool Apply(const FVector& Direction,float Distance,float Duration,FDemoKnockbackFinished Finished=FDemoKnockbackFinished());
    /** DeltaSeconds为World帧秒，暂停不推进；本帧消费击退即返回true，调用者必须跳过AI，包括完成/碰墙帧。 */
    bool Advance(float DeltaSeconds);
    /** Reason为稳定日志键；死亡/阶段结束/World销毁调用，丢弃未执行位移，不发布伤害或奖励。 */
    void Cancel(FName Reason);
private:
    FVector PushDirection=FVector::ZeroVector; // 本次服务器归一化平面方向，不保存攻击者引用。
    float PushDistance=0.f; // 小怪本次最大距离cm，每次Apply覆盖而非累加；Boss由目标门禁完全拒绝。
    float PushDuration=.25f; // 本次冻结时长秒，运行中修改Details不影响已提交运动。
    float Elapsed=0.f; // World累计秒，积分缓出曲线，使低帧率总距离不超过配置值。
    bool bActive=false; // 只由服务器Apply/Advance/Cancel写入，不复制或保存。
    FDemoKnockbackFinished Completion; // 本次最多一次结束回执；取消先清成员再同步回调，禁止悬空lambda捕获。
};
