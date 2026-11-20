// 职责：本人属性/天赋面板的打开与翻页；由PC的Tab触发，依赖菜单门禁与拥有者进度投影。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h" // 显式依赖公开阶段定义，独立翻译单元不能依赖Unity包含顺序。
#include "GAS/DemoAttributeSet.h"
#include "Debug/DemoLog.h"

void UDemoMenuFlowComponent::TabPressed()
{
    DEMO_LOG_CALL();
    if(IsCasinoBetInputOpen()) { CloseCasinoBetInput(); return; } // 原生输入层拥有键盘时不关闭底层终端。
    if(bStopped||!GetController()->IsLocalController()) { UE_LOG(LogFPSDemo,Log,TEXT("STATUS_REJECT lifecycle/owner")); return; }
    if(MenuPage==EDemoMenuPage::FirstRunGuide) { CloseFirstRunGuide(); return; } // 与Esc/关闭按钮共用设备持久已读，不穿透打开属性页。
    if(MenuPage==EDemoMenuPage::PlayerStatus) { CloseOverlay(); return; }
    if(HasBlockingOverlay()) { UE_LOG(LogFPSDemo,Log,TEXT("STATUS_REJECT existing overlay")); return; }
    // 原有终端Tab关闭行为优先，不能使用只读面板跳过奖励、确认或抽取事务。
    if(bMenuOpen||bNextLevelConfirmationOpen) { CloseUpgradeMenu(); return; }
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前World公开阶段，本地只读。
    const ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetController()->GetPawn()); // 不跨重生缓存Avatar。
    if(!State||(State->Phase!=EDemoPhase::Hub&&State->Phase!=EDemoPhase::Combat&&State->Phase!=EDemoPhase::Intermission)
        ||!Pawn||Pawn->IsCoopEliminated()||!Pawn->GetDemoAttributes()||Pawn->GetDemoAttributes()->GetHealth()<=0.f)
    { UE_LOG(LogFPSDemo,Log,TEXT("STATUS_REJECT phase/avatar")); return; }
    if(!GetController()->SetLocalMenuPaused(true)) { UE_LOG(LogFPSDemo,Warning,TEXT("STATUS_REJECT pause unavailable")); return; }
    StatusPage=0; bPauseMenuActive=true; MenuPage=EDemoMenuPage::PlayerStatus;
    RefreshMenuInput(); // 单人暂停，网络仅阻断本人移动/战斗，World继续运行。
    FDemoGameplayCommandRequest Request; // 请求最新只读投影，不传等级或属性；冷却实时读取复制ASC。
    Request.Command=EDemoGameplayCommand::RefreshView; GetController()->SubmitGameplayCommand(Request);
    UE_LOG(LogFPSDemo,Log,TEXT("STATUS_OPEN local read-only panel"));
}
int32 UDemoMenuFlowComponent::GetStatusPage() const
{
    DEMO_LOG_TICK();
    const int32 Count=GetController()->GetMenuSnapshot().OwnedTalentViews.Num(); // 服务器已拥有天赋，分页不截断目录。
    return FMath::Clamp(StatusPage,0,FMath::Max(0,(Count-1)/5));
}
void UDemoMenuFlowComponent::ChangeStatusPage(int32 Direction)
{
    DEMO_LOG_CALL();
    if(bStopped||MenuPage!=EDemoMenuPage::PlayerStatus||(Direction!=-1&&Direction!=1))
    { UE_LOG(LogFPSDemo,Log,TEXT("STATUS_PAGE_REJECT page/direction")); return; }
    StatusPage=GetStatusPage()+Direction; StatusPage=GetStatusPage(); // 钳制旧点击，不产生业务请求。
}
