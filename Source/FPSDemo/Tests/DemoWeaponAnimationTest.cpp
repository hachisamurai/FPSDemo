#include "Tests/DemoWeaponAnimationTest.h"
#include "AI/DemoEnemy.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/DemoFPAnimInstance.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CoreGlobals.h"
#include "Debug/DemoLog.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/DemoInteractable.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

ADemoWeaponAnimationTest::ADemoWeaponAnimationTest()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    // 连续帧捕获需要每个实际World帧观察；普通回归保持原来0.02秒频率与同一组等待/断言。
    bCaptureFrames = FParse::Param(FCommandLine::Get(),TEXT("DemoWeaponAnimationFrames"));
    PrimaryActorTick.TickInterval = bCaptureFrames ? 0.f : .02f;
}

bool ADemoWeaponAnimationTest::Check(bool Condition, const TCHAR* Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Display,TEXT("WEAPON_ANIMATION_TEST %s weapon=%d step=%d cancel=%d: %s"),
        Condition ? TEXT("PASS") : TEXT("FAIL"),WeaponIndex,Step,CancelIndex,Message);
    if (!Condition)
    {
        bFailed = true;
        SetActorTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false,1);
    }
    return Condition;
}

void ADemoWeaponAnimationTest::Advance(int32 NextStep, float Delay)
{
    DEMO_LOG_CALL();
    Step = NextStep;
    NextTime = GetWorld()->GetTimeSeconds() + Delay;
}

bool ADemoWeaponAnimationTest::InitializeFixture()
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 测试World规则对象，初始化后不跨地图保存指针。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 解锁夹具暂设Victory，随后正式StartRun重置。
    ADemoPlayerController* Controller = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 单人测试本地控制器。
    UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // Editor目标使用每次GI独立的临时档案。
    if (!Check(Mode && State && Controller && Profile,TEXT("isolated runtime services ready"))) return false;
    // 本专项不替代武器库解锁回归；先制造合法三难度通关记录，再走真实终端装备权限。
    State->Phase = EDemoPhase::Victory;
    State->LevelNumber = 10;
    for (int32 Index = 0; Index < 3; ++Index) // 合法难度枚举0..2，记录只落在本专项临时档案。
    {
        State->Difficulty = static_cast<EDemoDifficulty>(Index);
        Profile->RecordVictory(FGuid::NewGuid().ToString(),State->Difficulty);
    }
    State->Phase = EDemoPhase::Lobby;
    State->LevelNumber = 0;
    State->Difficulty = EDemoDifficulty::Normal;
    if (!Check(Mode->StartRun(),TEXT("fresh hub and real upgrade terminal initialized"))) return false;
    Controller->OnRunReady();
    ADemoCharacter* Player = Cast<ADemoCharacter>(Controller->GetPawn()); // StartRun可能重置Pawn，始终重新借用。
    if (!Check(Player && Player->GetDemoASC() && Player->GetWeaponComponent(),TEXT("player ASC and loadout ready"))) return false;
    FixedMainInstance = Player->GetMesh1P()->GetAnimInstance();
    if (!Check(FixedMainInstance.IsValid() && FixedMainInstance->IsA<UDemoFPAnimInstance>(),TEXT("initial pistol already uses fixed native main AnimBP"))) return false;
    State->Phase = EDemoPhase::Combat;
    for (TActorIterator<ADemoEnemy> Enemy(GetWorld()); Enemy; ++Enemy) // 敌人只隔离本World，不影响正式配置或生成表。
    {
        Enemy->SetActorTickEnabled(false);
        Enemy->SetActorEnableCollision(false);
    }
    Player->SetActorLocation(Mode->GetAreaCenter(0) + FVector(-800.f,0.f,140.f));
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    Player->GetCharacterMovement()->StopMovementImmediately();
    Controller->SetControlRotation(FRotator::ZeroRotator);
    return true;
}

bool ADemoWeaponAnimationTest::SelectAtTerminal(int32 Index)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* Controller = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 当前World控制器，只同步借用。
    ADemoCharacter* Player = Controller ? Cast<ADemoCharacter>(Controller->GetPawn()) : nullptr; // 装备后恢复其坐标。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 本专项保留安全区终端。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 暂时切换阶段只是夹具，不调用旅行或存档。
    if (!Check(Player && Mode && State && Mode->GetShopTerminal(),TEXT("fixture terminal exists"))) return false;
    const EDemoPhase PreviousPhase = State->Phase; // UI交互后恢复原来的Combat状态。
    const FVector PreviousLocation = Player->GetActorLocation(); // 原隔离拍摄位置，世界厘米。
    State->Phase = EDemoPhase::Hub;
    Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation() + FVector(-180.f,0.f,20.f));
    Mode->GetShopTerminal()->Interact(Player);
    Controller->SetTerminalWeaponPage(true);
    Controller->InspectWeapon(Index + 1);
    Controller->EquipInspectedWeapon();
    Controller->CloseWeaponTip();
    Controller->CloseUpgradeMenu();
    State->Phase = PreviousPhase;
    Player->SetActorLocation(PreviousLocation);
    return Check(Player->GetWeaponComponent()->GetPrimaryIndex() == Index,TEXT("primary selected through terminal and permanent unlock guards"));
}

bool ADemoWeaponAnimationTest::SelectCurrentWeapon()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 当前Pawn，取完不跨本函数保留。
    if (!Check(Player != nullptr,TEXT("weapon selection player exists"))) return false;
    // 手枪用例先准备一把真实主武器，保证后续切枪取消有合法目标；不能凭空注入库存。
    if (!SelectAtTerminal(WeaponIndex == 0 ? 0 : WeaponIndex - 1)) return false;
    if (WeaponIndex == 0 && !Check(Player->GetWeaponComponent()->EquipSlot(2),TEXT("pistol selected through normal secondary slot"))) return false;
    TestedWeapon = Player->GetWeaponComponent()->GetActiveWeapon();
    return Check(TestedWeapon.IsValid(),TEXT("selected weapon instance retained for stale callback checks"));
}

void ADemoWeaponAnimationTest::Capture(const FString& Name)
{
    DEMO_LOG_CALL();
    if (!bCaptureFrames && FParse::Param(FCommandLine::Get(),TEXT("DemoWeaponAnimationCapture")) && FApp::CanEverRender())
    {
        // 截图在下一帧完成，状态机后续等待至少0.15秒，避免立即切枪/结束进程丢失请求。
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/WeaponAnimation")/(Name + TEXT(".png")),false,false);
    }
}

void ADemoWeaponAnimationTest::CaptureAnimationFrame()
{
    DEMO_LOG_TICK();
    if (!bCaptureFrames || !bCapturingFullReload || !FApp::CanEverRender() || bFailed) return;
    const bool bReloadFinished = !TestedWeapon.IsValid() || !TestedWeapon->IsReloading(); // 完成时记录恢复姿势，不延长或重启GAS动作。
    const float Now = GetWorld()->GetTimeSeconds(); // 使用World时钟，暂停不重复截图，同一渲染帧最多一张。
    if (LastScreenshotFrame == GFrameCounter || (!bReloadFinished && Now + KINDA_SMALL_NUMBER < NextFrameCaptureTime)
        || FScreenshotRequest::IsScreenshotRequested()) return;
    const FString FrameName = FString::Printf(TEXT("%s_%04d.png"),*CaptureFramePrefix,CaptureFrameIndex); // 当前套动作独立连续序号，不含其他状态截图。
    FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/WeaponAnimation/Frames")/FrameName,false,false);
    // 记录请求对应实际World帧的时刻；不假设截图保存耗时恒定，也不强行将每张图片解释成1/30秒。
    const float RelativeGameSeconds = FMath::Max(0.f,Now - FrameCaptureStartTime); // 秒，起点来自同次StartReload，暂停不增加时间。
    CaptureTimingText += FString::Printf(TEXT("%s\t%.6f\n"),*FrameName,RelativeGameSeconds);
    ++CaptureFrameIndex;
    LastScreenshotFrame = GFrameCounter;
    NextFrameCaptureTime = Now + 1.f / 30.f;
    if (bReloadFinished)
    {
        bCapturingFullReload = false;
        const FString TimingPath = FPaths::ProjectSavedDir()/TEXT("Screenshots/WeaponAnimation/Frames")/(CaptureFramePrefix + TEXT("_timing.tsv")); // 与图片同目录/前缀，外部编码器据此构建VFR时间轴。
        if (!FFileHelper::SaveStringToFile(CaptureTimingText,*TimingPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            UE_LOG(LogFPSDemo,Error,TEXT("WEAPON_ANIMATION_FRAME_TIMING_FAILED path=%s"),*TimingPath); // 导出错误不改变Gameplay计时或既有断言结果。
        UE_LOG(LogFPSDemo,Display,TEXT("WEAPON_ANIMATION_FRAMES_COMPLETE set=%s frames=%d timing=%s"),*CaptureFramePrefix,CaptureFrameIndex,*TimingPath);
    }
}

bool ADemoWeaponAnimationTest::CheckEquipped(bool bCapture)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 只检查正在运行的Pawn，不读取Editor预览实例。
    ADemoWeaponBase* Weapon = TestedWeapon.Get(); // 当前枪弱引用，换型号时由装备组件继续管理生命周期。
    UDemoWeaponAnimationComponent* View = Player ? Player->FindComponentByClass<UDemoWeaponAnimationComponent>() : nullptr; // Pawn持有的真实协调器。
    UAnimInstance* Main = Player ? Player->GetMesh1P()->GetAnimInstance() : nullptr; // 必须等于第一次装备的对象，而不仅是同一Class。
    if (!Check(Weapon && View && Main && Main == FixedMainInstance.Get(),TEXT("switch preserves exact main AnimInstance object"))) return false;
    const TCHAR* Names[] = { TEXT("Pistol"),TEXT("Rifle"),TEXT("Shotgun"),TEXT("Sniper") }; // Catalog顺序与测试一致。
    const FString ExpectedClassName = FString(TEXT("ABP_FP_")) + Names[WeaponIndex] + TEXT("_C"); // 真实导入资产的预期类，不从运行时getter反推预期。
    UAnimInstance* Linked = Main->GetLinkedAnimLayerInstanceByClass(View->GetLinkedLayerClass()); // 引擎返回真实链接实例，不能只验证缓存Class。
    if (!Check(View->GetLinkedLayerClass() && View->GetLinkedLayerClass()->GetName() == ExpectedClassName
        && Linked && Linked->IsA<UDemoWeaponLayerAnimInstance>(),TEXT("real linked layer uses expected weapon child class"))) return false;
    if (!Check(Player->GetDemoASC()->AbilityActorInfo.IsValid() && Player->GetDemoASC()->AbilityActorInfo->GetAnimInstance() == Main,
        TEXT("ASC montage context explicitly uses Mesh1P main instance"))) return false;
    USkeletalMeshComponent* Mesh = Weapon->FindComponentByClass<USkeletalMeshComponent>(); // 正式枪械机械骨架，与手臂为两个独立网格。
    if (!Check(Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetNumBones() >= 3
        && Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Weapon->IsHidden(),TEXT("equipped gun has collision-free mechanical skeleton"))) return false;
    if (!Check(Player->GetMesh1P()->IsBoneHiddenByName(TEXT("upperarm_l")) == Weapon->Config.bHideSupportArm,
        TEXT("idle support-arm visibility restored for this weapon"))) return false;
    IdleLeftHand = Player->GetMesh1P()->GetSocketLocation(TEXT("hand_l"));
    if (bCapture) Capture(FString(Names[WeaponIndex]) + TEXT("-Idle"));
    return true;
}

void ADemoWeaponAnimationTest::CaptureMechanicalRestPose()
{
    DEMO_LOG_CALL();
    MechanicalRestPose.Reset();
    USkeletalMeshComponent* Mesh = TestedWeapon.IsValid() ? TestedWeapon->FindComponentByClass<USkeletalMeshComponent>() : nullptr; // 只存变换值，不缓存骨组件地址。
    if (!Mesh) return;
    for (int32 Index = 0; Index < Mesh->GetNumBones(); ++Index) // 本枪所有骨骼都应在完成/取消后回到同一参考状态。
    {
        const FName BoneName = Mesh->GetBoneName(Index); // UE导入后的真实骨名，无Blender手写名称假设。
        MechanicalRestPose.Add(BoneName,Mesh->GetSocketTransform(BoneName,RTS_Component));
    }
}

bool ADemoWeaponAnimationTest::StartReload(bool bEmpty)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 仅本步骤借用Owner。
    ADemoWeaponBase* Weapon = TestedWeapon.Get(); // 操作预先装备的相同实例，不能替换库存骗过取消检查。
    UDemoWeaponAnimationComponent* View = Player ? Player->FindComponentByClass<UDemoWeaponAnimationComponent>() : nullptr; // 表现只观察，不直接调用BeginReloadPresentation。
    if (!Check(Player && Weapon && View && Player->GetWeaponComponent()->GetActiveWeapon() == Weapon,TEXT("reload targets selected current instance"))) return false;
    Weapon->Config.bInfiniteReserve = false; // 仅修改专项Actor实例，不改资产CDO；有限备用可揭露重复结算。
    Weapon->Config.InitialReserve = 2; // 发数，上限足够让普通/空仓两种转移均可检查。
    Weapon->RestoreAmmo(bEmpty ? 0 : 1,2);
    bExpectedEmpty = bEmpty;
    ExpectedAmmo = bEmpty ? 2 : 3;
    ReloadDuration = Weapon->Config.ReloadSeconds;
    if (!Check(ReloadDuration > .5f && Player->GetWeaponComponent()->RequestReload(bEmpty),TEXT("real GAS reload ability starts using configured gameplay duration"))) return false;
    ReloadSequence = View->GetReloadSequence();
    if (!Check(Weapon->IsReloading() && View->IsReloadPresentationActive() && View->IsEmptyReload() == bEmpty
        && Player->GetDemoASC()->HasMatchingGameplayTag(DemoTags::Reloading),TEXT("reload state, tag, presentation and frozen variant agree"))) return false;
    const FDemoReloadPhaseAudit InitialAudit = View->GetReloadPhaseAudit(); // 开始当帧只冻结引擎实例身份；NativeUpdate尚未取样时允许SampleCount为0。
    ReloadMontageInstanceID = InitialAudit.MontageInstanceID;
    if (!Check(InitialAudit.Sequence == ReloadSequence && ReloadMontageInstanceID != INDEX_NONE,
        TEXT("reload phase audit binds this sequence and actual montage instance"))) return false;
    if (!Check(!Player->GetWeaponComponent()->RequestReload() && View->GetReloadSequence() == ReloadSequence,
        TEXT("duplicate request rejected without replacing sequence or deadline"))) return false;
    // 只在完整普通/空仓两次动作入口启动录帧；后续Step7的三种取消用例继续原始时序，但不污染序列目录。
    if (bCaptureFrames && FApp::CanEverRender() && (Step == 2 || Step == 4))
    {
        const TCHAR* Names[] = { TEXT("Pistol"),TEXT("Rifle"),TEXT("Shotgun"),TEXT("Sniper") }; // 文件名采用稳定武器种类，不用Actor临时名称。
        CaptureFramePrefix = FString(Names[WeaponIndex]) + (bEmpty ? TEXT("_Empty") : TEXT("_Tactical"));
        CaptureFrameIndex = 0;
        FrameCaptureStartTime = GetWorld()->GetTimeSeconds(); // 同次开始冻结起点，后续中段/完成步骤不重置。
        NextFrameCaptureTime = FrameCaptureStartTime;
        CaptureTimingText = TEXT("frame_filename\trelative_game_seconds\n"); // TSV列含义固定，秒数为真实游戏时钟而非名义30fps。
        bCapturingFullReload = true;
        CaptureAnimationFrame(); // 立即请求起始帧，后续帧由Tick等待期间采样。
    }
    return true;
}

bool ADemoWeaponAnimationTest::CheckReloadPose(const TCHAR* CaptureName)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 动画数据仅在World游戏线程读取。
    UDemoWeaponAnimationComponent* View = Player ? Player->FindComponentByClass<UDemoWeaponAnimationComponent>() : nullptr; // Pawn归属，不创建替代实例。
    const FDemoWeaponAnimationSet* Set = View ? View->GetAnimationSet() : nullptr; // 只读当前配置，强引用由层类CDO维持。
    if (!Check(Player && View && Set && TestedWeapon.IsValid(),TEXT("live animation configuration available"))) return false;
    if (!CheckReloadPhaseAudit()) return false; // 已等待40%玩法时长，应包含真实引擎UpdateMontage后的同帧测量。
    UAnimInstance* Main = Player->GetMesh1P()->GetAnimInstance(); // 固定主实例，执行真实FPAction Montage。
    UAnimMontage* ExpectedMontage = bExpectedEmpty ? Set->EmptyReload.ArmsMontage : Set->TacticalReload.ArmsMontage; // 由预先冻结版本选择预期资源。
    if (!Check(Main == FixedMainInstance.Get() && Main->GetCurrentActiveMontage() == ExpectedMontage
        && Main->Montage_IsPlaying(ExpectedMontage),TEXT("matching reload Montage plays in fixed main FPAction slot"))) return false;
    if (!Check(View->GetReloadProgress() > .2f && View->GetReloadProgress() < .8f && View->GetReloadSequence() == ReloadSequence
        && View->IsEmptyReload() == bExpectedEmpty && TestedWeapon->GetAmmo() == (bExpectedEmpty ? 0 : 1)
        && TestedWeapon->GetReserveAmmo() == 2,TEXT("mid-animation progress is real and cannot transfer ammo early"))) return false;
    if (!Check(!Player->GetMesh1P()->IsBoneHiddenByName(TEXT("upperarm_l"))
        && FVector::Dist(Player->GetMesh1P()->GetSocketLocation(TEXT("hand_l")),IdleLeftHand) > .5f,
        TEXT("reload unhides support arm and changes actual evaluated hand pose"))) return false;
    USkeletalMeshComponent* Gun = TestedWeapon->FindComponentByClass<USkeletalMeshComponent>(); // 独立枪Skeleton的实际求值组件。
    UAnimSequence* GunSequence = bExpectedEmpty ? Set->EmptyReload.WeaponSequence : Set->TacticalReload.WeaponSequence; // 与预期手臂动作同一配对配置。
    if (!Check(Gun && Gun->GetSingleNodeInstance() && GunSequence
        && FMath::IsNearlyEqual(Gun->GetSingleNodeInstance()->GetCurrentTime(),View->GetReloadProgress() * GunSequence->GetPlayLength(),.05f),
        TEXT("mechanical animation samples same normalized gameplay clock as arms presentation"))) return false;
    bool bMechanicalMoved = false; // 从骨姿势证明枪内零件实际运动，不能仅以播放状态为真作为通过依据。
    for (const TPair<FName,FTransform>& Rest : MechanicalRestPose) // 当前枪全部骨骼的参考值，仅在本步骤读取。
        if (!Gun->GetSocketTransform(Rest.Key,RTS_Component).Equals(Rest.Value,.1f)) bMechanicalMoved = true;
    if (!Check(bMechanicalMoved,TEXT("reload changes evaluated mechanical bone pose"))) return false;
    Capture(FString::Printf(TEXT("%d-%s"),WeaponIndex,CaptureName));
    return true;
}

bool ADemoWeaponAnimationTest::CheckReloadPhaseAudit()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 同步借用当前Pawn；切槽不会替换Pawn上的审计协调器。
    UDemoWeaponAnimationComponent* View = Player ? Player->FindComponentByClass<UDemoWeaponAnimationComponent>() : nullptr; // 清理保留最近一次审计，下次Begin才重置。
    if (!Check(View != nullptr,TEXT("reload phase audit coordinator remains valid"))) return false;
    const FDemoReloadPhaseAudit Audit = View->GetReloadPhaseAudit(); // 纯值快照；读取实际Montage/Gun时钟累计结果，不以期望phase回填作为测量。
    UE_LOG(LogFPSDemo,Display,TEXT("WEAPON_ANIMATION_PHASE_AUDIT weapon=%d seq=%d montageID=%d samples=%d maxErrorSeconds=%.6f"),
        WeaponIndex,Audit.Sequence,Audit.MontageInstanceID,Audit.SampleCount,Audit.MaximumErrorSeconds);
    return Check(Audit.Sequence == ReloadSequence && Audit.MontageInstanceID == ReloadMontageInstanceID
        && Audit.SampleCount > 0 && FMath::IsFinite(Audit.MaximumErrorSeconds)
        && Audit.MaximumErrorSeconds >= 0.f && Audit.MaximumErrorSeconds <= .002f,
        TEXT("current reload has actual same-frame samples and maximum clock error within two milliseconds"));
}

bool ADemoWeaponAnimationTest::CheckClean(int32 ExpectedAmmoValue, int32 ExpectedReserveValue)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 当前Pawn与装备可以不同于被取消的旧枪。
    UDemoWeaponAnimationComponent* View = Player ? Player->FindComponentByClass<UDemoWeaponAnimationComponent>() : nullptr; // 协调器须无旧动作残留。
    ADemoWeaponBase* Weapon = TestedWeapon.Get(); // 始终检查旧的锁定实例，防止切枪后误检查新枪弹药。
    if (!Check(Player && View && Weapon && !Weapon->IsReloading() && !View->IsReloadPresentationActive()
        && !Player->GetDemoASC()->HasMatchingGameplayTag(DemoTags::Reloading)
        && Weapon->GetAmmo() == ExpectedAmmoValue && Weapon->GetReserveAmmo() == ExpectedReserveValue,
        TEXT("reload fully ended and exact finite ammo accounting preserved"))) return false;
    if (!CheckReloadPhaseAudit()) return false; // 8次完成和12次取消均检查保留的最后审计，包括切槽清理后与旧deadline之后。
    if (!Check(Player->GetMesh1P()->GetAnimInstance() == FixedMainInstance.Get()
        && !FixedMainInstance->IsAnyMontagePlaying(),TEXT("cleanup preserves main instance and leaves no playing reload Montage"))) return false;
    USkeletalMeshComponent* Mesh = Weapon->FindComponentByClass<USkeletalMeshComponent>(); // 本枪机械骨架，恢复必须独立于当前激活槽。
    if (!Check(Mesh != nullptr,TEXT("cancelled/completed mechanical mesh still valid"))) return false;
    if (!Check(Mesh->GetSingleNodeInstance() && FMath::IsNearlyZero(Mesh->GetSingleNodeInstance()->GetCurrentTime(),.001f),
        TEXT("mechanical sequence clock resets to zero after completion/cancel"))) return false;
    for (const TPair<FName,FTransform>& Rest : MechanicalRestPose) // 比较组件空间，切枪附着点变化不影响本断言。
    {
        const FTransform Actual = Mesh->GetSocketTransform(Rest.Key,RTS_Component); // 已求值的真实骨姿势，包含缩放/可见性影响。
        if (!Actual.Equals(Rest.Value,.1f) || Mesh->IsBoneHiddenByName(Rest.Key))
        {
            UE_LOG(LogFPSDemo,Error,TEXT("WEAPON_ANIMATION_DIRTY_BONE bone=%s expected=%s actual=%s hidden=%d"),
                *Rest.Key.ToString(),*Rest.Value.ToString(),*Actual.ToString(),Mesh->IsBoneHiddenByName(Rest.Key));
            return Check(false,TEXT("all mechanical parts restored after complete/cancel"));
        }
    }
    TArray<UStaticMeshComponent*> PresentationComponents; // 可选手持弹匣归Pawn，未配置时允许不存在组件。
    Player->GetComponents(PresentationComponents);
    for (UStaticMeshComponent* Presentation : PresentationComponents) // 同步借用全部表现组件，不保存引用到下一帧。
        if (Presentation->GetFName() == TEXT("ReloadHandMagazine")
            && !Check(Presentation->bHiddenInGame,TEXT("optional hand magazine cannot remain visible after cleanup"))) return false;
    ADemoWeaponBase* Active = Player->GetWeaponComponent()->GetActiveWeapon(); // 当前激活枪决定左臂隐藏规则，不沿用旧枪。
    return Check(Active && Player->GetMesh1P()->IsBoneHiddenByName(TEXT("upperarm_l")) == Active->Config.bHideSupportArm,
        TEXT("current gun support-arm visibility restored after cleanup"));
}

void ADemoWeaponAnimationTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    CaptureAnimationFrame(); // 必须放在NextTime早退之前，覆盖中段断言前后的完整动画，而非只录等待起点。
    if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds() < 240.f,TEXT("bounded animation regression runtime"))) return;
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 只借用当前步骤的World玩家。
    UDemoWeaponComponent* Equipment = Player ? Player->GetWeaponComponent() : nullptr; // 真实装备/GAS权限入口，不模拟内部完成回调。
    switch (Step)
    {
    case 0:
        if (!InitializeFixture() || !SelectCurrentWeapon()) return;
        Advance(1,.5f);
        break;
    case 1:
        if (!CheckEquipped(true)) return;
        CaptureMechanicalRestPose();
        Advance(2,.25f);
        break;
    case 2:
        if (!StartReload(false)) return;
        Advance(3,ReloadDuration * .4f);
        break;
    case 3:
        if (!CheckReloadPose(TEXT("Tactical"))) return;
        Advance(4,ReloadDuration * .6f + .2f);
        break;
    case 4:
        if (!CheckClean(ExpectedAmmo,0)) return;
        // 唯一结算入口即便被误调用也必须拒绝已经完成的动作；不把手工完成当作成功路径。
        if (!Check(!TestedWeapon->CompleteReload() && TestedWeapon->GetAmmo() == ExpectedAmmo,
            TEXT("completed reload cannot transfer a second time"))) return;
        if (!StartReload(true)) return;
        Advance(5,ReloadDuration * .4f);
        break;
    case 5:
        if (!CheckReloadPose(TEXT("Empty"))) return;
        Advance(6,ReloadDuration * .6f + .2f);
        break;
    case 6:
        if (!CheckClean(ExpectedAmmo,0)) return;
        CancelIndex = 0;
        Advance(7,.2f);
        break;
    case 7:
    {
        if (!StartReload(true)) return;
        const float CancelPhases[] = { .1f,.45f,.85f }; // 拔匣前、换匣中、结束前分别走不同取消入口。
        Advance(8,ReloadDuration * CancelPhases[CancelIndex]);
        break;
    }
    case 8:
    {
        if (!Check(Equipment && FixedMainInstance.IsValid() && TestedWeapon.IsValid() && TestedWeapon->IsReloading(),TEXT("cancellation reaches an active real reload"))) return;
        if (!CheckReloadPhaseAudit()) return; // 最早取消已等待10%时长，需在停止Montage前证明至少完成一次真实同帧采样。
        if (CancelIndex == 0) Equipment->CancelActions(); // 普通菜单/阶段入口共用的正式取消路径。
        else if (CancelIndex == 1) FixedMainInstance->Montage_Stop(0.f); // 意外Montage停止必须回调取消GAS，不能继续等计时补弹。
        else if (!Check(Equipment->EquipSlot(WeaponIndex == 0 ? 1 : 2),TEXT("slot switch cancels old reload and links destination layer"))) return;
        const float CancelPhases[] = { .1f,.45f,.85f }; // 等到原deadline之后，检查取消的旧AbilityTask不会补弹。
        Advance(9,ReloadDuration * (1.f - CancelPhases[CancelIndex]) + .3f);
        break;
    }
    case 9:
        if (!CheckClean(0,2)) return;
        if (CancelIndex == 2 && !Check(Equipment && Equipment->EquipSlot(WeaponIndex == 0 ? 2 : 1),TEXT("switch back reuses original gun without reloading ammo"))) return;
        Advance(10,.3f);
        break;
    case 10:
        if (!CheckEquipped(false) || !CheckClean(0,2)) return;
        ++CancelIndex;
        Advance(CancelIndex < 3 ? 7 : 11,.15f);
        break;
    case 11:
        ++WeaponIndex;
        if (WeaponIndex < 4)
        {
            if (!SelectCurrentWeapon()) return;
            Advance(1,.5f);
        }
        else
        {
            UE_LOG(LogFPSDemo,Display,TEXT("DEMO_WEAPON_ANIMATION_SUCCESS: four real linked children, fixed main ASC context, eight reload variants, finite ammo, twelve timed cancellations, evaluated hand motion and mechanical restoration"));
            SetActorTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false,0);
        }
        break;
    default:
        Check(false,TEXT("animation test state must be known"));
        break;
    }
}
