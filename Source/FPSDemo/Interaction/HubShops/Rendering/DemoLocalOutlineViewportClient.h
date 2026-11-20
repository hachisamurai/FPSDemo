// 职责：保留CommonUI视口行为，在正常主View建立后把各本机玩家的实际View交给其私有轮廓组件。
// 使用：由Engine.GameViewportClientClassName选择；PIE与Standalone共用，不依赖Editor审查参数。
// 依赖：CommonUI、ULocalPlayer与DemoLocalOutlineComponent；不拥有捕获资源或修改主投影/画质。
#pragma once

#include "CoreMinimal.h"
#include "CommonGameViewportClient.h"
#include "DemoLocalOutlineViewportClient.generated.h"

class ULocalPlayer;
class FSceneView;
class FSceneViewFamily;

/** 正常游戏的CommonUI派生视口；按实际LocalPlayer分发，不能用Player0替代其他本机玩家。 */
UCLASS()
class FPSDEMO_API UDemoLocalOutlineViewportClient : public UCommonGameViewportClient
{
    GENERATED_BODY()
public:
    /** 引擎完成本次Family/Views构建后调用；先保留CommonUI行为，再同步借用各LocalPlayer的活View。
     * Family与Views仅在当前调用栈有效；没有World/View/本机PC时跳过，不创建组件或保存裸View引用。
     */
    virtual void FinalizeViews(FSceneViewFamily* Family,const TMap<ULocalPlayer*,FSceneView*>& Views) override;
};
