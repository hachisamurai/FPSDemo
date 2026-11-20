// 职责：任务与队伍快捷标记共用的屏幕/离屏方向提示。使用：HUD只消费已授权位置，当前本地视角投影；不发射线索敌、不复制相机或增加网络请求。
#include "UI/DemoHUD.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"

void ADemoHUD::DrawWorldDirectionIndicator(const FVector& Location, const FString& Text, FLinearColor Color, int32 Lane, bool bCaptionInScreen)
{
    DEMO_LOG_TICK();
    if (!Canvas || !PlayerOwner || Location.ContainsNaN() || UIScale <= 0.f) return;
    FVector ViewLocation; FRotator ViewRotation; // 使用拥有者实际相机；背后点不能直接投影成前方标记。
    PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
    const FVector Local = ViewRotation.UnrotateVector(Location - ViewLocation); // 相机空间X向前/Y向右/Z向上，cm。
    const ADemoCharacter* Player = Cast<ADemoCharacter>(PlayerOwner->GetPawn()); // 当前帧借用，观战相机仍可显示队伍标记。
    const bool bScoped = Player && Player->GetWeaponComponent() && Player->GetWeaponComponent()->GetScopeLevel() > 0;
    const FVector2D Center(ViewWidth * .5f, ViewHeight * .5f); // HUD设计像素，物理像素只在提交DrawLine时乘UIScale。
    const float ScopeRadius = FMath::Max(30.f, FMath::Min(Canvas->ClipX, Canvas->ClipY) * .44f / UIScale - 100.f); // 镜内预留短名/距离与沿边错位的宽度，不把文字越出AWP圆形视野。
    const FVector2D HalfBounds(FMath::Max(30.f, ViewWidth * .5f - 112.f), FMath::Max(30.f, ViewHeight * .5f - 190.f)); // 避开上方任务与底部生命/弹药。
    FVector2D Pixel; // 投影失败/背后时改用相机空间方向，不使用引擎负深度的镜像结果。
    const bool bProjected = Local.X > 1.f && PlayerOwner->ProjectWorldLocationToScreen(Location, Pixel, true);
    const FVector2D Projected = bProjected ? Pixel / UIScale : Center; // 属于本地子视口，不与其他玩家抢文字朝向。
    const FVector2D Delta = Projected - Center; // 屏幕向外方向，已应用实际FOV与视口宽高比。
    const bool bInside = bProjected && (bScoped ? Delta.Size() < ScopeRadius : FMath::Abs(Delta.X) < HalfBounds.X && FMath::Abs(Delta.Y) < HalfBounds.Y);
    const FString ShortText = Text.Left(bScoped ? 8 : 38); // 镜内使用短名以保留圆内边距。
    const float DistanceM = Local.Size() / 100.f; // 相机到目标直线距离，米。
    if (bInside)
    {
        const float Diamond = 4.5f; // 屏幕内目标精确锚点小菱形半径（设计px），与下方/上方错位文字分离。
        DrawLine((Projected.X) * UIScale, (Projected.Y - Diamond) * UIScale, (Projected.X + Diamond) * UIScale, (Projected.Y) * UIScale, Color, 1.5f * UIScale);
        DrawLine((Projected.X + Diamond) * UIScale, (Projected.Y) * UIScale, (Projected.X) * UIScale, (Projected.Y + Diamond) * UIScale, Color, 1.5f * UIScale);
        DrawLine((Projected.X) * UIScale, (Projected.Y + Diamond) * UIScale, (Projected.X - Diamond) * UIScale, (Projected.Y) * UIScale, Color, 1.5f * UIScale);
        DrawLine((Projected.X - Diamond) * UIScale, (Projected.Y) * UIScale, (Projected.X) * UIScale, (Projected.Y - Diamond) * UIScale, Color, 1.5f * UIScale);
        if (!bCaptionInScreen) return; // 屏内敌人/设施本身可见且有血条，只留菱形锚点，不在准星附近铺字（2026-11-19文字减负）。
        const float InScreenY = FMath::Clamp(Projected.Y - 22.f + FMath::Clamp(Lane, 0, 4) * 18.f, 72.f, ViewHeight - 148.f); // 按Lane纵向错开相邻终端/设施标签，防重叠。
        const FString InScreenCaption = DistanceM < 20.f ? ShortText : FString::Printf(TEXT("%s · %.0fm"), *ShortText, DistanceM); // 20m内肉眼可判断远近，省去米数。
        Label(InScreenCaption, FMath::Clamp(Projected.X, 165.f, ViewWidth - 165.f), InScreenY, 14.f, Color, true);
        return;
    }
    const FString Caption = FString::Printf(TEXT("%s%s · %.0fm"), Local.X < 0.f ? TEXT("后方 · ") : TEXT(""), *ShortText, DistanceM); // 离屏保留米数；正后方文字避免把下箭头误认成低处目标。
    FVector2D Direction = bProjected ? Delta : FVector2D(Local.Y, -Local.Z); // 背后按真正左右/上下引导转身，禁止把后方当屏幕中间。
    if (!Direction.Normalize()) Direction = FVector2D(0.f, 1.f); // 正后方和同位置的退化情况稳定指向下边，避免NaN。
    const float RayLength = bScoped ? ScopeRadius : FMath::Min(FMath::Abs(Direction.X) > .001f ? HalfBounds.X / FMath::Abs(Direction.X) : MAX_flt,
        FMath::Abs(Direction.Y) > .001f ? HalfBounds.Y / FMath::Abs(Direction.Y) : MAX_flt); // 矩形/圆边界解析交点，无每帧碰撞查询。
    const FVector2D Tangent(-Direction.Y, Direction.X); // 沿边小幅错开最多四人，箭头仍指向相同真实方向。
    const float LaneOffset = (FMath::Clamp(Lane, 0, 4) - 2) * 14.f; // 最大28设计像素，防标记密集完全重叠。
    const FVector2D Tip = Center + Direction * RayLength + Tangent * LaneOffset;
    const FVector2D Back = Tip - Direction * 13.f; // 空心三角，不依赖尚未制作的正式图标。
    const FVector2D Left = Back + Tangent * 6.f, Right = Back - Tangent * 6.f; // 与方向共用几何，任意朝向可读。
    DrawLine(Tip.X * UIScale, Tip.Y * UIScale, Left.X * UIScale, Left.Y * UIScale, Color, 2.f * UIScale);
    DrawLine(Tip.X * UIScale, Tip.Y * UIScale, Right.X * UIScale, Right.Y * UIScale, Color, 2.f * UIScale);
    DrawLine(Left.X * UIScale, Left.Y * UIScale, Right.X * UIScale, Right.Y * UIScale, Color, 2.f * UIScale);
    const FVector2D CaptionPoint = Tip - Direction * 28.f + Tangent * LaneOffset; // 文字在箭头内侧，镜内模式不跨入黑边。
    Label(Caption, FMath::Clamp(CaptionPoint.X, 165.f, ViewWidth - 165.f), FMath::Clamp(CaptionPoint.Y, 185.f, ViewHeight - 160.f), 13.f, Color, true);
}
