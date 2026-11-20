// 职责：正常游戏视口的最终View分发；保留CommonUI处理后，让每个本机PC的组件管理自己的轮廓。
// 使用：Config选择本类；不读取专项flags、不改输入/玩法，不让一个玩家的View驱动另一个玩家的捕获。
// 依赖：引擎公开LocalPlayer/View接口与DemoLocalOutlineComponent；所有借用均在当前GT调用栈内完成。
#include "Interaction/HubShops/Rendering/DemoLocalOutlineViewportClient.h"
#include "Interaction/HubShops/Rendering/DemoLocalOutlineComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void UDemoLocalOutlineViewportClient::FinalizeViews(FSceneViewFamily* Family,const TMap<ULocalPlayer*,FSceneView*>& Views)
{
    DEMO_LOG_TICK();
    Super::FinalizeViews(Family,Views);
    UWorld* ActiveWorld=GetWorld(); // 当前视口真实World；不使用全局World或Player0，避免PIE/旅行串到别的实例。
    if(!Family||!ActiveWorld)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("LOCAL_OUTLINE_VIEWPORT_SKIP reason=MissingFamilyOrWorld"));
        return;
    }
    for(const TPair<ULocalPlayer*,FSceneView*>& Entry:Views) // 本次引擎建立的LocalPlayer/View配对，不保留跨帧引用。
    {
        ULocalPlayer* LocalPlayer=Entry.Key; // 仅此视图的本机玩家；组件自行校验当前Avatar和Camera。
        FSceneView* LocalView=Entry.Value; // 引擎拥有的当前活View，调用返回后不再访问。
        APlayerController* LocalController=LocalPlayer?LocalPlayer->GetPlayerController(ActiveWorld):nullptr; // 必须从同一World解析本玩家PC。
        if(!LocalPlayer||!LocalView||!LocalController||!LocalController->IsLocalController())
        {
            UE_LOG(LogFPSDemo,VeryVerbose,TEXT("LOCAL_OUTLINE_VIEWPORT_SKIP reason=MissingViewOrLocalController"));
            continue;
        }
        if(UDemoLocalOutlineComponent* Outline=LocalController->FindComponentByClass<UDemoLocalOutlineComponent>()) // PC默认子对象拥有其私有资源，不在视口里临时创建。
            Outline->FinalizeLocalView(Family,LocalPlayer,LocalView);
        else
            UE_LOG(LogFPSDemo,VeryVerbose,TEXT("LOCAL_OUTLINE_VIEWPORT_SKIP reason=MissingOutlineComponent"));
    }
}
