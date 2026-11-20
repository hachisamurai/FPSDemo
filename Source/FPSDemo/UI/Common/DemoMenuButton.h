// 职责：项目CommonUI动作按钮与焦点/悬停/禁用视觉。使用：页面配置白名单意图；蓝图可提供Caption、ButtonSurface替换原生内容。
// 依赖：CommonButtonBase真实输入与内部按钮；蓝图先复制Designer树，原生先建内容，禁止控件内执行交易。
#pragma once
#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "UI/DemoUIStyle.h" // R04共用配色与已打包字体，按钮角色不表达业务权限。
#include "DemoMenuButton.generated.h"
class UDemoMenuScreen;
class UTextBlock;
class UBorder;
class USizeBox;

/** R04纯表现角色；不会改变动作白名单、业务Selected或CommonUI真实启用/焦点状态。 */
UENUM(BlueprintType)
enum class EDemoButtonVisualRole : uint8
{
    Standard, // 半透明灰色普通操作。
    Primary, // 骨白底/深字的主要提交操作。
    Tab, // 透明页签，选中以底部香槟线标识。
    Catalog, // 武器/弹药目录，选中以左侧香槟线标识。
    Menu // 大厅/暂停文字行；保持左对齐与克制底色。
};

/** 内部SButton仅提供点击与焦点，不绘制引擎默认浮雕；可见主题由ButtonSurface统一呈现。 */
UCLASS()
class FPSDEMO_API UDemoMenuButtonStyle : public UCommonButtonStyle
{
    GENERATED_BODY()
public:
    /** 所有正常/悬停/按下/选中/禁用Brush设为NoResource，零额外padding，不修改输入行为。 */
    UDemoMenuButtonStyle();
};

/** 统一键鼠/手柄焦点与点击，支持蓝图派生样式。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoMenuButton : public UCommonButtonBase
{
    GENERATED_BODY()
public:
    /** 原生默认使用透明CommonUI内部样式；蓝图仍可通过继承的Style显式替换。 */
    UDemoMenuButton(const FObjectInitializer& ObjectInitializer);
    /** 原生先建内容供CommonButtonBase包裹；蓝图让Super复制Designer树后才寻找具名表现控件。 */
    virtual bool Initialize() override;
    /** Screen为本实例弱拥有者，Action仅白名单；Caption/Enabled/Selected是当前表现。 */
    void Configure(UDemoMenuScreen* Screen,FName Action,const FString& Caption,bool Enabled,bool Selected);
    /** 页面构建后指定R04语义角色；仅更新字体、文字对齐和状态装饰，不提交操作或改命中尺寸。 */
    UFUNCTION(BlueprintCallable,Category="Demo|UI|Appearance")
    void SetVisualRole(EDemoButtonVisualRole Role);
    /** 返回本实例纯视觉角色，供实际控件验收/设计扩展读取，不参与权限。 */
    EDemoButtonVisualRole GetVisualRole() const;
    /** 返回当前按钮意图，供焦点恢复/测试；不能据此绕过可见性。 */
    FName GetAction() const;
    /** 动画只替换文案，避免重建控件或重复绑定。 */
    void SetCaption(const FString& Caption);
    /** 数字棋盘专用紧凑正文；初始化后调用，18号单行标签配小内边距，不改变CommonUI输入或权限。 */
    void UseGridDensity();
    /** Auto宽度导航/页脚使用单行期望尺寸，防止中文标签被最小宽度误折成两行；不截断文案。 */
    void UseSingleLineLabel();
#if WITH_EDITOR
    /** Editor专项只触发实际已激活按钮的原生点击链，生产包不提供。 */
    bool PressForTest();
#endif
protected:
    /** 重建Slate后重新解析当前树的具名表现控件；点击/导航仍由CommonButtonBase提供。 */
    virtual TSharedRef<SWidget> RebuildWidget() override;
    /** 真实点击回调；先验证弱页面，统一转发，不访问经济真值。 */
    virtual void NativeOnClicked() override;
    /** CommonUI实际悬停入口，保留其委托/蓝图事件后刷新纯视觉，不修改按钮权限。 */
    virtual void NativeOnHovered() override;
    /** 指针离开时恢复未悬停颜色；键盘焦点仍独立保留。 */
    virtual void NativeOnUnhovered() override;
    /** CommonUI实际按下/释放事件仅更新表现；不模拟点击、不重复绑定输入。 */
    virtual void NativeOnPressed() override;
    virtual void NativeOnReleased() override;
    /** 内部真实按钮和UserWidget共用的焦点入口；不能仅监听外层NativeOnFocusReceived。 */
    virtual void HandleFocusReceived() override;
    /** 内部按钮失焦时撤销本地高亮与临时确认键，不切换页面或触发点击。 */
    virtual void HandleFocusLost() override;
    /** CommonUI完成启用后刷新颜色；不额外改变原动作和可操作状态。 */
    virtual void NativeOnEnabled() override;
    /** CommonUI完成禁用后降低底色/文字亮度并撤销临时确认键，禁用项不能消费默认确认。 */
    virtual void NativeOnDisabled() override;
    /** 拆卸时清理焦点/悬停与临时确认键，不保留跨页面绑定。控件树仍归UUserWidget。 */
    virtual void NativeDestruct() override;

    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Appearance") bool bUseNativeStateColors=true; // 蓝图自行制作状态动画时关闭；不影响真实输入/权限。
    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Appearance") bool bUseNativeTypography=true; // true使用R04字体/对齐/换行；false由R02或Designer拥有完整文字排版，角色切换不覆盖，不改输入。
    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Appearance") FLinearColor NormalSurfaceColor=DemoUI::Raised; // 未选中/未聚焦底色，线性RGBA。
    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Appearance") FLinearColor HoverSurfaceColor=DemoUI::Hovered; // 鼠标/触摸模拟悬停底色，明显提亮。
    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Appearance") FLinearColor FocusSurfaceColor=DemoUI::SelectedHovered; // 键盘/手柄焦点优先于悬停，灰底亮度与前景色同时标识。
    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Appearance") FLinearColor SelectedSurfaceColor=DemoUI::Selected; // 当前页签或显式选中项，不等同于键盘焦点。
    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Appearance") FLinearColor DisabledSurfaceColor=DemoUI::Disabled; // 禁用时始终优先使用，避免灰项仍显示为可激活高亮。
private:
    /** 读取Caption/ButtonSurface及可选焦点/状态装饰；缺失时保留蓝图自定义树，绝不另建树覆盖。 */
    void ResolveVisualWidgets();
    /** 根据现有Enabled/选中/焦点/悬停改表现；不调用SetIsEnabled、业务接口或重新绑定委托。 */
    void RefreshVisualState();
    /** 初始化/角色或密度改变时应用字体与内容对齐；不在Tick或回调内重建WidgetTree。 */
    void ApplyTypographyAndLayout();
    /** UE5.4的SCommonButton忽略Enter；仅焦点按钮注册默认确认动作，失焦/禁用时撤销，沿CommonUI原生点击链执行。 */
    void RefreshFocusAcceptBinding();
    TWeakObjectPtr<UDemoMenuScreen> ScreenOwner; // 当前页面，销毁后按钮不能提交旧请求。
    FName Intent; // 受控本地动作名称，不是UFunction名。
    FString Label; // 重建Slate时恢复当前文案。
    bool bHasConfiguredCaption=false; // 尚未Configure时保留蓝图Designer预览文字，不以空Label覆盖。
    bool bMarkedSelected=false; // 页面传入的展示选中项，不修改CommonUI选择/点击语义。
    bool bPointerHighlighted=false; // CommonUI真实悬停事件维护的本地瞬时状态，不复制。
    bool bFocusHighlighted=false; // 内部真实按钮焦点事件维护；失焦/拆卸清除。
    bool bPressHighlighted=false; // CommonUI真实按下/释放维护；仅让文字下移1px，按钮热区保持原位。
    bool bGridDensity=false; // 紧凑数字格的内边距与单行需求；Slate重建后仍恢复本实例配置。
    bool bSingleLineLabel=false; // 导航/页脚的明确单行策略，避免重建后恢复自动换行。
    bool bNativeContentBuilt=false; // 只对本类创建的原生树调整Padding，Designer树的布局保持作者所有权。
    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Appearance") EDemoButtonVisualRole VisualRole=EDemoButtonVisualRole::Standard; // 本实例纯视觉角色，不复制/存档。
    UPROPERTY(Transient) FDataTableRowHandle FocusAcceptAction; // 本按钮临时拥有的默认确认绑定；不覆盖蓝图显式快捷键，失焦/禁用/拆卸清空。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CaptionWidget; // 原生或蓝图的具名Caption文本，随实例树释放。
    UPROPERTY(Transient) TObjectPtr<UBorder> ButtonSurfaceWidget; // 原生或蓝图的ButtonSurface；主题只染色，内容Padding仅应用于本类创建的原生树。
    UPROPERTY(Transient) TObjectPtr<UBorder> FocusFrameWidget; // 原生可选不可命中焦点框；非焦点用NoResource，绕开UE5.4独立Outline不随RenderOpacity消失的行为。
    UPROPERTY(Transient) TObjectPtr<UBorder> StateRailWidget; // 原生可选左/底状态线，不能单独表达操作权限。
    UPROPERTY(Transient) TObjectPtr<USizeBox> StateRailBounds; // 状态线尺寸；改变线的方向，不改变真实按钮区域。
};
