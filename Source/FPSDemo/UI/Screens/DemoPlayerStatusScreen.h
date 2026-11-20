// 职责：Tab构筑页的只读UMG投影。使用：读取本地Avatar GAS、装备和拥有者快照，不激活技能或修改成长。
#pragma once
#include "UI/Common/DemoMenuScreen.h"
#include "DemoPlayerStatusScreen.generated.h"
UCLASS(Blueprintable)
class FPSDEMO_API UDemoPlayerStatusScreen : public UDemoMenuScreen
{
    GENERATED_BODY()
protected:
    /** 启用R02暗黑电影感个人构筑页，保持只读Avatar/Owner快照与分页合同不变。 */
    virtual bool UsesR02Presentation() const override;
    /** 模型键只包含天赋页/内容；高频生命、冷却以文本更新，避免持续拆卸焦点控件。 */
    virtual FString RefreshKey() const override;
    /** 创建R04实时指标/永久账本/装备核心与独立天赋侧栏；分页动作固定在正文外。 */
    virtual void BuildBody() override;
    /** 暂停也更新本地复制数值，只改现有文本，不执行GAS或经济操作。 */
    virtual void AnimateScreen(float DeltaTime) override;
private:
    /** 每0.1秒借用当前Pawn与拥有者视图更新现有字段；不反推/写入成长来源，换Avatar不使用旧裸指针。 */
    void UpdateValues();
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HealthText; // 当前生命大读数，纯数值保持批准的数字字体。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> AttributesText; // 当前树的属性/来源说明。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> GrowthText; // 永久成长直接取来源账本，不用GAS合计反推永久来源。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SkillsText; // 当前树的GAS冷却/技能效果。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> EquipmentText; // 当前树的武器弹药与遗物。
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CoreText; // 本人正式核心与3/7关选择，不包含队友私有构筑。
    double NextValueRefresh=0; // 单调秒，每0.1秒查询一次，合作暂停页仍实时刷新。
};
