// 职责：本人枪墙焦点/本地高亮/三行UMG与Owner RPC适配。使用：PC默认复制组件，E仅消费一个目标；不预测扣款或拥有权，不加载任意客户端资源路径。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/HubShops/DemoHubShopTypes.h"
#include "Player/DemoPlayerCommands.h"
#include "DemoShopFocusComponent.generated.h"
class ADemoPlayerController;
class ADemoCharacter;
class APawn;
class UPrimitiveComponent;
class ADemoGunDisplay;
class ADemoCasinoDevice;
class UDemoHubShopDefinition;
class UMaterialInterface;
class UCameraComponent;

UCLASS()
class FPSDEMO_API UDemoShopFocusComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 本地逐帧只更新浮签位置，注册表候选按配置20Hz扫描；远端不建Widget。 */
    UDemoShopFocusComponent();
    /** 本地PC资源初始化；不运行交易或保存。 */
    virtual void BeginPlay() override;
    /** DeltaTime为World游戏秒；模态/阶段/Avatar变化撤焦，暂停Tick只清理，不推进扫描/报价/心跳。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;
    /** Owner卸载撤销本机Widget/高亮/输入映射，不销毁共享展示或改变未结娱乐局。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** PC SetupInput调用一次；有合法作者Action时返回true并接入唯一Started，缺资源保旧键回退。 */
    bool InstallInput(class UInputComponent* Input);
    /** Character设置旧输入时查询；true表示PC EnhancedInput已接管E，不再重复硬绑定。 */
    bool HasEnhancedInteraction() const;
    /** E入口优先消费墙枪/设备；无目标返回false才走原升级/出发终端。 */
    bool TryConsumeInteraction();
    /** 实际作者Mapping中的按键显示名，不由Widget硬写E/F。 */
    FString InteractionKeyLabel() const;
    /** 本机遮罩工厂：ViewerPawn拥有Source的仅CustomDepth副本，Stencil253仅由该私有副本使用。
     * 调用方负责只在本人合法瞄准时创建并销毁；不改Source材质/Stencil/交易。后处理只取二值整体外沿。
     * 两对象须有效且同World；返回无碰撞/非复制/不画主通道的Primitive，失败返回空。
     */
    UFUNCTION(BlueprintCallable,Category="Hub Shop|Visual")
    static UPrimitiveComponent* CreateLocalGunSilhouetteMask(APawn* ViewerPawn,UPrimitiveComponent* Source);
private:
    /** Started离散回调，当前Pawn有效才交原Character统一路由，不带任意目标玩家。 */
    void OnInteract();
    /** PC权威当前有效活动槽颁发会话Epoch；更换StorageKey/World即换代，不当持久权身份。 */
    FGuid ResolveSlotEpoch();
    /** Server仅接受稳定展示ID/World代数，距角/LOS和当前Pawn由Shop服务重验。 */
    UFUNCTION(Server,Reliable) void ServerQueryGun(FName DisplayId, uint32 WorldEpoch);
    /** Client只消费与当前焦点一致的服务端投影，迟到结果不复活旧高亮。 */
    UFUNCTION(Client,Reliable) void ClientGunQuote(const FDemoHubGunQuote& Quote);
    /** 可靠本人购买意图，不上传钱包/枪权/资源路径。 */
    UFUNCTION(Server,Reliable) void ServerPurchaseGun(const FDemoHubGunIntent& Intent);
    /** 成功只来自完整本地Store；本机不把Actor复制未到视为再次扣款理由。 */
    UFUNCTION(Client,Reliable) void ClientGunResult(const FDemoHubGunResult& Result);
    /** 设备入口只提交ID/World，权威租约由同World Shop服务所有。 */
    UFUNCTION(Server,Reliable) void ServerOpenDevice(FName DeviceId, uint32 WorldEpoch);
    /** 服务批准页面后携同一快照交PC更新MenuFlow，不冒用旧Shop终端。 */
    UFUNCTION(Client,Reliable) void ClientDeviceOpened(const FDemoCommandResult& Result, const struct FDemoPlayerMenuSnapshot& Snapshot);
    /** 仅租约存活页面每配置2秒保活，不刷新赌博随机/UTC时限。 */
    UFUNCTION(Server,Reliable) void ServerDeviceHeartbeat(FName DeviceId, const FGuid& Token, uint32 WorldEpoch);
    /** 本人关闭或迟到批准已不对应当前焦点时精确撤销旧Token，不能误关新租约。 */
    UFUNCTION(Server,Reliable) void ServerCloseDevice(const FGuid& Token);
    /** 更新本地唯一交互目标；清理旧UMG，失焦同时撤轮廓，显示对象弱引用不跨World保留。 */
    void SetFocus(AActor* Target);
    /** 每次准星注册查询命中变化时替换本人轮廓；空值/失效目标立即清理，不继承4/6度软焦点。 */
    void UpdateGunHighlight(ADemoGunDisplay* Display);
    /** 建原本人253遮罩后交PC唯一LocalOutline持有捕获/合成；本组件不再直接增删相机PP或改共享枪。 */
    void BuildHighlight(ADemoGunDisplay* Display);
    /** 每帧投影当前InfoAnchor并DPI换侧避准星；不扫描World/读盘/发交易。 */
    void UpdateCard();
    /** 只借用此组件Owner PC，不能读取Player0代表队友。 */
    ADemoPlayerController* PC() const;
    /** 只读已载目录的本人提示寿命，目录不可用时保守回退2秒，不加载资产。 */
    float NoticeSeconds() const;
    UPROPERTY(Transient) TObjectPtr<class UDemoShopFocusCard> Card; // 本地原生UMG，卸载/失焦撤销，不消费鼠标。
    UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> Highlight; // 本人Pawn拥有的OnlyOwnerSee私有Stencil253遮罩，MainPass/普通Depth不绘制。
    UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> SilhouetteBlendable; // 借PC LocalOutline本请求的Composite MID，仅供诊断/缓存；PP生命周期完全归渲染组件。
    TWeakObjectPtr<UCameraComponent> OutlineCamera; // 本请求的实际Avatar Camera，换Pawn/失效精确撤自己的请求。
    UPROPERTY(Transient) TObjectPtr<class UMaterialInterface> HighlightMaterial; // 本机首次可信DA加载的主Composite；不每帧加载或回退旧pass。
    UPROPERTY(Transient) TObjectPtr<class UMaterialInterface> HighlightMaskMaterial; // 与Composite同时一次加载的私有Capture Mask PP，原资产只读。
    UPROPERTY(Transient) TObjectPtr<class UInputAction> InteractAction; // PC拥有者输入资源，仅本机。
    UPROPERTY(Transient) TObjectPtr<class UInputMappingContext> Mapping; // 本机安装/撤销，无权威语义。
    TWeakObjectPtr<AActor> Focus; // 当前唯一注册展示/设备，卸载即无效。
    TWeakObjectPtr<AActor> Candidate; // 需连续稳定0.15秒的新目标。
    TWeakObjectPtr<APawn> FocusAvatar; // 换Pawn立即撤旧焦点/候选，不能继承旧UI授权。
    TWeakObjectPtr<ADemoGunDisplay> HighlightedDisplay; // 当前准星查询实际命中的注册枪；独立于带滞回的交互卡。
    TWeakObjectPtr<UPrimitiveComponent> HighlightSource; // 同枪组件重建后重制轮廓，源被销毁不留悬空副本。
    FDemoHubGunQuote CurrentQuote; // 仅拥有者最近报价，不能作为服务器扣款真值。
    FGuid SlotEpoch; // 服务器PC本World当前槽会话版本，不持久化。
    FString SlotStorageKey; // 当前服务器已绑定的持久栏身份，只用于检测换槽。
    TWeakObjectPtr<APawn> EpochAvatar; // 服务器换Avatar同样撤旧SlotEpoch，不让旧Pawn意图由新Pawn执行。
    FString PendingOperation; // 本机单笔等待ID，回复前不发第二笔购买。
    FString Notice; // 本人短提示，不公开钱包/私有玩法布局。
    double NoticeUntil = 0.; // 本地World秒截止。
    double CandidateSince = 0.; // 本地候选开始World游戏秒。
    double NextScanAt = 0.; // 有界20Hz注册近邻扫描，不逐帧遍历World。
    double NextQuoteAt = 0.; // 同焦点最多1Hz刷新报价，切换即查询。
    double NextHeartbeatAt = 0.; // 设备页2秒游戏时间保活。
    double LastServerQueryAt = -1.; // 服务器最短查询间隔，防恶意可靠RPC刷屏。
    bool bInputInstalled = false; // 唯一EnhancedInput绑定，Pawn重生不重复安装。
    bool bOpeningDevice = false; // 设备批准回执前不重复提交，迟到结果须再核本机焦点。
    bool bHighlightLoadAttempted = false; // 可信目录Composite/Mask同时仅尝试一次，缺资产不能逐帧读盘重试。
};
