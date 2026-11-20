// 职责：仅Editor的隔离战斗原型事实桥与只读视图。使用：真实扣血后的WITH_EDITOR hook广播，Editor GA订阅；运行包体不声明任何入口。
// 依赖：原Pawn/PS/CombatSession与同步委托；不保存玩法ID、钱包或权限，不向网络复制候选状态。
#pragma once
#include "CoreMinimal.h"

#if WITH_EDITOR
#include "GameplayTagContainer.h" // 跨模块只按值返回原伤害幅值Tag，不导出整个DemoTags静态符号表。
class ADemoCharacter;
class ADemoEnemy;

/** 本机交叉火力候选表现；Remaining使用World游戏秒，不是已保存/已批准的正式核心。 */
struct FPSDEMO_API FDemoEditorCrossfireView
{
    bool bActive=false; // 仅当前本机Pawn的候选GA仍在本场运行时为true。
    float Remaining=0.f; // 有效破势凭据剩余秒，0表示尚未近战起手或已经消费。
    FString TargetName; // 已确认受伤目标的公开名称，不带目标或玩家私有数据。
    float ChargeDamage=0.f; // 本次已冻结非元素冲击预算HP；实际结算仍经原Health GE。
    int32 ConsumedCharges=0; // 本次激活消费次数，调试只读，不形成奖励/存档。
    int32 ConfirmedImpacts=0; // 真正造成额外HP损失的冲击次数，零伤/无敌不冒充成功。
};

// 所有参数仅在游戏线程同步广播栈借用；Source/Enemy不由桥强持有，Source只能是原扣血流程所属Avatar。
DECLARE_MULTICAST_DELEGATE_FourParams(FDemoEditorMeleeConfirmed,ADemoCharacter*,ADemoEnemy*,float,bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDemoEditorShotCommitted,ADemoCharacter*,const FGuid&);
DECLARE_MULTICAST_DELEGATE_SevenParams(FDemoEditorDirectConfirmed,ADemoCharacter*,ADemoEnemy*,const FGuid&,float,bool,bool,bool);
DECLARE_DELEGATE_RetVal(FDemoEditorCrossfireView,FDemoEditorCrossfireViewGetter);

namespace DemoEditorCombatPrototype
{
    /** Editor候选/测试跨DLL取得原Runtime伤害幅值Tag；按值返回已注册Tag，不按字符串再注册或导出所有正式标签。 */
    FPSDEMO_API FGameplayTag MagnitudeTag();
    /** 近战实际HP损失后通知：参数为Source、Enemy、源端近战HP基数、是否致死；没有伤害权限。 */
    FPSDEMO_API FDemoEditorMeleeConfirmed& MeleeConfirmed();
    /** 原射击事务已成功扣弹/提交：Source、ShotId；仅允许在近战凭据之后真正发射的枪消费，旧飞行弹不预埋。 */
    FPSDEMO_API FDemoEditorShotCommitted& ShotCommitted();
    /** 原始枪械实际HP损失后通知：Source、Enemy、ShotId、实际HP损失、是否致死、是否派生、同枪该目标首次；不改原伤害。 */
    FPSDEMO_API FDemoEditorDirectConfirmed& DirectConfirmed();
    /** Owner必须为弱UObject绑定的候选GA；只给本Pawn注册一个读取器，跨World或换Avatar的旧键不会命中新Pawn。 */
    FPSDEMO_API bool RegisterView(ADemoCharacter* Pawn,UObject* Owner,FDemoEditorCrossfireViewGetter Getter);
    /** 只移除仍属于Owner的本Pawn读取器；旧GA结束不能误删新实例。 */
    FPSDEMO_API void UnregisterView(ADemoCharacter* Pawn,const UObject* Owner);
    /** HUD借用当前本机Pawn查询；失效/未启用返回空值，不读盘、不授予权限、不强持有GA。 */
    FPSDEMO_API FDemoEditorCrossfireView ReadView(const ADemoCharacter* Pawn);
}
#endif
