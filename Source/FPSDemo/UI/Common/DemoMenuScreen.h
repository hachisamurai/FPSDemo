// 职责：CommonUI页面基类与原生UMG构建助手。使用：独立页面派生BuildBody/RefreshKey，业务始终转发原MenuFlow。
#pragma once
#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "DemoMenuScreen.generated.h"

class UDemoMenuFlowComponent;
class ADemoPlayerController;
class UDemoMenuButton;
class UVerticalBox;
class UHorizontalBox;
class UPanelWidget;
class UTextBlock;
class UImage;
class UTexture2D;
class UBorder;
class USizeBox;
class UScrollBox;
class UDemoUITheme;

/** 原生UMG默认布局可由Widget蓝图的PageBody/PageHeading插槽替换；本类没有交易权限。 */
UCLASS(Abstract, Blueprintable)
class FPSDEMO_API UDemoMenuScreen : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    /** 初始化CommonUI输入/返回契约；ObjectInitializer由UObject系统提供。 */
    UDemoMenuScreen(const FObjectInitializer& ObjectInitializer);
    /** 协调器在模型变更后调用，按RefreshKey避免逐帧重建；Force仅首次或测试显式刷新。 */
    void RefreshScreen(bool Force=false);
    /** 只借用当前拥有者的菜单意图适配，不缓存钱包或服务器对象。 */
    UDemoMenuFlowComponent* Flow() const;
    /** 当前本地PC，旅行后旧页面不得继续使用。 */
    ADemoPlayerController* Controller() const;
    /** 当前实际控件的意图提交；验证页面激活/顶层，成功也不意味着交易已获批准。 */
    void SubmitAction(FName Action);
    /** 查找实际按钮用于测试/焦点，不创建业务操作。 */
    UDemoMenuButton* FindAction(FName Action) const;
    /** 协调器只查询当前页型的视觉变体，供根装饰同步；不改变输入或业务门禁。 */
    bool IsUsingR02Presentation() const;
    /** 给当前激活页配置只消费菜单输入；角色门禁仍归PC。 */
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
protected:
    /** 已接入页型显式覆写启用R02；S1武器/弹药、S2大厅/存档/暂停，其余页面默认保留原样。 */
    virtual bool UsesR02Presentation() const;
    /** 返回本页强持有的共享只读R02主题；首次构建加载一次，缺失时保留可用字体回退。 */
    UDemoUITheme* R02Theme() const;
    /** Parent为本页树；RulePadding是上下逻辑留白0..64，绘制按累计缩放对齐1物理像素，不创建输入或业务。 */
    void AddR02Rule(UPanelWidget* Parent,float RulePadding=12.f);
    /** 创建默认UMG布局，仅Widget蓝图没有提供PageBody时使用。 */
    virtual TSharedRef<SWidget> RebuildWidget() override;
    /** 释放控件引用/焦点记录，不访问异步或权威服务。 */
    virtual void NativeDestruct() override;
    /** CommonUI返回转交协调器，由原Flow决定撤销顺序。 */
    virtual bool NativeOnHandleBackAction() override;
    /** 取可用首按钮，重建时优先恢复同名操作焦点。 */
    virtual UWidget* NativeGetDesiredFocusTarget() const override;
    /** 内容重建时返回仍有效的原焦点按钮；首次构建无记录返回空，供模态设置安全默认且不夺回用户已移动的焦点。 */
    UWidget* GetRestoredFocusTarget() const;
    /** 正文清空重建前给子页机会记录非按钮控件（如文本输入框）的焦点状态；返回true则重建后触发焦点恢复。 */
    virtual bool CaptureExtraFocusBeforeRebuild();
    /** Tab作为关闭/取消快捷键；其余导航沿用Slate/CommonUI。 */
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    /** 只更新已构建的动画控件，不计算奖励或重建页面。 */
    virtual void NativeTick(const FGeometry& Geometry,float DeltaTime) override;
    /** 子类返回所有影响静态布局的模型键；不要包含逐帧动画时间。 */
    virtual FString RefreshKey() const;
    /** 子类使用下方助手创建Body内容；只能读取快照并注册受控意图。 */
    virtual void BuildBody();
    /** DeltaTime为本地真实帧秒，仅更新当前控件视觉。 */
    virtual void AnimateScreen(float DeltaTime);
    /** Heading/Subtitle是只读文案；所有页面共用标题区域。 */
    void SetHeading(const FString& Heading,const FString& Subtitle=FString());
    /** Parent当前树拥有；按本页R04/R02变体使用持久化复合字体，Text自动换行，Size为DPI前字号；不保存临时Slate字体进蓝图。 */
    UTextBlock* AddText(UPanelWidget* Parent,const FString& Text,int32 Size=18,FLinearColor Color=FLinearColor(FColor(240,237,230)));
    /** Action稳定白名单名，Enabled仅表现门禁，服务器仍重新校验；返回真实CommonUI按钮。 */
    UDemoMenuButton* AddAction(UPanelWidget* Parent,FName Action,const FString& Caption,bool Enabled=true,bool Selected=false);
    /** 紧凑操作使用Auto槽，不与说明平分宽度；Width为DPI前最小宽度，仍允许长文本增长。 */
    UDemoMenuButton* AddCompactAction(UPanelWidget* Parent,FName Action,const FString& Caption,bool Enabled=true,float Width=120.f);
    /** 返回置于正文外固定导航区；Action沿用原白名单，Enabled不代替服务端校验。 */
    void AddBack(FName Action,const FString& Caption,bool Enabled=true);
    /** 固定页头的轻量操作；Action仍走原意图门禁，Width为最小设计宽度，窄页自动回退导航区。 */
    UDemoMenuButton* AddHeaderAction(FName Action,const FString& Caption,bool Enabled=true,float Width=160.f);
    /** 主操作置于固定页脚右侧；短屏滚动正文时仍可点击，Width为最小宽度。 */
    UDemoMenuButton* AddFooterAction(FName Action,const FString& Caption,bool Enabled=true,float Width=180.f);
    /** 创建带内边距的灰阶内容卡；Parent拥有，水平父容器内按等宽排列。 */
    UVerticalBox* AddCard(UPanelWidget* Parent);
    /** 创建纯布局留白；Height为DPI前最小高度，横向自动填满；Stretch使纵向卡片把后续操作推至底部。 */
    void AddGap(UPanelWidget* Parent,float Height=16.f,bool Stretch=false);
    /** 显示独立滚动的右侧栏；用于下注/详情，不跟随左侧长列表滚走。 */
    UVerticalBox* ShowAside();
    /** 页面构建时设置DPI前边界与靠左/居中；仅修改运行时实例，不写入Designer资产。 */
    void ConfigureLayout(float Width=1320.f,float Height=820.f,bool LeftAligned=false);
    /** Parent当前树拥有，创建自适应水平/垂直容器；子控件不持有业务引用。 */
    UHorizontalBox* AddRow(UPanelWidget* Parent);
    UVerticalBox* AddColumn(UPanelWidget* Parent);
    /** 为Parent创建已加载Texture的等比图像；Size是DPI前最大边长，槽不拉伸，缺图只保留占位。 */
    UImage* AddIcon(UPanelWidget* Parent,UTexture2D* Texture,float Size=48.f);
    /** 蓝图可提供同名具名布局控件；默认树使用UMG Border/ScrollBox/VerticalBox。 */
    UPROPERTY(BlueprintReadOnly,meta=(BindWidgetOptional),Category="Demo|UI") TObjectPtr<UVerticalBox> PageBody;
    UPROPERTY(BlueprintReadOnly,meta=(BindWidgetOptional),Category="Demo|UI") TObjectPtr<UTextBlock> PageHeading; // 页面标题，本地实例拥有。
    UPROPERTY(BlueprintReadOnly,meta=(BindWidgetOptional),Category="Demo|UI") TObjectPtr<UTextBlock> PageSubtitle; // 余额/副标题，本地实例拥有。
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Demo|UI") FLinearColor SurfaceColor=FLinearColor(FColor(24,24,24)).CopyWithNewOpacity(.45f); // 旧资产兼容字段；R04规范PanelSurface由运行时主题统一覆盖，自定义非规范壳仍保留作者表现。
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Demo|UI") FLinearColor WorldDimColor=FLinearColor(.01f,.015f,.02f,.6f); // 旧资产兼容字段；R04规范壳统一深灰遮罩，大厅在BuildBody独立配置低Alpha，不再用旧蓝灰值染色。
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Demo|UI") float PanelWidth=1320.f; // 旧资产/模态的DPI前宽度；R04复杂页默认1600，窄页BuildBody显式配置，ScaleBox只保证可达，不替代可读性验收。
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> PageNavigation; // 正文外顶部导航，实例树拥有，不复制。
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> PageCategories; // 可选固定分类页签，避免商品列表滚动时失去切页入口。
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> PageFooter; // 正文外底部主要操作，实例树拥有，不复制。
private:
    /** 每次正文重建前按当前页型恢复/应用壳层样式，复用Terminal切页不会污染其他页。 */
    void ApplyPagePresentation();
    UPROPERTY(EditDefaultsOnly,Category="Demo|R02") mutable TObjectPtr<UDemoUITheme> LoadedR02Theme; // CDO硬引用确保Cook收集；页实例只读，不复制/写存档，不在Tick同步加载。
    mutable bool bR02ThemeResolved=false; // 缺资源也只尝试一次并记录，避免逐帧读盘。
    TArray<FName> ActionOrder; // 本次构建插入顺序；R02初始焦点确定性，隐藏按钮随重建移除。
    /** 为已有规范壳层增加导航/页脚/侧栏；缺少具名壳层时保留原树，助手退回正文。 */
    void BuildLayoutRegions();
    /** Scroll属于当前实例；统一R04细灰滚动条，保留真实滚动和溢出提示，不用隐藏条掩盖超出内容。 */
    void StyleScrollBox(UScrollBox* Scroll);
    /** 只更新本页正文短入场表现，Pause仍使用单调秒；减少动效立即复位，不操作输入栈/业务或重建控件。 */
    void UpdatePageTransition();
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PageWallet; // R04固定页头的本人双币余额；不读取其他玩家、不持久化。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PageSectionLabel; // R04英文区域眉题；由当前页名映射，纯展示不参与路由。
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> PageHeaderActions; // 宽页返回放入固定页头右侧；窄页/模态仍使用原独立导航，避免标题被挤窄。
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> PageAside; // 独立侧栏正文，关闭页面时随树释放。
    UPROPERTY(Transient) TObjectPtr<USizeBox> AsideBounds; // 默认300设计像素，未使用时折叠；内部文本另预留滚动条与边距。
    UPROPERTY(Transient) TMap<FName,TObjectPtr<UDemoMenuButton>> ActionButtons; // 当前显示的真实按钮，重建时清理，不能调用隐藏旧按钮。
    FString LastRefreshKey; // 结构刷新比较值，不持久化/复制。
    FName FocusAction; // 重建前仍有效的焦点操作，模态退出可恢复。
    double PageTransitionStart=-1.; // 仅首次构建/页面标题切换启动的单调秒；数值更新不重播，不复制/保存。
};
