// 职责：近战动画中的有效挥刀窗口标记与只读解析。使用：在TP标准AnimSequence放置一个此NotifyState；组件起刀时冻结区间，依赖现有权威时钟/扫掠，不依赖Teleport取样的通知回调。
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "DemoMeleeTraceWindow.generated.h"

class UAnimSequenceBase;

/** 仅作者时间窗，不持有Pawn/队列或共享可变攻击状态；支持多人共用同一动画资产。 */
UCLASS(meta=(DisplayName="Demo Melee Trace Window"))
class FPSDEMO_API UDemoMeleeTraceWindow : public UAnimNotifyState
{
    GENERATED_BODY()
public:
    /** 原生构造只设置编辑器显示，不在预览或客户端触发碰撞。 */
    UDemoMeleeTraceWindow();
    /** 编辑器通知轨道上的可读名称；不执行伤害，也不改变播放时钟。 */
    virtual FString GetNotifyName_Implementation() const override;
    /** 从标准TP动画读取唯一标记的起止源秒并输出0..1相位；缺失/重复/越界拒绝。
     * Sequence为本次同步只读资产，OutWindow仅成功发布，Error返回失败原因；组件每刀起始调用一次，不在Sweep子步遍历通知。 */
    static bool ReadWindow(const UAnimSequenceBase* Sequence,FVector2D& OutWindow,FString& Error);
};
