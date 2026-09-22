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

/** 本地输入与光标应用入口；菜单状态唯一归MenuFlow，公开菜单方法仅为兼容转发。 */
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
    /** 只读当前终端会话，权威服务在每次提交时校验对象和版本。 */
    ADemoInteractable* GetMenuTerminal() const;
    uint64 GetMenuTerminalGeneration() const;
    /** 当前本地菜单组件借用引用，用于专属回归或UI集成。 */
    class UDemoMenuFlowComponent* GetMenuFlow() const;
private:
    friend class UDemoMenuFlowComponent; // 仅菜单服务可请求实际InputMode；不开放输入资源写访问。
    /** 三个快捷键包装，仍绑定在控制器上并记录真实输入。 */
    void SelectFirst();
    void SelectSecond();
    void SelectThird();
    /** bEnabled指定菜单输入；统一应用映射、光标及锁定，避免叠加输入锁。 */
    void SetMenuInput(bool bEnabled);
    UPROPERTY(VisibleAnywhere, Category="Demo|Systems") TObjectPtr<class UDemoMenuFlowComponent> MenuFlow; // 本PC唯一页面状态，不跨旅行保留。
    UPROPERTY() TObjectPtr<UInputMappingContext> MappingContext; // 模板移动/视角映射，本地PC管理。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Input") TSoftObjectPtr<UInputMappingContext> CombatMappingAsset; // 武器映射资源路径，保留旧蓝图字段与Cook依赖。
    UPROPERTY() TObjectPtr<UInputMappingContext> CombatMappingContext; // BeginPlay加载后强引用，菜单仅启停本上下文。
};
