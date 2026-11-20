// 职责：R02显示Token、字体查询及确定性小排版工具；S0候选作者与样式板显式使用，不更改旧UI路由或业务。
// 使用：主题由UDataAsset拥有字体/材质引用，绘制只读；数字格按当前字体真实measure，缩放/最终像素对齐由表现调用方处理。
#include "UI/R02/DemoUITheme.h"

#include "Debug/DemoLog.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "UI/DemoUIFont.h"

UDemoUITheme::UDemoUITheme()
{
    DEMO_LOG_CALL();
    Base = FLinearColor(FColor(13, 12, 11));
    Ink = FLinearColor(FColor(233, 227, 215));
    Muted = FLinearColor(FColor(110, 115, 119));
    Ember = FLinearColor(FColor(180, 71, 47));
    Disabled = Ink.CopyWithNewOpacity(.32f);
}

FSlateFontInfo UDemoUITheme::GetSerifFont(int32 Size, FName Weight, bool bPreferChinese) const
{
    DEMO_LOG_TICK();
    if (Weight != TEXT("Regular") && Weight != TEXT("SemiBold"))
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("R02_FONT unsupported serif weight=%s; using Regular"), *Weight.ToString());
        Weight = TEXT("Regular");
    }
    UFont* Font = bPreferChinese ? SerifChineseFont.Get() : SerifLatinFont.Get(); // 仅本帧借用主题持有资源；不解析路径或加载磁盘。
    const int32 SafeSize = FMath::Clamp(Size, 1, 256); // 避免非法字号流入Slate；不同时应用DPI缩放。
    if (!Font)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("R02_FONT serif missing chinese=%d; using packaged Sans fallback"), bPreferChinese);
        return bPreferChinese ? DemoUI::TextFont(SafeSize, Weight) : DemoUI::DisplayFont(SafeSize, Weight);
    }
    return FSlateFontInfo(Font, SafeSize, Weight);
}

FSlateFontInfo UDemoUITheme::GetLabelFont(int32 Size, FName Weight, bool bDisplay) const
{
    DEMO_LOG_TICK();
    if (Weight != TEXT("Regular") && Weight != TEXT("Medium") && Weight != TEXT("SemiBold"))
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("R02_FONT unsupported label weight=%s; using Regular"), *Weight.ToString());
        Weight = TEXT("Regular");
    }
    UFont* Font = bDisplay ? LabelDisplayFont.Get() : LabelFont.Get(); // 主题作者显式选择本地资产，不依赖系统字体。
    const int32 SafeSize = FMath::Clamp(Size, 1, 256);
    return Font ? FSlateFontInfo(Font, SafeSize, Weight) : bDisplay ? DemoUI::DisplayFont(SafeSize, Weight) : DemoUI::TextFont(SafeSize, Weight);
}

FString DemoUIR02::ToRoman(int32 LevelNumber)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[TICK] DemoUIR02.ToRoman level=%d"), LevelNumber);
    static const TCHAR* RomanLevels[] = { TEXT("I"), TEXT("II"), TEXT("III"), TEXT("IV"), TEXT("V"), TEXT("VI"), TEXT("VII"), TEXT("VIII"), TEXT("IX"), TEXT("X") }; // 固定十关显示表，不成为权威目录。
    if (LevelNumber >= 1 && LevelNumber <= UE_ARRAY_COUNT(RomanLevels)) return RomanLevels[LevelNumber - 1];
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("R02_ROMAN outside ten-stage range=%d; retaining Arabic level"), LevelNumber);
    return FString::FromInt(LevelNumber);
}

float DemoUIR02::MeasureDigitCellWidth(const FSlateFontInfo& FontInfo)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[TICK] DemoUIR02.MeasureDigitCellWidth size=%.2f"), FontInfo.Size);
    if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("R02_DIGITS measure unavailable before Slate renderer initialization"));
        return 0.f;
    }
    const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService(); // Slate资源的同步引用，本调用结束即释放，不能跨线程使用。
    float Widest = 0.f; // 当前字体真实数字advance的最大值，DPI前Slate单位。
    for (TCHAR Digit = TEXT('0'); Digit <= TEXT('9'); ++Digit) Widest = FMath::Max(Widest, static_cast<float>(FontMeasure->Measure(FString::Chr(Digit), FontInfo).X));
    return FMath::CeilToFloat(Widest) + 2.f; // 两单位净空给相邻笔画；不强制字体内部伪等宽或缩放字形。
}

float DemoUIR02::PhysicalHairlineThickness(float EffectiveLayoutScale)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[TICK] DemoUIR02.PhysicalHairlineThickness scale=%.4f"), EffectiveLayoutScale);
    if (!FMath::IsFinite(EffectiveLayoutScale) || EffectiveLayoutScale <= SMALL_NUMBER)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("R02_HAIRLINE invalid accumulated scale=%f; using local one-unit fallback"), EffectiveLayoutScale);
        return 1.f;
    }
    return 1.f / EffectiveLayoutScale;
}
