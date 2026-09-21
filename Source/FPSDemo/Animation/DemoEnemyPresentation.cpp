#include "Animation/DemoEnemyPresentation.h"
#include "Animation/DemoEnemyAppearance.h"
#include "Animation/AnimMontage.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoCloseCombat.h" // 攻击锁定朝向和真实路线保持一致，不改根碰撞朝向。
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GAS/Abilities/DemoEnemyAnimationAbility.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"
#include "PhysicsEngine/PhysicsAsset.h" // 配置阶段检查实际受击资产，缺失时不退回宽松整球伤害。
#include "PhysicsEngine/BodySetup.h" // UE5.4的骨骼BodySetup定义随PhysicsAsset引入；共用实际凸体数据绘制调试线。
#include "Combat/DemoEnemyHitZones.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

// 默认关闭，仅本机绘制，不复制/写存档；1显示当前姿势真实凸体边线与区域名。
static TAutoConsoleVariable<int32> CVarDemoHitZones(TEXT("Demo.Combat.DebugHitZones"),0,TEXT("Draw animated weapon query hulls by hit region (0/1)."));

UDemoEnemyPresentation::UDemoEnemyPresentation()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=true;
    static ConstructorHelpers::FObjectFinder<UDemoEnemyAppearance> DroneAsset(TEXT("/Game/Enemies/Breach/Animation/DA_Chaser")); // CDO硬引用使打包递归收集。
    static ConstructorHelpers::FObjectFinder<UDemoEnemyAppearance> BossAsset(TEXT("/Game/Enemies/Breach/Animation/DA_Warden")); // 每骨架一套图和动作。
    Drone=DroneAsset.Object; Warden=BossAsset.Object;
    static ConstructorHelpers::FObjectFinder<UDemoEnemyHitProfile> Zones(TEXT("/Game/Data/Combat/DA_EnemyHitZones")); // 硬引用确保打包纳入配置。
    HitProfile=Zones.Object;
}
bool UDemoEnemyPresentation::Configure(bool bBoss,USkeletalMeshComponent* Mesh)
{
    DEMO_LOG_CALL(); Body=Mesh; Appearance=bBoss?Warden:Drone;
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 所有者与ASC生命周期一致。
    if (!Enemy||!Body||!Appearance||!Appearance->Mesh||!Appearance->AnimClass)
    { UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_ANIM_CONFIG missing cooked appearance boss=%d"),bBoss); return false; }
    Body->SetSkeletalMesh(Appearance->Mesh);
    if (!Body->GetPhysicsAsset()) UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_HIT_ASSET_MISSING mesh=%s; weapon damage disabled until asset repaired"),*GetNameSafe(Appearance->Mesh));
    const UDemoEnemyHitProfile* Rules=DemoEnemyHitZones::SelectValidProfile(HitProfile); // 与武器共享缺失/结构非法配置回退；未知骨架仍禁止受击。
    const bool bValidHitAsset=Body->GetPhysicsAsset()&&Rules&&Rules->ValidateSkeleton(Appearance->Mesh); // 每次配置重查，修复数据后重新Configure可以恢复查询。
    Body->SetCollisionEnabled(bValidHitAsset?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    if (!bValidHitAsset) UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_HIT_CONFIG rejects missing physics asset or unknown skeleton bones"));
    Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    Body->SetAnimInstanceClass(Appearance->AnimClass);
    Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Body->bEnableUpdateRateOptimizations=false; // 首版保证不可见/NullRHI下动作与Socket验证，后续按性能预算优化。
    Body->AddTickPrerequisiteActor(Enemy);
    AddTickPrerequisiteActor(Enemy);
    Enemy->GetAbilitySystemComponent()->RefreshAbilityActorInfo(); // Configure发生在BeginPlay之后，必须刷新原先空的AnimInstance。
    if (Enemy->HasAuthority()&&!AnimationAbility.IsValid())
        AnimationAbility=Enemy->GetAbilitySystemComponent()->GiveAbility(FGameplayAbilitySpec(UDemoEnemyAnimationAbility::StaticClass(),1));
    UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_ANIM_READY mesh=%s anim=%s"),*GetNameSafe(Appearance->Mesh),*GetNameSafe(Body->GetAnimInstance()));
    return Body->GetAnimInstance()!=nullptr;
}
bool UDemoEnemyPresentation::Play(FName Action,float Seconds)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 同步只借用，不把Actor保存在异步任务中。
    if (!Enemy||!Enemy->HasAuthority()||(!Enemy->IsAlive()&&Action!="Death")||!Appearance||!Body||!Body->GetAnimInstance()
        ||!AnimationAbility.IsValid()||!FMath::IsFinite(Seconds)||Seconds<0)
    { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_ANIM_REJECT action=%s dependency/duration"),*Action.ToString()); return false; }
    const TObjectPtr<UAnimMontage>* Found=Appearance->Actions.Find(Action); // 资产映射不热变更，缺失不影响权威伤害。
    if (!Found||!*Found) { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_ANIM_MISSING %s"),*Action.ToString()); return false; }
    const int32 Wanted=Action=="Death"?100:Action=="Hit"?10:Action=="Rage"?20:(Action=="Melee"||Action=="Fire")?40:60; // 优先级不改变玩法互斥。
    if (Body->GetAnimInstance()->IsAnyMontagePlaying()&&Wanted<Priority)
    {
        if (Action=="Rage") bRageQueued=true;
        UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_ANIM_DEFER %s priority=%d current=%d"),*Action.ToString(),Wanted,Priority); return false;
    }
    Enemy->GetAbilitySystemComponent()->CancelAbilityHandle(AnimationAbility);
    RequestedMontage=*Found; RequestedRate=Seconds>0?RequestedMontage->GetPlayLength()/Seconds:1.f; Priority=Wanted;
    if (Action=="Death") bRageQueued=false;
    const bool bStarted=Enemy->GetAbilitySystemComponent()->TryActivateAbility(AnimationAbility); // 同步激活后Task自行持有动画。
    UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_ANIM_PLAY %s rate=%.3f started=%d"),*Action.ToString(),RequestedRate,bStarted);
    return bStarted;
}
void UDemoEnemyPresentation::Cancel()
{
    DEMO_LOG_CALL();
    if (ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner())) // 同Actor ASC在EndPlay时尚有效。
        if (AnimationAbility.IsValid()) Enemy->GetAbilitySystemComponent()->CancelAbilityHandle(AnimationAbility);
    Priority=0; bRageQueued=false;
}
void UDemoEnemyPresentation::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    DrawHitZones(); // 守卫内部只在控制台显式开启时读取凸体数据。
    const ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 本帧目标，不从动画工作线程访问。
    if (!Enemy||!Enemy->IsAlive()||!Body||!Body->GetAnimInstance()) return;
    if (bRageQueued&&!Body->GetAnimInstance()->IsAnyMontagePlaying()) { bRageQueued=false; Play(TEXT("Rage")); }
    const APawn* Player=UGameplayStatics::GetPlayerPawn(this,0); // 当前单人目标；未来联机替换为权威目标快照。
    if (Player)
    {
        const UDemoCloseCombat* Close=Enemy->FindComponentByClass<UDemoCloseCombat>(); // Actor拥有的只读攻击朝向。
        const FVector Direction=Close&&Close->HasLockedFacing()?Close->GetLockedDirection():Player->GetActorLocation()-Enemy->GetActorLocation(); // 新前摇/冲刺保持锁向，Idle及旧角色仍看向玩家。
        const FRotator Facing(0,Direction.Rotation().Yaw,0); // 不将垂直瞄准倾斜到碰撞或HUD。
        Body->SetWorldRotation(FMath::RInterpTo(Body->GetComponentRotation(),Facing,DeltaTime,8.f));
    }
}
void UDemoEnemyPresentation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); Cancel(); Super::EndPlay(EndPlayReason);
}
UAnimMontage* UDemoEnemyPresentation::GetRequestedMontage() const { DEMO_LOG_TICK(); return RequestedMontage; }
float UDemoEnemyPresentation::GetRequestedRate() const { DEMO_LOG_TICK(); return RequestedRate; }
USkeletalMeshComponent* UDemoEnemyPresentation::GetMesh() const { DEMO_LOG_TICK(); return Body; }

void UDemoEnemyPresentation::DrawHitZones() const
{
    DEMO_LOG_TICK();
    if (CVarDemoHitZones.GetValueOnGameThread()==0||!Body||!Body->GetPhysicsAsset()) return;
    const UDemoEnemyHitProfile* Rules=DemoEnemyHitZones::SelectValidProfile(HitProfile); // 调试开启时与碰撞/伤害共用回退，不显示无效配置的倍率。
    if (!Rules) return;
    for (const USkeletalBodySetup* Setup:Body->GetPhysicsAsset()->SkeletalBodySetups) // 资产只读，绘制随动画求值后的骨变换。
    {
        if (!Setup) continue;
        EDemoEnemyHitRegion Region; float Multiplier=0.f; // 当前区域与倍率，用于诊断显示而非再次结算。
        if (!Rules->ResolveBone(Setup->BoneName,Region,Multiplier)) continue;
        const FColor Color=Region==EDemoEnemyHitRegion::Core?FColor::Red:Region==EDemoEnemyHitRegion::Arm?FColor::Cyan:FColor::Green; // 三类稳定调试色。
        const FTransform Bone=Body->GetSocketTransform(Setup->BoneName,RTS_World); // 当前动画姿势，不使用参考姿势画静止碰撞。
        for (const FKConvexElem& Hull:Setup->AggGeom.ConvexElems) // 零件可拆成多个凸片；环与凹护板的真实开口保持空隙。
        {
            const FTransform Transform=Hull.GetTransform()*Bone; // 凸体局部→骨→世界，厘米制。
            for (int32 Index=0;Index+2<Hull.IndexData.Num();Index+=3) // 复用烹饪后的凸体三角边索引，不猜测拓扑。
                for (int32 Edge=0;Edge<3;++Edge)
                {
                    const int32 A=Hull.IndexData[Index+Edge],B=Hull.IndexData[Index+(Edge+1)%3]; // 索引来自资产，调试也防御不完整数据。
                    if (Hull.VertexData.IsValidIndex(A)&&Hull.VertexData.IsValidIndex(B))
                        DrawDebugLine(GetWorld(),Transform.TransformPosition(Hull.VertexData[A]),Transform.TransformPosition(Hull.VertexData[B]),Color,false,0.f,0,.7f);
                }
        }
        DrawDebugString(GetWorld(),Bone.GetLocation(),FString::Printf(TEXT("%s %s x%.2f"),*Setup->BoneName.ToString(),DemoEnemyHitZones::RegionName(Region),Multiplier),nullptr,Color,0.f,true,.7f);
    }
}
