// 职责：R04本地持久化字体资产的初始化引用与Slate字体查询；菜单/Canvas只读使用，不管理玩家数据。
// 依赖：Editor命令DemoCreateR04Assets生成的UFont复合资产。CDO硬引用参与Cook，缺资源回退引擎字体并记录原因。
#pragma once
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/Object.h"
#include "DemoUIFont.generated.h"

class UFont;

/** 字体资源仅由此CDO强持有；首次初始化加载一次，不在逐帧绘制中解析路径或加载磁盘。 */
UCLASS()
class FPSDEMO_API UDemoUIFontSet : public UObject
{
    GENERATED_BODY()
public:
    /** CDO初始化时加载两套复合字体；缺少候选资产不阻止Editor首次导入。 */
    UDemoUIFontSet();
    /** 返回中文正文复合字体；仅借用CDO持有的资产，不转移所有权，缺失返回nullptr。 */
    UFont* GetTextFont() const;
    /** 返回英文/数字优先且含中文Fallback的复合字体，缺失返回nullptr。 */
    UFont* GetDisplayFont() const;
private:
    UPROPERTY() TObjectPtr<UFont> TextFont; // 原Noto SC的静态400/500/600实例；Cook依赖与GC强引用，不复制。
    UPROPERTY() TObjectPtr<UFont> DisplayFont; // Barlow主体＋中文回退，Cook依赖与GC强引用，不复制。
};

namespace DemoUI
{
    /** Canvas持有UPROPERTY强引用时使用；CDO同时保有资源，缺资产返回nullptr而非临时CompositeFont。 */
    FPSDEMO_API UFont* TextFontObject();
    /** 返回品牌/数字复合UFont，生命周期同TextFontObject。 */
    FPSDEMO_API UFont* DisplayFontObject();
    /** Size为DPI前字号（1..256），Weight限Regular/Medium/SemiBold；返回可持久化FontObject配置，不写资产。 */
    FPSDEMO_API FSlateFontInfo TextFont(int32 Size,FName Weight=TEXT("Regular"));
    /** 英文标题/型号/数字用此接口，中文由复合字体回退；输入限制与TextFont相同。 */
    FPSDEMO_API FSlateFontInfo DisplayFont(int32 Size,FName Weight=TEXT("Regular"));
    /** 纯ASCII数字读数专用UE伪等宽：每字宽度为.64×point字号×实际字体Scale；默认Medium，不是字体tnum特性。
     * 只给独立数字TextBlock/Canvas段使用，禁止套整段含中文的钱包/说明；Auto简单塑形路径下生效，布局与实际渲染另验。
     */
    FPSDEMO_API FSlateFontInfo NumberFont(int32 Size,FName Weight=TEXT("Medium"));
}
