// 职责：S1 R02个人阵亡后的无底板观战信息层。使用：DrawHUD在正常战斗信息之前调用；本人复活后立即自停，不参与观战切换或复活裁决。
// 依赖：本机权威观战Camera、GS公开阶段/队伍及Pawn公开生命。只显示观察对象，不读队友钱包/成长/分支/弹药库存。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h" // R02观战只用既有主题前景，背景始终是服务器指定观战相机的实时World。
#include "UI/DemoHUDReadout.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoAttributeSet.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/Coop/DemoCoopSpectatorCamera.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyRoster.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"

void ADemoHUD::DrawSpectatorStatus(ADemoPlayerController* PC)
{
    DEMO_LOG_TICK();
    const auto* Self=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 本人当前Pawn，仅本帧借用；不把ViewTarget当成本人权限。
    const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 当前World公开阶段，不能用相机位置推断复活/结算。
    if(!PC||!IsValid(Self)||!Self->IsCoopEliminated()||!State||State->Phase==EDemoPhase::Lobby||State->Phase==EDemoPhase::Victory||State->Phase==EDemoPhase::Defeat) return;
    const auto* Camera=Cast<ADemoCoopSpectatorCamera>(PC->GetViewTarget()); // 读取既有本机相机的弱目标，不发RPC、不自行挑选或Possess队友。
    const auto* Observed=Camera?Camera->GetObservedCharacter():Cast<ADemoCharacter>(PC->GetViewTarget()); // 兼容权威旧视图，仍不访问远端Controller。
    const auto* ObservedState=Observed?Observed->GetPlayerState<ADemoPlayerState>():nullptr; // 公开身份，不获取目标拥有者菜单快照。
    const bool TargetLive=IsValid(Observed)&&Observed!=Self&&!Observed->IsCoopEliminated(); // 目标失效等服务器切换，不显示旧目标为存活。
    if(IsR02GameplayPresentation())
    {
        DrawR02BossHeader(State,GetR02LiveBoss()); // 同一真实Boss公开名/关数/阶段/生命；本人阵亡不显示过期个人生命或队友私有资源。
        const float CenterX=ViewWidth*.5f,Y=ViewHeight-142.f; // 底部只有姓名/公开生命/等待与Esc，保持战斗中心和实际观战镜头。
        const FString Name=TargetLive?(ObservedState?ObservedState->GetPlayerName():FString(TEXT("姓名同步中"))):TEXT("等待观战目标");
        Label(TEXT("观战"),CenterX,Y-22.f,12.f,R02HUDTheme->Muted,true);
        Label(Name.Left(12),CenterX,Y,26.f,R02HUDTheme->Ink,true);
        const UDemoAttributeSet* Attributes=TargetLive?Observed->GetDemoAttributes():nullptr; // 只读目标原公开HP，不访问目标Controller/钱包/成长/弹药或复制私有值。
        if(Attributes)
        {
            Label(DemoHUDReadout::Number(Attributes->GetHealth())+TEXT(" / ")+DemoHUDReadout::Number(Attributes->GetMaxHealth()),CenterX,Y+38.f,18.f,R02HUDTheme->Ink,true);
            R02HUDLine(CenterX-150.f,Y+66.f,300.f,DemoHUDReadout::Fraction(Attributes->GetHealth(),Attributes->GetMaxHealth()),R02HUDTheme->Ink);
        }
        else if(TargetLive) Label(TEXT("生命同步中"),CenterX,Y+38.f,14.f,R02HUDTheme->Muted,true); // 缺值不伪造满血或0/100。
        const TCHAR* Waiting=!TargetLive?TEXT("等待服务器切换存活队友"):State->Phase==EDemoPhase::Combat?TEXT("队友清关后复活"):TEXT("等待复活状态同步"); // 原服务器裁决；不显示自算倒计时或切人热键。
        Label(Waiting,CenterX,Y+82.f,14.f,R02HUDTheme->Muted,true);
        Label(TEXT("Esc 菜单"),ViewWidth-142.f,ViewHeight-32.f,12.f,R02HUDTheme->Muted); // 只保留现有Esc菜单入口，不增加输入绑定。
        return;
    }
    const FLinearColor Ink(FColor(233,227,215)),Muted(FColor(110,115,119)),Ember(FColor(180,71,47)); // R02前景Token；真实等待/姓名用Ink，Ember仅小面积阵亡标记。

    Label(TEXT("BREACH / SPECTATOR"),48,40,13,Muted);
    R02HUDLine(32,44,4.f/UIScale,1.f,Ember); // 仅1px高的小阵亡刻度，不铺侧轨或底板。
    Label(State->bEndless?FString::Printf(TEXT("无尽 / 第 %d 关"),State->LevelNumber):FString::Printf(TEXT("第 %02d / 10 关"),State->LevelNumber),48,62,27,Ink);
    if(State->Phase!=EDemoPhase::Combat) Label(TEXT("正在同步下一阶段与复活状态"),48,112,15,Ink); // 不伪造倒计时，清关复活时由公开阵亡标记关闭本页。

    int32 Alive=0,Members=0; // 只统计当前World项目Pawn，不以Ready/私有成长推断生死。
    for(const APlayerState* Entry:State->PlayerArray)
        if(const auto* Member=Cast<ADemoPlayerState>(Entry))
            if(const auto* Pawn=Cast<ADemoCharacter>(Member->GetPawn())) if(IsValid(Pawn)) { ++Members; if(!Pawn->IsCoopEliminated()) ++Alive; }
    const float Right=ViewWidth-264.f; // 宽高比改变仍沿本地视口安全边缘，不共享另一玩家Canvas几何。
    Label(TEXT("SQUAD STATUS"),Right+16,40,12,Muted);
    Label(FString::Printf(TEXT("%d / %d"),Alive,Members),Right+16,63,30,Ink);
    Label(Alive>0?TEXT("队友存活 / 清关后复活"):TEXT("等待服务器结算"),Right+16,108,14,Ink);
    R02HUDLine(Right+16,136,216,1.f,Ink.CopyWithNewOpacity(.23f));

    const float X=ViewWidth*.5f-320.f,Y=ViewHeight-144.f; // 640px阅读区与左右名单分离；4:3仅增加上下空间，超宽不拉长此面板。
    Label(Alive>0?TEXT("你已阵亡 · 队友仍在行动，清关后自动复活"):TEXT("你已阵亡 · 正在等待服务器确认战局"),ViewWidth*.5f,Y-34,17,Ink,true);
    R02HUDLine(X+18,Y,604,1.f,Ink.CopyWithNewOpacity(.23f)); // 靠留白和发丝线划分公开目标，不铺大矩形衬底。
    Label(TEXT("WATCHING / 观战"),X+18,Y+11,12,Muted);
    const FString Name=TargetLive?(ObservedState?ObservedState->GetPlayerName():FString(TEXT("队友"))):TEXT("等待观战目标"); // 只有有效公开身份才显示姓名。
    Label(Name.Left(12),X+18,Y+36,24,Ink);
    const auto* Build=ObservedState?ObservedState->GetRunBuildComponent():nullptr; // 只读该组件明确公开的CoreId，绝不读取目标Snapshot/Runtime。
    const FString Core=Build&&!Build->PublicCoreId.IsNone()?DemoRunBuild::Title(Build->PublicCoreId).ToString():TEXT("未选择 / 未同步");
    Label(TargetLive?TEXT("公开核心 / ")+Core:TEXT("等待服务器切换存活队友"),X+18,Y+79,14,Ink);

    const auto* Attributes=TargetLive?Observed->GetDemoAttributes():nullptr; // Health/MaxHealth是原属性集的公开复制值，不能借此显示目标经济或天赋。
    Label(TEXT("观战目标生命"),X+385,Y+11,12,Muted);
    Label(Attributes?DemoHUDReadout::Number(Attributes->GetHealth())+TEXT(" / ")+DemoHUDReadout::Number(Attributes->GetMaxHealth()):TEXT("同步中"),X+385,Y+39,22,Ink);
    R02HUDLine(X+385,Y+77,235,Attributes?DemoHUDReadout::Fraction(Attributes->GetHealth(),Attributes->GetMaxHealth()):0.f,Ink);
    Label(TEXT("Esc · 个人菜单"),X+502.5f,Y+88.f,12,Ink,true); // 原Esc保留；不虚构A/D切人、倒计时或复活热区。

    if(State->Phase==EDemoPhase::Combat)
        for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It)
        {
            if(!It->IsBoss()||!It->IsAlive()) continue;
            const auto* Roster=It->FindComponentByClass<UDemoEnemyRoster>(); // 原公开Boss身份，目标未同步时使用通用名称。
            Label(Roster&&!Roster->GetLabel().IsEmpty()?Roster->GetLabel():TEXT("BOSS / 首领"),ViewWidth*.5f,37,20,Ink,true);
            R02HUDLine(ViewWidth*.5f-230,72,460,DemoHUDReadout::Fraction(It->GetHealth(),It->GetMaxHealth()),Ink,2.f); // S1候选2物理像素，与字号/视口缩放分开；仍待实图可读性验收。
            Label(DemoHUDReadout::Number(It->GetHealth())+TEXT(" / ")+DemoHUDReadout::Number(It->GetMaxHealth()),ViewWidth*.5f,86,13,Ink,true);
            break;
        }
}
