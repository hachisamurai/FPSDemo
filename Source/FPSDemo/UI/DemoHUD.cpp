// 职责：本地Canvas战斗HUD与尚未迁移的菜单。使用：DrawHUD读取控制器/GAS投影；CommonUI接管的页面跳过旧绘制/热区，命中表现不作为伤害判定。
#include "UI/DemoHUD.h"
#include "UI/Common/DemoUIManager.h"
#include "Player/DemoPlayerState.h"
#include "Player/Voice/DemoVoiceComponent.h" // 只读设备/实际音量，不在HUD开启麦克风。
#include "AI/DemoEnemyRoster.h" // 双Boss标题读取复制身份，不再把编织者显示为旧Warden。
#include "Weapons/Melee/DemoMeleeComponent.h" // 当前独立近战槽展示招式状态，避免把隐藏枪的弹药画成棒球棍资源。
#include "Debug/DemoEditorCheats.h" // 局内持续提示当前测试覆盖，编译包体不含查询或文案。
#include "UI/DemoUIStyle.h" // 所有页面共用灰阶透明底板，避免局部命名空间继续保留旧蓝底。
#include "UI/R02/DemoUITheme.h" // S1游戏HUD只读主题；旧Canvas菜单继续保留R04。
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "UI/DemoUIActions.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Economy/Gambling/DemoCasinoRange.h"
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


ADemoHUD::ADemoHUD()
{
	DEMO_LOG_CALL();
	static ConstructorHelpers::FObjectFinder<UDemoUITheme> R02ThemeAsset(TEXT("/Game/UI/RedesignR02/Theme/DA_R02CinemaTheme_S0R02.DA_R02CinemaTheme_S0R02")); // CDO强引用保证字体/Theme进入Cook依赖，不依赖外部路径。
	R02HUDTheme=R02ThemeAsset.Object;
	if(!R02HUDTheme) UE_LOG(LogFPSDemo,Warning,TEXT("R02 gameplay HUD theme missing; R04 fallback retained"));
	// R04独立资源目录保留旧背景以便回退；Editor命令DemoCreateR04Assets导入批准的原图，不读运行时外部PNG。
	static ConstructorHelpers::FObjectFinder<UTexture2D> Background(TEXT("/Game/UI/RedesignR04/Textures/T_LobbyBackground.T_LobbyBackground"));
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
	RuntimeFont=DemoUI::TextFontObject(); RuntimeDisplayFont=DemoUI::DisplayFontObject(); // CDO及本HUD持有已Cook字体，查询无逐帧路径加载。
	if(!RuntimeFont)
	{
		UE_LOG(LogFPSDemo,Warning,TEXT("R04 HUD text font missing; using engine composite fallback"));
		RuntimeFont=NewObject<UFont>(this,TEXT("BreachRuntimeFontFallback")); RuntimeFont->FontCacheType=EFontCacheType::Runtime; RuntimeFont->CompositeFont=*FCoreStyle::GetDefaultFont();
	}
	if(!RuntimeDisplayFont) RuntimeDisplayFont=RuntimeFont;
#if !UE_BUILD_SHIPPING
	// 测试只在显式参数启用，World 拥有测试 Actor；每次重开生成新实例，普通游玩不创建。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoUIValidation"))) GetWorld()->SpawnActor<ADemoUIValidation>();
#endif
}

void ADemoHUD::Label(const FString& Text, float X, float Y, float Size, FLinearColor Color, bool bCentered)
{
	DEMO_LOG_TICK();
	// 中文与混排使用Noto复合资产，纯英文/数字使用Barlow；复合字体均包含中文/符号回退。
	// Font/Item 只活到本次绘制；point 转设计像素后按视口比例栅格化，禁止拉伸离线字形。
	// UE5.4 的 CanvasTextItem::HasValidText 额外要求 UFont，即使 SlateFontInfo 已有复合字体。
	if (!RuntimeFont) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HUD font not initialized")); return; }
	bool bLatinOnly=true,bNumericOnly=!Text.IsEmpty(); // 仅纯ASCII读数用固定数字格，绝不压缩中文/玩家名。
	for(TCHAR Character:Text)
	{
		if(Character>127) bLatinOnly=false;
		if(!FChar::IsDigit(Character)&&!FChar::IsWhitespace(Character)&&Character!=TEXT('.')&&Character!=TEXT('/')&&Character!=TEXT(':')&&Character!=TEXT('%')&&Character!=TEXT('+')&&Character!=TEXT('-')&&Character!=TEXT('e')&&Character!=TEXT('E')) bNumericOnly=false;
	}
	const bool bR02=IsR02GameplayPresentation(); // DrawHUD已按本帧页型门禁缓存；不会把B08训练/旧Canvas菜单全局换皮。
	const int32 FontSize=FMath::Max(bR02?12:1,FMath::RoundToInt(Size*UIScale*.75f)); // R02小标签保留至少约16物理像素；设计像素转Slate point一次。
	FSlateFontInfo Font(bLatinOnly?RuntimeDisplayFont.Get():RuntimeFont.Get(),FontSize,bLatinOnly?TEXT("Medium"):TEXT("Regular"));
	if(bNumericOnly&&RuntimeDisplayFont==DemoUI::DisplayFontObject()) Font=DemoUI::NumberFont(FontSize); // 同一资产的纯数值副本，不更改共享字库/中文回退。
	if(bR02)
	{
		Font=bNumericOnly||Size>=26.f?R02HUDTheme->GetSerifFont(FontSize,TEXT("Regular"),!bLatinOnly):R02HUDTheme->GetLabelFont(FontSize,TEXT("Regular"),bLatinOnly);
		const FLinearColor Opaque=Color.CopyWithNewOpacity(1.f); // 保留原状态的Alpha，替换已有视觉语义Token，不以颜色反推业务状态。
		if(Opaque.Equals(DemoUI::Muted)) Color=R02HUDTheme->Muted.CopyWithNewOpacity(Color.A);
		else if(Opaque.Equals(DemoUI::Danger)) Color=R02HUDTheme->Ember.CopyWithNewOpacity(Color.A);
		else if(Opaque.Equals(DemoUI::Gold)||Opaque.Equals(DemoUI::Accent)||Opaque.Equals(DemoUI::Ink)||Opaque.Equals(FLinearColor::White)) Color=R02HUDTheme->Ink.CopyWithNewOpacity(Color.A);
		if(bNumericOnly&&FSlateApplication::IsInitialized())
		{
			const TSharedRef<FSlateFontMeasure> Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService(); // GT只借本次字体测量，不保存Slate资源到Actor。
			const float CellWidth=DemoUIR02::MeasureDigitCellWidth(Font); // 用真实0..9最大advance，非Barlow旧比例。
			float Width=0.f; // 当前字符串的物理像素宽；标点保持自身字宽，数字固定格。
			for(const TCHAR Character:Text) Width+=FChar::IsDigit(Character)?CellWidth:Measure->Measure(FString::Chr(Character),Font).X;
			float Cursor=X*UIScale-(bCentered?Width*.5f:0.f); // 左/居中锚点不随具体数字宽度抖动，位数变化仍按真实字符串长度变化。
			for(const TCHAR Character:Text)
			{
				const FString Glyph=FString::Chr(Character); // 本次单字形，不创建Texture或新UFont。
				const float Advance=FChar::IsDigit(Character)?CellWidth:Measure->Measure(Glyph,Font).X;
				FCanvasTextItem Digit(FVector2D(FMath::RoundToFloat(Cursor+Advance*.5f),FMath::RoundToFloat(Y*UIScale)),FText::FromString(Glyph),Font,Color);
				Digit.bCentreX=true; Digit.EnableShadow(R02HUDTheme->Base.CopyWithNewOpacity(.8f*Color.A),FVector2D(1.f)); Canvas->DrawItem(Digit); Cursor+=Advance;
			}
			return;
		}
	}
	FCanvasTextItem Item(FVector2D(X, Y) * UIScale, FText::FromString(Text), Font, Color);
	Item.bCentreX = bCentered;
	if(bR02) Item.EnableShadow(R02HUDTheme->Base.CopyWithNewOpacity(.8f*Color.A),FVector2D(1.f)); // 实时白模亮面可读性；阴影随原文字Alpha淡出，不加背景图/实心HUD卡。
	Canvas->DrawItem(Item);
}

bool ADemoHUD::IsR02GameplayPresentation() const
{ DEMO_LOG_TICK(); return bR02GameplayFrame&&R02HUDTheme!=nullptr; }

float ADemoHUD::FitR02NumeralSize(const FString& Text,float PreferredSize,float AvailableWidth) const
{
    DEMO_LOG_TICK();
    if(!R02HUDTheme||!FSlateApplication::IsInitialized()||Text.IsEmpty()) return PreferredSize;
    const int32 PointSize=FMath::Max(12,FMath::RoundToInt(PreferredSize*UIScale*.75f)); // 与Label当前物理字号口径一致，不读求解器/示例值。
    const FSlateFontInfo Font=R02HUDTheme->GetSerifFont(PointSize);
    const TSharedRef<FSlateFontMeasure> Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const float Cell=DemoUIR02::MeasureDigitCellWidth(Font); // 字形服务自身缓存度量，HUD不持跨旅行Slate对象。
    float Width=0.f; // 当前真实串物理宽；科学记数标点按自身宽度计。
    for(const TCHAR Character:Text) Width+=FChar::IsDigit(Character)?Cell:Measure->Measure(FString::Chr(Character),Font).X;
    return FMath::Max(16.f,PreferredSize*FMath::Min(1.f,FMath::Max(1.f,AvailableWidth)*UIScale/FMath::Max(1.f,Width)));
}

FLinearColor ADemoHUD::GetPanelSurface() const
{
    DEMO_LOG_TICK();
    // 保留旧配置字段名，避免已有HUD默认值丢失；所有页面在同一个入口钳制合法Alpha。
    return DemoUI::Surface.CopyWithNewOpacity(FMath::Clamp(LobbyPanelOpacity, .15f, .95f));
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
	// Fill为共享灰阶状态色；调用方Opacity乘主题Alpha，不能以默认1覆盖半透明。文字单独保持不透明。
	const FLinearColor Fill = !bEnabled ? DemoUI::Disabled : bQuiet ? (bHover ? DemoUI::Hovered : DemoUI::Raised) : (bHover ? DemoUI::SelectedHovered : DemoUI::Selected);
	Panel(X, Y, W, H, Fill.CopyWithNewOpacity(Fill.A * FMath::Clamp(Opacity, 0.f, 1.f)));
	Label(Text, X + W / 2.f, Y + H / 2.f - 9.f, 16.f, !bEnabled ? DemoUI::Muted : DemoUI::Ink, true);
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
	bR02GameplayFrame=false; // 每帧从关闭开始，提前返回/旅行前数据未就绪不沿上帧视觉状态。
	if (!Canvas || !PlayerOwner) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HUD waiting for viewport/owner")); return; }
	UIScale = FMath::Max(0.01f, FMath::Min(Canvas->ClipX / 1280.f, Canvas->ClipY / 720.f));
	ViewWidth = Canvas->ClipX / UIScale;
	ViewHeight = Canvas->ClipY / UIScale;
	// 本帧借用所有展示对象；不跨重生/OpenLevel 缓存 UObject 裸指针。
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(PlayerOwner);
    if(PC) if(const auto* UI=PC->FindComponentByClass<UDemoUIManager>()) if(UI->UsesWidgetMenu())
    {
        const auto* UIState=GetWorld()->GetGameState<ADemoGameState>(); // 用户已明确无背景图；大厅底色与交互文字由当前UMG页承担。
        if(UIState&&UIState->Phase==EDemoPhase::Lobby) DrawLobbyBackground();
        return;
    }
	ADemoCharacter* Player = Cast<ADemoCharacter>(PlayerOwner->GetPawn());
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	// 顶层页独占热区；合作菜单不暂停World，但Canvas持续绘制。
	if (PC && PC->HasBlockingOverlay()) { DrawSessionMenu(PC, State); return; }
	// 大厅不依赖 Avatar 属性就绪，不显示准星/武器/白模场景。
	if (PC && State && State->Phase == EDemoPhase::Lobby) { DrawLobby(PC, State); return; }
	const UDemoAttributeSet* Attributes = Player ? Player->GetDemoAttributes() : nullptr;
	if (!PC || !State || !Attributes)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HUD waiting for gameplay data"));
		Panel(ViewWidth*.5f-260,ViewHeight*.5f-64,520,128,DemoUI::Surface.CopyWithNewOpacity(.86f),2);
		Label(TEXT("PREPARING OPERATION"),ViewWidth*.5f,ViewHeight*.5f-40,16,DemoUI::Gold,true);
		Label(TEXT("正在初始化作战系统…"),ViewWidth*.5f,ViewHeight*.5f-3,24,DemoUI::Ink,true);
		DrawSystemStatus(PC);
		return;
	}
	// 模态确认与升级共用遮罩；先压暗世界再绘制状态和面板，按钮不会与场景导航重叠。
	if (PC->IsUpgradeMenuOpen() || PC->IsNextLevelConfirmationOpen() || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat)
		Panel(0, 0, ViewWidth, ViewHeight, DemoUI::Overlay, 0.f);
	if(State->Phase==EDemoPhase::Victory||State->Phase==EDemoPhase::Defeat)
	{
		DrawEndScreen(State); // 战报是独占信息层；透明板后不再绘制生命/技能/钱包文字，避免双层阅读与热区混杂。
		return;
	}
	const bool bCasinoPage=State->Phase==EDemoPhase::Hub&&PC->GetMenuFlow()->IsGambleMenuOpen()&&DemoCasino::ModeForChoice(PC->GetMenuFlow()->GetInspectedGamble())>=0; // B08旧娱乐页面不纳入S1。
	const bool bRangeActive=ADemoCasinoRange::Find(Player)!=nullptr; // 原训练区域继续自己的信息布局，不因共享Label改版。
	bR02GameplayFrame=!bCasinoPage&&!bRangeActive&&!PC->IsUpgradeMenuOpen()&&!PC->IsNextLevelConfirmationOpen();
	// Scope来自装备组件的真实状态，先盖场景再绘制可读的边缘HUD。
	if (!Player->IsCoopEliminated()&&Player->GetWeaponComponent()->GetScopeLevel() > 0) DrawScopeOverlay();
    UpdateAbilityFeedback(State, Player, Attributes); // 先于边缘绘制观测技能/核心/天赋边沿，不修改任何玩法真值。
    DrawPlayerDamageFeedback(PC,true); // 外缘先于信息层绘制；不把受击闪光叠到生命/钱包文字上。
    if(!bCasinoPage&&!bRangeActive) DrawAbilityEdgeFeedback(State, Player, Attributes); // 冲刺/治疗柔和边缘脉冲，仅本地表现。
	DrawSpectatorStatus(PC); // 本人阵亡只读真实观战目标，沿原清关复活规则；普通存活时内部直接返回。
	// Combat才有存活敌人；投影UI在瞄准镜内绘制且先于边缘HUD，模态页不泄露背后的敌人血条。
	if (State->Phase == EDemoPhase::Combat && !PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen())
    {
        DrawEnemyStatusBars();
        DrawObjectiveStatusBars(); // 与敌人标签共用模态门禁；每个本地HUD投影，不存在共享文字朝向。
    }
	if(!bCasinoPage&&!bRangeActive) DrawStatus(State, Player, Attributes);
    if(!bCasinoPage&&!bRangeActive) DrawPlayerDamageFeedback(PC,false); // ASC真值条先画，残影仅填已损失段，方向提示独立于枪械命中反馈。
    DrawVoiceStatus(PC);
    if(!PC->IsUpgradeMenuOpen()) DrawCasinoRangeOverlay(); // 打开终端时使用对应结算页，避免两个面板叠加。
	// 清关当帧仍显示已确认的最后一杀；大厅/模态暂停在上方返回，死亡观战不能显示队友回执。
	if (!Player->IsCoopEliminated() && !PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen()) DrawHitFeedback(Player);
	if(!bRangeActive&&!PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen()) DrawCampaignStatus(State,PC);
    if (PC->IsUpgradeMenuOpen()) DrawUpgradeMenu(PC, State, Player, Attributes);
	if (PC->IsNextLevelConfirmationOpen()) DrawNextLevelConfirmation(State);
	DrawSystemStatus(PC); // 回执与保存标签保持在独立边缘，菜单Root使用同源短通知。
#if WITH_EDITOR
    const FString TestStatus=DemoEditorCheats::Status(this); // 当前GI只读测试开关，不持久化、不混入真实钱包数值。
    if(!TestStatus.IsEmpty()) Label(TestStatus,ViewWidth*.5f,ViewHeight-44,14,DemoUI::Gold,true);
#endif
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

void ADemoHUD::DrawUpgradeMenu(ADemoPlayerController* PC, const ADemoGameState* State, ADemoCharacter* Player, const UDemoAttributeSet* Attributes)
{
	DEMO_LOG_TICK();
	if(PC->GetMenuFlow()->IsCoreMenuOpen()) { DrawCoreMenu(PC); return; }
    if(PC->GetMenuFlow()->IsGambleMenuOpen()) { DrawGambleMenu(PC,State); return; } // 研究页沿用终端生命周期。
	if (PC->IsAmmoMenuOpen()) { DrawAmmoMenu(PC, Player); return; } // 弹药页与武器页互斥，奖励页不出现此入口。
	if (PC->IsWeaponMenuOpen()) { DrawWeaponMenu(PC, Player); return; } // 同一终端输入锁内切换，奖励页不可进入武器库。
	// 设计矩形以视口中心定位；缩放只由 DrawHUD 决定，正文与 HitBox 始终同源。
	const float X = ViewWidth / 2.f - 480.f;
	const float Y = ViewHeight / 2.f - 240.f;
	const bool bReward = PC->IsRewardMenu(); // 免费奖励与商店共享生命周期，卡片/行式布局不同。
	Panel(X, Y, 960, 480, GetPanelSurface(), 12); // 主容器只绘一次，避免旧描边整块底板叠加Alpha使其近乎不透明。
	if (!bReward) // 页签置于右上空白区，保留原商店交易热区与奖励卡片布局。
	{
		if(GetWorld()->GetGameState<ADemoGameState>()->Phase==EDemoPhase::Hub) Button(TEXT("TerminalCore"),TEXT("流派核心"),X+680,Y+70,118,30,true,true);
        Button(TEXT("TerminalGamble"),TEXT("冒险 / 娱乐"),X+810,Y+70,118,30,true,true); // 非奖励页专属入口。
        Button(TEXT("TerminalStats"), TEXT("属性升级"), X + 430, Y + 25, 120, 36, true, true);
		Button(TEXT("TerminalWeapons"), TEXT("武器"), X + 560, Y + 25, 100, 36, true, true);
        if(State->Phase==EDemoPhase::Hub)Button(TEXT("TerminalAmmo"),TEXT("弹药类型"),X+670,Y+25,130,36,true,true); // 关间终端不允许换弹药。
	}
	Label(bReward ? TEXT("SECTOR CLEAR / 关卡完成") : TEXT("FIELD SYSTEMS / 备战终端"), X + 32, Y + 25, 13, DemoUI::Accent);
	Label(bReward ? (GetPlayerView().bBranchReward?TEXT("选择本轮流派分支"):TEXT("选择一项能力强化")) : TEXT("升级终端"), X + 32, Y + 51, 32, DemoUI::Ink);
	Label(bReward ? (GetPlayerView().bBranchReward?TEXT("核心二选一 · 替代本关免费强化 · 整轮结束清除"):TEXT("免费三选一 · 本关仅可领取一次 · 选择后前往本关中心终端")) : State->Phase == EDemoPhase::Hub ? TEXT("金币升级永久保留；仅所购属性的金币价格上涨。") : TEXT("银币升级仅本轮有效；仅所购属性的银币价格上涨。"), X + 32, Y + 98, 16, DemoUI::Muted);
	if (bReward)
	{
        const TArray<FDemoShopOfferSnapshot>& Offers=GetPlayerView().RewardOffers; // 权威投影未抵达时不创建可点击的虚假默认奖励。
		for (int32 Index = 0; Index < FMath::Min(3,Offers.Num()); ++Index) // 当前协议三种策略；按已到达数据绘制。
		{
            const FDemoShopOfferSnapshot& Offer=Offers[Index]; // 本帧借用标题/真实增量/可领状态。
			const float CardX = X + 32 + Index * 302.f; // 三卡等宽，间距 16。
			const bool bHover = IsHovered(CardX, Y + 145, 286, 228); // 整卡可点，非仅底部文字。
			Panel(CardX, Y + 145, 286, 228, bHover ? DemoUI::Hovered : DemoUI::Raised);
			Panel(CardX + 18, Y + 165, 40, 36, GetPanelSurface());
			Label(FString::FromInt(Index + 1), CardX + 38, Y + 173, 19, DemoUI::Accent, true);
			Label(TEXT("免费"), CardX + 228, Y + 176, 13, DemoUI::Accent);
			Label(Offer.Title, CardX + 18, Y + 220, 23, DemoUI::Ink);
			if(GetPlayerView().bBranchReward)
                for(int32 Line=0;Line<FMath::Min(4,FMath::DivideAndRoundUp(Offer.Detail.Len(),17));++Line) // 分支没有属性0→0；长规则按有界卡片分行显示。
                    Label(Offer.Detail.Mid(Line*17,17),CardX+18,Y+257+Line*19,14,DemoUI::Muted);
            else
            {
                Label(FString::Printf(TEXT("%g  →  %g"), Offer.CurrentValue, Offer.CurrentValue + Offer.Gain), CardX + 18, Y + 260, 24, DemoUI::Accent);
                Label(Offer.Detail, CardX + 18, Y + 302, 14, DemoUI::Muted);
            }
			Label(TEXT("选择此强化  →"), CardX + 18, Y + 343, 15, DemoUI::Ink);
			if (Offer.BlockReason.IsEmpty()) AddHitBox(FVector2D(CardX, Y + 145) * UIScale, FVector2D(286, 228) * UIScale, FName(*FString::Printf(TEXT("Upgrade%d"), Index)), true); // 等待权威投影时无可操作热区。
		}
		Label((GetPlayerView().bBranchReward?TEXT("点击卡片，或按 1 / 2 选择；本次不消耗货币"):TEXT("点击卡片，或按 1 / 2 / 3 选择；本次不消耗货币")), X + 32, Y + 415, 15, DemoUI::Muted);
		if (!PC->GetMenuMessage().IsEmpty()) Label(PC->GetMenuMessage(), X + 32, Y + 445, 14, DemoUI::Gold);
		return;
	}
	// 商店只读取服务器生成的商品行；Canvas每帧绘制不重复执行交易规则。
	const FDemoPlayerMenuSnapshot& View=GetPlayerView(); // 当前本人业务投影，生命周期属于PC。
	const bool bGold = State->Phase == EDemoPhase::Hub; // 与权威购买接口相同的区域判定，只展示不决定交易权限。
	const TCHAR* Currency = bGold ? TEXT("金币") : TEXT("银币"); // 本帧静态文案借用，禁止自动用另一钱包补差额。
	const int32 CurrencyPurchases = View.Progress.UpgradeProgress.Total(bGold); // 只展示总购买数，不作为各项价格。
	const float Values[] = { Attributes->GetWeaponDamageBonus(), Attributes->GetMaxHealth(), Attributes->GetMagazineBonus() }; // GAS全局伤害/容量加成与生命上限，避免把不同枪的基础值混为一谈。
	Button(TEXT("Close"), TEXT("×  关闭"), X + 822, Y + 25, 106, 36, true, true);
	for (int32 Index = 0; Index < FMath::Min(3,View.UpgradeOffers.Num()); ++Index) // 只绘制实际收到的商品，不使用本机硬编码默认值。
	{
        const FDemoShopOfferSnapshot& Offer=View.UpgradeOffers[Index]; // 与服务器结算使用同一条定义。
		const int32 Cost = View.UpgradeOffers.IsValidIndex(Index)?View.UpgradeOffers[Index].Cost:0; // 权威报价尚未抵达时禁用按钮。
		const bool bCanBuy = View.UpgradeOffers.IsValidIndex(Index)&&View.UpgradeOffers[Index].BlockReason.IsEmpty(); // 只读服务器逐项交易投影。
		const float RowY = Y + 143 + Index * 84.f; // 左侧属性行起点，右侧为摘要。
		Panel(X + 32, RowY + 8, 40, 40, DemoUI::Raised);
		Label(FString::Printf(TEXT("%02d"), Index + 1), X + 52, RowY + 18, 16, DemoUI::Accent, true);
		Label(Offer.Title, X + 88, RowY, 18, DemoUI::Ink);
		Label(FString::Printf(TEXT("%g  →  %g"), Offer.CurrentValue, Offer.CurrentValue + Offer.Gain), X + 228, RowY, 18, DemoUI::Ink);
		Label(Offer.Detail, X + 88, RowY + 35, 14, DemoUI::Muted);
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
	Label(TEXT("每项单独计价"), X + 710, Y + 340, 13, DemoUI::Muted); // 涨价幅度已配置化，不再显示写死的10。
	Label(PC->GetMenuMessage().IsEmpty() ? TEXT("各项独立定价 · 灰色按钮表示当前不可购买") : PC->GetMenuMessage(), X + 32, Y + 400, 15, DemoUI::Accent);
	Panel(X + 32, Y + 432, 896, 1, DemoUI::Edge, 0);
	Label(bGold ? TEXT("金币成长与各项价格永久保留") : TEXT("银币成长与各项价格在挑战结束后重置"), X + 32, Y + 447, 13, DemoUI::Muted);
	Label(TEXT("1 / 2 / 3 购买      TAB / E 关闭"), X + 654, Y + 447, 13, DemoUI::Muted);
}

void ADemoHUD::DrawNextLevelConfirmation(const ADemoGameState* State)
{
	DEMO_LOG_TICK();
    if(State->TeamReady.bRequired) { DrawTeamReadyMenu(State); return; }
    if (State->Phase == EDemoPhase::Hub)
    {
        // 首次出发直接选择并锁定难度；没有默认“是”按钮绕过选择。
        const float X = ViewWidth*.5f-350.f, Y = ViewHeight*.5f-235.f; // 独立难度模态布局。
        Panel(X,Y,700,470,GetPanelSurface(),12);
        Label(TEXT("选择本局难度"),X+32,Y+34,30,DemoUI::Ink);
        Label(TEXT("开始战斗后锁定，影响敌人生命、伤害和攻击频率"),X+32,Y+92,17,DemoUI::Muted);
        Button(TEXT("Easy"),TEXT("简单"),X+32,Y+154,196,65);
        Button(TEXT("Normal"),TEXT("普通"),X+252,Y+154,196,65);
        Button(TEXT("Hard"),TEXT("困难"),X+472,Y+154,196,65);
        const FDemoPlayerProgressSnapshot& Progress=GetPlayerView().Progress; // 本人服务器解锁投影，不能借用房主GI。
        Button(TEXT("Hell"),Progress.bHellUnlocked?TEXT("地狱"):TEXT("地狱 · 未解锁"),X+32,Y+245,306,58,Progress.bHellUnlocked);
        Button(TEXT("Endless"),Progress.bEndlessUnlocked?TEXT("无尽模式"):TEXT("无尽 · 未解锁"),X+362,Y+245,306,58,Progress.bEndlessUnlocked);
        Label(TEXT("地狱：整轮只用手枪开火通关困难（允许冲刺/治疗）"),X+32,Y+321,14,DemoUI::Muted);
        Label(TEXT("无尽：通关地狱；每10关出现Boss，成长与掉落逐关提升"),X+32,Y+348,14,DemoUI::Muted);
        if(const ADemoPlayerController* LocalPC=Cast<ADemoPlayerController>(GetOwningPlayerController())) Label(LocalPC->GetMenuMessage().Left(44),X+32,Y+378,13,DemoUI::Gold); // 配置/核心/保存拒绝必须可见。
        Button(TEXT("NextLevelNo"),TEXT("取消出发"),X+472,Y+400,196,44,true,true);
        return;
    }

	const float X = ViewWidth / 2.f - 280.f; // 确认框左边界，设计像素，按现有 UIScale 适配视口。
	const float Y = ViewHeight / 2.f - 150.f; // 确认框上边界；不覆盖底部生命/技能信息。
	Panel(X, Y, 560, 300, GetPanelSurface(), 12);
	Label(TEXT("准备出发"), X + 32, Y + 28, 15, DemoUI::Accent);
	Label(TEXT("是否进入下一关？"), X + 280, Y + 72, 30, DemoUI::Ink, true);
	Label(State->bEndless ? FString::Printf(TEXT("即将进入无尽第 %d 关"),State->LevelNumber+1) : FString::Printf(TEXT("即将进入第 %02d / %02d 关"), State->LevelNumber + 1, DemoCombatConfig::LevelCount), X + 280, Y + 121, 18, DemoUI::Gold, true);
	Label(TEXT("选择“否”可留在当前区域继续准备。"), X + 280, Y + 157, 15, DemoUI::Muted, true);
    if(const ADemoPlayerController* LocalPC=Cast<ADemoPlayerController>(GetOwningPlayerController())) Label(LocalPC->GetMenuMessage().Left(32),X+280,Y+184,12,DemoUI::Gold,true); // 单人同样显示出发失败原因。
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
    const FString Reason = GetPlayerView().TerminalBlockReason; // 已批准交互的服务器快照，点击仍重新校验。
    // 用户已删除二级武器详情层；Canvas回退始终绘制目录，明确卡片点击是装配，不借旧InspectedWeapon状态注册隐藏热区。
    Panel(X, Y, 960, 480, GetPanelSurface(), 12);
    Label(TEXT("FIELD ARMORY / 终端武器库"), X + 32, Y + 25, 13, DemoUI::Accent);
    Label(TEXT("武器"), X + 32, Y + 53, 30, DemoUI::Ink);
    Label(TEXT("点击已解锁主武器卡片装配 · 手枪固定保留"), X + 32, Y + 98, 15, DemoUI::Muted);
    Button(TEXT("TerminalStats"), TEXT("属性升级"), X + 430, Y + 25, 120, 36, true, true);
    Button(TEXT("TerminalWeapons"), TEXT("武器"), X + 560, Y + 25, 100, 36, true);
    if(GetWorld()->GetGameState<ADemoGameState>()->Phase==EDemoPhase::Hub) Button(TEXT("TerminalCore"),TEXT("流派核心"),X+680,Y+70,118,30,true,true);
    Button(TEXT("TerminalGamble"),TEXT("冒险 / 娱乐"),X+810,Y+70,118,30,true,true); // 原终端页签，实际权限仍由Flow/服务重核。
    if(GetWorld()->GetGameState<ADemoGameState>()->Phase==EDemoPhase::Hub) Button(TEXT("TerminalAmmo"),TEXT("弹药类型"),X+670,Y+25,130,36,true,true);
    Button(TEXT("Close"), TEXT("×  关闭"), X + 822, Y + 25, 106, 36, true, true);
    for (int32 Index = 0; Index < 4; ++Index) // 固定四列；手枪和未解锁项只展示说明，不发装配意图。
    {
        const float CardX = X + 32 + Index * 226.f; // 卡片宽212，间距14设计像素。
        const bool bUnlocked = GetPlayerView().Progress.UnlockedWeaponIds.Contains(DemoWeaponCatalog::IdAt(Index)); // 本人权威权限投影，不能由点击授予解锁。
        const bool bEquipped = Index == 0 || GetPlayerView().PrimaryWeaponId == DemoWeaponCatalog::IdAt(Index); // 手枪固定副槽；主槽事实来自真实拥有者快照。
        const ADemoWeaponBase* Definition = Equipment->GetCatalogWeapon(Index); // CDO蓝图属性，不创建锁定武器实例。
        const bool bCanEquip = Index>0&&bUnlocked&&Definition&&Reason.IsEmpty(); // 仅注册明确装配热区；服务器继续核验终端会话、权限和保存。
        const FLinearColor ItemInk = bUnlocked ? DemoUI::Ink : FLinearColor(FColor(143,143,143)); // 锁定Item整体使用中性灰色。
        Panel(CardX, Y + 137, 212, 226, bUnlocked ? DemoUI::Raised : DemoUI::Disabled);
        DrawWeaponIcon(Index, CardX + 26, Y + 147, ItemInk, bUnlocked);
        Label(Definition ? Definition->Config.DisplayName.ToString() : TEXT("资源未就绪"), CardX + 16, Y + 230, 23, ItemInk);
        const TCHAR* EquipState=!bUnlocked?TEXT("未购买 · 请到枪墙购买"):Index==0?TEXT("固定副武器 [2]"):bEquipped?TEXT("已装配 · 可重新装配"):TEXT("点击装配为主武器 [1]"); // 同一行明确卡片职责，避免把预览点击伪装成装配。
        Label(EquipState,CardX+16,Y+263,12,bUnlocked?DemoUI::Accent:ItemInk);
        if (Definition)
        {
            Label(FString::Printf(TEXT("基础伤害  %.0f × %d"), Definition->Config.BaseDamage, Definition->GetPelletCount()), CardX + 16, Y + 292, 14, ItemInk);
            Label(FString::Printf(TEXT("射速 %.0f RPM   弹匣 %d"), Definition->Config.RoundsPerMinute, Definition->Config.MagazineCapacity), CardX + 16, Y + 317, 13, ItemInk);
            Label(FString::Printf(TEXT("装填 %.1fs"), Definition->Config.ReloadSeconds), CardX + 16, Y + 339, 12, ItemInk);
        }
        if(bCanEquip) AddHitBox(FVector2D(CardX,Y+137)*UIScale,FVector2D(212,226)*UIScale,FName(*FString::Printf(TEXT("Weapon%d"),Index)),true); // 原WeaponN白名单现在直接转原EquipWeapon请求，无详情层。
    }
    Label(!PC->GetMenuMessage().IsEmpty()?PC->GetMenuMessage():Reason.IsEmpty()?TEXT("购买不会自动装配；终端装配不消耗金币。"):Reason,X+32,Y+385,14,Reason.IsEmpty()?DemoUI::Muted:DemoUI::Gold); // 保留真实回执/拒绝，不用本地点击推断成功。
    Label(Profile ? Profile->GetStatus() : TEXT("存档系统未就绪"), X + 32, Y + 406, 13, DemoUI::Muted);
    const UDemoCloudSync* Cloud = GetGameInstance()->GetSubsystem<UDemoCloudSync>(); // 本帧只读云状态，绘制不执行联网。
    if (Cloud) Label(Cloud->GetStatus(), X + 32, Y + 428, 12, DemoUI::Muted);
    Label(TEXT("展示蓝图基础参数；本局伤害与弹匣升级另行叠加"), X + 32, Y + 450, 12, DemoUI::Muted);
    Button(TEXT("ProfileSave"), TEXT("保存重试"), X + 800, Y + 415, 128, 36, true, true);
}

void ADemoHUD::DrawLobbyBackground()
{
	DEMO_LOG_TICK();
	if(!Canvas) return;
	// C02已获实施授权且用户要求无背景图；正式UMG与Legacy回退都只铺暖黑，不复活旧背景或改变关卡光照。
	DrawRect(R02HUDTheme?R02HUDTheme->Base:FLinearColor(FColor(13,12,11)),0,0,Canvas->ClipX,Canvas->ClipY);
}

void ADemoHUD::LobbyButton(FName Name, const FString& Text, float X, float Y, float W, float H, bool bSelected)
{
	DEMO_LOG_TICK();
	// 大厅与局内按钮共用灰阶主题；保留原悬停/选中亮度与独立热区。
	const bool bHover = IsHovered(X, Y, W, H); // 当前帧鼠标是否位于真实点击矩形内。
	const FLinearColor Fill = bSelected ? (bHover ? DemoUI::SelectedHovered : DemoUI::Selected) : (bHover ? DemoUI::Hovered : DemoUI::Raised); // 本帧状态色，主题值同时包含灰度与Alpha。
	Panel(X, Y, W, H, Fill);
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
	// 大厅与全部主UI共用同一底色/不透明度；标题、按钮和文字各自绘制。
	const FLinearColor LobbySurface = GetPanelSurface(); // 本帧共享主面板颜色，默认45%不透明。
	Panel(X, Y, 404, bHasError ? 536 : 472, LobbySurface, 12);
	Panel(X + 24, Y + 28, 3, 34, DemoUI::Accent, 0);
	Label(TEXT("B R E A C H"), X + 40, Y + 22, 38, DemoUI::Ink);
	Label(TEXT("十关突破  /  单人 · 合作"), X + 24, Y + 79, 16, DemoUI::Accent); // 合作入口仍使用同一套战役和存档规则。
	Panel(X + 24, Y + 112, 356, 1, DemoUI::Muted.CopyWithNewOpacity(0.25f), 0);
    // 难度改为安全区出发终端选择，大厅只提供存档入口和全局设置。
    Label(TEXT("从存档继续你的十关行动"),X+24,Y+151,21,DemoUI::Ink);
    Label(TEXT("新存档从安全区开始。"),X+24,Y+201,16,DemoUI::Muted);
    Label(TEXT("与出发终端交互时选择本局难度。"),X+24,Y+234,15,DemoUI::Muted);
	LobbyButton(TEXT("StartGame"), TEXT("[ENTER]  开始游戏"), X + 24, Y + 281, 356, 52);
    // 多人按钮进入自动搜索页，压缩原说明留白以保持面板和错误区域不重叠。
	LobbyButton(TEXT("Multiplayer"), TEXT("多人游戏 · 局域网"), X + 24, Y + 343, 356, 44);
	LobbyButton(TEXT("LobbySettings"), TEXT("设置"), X + 24, Y + 397, 172, 36);
	LobbyButton(TEXT("QuitGame"), TEXT("退出游戏"), X + 208, Y + 397, 172, 36);
	Label(TEXT("每5关三种小怪，每10关迎战 Boss"), X + 24, Y + 445, 13, DemoUI::Muted);
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
    DEMO_LOG_CALL(); Super::NotifyHitBoxClick(BoxName);
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(PlayerOwner); // 当前本地输入拥有者，不缓存World引用。
    if(!PC)return;
    if(auto* UI=PC->FindComponentByClass<UDemoUIManager>()) if(UI->UsesWidgetMenu())
    {
#if WITH_EDITOR
        UI->PressActionForTest(BoxName); // 旧自动化入口兼容真实当前Widget按钮；不存在/被模态遮挡的热区不能绕过。
#endif
        return; // 非Editor旧Canvas事件无效，实际输入走CommonUI按钮。
    }
    if(PC->GetMenuFlow()->IsCasinoBetInputOpen()) return; // Slate输入层拥有当前输入，拒绝旧Canvas热区穿透。
    if(HandleSessionClick(BoxName))return; // 模态页消费所有旧下层热区。
    if(PC->GetMenuFlow()->HandleCasinoClick(BoxName))return; // 专属有界动态棋盘热区，模态仍由上方统一阻断。
    PC->GetMenuFlow()->DispatchUIAction(DemoUIActions::Decode(BoxName));
}

const FDemoPlayerMenuSnapshot& ADemoHUD::GetPlayerView() const
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* PC=Cast<ADemoPlayerController>(PlayerOwner); // 本HUD只读拥有者，不遍历其他玩家经济。
    static const FDemoPlayerMenuSnapshot Empty; // 启动/旅行的安全只读空模型，不包含共享可变玩家状态。
    return PC?PC->GetMenuSnapshot():Empty;
}
