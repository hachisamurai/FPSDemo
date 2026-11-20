// 职责：Pawn双槽库存、枪械动作和检查点捕获；Editor借用测试枪只存在当前Pawn，捕获时保原合法库存/主槽。
// 使用：输入/GAS/Shop统一提交；依赖PS来源权限、武器Actor及动画表现，技能实现由对应GA维护。
#include "Weapons/DemoWeaponComponent.h"
#include "Engine/World.h" // 服务器库存使用World创建武器并管理各武器动作时钟。
#include "Economy/Gambling/DemoCasinoRange.h"
#include "GAS/Abilities/DemoRangeShotAbility.h"
#include "Weapons/Melee/DemoMeleeComponent.h" // 输入路由与枪械互斥统一经过近战组件，避免隐藏枪仍消耗弹药。
#include "Weapons/DemoLoadoutTransaction.h"
#include "Animation/DemoWeaponAnimationComponent.h"
// 对应头文件先于依赖，保证UE独立编译能检查本类声明自包含。
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Save/DemoRunSave.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Player/DemoPlayerState.h"
#include "Progression/Builds/DemoRunBuildComponent.h" // 核心接收服务器动作事实，不拥有库存或输入RPC。
#include "Player/DemoPlayerProgressComponent.h"
#include "Economy/DemoShopComponent.h"
#include "Net/UnrealNetwork.h"
#include "Game/FPSDemoGameMode.h"
#include "Engine/GameInstance.h"
#include "Characters/DemoCharacter.h"

#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/Abilities/DemoPlayerAbilityTypes.h"
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
    SetIsReplicatedByDefault(true); // Pawn组件独立复制本人库存；所有生成/弹药提交仍在服务器。
    PrimaryWeaponClasses.Add(TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(TEXT("/Game/Weapons/Blueprints/BP_Weapon_Rifle.BP_Weapon_Rifle_C"))));
    PrimaryWeaponClasses.Add(TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(TEXT("/Game/Weapons/Blueprints/BP_Weapon_Shotgun.BP_Weapon_Shotgun_C"))));
    PrimaryWeaponClasses.Add(TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(TEXT("/Game/Weapons/Blueprints/BP_Weapon_Sniper.BP_Weapon_Sniper_C"))));
    SecondaryWeaponClass = TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(TEXT("/Game/Weapons/Blueprints/BP_Weapon_Pistol.BP_Weapon_Pistol_C")));
}
ADemoCharacter* UDemoWeaponComponent::GetCharacter() const { DEMO_LOG_TICK(); return Cast<ADemoCharacter>(GetOwner()); }
ADemoWeaponBase* UDemoWeaponComponent::SpawnWeapon(TSubclassOf<ADemoWeaponBase> WeaponClass, FName WeaponId)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 本次调用借用组件Owner。
    if (!Character || !Character->HasAuthority() || !WeaponClass || !DemoWeaponCatalog::FindDefinition(WeaponId)) { UE_LOG(LogFPSDemo, Error, TEXT("Weapon class missing; run create_weapon_assets.py")); return nullptr; }
    FActorSpawnParameters Spawn; // World拥有新Actor，组件强引用跟踪并在卸载时销毁。
    Spawn.Owner = Character;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADemoWeaponBase* Weapon = GetWorld()->SpawnActor<ADemoWeaponBase>(WeaponClass, Character->GetActorTransform(), Spawn); // 成功后由库存持有，失败立即销毁。
    if (Weapon && (!Weapon->BindDefinition(*DemoWeaponCatalog::FindDefinition(WeaponId)) || !Weapon->InitializeForOwner(Character))) { Weapon->Destroy(); return nullptr; }
    return Weapon;
}
bool UDemoWeaponComponent::InitializeLoadout()
{
    DEMO_LOG_CALL();
    if (ActiveWeapon) return true;
    if (!GetOwner()->HasAuthority()) { UE_LOG(LogFPSDemo, Warning, TEXT("Loadout rejected: no authority")); return false; }
    LoadCatalogClasses(); // 只读目录与库存分离；新增定义不要求重复维护四个固定索引分支。
    PrimaryWeapons.SetNum(FMath::Max(0, CatalogClasses.Num() - 1));
    SecondaryWeapon = SpawnWeapon(CatalogClasses.IsValidIndex(0) ? CatalogClasses[0] : nullptr, DemoWeaponCatalog::IdAt(0));
    if (!SecondaryWeapon) { UE_LOG(LogFPSDemo, Error, TEXT("Loadout failed: pistol unavailable")); return false; }
    if (!CommitEquip(SecondaryWeapon, 2, INDEX_NONE)) { UE_LOG(LogFPSDemo, Error, TEXT("Loadout failed: pistol animation layer unavailable")); return false; }
    FDemoLoadoutSnapshot AmmoDefaults; // 领域默认值不创建SaveGame对象；首次ASC就绪授予普通弹，读档随后覆盖。
    GetOwner()->FindComponentByClass<UDemoAmmoComponent>()->RestoreSnapshot(AmmoDefaults);
    UE_LOG(LogFPSDemo, Log, TEXT("LOADOUT_READY pistol only=%s; primary requires terminal"), *SecondaryWeapon->GetName());
    return true;
}
void UDemoWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    AbortShopSelection(); // 候选未发布不在PrimaryWeapons中，卸载独立清理且不触碰旧实例。
    if (GetOwner()->HasAuthority()) { FinishSnapshot(false); CancelActions(); } // 卸载也清除尚未提交的新库存。
    GetWorld()->GetTimerManager().ClearTimer(FireTimer);
    for (ADemoWeaponBase* Weapon : PrimaryWeapons) if (GetOwner()->HasAuthority() && IsValid(Weapon)) Weapon->Destroy(); // 随Pawn结束，不泄漏到新World。
    if (GetOwner()->HasAuthority() && IsValid(SecondaryWeapon)) SecondaryWeapon->Destroy();
    PrimaryWeapons.Empty();
    ActiveWeapon = SecondaryWeapon = nullptr;
    Super::EndPlay(EndPlayReason);
}
bool UDemoWeaponComponent::EquipSlot(int32 Slot)
{
    DEMO_LOG_CALL();
    if (!GetOwner()->HasAuthority()) { ServerEquipSlot(Slot); return true; } // true仅表示请求已发送，最终槽位由服务器复制。
    ADemoCharacter* Character = GetCharacter(); // 通用行为门禁归Avatar/控制器请求层，本库存不依赖本地UI对象。
    if (!GetOwner()->HasAuthority() || !Character || !Character->CanUsePlayerSkills() || (Slot != 1 && Slot != 2))
    { UE_LOG(LogFPSDemo, Log, TEXT("Equip rejected: authority/slot/action gate")); return false; }
    ADemoWeaponBase* Next = Slot == 1 && PrimaryWeapons.IsValidIndex(PrimaryIndex) ? PrimaryWeapons[PrimaryIndex].Get() : Slot == 2 ? SecondaryWeapon.Get() : nullptr; // 目标库存实例。
    if (!Next) { UE_LOG(LogFPSDemo, Warning, TEXT("Equip rejected: missing instance")); return false; }
    if (UDemoMeleeComponent* Melee=GetOwner()->FindComponentByClass<UDemoMeleeComponent>()) Melee->SetEquipped(false); // 仅合法枪槽才退出近战；空主槽保持当前棒球棍。
    if (Next == ActiveWeapon) return true;
    return CommitEquip(Next, Slot, PrimaryIndex); // 四个装备入口共用原子表现提交，不在切枪重建主AnimBP。
}
bool UDemoWeaponComponent::SelectPrimary(int32 Index)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 旧本地接口只转服务器商店；客户端通过拥有者PC命令RPC发起。
    if (!Mode || !Mode->GetShopSystem() || Index < 0) { UE_LOG(LogFPSDemo, Log, TEXT("Primary selection rejected: authority/shop/index")); return false; }
    return Mode->GetShopSystem()->EquipPrimaryWeapon(DemoWeaponCatalog::IdAt(Index + 1), GetCharacter()).IsApplied();
}
bool UDemoWeaponComponent::ExecutePrimarySelection(FName WeaponId, int32 HeldSlot)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // Shop已完成个人权限、终端、保存事务策略；库存只校验结构安全。
    const FDemoWeaponDefinition* Definition = DemoWeaponCatalog::FindDefinition(WeaponId); // 同步借用稳定目录项。
    const int32 Index = DemoWeaponCatalog::IndexOf(WeaponId) - 1; // 兼容旧主枪数组，但生成身份显式传ID。
    if ((HeldSlot!=1 && HeldSlot!=2) || (HeldSlot==2 && !IsValid(SecondaryWeapon)) || !Character || !Character->HasAuthority() || !Definition || Definition->AllowedSlot != 1 || !PrimaryWeapons.IsValidIndex(Index)
        || !Character->GetDemoAttributes() || Character->GetDemoAttributes()->GetHealth() <= 0.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("Primary executor rejected: authority/definition/inventory/life")); return false; }
    if (!PrimaryWeapons[Index]) PrimaryWeapons[Index] = SpawnWeapon(CatalogClasses.IsValidIndex(Index + 1) ? CatalogClasses[Index + 1] : nullptr, WeaponId);
    if (!PrimaryWeapons[Index]) { UE_LOG(LogFPSDemo, Warning, TEXT("Primary selection failed: asset/init")); return false; }
    return CommitEquip(HeldSlot==2 ? SecondaryWeapon.Get() : PrimaryWeapons[Index].Get(), HeldSlot, Index);
}
void UDemoWeaponComponent::CyclePrimary() { DEMO_LOG_CALL(); UE_LOG(LogFPSDemo, Log, TEXT("Primary cycle rejected: choose unlocked weapon at terminal")); }
const ADemoWeaponBase* UDemoWeaponComponent::GetCatalogWeapon(int32 CatalogIndex) const
{
    DEMO_LOG_TICK();
    const_cast<UDemoWeaponComponent*>(this)->LoadCatalogClasses(); // 仅缓存只读资源，不修改玩家库存或权限。
    return CatalogClasses.IsValidIndex(CatalogIndex) && CatalogClasses[CatalogIndex] ? CatalogClasses[CatalogIndex]->GetDefaultObject<ADemoWeaponBase>() : nullptr;
}
void UDemoWeaponComponent::StartFire()
{
    DEMO_LOG_CALL();
    if(ADemoCasinoRange::Find(GetCharacter()))
    {
        if(!GetOwner()->HasAuthority()) { ServerStartFire(); return; }
        if(GetCharacter()&&GetCharacter()->GetDemoASC()) { FGameplayAbilitySpec Training(UDemoRangeShotAbility::StaticClass()); /* 一次性服务器能力，执行后移除。 */ GetCharacter()->GetDemoASC()->GiveAbilityAndActivateOnce(Training); }
        return;
    }
    if (UDemoMeleeComponent* Melee=GetOwner()->FindComponentByClass<UDemoMeleeComponent>(); Melee && Melee->IsEquipped()) { Melee->RequestAttack(0); return; } // 左键只发送横挥意图，实际左右方向由服务器按DA循环选择。
    if (!GetCharacter() || !GetCharacter()->CanUseFirearmActions())
    { UE_LOG(LogFPSDemo, Log, TEXT("Fire input rejected: local/authority phase or menu gate")); return; } // 拥有者在发送RPC前阻断菜单；服务器仍独立复核阶段/生命。
    if (!GetOwner()->HasAuthority()) { ServerStartFire(); return; }
    if (!GetOwner()->HasAuthority() || !ActiveWeapon)
    { UE_LOG(LogFPSDemo, Log, TEXT("Fire input rejected: state/weapon")); return; }
    if (ActiveWeapon->IsReloading() && (ActiveWeapon->Config.bReloadOneRoundAtATime || ActiveWeapon->IsInMagazineRecovery()) && ActiveWeapon->GetAmmo() >= ActiveWeapon->Config.AmmoPerShot)
    {
        // 逐颗武器沿用已有可射弹；整匣只有本次插入已提交才可中断尾段，即便旧弹匣本来还有弹。
        GetCharacter()->GetDemoASC()->CancelDemoAbility(UDemoReloadAbility::StaticClass());
        UE_LOG(LogFPSDemo, Log, TEXT("RELOAD_INTERRUPTED_BY_FIRE ammo=%d"),ActiveWeapon->GetAmmo());
    }
    bFireHeld = true;
    TryFireHeld();
}
void UDemoWeaponComponent::StopFire()
{
    DEMO_LOG_CALL();
    if (!GetOwner()->HasAuthority()) { ServerStopFire(); return; }
    bFireHeld = false;
    bCoreFollowupQueued = false;
    CoreFollowupWeapon.Reset();
    GetWorld()->GetTimerManager().ClearTimer(FireTimer);
}
bool UDemoWeaponComponent::QueueCoreFollowup(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 只借用首枪提交的当前Avatar，不为已经停止输入的玩家补发。
    if (!Character || !Character->HasAuthority() || !Character->CanUseFirearmActions() || !bFireHeld
        || !IsEquipped(Weapon) || bCoreFollowupQueued || bCoreFollowupExecuting)
    { UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_FOLLOWUP_REJECT authority/input/equipment/pending")); return false; }
    CoreFollowupWeapon = Weapon;
    bCoreFollowupQueued = true;
    return true;
}
bool UDemoWeaponComponent::IsCoreFollowupExecuting() const
{
    DEMO_LOG_TICK();
    return bCoreFollowupExecuting;
}
void UDemoWeaponComponent::TryFireHeld()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 每次Timer重新取Owner，避免旧武器继续连射。
    ADemoWeaponBase* Weapon = ActiveWeapon; // 本次输入执行锁定实例。
    if (!bFireHeld || !Character || !Character->CanUseFirearmActions() || !Weapon || Weapon->IsReloading()) { StopFire(); return; }
    if (Weapon->GetAmmo() < Weapon->Config.AmmoPerShot)
    {
        StopFire();
        RequestReload(true); // 空弹输入与R共享同一接口；没有射击成本/冷却/枪声。
        return;
    }
    const bool bQueuedFollowup = bCoreFollowupQueued && CoreFollowupWeapon.Get() == Weapon; // 首枪同步提交后才可能为true。
    const float Remaining = bQueuedFollowup ? FMath::Max(Weapon->GetFireCooldownRemaining(),Weapon->GetBoltCycleRemaining())
        : Weapon->GetFireCooldownRemaining(); // 短连额外等待完整枪机周期，绝不缩短机械门禁。
    if (bQueuedFollowup && Remaining <= KINDA_SMALL_NUMBER)
    { bCoreFollowupQueued = false; CoreFollowupWeapon.Reset(); } // 先消费队列，再进入同步GAS；第二枪不得递归继承队列。
    // 成本/标签失败必须停止本次输入，不能在0冷却时创建每毫秒重试的空转Timer。
    if (Remaining <= KINDA_SMALL_NUMBER)
    {
        TGuardValue<bool> FollowupGuard(bCoreFollowupExecuting,bQueuedFollowup); // 同步GA及Preview/Commit可见，失败/提前返回也恢复原值。
        if (!Character->GetDemoASC() || !Character->GetDemoASC()->ActivateDemoAbility(UDemoFireAbility::StaticClass()))
        { UE_LOG(LogFPSDemo, Log, TEXT("Fire request rejected by GAS; release required")); StopFire(); return; }
    }
    if (ActiveWeapon != Weapon || !bFireHeld) return;
    // 最后一发后立即停止，必须再次按键才自动装填，不能由旧的连射Timer代替新按键。
    const bool bContinueCore = bCoreFollowupQueued && CoreFollowupWeapon.Get() == Weapon; // 可由刚刚成功的首枪提交设置。
    if ((!bContinueCore && Weapon->Config.FireMode != EDemoFireMode::Automatic) || Weapon->GetAmmo() < Weapon->Config.AmmoPerShot || Weapon->IsReloading()) { StopFire(); return; }
    const float NextDelay = bContinueCore ? FMath::Max(Weapon->GetFireCooldownRemaining(),Weapon->GetBoltCycleRemaining()) : Weapon->GetFireCooldownRemaining(); // 秒，继续共用唯一射击Timer。
    GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &UDemoWeaponComponent::TryFireHeld, FMath::Max(.001f,NextDelay), false);
}
bool UDemoWeaponComponent::RequestReload(bool bFromEmpty)
{
    DEMO_LOG_CALL();
    if (const UDemoMeleeComponent* Melee=GetOwner()->FindComponentByClass<UDemoMeleeComponent>(); Melee && Melee->IsEquipped()) { UE_LOG(LogFPSDemo,Log,TEXT("Reload rejected: melee equipped")); return false; } // 近战无弹匣，不能补隐藏枪。
    if (!GetCharacter() || !GetCharacter()->CanUseFirearmActions())
    { UE_LOG(LogFPSDemo, Log, TEXT("Reload input rejected: local/authority phase or menu gate")); return false; } // 客户端不会从阻塞菜单补发旧输入；权威与每次装填完成再次检查。
    if (!GetOwner()->HasAuthority()) { ServerRequestReload(bFromEmpty); return true; } // 本地不补弹，结果由权威装填状态复制。
    ADemoCharacter* Character = GetCharacter(); // 持有者的ASC用于统一装填生命周期。
    if (Character && ActiveWeapon && ActiveWeapon->IsReloading())
    {
        UDemoRunBuildComponent* Build = UDemoRunBuildComponent::FindForPawn(Character); // 二次R只提交意图，服务器基于原装填时间判窗。
        const bool bHandled = !bFromEmpty && Build && Build->NotifyReloadPressed(ActiveWeapon);
        UE_LOG(LogFPSDemo,Log,TEXT("RELOAD_REPEAT handled=%d empty=%d"),bHandled,bFromEmpty);
        return bHandled; // 无论判窗成功与否，都不重启或重复授予Reload GA。
    }
    if (!GetOwner()->HasAuthority() || !Character || !Character->CanUseFirearmActions() || !ActiveWeapon || !ActiveWeapon->CanReload())
    { UE_LOG(LogFPSDemo, Log, TEXT("Reload request rejected: state/full/no reserve/pending")); return false; }
    StopFire();
    StopAim();
    UE_LOG(LogFPSDemo, Log, TEXT("RELOAD_REQUEST source=%s weapon=%s"), bFromEmpty ? TEXT("EmptyFire") : TEXT("Manual"), *ActiveWeapon->GetName());
    return Character->GetDemoASC()->ActivateDemoAbility(UDemoReloadAbility::StaticClass());
}
void UDemoWeaponComponent::CycleAim()
{
    DEMO_LOG_CALL();
    if (UDemoMeleeComponent* Melee=GetOwner()->FindComponentByClass<UDemoMeleeComponent>(); Melee && Melee->IsEquipped()) { Melee->RequestAttack(1); return; } // 右键招式来自DA第1项，不进入枪械开镜。
    // 拉栓中右手离开握把，禁止立刻重新开镜把动作隐藏；冷却截止后恢复原瞄准流程。
    if (ActiveWeapon && ActiveWeapon->GetBoltCycleRemaining() > KINDA_SMALL_NUMBER) { UE_LOG(LogFPSDemo, Log, TEXT("Aim rejected: bolt cycle")); return; }
    if (!GetOwner()->HasAuthority()) { ServerCycleAim(); return; }
    if (ActiveWeapon && ActiveWeapon->Config.bSupportsScope && ActiveWeapon->IsInMagazineRecovery() && GetCharacter() && GetCharacter()->CanUseCombatAbilities())
        GetCharacter()->GetDemoASC()->CancelDemoAbility(UDemoReloadAbility::StaticClass()); // 入匣后瞄准可接管回握，GA照常复核权限。
    ADemoCharacter* Character = GetCharacter(); // 本次借用本地Pawn。
    if (!GetOwner()->HasAuthority() || !Character || !Character->CanUseCombatAbilities() || !ActiveWeapon || !ActiveWeapon->Config.bSupportsScope || ActiveWeapon->IsReloading())
    { UE_LOG(LogFPSDemo, Log, TEXT("Aim rejected: phase/weapon/reload")); return; }
    if (ScopeLevel == 0) Character->GetDemoASC()->ActivateDemoAbility(UDemoAimAbility::StaticClass());
    else if (ScopeLevel == 1) SetScopeLevel(2);
    else StopAim();
}
bool UDemoWeaponComponent::BeginAim()
{
    DEMO_LOG_CALL();
    if (const UDemoMeleeComponent* Melee=GetOwner()->FindComponentByClass<UDemoMeleeComponent>(); Melee && Melee->IsEquipped()) { UE_LOG(LogFPSDemo,Log,TEXT("Aim rejected: melee equipped")); return false; } // GA直接激活同样受装备互斥限制。
    // GAS直接激活同样检查机械周期，避免绕过CycleAim输入入口。
    if (ActiveWeapon && ActiveWeapon->GetBoltCycleRemaining() > KINDA_SMALL_NUMBER) { UE_LOG(LogFPSDemo, Log, TEXT("BeginAim rejected: bolt cycle")); return false; }
    ADemoCharacter* Character = GetCharacter(); // Aim激活时借用Owner及摄像机以保存原FOV。
    if (!GetOwner()->HasAuthority() || !Character || !Character->CanUseCombatAbilities() || !ActiveWeapon || !ActiveWeapon->Config.bSupportsScope || ActiveWeapon->IsReloading())
    { UE_LOG(LogFPSDemo, Log, TEXT("BeginAim rejected: character/phase/weapon/reload")); return false; }
    UnscopedFOV = Character->GetFirstPersonCameraComponent()->FieldOfView;
    SetScopeLevel(1);
    return true;
}
void UDemoWeaponComponent::SetScopeLevel(int32 Level)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 服务器控制Scope真值；本地表现由统一OnRep路径执行。
    if (!Character || !Character->HasAuthority() || !ActiveWeapon || (Level != 1 && Level != 2)) return;
    ScopeLevel = Level;
    Character->RefreshMovementSpeed();
    OnRep_Scope(); // ListenServer拥有者不会收到自己的OnRep，需要显式使用同一表现入口。
    Character->ForceNetUpdate();
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
    if (!GetOwner()->HasAuthority() || ScopeLevel == 0) return;
    ScopeLevel = 0;
    if (ADemoCharacter* Character = GetCharacter()) { Character->RefreshMovementSpeed(); Character->ForceNetUpdate(); } // 服务器移动减速随Scope撤销。
    OnRep_Scope();
}
void UDemoWeaponComponent::CancelActions()
{
    DEMO_LOG_CALL();
    if (UDemoMeleeComponent* Melee=GetOwner()->FindComponentByClass<UDemoMeleeComponent>()) Melee->CancelAttack(); // 菜单/阶段/死亡同栈撤销近战，不重置后摇截止。
    if (GetCharacter() && GetCharacter()->GetWeaponAnimationComponent()) GetCharacter()->GetWeaponAnimationComponent()->ClearBoltCycle(); // 本地菜单/死亡立即清表现，权威截止仍由武器保留。
    if (!GetOwner()->HasAuthority()) { ServerCancelActions(); return; }
    StopFire();
    StopAim();
    if (GetCharacter() && GetCharacter()->GetDemoASC()) GetCharacter()->GetDemoASC()->CancelDemoAbility(UDemoReloadAbility::StaticClass());
    if (ActiveWeapon) ActiveWeapon->CancelReload();
}
void UDemoWeaponComponent::ReconcileGambleCapacity(const FDemoLoadoutSnapshot* Before)
{
    DEMO_LOG_CALL();
    if(!GetOwner()->HasAuthority()) { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_CAPACITY rejected authority")); return; }
    for(int32 Index=0;Index<=PrimaryWeapons.Num();++Index) // 所有实际持有枪械，含未装备主枪。
    {
        ADemoWeaponBase* Weapon=Index==0?SecondaryWeapon.Get():PrimaryWeapons[Index-1].Get(); // 本栈借用库存实例。
        if(!Weapon) continue;
        if(Before)
        {
            for(const auto& Saved:Before->Weapons) if(Saved.Id==Weapon->GetWeaponId()) Weapon->RestoreAmmo(Saved.Ammo,Saved.Reserve); // 策略Tag已先回滚，因此原容量合法。
        }
        else
        {
            const int32 Excess=FMath::Max(0,Weapon->GetAmmo()-Weapon->GetCapacity()); // 只回收超容部分，不免费填满扩容。
            if(Excess>0) Weapon->RestoreAmmo(Weapon->GetAmmo()-Excess,FMath::Min(100000,Weapon->GetReserveAmmo()+Excess));
        }
    }
}
void UDemoWeaponComponent::RefillAll(bool bFromReserveOnly)
{
    DEMO_LOG_CALL();
    for (ADemoWeaponBase* Weapon : PrimaryWeapons) if (IsValid(Weapon)) Weapon->Refill(bFromReserveOnly);
    if (SecondaryWeapon) SecondaryWeapon->Refill(bFromReserveOnly);
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
float UDemoWeaponComponent::GetAimMoveMultiplier() const
{
    DEMO_LOG_TICK();
    if (ScopeLevel <= 0 || !ActiveWeapon) return 1.f;
    float Multiplier = ActiveWeapon->Config.AimMoveMultiplier;
    if (const UDemoRunBuildComponent* Build = UDemoRunBuildComponent::FindForPawn(GetCharacter()))
    {
        const float Mitigation = Build->GetAimMoveMitigation(); // B09-C蓄势>=50%时抵消部分开镜减速，服务端与拥有者客户端使用同源复制状态。
        if (Mitigation > 0.f) Multiplier = FMath::Lerp(Multiplier, 1.f, Mitigation);
    }
    return Multiplier;
}
bool UDemoWeaponComponent::IsEquipped(const ADemoWeaponBase* Weapon) const
{
    DEMO_LOG_TICK();
    const UDemoMeleeComponent* Melee=GetOwner()->FindComponentByClass<UDemoMeleeComponent>(); // 库存枪仍保留，但近战期间不能通过GAS直接调用发射/装填。
    return Weapon && ActiveWeapon==Weapon && !(Melee && Melee->IsEquipped());
}

bool UDemoWeaponComponent::CommitEquip(ADemoWeaponBase* Weapon, int32 Slot, int32 NewPrimaryIndex)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 组件Owner同步借用，不改变现有装备权限，由各公开入口校验。
    UDemoWeaponAnimationComponent* Animation = Character ? Character->GetWeaponAnimationComponent() : nullptr; // Pawn拥有，负责预检和链接。
    FString Error; // 层配置失败原因，仅本次提交使用。
    const bool bNeedsLocalPresentation = Character && Character->IsLocallyControlled(); // 远程Pawn与专服不以本地动画加载状态决定装备业务。
    if (!Character || !Character->HasAuthority() || !Weapon || (bNeedsLocalPresentation && (!Animation || !Animation->ValidateWeapon(Weapon, Error))))
    { UE_LOG(LogFPSDemo, Error, TEXT("WEAPON_EQUIP_REJECT target=%s reason=%s"), *GetNameSafe(Weapon), *Error); return false; }
    ADemoWeaponBase* Previous = ActiveWeapon; // 保留旧库存实例，失败时恢复可见/槽位，不恢复被取消的换弹。
    const int32 PreviousSlot = ActiveSlot; // 同步事务开始时的槽位。
    const int32 PreviousPrimaryIndex = PrimaryIndex; // 同步事务开始时的主枪选择。
    CancelActions();
    if (Previous) Previous->SetEquipped(false);
    if (UDemoMeleeComponent* Melee=GetOwner()->FindComponentByClass<UDemoMeleeComponent>()) Melee->SetEquipped(false); // 终端/读档提交同样退出独立近战槽。
    ActiveWeapon = Weapon;
    ActiveSlot = Slot;
    PrimaryIndex = NewPrimaryIndex;
    if (bNeedsLocalPresentation && !Animation->EquipWeapon(Weapon))
    {
        ActiveWeapon = Previous;
        ActiveSlot = PreviousSlot;
        PrimaryIndex = PreviousPrimaryIndex;
        if (Previous) { Animation->EquipWeapon(Previous); Previous->SetEquipped(true); }
        UE_LOG(LogFPSDemo, Error, TEXT("WEAPON_EQUIP_ROLLBACK target=%s previous=%s"), *GetNameSafe(Weapon), *GetNameSafe(Previous));
        return false;
    }
    Weapon->SetEquipped(true);
    if (Previous != Weapon)
        if (UDemoRunBuildComponent* Build = UDemoRunBuildComponent::FindForPawn(Character)) Build->NotifyWeaponChanged(Previous,Weapon); // 仅成功提交装备后发布，回滚不消费跨枪许可。
    Character->ForceNetUpdate();
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_EQUIP_COMMITTED target=%s slot=%d primary=%d"), *Weapon->GetName(), Slot, NewPrimaryIndex);
    return true;
}

void UDemoWeaponComponent::LoadCatalogClasses()
{
    DEMO_LOG_TICK();
    if (CatalogClasses.Num() == DemoWeaponCatalog::Num()) return;
    CatalogClasses.Reset();
    for (int32 Index = 0; Index < DemoWeaponCatalog::Num(); ++Index) // 历史Class覆盖保留，新追加定义直接使用目录软类。
    {
        const FDemoWeaponDefinition& Definition = DemoWeaponCatalog::GetCatalog()->Definitions[Index]; // 目录持有，只借用本轮加载。
        const TSoftClassPtr<ADemoWeaponBase> LegacyClass = Index == 0 ? SecondaryWeaponClass
            : PrimaryWeaponClasses.IsValidIndex(Index - 1) ? PrimaryWeaponClasses[Index - 1] : TSoftClassPtr<ADemoWeaponBase>(); // 旧角色BP显式修改的类仍可覆盖；未改的默认路径不遮蔽新目录配置。
        const TArray<FDemoWeaponDefinition>& Native = GetDefault<UDemoWeaponDefinitionCatalog>()->Definitions; // 原生历史资源路径仅用于识别兼容覆盖，不形成第二套武器数值。
        const bool bCustomLegacyOverride = !LegacyClass.IsNull() && (!Native.IsValidIndex(Index) || LegacyClass != Native[Index].WeaponClass); // 项目新目录优先于旧默认路径。
        const TSoftClassPtr<ADemoWeaponBase> Class = bCustomLegacyOverride ? LegacyClass : Definition.WeaponClass; // 最终类CDO仍是Config唯一来源。
        CatalogClasses.Add(Class.LoadSynchronous());
    }
}
void UDemoWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL();
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UDemoWeaponComponent, PrimaryWeapons, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UDemoWeaponComponent, SecondaryWeapon, COND_OwnerOnly);
    DOREPLIFETIME(UDemoWeaponComponent, ActiveWeapon);
    DOREPLIFETIME(UDemoWeaponComponent, ActiveSlot);
    DOREPLIFETIME_CONDITION(UDemoWeaponComponent, PrimaryIndex, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UDemoWeaponComponent, ScopeLevel, COND_OwnerOnly);
}
void UDemoWeaponComponent::OnRep_Loadout()
{
    DEMO_LOG_CALL();
    LoadCatalogClasses();
    for (ADemoWeaponBase* Weapon : PrimaryWeapons) if (IsValid(Weapon)) Weapon->RefreshReplicatedPresentation(); // 引用和Owner先后到达均可重入修复。
    if (SecondaryWeapon) SecondaryWeapon->RefreshReplicatedPresentation();
    if (ActiveWeapon) ActiveWeapon->RefreshReplicatedPresentation();
    OnRep_Scope();
}
void UDemoWeaponComponent::OnRep_Scope()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = GetCharacter(); // 只操作拥有者的第一人称摄像机，队友不会被改FOV。
    if (!Character || !Character->IsLocallyControlled()) return;
    if (ScopeLevel > 0 && !ActiveWeapon) return; // 装备Actor未解析时延后，OnRep_Loadout重试。
    if (ScopeLevel > 0)
    {
        if (PresentedScopeLevel == 0) UnscopedFOV = Character->GetFirstPersonCameraComponent()->FieldOfView;
        Character->GetFirstPersonCameraComponent()->SetFieldOfView(ScopeLevel == 1 ? ActiveWeapon->Config.ScopeFOV : ActiveWeapon->Config.DeepScopeFOV);
        Character->GetMesh1P()->SetHiddenInGame(true, false);
        ActiveWeapon->SetScopedVisual(true);
    }
    else if (PresentedScopeLevel > 0)
    {
        Character->GetFirstPersonCameraComponent()->SetFieldOfView(UnscopedFOV);
        Character->GetMesh1P()->SetHiddenInGame(false, false);
        if (ActiveWeapon) ActiveWeapon->SetScopedVisual(false);
    }
    PresentedScopeLevel = ScopeLevel;
}
void UDemoWeaponComponent::ServerStartFire_Implementation() { DEMO_LOG_CALL(); StartFire(); }
void UDemoWeaponComponent::ServerStopFire_Implementation() { DEMO_LOG_CALL(); StopFire(); }
void UDemoWeaponComponent::ServerRequestReload_Implementation(bool bFromEmpty) { DEMO_LOG_CALL(); RequestReload(bFromEmpty); }
void UDemoWeaponComponent::ServerEquipSlot_Implementation(int32 Slot) { DEMO_LOG_CALL(); EquipSlot(Slot); }
void UDemoWeaponComponent::ServerCycleAim_Implementation() { DEMO_LOG_CALL(); CycleAim(); }
void UDemoWeaponComponent::ServerCancelActions_Implementation() { DEMO_LOG_CALL(); CancelActions(); }

void UDemoWeaponComponent::CaptureSnapshot(FDemoLoadoutSnapshot& Data, bool bResetAmmo) const
{
    DEMO_LOG_CALL();
    if (ShopCandidateIndex != INDEX_NONE && !bResetAmmo)
    {
        Data = ShopTarget; // 完整本地保存捕获候选；活体仍持旧枪，无提前装备通知/复制或供弹。
        UE_LOG(LogFPSDemo, Log, TEXT("SHOP_LOADOUT_CAPTURE prepared target=%s"), *Data.PrimaryId.ToString());
        return;
    }
    GetOwner()->FindComponentByClass<UDemoAmmoComponent>()->CaptureSnapshot(Data);
    Data.Weapons.Reset();
    Data.PrimaryId = PrimaryWeapons.IsValidIndex(PrimaryIndex) && PrimaryWeapons[PrimaryIndex] ? PrimaryWeapons[PrimaryIndex]->GetWeaponId() : NAME_None;
    Data.ActiveSlot = ActiveSlot;
#if WITH_EDITOR
    const ADemoPlayerState* CaptureState=GetCharacter()?GetCharacter()->GetPlayerState<ADemoPlayerState>():nullptr; // 本Pawn来源账本，绝不从菜单的调试投影回写权限。
    const UDemoPlayerProgressComponent* CaptureProgress=CaptureState?CaptureState->GetProgressComponent():nullptr;
    const FDemoWeaponOwnership* CaptureOwnership=CaptureProgress?&CaptureProgress->GetSnapshot().Ownership:nullptr; // 只在本次同步捕获借用。
    const bool bEditorPrimaryRedirected=EditorTestWeaponIds.Contains(Data.PrimaryId)&&(!CaptureOwnership||!CaptureOwnership->Contains(Data.PrimaryId)); // 只有测试主枪回退可修正缺失目标，不掩盖普通库存错误。
    if (bEditorPrimaryRedirected)
    {
        Data.PrimaryId=CaptureOwnership&&CaptureOwnership->Contains(EditorTestCheckpointPrimaryId)?EditorTestCheckpointPrimaryId:NAME_None;
        if (Data.ActiveSlot==1) Data.ActiveSlot=2; // 当前测试主枪不入档；载入时安全持手枪，合法主枪选择本身保留。
    }
#endif
    for (int32 Index = 0; Index <= PrimaryWeapons.Num(); ++Index) // 数组只遍历库存；实际保存身份读取各Actor冻结的DefinitionId。
    {
        const ADemoWeaponBase* Weapon = Index == 0 ? SecondaryWeapon.Get() : PrimaryWeapons[Index - 1].Get(); // 同步借用，不把Actor指针写入快照。
        if (!Weapon) continue;
#if WITH_EDITOR
        if (EditorTestWeaponIds.Contains(Weapon->GetWeaponId()) && (!CaptureOwnership||!CaptureOwnership->Contains(Weapon->GetWeaponId()))) continue; // 仅过滤本入口标记的借用物；其他非法库存仍由严格存档验证拒绝。
#endif
        FDemoWeaponSnapshot Snapshot; // 领域值独立于SaveGame序列化格式。
        Snapshot.Id = Weapon->GetWeaponId();
        Snapshot.Ammo = bResetAmmo ? Weapon->Config.MagazineCapacity + (Weapon->Config.bFixedMagazineCapacity?0:FMath::Max(0, FMath::FloorToInt(Data.MagazineBonus))) : Weapon->GetAmmo(); // 重开使用快照成长；固定双管不能在保存补给时膨胀。
        Snapshot.Reserve = bResetAmmo ? Weapon->Config.InitialReserve : Weapon->GetReserveAmmo();
        Data.Weapons.Add(Snapshot);
    }
#if WITH_EDITOR
    if (bEditorPrimaryRedirected&&!Data.PrimaryId.IsNone())
    {
        bool bFallbackHeld=false; // 真正保留的库存还必须包含该合法型号，换槽/恢复后不能留下悬空PrimaryId。
        for (const FDemoWeaponSnapshot& Held:Data.Weapons) if (Held.Id==Data.PrimaryId) bFallbackHeld=true;
        if (!bFallbackHeld) { Data.PrimaryId=NAME_None; Data.ActiveSlot=2; }
    }
#endif
    UE_LOG(LogFPSDemo, Log, TEXT("LOADOUT_CAPTURE owner=%s primary=%s active=%d count=%d restart=%d"), *GetNameSafe(GetOwner()), *Data.PrimaryId.ToString(), Data.ActiveSlot, Data.Weapons.Num(), bResetAmmo);
}
bool UDemoWeaponComponent::PrepareSnapshot(const FDemoLoadoutSnapshot& Data, const TArray<FName>& UnlockedWeapons)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character=GetCharacter(); // 当前拥有者，仅服务器可建立恢复事务。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 仅安全阶段允许重建库存。
    UDemoAmmoComponent* Ammo=GetOwner()->FindComponentByClass<UDemoAmmoComponent>(); // 弹药类型预检不修改GE。
    if (RestoreTransaction || !Character || !Character->HasAuthority() || !ActiveWeapon || !State || (State->Phase!=EDemoPhase::Hub && State->Phase!=EDemoPhase::Intermission)
        || (Data.ActiveSlot!=1 && Data.ActiveSlot!=2) || Data.Weapons.IsEmpty() || Data.Weapons.Num()>DemoWeaponCatalog::Num() || !Ammo || !Ammo->CanRestoreSnapshot(Data))
    { UE_LOG(LogFPSDemo,Warning,TEXT("LOADOUT_PREPARE rejected phase/data/readiness/reentry")); return false; }
    TSet<FName> Seen; // 整份库存先校验，再分配任何资源。
    for (const FDemoWeaponSnapshot& Saved : Data.Weapons)
    {
        if (Seen.Contains(Saved.Id) || !DemoWeaponCatalog::FindDefinition(Saved.Id) || !UnlockedWeapons.Contains(Saved.Id)
            || Saved.Ammo<0 || Saved.Reserve<0 || Saved.Ammo>100000 || Saved.Reserve>100000)
        { UE_LOG(LogFPSDemo,Warning,TEXT("LOADOUT_PREPARE rejected locked/duplicate/invalid weapon")); return false; }
        Seen.Add(Saved.Id);
    }
    if (!Seen.Contains(TEXT("pistol")) || (!Data.PrimaryId.IsNone() && (!Seen.Contains(Data.PrimaryId) || DemoWeaponCatalog::IndexOf(Data.PrimaryId)<=0))
        || (Data.ActiveSlot==1 && Data.PrimaryId.IsNone())) return false;
    LoadCatalogClasses();
    RestoreTransaction=NewObject<UDemoLoadoutTransaction>(this);
    RestoreTransaction->Target=Data; RestoreTransaction->BeforePrimary=PrimaryWeapons;
    RestoreTransaction->BeforeSecondary=SecondaryWeapon; RestoreTransaction->BeforeActive=ActiveWeapon;
    RestoreTransaction->BeforeSlot=ActiveSlot; RestoreTransaction->BeforeIndex=PrimaryIndex;
    Ammo->CaptureSnapshot(RestoreTransaction->BeforeAmmo);
    RestoreTransaction->PreparedPrimary.SetNum(FMath::Max(0,CatalogClasses.Num()-1));
    for (const FDemoWeaponSnapshot& Saved : Data.Weapons) // 创建完整新库存，旧库存始终保持有效以支持整队回滚。
    {
        const int32 Index=DemoWeaponCatalog::IndexOf(Saved.Id); // 已验证稳定ID。
        ADemoWeaponBase* Weapon=SpawnWeapon(CatalogClasses.IsValidIndex(Index)?CatalogClasses[Index]:nullptr,Saved.Id); // 临时Actor由事务强持有。
        if (!Weapon) { FinishSnapshot(false); return false; }
        Weapon->SetReplicates(false); Weapon->SetEquipped(false); // 准备阶段不对其他客户端发布临时库存。
        if (Index==0) RestoreTransaction->PreparedSecondary=Weapon; else RestoreTransaction->PreparedPrimary[Index-1]=Weapon;
    }
    ADemoWeaponBase* Target=Data.ActiveSlot==2?RestoreTransaction->PreparedSecondary.Get():RestoreTransaction->PreparedPrimary[DemoWeaponCatalog::IndexOf(Data.PrimaryId)-1].Get(); // 目标槽已验证。
    FString Error; // 本地动画依赖也在准备阶段检查，远端Pawn不要求第一人称表现。
    if (Character->IsLocallyControlled() && (!Character->GetWeaponAnimationComponent() || !Character->GetWeaponAnimationComponent()->ValidateWeapon(Target,Error)))
    { UE_LOG(LogFPSDemo,Warning,TEXT("LOADOUT_PREPARE animation failed %s"),*Error); FinishSnapshot(false); return false; }
    return true;
}
bool UDemoWeaponComponent::ApplyPreparedSnapshot()
{
    DEMO_LOG_CALL();
    if (!RestoreTransaction || RestoreTransaction->bInstalled || !GetOwner()->HasAuthority()) return false;
    const FDemoLoadoutSnapshot& Data=RestoreTransaction->Target; // 事务拥有的不可变目标值。
    const int32 Index=Data.PrimaryId.IsNone()?INDEX_NONE:DemoWeaponCatalog::IndexOf(Data.PrimaryId)-1; // 无主枪显式保留空选择。
    ADemoWeaponBase* Target=Data.ActiveSlot==2?RestoreTransaction->PreparedSecondary.Get():RestoreTransaction->PreparedPrimary[Index].Get(); // 已预生成，不再加载资源。
    if (!CommitEquip(Target,Data.ActiveSlot,Index)) return false;
    RestoreTransaction->bInstalled=true;
    PrimaryWeapons=RestoreTransaction->PreparedPrimary; SecondaryWeapon=RestoreTransaction->PreparedSecondary;
    for (const FDemoWeaponSnapshot& Saved : Data.Weapons) // 玩家目标属性已安装，此时恢复弹药不会按旧容量截断。
    {
        const int32 WeaponIndex=DemoWeaponCatalog::IndexOf(Saved.Id); // 仅当前栈使用。
        ADemoWeaponBase* Weapon=WeaponIndex==0?SecondaryWeapon.Get():PrimaryWeapons[WeaponIndex-1].Get(); // 新库存实例。
        Weapon->RestoreAmmo(Saved.Ammo,Saved.Reserve);
    }
    return GetOwner()->FindComponentByClass<UDemoAmmoComponent>()->RestoreSnapshot(Data);
}
bool UDemoWeaponComponent::FinishSnapshot(bool bCommit)
{
    DEMO_LOG_CALL();
    if (!RestoreTransaction) return true;
    if (bCommit && !RestoreTransaction->bInstalled) { UE_LOG(LogFPSDemo,Warning,TEXT("LOADOUT_FINISH rejected uninstalled transaction")); return false; }
    bool bSuccess=true; // 回滚错误必须明确报告，不能标记整队恢复成功。
    if (!bCommit && RestoreTransaction->bInstalled)
    {
        PrimaryWeapons=RestoreTransaction->BeforePrimary; SecondaryWeapon=RestoreTransaction->BeforeSecondary;
        bSuccess=CommitEquip(RestoreTransaction->BeforeActive,RestoreTransaction->BeforeSlot,RestoreTransaction->BeforeIndex);
        if (!GetOwner()->FindComponentByClass<UDemoAmmoComponent>()->RestoreSnapshot(RestoreTransaction->BeforeAmmo)) bSuccess=false;
    }
    const TArray<TObjectPtr<ADemoWeaponBase>>& Discard=bCommit?RestoreTransaction->BeforePrimary:RestoreTransaction->PreparedPrimary; // 确认销毁旧库存，回滚只销毁新资源。
    for (ADemoWeaponBase* Weapon : Discard) if (IsValid(Weapon)) Weapon->Destroy();
    ADemoWeaponBase* DiscardSecondary=bCommit?RestoreTransaction->BeforeSecondary.Get():RestoreTransaction->PreparedSecondary.Get(); // 与主库存使用同一提交方向。
    if (IsValid(DiscardSecondary)) DiscardSecondary->Destroy();
    if (bCommit)
    {
        for (ADemoWeaponBase* Weapon : PrimaryWeapons) if (IsValid(Weapon)) Weapon->SetReplicates(true);
        if (SecondaryWeapon) SecondaryWeapon->SetReplicates(true);
    }
    RestoreTransaction=nullptr; GetOwner()->ForceNetUpdate();
    if (bSuccess) { UE_LOG(LogFPSDemo,Log,TEXT("LOADOUT_TRANSACTION finished commit=%d"),bCommit); }
    else { UE_LOG(LogFPSDemo,Error,TEXT("LOADOUT_TRANSACTION rollback failed")); } // UE_LOG内部控制流必须使用花括号明确分支。
    return bSuccess;
}
bool UDemoWeaponComponent::RestoreSnapshot(const FDemoLoadoutSnapshot& Data)
{
    DEMO_LOG_CALL();
    const ADemoPlayerState* PS=GetCharacter()?GetCharacter()->GetPlayerState<ADemoPlayerState>():nullptr; // 旧单人接口仍验证本人服务器权限。
    if (!PS || !PrepareSnapshot(Data,PS->GetProgressComponent()->GetSnapshot().UnlockedWeaponIds)) return false;
    if (!ApplyPreparedSnapshot()) { FinishSnapshot(false); return false; }
    return FinishSnapshot(true);
}
void UDemoWeaponComponent::CaptureLoadout(UDemoRunSave& Data, bool bResetAmmo) const
{
    DEMO_LOG_CALL();
    FDemoLoadoutSnapshot Snapshot; // 旧SaveGame只是格式适配器，库存核心不再依赖它进行默认初始化或恢复。
    Snapshot.MagazineBonus = Data.MagazineBonus;
    CaptureSnapshot(Snapshot, bResetAmmo);
    Data.Weapons.Reset();
    for (const FDemoWeaponSnapshot& Weapon : Snapshot.Weapons) // 值转换保留V4字段协议。
    {
        FDemoSavedWeapon Saved; // 旧序列化结构，仅此兼容边界使用。
        Saved.Id = Weapon.Id; Saved.Ammo = Weapon.Ammo; Saved.Reserve = Weapon.Reserve;
        Data.Weapons.Add(Saved);
    }
    Data.PrimaryId = Snapshot.PrimaryId; Data.ActiveSlot = Snapshot.ActiveSlot;
    Data.UnlockedAmmoIds = Snapshot.UnlockedAmmoIds; Data.SelectedAmmoId = Snapshot.SelectedAmmoId;
}
bool UDemoWeaponComponent::RestoreLoadout(const UDemoRunSave& Data)
{
    DEMO_LOG_CALL();
    if (!Data.Validate()) { UE_LOG(LogFPSDemo, Warning, TEXT("LOADOUT_RESTORE rejected legacy save validation")); return false; }
    FDemoLoadoutSnapshot Snapshot; // 仅兼容旧调用链，权威权限仍由普通领域恢复再次验证。
    Snapshot.PrimaryId = Data.PrimaryId; Snapshot.ActiveSlot = Data.ActiveSlot; Snapshot.MagazineBonus = Data.MagazineBonus;
    Snapshot.UnlockedAmmoIds = Data.UnlockedAmmoIds; Snapshot.SelectedAmmoId = Data.SelectedAmmoId;
    for (const FDemoSavedWeapon& Saved : Data.Weapons) // 不复制UObject引用或存档元数据。
    {
        FDemoWeaponSnapshot Weapon; // 新领域值。
        Weapon.Id = Saved.Id; Weapon.Ammo = Saved.Ammo; Weapon.Reserve = Saved.Reserve;
        Snapshot.Weapons.Add(Weapon);
    }
    if (Snapshot.Weapons.IsEmpty())
    {
        // 仅旧SaveGame新档允许合法空库存；合作Roster快照仍须完整，不能用此兼容路径掩盖丢失的主枪。
        if (!Snapshot.PrimaryId.IsNone() || Snapshot.ActiveSlot != 2 || !InitializeLoadout() || !SecondaryWeapon)
        { UE_LOG(LogFPSDemo, Warning, TEXT("LOADOUT_DEFAULT_REJECT empty inventory with invalid primary/initialization")); return false; }
        FDemoWeaponSnapshot Pistol; // 把已初始化默认手枪转为完整领域值，不把空Roster快照当作新档。
        Pistol.Id = SecondaryWeapon->GetWeaponId(); Pistol.Ammo = SecondaryWeapon->GetAmmo(); Pistol.Reserve = SecondaryWeapon->GetReserveAmmo();
        Snapshot.Weapons.Add(Pistol);
        UE_LOG(LogFPSDemo, Log, TEXT("LOADOUT_DEFAULT adapted legacy empty save to initial pistol"));
    }
    return RestoreSnapshot(Snapshot);
}
