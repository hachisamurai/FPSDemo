// 职责：七种金币娱乐的真实UMG页面和只读结果动画。使用：本地CommonUI协调器激活，按钮仅转发既有MenuFlow意图。
#pragma once
#include "CoreMinimal.h"
#include "UI/Common/DemoMenuScreen.h"
#include "DemoCasinoScreen.generated.h"

class UProgressBar;
class UCanvasPanel;
class UWidget;
struct FDemoCasinoRules;
struct FDemoCasinoIntent;
struct FDemoArcadeRound;

#if WITH_EDITOR
/** Editor回归读取当前真实控件值；仅为跨帧快照，不调用动画、不读取Flow时钟或模拟停靠算法。 */
struct FDemoCasinoAnimationProbe
{
    uint32 ContentId=0; // 当前结果文本控件的UObject唯一编号；比较同一树，排除开始/结束重建造成的假变化。
    FString Value; // 当前实际结果文本，不是权威结果或时钟推算值。
    TArray<FVector2D> ReelTranslations; // 三轴各四格的实际RenderTranslation，DPI前像素。
    TArray<FString> ReelLabels; // 与上方同索引的当前可见符号名，用于最终停靠核对。
    TArray<FVector2D> ReelCellSizes; // 实际CachedGeometry局部尺寸，验证固定格宽已经完成Slate布局。
    TArray<FVector2D> ReelLabelSizes; // 实际标签局部宽高，检出中文被挤成多行/逐字竖排。
    TArray<bool> ReelLabelWrap; // 实际自动或显式换行设置；老虎机短标签应保持单行。
    TArray<FString> DiceValues; // 五枚实际TextBlock的当前字符，保留骰也从控件读回。
    TArray<FVector2D> ExplosionScales; // 24个实际Image的RenderScale，不含合成/预期值。
    TArray<FVector2D> ExplosionTranslations; // 24个实际Image的位移，验证火星在现有控件上移动。
    TArray<float> ExplosionOpacities; // 24个实际Image的不透明度，范围0..1。
    TArray<float> ArenaHealth; // 两个实际ProgressBar的Percent，范围0..1，不重放Battle来代算。
};
#endif

/** 不持有钱包、随机流或未揭雷位；确认框/金额输入由上层模态栈负责。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoCasinoScreen : public UDemoMenuScreen
{
    GENERATED_BODY()
public:
    /** 只切换本页说明层，不改变下注草稿或已保存结果；由当前真实按钮调用。 */
    void ToggleHelp();
    /** 返回时先关闭说明层，避免跳过玩法配置；不代表业务模态。 */
    bool IsHelpOpen() const;
#if WITH_EDITOR
    /** 同步复制当前UMG控件的表现值供跨帧断言；只读、不推进表现、不开放任何包体接口。 */
    FDemoCasinoAnimationProbe ReadAnimationForTest() const;
#endif
protected:
    /** 只包含草稿、公开账本及结构状态；真实动画时间不进入比较键。 */
    virtual FString RefreshKey() const override;
    /** 根据公开快照构建当前玩法；重建仅发生在模型变更时。 */
    virtual void BuildBody() override;
    /** DeltaTime为帧秒；以Flow的单调时间更新既有控件，绝不计算奖励。 */
    virtual void AnimateScreen(float DeltaTime) override;
private:
    /** Parent为当前树容器；Code/Title为只读区域身份，Selected仅控制R04细线，不改变交易权限。 */
    UVerticalBox* AddCasinoSection(UPanelWidget* Parent,const FString& Code,const FString& Title,bool Selected=false);
    /** Parent为当前树；用1px灰线组织内容，不创建热区或第二层事务。 */
    void AddCasinoRule(UPanelWidget* Parent);
    /** Label/Value来自公开报价或冻结合同；独立左右列，不用显示条模拟概率或未揭结果。 */
    void AddCasinoValue(UPanelWidget* Parent,const FString& Label,const FString& Value,FLinearColor Color);
    /** Parent由当前树拥有；Description为当前步骤，State为已确认状态，支持等待/错误而不伪造到账。 */
    void AddCasinoNotice(UPanelWidget* Parent,const FString& State,const FString& Description,bool Error=false);
    /** 按当前玩法生成完整步骤/结算/退出规则；实际金额和概率仍以公开报价或冻结合同为准。 */
    void BuildHelp();
    bool bHelpOpen=false; // 页面实例内的纯展示状态，不复制/持久化、不改变Flow。
    /** Parent为本次控件树；Enabled只控制表现，所有按钮仍走服务端二次校验。 */
    void BuildNumbers(UPanelWidget* Parent,bool Enabled);
    /** Parent为本次控件树；公布每轴权重和组合倍率，沿用已有弹药图标。 */
    void BuildSlots(UPanelWidget* Parent);
    /** Parent为本次控件树；只能读取公开OpenMask，不使用任何隐藏雷位。 */
    void BuildMine(UPanelWidget* Parent,bool Enabled);
    /** Parent为本次控件树；持续局面展示冻结合同，不用新配置重新定价。 */
    void BuildArcade(UPanelWidget* Parent,bool Enabled);
    /** Parent为本次控件树；展示金额输入入口、报价和持续局面的恢复/领取入口。 */
    void BuildStakePanel(UPanelWidget* Parent,bool Enabled);
    /** 只对已保存的终局构建结果控件；开箱安全格仍留在棋盘。 */
    void BuildResult();
    /** 在Parent内建立三轴裁切容器；所有符号来自既有目录，不同步加载资产。 */
    void BuildSlotReels(UPanelWidget* Parent);
    /** 在Parent内建立局部UMG爆炸层；不会修改世界/摄像机或执行经济操作。 */
    void BuildExplosion(UPanelWidget* Parent);
    /** Parent为某方回放卡片，Side仅决定机械兽UMG轮廓；没有真实战斗对象或输入热区。 */
    void BuildMechanicalFigure(UPanelWidget* Parent,int32 Side);
    /** Time为回执展示真实秒；更新本地烟团、冲击环与火星，跳过时隐藏。 */
    void AnimateExplosion(double Time);
    /** Index为0..4老虎机符号；仅借用玩家已加载弹药目录纹理，金币/缺图返回空。 */
    UTexture2D* SymbolIcon(int32 Index) const;
    /** 为Parent添加紧凑网格；ColumnCount为固定列数，Grid由当前WidgetTree拥有。 */
    class UUniformGridPanel* AddGrid(UPanelWidget* Parent,int32 ColumnCount);
    /** Button为已经注册意图的真实CommonUI按钮，Row/Column只控制UMG布局。 */
    void PlaceGridButton(UDemoMenuButton* Button,int32 Row,int32 Column);

    UPROPERTY(Transient) TObjectPtr<UTextBlock> RevealStatus; // 结果标题，真实时间更新；不复制。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RevealValue; // 数字/骰型/命中/最终到账文案，当前树拥有。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RevealMessage; // 只在动画完成后显示权威回执，不提前泄露文案。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RevealPayout; // 固定回执侧栏的本次实际到账；减少动效时可静态显示已确认结果，不执行二次发奖。
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> DiceFaces; // 五枚显示骰，停靠值只取服务器回执。
    UPROPERTY(Transient) TArray<TObjectPtr<UVerticalBox>> ReelCells; // 三轴各四个复用符号根控件；有限数量，不逐帧分配。
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ReelIcons; // 与ReelCells同索引，持有当前可见纹理。
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ReelLabels; // 与ReelCells同索引，金币无图时仍有名称。
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ReelStates; // 三轴停靠状态，不影响交易权限。
    UPROPERTY(Transient) TArray<TObjectPtr<UVerticalBox>> ArenaFigures; // 两方机械轮廓/卡片根，只有局部进攻位移。
    UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> ArenaHealthBars; // 两方公开生命回放，不读取世界战斗对象。
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ArenaHealthText; // 当前回放生命文本，根树销毁即失效。
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ExplosionPieces; // 局部爆炸核心、环、火星、烟，24个固定UMG图像。
    TArray<int32> ReelSymbolIndices; // 图标与文字的当前索引；只在变化时替换Brush，不逐帧加载。
    int32 AnimatedMode=INDEX_NONE; // 本次结果树的模式，-1表示选项页；不持久化。
};
