#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DemoHUD.generated.h"

class ADemoCharacter;
class ADemoPlayerController;
class ADemoGameState;
class ADemoEnemy;
class UDemoAttributeSet;
class UDemoWeaponComponent;
struct FStreamableHandle;

/** BREACH 战术终端风格原生 HUD；只读 GAS/流程数据，交互继续经过 Controller/GameMode。 */
UCLASS()
class FPSDEMO_API ADemoHUD : public AHUD
{
	GENERATED_BODY()
public:
	/** 硬引用项目背景纹理，保证 Cooker 收集；不依赖用户电脑上的原始下载路径。 */
	ADemoHUD();
	/** 创建由 HUD 强持有的运行时复合字体，确保 Canvas 能绘制中文；随 HUD 一起回收。 */
	virtual void BeginPlay() override;
	/** EndPlayReason为HUD/World卸载原因；取消图标异步委托并释放本地强引用缓存。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Equipment为本次借用目录；打开武器页时按CDO的Icon软引用异步加载，绘制阶段不读盘。 */
	void PrepareWeaponIcons(const UDemoWeaponComponent* Equipment);
	/** Index为目录0..3；只返回本HUD缓存的纹理，未配置/加载中/失败返回nullptr，调用者不得长期持有裸指针。 */
	const class UTexture2D* GetWeaponIcon(int32 Index) const;
	/** 每帧绘制边缘 HUD 与当前阶段面板；1280x720 设计坐标等比缩放，支持非 16:9 视口。 */
	virtual void DrawHUD() override;
	/** Enemy为当前World借用敌人，OutPixelAnchor输出头顶视口像素；死亡/隐藏/背后/遮挡/超距返回false，不改变Actor。 */
	bool GetEnemyStatusScreenAnchor(const ADemoEnemy* Enemy, FVector2D& OutPixelAnchor) const;
	/** BoxName 为注册的操作名称；输入转交控制器，由服务器再次验证，绝不直接修改属性。 */
	virtual void NotifyHitBoxClick(FName BoxName) override;
private:
	/** 每帧在世界场景上层绘制水平血条及GAS状态图标；不生成点击热区，不复制或写存档。 */
	void DrawEnemyStatusBars();
	/** Icon为HUD持有贴图，X/Y是设计像素左上角；等比放入32px方框，缺资源时显示Fallback文字。 */
	void DrawEnemyDebuffIcon(const class UTexture2D* Icon, float X, float Y, const TCHAR* Fallback);
	// 两张小型常驻状态纹理由HUD CDO强引用，启动加载且Cook可追踪；只影响本地表现，允许在HUD默认值替换。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Enemy UI") TObjectPtr<class UTexture2D> EnemyFireIcon;
	UPROPERTY(EditDefaultsOnly, Category="Demo|Enemy UI") TObjectPtr<class UTexture2D> EnemyIceIcon;
	// 本地头顶UI最大可见距离，厘米；限制遍历中射线检测范围，避免显示远处其他竞技场敌人。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Enemy UI", meta=(ClampMin="100", ClampMax="20000")) float EnemyStatusMaxDistance = 6000.f;
	/** StreamableManager游戏线程完成回调；弱UObject委托不会延长HUD生命周期，先强引用成功纹理再释放Handle。 */
	void OnWeaponIconsLoaded();
	// 当前目录Icon路径快照，不持有纹理；重开武器页读取新配置，取消旧请求后才能替换。
	TArray<FSoftObjectPath> WeaponIconPaths;
	// HUD拥有的纹理强引用，仅本地展示，不复制或写入玩家存档；换World释放。
	UPROPERTY(Transient) TArray<TObjectPtr<class UTexture2D>> WeaponIcons;
	// 本次批量异步请求保活未完成资源；EndPlay/新请求取消，委托仅在游戏线程读取上述缓存。
	TSharedPtr<FStreamableHandle> WeaponIconLoadHandle;
	/** PC/State 为本帧借用对象；背景图覆盖场景，半透明大厅面板锚定左侧偏上。 */
	void DrawLobby(ADemoPlayerController* PC, const ADemoGameState* State);
	/** PC/State为当前帧借用对象；只绘制顶层模态页，避免下层按钮仍注册点击区域。 */
	void DrawSessionMenu(ADemoPlayerController* PC, const ADemoGameState* State);
	/** Name为HitBox路由；有顶层页时消费所有点击，防止旧帧点击穿透。 */
	bool HandleSessionClick(FName Name);
	/** 背景按 cover 规则居中裁切 UV，保持原图比例；资源无效时回退为纯色而不暴露白模。 */
	void DrawLobbyBackground();
	/** Name/Text 为操作路由和文案，X/Y/W/H 为设计坐标；bSelected 只表示当前难度，灰色半透明样式独立于局内按钮。 */
	void LobbyButton(FName Name, const FString& Text, float X, float Y, float W, float H, bool bSelected = false);
	// 背景资产由 HUD 类默认值强引用，局内不绘制；运行时不读取外部 PNG，不复制。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Lobby") TObjectPtr<class UTexture2D> LobbyBackground;
	// 大厅中性灰面板不透明度 [0.15,0.95]；默认 0.45 让背景明显透出，文字单独保持不透明。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Lobby", meta=(ClampMin="0.15", ClampMax="0.95")) float LobbyPanelOpacity = 0.45f;
	// 面板中心相对视口高度的比例；0.43 即中间偏上，运行时钳制到安全边距。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Lobby", meta=(ClampMin="0.3", ClampMax="0.7")) float LobbyAnchorY = 0.43f;
	// 距视口左侧的设计像素，随 UIScale 等比缩放；非复制，本地显示配置。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Lobby", meta=(ClampMin="16", ClampMax="160")) float LobbyLeftMargin = 44.f;
	// CanvasTextItem 要求非空 UFont；以 transient 对象承载引擎复合字体，避免新增二进制字体资产。
	UPROPERTY(Transient) TObjectPtr<class UFont> RuntimeFont;
	/** Text 为中文文案；X/Y 为设计坐标，Size 为设计像素字号，Color 含透明度，bCentered 指定水平居中。 */
	void Label(const FString& Text, float X, float Y, float Size, FLinearColor Color, bool bCentered = false);
	/** X/Y/W/H 为设计矩形；Color 为面板颜色；Radius 是设计像素圆角，0 为直角。 */
	void Panel(float X, float Y, float W, float H, FLinearColor Color, float Radius = 8.f);
	/** Name 为路由，Text 为文案，X/Y/W/H 为设计矩形；禁用不注册热区，bQuiet 使用次要样式，Opacity 仅影响按钮底色[0,1]。 */
	void Button(FName Name, const FString& Text, float X, float Y, float W, float H, bool bEnabled = true, bool bQuiet = false, float Opacity = 1.f);
	/** X/Y/W/H 为设计矩形，Fraction 钳制到 0..1，Color 为实填颜色；生命/冷却共用无描边量条。 */
	void Meter(float X, float Y, float W, float H, float Fraction, FLinearColor Color);
	/** X/Y/W/H 为设计矩形；读取本地视口鼠标，无鼠标位置时返回 false。 */
	bool IsHovered(float X, float Y, float W, float H) const;
	/** State/Player/Attributes 均为 DrawHUD 本帧借用的非空对象；绘制常驻生命、技能、弹药与目标。 */
	void DrawStatus(const ADemoGameState* State, ADemoCharacter* Player, const UDemoAttributeSet* Attributes);
	/** 在场景上绘制圆形瞄准镜遮罩与细十字；物理像素保持任意宽高比下的正圆。 */
	void DrawScopeOverlay();
	/** PC/State/Player/Attributes 仅本帧借用；免费奖励与金币终端使用独立排版，共用真实属性。 */
	void DrawUpgradeMenu(ADemoPlayerController* PC, const ADemoGameState* State, ADemoCharacter* Player, const UDemoAttributeSet* Attributes);
	/** PC/Player本帧借用；绘制目录与模态Tips，属性读取真实武器CDO，装备交给控制器验证。 */
	void DrawWeaponMenu(ADemoPlayerController* PC, ADemoCharacter* Player);
	/** PC/Player为本帧借用，弹药目录和确认UI不直接扣币或写GAS。 */
	void DrawAmmoMenu(ADemoPlayerController* PC,ADemoCharacter* Player);
	/** Index=0..3，X/Y为设计坐标；Color供轮廓回退使用，bUnlocked控制贴图去色；保持原图比例适配160×76区域。 */
	void DrawWeaponIcon(int32 Index, float X, float Y, FLinearColor Color, bool bUnlocked);
	/** State 为本帧借用状态；绘制下一关编号与是/否热区，不在绘制阶段修改关卡或消费确认。 */
	void DrawNextLevelConfirmation(const ADemoGameState* State);
	/** State 是当前世界终局状态；显示本局统计并提供唯一重开入口。 */
	void DrawEndScreen(const ADemoGameState* State);
	// 下列值每帧刷新，仅用于绘制和热区变换，不复制、不持有 World 对象。
	float UIScale = 1.f;
	float ViewWidth = 1280.f;
	float ViewHeight = 720.f;
};
