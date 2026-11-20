// 职责：Editor交叉火力的真实ASC/Health GE/CombatSession与射击事务边界专项。
// 使用：-game -DemoCrossfirePrototypeTest -ExecCmds="Demo.CrossfirePrototype.Test"；须独立Saved/Tests/Crossfire设置INI，不进入普通试玩/非Editor。
// 近战用生产GE扣血后走原NotifyMeleeHit；枪用实际发射Context+真实PhysicsAsset查询。不是近战动画/Sweep或弹丸运输验收。
#include "Tests/DemoCrossfirePrototypeTest.h"
#include "GAS/DemoCrossfirePrototypeAbility.h"
#include "Debug/DemoEditorCombatPrototype.h"
#include "Debug/DemoLog.h"
#include "AI/DemoEnemy.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Characters/DemoCharacter.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Combat/DemoProjectileDamage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoCombatSession.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Save/DemoRunSave.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "CommonInputSubsystem.h" // 专项只驱动显式事务，隔离外部手柄在后台触发暂停/返回；不写设备设置。
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
    /** ConsoleWorld由引擎传入；双显式开关与独立INI限制防止测试结束进程/修改用户设备偏好。 */
    void StartCrossfireTest(UWorld* ConsoleWorld)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] StartCrossfireTest"));
        FString Settings; // 只核对显式设备INI路径，不读写正式玩家进度或设备文件。
        const FString Allowed=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tests/Crossfire/")); // 测试专用设置根。
        const bool HasSettings=FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),Settings);
        Settings=FPaths::ConvertRelativePathToFull(Settings); FPaths::NormalizeFilename(Settings); FPaths::CollapseRelativeDirectories(Settings);
        if(!FParse::Param(FCommandLine::Get(),TEXT("DemoCrossfirePrototypeTest"))||!ConsoleWorld||!ConsoleWorld->IsGameWorld()||ConsoleWorld->GetNetMode()!=NM_Standalone
            ||!HasSettings||!Settings.StartsWith(Allowed,ESearchCase::IgnoreCase))
        { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_TEST_START_REJECT requires dedicated Editor -game -DemoCrossfirePrototypeTest and Saved/Tests/Crossfire GameUserSettingsINI")); return; }
        for(TActorIterator<ADemoCrossfirePrototypeTest> It(ConsoleWorld);It;++It) if(IsValid(*It))
        { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_TEST_START_REJECT already running")); return; }
        ConsoleWorld->SpawnActor<ADemoCrossfirePrototypeTest>(); // 显式测试进程内唯一驱动，不注册到Runtime Bootstrap。
    }
    FAutoConsoleCommandWithWorld StartCommand(TEXT("Demo.CrossfirePrototype.Test"),TEXT("Dedicated Editor test process only; requires -DemoCrossfirePrototypeTest and isolated settings INI. Exits process after real GAS boundary checks."),FConsoleCommandWithWorldDelegate::CreateStatic(&StartCrossfireTest)); // Editor模块卸载自动注销，不持有World。
    /** 使用已注册的生产无敌Tag按值查询，不跨DLL引用未导出的NativeTag全局符号。 */
    FGameplayTag ImmunityTag()
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] CrossfireTest.ImmunityTag")); return FGameplayTag::RequestGameplayTag(TEXT("Demo.State.Invulnerable")); }
}

ADemoCrossfirePrototypeTest::ADemoCrossfirePrototypeTest()
{ DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; }
bool ADemoCrossfirePrototypeTest::Check(bool Condition,const TCHAR* Message)
{
    DEMO_LOG_CALL(); UE_LOG(LogFPSDemo,Display,TEXT("CROSSFIRE_TEST %s step=%d %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Step,Message);
    if(Condition) ++Passed; else { Failed=true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,1); } return Condition;
}
void ADemoCrossfirePrototypeTest::Advance(float Delay)
{ DEMO_LOG_CALL(); ++Step; NextTime=GetWorld()->GetTimeSeconds()+Delay; }
ADemoEnemy* ADemoCrossfirePrototypeTest::SpawnTarget(float Health)
{
    DEMO_LOG_CALL(); FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // World隔离高空靶场。
    ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(FVector(1000,1000.f*++SpawnCount,20000),FRotator::ZeroRotator,Params); // 分隔靶体避免真实射线串靶。
    if(!Enemy) return nullptr;
    FDemoEnemySpawnStats Stats; Stats.Health=Health; Stats.AttackPower=0; Stats.CoinReward=0; Enemy->Configure(Stats); // 无生成器登记，真实死亡不会伪造整关完成。
    Enemy->SetActorTickEnabled(false);
    USkeletalMeshComponent* Mesh=Enemy->FindComponentByClass<USkeletalMeshComponent>(); // 生产Enemy持有的真实骨骼刚体。
    UAnimSequence* Pose=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck")); // 与已有伤害靶场同一作者姿态，不改资产。
    if(!Mesh||!Pose) { Enemy->Destroy(); return nullptr; }
    Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode); Mesh->SetAnimation(Pose); Mesh->SetPosition(0,false); Mesh->TickAnimation(0,false); Mesh->RefreshBoneTransforms(); Mesh->UpdateComponentToWorld();
    return Enemy;
}
bool ADemoCrossfirePrototypeTest::BodyHit(ADemoEnemy* Enemy,FHitResult& OutHit) const
{
    DEMO_LOG_CALL();
    USkeletalMeshComponent* Mesh=Enemy?Enemy->FindComponentByClass<USkeletalMeshComponent>():nullptr; // 同步借用实际目标网格。
    const UDemoEnemyHitProfile* Profile=Enemy?Enemy->GetHitProfile():nullptr; // 目标生成时冻结规则，不能自行指定骨名绕过Resolve。
    if(!Mesh||!Profile) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CrossfirePrototypeBody),false); // 与实体弹相同通道、简单PhysicsAsset查询。
    for(const FDemoEnemyHitZoneRule& Rule:Profile->Regions) if(Rule.Region==EDemoEnemyHitRegion::Body)
        for(const FName Bone:Rule.BoneNames) if(Mesh->GetBoneIndex(Bone)!=INDEX_NONE)
            for(const FVector Axis:{FVector::ForwardVector,FVector::RightVector,FVector::UpVector})
            {
                const FVector Point=Mesh->GetBoneLocation(Bone); // 实际已求值骨中心；射线返回真实表面与实际BoneName。
                FHitResult Hit; EDemoEnemyHitRegion Region=EDemoEnemyHitRegion::Arm; float Multiplier=0; // 初始值不能冒充成功机身。
                if(GetWorld()->LineTraceSingleByChannel(Hit,Point+Axis*180,Point-Axis*180,ECC_GameTraceChannel3,Query)&&Hit.GetActor()==Enemy
                    &&Profile->ResolveHitForChannel(Hit,Region,Multiplier,ECC_GameTraceChannel3)&&Region==EDemoEnemyHitRegion::Body&&FMath::IsNearlyEqual(Multiplier,1.f)) { OutHit=Hit; return true; }
            }
    return false;
}
UDemoShotContext* ADemoCrossfirePrototypeTest::FireShot(ADemoCharacter* Player)
{
    DEMO_LOG_CALL(); ADemoProjectileBase::CancelAll(GetWorld()); // 取消前枪运输；测试不伪造ShotCommitted或ShotId。
    ADemoWeaponBase* Weapon=Player?Player->GetWeaponComponent()->GetActiveWeapon():nullptr; // 原固定手枪足够测试，默认无主枪不被改变。
    if(!Weapon) return nullptr;
    if(Weapon->GetAmmo()<Weapon->Config.AmmoPerShot) Weapon->Refill(); // 仅本次实例补给以覆盖边界数量，不改存档或默认库存。
    const int32 Before=Weapon->GetAmmo(); // 真实成本独立断言。
    if(!Check(Weapon->PrepareShot(),TEXT("original weapon prepares whole shot"))) return nullptr;
    Weapon->PayShotCost(); Weapon->CommitFireCooldown(); Weapon->ExecuteCommittedShot(); // 原事务负责成功挑战记录与Editor post-melee ShotCommitted hook。
    UDemoShotContext* Result=nullptr; // Context随后由测试UPROPERTY保活，弹体仍属于原Weapon/World。
    for(TActorIterator<ADemoProjectileBase> It(GetWorld());It;++It) if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&It->CanSimulate()&&It->GetShotContext()&&It->GetShotContext()->GetSourcePawn()==Player) { Result=It->GetShotContext(); break; }
    if(!Check(Result&&Result->IsAttackValid()&&Weapon->GetAmmo()==Before-Weapon->Config.AmmoPerShot,TEXT("successful original shot pays once and exposes actual committed context"))) return nullptr;
    ADemoProjectileBase::CancelAll(GetWorld()); // 后续Apply重现真实碰撞后的结算边界；本项不声称验证弹丸运输。
    return Result;
}
bool ADemoCrossfirePrototypeTest::ConfirmMelee(ADemoCharacter* Player,ADemoEnemy* Enemy,float RawDamage)
{
    DEMO_LOG_CALL(); if(!Check(Player&&Enemy&&Enemy->IsCombatSessionValid(),TEXT("melee fixture bound to real active target session"))) return false;
    UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Player); // 原PS组件，不手工广播候选桥。
    const float Before=Enemy->GetHealth(); // 只有真实损失才发送原近战确认入口。
    const bool Applied=DemoEffects::Apply(Player->GetAbilitySystemComponent(),Enemy->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-RawDamage);
    const float Actual=Before-Enemy->GetHealth(); // 实际GE处理免伤和致死，通知不能伪造Actual。
    if(!Check(Build&&Applied&&Actual>0,TEXT("original Health GE confirms actual melee HP loss"))) return false;
    Build->NotifyMeleeHit(Enemy,RawDamage,Actual,false,!Enemy->IsAlive(),FVector::ForwardVector);
    return true;
}
bool ADemoCrossfirePrototypeTest::Direct(UDemoShotContext* Context,ADemoEnemy* Enemy,float ExpectedLoss,int32 Pellet,bool Secondary)
{
    DEMO_LOG_CALL(); FHitResult Hit; // 真实碰撞查询，不预填目标骨/伤害倍率。
    if(!Check(Context&&Enemy&&BodyHit(Enemy,Hit),TEXT("real PhysicsAsset one-times body hit available"))) return false;
    const float Before=Enemy->GetHealth(); // 原枪10HP与额外冲击独立人工期望。
    return Check(DemoProjectileDamage::Apply(Context,Hit,10.f,Pellet,Secondary)&&FMath::IsNearlyEqual(Before-Enemy->GetHealth(),ExpectedLoss,.02f),TEXT("production damage path matches expected actual HP loss"));
}
void ADemoCrossfirePrototypeTest::ImmunizeAfterDamage(const FOnAttributeChangeData& Data)
{
    DEMO_LOG_CALL(); if(ImmunizeOnNextDamage&&Data.NewValue<Data.OldValue&&ImmunityASC.IsValid())
    { ImmunizeOnNextDamage=false; ImmunityASC->AddLooseGameplayTag(ImmunityTag()); } // 同步原枪扣血后、候选额外GE前授予生产无敌，不直接改冲击结果。
}
void ADemoCrossfirePrototypeTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds); if(Failed) return;
    if(StartedAt==0) StartedAt=FPlatformTime::Seconds();
    if(FPlatformTime::Seconds()-StartedAt>90) { Check(false,TEXT("bounded isolated runtime including resource handshake")); return; }
    ADemoPlayerController* PC=nullptr; // 专用单人World唯一Controller，不误用跨World玩家0。
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It) if(auto* Local=Cast<ADemoPlayerController>(It->Get())) if(Local->IsLocalController()) PC=Local;
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 本帧借用，旅行/重生不会继承旧指针。
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    ADemoPlayerState* PS=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr;
    if(!PC||!Player||!PS||!Mode||!State||!Player->GetDemoAttributes()) return; // 初始加载有界等待，不伪造ASC。
    if(Step<=1&&PC->GetMenuFlow()) PC->GetMenuFlow()->CloseFirstRunGuide(); // 只影响命令强制要求的独立设备INI；防首次指南暂停挡住资源握手。
    if(GetWorld()->GetTimeSeconds()<NextTime) return;
    UAbilitySystemComponent* ASC=Player->GetAbilitySystemComponent(); // 本人原PS ASC。
    UDemoRunBuildComponent* Build=PS->GetRunBuildComponent(); // 原核心选择值供隔离覆盖断言。
    FString Error; // 每次真实启停返回原因，不由测试写入成功状态。
    switch(Step)
    {
    case 0:
    {
        if(auto* Local=PC->GetLocalPlayer()) if(auto* Input=Local->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("CrossfireAutomation"),true); // 只属于本隔离LocalPlayer，World/进程退出释放，不影响正常试玩手柄。
        UDemoRunSaves* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // WITH_EDITOR每GI临时本地槽，退出自动清理，无云。
        if(!Check(Saves&&Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("isolated Editor local run starts through real Flow"))) return;
        Saves->ConsumePending(); PC->OnRunReady(); PC->GetMenuFlow()->CloseFirstRunGuide();
        if(!Check(Build->TrySelectCore(TEXT("B02")),TEXT("select actual formal core before prototype override"))) return;
        Mode->StartNextLevel(); Advance(.2f); break;
    }
    case 1:
        if(State->Phase!=EDemoPhase::Combat) { NextTime=GetWorld()->GetTimeSeconds()+.2; return; } // 正式资源/落脚点/引擎ACK自动推进，不手写Combat或伪造Session。
        if(!Check(UDemoCombatSession::FindActive(GetWorld())&&Build->IsEncounterEnabled(),TEXT("real Flow committed CombatSession and formal behavior"))) return;
        for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 保留天然登记敌人避免空关；靶子另行真实生成。
        Player->SetActorLocation(FVector(0,0,20000)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation(FRotator::ZeroRotator);
        Player->GetWeaponComponent()->EquipSlot(2); // 只用默认手枪，未解锁/装备主枪不被候选授予。
        Player->GetWeaponComponent()->GetActiveWeapon()->Config.BaseDamage=10.f; // 只修改本测试武器实例，独立人工HP基准。
        if(!Check(UDemoCrossfirePrototypeAbility::EnableForPlayer(Player,Error)&&!Build->IsEncounterEnabled()&&Build->GetSnapshot().CoreId==TEXT("B02"),TEXT("prototype activates on original ASC and stops formal runtime without changing choice"))) return;
        if(!Check(GetDefault<UDemoCrossfirePrototypeAbility>()->WindowSeconds==3.f&&GetDefault<UDemoCrossfirePrototypeAbility>()->ImpactRatio==.6f&&GetDefault<UDemoCrossfirePrototypeAbility>()->MaximumImpactDamage==60.f,TEXT("known prototype baseline is three seconds sixty-percent capped sixty HP"))) return;
        Target=SpawnTarget(); Other=SpawnTarget(); if(!Check(Target.IsValid()&&Other.IsValid(),TEXT("separate actual Enemy GAS targets created"))) return;
        Advance(); break;
    case 2:
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),10.f)) return;
        if(!Check(DemoEditorCombatPrototype::ReadView(Player).ConsumedCharges==0,TEXT("unopened ordinary gun damage remains normal and consumes no combo"))) return;
        Advance(); break;
    case 3:
        if(!ConfirmMelee(Player,Target.Get())) return;
        if(!Check(FMath::IsNearlyEqual(DemoEditorCombatPrototype::ReadView(Player).ChargeDamage,12.f)&&DemoEditorCombatPrototype::ReadView(Player).Remaining>2.9f,TEXT("surviving melee arms same-target three-second twelve-HP charge"))) return;
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),22.f)||!Direct(Shot,Target.Get(),10.f,1)) return;
        if(!Check(DemoEditorCombatPrototype::ReadView(Player).ConsumedCharges==1&&DemoEditorCombatPrototype::ReadView(Player).ConfirmedImpacts==1,TEXT("same shot extra pellet cannot repeat consumed impact"))) return;
        Advance(); break;
    case 4:
        if(!ConfirmMelee(Player,Target.Get())) return;
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Other.Get(),10.f)) return;
        if(!Check(DemoEditorCombatPrototype::ReadView(Player).Remaining>0&&DemoEditorCombatPrototype::ReadView(Player).ConsumedCharges==1,TEXT("different target keeps charge and ordinary damage"))) return;
        if(!Direct(Shot,Target.Get(),22.f)||!Direct(Shot,Other.Get(),10.f,1)) return;
        Advance(); break;
    case 5:
        if(!ConfirmMelee(Player,Target.Get())) return;
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),10.f,0,true)) return;
        if(!Check(DemoEditorCombatPrototype::ReadView(Player).Remaining>0,TEXT("derived hit cannot consume charge"))) return;
        if(!Direct(Shot,Target.Get(),22.f,1)) return; Advance(); break;
    case 6:
        OldShot=FireShot(Player); if(!OldShot||!ConfirmMelee(Player,Target.Get())||!Direct(OldShot,Target.Get(),10.f)) return;
        if(!Check(DemoEditorCombatPrototype::ReadView(Player).Remaining>0,TEXT("shot fired before melee cannot redeem new charge"))) return;
        Advance(); break;
    case 7:
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),22.f)||!ConfirmMelee(Player,Target.Get())) return;
        Advance(3.2f); break; // 等真实World游戏时钟与UObjectTimer到期，不手动调用ExpireCharge。
    case 8:
        if(!Check(DemoEditorCombatPrototype::ReadView(Player).Remaining==0,TEXT("charge naturally expires after three seconds"))) return;
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),10.f)||!ConfirmMelee(Player,Target.Get(),1000.f)) return;
        if(!Check(DemoEditorCombatPrototype::ReadView(Player).ChargeDamage==60.f,TEXT("large real melee damage freezes bounded sixty-HP budget"))) return;
        Advance(); break;
    case 9:
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),70.f)||!ConfirmMelee(Player,Target.Get())) return;
        Target->GetAbilitySystemComponent()->AddLooseGameplayTag(ImmunityTag()); Advance(); break;
    case 10:
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),0.f)) return;
        if(!Check(DemoEditorCombatPrototype::ReadView(Player).Remaining>0,TEXT("immune original gun hit cannot consume or claim successful impact"))) return;
        Target->GetAbilitySystemComponent()->RemoveLooseGameplayTag(ImmunityTag()); Advance(); break;
    case 11:
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),22.f)||!ConfirmMelee(Player,Target.Get())) return;
        ImmunityASC=Target->GetAbilitySystemComponent(); ImmunizeOnNextDamage=true;
        ImmunityBinding=ImmunityASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).AddUObject(this,&ADemoCrossfirePrototypeTest::ImmunizeAfterDamage); // Weak UObject callback removed in following step/EndPlay.
        Advance(); break;
    case 12:
    {
        const FDemoEditorCrossfireView Before=DemoEditorCombatPrototype::ReadView(Player); // 独立于GA内部预算，用实际HP和确认次数比较。
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),10.f)) return;
        const FDemoEditorCrossfireView After=DemoEditorCombatPrototype::ReadView(Player); // 原枪先扣10，额外GE被生产Tag拒绝，消费不等于实际冲击成功。
        if(!Check(After.ConsumedCharges==Before.ConsumedCharges+1&&After.ConfirmedImpacts==Before.ConfirmedImpacts,TEXT("immune additional GE consumes once without reporting fake impact"))) return;
        ImmunityASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).Remove(ImmunityBinding); ImmunityBinding.Reset(); ImmunityASC->RemoveLooseGameplayTag(ImmunityTag()); ImmunityASC.Reset();
        if(!ConfirmMelee(Player,Target.Get())) return;
        Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),5.f); // 仅残血边界，不复活或直接伪造死亡。
        Advance(); break;
    }
    case 13:
    {
        const FDemoEditorCrossfireView Before=DemoEditorCombatPrototype::ReadView(Player); // 致死原枪不能对尸体补伤。
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),5.f)) return;
        const FDemoEditorCrossfireView After=DemoEditorCombatPrototype::ReadView(Player);
        if(!Check(!Target->IsAlive()&&After.ConsumedCharges==Before.ConsumedCharges+1&&After.ConfirmedImpacts==Before.ConfirmedImpacts,TEXT("lethal original hit consumes without corpse impact"))) return;
        Target=SpawnTarget(); if(!Check(Target.IsValid(),TEXT("fresh target never resurrects old dead actor"))||!ConfirmMelee(Player,Target.Get(),6000.f)) return;
        if(!Check(!Target->IsAlive()&&DemoEditorCombatPrototype::ReadView(Player).Remaining==0,TEXT("lethal melee cannot arm a dead target"))) return;
        Target=SpawnTarget(); if(!Check(Target.IsValid(),TEXT("new live target for disable boundary"))||!ConfirmMelee(Player,Target.Get())) return;
        Advance(); break;
    }
    case 14:
        if(!Check(UDemoCrossfirePrototypeAbility::DisableForPlayer(Player,Error)&&!DemoEditorCombatPrototype::ReadView(Player).bActive&&!ASC->FindAbilitySpecFromClass(UDemoCrossfirePrototypeAbility::StaticClass()),TEXT("disable removes candidate spec and weak HUD reader"))) return;
        Shot=FireShot(Player); if(!Shot||!Direct(Shot,Target.Get(),10.f)) return;
        if(!Check(UDemoCrossfirePrototypeAbility::EnableForPlayer(Player,Error),TEXT("explicit enable creates clean new runtime"))||!ConfirmMelee(Player,Target.Get())) return;
        Advance(); break;
    case 15:
    {
        UDemoCombatSession* Session=UDemoCombatSession::FindActive(GetWorld()); // 原Flow拥有的真实撤销凭据，测试不替换它。
        if(!Check(Session!=nullptr,TEXT("active session exists before revocation"))) return;
        FHitResult Hit; if(!Check(BodyHit(Target.Get(),Hit),TEXT("cache real pending collision before target deactivation"))) return; // 撤销会关闭Enemy碰撞；晚到碰撞回调携带的是撤销前已取得的真实Hit。
        const float Before=Target->GetHealth(); Session->Invalidate(); // 同栈调用GA清理；不是只把界面改成Intermission。
        if(!Check(!DemoEditorCombatPrototype::ReadView(Player).bActive&&!ASC->FindAbilitySpecFromClass(UDemoCrossfirePrototypeAbility::StaticClass()),TEXT("session revocation synchronously removes prototype and pending charge"))) return;
        if(!Check(!DemoProjectileDamage::Apply(Shot,Hit,10.f,0)&&Target->GetHealth()==Before,TEXT("stale committed shot cannot damage after real session revocation"))) return;
        if(!Check(!UDemoCrossfirePrototypeAbility::EnableForPlayer(Player,Error)&&UDemoCrossfirePrototypeAbility::DisableForPlayer(Player,Error),TEXT("ended session rejects enable and disable remains idempotent"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_CROSSFIRE_PROTOTYPE_SUCCESS assertions=%d scope=Editor_original_ASC_GE_weapon_transaction_CombatSession_no_animation_sweep_transport_or_multiplayer"),Passed);
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
    }
}
void ADemoCrossfirePrototypeTest::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    if(ImmunityASC.IsValid()) { ImmunityASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).Remove(ImmunityBinding); ImmunityASC->RemoveLooseGameplayTag(ImmunityTag()); }
    ImmunityBinding.Reset(); ImmunityASC.Reset(); // 弱UObject回调不跨World留下夹具状态。
    Super::EndPlay(Reason);
}
