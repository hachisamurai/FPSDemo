// 职责：玩家控制器公开输入/菜单/会话接口。使用：HUD与Pawn调用，依赖MenuFlow及个人PS，不持有共享经济真值。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Game/DemoCombatConfig.h"
#include "UI/DemoMenuTypes.h"
#include "Player/DemoPlayerCommands.h"
#include "Player/Coop/Preparation/DemoEncounterPreparationTypes.h"
#include "DemoPlayerController.generated.h"

class UInputMappingContext;
class ADemoInteractable;
class ADemoCharacter; // 观战RPC只传服务器选中的队友Actor引用，不传相机变换或私有FP状态。
class UDemoLocalOutlineComponent; // 本PC唯一私有外沿捕获/合成资源持有者，与经济/输入组件独立。

/** 本地输入与光标应用入口；菜单状态唯一归MenuFlow，公开菜单方法仅为兼容转发。 */
UCLASS()
class FPSDEMO_API ADemoPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
    /** 仅本地设备批准RPC调用，更新同一拥有者菜单快照；不提交经济/任意终端会话。 */
    void AcceptHubShopMenuSnapshot(const FDemoPlayerMenuSnapshot& Snapshot);
    /** Request为服务器从冻结生成组构造的本关资源请求；只由本PC本地异步加载，不直接传送。 */
    UFUNCTION(Client, Reliable) void ClientPrepareEncounter(const FDemoEncounterPreloadRequest& Request);
    /** 仅取消同代数旧请求，关闭页面/换人/超时/旅行时撤销本地回调，迟到完成不能确认下一关。 */
    UFUNCTION(Client, Reliable) void ClientCancelEncounterPreload(int32 Generation);
    /** 服务器消费准备事务后通知本PC；Generation必须匹配成功ACK，旧房间等位置更新再退役。 */
    UFUNCTION(Client, Reliable) void ClientCommitEncounterPreload(int32 Generation);
    /** 全队回常驻安全区后调用，客户端延迟释放自己的房间；不修改战役或个人数据。 */
    UFUNCTION(Client, Reliable) void ClientResetEncounterRooms();
    /** 当前拥有者确认资源/落脚地面；Generation绑定当前冻结队伍，Code为固定错误名，不上传路径或命中。 */
    UFUNCTION(Server, Reliable) void ServerReportEncounterPreload(int32 Generation, bool bReady, FName Code);
	/** 显式加载模板增强输入映射，避免依赖旧蓝图 Controller 默认值。 */
	ADemoPlayerController();
	/** 添加本地映射并进入大厅 GameAndUI 模式，冻结角色等待开始。 */
	virtual void BeginPlay() override;
	/** EndPlayReason为卸载原因，移除本控制器加入的映射，不遗留跨World输入。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** DeltaTime为引擎帧秒数；暂停仍推进退出保存/显示确认，Editor另记录限频移动、内存与帧间隔快照。 */
	virtual void PlayerTick(float DeltaTime) override;
    /** DeltaTime为本帧秒；在原生输入/后坐求值前应用统一俯仰限制，控制方向与摄像机/武器始终一致。 */
    virtual void UpdateRotation(float DeltaTime) override;
    /** 服务器Possess及客户端PS复制后分别重绑本人只读进度；旧Avatar不保留委托。 */
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnRep_PlayerState() override;
    /** 客户端Pawn引用可能晚于PS与BeginPlay，复制就绪后推进同一有界握手。 */
    virtual void OnRep_Pawn() override;
    /** Request为白名单业务意图；补齐本PC序号后经拥有者RPC提交，不在UI直接访问GameMode。 */
    void SubmitGameplayCommand(FDemoGameplayCommandRequest Request);
    /** 只有拥有此PC的连接可调用；服务器通过this解析Pawn/PS，不接受客户端经济快照。 */
    UFUNCTION(Server, Reliable) void ServerGameplayCommand(const FDemoGameplayCommandRequest& Request);
    /** G Started本地入口；Combat/存活/无菜单才提交，使用本机单调时间节流，不指定位置或目标。 */
    void RequestTeamPing();
    /** 装饰性标记允许丢包；服务器独立限频并从本PC的真实Pawn视线解析，不接受客户端坐标或Actor。 */
    UFUNCTION(Server, Unreliable) void ServerTeamPing();
    /** PlayerKey为LAN设备键或EOS PUID关联键；服务器Public模式从P2P源重新推导，禁止凭自报键恢复存档。 */
    UFUNCTION(Server, Reliable) void ServerRegisterCoopPlayer(const FString& PlayerKey);
    /** bAccepted表示固定身份已登记；bRetryable仅表示依赖尚未就绪，Reason为安全的用户提示。 */
    UFUNCTION(Client, Reliable) void ClientCoopRegistrationResult(bool bAccepted, bool bRetryable, const FString& Reason);
    /** 权威与本地各自确认身份；Standalone兼容原初始化，网络玩家必须成功握手。 */
    bool IsCoopRegistered() const;
    /** HUD只读连接状态，最终失败保留可见提示，不允许未登记玩家自由参战。 */
    const FString& GetConnectionStatus() const;
    /** 服务器完成事实只发送给拥有者，本机Profile不参与其他玩家服务器权限校验。 */
    UFUNCTION(Client, Reliable) void ClientRunCompletion(const FDemoRunCompletion& Completion);
    /** Target为权威选定存活队友；拥有者创建独立TP观战相机，复活仍走既有ClientSetViewTarget(Pawn)。 */
    UFUNCTION(Client, Reliable) void ClientSpectateTeammate(ADemoCharacter* Target);
    /** 已生效/被拒绝的业务回执及权威菜单快照，按序消费，迟到回复不能重开旧页。 */
    UFUNCTION(Client, Reliable) void ClientGameplayCommandResult(EDemoGameplayCommand Command, const FDemoCommandResult& Result, const FDemoPlayerMenuSnapshot& Snapshot);
    /** 服务器进度变化时仅向拥有者推送，不带新的用户操作意图。 */
    UFUNCTION(Client, Reliable) void ClientRefreshMenuSnapshot(const FDemoPlayerMenuSnapshot& Snapshot);
    /** 房主保存/阶段变化后可显式推送当前本人视图，不依赖HUD轮询权威服务。 */
    void PublishMenuSnapshot();
    /** 只读本PC缓存；Canvas可每帧绘制，业务规则只在服务器计算。 */
    const FDemoPlayerMenuSnapshot& GetMenuSnapshot() const;
    /** 仅Standalone暂停World；联机Esc只锁定本机输入。 */
    bool SetLocalMenuPaused(bool bPaused);
    /** 本地离开连接回到入口地图；不会要求远程服务器关闭全队战局。 */
    void LeaveCoopToLobby();
    /** OutMessage为本机落盘失败原因；主机额外保存队伍检查点，客户端不写房主数据。 */
    bool SaveOwnedProgressBeforeExit(FString& OutMessage);
    /** 控制台开房/加入入口；地址限LAN主机/IP，最大4人规则由服务器登录检查。 */
    UFUNCTION(Exec) void HostCoop();
    UFUNCTION(Exec) void JoinCoop(const FString& Address);
	/** Esc只在局内暂停；子页返回上层，奖励/终端窗口不因暂停而跳过。 */
	void EscapePressed();
    /** Tab按下切换状态面板；已有终端沿用关闭动作，暂停时仍可关闭。 */
    void TabPressed();
	/** Index=0..2，选择有效档或打开空栏创建确认；Confirm表示是/否，否不创建文件。 */
	void SelectSaveSlot(int32 Index);
	/** Confirm来自当前创建弹窗，是则创建PendingSaveSlot，否只返回存档列表。 */
	void ConfirmCreateSave(bool Confirm);
	/** 已选槽完成OpenLevel恢复时调用，重置旧输入标志并根据阶段显示Reward/Victory。 */
	void OnRunReady();
	/** 仅Pause页接受：已在Hub只关闭Esc覆盖层并解除暂停；其他阶段的成长确认由GameMode再次校验。 */
	void ReturnHubPressed();
	/** Confirm为成长清空的明确选择；必须在当前ReturnHub页，否保留暂停与全部数据。 */
	void ConfirmReturnHub(bool Confirm);
	/** 当前Pause页返回大厅；安全阶段写检查点成功才旅行，战斗保留上次检查点。 */
	void ReturnLobbyPressed();
	/** Setting=0分辨率/1画质/2鼠标/3窗口/4垂直同步，Direction为+1/-1；仅修改待应用值。 */
	void CycleSetting(int32 Setting, int32 Direction);
	/** 应用设置；显示改变进入15秒确认，Confirm=false或超时回退旧显示值。 */
	void ApplyUserSettings();
	/** Confirm表示保留/恢复显示模式；仅有待确认变更时接受，不影响已应用画质与鼠标。 */
	void ConfirmDisplaySettings(bool Confirm);
	/** UI只读接口：顶层页、阻挡状态、待显示设置文本和剩余确认秒数。 */
	EDemoMenuPage GetMenuPage() const;
	/** 是否有顶层模态页；用于阻断旧奖励/商店/战斗操作，不代表World一定暂停。 */
	bool HasBlockingOverlay() const;
	/** 本控制器是否打开了暂停系列菜单，包括其设置与返回确认子页。 */
	bool IsPauseMenuOpen() const;
	/** Setting=0..4；返回本地设置草稿的格式化文本，无写盘副作用。 */
	FString GetSettingText(int32 Setting) const;
	/** 真实时间剩余秒，0代表无待确认显示模式；不使用暂停的World时钟。 */
	float GetDisplayConfirmSeconds() const;
	/** 返回/取消当前顶层页；显示模式待确认时先回退，绝不隐式保存。 */
	void CloseOverlay();
	/** 绑定升级数字键；Tab 关闭商店/取消出发，Enter 确认出发或处理大厅/终局。 */
	virtual void SetupInputComponent() override;
	/** bReward=true 为必须选择一次的免费奖励，false 为终端金币商店。 */
	void OpenUpgradeMenu(bool bReward);
	/** Tab 共用入口：取消出发确认或关闭商店；奖励选择前不可关闭，防止跳过关卡奖励状态。 */
	void CloseUpgradeMenu();
	/** Terminal 为本次交互的下一关终端；仅合法备战阶段且在范围内打开，弱引用不会延长终端生命周期。 */
	void OpenNextLevelConfirmation(ADemoInteractable* Terminal);
	/** “是”按钮/Enter 调用；重新校验交互条件，消费一次确认后交给 GameMode 推进。 */
	void ConfirmNextLevel();
	/** “否”按钮/Tab 调用；清除待确认请求，保留当前进度并恢复适合当前阶段的输入。 */
	void CancelNextLevelConfirmation();
	/** 本地确认框是否显示；不改变复制的 Hub/Intermission 阶段。 */
	bool IsNextLevelConfirmationOpen() const;
	/** Choice=0..2，转交权威 GameMode；成功后刷新/关闭界面。 */
	void SelectUpgrade(int32 Choice);
	/** 终局冻结移动但保持 UI/Enter 输入，停止持续射击。 */
	void ShowEndScreen();
	/** 当前菜单是否可见，只读本地状态，不复制。 */
	bool IsUpgradeMenuOpen() const;
	/** 当前可见菜单是否属于免费奖励。 */
	bool IsRewardMenu() const;
	/** bWeapons为终端页签：false属性/true武器；奖励页不允许切换，重新验证终端距离。 */
	void SetTerminalWeaponPage(bool bWeapons);
	/** 只读武器页状态；仍属于升级终端的模态输入生命周期。 */
	bool IsWeaponMenuOpen() const;
	/** 安全区弹药页入口，复用终端权限，成功才切换页签。 */
	void OpenAmmoMenu();
	/** 只读当前弹药页及查看项/报价，HUD不修改状态。 */
	bool IsAmmoMenuOpen() const;
	int32 GetInspectedAmmo() const;
	int32 GetPendingAmmoPrice() const;
	/** Index=0..3，只改变查看对象并撤销旧报价。 */
	void InspectAmmo(int32 Index);
	/** 已解锁则装配，未解锁则生成明确购买报价。 */
	void AmmoAction();
	/** Confirm来自当前购买确认，取消不扣币，确认重新验证价格和余额。 */
	void ConfirmAmmoPurchase(bool Confirm);
	/** CatalogIndex=0..3；点击Item打开说明，锁定武器也可查看，但不会直接装备。 */
	void InspectWeapon(int32 CatalogIndex);
	/** 返回当前Tips目录索引，INDEX_NONE为关闭；只读本地状态。 */
	int32 GetInspectedWeapon() const;
	/** 关闭当前说明框，保留武器页与终端输入锁。 */
	void CloseWeaponTip();
	/** Tips的明确装备按钮；重复验证解锁/范围/生命/阶段后才交给组件。 */
	void EquipInspectedWeapon();
	/** 终端重试本地保存和云连接；失败仍显示可读错误，不修改装备。 */
	void RetryProfileSave();
	/** 大厅云同步入口；Choice=0重试/1保留本机/2采用云端，非大厅拒绝覆盖检查点。 */
	void CloudAction(int32 Choice);
	/** 返回最新购买/拒绝提示，供 HUD 显示。 */
	const FString& GetMenuMessage() const;
	/** Enter 共用入口；优先处理出发确认，其余为大厅开始/终局重开，战斗中拒绝。 */
	void RestartPressed();
	/** 初始大厅调用；冻结角色物理移动，显示光标但不打开升级菜单。 */
	void ShowLobby();
	/** 大厅开始按钮/Enter 调用；隐藏主菜单并读取三个存档栏，不直接开始战斗。 */
	void StartGamePressed();
	/** Difficulty来自初始安全区终端；有效选择同时确认出发，第一关开始后锁定。 */
	void DifficultyPressed(EDemoDifficulty Difficulty);
	/** 安全区下一关终端选择无尽；权威再次验证解锁，不允许旧热区绕过。 */
	void EndlessPressed();
	/** 地狱完整通关后覆盖结算页的提示；确认/ESC关闭后恢复原结算页。 */
	void ShowEndlessUnlockTip();
	/** 大厅/暂停打开真实设置草稿页；分辨率、画质、鼠标和窗口由应用按钮提交。 */
	void SettingsPressed();
	/** 主大厅/暂停接受退出；先显示保存窗口，落盘并等待云端确认后走引擎QuitGame。 */
	void QuitPressed();
	/** 保存失败页重新尝试本地落盘和云同步；不能从其他页面触发。 */
	void RetryQuitSave();
	/** 云失败且本地保存成功时的明确选择；再次确认本地落盘后跳过云等待退出。 */
	void QuitWithLocalSave();
	/** 取消退出并恢复原大厅/暂停页；后台补传继续，晚到回执不能结束游戏。 */
	void CancelQuitSave();
	/** HUD只读保存阶段，不执行网络或磁盘操作。 */
	EDemoQuitState GetQuitState() const;
	/** HUD据此启用仅本地退出；本地写盘失败时必须禁用。 */
	bool IsQuitLocalSaved() const;
	/** 本地只读设置页显示状态，生命周期为此控制器。 */
	bool IsSettingsOpen() const;
    /** 只读当前终端会话，权威服务在每次提交时校验对象和版本。 */
    ADemoInteractable* GetMenuTerminal() const;
    uint64 GetMenuTerminalGeneration() const;
    /** 当前本地菜单组件借用引用，用于专属回归或UI集成。 */
    class UDemoMenuFlowComponent* GetMenuFlow() const;
private:
    UPROPERTY(VisibleAnywhere,Category="Demo|Feedback") TObjectPtr<class UDemoPlayerDamageFeedbackComponent> DamageFeedback; // 本PC拥有的OwnerOnly反馈子对象；跨Pawn重绑，不拥有ASC或存档。
    UPROPERTY(VisibleAnywhere, Category="Demo|Preload") TObjectPtr<class UDemoLocalEncounterPreload> EncounterPreload; // 本PC拥有的异步资源句柄，World卸载准确撤销，无常规Tick或复制。
    /** SetupInput时安装本PC的G动作；与Pawn解耦，重生不重复注册，服务器无LocalPlayer不装配。 */
    void InstallTeamPingInput();
    /** EndPlay/重复Setup前移除本PC映射和绑定，避免旧World事件或重复请求。 */
    void RemoveTeamPingInput();
    UPROPERTY(Transient) TObjectPtr<class UInputAction> TeamPingAction; // 本地布尔动作，Started只触发一次，无需创建二进制输入资产。
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> TeamPingMapping; // 本PC优先级2的G上下文，菜单由请求门禁阻挡。
    TWeakObjectPtr<class UEnhancedInputComponent> TeamPingInput; // 绑定来源，不延长旧输入组件寿命。
    uint32 TeamPingBindingHandle = 0; // 当前G绑定句柄，仅在TeamPingInput有效时用于精确移除。
    bool bTeamPingBound = false; // 句柄0也可能合法，使用独立标志管理绑定生命周期。
    double NextLocalTeamPingAt = 0.0; // FPlatformTime秒，限制本地正常请求；服务器另有不可绕过的1秒预算。
    /** SetupInputComponent调用：运行时创建Enhanced Input的T映射，PC持有，独立于武器/移动上下文。 */
    void InstallVoiceInput();
    /** EndPlay卸载本机映射并关麦；不修改全局输入配置或其他LocalPlayer。 */
    void RemoveVoiceInput();
    /** Started回调：把明确按下意图交给本人PS，麦克风门禁和设备错误由语音组件处理。 */
    void VoicePressed();
    /** Completed/Canceled和菜单切换共用松键入口，失焦兜底由语音组件轮询。 */
    void VoiceReleased();
    UPROPERTY(Transient) TObjectPtr<class UInputAction> VoiceAction; // 本PC创建的布尔动作；无需手工伪造输入uasset。
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> VoiceMapping; // 本地优先级2的T映射，World卸载时移除。
    // 本机视角限制边沿日志缓存，非复制、不存档；不参与瞄准或输入计算。
    bool bWasAtAimPitchLimit=false;
    /** bLifecycleReady由Possess/OnRep触发；按单调时间最多30秒，未就绪时每秒重试一次。 */
    void UpdateCoopRegistration(bool bLifecycleReady = false);
    bool bCoopRegistered = false; // 服务器固定队伍接纳、客户端收到确认后各自置位，不由客户端值复制授权。
    bool bRegistrationAwaitingReply = false; // 可靠请求在途不重复派发，30秒期限仍生效。
    bool bRegistrationFinished = false; // 最终失败/成功后停止重试，旅行建立新PC才重置。
    double RegistrationDeadline = 0.0; // FPlatformTime秒，BeginPlay启动；暂停不延长30秒握手预算。
    double NextRegistrationAttempt = 0.0; // 重试节流秒数；生命周期可提前一次但不能越过最终失败。
    FString ConnectionStatus; // 本机连接提示，不写入Profile或云端，不显示GUID。
    /** PS就绪时绑定一次，服务器变更发布业务投影，客户端变更只更新本人数值。 */
    void BindProgressView();
    /** 拥有者PS提交/复制回调；不从客户端向服务器提交值快照。 */
    void OnProgressChanged();
    UPROPERTY() FDemoPlayerMenuSnapshot MenuSnapshot; // 此PC拥有的只读投影，仅本地HUD借用，不持久化。
    TWeakObjectPtr<class UDemoPlayerProgressComponent> ObservedProgress; // 委托源弱引用，旅行时解绑。
    FDelegateHandle ProgressChangedHandle; // 当前PS回调句柄，避免重复绑定及旧Pawn回调。
    int32 NextRequestId = 0; // 本地PC递增操作序号，PC重建重置。
    int32 LastServerRequestId = 0; // 服务端去重水位，同一PC内重复请求不再执行。
    int32 LastClientResultId = 0; // 本地已消费回执水位，旧回复不得覆盖新意图。
    friend class UDemoMenuFlowComponent; // 仅菜单服务可请求实际InputMode；不开放输入资源写访问。
    friend class UDemoUIManager; // CommonUI退出时一次性交还旧Canvas/游戏输入；不允许Widget直接写门禁。
    /** 三个快捷键包装，仍绑定在控制器上并记录真实输入。 */
    void SelectFirst();
    void SelectSecond();
    /** 数字3回调：升级页选择第三项，正常游玩请求近战装备；不在同一输入中执行两种动作。 */
    void SelectThird();
    /** bEnabled指定菜单输入；仅菜单/阻塞/当前Pawn或输入子系统变化时提交，重复钱包回调不能重建映射或重抢焦点。 */
    void SetMenuInput(bool bEnabled);
    bool bInputPolicyApplied = false; // 本PC是否已完成首次输入应用；初始化前即使默认值相同也不能直接跳过。
    bool bAppliedMenuInput = false; // 上次实际应用的菜单模式，不属于服务器游戏状态或存档。
    bool bAppliedMovementBlocked = false; // 上次由菜单/登记/阵亡共同决定的移动锁，只有边沿才重置锁计数。
    TWeakObjectPtr<APawn> InputPolicyPawn; // 上次应用时的Avatar，重生/复制换Pawn必须补做门禁，不延长旧Pawn生命。
    TWeakObjectPtr<class UEnhancedInputLocalPlayerSubsystem> InputPolicySubsystem; // 本地输入容器重建后补应用；不跨LocalPlayer持有强引用。
#if WITH_EDITOR
    /** DeltaTime为引擎帧秒数；本地PC通常每秒采内存/移动，长帧可提前补样但警告最多每两秒一次，不查询磁盘/网络。 */
    void RecordRuntimeDiagnostics(float DeltaTime);
    double LastRuntimeTickAt = 0.0; // 单调秒，用于观察真实帧间隔，暂停/卡顿不依赖被钳制的World Delta。
    double LastRuntimeSampleAt = 0.0; // 每秒一次状态记录，0表示当前PC尚无样本。
    double LastRuntimeHitchAt = -10.0; // 长帧日志冷却截止比较基准，避免低FPS时再次形成日志洪泛。
    double WorstRuntimeGap = 0.0; // 当前采样区间内最大帧间隔秒数，输出后清零。
#endif
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoMenuFlowComponent> MenuFlow; // 本PC唯一页面状态，不跨旅行保留。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoUIManager> CommonUI; // 本PC本地Widget栈投影，不复制/保存业务数据。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoShopFocusComponent> ShopFocus; // 本人枪墙/设备交互，独立于View/相机实现。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoCasinoDeviceInputComponent> CasinoDeviceInput; // 本人默认复制输入通道；物理租约/nonce，不拥有经济真值。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoCasinoLocalViewComponent> CasinoLocalView; // 本机NoCollision私有层；Listen不动共享查询/锚点。
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<UDemoLocalOutlineComponent> LocalOutline; // 默认子对象只管理本人渲染；原枪Focus/设备LocalView按请求者借用，不复制经济或选择。
    UPROPERTY() TObjectPtr<UInputMappingContext> MappingContext; // 模板移动/视角映射，本地PC管理。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Input") TSoftObjectPtr<UInputMappingContext> CombatMappingAsset; // 武器映射资源路径，保留旧蓝图字段与Cook依赖。
    UPROPERTY() TObjectPtr<UInputMappingContext> CombatMappingContext; // BeginPlay加载后强引用，菜单仅启停本上下文。
};
