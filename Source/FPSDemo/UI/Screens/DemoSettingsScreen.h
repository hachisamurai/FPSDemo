// 职责：R04设备设置草稿与本机表现偏好页。使用：0..4仍交MenuFlow/显示回退，UI与受击舒适度由独立设备接口即时保存。
#pragma once
#include "UI/Common/DemoMenuScreen.h"
#include "DemoSettingsScreen.generated.h"
UCLASS(Blueprintable)
class FPSDEMO_API UDemoSettingsScreen : public UDemoMenuScreen
{
    GENERATED_BODY()
protected:
    /** 启用R02暗黑电影感变体，保持设置草稿、15秒显示确认与本机偏好逻辑不变。 */
    virtual bool UsesR02Presentation() const override;
    /** 五项草稿、提示或本机表现偏好变化才重建，持久化不属于Widget。 */
    virtual FString RefreshKey() const override;
    /** R04图像/操作分栏、五项原草稿与即时表现偏好；显示回退沿原确认流程。 */
    virtual void BuildBody() override;
private:
    /** 构建固定52/228/52尺寸、14净空的设置行；Parent为本次树，Index仍为原0..4，Name仅展示，值/应用由Flow决定。 */
    void AddSettingRow(UPanelWidget* Parent,int32 Index,const FString& Name);
    /** 构建与数值行同起点/右边缘的偏好行；Parent为本次树，Action为已有意图，Name/Detail仅展示，EnabledValue只读，不写设备数据。 */
    void AddPresentationRow(UPanelWidget* Parent,FName Action,const FString& Name,bool EnabledValue,const FString& Detail);
    /** Parent属于本次树；MinusAction/PlusAction为本机即时偏好意图，Name为展示名，Value为[0,1]强度，只创建有界步进控件。 */
    void AddFeedbackScaleRow(UPanelWidget* Parent,FName MinusAction,FName PlusAction,const FString& Name,float Value);
};
