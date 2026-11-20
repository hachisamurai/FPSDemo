// 职责：本机首次操作提示的显示/关闭生命周期。使用：MenuFlow空闲更新触发，Settings记录已读；不参与战局保存和网络复制。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Settings/DemoGameUserSettings.h"
#include "Game/DemoGameState.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h" // 阶段查询使用World完整类型，独立翻译单元不依赖Unity包含顺序。
#include "Debug/DemoLog.h"

void UDemoMenuFlowComponent::TryShowFirstRunGuide()
{
    DEMO_LOG_TICK();
    if(bStopped||bFirstRunGuideDismissed||!GetController()->IsLocalController()) return;
    const UDemoGameUserSettings* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 引擎已加载的设备对象，仅内存查询，不逐帧读INI。
    if(!Settings) return; // 启动期间设备设置可能未就绪；不伪造已读，不为重试刷日志。
    if(Settings->HasDismissedFirstRunGuide()) { bFirstRunGuideDismissed=true; return; }
    const ULocalPlayer* Local=GetController()->GetLocalPlayer(); // 专服/远端控制器没有本地视口，不能替其他玩家关闭提示。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // GS晚到时等待，不依赖BeginPlay先后。
    if(!Local||!Local->ViewportClient||!State||(State->Phase!=EDemoPhase::Lobby&&State->Phase!=EDemoPhase::Hub)
        ||HasBlockingOverlay()||bMenuOpen||bNextLevelConfirmationOpen||bPauseMenuActive) return;
    MenuPage=EDemoMenuPage::FirstRunGuide;
    MenuMessage.Empty();
    RefreshMenuInput(); // 保留底层大厅/Hub，只阻塞本人输入；联机世界不暂停，也不创建服务器终端会话。
    UE_LOG(LogFPSDemo,Log,TEXT("FIRST_RUN_GUIDE opened for local player"));
}

void UDemoMenuFlowComponent::CloseFirstRunGuide()
{
    DEMO_LOG_CALL();
    if(bStopped||!GetController()->IsLocalController()||MenuPage!=EDemoMenuPage::FirstRunGuide)
    { UE_LOG(LogFPSDemo,Log,TEXT("FIRST_RUN_GUIDE close rejected owner/page/lifecycle")); return; }
    if(UDemoGameUserSettings* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())) Settings->DismissFirstRunGuide();
    else UE_LOG(LogFPSDemo,Warning,TEXT("FIRST_RUN_GUIDE missing device settings; dismissal only retained for this controller"));
    bFirstRunGuideDismissed=true;
    MenuPage=EDemoMenuPage::None;
    MenuMessage.Empty();
    RefreshMenuInput(); // 大厅保留鼠标，Hub回游戏输入；不吞掉底层奖励、取消整队准备或改存档。
}
