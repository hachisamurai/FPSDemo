// 职责：R02四角命中点、真实碰撞闪记与聚合飘字；旧变体仍原斜线。只读本地HitFeedback事实，不反查或伪造伤害。
#include "UI/DemoHUD.h"
#include "UI/R02/DemoUITheme.h" // CDO强引用主题，仅改变本机显示；不用每帧资源路径查询。
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Combat/Feedback/DemoHitFeedbackComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Weapons/DemoWeaponComponent.h"

void ADemoHUD::DrawHitFeedback(const ADemoCharacter* Player)
{
    DEMO_LOG_TICK();
    const UDemoHitFeedbackComponent* Feedback = Player ? Player->FindComponentByClass<UDemoHitFeedbackComponent>() : nullptr; // 本帧借用Pawn组件，无跨重生指针。
    if (!Feedback || !Canvas || !PlayerOwner) return;
    const bool bR02=IsR02GameplayPresentation(); // 原菜单/ADS/阶段守卫仍由DrawHUD调用方负责，不用视觉样式授予命中。
    const float CenterX = ViewWidth * .5f, CenterY = ViewHeight * .5f; // 设计像素中心；不移动相机或基础瞄准点。
    const int32 Kind = Feedback->GetMarkerKind(); // 普通0、真实头部弱点1、直击击杀2；协议 bCore 保留，仅由当前 head 命中设置。
    const FLinearColor Colors[] = {bR02?R02HUDTheme->Ember:FLinearColor(.95f,.96f,1.f),bR02?R02HUDTheme->Ember:FLinearColor(1.f,.7f,.15f),bR02?R02HUDTheme->Ember:FLinearColor(1.f,.16f,.12f)}; // R02四角确认为Ember，以点大小/原寿命区分分级；不改真实命中事实。
    const float Age = Feedback->GetMarkerAge(); // 暂停跟随World，丢包不会补造标记。
    const float Duration = FMath::Clamp(Kind == 2 ? Feedback->KillMarkerSeconds : Feedback->HitMarkerSeconds, .05f, 1.f); // 与接收端优先级窗口一致。
    if (Age >= 0.f && Age < Duration)
    {
        const float Alpha = 1.f - FMath::Clamp((Age / Duration - .45f) / .55f, 0.f, 1.f); // 前45%稳亮，后半段淡出。
        const float Punch = 1.f - FMath::Clamp(Age / .09f, 0.f, 1.f); // 90ms由大向小收束，只改变UI。
        const float Inner = 7.f + Punch * 3.f, Outer = 16.f + Kind * 3.f + Punch * 6.f; // 空心中心保持瞄准可读，击杀臂更长。
        const FVector2D Directions[] = {FVector2D(-1,-1), FVector2D(1,-1), FVector2D(-1,1), FVector2D(1,1)}; // 四象限对角线。
        for (const FVector2D& Direction : Directions) // Canvas本帧消费，不创建Widget或持有方向引用。
        {
            if(bR02)
            {
                const FVector2D Point=(FVector2D(CenterX,CenterY)+Direction*(8.f+Kind*2.f+Punch*2.f))*UIScale; // 四角与实际中心同源，留空中心，不画X臂。
                const float PixelX=FMath::RoundToFloat(Point.X),PixelY=FMath::RoundToFloat(Point.Y),Side=Kind==2?3.f:2.f; // 物理像素点大小；击杀更大，非DPI拉伸线段。
                DrawRect(R02HUDTheme->Base.CopyWithNewOpacity(Alpha*.8f),PixelX-1.f,PixelY-1.f,Side+2.f,Side+2.f);
                DrawRect(Colors[Kind].CopyWithNewOpacity(Alpha),PixelX,PixelY,Side,Side);
            }
            else
            {
                const FVector2D Start = (FVector2D(CenterX, CenterY) + Direction * Inner) * UIScale; // 实际视口像素。
                const FVector2D End = (FVector2D(CenterX, CenterY) + Direction * Outer) * UIScale; // 终点不覆盖中心。
                DrawLine(Start.X, Start.Y, End.X, End.Y, FLinearColor(0,0,0,Alpha * .8f), (4.f + Kind) * UIScale);
                DrawLine(Start.X, Start.Y, End.X, End.Y, Colors[Kind].CopyWithNewOpacity(Alpha), (2.f + Kind * .5f) * UIScale);
            }
        }
        // 弱点已从胸前反应堆迁移到头部，提示文本同步采用实际区域，避免玩家仍瞄准胸口。
        if (Kind > 0) Label(Kind == 2 ? (Feedback->IsDestructionMarker() ? TEXT("摧毁") : TEXT("击杀")) : TEXT("头部命中"), CenterX, CenterY + 34.f, 15.f, Colors[Kind].CopyWithNewOpacity(Alpha), true);
    }
    if (!Feedback->bShowDamageNumbers) return;
    FVector ViewLocation; FRotator ViewRotation; // 当前本地相机世界视图，不使用Pawn身体朝向或服务器玩家0。
    PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
    const bool bScoped = Player->GetWeaponComponent() && Player->GetWeaponComponent()->GetScopeLevel() > 0; // 镜外黑边不显示飘字。
    const double Now = GetWorld()->GetTimeSeconds(); // 与缓存使用同一World秒。
    const TArray<FDemoHitFeedbackDisplay>& Hits = Feedback->GetHits(); // 本帧同步只读借用，客户端回调不会在绘制中途修改数组。
    for (int32 HitIndex = 0; HitIndex < Hits.Num(); ++HitIndex) // 固定最多12条，过期/离屏先剔除再查遮挡。
    {
        const FDemoHitFeedbackDisplay& Display = Hits[HitIndex]; // 当前待绘制的同枪同目标聚合值。
        const float NumberAge = static_cast<float>(Now - Display.StartedAt); // 同枪后续弹丸更新数值，不重置年龄。
        const float NumberDuration = FMath::Clamp(Feedback->DamageNumberSeconds, .1f, 2.f); // 防止非法旧BP值除零。
        bool bHasNewerHit = false; // 同目标只显示最近一枪；连射不会把六条数字叠在相同位置。
        for (int32 NewerIndex = HitIndex + 1; NewerIndex < Hits.Num(); ++NewerIndex) // 保留旧枪缓存供迟到弹丸合并，但不重复绘制。
            if (Hits[NewerIndex].Hit.TargetId == Display.Hit.TargetId && Now - Hits[NewerIndex].StartedAt < NumberDuration) { bHasNewerHit = true; break; }
        if (bHasNewerHit) continue;
        const FVector Position = Display.Hit.ImpactPoint; // 命中当时的世界位置，死亡目标无需继续存在。
        FVector2D Pixel; // ProjectWorldLocationToScreen输出物理视口像素。
        if (NumberAge < 0.f || NumberAge >= NumberDuration || FVector::DotProduct(ViewRotation.Vector(), Position - ViewLocation) <= 0.f
            || !PlayerOwner->ProjectWorldLocationToScreen(Position, Pixel)) continue;
        const FVector2D Anchor = Pixel / UIScale; // 转1280x720设计空间，保持字体和准星同一缩放。
        if (Anchor.X < 28.f || Anchor.X > ViewWidth - 65.f || Anchor.Y < 35.f || Anchor.Y > ViewHeight - 28.f) continue;
        if (bScoped && FVector2D::Distance(Pixel, FVector2D(Canvas->ClipX, Canvas->ClipY) * .5f) > FMath::Min(Canvas->ClipX, Canvas->ClipY) * .44f - 60.f * UIScale) continue;
        FHitResult Obstacle; // 只为显示做Visibility检查，结果绝不进入伤害函数。
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoHitFeedbackSight), false, Player); // 忽略射手，不显示墙后残留数字。
        if (GetWorld()->LineTraceSingleByChannel(Obstacle, ViewLocation, Position, ECC_Visibility, Query)
            && !Cast<ADemoEnemy>(Obstacle.GetActor()) && Obstacle.Distance + 10.f < FVector::Distance(ViewLocation, Position)) continue;
        const int32 NumberKind = Display.Hit.bKilled ? 2 : Display.Hit.bCore ? 1 : 0; // 同枪任何头部命中均升级数字色，兼容 bCore 协议，伤害总值仍为实际HP之和。
        const float Fade = 1.f - FMath::Clamp((NumberAge / NumberDuration - .5f) * 2.f, 0.f, 1.f); // 前半段稳定阅读后淡出。
        const float X = Anchor.X + 23.f, Y = Anchor.Y - 24.f - NumberAge * 38.f; // 向右上浮动，避开实际命中点与准星中心。
        const FString Number = Display.Hit.Damage < 1.f ? TEXT("<1") : FString::Printf(TEXT("%.0f"), Display.Hit.Damage); // 实际HP损失取整；非零小伤害不能显示成0。
        const float Size = 18.f + NumberKind * 2.f + FMath::Max(0.f, 1.f - NumberAge / .1f) * 4.f; // 100ms轻微弹出，头部/击杀稍大。
        Label(Number, X + 1.f, Y + 1.f, Size, FLinearColor(0,0,0,Fade));
        Label(Number, X, Y, Size, Colors[NumberKind].CopyWithNewOpacity(Fade));
        if (NumberAge < .12f) // 碰撞位置的四向短闪记与飘字一起显示，贴实际碰撞而非瞄准射线。
        {
            const float Radius = (3.f + NumberAge * 35.f) * UIScale; // 短促扩散，只占局部几像素。
            const FLinearColor Spark = Colors[NumberKind].CopyWithNewOpacity(1.f - NumberAge / .12f); // 120ms结束，不形成永久命中指示。
            if(bR02)
            {
                const float PixelX=FMath::RoundToFloat(Pixel.X),PixelY=FMath::RoundToFloat(Pixel.Y); // 冻结实际接触点投影；像素闪记只作表现，不沿当前枪口补算碰撞。
                DrawRect(Spark,PixelX-2.f,PixelY-2.f,1.f,1.f); DrawRect(Spark,PixelX+2.f,PixelY-2.f,1.f,1.f);
                DrawRect(Spark,PixelX-2.f,PixelY+2.f,1.f,1.f); DrawRect(Spark,PixelX+2.f,PixelY+2.f,1.f,1.f);
            }
            else
            {
                DrawLine(Pixel.X - Radius, Pixel.Y, Pixel.X + Radius, Pixel.Y, Spark, 2.f * UIScale);
                DrawLine(Pixel.X, Pixel.Y - Radius, Pixel.X, Pixel.Y + Radius, Spark, 2.f * UIScale);
            }
        }
    }
}
