// 职责：聚合扣血回执，发送拥有者HUD与相关客户端表面表现；新目录由服务器按武器/头部/表面选V11专业库空间命中声（设施分档V11、Boss钢板V5），射手另有普通/头部V11 3D层与V5击杀确认2D层。使用实体弹/近战结算后Confirm，依赖固定World池/Timer和CombatFeel配置，不预测伤害。
#include "Combat/Feedback/DemoHitFeedbackComponent.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/Melee/DemoMeleeComponent.h" // 只把服务器真实HP回执交给本人当前近战相机，声音/伤害链保持独立。
#include "Components/AudioComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundAttenuation.h" // 射手专属层3D衰减（OwnerAttenuation）。
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

UDemoHitFeedbackComponent::UDemoHitFeedbackComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true); // 默认子对象随Pawn复制，Client RPC只到拥有者，未拥有Pawn不广播HUD。
    for (int32 Index = 1; Index <= 3; ++Index) // 三个已导入的肉体声硬引用，不生成或改写二进制资产。
    {
        ConstructorHelpers::FObjectFinder<USoundWave> Wave(*FString::Printf(TEXT("/Game/Audio/SFX/Weapons/Sword/Hit/Waves/SW_Sword_Hit_%02d"), Index)); // CDO初始化时加载，射击路径不读盘。
        if (Wave.Succeeded()) ConfirmationSounds.Add(Wave.Object);
    }
}

void UDemoHitFeedbackComponent::Confirm(const FDemoHitFeedbackEvent& Hit, USoundBase* ImpactSound)
{
    DEMO_LOG_TICK(); // 实际每颗子弹调用；保留源码埋点，由限时Trace开启逐次入口。
    if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() || !Hit.ShotId.IsValid()
        || !FMath::IsFinite(Hit.Damage) || Hit.Damage <= 0.f || Hit.ImpactPoint.ContainsNaN())
    { UE_LOG(LogFPSDemo, Log, TEXT("HIT_FEEDBACK_REJECT authority/value")); return; }
    if (Hit.bMelee) // 每次实际扣血回执一次；冻结资源身份，不能用挥击声日志或HUD成功替代空间声链路证据。
        UE_LOG(LogFPSDemo, Log, TEXT("MELEE_CONTACT_SUBMIT shot=%s target=%u sound=%s metal_target=%d point=%s world=%s"),
            *Hit.ShotId.ToString(), Hit.TargetId, *GetPathNameSafe(ImpactSound), Hit.bMetalTarget, *Hit.ImpactPoint.ToCompactString(), *GetNameSafe(GetWorld()));
    if (UDemoMetalImpactSubsystem* Pool = GetWorld()->GetSubsystem<UDemoMetalImpactSubsystem>()) // 服务器所有玩家共用一个预算；不会因客户端数增加准入速率。
    {
        if (!Hit.ImpactNormal.ContainsNaN() && Pool->Admit(Hit.ShotId, Hit.TargetId, Hit.bMelee))
        {
            if (PendingImpacts.Num() >= 8) FlushPending(); // 每个不可靠批次有严格上限，可靠Gameplay不依赖此队列。
            FDemoMetalImpactEvent Event; // 冻结实际接触点，致死/击退后不能用怪物当前位置替代。
            Event.Point = Hit.ImpactPoint; Event.Normal = Hit.ImpactNormal; Event.bMelee = Hit.bMelee; Event.Sound = Hit.bMelee ? ImpactSound : nullptr;
            Event.bRedesigned = Hit.bRedesigned && !Hit.bMelee;
            Event.bSuppressSparks = Event.bRedesigned && Hit.Surface == EDemoImpactSurface::Flesh;
            Event.FleshTier = DemoHitFx::ComputeFleshTier(Hit.Surface, Hit.bRedesigned || Hit.bMelee, Hit.bCore); // 近战独立视觉许可，保留原bRedesigned音频含义。
            Event.bHeavyWeapon = UDemoCombatFeelSettings::IsHeavyWeapon(Hit.WeaponId);
            if (Event.FleshTier > 0) Event.bSuppressSparks = true; // 刀击血肉也只出血雾，不占近战金属保留槽；声音仍原DA。
            if (Event.bRedesigned) Event.Sound = GetDefault<UDemoCombatFeelSettings>()->SelectImpact(Hit.Surface, Hit.WeaponId, Hit.bCore, Hit.DeviceHealthFraction); // 服务器按原武器轻/重、head替代、设施剩余血量分档选择命中，Wave指针随原批次复制，网络字段不变。
            Event.bPlaySound = Hit.bMetalTarget || Event.bRedesigned; // 共用原服务器令牌与固定声部，声音也不按散弹颗数叠加。
            PendingImpacts.Add(Event);
        }
        else if (Hit.bMelee) UE_LOG(LogFPSDemo, Log, TEXT("MELEE_CONTACT_SKIPPED normal/admission shot=%s target=%u normal_valid=%d"),
            *Hit.ShotId.ToString(), Hit.TargetId, !Hit.ImpactNormal.ContainsNaN()); // Admit内部保留具体去重/限流Trace，不在每个扫掠子步刷日志。
    }
    else if (Hit.bMelee) UE_LOG(LogFPSDemo, Warning, TEXT("MELEE_CONTACT_SKIPPED missing world pool shot=%s"), *Hit.ShotId.ToString());
    for (FDemoHitFeedbackEvent& Existing : Pending) // 仅本次同步回调借用；同枪同目标合并减少散弹RPC。
    {
        if (Existing.ShotId != Hit.ShotId || Existing.TargetId != Hit.TargetId) continue;
        Existing.Damage = FMath::Min(Existing.Damage + Hit.Damage, 1.e9f);
        if ((Hit.bKilled ? 2 : Hit.bCore ? 1 : 0) > (Existing.bKilled ? 2 : Existing.bCore ? 1 : 0)) Existing.Surface = Hit.Surface; // 同帧更高结果携带对应表面，不能只OR结果而留下错误分类。
        Existing.bCore |= Hit.bCore; Existing.bKilled |= Hit.bKilled;
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_FEEDBACK_MERGED target=%u damage=%.2f"), Existing.TargetId, Existing.Damage);
        return;
    }
    if (Pending.Num() >= 16) FlushPending(); // 队列有界；异常超高命中率提前提交，不无限分配内存。
    Pending.Add(Hit);
    if (!FlushTimer.IsValid()) FlushTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UDemoHitFeedbackComponent::FlushPending);
}

void UDemoHitFeedbackComponent::FlushPending()
{
    DEMO_LOG_CALL();
    GetWorld()->GetTimerManager().ClearTimer(FlushTimer);
    const TArray<FDemoMetalImpactEvent> Impacts = MoveTemp(PendingImpacts); // 先消费，Listen同步执行时不会重入待发数组。
    PendingImpacts.Reset();
    if (!Impacts.IsEmpty()) MulticastMetalImpacts(Impacts);
    if (Pending.IsEmpty()) return;
    const TArray<FDemoHitFeedbackEvent> Batch = MoveTemp(Pending); // 先移出队列，RPC同步本机执行也不会读到正在修改的数据。
    Pending.Reset();
    UE_LOG(LogFPSDemo, Log, TEXT("HIT_FEEDBACK_BATCH count=%d"), Batch.Num());
    ClientConfirmHits(Batch);
}

void UDemoHitFeedbackComponent::MulticastMetalImpacts_Implementation(const TArray<FDemoMetalImpactEvent>& Events)
{
    DEMO_LOG_CALL();
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return; // 专服准入和复制，但不创建本地视觉/声音。
    if (UDemoMetalImpactSubsystem* Pool = GetWorld()->GetSubsystem<UDemoMetalImpactSubsystem>()) // 与本机拥有者无关；队友/观战者也看见同一个接触点。
        for (int32 Index = 0; Index < FMath::Min(Events.Num(), 8); ++Index) Pool->Play(Events[Index]); // 防御超大批次，不分配临时对象。
}

void UDemoHitFeedbackComponent::ClientConfirmHits_Implementation(const TArray<FDemoHitFeedbackEvent>& Hits)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = Cast<ADemoCharacter>(GetOwner()); // 当前拥有者借用，不解析服务器TargetId或敌人引用。
    if (!Player || !Player->IsLocallyControlled() || !GetWorld())
    { UE_LOG(LogFPSDemo, Log, TEXT("HIT_FEEDBACK_SKIPPED not local owner")); return; }
    const double Now = GetWorld()->GetTimeSeconds(); // 本机暂停友好时间，不将服务器时钟直接与HUD相减。
    TArray<FDemoHitFeedbackEvent> GradedBatch; // 本批至多16条候选；先取每枪最高结果，穿透同批普通不能抢先吞掉致死。
    for (int32 Index = Displays.Num() - 1; Index >= 0; --Index) // 反向删除过期缓存，避免持有任何跨关目标。
        if (Now - Displays[Index].StartedAt >= FMath::Clamp(DamageNumberSeconds, .1f, 2.f)) Displays.RemoveAt(Index);
    for (const FDemoHitFeedbackEvent& Hit : Hits) // 已授权的服务器回执；只消费有限合法显示值。
    {
        if (!Hit.ShotId.IsValid() || !FMath::IsFinite(Hit.Damage) || Hit.Damage <= 0.f || Hit.ImpactPoint.ContainsNaN())
        { UE_LOG(LogFPSDemo, Warning, TEXT("HIT_FEEDBACK_REJECT invalid receipt")); continue; }
        FDemoHitFeedbackDisplay* Display = nullptr; // 数组同步借用；Add之后重新取Last，不保存扩容前指针。
        for (FDemoHitFeedbackDisplay& Existing : Displays) // 跨帧散弹/穿透按射击ID和目标ID合并。
            if (Existing.Hit.ShotId == Hit.ShotId && Existing.Hit.TargetId == Hit.TargetId) { Display = &Existing; break; }
        if (!Display)
        {
            if (Displays.Num() >= 12) Displays.RemoveAt(0);
            FDemoHitFeedbackDisplay NewDisplay; // 首次位置保持稳定，后续弹丸不让数字在目标身上跳动。
            NewDisplay.Hit = Hit; NewDisplay.StartedAt = Now;
            Displays.Add(NewDisplay); Display = &Displays.Last();
        }
        else
        {
            Display->Hit.Damage = FMath::Min(Display->Hit.Damage + Hit.Damage, 1.e9f);
            Display->Hit.bCore |= Hit.bCore; Display->Hit.bKilled |= Hit.bKilled;
        }
        const int32 Kind = Hit.bKilled ? 2 : Hit.bCore ? 1 : 0; // 普通命中不覆盖仍在显示的核心/击杀。
        const float Lifetime = FMath::Clamp(MarkerKind == 2 ? KillMarkerSeconds : HitMarkerSeconds, .05f, 1.f); // 防止蓝图旧值越界。
        if (Kind >= MarkerKind || Now - MarkerStartedAt >= Lifetime)
        {
            MarkerKind = Kind; MarkerStartedAt = Now;
            bMarkerDestruction = Hit.bKilled && Hit.bMetalTarget;
        }
        Player->LastHitTime = static_cast<float>(Now); // 保留旧只读诊断兼容值，HUD改用本组件分级缓存。
        if(Hit.bMelee) if(UDemoMeleeComponent* Melee=Player->FindComponentByClass<UDemoMeleeComponent>()) Melee->ConfirmCameraHit(Hit.ShotId); // 自己Pawn的RPC/Avatar，不取Player0或按当前枪声猜刀配置。
        if (Hit.bRedesigned && !Hit.bMelee && !Hit.bMetalTarget)
        {
            FDemoHitFeedbackEvent* Best = nullptr; // 数组只在本同步批次借用，Add后不保留旧指针。
            for (FDemoHitFeedbackEvent& Candidate : GradedBatch) if (Candidate.ShotId == Hit.ShotId) { Best = &Candidate; break; }
            if (!Best) GradedBatch.Add(Hit);
            else if ((Hit.bKilled ? 2 : Hit.bCore ? 1 : 0) > (Best->bKilled ? 2 : Best->bCore ? 1 : 0)) *Best = Hit;
        }
        else PlayConfirmation(Hit, Now);
        UE_LOG(LogFPSDemo, Log, TEXT("HIT_FEEDBACK_RECEIVED shot=%s target=%u damage=%.2f core=%d kill=%d"),
            *Hit.ShotId.ToString(), Hit.TargetId, Hit.Damage, Hit.bCore, Hit.bKilled);
    }
    for (const FDemoHitFeedbackEvent& Best : GradedBatch) PlayRedesignedConfirmation(Best,Now); // 本批每枪一次；跨批再按最高等级账本升档。
}

void UDemoHitFeedbackComponent::PlayConfirmation(const FDemoHitFeedbackEvent& Hit, double Now)
{
    DEMO_LOG_CALL();
    if (Hit.bRedesigned && !Hit.bMelee && !Hit.bMetalTarget) { PlayRedesignedConfirmation(Hit, Now); return; }
    if (Hit.bMelee || Hit.bMetalTarget) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_FEEDBACK_AUDIO melee/objective uses spatial contact pool")); return; } // 近战用DA显式刀击Cue、设施用池金属默认；不能叠拥有者旧2D肉体声。
    if (HeardShots.Contains(Hit.ShotId)) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_FEEDBACK_AUDIO same shot suppressed")); return; }
    if (HeardShots.Num() >= 32) HeardShots.RemoveAt(0);
    HeardShots.Add(Hit.ShotId); // 即使限流或禁音也消费整枪，不能让后续散弹补播声音。
    if (ConfirmationSounds.IsEmpty() || ConfirmationVolume <= 0.f || Now - LastSoundAt < .075)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_FEEDBACK_AUDIO skipped muted/missing/rate limit")); return; }
    for (int32 Index = PlayingSounds.Num() - 1; Index >= 0; --Index) // 自动销毁后弱引用为空，无异步裸捕获。
        if (!PlayingSounds[Index].IsValid() || !PlayingSounds[Index]->IsPlaying()) PlayingSounds.RemoveAt(Index);
    if (PlayingSounds.Num() >= 3)
    {
        if (FadingSound.IsValid()) FadingSound->Stop(); // 提前停止旧退场声，持续替换也保持一个有界尾音。
        FadingSound = PlayingSounds[0];
        if (FadingSound.IsValid()) FadingSound->FadeOut(.025f, 0.f);
        PlayingSounds.RemoveAt(0);
    }
    USoundWave* Wave = ConfirmationSounds[FMath::RandRange(0, ConfirmationSounds.Num() - 1)]; // 强资源数组中同步借用；沿用肉体音色，绝不复用枪声。
    if (!Wave) { UE_LOG(LogFPSDemo, Warning, TEXT("HIT_FEEDBACK_AUDIO missing variant")); return; }
    const float Pitch = FMath::FRandRange(.95f, 1.05f); // 每枪一次轻微随机；不叠加Cue内部Modulator。
    const float Gain = FMath::Pow(10.f, FMath::FRandRange(-1.5f, 1.5f) / 20.f); // dB转线性，确认层留出混音余量。
    UAudioComponent* Audio = UGameplayStatics::CreateSound2D(this, Wave, FMath::Clamp(ConfirmationVolume, 0.f, 1.f) * Gain, Pitch); // 先创建再播放，World自动销毁，不跨地图持久化。
    if (Audio)
    {
        Audio->bIsUISound = false; // 必须在Play创建ActiveSound之前设置，单人暂停不能把确认声当菜单音继续播放。
        Audio->Play(); PlayingSounds.Add(Audio); LastSoundAt = Now;
    }
    else UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_FEEDBACK_AUDIO unavailable device"));
}

void UDemoHitFeedbackComponent::PlayRedesignedConfirmation(const FDemoHitFeedbackEvent& Hit, double Now)
{
    DEMO_LOG_CALL();
    const uint8 Kind = Hit.bKilled ? 2 : Hit.bCore ? 1 : 0; // 相同Shot跨目标只升不降，最多普通/弱点/致死各一短层。
    const int32 Existing = GradedShots.IndexOfByKey(Hit.ShotId); // 最多32枪线性查找，无目标引用或跨关对象。
    if (Existing != INDEX_NONE)
    {
        if (HeardGrades[Existing] >= Kind)
        {
            UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CONFIRM duplicate/lower grade kind=%u existing=%u"), static_cast<uint32>(Kind), static_cast<uint32>(HeardGrades[Existing]));
            return;
        }
        HeardGrades[Existing] = Kind;
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CONFIRM grade advanced index=%d kind=%u"), Existing, static_cast<uint32>(Kind));
    }
    else
    {
        if (GradedShots.Num() >= 32) { GradedShots.RemoveAt(0); HeardGrades.RemoveAt(0); }
        GradedShots.Add(Hit.ShotId); HeardGrades.Add(Kind);
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CONFIRM grade registered kind=%u retained=%d"), static_cast<uint32>(Kind), GradedShots.Num());
    }
    const UDemoCombatFeelSettings* Feel = GetDefault<UDemoCombatFeelSettings>(); // 发射选择已冻结，不在这里重新读取AB开关。
    if (!Feel->bPlayConfirmationSounds)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CONFIRM deliberately silent; grade consumed kind=%u"), static_cast<uint32>(Kind));
        return; // 仍消费普通/弱点/致死等级，HUD已更新；静音不进入缺资源Warning，也不补播旧完整肉体声。
    }
    const float Volume = Feel->GetConfirmationVolume(Kind); // 本等级本人层音量：普通OwnerBody、头部、击杀各自配置。
    if ((Existing == INDEX_NONE && Kind < 2 && Now - LastSoundAt < .075) || Volume <= 0.f)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CONFIRM suppressed by mix budget/zero volume kind=%d"), Kind); return; }
    // 射手专属层（同一枪只升档一次，霰弹不按弹丸叠加）。普通/头部：用户2026-11-17要求实录且“随距离变小”，在本枪冻结的命中点3D播放、
    // 使用比世界层平缓的OwnerAttenuation；击杀确认仍为2D。仅本机射手执行（ClientConfirmHits），队友听不到此层。
    const FVector Point = Hit.ImpactPoint; // 服务器回执的真实接触点世界cm；本同步调用内借用。
    PlayConfirmationWave(Feel->SelectConfirmation(Kind), Volume, Now, Kind < 2 ? &Point : nullptr, Kind < 2 ? Feel->OwnerAttenuation.Get() : nullptr);
}

void UDemoHitFeedbackComponent::PlayConfirmationWave(USoundWave* Wave, float Volume, double Now, const FVector* SpatialPoint, USoundAttenuation* Attenuation)
{
    DEMO_LOG_CALL();
    if (!Wave || !GetWorld() || Volume <= 0.f) { UE_LOG(LogFPSDemo, Warning, TEXT("FEEL_CONFIRM missing candidate wave/device")); return; }
    for (int32 Index = PlayingSounds.Num() - 1; Index >= 0; --Index)
        if (!PlayingSounds[Index].IsValid() || !PlayingSounds[Index]->IsPlaying()) PlayingSounds.RemoveAt(Index);
    if (PlayingSounds.Num() >= 3)
    {
        if (FadingSound.IsValid()) FadingSound->Stop();
        FadingSound = PlayingSounds[0]; if (FadingSound.IsValid()) FadingSound->FadeOut(.025f,0.f);
        PlayingSounds.RemoveAt(0);
    }
    const float Gain = Volume * FMath::Pow(10.f,FMath::FRandRange(-.5f,.5f)/20.f); // ±0.5dB单次随机，仅此声部。
    const float Pitch = FMath::FRandRange(.98f,1.02f);
    if (SpatialPoint && Attenuation && !SpatialPoint->ContainsNaN())
    {
        // 射手专属3D层：未附着、自动销毁，按OwnerAttenuation随距离变小；仍计入同一3活动+1淡出预算。
        UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Wave, *SpatialPoint, FRotator::ZeroRotator, Gain, Pitch, 0.f, Attenuation);
        if (Audio) { PlayingSounds.Add(Audio); LastSoundAt = Now; }
        UE_LOG(LogFPSDemo, Log, TEXT("FEEL_OWNER_PLAY wave=%s volume=%.2f point=%s spatial=1 active=%d"), *GetPathNameSafe(Wave), Gain, *SpatialPoint->ToCompactString(), Audio && Audio->IsPlaying());
        return;
    }
    if (SpatialPoint) UE_LOG(LogFPSDemo, Log, TEXT("FEEL_OWNER_PLAY spatial fallback to 2D (missing attenuation or invalid point)")); // 缺衰减时降级2D，不静音。
    UAudioComponent* Audio = UGameplayStatics::CreateSound2D(this,Wave,Gain,Pitch); // 击杀确认等2D层；World自动销毁，三声部＋一淡出。
    if (Audio) { Audio->bIsUISound = false; Audio->Play(); PlayingSounds.Add(Audio); LastSoundAt = Now; }
}

const TArray<FDemoHitFeedbackDisplay>& UDemoHitFeedbackComponent::GetHits() const { DEMO_LOG_TICK(); return Displays; }
float UDemoHitFeedbackComponent::GetMarkerAge() const { DEMO_LOG_TICK(); return GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds() - MarkerStartedAt) : -1.f; }
int32 UDemoHitFeedbackComponent::GetMarkerKind() const { DEMO_LOG_TICK(); return MarkerKind; }
bool UDemoHitFeedbackComponent::IsDestructionMarker() const { DEMO_LOG_TICK(); return bMarkerDestruction; }

void UDemoHitFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(FlushTimer);
    for (const TWeakObjectPtr<UAudioComponent>& Audio : PlayingSounds) // 卸载立即停止本组件拥有的确认层，其他枪声/世界音效不受影响。
        if (Audio.IsValid()) Audio->Stop();
    if (FadingSound.IsValid()) FadingSound->Stop();
    FadingSound.Reset();
    Pending.Reset(); PendingImpacts.Reset(); Displays.Reset(); HeardShots.Reset(); GradedShots.Reset(); HeardGrades.Reset(); PlayingSounds.Reset();
    Super::EndPlay(EndPlayReason);
}
