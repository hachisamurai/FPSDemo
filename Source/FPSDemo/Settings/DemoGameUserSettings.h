// 职责：设备级设置与持久偏好。使用：菜单调用带日志的读写接口，保存到GameUserSettings.ini，不进入战役或云存档。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "DemoGameUserSettings.generated.h"

/** 引擎本地设置扩展；画质/分辨率继承标准实现，鼠标倍率单独持久化。 */
UCLASS(Config=GameUserSettings)
class FPSDEMO_API UDemoGameUserSettings : public UGameUserSettings
{
    GENERATED_BODY()
public:
    /** 初始化新机器的灵敏度，保持模板1倍输入。 */
    UDemoGameUserSettings();
    /** bForceReload为引擎强制重载选项；读取后钳制外部编辑的非法倍率。 */
    virtual void LoadSettings(bool bForceReload = false) override;
    /** 引擎发现设备配置缺失/版本失效时重置；同时重置迁移标志，防止ValidateSettings覆盖首次VSync默认值。 */
    virtual void SetToDefaults() override;
    /** 菜单应用设备设置后同步游戏及Editor视口的VSync；不改变镜头位置或网络预测。 */
    virtual void ApplyNonResolutionSettings() override;
    /** 本地视口创建和设置应用时调用；显式控制台/启动参数的更高优先级仍保留。 */
    void ApplyFrameSync();
    /** 返回本机倍率[0.1,3]，高频视角输入只读。 */
    float GetMouseSensitivity() const;
    /** Value为待保存倍率；应用由菜单显式触发ApplySettings，不在每次加减时写盘。 */
    void SetMouseSensitivity(float Value);
    /** 本机抽取表现偏好；不上传、不影响权威奖励或概率。 */
    bool IsGambleAnimationSkipped() const;
    /** bSkip=true直接显示已提交结果；菜单点击时立即保存设备设置。 */
    void SetGambleAnimationSkipped(bool bSkip);
    /** 高频UI只读：本机是否已明确关闭新手操作提示；跨存档、重启与Editor试玩保留，不复制或上传。 */
    bool HasDismissedFirstRunGuide() const;
    /** 玩家点击“知道了”或关闭提示时调用；立即保存本机已读标记，不改变任何游戏进度。 */
    void DismissFirstRunGuide();
    /** 高频菜单只读设备偏好；为true时停用位移/滚动揭示等装饰动效，不改变权威等待和结果。 */
    bool IsUIMotionReduced() const;
    /** 用户设置页显式调用；bReduced立即保存设备设置，不上传战役/云。 */
    void SetUIMotionReduced(bool bReduced);
    /** 本机UI短音开关，不控制世界音效和队伍语音。 */
    bool IsUISoundEnabled() const;
    /** bEnabled由当前本机设置页切换，立即保存。 */
    void SetUISoundEnabled(bool bEnabled);
    /** 本机受击镜头Roll强度[0,1]，0完全关闭；只读设备值，不改变武器后坐力。 */
    float GetDamageShakeScale() const;
    /** Value为受击Roll倍率[0,1]，非法值回退1；本机设置页立即保存，不写战役/云。 */
    void SetDamageShakeScale(float Value);
    /** 本机受击屏幕边缘闪光倍率[0,1]，不隐藏方向、低血与实际生命信息。 */
    float GetDamageFlashScale() const;
    /** Value为边缘闪光倍率[0,1]，非法值回退1；立即保存设备设置。 */
    void SetDamageFlashScale(float Value);
    /** 本机受伤人声开关，不控制接触音、队伍语音或敌人声音。 */
    bool IsHurtVoiceEnabled() const;
    /** bEnabled仅控制本人受伤人声；设置页切换立即保存。 */
    void SetHurtVoiceEnabled(bool bEnabled);
    /** 本机低血呼吸/警示音开关，视觉生命提示仍保留。 */
    bool IsLowHealthAudioEnabled() const;
    /** bEnabled仅控制本人低血循环音；设置页切换立即保存。 */
    void SetLowHealthAudioEnabled(bool bEnabled);
private:
    UPROPERTY(Config) float DamageShakeScale=1.f; // 本机受击最终Roll倍率[0,1]；默认完整强度，不复制/上传。
    UPROPERTY(Config) float DamageFlashScale=1.f; // 本机短促边缘闪光倍率[0,1]；与原界面动效偏好独立。
    UPROPERTY(Config) bool bHurtVoiceEnabled=true; // 本人受伤人声默认开启，跨档/重启保留。
    UPROPERTY(Config) bool bLowHealthAudioEnabled=true; // 本人低血呼吸默认开启，不改变生命阈值或伤害。
    UPROPERTY(Config) bool bReduceUIMotion=false; // 设备共享偏好，跨档/重启保留，不复制。
    UPROPERTY(Config) bool bEnableUISound=true; // UI反馈默认开启；与世界/语音音量分离。
    UPROPERTY(Config) bool bFirstRunGuideDismissed = false; // 首次安装/无配置时显示；普通画质恢复默认不重置，删除本机设置文件才重新提示。
    /** 默认播放；跨存档共享本机偏好，Editor结束也保留。 */
    UPROPERTY(Config) bool bSkipGambleAnimation = false;
    // Config写入GameUserSettings.ini；不复制、不属于任何战役存档。
    UPROPERTY(Config) float MouseSensitivity = 1.f;
    // 旧UI没有提供VSync选项且继承引擎False；首次迁移开启，后续尊重玩家在设置中的选择。
    UPROPERTY(Config) int32 FrameSyncSettingsVersion = 0;
};
