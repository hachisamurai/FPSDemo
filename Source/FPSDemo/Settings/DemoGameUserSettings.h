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
    /** 返回本机倍率[0.1,3]，高频视角输入只读。 */
    float GetMouseSensitivity() const;
    /** Value为待保存倍率；应用由菜单显式触发ApplySettings，不在每次加减时写盘。 */
    void SetMouseSensitivity(float Value);
private:
    // Config写入GameUserSettings.ini；不复制、不属于任何战役存档。
    UPROPERTY(Config) float MouseSensitivity = 1.f;
};
