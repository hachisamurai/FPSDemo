// 职责：R02无底板文字动作及独立焦点/已选表现；沿现有DemoMenuButton继承CommonUI输入和受控动作转发。
// 使用：S0板与S1正式Screen通过SetR02Appearance赋只读主题/展示选中状态；正式Screen继续Configure原白名单，未配置Screen的板子不提交业务。
#pragma once

#include "CoreMinimal.h"
#include "UI/Common/DemoMenuButton.h"
#include "DemoR02EditorialButton.generated.h"

class UDemoUITheme;
class UBorder;
class USizeBox;
class UTextBlock;

/** 已选Ember底线、真实焦点Ink短刻度可同时存在，装饰不可命中，不创建新激活栈或经济入口。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoR02EditorialButton : public UDemoMenuButton
{
    GENERATED_BODY()

public:
    /** 关闭R04自动颜色/字体覆盖，实际CommonUIButton样式、输入与原动作路由保持继承。 */
    UDemoR02EditorialButton(const FObjectInitializer& ObjectInitializer);
    /** 原生实例先建无框内容树，再让CommonUI包真实按钮；蓝图派生使用其Designer树，不强制覆盖。 */
    virtual bool Initialize() override;

    /** Theme由调用者加载并由本实例强持有；Selected是纯显示值，Size是DPI前point，bChinese选择中文优先复合字体。 */
    void SetR02Appearance(UDemoUITheme* Theme, bool Selected, int32 Size = 24, bool bChinese = true);
    /** 启用整卡高亮模式（用于三选一能力卡）：悬停/焦点时整张卡底板提亮并点亮余烬红底线。 */
    void SetR02CardMode(bool bInCardMode);
    /** 返回纯展示选中，供S0验证同时焦点/已选，不改CommonUI或业务权限。 */
    bool IsR02Selected() const;
    /** 返回真实CommonUI焦点回调维护的表现值，供样式板证据读取，不制造模拟焦点。 */
    bool HasR02FocusHighlight() const;

protected:
    /** Slate重建后解析本树的R02具名控件并恢复样式，CommonUI继续持有真实SButton。 */
    virtual TSharedRef<SWidget> RebuildWidget() override;
    /** 实际焦点/指针/按压/权限回调先沿父类保持输入契约，再更新不可命中R02装饰。 */
    virtual void HandleFocusReceived() override;
    virtual void HandleFocusLost() override;
    virtual void NativeOnHovered() override;
    virtual void NativeOnUnhovered() override;
    virtual void NativeOnPressed() override;
    virtual void NativeOnReleased() override;
    virtual void NativeOnEnabled() override;
    virtual void NativeOnDisabled() override;
    /** 仅当完整累计布局缩放变化时调整一物理像素线厚度，不逐帧改文字/树/权限。 */
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    /** 清空瞬态视觉并沿原基类撤销焦点临时确认绑定；不持旧页面或Actor。 */
    virtual void NativeDestruct() override;

private:
    /** 查找本实例具名表现节点；Designer缺节点时仅记录，绝不追加第二棵按钮树。 */
    void ResolveR02Widgets();
    /** 只将只读主题和真实状态投影到文字/线，不调用SetIsEnabled或业务函数。 */
    void RefreshR02Visuals();
    /** 按当前累计布局缩放设置一物理像素的局部线厚度，不假定只有全局DPI。 */
    void UpdateLineScale(float EffectiveScale);

    UPROPERTY(Transient) TObjectPtr<UDemoUITheme> R02Theme; // 本实例只读主题强引用；不复制、不写盘，GC安全。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> R02Caption; // WidgetTree拥有的同名Caption，基类SetCaption仍能更新。
    UPROPERTY(Transient) TObjectPtr<UBorder> R02Surface; // 透明容器仅提供padding/文字布局，不绘实心CTA。
    UPROPERTY(Transient) TObjectPtr<UBorder> SelectionLine; // Ember已选线，不与焦点共用，不可命中。
    UPROPERTY(Transient) TObjectPtr<UBorder> FocusTick; // Ink短刻度，只由实际焦点事件点亮，不可命中。
    UPROPERTY(Transient) TObjectPtr<USizeBox> SelectionLineBounds; // 已选线局部厚度按有效缩放调整。
    UPROPERTY(Transient) TObjectPtr<USizeBox> FocusTickBounds; // 焦点线局部厚度按有效缩放调整。
    bool bR02Selected = false; // 调用方的纯显示选择，与CommonUI选择/业务状态分离。
    bool bR02Focus = false; // 当前实例实际焦点回调真值；失焦/销毁清空。
    bool bR02Hover = false; // 当前实例实际悬停回调真值，不替代焦点标记。
    bool bR02Pressed = false; // 当前实例按压表现；释放/失焦/禁用清空，不提交额外操作。
    bool bR02CardMode = false; // true时启用整张卡片区域的高亮底色与底部高亮线。
    bool bPreferChineseFont = true; // 中文动作选择中文优先衬线，否则Latin优先。
    int32 R02FontSize = 24; // DPI前point，SetR02Appearance钳制1..256，不在此乘视口缩放。
    float LastEffectiveScale = 1.f; // 上一次完整累计布局缩放，仅决定装饰线局部厚度，不作DPI全局配置。
};
