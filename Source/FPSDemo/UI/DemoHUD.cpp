#include "UI/DemoHUD.h"
// 对应头文件先于依赖，保证UE独立编译能检查本类声明自包含。
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoCloudSync.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "AI/DemoEnemy.h"
#include "Interaction/DemoInteractable.h"
#include "Game/FPSDemoGameMode.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Debug/DemoLog.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "UObject/ConstructorHelpers.h"
#include "CanvasItem.h"
#include "EngineUtils.h"
#include "RenderUtils.h"
#include "Styling/CoreStyle.h"
#include "Tests/DemoUIValidation.h"
#include "Misc/CommandLine.h"

namespace DemoUI
{
	// 固定深色战术界面：不透明菜单保证白模强光下对比度；战斗面板保留少量场景透视。
	// 用 sRGB 设计色转换到线性空间，防止 Canvas 输出时再次 gamma 转换导致面板泛灰。
	const FLinearColor Surface(FColor(24, 38, 45));
	const FLinearColor Raised(FColor(31, 49, 58));
	const FLinearColor Edge(FColor(48, 73, 84));
	const FLinearColor Ink(FColor(230, 240, 243));
	const FLinearColor Muted(FColor(154, 178, 188));
	const FLinearColor Accent(FColor(96, 219, 204));
	const FLinearColor Gold(FColor(248, 208, 118));
	const FLinearColor Danger(FColor(255, 108, 92));
	const FLinearColor Overlay(FColor(3, 6, 9, 220));
	const FLinearColor BattleSurface(FColor(12, 22, 29, 235)); // 轻透明，仅用于常驻 HUD。
}

ADemoHUD::ADemoHUD()
{
	DEMO_LOG_CALL();
	// Finder 在类构造阶段建立资产引用；源图拷贝和导入由 import_lobby_background.py 独立维护。
	static ConstructorHelpers::FObjectFinder<UTexture2D> Background(TEXT("/Game/UI/Textures/T_LobbyBackground.T_LobbyBackground"));
	LobbyBackground = Background.Object;
	if (!LobbyBackground) UE_LOG(LogFPSDemo, Warning, TEXT("Lobby background missing; using solid fallback"));
	// 状态图标全敌人共享两张已导入贴图；CDO硬引用确保Cook收集，无每帧加载或每只怪重复资产实例。
	static ConstructorHelpers::FObjectFinder<UTexture2D> FireIcon(TEXT("/Game/UI/Textures/ElementIcons/T_Status_Fire.T_Status_Fire"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> IceIcon(TEXT("/Game/UI/Textures/ElementIcons/T_Status_Ice.T_Status_Ice"));
	EnemyFireIcon = FireIcon.Object;
	EnemyIceIcon = IceIcon.Object;
	if (!EnemyFireIcon || !EnemyIceIcon) UE_LOG(LogFPSDemo, Warning, TEXT("Enemy status icon missing; text fallback enabled"));
}

void ADemoHUD::BeginPlay()
{
	DEMO_LOG_CALL();
	Super::BeginPlay();
	RuntimeFont = NewObject<UFont>(this, TEXT("BreachRuntimeFont"));
	RuntimeFont->FontCacheType = EFontCacheType::Runtime;
	RuntimeFont->CompositeFont = *FCoreStyle::GetDefaultFont();
#if !UE_BUILD_SHIPPING
	// 测试只在显式参数启用，World 拥有测试 Actor；每次重开生成新实例，普通游玩不创建。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoUIValidation"))) GetWorld()->SpawnActor<ADemoUIValidation>();
#endif
}

void ADemoHUD::Label(const FString& Text, float X, float Y, float Size, FLinearColor Color, bool bCentered)
{
	DEMO_LOG_TICK();
	// Runtime 复合字体包含 Roboto 和 DroidSansFallback；避免模板离线字体把中文画成方块。
	// Font/Item 只活到本次绘制；point 转设计像素后按视口比例栅格化，禁止拉伸离线字形。
	// UE5.4 的 CanvasTextItem::HasValidText 额外要求 UFont，即使 SlateFontInfo 已有复合字体。
	if (!RuntimeFont) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HUD font not initialized")); return; }
	const FSlateFontInfo Font(RuntimeFont, FMath::Max(1, FMath::RoundToInt(Size * UIScale * 0.75f)), TEXT("Regular"));
	FCanvasTextItem Item(FVector2D(X, Y) * UIScale, FText::FromString(Text), Font, Color);
	Item.bCentreX = bCentered;
	Canvas->DrawItem(Item);
}

void ADemoHUD::Panel(float X, float Y, float W, float H, FLinearColor Color, float Radius)
{
	DEMO_LOG_TICK();
	if (W <= 0.f || H <= 0.f) return;
	// 小型进度条直接绘制；面板使用单次三角扇绘制，避免半透明圆角重叠叠色。
	if (Radius <= 0.f) { DrawRect(Color, X * UIScale, Y * UIScale, W * UIScale, H * UIScale); return; }
	// R 为限制后的半径；Points 是顺时针边界，Triangles 仅为本次 Canvas 提交的临时顶点。
	const float R = FMath::Min(Radius, FMath::Min(W, H) / 2.f);
	TArray<FVector2D, TInlineAllocator<28>> Points;
	TArray<FCanvasUVTri> Triangles;
	// Corner 是四角序号，Segment 将每个 90 度弧分为六段，成本与视口分辨率无关。
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FVector2D Center(X + ((Corner == 0 || Corner == 3) ? R : W - R), Y + (Corner < 2 ? R : H - R)); // 当前角弧心。
		for (int32 Segment = 0; Segment <= 6; ++Segment)
		{
			const float Angle = PI + Corner * HALF_PI + Segment * HALF_PI / 6.f; // 从左上开始的顺时针弧度。
			Points.Add((Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R) * UIScale);
		}
	}
	// Index 逐条边组成三角扇；每个顶点同色，白色纹理只用于引擎标准混合路径。
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef(); // 数组持有，仅本循环借用。
		Triangle.V0_Pos = FVector2D(X + W / 2.f, Y + H / 2.f) * UIScale;
		Triangle.V1_Pos = Points[Index];
		Triangle.V2_Pos = Points[(Index + 1) % Points.Num()];
		Triangle.V0_Color = Triangle.V1_Color = Triangle.V2_Color = Color;
	}
	FCanvasTriangleItem Item(Triangles, GWhiteTexture); // Canvas 在当前帧消费，不保留成员引用。
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

bool ADemoHUD::IsHovered(float X, float Y, float W, float H) const
{
	DEMO_LOG_TICK();
	// MouseX/Y 为视口物理像素，和注册 HitBox 使用同一转换，避免 DPI 下视觉与热区错位。
	float MouseX = 0.f;
	float MouseY = 0.f;
	return PlayerOwner && PlayerOwner->bShowMouseCursor && PlayerOwner->GetMousePosition(MouseX, MouseY)
		&& MouseX >= X * UIScale && MouseX < (X + W) * UIScale && MouseY >= Y * UIScale && MouseY < (Y + H) * UIScale;
}

void ADemoHUD::Button(FName Name, const FString& Text, float X, float Y, float W, float H, bool bEnabled, bool bQuiet, float Opacity)
{
	DEMO_LOG_TICK();
	// Hover 只改变表现；禁用按钮不注册 HitBox，控制器仍会再次检查键盘或旧点击请求。
	const bool bHover = bEnabled && IsHovered(X, Y, W, H);
	// Fill 为本次按钮底色；透明度只作用于底色，文字保持完整对比度，局内按钮默认仍为不透明。
	const FLinearColor Fill = !bEnabled ? DemoUI::Raised : bQuiet ? (bHover ? DemoUI::Edge : DemoUI::Raised) : (bHover ? DemoUI::Ink : DemoUI::Accent);
	Panel(X, Y, W, H, Fill.CopyWithNewOpacity(FMath::Clamp(Opacity, 0.f, 1.f)));
	Label(Text, X + W / 2.f, Y + H / 2.f - 9.f, 16.f, !bEnabled ? DemoUI::Muted : bQuiet ? DemoUI::Ink : DemoUI::Surface, true);
	if (bEnabled) AddHitBox(FVector2D(X, Y) * UIScale, FVector2D(W, H) * UIScale, Name, true);
}

void ADemoHUD::Meter(float X, float Y, float W, float H, float Fraction, FLinearColor Color)
{
	DEMO_LOG_TICK();
	Panel(X, Y, W, H, DemoUI::Edge, 0.f);
	Panel(X, Y, W * FMath::Clamp(Fraction, 0.f, 1.f), H, Color, 0.f);
}

void ADemoHUD::DrawHUD()
{
	DEMO_LOG_TICK();
	Super::DrawHUD();
	if (!Canvas || !PlayerOwner) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HUD waiting for viewport/owner")); return; }
	UIScale = FMath::Max(0.01f, FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 720.f));
	ViewWidth = Canvas->ClipX / UIScale;
	ViewHeight = Canvas->ClipY / UIScale;
	// 本帧借用所有展示对象；不跨重生/OpenLevel 缓存 UObject 裸指针。
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(PlayerOwner);
	ADemoCharacter* Player = Cast<ADemoCharacter>(PlayerOwner->GetPawn());
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	// 顶层页独占热区，暂停不推进世界但Canvas继续绘制。
	if (PC && State && PC->HasBlockingOverlay()) { DrawSessionMenu(PC, State); return; }
	// 大厅不依赖 Avatar 属性就绪，不显示准星/武器/白模场景。
	if (PC && State && State->Phase == EDemoPhase::Lobby) { DrawLobby(PC, State); return; }
	const UDemoAttributeSet* Attributes = Player ? Player->GetDemoAttributes() : nullptr;
	if (!PC || !State || !Attributes)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HUD waiting for gameplay data"));
		Label(TEXT("正在初始化作战系统…"), 32, 32, 20, DemoUI::Ink);
		return;
	}
	// 模态确认与升级共用遮罩；先压暗世界再绘制状态和面板，按钮不会与场景导航重叠。
	if (PC->IsUpgradeMenuOpen() || PC->IsNextLevelConfirmationOpen() || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat)
		Panel(0, 0, ViewWidth, ViewHeight, DemoUI::Overlay, 0.f);
	// Scope来自装备组件的真实状态，先盖场景再绘制可读的边缘HUD。
	if (Player->GetWeaponComponent()->GetScopeLevel() > 0) DrawScopeOverlay();
	// Combat才有存活敌人；投影UI在瞄准镜内绘制且先于边缘HUD，模态页不泄露背后的敌人血条。
	if (State->Phase == EDemoPhase::Combat && !PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen()) DrawEnemyStatusBars();
	DrawStatus(State, Player, Attributes);
	if (PC->IsUpgradeMenuOpen()) DrawUpgradeMenu(PC, State, Player, Attributes);
	if (PC->IsNextLevelConfirmationOpen()) DrawNextLevelConfirmation(State);
	if (State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat) DrawEndScreen(State);
	// 保存状态位于所有局内面板之外，自动保存失败也能立即看见；HUD只读GI缓存。
	const UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>();
	if (Saves && Saves->GetActiveSlot() != INDEX_NONE) Label(Saves->GetStatus(), 28, ViewHeight - 21, 12, DemoUI::Gold);
}

void ADemoHUD::DrawScopeOverlay()
{
	DEMO_LOG_TICK();
	// Center/Radius以实际像素计算，不用16:9设计坐标，4:3或超宽视口都保持正圆。
	const FVector2D Center(Canvas->ClipX * .5f, Canvas->ClipY * .5f);
	const float Radius = FMath::Min(Canvas->ClipX, Canvas->ClipY) * .44f;
	const float Outside = FVector2D(Canvas->ClipX, Canvas->ClipY).Size(); // 外圆覆盖整个视口。
	TArray<FCanvasUVTri> Triangles; // 本帧临时顶点，Canvas提交后不保留引用。
	Triangles.Reserve(512);
	for (int32 Segment = 0; Segment < 256; ++Segment) // 256段避免1080p下明显多边形边缘。
	{
		// 两端的单位方向及内外顶点仅为当前环段使用。
		const float A = Segment * 2.f * PI / 256.f;
		const float B = (Segment + 1) * 2.f * PI / 256.f;
		const FVector2D U(FMath::Cos(A), FMath::Sin(A)), V(FMath::Cos(B), FMath::Sin(B));
		FCanvasUVTri& First = Triangles.AddDefaulted_GetRef();
		First.V0_Pos = Center + U * Radius;
		First.V1_Pos = Center + U * Outside;
		First.V2_Pos = Center + V * Outside;
		First.V0_Color = First.V1_Color = First.V2_Color = FLinearColor::Black;
		FCanvasUVTri& Second = Triangles.AddDefaulted_GetRef();
		Second.V0_Pos = Center + U * Radius;
		Second.V1_Pos = Center + V * Outside;
		Second.V2_Pos = Center + V * Radius;
		Second.V0_Color = Second.V1_Color = Second.V2_Color = FLinearColor::Black;
	}
	FCanvasTriangleItem Mask(Triangles, GWhiteTexture); // 不透明遮罩避免场景漏光。
	Mask.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Mask);
	// 全直径细十字保持中心精度，黑色线宽随分辨率温和变化。
	const float Thickness = FMath::Max(1.f, Canvas->ClipY / 720.f);
	DrawLine(Center.X - Radius, Center.Y, Center.X + Radius, Center.Y, FLinearColor::Black, Thickness);
	DrawLine(Center.X, Center.Y - Radius, Center.X, Center.Y + Radius, FLinearColor::Black, Thickness);
}

void ADemoHUD::DrawStatus(const ADemoGameState* State, ADemoCharacter* Player, const UDemoAttributeSet* Attributes)
{
	DEMO_LOG_TICK();
	// Combat 控制技能文字与准星；ASC 借用 PlayerState 所有的组件，读数来自真实 GAS。
	const bool bCombat = State->Phase == EDemoPhase::Combat;
	const bool bIntermission = State->Phase == EDemoPhase::Intermission; // 清关原地备战与初始安全区使用不同导航文案。
	UDemoAbilitySystemComponent* ASC = Player->GetDemoASC();
	const float Bottom = ViewHeight - 112.f; // 底部保留 24 设计像素安全边距。
	Panel(24, 24, 300, 96, DemoUI::BattleSurface);
	Panel(42, 43, 4, 22, DemoUI::Accent, 0.f);
	Label(TEXT("B R E A C H"), 58, 38, 24, DemoUI::Ink);
	// 终局与奖励不误标为安全区，阶段文案不依赖菜单是否已完成输入切换。
	const FString PhaseTitle = State->bEndless ? FString::Printf(TEXT("无尽 第%d关 · 最高通关%d"),State->LevelNumber,State->BestEndlessLevel) : bCombat ? FString::Printf(TEXT("第 %02d / %d 关  ·  怪物 Lv%d"), State->LevelNumber, DemoCombatConfig::LevelCount, State->LevelNumber)
		: bIntermission ? TEXT("关卡已清空  /  原地备战") : State->Phase == EDemoPhase::Hub ? TEXT("安全区  /  备战与能力升级") : State->Phase == EDemoPhase::Reward ? TEXT("关卡完成  /  选择能力强化") : TEXT("行动结算  /  本局统计");
	Label(PhaseTitle, 42, 72, 16, DemoUI::Muted);
	Label(bCombat ? FString::Printf(TEXT("剩余 %d   ·   累计击杀 %d"), State->EnemiesRemaining, State->TotalKills) : bIntermission ? TEXT("前往本关中心：升级终端 / 下一关") : State->Phase == EDemoPhase::Hub ? TEXT("终端强化后，前往入口继续任务") : TEXT("BREACH / 突破行动"), 42, 98, 13, bCombat ? DemoUI::Ink : DemoUI::Accent);
	// 两种货币同时可见，紧凑两行不侵占Boss血条；用途由文字明确说明。
	Panel(ViewWidth - 242, 24, 218, 90, DemoUI::Surface);
	Label(FString::Printf(TEXT("金币  %d"), State->Coins), ViewWidth - 222, 34, 21, DemoUI::Gold);
	Label(FString::Printf(TEXT("银币  %d"), State->SilverCoins), ViewWidth - 222, 70, 21, DemoUI::Ink);
	// 第一关开始后难度锁定；修改入口只存在于初始安全区下一关终端。
	Label(State->bEndless ? TEXT("模式：无尽 / 地狱基线") : State->Difficulty == EDemoDifficulty::Hell ? TEXT("难度：地狱") : State->Difficulty == EDemoDifficulty::Easy ? TEXT("难度：简单") : State->Difficulty == EDemoDifficulty::Hard ? TEXT("难度：困难") : TEXT("难度：普通"), ViewWidth - 222, 97, 12, DemoUI::Muted); // 难度留在钱包面板内部，终端弹窗不会遮住半行文字。
	Panel(24, Bottom, 292, 88, DemoUI::BattleSurface);
	Label(TEXT("VITALS / 生命状态"), 42, Bottom + 12, 12, DemoUI::Muted);
	Label(FString::Printf(TEXT("%.0f"), Attributes->GetHealth()), 42, Bottom + 29, 32, Attributes->GetHealth() < 30.f ? DemoUI::Danger : DemoUI::Ink);
	Label(FString::Printf(TEXT("/ %.0f"), Attributes->GetMaxHealth()), 116, Bottom + 46, 14, DemoUI::Muted);
	Meter(42, Bottom + 74, 256, 4, Attributes->GetHealth() / FMath::Max(1.f, Attributes->GetMaxHealth()), DemoUI::Accent);
	Panel(ViewWidth - 270, Bottom, 246, 88, DemoUI::BattleSurface);
	// 装备组件与武器均为当前帧借用，弹药直接读实例；不缓存跨切枪/重生的数据。
	const UDemoWeaponComponent* Equipment = Player->GetWeaponComponent();
	const ADemoWeaponBase* Weapon = Equipment->GetActiveWeapon();
	if (Weapon)
	{
		Label(FString::Printf(TEXT("[%d] %s  /  %s"), Equipment->GetActiveSlot(), *Weapon->Config.DisplayName.ToString(), Weapon->Config.bInfiniteReserve ? TEXT("备用 ∞") : *FString::Printf(TEXT("备用 %d"), Weapon->GetReserveAmmo())), ViewWidth - 252, Bottom + 12, 12, DemoUI::Muted);
		Label(FString::Printf(TEXT("%02d"), Weapon->GetAmmo()), ViewWidth - 252, Bottom + 29, 32, Weapon->GetAmmo() == 0 ? DemoUI::Danger : DemoUI::Ink);
		Label(FString::Printf(TEXT("/ %d"), Weapon->GetCapacity()), ViewWidth - 177, Bottom + 46, 14, DemoUI::Muted);
		Label(FString::Printf(TEXT("[R] 装填 · 伤害 %.1f × %d"), Weapon->GetDamagePerPellet(), Weapon->GetPelletCount()), ViewWidth - 252, Bottom + 70, 12, DemoUI::Muted);
	}
	// 当前弹药类型提示独立于武器实例，切枪不会改变主副武器共用选择。
    const UDemoAmmoComponent* AmmoSelection=Player->FindComponentByClass<UDemoAmmoComponent>(); // 本帧借用，目录已加载。
    if(AmmoSelection && AmmoSelection->GetCatalog()->Entries.IsValidIndex(AmmoSelection->GetSelected()))
        Label(AmmoSelection->GetCatalog()->Entries[AmmoSelection->GetSelected()].Name.ToString(),ViewWidth-110,Bottom+45,13,DemoUI::Accent);
	// 主武器只在终端装备；空主槽明确提示，避免旧B键说明误导玩家。
	Label(Equipment->GetPrimaryIndex() == INDEX_NONE ? TEXT("仅持手枪 · 前往终端装备已解锁主武器") : TEXT("[1] 主武器  [2] 手枪  [右键] 狙击镜"), ViewWidth - 370, Bottom - 26, 12, DemoUI::Muted);
	// HUD与GA共用技能许可，清关/通关回Hub不再误报“战斗外锁定”；菜单阻塞优先于冷却/满血。
	const bool bSkillsAllowed = Player->CanUsePlayerSkills(); // 本帧技能权限，武器准星仍由bCombat控制。
	for (int32 Index = 0; Index < 2; ++Index) // Index=0 冲刺，1 治疗；只构建展示，不触发能力。
	{
		const float X = ViewWidth / 2.f - 152.f + Index * 158.f; // 技能面板左边界。
		const float Remaining = ASC ? ASC->GetCooldownRemaining(Index == 0 ? DemoTags::DashCooldown : DemoTags::HealCooldown) : 0.f; // 秒。
		const bool bFullHealth = Index == 1 && Attributes->GetHealth() >= Attributes->GetMaxHealth(); // 治疗不浪费技能。
		const FString Status = !bSkillsAllowed ? TEXT("当前不可用") : Remaining > 0.f ? FString::Printf(TEXT("冷却 %.1fs"), Remaining) : bFullHealth ? TEXT("生命已满") : TEXT("就绪"); // 与GAS阶段/菜单限制一致，仍保留满血不消费冷却。
		Panel(X, Bottom + 8, 148, 80, DemoUI::Surface);
		Label(Index == 0 ? TEXT("冲刺   [SHIFT]") : TEXT("治疗       [Q]"), X + 12, Bottom + 20, 14, DemoUI::Ink);
		Label(Status, X + 12, Bottom + 48, 14, bSkillsAllowed && Remaining <= 0.f && !bFullHealth ? DemoUI::Accent : DemoUI::Muted);
		if (bSkillsAllowed && Remaining > 0.f) Meter(X + 12, Bottom + 75, 124, 3, 1.f - Remaining / (Index == 0 ? 4.f : 12.f), DemoUI::Accent);
	}
	if (bCombat)
	{
		const float CenterX = ViewWidth / 2.f; // 准星与相机中心一致，不随左右 HUD 排版移动。
		const float CenterY = ViewHeight / 2.f;
		const FLinearColor CrossColor = DemoUI::Ink; // 准星始终使用原灰白色，命中反馈由独立红色斜线承担。
		// 镜内使用专用细十字，普通准星仅在未瞄准时显示。
		if (Equipment->GetScopeLevel() == 0)
		{
		Panel(CenterX - 11, CenterY - 1, 7, 2, CrossColor, 0);
		Panel(CenterX + 4, CenterY - 1, 7, 2, CrossColor, 0);
		Panel(CenterX - 1, CenterY - 11, 2, 7, CrossColor, 0);
		Panel(CenterX - 1, CenterY + 4, 2, 7, CrossColor, 0);
		}
		// 使用既有真实武器命中时间：0.15秒内在四个45°方向绘制短线；连击刷新时间，开镜也显示，不修改准星/镜内十字颜色。
		const float HitAge = static_cast<float>(GetWorld()->GetTimeSeconds()) - Player->LastHitTime; // 与float命中时间统一精度，避免同帧double相减得到微小负数而漏掉首帧；暂停不推进。
		if (HitAge >= 0.f && HitAge < 0.15f)
		{
			UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_MARKER age=%.3f"), HitAge); // 绘制诊断沿用高频级别，默认调试配置可追踪真实触发时间。
			const FVector2D Directions[] = { FVector2D(-1,-1), FVector2D(1,-1), FVector2D(-1,1), FVector2D(1,1) }; // 四象限对角方向，不单位化以便直接使用X/Y设计像素偏移。
			const FLinearColor HitColor(FColor(255,48,48)); // 独立红色命中标记，不复用会受主题更改影响的青色Accent。
			for (const FVector2D& Direction : Directions) // 本帧局部数组借用；每象限画一段，中心留空，不拼成遮挡瞄准点的完整X。
			{
				DrawLine((CenterX + Direction.X * 7.f) * UIScale, (CenterY + Direction.Y * 7.f) * UIScale,
					(CenterX + Direction.X * 14.f) * UIScale, (CenterY + Direction.Y * 14.f) * UIScale, HitColor, 2.f * UIScale);
			}
		}
		if (ASC && ASC->HasMatchingGameplayTag(DemoTags::Reloading)) Label(TEXT("装填中…"), CenterX, CenterY + 44, 16, DemoUI::Accent, true);
		if (GetWorld()->GetTimeSeconds() - Player->LastDamageTime < 0.35f) Label(TEXT("受到攻击"), CenterX, CenterY - 70, 18, DemoUI::Danger, true);
		// It 只遍历当前 World；仅活 Boss 显示血条，小怪仍由剩余敌人数跟踪。
		for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It)
		{
			if (!It->IsBoss() || !It->IsAlive()) continue;
			Panel(CenterX - 250, 24, 500, 82, DemoUI::Surface);
			Label(TEXT("WARDEN / 典狱长"), CenterX - 230, 37, 19, DemoUI::Ink);
			Label(FString::Printf(TEXT("%.0f / %.0f"), It->GetHealth(), It->GetMaxHealth()), CenterX + 118, 40, 16, DemoUI::Gold);
			Meter(CenterX - 230, 71, 460, 5, It->GetHealth() / FMath::Max(1.f, It->GetMaxHealth()), DemoUI::Gold);
			Label(TEXT("击败 Boss 并清除全部小怪"), CenterX, 85, 12, DemoUI::Muted, true);
			break;
		}
	}
	else if (State->Phase == EDemoPhase::Hub || bIntermission)
	{
		const ADemoPlayerController* PC = Cast<ADemoPlayerController>(PlayerOwner); // 借用控制器，检测输入模式。
		// 模态窗口打开时隐藏中央导航，保留周边金币与生命信息。
		if (PC && !PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen())
		{
			Panel(ViewWidth / 2.f - 210, ViewHeight / 2.f + 65, 420, 76, DemoUI::Surface);
			Label(State->bEndless ? FString::Printf(TEXT("无尽 · 下一关 %d · 最高通关 %d"),State->LevelNumber+1,State->BestEndlessLevel) : FString::Printf(TEXT("%s  ·  下一关 %02d / %02d"), bIntermission ? TEXT("原地备战") : TEXT("安全区"), FMath::Min(DemoCombatConfig::LevelCount, State->LevelNumber + 1), DemoCombatConfig::LevelCount), ViewWidth / 2.f, ViewHeight / 2.f + 78, 16, DemoUI::Accent, true);
			const ADemoInteractable* Target = Player->FindInteractable(); // 借用附近物体，没有物体时给出导航提示。
			Label(Target ? Target->GetPrompt() : bIntermission ? TEXT("前往本关中心，靠近终端按 E 交互") : TEXT("靠近升级终端或关卡入口，按 E 交互"), ViewWidth / 2.f, ViewHeight / 2.f + 108, 16, DemoUI::Ink, true);
			Label(TEXT("WASD 移动     鼠标 瞄准     SPACE 跳跃"), ViewWidth / 2.f, Bottom - 28, 13, DemoUI::Ink, true);
		}
	}
}

void ADemoHUD::DrawUpgradeMenu(ADemoPlayerController* PC, const ADemoGameState* State, ADemoCharacter* Player, const UDemoAttributeSet* Attributes)
{
	DEMO_LOG_TICK();
	if (PC->IsAmmoMenuOpen()) { DrawAmmoMenu(PC, Player); return; } // 弹药页与武器页互斥，奖励页不出现此入口。
	if (PC->IsWeaponMenuOpen()) { DrawWeaponMenu(PC, Player); return; } // 同一终端输入锁内切换，奖励页不可进入武器库。
	// 设计矩形以视口中心定位；缩放只由 DrawHUD 决定，正文与 HitBox 始终同源。
	const float X = ViewWidth / 2.f - 480.f;
	const float Y = ViewHeight / 2.f - 240.f;
	const bool bReward = PC->IsRewardMenu(); // 免费奖励与商店共享生命周期，卡片/行式布局不同。
	Panel(X, Y, 960, 480, DemoUI::Edge, 12);
	Panel(X + 1, Y + 1, 958, 478, DemoUI::Surface, 11);
	if (!bReward) // 页签置于右上空白区，保留原商店交易热区与奖励卡片布局。
	{
		Button(TEXT("TerminalStats"), TEXT("属性升级"), X + 430, Y + 25, 120, 36, true, true);
		Button(TEXT("TerminalWeapons"), TEXT("武器"), X + 560, Y + 25, 100, 36, true, true);
        if(State->Phase==EDemoPhase::Hub)Button(TEXT("TerminalAmmo"),TEXT("弹药类型"),X+670,Y+25,130,36,true,true); // 关间终端不允许换弹药。
	}
	Label(bReward ? TEXT("SECTOR CLEAR / 关卡完成") : TEXT("FIELD SYSTEMS / 备战终端"), X + 32, Y + 25, 13, DemoUI::Accent);
	Label(bReward ? TEXT("选择一项能力强化") : TEXT("升级终端"), X + 32, Y + 51, 32, DemoUI::Ink);
	Label(bReward ? TEXT("免费三选一 · 本关仅可领取一次 · 选择后前往本关中心终端") : State->Phase == EDemoPhase::Hub ? TEXT("金币升级永久保留；仅所购属性的金币价格上涨。") : TEXT("银币升级仅本轮有效；仅所购属性的银币价格上涨。"), X + 32, Y + 98, 16, DemoUI::Muted);
	if (bReward)
	{
		const float Values[] = { Attributes->GetWeaponDamageBonus(), Player->GetHealAmount(), Player->GetDashSpeed() }; // 全局伤害加成/治疗/冲刺的升级前值，伤害不是某一武器的基础伤害。
		const float Bonuses[] = { 10.f, 20.f, 350.f }; // 与 ApplyAbilityReward 三项对应，不影响冷却。
		const TCHAR* Titles[] = { TEXT("强效弹药"), TEXT("战地医疗"), TEXT("机动强化") }; // 0..2 路由顺序禁止随排版改动。
		const TCHAR* Details[] = { TEXT("全武器伤害加成 +10"), TEXT("治疗恢复量 +20 HP"), TEXT("冲刺速度 +350 cm/s") }; // 当前存在的收益。
		for (int32 Index = 0; Index < 3; ++Index) // Index 是服务器识别的奖励选项号。
		{
			const float CardX = X + 32 + Index * 302.f; // 三卡等宽，间距 16。
			const bool bHover = IsHovered(CardX, Y + 145, 286, 228); // 整卡可点，非仅底部文字。
			Panel(CardX, Y + 145, 286, 228, bHover ? DemoUI::Edge : DemoUI::Raised);
			Panel(CardX + 18, Y + 165, 40, 36, DemoUI::Surface);
			Label(FString::FromInt(Index + 1), CardX + 38, Y + 173, 19, DemoUI::Accent, true);
			Label(TEXT("免费"), CardX + 228, Y + 176, 13, DemoUI::Accent);
			Label(Titles[Index], CardX + 18, Y + 220, 23, DemoUI::Ink);
			Label(FString::Printf(TEXT("%.0f  →  %.0f"), Values[Index], Values[Index] + Bonuses[Index]), CardX + 18, Y + 260, 24, DemoUI::Accent);
			Label(Details[Index], CardX + 18, Y + 302, 14, DemoUI::Muted);
			Label(TEXT("选择此强化  →"), CardX + 18, Y + 343, 15, DemoUI::Ink);
			AddHitBox(FVector2D(CardX, Y + 145) * UIScale, FVector2D(286, 228) * UIScale, FName(*FString::Printf(TEXT("Upgrade%d"), Index)), true);
		}
		Label(TEXT("点击卡片，或按 1 / 2 / 3 选择；本次不消耗货币"), X + 32, Y + 415, 15, DemoUI::Muted);
		if (!PC->GetMenuMessage().IsEmpty()) Label(PC->GetMenuMessage(), X + 32, Y + 445, 14, DemoUI::Gold);
		return;
	}
	// Mode 为本地权威对象，范围/阶段/余额与交易共用判断，不能只凭钱包启用购买。
	const AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
	const bool bGold = State->Phase == EDemoPhase::Hub; // 与权威购买接口相同的区域判定，只展示不决定交易权限。
	const TCHAR* Currency = bGold ? TEXT("金币") : TEXT("银币"); // 本帧静态文案借用，禁止自动用另一钱包补差额。
	const int32 CurrencyPurchases = bGold ? State->GoldPurchases : State->SilverPurchases; // 只展示总购买数，不作为各项价格。
	const float Values[] = { Attributes->GetWeaponDamageBonus(), Attributes->GetMaxHealth(), Attributes->GetMagazineBonus() }; // GAS全局伤害/容量加成与生命上限，避免把不同枪的基础值混为一谈。
	const float Bonuses[] = { 5.f, 25.f, 4.f }; // 伤害、生命上限、弹匣的单次增量。
	const TCHAR* Titles[] = { TEXT("武器调校"), TEXT("生命强化"), TEXT("扩容弹匣") }; // 下标与购买路由一致。
	const TCHAR* Details[] = { TEXT("全武器伤害加成 +5"), TEXT("生命上限 +25，同时恢复 25 HP"), TEXT("全武器容量 +4，同时各补 4 发") }; // 补给不超过新上限。
	Button(TEXT("Close"), TEXT("×  关闭"), X + 822, Y + 25, 106, 36, true, true);
	for (int32 Index = 0; Index < 3; ++Index) // 每行独立定价/余额校验，昂贵项目买不起不能禁用其他行。
	{
		const int32 Cost = Mode ? Mode->GetUpgradeCost(Index) : 0; // 当前项目当前币种价格。
		const bool bCanBuy = Mode && Mode->GetPurchaseBlockReason(Player, Index).IsEmpty(); // 本行实际可买状态。
		const float RowY = Y + 143 + Index * 84.f; // 左侧属性行起点，右侧为摘要。
		Panel(X + 32, RowY + 8, 40, 40, DemoUI::Raised);
		Label(FString::Printf(TEXT("%02d"), Index + 1), X + 52, RowY + 18, 16, DemoUI::Accent, true);
		Label(Titles[Index], X + 88, RowY, 18, DemoUI::Ink);
		Label(FString::Printf(TEXT("%.0f  →  %.0f"), Values[Index], Values[Index] + Bonuses[Index]), X + 228, RowY, 18, DemoUI::Ink);
		Label(Details[Index], X + 88, RowY + 35, 14, DemoUI::Muted);
		Button(FName(*FString::Printf(TEXT("Upgrade%d"), Index)), FString::Printf(TEXT("%d %s"), Cost, Currency), X + 536, RowY + 3, 122, 42, bCanBuy);
		if (Index < 2) Panel(X + 32, RowY + 67, 626, 1, DemoUI::Edge, 0);
	}
	Panel(X + 690, Y + 143, 238, 241, DemoUI::Raised);
	Label(TEXT("当前配置 / LOADOUT"), X + 710, Y + 161, 13, DemoUI::Muted);
	Label(FString::Printf(TEXT("伤害加成          %.0f"), Values[0]), X + 710, Y + 197, 15, DemoUI::Ink);
	Label(FString::Printf(TEXT("生命上限          %.0f"), Values[1]), X + 710, Y + 225, 15, DemoUI::Ink);
	Label(FString::Printf(TEXT("容量加成          %.0f"), Values[2]), X + 710, Y + 253, 15, DemoUI::Ink);
	Panel(X + 710, Y + 286, 198, 1, DemoUI::Edge, 0);
	Label(FString::Printf(TEXT("%s已购          %d 次"), Currency, CurrencyPurchases), X + 710, Y + 299, 14, DemoUI::Muted);
	Label(TEXT("仅购买的属性价格 +10"), X + 710, Y + 340, 13, DemoUI::Muted);
	Label(PC->GetMenuMessage().IsEmpty() ? TEXT("各项独立定价 · 灰色按钮表示当前不可购买") : PC->GetMenuMessage(), X + 32, Y + 400, 15, DemoUI::Accent);
	Panel(X + 32, Y + 432, 896, 1, DemoUI::Edge, 0);
	Label(bGold ? TEXT("金币成长与各项价格永久保留") : TEXT("银币成长与各项价格在挑战结束后重置"), X + 32, Y + 447, 13, DemoUI::Muted);
	Label(TEXT("1 / 2 / 3 购买      TAB / E 关闭"), X + 654, Y + 447, 13, DemoUI::Muted);
}

void ADemoHUD::DrawNextLevelConfirmation(const ADemoGameState* State)
{
	DEMO_LOG_TICK();
    if (State->Phase == EDemoPhase::Hub)
    {
        // 首次出发直接选择并锁定难度；没有默认“是”按钮绕过选择。
        const float X = ViewWidth*.5f-350.f, Y = ViewHeight*.5f-235.f; // 独立难度模态布局。
        Panel(X,Y,700,470,DemoUI::Surface,12);
        Label(TEXT("选择本局难度"),X+32,Y+34,30,DemoUI::Ink);
        Label(TEXT("开始战斗后锁定，影响敌人生命、伤害和攻击频率"),X+32,Y+92,17,DemoUI::Muted);
        Button(TEXT("Easy"),TEXT("简单"),X+32,Y+154,196,65);
        Button(TEXT("Normal"),TEXT("普通"),X+252,Y+154,196,65);
        Button(TEXT("Hard"),TEXT("困难"),X+472,Y+154,196,65);
        const UDemoPlayerProfile* Progress=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 只读解锁事实；GameMode在点击时重复验证。
        Button(TEXT("Hell"),Progress&&Progress->IsHellUnlocked()?TEXT("地狱"):TEXT("地狱 · 未解锁"),X+32,Y+245,306,58,Progress&&Progress->IsHellUnlocked());
        Button(TEXT("Endless"),Progress&&Progress->IsEndlessUnlocked()?TEXT("无尽模式"):TEXT("无尽 · 未解锁"),X+362,Y+245,306,58,Progress&&Progress->IsEndlessUnlocked());
        Label(TEXT("地狱：整轮只用手枪开火通关困难（允许冲刺/治疗）"),X+32,Y+321,14,DemoUI::Muted);
        Label(TEXT("无尽：通关地狱；每5关出现Boss，成长与掉落逐关提升"),X+32,Y+348,14,DemoUI::Muted);
        Button(TEXT("NextLevelNo"),TEXT("取消出发"),X+472,Y+400,196,44,true,true);
        return;
    }

	const float X = ViewWidth / 2.f - 280.f; // 确认框左边界，设计像素，按现有 UIScale 适配视口。
	const float Y = ViewHeight / 2.f - 150.f; // 确认框上边界；不覆盖底部生命/技能信息。
	Panel(X, Y, 560, 300, DemoUI::Surface, 12);
	Label(TEXT("准备出发"), X + 32, Y + 28, 15, DemoUI::Accent);
	Label(TEXT("是否进入下一关？"), X + 280, Y + 72, 30, DemoUI::Ink, true);
	Label(State->bEndless ? FString::Printf(TEXT("即将进入无尽第 %d 关"),State->LevelNumber+1) : FString::Printf(TEXT("即将进入第 %02d / %02d 关"), State->LevelNumber + 1, DemoCombatConfig::LevelCount), X + 280, Y + 121, 18, DemoUI::Gold, true);
	Label(TEXT("选择“否”可留在当前区域继续准备。"), X + 280, Y + 157, 15, DemoUI::Muted, true);
	Button(TEXT("NextLevelNo"), TEXT("否 · 留在这里 [TAB]"), X + 32, Y + 212, 238, 52, true, true);
	Button(TEXT("NextLevelYes"), TEXT("是 · 进入下一关 [ENTER]"), X + 290, Y + 212, 238, 52);
}

void ADemoHUD::PrepareWeaponIcons(const UDemoWeaponComponent* Equipment)
{
	DEMO_LOG_CALL();
	// 取消也移除已排队的完成委托；随后才替换路径，防止旧请求把结果写进新目录。
	if (WeaponIconLoadHandle) { WeaponIconLoadHandle->CancelHandle(); WeaponIconLoadHandle.Reset(); }
	WeaponIconPaths.SetNum(4);
	WeaponIcons.SetNum(4);
	TArray<FSoftObjectPath> PendingPaths; // 本次去重的尚未驻留路径，只交给流送管理器，不跨线程访问目录CDO。
	for (int32 Index = 0; Index < 4; ++Index) // 固定目录顺序；空图标为合法配置，维持轮廓回退。
	{
		const ADemoWeaponBase* Definition = Equipment ? Equipment->GetCatalogWeapon(Index) : nullptr; // 本次同步借用CDO。
		WeaponIconPaths[Index] = Definition ? Definition->Config.Icon.ToSoftObjectPath() : FSoftObjectPath();
		WeaponIcons[Index] = Cast<UTexture2D>(WeaponIconPaths[Index].ResolveObject());
		if (!WeaponIconPaths[Index].IsNull() && !WeaponIcons[Index]) PendingPaths.AddUnique(WeaponIconPaths[Index]);
	}
	if (PendingPaths.IsEmpty()) { UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_ICONS ready: resident or fallback")); return; }
	// CreateUObject弱绑定当前HUD，无Lambda捕获；UE在游戏线程回调，EndPlay显式取消以避免旅行后的迟到访问。
	WeaponIconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(PendingPaths,
		FStreamableDelegate::CreateUObject(this, &ADemoHUD::OnWeaponIconsLoaded));
	if (!WeaponIconLoadHandle) UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_ICONS request failed; using silhouettes"));
}
void ADemoHUD::OnWeaponIconsLoaded()
{
	DEMO_LOG_CALL();
	for (int32 Index = 0; Index < WeaponIconPaths.Num(); ++Index) // 仅当前请求快照，任何失败都保持该卡的轮廓。
	{
		WeaponIcons[Index] = Cast<UTexture2D>(WeaponIconPaths[Index].ResolveObject());
		if (!WeaponIconPaths[Index].IsNull() && !WeaponIcons[Index]) UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_ICON failed path=%s; fallback until next page open"), *WeaponIconPaths[Index].ToString());
	}
	// UPROPERTY已保活成功纹理，不再依赖加载句柄；关闭终端仍缓存，HUD卸载统一释放。
	WeaponIconLoadHandle.Reset();
	UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_ICONS async complete"));
}
const UTexture2D* ADemoHUD::GetWeaponIcon(int32 Index) const
{
	DEMO_LOG_TICK();
	return WeaponIcons.IsValidIndex(Index) ? WeaponIcons[Index].Get() : nullptr;
}
void ADemoHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
	if (WeaponIconLoadHandle) { WeaponIconLoadHandle->CancelHandle(); WeaponIconLoadHandle.Reset(); }
	WeaponIcons.Empty();
	WeaponIconPaths.Empty();
	Super::EndPlay(EndPlayReason);
}
void ADemoHUD::DrawWeaponIcon(int32 Index, float X, float Y, FLinearColor Color, bool bUnlocked)
{
	DEMO_LOG_TICK();
	const UTexture2D* Icon = GetWeaponIcon(Index); // 只读缓存；此函数禁止同步加载或访问外部文件路径。
	if (Icon && Icon->GetResource() && Icon->GetSizeX() > 0 && Icon->GetSizeY() > 0)
	{
		const float Fit = FMath::Min(160.f / Icon->GetSizeX(), 76.f / Icon->GetSizeY()); // contain缩放，宽高比不变且不裁掉武器细节。
		const FVector2D Size(Icon->GetSizeX() * Fit, Icon->GetSizeY() * Fit); // 设计像素尺寸，DrawItem再换算为视口像素。
		const FVector2D Position(X + (160.f - Size.X) * .5f, Y + (76.f - Size.Y) * .5f); // Icon盒中居中。
		FCanvasTileItem Tile(Position * UIScale, Icon->GetResource(), Size * UIScale, bUnlocked ? FLinearColor::White : FLinearColor(.65f,.65f,.65f,1.f)); // 本帧绘制批次，不持有跨帧裸纹理。
		// UE5.4 Canvas通道掩码低5位为RGBA+去色；31保留Alpha并将RGB平均为灰，彩色Icon锁定时也能正确置灰。
		Tile.BlendMode = bUnlocked ? SE_BLEND_Translucent : static_cast<ESimpleElementBlendMode>(SE_BLEND_RGBA_MASK_START + 31);
		Canvas->DrawItem(Tile);
		return;
	}
	// 未配置、异步未完成或资源无效时显示原有轮廓；不让可选美术资源阻塞武器功能。
	if (Index == 0)
	{
		Panel(X + 30, Y + 18, 78, 17, Color, 3);
		Panel(X + 37, Y + 34, 19, 37, Color, 2);
		Panel(X + 56, Y + 37, 27, 5, Color, 0);
		Panel(X + 79, Y + 34, 5, 15, Color, 0);
		Panel(X + 56, Y + 47, 26, 4, Color, 0);
		return;
	}
	Panel(X + 8, Y + 31, 30, 24, Color, 2);
	Panel(X + 32, Y + 28, 71, 17, Color, 3);
	Panel(X + 100, Y + 30, 47, 6, Color, 0);
	Panel(X + 49, Y + 44, 12, 24, Color, 1);
	if (Index == 1) Panel(X + 75, Y + 43, 16, 30, Color, 2);
	if (Index == 2) Panel(X + 88, Y + 40, 48, 9, Color, 2);
	if (Index == 3)
	{
		Panel(X + 58, Y + 14, 52, 9, Color, 3);
		Panel(X + 73, Y + 22, 5, 8, Color, 0);
		Panel(X + 94, Y + 22, 5, 8, Color, 0);
		Panel(X + 141, Y + 27, 14, 11, Color, 1);
	}
}
void ADemoHUD::DrawWeaponMenu(ADemoPlayerController* PC, ADemoCharacter* Player)
{
	DEMO_LOG_TICK();
	const float X = ViewWidth / 2.f - 480.f; // 与属性商店共用居中锚点，设计宽960。
	const float Y = ViewHeight / 2.f - 240.f; // 设计高480，保留四周HUD上下文。
	const UDemoWeaponComponent* Equipment = Player->GetWeaponComponent(); // 本帧借用装备实例及缓存目录。
	const UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // GI拥有永久进度。
	const int32 Inspected = PC->GetInspectedWeapon(); // INDEX_NONE表示没有模态Tips，不缓存到下一帧。
	const bool bTipOpen = Inspected != INDEX_NONE; // 弹窗打开后下层不注册任何可点击热区。
	const AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 权威范围状态仅用于显示，点击仍重新检查。
	const FString Reason = Mode ? Mode->GetTerminalBlockReason(Player) : TEXT("当前仅支持单人模式"); // 当前操作阻塞原因。
	Panel(X, Y, 960, 480, DemoUI::Surface, 12);
	Label(TEXT("FIELD ARMORY / 终端武器库"), X + 32, Y + 25, 13, DemoUI::Accent);
	Label(TEXT("武器"), X + 32, Y + 53, 30, DemoUI::Ink);
	Label(TEXT("点击卡片查看条件 · 解锁后在此装备主武器 · 手枪固定保留"), X + 32, Y + 98, 15, DemoUI::Muted);
	Button(TEXT("TerminalStats"), TEXT("属性升级"), X + 430, Y + 25, 120, 36, !bTipOpen, true);
	Button(TEXT("TerminalWeapons"), TEXT("武器"), X + 560, Y + 25, 100, 36, !bTipOpen);
    if(GetWorld()->GetGameState<ADemoGameState>()->Phase==EDemoPhase::Hub)Button(TEXT("TerminalAmmo"),TEXT("弹药类型"),X+670,Y+25,130,36,!bTipOpen,true); // 固定安全区入口。
	Button(TEXT("Close"), TEXT("×  关闭"), X + 822, Y + 25, 106, 36, !bTipOpen, true);
	for (int32 Index = 0; Index < 4; ++Index) // 固定目录四列；状态由存档和本局槽位分别读取。
	{
		const float CardX = X + 32 + Index * 226.f; // 卡片宽212，间距14设计像素。
		const bool bUnlocked = Profile && Profile->IsUnlocked(DemoWeaponCatalog::IdAt(Index)); // 永久解锁，不等同于持有。
		const bool bEquipped = Index == 0 || Equipment->GetPrimaryIndex() == Index - 1; // 手枪固定副槽，主槽只能有一个选中型号。
		const ADemoWeaponBase* Definition = Equipment->GetCatalogWeapon(Index); // CDO蓝图属性，不创建锁定武器实例。
		const FLinearColor ItemInk = bUnlocked ? DemoUI::Ink : FLinearColor(FColor(143,143,143)); // 锁定Item整体使用中性灰色。
		Panel(CardX, Y + 137, 212, 226, bUnlocked ? DemoUI::Raised : FLinearColor(FColor(37,37,37)));
		DrawWeaponIcon(Index, CardX + 26, Y + 147, ItemInk, bUnlocked); // 优先软引用贴图，锁定状态只影响展示。
		Label(Definition ? Definition->Config.DisplayName.ToString() : TEXT("资源未就绪"), CardX + 16, Y + 230, 23, ItemInk);
		Label(!bUnlocked ? TEXT("待解锁") : bEquipped ? (Index == 0 ? TEXT("已解锁 · 固定副武器") : TEXT("已解锁 · 主武器已装备")) : TEXT("已解锁 · 可装备"), CardX + 16, Y + 263, 12, bUnlocked ? DemoUI::Accent : ItemInk);
		if (Definition)
		{
			Label(FString::Printf(TEXT("基础伤害  %.0f × %d"), Definition->Config.BaseDamage, Definition->GetPelletCount()), CardX + 16, Y + 292, 14, ItemInk);
			Label(FString::Printf(TEXT("射速 %.0f RPM   弹匣 %d"), Definition->Config.RoundsPerMinute, Definition->Config.MagazineCapacity), CardX + 16, Y + 317, 13, ItemInk);
			Label(FString::Printf(TEXT("装填 %.1fs"), Definition->Config.ReloadSeconds), CardX + 16, Y + 339, 12, ItemInk);
		}
		if (!bTipOpen) AddHitBox(FVector2D(CardX, Y + 137) * UIScale, FVector2D(212,226) * UIScale, FName(*FString::Printf(TEXT("Weapon%d"), Index)), true);
	}
	Label(Reason.IsEmpty() ? TEXT("每局初始仅手枪；永久解锁保留，本局装备需在终端选择。") : Reason, X + 32, Y + 385, 14, Reason.IsEmpty() ? DemoUI::Muted : DemoUI::Gold);
	Label(Profile ? Profile->GetStatus() : TEXT("存档系统未就绪"), X + 32, Y + 406, 13, DemoUI::Muted); // 留出独立云状态行，避免与基础参数说明重叠。
	const UDemoCloudSync* Cloud = GetGameInstance()->GetSubsystem<UDemoCloudSync>(); // 本帧只读云状态，绘制不执行联网。
	if (Cloud) Label(Cloud->GetStatus(), X + 32, Y + 428, 12, DemoUI::Muted);
	Label(TEXT("展示蓝图基础参数；本局伤害与弹匣升级另行叠加"), X + 32, Y + 450, 12, DemoUI::Muted);
	Button(TEXT("ProfileSave"), TEXT("保存重试"), X + 800, Y + 415, 128, 36, !bTipOpen, true);
	if (!bTipOpen) return;
	// 模态层仅注册关闭/装备按钮，下层卡片和页签不会响应穿透点击。
	Panel(0, 0, ViewWidth, ViewHeight, DemoUI::Overlay, 0);
	const float TipX = ViewWidth / 2.f - 320; // 说明窗宽640，使用相同等比坐标。
	const float TipY = ViewHeight / 2.f - 150; // 说明窗高300，长失败提示有独立一行。
	const ADemoWeaponBase* Definition = Equipment->GetCatalogWeapon(Inspected); // 当前卡片定义的只读CDO。
	const bool bUnlocked = Profile && Profile->IsUnlocked(DemoWeaponCatalog::IdAt(Inspected)); // 点击时控制器会再次查证。
	Panel(TipX, TipY, 640, 300, DemoUI::Surface, 12);
	Label(Definition ? Definition->Config.DisplayName.ToString() : TEXT("武器详情"), TipX + 28, TipY + 25, 28, DemoUI::Ink);
	Label(DemoWeaponCatalog::UnlockText(Inspected), TipX + 28, TipY + 81, 20, bUnlocked ? DemoUI::Accent : DemoUI::Muted);
	Label(bUnlocked ? (Inspected == 0 ? TEXT("手枪已默认持有，占用副武器槽 [2]。") : TEXT("条件已达成。点击装备后占用主武器槽 [1]。")) : TEXT("当前未解锁；完成对应难度后永久保存解锁记录。"), TipX + 28, TipY + 120, 16, DemoUI::Ink);
	Label(TEXT("解锁不会自动发放武器；不消耗金币。"), TipX + 28, TipY + 151, 15, DemoUI::Muted);
	Label(!PC->GetMenuMessage().IsEmpty() ? PC->GetMenuMessage() : Reason, TipX + 28, TipY + 190, 14, DemoUI::Gold);
	Button(TEXT("WeaponTipClose"), TEXT("返回武器库"), TipX + 28, TipY + 238, 180, 40, true, true);
	if (Inspected > 0) Button(TEXT("WeaponEquip"), bUnlocked ? TEXT("装备为主武器") : TEXT("尚未解锁"), TipX + 366, TipY + 238, 246, 40, bUnlocked && Definition && Reason.IsEmpty());
}

void ADemoHUD::DrawEndScreen(const ADemoGameState* State)
{
	DEMO_LOG_TICK();
	// 结算只负责展示；银币/成长由GM在胜利时清理，金币和解锁保留。
	const bool bVictory = State->Phase == EDemoPhase::Victory;
	const float X = ViewWidth / 2.f - 380.f;
	const float Y = ViewHeight / 2.f - 190.f;
	Panel(X, Y, 760, 380, DemoUI::Surface, 12);
	Label(bVictory ? TEXT("MISSION COMPLETE") : TEXT("RUN ENDED"), ViewWidth / 2.f, Y + 30, 14, bVictory ? DemoUI::Accent : DemoUI::Danger, true);
	Label(bVictory ? TEXT("全部关卡突破完成") : TEXT("本次行动结束"), ViewWidth / 2.f, Y + 64, 36, DemoUI::Ink, true);
	Label(bVictory ? TEXT("金币成长与武器解锁保留；银币和临时成长已清空。") : FString::Printf(TEXT("止步第 %d 关 · 金币成长与解锁保留，临时成长重置"), State->LevelNumber), ViewWidth / 2.f, Y + 118, 17, DemoUI::Muted, true);
	// 系统异常保留原始失败原因；超长诊断留给日志，屏幕截断避免覆盖按钮。
	if (!bVictory && !State->FailureReason.IsEmpty()) Label(State->FailureReason == TEXT("You were eliminated") ? TEXT("生命耗尽") : State->FailureReason.Left(64), ViewWidth / 2.f, Y + 150, 13, DemoUI::Muted, true);
	if (bVictory) // 已解锁武器回到安全区即可在终端装备，当前已持有装备不会清空。
	{
		const int32 UnlockedIndex = static_cast<int32>(State->Difficulty) + 1; // 三难度对应步枪/散弹/狙击目录位置。
		const ADemoCharacter* Player = Cast<ADemoCharacter>(PlayerOwner->GetPawn()); // 本帧借用Avatar用于查询目录CDO。
		const ADemoWeaponBase* Definition = Player ? Player->GetWeaponComponent()->GetCatalogWeapon(UnlockedIndex) : nullptr; // 只读蓝图名称。
		// 困难通关现在包含散弹枪权限，结算文案与武器目录/本地及云端派生规则一致，不补造普通通关事实。
		Label(State->Difficulty==EDemoDifficulty::Hell ? TEXT("已通关最高难度，解锁无尽模式") : State->Difficulty==EDemoDifficulty::Hard && State->PistolChallenge==1 ? TEXT("手枪挑战完成：已解锁地狱、散弹枪与狙击枪") : State->Difficulty==EDemoDifficulty::Hard ? TEXT("已解锁：散弹枪与狙击枪 · 返回安全区后前往终端装备") : State->FailureReason.IsEmpty() ? FString::Printf(TEXT("已解锁：%s · 返回安全区后前往终端装备"), Definition ? *Definition->Config.DisplayName.ToString() : TEXT("主武器")) : State->FailureReason.Left(48), ViewWidth / 2.f, Y + 150, 14, DemoUI::Accent, true);
		if (const UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Label(Profile->GetStatus(), ViewWidth / 2.f, Y + 356, 12, DemoUI::Muted, true); // 保存状态独立于胜利本身。
	}
	const int32 Values[] = { State->TotalKills, State->Coins, State->SilverCoins }; // 只读统计，结算明确展示保留金币/已清银币。
	const TCHAR* Titles[] = { TEXT("累计击杀"), TEXT("保留金币"), TEXT("银币余额") }; // 与数值一一对应。
	for (int32 Index = 0; Index < 3; ++Index) // 三列等宽，信息不依赖颜色区分。
	{
		const float Center = X + 150 + Index * 230.f; // 当前统计列中心。
		Label(FString::FromInt(Values[Index]), Center, Y + 195, 32, Index == 1 ? DemoUI::Gold : DemoUI::Ink, true);
		Label(Titles[Index], Center, Y + 239, 14, DemoUI::Muted, true);
	}
	// 胜利和死亡均清空临时成长/银币，金币成长与解锁保留；系统错误仍返回大厅。
	Button(TEXT("Restart"), bVictory ? TEXT("[ENTER]  返回安全区 · 继续挑战") : State->bReturnToHubOnRestart ? TEXT("[ENTER]  返回安全区 · 重新挑战") : TEXT("[ENTER]  返回大厅"), X + 215, Y + 296, 330, 52);
}

void ADemoHUD::DrawLobbyBackground()
{
	DEMO_LOG_TICK();
	if (!LobbyBackground || !LobbyBackground->GetResource() || LobbyBackground->GetSizeX() <= 0 || LobbyBackground->GetSizeY() <= 0)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("Lobby background unavailable; drawing solid fallback"));
		Panel(0, 0, ViewWidth, ViewHeight, DemoUI::Surface, 0);
		return;
	}
	// 只裁切 UV，不拉伸图片；宽屏裁上下，窄屏裁左右，中心人物始终保持原比例。
	const float ImageAspect = static_cast<float>(LobbyBackground->GetSizeX()) / LobbyBackground->GetSizeY(); // 源图宽高比。
	const float ScreenAspect = ViewWidth / ViewHeight; // 当前视口宽高比，不假定 16:9。
	const float UVWidth = FMath::Min(1.f, ScreenAspect / ImageAspect); // 保留的横向纹理比例。
	const float UVHeight = FMath::Min(1.f, ImageAspect / ScreenAspect); // 保留的纵向纹理比例。
	DrawTexture(LobbyBackground, 0, 0, Canvas->ClipX, Canvas->ClipY, (1.f - UVWidth) / 2.f, (1.f - UVHeight) / 2.f,
		UVWidth, UVHeight, FLinearColor::White, BLEND_Opaque);
}

void ADemoHUD::LobbyButton(FName Name, const FString& Text, float X, float Y, float W, float H, bool bSelected)
{
	DEMO_LOG_TICK();
	// 大厅使用无色相灰阶，不再复用战斗 UI 的青色强调；悬停与持久选中仍清楚区分。
	const bool bHover = IsHovered(X, Y, W, H); // 当前帧鼠标是否位于真实点击矩形内。
	const uint8 Gray = bSelected ? (bHover ? 160 : 136) : (bHover ? 120 : 78); // sRGB 灰度亮度，三个通道相同。
	const float Alpha = bSelected ? (bHover ? 0.68f : 0.62f) : (bHover ? 0.60f : 0.42f); // 底色不透明度，始终保留背景细节。
	Panel(X, Y, W, H, FLinearColor(FColor(Gray, Gray, Gray)).CopyWithNewOpacity(Alpha));
	Label(Text, X + W / 2.f, Y + H / 2.f - 9.f, 16.f, DemoUI::Ink, true);
	// 细灰白下划线只标记当前难度，避免鼠标移开后无法区分已选项；不改变热区。
	if (bSelected) Panel(X + 12, Y + H - 4, W - 24, 2, FLinearColor(FColor(220, 220, 220, 190)), 0);
	AddHitBox(FVector2D(X, Y) * UIScale, FVector2D(W, H) * UIScale, Name, true);
}

void ADemoHUD::DrawLobby(ADemoPlayerController* PC, const ADemoGameState* State)
{
	DEMO_LOG_TICK();
	DrawLobbyBackground();
	// 面板宽 404、高 472 设计像素；左侧锚定、中心在视口 43% 高度，给人物保留中右画面。
	const float X = FMath::Clamp(LobbyLeftMargin, 16.f, 160.f); // 左边距，设计像素。
	const float Y = FMath::Clamp(ViewHeight * LobbyAnchorY - 236.f, 24.f, ViewHeight - 560.f); // 额外预留启动错误区域。
	const bool bHasError = !State->FailureReason.IsEmpty() && !PC->IsSettingsOpen(); // 配置错误仍在大厅提示。
	// 大厅独立使用无蓝色色相的深灰底；仅底色混合背景，标题、按钮和文字的透明度分别控制。
	const FLinearColor LobbySurface = FLinearColor(FColor(24, 24, 24)).CopyWithNewOpacity(FMath::Clamp(LobbyPanelOpacity, 0.15f, 0.95f)); // 本帧面板底色，默认45%不透明。
	Panel(X, Y, 404, bHasError ? 536 : 472, LobbySurface, 12);
	Panel(X + 24, Y + 28, 3, 34, DemoUI::Accent, 0);
	Label(TEXT("B R E A C H"), X + 40, Y + 22, 38, DemoUI::Ink);
	Label(TEXT("十关突破  /  单人战役"), X + 24, Y + 79, 16, DemoUI::Accent);
	Panel(X + 24, Y + 112, 356, 1, DemoUI::Muted.CopyWithNewOpacity(0.25f), 0);
    // 难度改为安全区出发终端选择，大厅只提供存档入口和全局设置。
    Label(TEXT("从存档继续你的十关行动"),X+24,Y+151,21,DemoUI::Ink);
    Label(TEXT("新存档从安全区开始。"),X+24,Y+201,16,DemoUI::Muted);
    Label(TEXT("与出发终端交互时选择本局难度。"),X+24,Y+234,15,DemoUI::Muted);
	LobbyButton(TEXT("StartGame"), TEXT("[ENTER]  开始游戏"), X + 24, Y + 281, 356, 52);
	LobbyButton(TEXT("LobbySettings"), TEXT("设置"), X + 24, Y + 347, 172, 40);
	LobbyButton(TEXT("QuitGame"), TEXT("退出游戏"), X + 208, Y + 347, 172, 40);
	Label(TEXT("第 5 / 10 关迎战 Boss"), X + 24, Y + 409, 14, DemoUI::Ink);
	Label(TEXT("清关选择强化，安全区金币升级。"), X + 24, Y + 437, 13, DemoUI::Muted);
	if (bHasError)
	{
		Label(TEXT("启动失败，请检查配置与日志："), X + 24, Y + 474, 14, DemoUI::Danger);
		// 窄面板仅展示诊断摘要，完整原因由 GameMode 记录，避免英文长句横向越界。
		Label(State->FailureReason.Left(42), X + 24, Y + 503, 12, DemoUI::Gold);
	}
	// 独立底部条不改变既有左上大厅锚点；冲突操作只作用于检查点且先自动归档两份副本。
	const UDemoCloudSync* Cloud = GetGameInstance()->GetSubsystem<UDemoCloudSync>(); // GI只读状态，当前帧借用。
	if (Cloud)
	{
		Panel(24, ViewHeight - 91, ViewWidth - 48, 67, LobbySurface, 8);
		Label(Cloud->GetStatus(), 40, ViewHeight - 72, 14, DemoUI::Ink);
		if (Cloud->HasConflict())
		{
			LobbyButton(TEXT("CloudLocal"), TEXT("保留本机检查点"), ViewWidth - 380, ViewHeight - 79, 166, 40);
			LobbyButton(TEXT("CloudRemote"), TEXT("使用云端检查点"), ViewWidth - 202, ViewHeight - 79, 166, 40);
		}
		else LobbyButton(TEXT("CloudRetry"), TEXT("重试同步"), ViewWidth - 180, ViewHeight - 79, 144, 40);
	}
}

void ADemoHUD::NotifyHitBoxClick(FName BoxName)
{
	DEMO_LOG_CALL();
	Super::NotifyHitBoxClick(BoxName);
	// 借用当前拥有者；按钮/数字键共用业务入口，热区不保存 UObject 回调。
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(PlayerOwner);
	if (!PC) { UE_LOG(LogFPSDemo, Warning, TEXT("HUD click rejected: no Demo controller")); return; }
	UE_LOG(LogFPSDemo, Log, TEXT("UI click: %s"), *BoxName.ToString());
	if (HandleSessionClick(BoxName)) return; // 最上层先消费，旧帧热区不能穿透。
	if (BoxName == TEXT("CloudRetry")) { PC->CloudAction(0); return; }
	if (BoxName == TEXT("CloudLocal")) { PC->CloudAction(1); return; }
	if (BoxName == TEXT("CloudRemote")) { PC->CloudAction(2); return; }
	if(BoxName==TEXT("TerminalAmmo"))PC->OpenAmmoMenu();
    else if(BoxName==TEXT("Ammo0"))PC->InspectAmmo(0);
    else if(BoxName==TEXT("Ammo1"))PC->InspectAmmo(1);
    else if(BoxName==TEXT("Ammo2"))PC->InspectAmmo(2);
    else if(BoxName==TEXT("Ammo3"))PC->InspectAmmo(3);
    else if(BoxName==TEXT("AmmoAction"))PC->AmmoAction();
    else if(BoxName==TEXT("AmmoBuyYes"))PC->ConfirmAmmoPurchase(true);
    else if(BoxName==TEXT("AmmoBuyNo"))PC->ConfirmAmmoPurchase(false);
    else if (BoxName == TEXT("TerminalStats")) PC->SetTerminalWeaponPage(false);
	else if (BoxName == TEXT("TerminalWeapons")) PC->SetTerminalWeaponPage(true);
	else if (BoxName == TEXT("Weapon0")) PC->InspectWeapon(0);
	else if (BoxName == TEXT("Weapon1")) PC->InspectWeapon(1);
	else if (BoxName == TEXT("Weapon2")) PC->InspectWeapon(2);
	else if (BoxName == TEXT("Weapon3")) PC->InspectWeapon(3);
	else if (BoxName == TEXT("WeaponTipClose")) PC->CloseWeaponTip();
	else if (BoxName == TEXT("WeaponEquip")) PC->EquipInspectedWeapon();
	else if (BoxName == TEXT("ProfileSave")) PC->RetryProfileSave();
	else if (BoxName == TEXT("Upgrade0")) PC->SelectUpgrade(0);
	else if (BoxName == TEXT("Upgrade1")) PC->SelectUpgrade(1);
	else if (BoxName == TEXT("Upgrade2")) PC->SelectUpgrade(2);
	else if (BoxName == TEXT("Close")) PC->CloseUpgradeMenu();
	// 热区只传递意图；控制器检查当前确认请求并重新验证世界状态，旧帧重复点击无效。
	else if (BoxName == TEXT("NextLevelYes")) PC->ConfirmNextLevel();
	else if (BoxName == TEXT("NextLevelNo")) PC->CancelNextLevelConfirmation();
	else if (BoxName == TEXT("Restart")) PC->RestartPressed();
	else if (BoxName == TEXT("StartGame")) PC->StartGamePressed();
	else if (BoxName == TEXT("Easy")) PC->DifficultyPressed(EDemoDifficulty::Easy);
	else if (BoxName == TEXT("Normal")) PC->DifficultyPressed(EDemoDifficulty::Normal);
	else if (BoxName == TEXT("Hard")) PC->DifficultyPressed(EDemoDifficulty::Hard);
	else if (BoxName == TEXT("Hell")) PC->DifficultyPressed(EDemoDifficulty::Hell);
	else if (BoxName == TEXT("Endless")) PC->EndlessPressed(); // 专用模式入口仍经权威解锁校验。
	else if (BoxName == TEXT("LobbySettings")) PC->SettingsPressed();
	else if (BoxName == TEXT("QuitGame")) PC->QuitPressed();
	else UE_LOG(LogFPSDemo, Warning, TEXT("HUD click rejected: unknown action"));
}
