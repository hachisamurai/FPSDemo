// 职责：枪墙增量库存的准备/耐久后发布屏障。使用：仅权威Shop同步事务调用；CaptureSnapshot保存目标值，旧枪/冷却/弹量不重建，依赖原武器目录与表现预检。
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Player/DemoPlayerState.h" // 测试库存只读本人真实拥有权，不能写Paid/Legacy来源。
#include "Player/DemoPlayerProgressComponent.h"
#include "Save/DemoRunSave.h" // 提供同 GI 的临时重开选择，与磁盘枪权分开。
#endif

#if WITH_EDITOR
void UDemoRunSaves::CaptureEditorRestartWeapon(const UDemoWeaponComponent& Inventory)
{
    DEMO_LOG_CALL();
    EditorRestartRun.Reset();
    EditorRestartPrimary=NAME_None;
    const UDemoRunSave* Checkpoint=ActiveSlot==INDEX_NONE?DetachedRestart.Get():GetSlot(ActiveSlot); // 绑定即将恢复的真实轮次，包括清关异常后的旧检查点。
    if (!Checkpoint || !Inventory.GetWorld() || Inventory.GetWorld()->GetNetMode()!=NM_Standalone || !Inventory.PrimaryWeapons.IsValidIndex(Inventory.PrimaryIndex)) return;
    const ADemoWeaponBase* Weapon=Inventory.PrimaryWeapons[Inventory.PrimaryIndex];
    if (!IsValid(Weapon) || !Inventory.EditorTestWeaponIds.Contains(Weapon->GetWeaponId()) || Checkpoint->Ownership.Contains(Weapon->GetWeaponId())) return;
    EditorRestartRun=Checkpoint->RunId;
    EditorRestartPrimary=Weapon->GetWeaponId();
    EditorRestartSlot=Inventory.ActiveSlot;
    UE_LOG(LogFPSDemo,Log,TEXT("EDITOR_RESTART_WEAPON_CAPTURE id=%s slot=%d"),*EditorRestartPrimary.ToString(),EditorRestartSlot);
}

void UDemoRunSaves::RestoreEditorRestartWeapon(const FString& RunId, UDemoWeaponComponent& Inventory)
{
    DEMO_LOG_CALL();
    if (EditorRestartPrimary.IsNone() || EditorRestartRun!=RunId || !Inventory.GetWorld() || Inventory.GetWorld()->GetNetMode()!=NM_Standalone) return;
    const FName Borrowed=EditorRestartPrimary; // 提前摘下凭据，装备回调重入不能再次使用。
    const int32 Slot=EditorRestartSlot; // 本次提交的手持槽。
    EditorRestartRun.Reset();
    EditorRestartPrimary=NAME_None;
    const bool bRestored=Inventory.ExecuteEditorTestPrimarySelection(Borrowed,Slot);
    UE_LOG(LogFPSDemo,Log,TEXT("EDITOR_RESTART_WEAPON_RESTORE id=%s slot=%d applied=%d"),*Borrowed.ToString(),Slot,bRestored);
}

bool UDemoWeaponComponent::ExecuteEditorTestPrimarySelection(FName WeaponId, int32 HeldSlot)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player=GetCharacter(); // 本同步栈的本机权威Avatar，不接受另一个玩家对象。
    const ADemoPlayerState* State=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr;
    const UDemoPlayerProgressComponent* Progress=State?State->GetProgressComponent():nullptr;
    const FDemoWeaponDefinition* Definition=DemoWeaponCatalog::FindDefinition(WeaponId); // 只接受主枪目录白名单。
    if (!Player||!Player->HasAuthority()||!Player->IsLocallyControlled()||!GetWorld()||GetWorld()->GetNetMode()!=NM_Standalone||!Progress||!Definition||Definition->AllowedSlot!=1||!Progress->IsWeaponUnlocked(WeaponId)||Progress->GetSnapshot().Ownership.Contains(WeaponId)||RestoreTransaction||ShopCandidateIndex!=INDEX_NONE)
    { UE_LOG(LogFPSDemo,Warning,TEXT("EDITOR_TEST_EQUIP_REJECT context/permission/transaction id=%s"),*WeaponId.ToString()); return false; }
    FDemoLoadoutSnapshot Before; CaptureSnapshot(Before); // 此值已剔除先前借用枪，只保存合法主槽，不缓存整份过期弹量。
    const FName PreviousPrimary=EditorTestCheckpointPrimaryId; // 失败恢复本地标记，不改变持久数据。
    const bool AlreadyTracked=EditorTestWeaponIds.Contains(WeaponId); // 同型号重装保留既有Actor和弹药。
    EditorTestCheckpointPrimaryId=Before.PrimaryId; EditorTestWeaponIds.Add(WeaponId); // 在装备回调前先建立捕获屏障。
    if (!ExecutePrimarySelection(WeaponId,HeldSlot))
    {
        const int32 FailedIndex=DemoWeaponCatalog::IndexOf(WeaponId)-1; // executor可能已成功创建Actor、但表现提交失败；该Actor仍须被捕获屏障保护。
        if (!AlreadyTracked&&(!PrimaryWeapons.IsValidIndex(FailedIndex)||!IsValid(PrimaryWeapons[FailedIndex]))) EditorTestWeaponIds.Remove(WeaponId);
        EditorTestCheckpointPrimaryId=PreviousPrimary;
        UE_LOG(LogFPSDemo,Warning,TEXT("EDITOR_TEST_EQUIP_REJECT inventory/presentation id=%s"),*WeaponId.ToString()); return false;
    }
    UE_LOG(LogFPSDemo,Log,TEXT("EDITOR_TEST_EQUIPPED id=%s permanent_grant=0 checkpoint_primary=%s"),*WeaponId.ToString(),*Before.PrimaryId.ToString());
    return true;
}
#endif

bool UDemoWeaponComponent::PrepareShopSelection(FName WeaponId, bool bAllowInitialSupply)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = GetCharacter(); // Owner仅在当前同步栈借用，禁止异步跨Pawn提交。
    const FDemoWeaponDefinition* Definition = DemoWeaponCatalog::FindDefinition(WeaponId); // 目录唯一身份/槽位，数值仍由真实CDO提供。
    const int32 Index = DemoWeaponCatalog::IndexOf(WeaponId) - 1; // 主枪数组不含手枪。
    if (!Player || !Player->HasAuthority() || !Definition || Definition->AllowedSlot != 1 || RestoreTransaction || ShopCandidateIndex != INDEX_NONE
        || !Player->GetDemoAttributes() || Player->GetDemoAttributes()->GetHealth() <= 0.f || !PrimaryWeapons.IsValidIndex(Index))
    { UE_LOG(LogFPSDemo, Log, TEXT("SHOP_LOADOUT_PREPARE_REJECT authority/definition/life/transaction")); return false; }
    LoadCatalogClasses();
    ADemoWeaponBase* Candidate = PrimaryWeapons[Index]; // 已有实例直接借用，禁止免费装备补弹/重置NextFireTime或ShotSequence。
    bool bCreated = false; // 仅本次创建的增量Actor由失败路径销毁。
    if (!Candidate)
    {
        const TSubclassOf<ADemoWeaponBase> Class = CatalogClasses.IsValidIndex(Index + 1) ? CatalogClasses[Index + 1] : nullptr; // 已预载的真实类，不接受客户端路径。
        if (!Class) { UE_LOG(LogFPSDemo, Warning, TEXT("SHOP_LOADOUT_PREPARE_REJECT missing class")); return false; }
        Candidate = GetWorld()->SpawnActorDeferred<ADemoWeaponBase>(Class, Player->GetActorTransform(), Player, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Candidate) { UE_LOG(LogFPSDemo, Warning, TEXT("SHOP_LOADOUT_PREPARE_REJECT allocation")); return false; }
        bCreated = true;
        Candidate->SetReplicates(false); Candidate->SetActorHiddenInGame(true); // 必须在FinishSpawning/BeginPlay之前设置，准备Actor不可被网络公布。
        Candidate->FinishSpawning(Player->GetActorTransform());
        if (!Candidate->BindDefinition(*Definition) || !Candidate->InitializeForOwner(Player, false))
        { Candidate->Destroy(); UE_LOG(LogFPSDemo, Warning, TEXT("SHOP_LOADOUT_PREPARE_REJECT initialization")); return false; }
        if (!bAllowInitialSupply) Candidate->RestoreAmmo(0, 0); // 已消费初始补给但异常缺实例时不再赠弹；正常读档应恢复原保存库存。
    }
    FString Error; // 本人本地表现预检原因；远端Pawn不要求客户端手臂资源就绪才能购买。
    UDemoWeaponAnimationComponent* Animation = Player->GetWeaponAnimationComponent(); // 仅检查，不调用EquipWeapon，不触发原装备副作用。
    if (Player->IsLocallyControlled() && (!Animation || !Animation->ValidateWeapon(Candidate, Error)))
    { if (bCreated) Candidate->Destroy(); UE_LOG(LogFPSDemo, Warning, TEXT("SHOP_LOADOUT_PREPARE_REJECT presentation=%s"), *Error); return false; }
    CaptureSnapshot(ShopTarget); // 设置事务标志前采集旧库存，保全部弹药类型/已持枪实际弹量。
    ShopTarget.MagazineBonus = Player->GetDemoAttributes()->GetMagazineBonus(); // 与Assembler/Coop个人属性捕获一致，不能丢掉合法容量成长。
    bool bTargetCaptured=false; // Editor借用Actor可能存在于活库存、但被合法检查点捕获剔除；正式首购仍须纳入保存目标。
    for (const FDemoWeaponSnapshot& Existing:ShopTarget.Weapons) if (Existing.Id==WeaponId) bTargetCaptured=true;
    if (!bTargetCaptured)
    {
        FDemoWeaponSnapshot Added; // 新增唯一一把，不重建或复制旧枪弹药。
        Added.Id = WeaponId; Added.Ammo = Candidate->GetAmmo(); Added.Reserve = Candidate->GetReserveAmmo();
        ShopTarget.Weapons.Add(Added);
    }
    ShopTarget.PrimaryId = WeaponId; ShopTarget.ActiveSlot = 1;
    ShopCandidate = Candidate; ShopCandidateIndex = Index; bShopCandidateNew = bCreated;
    UE_LOG(LogFPSDemo, Log, TEXT("SHOP_LOADOUT_PREPARED id=%s created=%d"), *WeaponId.ToString(), bCreated);
    return true;
}

bool UDemoWeaponComponent::PublishShopSelection()
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ShopCandidate || !PrimaryWeapons.IsValidIndex(ShopCandidateIndex))
    { UE_LOG(LogFPSDemo, Error, TEXT("SHOP_LOADOUT_PUBLISH_REJECT missing durable candidate; restore saved checkpoint")); return false; }
    ADemoWeaponBase* Target = ShopCandidate; // 保存已成功，之后即使表现失败也保留该库存与权利。
    const int32 Index = ShopCandidateIndex; // 清准备标记后Capture重新采真实库存。
    PrimaryWeapons[Index] = Target; Target->SetReplicates(true);
    ShopCandidate = nullptr; ShopCandidateIndex = INDEX_NONE; bShopCandidateNew = false; ShopTarget = FDemoLoadoutSnapshot();
    const bool bEquipped = CommitEquip(Target, 1, Index); // 只有耐久后才取消旧动作、切表现、RunBuild通知及ForceNetUpdate。
    if (!bEquipped) UE_LOG(LogFPSDemo, Warning, TEXT("SHOP_LOADOUT_DURABLE_PRESENTATION_PENDING id=%s"), *Target->GetWeaponId().ToString());
    return bEquipped;
}

void UDemoWeaponComponent::AbortShopSelection()
{
    DEMO_LOG_CALL();
    if (bShopCandidateNew && IsValid(ShopCandidate)) ShopCandidate->Destroy(); // 旧实例与它的射击序号/截止/弹药完全保留。
    ShopCandidate = nullptr; ShopCandidateIndex = INDEX_NONE; bShopCandidateNew = false; ShopTarget = FDemoLoadoutSnapshot();
}
