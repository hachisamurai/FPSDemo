// 职责：R04本机表现偏好与反馈音入口。使用：Widget仅读取偏好/请求本LocalPlayer短音，不拥有经济或业务成功状态。
// 依赖：GameUserSettings设备配置、UI音效资产、原LocalPlayer生命周期；不复制、不进入战役/云存档。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "DemoUIPresentation.generated.h"

class APlayerController;
class USoundBase;

/** 仅表现语义，Success必须由真实业务回执触发，按下按钮不能冒充成功。 */
enum class EDemoUISound : uint8 { Hover, Press, Back, Reject, Success };

namespace DemoUI
{
    /** 本机设备设置只读；配置尚未加载时false。不影响玩法时钟、服务器等待或原赌场跳过偏好。 */
    FPSDEMO_API bool ReducedMotion();
    /** 本机UI短音开关只读；默认true，缺声音资产时仍安全静音回退。 */
    FPSDEMO_API bool UISoundEnabled();
    /** 显式菜单点击后立即保存本机偏好，Value不修改战役、云数据或赌场随机结果。 */
    FPSDEMO_API void SetReducedMotion(bool Value);
    /** 显式菜单点击后立即保存本机UI音偏好，Value不控制世界音频/语音。 */
    FPSDEMO_API void SetUISoundEnabled(bool Value);
    /** Owner须为当前本LocalPlayer控制器；短音只在此设备播放，无Owner/远端/未启用时不创建声音。 */
    FPSDEMO_API void PlayFeedback(APlayerController* Owner,EDemoUISound Sound);
}

/** 常驻5个小音效的硬引用，Cook可达；不在按钮事件/每帧同步读盘。 */
UCLASS()
class FPSDEMO_API UDemoUIPresentationAssets : public UObject
{
    GENERATED_BODY()
public:
    /** CDO一次加载项目自产短音，缺资源允许作者首次导入后重启验证。 */
    UDemoUIPresentationAssets();
    /** Index对应EDemoUISound 0..4，返回本CDO借用对象，非法/缺失返回nullptr。 */
    USoundBase* GetSound(EDemoUISound Index) const;
private:
    UPROPERTY() TArray<TObjectPtr<USoundBase>> Sounds; // CDO强引用，生命周期同引擎，无网络/进度数据。
};

/** 每个LocalPlayer独立限频，关卡旅行不重复播放悬停/焦点声；无Tick。 */
UCLASS()
class FPSDEMO_API UDemoUIPresentationSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    /** 只接受本地菜单表现请求；交互反馈不推断购买成功，不向服务器提交任何动作。 */
    void Play(APlayerController* Owner,EDemoUISound Sound);
private:
    double LastHoverTime=-1.; // 本机单调秒；同帧悬停+键盘焦点共用80ms门禁，不持有World。
    double LastActionTime=-1.; // 同语义操作声40ms限频，不阻止任何真实按钮/业务请求。
    double LastResultTime=-1.; // 回执声独立60ms桶，Standalone同步回执不会被前面的Press吞掉。
    double LastBackTime=-1.; // 返回声独立桶，按钮返回与键盘返回共用去重。
};
