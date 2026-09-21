#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Economy/DemoShopComponent.h" // 购买及权限已迁入权威交易服务。
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
#if WITH_EDITOR
#include "Player/DemoPlayerProfile.h" // 临时全解锁由GI统一持有，Pawn换图/重生不丢失。
#endif

UDemoAmmoComponent::UDemoAmmoComponent()
{
    DEMO_LOG_CALL();
    ConstructorHelpers::FObjectFinder<UDemoAmmoCatalog> Data(TEXT("/Game/Data/Ammo/DA_AmmoCatalog.DA_AmmoCatalog")); // 资产由Editor脚本创建，构造硬引用供Cook收集。
    Catalog=Data.Object;
}
const UDemoAmmoCatalog* UDemoAmmoComponent::GetCatalog() const { DEMO_LOG_TICK(); return Catalog?Catalog.Get():GetDefault<UDemoAmmoCatalog>(); }
bool UDemoAmmoComponent::IsUnlocked(int32 Index) const
{
    DEMO_LOG_TICK();
    if (Index < 0 || Index > 3) return false; // Debug也不允许越过稳定目录白名单。
#if WITH_EDITOR
    const UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr; // CDO/销毁期间可能没有World，仅读当前GI。
    const UDemoPlayerProfile* Profile = Instance ? Instance->GetSubsystem<UDemoPlayerProfile>() : nullptr; // GI拥有，不跨调用保存。
    if (Profile && Profile->IsDebugUnlockAllForSession()) return true; // 仅本次Editor会话临时权限。
#endif
    return Index==0 || (GetCatalog()->Entries.IsValidIndex(Index) && (GetCatalog()->Entries[Index].bUnlockedByDefault||Unlocked.Contains(UDemoAmmoCatalog::IdAt(Index))));
}
int32 UDemoAmmoComponent::GetSelected() const { DEMO_LOG_TICK(); return Selected; }
FString UDemoAmmoComponent::GetBlockReason() const
{
    DEMO_LOG_TICK();
    const AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 旧库存API只转发权限，方便既有HUD和测试复用。
    return Mode ? Mode->GetShopSystem()->GetAmmoBlockReason(Cast<ADemoCharacter>(GetOwner())) : TEXT("商店服务不可用");
}
bool UDemoAmmoComponent::Purchase(int32 Index,int32 QuotedCost,FString& OutMessage)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 兼容旧UI入口，金币/报价/解锁提交唯一转交商店系统。
    if (!Mode) { OutMessage=TEXT("商店服务不可用"); UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_PURCHASE_REJECT missing shop")); return false; }
    return Mode->GetShopSystem()->PurchaseAmmo(Cast<ADemoCharacter>(GetOwner()), Index, QuotedCost, OutMessage);
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
