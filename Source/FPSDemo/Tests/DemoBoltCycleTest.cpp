// 职责：当前 L96X 的真实 GAS 拉栓回归；通过 -DemoBoltCycleTest 启动，检查配套 Charger 骨与手部时间。
#include "Tests/DemoWeaponAnimationTest.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Animation/AnimInstance.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"
#include "Game/FPSDemoGameMode.h"
#include "AI/DemoEnemy.h"
#include "EngineUtils.h"

void ADemoWeaponAnimationTest::TickBoltCycleTest()
{
    DEMO_LOG_TICK();
    if (bFailed) return;
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 当前World本地玩家。
    UDemoWeaponComponent* Equipment = Player ? Player->GetWeaponComponent() : nullptr; // 正式输入/GAS入口。
    UDemoWeaponAnimationComponent* View = Player ? Player->GetWeaponAnimationComponent() : nullptr; // 实际动画时钟读接口。
    ADemoWeaponBase* Weapon = TestedWeapon.Get(); // 测试锁定原枪，切槽不改变断言对象。
    if (Step==21 && Weapon && Player) // 独立读取整个启动至中段的实际骨轨迹，不假定原动作抽栓峰值时刻。
    {
        const FTransform CurrentBolt=Weapon->GetWeaponSkeletalMesh()->GetSocketTransform(TEXT("Charger"),RTS_Component); // 当前周期实际组件空间骨，单位cm。
        MaximumBoltTravel=FMath::Max(MaximumBoltTravel,static_cast<float>(FVector::Distance(BoltRest.GetTranslation(),CurrentBolt.GetTranslation())));
        MaximumBoltWristTravel=FMath::Max(MaximumBoltWristTravel,static_cast<float>(FVector::Distance(BoltIdleWrist,Player->GetMesh1P()->GetSocketLocation(TEXT("hand_r")))));
    }
    if (GetWorld()->GetTimeSeconds() < NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds() < 90.f, TEXT("bolt regression bounded runtime"))) return;
    switch (Step)
    {
    case 0:
    {
        WeaponIndex = 3; // 真终端选择狙击，不直接生成免权限库存。
        if (!InitializeFixture() || !SelectCurrentWeapon()) return;
        ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 原换弹夹具不要求战斗凭据；实体射击必须正式开始第一关。
        State->Phase = EDemoPhase::Hub;
        GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->StartNextLevel();
        if (!Check(State->Phase == EDemoPhase::Combat && State->LevelNumber == 1, TEXT("real combat session opened for projectile transaction"))) return;
        for (TActorIterator<ADemoEnemy> Enemy(GetWorld()); Enemy; ++Enemy) // 本专项冻结新关敌人，保留存活数量避免自动清关。
        { Enemy->SetActorTickEnabled(false); Enemy->SetActorEnableCollision(false); }
        OriginalRPM = TestedWeapon->Config.RoundsPerMinute;
        Advance(20,.5f);
        break;
    }
    case 20:
    {
        if (!CheckEquipped(false)) return;
        Weapon->Config.RoundsPerMinute = BoltCase == 1 ? 75.f : BoltCase == 2 ? 30.f : OriginalRPM; // 仅本次运行实例测试快/慢射速，CDO不变。
        ExpectedBoltSeconds = 60.f / Weapon->Config.RoundsPerMinute;
        Weapon->RestoreAmmo(BoltCase == 3 ? 1 : 2, 10);
        BoltIdleWrist = Player->GetMesh1P()->GetSocketLocation(TEXT("hand_r"));
        if (!Check(Weapon->GetWeaponSkeletalMesh()->GetBoneIndex(TEXT("Charger"))!=INDEX_NONE,TEXT("native L96X Charger bone exists"))) return;
        BoltRest = Weapon->GetWeaponSkeletalMesh()->GetSocketTransform(TEXT("Charger"), RTS_Component);
        MaximumBoltTravel=MaximumBoltWristTravel=0.f;
        Equipment->StartFire(); Equipment->StopFire(); // 实际提交实体弹丸和射击成本。
        if (!Check(Weapon->GetAmmo() == (BoltCase == 3 ? 0 : 1), TEXT("successful shot consumes exactly one round"))) return;
        if (!Check(FMath::IsNearlyEqual(Weapon->GetBoltCycleState().Duration, ExpectedBoltSeconds, .0001f)
            && FMath::IsNearlyEqual(Weapon->GetFireCooldownRemaining(), Weapon->GetBoltCycleRemaining(), .0001f), TEXT("bolt duration and authoritative shot deadline agree"))) return;
        Advance(21,ExpectedBoltSeconds*.55f);
        break;
    }
    case 21:
    {
        const FDemoReloadPhaseAudit Audit = View->GetBoltCycleAudit(); // 引擎实际Montage与Gun位置采样，非配置自比较。
        if (!Check(Audit.SampleCount > 5 && Audit.MaximumErrorSeconds < .002f && Audit.ArmsProgress > .4f && Audit.ArmsProgress < .7f,
            TEXT("paired bolt animation follows shot clock within 2ms"))) return;
        UE_LOG(LogFPSDemo,Log,TEXT("BOLT_GEOMETRY_AUDIT wrist=%.3f charger=%.3f"),MaximumBoltWristTravel,MaximumBoltTravel);
        if (!Check(MaximumBoltWristTravel>2.f && MaximumBoltTravel>2.f,TEXT("right hand and actual Charger move during the sampled cycle"))) return;
        const int32 AmmoBefore = Weapon->GetAmmo(); // 尝试连点和换弹不得产生第二笔成本或提前补弹。
        Equipment->StartFire(); Equipment->StopFire();
        if (!Check(Weapon->GetAmmo() == AmmoBefore && !Equipment->RequestReload() && !Equipment->BeginAim(), TEXT("fire reload aim blocked during bolt cycle including final round"))) return;
        Capture(FString::Printf(TEXT("Sniper-Bolt-%d"),BoltCase));
        if (BoltCase == 4)
        {
            if (!Check(Equipment->EquipSlot(2), TEXT("switch away during bolt cycle"))) return;
            Advance(23,.12f);
        }
        else
        {
            if (BoltCase == 5) FixedMainInstance->Montage_Stop(0.f); // 非正常外部中断不能清除Gameplay射速。
            Advance(22,Weapon->GetBoltCycleRemaining()+.1f);
        }
        break;
    }
    case 23:
        if (!Check(!FixedMainInstance->IsAnyMontagePlaying() && Weapon->GetFireCooldownRemaining() > 0.f, TEXT("switch cleans old montage but preserves cooldown"))) return;
        if (!Check(Equipment->EquipSlot(1), TEXT("switch back resumes original weapon"))) return;
        Advance(24,.05f);
        break;
    case 24:
        if (!Check(View->GetBoltCycleAudit().ArmsProgress > .6f && Weapon->GetFireCooldownRemaining() > 0.f,
            TEXT("re-equip seeks existing cycle instead of restarting"))) return;
        Advance(22,Weapon->GetBoltCycleRemaining()+.1f);
        break;
    case 22:
        if (!Check(Weapon->GetFireCooldownRemaining() == 0.f && Weapon->GetBoltCycleRemaining() == 0.f && Weapon->CanReload()
            && !FixedMainInstance->IsAnyMontagePlaying(), TEXT("cycle ends at fire deadline and releases reload"))) return;
        if (!CheckSupportGrip()) return; // 对比压缩Idle，覆盖右手归握和左肘稳定。
        if (!Check(BoltRest.Equals(Weapon->GetWeaponSkeletalMesh()->GetSocketTransform(TEXT("Charger"), RTS_Component), .05f), TEXT("bolt returns to mechanical rest"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("BOLT_CYCLE_CASE_PASS case=%d seconds=%.6f samples=%d error=%.6f"), BoltCase, ExpectedBoltSeconds, View->GetBoltCycleAudit().SampleCount, View->GetBoltCycleAudit().MaximumErrorSeconds);
        ++BoltCase;
        if (BoltCase < 6) Advance(20,.15f);
        else
        {
            Weapon->Config.RoundsPerMinute = OriginalRPM; // 恢复测试实例配置，退出不保存临时数据。
            UE_LOG(LogFPSDemo,Display,TEXT("DEMO_BOLT_CYCLE_SUCCESS cases=6: default/fast/slow/final-round/switch-resume/interruption"));
            SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
        }
        break;
    default:
        Check(false,TEXT("known bolt cycle test state"));
        break;
    }
}
