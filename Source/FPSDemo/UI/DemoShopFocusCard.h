// 职责：枪边三行原生UMG浮签，无按钮/鼠标/交易权限。使用：本人Focus组件提供服务器投影与屏幕位置；依赖项目现有中文字体。
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DemoShopFocusCard.generated.h"
UCLASS()
class FPSDEMO_API UDemoShopFocusCard : public UUserWidget
{
    GENERATED_BODY()
public:
    /** 三行由权威投影/本机动作提示格式化，Size为DPI前逻辑尺寸。 */
    void SetLines(const FString& First, const FString& Second, const FString& Third, const FVector2D& Size);
protected:
    /** 首次构建小卡片，正常HitTestInvisible，保持GameOnly不夺光标。 */
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> Lines[3]; // WidgetTree拥有，只更新文本不逐帧重建。
    UPROPERTY(Transient) TObjectPtr<class USizeBox> Sizing; // 屏幕逻辑尺寸，不是世界厘米。
};
