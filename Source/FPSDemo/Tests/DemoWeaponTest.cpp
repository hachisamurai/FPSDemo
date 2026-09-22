#include "Tests/DemoWeaponTest.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Interaction/DemoInteractable.h"
#include "Engine/GameInstance.h"
#include "Game/FPSDemoGameMode.h"
#include "AI/DemoEnemy.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "GAS/DemoGameplayAbility.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

ADemoWeaponTest::ADemoWeaponTest()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .02f;
}
bool ADemoWeaponTest::Check(bool Condition, const TCHAR* Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Display, TEXT("WEAPON_TEST %s step=%d: %s"), Condition ? TEXT("PASS") : TEXT("FAIL"), Step, Message);
    if (!Condition) { bFailed = true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 1); }
    return Condition;
}
void ADemoWeaponTest::Advance(float Delay)
{
    DEMO_LOG_CALL();
    ++Step;
    NextTime = GetWorld()->GetTimeSeconds() + Delay;
}
void ADemoWeaponTest::SendKey(FKey Key, bool bDown)
{
    DEMO_LOG_CALL();
    // PC由当前World拥有；合成按键只进入本测试游戏，不控制桌面或其他应用。
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    PC->InputKey(FInputKeyParams(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.0 : 0.0));
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_TEST_KEY %s down=%d"), *Key.ToString(), bDown);
}
void ADemoWeaponTest::Capture(const TCHAR* Name)
{
    DEMO_LOG_CALL();
    if (FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponCapture")))
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/Weapons")/FString(Name)+TEXT(".png"), false, false);
}
bool ADemoWeaponTest::SelectAtTerminal(int32 Index)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0)); // 同步借用测试控制器。
    ADemoCharacter* Player = Cast<ADemoCharacter>(PC->GetPawn()); // 测试玩家，操作后恢复坐标。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 初始安全区终端在此武器专项内保留。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 仅测试夹具临时切换阶段；真实十关流程由ArmoryTest覆盖。
    const EDemoPhase PreviousPhase = State->Phase; // 装备后恢复测试战斗阶段。
    const FVector PreviousLocation = Player->GetActorLocation(); // 原来的隔离射击位置。
    if (!Mode->GetShopTerminal()) return Check(false, TEXT("weapon fixture terminal exists"));
    State->Phase = EDemoPhase::Hub;
    Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation() + FVector(-180,0,20));
    Mode->GetShopTerminal()->Interact(Player);
    PC->SetTerminalWeaponPage(true);
    PC->InspectWeapon(Index + 1);
    PC->EquipInspectedWeapon();
    PC->CloseWeaponTip();
    PC->CloseUpgradeMenu();
    State->Phase = PreviousPhase;
    Player->SetActorLocation(PreviousLocation);
    return Check(Player->GetWeaponComponent()->GetPrimaryIndex() == Index, TEXT("weapon fixture selects via terminal and real equip guards"));
}
bool ADemoWeaponTest::CheckWeaponVisual(const TCHAR* Label)
{
    DEMO_LOG_CALL();
    // 所有引用仅借用本步骤已初始化的 Pawn；终端装备与动画稳定后才进行测量。
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    ADemoWeaponBase* Weapon = Player ? Player->GetWeaponComponent()->GetActiveWeapon() : nullptr;
    UStaticMeshComponent* Legacy = Weapon ? Weapon->FindComponentByClass<UStaticMeshComponent>() : nullptr; // 迁移后旧静态外观应为空。
    USkeletalMeshComponent* Visual = Weapon ? Weapon->FindComponentByClass<USkeletalMeshComponent>() : nullptr; // 实际活动枪械骨骼，枪口也必须来自这里。
    if (!Check(Visual && Weapon->Config.Mesh && Visual->GetSkeletalMeshAsset() == Weapon->Config.Mesh
        && !Weapon->Config.StaticMesh && Legacy && !Legacy->GetStaticMesh() && !Weapon->IsHidden()
        && Visual->GetCollisionEnabled() == ECollisionEnabled::NoCollision
        && Visual->DoesSocketExist(Weapon->Config.MuzzleSocket), TEXT("skeletal gun active, static visual empty, no collision, muzzle socket present"))) return false;
    // 相机局部坐标厘米，+X 前、+Y 右、+Z 上；用于验证模型朝向与第一人称占屏。
    const FTransform Camera = Player->GetFirstPersonCameraComponent()->GetComponentTransform();
    const FVector LocalMuzzle = Camera.InverseTransformPosition(Weapon->GetMuzzleLocation());
    const FVector LocalOrigin = Camera.InverseTransformPosition(Visual->GetComponentLocation());
    const FRotator LocalRotation = Camera.InverseTransformRotation(Visual->GetComponentQuat()).Rotator();
    UE_LOG(LogFPSDemo, Display, TEXT("WEAPON_MESH_POSE %s origin=%s rotation=%s muzzle=%s grip=%s"), Label,
        *LocalOrigin.ToString(), *LocalRotation.ToString(), *LocalMuzzle.ToString(),
        *Player->GetMesh1P()->GetSocketTransform(Weapon->Config.AttachSocket).ToString());
    // 稳定武器锚点独立于双手；待机支撑手由公共IK校正，换弹双手使用成对烘焙动作。
    const FVector RightHand = Camera.InverseTransformPosition(Player->GetMesh1P()->GetSocketLocation(TEXT("hand_r")));
    const FVector LeftHand = Camera.InverseTransformPosition(Player->GetMesh1P()->GetSocketLocation(TEXT("hand_l")));
    UE_LOG(LogFPSDemo, Display, TEXT("WEAPON_HAND_POSE right=%s left=%s"), *RightHand.ToString(), *LeftHand.ToString());
    if (!Check(Weapon->GetMuzzleLocation().Equals(Visual->GetSocketLocation(Weapon->Config.MuzzleSocket), .01f), TEXT("ballistics uses actual skeletal muzzle"))) return false;
    // 验证实际待机朝向与镜前位置；错轴 FBX/握点偏移不能只靠截图发现。
    if (!Check(LocalMuzzle.X > 20.f && FMath::Abs(LocalRotation.Yaw) < 2.f && FMath::Abs(LocalRotation.Pitch) < 2.f
        && Player->GetMesh1P()->IsBoneHiddenByName(TEXT("upperarm_l")) == Weapon->Config.bHideSupportArm,
        TEXT("muzzle faces camera forward; support arm follows active weapon and restores on switch"))) return false;
    Capture(Label);
    return true;
}
void ADemoWeaponTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds() < 90.f, TEXT("bounded runtime"))) return;
    // 所有裸指针只借用本步对象，不由测试改变生命周期。
    ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this, 0));
    ADemoCharacter* Player = PC ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr;
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
    if (!Check(Player && Mode && State && Player->GetDemoASC(), TEXT("runtime ready"))) return;
    UDemoWeaponComponent* Equipment = Player->GetWeaponComponent();
    ADemoWeaponBase* Weapon = Equipment->GetActiveWeapon();
    UDemoAbilitySystemComponent* ASC = Player->GetDemoASC();
    if (!Check(Weapon != nullptr, TEXT("real blueprint loadout ready"))) return;
    switch (Step)
    {
    case 0:
    {
        if (!Check(Equipment->GetActiveSlot() == 2 && Equipment->GetPrimaryIndex() == INDEX_NONE, TEXT("new loadout starts pistol only"))) return;
        // 本专项隔离槽的解锁夹具：不修改正常玩家档案。真实三难度完整通关在DemoArmoryTest验证。
        UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // GI拥有的测试档案。
        State->Phase = EDemoPhase::Victory;
        State->LevelNumber = 10;
        for (int32 Index = 0; Index < 3; ++Index) // 模拟已完成三难度的持久进度，便于聚焦射击/换弹时序。
        {
            State->Difficulty = static_cast<EDemoDifficulty>(Index);
            Profile->RecordVictory(FGuid::NewGuid().ToString(), State->Difficulty);
        }
        State->Phase = EDemoPhase::Lobby;
        State->LevelNumber = 0;
        State->Difficulty = EDemoDifficulty::Normal;
        Mode->StartRun(); // 存档选择属于Session专项；武器夹具直接建立无活动存档的新局。
        PC->OnRunReady();
        if (!SelectAtTerminal(0)) return; // 步枪必须显式在终端装备，不能假设初始拥有。
        Advance(.5f); // 等待输入上下文重建，按键不能早于IMC生效。
        break;
    }
    case 1:
        Rifle = Weapon;
        if (!Check(State->Phase == EDemoPhase::Hub && Equipment->GetActiveSlot() == 1 && Weapon->GetAmmo() == 12
            && Weapon->Config.FireMode == EDemoFireMode::Automatic, TEXT("terminal-selected rifle primary in safe hub"))) return;
        SendKey(EKeys::Two, true);
        Advance();
        break;
    case 2:
        Pistol = Weapon;
        // 使用当前蓝图的策划伤害值校验实例；当前资产为18，旧硬编码20会在真实换枪成功后误报。
        if (!Check(Equipment->GetActiveSlot() == 2 && Weapon->Config.FireMode == EDemoFireMode::SemiAutomatic
            && Weapon->GetClass()->GetFName() == TEXT("BP_Weapon_Pistol_C")
            && Weapon->Config.BaseDamage == Weapon->GetClass()->GetDefaultObject<ADemoWeaponBase>()->Config.BaseDamage,
            TEXT("physical 2 maps to current pistol BP and authored damage"))) return;
        SendKey(EKeys::Two, false);
        if (!SelectAtTerminal(1)) return; // 已解锁散弹也必须走终端，旧B键不再授予武器。
        Advance();
        break;
    case 3:
        if (!Check(Weapon->IsA<ADemoShotgunWeapon>() && Equipment->GetActiveSlot() == 1 && Weapon->GetPelletCount() == 8, TEXT("terminal selects shotgun primary, separate from secondary slot"))) return;
        Advance();
        break;
    case 4:
        if (!SelectAtTerminal(2)) return; // 狙击枪在终端选择，不更改副武器实例。
        Advance();
        break;
    case 5:
        if (!Check(Weapon->IsA<ADemoSniperWeapon>() && Weapon->Config.bSupportsScope && Weapon->GetAmmo() == 5, TEXT("terminal selects sniper BP with persisted config"))) return;
        State->Phase = EDemoPhase::Combat; State->LevelNumber=1; // 合法战斗快照需正关号；保留安全区终端用于本测试的真实装备权限。
        // 隔离攻击/碰撞；测试靶子另行生成，不引起自动清关。
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); }
        Player->SetActorLocation(Mode->GetAreaCenter(0) + FVector(0,0,700));
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Player->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator::ZeroRotator);
        if (!Check(!Equipment->SelectPrimary(0), TEXT("combat rejects primary-model change"))) return;
        SendKey(EKeys::RightMouseButton, true);
        Advance(.5f);
        break;
    case 6:
        if (!Check(Equipment->GetScopeLevel() == 1 && Player->GetFirstPersonCameraComponent()->FieldOfView == 40.f
            && Weapon->IsHidden() && Player->GetMesh1P()->bHiddenInGame && ASC->HasMatchingGameplayTag(DemoTags::Aiming), TEXT("right mouse activates GAS scope, FOV40 and hides arms/weapon"))) return;
        Capture(TEXT("01-Scope40"));
        SendKey(EKeys::RightMouseButton, false);
        Advance();
        break;
    case 7:
        SendKey(EKeys::RightMouseButton, true);
        Advance();
        break;
    case 8:
        if (!Check(Equipment->GetScopeLevel() == 2 && Player->GetFirstPersonCameraComponent()->FieldOfView == 10.f, TEXT("second press gives true FOV10"))) return;
        Capture(TEXT("02-Scope10"));
        SendKey(EKeys::RightMouseButton, false);
        Advance();
        break;
    case 9:
        SendKey(EKeys::RightMouseButton, true);
        Advance();
        break;
    case 10:
        if (!Check(Equipment->GetScopeLevel() == 0 && Player->GetFirstPersonCameraComponent()->FieldOfView == 90.f
            && !Weapon->IsHidden() && !Player->GetMesh1P()->bHiddenInGame && !ASC->HasMatchingGameplayTag(DemoTags::Aiming), TEXT("third press restores FOV, visuals and GAS tag"))) return;
        SendKey(EKeys::RightMouseButton, false);
        SendKey(EKeys::Two, true);
        Advance();
        break;
    case 11:
        if (!Check(Weapon == Pistol.Get() && Weapon->GetAmmo() == 12, TEXT("switch reuses pistol instance with independent magazine"))) return;
        SendKey(EKeys::Two, false);
        // 仅此测试实例改为有限2发备用；Refill模拟补给，随后受控接口清空弹匣。
        Weapon->Config.bInfiniteReserve = false;
        Weapon->Config.InitialReserve = 2;
        Weapon->Refill();
        Weapon->ModifyAmmo(-100);
        SendKey(EKeys::LeftMouseButton, true);
        Advance(.3f);
        break;
    case 12:
        if (!Check(Weapon->IsReloading() && Weapon->GetAmmo() == 0 && Weapon->GetReserveAmmo() == 2
            && Weapon->GetFireCooldownRemaining() == 0.f && ASC->HasMatchingGameplayTag(DemoTags::Reloading), TEXT("empty physical fire starts reload without shot cost or cooldown"))) return;
        SendKey(EKeys::R, true);
        if (!Check(!Equipment->RequestReload(), TEXT("duplicate manual reload rejected without restarting task"))) return;
        Advance(.2f);
        break;
    case 13:
        SendKey(EKeys::R, false);
        Advance(.85f); // 总等待超过1.2秒；若R重复重启计时，此时仍未完成。
        break;
    case 14:
        if (!Check(!Weapon->IsReloading() && Weapon->GetAmmo() == 2 && Weapon->GetReserveAmmo() == 0
            && !ASC->HasMatchingGameplayTag(DemoTags::Reloading), TEXT("original reload deadline transfers finite reserve once; held fire does not shoot"))) return;
        SendKey(EKeys::LeftMouseButton, false);
        Advance();
        break;
    case 15:
        SendKey(EKeys::LeftMouseButton, true);
        Advance(.6f);
        break;
    case 16:
        if (!Check(Weapon->GetAmmo() == 1, TEXT("held pistol fires once despite elapsed fire interval"))) return;
        SendKey(EKeys::LeftMouseButton, false);
        Advance();
        break;
    case 17:
        SendKey(EKeys::LeftMouseButton, true);
        Advance(.4f);
        break;
    case 18:
        if (!Check(Weapon->GetAmmo() == 0 && !Weapon->IsReloading(), TEXT("last round alone never starts reload"))) return;
        SendKey(EKeys::LeftMouseButton, false);
        Advance();
        break;
    case 19:
        SendKey(EKeys::LeftMouseButton, true);
        Advance();
        break;
    case 20:
        if (!Check(!Weapon->IsReloading() && Weapon->GetAmmo() == 0 && Weapon->GetReserveAmmo() == 0, TEXT("empty reserve rejects empty-fire reload safely"))) return;
        SendKey(EKeys::LeftMouseButton, false);
        Weapon->Config.bInfiniteReserve = true;
        Weapon->Config.InitialReserve = 60;
        SendKey(EKeys::R, true);
        Advance(.3f);
        break;
    case 21:
        if (!Check(Weapon->IsReloading(), TEXT("manual R starts same reload interface"))) return;
        SendKey(EKeys::R, false);
        SendKey(EKeys::One, true);
        Advance(1.5f);
        break;
    case 22:
        if (!Check(Weapon->IsA<ADemoSniperWeapon>() && Pistol->GetAmmo() == 0 && !Pistol->IsReloading()
            && !ASC->HasMatchingGameplayTag(DemoTags::Reloading), TEXT("physical 1 cancels old reload; delayed task cannot refill either gun"))) return;
        SendKey(EKeys::One, false);
        if (!SelectAtTerminal(1)) return; // 临时移到终端并恢复射击位置，仍执行生产装备权限。
        {
            // 大靶子包住600cm处6度散布，用于准确断言8颗实体弹丸累计伤害而非概率命中。
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ADemoEnemy* Probe = GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(), Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(500,0,0), FRotator::ZeroRotator, Spawn);
            FDemoEnemySpawnStats Stats; // 1000HP、不自动攻击，仅测试目标；不加入GM注册表。
            Stats.Health = 1000.f;
            Probe->Configure(Stats);
            Probe->SetActorTickEnabled(false);
            Probe->SetActorScale3D(FVector(4.f));
            Target = Probe;
        }
        DemoEffects::Apply(ASC, ASC, UDemoPowerEffect::StaticClass(), 5.f);
        SendKey(EKeys::LeftMouseButton, true);
        Capture(TEXT("03-Shotgun")); // 请求后保留霰弹至少0.9秒，避免下一帧截图记录切换后的狙击枪。
        Advance(.9f);
        break;
    case 23:
        if (!Check(Weapon->GetAmmo() == 5 && Target.IsValid() && FMath::IsNearlyEqual(Target->GetHealth(),931.f,.01f), TEXT("shotgun held input costs one shell, 8 projectiles total64 plus global5 after actual flight"))) return;
        SendKey(EKeys::LeftMouseButton, false);
        // 关闭靶子碰撞，后续狙击/自动步枪检查只关心成本和时序。
        Target->SetActorEnableCollision(false);
        if (!SelectAtTerminal(2)) return; // 切回保留原狙击枪弹药实例。
        SendKey(EKeys::RightMouseButton, true);
        Advance();
        break;
    case 24:
        SendKey(EKeys::RightMouseButton, false);
        SendKey(EKeys::LeftMouseButton, true);
        Advance();
        break;
    case 25:
        if (!Check(Weapon->GetAmmo() == 4 && Equipment->GetScopeLevel() == 0 && Player->GetFirstPersonCameraComponent()->FieldOfView == 90.f, TEXT("sniper shot consumes one round and exits scope"))) return;
        SendKey(EKeys::LeftMouseButton, false);
        SendKey(EKeys::RightMouseButton, true);
        Advance();
        break;
    case 26:
        SendKey(EKeys::RightMouseButton, false);
        if (!Check(Equipment->GetScopeLevel() == 1, TEXT("scope re-enters after shot"))) return;
        SendKey(EKeys::R, true);
        Advance();
        break;
    case 27:
        if (!Check(Equipment->GetScopeLevel() == 0 && Weapon->IsReloading(), TEXT("reload cancels aiming and restores FOV"))) return;
        SendKey(EKeys::R, false);
        Equipment->CancelActions();
        if (!SelectAtTerminal(0)) return; // 模型切换与取消装填走终端同一路径。
        Rifle->ModifyAmmo(-10); // 留2发，验证自动武器连射耗尽后停止而非自动换弹。
        SendKey(EKeys::LeftMouseButton, true);
        Advance(.6f);
        break;
    case 28:
        if (!Check(Weapon == Rifle.Get() && Weapon->GetAmmo() == 0 && !Weapon->IsReloading(), TEXT("automatic rifle fires both rounds then stops while key remains down"))) return;
        SendKey(EKeys::LeftMouseButton, false);
        Advance();
        break;
    case 29:
        SendKey(EKeys::LeftMouseButton, true);
        Advance(1.7f);
        break;
    case 30:
        if (!Check(Weapon->GetAmmo() == 12 && !Weapon->IsReloading(), TEXT("new empty rifle press reloads; held key never auto-resumes"))) return;
        SendKey(EKeys::LeftMouseButton, false);
        // 菜单输入所有权必须通过真实终端打开；远距离直接Open会被新的生产权限守卫正确拒绝。
        if (!Check(Mode->GetShopTerminal()!=nullptr,TEXT("input-menu fixture retains real hub terminal"))) return;
        MenuReturnLocation=Player->GetActorLocation(); State->Phase = EDemoPhase::Hub;
        State->Coins = 100;
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
        Mode->GetShopTerminal()->Interact(Player);
        if (!Check(PC->IsUpgradeMenuOpen(),TEXT("real in-range terminal opens input-blocking menu"))) return;
        Advance();
        break;
    case 31:
        SendKey(EKeys::Two, true);
        Advance();
        break;
    case 32:
        if (!Check(Equipment->GetActiveSlot() == 1 && PC->IsUpgradeMenuOpen(), TEXT("menu removes combat mapping so numeric choice never switches weapon"))) return;
        SendKey(EKeys::Two, false);
        PC->CloseUpgradeMenu();
        Equipment->CancelActions();
        Player->SetActorLocation(MenuReturnLocation); // 菜单关闭后才回到靶场，保持真实会话范围约束。
        // 原有功能断言通过后，额外走四把实模的稳定帧截图，避免请求截图同帧切枪。
        // 清理先前的放大靶子并返回战斗画面；只影响专项夹具，四张截图使用相同朝向。
        if (Target.IsValid()) Target->Destroy();
        State->Phase = EDemoPhase::Combat;
        if (!SelectAtTerminal(0)) return;
        Advance(.5f);
        break;
    case 33:
        if (!CheckWeaponVisual(TEXT("Mesh-Rifle"))) return;
        Advance(.5f);
        break;
    case 34:
        SendKey(EKeys::Two, true);
        Advance(.5f);
        break;
    case 35:
        SendKey(EKeys::Two, false);
        if (!CheckWeaponVisual(TEXT("Mesh-Pistol"))) return;
        Advance(.5f);
        break;
    case 36:
        if (!SelectAtTerminal(1)) return;
        Advance(.5f);
        break;
    case 37:
        if (!CheckWeaponVisual(TEXT("Mesh-Shotgun"))) return;
        Advance(.5f);
        break;
    case 38:
        if (!SelectAtTerminal(2)) return;
        Advance(.5f);
        break;
    case 39:
        if (!CheckWeaponVisual(TEXT("Mesh-Sniper"))) return;
        Advance(.5f);
        break;
    case 40:
        UE_LOG(LogFPSDemo, Display, TEXT("DEMO_WEAPON_SUCCESS: real key mapping, four BP skeletal meshes and sockets, empty-trigger reload, finite reserve, cancellation, shotgun damage, sniper scope"));
        SetActorTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    }
}
