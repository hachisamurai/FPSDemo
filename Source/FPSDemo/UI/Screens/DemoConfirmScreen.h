// 职责：统一CommonUI确认与金额输入模态；武器详情Kind4已删除。使用：UIManager按原Flow配置Kind，不持交易状态。
#pragma once
#include "CoreMinimal.h"
#include "UI/Common/DemoMenuScreen.h"
#include "DemoConfirmScreen.generated.h"
class UEditableTextBox;
class UTextBlock;
UCLASS(Blueprintable)
class FPSDEMO_API UDemoConfirmScreen : public UDemoMenuScreen
{
    GENERATED_BODY()
public:
    /** 模态作为独立CommonUI输入根，底层菜单即使保持激活也不能接收动作。 */
    UDemoConfirmScreen(const FObjectInitializer& ObjectInitializer);
    /** 设置当前表现类型；只有协调器可据Flow生成该种类，不代表购买授权。 */
    void SetKind(int32 InKind);
    /** 金额确认按钮与Enter共用解析接口；失败保留原文本和焦点，不关闭模态。 */
    void AcceptBet();
protected:
    /** 弹药解锁沿既有R02；新档/返回安全区仅在各自R02源页意图下跟随；无武器详情种类，其他确认保原安全默认取消。 */
    virtual bool UsesR02Presentation() const override;
    /** 只随种类/冻结报价/反馈变化重建，文本编辑期间不以输入内容驱动结构刷新。 */
    virtual FString RefreshKey() const override;
    /** 构建真实UMG编辑框或确认按钮；金额解析仍在Flow完成。 */
    virtual void BuildBody() override;
    /** 金额框优先焦点；其他确认页使用基类按钮恢复。 */
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
private:
    /** Parent为固定操作区，Action沿原白名单；统一确认/取消按钮层级，不在控件内执行交易。 */
    UDemoMenuButton* AddDialogAction(UPanelWidget* Parent,FName Action,const FString& Caption,bool Enabled=true);
    /** Method仅接受Enter，焦点丢失不提交；UObject动态委托随控件清理。 */
    UFUNCTION() void OnBetCommitted(const FText& Text,ETextCommit::Type Method);
    int32 Kind=0; // 表现种类，无独立确认/费用真值，不复制；旧4武器详情退役，其他编号不重排。
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> BetEntry; // 当前金额模态拥有，清除时不保留用户文本。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ErrorText; // 解析错误本地表现，无业务副作用。
};
