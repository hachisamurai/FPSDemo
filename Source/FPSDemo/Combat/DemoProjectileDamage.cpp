// 职责：实体弹真实命中伤害。敌人经部位/核心/GAS门禁返回反馈，设施走独立接口；折向不递归触发核心，射线不结算伤害。
#include "Combat/DemoProjectileDamage.h"
#include "Combat/DemoDamageableTarget.h" // 设施有独立ASC与会话门禁，不伪装敌人以复用奖励。
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Combat/DemoEnemyHitZones.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Combat/Feedback/DemoHitFeedbackComponent.h" // 直击实际HP快照转为纯表现事件，与装备切换和伤害权限分离。
#include "AbilitySystemComponent.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"

bool DemoProjectileDamage::Apply(UDemoShotContext* Shot, const FHitResult& Hit, float BaseDamage, int32 PelletIndex, bool bSecondary)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs pellet=%d target=%s"), __FUNCTION__, PelletIndex, *GetNameSafe(Hit.GetActor()));
    if (IDemoDamageableTarget* Target = Cast<IDemoDamageableTarget>(Hit.GetActor())) // 只接受真实运动碰撞；设施直击不累计弱点/击杀/冻结。
    {
        if (!IsValid(Shot) || !Shot->IsAttackValid() || !FMath::IsFinite(BaseDamage) || BaseDamage <= 0.f)
        { UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_OBJECTIVE_REJECT invalid context/value")); return false; }
        FDemoTargetDamageRequest Request; // 同栈借用来源与会话，设施内部再次核对Active和原Avatar。
        Request.Session = Shot->GetCombatSession(); Request.SourceASC = Shot->GetSourceASC(); Request.SourceAvatar = Shot->GetSourcePawn();
        Request.AttackId = Shot->GetShotId(); Request.WeaponId = Shot->GetWeaponId(); Request.Hit = Hit;
        Request.bRedesignedFeedback = Shot->UsesRedesignedFeedback(); // 对设施同样保留原发射AB选择。
        Request.Damage = BaseDamage * Shot->GetRunBuildDamageMultiplier(nullptr, false, Hit.ImpactPoint, bSecondary);
        Request.AmmoType = Shot->GetAmmoType(); Request.Ammo = Shot->GetAmmoSnapshot();
        Request.bApplyElement = !bSecondary && Shot->TryMarkElement(Hit.GetActor()); // 散弹同枪同设施只加一次火DOT，冰冻/击杀等均不进入敌人状态机。
        const FDemoTargetDamageResult Result = Target->ApplyTargetDamage(Request); // 设施自己通过Health GE结算，不发送Enemy直击/天赋事实。
        return Result.bAccepted;
    }
    ADemoEnemy* Enemy = Cast<ADemoEnemy>(Hit.GetActor()); // 只借用运动碰撞实际命中的目标，不查询原瞄准射线。
    UAbilitySystemComponent* SourceASC = IsValid(Shot) ? Shot->GetSourceASC() : nullptr; // Context内弱源解析。
    UAbilitySystemComponent* TargetASC = IsValid(Enemy) ? Enemy->GetAbilitySystemComponent() : nullptr; // 敌人拥有的ASC。
    const UDemoEnemyHitProfile* Profile = IsValid(Enemy) && Enemy->GetHitProfile() ? Enemy->GetHitProfile() : IsValid(Shot) ? Shot->GetHitProfile() : nullptr; // 目标冻结骨架定义优先；旧敌人兼容才用发射时武器规则。
    EDemoEnemyHitRegion Region = EDemoEnemyHitRegion::Body; // 仅ResolveHit成功后可使用，不作为缺骨回退。
    float Multiplier = 0.f; // 区域倍率，失败保持零。
    if (!IsValid(Shot) || !Shot->IsAttackValid() || !IsValid(Enemy) || !Enemy->IsAlive()
        || !Enemy->IsCombatSessionValid() || !SourceASC || !TargetASC || !TargetASC->IsOwnerActorAuthoritative() || !Profile
        || !FMath::IsFinite(BaseDamage) || BaseDamage < 0.f
        || !Profile->ResolveHitForChannel(Hit, Region, Multiplier, ECC_GameTraceChannel3))
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_DAMAGE_REJECT target=%s bone=%s invalid context/authority/body"), *GetNameSafe(Enemy), *Hit.BoneName.ToString());
        return false;
    }
    const float Damage = BaseDamage * Multiplier * Shot->GetConditionalDamageMultiplier(Enemy->GetHealth(),Enemy->GetMaxHealth()) * (TargetASC->HasMatchingGameplayTag(DemoAmmoTags::Frozen)?Shot->GetFrozenDamageMultiplier():1.f)
        * Shot->GetRunBuildDamageMultiplier(Enemy, Region == EDemoEnemyHitRegion::Core, Hit.ImpactPoint, bSecondary); // 核心源端增益独立乘一次；精准批准首敌弱点，折向派生排除。
    if (!FMath::IsFinite(Damage))
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_DAMAGE_REJECT target=%s non-finite multiplied damage"), *GetNameSafe(Enemy));
        return false;
    }
    if (Damage <= 0.f)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_DAMAGE_ZERO target=%s bone=%s valid geometry without HP/element feedback"), *GetNameSafe(Enemy), *Hit.BoneName.ToString());
        return true; // 几何有效的免伤区域仍允许穿透，但不生成GE或元素/反馈。
    }
    const float Before = Enemy->GetHealth(); // 以实际HP变化判断是否击中有效，尊重当帧Boss无敌。
    const EDemoImpactSurface FeedbackSurface = Enemy->GetImpactSurface(Hit.BoneName); // GE/天赋可能同步死亡或清场，先冻结纯表现表面。
    const bool bRedesigned = Shot->UsesRedesignedFeedback(); // 原发射选择，不读当前装备或失效后的Session。
    const FName FeedbackWeapon = Shot->GetWeaponId(); // 切枪后仍使用原武器身份。
    FGameplayEffectContextHandle EffectContext = SourceASC->MakeEffectContext(); // 每颗实际命中独立GE，ShotContext用于全枪反馈去重。
    EffectContext.AddHitResult(Hit, true);
    EffectContext.AddSourceObject(Shot);
    FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(), 1.f, EffectContext); // 不修改GE CDO。
    if (!Spec.IsValid())
    {
        UE_LOG(LogFPSDemo, Error, TEXT("PROJECTILE_DAMAGE_REJECT invalid Health GE spec"));
        return false;
    }
    Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude, -Damage);
    SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
    // 死亡可同步清关并撤销全部子弹；Context由调用栈保活，只读取仍有效对象后再做后续处理。
    const bool bActualDamage = IsValid(Enemy) && Enemy->GetHealth() < Before; // 致死也可有准星反馈，但不新增Debuff。
    const float ActualDamage = bActualDamage ? FMath::Clamp(Before - Enemy->GetHealth(), 0.f, Before) : 0.f; // GE后立即冻结直击HP损失，后续天赋/元素回调不混入飘字。
    const bool bKilled = bActualDamage && Enemy->GetHealth() <= 0.f; // 最后一杀可能已经结束CombatSession，确认显示不再用会话门禁丢弃。
    const float OverkillDamage = bKilled ? FMath::Max(0.f, Damage - ActualDamage) : 0.f; // 致死过量伤害，供B09溢伤追击与连爆消费。
    const uint32 FeedbackTargetId = bActualDamage ? Enemy->GetUniqueID() : 0; // 在天赋回调前冻结纯值，后续反馈不再解引用可能卸载的目标。
    if (bActualDamage) Shot->NotifyRunBuildHit(Enemy, ActualDamage, OverkillDamage, Hit.ImpactPoint, Region == EDemoEnemyHitRegion::Core, bKilled, bSecondary); // 实际HP扣除后才计核心层数/冻结；免疫/打空不能贡献。
    if(bActualDamage) Shot->NotifyDirectHit(ActualDamage,Region==EDemoEnemyHitRegion::Core); // 仅实际HP损失；过量伤害、DOT和爆炸不能虚增虹吸/返弹。
    ADemoCharacter* Source = Shot->GetSourcePawn(); // GE之后重新解析弱源，不能沿用旧Pawn裸指针。
    if (bActualDamage && IsValid(Source))
    {
        if (UDemoHitFeedbackComponent* Feedback = Source->FindComponentByClass<UDemoHitFeedbackComponent>()) // 只借用原射手的Pawn组件，绝不查询玩家0。
        {
            FDemoHitFeedbackEvent Receipt; // 无目标引用；死亡清理/客户端销毁顺序不会阻止准星与数字到达。
            Receipt.ShotId = Shot->GetShotId(); Receipt.TargetId = FeedbackTargetId; Receipt.ImpactPoint = Hit.ImpactPoint;
            Receipt.ImpactNormal = Hit.ImpactNormal; // 与实际骨骼碰撞共享法线，GAS扣血后才允许生成池化金属火花。
            Receipt.Damage = ActualDamage; Receipt.bCore = Region == EDemoEnemyHitRegion::Core; Receipt.bKilled = bKilled;
            Receipt.bRedesigned = bRedesigned; Receipt.Surface = FeedbackSurface; Receipt.WeaponId = FeedbackWeapon;
            Feedback->Confirm(Receipt);
        }
    }
    if (bActualDamage && Shot->IsAttackValid() && IsValid(Enemy) && Enemy->IsAlive()
        && IsValid(TargetASC) && Enemy->GetAbilitySystemComponent() == TargetASC)
    {
        const int32 Type = Shot->GetAmmoType(); // 发射冻结类型，切枪/切弹不改变飞行中伤害。
        if ((Type == 1 || Type == 2) && Shot->TryMarkElement(Enemy))
        {
            UDemoAmmoStatus* Status = Enemy->FindComponentByClass<UDemoAmmoStatus>(); // Enemy拥有的状态组件，本调用借用。
            if (Status) Status->ApplySnapshot(Shot->GetSourceASC(), Type, Shot->GetAmmoSnapshot());
            else UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_AMMO_STATUS_MISSING target=%s"), *GetNameSafe(Enemy));
        }
        else if (Type == 3)
        {
            FGameplayCueParameters Cue; // 穿透表现只在实际伤害后播放，不承担第二目标伤害查询。
            Cue.EffectContext = EffectContext;
            Cue.Location = Hit.ImpactPoint;
            Cue.Normal = Hit.ImpactNormal;
            Cue.RawMagnitude = Damage;
            TargetASC->ExecuteGameplayCue(DemoAmmoTags::PiercingCue, Cue);
        }
    }
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_DAMAGE shot=%s pellet=%d level=%d target=%s bone=%s region=%s damage=%.2f actual=%d"),
        *Shot->GetShotId().ToString(), PelletIndex, Shot->GetLevelNumber(), *GetNameSafe(Enemy), *Hit.BoneName.ToString(),
        DemoEnemyHitZones::RegionName(Region), Damage, bActualDamage);
    return true;
}
