#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Save/DemoRunSave.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "AbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"

UDemoAmmoComponent::UDemoAmmoComponent()
{
    DEMO_LOG_CALL();
    ConstructorHelpers::FObjectFinder<UDemoAmmoCatalog> Data(TEXT("/Game/Data/Ammo/DA_AmmoCatalog.DA_AmmoCatalog")); // 资产由Editor脚本创建，构造硬引用供Cook收集。
    Catalog=Data.Object;
}
const UDemoAmmoCatalog* UDemoAmmoComponent::GetCatalog() const { DEMO_LOG_TICK(); return Catalog?Catalog.Get():GetDefault<UDemoAmmoCatalog>(); }
bool UDemoAmmoComponent::IsUnlocked(int32 Index) const { DEMO_LOG_TICK(); return Index==0 || (GetCatalog()->Entries.IsValidIndex(Index) && (GetCatalog()->Entries[Index].bUnlockedByDefault||Unlocked.Contains(UDemoAmmoCatalog::IdAt(Index)))); }
int32 UDemoAmmoComponent::GetSelected() const { DEMO_LOG_TICK(); return Selected; }
FString UDemoAmmoComponent::GetBlockReason() const
{
    DEMO_LOG_TICK();
    ADemoCharacter* Player=Cast<ADemoCharacter>(GetOwner()); // 当前Pawn，只借用本调用。
    const auto* PC=Player?Cast<ADemoPlayerController>(Player->GetController()):nullptr; // UI入口仍需权威校验。
    const auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 单人权威交易服务。
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前钱包/阶段。
    if(!Player||!Player->HasAuthority()||!PC||!PC->IsAmmoMenuOpen()||PC->HasBlockingOverlay()||!Mode||!State||State->Phase!=EDemoPhase::Hub)return TEXT("仅可在安全区弹药终端操作");
    if(GetWorld()->IsPaused())return TEXT("暂停期间不能装配或购买");
    if(GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>()->GetActiveSlot()==INDEX_NONE)return TEXT("请先选择存档");
    return Mode->GetTerminalBlockReason(Player);
}
bool UDemoAmmoComponent::Purchase(int32 Index,int32 QuotedCost,FString& OutMessage)
{
    DEMO_LOG_CALL();
    OutMessage=GetBlockReason();
    const UDemoAmmoCatalog* Data=GetCatalog(); // 确认时重新读取配置，拒绝过期报价。
    auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 钱包与检查点同一游戏线程提交。
    if(!OutMessage.IsEmpty()||!Data->Validate()||!Data->Entries.IsValidIndex(Index)||IsUnlocked(Index)||Data->Entries[Index].UnlockGoldCost!=QuotedCost||State->Coins<QuotedCost)
    { if(OutMessage.IsEmpty())OutMessage=TEXT("购买失败：已解锁、金币不足或价格已变化"); UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_PURCHASE_REJECT %s"),*OutMessage); return false; }
    State->Coins-=QuotedCost; Unlocked.AddUnique(UDemoAmmoCatalog::IdAt(Index));
    // 同步检查点将金币和解锁一起保存；期间无异步帧，写入失败回滚内存，不向UI发布成功。
    if(!GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->SaveCheckpoint())
    { State->Coins+=QuotedCost; Unlocked.Remove(UDemoAmmoCatalog::IdAt(Index)); OutMessage=TEXT("保存失败，金币未扣除，请重试"); UE_LOG(LogFPSDemo,Error,TEXT("AMMO_PURCHASE_ROLLBACK")); return false; }
    OutMessage=TEXT("已永久解锁，可免费装配"); UE_LOG(LogFPSDemo,Log,TEXT("AMMO_PURCHASE id=%d gold=%d"),Index,QuotedCost); return true;
}
bool UDemoAmmoComponent::Equip(int32 Index,FString& OutMessage)
{
    DEMO_LOG_CALL(); OutMessage=GetBlockReason();
    // 普通弹始终可作为撤销特殊装配的回退，特殊数值配置损坏不能阻止玩家恢复基础射击。
    if(!OutMessage.IsEmpty()||Index<0||Index>3||!IsUnlocked(Index)||(Index!=0&&!GetCatalog()->Validate())) { if(OutMessage.IsEmpty())OutMessage=TEXT("弹药未解锁或配置无效"); UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_EQUIP_REJECT %s"),*OutMessage); return false; }
    const int32 Previous=Selected; // 失败恢复原选择，现有装配GE保持不动。
    Selected=Index;
    if(!GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->SaveCheckpoint()) { Selected=Previous; OutMessage=TEXT("保存失败，保留原弹药"); UE_LOG(LogFPSDemo,Error,TEXT("AMMO_EQUIP_ROLLBACK")); return false; }
    RefreshEffect(); OutMessage=TEXT("弹药已装配，主副武器共用"); return true;
}
void UDemoAmmoComponent::Capture(UDemoRunSave& Data) const
{
    DEMO_LOG_CALL(); Data.UnlockedAmmoIds=Unlocked;
    for(int32 Index=0;Index<4;++Index)if(IsUnlocked(Index))Data.UnlockedAmmoIds.AddUnique(UDemoAmmoCatalog::IdAt(Index)); // 默认解锁也固化为值，配置后续调整不收回购买权益。
    Data.SelectedAmmoId=UDemoAmmoCatalog::IdAt(Selected);
}
void UDemoAmmoComponent::Restore(const UDemoRunSave& Data)
{
    DEMO_LOG_CALL(); Unlocked=Data.UnlockedAmmoIds; Unlocked.AddUnique(TEXT("normal")); Selected=UDemoAmmoCatalog::IndexOf(Data.SelectedAmmoId);
    if(Selected<0||!IsUnlocked(Selected)){Selected=0; UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_RESTORE fallback normal"));}
    RefreshEffect();
}
void UDemoAmmoComponent::RefreshEffect()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player=Cast<ADemoCharacter>(GetOwner()); // PS ASC在Avatar更替时可能尚未初始化。
    UAbilitySystemComponent* ASC=Player?Player->GetAbilitySystemComponent():nullptr; // 同步借用。
    if(!ASC){UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_LOADOUT missing ASC"));return;}
    if(LoadoutEffect.IsValid())ASC->RemoveActiveGameplayEffect(LoadoutEffect);
    FGameplayEffectSpecHandle Spec=ASC->MakeOutgoingSpec(UDemoAmmoLoadoutEffect::StaticClass(),1,ASC->MakeEffectContext()); // 每次独立Spec，动态授予唯一装配Tag。
    Spec.Data->DynamicGrantedTags.AddTag(DemoAmmoTags::Type(Selected)); LoadoutEffect=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
    UE_LOG(LogFPSDemo,Log,TEXT("AMMO_EQUIPPED type=%d"),Selected);
}
void UDemoAmmoComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if(ADemoCharacter* Player=Cast<ADemoCharacter>(GetOwner()))if(auto* ASC=Player->GetAbilitySystemComponent())if(LoadoutEffect.IsValid())ASC->RemoveActiveGameplayEffect(LoadoutEffect); // 不清其他永久成长或技能冷却。
    Super::EndPlay(EndPlayReason);
}
