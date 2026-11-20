// 职责：Editor 的真实 UMG/CommonUI 专项回归。使用 -DemoCommonUITest 和隔离 GameUserSettingsINI 启动；依赖正式 UIManager、按钮、Slate 输入及临时存档。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h" // Editor 键盘夹具签名使用 FKey；不依赖其他翻译单元的传递包含。
#include "Player/Coop/Preparation/DemoEncounterPreparationTypes.h" // 表现夹具保存并还原公开握手值；不修改准备服务内部状态。
#include "DemoCommonUITest.generated.h"

class UDemoMenuButton;

/** 只在 Editor 显式创建的实机夹具；测试业务入口不会注册到非 Editor 包体。 */
UCLASS(Transient,NotBlueprintable)
class FPSDEMO_API ADemoCommonUITest : public AActor
{
    GENERATED_BODY()
public:
    /** 启用暂停期间的晚期 Tick，以真实时间等待 Slate 焦点与渲染完成。 */
    ADemoCommonUITest();
    /** DeltaSeconds 是 World 秒；UI 截止使用单调时间，单次失败立即退出隔离测试进程。 */
    virtual void Tick(float DeltaSeconds) override;
private:
#if WITH_EDITOR
    /** 记录实测 Value 和原因 Reason；失败停止状态机，不把界面存在当作交易通过。 */
    bool Check(bool Value,const TCHAR* Reason);
    /** 经当前实际 CommonUI 按钮提交 Action，调用前后同步页面；没有直接交易旁路。 */
    bool Press(FName Action);
    /** 向当前 Slate 焦点键入 Text；不修改系统剪贴板或直接设置业务草稿。 */
    void Type(const TCHAR* Text);
    /** 真实 Slate 按下和松开 InputKey；Control 仅用于编辑框全选。 */
    void Key(FKey InputKey,bool Control=false);
    /** 故障夹具：临时绑定到正式同步保存委托，只返回失败，不访问真实玩家文件。 */
    bool RejectSave();
    /** 下一 Stage 在 Delay 秒后执行；显式分帧使焦点/Slate 绘制可以真正发生。 */
    void Next(int32 Stage,double Delay=.2);
    /** 仅额外-DemoGameplayUISpine启用的表现夹具；构造服务器阶段/目标/冻结摘要，检查真实UMG布局；不算实际清关、奖励结算或多人验收。 */
    void TickGameplaySpine();
    /** 仅额外-DemoUICompletePresentation启用；真实设置/武器比较回归及明确synthetic的房间、Loading、观战截图，结束后进入既有Spine。 */
    void TickCompletePresentation();
    int32 CurrentStage=0; // 单个隔离运行的状态机位置，不复制。
    int32 Passed=0; // 当前专项真实通过的断言数，仅成功日志汇总。
    int32 Serial=0; // 交易前后账本序号，验证重复按钮不能二次结算。
    int32 Wallet=0; // 保存故障前金币，不依赖随机胜负。
    double Deadline=0; // 单调秒；菜单暂停仍继续等待真实 UI 帧。
    double StartedAt=0; // 整个夹具的单调起点，120 秒超时视为失败。
    bool Failed=false; // 首个失败后不再推进或写入测试存档。
    FVector2D WeaponTabsOrigin=FVector2D::ZeroVector; // R04武器页已绘制的页签屏幕原点，用于弹药切页实际几何对照。
    FVector2D WeaponTabsSize=FVector2D::ZeroVector; // 同帧页签尺寸，不保留旧UWidget/FGeometry引用。
    bool PresentationOriginalReducedMotion=false; // 两项即时设备偏好的原值，只对调用方隔离INI操作，结束经真实按钮还原。
    bool PresentationOriginalUISound=true;
    float PresentationOriginalSensitivity=1.f; // 未应用灵敏度草稿的设备值对照，界面开关不得隐式应用。
    TArray<FString> PresentationUnappliedDraft; // 五项原设置草稿，逐项读正式Flow，不从预期字符串构造。
    float ComparisonOriginalDamageBonus=0.f; // 临时GAS伤害对照，比较表检查完后精确还原。
    float ComparisonOriginalMagazineBonus=0.f; // 临时GAS容量对照，不更改Progress/武器CDO/持久存档。
    float ComparisonOriginalLiveDamage=0.f; // 实际手枪实例的伤害读数，确认GAS夹具确实影响玩家，避免空测。
    FString ComparisonRenderedValues; // 真实UMG比较表的四行文本快照，变更GAS后必须保持同一CDO口径。
    FDemoEncounterPreparationView PresentationOriginalPreparation; // GS公开准备视图原值，Loading截图后立即恢复；不会创建真实预载请求。
#endif
    UPROPERTY(Transient) TObjectPtr<UDemoMenuButton> StaleButton; // 保活已显示的旧控件以测试失效点击；不在当前页面查找失败后偷换成业务调用。
};
