// 职责：存档/暂停采用R02文字档案与现场菜单，退出/指南/房间保留原主题；页面仍完全由Flow选择，无独立业务状态。
#pragma once
#include "UI/Common/DemoMenuScreen.h"
#include "DemoSessionScreen.generated.h"
class UEditableTextBox;
UCLASS(Blueprintable)
class FPSDEMO_API UDemoSessionScreen : public UDemoMenuScreen
{
    GENERATED_BODY()
public:
    /** 顶层真实按钮通过UIManager调用；只提交本次显示的行号/版本，刷新后的旧点击由服务拒绝。 */
    void JoinDisplayedRoom(int32 Row);
    /** 域名/IP直连按钮与Enter共用提交入口；校验与旅行均交MenuFlow/RoomSubsystem，失败保留原输入与焦点。 */
    void SubmitDirectJoin();
protected:
    /** 仅Saves/CreateSave及Pause/ReturnHub跟随R02；Rooms/Exit/Guide和设置不在本次视觉范围内。 */
    virtual bool UsesR02Presentation() const override;
    /** 比较存档缓存、页面状态与房间修订，不在Widget内刷新磁盘或搜索网络。 */
    virtual FString RefreshKey() const override;
    /** 将Flow当前页面投影为UMG；创建/返回确认的下层分别是存档与暂停。 */
    virtual void BuildBody() override;
    /** 房间列表刷新重建前捕获直连输入框焦点与草稿，避免后台搜索结束打断输入。 */
    virtual bool CaptureExtraFocusBeforeRebuild() override;
    /** 若重建前焦点在直连输入框则恢复至新输入框，否则回退基类焦点策略。 */
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    /** 清理本页持有的直连输入框临时引用与焦点状态。 */
    virtual void NativeDestruct() override;
private:
    /** Parent为当前树；创建R04轻透明信息卡与顶部细线，外部不持有该树，不接管业务状态。 */
    UVerticalBox* AddSessionCard(UPanelWidget* Parent);
    /** Parent为当前卡片/列；添加一条不可交互细分隔，不影响焦点或命中边界。 */
    void AddSessionRule(UPanelWidget* Parent);
    /** Parent为当前列，Text只读；LabelSize为逻辑字号，英文眉题使用批准的Barlow字体。 */
    UTextBlock* AddSessionLabel(UPanelWidget* Parent,const FString& Text,int32 LabelSize=15);
    /** 三个真实槽按竖向档案显示创建本地时间；Save0..2、房主选槽和损坏保护仍由原Flow/服务验证。 */
    void BuildSaves();
    /** 左侧编号五动作，右侧只读关数/难度/银币；单人按实际暂停状态命名，合作明确世界继续运行。 */
    void BuildPause();
    /** 退出保存状态只读；取消/重试沿用原状态机。 */
    void BuildExit();
    /** 房间服务缓存投影，绝不在构建期间启动异步请求。 */
    void BuildRooms();
    /** 一次性操作提示仍由设备设置记录已读。 */
    void BuildGuide();
    /** 输入变更时实时同步直连草稿到RoomSubsystem并清除旧校验错误，不触发网络操作。 */
    UFUNCTION() void OnDirectAddressChanged(const FText& Text);
    /** 仅在Enter提交时触发直连，失焦不发起网络连接。 */
    UFUNCTION() void OnDirectAddressCommitted(const FText& Text,ETextCommit::Type Method);
    uint32 RoomRevision=0; // 实际显示目录版本，点击时不得替换为服务最新版本。
    int32 RoomOffset=0; // 本页第一行在服务结果中的位置，固定每页5行。
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> DirectAddressEntry; // 域名/IP直连输入框，当前页面树拥有，随重建替换。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DirectErrorText; // 直连格式/状态错误提示文本，纯本机展示。
    FString DirectErrorMessage; // 最近一次直连校验失败文案，编辑或切页后清空。
    bool bRestoreDirectFocus=false; // 仅本次重建前直连输入框持焦点时为true，重建后恢复光标。
};
