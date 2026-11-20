// 职责：各本地玩家独立投影设施状态文字/血条。使用：Combat且无阻塞菜单时HUD调用；依赖目标复制纯值、当前视角与遮挡，不写Actor/GAS/相机。
#include "UI/DemoHUD.h"
#include "UI/R02/DemoUITheme.h" // 本帧视觉Token，公开目标事实/投影裁剪仍原路径。
#include "Game/Objectives/DemoObjectiveDevice.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

void ADemoHUD::DrawObjectiveStatusBars()
{
    DEMO_LOG_TICK();
    if (!Canvas || !PlayerOwner || UIScale <= 0.f) return;
    const bool bR02=IsR02GameplayPresentation(); // 菜单/训练等旧页型保原绘制，不据颜色授权目标交互。
    FVector ViewLocation; FRotator ViewRotation; // 当前拥有者相机，包含观战视角；不使用服务器或玩家0的朝向。
    PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
    const ADemoCharacter* Player = Cast<ADemoCharacter>(PlayerOwner->GetPawn()); // 本帧借用，切Pawn不留缓存。
    const bool bScoped = Player && Player->GetWeaponComponent() && Player->GetWeaponComponent()->GetScopeLevel() > 0; // 镜外黑边不显示设施标签。
    for (TActorIterator<ADemoObjectiveDevice> It(GetWorld()); It; ++It) // 当前任务通常只有两个；Actor卸载后迭代器自然不再提供。
    {
        const ADemoObjectiveDevice* Device = *It; // HUD只读，不持有跨关目标强引用。
        if (!IsValid(Device) || Device->IsHidden()) continue;
        const FDemoObjectiveDeviceView View = Device->GetStatusView(); // 复制HP和网格顶端挂点，不从客户端ASC计算第二份生命。
        const FVector ToAnchor = View.Anchor - ViewLocation; // 世界厘米，用于距离和正面视锥快速排除。
        if (ToAnchor.ContainsNaN() || ToAnchor.SizeSquared() > FMath::Square(ObjectiveStatusMaxDistance)
            || FVector::DotProduct(ViewRotation.Vector(), ToAnchor) <= 0.f) continue;
        FVector2D Pixel; // 使用所属本地玩家子视口坐标；Canvas只画本视口，不将某个世界文字转向一个玩家。
        if (!PlayerOwner->ProjectWorldLocationToScreen(View.Anchor, Pixel, true)) continue;
        const FVector2D Anchor = Pixel / UIScale; // 转设计像素；标签宽156、高50，整个面板需在可见屏幕内。
        if (Anchor.X < 82.f || Anchor.X > ViewWidth - 82.f || Anchor.Y < 54.f || Anchor.Y > ViewHeight - 12.f) continue;
        if (bScoped && FVector2D::Distance(Pixel, FVector2D(Canvas->ClipX, Canvas->ClipY) * .5f) + 100.f * UIScale
            > FMath::Min(Canvas->ClipX, Canvas->ClipY) * .44f) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ObjectiveStatusVisibility), false, PlayerOwner->GetPawn()); // 只查显示遮挡，不参与伤害。
        Query.AddIgnoredActor(Device); // 设施自身不能遮住自己的标签；墙体和其他实体仍遮挡。
        FHitResult Obstacle; // 当前帧摄像机到设施中心的视线，完成后立即丢弃。
        if (GetWorld()->LineTraceSingleByChannel(Obstacle, ViewLocation, Device->GetActorLocation(), ECC_Visibility, Query)) continue;
        const FLinearColor Color = bR02?(View.bDestroyed?R02HUDTheme->Ink:R02HUDTheme->Ember):(View.bDestroyed?FLinearColor(.28f,.94f,.62f):FLinearColor(1.f,.7f,.2f)); // 完成/进行同时有真实文字，不只靠色相。
        const FString Title = View.bDestroyed ? TEXT("已摧毁") : TEXT("摧毁目标"); // 中文使用HUD已有运行时字体，不依赖TextRender内置字体。
        Label(Title, Anchor.X + 1.f, Anchor.Y - 47.f, 16.f, FLinearColor::Black, true);
        Label(Title, Anchor.X, Anchor.Y - 48.f, 16.f, Color, true);
        if (!View.bDestroyed)
        {
            const FString Health = FString::Printf(TEXT("%.0f / %.0f"), View.Health, View.MaxHealth); // 权威复制HP，只格式化显示。
            Label(Health, Anchor.X + 1.f, Anchor.Y - 26.f, 13.f, FLinearColor::Black, true);
            Label(Health, Anchor.X, Anchor.Y - 27.f, 13.f, FLinearColor::White, true);
            if(bR02) R02HUDLine(Anchor.X-78.f,Anchor.Y-8.f,156.f,View.MaxHealth>0.f?View.Health/View.MaxHealth:0.f,Color);
            else Meter(Anchor.X-78.f,Anchor.Y-8.f,156.f,5.f,View.MaxHealth>0.f?View.Health/View.MaxHealth:0.f,Color);
        }
    }
}
