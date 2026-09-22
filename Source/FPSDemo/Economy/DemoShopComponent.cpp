#include "Economy/DemoShopComponent.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Game/DemoGameState.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Interaction/DemoInteractable.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Save/DemoRunSave.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"

namespace
{
    /** Reason为当前同步查询的拒绝文本；高频HUD查询仍记录原因，使用VeryVerbose避免默认显示刷屏。 */
    FString ShopRejection(const FString& Reason)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] ShopRejection: %s"), *Reason);
        return Reason;
    }
}

UDemoShopComponent::UDemoShopComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
void UDemoShopComponent::InitializeServices(UDemoTerminalComponent* Terminal, FDemoSaveCheckpointRequest SaveRequest)
{
    DEMO_LOG_CALL();
    if (bStopped || !Terminal || Terminal->GetOwner() != GetOwner() || !SaveRequest.IsBound()) { UE_LOG(LogFPSDemo, Error, TEXT("SHOP_INIT invalid dependencies")); return; }
    Terminals = Terminal; SaveCheckpointRequest = MoveTemp(SaveRequest);
}
void UDemoShopComponent::Shutdown() { DEMO_LOG_CALL(); bStopped = true; SaveCheckpointRequest.Unbind(); Terminals.Reset(); }
void UDemoShopComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) { DEMO_LOG_CALL(); Shutdown(); Super::EndPlay(EndPlayReason); }

int32 UDemoShopComponent::GetUpgradeCost(int32 Choice) const
{
	DEMO_LOG_TICK();
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前World账本，只读查询币种与独立价格。
	// 只涨当前币种的当前属性；查询非法索引不会访问数组或默认为另一属性。
	return State ? State->UpgradeProgress.Cost(Choice, State->Phase == EDemoPhase::Hub) : INDEX_NONE;
}

bool UDemoShopComponent::PurchaseUpgrade(int32 Choice, ADemoCharacter* Purchaser)
{
	DEMO_LOG_CALL();
	// State 借用当前 World；Cost 在效果应用前快照，购买成功后才累计涨价次数。
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	const int32 Cost = GetUpgradeCost(Choice);
	// 与 UI 共用可用性读取，但每次交易仍重新执行，避免使用上一帧按钮状态授权。
	const FString BlockReason = GetPurchaseBlockReason(Purchaser, Choice);
	if (!BlockReason.IsEmpty() || Choice < 0 || Choice > 2)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("Purchase rejected: choice=%d reason=%s"), Choice, *BlockReason);
		return false;
	}
	TGuardValue<bool> TransactionGuard(bTransactionInProgress, true); // 本同步栈退出自动释放重入门控，不捕获Pawn。
	// 属性修改成功后才扣费；加生命上限后补足 25 点生命，弹匣升级后补足 4 发。
	TSubclassOf<UGameplayEffect> EffectClass = Choice == 0 ? UDemoPowerEffect::StaticClass() : Choice == 1 ? UDemoMaxHealthEffect::StaticClass() : UDemoMagazineEffect::StaticClass();
	if (!DemoEffects::Apply(Purchaser->GetDemoASC(), Purchaser->GetDemoASC(), EffectClass, Choice == 0 ? 5.f : Choice == 1 ? 25.f : 4.f)) return false;
	// 区域由权威Phase决定，UI不能传币种参数绕过；购买总数仍保留统计用途。
	if (State->Phase == EDemoPhase::Hub)
	{
		State->Coins -= Cost; ++State->GoldPurchases; ++State->UpgradeProgress.GoldLevels[Choice];
		// 记录本次实际授予的永久增量；不能从合计GAS值或购买总数反推来源。
		if (Choice == 0) State->UpgradeProgress.PermanentDamage += 5.f;
		else if (Choice == 1) State->UpgradeProgress.PermanentHealth += 25.f;
		else State->UpgradeProgress.PermanentMagazine += 4.f;
	}
	else { State->SilverCoins -= Cost; ++State->SilverPurchases; ++State->UpgradeProgress.SilverLevels[Choice]; }
	++State->Purchases;
	if (Choice == 1) DemoEffects::Apply(Purchaser->GetDemoASC(), Purchaser->GetDemoASC(), UDemoHealthEffect::StaticClass(), 25.f);
	if (Choice == 2) Purchaser->GetWeaponComponent()->AddAmmoToAll(4); // 全局容量GE先生效，再给每把已持有武器补新增4发。
	if (!SaveCheckpointRequest.Execute()) UE_LOG(LogFPSDemo, Warning, TEXT("SHOP_ATTRIBUTE save pending; in-memory purchase retained")); // 延续既有属性购买语义：保存失败保留内存成长，由保存状态UI提示并在后续检查点重试。
	// 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
	UE_LOG(LogFPSDemo, Display, TEXT("PURCHASE choice=%d cost=%d currency=%s gold=%d silver=%d"), Choice, Cost, State->Phase == EDemoPhase::Hub ? TEXT("gold") : TEXT("silver"), State->Coins, State->SilverCoins);
	return true;
}

FString UDemoShopComponent::GetPurchaseBlockReason(ADemoCharacter* Purchaser, int32 Choice) const
{
	DEMO_LOG_TICK();
	const FString Reason = GetTerminalBlockReason(Purchaser); // 先执行所有终端操作共用的权威检查，再判断商店余额。
	if (!Reason.IsEmpty()) return Reason;
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 共用验证已保证存在，只在本次查询借用。
	const ADemoPlayerController* PC = Cast<ADemoPlayerController>(Purchaser->GetController()); // 当前拥有者的页面决定购买入口。
	if (PC->IsWeaponMenuOpen() || PC->IsAmmoMenuOpen()) return ShopRejection(TEXT("请切换至属性升级页购买"));
	const bool bGold = State->Phase == EDemoPhase::Hub; // 当前注册终端所属区域，钱包余额与显示币种共用这一判定。
	const int32 Balance = bGold ? State->Coins : State->SilverCoins; // 本次同步只读余额，不用另一币种自动补足。
	const int32 Cost = GetUpgradeCost(Choice); // 每项分别判断不足，其他便宜属性仍能购买。
	if (Cost == INDEX_NONE) return ShopRejection(TEXT("升级项目无效"));
	if (State->Purchases >= 100000) return ShopRejection(TEXT("升级次数已达上限")); // 永久次数计入总上限，避免生成无法保存的快照。
	const UDemoAttributeSet* Attributes = Purchaser->GetDemoAttributes(); // 交易前检查合计属性上限，拒绝不能持久化的增量。
	if (!Attributes || (Choice == 0 && Attributes->GetWeaponDamageBonus() > 9995.f)
		|| (Choice == 1 && Attributes->GetMaxHealth() > 9999975.f) || (Choice == 2 && Attributes->GetMagazineBonus() > 9996.f)) return ShopRejection(TEXT("该属性已达升级上限"));
	if (Balance < Cost) return ShopRejection(FString::Printf(TEXT("%s不足，还需 %d %s"), bGold ? TEXT("金币") : TEXT("银币"), Cost - Balance, bGold ? TEXT("金币") : TEXT("银币")));
	return FString();
}

FString UDemoShopComponent::GetTerminalBlockReason(ADemoCharacter* Player) const
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* PC = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 菜单只提供会话，服务重新校验World/范围/生命。
    if (bStopped || bTransactionInProgress || !Terminals.IsValid() || !SaveCheckpointRequest.IsBound() || !PC
        || PC->HasBlockingOverlay() || !PC->IsUpgradeMenuOpen() || PC->IsRewardMenu() || PC->IsNextLevelConfirmationOpen())
        return ShopRejection(TEXT("请在备战阶段与升级终端交互"));
    if (PC->GetMenuTerminal() != Terminals->GetShopTerminal() || PC->GetMenuTerminalGeneration() == 0)
        return ShopRejection(TEXT("终端交互已过期，请重新交互"));
    return Terminals->ValidateInteraction(Player, PC->GetMenuTerminal(), PC->GetMenuTerminalGeneration());
}

FString UDemoShopComponent::GetAmmoBlockReason(ADemoCharacter* Player) const
{
    DEMO_LOG_TICK();
    const auto* PC=Player?Cast<ADemoPlayerController>(Player->GetController()):nullptr; // UI入口仍需权威校验。
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前钱包/阶段。
    if(!Player||!Player->HasAuthority()||!PC||!PC->IsAmmoMenuOpen()||PC->HasBlockingOverlay()||!State||State->Phase!=EDemoPhase::Hub)return ShopRejection(TEXT("仅可在安全区弹药终端操作"));
    if(GetWorld()->IsPaused())return ShopRejection(TEXT("暂停期间不能装配或购买"));
    if(GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>()->GetActiveSlot()==INDEX_NONE)return ShopRejection(TEXT("请先选择存档"));
    return GetTerminalBlockReason(Player); // 共享范围/生命/阶段门控，避免弹药页另写权限。
}

bool UDemoShopComponent::PurchaseAmmo(ADemoCharacter* Player, int32 Index, int32 QuotedCost, FString& OutMessage)
{
    DEMO_LOG_CALL();
    OutMessage=GetAmmoBlockReason(Player);
    UDemoAmmoComponent* Ammo = Player ? Player->FindComponentByClass<UDemoAmmoComponent>() : nullptr; // 库存唯一属于Pawn，商店只在同步提交期间借用。
    if (!Ammo) { OutMessage=TEXT("弹药库存不可用"); UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_PURCHASE_REJECT missing inventory")); return false; }
    const UDemoAmmoCatalog* Data=Ammo->GetCatalog(); // 确认时重新读取配置，拒绝过期报价。
    auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 钱包与检查点同一游戏线程提交。
    if(!OutMessage.IsEmpty()||!Data->Validate()||!Data->Entries.IsValidIndex(Index)||Ammo->IsUnlocked(Index)||Data->Entries[Index].UnlockGoldCost!=QuotedCost||State->Coins<QuotedCost)
    { if(OutMessage.IsEmpty())OutMessage=TEXT("购买失败：已解锁、金币不足或价格已变化"); UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_PURCHASE_REJECT %s"),*OutMessage); return false; }
    TGuardValue<bool> TransactionGuard(bTransactionInProgress, true); // 同步保存结束前拒绝重入，失败自动释放。
    State->Coins-=QuotedCost; Ammo->Unlocked.AddUnique(UDemoAmmoCatalog::IdAt(Index));
    // 同步检查点将金币和解锁一起保存；期间无异步帧，写入失败回滚内存，不向UI发布成功。
    if(!SaveCheckpointRequest.Execute())
    { State->Coins+=QuotedCost; Ammo->Unlocked.Remove(UDemoAmmoCatalog::IdAt(Index)); OutMessage=TEXT("保存失败，金币未扣除，请重试"); UE_LOG(LogFPSDemo,Error,TEXT("AMMO_PURCHASE_ROLLBACK")); return false; }
    // 发布包保留已提交的关键状态/交易结果；函数调用和逐帧细节仍使用Log/VeryVerbose。
    OutMessage=TEXT("已永久解锁，可免费装配"); UE_LOG(LogFPSDemo,Display,TEXT("AMMO_PURCHASE id=%d gold=%d"),Index,QuotedCost); return true;
}
