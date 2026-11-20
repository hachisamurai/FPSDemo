#include "Tests/DemoWeaponAnimationTest.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Game/FPSDemoGameMode.h"
#include "AI/DemoEnemy.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"

void ADemoWeaponAnimationTest::TickTubeReloadTest()
{
    DEMO_LOG_TICK();
    if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds() < 90.f,TEXT("tube test bounded runtime"))) return;
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 当前World真实本地玩家。
    UDemoWeaponComponent* Equipment = Player ? Player->GetWeaponComponent() : nullptr; // 只调用正式输入/装备入口。
    ADemoWeaponBase* Weapon = TestedWeapon.Get(); // 固定测试枪，切槽后仍检查旧枪计时器。
    switch (Step)
    {
    case 0:
    {
        WeaponIndex = 2; // 用终端装备霰弹枪，不绕过装备配置。
        if (!InitializeFixture() || !SelectCurrentWeapon()) return;
        ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 射击事务要求真实战斗凭据。
        State->Phase = EDemoPhase::Hub;
        GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->StartNextLevel();
        if (!Check(State->Phase == EDemoPhase::Combat,TEXT("tube real combat starts"))) return;
        for (TActorIterator<ADemoEnemy> Enemy(GetWorld()); Enemy; ++Enemy) // 冻结敌人但保留存活数量，避免测试中自动清关。
        { Enemy->SetActorTickEnabled(false); Enemy->SetActorEnableCollision(false); }
        Advance(20,.5f);
        break;
    }
    case 20:
        if (!CheckEquipped(true) || !Check(Weapon->Config.bReloadOneRoundAtATime,TEXT("tube config enabled in actual asset"))) return;
        Weapon->Config.bInfiniteReserve = false; // 仅测试Actor实例，有限弹药揭露重复转移。
        Weapon->RestoreAmmo(0,3);
        if (!Check(Equipment->RequestReload(true),TEXT("empty tube reload starts"))) return;
        ReloadSequence = Weapon->GetReloadSequence();
        ReloadDuration = Weapon->GetReloadDuration();
        if (!Check(!Weapon->InsertReloadShell(ReloadSequence),TEXT("early insert rejected"))) return;
        Advance(21,ReloadDuration*Weapon->Config.ShellInsertPhase-.15f);
        break;
    case 21:
        Equipment->StartFire(); Equipment->StopFire();
        if (!Check(Weapon->IsReloading() && Weapon->GetAmmo()==0 && Weapon->GetReserveAmmo()==3,TEXT("empty fire preserves pending first shell"))) return;
        Advance(22,.22f);
        break;
    case 22:
        if (!Check(Weapon->IsReloading() && Weapon->GetAmmo()==1 && Weapon->GetReserveAmmo()==2
            && !Weapon->InsertReloadShell(ReloadSequence),TEXT("insert gives immediate single round and rejects duplicate"))) return;
        Capture(TEXT("Shotgun-ShellInserted"));
        Equipment->StartFire(); Equipment->StopFire();
        if (!Check(!Weapon->IsReloading() && Weapon->GetAmmo()==0 && Weapon->GetReserveAmmo()==2,TEXT("fire interrupts reload and spends inserted round"))) return;
        Advance(23,ReloadDuration+.2f);
        break;
    case 23:
        if (!Check(Weapon->GetAmmo()==0 && Weapon->GetReserveAmmo()==2 && !Weapon->InsertReloadShell(ReloadSequence),TEXT("cancelled task and stale insert cannot add ammunition"))) return;
        if (!Check(Equipment->RequestReload(),TEXT("finite reserve looping begins"))) return;
        ReloadSequence = Weapon->GetReloadSequence();
        Advance(24,Weapon->Config.EmptyShellReloadSeconds+Weapon->Config.ReloadSeconds+.25f);
        break;
    case 24:
        if (!Check(!Weapon->IsReloading() && Weapon->GetAmmo()==2 && Weapon->GetReserveAmmo()==0
            && Weapon->GetReloadSequence()==ReloadSequence+1,TEXT("two cycles consume exactly two reserve shells and stop"))) return;
        Weapon->RestoreAmmo(Weapon->GetCapacity()-1,3);
        if (!Check(Equipment->RequestReload(),TEXT("one free slot reload starts"))) return;
        Advance(25,Weapon->Config.ReloadSeconds+.25f);
        break;
    case 25:
        if (!Check(!Weapon->IsReloading() && Weapon->GetAmmo()==Weapon->GetCapacity() && Weapon->GetReserveAmmo()==2
            && !Equipment->RequestReload(),TEXT("capacity stops loop without wasting reserve"))) return;
        Weapon->RestoreAmmo(1,3);
        if (!Check(Equipment->RequestReload(),TEXT("before insert cancel starts"))) return;
        Advance(26,.15f);
        break;
    case 26:
        Equipment->CancelActions();
        Advance(27,Weapon->Config.ReloadSeconds+.2f);
        break;
    case 27:
        if (!Check(Weapon->GetAmmo()==1 && Weapon->GetReserveAmmo()==3,TEXT("cancel before insert transfers nothing"))) return;
        if (!Check(Equipment->RequestReload(),TEXT("after insert switch starts"))) return;
        ReloadSequence = Weapon->GetReloadSequence();
        Advance(28,Weapon->Config.ReloadSeconds*Weapon->Config.ShellInsertPhase+.06f);
        break;
    case 28:
        if (!Check(Weapon->GetAmmo()==2 && Weapon->GetReserveAmmo()==2 && Equipment->EquipSlot(2),TEXT("switch after insert retains round"))) return;
        Advance(29,Weapon->Config.ReloadSeconds+.2f);
        break;
    case 29:
        if (!Check(!Weapon->IsReloading() && Weapon->GetAmmo()==2 && Weapon->GetReserveAmmo()==2 && Equipment->EquipSlot(1),TEXT("old gun task stays cancelled after switching"))) return;
        Advance(30,.3f);
        break;
    case 30:
        if (!CheckSupportGrip()) return;
        Weapon->Config.bInfiniteReserve = true; // 验证无限备用只补管仓，不减少显示备用。
        Weapon->RestoreAmmo(Weapon->GetCapacity()-2,0);
        if (!Check(Equipment->RequestReload(),TEXT("infinite reserve loop starts"))) return;
        Advance(31,2*Weapon->Config.ReloadSeconds+.3f);
        break;
    case 31:
        if (!Check(!Weapon->IsReloading() && Weapon->GetAmmo()==Weapon->GetCapacity() && Weapon->GetReserveAmmo()==0
            && !Player->GetDemoASC()->HasMatchingGameplayTag(DemoTags::Reloading),TEXT("infinite reserve fills capacity and releases GAS tag"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_TUBE_RELOAD_SUCCESS: early/duplicate/stale/instant/fire/finite/capacity/cancel/switch/infinite"));
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
        break;
    default:
        Check(false,TEXT("known tube test state"));
        break;
    }
}
