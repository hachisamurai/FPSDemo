#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Save/DemoRunSave.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Player/DemoPlayerProfile.h"
#include "Game/FPSDemoGameMode.h"
#include "Engine/GameInstance.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoGameplayAbility.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "Game/DemoGameState.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"

UDemoWeaponComponent::UDemoWeaponComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick = false;
    PrimaryWeaponClasses.Add(TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(TEXT("/Game/Weapons/Blueprints/BP_Weapon_Rifle.BP_Weapon_Rifle_C"))));
    PrimaryWeaponClasses.Add(TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(TEXT("/Game/Weapons/Blueprints/BP_Weapon_Shotgun.BP_Weapon_Shotgun_C"))));
    PrimaryWeaponClasses.Add(TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(TEXT("/Game/Weapons/Blueprints/BP_Weapon_Sniper.BP_Weapon_Sniper_C"))));
    SecondaryWeaponClass = TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(TEXT("/Game/Weapons/Blueprints/BP_Weapon_Pistol.BP_Weapon_Pistol_C")));
}
ADemoCharacter* UDemoWeaponComponent::GetCharacter() const { DEMO_LOG_TICK(); return Cast<ADemoCharacter>(GetOwner()); }
ADemoWeaponBase* UDemoWeaponComponent::SpawnWeapon(TSubclassOf<ADemoWeaponBase> WeaponClass)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 本次调用借用组件Owner。
    if (!Character || !WeaponClass) { UE_LOG(LogFPSDemo, Error, TEXT("Weapon class missing; run create_weapon_assets.py")); return nullptr; }
    FActorSpawnParameters Spawn; // World拥有新Actor，组件强引用跟踪并在卸载时销毁。
    Spawn.Owner = Character;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADemoWeaponBase* Weapon = GetWorld()->SpawnActor<ADemoWeaponBase>(WeaponClass, Character->GetActorTransform(), Spawn); // 成功后由库存持有，失败立即销毁。
    if (Weapon && !Weapon->InitializeForOwner(Character)) { Weapon->Destroy(); return nullptr; }
    return Weapon;
}
bool UDemoWeaponComponent::InitializeLoadout()
{
    DEMO_LOG_CALL();
    if (ActiveWeapon) return true;
    if (!GetOwner()->HasAuthority()) { UE_LOG(LogFPSDemo, Warning, TEXT("Loadout rejected: no authority")); return false; }
    // 目录加载类用于UI读取真实蓝图参数；不会生成主武器，更不会因存档解锁自动装备。
    CatalogClasses.Empty();
    CatalogClasses.Add(SecondaryWeaponClass.LoadSynchronous());
    for (const TSoftClassPtr<ADemoWeaponBase>& Class : PrimaryWeaponClasses)
    {
        CatalogClasses.Add(Class.LoadSynchronous()); // Class为组件配置的软类，仅同步加载定义。
    }
    PrimaryWeapons.SetNum(PrimaryWeaponClasses.Num()); // 空槽直到真实终端装备，避免锁定武器成为隐藏库存。
    SecondaryWeapon = SpawnWeapon(SecondaryWeaponClass.LoadSynchronous());
    if (!SecondaryWeapon) { UE_LOG(LogFPSDemo, Error, TEXT("Loadout failed: pistol unavailable")); return false; }
    PrimaryIndex = INDEX_NONE;
    ActiveSlot = 2;
    ActiveWeapon = SecondaryWeapon;
    ActiveWeapon->SetEquipped(true);
    UDemoRunSave* AmmoDefaults=NewObject<UDemoRunSave>(this); // 首次ASC就绪后授予普通弹装配GE，读档随后覆盖。
    GetOwner()->FindComponentByClass<UDemoAmmoComponent>()->Restore(*AmmoDefaults);
    UE_LOG(LogFPSDemo, Log, TEXT("LOADOUT_READY pistol only=%s; primary requires terminal"), *SecondaryWeapon->GetName());
    return true;
}
void UDemoWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    CancelActions();
    for (ADemoWeaponBase* Weapon : PrimaryWeapons) if (IsValid(Weapon)) Weapon->Destroy(); // 随Pawn结束，不泄漏到新World。
    if (IsValid(SecondaryWeapon)) SecondaryWeapon->Destroy();
    PrimaryWeapons.Empty();
    ActiveWeapon = SecondaryWeapon = nullptr;
    Super::EndPlay(EndPlayReason);
}
bool UDemoWeaponComponent::EquipSlot(int32 Slot)
{
    DEMO_LOG_CALL();
    // 装备只允许存活、非菜单、Hub/Intermission/Combat，阶段变更不能由输入绕过。
    ADemoCharacter* Character = GetCharacter(); // 当前Owner，仅同步借用以检查生命。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前World阶段，不跨关卡缓存。
    const ADemoPlayerController* PC = Character ? Cast<ADemoPlayerController>(Character->GetController()) : nullptr; // 模态窗口与本地输入所有权。
    if (!GetOwner()->HasAuthority() || !Character || !PC || PC->IsUpgradeMenuOpen() || PC->HasBlockingOverlay() || !State
        || (State->Phase != EDemoPhase::Hub && State->Phase != EDemoPhase::Intermission && State->Phase != EDemoPhase::Combat)
        || PC->IsNextLevelConfirmationOpen() || !Character->GetDemoAttributes() || Character->GetDemoAttributes()->GetHealth() <= 0.f || (Slot != 1 && Slot != 2))
    { UE_LOG(LogFPSDemo, Log, TEXT("Equip rejected: slot/phase/menu/life")); return false; }
    ADemoWeaponBase* Next = Slot == 1 && PrimaryWeapons.IsValidIndex(PrimaryIndex) ? PrimaryWeapons[PrimaryIndex].Get() : Slot == 2 ? SecondaryWeapon.Get() : nullptr; // 目标库存实例。
    if (!Next) { UE_LOG(LogFPSDemo, Warning, TEXT("Equip rejected: missing instance")); return false; }
    if (Next == ActiveWeapon) return true;
    CancelActions();
    if (ActiveWeapon) ActiveWeapon->SetEquipped(false);
    ActiveWeapon = Next;
    ActiveSlot = Slot;
    ActiveWeapon->SetEquipped(true);
    return true;
}
bool UDemoWeaponComponent::SelectPrimary(int32 Index)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 同步借用本组件Owner，权限检查时不能使用旧Pawn。
    const ADemoPlayerController* PC = Character ? Cast<ADemoPlayerController>(Character->GetController()) : nullptr; // 借用菜单状态，不持有控制器。
    const AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 权威终端范围/生命/阶段规则。
    UDemoPlayerProfile* Profile = GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // GI持有的永久进度，只借用本次。
    if (!GetOwner()->HasAuthority() || !PrimaryWeapons.IsValidIndex(Index) || !PC || PC->HasBlockingOverlay() || !PC->IsWeaponMenuOpen() // 组件同样拒绝顶层暂停/设置，不能绕开Controller。
        || !Mode || !Mode->GetTerminalBlockReason(Character).IsEmpty() || !Profile || !Profile->IsUnlocked(DemoWeaponCatalog::IdAt(Index + 1)))
    { UE_LOG(LogFPSDemo, Log, TEXT("Primary selection rejected: terminal/phase/life/locked/index")); return false; }
    // 延迟生成，失败不改当前装备；曾持有实例保留弹匣/备用弹药，不能利用切换免费装填。
    if (!PrimaryWeapons[Index]) PrimaryWeapons[Index] = SpawnWeapon(CatalogClasses.IsValidIndex(Index + 1) ? CatalogClasses[Index + 1] : nullptr);
    if (!PrimaryWeapons[Index]) { UE_LOG(LogFPSDemo, Warning, TEXT("Primary selection failed: asset/init")); return false; }
    CancelActions();
    if (ActiveWeapon) ActiveWeapon->SetEquipped(false);
    PrimaryIndex = Index;
    ActiveWeapon = PrimaryWeapons[Index];
    ActiveSlot = 1;
    ActiveWeapon->SetEquipped(true);
    Profile->RememberPrimary(DemoWeaponCatalog::IdAt(Index + 1));
    // 装备成功是安全阶段检查点更新，不将主武器选择混入永久解锁写盘。
    GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->SaveCheckpoint();
    UE_LOG(LogFPSDemo, Log, TEXT("TERMINAL_EQUIPPED primary=%d"), Index);
    return true;
}
void UDemoWeaponComponent::CyclePrimary() { DEMO_LOG_CALL(); UE_LOG(LogFPSDemo, Log, TEXT("Primary cycle rejected: choose unlocked weapon at terminal")); }
const ADemoWeaponBase* UDemoWeaponComponent::GetCatalogWeapon(int32 CatalogIndex) const
{
    DEMO_LOG_TICK();
    return CatalogClasses.IsValidIndex(CatalogIndex) && CatalogClasses[CatalogIndex] ? CatalogClasses[CatalogIndex]->GetDefaultObject<ADemoWeaponBase>() : nullptr;
}
void UDemoWeaponComponent::StartFire()
{
    DEMO_LOG_CALL();
    if (!GetCharacter() || !GetCharacter()->CanUseCombatAbilities() || !ActiveWeapon)
    { UE_LOG(LogFPSDemo, Log, TEXT("Fire input rejected: state/weapon")); return; }
    bFireHeld = true;
    TryFireHeld();
}
void UDemoWeaponComponent::StopFire()
{
    DEMO_LOG_CALL();
    bFireHeld = false;
    GetWorld()->GetTimerManager().ClearTimer(FireTimer);
}
void UDemoWeaponComponent::TryFireHeld()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 每次Timer重新取Owner，避免旧武器继续连射。
    ADemoWeaponBase* Weapon = ActiveWeapon; // 本次输入执行锁定实例。
    if (!bFireHeld || !Character || !Character->CanUseCombatAbilities() || !Weapon || Weapon->IsReloading()) { StopFire(); return; }
    if (Weapon->GetAmmo() < Weapon->Config.AmmoPerShot)
    {
        StopFire();
        RequestReload(true); // 空弹输入与R共享同一接口；没有射击成本/冷却/枪声。
        return;
    }
    const float Remaining = Weapon->GetFireCooldownRemaining(); // 按真实剩余时间重试自动武器，不用固定0.19秒轮询。
    // 成本/标签失败必须停止本次输入，不能在0冷却时创建每毫秒重试的空转Timer。
    if (Remaining <= KINDA_SMALL_NUMBER && (!Character->GetDemoASC() || !Character->GetDemoASC()->ActivateDemoAbility(UDemoFireAbility::StaticClass())))
    { UE_LOG(LogFPSDemo, Log, TEXT("Fire request rejected by GAS; release required")); StopFire(); return; }
    if (ActiveWeapon != Weapon || !bFireHeld) return;
    // 最后一发后立即停止，必须再次按键才自动装填，不能由旧的连射Timer代替新按键。
    if (Weapon->Config.FireMode != EDemoFireMode::Automatic || Weapon->GetAmmo() < Weapon->Config.AmmoPerShot || Weapon->IsReloading()) { StopFire(); return; }
    GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &UDemoWeaponComponent::TryFireHeld, FMath::Max(.001f,Weapon->GetFireCooldownRemaining()), false);
}
bool UDemoWeaponComponent::RequestReload(bool bFromEmpty)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 持有者的ASC用于统一装填生命周期。
    if (!Character || !Character->CanUseCombatAbilities() || !ActiveWeapon || !ActiveWeapon->CanReload())
    { UE_LOG(LogFPSDemo, Log, TEXT("Reload request rejected: state/full/no reserve/pending")); return false; }
    StopFire();
    StopAim();
    UE_LOG(LogFPSDemo, Log, TEXT("RELOAD_REQUEST source=%s weapon=%s"), bFromEmpty ? TEXT("EmptyFire") : TEXT("Manual"), *ActiveWeapon->GetName());
    return Character->GetDemoASC()->ActivateDemoAbility(UDemoReloadAbility::StaticClass());
}
void UDemoWeaponComponent::CycleAim()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 本次借用本地Pawn。
    if (!Character || !Character->CanUseCombatAbilities() || !ActiveWeapon || !ActiveWeapon->Config.bSupportsScope || ActiveWeapon->IsReloading())
    { UE_LOG(LogFPSDemo, Log, TEXT("Aim rejected: phase/weapon/reload")); return; }
    if (ScopeLevel == 0) Character->GetDemoASC()->ActivateDemoAbility(UDemoAimAbility::StaticClass());
    else if (ScopeLevel == 1) SetScopeLevel(2);
    else StopAim();
}
bool UDemoWeaponComponent::BeginAim()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // Aim激活时借用Owner及摄像机以保存原FOV。
    if (!Character || !Character->CanUseCombatAbilities() || !ActiveWeapon || !ActiveWeapon->Config.bSupportsScope || ActiveWeapon->IsReloading())
    { UE_LOG(LogFPSDemo, Log, TEXT("BeginAim rejected: character/phase/weapon/reload")); return false; }
    UnscopedFOV = Character->GetFirstPersonCameraComponent()->FieldOfView;
    SetScopeLevel(1);
    return true;
}
void UDemoWeaponComponent::SetScopeLevel(int32 Level)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // Scope状态只由当前装备控制，不改变相机类默认值。
    if (!Character || !ActiveWeapon || (Level != 1 && Level != 2)) return;
    ScopeLevel = Level;
    Character->GetFirstPersonCameraComponent()->SetFieldOfView(Level == 1 ? ActiveWeapon->Config.ScopeFOV : ActiveWeapon->Config.DeepScopeFOV);
    Character->GetMesh1P()->SetHiddenInGame(true, false);
    ActiveWeapon->SetScopedVisual(true);
    Character->RefreshMovementSpeed();
    UE_LOG(LogFPSDemo, Log, TEXT("SCOPE_ENTER level=%d fov=%.1f"), Level, Character->GetFirstPersonCameraComponent()->FieldOfView);
}
void UDemoWeaponComponent::StopAim()
{
    DEMO_LOG_CALL();
    if (GetCharacter() && GetCharacter()->GetDemoASC()) GetCharacter()->GetDemoASC()->CancelDemoAbility(UDemoAimAbility::StaticClass());
    EndAim(); // 幂等恢复，覆盖ASC已销毁或尚未授予技能的卸载路径。
}
void UDemoWeaponComponent::EndAim()
{
    DEMO_LOG_CALL();
    if (ScopeLevel == 0) return;
    ScopeLevel = 0;
    if (ADemoCharacter* Character = GetCharacter())
    {
        Character->GetFirstPersonCameraComponent()->SetFieldOfView(UnscopedFOV);
        Character->GetMesh1P()->SetHiddenInGame(false, false);
        Character->RefreshMovementSpeed();
    }
    if (ActiveWeapon) ActiveWeapon->SetScopedVisual(false);
    UE_LOG(LogFPSDemo, Log, TEXT("SCOPE_EXIT restored=%.1f"), UnscopedFOV);
}
void UDemoWeaponComponent::CancelActions()
{
    DEMO_LOG_CALL();
    StopFire();
    StopAim();
    if (GetCharacter() && GetCharacter()->GetDemoASC()) GetCharacter()->GetDemoASC()->CancelDemoAbility(UDemoReloadAbility::StaticClass());
    if (ActiveWeapon) ActiveWeapon->CancelReload();
}
void UDemoWeaponComponent::RefillAll()
{
    DEMO_LOG_CALL();
    for (ADemoWeaponBase* Weapon : PrimaryWeapons) if (IsValid(Weapon)) Weapon->Refill(); // 所有已持有主武器同步补给。
    if (SecondaryWeapon) SecondaryWeapon->Refill();
}
void UDemoWeaponComponent::AddAmmoToAll(int32 Delta)
{
    DEMO_LOG_CALL();
    for (ADemoWeaponBase* Weapon : PrimaryWeapons) if (IsValid(Weapon)) Weapon->ModifyAmmo(Delta); // 容量加成已由GAS应用，再补增加的发数。
    if (SecondaryWeapon) SecondaryWeapon->ModifyAmmo(Delta);
}
ADemoWeaponBase* UDemoWeaponComponent::GetActiveWeapon() const { DEMO_LOG_TICK(); return ActiveWeapon; }
int32 UDemoWeaponComponent::GetActiveSlot() const { DEMO_LOG_TICK(); return ActiveSlot; }
int32 UDemoWeaponComponent::GetPrimaryIndex() const { DEMO_LOG_TICK(); return PrimaryIndex; }
int32 UDemoWeaponComponent::GetScopeLevel() const { DEMO_LOG_TICK(); return ScopeLevel; }
float UDemoWeaponComponent::GetAimMoveMultiplier() const { DEMO_LOG_TICK(); return ScopeLevel > 0 && ActiveWeapon ? ActiveWeapon->Config.AimMoveMultiplier : 1.f; }
bool UDemoWeaponComponent::IsEquipped(const ADemoWeaponBase* Weapon) const { DEMO_LOG_TICK(); return Weapon && ActiveWeapon == Weapon; }

void UDemoWeaponComponent::CaptureLoadout(UDemoRunSave& Data) const
{
    DEMO_LOG_CALL();
    GetOwner()->FindComponentByClass<UDemoAmmoComponent>()->Capture(Data); // 同一快照记录币种解锁和装配。
    Data.Weapons.Empty();
    Data.PrimaryId = PrimaryIndex == INDEX_NONE ? NAME_None : DemoWeaponCatalog::IdAt(PrimaryIndex+1);
    Data.ActiveSlot = ActiveSlot;
    for (int32 Index = 0; Index <= PrimaryWeapons.Num(); ++Index) // 0为手枪，其余按稳定目录ID保存。
    {
        const ADemoWeaponBase* Weapon = Index == 0 ? SecondaryWeapon.Get() : PrimaryWeapons[Index-1].Get(); // 本次只读库存。
        if (!Weapon) continue;
        FDemoSavedWeapon Snapshot; // 独立值对象，无Actor生命周期依赖。
        Snapshot.Id = DemoWeaponCatalog::IdAt(Index);
        Snapshot.Ammo = Weapon->GetAmmo(); Snapshot.Reserve = Weapon->GetReserveAmmo();
        Data.Weapons.Add(Snapshot);
    }
}
bool UDemoWeaponComponent::RestoreLoadout(const UDemoRunSave& Data)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 只能在新World Hub重建时恢复，不能当战斗装备后门。
    const UDemoPlayerProfile* Profile = GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 永久解锁只读。
    if (!GetOwner()->HasAuthority() || !State || State->Phase != EDemoPhase::Hub || !Data.Validate() || !Profile) { UE_LOG(LogFPSDemo, Warning, TEXT("RestoreLoadout rejected: authority/phase/data/profile")); return false; }
    GetOwner()->FindComponentByClass<UDemoAmmoComponent>()->Restore(Data); // 权威Hub恢复稳定ID及对应装配GE。
    CancelActions();
    for (const FDemoSavedWeapon& Saved : Data.Weapons) // 仅持久化记录的武器才生成，锁定记录拒绝整个恢复。
    {
        const int32 Index = DemoWeaponCatalog::IndexOf(Saved.Id); // 目录ID转瞬时索引，不在存档保存类路径。
        if (!Profile->IsUnlocked(Saved.Id) || Index < 0) { UE_LOG(LogFPSDemo, Warning, TEXT("Save requests locked weapon")); return false; }
        if (Index > 0 && !PrimaryWeapons[Index-1]) PrimaryWeapons[Index-1] = SpawnWeapon(CatalogClasses[Index]);
        ADemoWeaponBase* Weapon = Index == 0 ? SecondaryWeapon.Get() : PrimaryWeapons[Index-1].Get(); // 借用刚创建/已有实例。
        if (!Weapon) { UE_LOG(LogFPSDemo, Error, TEXT("RestoreLoadout failed to spawn %s"), *Saved.Id.ToString()); return false; }
        Weapon->RestoreAmmo(Saved.Ammo, Saved.Reserve);
    }
    PrimaryIndex = Data.PrimaryId.IsNone() ? INDEX_NONE : DemoWeaponCatalog::IndexOf(Data.PrimaryId)-1;
    ActiveWeapon->SetEquipped(false);
    ActiveSlot = Data.ActiveSlot;
    ActiveWeapon = ActiveSlot == 1 ? PrimaryWeapons[PrimaryIndex].Get() : SecondaryWeapon.Get();
    if (!ActiveWeapon) { UE_LOG(LogFPSDemo, Error, TEXT("RestoreLoadout missing active instance")); return false; }
    ActiveWeapon->SetEquipped(true);
    return true;
}
