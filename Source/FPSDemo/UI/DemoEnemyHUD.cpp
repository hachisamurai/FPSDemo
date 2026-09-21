#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "UI/DemoHUD.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/DemoTags.h"
#include "Debug/DemoLog.h"
#include "AbilitySystemComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CanvasItem.h"

bool ADemoHUD::GetEnemyStatusScreenAnchor(const ADemoEnemy* Enemy, FVector2D& OutPixelAnchor) const
{
	DEMO_LOG_TICK();
	OutPixelAnchor = FVector2D::ZeroVector;
	if (!IsValid(Enemy) || !Enemy->IsAlive() || Enemy->IsHidden() || !PlayerOwner || Enemy->GetWorld() != GetWorld()) return false;
	// Canvas仅在DrawHUD期间有效；可见性接口使用真实视口尺寸，允许调试/测试在Actor Tick安全查询。
	int32 PixelWidth = 0;
	int32 PixelHeight = 0;
	PlayerOwner->GetViewportSize(PixelWidth, PixelHeight);
	if (PixelWidth <= 0 || PixelHeight <= 0) return false;
	// 只借用当前视角，不依赖Pawn朝向；世界竖直偏移保留在头顶，输出UI本身没有世界旋转。
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerOwner->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Anchor = Enemy->GetActorLocation() + FVector(0, 0, Enemy->IsBoss() ? 160.f : 80.f); // 适配现有48/115cm敌人碰撞球。
	const FVector ToAnchor = Anchor - ViewLocation; // 摄像机到头顶，厘米；背后点不可投影成前方UI。
	if (ToAnchor.SizeSquared() > FMath::Square(EnemyStatusMaxDistance) || FVector::DotProduct(ViewRotation.Vector(), ToAnchor) <= 0.f) return false;
	if (!PlayerOwner->ProjectWorldLocationToScreen(Anchor, OutPixelAnchor)) return false;
	// 先裁剪再做视线射线；预留120px血条和右侧两枚32px图标，防止边缘出现截断UI。
	const float Scale = FMath::Max(.01f, FMath::Min(PixelWidth / 1280.f, PixelHeight / 720.f)); // 物理像素/设计像素，与DrawHUD一致。
	if (OutPixelAnchor.X < 64.f * Scale || OutPixelAnchor.X > PixelWidth - 140.f * Scale
		|| OutPixelAnchor.Y < 24.f * Scale || OutPixelAnchor.Y > PixelHeight - 24.f * Scale) return false;
	// 狙击镜外黑色区域不显示敌人信息；整个小面板都要落在圆内，保守半径防止边缘泄漏。
	const ADemoCharacter* Player = Cast<ADemoCharacter>(PlayerOwner->GetPawn()); // 本帧借用，HUD不缓存Pawn。
	if (Player && Player->GetWeaponComponent()->GetScopeLevel() > 0
		&& FVector2D::Distance(OutPixelAnchor, FVector2D(PixelWidth, PixelHeight) * .5f) + 140.f * Scale > FMath::Min(PixelWidth, PixelHeight) * .44f) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(EnemyStatusVisibility), false, PlayerOwner->GetPawn()); // 忽略本地Pawn，遮挡采用场景Visibility通道。
	Query.AddIgnoredActor(Enemy); // 目标本身不遮住自己的头顶UI；其他敌人仍可遮挡。
	FHitResult Hit; // 仅用于当前摄像机到敌人中心的可见性测试，不用于伤害。
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, Enemy->GetActorLocation(), ECC_Visibility, Query)) return false;
	return true;
}

void ADemoHUD::DrawEnemyDebuffIcon(const UTexture2D* Icon, float X, float Y, const TCHAR* Fallback)
{
	DEMO_LOG_TICK();
	// 图标直接按贴图Alpha叠加到场景，不绘制黑色底板；透明区域完整露出世界画面。
	if (!Icon || !Icon->GetResource() || Icon->GetSizeX() <= 0 || Icon->GetSizeY() <= 0)
	{
		Label(Fallback, X + 16.f, Y + 8.f, 14.f, FLinearColor::White, true);
		return;
	}
	const float Fit = 32.f / FMath::Max(Icon->GetSizeX(), Icon->GetSizeY()); // 等比contain，不裁透明边，也不染青色。
	const FVector2D Size(Icon->GetSizeX() * Fit, Icon->GetSizeY() * Fit); // 设计像素；一次Canvas提交期间有效。
	const FVector2D Position = FVector2D(X, Y) + (FVector2D(32.f, 32.f) - Size) * .5f; // 方框内居中。
	FCanvasTileItem Tile(Position * UIScale, Icon->GetResource(), Size * UIScale, FLinearColor::White); // HUD强引用贴图，Canvas仅借用渲染资源。
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void ADemoHUD::DrawEnemyStatusBars()
{
	DEMO_LOG_TICK();
	for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 仅遍历本World，不持有跨关卡或死亡对象。
	{
		const ADemoEnemy* Enemy = *It; // 当前帧借用；死亡立即不画，不等延迟Destroy。
		FVector2D Anchor; // 投影输出物理像素，布局时只转换一次为设计坐标。
		if (!GetEnemyStatusScreenAnchor(Enemy, Anchor)) continue;
		const float X = Anchor.X / UIScale - 60.f; // 血条中心固定在头顶投影点，图标增减不使血条横移。
		const float Y = Anchor.Y / UIScale;
		const float Health = Enemy->GetHealth(); // GAS当前生命，伤害/治疗不另维护插值副本。
		const float Maximum = Enemy->GetMaxHealth(); // 最大生命可被GE改变，非正值按空条处理。
		const FLinearColor Fill = Enemy->IsBoss() ? FLinearColor(FColor(248, 178, 79)) : FLinearColor(FColor(245, 91, 84)); // Boss橙/普通红，独立于状态颜色。
		// 移除血量文字与进度条外围的黑底；当前生命只绘制彩色填充，已损失部分保持透明。
		Label(FString::Printf(TEXT("Lv%d  %.0f / %.0f"), Enemy->GetMonsterLevel(), Health, Maximum), X + 60.f, Y - 18.f, 12.f, FLinearColor::White, true);
		Panel(X, Y, 120.f * FMath::Clamp(Maximum > 0.f ? Health / Maximum : 0.f, 0.f, 1.f), 10.f, Fill, 0.f); // 不调用带深色槽底的通用Meter，其他HUD进度条保持原样。
		const UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent(); // 敌人ASC为唯一状态来源；装备元素弹不等于敌人有Debuff。
		float IconX = X + 128.f; // 从血条右侧依次紧凑排列，最多火/冰两枚；只在当前帧递增。
		if (ASC && ASC->HasMatchingGameplayTag(DemoTags::Burn))
		{
			DrawEnemyDebuffIcon(EnemyFireIcon, IconX, Y - 16.f, TEXT("火"));
            Label(FString::FromInt(Enemy->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Burn)),IconX+16,Y+18,12,FLinearColor::White,true); // 栈数来自ActiveGE，不用Tag计数。
			IconX += 36.f;
		}
		// 冰霜减速或冻结共用冰图标；仅有冰霜免疫时不显示，不将Tag计数显示成叠层。
		if (ASC && (ASC->HasMatchingGameplayTag(DemoTags::Chill) || ASC->HasMatchingGameplayTag(DemoTags::Frozen)))
			{
            DrawEnemyDebuffIcon(EnemyIceIcon, IconX, Y - 16.f, TEXT("冰"));
            Label(ASC->HasMatchingGameplayTag(DemoTags::Frozen)?TEXT("冻结"):FString::FromInt(Enemy->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Chill)),IconX+16,Y+18,12,FLinearColor::White,true); // 冻结与冰霜层分开表达。
        }
	}
}
