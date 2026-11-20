// 职责：玩家受击快照冻结、接收序号去重和Profile合法性的无World纯合同专项。
// 使用：Automation RunTests FPSDemo.Feedback.PlayerDamageContract；只调用生产PostAttributeChange/AcceptSequence/Validate，依赖两类显式friend测试入口。
// 边界：所有可变UObject均为独立瞬态实例，不改CDO、不执行GE/Pre或PostExecute、不创建World/PIE/游戏；不代表真实扣血、死亡、UI或联机验收。
#include "Misc/AutomationTest.h"
#include "GAS/DemoAttributeSet.h"
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackComponent.h"
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackProfile.h"
#include "UObject/UObjectGlobals.h"
#include "Debug/DemoLog.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoPlayerDamageContractTest,"FPSDemo.Feedback.PlayerDamageContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters由Automation提供且不作为路径/配置输入；RunTest同步构造独立瞬态对象，手动执行帧只用于触达生产纯快照逻辑。 */
bool FDemoPlayerDamageContractTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FDemoPlayerDamageContractTest::RunTest"));
    const FGameplayAttribute HealthAttribute=UDemoAttributeSet::GetHealthAttribute(); // 生产反射字段身份；不建立ASC、Actor或World。

    UDemoAttributeSet* Damage=NewObject<UDemoAttributeSet>(GetTransientPackage(),NAME_None,RF_Transient); // 独立非CDO属性实例，HP属性本身仍保持构造值。
    UDemoAttributeSet::FHealthExecuteFrame& DamageFrame=Damage->HealthExecuteStack.AddDefaulted_GetRef(); // 模拟已获准Health执行帧，跳过需要真实Avatar/World的PreExecute。
    DamageFrame.Before=100.f; DamageFrame.bDamage=true; DamageFrame.Cue.MaxHealth=100.f;
    Damage->PostAttributeChange(HealthAttribute,100.f,88.f); // 仅触发生产写后快照，不调用SetHealth或应用GE。
    TestTrue(TEXT("First damage write captures its frame"),Damage->HealthExecuteStack.Last().bCaptured);
    TestEqual(TEXT("First damage write freezes actual loss 12"),Damage->HealthExecuteStack.Last().Cue.HealthLost,12.f);
    TestEqual(TEXT("First damage write freezes health after 88"),Damage->HealthExecuteStack.Last().Cue.HealthAfter,88.f);
    TestFalse(TEXT("Nonfatal write remains nonfatal"),Damage->HealthExecuteStack.Last().Cue.bFatal);
    Damage->PostAttributeChange(HealthAttribute,88.f,94.f); // 同步死亡委托/其他回调可能治疗，已采集帧不能被后续写入覆盖。
    TestEqual(TEXT("Later healing preserves frozen loss 12"),Damage->HealthExecuteStack.Last().Cue.HealthLost,12.f);
    TestEqual(TEXT("Later healing preserves frozen health after 88"),Damage->HealthExecuteStack.Last().Cue.HealthAfter,88.f);
    TestEqual(TEXT("Snapshot contract did not execute a real HP mutation"),Damage->GetHealth(),100.f);

    UDemoAttributeSet* Fatal=NewObject<UDemoAttributeSet>(GetTransientPackage(),NAME_None,RF_Transient); // 致死用例不复用上一帧状态。
    UDemoAttributeSet::FHealthExecuteFrame& FatalFrame=Fatal->HealthExecuteStack.AddDefaulted_GetRef(); // 独立低血执行帧，实际损失应被Before限制。
    FatalFrame.Before=8.f; FatalFrame.bDamage=true; FatalFrame.Cue.MaxHealth=100.f;
    Fatal->PostAttributeChange(HealthAttribute,8.f,0.f);
    TestTrue(TEXT("Zero after damage freezes fatal outcome"),Fatal->HealthExecuteStack.Last().Cue.bFatal);
    TestEqual(TEXT("Fatal damage freezes actual loss 8"),Fatal->HealthExecuteStack.Last().Cue.HealthLost,8.f);
    TestEqual(TEXT("Fatal damage freezes zero health after"),Fatal->HealthExecuteStack.Last().Cue.HealthAfter,0.f);
    Fatal->PostAttributeChange(HealthAttribute,0.f,100.f); // 原Avatar同步重置/复活写入不能抹掉已成立的致死事实。
    TestTrue(TEXT("Synchronous reset cannot erase fatal outcome"),Fatal->HealthExecuteStack.Last().Cue.bFatal);
    TestEqual(TEXT("Synchronous reset cannot erase fatal loss 8"),Fatal->HealthExecuteStack.Last().Cue.HealthLost,8.f);
    TestEqual(TEXT("Synchronous reset cannot replace fatal health after"),Fatal->HealthExecuteStack.Last().Cue.HealthAfter,0.f);

    UDemoAttributeSet* Rescued=NewObject<UDemoAttributeSet>(GetTransientPackage(),NAME_None,RF_Transient); // 只验证Guard已标记后的采集合同，不执行依赖真实World的救援逻辑。
    UDemoAttributeSet::FHealthExecuteFrame& RescueFrame=Rescued->HealthExecuteStack.AddDefaulted_GetRef(); // 原始攻击已被Guard限制在1HP，仍保留其救援结果位。
    RescueFrame.Before=1.f; RescueFrame.bDamage=true; RescueFrame.Cue.bRescued=true;
    Rescued->PostAttributeChange(HealthAttribute,1.f,1.f);
    TestTrue(TEXT("Guarded one-HP write still captures its result frame"),Rescued->HealthExecuteStack.Last().bCaptured);
    TestEqual(TEXT("Guarded one-HP rescue never invents damage loss"),Rescued->HealthExecuteStack.Last().Cue.HealthLost,0.f);
    TestEqual(TEXT("Guarded one-HP rescue preserves actual health after"),Rescued->HealthExecuteStack.Last().Cue.HealthAfter,1.f);
    TestTrue(TEXT("Guarded one-HP rescue preserves rescue result flag"),Rescued->HealthExecuteStack.Last().Cue.bRescued);
    TestFalse(TEXT("Guarded one-HP rescue is not fatal"),Rescued->HealthExecuteStack.Last().Cue.bFatal);

    UDemoAttributeSet* NonDamage=NewObject<UDemoAttributeSet>(GetTransientPackage(),NAME_None,RF_Transient); // 治疗与初始化使用第三个独立实例。
    UDemoAttributeSet::FHealthExecuteFrame& HealingFrame=NonDamage->HealthExecuteStack.AddDefaulted_GetRef(); // 非伤害修改器仍可占据执行栈顶。
    HealingFrame.Before=20.f; HealingFrame.bDamage=false;
    NonDamage->PostAttributeChange(HealthAttribute,20.f,70.f);
    TestFalse(TEXT("Healing frame never captures a damage fact"),NonDamage->HealthExecuteStack.Last().bCaptured);
    TestEqual(TEXT("Healing frame leaves damage loss empty"),NonDamage->HealthExecuteStack.Last().Cue.HealthLost,0.f);
    TestEqual(TEXT("Healing frame leaves damage after snapshot empty"),NonDamage->HealthExecuteStack.Last().Cue.HealthAfter,0.f);
    NonDamage->HealthExecuteStack.Reset();
    NonDamage->PostAttributeChange(HealthAttribute,0.f,100.f); // 无GE执行帧的初始化写入不应创建受击帧。
    TestEqual(TEXT("Initialization without execution frame creates no capture"),NonDamage->HealthExecuteStack.Num(),0);
    UDemoAttributeSet::FHealthExecuteFrame& InitializationFrame=NonDamage->HealthExecuteStack.AddDefaulted_GetRef(); // 即便初始化写入显式位于非伤害帧也必须旁路。
    InitializationFrame.Before=0.f; InitializationFrame.bDamage=false;
    NonDamage->PostAttributeChange(HealthAttribute,0.f,100.f);
    TestFalse(TEXT("Initialization frame never captures a damage fact"),NonDamage->HealthExecuteStack.Last().bCaptured);

    UDemoAttributeSet* Nested=NewObject<UDemoAttributeSet>(GetTransientPackage(),NAME_None,RF_Transient); // 嵌套用例独立构造，不借前例的已捕获标记。
    UDemoAttributeSet::FHealthExecuteFrame& OuterFrame=Nested->HealthExecuteStack.AddDefaulted_GetRef(); // 外层伤害尚未匹配写入。
    OuterFrame.Before=100.f; OuterFrame.bDamage=true;
    UDemoAttributeSet::FHealthExecuteFrame& InnerHealing=Nested->HealthExecuteStack.AddDefaulted_GetRef(); // 内层非伤害遮住外层，模拟同步嵌套Health修改器。
    InnerHealing.Before=88.f; InnerHealing.bDamage=false;
    Nested->PostAttributeChange(HealthAttribute,88.f,94.f);
    TestFalse(TEXT("Nested healing does not capture its own damage"),Nested->HealthExecuteStack[1].bCaptured);
    TestFalse(TEXT("Nested healing cannot consume outer uncaptured frame"),Nested->HealthExecuteStack[0].bCaptured);
    TestEqual(TEXT("Nested healing cannot write outer loss"),Nested->HealthExecuteStack[0].Cue.HealthLost,0.f);
    Nested->HealthExecuteStack.Pop(EAllowShrinking::No); // 只移除测试手动压入的内层帧，不调用依赖ASC的PostExecute。
    Nested->PostAttributeChange(HealthAttribute,100.f,88.f);
    TestTrue(TEXT("Outer damage captures after nested healing frame leaves"),Nested->HealthExecuteStack[0].bCaptured);
    TestEqual(TEXT("Outer damage retains its own loss 12"),Nested->HealthExecuteStack[0].Cue.HealthLost,12.f);
    UDemoAttributeSet::FHealthExecuteFrame& InnerDamage=Nested->HealthExecuteStack.AddDefaulted_GetRef(); // 进一步确认后续内层伤害也只写自己的栈顶。
    InnerDamage.Before=88.f; InnerDamage.bDamage=true;
    Nested->PostAttributeChange(HealthAttribute,88.f,80.f);
    TestEqual(TEXT("Nested damage captures its own loss 8"),Nested->HealthExecuteStack[1].Cue.HealthLost,8.f);
    TestEqual(TEXT("Nested damage leaves outer loss 12 unchanged"),Nested->HealthExecuteStack[0].Cue.HealthLost,12.f);
    TestEqual(TEXT("Nested damage leaves outer health after 88 unchanged"),Nested->HealthExecuteStack[0].Cue.HealthAfter,88.f);

    UDemoPlayerDamageFeedbackComponent* Sequence=NewObject<UDemoPlayerDamageFeedbackComponent>(GetTransientPackage(),NAME_None,RF_Transient); // 仅访问不依赖Owner/World的生产接收序号窗口，不调用BeginPlay或RPC。
    TestFalse(TEXT("Reserved sequence zero is rejected"),Sequence->AcceptSequence(0));
    TestTrue(TEXT("First nonzero sequence is accepted"),Sequence->AcceptSequence(100));
    TestFalse(TEXT("Repeated newest sequence is rejected"),Sequence->AcceptSequence(100));
    TestTrue(TEXT("Forward gap is accepted"),Sequence->AcceptSequence(102));
    TestTrue(TEXT("Unseen out-of-order sequence is accepted"),Sequence->AcceptSequence(101));
    TestFalse(TEXT("Repeated out-of-order sequence is rejected"),Sequence->AcceptSequence(101));
    TestTrue(TEXT("Unseen sequence at age 63 is inside window"),Sequence->AcceptSequence(39));
    TestFalse(TEXT("Unseen sequence at age 64 is outside window"),Sequence->AcceptSequence(38));
    TestEqual(TEXT("Out-of-order receive never rewinds newest sequence"),Sequence->LastSequence,uint32(102));
    TestTrue(TEXT("Next sequence advances window by one"),Sequence->AcceptSequence(103));
    TestFalse(TEXT("Previously accepted age 63 falls outside after advance"),Sequence->AcceptSequence(39));
    TestTrue(TEXT("Large forward gap starts a fresh bounded window"),Sequence->AcceptSequence(200));
    TestEqual(TEXT("Large gap discards stale bits instead of shifting by 64 or more"),Sequence->Seen,uint64(1));
    TestTrue(TEXT("Fresh large-gap window accepts unseen age 63"),Sequence->AcceptSequence(137));
    TestFalse(TEXT("Fresh large-gap window rejects age 64"),Sequence->AcceptSequence(136));
    TestTrue(TEXT("Exact forward gap 64 starts another fresh window"),Sequence->AcceptSequence(264));
    TestEqual(TEXT("Exact gap 64 resets bitmap to newest bit"),Sequence->Seen,uint64(1));

    UDemoPlayerDamageFeedbackComponent* Wrapped=NewObject<UDemoPlayerDamageFeedbackComponent>(GetTransientPackage(),NAME_None,RF_Transient); // 回绕测试独立窗口，不能依靠上例先验LastSequence。
    const uint32 LastSerial=std::numeric_limits<uint32>::max(); // uint32网络序号上限；0是保留值，合法发号从最大值跳到1。
    TestTrue(TEXT("First sequence may start near uint32 wrap"),Wrapped->AcceptSequence(LastSerial-1));
    TestTrue(TEXT("Maximum uint32 sequence is accepted"),Wrapped->AcceptSequence(LastSerial));
    TestTrue(TEXT("Sequence one after uint32 wrap is newer"),Wrapped->AcceptSequence(1));
    TestFalse(TEXT("Reserved zero remains rejected across wrap"),Wrapped->AcceptSequence(0));
    TestFalse(TEXT("Previously received maximum stays duplicate across wrap"),Wrapped->AcceptSequence(LastSerial));
    TestTrue(TEXT("Unseen late sequence before wrap remains inside window"),Wrapped->AcceptSequence(LastSerial-2));
    TestFalse(TEXT("Old initial sequence stays duplicate across wrap"),Wrapped->AcceptSequence(LastSerial-1));
    TestFalse(TEXT("Age 64 across wrap is rejected"),Wrapped->AcceptSequence(LastSerial-62));
    TestEqual(TEXT("Late pre-wrap receive does not rewind newest sequence one"),Wrapped->LastSequence,uint32(1));
    TestTrue(TEXT("Sequence advances normally after wrap"),Wrapped->AcceptSequence(2));

    UDemoPlayerDamageFeedbackProfile* Defaults=NewObject<UDemoPlayerDamageFeedbackProfile>(GetTransientPackage(),NAME_None,RF_Transient); // 只读构造默认的独立资产实例，缺声音允许安全降级。
    TestTrue(TEXT("Native default profile validates without audio assets"),Defaults->Validate());
    UDemoPlayerDamageFeedbackProfile* NonFinite=NewObject<UDemoPlayerDamageFeedbackProfile>(GetTransientPackage(),NAME_None,RF_Transient); // 非有限时间不得泄露到Timer/包络计算。
    NonFinite->IndicatorSeconds=std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("NaN profile timing is rejected"),NonFinite->Validate());
    UDemoPlayerDamageFeedbackProfile* InvertedTier=NewObject<UDemoPlayerDamageFeedbackProfile>(GetTransientPackage(),NAME_None,RF_Transient); // 轻/重阈值不满足严格递增。
    InvertedTier->LightFraction=.4f; InvertedTier->HeavyFraction=.2f;
    TestFalse(TEXT("Inverted severity thresholds are rejected"),InvertedTier->Validate());
    UDemoPlayerDamageFeedbackProfile* InvertedLow=NewObject<UDemoPlayerDamageFeedbackProfile>(GetTransientPackage(),NAME_None,RF_Transient); // 低血退出必须高于进入阈值，防止来回抖动。
    InvertedLow->LowEnter=.4f; InvertedLow->LowExit=.3f;
    TestFalse(TEXT("Inverted low-health hysteresis is rejected"),InvertedLow->Validate());
    UDemoPlayerDamageFeedbackProfile* CrossedCritical=NewObject<UDemoPlayerDamageFeedbackProfile>(GetTransientPackage(),NAME_None,RF_Transient); // 危急退出不能跨过普通低血进入阈值。
    CrossedCritical->CriticalExit=.31f;
    TestFalse(TEXT("Overlapping critical and low-health thresholds are rejected"),CrossedCritical->Validate());
    UDemoPlayerDamageFeedbackProfile* RollLimit=NewObject<UDemoPlayerDamageFeedbackProfile>(GetTransientPackage(),NAME_None,RF_Transient); // 受击镜头配置边界是角度，不是输入转向幅度。
    RollLimit->LightRoll=.4f; RollLimit->HeavyRoll=.4f;
    TestTrue(TEXT("Roll limit 0.4 degrees is accepted"),RollLimit->Validate());
    UDemoPlayerDamageFeedbackProfile* ExcessRoll=NewObject<UDemoPlayerDamageFeedbackProfile>(GetTransientPackage(),NAME_None,RF_Transient); // 越界单独实例，不能沿用已经通过的边界对象。
    ExcessRoll->HeavyRoll=.401f;
    TestFalse(TEXT("Roll above 0.4 degrees is rejected"),ExcessRoll->Validate());
    UDemoPlayerDamageFeedbackProfile* NegativeRoll=NewObject<UDemoPlayerDamageFeedbackProfile>(GetTransientPackage(),NAME_None,RF_Transient); // Profile幅度必须非负，左右符号由回执方向合成。
    NegativeRoll->LightRoll=-.001f;
    TestFalse(TEXT("Negative configured Roll amplitude is rejected"),NegativeRoll->Validate());

    return true; // Automation保留每条断言失败；不把本纯合同专项扩大为真实GAS/网络/声音/UI验收。
}
#endif
