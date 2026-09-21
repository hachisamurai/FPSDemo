#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoShopComponent.generated.h"

class ADemoCharacter;

/** 权威终端交易服务；GameState钱包、属性账本、Pawn库存和GI存档保持各自唯一所有者。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoShopComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick；UI查询与提交均走同一组校验，每次提交重新验证。 */
    UDemoShopComponent();
    /** Choice=0伤害/1生命/2弹匣；按当前安全区/关间币种查询独立价格，非法返回INDEX_NONE。 */
    int32 GetUpgradeCost(int32 Choice) const;
    /** Purchaser为本次借用Pawn，Choice=0..2；检查终端、页面、属性上限及钱包，返回可展示原因。 */
    FString GetPurchaseBlockReason(ADemoCharacter* Purchaser, int32 Choice) const;
    /** Player必须是权威活Pawn且在当前终端250cm内；允许Hub/Intermission，无顶层模态/暂停。 */
    FString GetTerminalBlockReason(ADemoCharacter* Player) const;
    /** Choice/Purchaser为已选商品和购买者，内部重验；只涨该属性价格，成功应用后自动保存。 */
    bool PurchaseUpgrade(int32 Choice, ADemoCharacter* Purchaser);
    /** Player为本次弹药购买/装配Owner；额外要求安全区弹药页和有效存档槽。 */
    FString GetAmmoBlockReason(ADemoCharacter* Player) const;
    /** Index=0..3，QuotedCost为确认报价；同步保存金币与解锁，失败回滚，OutMessage反馈可见原因。 */
    bool PurchaseAmmo(ADemoCharacter* Player, int32 Index, int32 QuotedCost, FString& OutMessage);
private:
    bool bTransactionInProgress = false; // 游戏线程同步事务重入门控，不是跨线程锁；避免GAS回调递归购买。
};
