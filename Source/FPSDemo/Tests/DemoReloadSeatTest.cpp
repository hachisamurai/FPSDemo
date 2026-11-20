// 职责：在隔离的 Editor 单机游戏中验证正式装填 GA、插入后开火及跨关库存。随 -DemoWeaponAnimationTest -DemoReloadSeatTest 启动。
#include "Tests/DemoWeaponAnimationTest.h"
#if WITH_EDITOR
#include "Characters/DemoCharacter.h"
#include "Debug/DemoLog.h"
#include "Economy/DemoShopComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Interaction/DemoInteractable.h"
#include "Kismet/GameplayStatics.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Save/DemoRunSave.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

void ADemoWeaponAnimationTest::TickReloadSeatTest()
{
    DEMO_LOG_TICK();
    const float Now=GetWorld()->GetTimeSeconds(); // 游戏秒与真实装填任务使用同一时钟。
    if (bFailed || Now<1.f || Now<NextTime) return;
    if (Now>60.f) { Check(false,TEXT("seat regression timeout")); return; }
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 仅当前独立测试玩家。
    ADemoPlayerController* PC=Pawn?Cast<ADemoPlayerController>(Pawn->GetController()):nullptr;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 使用正式出发和终端事务。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    if (!Pawn||!PC||!Mode||!State||!Pawn->GetWeaponComponent()) return;
    UDemoWeaponComponent* Inventory=Pawn->GetWeaponComponent(); // 跨帧重新解析持有者。
    ADemoWeaponBase* Weapon=Inventory->GetActiveWeapon(); // 当前手持槽；夹具还会检查收起的步枪。
    if (!Weapon) return;
    switch (Step)
    {
        case 0:
        {
            UDemoRunSaves* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 此 Editor GI 的隔离槽。
            if (PC->GetMenuFlow()->GetMenuPage()==EDemoMenuPage::FirstRunGuide) PC->GetMenuFlow()->CloseFirstRunGuide();
            if (!Check(Saves&&Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("fresh isolated slot starts run"))) return;
            Saves->ConsumePending(); PC->OnRunReady(); PC->ConsoleCommand(TEXT("Demo.Debug.UnlockAll"),true);
            Pawn->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110));
            Mode->GetShopTerminal()->Interact(Pawn);
            if (!Check(Mode->GetShopSystem()->EquipPrimaryWeapon(TEXT("rifle"),Pawn).IsApplied(),TEXT("equip real rifle at hub"))) return;
            PC->CloseUpgradeMenu(); TestedWeapon=Inventory->GetActiveWeapon(); // 收起前给步枪保留独立有限备弹。
            if (!Check(TestedWeapon.IsValid()&&TestedWeapon->GetWeaponId()==TEXT("rifle")&&Inventory->EquipSlot(2),TEXT("pistol and stowed rifle are distinct"))) return;
            UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Pawn);
            FDemoRunBuildSnapshot Locked; Locked.CoreId=TEXT("B04"); Locked.bLocked=true; Locked.Revision=1; // 第一关间检查点要求核心锁定。
            if (!Check(Build&&Build->RestoreSnapshot(Locked),TEXT("legal intermission core restored"))) return;
            State->LevelNumber=1; State->Phase=EDemoPhase::Intermission; // 只构造起始关间，下一关走实际准备、保存和生成流程。
            Advance(1,.25f); return;
        }
        case 1:
            Weapon->Config.bInfiniteReserve=false; Weapon->RestoreAmmo(1,4); // 战术装填前仍留一发，检测过早打断。
            if (!Check(Inventory->RequestReload()&&!Weapon->CommitReloadInsertion(Weapon->GetReloadSequence()),TEXT("manual reload refuses early insertion"))) return;
            Inventory->StartFire();
            if (!Check(Weapon->IsReloading()&&Weapon->GetAmmo()==1,TEXT("trigger before seating cannot fire old magazine"))) return;
            Inventory->CancelActions();
            if (!Check(Weapon->GetAmmo()==1&&Weapon->GetReserveAmmo()==4,TEXT("cancel before seat conserves ammo"))) return;
            Weapon->RestoreAmmo(0,4); Inventory->StartFire(); // 空弹单击走同一GAS。
            if (!Check(Weapon->IsReloading()&&Weapon->IsEmptyReload(),TEXT("empty trigger starts real reload"))) return;
            ReloadSequence=Weapon->GetReloadSequence();
            Advance(2,Weapon->GetReloadInsertionSeconds()+.06f); return;
        case 2:
            if (!Check(Weapon->IsInMagazineRecovery()&&Weapon->GetAmmo()==4&&Weapon->GetReserveAmmo()==0
                &&!Weapon->CommitReloadInsertion(ReloadSequence),TEXT("seat transferred once during unfinished animation"))) return;
            Inventory->StartFire(); Inventory->StopFire();
            if (!Check(!Weapon->IsReloading()&&Weapon->GetAmmo()==3,TEXT("post-seat shot consumes a round before animation end"))) return;
            Advance(3,Weapon->GetReloadDuration()); return;
        case 3:
        {
            if (!Check(Weapon->GetAmmo()==3&&Weapon->GetReserveAmmo()==0,TEXT("old tail cannot replenish spent round"))) return;
            TestedWeapon->Config.bInfiniteReserve=false; TestedWeapon->RestoreAmmo(0,2);
            Weapon->Config.bInfiniteReserve=true; Weapon->RestoreAmmo(1,0); // 全队进关沿用各自库存和各枪备弹语义。
            if (!Check(Inventory->RequestReload(),TEXT("intermission reload is active when departure begins"))) return;
            Mode->StartNextLevel(); // 此时夹具刻意保留未领取的第一关奖励，观察真实出发拒绝。
            if (!Check(State->Phase==EDemoPhase::Intermission&&Weapon->GetAmmo()==1&&TestedWeapon->GetAmmo()==0
                &&TestedWeapon->GetReserveAmmo()==2,TEXT("blocked departure leaves both magazines and reserve untouched"))) return;
            UDemoPlayerProgressComponent* Progress=Pawn->GetPlayerState<ADemoPlayerState>()->GetProgressComponent();
            FDemoPlayerProgressSnapshot Claimed=Progress->GetSnapshot(); // 补齐已领取第一关奖励的关间起始条件，不修改弹药或模拟第二关提交。
            Claimed.LastRewardLevel=1; Progress->Commit(Claimed);
            Mode->StartNextLevel(); Advance(4,.1f); return;
        }
        case 4:
            if (State->Phase!=EDemoPhase::Combat) return;
            if (!Check(State->LevelNumber==2&&!Weapon->IsReloading()&&Weapon->GetAmmo()==Weapon->GetCapacity()
                &&TestedWeapon->GetAmmo()==2&&TestedWeapon->GetReserveAmmo()==0,TEXT("real level two fills held pistol and stowed finite rifle"))) return;
            Advance(5,.2f); return;
        case 5:
            if (!Check(Inventory->EquipSlot(1)&&Inventory->GetActiveWeapon()==TestedWeapon.Get(),TEXT("stowed rifle can be equipped after departure"))) return;
            TestedWeapon->RestoreAmmo(1,5);
            if (!Check(Inventory->RequestReload(),TEXT("combat tactical rifle reload started"))) return;
            Advance(6,TestedWeapon->GetReloadInsertionSeconds()*.5f); return;
        case 6:
            if (!Check(Inventory->RequestReload()&&Weapon->GetAmmo()==1,TEXT("B04 beat accepted before rifle seating"))) return;
            Advance(7,Weapon->GetReloadInsertionSeconds()*.5f+.05f); return;
        case 7:
        {
            const auto& Rhythm=UDemoRunBuildComponent::FindForPawn(Pawn)->GetRuntimeView(); // 实际能力的权威凭据，无测试直接授予。
            if (!Check(Weapon->IsInMagazineRecovery()&&Weapon->GetAmmo()==6&&Weapon->GetReserveAmmo()==0
                &&Rhythm.ActionState==TEXT("Rhythm.Credits")&&Rhythm.ActionStacks>0,TEXT("B04 success and ammo available at seat before rifle tail"))) return;
            SeatTestCreditDeadline=Rhythm.ServerEndTime;
            Advance(8,Weapon->GetReloadDuration()-Weapon->GetReloadInsertionSeconds()+.15f); return;
        }
        case 8:
            if (!Check(!Weapon->IsReloading()&&Weapon->GetAmmo()==6&&Weapon->GetReserveAmmo()==0
                &&FMath::IsNearlyEqual(UDemoRunBuildComponent::FindForPawn(Pawn)->GetRuntimeView().ServerEndTime,SeatTestCreditDeadline),
                TEXT("natural recovery completes without a second transfer or rhythm reward"))) return;
            UE_LOG(LogFPSDemo,Display,TEXT("DEMO_RELOAD_SEAT_SUCCESS"));
            SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); return;
    }
}
#endif
