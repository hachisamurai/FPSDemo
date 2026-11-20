// 职责：权威表现限流、本机固定ISM/空间音频池与短寿命弹道；World自动建立，旧金属依赖DA_MetalImpact、新表面依赖CombatFeel预载目录。仅游戏线程，无伤害/物理权限。
#include "Combat/Feedback/DemoMetalImpactSubsystem.h"
#include "Combat/Feedback/DemoCombatFeelSettings.h"
#include "Combat/Feedback/DemoMetalImpactSettings.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundBase.h" // 诊断读取实际接触资源完整路径；不加载或改写资产。
#include "UObject/ConstructorHelpers.h"

UDemoMetalImpactSubsystem::UDemoMetalImpactSubsystem()
{
    DEMO_LOG_CALL();
    ConstructorHelpers::FObjectFinderOptional<UDemoMetalImpactSettings> Asset(TEXT("/Game/VFX/Impacts/DA_MetalImpact")); // 仅CDO/构造读取，命中不加载。
    Settings = Asset.Get();
}

bool UDemoMetalImpactSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    DEMO_LOG_CALL();
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UDemoMetalImpactSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL();
    Super::Initialize(Collection);
    Recent.Reserve(128); Targets.Reserve(128);
    BulletRate = Settings && FMath::IsFinite(Settings->BulletEventsPerSecond) ? FMath::Clamp(Settings->BulletEventsPerSecond, 1.f, 120.f) : 48.f; // 非有限配置回退默认，不能让NaN破坏令牌桶。
    Interval = Settings && FMath::IsFinite(Settings->TargetInterval) ? FMath::Clamp(Settings->TargetInterval, .02f, .5f) : .06f;
    MaxDistance = Settings && FMath::IsFinite(Settings->MaxDistance) ? FMath::Clamp(Settings->MaxDistance, 100.f, 10000.f) : 2500.f;
    LastAdmission = GetWorld()->GetTimeSeconds();
}

void UDemoMetalImpactSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    DEMO_LOG_CALL();
    Super::OnWorldBeginPlay(InWorld);
    if (InWorld.GetNetMode() == NM_DedicatedServer) { UE_LOG(LogFPSDemo, Log, TEXT("METAL_POOL dedicated: admission only")); return; }
    const bool bVisualReady = Settings && Settings->SparkMesh && Settings->FlashMesh && Settings->SparkMaterial && Settings->FlashMaterial; // 世界接触音不能依赖金属网格/材质是否齐全。
    FActorSpawnParameters Params; // 本机表现Actor不保存、不复制，不参与碰撞生成判定。
    Params.ObjectFlags = RF_Transient;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    PoolActor = InWorld.SpawnActor<AActor>(Params);
    if (!PoolActor) { UE_LOG(LogFPSDemo, Warning, TEXT("METAL_POOL actor allocation failed")); return; }
    PoolActor->SetReplicates(false);
    PoolActor->Tags.Add(TEXT("DemoMetalImpactPool")); // 运行诊断定位共享池，不注册敌人/存档，也不赋予伤害权限。
    USceneComponent* Root = NewObject<USceneComponent>(PoolActor, TEXT("MetalPoolRoot")); // Actor拥有，注册后直到World卸载。
    PoolActor->SetRootComponent(Root); Root->RegisterComponent();
    const int32 VoiceCount = Settings ? FMath::Clamp(Settings->AudioVoices,1,8) : 6; // 缺金属DA仍可建立候选固定声音池。
    Voices.Reserve(VoiceCount);
    for (int32 Index = 0; Index < VoiceCount; ++Index)
    {
        UAudioComponent* Voice = NewObject<UAudioComponent>(PoolActor); // 固定预建声部，命中不创建临时声音对象。
        Voice->bAutoActivate = false; Voice->bAutoDestroy = false; Voice->bIsUISound = false;
        Voice->SetupAttachment(Root); Voice->SetSound(Settings ? Settings->MeleeSound.Get() : nullptr); Voice->RegisterComponent();
        Voices.Add(Voice);
    }
    Stats.PoolObjects = 2 + Voices.Num(); // 即使旧金属资源缺失，血雾仍独立建立。
    InitializeFleshMist();
    if (!bVisualReady)
    { UE_LOG(LogFPSDemo, Warning, TEXT("METAL_POOL visual resources missing; fixed contact audio/mist remain independent")); return; }
    const int32 Capacity = FMath::Clamp(Settings->ImpactSlots,1,32); // 视觉资源完整才建立固定ISM预算。
    Reserved = FMath::Clamp(Settings->MeleeReservedSlots,0,Capacity-1);
    Streaks = NewObject<UInstancedStaticMeshComponent>(PoolActor, TEXT("MetalStreakPool"));
    Flashes = NewObject<UInstancedStaticMeshComponent>(PoolActor, TEXT("MetalFlashPool"));
    for (UInstancedStaticMeshComponent* Mesh : {Streaks.Get(), Flashes.Get()}) // 两个共享绘制组件，不为每次命中创建对象。
    {
        Mesh->SetupAttachment(Root); Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false); Mesh->SetCastShadow(false); Mesh->bReceivesDecals = false;
        Mesh->SetStaticMesh(Mesh == Streaks ? Settings->SparkMesh : Settings->FlashMesh);
        Mesh->SetMaterial(0, Mesh == Streaks ? Settings->SparkMaterial : Settings->FlashMaterial);
        Mesh->NumCustomDataFloats = 1; Mesh->RegisterComponent();
    }
    const FTransform Hidden(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector); // 空闲实例原地零缩放，保留索引避免Add/Remove开销。
    Slots.SetNum(Capacity); SparkTransforms.Init(Hidden, Capacity * 12); FlashTransforms.Init(Hidden, Capacity);
    Streaks->AddInstances(SparkTransforms, false); Flashes->AddInstances(FlashTransforms, false);
    Stats.PoolObjects += 2; // 两个金属ISM；血雾已有则额外计入，命中永不增加。
    UE_LOG(LogFPSDemo, Display, TEXT("METAL_POOL_READY slots=%d reserved=%d instances=%d voices=%d objects=%d"), Capacity, Reserved, Capacity * 13, Voices.Num(), Stats.PoolObjects);
}

bool UDemoMetalImpactSubsystem::Admit(const FGuid& Shot, uint32 Target, bool bMelee)
{
    DEMO_LOG_TICK();
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !Shot.IsValid() || Target == 0)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("METAL_REJECT invalid authority/identity")); return false; }
    for (const FDemoMetalImpactStamp& Stamp : Recent) // 纯值128项线性扫描，不持有跨关敌人。
        if (Stamp.Shot == Shot && Stamp.Target == Target) { ++Stats.Deduplicated; UE_LOG(LogFPSDemo, VeryVerbose, TEXT("METAL_REJECT same shot/target")); return false; }
    const FDemoMetalImpactStamp Stamp{Shot, Target}; // 限流失败仍消费该枪目标组合，后续弹丸不能补播。
    if (Recent.Num() < 128) Recent.Add(Stamp); else { Recent[RecentCursor] = Stamp; RecentCursor = (RecentCursor + 1) % 128; }
    const double Now = GetWorld()->GetTimeSeconds(); // 仅本World时间，服务器暂停不补充令牌。
    const double Elapsed = FMath::Max(0., Now - LastAdmission); // 防御时钟回退，不产生负令牌。
    LastAdmission = Now;
    BulletTokens = FMath::Min(8., BulletTokens + Elapsed * BulletRate);
    MeleeTokens = FMath::Min(4., MeleeTokens + Elapsed * 16.);
    FDemoMetalImpactTarget* Previous = nullptr; // 同步借用，数组扩容后不再引用旧指针。
    if (!bMelee) for (FDemoMetalImpactTarget& Item : Targets) if (Item.Target == Target) { Previous = &Item; break; } // 查本目标上次已接受时间。
    double& Tokens = bMelee ? MeleeTokens : BulletTokens; // 分开的近战预算，枪弹不能消耗。
    if (Tokens < 1. || (Previous && Now - Previous->At < Interval))
    { ++Stats.RateDropped; UE_LOG(LogFPSDemo, VeryVerbose, TEXT("METAL_REJECT rate target=%u melee=%d"), Target, bMelee); return false; }
    Tokens -= 1.;
    if (!bMelee)
    {
        if (Previous) Previous->At = Now;
        else
        {
            const FDemoMetalImpactTarget NewTarget{Target, Now}; // 成功准入后才记录间隔，不让失败反馈延长屏蔽。
            if (Targets.Num() < 128) Targets.Add(NewTarget); else { Targets[TargetCursor] = NewTarget; TargetCursor = (TargetCursor + 1) % 128; }
        }
    }
    ++Stats.Accepted;
    return true;
}

void UDemoMetalImpactSubsystem::Play(const FDemoMetalImpactEvent& Event)
{
    DEMO_LOG_TICK();
    if (!PoolActor || Event.Point.ContainsNaN() || Event.Normal.ContainsNaN())
    {
        if (Event.bMelee) UE_LOG(LogFPSDemo, Log, TEXT("MELEE_CONTACT_SKIPPED missing pool/invalid event world=%s"), *GetNameSafe(GetWorld()));
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("METAL_PLAY skipped missing pool/invalid event")); return;
    }
    float DistanceSquared = TNumericLimits<float>::Max(); // 所有本地观众中最近者；Listen房主及分屏均只建一个池。
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) // World拥有的控制器，当前帧借用。
    {
        APlayerController* PC = It->Get(); // 不用远端控制器位置做本机LOD。
        if (!PC || !PC->IsLocalController()) continue;
        FVector ViewLocation; FRotator ViewRotation; // 当前相机世界cm/角度，包含死亡观战视角。
        PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
        DistanceSquared = FMath::Min(DistanceSquared, static_cast<float>(FVector::DistSquared(ViewLocation, Event.Point)));
    }
    if (DistanceSquared > FMath::Square(MaxDistance))
    {
        if (Event.FleshTier > 0 && Event.bSuppressSparks) SpawnFleshMist(Event, DistanceSquared); // 新视觉可单独调距离，原音频距离预算不变。
        ++Stats.DistanceDropped;
        if (Event.bMelee) UE_LOG(LogFPSDemo, Log, TEXT("MELEE_CONTACT_SKIPPED distance_cm=%.1f limit_cm=%.1f world=%s"), FMath::Sqrt(DistanceSquared), MaxDistance, *GetNameSafe(GetWorld()));
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("METAL_PLAY distance culled")); return;
    }
    USoundBase* ImpactSound = Event.Sound ? Event.Sound.Get() : Settings ? Settings->MeleeSound.Get() : nullptr; // 声音与视觉就绪分开；缺旧DA仍能播放候选显式Wave。
    bool bAudioRequested = false; // 本次同步事件是否找到空声部并调用Play；只供有界诊断，不表示已解码或实际可听。
    if ((Event.bMelee || Event.bPlaySound) && ImpactSound)
    {
        for (UAudioComponent* Voice : Voices) // 第一个空声部复用；所有声部忙则自然省略，不抢断已有金属尾音。
            if (Voice && !Voice->IsPlaying())
            {
                const UDemoCombatFeelSettings* Feel = GetDefault<UDemoCombatFeelSettings>(); // CDO资源已加载；不按网络路径加载。
                const float ContactGain = Event.bRedesigned ? FMath::Pow(10.f, FMath::FRandRange(-1.5f, 1.5f) / 20.f) : 1.f; // 新空间Wave每次±1.5dB；旧近战/Cue保持原音量路径。
                Voice->AttenuationSettings = Event.bRedesigned ? Feel->ContactAttenuation.Get() : nullptr; // 回到旧Cue时必须清除新Wave衰减覆盖。
                Voice->SetVolumeMultiplier(Event.bRedesigned ? FMath::Clamp(Feel->ContactVolume,0.f,1.f) * ContactGain : 1.f);
                Voice->SetPitchMultiplier(Event.bRedesigned ? FMath::FRandRange(.95f,1.05f) : 1.f);
                Voice->SetSound(ImpactSound); Voice->SetWorldLocation(Event.Point); Voice->Play(); ++Stats.AudioPlayed; bAudioRequested = true;
                if (Event.bMelee) UE_LOG(LogFPSDemo, Log, TEXT("MELEE_CONTACT_REQUEST sound=%s fallback=%d component=%s active=%d device=%d allowed=%d point=%s component_point=%s distance_cm=%.1f world=%s"),
                    *GetPathNameSafe(ImpactSound), Event.Sound == nullptr, *GetNameSafe(Voice), Voice->IsPlaying(), Voice->GetAudioDevice() != nullptr, GetWorld()->bAllowAudioPlayback,
                    *Event.Point.ToCompactString(), *Voice->GetComponentLocation().ToCompactString(), FMath::Sqrt(DistanceSquared), *GetNameSafe(GetWorld())); // active只证明引擎接受为ActiveSound，不能充当听感验收。
                if (!Event.bMelee) UE_LOG(LogFPSDemo, Log, TEXT("FEEL_IMPACT_PLAY sound=%s redesigned=%d volume=%.3f distance_cm=%.1f active=%d"),
                    *GetPathNameSafe(ImpactSound), Event.bRedesigned, Voice->VolumeMultiplier, FMath::Sqrt(DistanceSquared), Voice->IsPlaying()); // 2026-11-17试听核对：弹击已受Admit限流，逐次记录实际资源身份。
                break;
            }
    }
    if (Event.bMelee && !bAudioRequested) UE_LOG(LogFPSDemo, Log, TEXT("MELEE_CONTACT_SKIPPED sound=%s voices=%d reason=%s world=%s"),
        *GetPathNameSafe(ImpactSound), Voices.Num(), ImpactSound ? TEXT("no_idle_voice") : TEXT("missing_sound"), *GetNameSafe(GetWorld()));
    if (Event.FleshTier > 0 && Event.bSuppressSparks) SpawnFleshMist(Event, DistanceSquared); // 血雾独立槽，先于旧肉体早退；不占近战金属保留。
    if (Event.bSuppressSparks || !Streaks || !Flashes) { Stats.LastPoint = Event.Point; return; } // 血肉独立血雾/空间声，身体短闪独立。
    int32 Free = INDEX_NONE; // 仅查当前已分配槽，近战优先用尾部保留区，弹击只用公共区。
    const int32 Limit = Event.bMelee ? Slots.Num() : Slots.Num() - Reserved; // 保护近战视觉的固定名额。
    for (int32 Index = Limit - 1; Index >= 0; --Index) if (Slots[Index].Age < 0.f) { Free = Index; break; } // 同步定位空闲槽。
    if (Free == INDEX_NONE) { ++Stats.PoolDropped; UE_LOG(LogFPSDemo, VeryVerbose, TEXT("METAL_PLAY pool full")); return; }
    FDemoMetalImpactSlot& Slot = Slots[Free]; // 本池固定数组引用，播放期间地址不变。
    const FVector Normal = FVector(Event.Normal).GetSafeNormal(.001f, FVector::UpVector); // 防御零法线，用向上短火花避免NaN。
    Slot.Origin = Event.Point + Normal * .8f; Slot.Age = 0.f;
    Slot.Count = DistanceSquared > FMath::Square(MaxDistance * .64f) ? 3 : DistanceSquared > FMath::Square(MaxDistance * .32f) ? 5 : Event.bMelee ? 12 : 8;
    for (int32 Index = 0; Index < Slot.Count; ++Index) // 每次命中最多12个纯数值初始化，无UObject/组件/粒子实例分配。
    {
        FDemoMetalSpark& Spark = Slot.Sparks[Index]; // 当前槽内固定根数；外向80度锥模拟金属弹跳散射。
        Spark.Velocity = FMath::VRandCone(Normal, FMath::DegreesToRadians(80.f)) * FMath::FRandRange(180.f, Event.bMelee ? 580.f : 460.f);
        Spark.Life = FMath::FRandRange(.15f, .32f); Spark.Length = FMath::FRandRange(3.f, 9.f);
    }
    ++Active; ++Stats.Played; Stats.PeakActive = FMath::Max(Stats.PeakActive, Active); Stats.LastPoint = Event.Point;
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("METAL_PLAY point=%s normal=%s melee=%d sparks=%d active=%d"), *Slot.Origin.ToCompactString(), *Normal.ToCompactString(), Event.bMelee, Slot.Count, Active);
}

void UDemoMetalImpactSubsystem::Tick(float DeltaTime)
{
    DEMO_LOG_TICK();
    if (FleshMistPool && MistState.Active > 0)
    {
        MistState.Step(DeltaTime, *GetDefault<UDemoHitFxSettings>());
        for (int32 Index=0; Index<MistState.Fades.Num(); ++Index) FleshMistPool->SetCustomDataValue(Index,0,MistState.Fades[Index],false); // 固定缓冲，无每帧分配。
        FleshMistPool->BatchUpdateInstancesTransforms(0,MistState.Transforms,true,true,true);
    }
    if (!Streaks || !Flashes) return;
    const FTransform Hidden(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector); // 所有结束实例清零，保留池索引。
    for (int32 Index = 0; Index < Slots.Num(); ++Index) // 固定上限32簇，不按历史命中数量增长。
    {
        FDemoMetalImpactSlot& Slot = Slots[Index]; // 仅当前帧同步借用。
        if (Slot.Age < 0.f) continue;
        Slot.Age += FMath::Max(0.f, DeltaTime);
        for (int32 SparkIndex = 0; SparkIndex < 12; ++SparkIndex) // 固定12槽，LOD未用的根同样隐藏。
        {
            const int32 Instance = Index * 12 + SparkIndex; // ISM固定偏移，从不Remove导致换序。
            const FDemoMetalSpark& Spark = Slot.Sparks[SparkIndex]; // 本簇初始速度与生存时间。
            if (SparkIndex >= Slot.Count || Slot.Age >= Spark.Life) { SparkTransforms[Instance] = Hidden; continue; }
            const float Fade = 1.f - Slot.Age / Spark.Life; // 用CustomData衰减亮度及宽度，白热色逐渐变橙。
            const FVector Velocity = Spark.Velocity + FVector(0,0,-320.f * Slot.Age); // cm/s，无碰撞物理模拟。
            const FVector Position = Slot.Origin + Spark.Velocity * Slot.Age + FVector(0,0,-160.f * FMath::Square(Slot.Age)); // 匀加速解析位移。
            const float Width = .0075f * (.4f + .6f * Fade); // 100cm基准网格对应0.3..0.75cm厚度，远距/时间抗锯齿下仍保持可见的短金属细线。
            SparkTransforms[Instance] = FTransform(Velocity.Rotation(), Position, FVector(Spark.Length * (.35f + Fade * .65f) / 100.f, Width, Width));
            Streaks->SetCustomDataValue(Instance, 0, Fade, false);
        }
        const float Flash = FMath::Max(0.f, 1.f - Slot.Age / .055f); // 瞬闪55ms，无持久发光球/动态灯。
        FlashTransforms[Index] = Flash > 0.f ? FTransform(FQuat::Identity, Slot.Origin, FVector(.055f * Flash)) : Hidden;
        Flashes->SetCustomDataValue(Index, 0, Flash, false);
        if (Slot.Age >= .32f) { Slot.Age = -1.f; --Active; }
    }
    Streaks->BatchUpdateInstancesTransforms(0, SparkTransforms, true, true, true);
    Flashes->BatchUpdateInstancesTransforms(0, FlashTransforms, true, true, true);
}

bool UDemoMetalImpactSubsystem::IsTickable() const { DEMO_LOG_TICK(); return (Active > 0 || MistState.Active > 0) && !IsTemplate(); }
TStatId UDemoMetalImpactSubsystem::GetStatId() const { DEMO_LOG_TICK(); RETURN_QUICK_DECLARE_CYCLE_STAT(DemoMetalImpactPool, STATGROUP_Tickables); }
const FDemoMetalImpactStats& UDemoMetalImpactSubsystem::GetStats() const { DEMO_LOG_TICK(); return Stats; }

void UDemoMetalImpactSubsystem::Deinitialize()
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Log, TEXT("METAL_POOL_SUMMARY accepted=%llu dedup=%llu rate=%llu played=%llu pool_drop=%llu distance=%llu audio=%llu peak=%d objects=%d mist_played=%llu mist_pool_drop=%llu"),
        Stats.Accepted, Stats.Deduplicated, Stats.RateDropped, Stats.Played, Stats.PoolDropped, Stats.DistanceDropped, Stats.AudioPlayed, Stats.PeakActive, Stats.PoolObjects, Stats.MistPlayed, Stats.MistPoolDropped);
    for (UAudioComponent* Voice : Voices) if (Voice) Voice->Stop(); // 所有World声音显式停止，旅行/停止PIE不遗留尾音。
    if (PoolActor) PoolActor->Destroy();
    PoolActor = nullptr; Streaks = nullptr; Flashes = nullptr; FleshMistPool = nullptr; Voices.Reset();
    MistState.Slots.Reset(); MistState.Transforms.Reset(); MistState.Fades.Reset(); MistState.Active=0;
    Slots.Reset(); SparkTransforms.Reset(); FlashTransforms.Reset(); Recent.Reset(); Targets.Reset(); Active = 0;
    Super::Deinitialize();
}

void UDemoMetalImpactSubsystem::InitializeFleshMist()
{
    DEMO_LOG_CALL();
    const UDemoHitFxSettings* Fx=GetDefault<UDemoHitFxSettings>(); // CDO已预载，World一次冻结容量，不在命中路径加载。
    if(!Fx->bFleshImpactMist) { UE_LOG(LogFPSDemo,Log,TEXT("FLESH_MIST_DISABLED config")); return; }
    if(!Fx->MistMaterial || !Fx->MistMesh)
    { UE_LOG(LogFPSDemo,Warning,TEXT("FLESH_MIST_DISABLED missing material")); return; } // 此初始化每World只调用一次，缺材质无每击中告警。
    if(!PoolActor) { UE_LOG(LogFPSDemo,Warning,TEXT("FLESH_MIST_DISABLED missing pool actor")); return; }
    FleshMistPool=NewObject<UInstancedStaticMeshComponent>(PoolActor,TEXT("FleshMistPool")); // 唯一新增表现对象；World卸载随Actor销毁。
    FleshMistPool->SetupAttachment(PoolActor->GetRootComponent()); FleshMistPool->SetMobility(EComponentMobility::Movable);
    FleshMistPool->SetCollisionEnabled(ECollisionEnabled::NoCollision); FleshMistPool->SetGenerateOverlapEvents(false);
    FleshMistPool->SetCanEverAffectNavigation(false); FleshMistPool->SetCastShadow(false); FleshMistPool->bReceivesDecals=false;
    FleshMistPool->SetStaticMesh(Fx->MistMesh); FleshMistPool->SetMaterial(0,Fx->MistMaterial);
    FleshMistPool->NumCustomDataFloats=1; FleshMistPool->RegisterComponent();
    MistState.Initialize(Fx->MistSlots,Fx->MistPuffsMax); FleshMistPool->AddInstances(MistState.Transforms,false);
    ++Stats.PoolObjects;
    UE_LOG(LogFPSDemo,Log,TEXT("METAL_POOL_READY mist_slots=%d mist_instances=%d"),MistState.Slots.Num(),MistState.Transforms.Num());
}

void UDemoMetalImpactSubsystem::SpawnFleshMist(const FDemoMetalImpactEvent& Event,float DistanceSquared)
{
    DEMO_LOG_TICK();
    if(!FleshMistPool) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("FLESH_MIST_SKIPPED disabled/missing pool")); return; }
    const uint64 OldDropped=MistState.Dropped; // 单次调用前快照，只有满池才计入drop，不把距离裁剪混入池满。
    const int32 Puffs=MistState.Spawn(Event.Point,Event.Normal,Event.FleshTier,Event.bHeavyWeapon,DistanceSquared,*GetDefault<UDemoHitFxSettings>());
    if(MistState.Dropped>OldDropped) {++Stats.MistPoolDropped; UE_LOG(LogFPSDemo,VeryVerbose,TEXT("FLESH_MIST_POOL_FULL"));}
    if(Puffs==0) return;
    ++Stats.MistPlayed;
    UE_LOG(LogFPSDemo,Log,TEXT("FLESH_MIST_PLAY tier=%d heavy=%d puffs=%d dist_cm=%.0f active=%d"),Event.FleshTier,Event.bHeavyWeapon,Puffs,FMath::Sqrt(DistanceSquared),MistState.Active);
}
