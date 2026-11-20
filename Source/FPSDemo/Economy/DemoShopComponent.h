// 职责：权威终端事务入口。使用：GameMode解析拥有者后调用；业务保存成功才发布，依赖个人账本与检查点委托。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Contracts/DemoGameplayContracts.h"
#include "Interaction/HubShops/DemoHubShopTypes.h"
#include "DemoShopComponent.generated.h"

class ADemoCharacter;
class UDemoTerminalComponent;
struct FDemoPlayerMenuSnapshot;
struct FDemoCasinoControlRequest; // 本人类型化物理输入；可信绑定验证在服务端独立适配源。
class ADemoPlayerController;

/** 仅当前World的权威设备租约；不含第二份钱包/局面，不持有跨World强Pawn指针。 */
struct FDemoCasinoDeviceLease
{
    TWeakObjectPtr<ADemoCharacter> Player; // 当前租用者Avatar，换Pawn不继承。
    FGuid Token; // 本人随机租约，只经Owner RPC投影。
    uint32 WorldEpoch = 0; // 设备注册重建即作废。
    double ExpiresAt = 0.; // World游戏秒，单人暂停不消耗租约。
    int32 Mode = INDEX_NONE; // 可信设备绑定原Mode0..6。
    uint32 ControlsRevision=0; // 服务器开租约时冻结；0原页面，非0必须走真实控件授权，不接受旧无控件Casino请求。
};
// 同步游戏线程检查点提交请求；由RunFlow绑定UObject弱委托，不保存具体GameMode引用。
DECLARE_DELEGATE_RetVal(bool, FDemoSaveCheckpointRequest);

/** 权威终端交易服务；GameState钱包、属性账本、Pawn库存和GI存档保持各自唯一所有者。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoShopComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick；UI查询与提交均走同一组校验，每次提交重新验证。 */
    UDemoShopComponent();
    /** Terminal为同World交互服务，SaveRequest为当前RunFlow保存入口；不在初始化时交易或保存。 */
    void InitializeServices(UDemoTerminalComponent* Terminal, FDemoSaveCheckpointRequest SaveRequest);
    /** 停止旅行后的新交易，并解绑保存委托；不回滚已经提交的业务。 */
    void Shutdown();
    /** EndPlayReason为卸载原因，独立撤销依赖，重复调用安全。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** Choice=0伤害/1生命/2弹匣；按当前安全区/关间币种查询独立价格，非法返回INDEX_NONE。 */
    int32 GetUpgradeCost(int32 Choice) const;
    /** Purchaser为本次借用Pawn，Choice=0..2；检查终端、页面、属性上限及钱包，返回可展示原因。 */
    FString GetPurchaseBlockReason(ADemoCharacter* Purchaser, int32 Choice) const;
    /** Player必须是权威活Pawn且在当前终端250cm内；允许Hub/Intermission，无顶层模态/暂停。 */
    FString GetTerminalBlockReason(ADemoCharacter* Player) const;
    /** Choice/Purchaser为已选商品和购买者，内部重验；只涨该属性价格，成功应用后自动保存。 */
    bool PurchaseUpgrade(int32 Choice, ADemoCharacter* Purchaser);
    /** Choice/Purchaser 来自拥有者请求；返回业务与保存的独立结果。 */
    FDemoCommandResult PurchaseUpgradeResult(int32 Choice, ADemoCharacter* Purchaser);
    /** WeaponId 为目录 ID，Purchaser 为拥有者 Pawn；服务重新验证本人解锁及终端会话。 */
    FDemoCommandResult EquipPrimaryWeapon(FName WeaponId, ADemoCharacter* Purchaser);
    /** 枪墙当前实际报价；SlotEpoch由Owner PC颁发，World/目录/CDO/活动槽每次重验，不创建武器实例。 */
    FDemoHubGunQuote QueryGunOffer(ADemoCharacter* Player, FName DisplayId, uint32 WorldEpoch, const FGuid& SlotEpoch) const;
    /** 独立首购/免费装备事务；CurrentSlotEpoch只来自Owner组件，准备/本地耐久成功后才公布装备与余额，失败原子撤销。 */
    FDemoHubGunResult PurchaseOrEquipGun(ADemoCharacter* Player, const FDemoHubGunIntent& Intent, const FGuid& CurrentSlotEpoch);
    /** 同步事务发布屏障；GM命令入口据此拒绝重入出发/旅行，不能作为跨线程互斥锁。 */
    bool IsTransactionInProgress() const;
    /** Player为当前PC的Pawn，枪墙逻辑Hub/活动槽/生命/准备门禁，与旧终端会话独立。 */
    FString GetHubShopBlockReason(ADemoCharacter* Player,FName* OutCode=nullptr) const;
    /** Player/Index 为本人及弹药目录项；保存失败回滚装配，不保留隐式成功。 */
    FDemoCommandResult EquipAmmo(ADemoCharacter* Player, int32 Index);
    /** Player/Choice 为个人报价查询；不读取全队共享钱包。 */
    int32 GetPlayerUpgradeCost(const ADemoCharacter* Player, int32 Choice) const;
    /** Player为本次弹药购买/装配Owner；额外要求安全区弹药页和有效存档槽。 */
    FString GetAmmoBlockReason(ADemoCharacter* Player) const;
    /** Index=0..3，QuotedCost为确认报价；同步保存金币与解锁，失败回滚，OutMessage反馈可见原因。 */
    bool PurchaseAmmo(ADemoCharacter* Player, int32 Index, int32 QuotedCost, FString& OutMessage);
    /** Player为本人，Choice=0..11白名单策略，ExpectedPrice/Serial来自确认报价；保存失败整笔回滚。 */
    FDemoCommandResult Gamble(ADemoCharacter* Player, int32 Choice, int32 ExpectedPrice, int32 Serial);
    /** Intent为七种金币玩法，Serial为本人确认版本；保存失败回滚。物理租约必须处于可信控件执行作用域，原页面保持兼容。 */
    FDemoCommandResult Casino(ADemoCharacter* Player,const FDemoCasinoIntent& Intent,int32 Serial,FName DeviceId=NAME_None,const FGuid& DeviceToken=FGuid(),uint32 WorldEpoch=0);
    /** 仅权威Owner输入组件调用；从注册ControlId/真实命中推导Mode/Operation，再沿GM命令执行以保Ready/保存行为。 */
    FDemoCommandResult ExecuteCasinoControl(ADemoPlayerController* Controller,const FDemoCasinoControlRequest& Request);
    /** 只打开合法实体设备对应旧页面；有未结局时仅允许同Mode恢复，一台一人且不扣款。 */
    FDemoCommandResult OpenCasinoDevice(ADemoCharacter* Player,FName DeviceId,uint32 WorldEpoch);
    /** 退出/换交互撤租约，不删除Mine/Arcade保存局；ExpectedToken有效时只撤同一次租约。 */
    void CloseCasinoDevice(ADemoCharacter* Player,const FGuid& ExpectedToken=FGuid());
    /** 校验现Owner租约/距角LOS并保活；不重抽随机、不延长靶场UTC时限。 */
    bool RenewCasinoDevice(ADemoCharacter* Player,FName DeviceId,const FGuid& Token,uint32 WorldEpoch);
    /** 原Casino事务独立权限入口；Mode必须等于设备绑定，不能上传另种玩法或借旧终端。 */
    FString GetCasinoDeviceBlockReason(ADemoCharacter* Player,FName DeviceId,const FGuid& Token,uint32 WorldEpoch,int32 Mode) const;
    /** 向本人菜单投影当前有效租约；没有会话保持旧终端投影原样。 */
    void ProjectCasinoDevice(ADemoCharacter* Player,FDemoPlayerMenuSnapshot& View) const;
    /** 服务器当前Avatar是否持有未过期Physical租约；仅拒绝动作的门，不授交互/伤害权限，不依赖客户端bOperating。 */
    bool IsPhysicalCasinoOperator(const ADemoCharacter* Player) const;
private:
    /** 旧终端只装备真实已拥有权；复用增量保存后发布屏障，不为旧UI免费授新枪或刷新弹量。 */
    FDemoCommandResult EquipOwnedFromTerminal(FName WeaponId, ADemoCharacter* Player);
    TWeakObjectPtr<UDemoTerminalComponent> Terminals; // World组件借用，不延长旧World生命周期。
    FDemoSaveCheckpointRequest SaveCheckpointRequest; // 当前同步业务保存入口，退出时解绑。
    bool bStopped = false; // 明确禁止OpenLevel生效前的旧UI继续交易。
    bool bTransactionInProgress = false; // 游戏线程同步事务重入门控，不是跨线程锁；避免GAS回调递归购买。
    TMap<FName,FDemoCasinoDeviceLease> DeviceLeases; // 最多作者七设备；过期/换Pawn/离开只撤租约，不清私人持续局。
    FGuid ExecutingPhysicalToken; // 仅同步游戏线程可信执行栈暂存；TGuardValue退出复原，客户端/RPC不能写此字段。
};
