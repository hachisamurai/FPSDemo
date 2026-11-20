// 职责：R02 正式 Canvas HUD 的物理像素细线与单点准星；只消费原 HUD 已通过阶段/ADS/菜单门禁的绘制调用。
// 使用：Status 等本帧表现读取 CDO 强引用 R02HUDTheme；不加载资源、不提供玩法授权、不增加全屏背景或底板。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"

void ADemoHUD::R02HUDLine(float X,float Y,float W,float Fraction,FLinearColor Color,float PhysicalPixels)
{
    DEMO_LOG_TICK();
    if(!Canvas||!IsR02GameplayPresentation()||W<=0.f) return;
    const float PixelX=FMath::RoundToFloat(X*UIScale),PixelY=FMath::RoundToFloat(Y*UIScale); // 实际栅格对齐，不把设计像素当物理像素。
    const float PixelW=FMath::Max(1.f,FMath::RoundToFloat(W*UIScale)); // 只横向随视口缩放，保持原设计布局范围。
    const float PixelH=FMath::Clamp(FMath::RoundToFloat(PhysicalPixels),1.f,6.f); // 主题 Boss2px 与普通1px不在1080p变成3/1.5px。
    FLinearColor Ink=Color; // 已传主题色/透明度原样保留；仅映射明确旧语义色，不把Ember残影或自有淡出变成Ink。
    if(Color.Equals(DemoUI::Ink)||Color.Equals(DemoUI::Accent)) Ink=R02HUDTheme->Ink;
    else if(Color.Equals(DemoUI::Muted)) Ink=R02HUDTheme->Muted;
    else if(Color.Equals(DemoUI::Gold)||Color.Equals(DemoUI::Danger)) Ink=R02HUDTheme->Ember;
    Ink.A=Color.A;
    DrawRect(R02HUDTheme->Muted.CopyWithNewOpacity(R02HUDTheme->HairlineOpacity*Color.A),PixelX,PixelY,PixelW,PixelH);
    const float Filled=FMath::RoundToFloat(PixelW*FMath::Clamp(Fraction,0.f,1.f)); // 真实只读进度钳制用于绘制，不修改原数据。
    if(Filled>0.f) DrawRect(Ink,PixelX,PixelY,Filled,PixelH);
}

void ADemoHUD::DrawR02Reticle()
{
    DEMO_LOG_TICK();
    if(!Canvas||!IsR02GameplayPresentation()) return;
    const float CenterX=FMath::RoundToFloat(Canvas->ClipX*.5f),CenterY=FMath::RoundToFloat(Canvas->ClipY*.5f); // 当前物理视口中心，始终沿实际瞄准中心。
    DrawRect(R02HUDTheme->Base,CenterX-2.f,CenterY-2.f,5.f,5.f); // 1px 暗边只服务局部白点对比，不形成准星底板或十字。
    DrawRect(R02HUDTheme->Ink,CenterX-1.f,CenterY-1.f,3.f,3.f); // 720p/1080p 均为单个3×3物理px点形。
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr;
    const ADemoCharacter* Player=GetOwningPlayerController()?Cast<ADemoCharacter>(GetOwningPlayerController()->GetPawn()):nullptr;
    if(IsCoreEmpoweredNow(Player,State))
    {
        // 核心强化待兑现时在中心点四角绘制1物理px余烬角标，不遮挡瞄准中心。
        const FLinearColor Glow=R02HUDTheme->Ember.CopyWithNewOpacity(.92f);
        const float Offset=FMath::RoundToFloat(7.f*UIScale);
        const float Arm=FMath::Max(3.f,FMath::RoundToFloat(4.f*UIScale));
        DrawRect(Glow,CenterX-Offset-Arm,CenterY-Offset,Arm,1.f); DrawRect(Glow,CenterX-Offset,CenterY-Offset,1.f,Arm);
        DrawRect(Glow,CenterX+Offset,CenterY-Offset,Arm,1.f); DrawRect(Glow,CenterX+Offset,CenterY-Offset,1.f,Arm);
        DrawRect(Glow,CenterX-Offset-Arm,CenterY+Offset,Arm,1.f); DrawRect(Glow,CenterX-Offset,CenterY+Offset-Arm+1.f,1.f,Arm);
        DrawRect(Glow,CenterX+Offset,CenterY+Offset,Arm,1.f); DrawRect(Glow,CenterX+Offset,CenterY+Offset-Arm+1.f,1.f,Arm);
    }
}
