// 职责：R02 S1 的显式 Editor 独立游戏运行夹具；只创建隔离临时档，复用正式 CommonUI/交易/战斗入口。
// 使用：-game -DemoR02S1Audit -DemoR02S1Output="目录"；附加 -DemoR02S1Interactive 在正式武器页交还用户，不自动退出。
// 边界：真实运行与 SYNTHETIC_PRESENTATION 样本分别登记；不验证十关、多人、Cook，不修改正式配置或保存格式。
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h" // FKey 是 Slate 键盘注入的值参数；不依赖 Unity 的传递包含。
#include "Subsystems/WorldSubsystem.h"
#include "DemoR02S1Audit.generated.h"

class ADemoCharacter;
class ADemoPlayerController;
class ADemoEnemy;
class UDemoMenuButton;

/** 同一 World 的有界、可退出审查；不向普通 PIE、非 Editor 游戏或多人 World 注入行为。 */
UCLASS()
class FPSDEMO_API UDemoR02S1Audit : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 仅显式 Editor -game Standalone 的正式白模入口；与旧样式板/菜单夹具互斥。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** 只支持独立 Game World，不在普通 Editor/PIE 中创建。 */
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    /** 建本次证据目录并绑定当前 World 的 UObject 弱末帧委托，不接管输入栈。 */
    virtual void OnWorldBeginPlay(UWorld& World) override;
    /** 精确移除自有委托、释放自有样本；不取消他人的截图请求。 */
    virtual void Deinitialize() override;
private:
#if WITH_EDITOR
    /** GT 有界状态机；World/Type/Delta 为引擎回调值，只处理本 World 的完整游戏 Tick。 */
    void OnPostTick(UWorld* World,ELevelTick Type,float Delta);
    /** GT 每帧仅核单调绝对超时；World 暂停仍执行，不推进步骤、模拟输入或重设菜单。 */
    void OnFrameWatchdog();
    /** 记录明确断言；失败立即写报告并退出自有测试进程，Reason 不含私有账户数据。 */
    bool Check(bool bValue,const FString& Reason);
    /** 仅按当前顶层实际 CommonUI 按钮提交 Action；提交不等于业务成功，后续独立读取权威结果。 */
    bool Press(FName Action);
    /** 向实际 Slate 焦点注入一次按下/抬起，InputKey 为本机键，不强行设置焦点。 */
    void Key(FKey InputKey);
    /** NextStage 为有限阶段，Delay 为单调真实秒；等待正式 UI 绘制/焦点落定。 */
    void Next(int32 NextStage,double Delay=.65);
    /** 正常 PNG 包含 Canvas/Slate；Scope 明确 REAL_RUNTIME 或 SYNTHETIC_PRESENTATION，不读 RT。 */
    bool Capture(const FString& Name,const FString& Scope,bool bValidateLayout=true);
    /** 检查真实顶层 R02 页面、按钮 Outer 的 Activatable 父链和 Slate 焦点，不人工制造焦点。 */
    bool CheckR02Page(FName RepresentativeAction);
    /** Button 为真实旧对象弱引用；Context 标记场景。有效才实际点击核拒绝，已回收则明确记录未执行点击。 */
    bool CheckStaleButton(TWeakObjectPtr<UDemoMenuButton> Button,const FString& Context);
    /** 回收仅本夹具的 Boss/阵亡表现；不会修改真实伤害或交易回执。 */
    void RestorePresentationFixture();
    /** 写 audit.json；bHandOff 只在交互武器页就绪时为 true，并保留正常游戏让用户操作。 */
    void Finish(bool bSuccess,const FString& Reason,bool bHandOff=false);

    TWeakObjectPtr<ADemoPlayerController> PC; // 当前真实本机 PC，不延长旅行前拥有者的生命周期。
    TWeakObjectPtr<ADemoCharacter> Player; // 当前 PC 的实际 Pawn，始终核对拥有关系。
    TWeakObjectPtr<ADemoEnemy> PresentationBoss; // 仅后置样本生成的无攻击/无奖励 Boss，结束精确回收。
    TWeakObjectPtr<ADemoEnemy> RealShotTarget; // 当前真实Combat最近活敌人的弱引用，仅用于一次普通发射前重新朝向，不生成目标或篡改命中。
    TWeakObjectPtr<UDemoMenuButton> StaleUnderlying,StaleConfirm; // 真实旧按钮的弱引用，核模态穿透和重复确认拒绝。
    FDelegateHandle TickHandle; // 本 World GT 末帧弱委托，Finish/Deinitialize 幂等解除。
    FDelegateHandle WatchdogHandle; // 独立于 World 暂停的 GT OnEndFrame 超时租约，所有结束路径精确解除。
    FString Directory,PendingPNG; // 显式输出目录和最多一个在途正常截图，不改用户截图设置。
    TArray<FString> PNGs,CaptureScopes,Assertions,Notes; // 本次可复核事实和范围；不记录他人状态或私有钱包。
    FString DurableProgressBeforeDirectEquip,ProfileBeforeDirectEquip; // Editor装配夹具前的永久状态只在内存对比，不输出账号正文。
    uint64 SaveSerialBeforeDirectEquip=0; // 真实GI保存水位，临时装备/重复点击不得推进。
    int32 DirectEquipAmmo=0,DirectEquipReserve=0; // 显式非满弹库存夹具后的原件读数，重复装配不得补弹。
    bool bDirectEquipObserved=false; // 实际UI装配及重复/无永久写入核验后才为true，不代表付费拥有枪测试。
    FName AmmoId,InitialAmmoSelection,PrimaryBeforeInspection; // 实际服务器报价/本人装配的对比值，非权限来源。
    double Started=0.,Deadline=0.,StepStarted=0.,ResourceStableSince=-1.,ShotStarted=0.; // 单调真实秒；暂停也可超时。
    uint64 ShotFrame=0; // 请求截图时的帧号，后续帧仅检查 PNG 文件是否产生。
    int32 Stage=0,Passed=0,InitialCoins=0,QuotedPrice=0; // 有限步骤/断言数/实际交易前余额和报价，单位金币。
    float HealthBeforeDamageWait=0.f,DamageWorldTimeBefore=-1.f; // 正式 Combat 等待前的实际 HP/最近受击 World 秒。
    double RealShotWorldTime=-1.; // 本次真实StartFire前的World秒；只关联后续真正收到的ShotId/StartedAt，不作为伤害输入。
    bool bFinished=false,bInteractive=false,bInputFiltered=false; // 本次审查生命周期/交互分支/本机输入过滤租约。
    bool bTrueCombatObserved=false,bObservedRealDamage=false,bSyntheticStarted=false,bForcedEliminated=false; // 证据分类；样本不能冒充真实战斗或多人。
    bool bSyntheticLowHealthGEApplied=false; // 仅条件性环境 GE 实际提交成功才为 true；天然已低血时不冒称施加了夹具伤害。
    bool bRealShotRequested=false,bRealShotSpentAmmo=false,bObservedRealHit=false; // 一次原输入链请求/实际扣弹/实际新命中回执分别记录，不把发射或未观测等同命中。
    bool bRealHitVisibleCapture=false,bHitObservationDiagnosticLogged=false; // 真实标记可见时请求且正常PNG完成才记截图；一次诊断不逐帧刷日志，不修改标记寿命。
#endif
};
