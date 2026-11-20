// 职责：Editor真实UMG几何回归。使用：截图前对当前激活页调用Validate；依赖完成至少一帧Slate布局，非Editor不暴露接口。
#pragma once
#include "CoreMinimal.h"
#if WITH_EDITOR
class UDemoMenuScreen;
namespace DemoUILayoutChecks
{
    /** Name为专项截图短名；显式DemoUIScreenshotDir仅允许项目Art/UI或Saved内路径，默认沿用既有截图目录。 */
    FPSDEMO_API FString ScreenshotPath(const FString& Name);
    /** 检查可见操作互不重叠、固定区不侵入正文及紧凑调节器；Error返回第一个实际几何失败，不模拟布局算法。 */
    bool Validate(UDemoMenuScreen* Screen,FString& Error);
    /** 截图同帧排队弱页面，在Slate完成后的OnEndFrame采样；避免下一Tick已重建正文导致读取空缓存。 */
    void Queue(UDemoMenuScreen* Screen);
    /** 每个测试Tick在截止时间判断之前调用；0无待办，1通过，-1失败，Error包含实测原因。 */
    int32 ProcessPending(FString& Error);
}
#endif
