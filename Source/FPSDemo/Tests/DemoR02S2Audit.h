// 职责：R02 S2 大厅/暂停/核心/奖励的显式 Editor 单人夹具；正式链、测试GE清关与Boss表现样本分别记证据。
// 使用：-game -DemoR02S2Audit -DemoR02S2Output="全新目录"，启动器须提供独立 GameUserSettingsINI。
// 边界：不写 Phase/存档/回执，不点击正式退出动作；完成后写 audit.json 并正常结束自有测试进程，由 GI 清临时档。
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoR02S2Audit.generated.h"

class ADemoPlayerController;
class ADemoEnemy;
class UDemoCombatSession;
class UDemoMenuButton;
class ULocalPlayer;

/** 每个实际 World 仅持弱输入引用；显式进程内纯值会话跨正式 OpenLevel 保存步骤与证据。 */
UCLASS()
class FPSDEMO_API UDemoR02S2Audit : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 只创建于显式 Editor -game、正式白模地图、Standalone；拒绝其他已知 UI 夹具并行。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** Type 为引擎 World 类型；不支持普通 Editor、PIE 或非 Editor 包体。 */
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    /** World 为真实新地图；首次建立全新证据目录，旅行后只续用同一 GI 的纯值会话。 */
    virtual void OnWorldBeginPlay(UWorld& World) override;
    /** 只撤销本 World 的末帧驱动和输入过滤；正式旅行不重置进程级绝对超时。 */
    virtual void Deinitialize() override;
private:
#if WITH_EDITOR
    /** GT 末帧推进有限步骤；World 暂停仍运行，不依赖暂停的 World Tick/计时器。 */
    void OnEndFrame();
    /** Value 为明确事实；失败停止夹具并写报告，不尝试修正生产状态。 */
    bool Check(bool Value,const FString& Reason);
    /** Action 为当前顶层真实按钮白名单；只点击生产控件，不直接提交业务。 */
    bool Press(FName Action);
    /** Input 为本机物理键；向现有 Slate 焦点注入一组按下/抬起，不强制焦点。 */
    void Key(FKey Input);
    /** NextStage 为有限步骤；Delay 单位真实秒，旅行前先写步骤以避免旧 World 重复提交。 */
    void Next(int32 NextStage,double Delay=.65);
    /** Action 为代表按钮；读取实际 R02 活页、Outer 父链与焦点，不制造焦点。 */
    bool CheckR02Page(FName Action);
    /** Name 为新 PNG 短名，Scope 明确真实/测试GE/合成样本；ValidateLayout仅HUD样本关闭，正常截图含Canvas/Slate。 */
    bool Capture(const FString& Name,const FString& Scope,bool ValidateLayout=true);
    /** 仅显式Boss样本开始后停用本测试World的AI；不造成死亡或奖励，不恢复成真实遭遇验收。 */
    void FreezeBossSampleActors();
    /** 精确回收本夹具的隐藏无奖励Boss；不会销毁生产敌人或取消正式刷怪服务。 */
    void RestorePresentationFixture();
    /** Success 只覆盖本夹具有限单人流程；写报告后正常结束自有独立进程，不点击正式 Quit/PauseQuit。 */
    void Finish(bool Success,const FString& Reason);

    TWeakObjectPtr<ADemoPlayerController> PC; // 当前真实本机 PC；旅行后由新 World 重取，不延长旧 PC 生命周期。
    TWeakObjectPtr<UWorld> PauseSampleWorld; // 当前暂停截图的真实 World，下一检查帧必须同一对象，旅行后不复用基准。
    TWeakObjectPtr<ADemoEnemy> PresentationBoss; // 后置显式无奖励Boss样本，真实Actor字段供HUD读；结束精确回收。
    TWeakObjectPtr<UDemoCombatSession> FixtureCombatSession; // 合法第一关实际提交的会话；Reward时应为同一已撤销对象，不用Spawner临时完成锁存代替清关。
    TWeakObjectPtr<UDemoMenuButton> ConsumedRewardButton; // 真正领取前保留的弱按钮，用于存活旧控件重复点击拒绝检查；失效明确记未运行。
    TWeakObjectPtr<ULocalPlayer> FilteredLocalPlayer; // 仅本夹具本机手柄干扰过滤租约，结束/旅行精确撤销。
    FDelegateHandle FrameHandle; // 本 World GT 弱 UObject 末帧委托，Deinitialize/Finish 幂等移除。
    bool bOwnsSession=false; // 仅初始化成功且 GI 身份匹配时允许驱动，不代表业务成功。
#endif
};
