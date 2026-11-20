// 职责：本人经济/成长账本的权威提交和拥有者复制。用法：成功提交后差异同步天赋并广播；不在组件内实现具体天赋行为。
#include "Player/DemoPlayerProgressComponent.h"
// 对应头文件先包含，确保UBT独立编译检查本类头文件自包含；功能逻辑不变。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h" // 本轮结束精确撤销救援保护GE。
#include "Debug/DemoLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Weapons/DemoWeaponCatalog.h"
#if WITH_EDITOR
#include "Player/DemoPlayerProfile.h" // 模式覆盖仅在Editor编译；正式账本不依赖调试状态。
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#endif
UDemoPlayerProgressComponent::UDemoPlayerProgressComponent()
{ DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void UDemoPlayerProgressComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION(UDemoPlayerProgressComponent, PublicSnapshot, COND_OwnerOnly); }
const FDemoPlayerProgressSnapshot& UDemoPlayerProgressComponent::GetSnapshot() const { DEMO_LOG_TICK(); return GetOwner()&&GetOwner()->HasAuthority()?Snapshot:PublicSnapshot; }
FDemoPlayerProgressSnapshot UDemoPlayerProgressComponent::GetPublicSnapshot() const
{ DEMO_LOG_TICK(); FDemoPlayerProgressSnapshot Copy=GetSnapshot(); /* 显式值复制，绝不清空权威棋盘。 */ Copy.Gambling.Mine=Copy.Gambling.Mine.PublicCopy(); return Copy; }
bool UDemoPlayerProgressComponent::Commit(const FDemoPlayerProgressSnapshot& Next,bool bNotify)
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || Next.Coins < 0 || Next.SilverCoins < 0
        || Next.SilverCoins > 100000000 || Next.Kills < 0 || Next.PistolChallenge < 0 || Next.PistolChallenge > 2
        || Next.BestEndlessLevel < 0 || Next.LastRewardLevel < 0 || (!Next.UpgradeProgress.Validate() || !Next.Gambling.Validate())
        || !Next.Ownership.Validate() || Next.UnlockedWeaponIds.Num() > DemoWeaponCatalog::Num() || !Next.UnlockedWeaponIds.Contains(TEXT("pistol")))
    { UE_LOG(LogFPSDemo, Warning, TEXT("Player progress rejected: authority or invalid ledger")); return false; }
    for (const FName Id : Next.UnlockedWeaponIds) // 用同一稳定定义目录验证，新增主武器无需改钱包代码。
        if (!DemoWeaponCatalog::FindDefinition(Id))
        { UE_LOG(LogFPSDemo, Warning, TEXT("Player progress rejected: unknown weapon")); return false; }
    const int32 Revision = Snapshot.Revision < MAX_int32 ? Snapshot.Revision + 1 : 1; // 只由权威提交推进，忽略入参版本。
    const bool Changed=Snapshot.Gambling.Talents!=Next.Gambling.Talents||Snapshot.Gambling.TalentRanks!=Next.Gambling.TalentRanks||Snapshot.Gambling.EquippedRelic!=Next.Gambling.EquippedRelic; // 等级/拥有项/遗物变化触发差异同步；纯钱包变化不刷新，升级不重建其他天赋。
    Snapshot = Next; Snapshot.Revision = Revision; Snapshot.UnlockedWeaponIds=Snapshot.Ownership.EffectiveIds(); // 缓存只能由来源账本派生，旧奖励写缓存不授新权。
#if WITH_EDITOR
    const APlayerController* LocalOwner = Cast<APlayerController>(GetOwner()->GetOwner()); // 只借用此PS所属控制器，禁止把房主GI权限授予其他玩家。
    const UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr; // 同步读取当前World，不访问其他PIE实例。
    const UDemoPlayerProfile* Profile = Instance ? Instance->GetSubsystem<UDemoPlayerProfile>() : nullptr; // GI拥有覆盖，旧检查点的false不能取消本次试玩授权。
    if (LocalOwner && LocalOwner->IsLocalController() && Profile && Profile->IsDebugUnlockAllModesForSession())
    {
        if (!Snapshot.bHellUnlocked || !Snapshot.bEndlessUnlocked)
            UE_LOG(LogFPSDemo, Log, TEXT("DEBUG_UNLOCK_MODES applied to local progress")); // 只记录权限转换，正常交易不会重复关键日志。
        Snapshot.bHellUnlocked = true; Snapshot.bEndlessUnlocked = true; // 可随Editor临时检查点捕获；停止试玩统一删除，Profile事实不变。
    }
#endif
    if(Changed) if(const IAbilitySystemInterface* OwnerASC=Cast<IAbilitySystemInterface>(GetOwner())) DemoGambleEffects::Sync(OwnerASC->GetAbilitySystemComponent(),Snapshot.Gambling); // PS持有ASC，先同步再广播。
    if(bNotify) PublishCommitted(); // 禁止失败事务通过OnChanged提前发送可靠菜单RPC。
    return true;
}
void UDemoPlayerProgressComponent::PublishCommitted()
{
    DEMO_LOG_CALL();
    if(!GetOwner()||!GetOwner()->HasAuthority()) { UE_LOG(LogFPSDemo,Log,TEXT("PROGRESS_PUBLISH rejected authority")); return; }
    PublicSnapshot=GetPublicSnapshot(); // 仅保存成功后发布，不复制候选或未公开雷位。
    GetOwner()->ForceNetUpdate(); OnChanged.Broadcast();
}
bool UDemoPlayerProgressComponent::IsWeaponUnlocked(FName WeaponId) const
{
    DEMO_LOG_TICK();
    if (!DemoWeaponCatalog::FindDefinition(WeaponId)) return false;
#if WITH_EDITOR
    const APlayerController* LocalOwner=GetOwner()?Cast<APlayerController>(GetOwner()->GetOwner()):nullptr; // 不借房主权限给队友。
    const UGameInstance* Instance=GetWorld()?GetWorld()->GetGameInstance():nullptr;
    const UDemoPlayerProfile* Profile=Instance?Instance->GetSubsystem<UDemoPlayerProfile>():nullptr; // 运行期覆盖不入来源账本。
    if (GetWorld()&&GetWorld()->GetNetMode()==NM_Standalone&&LocalOwner&&LocalOwner->IsLocalController()&&Profile&&Profile->IsDebugUnlockAllForSession()) return true; // 仅本人单人试玩；不将旧GI覆盖带入Listen或远端权限。
#endif
    return GetSnapshot().Ownership.Contains(WeaponId);
}
void UDemoPlayerProgressComponent::ResetTemporary()
{
    DEMO_LOG_CALL();
    FDemoPlayerProgressSnapshot Next = Snapshot; // 事务值副本，保留当前永久来源和个人权限。
    if(const IAbilitySystemInterface* OwnerASC=Cast<IAbilitySystemInterface>(GetOwner()))
    { FGameplayEffectQuery Query; Query.EffectDefinition=UDemoGambleProtectionEffect::StaticClass(); /* 只移除本轮遗物保护，不重置正常冲刺/治疗冷却。 */ if(OwnerASC->GetAbilitySystemComponent()) OwnerASC->GetAbilitySystemComponent()->RemoveActiveEffects(Query); }
    Next.Gambling.ResetRun(); // 只清本轮策略，永久研究保留。
    Next.SilverCoins = 0; Next.UpgradeProgress.ResetTemporary(); Next.PistolChallenge = 0; Next.LastRewardLevel = 0;
    Commit(Next);
}
void UDemoPlayerProgressComponent::OnRep_Snapshot() { DEMO_LOG_CALL(); OnChanged.Broadcast(); }
