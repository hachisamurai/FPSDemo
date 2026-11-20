#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Economy/DemoShopComponent.h" // 购买及权限已迁入权威交易服务。
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Characters/DemoCharacter.h"
#include "Net/UnrealNetwork.h"
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
    SetIsReplicatedByDefault(true); // 各Pawn独立库存，客户端仅拥有者展示。
}
const UDemoAmmoCatalog* UDemoAmmoComponent::GetCatalog() const { DEMO_LOG_TICK(); return Catalog?Catalog.Get():GetDefault<UDemoAmmoCatalog>(); }
bool UDemoAmmoComponent::IsUnlocked(int32 Index) const
{
    DEMO_LOG_TICK();
    if (Index < 0 || Index > 3) return false; // Debug也不允许越过稳定目录白名单。
#if WITH_EDITOR
    const UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr; // CDO/销毁期间可能没有World，仅读当前GI。
    const UDemoPlayerProfile* Profile = Instance ? Instance->GetSubsystem<UDemoPlayerProfile>() : nullptr; // GI拥有，不跨调用保存。
    const ADemoCharacter* Character = Cast<ADemoCharacter>(GetOwner()); // 主机调试只影响本地玩家，不能给远端玩家自动授予房主权限。
    if (Character && Character->HasAuthority() && Character->IsLocallyControlled() && Profile && Profile->IsDebugUnlockAllForSession()) return true;
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
bool UDemoAmmoComponent::Equip(int32 Index, FString& OutMessage)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 旧接口仅适配权威Shop，联网UI由拥有者PC请求。
    if (!Mode || !Mode->GetShopSystem()) { OutMessage = TEXT("商店服务不可用"); UE_LOG(LogFPSDemo, Log, TEXT("AMMO_EQUIP rejected authority/shop")); return false; }
    const FDemoCommandResult Result = Mode->GetShopSystem()->EquipAmmo(Cast<ADemoCharacter>(GetOwner()), Index); // 结构化结果不靠中文内容判定。
    OutMessage = Result.Message;
    return Result.IsApplied();
}
bool UDemoAmmoComponent::ExecuteSelection(int32 Index)
{
    DEMO_LOG_CALL();
    if (!GetOwner()->HasAuthority() || !IsUnlocked(Index) || Index < 0 || Index > 3 || (Index != 0 && !GetCatalog()->Validate()))
    { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_EXECUTE rejected authority/unlocked/config")); return false; }
    const int32 Previous = Selected; // 仅GE成功才提交选择，失败保留旧类型/效果。
    Selected = Index;
    if (!RefreshEffect()) { Selected = Previous; return false; }
    GetOwner()->ForceNetUpdate();
    return true;
}
void UDemoAmmoComponent::CaptureSnapshot(FDemoLoadoutSnapshot& Data) const
{
    DEMO_LOG_CALL();
    Data.UnlockedAmmoIds = Unlocked;
    for (int32 Index = 0; Index < 4; ++Index) // 默认解锁固化为当前玩家领域值，不能在重生收回已拥有权益。
        if (IsUnlocked(Index)) Data.UnlockedAmmoIds.AddUnique(UDemoAmmoCatalog::IdAt(Index));
    Data.SelectedAmmoId = UDemoAmmoCatalog::IdAt(Selected);
}
bool UDemoAmmoComponent::CanRestoreSnapshot(const FDemoLoadoutSnapshot& Data) const
{
    DEMO_LOG_CALL();
    const ADemoCharacter* Player=Cast<ADemoCharacter>(GetOwner()); // 当前服务器Avatar，不接受客户端上传的库存授权。
    const int32 Target=UDemoAmmoCatalog::IndexOf(Data.SelectedAmmoId); // 不允许未知类型静默转换成普通弹。
    if (!Player || !Player->HasAuthority() || !Player->GetAbilitySystemComponent() || Target<0 || !Data.UnlockedAmmoIds.Contains(TEXT("normal"))
        || !Data.UnlockedAmmoIds.Contains(Data.SelectedAmmoId) || Data.UnlockedAmmoIds.Num()>4 || (Target!=0 && (!GetCatalog() || !GetCatalog()->Validate())))
    { UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_RESTORE rejected authority/ASC/catalog/selection")); return false; }
    TSet<FName> Seen; // 持久数据必须无重复且全部为已知类型。
    for (const FName Id : Data.UnlockedAmmoIds)
    {
        if (Seen.Contains(Id) || UDemoAmmoCatalog::IndexOf(Id)<0) return false;
        Seen.Add(Id);
    }
    return true;
}
bool UDemoAmmoComponent::RestoreSnapshot(const FDemoLoadoutSnapshot& Data)
{
    DEMO_LOG_CALL();
    if (!CanRestoreSnapshot(Data)) return false;
    const TArray<FName> PreviousUnlocked=Unlocked; // GE应用失败时还原目录值，旧GE由RefreshEffect保持。
    const int32 PreviousSelected=Selected; // 生命周期仅本次同步事务。
    Unlocked=Data.UnlockedAmmoIds; Selected=UDemoAmmoCatalog::IndexOf(Data.SelectedAmmoId);
    if (!RefreshEffect())
    { Unlocked=PreviousUnlocked; Selected=PreviousSelected; UE_LOG(LogFPSDemo,Warning,TEXT("AMMO_RESTORE rolled back")); return false; }
    GetOwner()->ForceNetUpdate(); return true;
}
void UDemoAmmoComponent::Capture(UDemoRunSave& Data) const
{
    DEMO_LOG_CALL();
    FDemoLoadoutSnapshot Snapshot; // SaveGame仅兼容序列化边界，实际库存只处理领域值。
    CaptureSnapshot(Snapshot);
    Data.UnlockedAmmoIds = Snapshot.UnlockedAmmoIds;
    Data.SelectedAmmoId = Snapshot.SelectedAmmoId;
}
void UDemoAmmoComponent::Restore(const UDemoRunSave& Data)
{
    DEMO_LOG_CALL();
    FDemoLoadoutSnapshot Snapshot; // 原V4字段无损转为新领域接口。
    Snapshot.UnlockedAmmoIds = Data.UnlockedAmmoIds;
    Snapshot.SelectedAmmoId = Data.SelectedAmmoId;
    RestoreSnapshot(Snapshot);
}
void UDemoAmmoComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL();
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UDemoAmmoComponent, Unlocked, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UDemoAmmoComponent, Selected, COND_OwnerOnly);
}
void UDemoAmmoComponent::OnRep_Inventory()
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Log, TEXT("AMMO_VIEW replicated selected=%d unlocked=%d"), Selected, Unlocked.Num());
}
bool UDemoAmmoComponent::RefreshEffect()
{
    DEMO_LOG_CALL();
    if (!GetOwner()->HasAuthority()) { UE_LOG(LogFPSDemo, Log, TEXT("AMMO_EFFECT rejected client mutation")); return false; }
    ADemoCharacter* Player = Cast<ADemoCharacter>(GetOwner()); // PS ASC在Avatar更替时可能尚未初始化。
    UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr; // 当前同步借用，效果仍属于本人PS。
    if (!ASC) { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_LOADOUT missing ASC")); return false; }
    FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UDemoAmmoLoadoutEffect::StaticClass(), 1, ASC->MakeEffectContext()); // 每次独立Spec，不修改共享GE CDO。
    if (!Spec.IsValid()) { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_LOADOUT invalid spec")); return false; }
    Spec.Data->DynamicGrantedTags.AddTag(DemoAmmoTags::Type(Selected));
    const FActiveGameplayEffectHandle Next = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()); // 先准备新效果，应用失败不能移除旧有效装配。
    if (!Next.IsValid()) { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_LOADOUT effect application failed")); return false; }
    if (LoadoutEffect.IsValid()) ASC->RemoveActiveGameplayEffect(LoadoutEffect);
    LoadoutEffect = Next;
    UE_LOG(LogFPSDemo, Log, TEXT("AMMO_EQUIPPED type=%d"), Selected);
    return true;
}
void UDemoAmmoComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if(GetOwner()->HasAuthority()) if(ADemoCharacter* Player=Cast<ADemoCharacter>(GetOwner()))if(auto* ASC=Player->GetAbilitySystemComponent())if(LoadoutEffect.IsValid())ASC->RemoveActiveGameplayEffect(LoadoutEffect); // 不清其他永久成长或技能冷却。
    Super::EndPlay(EndPlayReason);
}
