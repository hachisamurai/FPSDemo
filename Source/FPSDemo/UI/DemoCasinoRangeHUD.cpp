// 职责：真实赏金射击训练的个人HUD。使用：常规HUD每帧调用，只读本人训练Actor复制值，无命中或派奖权限。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "Economy/Gambling/DemoCasinoRange.h"
#include "Characters/DemoCharacter.h"
#include "Debug/DemoLog.h"
void ADemoHUD::DrawCasinoRangeOverlay()
{
    DEMO_LOG_TICK();
    const auto* Player=PlayerOwner?Cast<ADemoCharacter>(PlayerOwner->GetPawn()):nullptr; // 当前本地Avatar，旅行不缓存。
    const auto* Range=ADemoCasinoRange::Find(Player); if(!Range) return;
    const int64 Now=(FDateTime::UtcNow()-FDateTime(1970,1,1)).GetTicks()/ETimespan::TicksPerMillisecond; // 仅HUD倒计时，结算仍以服务器UTC为准。
    const int32 Seconds=int32(FMath::Clamp<int64>((Range->DeadlineMs-Now+999)/1000,0,120)); // 有界显示，远端时钟偏差不改变成绩。
    Panel(ViewWidth/2-310,85,620,82,GetPanelSurface(),8);
    Label(FString::Printf(TEXT("赏金靶场  %d秒  ·  得分%d  ·  靶%d/10  ·  剩余%d发"),Seconds,Range->Score,Range->Hits,Range->ShotsLeft),ViewWidth/2,99,20,DemoUI::Gold,true);
    Label(Range->Feedback.IsEmpty()?TEXT("鼠标瞄准 · 左键单发 · 上方小靶为头部 · 不消耗武器弹药"):Range->Feedback,ViewWidth/2,135,16,DemoUI::Ink,true);
    Panel(ViewWidth/2-8,ViewHeight/2-1,16,2,DemoUI::Gold,0); Panel(ViewWidth/2-1,ViewHeight/2-8,2,16,DemoUI::Gold,0);
}
