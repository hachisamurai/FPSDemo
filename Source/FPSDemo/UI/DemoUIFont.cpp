// 职责：集中R04字体加载/查询，供CommonUI和Canvas共用；只返回显示配置，不读取或修改玩法。
// 使用：由DemoUIStyle包含公开声明；资源缺失时回退CoreStyle，正式资产由Editor命令生成，不依赖Windows字体。
#include "UI/DemoUIFont.h"
#include "Debug/DemoLog.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

UDemoUIFontSet::UDemoUIFontSet()
{
    DEMO_LOG_CALL();
    static ConstructorHelpers::FObjectFinder<UFont> TextResource(TEXT("/Game/UI/RedesignR04/Fonts/F_BreachText.F_BreachText")); // CDO资产引用使字体与FontFace可被Cook收集。
    static ConstructorHelpers::FObjectFinder<UFont> DisplayResource(TEXT("/Game/UI/RedesignR04/Fonts/F_BreachDisplay.F_BreachDisplay")); // 独立复合字体，正文与英文混排不改默认系统字体。
    TextFont=TextResource.Object;
    DisplayFont=DisplayResource.Object;
    if(!TextFont||!DisplayFont) UE_LOG(LogFPSDemo,Warning,TEXT("UIR_R04 fonts unavailable text=%d display=%d; engine fallback, run DemoCreateR04Assets then restart process"),TextFont!=nullptr,DisplayFont!=nullptr);
}

UFont* UDemoUIFontSet::GetTextFont() const { DEMO_LOG_TICK(); return TextFont; }
UFont* UDemoUIFontSet::GetDisplayFont() const { DEMO_LOG_TICK(); return DisplayFont; }

namespace DemoUI
{
    UFont* TextFontObject() { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] DemoUI.TextFontObject")); return GetDefault<UDemoUIFontSet>()->GetTextFont(); }
    UFont* DisplayFontObject() { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] DemoUI.DisplayFontObject")); return GetDefault<UDemoUIFontSet>()->GetDisplayFont(); }
    /** Font为CDO持有的可空资产，Size/Weight是调用方显示请求；查询不缓存FontInfo或改变字体内部状态。 */
    FSlateFontInfo ResolveFont(UFont* Font,int32 Size,FName Weight)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] DemoUI.ResolveFont"));
        const bool bKnownWeight=Weight==TEXT("Regular")||Weight==TEXT("Medium")||Weight==TEXT("SemiBold"); // UFont实际已生成的三种命名实例。
        if(!bKnownWeight) { UE_LOG(LogFPSDemo,Warning,TEXT("UIR_R04 unsupported typeface=%s; using Regular"),*Weight.ToString()); Weight=TEXT("Regular"); }
        const int32 ClampedSize=FMath::Clamp(Size,1,256); // 防止非法字号进入Slate；不把DPI缩放做两遍。
        return Font?FSlateFontInfo(Font,ClampedSize,Weight):FCoreStyle::GetDefaultFontStyle(Weight==TEXT("Regular")?TEXT("Regular"):TEXT("Bold"),ClampedSize);
    }

    FSlateFontInfo TextFont(int32 Size,FName Weight)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] DemoUI.TextFont"));
        return ResolveFont(GetDefault<UDemoUIFontSet>()->GetTextFont(),Size,Weight);
    }

    FSlateFontInfo DisplayFont(int32 Size,FName Weight)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] DemoUI.DisplayFont"));
        return ResolveFont(GetDefault<UDemoUIFontSet>()->GetDisplayFont(),Size,Weight);
    }

    FSlateFontInfo NumberFont(int32 Size,FName Weight)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] DemoUI.NumberFont"));
        FSlateFontInfo Font=DisplayFont(Size,Weight); // 本次调用独占配置副本，不更改共享Display或UFont资产。
        Font.bForceMonospaced=true; // UE5.4简单塑形按固定advance居中字形；不启用或冒充Barlow的GSUB tnum。
        Font.MonospacedWidth=.64f; // 比例×point字号×Scale，含96/72栅格余量，容纳Barlow Medium最宽数字；不是CSS tnum或像素常量。
        Font.LetterSpacing=0; // 纯数字不叠加tracking，避免额外间距再次改变固定字格。
        return Font;
    }
}
