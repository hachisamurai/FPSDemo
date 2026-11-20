// 职责：下一关终端的公开队伍准备页；读取GS和本人菜单投影，不读取其他玩家钱包。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "Game/DemoGameState.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Debug/DemoLog.h"

void ADemoHUD::DrawTeamReadyMenu(const ADemoGameState* State)
{
    DEMO_LOG_TICK();
    const FDemoTeamReadySnapshot& Team=State->TeamReady; // 引用仅本帧使用，晚到复制可改变版本和人数。
    const FDemoPlayerMenuSnapshot& View=GetPlayerView(); // 本人权限和解锁，绝不使用房主本地存档猜测。
    const ADemoPlayerController* PC=Cast<ADemoPlayerController>(GetOwningPlayerController()); // 本HUD拥有者。
    const ADemoPlayerState* Self=PC?PC->GetPlayerState<ADemoPlayerState>():nullptr; // 仅用于选中本人行，不授权交易。
    const float X=ViewWidth*.5f-450.f,Y=ViewHeight*.5f-282.f; // 900×564设计坐标，最多4行。
    Panel(X,Y,900,564,GetPanelSurface(),12);
    Label(TEXT("全队准备"),X+30,Y+26,30,DemoUI::Ink);
    const TCHAR* Difficulty=State->Difficulty==EDemoDifficulty::Easy?TEXT("简单"):State->Difficulty==EDemoDifficulty::Normal?TEXT("普通"):State->Difficulty==EDemoDifficulty::Hard?TEXT("困难"):TEXT("地狱"); // 仅展示枚举，不决定战斗数值。
    Label(FString::Printf(TEXT("%s · %s · 即将进入第 %d 关"),State->bEndless?TEXT("无尽"):TEXT("战役"),Difficulty,State->LevelNumber+1),X+30,Y+74,17,DemoUI::Gold);
    if(State->Phase==EDemoPhase::Hub)
    {
        Button(TEXT("Easy"),TEXT("简单"),X+30,Y+108,150,38,View.bCanLeadSession);
        Button(TEXT("Normal"),TEXT("普通"),X+198,Y+108,150,38,View.bCanLeadSession);
        Button(TEXT("Hard"),TEXT("困难"),X+366,Y+108,150,38,View.bCanLeadSession);
        Button(TEXT("Hell"),View.Progress.bHellUnlocked?TEXT("地狱"):TEXT("地狱 · 锁定"),X+534,Y+108,150,38,View.bCanLeadSession&&View.Progress.bHellUnlocked);
        Button(TEXT("Endless"),View.Progress.bEndlessUnlocked?TEXT("无尽"):TEXT("无尽 · 锁定"),X+702,Y+108,168,38,View.bCanLeadSession&&View.Progress.bEndlessUnlocked);
    }
    else Label(TEXT("本轮难度已锁定；购物或更换装备后需要重新准备。"),X+30,Y+118,15,DemoUI::Muted);
    bool bSelfReady=false,bCanReady=false; // 本帧按钮状态只消费服务端投影。
    for(int32 Index=0;Index<FMath::Min(4,Team.Members.Num());++Index)
    {
        const FDemoTeamReadyMember& Member=Team.Members[Index]; // 公开身份与核心，未包含任何私人经济值。
        const float RowY=Y+170+Index*60.f; // 每行核心、状态及原因占两行，长名字有固定上限。
        Label(Member.Name.Left(18)+(Member.Player==Self?TEXT("（你）"):TEXT("")),X+30,RowY,17,DemoUI::Ink);
        Label(Member.CoreId.IsNone()?TEXT("未选核心 / 旧模式"):DemoRunBuild::Title(Member.CoreId).ToString(),X+280,RowY,16,DemoUI::Muted);
        Label(Member.bReady?TEXT("已准备"):TEXT("未准备"),X+716,RowY,17,Member.bReady?DemoUI::Accent:DemoUI::Gold);
        Label(Member.BlockReason.IsEmpty()?(Member.bReady?TEXT("等待房主出发"):TEXT("请点击准备")):Member.BlockReason,X+280,RowY+25,13,DemoUI::Muted);
        if(Member.Player==Self) { bSelfReady=Member.bReady; bCanReady=Member.BlockReason.IsEmpty(); }
    }
    Label(Team.bCanDepart?TEXT("全员准备完成，房主可以出发。"):Team.BlockReason.Left(48),X+30,Y+420,15,Team.bCanDepart?DemoUI::Accent:DemoUI::Gold);
    Label(Team.LastChangeReason.Left(48),X+30,Y+448,13,DemoUI::Muted);
    if(PC) Label(PC->GetMenuMessage().Left(56),X+30,Y+470,12,DemoUI::Muted); // 旧版本/保存失败等RPC反馈不能只留日志。
    Button(TEXT("NextLevelNo"),TEXT("关闭并取消准备"),X+30,Y+492,250,44,true,true);
    Button(bSelfReady?TEXT("TeamNotReady"):TEXT("TeamReady"),bSelfReady?TEXT("取消准备"):TEXT("准备"),X+310,Y+492,250,44,bSelfReady||bCanReady);
    Button(TEXT("NextLevelYes"),View.bCanLeadSession?TEXT("全队出发 [ENTER]"):TEXT("等待房主出发"),X+590,Y+492,280,44,View.bCanLeadSession&&Team.bCanDepart);
}
