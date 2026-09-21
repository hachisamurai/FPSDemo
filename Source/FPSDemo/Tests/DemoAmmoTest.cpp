#include "Tests/DemoAmmoTest.h"
#include "Game/FPSDemoGameMode.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Interaction/DemoInteractable.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "AI/DemoEnemy.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "UnrealClient.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

ADemoAmmoTest::ADemoAmmoTest(){DEMO_LOG_CALL();PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.02f;}
bool ADemoAmmoTest::Check(bool Condition,const TCHAR* Message)
{
    DEMO_LOG_CALL();UE_LOG(LogFPSDemo,Display,TEXT("AMMO_TEST %s %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Message);
    if(!Condition){Failed=true;FPlatformMisc::RequestExitWithStatus(false,1);}return Condition;
}
void ADemoAmmoTest::Advance(float Delay){DEMO_LOG_CALL();++Step;Next=GetWorld()->GetTimeSeconds()+Delay;}
ADemoEnemy* ADemoAmmoTest::SpawnTarget(FVector Location)
{
    DEMO_LOG_CALL();
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 空中夹具避免地形干扰射线。
    ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),Location,FRotator::ZeroRotator,Params); // World拥有测试对象。
    FDemoEnemySpawnStats Stats;Stats.Health=1000;Stats.AttackPower=0; // 不依赖难度，比较精确理论伤害。
    Enemy->Configure(Stats);Enemy->SetActorTickEnabled(false);return Enemy;
}
void ADemoAmmoTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();Super::Tick(DeltaSeconds);if(Failed||GetWorld()->GetTimeSeconds()<Next)return;
    if(!Check(GetWorld()->GetTimeSeconds()<40,TEXT("bounded runtime")))return;
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 每步重新借用，避免跨World悬空。
    auto* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 当前测试Avatar。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 实际单人模式。
    auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 测试钱包与阶段。
    auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // Editor自动隔离的GI临时槽。
    if(!Check(PC&&Player&&Mode&&State,TEXT("world ready")))return;
    auto* Ammo=Player->FindComponentByClass<UDemoAmmoComponent>(); // 当前玩家装配组件。
    auto* Weapon=Player->GetWeaponComponent()->GetActiveWeapon(); // 使用真实手枪BP和Fire GA。
    const UDemoAmmoCatalog* Config=Ammo->GetCatalog(); // 测试生产资产，不改共享默认值。
    switch(Step)
    {
    case 0:
        if(!Check(Config->Validate()&&Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("catalog and isolated save")))return;
        Saves->ConsumePending();PC->OnRunReady();State->Coins=1500;
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,0));
        Mode->GetShopTerminal()->Interact(Player);PC->OpenAmmoMenu();
        PC->InspectAmmo(1);PC->AmmoAction();PC->ConfirmAmmoPurchase(false);
        if(!Check(State->Coins==1500&&!Ammo->IsUnlocked(1),TEXT("cancel purchase changes nothing")))return;
        for(int32 Index=1;Index<4;++Index){PC->InspectAmmo(Index);PC->AmmoAction();PC->ConfirmAmmoPurchase(true);} // 真实UI提交三个独立价格。
        PC->ConfirmAmmoPurchase(true); // 重放旧确认必须拒绝，不能再次扣币。
        if(!Check(State->Coins==300&&Ammo->IsUnlocked(1)&&Ammo->IsUnlocked(2)&&Ammo->IsUnlocked(3)&&Ammo->GetSelected()==0,TEXT("buy once, do not auto equip")))return;
        PC->InspectAmmo(1);PC->AmmoAction();
        Snapshot=DuplicateObject<UDemoRunSave>(Saves->GetSlot(0),this);
        if(!Check(Snapshot->Version==4&&Snapshot->Coins==300&&Snapshot->UnlockedAmmoIds.Num()==4&&Snapshot->SelectedAmmoId==TEXT("fire"),TEXT("wallet and unlocks saved atomically")))return;
        FScreenshotRequest::RequestScreenshot(TEXT("AmmoTerminalGame.png"),false,false); // 捕获真实现成Icon/交易状态，下一帧由引擎保存。
        Advance(.3f);break;
    case 1:
        PC->CloseUpgradeMenu();State->Phase=EDemoPhase::Combat;State->LevelNumber=1;
        Player->SetActorLocation(FVector(0,0,1600));Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);Player->GetCharacterMovement()->StopMovementImmediately();PC->SetControlRotation(FRotator::ZeroRotator);
        Weapon->Config.SpreadHalfAngle=0; // 仅本次手枪实例确定性射线，不修改BP/CDO。
        Target=SpawnTarget(Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(600,0,0));
        Before=Target->GetHealth();Player->GetWeaponComponent()->StartFire();Player->GetWeaponComponent()->StopFire();
        if(!Check(Target->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Burn)==1&&Target->GetHealth()<Before,TEXT("real Fire GA applies one burn stack")))return;
        Before=Target->GetHealth();Advance(Config->BurnPeriod+.15f);break;
    case 2:
        if(!Check(FMath::IsNearlyEqual(Target->GetHealth(),Before-Config->BurnDamagePerStack,.01f),TEXT("periodic GAS burn deals configured damage")))return;
        Before=Target->GetHealth();
        for(int32 Index=1;Index<Config->BurnThreshold;++Index)Target->FindComponentByClass<UDemoAmmoStatus>()->Apply(Player->GetAbilitySystemComponent(),1,Config); // 同一帧阈值边沿，不能依赖第N+1次Overflow。
        if(!Check(FMath::IsNearlyEqual(Target->GetHealth(),Before-Config->ExplosionDamage,.01f)&&Target->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Burn)==0,TEXT("Nth stack explodes exactly once and consumes burn")))return;
        for(int32 Index=0;Index<Config->FreezeThreshold-1;++Index)Target->FindComponentByClass<UDemoAmmoStatus>()->Apply(Player->GetAbilitySystemComponent(),2,Config);
        if(!Check(FMath::IsNearlyEqual(Target->GetAbilitySystemComponent()->GetNumericAttribute(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute()),1-Config->MaxSlow,.01f),TEXT("chill stack controls GAS movement multiplier")))return;
        Target->FindComponentByClass<UDemoAmmoStatus>()->Apply(Player->GetAbilitySystemComponent(),2,Config);
        if(!Check(Target->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Frozen)&&Target->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Immune),TEXT("freeze and immunity granted together")))return;
        Target->FindComponentByClass<UDemoAmmoStatus>()->Apply(Player->GetAbilitySystemComponent(),2,Config);
        if(!Check(Target->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Chill)==0,TEXT("immune enemy rejects new chill stacks")))return;
        Advance(Config->FreezeDuration+.15f);break;
    case 3:
        if(!Check(!Target->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Frozen)&&Target->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Immune)&&Target->GetAbilitySystemComponent()->GetNumericAttribute(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute())==1,TEXT("thaw restores movement while immunity remains")))return;
        Advance(Config->PostThawImmunityDuration+.1f);break;
    case 4:
        if(!Check(!Target->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Immune),TEXT("immunity naturally expires")))return;
        Target->Destroy();Snapshot->SelectedAmmoId=TEXT("piercing");Ammo->Restore(*Snapshot); // 独立测试夹具切类型，UI权限此前已测试。
        Target=SpawnTarget(Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(600,0,0));
        Behind=SpawnTarget(Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(900,-8,0));
        Before=Weapon->GetDamagePerPellet()*Config->PiercingDamageMultiplier;
        Player->GetWeaponComponent()->StartFire();Player->GetWeaponComponent()->StopFire();
        if(!Check(FMath::IsNearlyEqual(Target->GetHealth(),1000-Before,.02f)&&FMath::IsNearlyEqual(Behind->GetHealth(),1000-Before*Config->SecondaryDamageRatio,.02f),TEXT("actual hitscan pierces one target at half theoretical damage")))return;
        Advance(.5f);break;
    case 5:
    {
        AStaticMeshActor* Wall=GetWorld()->SpawnActor<AStaticMeshActor>(); // 本步骤自有墙体夹具，用真实Visibility阻挡验证不能穿墙。
        Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Wall->SetActorLocation(Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(750,0,0));Wall->SetActorScale3D(FVector(.2f,4,4));
        Before=Behind->GetHealth();Player->GetWeaponComponent()->StartFire();Player->GetWeaponComponent()->StopFire();
        if(!Check(Behind->GetHealth()==Before,TEXT("wall blocks penetration continuation")))return;
        Wall->Destroy();Behind->Destroy();Target->Destroy();
        Snapshot->SelectedAmmoId=TEXT("fire");Ammo->Restore(*Snapshot);
        Target=SpawnTarget(Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(600,0,0));
        ADemoShotgunWeapon* Shotgun=GetWorld()->SpawnActor<ADemoShotgunWeapon>(); // 隔离武器实例验证生产散弹弹道，不修改永久武器解锁。
        Shotgun->Config.SpreadHalfAngle=0;Shotgun->InitializeForOwner(Player);Shotgun->SetEquipped(true);Shotgun->PayShotCost();Shotgun->ExecuteCommittedShot();
        if(!Check(Target->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Burn)==1&&FMath::IsNearlyEqual(Target->GetHealth(),1000-Shotgun->GetDamagePerPellet()*Shotgun->GetPelletCount(),.02f),TEXT("shotgun pellets aggregate damage and apply exactly one stack")))return;
        Shotgun->Destroy();Advance(.1f);break;
    }
    case 6:
        Target->FindComponentByClass<UDemoAmmoStatus>()->Apply(Player->GetAbilitySystemComponent(),1,Config);
        DemoEffects::Apply(Player->GetAbilitySystemComponent(),Target->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-2000);
        if(!Check(Target->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Burn)==0,TEXT("death clears periodic GE immediately")))return;
        if(!Check(Saves->ResetActive(EDemoDifficulty::Normal,300,FDemoUpgradeProgress())&&Saves->GetSlot(0)->UnlockedAmmoIds.Num()==4&&Saves->GetSlot(0)->SelectedAmmoId==TEXT("fire"),TEXT("run reset preserves purchased ammo and saved selection")))return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_AMMO_TEST_SUCCESS"));SetActorTickEnabled(false);FPlatformMisc::RequestExitWithStatus(false,0);break;
    }
}
