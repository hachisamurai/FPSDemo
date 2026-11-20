// 职责：本地Canvas HUD与菜单绘制。使用：控制器持有、每帧读取GAS/菜单快照；命中显示依赖Pawn HitFeedback组件，绘制不修改玩法真值。
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
class ADemoWeaponBase;
class UDemoUITheme;
struct FStreamableHandle;
struct FDemoPlayerMenuSnapshot;

/** 本机能力/天赋触发的短时HUD提示条目；仅本地绘制，不复制、不写档。 */
struct FDemoAbilityToast
{
    FString Text;
    FLinearColor Color = FLinearColor::White;
    double ExpireAt = 0.;
};

/** 本机观察到的技能/核心/天赋边沿状态；仅用于触发视觉脉冲，不裁决任何玩法逻辑。 */
struct FDemoAbilityFeedbackState
{
    TWeakObjectPtr<const ADemoCharacter> ObservedPawn;
    TWeakObjectPtr<const ADemoWeaponBase> ObservedWeapon;
    uint8 ObservedPhase = 255;
    float ObservedHealth = -1.f;
    int32 ObservedAmmo = -1;
    bool bWasReloading = false;
    bool bWasDashCooldown = false;
    bool bWasHealCooldown = false;
    int32 ObservedMoveStacks = 0;
    int32 ObservedChargedShots = 0;
    int32 ObservedPrecisionStacks = 0;
    FName ObservedActionState = NAME_None;
    int32 ObservedActionStacks = 0;
    double DashFlashUntil = 0.;
    double HealFlashUntil = 0.;
    float LastHealDelta = 0.f;
    double CorePulseUntil = 0.;
    double DashReadyFlashUntil = 0.;
    double HealReadyFlashUntil = 0.;
    TArray<FDemoAbilityToast> Toasts;
};

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
	/** State/Player为当前World只读借用；普通本人存活视点显示单点，与可否射击独立，模态/观战/专用Scope不叠点，无Canvas依赖。 */
	bool ShouldDrawWorldReticle(const ADemoGameState* State,const ADemoCharacter* Player) const;
	/** Enemy为当前World借用敌人，OutPixelAnchor输出头顶视口像素；死亡/隐藏/背后/遮挡/超距返回false，不改变Actor。 */
	bool GetEnemyStatusScreenAnchor(const ADemoEnemy* Enemy, FVector2D& OutPixelAnchor) const;
	/** BoxName 为注册的操作名称；输入转交控制器，由服务器再次验证，绝不直接修改属性。 */
	virtual void NotifyHitBoxClick(FName BoxName) override;
private:
    /** 本帧已排除旧菜单/终局/靶场后是否消费 R02；只读绘制标记和 CDO 主题，不遍历 Actor 或加载资源。 */
    bool IsR02GameplayPresentation() const;
    /** X/Y/W 为设计坐标，Fraction 为真实只读进度；PhysicalPixels 是物理像素厚度，按真实视口对齐后画线，不生成底板。 */
    void R02HUDLine(float X,float Y,float W,float Fraction,FLinearColor Color,float PhysicalPixels=1.f);
    /** 仅由 ShouldDrawWorldReticle 既有守卫调用；真实视口中心 3×3 物理像素 Ink 单点及 1px Base 描边，不画十字或更改瞄准。 */
    void DrawR02Reticle();
    /** 当前帧真实活Boss借用；仅R02 Combat查询，每帧至多遍历一次，弱缓存不拥有Actor或战斗权限。 */
    const ADemoEnemy* GetR02LiveBoss() const;
    /** State/Boss是本帧真实公开对象；绘制C07大名称、真实关数/复制阶段和2物理px生命线，不复刻概念图身份/阶段。 */
    void DrawR02BossHeader(const ADemoGameState* State,const ADemoEnemy* Boss);
    /** Text为真实数值串，PreferredSize/AvailableWidth为设计字号/可用宽；按实际衬线数字格测量缩小大读数，不截断数值或借旧Barlow比例。 */
    float FitR02NumeralSize(const FString& Text,float PreferredSize,float AvailableWidth) const;
    UPROPERTY(EditDefaultsOnly,Category="Demo|R02 HUD") TObjectPtr<UDemoUITheme> R02HUDTheme; // HUD CDO 强引用只读 R02 主题，Cook 可追踪；不复制、不写存档、不逐帧加载。
    bool bR02GameplayFrame=false; // DrawHUD 每帧更新，Label 只读；排除旧菜单/终局/靶场，非复制/保存，避免逐文字遍历 Actor。
    mutable TWeakObjectPtr<ADemoEnemy> R02BossActor; // 本HUD当前帧的公开Boss弱引用，不跨World保活、不复制、不写档。
    mutable uint64 R02BossObservedFrame=MAX_uint64; // 当前引擎帧查询游标；Status/Spectator/Voice/Campaign共用，非业务版本。
    uint64 R02ObservedSaveSerial=0; // 只读本GI保存修订的本地展示游标，非网络/业务修订，不写存档。
    FString R02ObservedSaveStatus; // 上次已显示的保存文案；与Serial一起只驱动有限提示。
    double R02SaveNoticeUntil=0.; // 单调真实秒，新的本地保存/状态只提示三秒，不常驻成功文字。
    /** 本地菜单关闭时的回执/连接与检查点状态；只读已有服务，不把Toast视为落盘凭据，不注册输入热区。 */
    void DrawSystemStatus(ADemoPlayerController* PC);
    /** PC为本帧本地控制器；R02显示实际语音状态及真队友，Boss时移到C07左上公开姓名/HP/观战表，常态布局不改，无热区。 */
    void DrawVoiceStatus(ADemoPlayerController* PC);
    /** PC为本机拥有者；仅个人阵亡时读权威观战Camera，R02只留公开姓名/HP/等待与原Esc，Boss头复用，不修改相机/复活规则。 */
    void DrawSpectatorStatus(ADemoPlayerController* PC);
    /** 只借用拥有者PC的菜单模型；未就绪返回拒绝操作的空投影，绘制不访问权威服务。 */
    const FDemoPlayerMenuSnapshot& GetPlayerView() const;
	/** 每帧在世界场景上层绘制水平血条及GAS状态图标；不生成点击热区，不复制或写存档。 */
	void DrawEnemyStatusBars();
    /** 当前视口投影设施头顶文字/血条；每位玩家独立朝向、遮挡和镜内裁剪，不旋转或复制目标Actor。 */
    void DrawObjectiveStatusBars();
    // 设施标签最大显示距离cm；投影/裁剪先于最多两个目标的遮挡查询，不扫描其他World。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Objective UI", meta=(ClampMin="100", ClampMax="20000")) float ObjectiveStatusMaxDistance = 6000.f;
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
    /** 本机首次提示覆盖页；使用已有OverlayBack关闭意图，绘制只读，设备已读由MenuFlow保存。 */
    void DrawFirstRunGuide();
    /** PC为当前帧拥有者；只读实时GAS、装备与服务端天赋投影，不修改属性/存档。 */
    void DrawPlayerStatus(ADemoPlayerController* PC);
	/** Name为HitBox路由；有顶层页时消费所有点击，防止旧帧点击穿透。 */
	bool HandleSessionClick(FName Name);
	/** 大厅统一铺纯暖黑，正式UMG与Legacy回退均不再绘制图片或修改World光照。 */
	void DrawLobbyBackground();
	/** Name/Text 为操作路由和文案，X/Y/W/H 为设计坐标；bSelected控制选中亮度，灰阶透明度与局内按钮共用主题。 */
	void LobbyButton(FName Name, const FString& Text, float X, float Y, float W, float H, bool bSelected = false);
	// 历史背景字段/CDO引用保留兼容旧资产；当前大厅不绘制此图片，不读取外部PNG、不复制。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Lobby") TObjectPtr<class UTexture2D> LobbyBackground;
	// 历史字段名保留以兼容已有CDO；现统一控制所有主UI底板不透明度[0.15,0.95]，默认0.45，文字独立不透明，不复制。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Lobby", meta=(ClampMin="0.15", ClampMax="0.95")) float LobbyPanelOpacity = 0.45f;
	// 面板中心相对视口高度的比例；0.43 即中间偏上，运行时钳制到安全边距。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Lobby", meta=(ClampMin="0.3", ClampMax="0.7")) float LobbyAnchorY = 0.43f;
	// 距视口左侧的设计像素，随 UIScale 等比缩放；非复制，本地显示配置。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Lobby", meta=(ClampMin="16", ClampMax="160")) float LobbyLeftMargin = 44.f;
	// CanvasTextItem要求非空UFont；强持有R04中文复合资产，缺资产时用实例引擎回退，不写共享字体。
	UPROPERTY(Transient) TObjectPtr<class UFont> RuntimeFont;
	UPROPERTY(Transient) TObjectPtr<class UFont> RuntimeDisplayFont; // R04纯英文/数字的Barlow复合字体，本HUD强持有，不复制。
	/** Text 为中文文案；X/Y 为设计坐标，Size 为设计像素字号，Color 含透明度，bCentered 指定水平居中。 */
	void Label(const FString& Text, float X, float Y, float Size, FLinearColor Color, bool bCentered = false);
	/** X/Y/W/H 为设计矩形；Color 为面板颜色；Radius 是设计像素圆角，0 为直角。 */
	void Panel(float X, float Y, float W, float H, FLinearColor Color, float Radius = 8.f);
	/** 每次绘制读取本HUD共享不透明度，返回中性灰主面板颜色；不改变文字/图标或网络状态。 */
	FLinearColor GetPanelSurface() const;
	/** Name 为路由，Text 为文案，X/Y/W/H 为设计矩形；禁用不注册热区，bQuiet使用次级灰底，Opacity为主题底色Alpha的[0,1]倍率。 */
	void Button(FName Name, const FString& Text, float X, float Y, float W, float H, bool bEnabled = true, bool bQuiet = false, float Opacity = 1.f);
	/** X/Y/W/H 为设计矩形，Fraction 钳制到 0..1，Color 为实填颜色；生命/冷却共用无描边量条。 */
	void Meter(float X, float Y, float W, float H, float Fraction, FLinearColor Color);
	/** X/Y/W/H 为设计矩形；读取本地视口鼠标，无鼠标位置时返回 false。 */
	bool IsHovered(float X, float Y, float W, float H) const;
	/** State/Player/Attributes为DrawHUD本帧借用；R02无底板分区只读本人钱包/GAS/核心计数，旧变体保留，阶段/技能/ADS守卫共用原逻辑。 */
	void DrawStatus(const ADemoGameState* State, ADemoCharacter* Player, const UDemoAttributeSet* Attributes);
    /** PC为本帧本人控制器；bBackgroundOnly先于状态HUD画柔和外缘，false随后画来向/生命损失残影与状态，仅读受击组件快照。 */
    void DrawPlayerDamageFeedback(ADemoPlayerController* PC,bool bBackgroundOnly);
    /** State/Player/Attributes为本帧只读借用；检测技能冷却、回血、补弹、移速及核心充能边沿并更新本地提示包络。 */
    void UpdateAbilityFeedback(const ADemoGameState* State, const ADemoCharacter* Player, const UDemoAttributeSet* Attributes);
    /** 在信息层之前绘制冲刺/治疗的柔和屏幕边缘脉冲；仅本地视觉，不遮挡中心准星。 */
    void DrawAbilityEdgeFeedback(const ADemoGameState* State, const ADemoCharacter* Player, const UDemoAttributeSet* Attributes);
    /** 在左下生命区上方绘制能力/天赋触发短提示与已拥有天赋计数；仅本地视觉。 */
    void DrawAbilityProcToasts(const ADemoCharacter* Player, const UDemoAttributeSet* Attributes);
    /** 当前玩家是否处于核心强化待兑现状态（用于准星角标提示）；纯只读查询。 */
    bool IsCoreEmpoweredNow(const ADemoCharacter* Player, const ADemoGameState* State) const;
    FDemoAbilityFeedbackState AbilityFeedback; // 本机能力视觉反馈状态，不复制/不写档。
	/** Player为当前本地Avatar；只读有界真实命中缓存，R02画四角点/投影数字，旧变体画斜线，均不产生伤害或输入。 */
	void DrawHitFeedback(const ADemoCharacter* Player);
	/** 在场景上绘制圆形瞄准镜遮罩与细十字；物理像素保持任意宽高比下的正圆。 */
	void DrawScopeOverlay();
	/** PC/State/Player/Attributes 仅本帧借用；免费奖励与金币终端使用独立排版，共用真实属性。 */
	/** 服务器核心投影卡片，绘制不提交选择。 */
    void DrawCoreMenu(ADemoPlayerController* PC);
    /** 公开目标、分区进场短显与全阶段方向引导；导航仅表现，不在HUD裁决。2026-11-19文字减负：R02常驻只留一行目标，地点/路线交给雷达与短显。 */
    void DrawCampaignStatus(const ADemoGameState* State, ADemoPlayerController* PC);
    /** State/PC为当前帧只读借用；右上角绘制十关功能区/走廊/安全区拓扑雷达、本人朝向锥及实时目标点，不画标题/图例文字，不修改玩法或网络状态。 */
    void DrawTacticalMinimap(const ADemoGameState* State, ADemoPlayerController* PC);
    /**
     * Location为服务器授权的任务/标记位置，Text/Color只读显示，Lane=0..4分散重叠；离屏/背后画方向箭头+文字+米数，开镜时约束到镜内。
     * bCaptionInScreen=false时目标在屏内只画小菱形不写字（用于本身已有血条/实体可辨认的敌人与设施）；屏内20m内省略米数。
     */
    void DrawWorldDirectionIndicator(const FVector& Location, const FString& Text, FLinearColor Color, int32 Lane, bool bCaptionInScreen = true);
    /**
     * 战斗/清关时进入新功能分区后在左上短显“B区 · 折线查验坪”约2.5秒后淡出，替代常驻“当前位置/区域路线”两行。
     * ZoneKey=关数*16+分区索引，INDEX_NONE表示当前在走廊或无D03拓扑（不触发、只续画未结束的短显）；ZoneId/ZoneName为静态拓扑表常量；Y为设计像素纵坐标。仅本地显示，不复制、不存档。
     */
    void DrawZoneEntryToast(int32 ZoneKey, const TCHAR* ZoneId, const TCHAR* ZoneName, float Y);
    // 分区短显：上次已触发的ZoneKey；安全区阶段重置为INDEX_NONE，使下一次进关首个分区重新短显。本地HUD状态，不复制/不写档。
    int32 ZoneToastKey = INDEX_NONE;
    // 分区短显结束时刻（World秒）；<=当前时刻不绘制。单人暂停时随World时间冻结。本地状态。
    double ZoneToastUntil = 0.;
    // 分区短显文案，仅在ZoneKey变化时格式化一次，避免每帧分配。本地状态。
    FString ZoneToastText;
    void DrawUpgradeMenu(ADemoPlayerController* PC, const ADemoGameState* State, ADemoCharacter* Player, const UDemoAttributeSet* Attributes);
	/** PC/Player本帧借用；只绘目录与明确直接装配热区，无二级详情；属性读真实CDO，原权威服务验证装备。 */
	void DrawWeaponMenu(ADemoPlayerController* PC, ADemoCharacter* Player);
	/** PC/State为当前帧权威投影；绘制银币抽取、金币回收及遗物直购，确认时只注册确认/取消热区，不在HUD结算。 */
    void DrawGambleMenu(ADemoPlayerController* PC,const ADemoGameState* State);
    /** 金币娱乐独立详情：公开赔率/下注、数字、三轴和货箱；全部按钮转入MenuFlow校验。 */
    void DrawCasinoMenu(ADemoPlayerController* PC);
    /** Index=0..4符号，X/Y为56px图标左上角设计坐标；复用已加载Ammo图标，金币使用文字，无每帧资源加载。 */
    void DrawCasinoSymbol(int32 Index,float X,float Y);
    /** 新玩法左侧交互或全宽结果；PC只借用公开快照，X/Y设计坐标，bResult决定布局。 */
    void DrawArcadePanel(ADemoPlayerController* PC,float X,float Y,bool bResult);
    /** UI爆炸环、碎片与闪光；Time为回执后的真实秒，不生成世界伤害。 */
    void DrawCasinoExplosion(float X,float Y,double Time);
    /** 个人靶场HUD，游戏中显示剩余秒、靶数、命中和枪数，不提供经济权限。 */
    void DrawCasinoRangeOverlay();
    /** PC提供只读滚动状态；X/Y为终端设计坐标，中央指针对齐权威结果，Canvas剪裁卡片边缘。 */
    void DrawGambleReel(ADemoPlayerController* PC,float X,float Y);
    /** Text先按显式换行区分金额/情报，再按保守中文列宽折行；X/Y设计坐标，Lines限制详情高度。 */
    void DrawGambleText(const FString& Text,float X,float Y,int32 Lines);
    /** PC/Player为本帧借用，绘制弹药目录、金币价格与确认UI，不直接扣币或写GAS。 */
    void DrawAmmoMenu(ADemoPlayerController* PC,ADemoCharacter* Player);
	/** Index=0..3，X/Y为设计坐标；Color供轮廓回退使用，bUnlocked控制贴图去色；保持原图比例适配160×76区域。 */
	void DrawWeaponIcon(int32 Index, float X, float Y, FLinearColor Color, bool bUnlocked);
	/** State 为本帧借用状态；绘制下一关编号与是/否热区，不在绘制阶段修改关卡或消费确认。 */
	void DrawNextLevelConfirmation(const ADemoGameState* State);
    /** 复制的最多4人准备列表；显示各自核心、阻塞原因及房主档位，绘制不修改Ready。 */
    void DrawTeamReadyMenu(const ADemoGameState* State);
	/** State为公开终局；只显示代数匹配的本人冻结战报，房主提供原回Hub/重开入口，客户端等待不推算奖励。 */
	void DrawEndScreen(const ADemoGameState* State);
	// 下列值每帧刷新，仅用于绘制和热区变换，不复制、不持有 World 对象。
	float UIScale = 1.f;
	float ViewWidth = 1280.f;
	float ViewHeight = 720.f;
    uint32 DrawnRoomRevision = 0; // 最近实际绘制的列表版本，点击不能使用尚未呈现的新结果。
    int32 DrawnRoomOffset = 0; // 最近实际绘制的分页偏移，翻页后旧热区仍对应旧行。
};
