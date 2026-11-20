// 职责：Editor原型的有界同步事件与弱读取器注册。使用：Editor GA显式启用/停用；候选类与控制台命令由FPSDemoEditor模块拥有。
// 依赖：所有调用在游戏线程；不缓存World强引用、不写存档、不改变正式RunBuild目录或网络协议。
#include "Debug/DemoEditorCombatPrototype.h"
#if WITH_EDITOR
#include "Characters/DemoCharacter.h"
#include "GAS/DemoTags.h" // 原Tag符号只在Runtime模块内部读取，通过单一Editor桥按值跨DLL。
#include "Debug/DemoLog.h"

namespace
{
    FDemoEditorMeleeConfirmed MeleeEvents; // 仅Editor游戏线程的已确认近战通知，GA退出精确解绑。
    FDemoEditorShotCommitted ShotEvents; // 仅真正发射成功后通知，不等于命中，不产生伤害。
    FDemoEditorDirectConfirmed DirectEvents; // 仅Editor游戏线程的已确认直击通知，不拥有监听者。
    struct FViewEntry
    {
        TWeakObjectPtr<UObject> Owner; // 注册GA由原ASC保活，桥只弱持有。
        FDemoEditorCrossfireViewGetter Getter; // 必须CreateUObject绑定Owner，不能捕获裸对象。
    };
    TMap<TWeakObjectPtr<ADemoCharacter>,FViewEntry> Views; // 仅显式启用的Pawn，注册时修剪失效键，不跨World复用。
}

FGameplayTag DemoEditorCombatPrototype::MagnitudeTag()
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoEditorCombatPrototype::MagnitudeTag")); return DemoTags::Magnitude; }
FDemoEditorMeleeConfirmed& DemoEditorCombatPrototype::MeleeConfirmed()
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoEditorCombatPrototype::MeleeConfirmed")); return MeleeEvents; }
FDemoEditorShotCommitted& DemoEditorCombatPrototype::ShotCommitted()
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoEditorCombatPrototype::ShotCommitted")); return ShotEvents; }
FDemoEditorDirectConfirmed& DemoEditorCombatPrototype::DirectConfirmed()
{ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoEditorCombatPrototype::DirectConfirmed")); return DirectEvents; }
bool DemoEditorCombatPrototype::RegisterView(ADemoCharacter* Pawn,UObject* Owner,FDemoEditorCrossfireViewGetter Getter)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCombatPrototype::RegisterView"));
    if(!IsInGameThread()||!IsValid(Pawn)||!IsValid(Owner)||!Getter.IsBoundToObject(Owner))
    { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_VIEW_REJECT context/thread/owner")); return false; }
    for(auto It=Views.CreateIterator();It;++It) if(!It.Key().IsValid()||!It.Value().Owner.IsValid()) It.RemoveCurrent(); // 注册低频修剪，死亡/World清理通常主动注销。
    if(const FViewEntry* Existing=Views.Find(Pawn)) if(Existing->Owner.IsValid()&&Existing->Owner!=Owner)
    { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_VIEW_REJECT different active owner")); return false; }
    Views.Add(Pawn,{Owner,MoveTemp(Getter)}); return true;
}
void DemoEditorCombatPrototype::UnregisterView(ADemoCharacter* Pawn,const UObject* Owner)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCombatPrototype::UnregisterView"));
    if(!IsInGameThread()||!Pawn) return;
    const FViewEntry* Entry=Views.Find(Pawn); // 同步只借用到Remove，不跨调用栈缓存。
    if(Entry&&Entry->Owner.Get()==Owner) Views.Remove(Pawn);
}
FDemoEditorCrossfireView DemoEditorCombatPrototype::ReadView(const ADemoCharacter* Pawn)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoEditorCombatPrototype::ReadView"));
    if(!IsInGameThread()||!IsValid(Pawn)) return {};
    const FViewEntry* Entry=Views.Find(const_cast<ADemoCharacter*>(Pawn)); // 只用身份查弱键，不修改Pawn；Getter同步返回纯值，不把Map元素暴露给HUD。
    return Entry&&Entry->Owner.IsValid()&&Entry->Getter.IsBound()?Entry->Getter.Execute():FDemoEditorCrossfireView();
}
#endif
