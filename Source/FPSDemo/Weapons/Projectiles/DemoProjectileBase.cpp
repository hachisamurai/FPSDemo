// 职责：实体弹飞行/碰撞与冻结弹种表现广播；只读出口查询、弹射边界及实例着色不改ShotContext伤害或服务器运动。 本人首段使用发射帧最终可见枪口。
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Engine/HitResult.h" // 实体接触值/目标DamageRequest需完整定义，非Editor单文件不借PCH补全。
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Projectiles/DemoProjectileMovementComponent.h"
#include "Weapons/Projectiles/DemoProjectileTracer.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Game/DemoGameState.h" // 阶段代数保护备战实体弹与跨Actor通道迟到的纯表现。
#include "Combat/DemoProjectileDamage.h"
#include "Combat/DemoDamageableTarget.h" // 设施使用同一真实弹体碰撞，但不进入敌人穿透/弹射/奖励路径。
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Components/SphereComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Debug/DemoLog.h"
#include "Net/UnrealNetwork.h"

#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
namespace
{
    // 只存在于Editor目标；按需显示已实际扫掠的路径/接触点，默认关闭，所有非Editor包不注册此调试命令。
    TAutoConsoleVariable<int32> CVarDemoProjectileTrajectories(TEXT("Demo.Debug.ProjectileTrajectories"), 0,
        TEXT("Editor only: draw actual player projectile sweep paths (0 off, 1 on)."), ECVF_Cheat);
}
#endif

ADemoProjectileBase::ADemoProjectileBase()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false; // 每帧校验与移动统一由Movement执行，避免两份Tick顺序依赖。
    bReplicates = false; // 准备阶段不建立网络Actor；成功提交激活后才复制位置与外观，碰撞永远只在服务器。
    InitialLifeSpan = 0.f; // 准备阶段不启动时限，扣费成功激活后才开始计时。
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("ProjectileCollision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(1.f);
    Collision->SetCollisionObjectType(ECC_GameTraceChannel3);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Collision->SetGenerateOverlapEvents(false);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileVisual"));
    CosmeticRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ProjectileCosmeticRoot"));
    CosmeticRoot->SetupAttachment(Collision);
    CosmeticRoot->SetIsReplicated(false); // Host本机渲染修正不能复制给世界观察者。
    Visual->SetupAttachment(CosmeticRoot);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetGenerateOverlapEvents(false);
    Visual->SetCastShadow(false);
    // 基础球体是引擎已有Cook资源；子弹BP可以覆盖，不写入任何自造uasset。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere")); // CDO硬引用默认外观，不在逐枪路径加载。
    Config.Mesh = Sphere.Object;
    Visual->SetStaticMesh(Config.Mesh);
    Visual->SetRelativeTransform(Config.VisualTransform);
    Movement = CreateDefaultSubobject<UDemoProjectileMovementComponent>(TEXT("ProjectileMovement"));
    Movement->SetUpdatedComponent(Collision);
    Movement->SetAutoActivate(false);
    Movement->bAutoUpdateTickRegistration = true;
    Movement->ProjectileGravityScale = 0.f;
    Movement->bInitialVelocityInLocalSpace = false; // Prepare给世界方向，FinishSpawning不能再按Actor旋转一次。
    Movement->bRotationFollowsVelocity = true;
    Movement->bShouldBounce = false;
    Movement->bSweepCollision = true;
    Movement->bForceSubStepping = true;
    Movement->MaxSimulationTimeStep = 1.f / 120.f;
    Movement->MaxSimulationIterations = 8;
    Movement->BounceAdditionalIterations = 2; // 两个合法穿透接触可消化本帧剩余时间。
    Movement->Velocity = FVector::ZeroVector;
    Movement->InitialSpeed = 0.f;
    SetActorHiddenInGame(true);
}

void ADemoProjectileBase::OnConstruction(const FTransform& Transform)
{
    DEMO_LOG_CALL();
    Super::OnConstruction(Transform);
    ApplyConfiguration();
}

void ADemoProjectileBase::ApplyConfiguration()
{
    DEMO_LOG_CALL();
    SetActorScale3D(FVector::OneVector); // BP外观缩放仅来自VisualTransform，根球保持精确世界厘米半径。
    Collision->SetSphereRadius(FMath::IsFinite(Config.CollisionRadius) ? FMath::Clamp(Config.CollisionRadius, .1f, 20.f) : 1.f);
    // 构造脚本完成后恢复系统碰撞协议；派生BP只配置外观/尺寸，不得悄悄改为根球或Overlap伤害。
    Collision->SetSimulatePhysics(false);
    Collision->SetCollisionObjectType(ECC_GameTraceChannel3);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    Collision->SetGenerateOverlapEvents(false);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetStaticMesh(Config.Mesh);
    Visual->SetSimulatePhysics(false);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CosmeticRoot->SetAbsolute(false,false,false); CosmeticRoot->SetRelativeTransform(FTransform::Identity);
    if (Visual->GetAttachParent()!=CosmeticRoot) Visual->AttachToComponent(CosmeticRoot,FAttachmentTransformRules::KeepRelativeTransform); // 旧BP父组件序列化仍强制收敛到新非复制显示层。
    Visual->SetRelativeTransform(Config.VisualTransform.ContainsNaN() ? FTransform::Identity : Config.VisualTransform);
    for (int32 Index = 0; Index < Config.MaterialOverrides.Num(); ++Index) // 索引对应外观材质槽，不改变碰撞形状。
    {
        if (Config.MaterialOverrides[Index]) Visual->SetMaterial(Index, Config.MaterialOverrides[Index]);
    }
    Movement->Velocity = FVector::ZeroVector;
    Movement->Deactivate();
    Movement->SetComponentTickEnabled(false);
}

bool ADemoProjectileBase::Prepare(UDemoShotContext* Context, int32 PelletIndex, const FVector& Direction)
{
    DEMO_LOG_CALL();
    FString Error; // 子弹CDO配置校验原因；拒绝时调用方销毁整组准备弹。
    if (!HasAuthority() || bConsumed || bPrepared || bFlying || !IsValid(Context) || !Context->IsFlightValid()
        || !Config.Validate(Error) || PelletIndex < 0 || PelletIndex > 63 || Direction.ContainsNaN() || Direction.IsNearlyZero() // 双弹允许32基础散弹×2，仍有界。
        || !Context->GetSourcePawn() || Context->GetSourcePawn()->GetWorld() != GetWorld())
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_PREPARE_REJECT actor=%s pellet=%d reason=%s"), *GetName(), PelletIndex, *Error);
        return false;
    }
    // FinishSpawning期间蓝图可能覆盖实例速度/寿命；必须再次验证，不能仅信任开火前的CDO检查。
    if (Config.MaxLifeSeconds * Config.InitialSpeed < Context->GetRange())
    {
        const ADemoProjectileBase* Defaults=GetClass()->GetDefaultObject<ADemoProjectileBase>(); // 只读构造后实例与所属类默认值，区分作者覆盖和临时夹具配置，不改变拒绝门。
        UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_PREPARE_REJECT actor=%s configured lifetime cannot cover weapon range instanceSpeed=%.6f instanceLife=%.6f coverage=%.6f range=%.6f cdoSpeed=%.6f cdoLife=%.6f"),
            *GetName(),Config.InitialSpeed,Config.MaxLifeSeconds,Config.MaxLifeSeconds*Config.InitialSpeed,Context->GetRange(),
            Defaults?Defaults->Config.InitialSpeed:-1.f,Defaults?Defaults->Config.MaxLifeSeconds:-1.f);
        return false;
    }
    ApplyConfiguration(); // 蓝图构造已完成，强制准备态关闭运动，不能在Commit前撞敌人。
    ShotContext = Context;
    ShotContext->RegisterProjectile(); bContextRegistered=true; // 全组Prepare完再Commit，首弹同步销毁也不提前结算整枪。
    LaunchPhaseRevision = Context->GetPhaseRevision();
    Launch.PelletIndex = PelletIndex;
    Launch.Origin = GetActorLocation();
    Launch.Direction = Direction.GetSafeNormal();
    PresentationLaunch.ShotId=Context->GetShotId();
    PresentationLaunch.Pellet=PelletIndex;
    PresentationLaunch.Shooter=Context->GetSourcePawn();
    PresentationLaunch.AmmoType=Context->GetAmmoType(); // 发射时冻结，切装配不追改已发弹。
    VisualStyle=DemoAmmoVisuals::Resolve(PresentationLaunch.Shooter,PresentationLaunch.AmmoType);
    bVisualStyleResolved=true;
    if (GetNetMode()!=NM_DedicatedServer) DemoAmmoVisuals::ApplyMesh(Visual,VisualStyle,Config.TracerMaterial);
    PresentationLaunch.Origin=Launch.Origin;
    PresentationLaunch.Direction=Launch.Direction;
    PresentationLaunch.Phase=Context->GetPhaseRevision();
    PresentationLaunch.Level=Context->GetLevelNumber();
    PresentationLaunch.bScoped=Context->GetSourcePawn()->GetWeaponComponent()->GetScopeLevel()>0;
    LastTravelLocation = Launch.Origin;
    LastNetworkPosition = Launch.Origin;
    LastNetworkTime = GetWorld()->GetTimeSeconds();
    TravelledDistance = 0.f;
    LastNetworkDistance=0.f; NetworkSequence=0;
    LastCosmeticPosition=Launch.Origin; CosmeticTravelDistance=0.f; CosmeticRenderScale=1.f;
    Collision->IgnoreActorWhenMoving(Context->GetSourcePawn(), true);
    if (GetOwner()) Collision->IgnoreActorWhenMoving(GetOwner(), true); // Owner为发射武器，枪Mesh即使配置错误也不自伤。
    SetActorRotation(Launch.Direction.Rotation());
    SetActorHiddenInGame(true);
    SetLifeSpan(0.f);
    bPrepared = true;
    if (UDemoCombatSession* Session = Context->GetCombatSession()) // 只注册本枪冻结的战斗凭据；备战弹不占用下一关预创建会话。
        if (!Session->RegisterCleanup(this, FSimpleDelegate::CreateUObject(this, &ADemoProjectileBase::CancelProjectile))) return false;
    return true;
}

bool ADemoProjectileBase::ActivateProjectile()
{
    DEMO_LOG_CALL();
    if (!IsPrepared() || !ShotContext->IsFlightValid())
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_ACTIVATE_REJECT actor=%s stale prepared context"), *GetName());
        CancelProjectile();
        return false;
    }
    bFlying = true; // 必须先转状态，再开放可能触发同步回调的碰撞/表现。
    PresentationLaunch.ServerTime=GetWorld()->GetTimeSeconds(); // Commit成功后一次冻结，不使用构造/准备耗时。
    SetReplicates(true); SetReplicateMovement(true); // 长飞行弹显示用户配置Mesh，短命弹另用真实段RPC保障可读性。
    NetUpdateFrequency = 60.f; MinNetUpdateFrequency = 20.f; // 只影响外观采样，不修改权威扫掠步长或飞行速度。
    ForceNetUpdate();
    bPrepared = false;
    SetActorHiddenInGame(false);
    // Actor显隐不会清除组件自身的Hidden/Owner限制；激活时显式恢复世界子弹外观，不改用户Mesh或缩放。
    Visual->SetHiddenInGame(false);
    Visual->SetVisibility(true);
    Visual->SetOwnerNoSee(false);
    Visual->SetOnlyOwnerSee(false);
    SetActorEnableCollision(true); // 不继承蓝图构造期间的临时Actor禁碰撞状态。
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Movement->SetUpdatedComponent(Collision);
    Movement->InitialSpeed = Config.InitialSpeed;
    Movement->MaxSpeed = Config.InitialSpeed;
    Movement->Velocity = Launch.Direction * Config.InitialSpeed;
    Movement->Activate(true);
    Movement->SetComponentTickEnabled(true);
    SetLifeSpan(Config.MaxLifeSeconds);
    LastCosmeticPosition=UpdateCosmeticPosition(0.f); // Host/Standalone也从确认的本机F0开始显示，无第二颗伤害弹。
    if (ADemoWeaponBase* Weapon=Cast<ADemoWeaponBase>(GetOwner())) Weapon->RegisterProjectilePresentation(this,PresentationLaunch);
    if (Config.TrailEffect)
    {
        Trail = UNiagaraFunctionLibrary::SpawnSystemAttached(Config.TrailEffect, CosmeticRoot, NAME_None, FVector::ZeroVector,
            FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
        if (Trail) { Trail->SetAbsolute(false, false, true); Trail->SetWorldScale3D(FVector::OneVector); } // 避免随CosmeticRoot动态缩放导致Niagara拖尾拉扯变形。
    }
    CreateLocalTracer(LastCosmeticPosition);
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_LAUNCHED shot=%s pellet=%d level=%d speed=%.1f"),
        *ShotContext->GetShotId().ToString(), Launch.PelletIndex, ShotContext->GetLevelNumber(), Config.InitialSpeed);
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_VISUAL mesh=%s scale=%s actorHidden=%d visible=%d hiddenInGame=%d onlyOwner=%d ownerNoSee=%d tracer=%s"),
        *GetNameSafe(Visual->GetStaticMesh()), *Visual->GetRelativeScale3D().ToString(), IsHidden(), Visual->GetVisibleFlag(),
        Visual->bHiddenInGame, Visual->bOnlyOwnerSee, Visual->bOwnerNoSee, *GetNameSafe(Tracer));
    OnProjectileLaunched(); // 蓝图仅表现；即使主动销毁，发射已发生，不能按失败退款。
    return true;
}

void ADemoProjectileBase::CancelProjectile()
{
    DEMO_LOG_CALL();
    if (bConsumed) return;
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_CANCEL shot=%s pellet=%d distance=%.2f flying=%d"),
        ShotContext ? *ShotContext->GetShotId().ToString() : TEXT("unprepared"), Launch.PelletIndex, TravelledDistance, bFlying);
    if (IsValid(ShotContext) && ShotContext->IsFlightValid()) PublishNetworkTravel(true,true); // 零长终态也发布，阻止晚到复制Actor重显。
    bConsumed = true; // 先闩锁，Destroy/EndPlay和Flow同步取消都不能二次进入。
    if(bContextRegistered&&ShotContext) { bContextRegistered=false; ShotContext->ResolveProjectile(); }
    bFlying = false;
    bPrepared = false;
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Movement->StopMovementImmediately();
    Movement->Deactivate();
    Movement->SetComponentTickEnabled(false);
    SetLifeSpan(0.f);
    if (Trail) { Trail->DeactivateImmediate(); Trail->DestroyComponent(); Trail = nullptr; }
    if (IsValid(Tracer))
    {
        // 正常命中/距离结束只保留纯表现；世界或Avatar失效立即删除，不能把战斗效果带进安全区。
        if (IsValid(ShotContext) && ShotContext->IsFlightValid()) Tracer->Finish();
        else Tracer->Destroy();
        Tracer = nullptr;
    }
    Destroy();
}

void ADemoProjectileBase::CancelAll(UWorld* World)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs world=%s"), __FUNCTION__, *GetNameSafe(World));
    if (!World) return;
    for (TActorIterator<ADemoProjectileBase> It(World); It; ++It) // World拥有Actor；取消幂等，允许清关在伤害调用栈中执行。
    {
        It->CancelProjectile();
    }
    ADemoProjectileTracer::CancelAll(World); // 同时收回已经脱离原弹的淡出Actor，不等0.06秒尾迹。
    for (TActorIterator<ADemoWeaponBase> It(World);It;++It) It->ClearProjectilePresentation(); // 边沿清理，本机F0不可跨阶段复用。
}

void ADemoProjectileBase::LifeSpanExpired()
{
    DEMO_LOG_CALL();
    CancelProjectile();
}
void ADemoProjectileBase::ReconcilePhase(UWorld* World)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs world=%s"), __FUNCTION__, *GetNameSafe(World));
    const ADemoGameState* State = World ? World->GetGameState<ADemoGameState>() : nullptr; // 只处理客户端自己的已接收阶段，不向服务器发取消意图。
    if (!State || World->GetNetMode() != NM_Client) return;
    ADemoProjectileTracer::CancelAll(World); // 短亮条只来自revision相符的RPC，阶段切换统一清理，不保活历史路径。
    for (TActorIterator<ADemoWeaponBase> It(World);It;++It) It->ClearProjectilePresentation();
    for (TActorIterator<ADemoProjectileBase> It(World); It; ++It) // World拥有复制弹Actor，仅在阶段通知遍历，不逐帧扫描。
    {
        // 有符号差值按uint32回绕识别旧代数；实际一次飞行不可能横跨2^31次阶段变化。0是尚未收到完整初始复制。
        const int32 RevisionDelta = static_cast<int32>(It->LaunchPhaseRevision - State->PhaseRevision);
        if (It->LaunchPhaseRevision != 0 && State->PhaseRevision != 0 && RevisionDelta < 0) It->CancelProjectile();
        else It->OnRep_Flying(); // 新阶段实体先到时暂时隐藏；GS随后追上即可重新显示，旧伤害仍从未在客户端运行。
    }
}

void ADemoProjectileBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if(bContextRegistered&&ShotContext) { bContextRegistered=false; ShotContext->ResolveProjectile(); } // 外部销毁/旅行也收敛整枪；会话失效不结算。
    bConsumed = true;
    bFlying = false;
    bPrepared = false;
    Movement->Deactivate();
    if (Trail) { Trail->DeactivateImmediate(); Trail->DestroyComponent(); Trail = nullptr; }
    // 正常Cancel已移交淡出并清空引用；未经过Cancel的外部Destroy/旅行必须立即撤销仍在更新的亮条。
    if (IsValid(Tracer) && !Tracer->IsFinishing()) Tracer->Destroy();
    Tracer = nullptr;
    // 不在这里清空ShotContext：当前同步GE调用栈可能仍在使用它，Actor回收时自然释放强引用。
    Super::EndPlay(EndPlayReason);
}

bool ADemoProjectileBase::IsPrepared() const { DEMO_LOG_TICK(); return bPrepared && !bFlying && !bConsumed && IsValid(ShotContext); }
bool ADemoProjectileBase::CanSimulate() const
{
    DEMO_LOG_TICK();
    return HasAuthority() && bFlying && !bConsumed && !bResolvingImpact && IsValid(ShotContext) && ShotContext->IsFlightValid();
}
float ADemoProjectileBase::GetRemainingDistance() const
{
    DEMO_LOG_TICK();
    return ShotContext ? FMath::Max(0.f, ShotContext->GetRange() - TravelledDistance) : 0.f;
}
void ADemoProjectileBase::RecordTravel(const FVector& Position)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (CVarDemoProjectileTrajectories.GetValueOnGameThread() > 0)
        DrawDebugLine(GetWorld(), LastTravelLocation, Position, FColor::Cyan, false, 1.f, 0, .75f);
#endif
    const bool bLaunchStep=TravelledDistance==0.f && !Position.Equals(LastTravelLocation,UE_SMALL_NUMBER); // 只消费第一个实际非零扫掠。
    const FVector PreviousPhysical=LastTravelLocation; // 区分只在Owner近枪段存在的曲线，不改变物理累计。
    TravelledDistance += FVector::Distance(LastTravelLocation, Position);
    LastTravelLocation = Position;
    UpdatePiercingExit(Position); // 只查询已实际飞过的后方段；从不调用伤害或改变运动输入。
    const FVector Presented=UpdateCosmeticPosition(TravelledDistance); // 同一映射同时带Mesh/Trail/亮条；实际路程仍为原值。
    CosmeticTravelDistance+=FVector::Distance(LastCosmeticPosition,Presented);
    const bool bPreviousOffset=!LastCosmeticPosition.Equals(PreviousPhysical,.01f); // 端点是否曾处于同一FP桥。
    const bool bCurrentOffset=!Presented.Equals(Position,.01f);
    if (!bConsumed && IsValid(Tracer))
    {
        ADemoWeaponBase* LaunchWeapon=bLaunchStep && bPreviousOffset && !bRicochetUsed ? Cast<ADemoWeaponBase>(GetOwner()) : nullptr; // 仅首个非零步借用。
        // 2026-11-19 OWNER-FOLLOW：CS2式（枪管不旋转）下RegisterLaunchTracer返回false，首段不再单独Finish驻留在世界旧位置；
        // 同一条亮条随后续帧移动，只在终点淡出，避免移动射击时“火光残留上一帧位置”。旧贴枪管模式保持原分支。
        if (LaunchWeapon && LaunchWeapon->RegisterLaunchTracer(PresentationLaunch,Tracer,TravelledDistance))
        {
            // 首段有自己的实例，后续子步/提前命中不能在首帧渲染前覆盖它。
            const bool bAttached=true;
            Tracer->Finish(); Tracer=nullptr;
            // 出膛短闪与离枪后的已飞段各有自己的空间。2026-11-19 OWNER-CONVERGE：尾段沿本人视觉收敛线（F0→准星目标）接续，
            // 只让出贴枪管的约60cm短闪；旧版从物理（视线中心）路径取尾起点，会把亮条画回准星附近。
            const FVector TailStart=bAttached && bCurrentOffset
                ? LastCosmeticPosition+(Presented-LastCosmeticPosition).GetClampedToMaxSize(60.f) : Presented;
            CosmeticTravelDistance=FVector::Distance(TailStart,Presented);
            CreateLocalTracer(TailStart);
            if (IsValid(Tracer) && CosmeticTravelDistance>0.f) Tracer->UpdateTravel(Presented,CosmeticTravelDistance);
        }
        else if (bPreviousOffset && !bCurrentOffset)
        {
            Tracer->UpdateSegment(LastCosmeticPosition,Presented);
            Tracer->Finish(); Tracer=nullptr; CosmeticTravelDistance=0.f;
            CreateLocalTracer(Presented);
        }
        else if (bPreviousOffset || bCurrentOffset) Tracer->UpdateSegment(LastCosmeticPosition,Presented);
        else Tracer->UpdateTravel(Presented,CosmeticTravelDistance);
    }
    LastCosmeticPosition=Presented;
    if (!bConsumed) PublishNetworkTravel(false); // 独立频率门控只限流表现，不改变真实碰撞Tick。
}
UDemoShotContext* ADemoProjectileBase::GetShotContext() const { DEMO_LOG_TICK(); return ShotContext; }
int32 ADemoProjectileBase::GetPelletIndex() const { DEMO_LOG_TICK(); return Launch.PelletIndex; }

bool ADemoProjectileBase::ResolveImpact(const FHitResult& Hit)
{
    DEMO_LOG_CALL();
    if (!CanSimulate() || !Hit.bBlockingHit || TravelledDistance > ShotContext->GetRange() + .01f)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_IMPACT_REJECT actor=%s invalid phase/range/hit"), *GetName());
        CancelProjectile();
        return false;
    }
    UpdateCosmeticPosition(TravelledDistance,true); // 真实接触/可能折向时结束近枪桥，不跨拐角拖出假路径。
    PublishNetworkTravel(true); // 在GE可能同步清场前发送真实接触终点，不等待该短命Actor建立网络通道。
    ADemoEnemy* Enemy = Cast<ADemoEnemy>(Hit.GetActor()); // 真实当前阻挡者；墙体及其他Actor不能产生敌人伤害。
    if (!IsValid(Enemy))
    {
        bEnvironmentImpact=true; EnvironmentPoint=Hit.ImpactPoint;
        EnvironmentNormal=Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
        PublishNetworkTravel(true); // 接触事实与终包均携带，首接触包丢失可补显；绝不代表伤害确认。
        ADemoProjectileTracer::SpawnContact(GetWorld(),Config,VisualStyle,EnvironmentPoint,EnvironmentNormal,false);
    }
#if WITH_EDITOR
    if (CVarDemoProjectileTrajectories.GetValueOnGameThread() > 0)
        DrawDebugSphere(GetWorld(), Hit.ImpactPoint, Config.CollisionRadius, 8, Enemy ? FColor::Green : FColor::Red, false, 1.f);
#endif
    if (!ShotContext->IsAttackValid())
    {
        // 备战弹仍由真实扫掠阻挡，碰撞到敌人/设施也不能进入伤害、元素、返弹、弹射、奖励或肉体受伤反馈。
        bFlying = false;
        if (!IsValid(Enemy)) PlayWallImpact(Hit);
        OnProjectileImpact(Hit, false); // 仅环境表现，BP不得把该回调作为施伤许可。
        CancelProjectile();
        return false;
    }
    if (!IsValid(Enemy) && Cast<IDemoDamageableTarget>(Hit.GetActor())) // 设施不是Enemy，仍必须在真实飞行碰撞到达后结算。
    {
        bFlying = false; bResolvingImpact = true; // 先消费弹体资格，GE/目标完成同步重入无法二次打击。
        TStrongObjectPtr<UDemoShotContext> ContextGuard(ShotContext); // 设施致死可触发候选/清场，保活本枪上下文至本栈结束。
        const float Damage = AcceptedEnemyCount == 0 ? ContextGuard->GetFirstHitBaseDamage(Hit.ImpactPoint)
            : FirstHitBaseDamage * (bRicochetUsed ? ContextGuard->GetRicochetRatio() : ContextGuard->GetAmmoSnapshot().SecondaryDamageRatio); // 穿过敌人后遇到设施保留已经冻结的衰减，不能重新当首敌满伤。
        const bool bAccepted = DemoProjectileDamage::Apply(ContextGuard.Get(), Hit, Damage, Launch.PelletIndex, bRicochetUsed); // 设施损血自行提交权威金属池与拥有者HUD，不能叠播墙体声音。
        bResolvingImpact = false;
        if (!IsValid(this) || bConsumed) return false;
        if (!bAccepted) PlayWallImpact(Hit); // 未授权/免伤目标仍可有普通环境接触，不能伪造伤害确认。
        OnProjectileImpact(Hit, false); // 设施不冒充敌人；已接受损血反馈由目标统一处理。
        CancelProjectile();
        return false; // 设施截停该弹，不参与敌人穿透数量或二次弹射索敌。
    }
    if (!IsValid(Enemy))
    {
        bFlying = false; // 表现事件之前关闭伤害资格，禁止表现回调重入。
        PlayWallImpact(Hit);
        OnProjectileImpact(Hit, false);
        CancelProjectile();
        return false;
    }
    if (HitActors.Contains(Enemy))
    {
        Collision->IgnoreActorWhenMoving(Enemy, true); // 理论上已忽略，仍防同敌多个骨骼刚体在同一子步重复返回。
        return true;
    }
    HitActors.Add(Enemy); // 必须先登记再提交GE，死亡/清关可能同步重入取消流程。
    bResolvingImpact = true;
    const bool bPiercing = ShotContext->GetAmmoType() == 3; // 本枪冻结的穿透类型。
    const bool bCanRicochet=ShotContext->GetRicochetRatio()>0.f&&!bRicochetUsed&&AcceptedEnemyCount==0; // 首敌专属；穿透二段不能再连锁。
    if (!bPiercing&&!bCanRicochet) bFlying = false; // bResolvingImpact同时阻止同步GE重入运动。
    const float HealthBefore=Enemy->GetHealth(); // 几何接受但免伤不能触发弹射。
    if (AcceptedEnemyCount == 0) FirstHitBaseDamage = ShotContext->GetFirstHitBaseDamage(Hit.ImpactPoint);
    const float BaseDamage = AcceptedEnemyCount == 0 ? FirstHitBaseDamage
        : FirstHitBaseDamage * (bRicochetUsed?ShotContext->GetRicochetRatio():ShotContext->GetAmmoSnapshot().SecondaryDamageRatio); // 弹射二段与穿透二段互斥，各自独立计算命中部位。
    TStrongObjectPtr<UDemoShotContext> ContextGuard(ShotContext); // 同步GE可能Destroy本弹；跨同步回调保活上下文，离开此栈即释放。
    ContextGuard->BeginImpactResolution(); // 在真实HP回执完成前，外部Destroy不能把本枪误判为落空。
    const bool bAccepted = DemoProjectileDamage::Apply(ContextGuard.Get(), Hit, BaseDamage, Launch.PelletIndex, bRicochetUsed); // 折向为派生，穿透仍直击；核心不重复触发派生计数。
    ContextGuard->EndImpactResolution();
    bResolvingImpact = false;
    if (!IsValid(this) || bConsumed) return false; // 最后一敌死亡可同步清关/清弹。
    if (!ContextGuard->IsAttackValid()) { CancelProjectile(); return false; } // Avatar/阶段直接变化也立即结束，不等待下一帧。
    if (bAccepted) ++AcceptedEnemyCount;
    OnProjectileImpact(Hit, true);
    if (!IsValid(this) || bConsumed) return false; // 表现事件也可能主动销毁，不继续访问组件。
    if(bAccepted&&bCanRicochet&&IsValid(Enemy)&&Enemy->GetHealth()<HealthBefore&&TryRicochet(Hit)) return true; // 只转向，不在搜敌射线上提前扣血。
    if (bAccepted && bPiercing && !bRicochetUsed && AcceptedEnemyCount < 2 && ContextGuard->IsAttackValid())
    {
        Collision->IgnoreActorWhenMoving(Enemy, true); // 忽略整只敌人，不跳移球心，不射线寻找后敌。
        PiercingComponent=Hit.GetComponent(); PiercingEntry=Hit.ImpactPoint;
        PiercingDirection=Movement->Velocity.GetSafeNormal(); // 借用首敌真实物理组件供后续只读出口特效，不影响忽略集合。
        return true;
    }
    CancelProjectile();
    return false;
}

void ADemoProjectileBase::PlayWallImpact(const FHitResult& Hit)
{
    DEMO_LOG_CALL();
    if (Config.WallImpactEffect)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Config.WallImpactEffect, Hit.ImpactPoint, Hit.ImpactNormal.Rotation());
    if (Config.WallImpactSound) UGameplayStatics::PlaySoundAtLocation(this, Config.WallImpactSound, Hit.ImpactPoint);
}

void ADemoProjectileBase::PublishNetworkTravel(bool bForce,bool bTerminal)
{
    DEMO_LOG_TICK();
    if (!HasAuthority() || bConsumed || (!bFlying && NetworkSequence==0) || GetNetMode() == NM_Standalone || !ShotContext || !ShotContext->IsFlightValid()
        || (!bForce && LastNetworkPosition.Equals(LastTravelLocation,.01f))) return;
    const float Now = GetWorld()->GetTimeSeconds(); // 表现发送使用World时钟，暂停不继续产生消息。
    if (!bForce && Now - LastNetworkTime < .04f) return;
    ADemoWeaponBase* Weapon = Cast<ADemoWeaponBase>(GetOwner()); // 只借用来源Actor通道；切枪仍保留库存，死亡销毁后不再显示旧弹迹。
    if (IsValid(Weapon))
    {
        FDemoProjectilePresentationSample Sample; // 全部来自权威已发生运动，不含准星目标/预测接触。
        Sample.Launch=PresentationLaunch; Sample.Sequence=++NetworkSequence;
        Sample.Start=LastNetworkPosition; Sample.End=LastTravelLocation;
        Sample.StartDistance=LastNetworkDistance; Sample.EndDistance=TravelledDistance;
        Sample.ServerTime=Now; Sample.bFirst=!bPublishedNetworkTravel;
        Sample.bBoundary=bForce; Sample.bTerminal=bTerminal;
        Sample.bRicochetSegment=bRicochetUsed; Sample.RicochetPoint=RicochetPoint; Sample.RicochetDirection=RicochetDirection;
        Sample.bEnvironmentImpact=bEnvironmentImpact; Sample.EnvironmentPoint=EnvironmentPoint; Sample.EnvironmentNormal=EnvironmentNormal;
        Sample.bPiercingExit=bPiercingExit; Sample.PiercingExitPoint=PiercingExitPoint; Sample.PiercingExitDirection=PiercingDirection;
        Weapon->BroadcastProjectileTravel(this,Sample);
        bPublishedNetworkTravel = true; // 记录已发送而非已收到；不可靠首包丢失只缺表现，不把后续真实段重新拉回枪口。
    }
    LastNetworkPosition = LastTravelLocation;
    LastNetworkDistance=TravelledDistance;
    LastNetworkTime = Now;
}

void ADemoProjectileBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL();
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADemoProjectileBase, bFlying);
    DOREPLIFETIME(ADemoProjectileBase, LaunchPhaseRevision);
    DOREPLIFETIME_CONDITION(ADemoProjectileBase, PresentationLaunch, COND_InitialOnly);
}
void ADemoProjectileBase::OnRep_Flying()
{
    DEMO_LOG_CALL();
    if (HasAuthority()) return;
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // GS与子弹Actor通道可乱序；不匹配时隐藏等待，ReconcilePhase在GS变化后重试。
    const bool bVisibleFlight = bFlying && State && State->PhaseRevision == LaunchPhaseRevision
        && State->PhaseRevision==PresentationLaunch.Phase && State->LevelNumber==PresentationLaunch.Level
        && PresentationLaunch.ShotId.IsValid() && PresentationLaunch.Shooter && Cast<ADemoWeaponBase>(GetOwner())
        && !CastChecked<ADemoWeaponBase>(GetOwner())->IsProjectilePresentationTerminal(PresentationLaunch)
        && (State->Phase == EDemoPhase::Combat || State->Phase == EDemoPhase::Intermission); // 完整身份前不先闪TP再跳FP；绝不激活客户端伤害。
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetActorEnableCollision(false);
    Movement->Deactivate();
    Movement->SetComponentTickEnabled(false); // 客户端不启动本地扫掠或ResolveImpact，更不预测GAS伤害。
    SetActorHiddenInGame(!bVisibleFlight);
    Visual->SetHiddenInGame(!bVisibleFlight);
    Visual->SetVisibility(bVisibleFlight);
    Visual->SetOwnerNoSee(false); Visual->SetOnlyOwnerSee(false);
    if (bVisibleFlight)
    {
        // 客户端没有运动累计器；首段直线用实际复制根的轴向飞长，边界/单调终态仍由真实段消息收紧。
        const float Distance=FMath::Max(ClientPresentationDistance,static_cast<float>(FMath::Max(0.,FVector::DotProduct(GetActorLocation()-FVector(PresentationLaunch.Origin),FVector(PresentationLaunch.Direction)))));
        UpdateCosmeticPosition(Distance);
        if (!bVisualStyleResolved)
        {
            VisualStyle=CastChecked<ADemoWeaponBase>(GetOwner())->ResolveProjectileStyle(PresentationLaunch);
            bVisualStyleResolved=true; // 与已到的RPC共享冻结回退值，不能Actor/亮条因Shooter晚到而颜色不一致。
            DemoAmmoVisuals::ApplyMesh(Visual,VisualStyle,Config.TracerMaterial);
        }
        CastChecked<ADemoWeaponBase>(GetOwner())->RegisterProjectilePresentation(this,PresentationLaunch); // 初始样本/Actor任意先后都绑定同一纯表现状态。
    }
    if (bVisibleFlight && Config.TrailEffect && !Trail)
    {
        Trail = UNiagaraFunctionLibrary::SpawnSystemAttached(Config.TrailEffect, CosmeticRoot, NAME_None, FVector::ZeroVector,
            FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
        if (Trail) { Trail->SetAbsolute(false, false, true); Trail->SetWorldScale3D(FVector::OneVector); } // 避免随CosmeticRoot动态缩放导致Niagara拖尾拉扯变形。
    }
    else if (!bVisibleFlight && Trail) { Trail->DeactivateImmediate(); Trail->DestroyComponent(); Trail = nullptr; }
}

void ADemoProjectileBase::OnRep_ReplicatedMovement()
{
    DEMO_LOG_TICK();
    Super::OnRep_ReplicatedMovement();
    if (!HasAuthority() && bFlying)
    {
        const float Distance=FMath::Max(ClientPresentationDistance,static_cast<float>(FMath::Max(0.,FVector::DotProduct(GetActorLocation()-FVector(PresentationLaunch.Origin),FVector(PresentationLaunch.Direction)))));
        UpdateCosmeticPosition(Distance); // 只移动非复制子层，Movement仍关闭；不等待开启Tracer。
        if (ADemoWeaponBase* Weapon=Cast<ADemoWeaponBase>(GetOwner())) Weapon->RegisterProjectilePresentation(this,PresentationLaunch);
    }
}
void ADemoProjectileBase::ReceivePresentationSample(const FDemoProjectilePresentationSample& Sample)
{
    DEMO_LOG_TICK();
    if (HasAuthority() || Sample.Launch.ShotId!=PresentationLaunch.ShotId || Sample.Launch.Pellet!=PresentationLaunch.Pellet) return;
    ClientPresentationDistance=FMath::Max(ClientPresentationDistance,Sample.EndDistance);
    if (Sample.bTerminal)
    {
        SetActorHiddenInGame(true); Visual->SetHiddenInGame(true); Visual->SetVisibility(false);
        if (Trail) { Trail->DeactivateImmediate(); Trail->DestroyComponent(); Trail=nullptr; }
        return; // 不延迟HP、不生成新Mesh、不销毁任何权威Actor。
    }
    UpdateCosmeticPosition(ClientPresentationDistance,DemoProjectilePresentation::IsWorldBoundary(Sample)); // 首弹射边界包丢失也由后续重复事实收紧，出射永远使用世界轨迹。
}
void ADemoProjectileBase::OnRep_Owner()
{
    DEMO_LOG_CALL();
    Super::OnRep_Owner();
    if (!HasAuthority()) OnRep_Flying(); // 武器引用晚到与初始飞行描述共用可见性入口。
}
void ADemoProjectileBase::RefreshLaunchVisual()
{
    DEMO_LOG_TICK();
    if (!bFlying || bConsumed || IsHidden()) return;
    const float Distance=HasAuthority()?TravelledDistance:FMath::Max(ClientPresentationDistance,
        static_cast<float>(FMath::Max(0.,FVector::DotProduct(GetActorLocation()-FVector(PresentationLaunch.Origin),FVector(PresentationLaunch.Direction)))));
    LastCosmeticPosition=UpdateCosmeticPosition(Distance); // 若尚未移动，下一步从本帧最终枪口开始；无客户端模拟。
}
void ADemoProjectileBase::CreateLocalTracer(const FVector& Origin)
{
    DEMO_LOG_CALL();
    if (!Config.bEnableTracer || GetNetMode()==NM_DedicatedServer) return;
    FActorSpawnParameters Spawn; // 无Owner/伤害依赖，原弹结束后仍可独立淡出。
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Tracer=GetWorld()->SpawnActor<ADemoProjectileTracer>(Origin,GetActorRotation(),Spawn);
    if (IsValid(Tracer) && !Tracer->Initialize(Config,Origin,VisualStyle,bRicochetUsed)) { Tracer->Destroy(); Tracer=nullptr; }
    if (IsValid(Tracer)) Tracer->SetCrossSectionScale(CosmeticRenderScale); // 作者宽度只乘一次，长轴仍由实际映射端点确定。
    if (!IsValid(Tracer)) UE_LOG(LogFPSDemo,Warning,TEXT("PROJECTILE_TRACER_CREATE_FAILED actor=%s gameplay remains active"),*GetName());
}
FVector ADemoProjectileBase::UpdateCosmeticPosition(float Distance,bool bBoundary)
{
    DEMO_LOG_TICK();
    const FVector Physical=GetActorLocation(); // 唯一物理根只读，任何输出都不能用于碰撞/伤害。
    ADemoWeaponBase* Weapon=Cast<ADemoWeaponBase>(GetOwner());
    const FVector Presented=Weapon?Weapon->ResolveProjectileVisual(PresentationLaunch,Physical,Distance,Config,bBoundary):Physical;
    CosmeticRenderScale=Weapon?Weapon->GetProjectileVisualScale(PresentationLaunch,Config):1.f; // Resolve已冻结/收紧资格；不能每帧读取当前枪的FP比例。
    if (CosmeticRoot)
    {
        CosmeticRoot->SetWorldLocation(Presented);
        if (Distance>0.f && !Presented.Equals(Physical, .01f) && !Presented.Equals(LastCosmeticPosition, .05f))
        {
            CosmeticRoot->SetWorldRotation((Presented - LastCosmeticPosition).GetSafeNormal().Rotation()); // 桥接曲线段使弹体与拖尾朝向实际视觉切线，避免移动射击斜向扭曲。
        }
        else
        {
            CosmeticRoot->SetRelativeRotation(FRotator::ZeroRotator);
        }
        CosmeticRoot->SetWorldScale3D(FVector(CosmeticRenderScale)); // VisualTransform仅继承一次；Sphere/Movement/Actor world scale完全不变。
    }
    if (IsValid(Tracer)) Tracer->SetCrossSectionScale(CosmeticRenderScale); // 只改Y/Z，不能再次缩短已映射段。
    if (bBoundary && !LastCosmeticPosition.Equals(Physical,.01f))
    {
        if (IsValid(Tracer)) Tracer->Destroy(); // 近距真实接触不让旧偏移尾迹留在虚假接触点。
        Tracer=nullptr; CosmeticTravelDistance=0.f; LastCosmeticPosition=Physical;
        if (HasAuthority() && bFlying && !bConsumed) CreateLocalTracer(Physical); // 穿透/折向后的后续真实运动继续沿世界轨迹。
    }
    return Presented;
}
#if WITH_EDITOR
FVector ADemoProjectileBase::GetCosmeticLocationForAudit() const { DEMO_LOG_TICK(); return CosmeticRoot?CosmeticRoot->GetComponentLocation():GetActorLocation(); }
float ADemoProjectileBase::GetCosmeticScaleForAudit() const { DEMO_LOG_TICK(); return CosmeticRoot?static_cast<float>(CosmeticRoot->GetComponentScale().X):1.f; }
float ADemoProjectileBase::GetActualDistanceForAudit() const { DEMO_LOG_TICK(); return TravelledDistance; }
const FDemoProjectilePresentationLaunch& ADemoProjectileBase::GetPresentationLaunchForAudit() const { DEMO_LOG_TICK(); return PresentationLaunch; }
#endif
