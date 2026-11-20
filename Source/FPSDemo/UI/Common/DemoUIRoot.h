// 职责：每个本地玩家的UMG根布局。使用：UIManager添加到PlayerScreen，两个CommonUI栈分别负责菜单与模态。
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DemoUIRoot.generated.h"
class UCommonActivatableWidgetStack;
class UBorder;
class UTextBlock;
UCLASS(Blueprintable)
class FPSDEMO_API UDemoUIRoot : public UUserWidget
{
    GENERATED_BODY()
public:
    /** Message为空隐藏；Warning仅控制颜色，控件始终不可命中，不改变栈/焦点或业务权限。 */
    void SetFeedback(const FString& Message,bool Warning);
    /** 协调器按实际活动页面选择根装饰变体；只改外观，不触碰栈/焦点/反馈真值。 */
    void SetR02Presentation(bool Enabled);
    UPROPERTY(BlueprintReadOnly,meta=(BindWidgetOptional),Category="Demo|UI") TObjectPtr<UCommonActivatableWidgetStack> MenuStack; // 本玩家菜单层，根实例拥有。
    UPROPERTY(BlueprintReadOnly,meta=(BindWidgetOptional),Category="Demo|UI") TObjectPtr<UCommonActivatableWidgetStack> ModalStack; // 更高Z序，模态期间禁用底层指针。
protected:
    /** 原生默认UMG Overlay，允许蓝图提供同名栈替换布局。 */
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TObjectPtr<UBorder> FeedbackSurface; // 当前根树拥有的短通知，不跨旅行。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FeedbackLabel; // 只读拥有者回执，随根销毁。
    bool bR02Presentation=false; // 当前本机根装饰页型，不复制/保存。
};
