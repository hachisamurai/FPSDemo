#include "Tests/DemoEnemyAttackTest.h"
#include "Tests/DemoProjectileFixtures.h"
#include "AI/DemoEnemy.h"
#include "Animation/DemoEnemyPresentation.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Animation/AnimSequence.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "AbilitySystemComponent.h"
#include "Save/DemoRunSave.h"
#include "Debug/DemoLog.h"
#include "PhysicsEngine/BodyInstance.h" // 同时验证动画后真实运动学刚体变换，不能只观察渲染骨。

namespace DemoHitZoneTest
{
    /** Enemy为World持有夹具；Action/Seconds指定同骨架序列与采样秒，更新真实物理体后固定姿势。 */
    USkeletalMeshComponent* Pose(ADemoEnemy* Enemy,const FString& Action,float Seconds)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] HitZoneTest::Pose %s %.3f"),*Action,Seconds);
        UDemoEnemyPresentation* View=Enemy->FindComponentByClass<UDemoEnemyPresentation>(); // 关闭自主朝向，不改生产类默认值。
        View->Cancel(); View->SetComponentTickEnabled(false); Enemy->SetActorTickEnabled(false);
        USkeletalMeshComponent* Mesh=View->GetMesh(); // 唯一真实受击网格，查询不创建替代碰撞体。
        UAnimSequence* Sequence=LoadObject<UAnimSequence>(nullptr,*Action); // 已导入源序列由网格播放实例保活。
        Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode); Mesh->PlayAnimation(Sequence,false);
        Mesh->SetPosition(Seconds,false); Mesh->TickAnimation(0.f,false); Mesh->RefreshBoneTransforms();
        Mesh->SetComponentTickEnabled(false);
        return Mesh;
    }
    /** Mesh为当前受击网格，Y/Z为模型空间厘米，OutHit接收实际世界PhysicsAsset最近命中。 */
    bool Trace(USkeletalMeshComponent* Mesh,float Y,float Z,FHitResult& OutHit)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] HitZoneTest::Trace"));
        const FTransform Transform=Mesh->GetComponentTransform(); // 本次查询快照，不缓存跨帧骨变换。
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoHitZoneFixture),false); // 查简单骨骼体，不查逐三角形/不伪造BoneName。
        return Mesh->GetWorld()->LineTraceSingleByChannel(OutHit,Transform.TransformPosition(FVector(500,Y,Z)),
            Transform.TransformPosition(FVector(-500,Y,Z)),DemoEnemyHitZones::TraceChannel,Query)&&OutHit.GetComponent()==Mesh;
    }
    /** Mesh/Profile为只读夹具，Wanted为明确区域；OutPoint为稳定表面点，周围2cm仍需命中相同区域。 */
    bool FindRegion(USkeletalMeshComponent* Mesh,const UDemoEnemyHitProfile* Profile,EDemoEnemyHitRegion Wanted,FVector& OutPoint)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] HitZoneTest::FindRegion %d"),static_cast<int32>(Wanted));
        for (int32 Z=-44;Z<=40;Z+=2) // 覆盖Chaser主体/臂/核心的真实厘米边界。
            for (int32 Y=-74;Y<=74;Y+=2) // 先扫描表面，再做邻域稳健性检查，避免枪口视差刚好落在缝隙。
            {
                FHitResult Hit; // 真实射线结果，未知骨不可按默认机身蒙混通过。
                EDemoEnemyHitRegion Region; float Multiplier=0.f; // 当前查询区域及配置倍率，无网络状态。
                if (!Trace(Mesh,Y,Z,Hit)||!Profile->ResolveHit(Hit,Region,Multiplier)||Region!=Wanted) continue;
                bool bStable=true; // 四邻域都需要相同区域，几何边界处继续寻找更内部探针。
                for (const FVector2D Delta:{FVector2D(2,0),FVector2D(-2,0),FVector2D(0,2),FVector2D(0,-2)})
                {
                    FHitResult Neighbor; // 仅用于本点可靠性，不参与最终武器伤害。
                    if (!Trace(Mesh,Y+Delta.X,Z+Delta.Y,Neighbor)||!Profile->ResolveHit(Neighbor,Region,Multiplier)||Region!=Wanted) { bStable=false; break; }
                }
                if (!bStable) continue;
                OutPoint=Mesh->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
                UE_LOG(LogFPSDemo,Log,TEXT("HIT_ZONE_PROBE bone=%s point=%s region=%d"),*Hit.BoneName.ToString(),*OutPoint.ToString(),static_cast<int32>(Wanted)); return true;
            }
        return false;
    }
    /** Enemy/Player为当前夹具，Point为已经查到的模型表面，Distance为相机前目标距离厘米。 */
    void Aim(ADemoEnemy* Enemy,ADemoCharacter* Player,const FVector& Point,float Distance)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] HitZoneTest::Aim %s"),*Point.ToString());
        USkeletalMeshComponent* Mesh=Enemy->FindComponentByClass<USkeletalMeshComponent>(); // 只旋转网格，保留球根朝向。
        Mesh->SetWorldRotation(FRotator(0,180,0));
        const FVector Camera=Player->GetFirstPersonCameraComponent()->GetComponentLocation(); // 玩家已固定yaw0，沿+X开火。
        Enemy->SetActorLocation(Camera+FVector(Distance,0,0)-Mesh->GetComponentTransform().TransformVector(Point));
        Mesh->RefreshBoneTransforms(); // 传送后让查询体在同帧到达最新位置。
    }
    /** Player为隔离试玩Pawn；Id只修改测试实例弹药状态，不伪造金币交易或写正式档。 */
    void Ammo(ADemoCharacter* Player,FName Id)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] HitZoneTest::Ammo %s"),*Id.ToString());
        UDemoRunSave* Data=NewObject<UDemoRunSave>(Player); // 短期夹具值对象，由当前Pawn拥有直到World结束。
        Data->UnlockedAmmoIds={TEXT("normal"),TEXT("fire"),TEXT("frost"),TEXT("piercing")}; Data->SelectedAmmoId=Id;
        Player->FindComponentByClass<UDemoAmmoComponent>()->Restore(*Data);
    }
    /** Player当前武器通过真实Fire GA执行一次；测试在各步骤之间等待冷却，不调用伤害入口伪造命中。 */
    void Fire(ADemoCharacter* Player)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] HitZoneTest::Fire"));
        Player->GetWeaponComponent()->StartFire(); Player->GetWeaponComponent()->StopFire();
    }
}

void ADemoEnemyAttackTest::TickHitZoneTest()
{
    DEMO_LOG_TICK(); if (bFailed||GetWorld()->GetTimeSeconds()<NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds()<60.f,TEXT("hit zones bounded runtime"))) return;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 单人权威规则实例。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 单帧重新获取，避免旧Pawn。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 当前真实Avatar。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前阶段，夹具只在本测试中显式设为Combat。
    const UDemoEnemyHitProfile* Profile=LoadObject<UDemoEnemyHitProfile>(nullptr,TEXT("/Game/Data/Combat/DA_EnemyHitZones.DA_EnemyHitZones")); // 检查真正保存资产，不只检查原生默认值。
    if (!Check(Mode&&Player&&State&&Profile,TEXT("hit zone dependencies and cooked profile"))) return;
    ADemoWeaponBase* Weapon=Player->GetWeaponComponent()->GetActiveWeapon(); // 已初始化的真实手枪。
    const UDemoAmmoCatalog* AmmoConfig=Player->FindComponentByClass<UDemoAmmoComponent>()->GetCatalog(); // DOT和穿透独立数据。
    USkeletalMeshComponent* Mesh=Shooter.IsValid()?Shooter->FindComponentByClass<USkeletalMeshComponent>():nullptr; // 本帧借用。
    switch (Step)
    {
    case 0:
        if (!Check(Mode->StartRun(),TEXT("hit zones isolated run"))) return;
        PC->OnRunReady(); State->Phase=EDemoPhase::Combat; State->LevelNumber=1;
        Player->SetActorLocation(FVector(0,0,3000)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Player->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation(FRotator::ZeroRotator);
        Weapon->Config.SpreadHalfAngle=0; Weapon->Config.RecoilPitch=0; Weapon->Config.BaseDamage=20; Weapon->Config.FalloffStart=4000; Weapon->Config.Range=5000; // 只改本次武器，明确定义20基础伤害。
        Weapon->Config.MuzzleSocket=NAME_None; // 使用生产相机偏移回退，摆脱手臂动作对小弱点的枪口抖动。
        Shooter=SpawnEnemy(FVector(1000,0,3000),false); Boss=SpawnEnemy(FVector(1000,2000,3000),true);
        for (ADemoEnemy* Enemy:{Shooter.Get(),Boss.Get()}) // 活体夹具保留原健康委托和GAS，增加血量避免中途死亡。
        {
            Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetMaxHealthAttribute(),5000);
            Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),5000);
            DemoHitZoneTest::Pose(Enemy,FString::Printf(TEXT("/Game/Enemies/Breach/QA/A_Breach_%s_RigCheck.A_Breach_%s_RigCheck"),Enemy->IsBoss()?TEXT("Warden"):TEXT("Chaser"),Enemy->IsBoss()?TEXT("Warden"):TEXT("Chaser")),0);
        }
        Advance(.2f); break;
    case 1:
    {
        if (!Check(Mesh&&Mesh->GetPhysicsAsset()&&Mesh->GetCollisionEnabled()==ECollisionEnabled::QueryOnly&&!Mesh->IsSimulatingPhysics(),TEXT("query-only animated hit asset"))) return;
        if (!Check(CastChecked<USphereComponent>(Shooter->GetRootComponent())->GetCollisionResponseToChannel(DemoEnemyHitZones::TraceChannel)==ECR_Ignore,TEXT("movement sphere ignores weapon trace"))) return;
        if (!Check(Profile->ValidateSkeleton(Mesh->GetSkeletalMeshAsset()),TEXT("all imported bones explicitly mapped"))) return;
        if (!Check(DemoHitZoneTest::FindRegion(Mesh,Profile,EDemoEnemyHitRegion::Body,HitZonePoints[0])
            &&DemoHitZoneTest::FindRegion(Mesh,Profile,EDemoEnemyHitRegion::Arm,HitZonePoints[1])
            &&DemoHitZoneTest::FindRegion(Mesh,Profile,EDemoEnemyHitRegion::Core,HitZonePoints[2]),TEXT("true physics traces find body arm and core"))) return;
        if (!Check(Mesh->GetBodyInstance(TEXT("forearm_l"))!=nullptr,TEXT("forearm physics body exists"))) return;
        const FTransform ArmBefore=Mesh->GetBodyInstance(TEXT("forearm_l"))->GetUnrealWorldTransform(); // 当前Chaos运动学刚体，非渲染骨的替代值。
        DemoHitZoneTest::Pose(Shooter.Get(),TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),1.f);
        const FTransform ArmAfter=Mesh->GetBodyInstance(TEXT("forearm_l"))->GetUnrealWorldTransform(); // 同一实际刚体在诊断动作展开后的变换。
        if (!Check(!ArmBefore.Equals(ArmAfter,.01f),TEXT("physics arm bodies follow skeletal animation"))) return;
        DemoHitZoneTest::Pose(Shooter.Get(),TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f);
        USkeletalMeshComponent* BossMesh=DemoHitZoneTest::Pose(Boss.Get(),TEXT("/Game/Enemies/Breach/Animation/Sequences/A_Breach_Warden_Global.A_Breach_Warden_Global"),0.f); // 同一射线验证两种护板姿势。
        FHitResult Closed; FHitResult Open; // 真世界射线，Y0 Z54是源模型偏心核心窗口，不使用看不见的遮挡盒。
        if (!Check(DemoHitZoneTest::Trace(BossMesh,0,54,Closed)&&Closed.BoneName==TEXT("shutter_top"),TEXT("closed core shutter blocks weapon trace"))) return;
        DemoHitZoneTest::Pose(Boss.Get(),TEXT("/Game/Enemies/Breach/Animation/Sequences/A_Breach_Warden_Global.A_Breach_Warden_Global"),2.f);
        if (!Check(DemoHitZoneTest::Trace(BossMesh,0,54,Open)&&Open.BoneName==TEXT("core"),TEXT("same ray hits core after animated shutter opens"))) return;
        DemoHitZoneTest::Ammo(Player,TEXT("normal")); DemoHitZoneTest::Aim(Shooter.Get(),Player,HitZonePoints[0],700); Advance(.2f); break;
    }
    case 2:
        if (!bHitZoneShotPending) HitZoneHealth=Shooter->GetHealth(); // 保存开火前快照，等待期间不能重复覆盖。
        if (!DemoProjectileFixtures::FireAndWait(Player,bHitZoneShotPending,HitZoneShotDeadline)) return;
        if (!Check(FMath::IsNearlyEqual(HitZoneHealth-Shooter->GetHealth(),20.f,.02f),TEXT("body real Fire GA damage 20 x 1"))) return;
        DemoHitZoneTest::Aim(Shooter.Get(),Player,HitZonePoints[1],700); Advance(.5f); break;
    case 3:
        if (!bHitZoneShotPending) HitZoneHealth=Shooter->GetHealth(); // 保存开火前快照，等待期间不能重复覆盖。
        if (!DemoProjectileFixtures::FireAndWait(Player,bHitZoneShotPending,HitZoneShotDeadline)) return;
        if (!Check(FMath::IsNearlyEqual(HitZoneHealth-Shooter->GetHealth(),13.f,.02f),TEXT("arm real Fire GA damage 20 x .65"))) return;
        DemoHitZoneTest::Aim(Shooter.Get(),Player,HitZonePoints[2],700); Advance(.5f); break;
    case 4:
        if (!bHitZoneShotPending) HitZoneHealth=Shooter->GetHealth(); // 保存开火前快照，等待期间不能重复覆盖。
        if (!DemoProjectileFixtures::FireAndWait(Player,bHitZoneShotPending,HitZoneShotDeadline)) return;
        if (!Check(FMath::IsNearlyEqual(HitZoneHealth-Shooter->GetHealth(),40.f,.02f),TEXT("core real Fire GA damage 20 x 2"))) return;
        DemoHitZoneTest::Ammo(Player,TEXT("fire")); Advance(.5f); break;
    case 5:
        if (!bHitZoneShotPending) HitZoneHealth=Shooter->GetHealth(); // 保存开火前快照，等待期间不能重复覆盖。
        if (!DemoProjectileFixtures::FireAndWait(Player,bHitZoneShotPending,HitZoneShotDeadline)) return;
        if (!Check(FMath::IsNearlyEqual(HitZoneHealth-Shooter->GetHealth(),40.f,.02f),TEXT("fire direct core damage retains regional bonus"))) return;
        HitZoneHealth=Shooter->GetHealth(); Advance(AmmoConfig->BurnPeriod+.08f); break;
    case 6:
        if (!Check(FMath::IsNearlyEqual(HitZoneHealth-Shooter->GetHealth(),AmmoConfig->BurnDamagePerStack,.02f),TEXT("core burn tick does not inherit x2"))) return;
        HitZoneHealth=Shooter->GetHealth();
        for (int32 Index=1;Index<AmmoConfig->BurnThreshold;++Index) Shooter->FindComponentByClass<UDemoAmmoStatus>()->Apply(Player->GetAbilitySystemComponent(),1,AmmoConfig); // 补齐真实GE阈值，不直接扣血。
        if (!Check(FMath::IsNearlyEqual(HitZoneHealth-Shooter->GetHealth(),AmmoConfig->ExplosionDamage,.02f),TEXT("core-triggered explosion uses its own damage"))) return;
        Advance(.5f); break;
    case 7:
    {
        if (!bHitZoneShotPending)
        {
            if (!Check(DemoProjectileFixtures::EquipPrimary(Player,1),TEXT("shotgun uses real terminal-equipped instance"))) return;
            ADemoWeaponBase* PreparedWeapon=Player->GetWeaponComponent()->GetActiveWeapon(); // 准备凭据必须对应装备组件真正的当前实例。
            PreparedWeapon->Config.SpreadHalfAngle=0; PreparedWeapon->Config.RecoilPitch=0; PreparedWeapon->Config.MuzzleSocket=NAME_None;
            PreparedWeapon->Config.FalloffStart=4000; PreparedWeapon->Config.Range=5000; // 排除700cm处默认距离衰减，只验证真实核心碰撞与每枪去重。
            Shooter->FindComponentByClass<UDemoAmmoStatus>()->Clear(); HitZoneHealth=Shooter->GetHealth();
        }
        ADemoWeaponBase* Shotgun=Player->GetWeaponComponent()->GetActiveWeapon(); // World库存借用，不再用未装备临时Actor绕过GAS。
        if (!DemoProjectileFixtures::FireAndWait(Player,bHitZoneShotPending,HitZoneShotDeadline)) return;
        if (!Check(FMath::IsNearlyEqual(HitZoneHealth-Shooter->GetHealth(),Shotgun->GetDamagePerPellet()*Shotgun->GetPelletCount()*2.f,.05f)
            &&Shooter->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Burn)==1,TEXT("shotgun core projectiles apply independent GAS hits and one burn stack"))) return;
        if (!Check(Player->GetWeaponComponent()->EquipSlot(2),TEXT("return to real pistol after shotgun"))) return;
        Shooter->FindComponentByClass<UDemoAmmoStatus>()->Clear();
        DemoHitZoneTest::Ammo(Player,TEXT("piercing"));
        // 第二目标使用另一Chaser，精确放到真实枪口射线延长线上；不借用首目标的核心倍率。
        Boss->Destroy(); Boss=SpawnEnemy(FVector(1000,2000,3000),false);
        Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetMaxHealthAttribute(),5000);
        Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),5000);
        DemoHitZoneTest::Pose(Boss.Get(),TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0);
        DemoHitZoneTest::Aim(Boss.Get(),Player,HitZonePoints[1],1000);
        Advance(.5f); break;
    }
    case 8:
    {
        // 首目标700cm，后目标1000cm；按枪口视差继续偏移，使后者手臂落在实际穿透线而不是相机中心线。
        const FVector Camera=Player->GetFirstPersonCameraComponent()->GetComponentLocation(); // 当次真实相机位置。
        const FVector Muzzle=Weapon->GetMuzzleLocation(); // 已配置稳定fallback，仍走生产接口。
        const FVector Direction=(Camera+FVector(700,0,0)-Muzzle).GetSafeNormal(); // 与零散布相机/枪口二段瞄准一致。
        if (!bHitZoneShotPending)
        {
            Boss->AddActorWorldOffset(FVector(0,Direction.Y/Direction.X*300,Direction.Z/Direction.X*300));
            HitZoneHealth=Shooter->GetHealth(); HitZoneRearHealth=Boss->GetHealth(); // 两目标伤前快照必须跨越真实飞行帧。
        }
        if (!DemoProjectileFixtures::FireAndWait(Player,bHitZoneShotPending,HitZoneShotDeadline)) return;
        if (!Check(FMath::IsNearlyEqual(HitZoneHealth-Shooter->GetHealth(),20*AmmoConfig->PiercingDamageMultiplier*2,.05f)
            &&FMath::IsNearlyEqual(HitZoneRearHealth-Boss->GetHealth(),20*AmmoConfig->PiercingDamageMultiplier*AmmoConfig->SecondaryDamageRatio*.65f,.05f),TEXT("piercing core then arm uses independent regional multipliers"))) return;
        AActor* Blocker=GetWorld()->SpawnActor<AActor>(); // 测试专有墙体，不修改关卡持久资产。
        UBoxComponent* Box=NewObject<UBoxComponent>(Blocker); // Actor持有碰撞组件，默认BlockAll验证墙体通道继承。
        Blocker->SetRootComponent(Box); Box->SetBoxExtent(FVector(10,150,150)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
        Blocker->SetActorLocation(Camera+FVector(850,0,0)); Wall=Blocker;
        Advance(.5f); break;
    }
    case 9:
        if (!bHitZoneShotPending) HitZoneHealth=Boss->GetHealth(); // 墙体一直存活到飞行完成。
        if (!DemoProjectileFixtures::FireAndWait(Player,bHitZoneShotPending,HitZoneShotDeadline)) return;
        if (!Check(Boss->GetHealth()==HitZoneHealth,TEXT("wall blocks piercing after first enemy"))) return;
        Wall->SetActorLocation(Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(350,0,0));
        Advance(.5f); break;
    case 10:
        if (!bHitZoneShotPending) HitZoneHealth=Shooter->GetHealth(); // 保存开火前快照，等待期间不能重复覆盖。
        if (!DemoProjectileFixtures::FireAndWait(Player,bHitZoneShotPending,HitZoneShotDeadline)) return;
        if (!Check(Shooter->GetHealth()==HitZoneHealth,TEXT("wall blocks actual projectile before skeletal target"))) return;
        Wall->Destroy(); Shooter->Destroy(); Boss->Destroy();
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_ENEMY_HIT_ZONE_SUCCESS")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
}
