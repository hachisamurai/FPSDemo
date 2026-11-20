#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoSettingsFlow.generated.h"

/** MenuFlow拥有的设备设置草稿；无Tick/World对象引用，显示确认使用单调时间。 */
UCLASS()
class FPSDEMO_API UDemoSettingsFlow : public UObject
{
    GENERATED_BODY()
public:
    /** 打开设置时读Engine当前值，绝不在此写盘；不可用时保留错误文案。 */
    void Begin();
    /** Setting=0..4（4为垂直同步），Direction只能±1；有待确认显示变更时拒绝编辑。 */
    void Cycle(int32 Setting,int32 Direction);
    /** Setting=0..4；只读草稿文本，不执行磁盘/引擎设置写入。 */
    FString GetText(int32 Setting) const;
    /** 应用草稿；显示变化创建15秒回退窗口，其他设置立即保存。 */
    void Apply();
    /** bKeep由明确按钮或超时/卸载提供；只回退显示，保留已应用画质和鼠标。 */
    void Confirm(bool bKeep);
    /** 无待确认返回0；暂停也持续计时，由MenuFlow主动轮询。 */
    float GetRemainingSeconds() const;
    /** 只读待确认标志及超时边沿，不修改页面。 */
    bool HasPendingDisplay() const;
    bool HasExpired() const;
    /** 返回草稿拥有的状态文本，仅本同步调用借用。 */
    const FString& GetMessage() const;
private:
    TArray<FIntPoint> Resolutions; // 常见尺寸加当前自定义尺寸，单位像素。
    int32 ResolutionChoice=0; // 当前草稿索引，Apply必须验证范围。
    int32 QualityChoice=3; // 0..3，-1保留当前自定义画质。
    float SensitivityChoice=1.f; // 输入倍率0.1..3.0。
    int32 WindowChoice=2; // Engine EWindowMode值，不序列化。
    bool bVSyncChoice=true; // 本机帧同步草稿；应用后同时覆盖游戏和PIE呈现，允许玩家关闭。
    FIntPoint PreviousResolution; // 仅当前显示确认窗口内的回退像素尺寸。
    int32 PreviousWindowMode=2; // 待确认变更前的Engine窗口模式。
    double DisplayConfirmDeadline=0; // FPlatformTime秒，0表示没有显示事务。
    FString Message; // 局部设置反馈，MenuFlow选择何时展示到当前页。
};
