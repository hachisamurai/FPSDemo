// 职责：GAS可复制属性与健康结算门禁。使用：伤害/治疗/移动通过GE聚合，Pre/PostExecute拒绝非法状态，不直接发奖励。
#include "GAS/DemoAttributeSet.h"
#include "AI/DemoEnemyRoster.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "AbilitySystemComponent.h"
#include "Debug/DemoEditorCheats.h" // 仅Editor的伤害拦截位于医疗遗物之前，测试不会消耗真实救援次数。
// 对应头文件先包含，确保UBT独立编译检查本类头文件自包含；功能逻辑不变。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "GameplayEffectExtension.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "GAS/DemoEffects.h"
#include "Game/DemoGameState.h"
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

// 移动倍率基础值1确保无状态时仍使用原650cm/s步速。
UDemoAttributeSet::UDemoAttributeSet() : FireRateMultiplier(1.f), MoveSpeedMultiplier(1.f), Health(100.f), MaxHealth(100.f), AttackPower(25.f), WeaponDamageBonus(0.f), MagazineBonus(0.f) // 射速独立临时属性，未获得天赋时保持原RPM。
{
	DEMO_LOG_CALL();
}

void UDemoAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	DEMO_LOG_CALL();
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, FireRateMultiplier, COND_None, REPNOTIFY_Always); // 玩家UI/表现与权威使用相同聚合值。
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, WeaponDamageBonus, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, MagazineBonus, COND_None, REPNOTIFY_Always);
}

void UDemoAttributeSet::ClampValue(const FGameplayAttribute& Attribute, float& NewValue) const
{
	DEMO_LOG_TICK();
	if (Attribute == GetHealthAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
    else if (Attribute == GetFireRateMultiplierAttribute()) NewValue = FMath::IsFinite(NewValue)?FMath::Max(NewValue,1.f):1.f; // 无玩法等级上限，拒绝非有限值；GE移除恢复基础1。
	else if (Attribute == GetMaxHealthAttribute()) NewValue = FMath::Max(1.f, NewValue);
	// 局内武器加成独立于敌人攻击力，设置合理上限避免错误配置溢出整数弹药。
	else if (Attribute == GetWeaponDamageBonusAttribute() || Attribute == GetMagazineBonusAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, 10000.f);
	else if (Attribute == GetAttackPowerAttribute()) NewValue = FMath::Max(0.f, NewValue);
	// 只允许减速；到期重新聚合为基础值1，不手动覆盖其他属性。
	else if (Attribute == GetMoveSpeedMultiplierAttribute()) NewValue = FMath::IsFinite(NewValue)?FMath::Clamp(NewValue,.1f,10.f):1.f; // 允许正向加速，有限物理范围与NaN保护，不是天赋等级上限。
}

void UDemoAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	DEMO_LOG_CALL();
	Super::PreAttributeChange(Attribute, NewValue);
	ClampValue(Attribute, NewValue);
}

void UDemoAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	DEMO_LOG_CALL();
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampValue(Attribute, NewValue);
}

bool UDemoAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	DEMO_LOG_CALL();
#if WITH_EDITOR
    if(Data.EvaluatedData.Attribute==GetHealthAttribute()&&DemoEditorCheats::Invulnerable(GetOwningAbilitySystemComponent()->GetAvatarActor())
        &&((Data.EvaluatedData.ModifierOp==EGameplayModOp::Additive&&Data.EvaluatedData.Magnitude<0)
            ||(Data.EvaluatedData.ModifierOp==EGameplayModOp::Override&&Data.EvaluatedData.Magnitude<GetHealth())
            ||(Data.EvaluatedData.ModifierOp==EGameplayModOp::Multiplicitive&&Data.EvaluatedData.Magnitude<1)
            ||(Data.EvaluatedData.ModifierOp==EGameplayModOp::Division&&Data.EvaluatedData.Magnitude>1)))
    { UE_LOG(LogFPSDemo,Log,TEXT("DEBUG_GOD_DAMAGE_BLOCKED magnitude=%.2f"),Data.EvaluatedData.Magnitude);return false; } // 只阻止伤害；治疗、属性初始化和游戏流程不改。
#endif
	// 本项目枪伤/爆炸/持续伤害均通过负向加法Health GE；同时防御更低的Override值。
	if (Data.EvaluatedData.Attribute==GetHealthAttribute() && GetOwningAbilitySystemComponent()->HasMatchingGameplayTag(DemoBossTags::Invulnerable)
		&& ((Data.EvaluatedData.ModifierOp==EGameplayModOp::Additive && Data.EvaluatedData.Magnitude<0)
			|| (Data.EvaluatedData.ModifierOp==EGameplayModOp::Override && Data.EvaluatedData.Magnitude<GetHealth())))
	{ UE_LOG(LogFPSDemo,Log,TEXT("DAMAGE_BLOCKED invulnerable target=%s magnitude=%.1f"),*GetNameSafe(GetOwningActor()),Data.EvaluatedData.Magnitude); return false; }
	if (Data.EvaluatedData.Attribute==GetHealthAttribute())
        if (AActor* Avatar=GetOwningAbilitySystemComponent()->GetAvatarActor()) // 当前ASC Avatar借用；玩家没有Roster组件。
            if (UDemoEnemyRoster* Roster=Avatar->FindComponentByClass<UDemoEnemyRoster>()) Roster->FilterDamage(Data); // GAS统一拦截所有有方向的武器Health GE。
    const FGameplayTag Protection=FGameplayTag::RequestGameplayTag(TEXT("Demo.Gamble.RescueProtection"),false); // 原救援已注册；不存在时不猜测结果。
    const bool bProtectedBefore=Protection.IsValid()&&GetOwningAbilitySystemComponent()->HasMatchingGameplayTag(Protection); // Guard可能同步增加保护，前后差分只标本次触发。
    const bool bDamageBeforeGuard=Data.EvaluatedData.Attribute==GetHealthAttribute()&&FMath::IsFinite(Data.EvaluatedData.Magnitude)
        &&((Data.EvaluatedData.ModifierOp==EGameplayModOp::Additive&&Data.EvaluatedData.Magnitude<0.f)
            ||(Data.EvaluatedData.ModifierOp==EGameplayModOp::Override&&Data.EvaluatedData.Magnitude<GetHealth())
            ||(Data.EvaluatedData.ModifierOp==EGameplayModOp::Multiplicitive&&Data.EvaluatedData.Magnitude<1.f)
            ||(Data.EvaluatedData.ModifierOp==EGameplayModOp::Division&&Data.EvaluatedData.Magnitude>1.f)); // 1HP救援可把伤害改0，仍保留结果类型，不伪造损血。
	// 在Health写入及死亡委托之前截获致死，保护不冒充冲刺躲避。
    if(Data.EvaluatedData.Attribute==GetHealthAttribute()&&!DemoGambleEffects::GuardHealth(Data,GetHealth())) return false;
    const bool bAllowed=Super::PreGameplayEffectExecute(Data); // 只有真正会执行的Health修改器压栈，被拒绝的GE没有Post。
    if(bAllowed&&Data.EvaluatedData.Attribute==GetHealthAttribute())
    {
        FHealthExecuteFrame Frame; Frame.Before=GetHealth(); Frame.Player=Cast<ADemoCharacter>(GetOwningAbilitySystemComponent()->GetAvatarActor());
        Frame.bDamage=Frame.Player.IsValid()&&Frame.Player->HasAuthority()&&bDamageBeforeGuard;
        if(Frame.bDamage)
        {
            Frame.Feedback=UDemoPlayerDamageFeedbackComponent::Find(Cast<APlayerController>(Frame.Player->GetController()));
            Frame.Cue.AvatarGeneration=Frame.Feedback.IsValid()?Frame.Feedback->GenerationFor(Frame.Player.Get()):0;
            const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 冻结命中时的阶段，不使用死亡后的新阶段。
            Frame.Cue.PhaseRevision=State?State->PhaseRevision:0; Frame.Cue.ServerTime=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
            Frame.Cue.MaxHealth=GetMaxHealth(); Frame.Cue.bRescued=!bProtectedBefore&&Protection.IsValid()&&GetOwningAbilitySystemComponent()->HasMatchingGameplayTag(Protection);
            const FGameplayEffectContextHandle& Context=Data.EffectSpec.GetContext(); // 仅同步借用，绝不跨Post保存。
            UAbilitySystemComponent* Source=Context.GetOriginalInstigatorAbilitySystemComponent(); Frame.Enemy=Source?Cast<ADemoEnemy>(Source->GetAvatarActor()):nullptr;
            FGameplayTagContainer Metadata; Data.EffectSpec.GetAllAssetTags(Metadata); // 只接受显式辅助入口的表现标签，旧GE默认Unknown。
            if(Metadata.HasTagExact(DemoPlayerDamageTags::Root))
            {
                if(Metadata.HasTagExact(DemoPlayerDamageTags::Projectile)) Frame.Cue.Kind=EDemoPlayerDamageKind::Projectile;
                else if(Metadata.HasTagExact(DemoPlayerDamageTags::Melee)) Frame.Cue.Kind=EDemoPlayerDamageKind::Melee;
                else if(Metadata.HasTagExact(DemoPlayerDamageTags::Charge)) Frame.Cue.Kind=EDemoPlayerDamageKind::Charge;
                else if(Metadata.HasTagExact(DemoPlayerDamageTags::Area)) Frame.Cue.Kind=EDemoPlayerDamageKind::Area;
                else if(Metadata.HasTagExact(DemoPlayerDamageTags::Global)) Frame.Cue.Kind=EDemoPlayerDamageKind::Global;
                else if(Metadata.HasTagExact(DemoPlayerDamageTags::Periodic)) Frame.Cue.Kind=EDemoPlayerDamageKind::Periodic;
                else if(Metadata.HasTagExact(DemoPlayerDamageTags::Environment)) Frame.Cue.Kind=EDemoPlayerDamageKind::Environment;
                if(Metadata.HasTagExact(DemoPlayerDamageTags::Omni)) Frame.Cue.DirectionMode=EDemoPlayerDamageDirection::Omni;
                else if(Context.Get()&&Context.Get()->HasOrigin())
                {
                    const FVector Direction=(Context.GetOrigin()-Frame.Player->GetActorLocation()).GetSafeNormal(); // 命中当时冻结，客户端不追踪来源Actor。
                    Frame.Cue.DirectionMode=Direction.IsNearlyZero()?EDemoPlayerDamageDirection::Omni:EDemoPlayerDamageDirection::Directional; Frame.Cue.WorldDirection=Direction;
                }
            }
        }
        HealthExecuteStack.Add(Frame);
    }
    return bAllowed;
}
void UDemoAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute,float OldValue,float NewValue)
{
    DEMO_LOG_CALL(); Super::PostAttributeChange(Attribute,OldValue,NewValue);
    if(Attribute!=GetHealthAttribute()||HealthExecuteStack.IsEmpty()) return;
    FHealthExecuteFrame& Frame=HealthExecuteStack.Last(); // 当前执行帧匹配的首次Health写入；只记录，不触发委托/治疗。
    if(!Frame.bDamage||Frame.bCaptured) return;
    Frame.bCaptured=true;
    if(!FMath::IsFinite(OldValue)||!FMath::IsFinite(NewValue)) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_DAMAGE_CAPTURE nonfinite health")); return; }
    Frame.Cue.HealthLost=FMath::Clamp(OldValue-NewValue,0.f,FMath::Max(0.f,Frame.Before)); Frame.Cue.HealthAfter=NewValue; Frame.Cue.bFatal=NewValue<=0.f;
}
void UDemoAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	DEMO_LOG_CALL();
	Super::PostGameplayEffectExecute(Data);
	if (GetHealth() > GetMaxHealth()) SetHealth(GetMaxHealth());
    if(Data.EvaluatedData.Attribute==GetHealthAttribute()&&HealthExecuteStack.Num()>0)
    {
        const FHealthExecuteFrame Frame=HealthExecuteStack.Pop(EAllowShrinking::No); // 发布前先弹栈，派生治疗不能改写这次损血。
        ADemoCharacter* Player=Frame.Player.Get(); ADemoEnemy* Enemy=Frame.Enemy.Get(); // 原弱引用，而非此时可能已替换的Avatar。
        if(Frame.bDamage&&Frame.bCaptured&&(Frame.Cue.HealthLost>0.f||Frame.Cue.bRescued))
        {
            if(Frame.Feedback.IsValid()) Frame.Feedback->Submit(Frame.Cue); // 致死/撤销Session后仍发布已成立事实，由拥有者决定死亡优先级。
            if(Frame.Cue.HealthLost>0.f&&Player&&Player==GetOwningAbilitySystemComponent()->GetAvatarActor()&&!Player->IsCoopEliminated()&&GetHealth()>0.f&&!Frame.Cue.bRescued&&Enemy&&Enemy->IsCombatSessionValid()&&Enemy->GetWorld()==Player->GetWorld()&&Data.EvaluatedData.ModifierOp==EGameplayModOp::Additive&&Data.EvaluatedData.Magnitude<0.f)
                if(UDemoRunBuildComponent* Core=UDemoRunBuildComponent::FindForPawn(Player)) Core->NotifyPlayerDamaged(Enemy,Frame.Cue.HealthLost); // B07保留自身存活/来源门禁，救援不额外积攒反击额度。
        }
        else if(Frame.bDamage&&!Frame.bCaptured) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PLAYER_DAMAGE_CAPTURE no matched write; omitted"));
    }
	UE_LOG(LogFPSDemo, Log, TEXT("GE target=%s attribute=%s magnitude=%.2f health=%.1f weaponBonus=%.1f capacityBonus=%.0f"),
		*GetNameSafe(GetOwningActor()), *Data.EvaluatedData.Attribute.GetName(), Data.EvaluatedData.Magnitude, GetHealth(), GetWeaponDamageBonus(), GetMagazineBonus());
}

// Property 指定复制属性；OldValue 是引擎传入的旧值，不被回调保存。
#define DEMO_REP(Property) void UDemoAttributeSet::OnRep_##Property(const FGameplayAttributeData& OldValue) \
{ DEMO_LOG_CALL(); GAMEPLAYATTRIBUTE_REPNOTIFY(UDemoAttributeSet, Property, OldValue); }
DEMO_REP(Health)
DEMO_REP(FireRateMultiplier) // 与其他属性共用带调用日志的复制回调。
DEMO_REP(MoveSpeedMultiplier)
DEMO_REP(MaxHealth)
DEMO_REP(AttackPower)
DEMO_REP(WeaponDamageBonus)
DEMO_REP(MagazineBonus)
#undef DEMO_REP
