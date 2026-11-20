// 职责：本地菜单状态与输入门禁。使用：PC持有组件，HUD只读投影，经济操作通过拥有者请求网关。
#pragma once
#include "CoreMinimal.h"
#include "UI/DemoUIActions.h"
#include "Components/ActorComponent.h"
#include "Player/DemoPlayerController.h"
#include "UI/Flow/DemoGambleReel.h" // 纯UI滚动状态；不拥有经济结果。
#include "Interaction/HubShops/Casino/DemoCasinoControlTypes.h" // 世界控件只收类型化绑定，不能反射UI字符串取得服务器授权。
#include "DemoMenuFlowComponent.generated.h"

/** 本地PC拥有的唯一菜单状态机；不复制，不持有钱包，不直接执行权威购买。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoMenuFlowComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无组件Tick；PC在暂停仍执行的PlayerTick提供真实时间更新。 */
    UDemoMenuFlowComponent();
    /** Action为白名单Canvas意图，先校验顶层页面再转业务入口；不直接授予网络权限。 */
    void DispatchUIAction(EDemoUIAction Action);
    /** 当前批准的Hub升级终端打开核心页，不隐式选择或改模式。 */
    void OpenCoreMenu();
    /** 只读页面状态，生命周期归原终端菜单。 */
    bool IsCoreMenuOpen() const;
    /** Choice=0..CoreCount-1，提交本人选择修订，服务器复核Hub/距离并保存；升级终端核心页或Hub出发页均可发起，各用其已签发终端会话。 */
    void SelectCore(int32 Choice);
    /** Hub非无尽且出发确认页打开、出发终端仍有效时为true；只读，供出发页核心预览/选择门禁，不代表服务器已允许。 */
    bool IsDepartureCorePickerOpen() const;
    /** Command/Result来自本PC拥有者可靠回执；更新文案并在成功后消费当前操作页。 */
    void ApplyCommandResult(EDemoGameplayCommand Command, const FDemoCommandResult& Result);
    /** Snapshot为服务器批准的当前交互，只有成功BeginInteraction回执才能打开终端。 */
    void OpenApprovedInteraction(const FDemoPlayerMenuSnapshot& Snapshot);
    /** Owner设备批准后直接打开对应既有玩法页，不冒充远处升级终端，不新增概率/总列表。 */
    void OpenCasinoDevicePage(const FDemoPlayerMenuSnapshot& Snapshot);
    /** 本机是否在有效设备专属页，用于租约心跳/关闭与防止跨设备列表操作。 */
    bool IsCasinoDevicePage() const;
    /** 服务器批准的物理上下文初始化原草稿；不设置bMenuOpen，不占用全屏菜单。 */
    void BeginCasinoDeviceOperation(const FDemoPlayerMenuSnapshot& Snapshot);
    /** 本地Operate退出时清草稿等待/表现，不清任何权威未结局或钱包。 */
    void EndCasinoDeviceOperation();
    /** 本人独立复制组件拥有有效Operate；用于共用提交和本地输入互斥，不替代服务器租约。 */
    bool IsCasinoDeviceOperating() const;
    /** 当前真实命中的可信绑定；本地选择只改草稿，拉杆走原报价/确认/Waiting，再交Owner RPC。 */
    void SubmitDeviceAction(const FDemoCasinoControlBinding& Binding);
    /** 本组件nonce验证后的Owner回执，复用原结果关联逻辑，不从UI发奖。 */
    void ReceiveCasinoDeviceResult(const FDemoCommandResult& Result);
    /** 本机金额文本，只作独立NoCollision屏显示，不复制、不进入存档。 */
    const FString& GetCasinoDeviceAmountText() const;
    /** GameState阶段复制/权威事件触发，更新本机底层页面，保留设置/退出覆盖层。 */
    void RefreshFromReplicatedState();
    /** Reason为最终握手失败/超时；即使GameState尚未复制也提供本机回大厅与退出入口。 */
    void ShowConnectionFailure(const FString& Reason);
    /** bSearch=true表示用户点击入口自动搜索；失败旅行恢复用false保留诊断。 */
    void MultiplayerPressed(bool bSearch = true);
    /** 房间页显式刷新；服务忙时不叠加异步请求。 */
    void RefreshRooms();
    /** bPublic为显式网络选项；房间服务拒绝在连接/SDK操作中切换提供者。 */
    void SelectRoomNetwork(bool bPublic);
    /** 房间页账号入口，已登录则注销；公网搜索成功前不能创建或加入。 */
    void RoomAccountPressed();
    /** 重新搜索后选择兼容的开放房间，不自动创建存档。 */
    void QuickJoinRoom();
    /** 从房间页转到房主专用存档选择页，不复用普通单机旅行命令。 */
    void CreateRoomPressed();
    /** Index/Revision为HUD实际绘制的行与版本，服务拒绝刷新后的旧点击。 */
    void JoinRoom(int32 Index, uint32 Revision);
    /** RawAddress为用户输入的域名或IP（可选:端口）；仅本机多人页有效，校验与直连旅行均交RoomSubsystem。 */
    bool JoinDirectRoom(const FString& RawAddress, FString& OutError);
    /** Direction=-1/+1翻页；界面每页最多5行，服务可返回更多房间。 */
    void ChangeRoomPage(int32 Direction);
    /** HUD只读页码/开房选槽标志，不修改业务状态。 */
    int32 GetRoomPage() const;
    bool IsChoosingHostSave() const;
    /** DeltaTime为PC帧间隔，退出/显示确认使用单调时间；闲置不发网络或写盘。 */
    void UpdateRealtime(float DeltaTime);
    /** PC结束时撤销退出意图和显示确认，GI上传队列继续独立存活。 */
    void Shutdown();
    /** EndPlayReason为控制器卸载原因，重复Shutdown安全。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** 当前升级终端会话的弱引用与版本，商店提交时重新验证；奖励页没有交易会话。 */
    ADemoInteractable* GetMenuTerminal() const;
    uint64 GetMenuTerminalGeneration() const;

	/** Esc只在局内暂停；子页返回上层，奖励/终端窗口不因暂停而跳过。 */
	void EscapePressed();
    /** Tab切换本人状态；终端保留原关闭语义，奖励/其他模态不允许被绕过。 */
    void TabPressed();
    /** Direction为-1/+1；只改变本地天赋分页，不发经济RPC。 */
    void ChangeStatusPage(int32 Direction);
    /** 返回当前分页（每页5项），按最新拥有者投影钳制，HUD只读。 */
    int32 GetStatusPage() const;
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
    /** 当前首次操作提示的关闭按钮/Esc/Tab入口；先记录本机已读，再恢复底层页面，不暂停网络World。 */
    void CloseFirstRunGuide();
		/** bReward=true 为必须选择一次的免费奖励，false 为终端金币商店。 */
	void OpenUpgradeMenu(bool bReward);
	/** Tab 共用入口：取消出发确认或关闭商店；奖励选择前不可关闭，防止跳过关卡奖励状态。 */
	void CloseUpgradeMenu();
	/** Terminal 为本次交互的下一关终端；仅合法备战阶段且在范围内打开，弱引用不会延长终端生命周期。 */
	void OpenNextLevelConfirmation(ADemoInteractable* Terminal);
	/** “是”按钮/Enter 调用；重新校验交互条件，消费一次确认后交给 GameMode 推进。 */
	void ConfirmNextLevel();
    /** bReady为明确目标；只从已批准下一关页面提交GS版本，成功回执才改变显示。 */
    void SetTeamReady(bool bReady);
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
	/** 本机终端导航。false 回到属性首页并收起已结算抽取的表现，true 打开武器页。须持有有效商店会话，抽取确认或等待回执时拒绝切页。 */
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
	/** CatalogIndex=0..3；仅保既有服务/动画夹具的本地查看索引，不再打开可见详情；正式UMG目录预览归Screen。 */
	void InspectWeapon(int32 CatalogIndex);
	/** 返回兼容查看索引，INDEX_NONE为未设置；不再决定CommonUI/Canvas模态显示。 */
	int32 GetInspectedWeapon() const;
	/** 清除兼容查看索引，保留武器页与终端输入锁；正式界面没有二级说明框。 */
	void CloseWeaponTip();
	/** 兼容旧调用者的明确装备意图；转同一目录装备入口，权威服务核验权限/范围/生命/阶段。 */
	void EquipInspectedWeapon();
    /** CatalogIndex为可信四枪目录索引，1..3主枪；武器库明确装配动作直接走原EquipWeapon权威请求，不打开详情或改本地装备/存档。 */
    void EquipCatalogWeapon(int32 CatalogIndex);
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
    /** 终端研究页查询；仅本地UI，不持业务钱包或随机种子。 */
    bool IsGambleMenuOpen() const;
    /** 读取本机设备偏好，默认保留完整滚动动画。 */
    bool IsGambleAnimationSkipped() const;
    /** 本地勾选热区；等待交易或确认模态时拒绝，滚动中勾选立即停靠已提交结果。 */
    void ToggleGambleAnimation();
    /** 当前查看的报价索引，INDEX_NONE表示尚未选项；只用于本地详情。 */
    int32 GetInspectedGamble() const;
    /** 当前奖池详情页，访问时钳制至最新报价；不改变抽取顺序/概率。 */
    int32 GetGambleRewardPage() const;
    /** Direction只接受-1或1；仅非模态奖池详情页响应翻页。 */
    void ChangeGambleRewardPage(int32 Direction);
    /** 是否持有待确认的冻结费用；HUD据此阻断底层热区。 */
    bool IsGambleConfirmOpen() const;
    /** 是否等待当前可靠交易回执；等待期间拒绝重复提交。 */
    bool IsGambleWaiting() const;
    /** 本帧借用只读卡片/位移，HUD不得修改停靠结果或发奖。 */
    const FDemoGambleReel& GetGambleReel() const;
    /** 已停靠后的返回列表；滚动中拒绝，关页由统一生命周期撤销。 */
    void DismissGambleReveal();
    /** HUD每帧读取当前结果的续抽限制；空串可用，余额/奖池只读最新权威报价。 */
    FString GetContinueGambleBlockReason() const;
    /** 停稳结果页按已显示费用直接续抽当前项目；重取序号/奖池，等待或滚动时拒绝。 */
    void ContinueGamble();
    /** 合法升级终端中打开研究页，撤销旧确认。 */
    void OpenGambleMenu();
    /** Choice=0..11服务器报价索引，只查看，不直接扣币。 */
    void InspectGamble(int32 Choice);
    /** 复制当前报价与序号后打开确认，尚不提交。 */
    void RequestGamble();
    /** bConfirm为显式是/否；先消费意图，再经拥有者PC请求权威事务。 */
    void ConfirmGamble(bool bConfirm);
    /** 消费Casino.前缀热区；格式与页面/等待/模态都校验，未知项不穿透其他菜单。 */
    bool HandleCasinoClick(FName HitBox);
    /** 仅播放已提交终局；跳过设置立即完成，所有入口共用防重入条件。 */
    bool IsCasinoRolling() const;
    /** 当前下注草稿，只供本帧HUD读取，不包含任何开奖结果。 */
    const FDemoCasinoIntent& GetCasinoDraft() const;
    /** 本机按权威公开配置计算预览；提交仍由服务端校验，错误禁用。 */
    FDemoCasinoQuote GetCasinoQuote() const;
    /** 已保存的结果，UI只负责揭晓；关闭页面清空展示不撤销业务。 */
    const FDemoCasinoResult& GetCasinoResult() const;
    /** 真实时间揭晓进度，暂停World仍推进；跳过动画时直接返回完成时间。 */
    double GetCasinoRevealTime() const;
    /** 打开UMG金额输入意图，必须本地安全区、无模态/未结箱/等待回执；CommonUI协调器创建控件。 */
    void OpenCasinoBetInput();
    /** 仅关闭输入层；旅行/终端消失/取消时调用，不提交草稿或下注。 */
    void CloseCasinoBetInput();
    /** 输入层存活标志，Canvas/快捷键用来阻止旧页面穿透。 */
    bool IsCasinoBetInputOpen() const;
    /** Text为原生控件当前文本；重新检查页面有效性与整数范围，成功修改草稿并关输入层，Error给就地提示。 */
    bool CommitCasinoBetText(const FString& Text,FString& Error);
private:
    bool bCasinoDevicePage=false; // 本机页面身份；授权仍由可靠快照Token及服务器租约，退出清理不删未结局。
    /** PC真实时间更新中尝试一次性提示；仅本地大厅/安全区空闲页，依赖本机设置，不创建RPC或存档。 */
    void TryShowFirstRunGuide();
    bool bFirstRunGuideDismissed = false; // 本PC对本机已读状态的缓存；新PC仍查询设备持久标记，旅行不把未读当已读。
    /** 检查当前页面是否允许编辑金额；不依赖输入层自身是否已经打开。 */
    bool CanEditCasinoBet() const;
    bool bCasinoBetInputOpen=false; // 本机金额输入意图真值；Widget由CommonUI栈拥有，关闭/旅行清除，不持有经济状态。
    /** 报价合法后冻结参数/序号并打开确认，未执行扣款。 */
    void RequestCasino();
    /** 冻结意图经拥有者命令提交；必须先置等待，兼容Listen同步回执。 */
    void SubmitCasino(const FDemoCasinoIntent& Intent);
    /** 原页面确认和物理拉杆共同消费冻结PendingCasino；调用者先验证自己的页面/Operate。 */
    void ConfirmCasinoWager(bool bConfirm);
    /** 共用十进制ParseStake/最低值验证；调用者先检查可编辑上下文，失败不改原Stake。 */
    bool CommitCasinoDraftText(const FString& Text,FString& Error);
    FString CasinoDeviceAmountText; // 本机物理键盘草稿，空串允许编辑但不可提交，金额无小数。
    bool bCasinoFirstDigitReplacesAmount=true; // 进入/确认后的预填金额首键替换，后续数字追加；仅本人编辑状态，不复制/存档。
    FDemoCasinoIntent CasinoDraft; // 本机可编辑下注，关闭确认前不可改变。
    FDemoCasinoIntent PendingCasino; // 确认冻结完整参数，与PendingGambleSerial同生命周期。
    FDemoCasinoResult CasinoReveal; // 仅公开已保存结果，不含未揭雷位。
    double CasinoRevealStarted=0; // FPlatformTime真实时间，UI动画不依赖World Tick。
    int32 StatusPage=0; // 本地状态面板天赋页，打开重置；不复制/写档。
    /** 关闭/旅行/新终端时清理待确认、等待和动画；已保存的结果不受影响。 */
    void ResetGamblePresentation();
    /** Result为本人可靠命令回执；只接受当前等待的项目和下一账本序号。 */
    void ReceiveGambleResult(const FDemoCommandResult& Result);
    FDemoGambleReel GambleReel; // 生命周期严格归本MenuFlow，真实时间在PC暂停Tick驱动。
    int32 WaitingGambleChoice=INDEX_NONE; // 提交前冻结，晚到回执不可借用新查看项启动动画。
    int32 InspectedGamble=INDEX_NONE; // 当前查看项，关闭/新交互不重用确认。
    int32 GambleRewardPage=0; // 本地每页一项的奖池预览，不进入服务器账本。
    int32 PendingGamblePrice=INDEX_NONE; // 待确认币价，-1无确认。
    int32 PendingGambleSerial=0; // 与币价一起冻结的账本序号。
    bool bGambleWaiting=false; // 一次本地请求等待可靠回执，收到成功/失败都释放。

    /** 当前Owner必须是DemoPC；仅业务执行期间借用，不跨控制器保留。 */
    ADemoPlayerController* GetController() const;
    /** bEnabled为菜单占用输入意图，实际映射/光标/锁定统一由PC执行。 */
    void SetMenuInput(bool bEnabled);
    TWeakObjectPtr<ADemoInteractable> MenuTerminal; // 打开升级菜单时的真实终端，重建后旧交易失效。
    uint64 MenuTerminalGeneration = 0; // 打开时版本，0表示没有交易会话。
    uint64 NextTerminalGeneration = 0; // 出发确认的终端版本，不能借新终端执行旧请求。
    bool bStopped = false; // PC卸载后的退出结果和确认请求不得重新激活流程。
    int32 LastObservedPhase = INDEX_NONE; // 本地已处理公开阶段，数值变更不会反复重置当前菜单。
    int32 LastObservedLevel = INDEX_NONE; // 同阶段切关时仍撤销旧终端意图。
    bool bApprovedInteractionPending = false; // 交互回执先于GameState阶段复制时延迟打开，随后仅消费最新权威快照。

	/** 暂停时每帧调用；延迟本地保存使弹窗先绘制，轮询弱UObject网络结果。 */
	void UpdateQuitSave();
	/** 写永久档和安全阶段检查点；Combat保持现有出发检查点，不保存半场战斗。 */
	bool SaveBeforeQuit();
	/** 保存成功提示结束后唯一退出入口；不在HTTP回调或GI析构内等待网络。 */
	void FinishQuit();
    UPROPERTY() TObjectPtr<class UDemoExitSaveFlow> ExitFlow; // 本MenuFlow唯一退出等待对象；无PC裸指针或HTTP委托。
	EDemoMenuPage QuitBackPage = EDemoMenuPage::None; // 取消退出恢复大厅None或已有Pause。
	/** 当前阶段+上下层模态状态共同决定映射、光标与移动锁定。 */
	void RefreshMenuInput();
	/** 读取Engine本地设置到编辑草稿；不在打开界面时写磁盘。 */
	void BeginSettings();
	// 顶层模态状态只属于本地控制器，旅行/重生不持久化。
	EDemoMenuPage MenuPage = EDemoMenuPage::None;
	EDemoMenuPage SettingsBackPage = EDemoMenuPage::None; // 设置返回目的页：大厅None或局内Pause。
	bool bPauseMenuActive = false; // 本控制器是否拥有暂停，关闭顶层Pause时解除。
	// 新存档确认的栏位，关闭后清除，避免旧按钮覆盖其他槽。
	int32 PendingSaveSlot = INDEX_NONE;
    UPROPERTY() TObjectPtr<class UDemoSettingsFlow> SettingsFlow; // 此MenuFlow独占的设备草稿，不拥有页面或输入映射。
	// 终端本地页签，不复制；打开/关闭/终局重置，奖励页始终false。
	EDemoTerminalPage TerminalPage = EDemoTerminalPage::Stats; // 互斥页签取代两个可能冲突的bool。
	int32 InspectedAmmo = 0; // 查看项0..3，与实际装配分离。
	int32 PendingAmmoPrice = INDEX_NONE; // 待确认报价，切页/关闭清除，不持久化。
	// 当前详情Item的目录索引0..3；不持有武器Actor，关闭或页面变化重置。
	int32 InspectedWeapon = INDEX_NONE; // 仅保旧服务/动画夹具的查看索引，不复制/保存；正式武器库不再设置或显示二级模态。
	/** Terminal 仅本次借用；验证拥有者、权威状态、当前入口及 250cm 距离，不修改世界。 */
	bool CanRequestNextLevel(const ADemoInteractable* Terminal) const;
	// 本地模态窗口标志；独立于弱引用有效性，终端销毁后仍允许取消恢复输入，不复制。
	bool bNextLevelConfirmationOpen = false;
	// 本次安全区出发是否明确点选难度；关间确认不使用此标志。
	bool bDifficultyChosen = false;
	// World 持有终端；弱引用不阻止关卡切换/Destroy，确认时重新验证。
	TWeakObjectPtr<ADemoInteractable> PendingNextLevelTerminal;
	// 打开时的已完成关卡编号 0..9；INDEX_NONE 表示没有请求，避免使用旧关卡确认。
	int32 PendingCompletedLevel = INDEX_NONE;
	// 菜单状态仅属于拥有此控制器的本地玩家。
	bool bMenuOpen = false;
	bool bRewardMenu = false;
	// 本地菜单反馈文本，失败提示保留直到下次操作/关闭。
    FString MenuMessage;
    int32 RoomPage = 0; // 本地列表页码，不影响OSS结果排序和版本。
    bool bChoosingHostSave = false; // Saves/CreateSave共用布局，但明确区分单机与开房意图。
};
