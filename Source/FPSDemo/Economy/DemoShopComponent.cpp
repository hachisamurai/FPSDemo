#include "Economy/DemoShopComponent.h"
#include "Debug/DemoEditorCheats.h" // 只在WITH_EDITOR分支调整实际扣款，正常报价/权限/次数不改变。
#include "Economy/DemoUpgradeCatalog.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Game/DemoGameState.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Save/DemoRunSave.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"
namespace
{
    /** Player 为同步借用 Avatar；容器归本人 PlayerState，不能借用房主钱包。 */
    UDemoPlayerProgressComponent* PersonalProgress(const ADemoCharacter* Player)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
        const ADemoPlayerState* State = Player ? Player->GetPlayerState<ADemoPlayerState>() : nullptr; // 重生时 PS 可以先于 Pawn 保留。
        return State ? State->GetProgressComponent() : nullptr;
    }
}
UDemoShopComponent::UDemoShopComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
void UDemoShopComponent::InitializeServices(UDemoTerminalComponent* Terminal, FDemoSaveCheckpointRequest SaveRequest)
{
    DEMO_LOG_CALL();
    if (bStopped || !Terminal || Terminal->GetOwner() != GetOwner() || !SaveRequest.IsBound())
    { UE_LOG(LogFPSDemo, Error, TEXT("SHOP_INIT invalid dependencies")); return; }
    Terminals = Terminal; SaveCheckpointRequest = MoveTemp(SaveRequest);
}
void UDemoShopComponent::Shutdown() { DEMO_LOG_CALL(); bStopped = true; SaveCheckpointRequest.Unbind(); Terminals.Reset(); DeviceLeases.Reset(); }
void UDemoShopComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) { DEMO_LOG_CALL(); Shutdown(); Super::EndPlay(EndPlayReason); }
int32 UDemoShopComponent::GetUpgradeCost(int32 Choice) const
{
    DEMO_LOG_TICK();
    // 旧单人调用适配；联网 UI 必须传本人，不能把 Player0 的报价用作其他玩家的授权。
    return GetPlayerUpgradeCost(Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)), Choice);
}
int32 UDemoShopComponent::GetPlayerUpgradeCost(const ADemoCharacter* Player, int32 Choice) const
{
    DEMO_LOG_TICK();
    const UDemoPlayerProgressComponent* Progress = PersonalProgress(Player); // 只读本人账本。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 公共阶段仅决定币种。
    return Progress && State ? Progress->GetSnapshot().UpgradeProgress.Cost(Choice, State->Phase == EDemoPhase::Hub) : INDEX_NONE;
}
FString UDemoShopComponent::GetTerminalBlockReason(ADemoCharacter* Player) const
{
    DEMO_LOG_TICK();
    if (bStopped || bTransactionInProgress || !GetOwner()->HasAuthority() || !Terminals.IsValid() || !SaveCheckpointRequest.IsBound() || !PersonalProgress(Player))
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("SHOP_REJECT unavailable authority/session/player")); return TEXT("交易服务或玩家尚未就绪"); }
    return Terminals->ValidateSession(Player, true);
}
FString UDemoShopComponent::GetPurchaseBlockReason(ADemoCharacter* Purchaser, int32 Choice) const
{
    DEMO_LOG_TICK();
    const FString Reason = GetTerminalBlockReason(Purchaser); // 每次提交重验会话、位置、生命与阶段。
    if (!Reason.IsEmpty()) return Reason;
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 前述验证保证存在。
    const FDemoPlayerProgressSnapshot& Data = PersonalProgress(Purchaser)->GetSnapshot(); // 同步只读，不保存引用到下一帧。
    const int32 Cost = GetPlayerUpgradeCost(Purchaser, Choice); // 此人的此项价格。
    if (Cost < 0) return TEXT("升级项目无效");
    if (Data.UpgradeProgress.Total(true) + Data.UpgradeProgress.Total(false) >= 100000) return TEXT("升级次数已达上限");
    const UDemoAttributeSet* Attributes = Purchaser->GetDemoAttributes(); // GAS 合计上限与存档格式保持一致。
    const UDemoUpgradeCatalog* Catalog = UDemoUpgradeCatalog::Get(); // 缺失配置禁止交易，不能显示一个价格却应用另一组默认增量。
    const FDemoUpgradeDefinition* Item = Catalog ? Catalog->Find(false,Choice) : nullptr; // 校验的业务索引只映射稳定商品ID。
    const float Current = Attributes ? (Choice==0?Attributes->GetWeaponDamageBonus():Choice==1?Attributes->GetMaxHealth():Attributes->GetMagazineBonus()) : 0.f; // 购买前实际合计值。
    if (!Attributes || !Item || Current > Item->Cap-Item->Increment) return TEXT("该属性已达上限或升级配置不可用");
    int32 RequiredCost=Cost; // 只用于余额校验，保持原商品价格和后续涨价规则。
#if WITH_EDITOR
    RequiredCost=DemoEditorCheats::Debit(Purchaser,State->Phase==EDemoPhase::Hub,Cost);
#endif
    if ((State->Phase == EDemoPhase::Hub ? Data.Coins : Data.SilverCoins) < RequiredCost) return TEXT("个人货币余额不足");
    return FString();
}
bool UDemoShopComponent::PurchaseUpgrade(int32 Choice, ADemoCharacter* Purchaser)
{ DEMO_LOG_CALL(); return PurchaseUpgradeResult(Choice, Purchaser).IsApplied(); }
FDemoCommandResult UDemoShopComponent::PurchaseUpgradeResult(int32 Choice, ADemoCharacter* Purchaser)
{
    DEMO_LOG_CALL();
    const FString Reason = GetPurchaseBlockReason(Purchaser, Choice); // UI 报价不是扣款依据。
    if (!Reason.IsEmpty()) return FDemoCommandResult::Reject(TEXT("PurchaseRejected"), Reason);
    TGuardValue<bool> Guard(bTransactionInProgress, true); // 同步 GAS 回调不能递归进入第二笔购买。
    UDemoPlayerProgressComponent* Progress = PersonalProgress(Purchaser); // 当前 PS 唯一账本。
    FDemoPlayerProgressSnapshot Next = Progress->GetSnapshot(); // 先准备整笔钱包/价格变更。
    const bool bGold = GetWorld()->GetGameState<ADemoGameState>()->Phase == EDemoPhase::Hub; // 服务器决定币种。
    int32 Cost = Next.UpgradeProgress.Cost(Choice, bGold); // 同栈冻结实际扣款；Editor无限币只免扣，不伪造钱包或重置涨价。
#if WITH_EDITOR
    Cost=DemoEditorCheats::Debit(Purchaser,bGold,Cost);
#endif
    const FDemoUpgradeDefinition& Item = *UDemoUpgradeCatalog::Get()->Find(false,Choice); // 前述完整校验保证存在；本次同步事务冻结配置。
    const TSubclassOf<UGameplayEffect> Effect = Choice == 0 ? UDemoPowerEffect::StaticClass() : Choice == 1 ? UDemoMaxHealthEffect::StaticClass() : UDemoMagazineEffect::StaticClass(); // 策略白名单，数值来自目录。
    const FDemoPlayerProgressSnapshot Before = Next; // 先提交合法账本，再应用效果；失败可回滚，不能免费获得升级。
    if (bGold)
    {
        Next.Coins -= Cost; ++Next.UpgradeProgress.GoldLevels[Choice];
        if (Choice == 0) Next.UpgradeProgress.PermanentDamage += Item.Increment;
        else if (Choice == 1) Next.UpgradeProgress.PermanentHealth += Item.Increment;
        else Next.UpgradeProgress.PermanentMagazine += Item.Increment;
    }
    else { Next.SilverCoins -= Cost; ++Next.UpgradeProgress.SilverLevels[Choice]; }
    if (!Progress->Commit(Next)) return FDemoCommandResult::Reject(TEXT("LedgerRejected"), TEXT("成长账本提交失败"));
    if (!DemoEffects::Apply(Purchaser->GetDemoASC(), Purchaser->GetDemoASC(), Effect, Item.Increment))
    { Progress->Commit(Before); return FDemoCommandResult::Reject(TEXT("EffectRejected"), TEXT("升级效果未能应用，账本已还原")); }
    if (Choice == 1) DemoEffects::Apply(Purchaser->GetDemoASC(), Purchaser->GetDemoASC(), UDemoHealthEffect::StaticClass(), Item.Increment);
    if (Choice == 2) Purchaser->GetWeaponComponent()->AddAmmoToAll(FMath::RoundToInt(Item.Increment)); // 目录要求整数发数。
    const bool bSaved = SaveCheckpointRequest.Execute(); // 属性已经应用，保存失败保留内存，明示等待重试。
    UE_LOG(LogFPSDemo, Display, TEXT("PURCHASE player=%s choice=%d cost=%d saved=%d"), *GetNameSafe(Purchaser), Choice, Cost, bSaved);
    // 与新增赌博页并存后，普通升级回执也明确本次币种/金额，不能让玩家混淆永久与当轮消费。
    const FString Payment=FString::Printf(TEXT("已消耗 %d %s"),Cost,bGold?TEXT("金币"):TEXT("银币")); // 权威实际扣款，UI不自行推算。
    return FDemoCommandResult::Applied(bSaved, Payment+(bSaved?TEXT("，升级成功，检查点已保存"):TEXT("，升级已生效，房主检查点保存失败，请重试")));
}
FString UDemoShopComponent::GetAmmoBlockReason(ADemoCharacter* Player) const
{
    DEMO_LOG_TICK();
    const FString Reason = GetTerminalBlockReason(Player); // 无 UI 页面权限依赖，网络请求必须已交互。
    if (!Reason.IsEmpty()) return Reason;
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 弹药解锁只在安全区。
    if (State->Phase != EDemoPhase::Hub) return TEXT("仅可在安全区操作弹药终端");
    if (GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>()->GetActiveSlot() == INDEX_NONE) return TEXT("房主尚未选择存档");
    return FString();
}
bool UDemoShopComponent::PurchaseAmmo(ADemoCharacter* Player, int32 Index, int32 QuotedCost, FString& OutMessage)
{
    DEMO_LOG_CALL();
    OutMessage = GetAmmoBlockReason(Player);
    UDemoAmmoComponent* Ammo = Player ? Player->FindComponentByClass<UDemoAmmoComponent>() : nullptr; // 本人 Pawn 库存。
    UDemoPlayerProgressComponent* Progress = PersonalProgress(Player); // 同一笔提交的个人钱包。
    const UDemoAmmoCatalog* Data = Ammo ? Ammo->GetCatalog() : nullptr; // 服务器配置再次计算价格。
    int32 Debit=QuotedCost; // 真实扣款独立于客户端传入且必须匹配的原报价。
#if WITH_EDITOR
    Debit=DemoEditorCheats::Debit(Player,true,QuotedCost);
#endif
    if (!OutMessage.IsEmpty() || !Data || !Progress || !Data->Validate() || !Data->Entries.IsValidIndex(Index)
        || Ammo->IsUnlocked(Index) || Data->Entries[Index].UnlockGoldCost != QuotedCost || QuotedCost < 0 || Progress->GetSnapshot().Coins < Debit)
    { if (OutMessage.IsEmpty()) OutMessage = TEXT("购买失败：权限、余额或报价已变化"); UE_LOG(LogFPSDemo, Log, TEXT("AMMO_PURCHASE_REJECT %s"), *OutMessage); return false; }
    TGuardValue<bool> Guard(bTransactionInProgress, true); // 写盘完成前不接受同步重入交易。
    const FDemoPlayerProgressSnapshot Before = Progress->GetSnapshot(); // 失败回滚保存前的个人值。
    FDemoPlayerProgressSnapshot Next = Before; // 不修改其他玩家余额。
    Next.Coins -= Debit; // Editor免扣不会产生伪造的十亿金币，也保留失败回滚路径。
    if (!Progress->Commit(Next)) return false;
    Ammo->Unlocked.AddUnique(UDemoAmmoCatalog::IdAt(Index));
    if (!SaveCheckpointRequest.Execute())
    {
        Progress->Commit(Before); Ammo->Unlocked.Remove(UDemoAmmoCatalog::IdAt(Index));
        OutMessage = TEXT("保存失败，金币未扣除，请重试"); UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_PURCHASE_ROLLBACK")); return false;
    }
    OutMessage = TEXT("已永久解锁，可免费装配"); UE_LOG(LogFPSDemo, Display, TEXT("AMMO_PURCHASE player=%s id=%d cost=%d"), *GetNameSafe(Player), Index, QuotedCost); return true;
}
FDemoCommandResult UDemoShopComponent::EquipPrimaryWeapon(FName WeaponId, ADemoCharacter* Purchaser)
{
    DEMO_LOG_CALL();
    return EquipOwnedFromTerminal(WeaponId,Purchaser); // 旧终端保持合法已拥有装备，更新为完整本地保存后发布。
}
FDemoCommandResult UDemoShopComponent::EquipAmmo(ADemoCharacter* Player, int32 Index)
{
    DEMO_LOG_CALL();
    const FString Reason = GetAmmoBlockReason(Player); // 不相信客户端解锁/选择状态。
    if (!Reason.IsEmpty()) return FDemoCommandResult::Reject(TEXT("TerminalRejected"), Reason);
    UDemoAmmoComponent* Ammo = Player->FindComponentByClass<UDemoAmmoComponent>(); // 本人库存同步借用。
    if (!Ammo || !Ammo->IsUnlocked(Index)) return FDemoCommandResult::Reject(TEXT("AmmoLocked"), TEXT("弹药尚未解锁"));
    FDemoLoadoutSnapshot Before; // 值备份供失败回滚，避免存档失败仍偷偷改变装配。
    Ammo->CaptureSnapshot(Before);
    TGuardValue<bool> Guard(bTransactionInProgress, true); // 受控执行入口不再调用 Shop 以避免循环。
    if (!Ammo->ExecuteSelection(Index)) return FDemoCommandResult::Reject(TEXT("AmmoInvalid"), TEXT("弹药配置不可用"));
    if (!SaveCheckpointRequest.Execute()) { Ammo->RestoreSnapshot(Before); return FDemoCommandResult::Reject(TEXT("SaveFailed"), TEXT("保存失败，已恢复原弹药")); }
    return FDemoCommandResult::Applied(true, TEXT("弹药已装配并保存"));
}
