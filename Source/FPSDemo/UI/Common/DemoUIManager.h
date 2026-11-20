// 职责：将单一MenuFlow意图投影到本地CommonUI栈。使用：PC持有，Tick同步；不持有金币或业务结果副本。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoUIManager.generated.h"
class UDemoUIRoot;
class UDemoMenuScreen;
struct FDemoCommandResult;
enum class EDemoGameplayCommand : uint8;
/** 迁移期协调器：菜单/模态是Flow的表现，Canvas与UMG互斥，不提供服务器权限。 */
UCLASS(ClassGroup=(Demo),Config=Game)
class FPSDEMO_API UDemoUIManager : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 默认类可在控制器蓝图配置，专服不创建Widget。 */
    UDemoUIManager();
    /** PC真实时间更新后同步已有模型；无模型变化不重建Widget树。 */
    void Sync();
    /** 当前意图是否属于迁移范围，HUD据此停止旧绘制/热区；不依赖上一帧栈状态。 */
    bool UsesWidgetMenu() const;
    /** 实际输入尚未交还时为true；与意图区分，避免同帧先关后开菜单时旧PC把光标隐藏。 */
    bool OwnsInput() const;
    /** 输入路由只承认当前最上层且实际激活的页面。 */
    bool IsTopScreen(const UDemoMenuScreen* Screen) const;
    /** 本玩家当前模态优先，否则菜单；无迁移页面返回空。 */
    UDemoMenuScreen* GetActiveScreen() const;
    /** 真实栈数量只读，供诊断和自动化核对生命周期。 */
    int32 GetMenuStackCount() const;
    int32 GetModalStackCount() const;
    /** 从已验证顶层按钮提交白名单意图，不直接操作交易组件。 */
    void SubmitAction(FName Action);
    /** Esc/Tab/CommonUI返回按明确模态→详情→终端顺序撤销，禁止直接隐藏遗留业务会话。 */
    void Back();
    /** 拥有者可靠回执验证RequestId后调用；仅缓存短期展示文案/音效，绝不修改交易结果或保存状态。 */
    void PresentCommandResult(EDemoGameplayCommand Command,const FDemoCommandResult& Result);
    /** 当前单调时间内可见的反馈，空表示无；HUD/Root借用只读，不传播到其它玩家。 */
    FString GetFeedbackText() const;
    /** 当前通知是否拒绝/待保存；颜色只负责强调，完整原因仍在文本中。 */
    bool IsFeedbackWarning() const;
    /** 旅行/PC销毁前撤销所有本地Widget；幂等，不取消已保存业务结果。 */
    void Shutdown();
#if WITH_EDITOR
    /** 测试兼容入口只触发当前实际UMG按钮，不能调用不存在或被模态遮挡的旧Canvas操作。 */
    bool PressActionForTest(FName Action);
#endif
protected:
    /** PC卸载可能再次调用，重复释放安全。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** 返回当前模态种类：0无、1下注、2金额、3弹药、4武器、5新档、6返回安全区、7显示设置、8解锁提示。 */
    int32 DesiredModal() const;
    /** 页面层清空后把输入明确交还旧Canvas/游戏；CommonUI默认配置不自动覆盖未迁移页面。 */
    void RestoreLegacyInput();
    /** 首次本地菜单加载轻量页面类；错误资产回退原生控件并警告，专服不触发资源加载。 */
    void ResolvePageClasses();
    UPROPERTY(Config,EditDefaultsOnly,Category="Demo|UI") TSoftClassPtr<UDemoUIRoot> RootClass; // 可替换Widget蓝图，必须保留两具名栈。
    UPROPERTY(Config,EditDefaultsOnly,Category="Demo|UI") TSoftClassPtr<UDemoMenuScreen> TerminalClass; // 原生终端默认页/蓝图派生。
    UPROPERTY(Config,EditDefaultsOnly,Category="Demo|UI") TSoftClassPtr<UDemoMenuScreen> CasinoClass; // 七玩法详情默认页/蓝图派生。
    UPROPERTY(Config,EditDefaultsOnly,Category="Demo|UI") TSoftClassPtr<UDemoMenuScreen> ModalClass; // 统一确认和金额输入页，必须派生DemoConfirmScreen。
    UPROPERTY(Config,EditDefaultsOnly,Category="Demo|UI") TSoftClassPtr<UDemoMenuScreen> LobbyClass; // 大厅根页，可替换外壳。
    UPROPERTY(Config,EditDefaultsOnly,Category="Demo|UI") TSoftClassPtr<UDemoMenuScreen> SessionClass; // 存档/暂停/退出/房间共用会话页。
    UPROPERTY(Config,EditDefaultsOnly,Category="Demo|UI") TSoftClassPtr<UDemoMenuScreen> SettingsClass; // 原设备设置草稿的表现。
    UPROPERTY(Config,EditDefaultsOnly,Category="Demo|UI") TSoftClassPtr<UDemoMenuScreen> StatusClass; // Tab只读属性页。
    UPROPERTY(Transient) TSubclassOf<UDemoUIRoot> LoadedRootClass; // 本PC菜单期间强引用，避免GC后重复加载。
    UPROPERTY(Transient) TSubclassOf<UDemoMenuScreen> LoadedTerminalClass; // 已验证父类的本地页面类。
    UPROPERTY(Transient) TSubclassOf<UDemoMenuScreen> LoadedCasinoClass; // 已验证父类的本地页面类。
    UPROPERTY(Transient) TSubclassOf<UDemoMenuScreen> LoadedModalClass; // 已验证确认页父类，保证SetKind和金额接口。
    UPROPERTY(Transient) TSubclassOf<UDemoMenuScreen> LoadedLobbyClass; // 经父类校验的本地强引用。
    UPROPERTY(Transient) TSubclassOf<UDemoMenuScreen> LoadedSessionClass; // 含房间版本化点击接口。
    UPROPERTY(Transient) TSubclassOf<UDemoMenuScreen> LoadedSettingsClass; // 设置页生命周期同本PC。
    UPROPERTY(Transient) TSubclassOf<UDemoMenuScreen> LoadedStatusClass; // 属性页生命周期同本PC。
    UPROPERTY(Transient) TObjectPtr<UDemoUIRoot> Root; // 此PC/LocalPlayer独占，不跨旅行持有。
    bool bStopped=false; // Shutdown后迟到回执不能重建页面。
    bool bOwnedInput=false; // 只在真实交接边沿恢复旧输入。
    bool bSyncing=false; // 防止同步构建回调重入。
    int32 ActiveModal=0; // 仅栈实例类型标记，内容/确认真值始终读Flow。
    double NextModelRefresh=0; // 单调真实秒；无本地操作时最多10Hz比较模型，动画仍由Widget逐帧更新。
    FString FeedbackText; // 最近业务回执的有限展示文案，不当作业务结果副本/钱包，不写存档。
    double FeedbackUntil=0.; // 本机单调截止秒；暂停继续显示到期，不依赖World Timer。
    bool bFeedbackWarning=false; // 拒绝或已应用待落盘，不能播成功音。
    bool bFeedbackAwaitedPreparation=false; // StartLevel在真实准备门禁中的短回执；公开Combat或后续阶段且准备已结束时不重现过期通知，不修改Result/失败/门禁。
};
