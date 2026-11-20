// 职责：本人PC的实体赌场输入/细焦点/单次主键归属与租约RPC。使用现IA_Fire转发，数字只改本人草稿，slots.pull复用原权威保存事务。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlTypes.h"
#include "Player/DemoPlayerCommands.h"
#include "DemoCasinoDeviceInputComponent.generated.h"
class ADemoPlayerController;
class ADemoCasinoDevice;
class UPrimitiveComponent;
struct FHitResult; // 私有即时射线输出借用，只在本游戏线程调用栈存活。

/** 单次主键Started到Completed/Canceled的本机归属；非复制，不作为服务器玩法权限。 */
enum class EDemoPrimaryPressOwner : uint8 { None, Weapon, Device, Blocked };

UCLASS()
class FPSDEMO_API UDemoCasinoDeviceInputComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 建立本人RPC通道；仅本地PC执行输入/心跳Tick，服务端单独处理可靠意图。 */
    UDemoCasinoDeviceInputComponent();
    /** 只给本地PC绑定应用失焦清理；弱UObject委托在EndPlay解除，不轮询鼠标或增加另一输入Action。 */
    virtual void BeginPlay() override;
    /** 既有IA_Fire Started调用；true表示设备/阻塞已消费，false才交原武器入口。每次按下冻结归属。 */
    bool TryRoutePrimaryPressed();
    /** 同Action Completed/Canceled调用；true表示不需停武器，false才交原StopFiring，防数字逐键RPC。 */
    bool TryRoutePrimaryReleased();
    /** Owner服务器批准后进入独立Operate，不打开/隐藏全屏菜单，不改变相机。 */
    bool BeginOperate(const FDemoPlayerMenuSnapshot& Snapshot);
    /** 本地实际交互键入口；Operate时只处理当前首次命中的控件，未命中仍消费防穿透旧交互。 */
    bool TryConsumeInteraction();
    /** 当前Avatar/设备/Token仍属于此本地Operate；不代表服务器授权免复核。 */
    bool IsOperating() const;
    /** 同一World当前设备，只供本地显示和实际夹具取查询点，不返回钱包或服务器令牌。 */
    ADemoCasinoDevice* GetCurrentDevice() const;
    /** Esc/主动退出/生命周期撤Operate；bNotifyServer=false只用于已失效的服务器租约回执。 */
    void ExitOperation(bool bNotifyServer=true);
    /** MenuFlow冻结草稿后调用；请求只带确认Stake/Rules和当前命中的ControlId，不带客户端结果。 */
    bool SubmitCasinoControl(const FDemoCasinoIntent& Intent,int32 ExpectedSerial);
    /** 本机最后已提交的组件nonce，只读诊断，不授予权威或重发请求。 */
    int32 GetLastSubmittedRequestId() const;
    /** 当前是否等待本组件正式回执，供真实夹具核重复按键。 */
    bool IsWaiting() const;
    /** 当前20Hz细焦点的稳定控件ID，只供本人HUD/有界验收读取，NAME_None无目标。 */
    FName GetFocusedControlId() const;
    /** 当前实际焦点是否可激活；等待/演出中普通键禁用，Exit仍可离开。 */
    bool IsFocusedControlEnabled() const;
    /** 当前焦点的本人短提示，包含动作或禁用原因；不复制私人金额。 */
    const FString& GetFocusedControlPrompt() const;
    /** 本次主键归属的只读稳定名，供输入状态验收；不是操作Token或经济权限。 */
    FName GetPrimaryPressOwner() const;
    /** World游戏秒保活；离开、遮挡、死亡/阶段/Avatar变化清理；不能靠全屏页维持。 */
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTick) override;
    /** 卸载只撤本机层/旧租约，不跨World发经济或保留Pawn。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** 仅借用当前组件Owner PC，不使用Player0。 */
    ADemoPlayerController* PC() const;
    /** 共用E兼容/左键的一次性控件激活，失败也消费Operate输入；不在Released重复触发。 */
    bool ActivateAimedControl();
    /** GT按当前共享眼位做首次阻挡射线，返回可信Binding副本；Hit/Reason供有限诊断，不接受客户端自报坐标。 */
    bool TraceAimedControl(FDemoCasinoControlBinding& Binding,FHitResult& Hit,FString& Reason) const;
    /** 当前本地上下文/等待/演出决定按钮可用性；Reason仅本人短提示，Exit不被等待经济回执锁死。 */
    bool CanActivateControl(const FDemoCasinoControlBinding& Binding,FString& Reason) const;
    /** 每个现20Hz扫描周期刷新一次细焦点；点击另即时复查，不缓存激活权。 */
    void RefreshControlFocus();
    /** 原Profile控件ID/可用状态变更时更新本人高亮与短提示，NAME_None清理；不移动Root Query。 */
    void SetControlFocus(FName ControlId,bool bEnabled,const FString& Prompt);
    /** 为本地可信控件构造短操作提示；Stake只读当前本人草稿，不作为服务器报价。 */
    FString BuildControlPrompt(const FDemoCasinoControlBinding& Binding,bool bEnabled,const FString& Reason) const;
    /** 本次按压转成阻塞并保留释放屏障；退出/失焦/Avatar变化不得把旧held翻译成下注或开火。 */
    void FencePrimaryPress();
    /** Slate应用激活回调；bActive=false时清本PC输入/租约，true不自动下注或重开设备，游戏线程弱绑定。 */
    void OnApplicationActivationChanged(bool bActive);
    /** 本人通道，nonce先消费；服务器可信服务重建意图并复用GM命令事务/Ready逻辑。 */
    UFUNCTION(Server,Reliable) void ServerSubmitDeviceAction(const FDemoCasinoControlRequest& Request);
    /** 独立nonce的Owner回执；不混入旧PC LastClientResultId，不让迟到结果重开机台。 */
    UFUNCTION(Client,Reliable) void ClientDeviceActionResult(int32 RequestId,const FGuid& RequestToken,const FDemoCommandResult& Result,const FDemoPlayerMenuSnapshot& Snapshot);
    /** 只续原本人Token，失效时回当前快照，客户端不能指定另一个玩家。 */
    UFUNCTION(Server,Reliable) void ServerMaintainDevice(FName DeviceId,const FGuid& ExpectedToken,uint32 ExpectedEpoch);
    /** 原Token失效精确结束本地旧Operate，不能清掉后来新Token。 */
    UFUNCTION(Client,Reliable) void ClientDeviceLeaseLost(const FGuid& ExpectedToken);
    /** 只关闭ExpectedToken，迟到关闭不撤后来新租约。 */
    UFUNCTION(Server,Reliable) void ServerReleaseDevice(const FGuid& ExpectedToken);
    TWeakObjectPtr<ADemoCasinoDevice> Device; // 只在本地Operate借用，不复制/持久化Actor。
    TWeakObjectPtr<APawn> Avatar; // 本次Avatar身份，重生立即失效。
    FGuid Token; // 本人已批准租约，永不输出日志或公共复制。
    uint32 Epoch=0; // 当前World注册代数，跨World不保留。
    uint32 Revision=0; // 当前可信查询绑定版本，旧击中不能复用。
    FName ActiveControl; // 本次实际首次命中控件；只在同步输入栈中设置。
    int32 NextRequestId=0; // 本机组件私有单调序号，跨Operate不归零。
    int32 PendingRequestId=0; // 当前单笔Owner回执相关，0无等待。
    int32 LastServerRequestId=0; // 服务端执行前消费的组件序号，不与旧PC请求混用。
    double NextHeartbeatAt=0.; // World游戏秒；暂停不偷耗租约。
    double LastServerActionAt=-100.; // 服务端可靠经济意图限频，避免恶意刷盘/日志。
    bool bOperating=false; // 仅独立世界操作上下文，不使用隐形bMenuOpen授权。
    EDemoPrimaryPressOwner PrimaryOwner=EDemoPrimaryPressOwner::None; // Started冻结，直到本次释放或取消；设备失败也不穿透。
    TWeakObjectPtr<APawn> PrimaryAvatar; // 本次输入所属Avatar，与设备租约Avatar独立，换Pawn不能误停新Pawn枪。
    bool bPrimaryDown=false; // 已见Started且尚未Completed/Canceled，不由设备焦点改变重置。
    bool bAwaitPrimaryRelease=false; // 上下文切换时持键屏障，松开前吞掉重复Started。
    FName FocusedControl=NAME_None; // 20Hz本人细焦点，不用于RPC授权。
    FString FocusedPrompt; // 本人短操作/失败提示，不输出Token。
    bool bFocusedEnabled=false; // 与FocusedControl同帧提交，非复制。
    double NextControlScanAt=0.; // 本World游戏秒，沿Profile ScanHz预算5..30Hz。
    double ContextReadyUntil=0.; // BeginOperate后最多2游戏秒等待客户端WorldEpoch晚到，期间禁动作/心跳。
    FDelegateHandle DeactivateHandle; // 本地Slate应用激活弱绑定，EndPlay核Slate存在后精确移除。
};
