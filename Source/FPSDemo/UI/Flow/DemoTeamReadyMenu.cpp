// 职责：本机准备按钮意图；只提交拥有者PC请求，所有权限和版本校验在GM准备组件。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"

void UDemoMenuFlowComponent::SetTeamReady(bool bReady)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前已复制目录，不缓存上一关的版本。
    if(bStopped||!GetController()->IsLocalController()||HasBlockingOverlay()||!bNextLevelConfirmationOpen||!State||!State->TeamReady.bRequired)
    { UE_LOG(LogFPSDemo,Log,TEXT("TEAM_READY_UI rejected phase/page")); return; }
    FDemoGameplayCommandRequest Request; // 不传其他玩家身份或直接写PS；明确目标抵御双击反转。
    Request.Command=EDemoGameplayCommand::SetReady; Request.bReady=bReady; Request.ReadyRevision=State->TeamReady.Revision; Request.ExpectedLevel=State->LevelNumber;
    MenuMessage=TEXT("等待服务器确认准备状态"); GetController()->SubmitGameplayCommand(Request);
}
