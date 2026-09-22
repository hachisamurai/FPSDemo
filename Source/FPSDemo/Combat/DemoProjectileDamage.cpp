#include "Combat/DemoProjectileDamage.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Combat/DemoEnemyHitZones.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "AbilitySystemComponent.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"

bool DemoProjectileDamage::Apply(UDemoShotContext* Shot, const FHitResult& Hit, float BaseDamage, int32 PelletIndex)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs pellet=%d target=%s"), __FUNCTION__, PelletIndex, *GetNameSafe(Hit.GetActor()));
    ADemoEnemy* Enemy = Cast<ADemoEnemy>(Hit.GetActor()); // 只借用运动碰撞实际命中的目标，不查询原瞄准射线。
    UAbilitySystemComponent* SourceASC = IsValid(Shot) ? Shot->GetSourceASC() : nullptr; // Context内弱源解析。
    UAbilitySystemComponent* TargetASC = IsValid(Enemy) ? Enemy->GetAbilitySystemComponent() : nullptr; // 敌人拥有的ASC。
    EDemoEnemyHitRegion Region = EDemoEnemyHitRegion::Body; // 仅ResolveHit成功后可使用，不作为缺骨回退。
    float Multiplier = 0.f; // 区域倍率，失败保持零。
    if (!IsValid(Shot) || !Shot->IsAttackValid() || !IsValid(Enemy) || !Enemy->IsAlive()
        || !SourceASC || !TargetASC || !TargetASC->IsOwnerActorAuthoritative() || !Shot->GetHitProfile()
        || !FMath::IsFinite(BaseDamage) || BaseDamage < 0.f
        || !Shot->GetHitProfile()->ResolveHitForChannel(Hit, Region, Multiplier, ECC_GameTraceChannel3))
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_DAMAGE_REJECT target=%s bone=%s invalid context/authority/body"), *GetNameSafe(Enemy), *Hit.BoneName.ToString());
        return false;
    }
    const float Damage = BaseDamage * Multiplier; // 每颗只乘一次自身命中部位，不将首敌核心倍率传递给第二敌。
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
    ADemoCharacter* Source = Shot->GetSourcePawn(); // GE之后重新解析弱源，不能沿用旧Pawn裸指针。
    if (bActualDamage && IsValid(Source) && Source->GetWorld()) Source->LastHitTime = Source->GetWorld()->GetTimeSeconds();
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
