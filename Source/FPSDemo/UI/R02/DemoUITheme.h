// 职责：R02电影感只读显示主题与排版帮助接口。S0样式板和S1武器/弹药及HUD显式消费此资产，不全局替换R04或业务配置。
// 依赖：UMG/Slate与现有DemoUIFont只读回退。字体/材质由Editor作者流程赋值并随主题强引用；本类不加载路径、不持World、不复制或存盘。
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "DemoUITheme.generated.h"

class UFont;
class UMaterialInterface;

/** 候选主题只定义视觉：页面显式选取，权限、页面状态、经济和存档仍由现有Flow/PC裁决。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoUITheme : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 初始化R02默认色/尺寸，不读取尚未创建的新资源；资产作者随后赋字体与材质。 */
    UDemoUITheme();

    /** 返回所选衬线字体；Size为DPI前point，Weight只允许Regular/SemiBold。缺失回现有字体并记录，绝不伪造成功加载。 */
    FSlateFontInfo GetSerifFont(int32 Size, FName Weight = TEXT("Regular"), bool bPreferChinese = false) const;

    /** 返回已打包Sans标签字体，bDisplay为英文/数字优先；不修改UFont共享对象，也不支持任意系统字体路径。 */
    FSlateFontInfo GetLabelFont(int32 Size, FName Weight = TEXT("Regular"), bool bDisplay = false) const;

    // 以下颜色均为线性RGBA；默认由sRGB色值正确转换，不参与业务授权/复制或保存。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Color") FLinearColor Base;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Color") FLinearColor Ink;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Color") FLinearColor Muted;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Color") FLinearColor Ember;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Color") FLinearColor Disabled;

    // 装饰线Alpha，范围0..1；厚度由最终几何缩放换算，不使用此参数表达关键操作权限。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Line", meta = (ClampMin = "0.0", ClampMax = "1.0")) float HairlineOpacity = .23f;
    // 全屏模态暗化Alpha，范围0..1；只为现有模态提供视觉，不能代替其输入阻塞。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Atmosphere", meta = (ClampMin = "0.0", ClampMax = "1.0")) float ModalOpacity = .82f;
    // 菜单颗粒强度，范围0..1；0可关闭，减少UI动效时冻结时间而非改变业务等待。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Atmosphere", meta = (ClampMin = "0.0", ClampMax = "1.0")) float GrainStrength = .025f;
    // 菜单暗角强度，范围0..1；不在普通战斗HUD使用，不修改相机/场景曝光。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Atmosphere", meta = (ClampMin = "0.0", ClampMax = "1.0")) float VignetteStrength = .22f;
    // 可选大厅/终局遮幅目标宽高比；仅是排版输入，4:3或内容不足时页面应关闭遮幅。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Atmosphere", meta = (ClampMin = "1.0", ClampMax = "4.0")) float LetterboxAspect = 2.39f;
    // Boss条候选物理像素厚度，S0仅展示比较，S1实机未验前不能宣称冻结。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Line", meta = (ClampMin = "1.0", ClampMax = "6.0")) float BossLinePhysicalPixels = 2.f;

    // 作者赋值的持久化字体强引用；标题/数字Latin优先，中文标题Chinese优先，各复合字体自行提供回退。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Font") TObjectPtr<UFont> SerifLatinFont;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Font") TObjectPtr<UFont> SerifChineseFont;
    // 小正文/按键沿既有Sans资产；可空，空值由查询接口回现有已打包字体，不依赖Windows安装。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Font") TObjectPtr<UFont> LabelFont;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Font") TObjectPtr<UFont> LabelDisplayFont;
    // UI域颗粒/暗角材质强引用；不能借此读取/去色其后实时World，页面负责层序和无命中显示。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "R02|Atmosphere") TObjectPtr<UMaterialInterface> AtmosphereMaterial;
};

namespace DemoUIR02
{
    /** 纯显示关数：1..10返回I..X，超出返回阿拉伯数；不钳制/改写真实关卡，也不写存档。 */
    FPSDEMO_API FString ToRoman(int32 LevelNumber);

    /** 在Game/Slate线程以实际字体度量0..9最大advance并加2单位净空，返回DPI前格宽；Slate未就绪返回0并记录，不套Barlow常数。 */
    FPSDEMO_API float MeasureDigitCellWidth(const FSlateFontInfo& FontInfo);

    /** EffectiveLayoutScale为完整累计布局缩放，返回对应一物理像素的局部厚度；非法/零缩放返回1并记录，调用者仍须像素对齐。 */
    FPSDEMO_API float PhysicalHairlineThickness(float EffectiveLayoutScale);
}
