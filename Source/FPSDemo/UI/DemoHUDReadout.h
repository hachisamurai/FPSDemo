// 职责：Canvas只读读数的有限格式与量条边界。使用：生命/弹药/观战HUD共用，不改变GAS、钱包或复制数据。
// 大浮点用科学记数保留阅读宽度；钱包始终由原完整整数格式处理，不能用缩写隐藏交易金额。
#pragma once
#include "CoreMinimal.h"
#include "Debug/DemoLog.h"

namespace DemoHUDReadout
{
    /** Value为当前只读健康/上限/伤害值，Decimals<=0整值、>0保留1位；超大/超小非零值用有界科学记数，非有限显示同步占位。 */
    inline FString Number(float Value,int32 Decimals=0)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoHUDReadout::Number"));
        if(!FMath::IsFinite(Value)) return TEXT("—");
        const float Magnitude=FMath::Abs(Value); // 纯展示阈值，不截断真正属性或存档值。
        if(Magnitude>=100000.f||(Magnitude>0.f&&Magnitude<.01f)) return FString::Printf(TEXT("%.1e"),static_cast<double>(Value));
        return Decimals>0?FString::Printf(TEXT("%.1f"),static_cast<double>(Value)):FString::Printf(TEXT("%.0f"),static_cast<double>(Value));
    }
    /** Value/Maximum均为本帧公开读数；无效/未同步返回空条，有限输入才钳制0..1，防NaN进入Canvas顶点。 */
    inline float Fraction(float Value,float Maximum)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoHUDReadout::Fraction"));
        return FMath::IsFinite(Value)&&FMath::IsFinite(Maximum)&&Maximum>0.f?FMath::Clamp(Value/Maximum,0.f,1.f):0.f;
    }
}
