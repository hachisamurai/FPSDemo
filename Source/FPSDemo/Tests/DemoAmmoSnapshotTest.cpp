#include "Tests/DemoProjectileFixtures.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "GAS/Ammo/DemoAmmoEffectSnapshot.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/DemoAttributeSet.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Debug/DemoLog.h"

namespace
{
    /** bCondition为真实ASC结果；Message为诊断用例，调用方统一决定专项进程退出码。 */
    bool CheckSnapshot(bool bCondition,const TCHAR* Message)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        UE_LOG(LogFPSDemo,Display,TEXT("AMMO_SNAPSHOT_TEST %s %s"),bCondition?TEXT("PASS"):TEXT("FAIL"),Message);
        return bCondition;
    }
}

namespace DemoAmmoSnapshotTests
{
    /** World/Player借用已经建立Combat的隔离测试World；同步创建未注册刷怪系统的靶，不修改资产/CDO或正式存档。 */
    bool Run(UWorld* World,ADemoCharacter* Player)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        const ADemoGameState* State=World?World->GetGameState<ADemoGameState>():nullptr; // 只读阶段，不替测试建立或伪造战役状态。
        UAbilitySystemComponent* Source=IsValid(Player)?Player->GetAbilitySystemComponent():nullptr; // 本函数同步借用真实玩家ASC。
        if(!CheckSnapshot(World&&State&&State->Phase==EDemoPhase::Combat&&Source,TEXT("combat fixture and source ASC valid")))return false;

        FActorSpawnParameters Spawn; // 独立高空靶由World持有，不加入战役注册表，不产生金币或清关。
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ADemoEnemy* Enemy=World->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),FVector(80000,80000,10000),FRotator::ZeroRotator,Spawn); // 返回后直到Destroy只在本同步栈借用。
        if(!CheckSnapshot(IsValid(Enemy),TEXT("create independent status target")))return false;
        FDemoEnemySpawnStats Stats; // 高生命固定靶，测试内不触发死亡/阶段切换。
        Stats.Health=1000.f;
        Stats.AttackPower=0.f;
        Enemy->Configure(Stats);
        Enemy->SetActorTickEnabled(false);
        UDemoAmmoStatus* Status=Enemy->FindComponentByClass<UDemoAmmoStatus>(); // Enemy拥有，测试结束通过Destroy清理。
        UAbilitySystemComponent* Target=Enemy->GetAbilitySystemComponent(); // GAS是层数、冻结和移动倍率的唯一真值。
        if(!CheckSnapshot(Status&&Target,TEXT("target owns production status and ASC"))){Enemy->Destroy();return false;}
        TStrongObjectPtr<UDemoAmmoCatalog> Catalog(NewObject<UDemoAmmoCatalog>()); // 只修改本测试新目录；强引用覆盖可能同步触发GC的回调。
        bool bPassed=true; // 汇总全部独立断言，失败仍执行末尾清理。

        Catalog->BurnThreshold=3;
        Catalog->ExplosionDamage=37.f;
        Catalog->BurnDuration=20.f;
        Catalog->BurnPeriod=5.f;
        Catalog->BurnDamagePerStack=7.f;
        const FDemoAmmoEffectSnapshot FirstFire=FDemoAmmoEffectSnapshot::FromCatalog(Catalog.Get()); // 模拟开火时复制，随后Catalog可改变。
        Status->ApplySnapshot(Source,1,FirstFire);
        const float InitialHealth=Enemy->GetHealth(); // 同步场景不推进World，灼烧不会掺入周期伤害。
        bPassed&=CheckSnapshot(Status->Count(DemoAmmoTags::Burn)==1,TEXT("first fire snapshot starts one stack"));
        Catalog->BurnThreshold=1;
        Catalog->ExplosionDamage=91.f;
        Catalog->BurnDuration=8.f;
        Catalog->BurnPeriod=2.f;
        Catalog->BurnDamagePerStack=19.f;
        Status->Apply(Source,1,Catalog.Get()); // 兼容接口也必须尊重已有首层，不能用新目录阈值立即引爆。
        bPassed&=CheckSnapshot(Status->Count(DemoAmmoTags::Burn)==2&&FMath::IsNearlyEqual(Enemy->GetHealth(),InitialHealth),TEXT("catalog edits do not replace active first-stack threshold"));
        Status->Apply(Source,1,Catalog.Get());
        bPassed&=CheckSnapshot(Status->Count(DemoAmmoTags::Burn)==0&&FMath::IsNearlyEqual(Enemy->GetHealth(),InitialHealth-37.f),TEXT("old group consumes at original threshold and explosion damage"));
        Status->Apply(Source,1,Catalog.Get());
        bPassed&=CheckSnapshot(Status->Count(DemoAmmoTags::Burn)==0&&FMath::IsNearlyEqual(Enemy->GetHealth(),InitialHealth-128.f),TEXT("next group adopts new values and threshold one consumes before Apply returns"));
        bPassed&=CheckSnapshot(FirstFire.BurnThreshold==3&&FMath::IsNearlyEqual(FirstFire.BurnDamagePerStack,7.f),TEXT("shot snapshot remains independent of later catalog edits"));
        Status->Clear();

        Catalog->BurnThreshold=4;
        Catalog->ExplosionDamage=23.f;
        Catalog->FreezeThreshold=3;
        Catalog->SlowPerStack=.12f;
        Catalog->MaxSlow=.8f;
        Catalog->ChillDuration=6.f;
        Catalog->FreezeDuration=1.f;
        Catalog->PostThawImmunityDuration=3.f;
        const FDemoAmmoEffectSnapshot Separate=FDemoAmmoEffectSnapshot::FromCatalog(Catalog.Get()); // 火冰分别在各自首层绑定同一个不可变输入值的独立副本。
        Status->ApplySnapshot(Source,1,Separate);
        Status->ApplySnapshot(Source,2,Separate);
        Catalog->BurnThreshold=1;
        Catalog->ExplosionDamage=101.f;
        Catalog->FreezeThreshold=1;
        Catalog->SlowPerStack=.4f;
        const FDemoAmmoEffectSnapshot Later=FDemoAmmoEffectSnapshot::FromCatalog(Catalog.Get()); // 后续命中故意使用冲突阈值，验证已有两组互不覆盖。
        const float BeforeInterleave=Enemy->GetHealth(); // 当前火组第一次满层以前不应产生直伤。
        Status->ApplySnapshot(Source,2,Later);
        Status->ApplySnapshot(Source,1,Later);
        bPassed&=CheckSnapshot(Status->Count(DemoAmmoTags::Burn)==2&&Status->Count(DemoAmmoTags::Chill)==2
            &&!Target->HasMatchingGameplayTag(DemoAmmoTags::Frozen)&&FMath::IsNearlyEqual(Enemy->GetHealth(),BeforeInterleave)
            &&FMath::IsNearlyEqual(Target->GetNumericAttribute(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute()),.76f,.001f),TEXT("interleaved fire and frost retain independent first-stack thresholds and slow"));
        Status->ApplySnapshot(Source,2,Later);
        bPassed&=CheckSnapshot(Status->Count(DemoAmmoTags::Chill)==0&&Status->Count(DemoAmmoTags::Burn)==2
            &&Target->HasMatchingGameplayTag(DemoAmmoTags::Frozen)&&Target->HasMatchingGameplayTag(DemoAmmoTags::Immune),TEXT("frost consumes at its original third stack without removing fire"));
        Status->ApplySnapshot(Source,1,Later);
        Status->ApplySnapshot(Source,1,Later);
        bPassed&=CheckSnapshot(Status->Count(DemoAmmoTags::Burn)==0&&FMath::IsNearlyEqual(Enemy->GetHealth(),BeforeInterleave-23.f)
            &&Target->HasMatchingGameplayTag(DemoAmmoTags::Frozen),TEXT("fire original fourth stack explodes independently of frozen lifetime"));
        Status->Clear();

        Status->ApplySnapshot(Source,2,Later); // 首层阈值1必须在Added回调内同步取得快照，不依赖Apply返回后登记句柄。
        bPassed&=CheckSnapshot(Status->Count(DemoAmmoTags::Chill)==0&&Target->HasMatchingGameplayTag(DemoAmmoTags::Frozen)
            &&Target->HasMatchingGameplayTag(DemoAmmoTags::Immune),TEXT("threshold-one frost freezes during first synchronous Added callback"));
        Status->Clear();
        bPassed&=CheckSnapshot(!Target->HasMatchingGameplayTag(DemoAmmoTags::Frozen)&&!Target->HasMatchingGameplayTag(DemoAmmoTags::Immune)
            &&Status->Count(DemoAmmoTags::Burn)==0&&Status->Count(DemoAmmoTags::Chill)==0
            &&FMath::IsNearlyEqual(Target->GetNumericAttribute(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute()),1.f),TEXT("clear removes all independent status groups and derived movement"));
        Enemy->Destroy();
        if(bPassed)UE_LOG(LogFPSDemo,Display,TEXT("DEMO_AMMO_SNAPSHOT_TEST_SUCCESS"));
        return bPassed;
    }
}
