// 职责：在游戏线程适配 Lyra 原生图；沿用武器/瞄准时钟，以近战DA驱动上身Montage及连续连招切换，清理中断，不复制骨骼或替换腿部移动。
#include "Animation/DemoLyraAnimInstance.h"
#include "Characters/DemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h" // 表现只消费服务器时钟，不复制动画逐帧骨骼。
#include "Game/DemoGameState.h" // 模拟代理无Controller，只沿公共阶段决定是否显示已提交的枪械动作。
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "Debug/DemoLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Weapons/Melee/DemoMeleeComponent.h" // Slot仅消费现有预测/复制时钟，不重复启动GAS或权威扫掠。
#include "Animation/AnimSequence.h"
#include "Animation/DemoPlayerAppearance.h" // 每枪可选后坐配置，空值沿原绝对Fire兼容路径。
#include "Animation/DemoPlayerPresentationComponent.h" // Pawn持有的实际Appearance，只在游戏线程同步借用。
#include "Weapons/DemoWeaponComponent.h" // 当前实际武器实例身份，不从槽位或名字猜测枪序号。
#include "Weapons/DemoWeaponBase.h" // 成功射击Sequence/ServerStart/Duration是唯一已提交时钟。

void UDemoLyraAnimInstance::NativeInitializeAnimation()
{
    DEMO_LOG_CALL();
    Super::NativeInitializeAnimation();
    NativeIdleBreakWeight=IdleBreakMotionWeight=IdleBreakRearmRemaining=0.f; bAllowNativeIdleBreak=false; bIdleBreakWaitForNeutral=false; IdleBreakWeapon=NAME_None; bHasIdleAim=false; // Avatar/图重建清空自然待机历史及原生资格。
    ActiveLayer=nullptr; GroundDistance=0.f; MissingFlags.Empty(); LastDashStartTime=-100.f; CurrentDashMontage=nullptr;
    CurrentMeleeMontage=nullptr; MeleeMontageSerial=0; MeleeMontageSource=nullptr; MeleeMontageWeapon=NAME_None;
    CurrentGunRecoilMontage=nullptr; GunRecoilMontageInstanceID=INDEX_NONE; GunRecoilMontageWeight=0.f; GunRecoilMontageStartCount=0;
    GunRecoilConsumedWeapon.Reset(); GunRecoilConsumedSequence=0; GunRecoilConsumedSequences.Empty(); GunRecoilMontageWeapon.Reset(); bGunRecoilStopping=false; // 不跨Avatar消费旧成功枪或保留旧Slot权重。
#if WITH_EDITOR
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && (FParse::Param(FCommandLine::Get(),TEXT("DemoSOMAReferenceIdle")) || FParse::Param(FCommandLine::Get(),TEXT("DemoSOMAFPAdapt"))))
    {
        bMeleeUpperBodyMontageFallback=true; bUseMeleeUpperBodyMontage=true; // 旧Editor候选保持强制行为；正式武器由DA显式开启，不依赖命令行参数。
        UE_LOG(LogFPSDemo,Log,TEXT("SOMA_CANDIDATE_UPPER_BODY_MONTAGE enabled=1"));
    }
    // Editor对照参数可强制新/旧策略；普通运行使用类默认配置，不改主图PropertyAccess类身份或冻结资产。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderCandidate"))) bPreserveLocomotionSpine=true;
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderLegacy"))) bPreserveLocomotionSpine=false;
    MotionIsolationMode=NAME_None;
    FString Isolation; // 仅专属测试入口接受有限模式，普通试玩不改变生产混合。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerAnimationTest")) && FParse::Value(FCommandLine::Get(),TEXT("DemoMotionIsolation="),Isolation))
    {
        if (Isolation==TEXT("Current") || Isolation==TEXT("NoAim") || Isolation==TEXT("Native") || Isolation==TEXT("Ready")) MotionIsolationMode=FName(*Isolation);
        else UE_LOG(LogFPSDemo,Warning,TEXT("MOTION_ISOLATION_REJECT unknown mode=%s"),*Isolation);
        UE_LOG(LogFPSDemo,Log,TEXT("MOTION_ISOLATION mode=%s"),*MotionIsolationMode.ToString());
    }
    if (MotionIsolationMode==TEXT("Ready"))
    {
        // 仅显式候选进程选择独立Linked Layer资产；主图身份保持不变，避免PropertyAccess类契约失效。
        bUseWeaponReadyLocomotion=true;
        for (const FName Id:{FName(TEXT("pistol")),FName(TEXT("rifle")),FName(TEXT("shotgun")),FName(TEXT("sniper"))})
        {
            const FString Path=FString::Printf(TEXT("/Game/Player/LyraReady/ABP_Ready_%s.ABP_Ready_%s_C"),*Id.ToString(),*Id.ToString());
            UClass* Candidate=LoadClass<UAnimInstance>(nullptr,*Path); // 游戏线程同步装入隔离候选，不在动画工作线程加载。
            if (Candidate) WeaponLayers.Add(Id,Candidate);
            else UE_LOG(LogFPSDemo,Error,TEXT("MOTION_READY_LAYER_MISSING %s"),*Path);
        }
    }
#endif
    UE_LOG(LogFPSDemo,Log,TEXT("LYRA_SPINE_POLICY preserve=%d class=%s"),bPreserveLocomotionSpine,*GetClass()->GetName());
}

void UDemoLyraAnimInstance::PublishFlag(FName PropertyName,bool Value)
{
    DEMO_LOG_TICK();
    FBoolProperty* Property=FindFProperty<FBoolProperty>(GetClass(),PropertyName); // 当前生成类拥有反射属性；只在游戏线程访问。
    if(Property) Property->SetPropertyValue_InContainer(this,Value);
    else if(!MissingFlags.Contains(PropertyName))
    { MissingFlags.Add(PropertyName); UE_LOG(LogFPSDemo,Warning,TEXT("LYRA_FLAG_MISSING class=%s property=%s"),*GetClass()->GetName(),*PropertyName.ToString()); }
}

void UDemoLyraAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::NativeUpdateAnimation(DeltaSeconds);
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(TryGetPawnOwner()); // 本帧借用，预览无Pawn时不访问World。
    UpdateMeleeMontage(); // Super先发布本帧DA策略/作者phase；无Pawn/无Appearance也撤销旧Slot，不累积另一套播放时间。
    UpdateGunRecoilMontage(); // Body唯一开火增量；显式phase→同帧MG求值快照→机械，不播放旧绝对Fire。
    if(!Pawn) return;
    const UCharacterMovementComponent* Move=Pawn->GetCharacterMovement(); // Character拥有，仅读真实物理状态。
    GroundDistance=0.f;
    if(Move && !Move->IsMovingOnGround())
    {
        const FVector Start=Pawn->GetActorLocation(); // 胶囊中心的世界厘米。
        FHitResult Hit; // 本次向下地面查询结果，不跨线程共享。
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DemoLyraGround),false,Pawn);
        const float HalfHeight=Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); // 从中心距离扣除真实胶囊半高。
        GroundDistance=GetWorld()->LineTraceSingleByChannel(Hit,Start,Start-FVector(0,0,100000.f),ECC_Visibility,Params)?FMath::Max(0.f,Hit.Distance-HalfHeight):100000.f;
    }
    // 操作权重仍由原状态维护；WeaponAnchor在保留模式下先恢复原生腰背，再让上身层仅改变武器相关分支。
    ActionOverlayWeight=UpperBodyWeight;
    bDirectionalLocomotion=true;
    PublishFlag(TEXT("GameplayTag_IsDashing"),bSprinting);
    PublishFlag(TEXT("GameplayTag_IsFiring"),UpperBodyAction==TEXT("Fire") || UpperBodyAction==TEXT("Bolt"));
    PublishFlag(TEXT("GameplayTag_IsReloading"),UpperBodyAction==TEXT("Reload") || UpperBodyAction==TEXT("EmptyReload"));
    const UDemoMeleeComponent* Melee=Pawn->FindComponentByClass<UDemoMeleeComponent>(); // 当前Pawn组件只在游戏线程借用，稳定WeaponId可任意配置。
    PublishFlag(TEXT("GameplayTag_IsMelee"),Melee && Melee->IsEquipped() && Melee->GetDefinition() && !Pawn->IsCoopEliminated()); // 装备真值而非硬编码名；新刀long_grip_blade与旧棒球棍均正确发布。
    // 身体±45度及移动朝相机仍由DemoCharacter负责，避免原图再累计一份ActorYaw抵消。
    PublishFlag(TEXT("bEnableRootYawOffset"),false);
    PublishFlag(TEXT("EnableControlRig"),false);
    PublishFlag(TEXT("UseFootPlacement"),false);
    const bool bReady=bUseWeaponReadyLocomotion && bAimWeaponActive && bCalibratedBoreAim && !bSprinting; // 近战/死亡/闪避保留原生全身表现。
    if (bReady!=bWeaponReadyLocomotion) UE_LOG(LogFPSDemo,Log,TEXT("LYRA_WEAPON_READY active=%d weapon=%s"),bReady,*GetUpperWeaponId().ToString());
    bWeaponReadyLocomotion=bReady;
    UpdateIdleBreakMotion(DeltaSeconds); // 当前装备/动作已经发布，随后只通过代理把纯值交给同帧骨节点。
#if WITH_EDITOR
    // 单变量对照发生于图求值前；不改源动画、物理位移或原生状态机，隔离结果不能作为持枪验收。
    if (MotionIsolationMode==TEXT("Native")) ActionOverlayWeight=0.f;
    if (MotionIsolationMode==TEXT("Native") || MotionIsolationMode==TEXT("NoAim"))
    { bAimWeaponActive=false; HeadYaw=0.f; }
#endif
}

void UDemoLyraAnimInstance::CancelGunRecoilMontage(const TCHAR* Reason)
{
    DEMO_LOG_TICK();
    if (!CurrentGunRecoilMontage && GunRecoilMontageInstanceID==INDEX_NONE) return;
    // StopGroupByName只遍历IsActive，会遗漏已在淡出的旧实例；这里同时收掉专组所有实例，绝不触及近战/Dash组。
    StopAllMontagesByGroupName(TEXT("DemoGunRecoilGroup"),FMontageBlendSettings(0.f)); // 5.4正式签名，Standard零秒清理，不触发缺失的惯性混合通道。
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_CANCEL owner=%s reason=%s consumed=%d"),*GetNameSafe(TryGetPawnOwner()),Reason,GunRecoilConsumedSequence);
    CurrentGunRecoilMontage=nullptr; GunRecoilMontageInstanceID=INDEX_NONE; GunRecoilMontageWeight=0.f;
    GunRecoilMontageWeapon.Reset(); bGunRecoilStopping=false;
    UpdateMontageEvaluationData(); // 同帧移除旧快照，不追加UpdateWeight或Advance。
}

void UDemoLyraAnimInstance::UpdateGunRecoilMontage()
{
    DEMO_LOG_TICK();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(TryGetPawnOwner()); // 本帧同步借用，不从动画工作线程读取Pawn或网络对象。
    ADemoWeaponBase* Weapon=Pawn && Pawn->GetWeaponComponent()?Pawn->GetWeaponComponent()->GetActiveWeapon():nullptr;
    const UDemoPlayerAppearance* Appearance=Pawn && Pawn->GetPlayerPresentation()?Pawn->GetPlayerPresentation()->GetAppearance():nullptr;
    const FDemoPlayerWeaponPoses* Poses=Appearance && Weapon?Appearance->WeaponPoses.Find(Weapon->GetWeaponId()):nullptr; // 当前Pawn持有配置，函数结束不留裸指针。
    const ADemoGameState* PublicState=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 所有端可读，不能用缺少Controller推断模拟代理不允许显示。
    const bool bPublicPhaseAllowed=PublicState && (PublicState->Phase==EDemoPhase::Combat || PublicState->Phase==EDemoPhase::Intermission); // 仅表现门禁，原Shot仍由服务器唯一提交。
    const bool bLocallyBlocked=Pawn && Pawn->IsLocallyControlled() && !Pawn->CanUseFirearmActions(); // 只有本机检查本地菜单/暂停/技能条件；远端没有Controller也可播放。
    const bool bEquipped=Pawn && Pawn->GetWeaponComponent() && Pawn->GetWeaponComponent()->IsEquipped(Weapon); // 复制库存/近战装备真值，不按武器名字判断。
    const bool bPubliclyBlocked=!bPublicPhaseAllowed || !bEquipped || (Pawn && Pawn->IsCoopEliminated()); // 远端只采用公开阶段、装备与生存状态。
    if (!bUseAdditiveGunRecoil || !GunRecoilMontage || !Pawn || !Weapon || !Poses || Pawn->IsCoopEliminated()
        || bPubliclyBlocked || bLocallyBlocked || Weapon->IsReloading() || UpperBodyAction==TEXT("Bolt"))
    {
        if (Weapon && Pawn && (bPubliclyBlocked || bLocallyBlocked || Weapon->IsReloading())) // 缺Controller不是远端阻塞；仅真实公开/本机阻塞消费，短暂操作混出仍可按原相位续入。
        {
            const int32 CancelledSequence=Weapon->GetShotPresentationState().Sequence; // 权威身份只读，不能退款或修改Gameplay。
            if (CancelledSequence>0) GunRecoilConsumedSequences.Add(TWeakObjectPtr<ADemoWeaponBase>(Weapon),CancelledSequence);
        }
        CancelGunRecoilMontage(TEXT("policy/operation/menu/death")); return;
    }
    if (CurrentGunRecoilMontage && (GunRecoilMontageWeapon.Get()!=Weapon || CurrentGunRecoilMontage!=GunRecoilMontage))
        CancelGunRecoilMontage(TEXT("weapon/config changed"));
    const FDemoBoltCycleState Shot=Weapon->GetShotPresentationState(); // 原子复制凭据；不增加网络字段或更改冷却。
    const AGameStateBase* State=GetWorld()->GetGameState(); // 本机和远端均读同一服务器游戏时钟。
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Elapsed=static_cast<float>(Now-Shot.ServerStartTime); // 秒；尚未到起点时不消费序号，避免对时微差永远漏播。
    if (Elapsed<0.f || Shot.Sequence<=0 || Shot.Duration<=0.f) return;
    const TWeakObjectPtr<ADemoWeaponBase> WeaponKey(Weapon); // 以对象代次而非稳定ID区分同名新库存，Actor销毁不会被本Map保活。
    const int32* Consumed=GunRecoilConsumedSequences.Find(WeaponKey); // 指针只在当前同步栈借用，下面Add前不再访问。
    const bool bNewShot=!Consumed || *Consumed!=Shot.Sequence; // 同一个已提交枪只消费一次，包括切枪后重新装备。
    if (bNewShot)
    {
        GunRecoilConsumedWeapon=Weapon; GunRecoilConsumedSequence=Shot.Sequence; // 播放失败/过期也消费，不能每帧重试或重复后坐。
        GunRecoilConsumedSequences.Add(WeaponKey,Shot.Sequence);
        for (auto Entry=GunRecoilConsumedSequences.CreateIterator();Entry;++Entry) if (!Entry.Key().IsValid()) Entry.RemoveCurrent(); // 库存通常四把；失效弱键及时回收，不跨World无界积累。
        if (UpperBodyAction==TEXT("Fire"))
        {
            const FAnimMontageInstance* Previous=GetMontageInstanceForID(GunRecoilMontageInstanceID); // 包含混出期的真实上一发姿态。
            const bool bRetrigger=Previous && Previous->GetWeight()>KINDA_SMALL_NUMBER;
            if (bRetrigger)
            {
                PreviousUpperBodyAction=TEXT("Fire"); PreviousActionProgress=FMath::Clamp(Previous->GetPosition()/FMath::Max(CurrentGunRecoilMontage->GetPlayLength(),KINDA_SMALL_NUMBER),0.f,1.f); // 机械冻结在上一发实际MG相位。
            }
            const float BlendSeconds=bRetrigger?Poses->RecoilRetriggerBlendSeconds:Poses->RecoilBlendInSeconds; // 游戏秒，不压缩源动作到Gameplay间隔。
            FAlphaBlendArgs Blend(BlendSeconds); Blend.BlendOption=EAlphaBlendOption::Cubic; // 新实例从0跨淡，旧实例保留其实际当前Pose。
            // UE5.4的bStopAllMontages只停止同组；true确保唯一后坐组，不会停止独立近战/Dash/FPAction。
            if (Montage_PlayWithBlendIn(GunRecoilMontage,Blend,1.f,EMontagePlayReturnType::MontageLength,GunRecoilSourceTime,true)<=0.f)
            { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_RECOIL_PLAY_REJECT owner=%s seq=%d asset=%s"),*GetNameSafe(Pawn),Shot.Sequence,*GetNameSafe(GunRecoilMontage)); CancelGunRecoilMontage(TEXT("Montage_Play failed")); return; }
            CurrentGunRecoilMontage=GunRecoilMontage; GunRecoilMontageWeapon=Weapon; bGunRecoilStopping=false;
            FAnimMontageInstance* Started=GetActiveInstanceForMontage(CurrentGunRecoilMontage); // 刚创建的新实例，上一发淡出不共享此ID。
            if (!Started) { CancelGunRecoilMontage(TEXT("new instance unavailable")); return; }
            GunRecoilMontageInstanceID=Started->GetInstanceID(); Started->PushDisableRootMotion();
            Montage_SetPlayRate(CurrentGunRecoilMontage,0.f); ++GunRecoilMontageStartCount; // 权重仍沿引擎Delta更新；仅位置使用已提交服务器phase。
            UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_START owner=%s weapon=%s seq=%d instance=%d source=%.6f retrigger=%d starts=%d"),*GetNameSafe(Pawn),*GetNameSafe(Weapon),Shot.Sequence,GunRecoilMontageInstanceID,GunRecoilSourceTime,bRetrigger,GunRecoilMontageStartCount);
            UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_CLOCK_SYNC owner=%s seq=%d elapsed=%.6f source=%.6f presentation=%.6f policy=sample_current_shot_no_delayed_replay"),
                *GetNameSafe(Pawn),Shot.Sequence,Elapsed,GunRecoilSourceTime,Poses->GetRecoilPresentationSeconds()); // 操作混出期间可能延迟启用；明确采原枪当前时刻，不从0迟播或承诺无表现延迟。
        }
        else UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_SKIP owner=%s seq=%d elapsed=%.4f reason=expired/no active window"),*GetNameSafe(Pawn),Shot.Sequence,Elapsed);
    }
    FAnimMontageInstance* Instance=GetMontageInstanceForID(GunRecoilMontageInstanceID); // 引擎拥有，不能跨帧保存裸指针。
    if (!CurrentGunRecoilMontage || !Instance) { CurrentGunRecoilMontage=nullptr; GunRecoilMontageInstanceID=INDEX_NONE; GunRecoilMontageWeight=0.f; return; }
    if (UpperBodyAction==TEXT("Fire") && !bGunRecoilStopping)
        Instance->SetPosition(GunRecoilSourceTime); // 不独立累加Delta，也不回放隐藏FPAction的时间。
    else if (!bGunRecoilStopping)
    {
        bGunRecoilStopping=true; Instance->Stop(FAlphaBlend(CurrentGunRecoilMontage->BlendOut,Poses->RecoilBlendOutSeconds),false); // 正常结束只启动一次尾部淡出。
        UE_LOG(LogFPSDemo,Log,TEXT("GUN_RECOIL_FINISH owner=%s seq=%d position=%.6f"),*GetNameSafe(Pawn),GunRecoilConsumedSequence,Instance->GetPosition());
    }
    GunRecoilMontageWeight=FMath::Clamp(Instance->GetWeight(),0.f,1.f);
    GunRecoilSourceTime=Instance->GetPosition(); GunRecoilPhase=FMath::Clamp(GunRecoilSourceTime/FMath::Max(CurrentGunRecoilMontage->GetPlayLength(),KINDA_SMALL_NUMBER),0.f,1.f); // 缓存实际源相位供机械/验收，不用固定Hold的UpperBodyTime。
    if (UpperBodyAction==TEXT("Fire")) { ActionProgress=GunRecoilPhase; UpperBodyBlendAlpha=GunRecoilMontageWeight; }
    else if (PreviousUpperBodyAction==TEXT("Fire")) UpperBodyBlendAlpha=1.f-GunRecoilMontageWeight; // 完成回位时身体/机械都沿实际淡出。
    UpdateMontageEvaluationData(); // NativeUpdate晚于引擎初次缓存，更新一次当前快照；不重复推进任何MG/事件。
}

void UDemoLyraAnimInstance::UpdateMeleeMontage()
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=Cast<ADemoCharacter>(TryGetPawnOwner()); // 只在游戏线程借用，预览窗口允许没有角色。
    const UDemoMeleeComponent* Melee=Pawn?Pawn->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 组件持有源序列，AnimInstance同时强引用当前Montage。
    const UDemoMeleeDefinition* Definition=Melee && Melee->IsEquipped()?Melee->GetDefinition():nullptr; // 卸下即禁止沿旧源继续，Definition只在同步栈借用。
    const bool bAttack=bUseMeleeUpperBodyMontage && MeleeAttackSequence && Definition && Melee->IsAttacking() && !Pawn->IsCoopEliminated();
    if (!bAttack)
    {
        if (CurrentMeleeMontage && Montage_IsActive(CurrentMeleeMontage))
        {
            const float Recovery=Definition?Definition->MeleeRecoveryBlendSeconds:.10f; // 当前合法持握回位共用配置；卸下/死亡保原快速清理，不续播伤害或推进源时钟。
            Montage_Stop(Recovery,CurrentMeleeMontage);
            UpdateMontageEvaluationData(); // NativeUpdate晚于引擎本帧初次快照；停止事件同帧可见，不能沿旧ActiveData突然空Slot。
            UE_LOG(LogFPSDemo,Log,TEXT("MELEE_UPPER_MONTAGE_STOP owner=%s recovery_seconds=%.3f"),*GetNameSafe(Pawn),Recovery);
        }
        CurrentMeleeMontage=nullptr; MeleeMontageSource=nullptr; MeleeMontageWeapon=NAME_None; return;
    }
    const uint32 Serial=Melee->GetAttackSerial(); // 预测凭据确认后序号保持，旁观者按已有复制起点恢复相同源时刻。
    FAnimMontageInstance* ExistingInstance=CurrentMeleeMontage?GetActiveInstanceForMontage(CurrentMeleeMontage):nullptr; // 本游戏线程短期借用；不能把已停止/零权重旧实例重新收养为连续表现。
    const bool bContinueSameInstance=Definition->bUseFullContextForComboContinuation && Serial!=MeleeMontageSerial
        && MeleeMontageSource==MeleeAttackSequence && MeleeMontageWeapon==Definition->WeaponId
        && ExistingInstance && Montage_IsActive(CurrentMeleeMontage) && ExistingInstance->GetWeight()>0.f
        && Melee->IsContinuousPoseTransition(false,MeleeMontageSource.Get(),MeleeMontageSerial); // 同组真实连续边且同资源才复用；普通新组即使同资源也必须沿原起招重建。
    if (bContinueSameInstance)
    {
        const uint32 PreviousSerial=MeleeMontageSerial; // 只推进本实例的表现去重记账，不写组件State/GA/服务器或源时间。
        MeleeMontageSerial=Serial;
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_UPPER_MONTAGE_CONTINUE owner=%s weapon=%s previous_serial=%u serial=%u instance=%d source=%s position=%.6f weight=%.6f"),
            *GetNameSafe(Pawn),*MeleeMontageWeapon.ToString(),PreviousSerial,Serial,ExistingInstance->GetInstanceID(),*GetNameSafe(MeleeAttackSequence),MeleeAttackTime,ExistingInstance->GetWeight());
    }
    if (!CurrentMeleeMontage || Serial!=MeleeMontageSerial || MeleeMontageSource!=MeleeAttackSequence || MeleeMontageWeapon!=Definition->WeaponId)
    {
        const bool bContinuous=CurrentMeleeMontage && MeleeMontageWeapon==Definition->WeaponId
            && Melee->IsContinuousPoseTransition(false,MeleeMontageSource.Get(),MeleeMontageSerial); // 仅作者核过的兼容前缀/相邻边，普通起招仍保留原混入。
        if (CurrentMeleeMontage) Montage_Stop(bContinuous?0.f:.04f,CurrentMeleeMontage);
        CurrentMeleeMontage=PlaySlotAnimationAsDynamicMontage(MeleeAttackSequence,TEXT("DemoMeleeUpperBody"),bContinuous?0.f:.04f,.10f,1.f,1,-1.f,MeleeAttackTime);
        MeleeMontageSerial=Serial;
        if (!CurrentMeleeMontage)
        { MeleeMontageSource=nullptr; MeleeMontageWeapon=NAME_None; UE_LOG(LogFPSDemo,Error,TEXT("MELEE_UPPER_MONTAGE_FAILED source=%s"),*GetNameSafe(MeleeAttackSequence)); return; }
        MeleeMontageSource=MeleeAttackSequence; MeleeMontageWeapon=Definition->WeaponId; // 原子发布新Montage身份，重复预测确认不重启。
        CurrentMeleeMontage->bEnableAutoBlendOut=false; // 显式phase由组件拥有；引擎不得在源尾先自动归零，再让正常完成分支失去混出历史。
        if (FAnimMontageInstance* Instance=GetActiveInstanceForMontage(CurrentMeleeMontage))
        {
            Instance->PushDisableRootMotion(); // 局部攻击不得提取源RootMotion驱动胶囊。
            if (bContinuous)
            {
                const float InitialWeight=Instance->GetWeight(); // 新实例尚未经过本帧之前已完成的Montage_UpdateWeight；仅供一次切换诊断。
                // UE5.4 Play仅将零时长Blend标脏，旧实例Stop(0)却已立即归零。NativeUpdate此处补一次零Delta权重初始化，
                // 随后EvaluationData才包含当前Slot；不Advance、不重复游戏Delta，不推进源时间或动画Notify，允许正常BlendedIn事件排队。
                Instance->UpdateWeight(0.f);
                UE_LOG(LogFPSDemo,Log,TEXT("MELEE_UPPER_CONTINUOUS_WEIGHT_INIT owner=%s serial=%u instance=%d before=%.6f after=%.6f"),
                    *GetNameSafe(Pawn),Serial,Instance->GetInstanceID(),InitialWeight,Instance->GetWeight());
            }
        }
        Montage_SetPlayRate(CurrentMeleeMontage,0.f); // UE5.4的权重仍按DeltaSeconds更新；位置由统一作者phase控制，支持非线性PlaybackKeys。
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_UPPER_MONTAGE_START owner=%s weapon=%s serial=%u source=%s position=%.4f continuous=%d"),*GetNameSafe(Pawn),*MeleeMontageWeapon.ToString(),Serial,*GetNameSafe(MeleeAttackSequence),MeleeAttackTime,bContinuous);
    }
    Montage_SetPosition(CurrentMeleeMontage,MeleeAttackTime); // 仅表现同步；声音/命中窗口仍由组件按同一SampleProgress处理。
    // UE5.4 UAnimInstance::UpdateAnimation先执行UpdateMontageEvaluationData，再调用NativeUpdateAnimation。
    // 此处只改Montage实例位置会让AnimGraph仍读取前一帧的求值快照；高速挥击时GetPosition虽同步，实际手骨却落后一帧。
    // 在游戏线程、图任务派发前重建当前快照；除新连续实例的零Delta权重初始化外，不重复权重更新、不Advance或加入DeltaSeconds，保留唯一作者时钟。
    UpdateMontageEvaluationData();
}

void UDemoLyraAnimInstance::RefreshWeaponLayer()
{
    DEMO_LOG_TICK();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(TryGetPawnOwner()); // 同步借用当前Avatar，编辑器预览不动态挂层。
    if(!Pawn) return;
    const TSubclassOf<UAnimInstance>* Configured=WeaponLayers.Find(GetUpperWeaponId()); // 稳定ID查表，不写每枪分支。
    const TSubclassOf<UAnimInstance> Desired=Configured && *Configured?*Configured:DefaultLayer;
    if(Desired && Desired!=ActiveLayer)
    {
        ActiveLayer=Desired; // 链接可能触发初始化回调，先发布身份避免重复进入。
        LinkAnimClassLayers(Desired);
        UE_LOG(LogFPSDemo,Log,TEXT("LYRA_LAYER_LINKED pawn=%s weapon=%s layer=%s"),*Pawn->GetName(),*GetUpperWeaponId().ToString(),*GetNameSafe(Desired));
    }
    UpdateDashPresentation();
}

void UDemoLyraAnimInstance::UpdateDashPresentation()
{
    DEMO_LOG_TICK();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(TryGetPawnOwner()); // 仅游戏线程同步读取Avatar，工作线程不访问网络对象。
    if(!Pawn) return;
    if(Pawn->IsCoopEliminated())
    {
        if(CurrentDashMontage && Montage_IsPlaying(CurrentDashMontage))
        { Montage_Stop(.1f,CurrentDashMontage); UE_LOG(LogFPSDemo,Log,TEXT("LYRA_DASH_STOP eliminated")); }
        return;
    }
    const FDemoDashPresentation& Dash=Pawn->GetDashPresentation(); // Pawn持有的原子复制凭据，不在表现层改写。
    if(Dash.ServerStartTime<=LastDashStartTime) return;
    const AGameStateBase* State=GetWorld()->GetGameState(); // 允许无GameState的离线预览回退World时钟。
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    const float Elapsed=static_cast<float>(Now-Dash.ServerStartTime); // 秒；乱序时钟尚未到起点则保留凭据等下一帧。
    if(Elapsed<0.f) return;
    LastDashStartTime=Dash.ServerStartTime;
    if(Elapsed>=DashPresentationSeconds)
    { UE_LOG(LogFPSDemo,Log,TEXT("LYRA_DASH_SKIP expired elapsed=%.3f"),Elapsed); return; }
    const float Yaw=FMath::UnwindDegrees(Dash.LocalDirectionDegrees); // 与原Lyra SelectDirectionalMontage相同严格边界。
    const int32 Index=FMath::Abs(Yaw)<45.f?0:FMath::Abs(Yaw)>135.f?2:Yaw>0.f?1:3;
    if(!DashMontages.IsValidIndex(Index) || !DashMontages[Index])
    { UE_LOG(LogFPSDemo,Warning,TEXT("LYRA_DASH_MONTAGE_MISSING index=%d"),Index); return; }
    CurrentDashMontage=DashMontages[Index];
    const float Rate=CurrentDashMontage->GetPlayLength()/FMath::Max(.2f,DashPresentationSeconds); // 全动作压缩到既有表现窗口，不裁切尾部。
    const float Played=Montage_Play(CurrentDashMontage,Rate,EMontagePlayReturnType::MontageLength,Elapsed*Rate,false);
    UE_LOG(LogFPSDemo,Log,TEXT("LYRA_DASH_PLAY pawn=%s montage=%s yaw=%.2f elapsed=%.3f rate=%.3f result=%.3f"),*Pawn->GetName(),*GetNameSafe(CurrentDashMontage),Yaw,Elapsed,Rate,Played);
}
