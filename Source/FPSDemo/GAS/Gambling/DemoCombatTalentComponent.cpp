// 职责：Avatar寿命的命中预算与冲刺凭据适配。使用：武器提交实际伤害、Dash提交成功窗口，收益值来自天赋系统聚合快照。
#include "GAS/Gambling/DemoCombatTalentComponent.h"
#include "GAS/Talents/DemoTalentCatalog.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

UDemoCombatTalentComponent::UDemoCombatTalentComponent()
{ DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; }
void UDemoCombatTalentComponent::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 当前组件唯一拥有者。
    if(!Pawn||!Pawn->HasAuthority()) return;
    AvatarHandle=Pawn->OnDemoAvatarReady.AddUObject(this,&UDemoCombatTalentComponent::BindASC);
    BindASC();
}
void UDemoCombatTalentComponent::BindASC()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 不跨同步回调缓存裸Avatar。
    UAbilitySystemComponent* ASC=Pawn?Pawn->GetAbilitySystemComponent():nullptr; // PS拥有的GAS。
    if(!Pawn||!Pawn->HasAuthority()||!ASC||ASC==BoundASC.Get()) return;
    if(BoundASC.IsValid()) BoundASC->RegisterGameplayTagEvent(DemoTags::DashEvading).Remove(DashHandle);
    DashWeapon.Reset(); BoundASC=ASC;
    DashHandle=ASC->RegisterGameplayTagEvent(DemoTags::DashEvading).AddUObject(this,&UDemoCombatTalentComponent::OnDashTagChanged);
}
void UDemoCombatTalentComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); DashWeapon.Reset();
    if(BoundASC.IsValid()&&BoundASC->IsOwnerActorAuthoritative()&&BoundASC->GetAvatarActor()==GetOwner()) BoundASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(DemoTalentEvents::CombatTransient)); // 旧Pawn晚EndPlay不能删除新Avatar的效果。
    if(BoundASC.IsValid()) BoundASC->RegisterGameplayTagEvent(DemoTags::DashEvading).Remove(DashHandle);
    if(ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner())) Pawn->OnDemoAvatarReady.Remove(AvatarHandle); // World结束不补弹。
    BoundASC.Reset(); Super::EndPlay(Reason);
}
void UDemoCombatTalentComponent::Siphon(float ActualDamage,float Ratio,float Budget)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 来源命中已通过ShotContext战斗凭据校验。
    const UDemoAttributeSet* Attr=Pawn?Pawn->GetDemoAttributes():nullptr; // 属性只读，治疗必须走GE。
    const UDemoGambleSettings* Settings=GetDefault<UDemoGambleSettings>(); // 上限以当前有效配置为准。
    if(!Pawn||!Pawn->HasAuthority()||!BoundASC.IsValid()||!Attr||Attr->GetHealth()<=0.f
        ||!FMath::IsFinite(ActualDamage)||ActualDamage<=0.f||!FMath::IsFinite(Ratio)||Ratio<=0.f||!FMath::IsFinite(Budget)||Budget<0.f||!Settings->Validate())
    { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_SIPHON_REJECT invalid source/damage/config")); return; }
    const double Now=GetWorld()->GetTimeSeconds(); // 暂停不流逝，单个Pawn不跨World。
    if(HealWindowStart<0.||Now-HealWindowStart>=1.) { HealWindowStart=Now; HealingUsed=0.f; }
    const float Amount=FMath::Min3(ActualDamage*Ratio,FMath::Max(0.f,(Budget>0.f?Budget:Settings->SiphonPerSecond)-HealingUsed),FMath::Max(0.f,Attr->GetMaxHealth()-Attr->GetHealth())); // 过量治疗不消耗预算。
    if(Amount<=0.f) { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_SIPHON_REJECT full health/budget")); return; }
    HealingUsed+=Amount; // GE可能同步触发其他订阅者，先占用窗口预算。
    DemoEffects::Apply(BoundASC.Get(),BoundASC.Get(),UDemoHealthEffect::StaticClass(),Amount);
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_SIPHON applied=%.3f window=%.3f"),Amount,HealingUsed);
}
void UDemoCombatTalentComponent::ArmDash()
{
    DEMO_LOG_CALL(); BindASC(); DashWeapon.Reset(); DashDeadline=-1.; // 新一次成功冲刺不复用旧到期凭据。
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 本次已提交成功的冲刺Avatar。
    if(!Pawn||!BoundASC.IsValid()||!BoundASC->HasMatchingGameplayTag(DemoTags::DashEvading)
        ||GetDefault<UDemoTalentCatalog>()->Evaluate(BoundASC.Get()).DashFraction<=0.f) return;
    const FGameplayEffectQuery Query=FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(DemoTags::DashEvading)); // 查询实际窗口GE，避免复制一份0.45秒硬编码。
    float Remaining=0.f; // 本次躲避标签的最长剩余游戏秒，有限Duration GE才可补弹。
    for(float Seconds:BoundASC->GetActiveEffectsTimeRemaining(Query)) Remaining=FMath::Max(Remaining,Seconds); // 同步只读GE时间，不持有Effect指针跨回调。
    if(Remaining<=0.f) { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_DASHLOAD_REJECT missing finite evade effect")); return; }
    DashFraction=GetDefault<UDemoTalentCatalog>()->Evaluate(BoundASC.Get()).DashFraction; // 按启动时等级冻结装填收益。
    DashDeadline=GetWorld()->GetTimeSeconds()+Remaining;
    DashWeapon=Pawn->GetWeaponComponent()->GetActiveWeapon();
}
void UDemoCombatTalentComponent::OnDashTagChanged(const FGameplayTag Tag,int32 Count)
{
    DEMO_LOG_CALL();
    if(Count!=0) return;
    ADemoWeaponBase* Weapon=DashWeapon.Get(); // 先消费，防止补弹同步回调再次触发。
    DashWeapon.Reset();
    if(!Weapon) return;
    if(GetWorld()->GetTimeSeconds()+KINDA_SMALL_NUMBER<DashDeadline)
    { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_DASHLOAD_REJECT evade removed before expiry")); return; } // 清场/旅行/取消不等于冲刺自然完成。
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 结束时必须仍为原Avatar且可使用技能。
    if(!Pawn||!Pawn->CanUsePlayerSkills()||Pawn->GetWeaponComponent()->GetActiveWeapon()!=Weapon
        ||GetDefault<UDemoTalentCatalog>()->Evaluate(BoundASC.Get()).DashFraction<=0.f||!GetDefault<UDemoGambleSettings>()->Validate())
    { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_DASHLOAD_REJECT changed weapon/life/menu/talent")); return; }
    Weapon->TransferReserveFraction(DashFraction); // 仅使用这次成功冲刺冻结的等级效果。
}
