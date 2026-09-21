#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Game/DemoCombatConfig.h"
#include "DemoPlayerController.generated.h"

class UInputMappingContext;
class ADemoInteractable;

/** 终端互斥页签，不复制，只有安全区提供弹药页。 */
enum class EDemoTerminalPage : uint8 { Stats, Weapons, Ammo };

/** 置顶模态页；暂停可覆盖奖励/终端，关闭后恢复下层而不丢失必须领取的奖励。 */
UENUM()
enum class EDemoMenuPage : uint8 { None, Saves, CreateSave, Pause, ReturnHub, Settings, QuitSaving, EndlessUnlock }; // 新提示追加，旧菜单值保持不变。

/** 保存退出的本地UI阶段；失败保留窗口，成功展示片刻后才结束游戏。 */
enum class EDemoQuitState : uint8 { Idle, SavingLocal, WaitingCloud, Saved, Failed };

/** 本地菜单输入与光标状态；购买/清关奖励的权威验证留给 GameMode。 */
UCLASS()
class FPSDEMO_API ADemoPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	/** 显式加载模板增强输入映射，避免依赖旧蓝图 Controller 默认值。 */
	ADemoPlayerController();
	/** 添加本地映射并进入大厅 GameAndUI 模式，冻结角色等待开始。 */
	virtual void BeginPlay() override;
	/** EndPlayReason为卸载原因，移除本控制器加入的映射，不遗留跨World输入。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** DeltaTime为引擎帧秒数；暂停仍推进退出保存与显示模式确认的真实时间倒计时。 */
	virtual void PlayerTick(float DeltaTime) override;
	/** Esc只在局内暂停；子页返回上层，奖励/终端窗口不因暂停而跳过。 */
	void EscapePressed();
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
	/** Setting=0分辨率/1画质/2鼠标/3窗口，Direction为+1/-1；仅修改待应用值。 */
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
	/** Setting=0..3；返回本地设置草稿的格式化文本，无写盘副作用。 */
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
private:
	/** 暂停时每帧调用；延迟本地保存使弹窗先绘制，轮询弱UObject网络结果。 */
	void UpdateQuitSave();
	/** 写永久档和安全阶段检查点；Combat保持现有出发检查点，不保存半场战斗。 */
	bool SaveBeforeQuit();
	/** 保存成功提示结束后唯一退出入口；不在HTTP回调或GI析构内等待网络。 */
	void FinishQuit();
	EDemoQuitState QuitState = EDemoQuitState::Idle; // 仅本地控制器状态，不复制、不保存。
	EDemoMenuPage QuitBackPage = EDemoMenuPage::None; // 取消退出恢复大厅None或已有Pause。
	bool bQuitLocalSaved = false; // 两种本地存档均写入成功后才允许离线退出。
	double QuitNextActionTime = 0; // 单调秒数；首帧绘制延迟和成功提示1秒，不受暂停影响。
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
	// 设置草稿在应用前不影响引擎；分辨率列表包含常见尺寸和当前自定义尺寸。
	TArray<FIntPoint> Resolutions;
	int32 ResolutionChoice = 0; // Resolutions中的草稿索引，打开时同步当前Engine值。
	int32 QualityChoice = 3; // 0..3总体画质，-1保留引擎现有自定义组合。
	float SensitivityChoice = 1.f; // 0.1..3.0鼠标输入倍率，默认1保持模板手感。
	int32 WindowChoice = 2; // EWindowMode 0全屏/1无边框/2窗口，不跨World持有。
	// 显示回退快照与真实时间deadline；<=0表示无需确认，暂停不会冻结倒计时。
	FIntPoint PreviousResolution;
	int32 PreviousWindowMode = 2; // 应用前EWindowMode值，取消/超时恢复。
	double DisplayConfirmDeadline = 0; // FPlatformTime绝对秒数，0表示没有待确认显示变更。
	// 终端本地页签，不复制；打开/关闭/终局重置，奖励页始终false。
	EDemoTerminalPage TerminalPage = EDemoTerminalPage::Stats; // 互斥页签取代两个可能冲突的bool。
	int32 InspectedAmmo = 0; // 查看项0..3，与实际装配分离。
	int32 PendingAmmoPrice = INDEX_NONE; // 待确认报价，切页/关闭清除，不持久化。
	// 当前详情Item的目录索引0..3；不持有武器Actor，关闭或页面变化重置。
	int32 InspectedWeapon = INDEX_NONE;
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
	/** 三个快捷键包装，记录真实输入调用。 */
	void SelectFirst();
	void SelectSecond();
	void SelectThird();
	/** bEnabled 指定菜单输入；统一设置光标和移动/视角锁定，避免锁计数叠加。 */
	void SetMenuInput(bool bEnabled);
	// 本地增强输入上下文，默认子资源硬引用确保打包收集。
	UPROPERTY() TObjectPtr<UInputMappingContext> MappingContext;
	// 战斗武器映射，Cooker沿软引用收集六个InputAction；菜单期间移除该上下文。
	UPROPERTY(EditDefaultsOnly, Category="Demo|Input") TSoftObjectPtr<UInputMappingContext> CombatMappingAsset;
	// BeginPlay加载后的强引用，SetMenuInput只启停它，不ClearAllMappings破坏其他系统。
	UPROPERTY() TObjectPtr<UInputMappingContext> CombatMappingContext;
	// 菜单状态仅属于拥有此控制器的本地玩家。
	bool bMenuOpen = false;
	bool bRewardMenu = false;
	// 本地菜单反馈文本，失败提示保留直到下次操作/关闭。
	FString MenuMessage;
};
