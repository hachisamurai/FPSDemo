// 职责：近战GAS权威、数据相位与双视角表现；已承诺连招短暂续采只填补表现边界。预测在DemoMeleePrediction.cpp维护，伤害只读服务器State/窗口/TP轨迹。
// Editor隔离审片须同时指定DemoMeleeTest及固定修订标志；R05胸腰/R06尾段/R07本体避让候选不改变正常入口或非Editor包体。
// 旧canonical衔接A/B仅-game Standalone GameWorld及DemoMeleeComboRegression+DemoMeleeLegacyContinuationAB双旗标启用；私有闸不修改DA/时钟/权威状态。
// 新恢复候选仅Editor -game/Standalone/GameWorld及DemoMeleeTest+DemoMeleeRecoveryR02，固定白名单；本地Idle epoch只用于显式表现，不复制。
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Weapons/Melee/DemoMeleeAbility.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Combat/DemoDamageableTarget.h" // 设施通过自己的GAS生命受伤，不依赖Enemy继承或奖励事件。
#include "GAS/Ammo/DemoAmmoEffects.h" // 在真实近战扣血前冻结Frozen事实，不能在GE后才猜测是否满足冰裂。
#include "Progression/Builds/DemoRunBuildComponent.h" // 只将实际命中事实交给PS核心模块，武器不拥有核心计数/分支。
#include "Combat/Feedback/DemoHitFeedbackComponent.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyKnockbackComponent.h" // 目标负责受击位移与碰撞，武器只提交服务器确认的招式参数。
#include "Audio/DemoWeaponAudio.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Misc/CommandLine.h" // 隔离姿态诊断只允许Editor白名单候选，不进入包体。
#include "Misc/Parse.h"
#endif

UDemoMeleeComponent::UDemoMeleeComponent()
{
    DEMO_LOG_CALL(); SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    // 用户已确认正式迁移长柄双手刀；定义承载模型、单招及上身层开关，旧棒球棍DA保持可回退。
    DefinitionAsset=TSoftObjectPtr<UDemoMeleeDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Melee/DA_LongGripBlade.DA_LongGripBlade")));
}
void UDemoMeleeComponent::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    Definition=DefinitionAsset.LoadSynchronous();
#if WITH_EDITOR
    bEditorDisableFullContextForProbe=false; // 新Avatar默认真实资产策略；不继承上次试玩/对象重用的诊断选择。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeLegacyContinuationAB")))
    {
        const bool bIsolatedProbe=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeComboRegression")) && FParse::Param(FCommandLine::Get(),TEXT("game"))
            && GetWorld() && GetWorld()->WorldType==EWorldType::Game && GetWorld()->GetNetMode()==NM_Standalone; // 只读本次真实运行环境；PIE、网络和普通Editor均不开放闸。
        if (bIsolatedProbe)
        {
            bEditorDisableFullContextForProbe=true;
            UE_LOG(LogFPSDemo,Log,TEXT("MELEE_FULL_CONTEXT_PROBE_DISABLED owner=%s reason=explicit_isolated_LegacyContinuationAB_same_DA_assets_and_gameplay_clock"),*GetNameSafe(GetOwner())); // 每组件BeginPlay一次，不逐帧输出诊断模式。
        }
        else UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_FULL_CONTEXT_PROBE_REJECT owner=%s reason=requires_DemoMeleeComboRegression_game_Standalone_GameWorld"),*GetNameSafe(GetOwner())); // 不满足隔离门时保默认false，不能影响生产或网络表现。
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeRecoveryR02")))
    {
        const bool bRecoveryProbe=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("game"))
            && GetWorld() && GetWorld()->WorldType==EWorldType::Game && GetWorld()->GetNetMode()==NM_Standalone; // 仅隔离Editor真实游戏进程，不开放PIE/Listen/非Editor或任意包路径。
        if (bRecoveryProbe)
        {
            const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/NativeIdleAim20261115/RecoveryR02/DA_MeleeRecovery_R02.DA_MeleeRecovery_R02"); // Root真实Editor创建的独立候选，找不到时原Validate拒绝，不静默回退冒充验收。
            Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
            UE_LOG(LogFPSDemo,Log,TEXT("MELEE_RECOVERY_R02_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
        }
        else UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RECOVERY_R02_REJECT reason=requires_DemoMeleeTest_game_Standalone_GameWorld")); // 不满足门时保正式默认，并阻止误落其它候选分支。
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladeClearanceR07")))
    {
        // Editor只加载本次固定本体避让候选，不接受任意路径，不修改当前正式DA或复制/伤害/声音逻辑。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeClearanceR07/DA_BladeClearanceR07.DA_BladeClearanceR07"); // 项目内精确审片白名单。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BLADE_CLEARANCE_R07_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladeTorsoR06")))
    {
        // 仅本次尾段连续回收独立候选；固定DA白名单，不接受任意路径，不写入正式刀或改变伤害/声音。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR06/DA_BladeTorsoR06.DA_BladeTorsoR06"); // 只在Editor显式测试时加载，包体不编译此分支。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BLADE_TORSO_R06_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladeTorsoR05")))
    {
        // 只允许当前胸腰/肩臂独立候选；不能由命令行注入任意DA，也不覆盖正式刀定义或既有R04候选。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/MannyBladeTorsoR05/DA_BladeTorsoR05.DA_BladeTorsoR05"); // 固定Editor白名单。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BLADE_TORSO_R05_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeBladePostureR04")))
    {
        // 固定R04候选仅供Editor实际移动/骨姿审片，默认正式DA及包体入口保持不变。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/MannyBladePostureR04/DA_BladePostureR04.DA_BladePostureR04");
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BLADE_POSTURE_R04_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoSOMAFPAdapt")))
    {
        // 仅显式Editor审片启用离线FP肩肘派生；TP源、伤害时钟与正式DA不替换。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,TEXT("/Game/Weapons/Melee/Candidates/SOMAFirstPersonAdapt20261030/DA_SOMA_FPAdaptR03.DA_SOMA_FPAdaptR03"));
        UE_LOG(LogFPSDemo,Log,TEXT("SOMA_FP_ADAPT_R03_CANDIDATE loaded=%d"),Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoSOMAReferenceIdle")))
    {
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,TEXT("/Game/Weapons/Melee/Candidates/SOMAReferenceIdle20261030/DA_SOMA_RigidFPPreview.DA_SOMA_RigidFPPreview")); // 仅Editor测试白名单，正式DA/伤害数值不替换。
        UE_LOG(LogFPSDemo,Log,TEXT("SOMA_RIGID_FP_CANDIDATE loaded=%d"),Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeAnatomyCandidate")))
    {
        // 034使用用户接受的新01和独立左右横扫；仅Editor隔离夹具，正式DA及此前否决候选不变。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/AnatomySwing20261028/DA_AnatomyPreview.DA_AnatomyPreview"); // 显式白名单，不接受任意路径。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_ANATOMY_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeCoordinatedCandidate")))
    {
        // 033保持旧版肘腕路径后独立修订接触/随挥；仅Editor显式夹具加载，未经用户验收不提升正式DA。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/CoordinatedSwing20261027/DA_PowerSwingPreview.DA_PowerSwingPreview"); // 固定白名单，与旧候选和正常运行资源隔离。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_COORDINATED_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeParentCandidate")))
    {
        // 032只在Editor隔离World验证完整父级驱动横扫，不改正式DA或旧031故障复现入口。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/ParentDrivenSwing20261027/DA_PowerSwingPreview.DA_PowerSwingPreview"); // 固定白名单候选，两手/骨长/镜头与正式配置隔离。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_PARENT_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleePowerCandidate")))
    {
        // 031独立审片修订：只有显式Editor夹具加载，旧候选与正式配置继续可回退。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/ApexPowerSwing20261027/DA_PowerSwingPreview.DA_PowerSwingPreview"); // 固定候选白名单，无任意路径注入。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_POWER_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeRelaxedCandidate")))
    {
        // 仅隔离验收World启用；实际GAS/FP图播放新循环和完整左右攻击，正式DA与枪械手臂不变。
        const TCHAR* CandidatePath=TEXT("/Game/Weapons/Melee/Candidates/RelaxedMotion20261026/DA_RelaxedMotionPreview.DA_RelaxedMotionPreview"); // 白名单候选，不接受命令行任意资源。
        Definition=LoadObject<UDemoMeleeDefinition>(nullptr,CandidatePath);
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RELAXED_CANDIDATE asset=%s loaded=%d"),CandidatePath,Definition!=nullptr);
    }
    else if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeRigAudit")))
    {
        FString PoseId; // 只选择静态验收白名单，IdleRelaxed为01修订；不接受任意资源路径。
        FParse::Value(FCommandLine::Get(),TEXT("DemoMeleeAuditPose="),PoseId);
        if (PoseId==TEXT("Baseline") || PoseId==TEXT("01") || PoseId==TEXT("03") || PoseId==TEXT("07") || PoseId==TEXT("IdleRelaxed"))
        {
            const FString Path=PoseId==TEXT("IdleRelaxed")
                ? TEXT("/Game/Weapons/Melee/Candidates/IdleRelaxed20261026/DA_PoseIdleRelaxed.DA_PoseIdleRelaxed")
                : FString::Printf(TEXT("/Game/Weapons/Melee/Candidates/GripFix20261026/DA_Pose%s.DA_Pose%s"),*PoseId,*PoseId); // 候选DA只在当前临时World加载，保留旧01供同镜头对比。
            Definition=LoadObject<UDemoMeleeDefinition>(nullptr,*Path);
            UE_LOG(LogFPSDemo,Display,TEXT("MELEE_RIG_AUDIT_CONFIG pose=%s asset=%s"),*PoseId,*Path);
        }
        else { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_RIG_AUDIT invalid pose ID")); Definition=nullptr; }
    }
#endif
    FString Error; // 配置错误只使近战不可用，禁止拿错误资产冒充成功。
    if (!Definition || !Definition->Validate(Error)) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_UNAVAILABLE %s"),*Error); Definition=nullptr; return; }
    if (Character()) AddTickPrerequisiteComponent(Character()->GetMesh()); // TP真实骨已求值后才更新附属表现。
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_READY owner=%s definition=%s attacks=%d"),*GetNameSafe(GetOwner()),*Definition->WeaponId.ToString(),Definition->Attacks.Num());
}
ADemoCharacter* UDemoMeleeComponent::Character() const { DEMO_LOG_TICK(); return Cast<ADemoCharacter>(GetOwner()); }
UDemoMeleeDefinition* UDemoMeleeComponent::GetDefinition() const { DEMO_LOG_TICK(); return Definition; }
USkeletalMeshComponent* UDemoMeleeComponent::GetFirstPersonArmsMesh() const { DEMO_LOG_TICK(); return FirstPersonArms; }
UStaticMeshComponent* UDemoMeleeComponent::GetFirstPersonWeaponMesh() const { DEMO_LOG_TICK(); return FirstPersonBat; }
UStaticMeshComponent* UDemoMeleeComponent::GetThirdPersonWeaponMesh() const { DEMO_LOG_TICK(); return ThirdPersonBat; }
void UDemoMeleeComponent::RefreshOwnerShadowPolicy()
{
    DEMO_LOG_TICK();
    if (!ThirdPersonBat) return;
    const ADemoCharacter* Pawn=Character(); // 只借用本组件当前Avatar，不跨Pawn保留可见授权。
    const bool bShow=Pawn && State.bEquipped && !Pawn->IsCoopEliminated(); // 原装备/生命真值，不以FP是否已创建猜测。
    const bool bProxy=Pawn && Pawn->GetPlayerPresentation() && Pawn->GetPlayerPresentation()->IsOwnerShadowProxyActive();
    const bool bHiddenShadow=bShow && !bProxy; // 外部观察者仍看真实TP刀，仅撤销本人原隐藏投影。
    const bool bChanged=ThirdPersonBat->CastShadow!=bShow || ThirdPersonBat->bCastHiddenShadow!=bHiddenShadow;
    ThirdPersonBat->SetCastShadow(bShow);
    ThirdPersonBat->SetCastHiddenShadow(bHiddenShadow);
    if (bChanged) UE_LOG(LogFPSDemo,Log,TEXT("MELEE_TP_SHADOW owner=%s equipped=%d eliminated=%d proxy=%d cast=%d hidden_shadow=%d mesh=%s"),
        *GetNameSafe(Pawn),State.bEquipped,Pawn?Pawn->IsCoopEliminated():true,bProxy,ThirdPersonBat->CastShadow,ThirdPersonBat->bCastHiddenShadow,*GetNameSafe(ThirdPersonBat->GetStaticMesh()));
}
bool UDemoMeleeComponent::IsEquipped() const { DEMO_LOG_TICK(); return Definition && State.bEquipped; }
uint32 UDemoMeleeComponent::GetAttackSerial() const { DEMO_LOG_TICK(); return PresentationState().Serial; } // 拥有者确认不换序号，避免同一预测动作被重新混合。
float UDemoMeleeComponent::SegmentEndPhase(const FDemoMeleeState& Snapshot) const
{
    DEMO_LOG_TICK();
    return Definition && Definition->Attacks.IsValidIndex(Snapshot.Attack) && Snapshot.bComboExitCommitted
        ?Definition->Attacks[Snapshot.Attack].ComboEndPhase:1.f;
}
double UDemoMeleeComponent::SegmentEndTime(const FDemoMeleeState& Snapshot) const
{
    DEMO_LOG_TICK();
    return Snapshot.StartTime+(Definition && Definition->Attacks.IsValidIndex(Snapshot.Attack)
        ?Definition->Attacks[Snapshot.Attack].RuntimeAtPhase(SegmentEndPhase(Snapshot)):0.f);
}
float UDemoMeleeComponent::GetAbilityWaitSeconds() const
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || !Definition->Attacks.IsValidIndex(State.Attack)) return 0.f;
    const FDemoMeleeAttack& Attack=Definition->Attacks[State.Attack]; // GA只读本次权威刀，不使用客户端表现起点。
    const double Now=ServerTime(),End=SegmentEndTime(State); // 截止从同一作者时轴推导，不累加任务耗时。
    const double Branch=State.StartTime+Attack.RuntimeAtPhase(Attack.ComboBranchPhase);
    const double Wake=State.bPrimarySequence && !Attack.ComboTargetId.IsNone() && Now+1.e-6<Branch?FMath::Min(Branch,End):End;
    return static_cast<float>(FMath::Max(0.,Wake-Now));
}
bool UDemoMeleeComponent::IsAttackSegmentComplete() const
{
    DEMO_LOG_TICK();
    return !Definition || !Definition->Attacks.IsValidIndex(State.Attack) || ServerTime()+1.e-6>=SegmentEndTime(State);
}
bool UDemoMeleeComponent::CanAdvanceCombo() const
{
    DEMO_LOG_TICK();
    if (!IsAttackContextValid() || !State.bComboExitCommitted || !State.bPrimarySequence || !bBufferedCombo || BufferedInput!=0
        || BufferedSourceSerial!=State.Serial || !Definition->Attacks.IsValidIndex(BufferedTargetAttack) || !IsAttackSegmentComplete()) return false;
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>(); // 接受与消费必须同阶段/关号。
    const FDemoMeleeAttack& Attack=Definition->Attacks[State.Attack];
    if (ServerTime()>BufferedExpires) UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_ADVANCE_REJECT expired request=%u late_seconds=%.6f"),BufferedRequestId,ServerTime()-BufferedExpires);
    return GameState && GameState->Phase==BufferedPhase && GameState->LevelNumber==BufferedLevel && ServerTime()<=BufferedExpires
        && Definition->Attacks[BufferedTargetAttack].Id==Attack.ComboTargetId
        && ResolveInputAttack(0,State.NextHorizontalCursor)==BufferedTargetAttack;
}
bool UDemoMeleeComponent::AdvanceCombo()
{
    DEMO_LOG_CALL();
    if (!CanAdvanceCombo()) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_ADVANCE_REJECT context/queue/boundary")); return false; }
    const FDemoMeleeState Parent=State; // 补扫可能同步死亡/清关，后续凭据须重新校验。
    const FDemoMeleeAttack& Previous=Definition->Attacks[Parent.Attack];
    const float From=FMath::Max(PreviousProgress,static_cast<float>(FrozenHitWindow.X));
    if (From<FrozenHitWindow.Y) SweepAttack(From,FrozenHitWindow.Y);
    if (State.Serial!=Parent.Serial || !CanAdvanceCombo()) return false;
    const double PlannedStart=SegmentEndTime(Parent); // 长帧保留计划边界，下一刀从Entry补到当下。
    const int32 Target=BufferedTargetAttack;
    const uint32 Request=BufferedRequestId;
    const float Entry=Previous.ComboEntryPhase;
    FVector2D NextWindow; FString WindowError; // 旧刀补扫完成后才冻结目标窗口；失败保持原状态交GA结束，不跨桥产生伤害。
    if (!Definition->Attacks[Target].ResolveHitWindow(NextWindow,WindowError))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_COMBO_ADVANCE_REJECT window=%s"),*WindowError); return false; }
    ClearBufferedInput(); // 先消费唯一槽，不继承同一次旧按键。
    State.Attack=Target; ++State.Serial; State.OwnerRequestId=Request;
    State.StartTime=PlannedStart-Definition->Attacks[Target].RuntimeAtPhase(Entry);
    State.bPrimarySequence=true; State.bComboExitCommitted=false; State.bStartedFromCombo=true;
    State.ComboSourceAttack=Parent.Attack; State.EntryPhase=Entry;
    HorizontalCursor=(HorizontalCursor+1)%Definition->HorizontalAttackSequence.Num(); State.NextHorizontalCursor=HorizontalCursor;
    NextAttackTime=SegmentEndTime(State); PreviousProgress=Entry; FrozenHitWindow=NextWindow;
    FrozenDamage=Definition->Attacks[Target].Damage+Character()->GetDemoAttributes()->GetWeaponDamageBonus();
    HitTargets.Reset(); bWorldImpactPlayed=false;
    Character()->SetActorRotation(FRotator(0,Character()->GetBaseAimRotation().Yaw,0));
    OnRep_State(); GetOwner()->ForceNetUpdate();
    if (Request!=0) ClientAttackResult(Request,true,State,NextAttackTime); // 此处才确认后继真正开始。
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_STARTED serial=%u request=%u target=%s entry=%.6f late_seconds=%.6f"),
        State.Serial,Request,*Definition->Attacks[Target].Id.ToString(),Entry,FMath::Max(0.,ServerTime()-PlannedStart));
    return true;
}
bool UDemoMeleeComponent::IsContinuousPoseTransition(bool bFirstPerson,const UAnimSequence* Previous,uint32 PreviousSerial) const
{
    DEMO_LOG_TICK();
    const FDemoMeleeState& Visual=PresentationState(); // 仅选择表现，不改变服务器伤害。
    if (!Previous || !Definition || !Definition->Attacks.IsValidIndex(Visual.Attack)) return false;
    const FDemoMeleeAttack& Attack=Definition->Attacks[Visual.Attack];
    if (Visual.bComboExitCommitted && Visual.Serial==PreviousSerial)
    {
        if (Previous==(bFirstPerson?Attack.FirstPerson.Get():Attack.ThirdPerson.Get())) return true;
        float ParentContextTime=0.f; // 仅用当前真实快照冻结Entry核父源资格，不伪造/推进State或业务时间；秒输出不用于改播放位置。
        return Previous==GetFullContextComboPose(Visual,bFirstPerson,Visual.EntryPhase,ParentContextTime); // 子刀可正读合法父上下文；排入下一刀后允许无冻结混合切到自己的同姿前缀。
    }
    if (!Visual.bStartedFromCombo || PreviousSerial==0 || Visual.Serial!=PreviousSerial+1 || !Definition->Attacks.IsValidIndex(Visual.ComboSourceAttack)) return false;
    const FDemoMeleeAttack& Parent=Definition->Attacks[Visual.ComboSourceAttack];
    return Parent.ComboTargetId==Attack.Id && Previous==(bFirstPerson?Parent.ComboFirstPerson.Get():Parent.ComboThirdPerson.Get());
}
double UDemoMeleeComponent::ServerTime() const
{
    DEMO_LOG_TICK();
    const AGameStateBase* GameState=GetWorld()?GetWorld()->GetGameState():nullptr; // 本World同步时钟只在游戏线程读取。
    return GameState?GameState->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0.;
}
bool UDemoMeleeComponent::IsAttacking() const
{
    DEMO_LOG_TICK();
    const FDemoMeleeState& Visual=PresentationState(); // 这里只服务动画/输入，权威IsAttackContextValid仍只读State。
    if (!IsEquipped() || !Definition->Attacks.IsValidIndex(Visual.Attack)) return false;
    const double Now=PresentationTime(); // 一次取同端时钟，避免姿态查询与Slot门禁用不同的边界判断。
    if (Now<SegmentEndTime(Visual)) return true;
    float Time=0.f,Progress=0.f; // 只探测表现补位资格，不推进行为或修改当前State。
    return GetCommittedComboContinuation(Visual,Now,false,Time,Progress);
}

bool UDemoMeleeComponent::GetCommittedComboContinuation(const FDemoMeleeState& Snapshot,double Now,bool bFirstPerson,float& Time,float& Progress) const
{
    DEMO_LOG_TICK(); Time=0.f; Progress=0.f;
    const ADemoCharacter* Pawn=Character(); // 模拟代理没有Controller；不能以本机输入门禁误拒所有远端表现。
    const ADemoGameState* GameState=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr;
    if (!Pawn || !IsEquipped() || !Definition->Attacks.IsValidIndex(Snapshot.Attack) || !Snapshot.bComboExitCommitted || !Snapshot.bPrimarySequence
        || Pawn->IsCoopEliminated() || !Pawn->GetDemoAttributes() || Pawn->GetDemoAttributes()->GetHealth()<=0.f || !GameState || GetWorld()->IsPaused()
        || (GameState->Phase!=EDemoPhase::Combat && GameState->Phase!=EDemoPhase::Intermission && GameState->Phase!=EDemoPhase::Hub)) return false;
    if (Pawn->HasAuthority() && !IsAttackContextValid()) return false; // 服务器保原会话/阶段/关号/菜单授权；此函数永不延长GE窗口。
    if (Pawn->IsLocallyControlled() && (!Pawn->CanUsePlayerSkills() || (bUsePrediction && (bPredictionCancelled
        || GameState->Phase!=PredictedPhase || GameState->LevelNumber!=PredictedLevel)))) return false;
    const double Boundary=SegmentEndTime(Snapshot); // 原计划交接时刻，不能由当前帧重新起算。
    if (Now<Boundary || Now>Boundary+ComboPresentationGraceSeconds) return false;
    const FDemoMeleeAttack& Parent=Definition->Attacks[Snapshot.Attack];
    const int32 TargetIndex=ResolveInputAttack(0,Snapshot.NextHorizontalCursor); // 仅从已验证主循环找后继，不接受网络指定任意动画。
    if (!Definition->Attacks.IsValidIndex(TargetIndex) || Definition->Attacks[TargetIndex].Id!=Parent.ComboTargetId) return false;
    const FDemoMeleeAttack& Target=Definition->Attacks[TargetIndex];
    const UAnimSequence* Canonical=bFirstPerson?Parent.FirstPerson.Get():Parent.ThirdPerson.Get();
    const UAnimSequence* Continuation=bFirstPerson?Parent.ComboFirstPerson.Get():Parent.ComboThirdPerson.Get();
    const UAnimSequence* TargetCanonical=bFirstPerson?Target.FirstPerson.Get():Target.ThirdPerson.Get();
    if (!Canonical || !Continuation || !TargetCanonical) return false;
    const float TargetPhase=FMath::Max(Parent.ComboEntryPhase,Target.SampleProgress(Target.RuntimeAtPhase(Parent.ComboEntryPhase)+static_cast<float>(Now-Boundary)));
    Time=FMath::Min(Continuation->GetPlayLength(),Canonical->GetPlayLength()*Parent.ComboEndPhase
        +TargetCanonical->GetPlayLength()*(TargetPhase-Parent.ComboEntryPhase)); // 完整Combo尾部是实际后继动作；按后继自己的Duration/曲线续采，不沿父刀倍率错放。
    Progress=FMath::Clamp(Time/FMath::Max(Canonical->GetPlayLength(),KINDA_SMALL_NUMBER),0.f,1.f);
    return true;
}
bool UDemoMeleeComponent::SetEquipped(bool bEquip)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Character(); // 本次输入所属Avatar。
    if (!Pawn || !Definition || (bEquip && !Pawn->CanUsePlayerSkills())) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_EQUIP_REJECT config/phase/life/menu")); return false; }
    if (!Pawn->HasAuthority()) { if (!bEquip) CancelPredictedPresentation(); ServerSetEquipped(bEquip); return true; } // 本机收起立即取消预测，晚到确认不得复活。
    if (State.bEquipped==bEquip) return true;
    Pawn->GetWeaponComponent()->CancelActions(); // 此入口也取消近战，但不改变装备，不能递归SetEquipped。
    State.bEquipped=bEquip; State.Attack=INDEX_NONE;
    IdlePoseEpoch=-1.; bIdleClockObserved=false; // 明确装备边沿即使同帧卸下再装也重新从Idle0表现，不增加复制身份或改变原State。
    if(bEquip) if(UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Pawn)) Build->NotifyWeaponChanged(Pawn->GetWeaponComponent()->GetActiveWeapon(),nullptr); // 收枪进近战不是枪到枪携带；清节律凭据，不能绕过一次切枪限制。
    OnRep_State(); Pawn->ForceNetUpdate();
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_EQUIP owner=%s equipped=%d"),*Pawn->GetName(),bEquip); return true;
}
void UDemoMeleeComponent::EquipPressed() { DEMO_LOG_CALL(); SetEquipped(true); }
void UDemoMeleeComponent::RequestAttack(int32 Index)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Character(); // 只允许自身Pawn发起能力请求。
    if (!Pawn || !IsEquipped() || (Index!=0 && Index!=1) || !Definition->Attacks.IsValidIndex(Index) || !Pawn->CanUsePlayerSkills()) // 可选副攻击不存在时在本地和服务器都立即拒绝，不发起无意义的RPC/GA。
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_REQUEST_REJECT index=%d equipment/phase"),Index); return; }
    if (!Pawn->HasAuthority()) { PredictAttack(Index); return; } // 同帧表现，仍由可靠RPC让服务器GAS批准伤害。
    if (State.Attack!=INDEX_NONE || ServerTime()<NextAttackTime || !Pawn->GetDemoASC())
    { if (!TryQueueLocalCombo(Index) && !BufferLocalInput(Index,NextAttackTime)) UE_LOG(LogFPSDemo,Log,TEXT("MELEE_REQUEST_REJECT recovery/active/ASC")); return; }
    const int32 StartCursor=Index==0 && Definition->bRestartPrimarySequenceAfterRecovery?0:HorizontalCursor; // 已通过原active/recovery门；只选新组首刀，不在请求/GA拒绝前改权威游标。
    RequestedAttack=ResolveInputAttack(Index,StartCursor); // 双端同一解析规则；连段Advance不经过本Standalone重开入口。
    bRequestedHorizontal=Index==0;
    if (RequestedAttack==INDEX_NONE) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_REQUEST_REJECT missing sequence attack cursor=%d"),StartCursor); bRequestedHorizontal=false; return; }
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PRIMARY_START_SELECT input=%d restart_after_recovery=%d stored_cursor=%d selected_cursor=%d attack=%d"),Index,Definition->bRestartPrimarySequenceAfterRecovery,HorizontalCursor,StartCursor,RequestedAttack);
    Pawn->GetDemoASC()->ActivateDemoAbility(UDemoMeleeAbility::StaticClass());
    RequestedAttack=INDEX_NONE; bRequestedHorizontal=false; // 同步请求结束清凭据，不能直接外部激活GA重复消费旧方向。
}
bool UDemoMeleeComponent::BeginAttack()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Character(); // GAS当前Avatar仍须是本Pawn。
    const ADemoGameState* GameState=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 服务器冻结当前阶段，不借用旧战斗的伤害授权。
    UDemoCombatSession* Active=GameState && GameState->Phase==EDemoPhase::Combat?UDemoCombatSession::FindActive(GetWorld()):nullptr; // 安全区/关间不需要战斗会话，只播放和扫掠环境。
    if (!Pawn || !Pawn->HasAuthority() || !IsEquipped() || !Definition->Attacks.IsValidIndex(RequestedAttack)
        || !Pawn->CanUsePlayerSkills() || !Pawn->GetDemoAttributes() || !GameState
        || (GameState->Phase==EDemoPhase::Combat && (!Active || !Active->IsActive())) || State.Attack!=INDEX_NONE || ServerTime()<NextAttackTime)
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BEGIN_REJECT authority/request/session/recovery")); return false; }
    FVector2D Window; FString WindowError; // 只在服务器真实起刀读一次，通知时间与伤害/表现使用相同作者时轴。
    if (!Definition->Attacks[RequestedAttack].ResolveHitWindow(Window,WindowError))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_BEGIN_REJECT window=%s"),*WindowError); return false; }
    FrozenHitWindow=Window;
    State.Attack=RequestedAttack; ++State.Serial; State.StartTime=ServerTime();
    State.bPrimarySequence=bRequestedHorizontal; State.bComboExitCommitted=false; State.bStartedFromCombo=false;
    State.ComboSourceAttack=INDEX_NONE; State.EntryPhase=0.f;
    State.OwnerRequestId=CurrentInputRequest; // 仅关联本次拥有者请求，不从客户端接受起招时间或命中点。
    if (bRequestedHorizontal && !Definition->HorizontalAttackSequence.IsEmpty())
    {
        const int32 StartCursor=Definition->bRestartPrimarySequenceAfterRecovery?0:HorizontalCursor; // 本Standalone实际首刀的位置；只在GAS提交及起刀校验成功后推进。
        HorizontalCursor=(StartCursor+1)%Definition->HorizontalAttackSequence.Num(); // 新组0→1；普通取消/拒绝不另行重置，真正连段沿原Advance路径继续。
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PRIMARY_CURSOR_COMMITTED serial=%u restart_after_recovery=%d start_cursor=%d next_cursor=%d"),State.Serial,Definition->bRestartPrimarySequenceAfterRecovery,StartCursor,HorizontalCursor);
    }
    State.NextHorizontalCursor=HorizontalCursor; // 复制下一个候选动作给拥有者预测，服务器仍保有最终选择权。
    const FDemoMeleeAttack& Attack=Definition->Attacks[State.Attack]; // 当前只读配置，由组件强引用保活。
    NextAttackTime=State.StartTime+Attack.Duration; PreviousProgress=0.f;
    FrozenDamage=Attack.Damage+Pawn->GetDemoAttributes()->GetWeaponDamageBonus();
    HitTargets.Reset(); bWorldImpactPlayed=false; Session=Active; AttackPhase=GameState->Phase; AttackLevel=GameState->LevelNumber;
    if (Active) Active->RegisterCleanup(this,FSimpleDelegate::CreateUObject(this,&UDemoMeleeComponent::CancelAttack)); // Combat用游戏线程弱UObject回调同栈撤销；无会话动作由阶段快照/菜单取消守卫。
    Pawn->SetActorRotation(FRotator(0,Pawn->GetBaseAimRotation().Yaw,0)); // 挥棍时身体先对准视线，移动/站立沿同一攻击方向。
    OnRep_State(); Pawn->ForceNetUpdate();
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_BEGIN serial=%u attack=%s damage=%.1f duration=%.2f phase=%d level=%d"),State.Serial,*Attack.Id.ToString(),FrozenDamage,Attack.Duration,static_cast<int32>(AttackPhase),AttackLevel); return true; // 常规挥击属业务调用，包体不逐次输出。
}
bool UDemoMeleeComponent::IsAttackContextValid() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=Character(); // 仅当前调用借用，换Avatar后原组件不得继续挥击。
    const ADemoGameState* GameState=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 不缓存可销毁World的状态对象。
    return Pawn && Pawn->HasAuthority() && IsEquipped() && Definition->Attacks.IsValidIndex(State.Attack) && Pawn->CanUsePlayerSkills()
        && GameState && GameState->Phase==AttackPhase && GameState->LevelNumber==AttackLevel
        && (AttackPhase!=EDemoPhase::Combat || (Session.IsValid() && Session->IsActive()));
}
void UDemoMeleeComponent::CancelAttack()
{
    DEMO_LOG_CALL();
    CancelCameraAttack(); // 明确撤销当前身份和包络，迟到回执不得重新启动旧反馈。
    if(GetOwner() && GetOwner()->HasAuthority() && State.Attack!=INDEX_NONE)
        State.LastCancelledSerial=State.Serial; // 同步清关可能正在FinishAttack补扫内；外层仍发布该撤销，不能当自然末刀重放反馈。
    if (GetOwner() && !GetOwner()->HasAuthority())
    {
        CancelPredictedPresentation(); // 先保已知子刀完整恢复，再由此函数清槽；提前Clear会抹掉早到确认事实。
        if (Character() && Character()->IsLocallyControlled()) ServerCancelAttack(); // 只允许当前拥有者取消本人权威动作。
        return;
    }
    const uint32 PendingRequest=BufferedRequestId,PendingParent=BufferedParentRequestId; // 清槽前冻结回执身份，不能把排队拒绝混作当前刀失败。
    const bool bPendingCombo=bBufferedCombo;
    ClearBufferedInput(); // 菜单/切枪/死亡即便GA已结束也不能偷打一刀。
    if (!GetOwner()) return;
    if (GetOwner()->HasAuthority() && PendingRequest!=0)
    {
        if (bPendingCombo) ClientComboQueueResult(PendingRequest,PendingParent,false,State,NextAttackTime);
        else ClientAttackResult(PendingRequest,false,State,NextAttackTime);
    }
    if (bFinishing) return; // 正常收招补扫可能同步清关；队列已拒绝，活动GA由外层统一清理。
    if (State.Attack==INDEX_NONE) return;
    if (Character() && Character()->GetDemoASC()) Character()->GetDemoASC()->CancelDemoAbility(UDemoMeleeAbility::StaticClass());
    if (State.Attack!=INDEX_NONE) FinishAttack(true); // Avatar卸载/GA已移除的兜底，不保留可继续伤害的窗口。
}
void UDemoMeleeComponent::FinishAttack(bool bCancelled)
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || State.Attack==INDEX_NONE || bFinishing) return;
    if(bCancelled)
    {
        State.LastCancelledSerial=State.Serial; // GAS可直接EndAbility而不经CancelAttack包装；取消事实和姿态在同一原子状态复制。
        CancelCameraAttack(); // 必须在改写Attack/Request前撤销本刀；不额外改变伤害、恢复或GAS时钟。
    }
    bFinishing=true;
    if ((bCancelled || State.bComboExitCommitted) && Definition && Definition->Attacks.IsValidIndex(State.Attack))
        NextAttackTime=FMath::Max(NextAttackTime,State.StartTime+Definition->Attacks[State.Attack].Duration); // 成功衔接不结束旧刀；到此仍承诺说明取消/过期/提交失败，不能偷减恢复期。
    if (!bCancelled && IsAttackContextValid()) // 环境扫掠也可补齐；ApplyHit继续独立限制只有Combat能扣血。
    {
        const float From=FMath::Max(PreviousProgress,static_cast<float>(FrozenHitWindow.X)); // Timer可能先于组件Tick；只补本刀冻结窗口尚未结算部分。
        if (From<FrozenHitWindow.Y) SweepAttack(From,FrozenHitWindow.Y);
    }
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_END serial=%u cancelled=%d hits=%d"),State.Serial,bCancelled,HitTargets.Num()); // 常规攻击结束不进入包体高频日志。
    if (bBufferedCombo)
    {
        const uint32 PendingRequest=BufferedRequestId,PendingParent=BufferedParentRequestId; // 正常GA结束但未推进（过期/Commit失败）也必须拒绝一次。
        ClearBufferedInput();
        if (PendingRequest!=0) ClientComboQueueResult(PendingRequest,PendingParent,false,State,NextAttackTime);
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_DISCARDED finish/cancel/commit/expired"));
    }
    State.Attack=INDEX_NONE; State.bComboExitCommitted=false; State.bStartedFromCombo=false; State.ComboSourceAttack=INDEX_NONE; State.EntryPhase=0.f;
    Session.Reset(); HitTargets.Reset();
    GetOwner()->ForceNetUpdate(); OnRep_State(); bFinishing=false; // 外层结束后才开放后继请求。
}
UAnimSequence* UDemoMeleeComponent::GetFullContextComboPose(const FDemoMeleeState& Snapshot,bool bFirstPerson,float ChildProgress,float& Time) const
{
    DEMO_LOG_TICK(); Time=0.f;
#if WITH_EDITOR
    if (bEditorDisableFullContextForProbe) return nullptr; // 隔离A/B原因已在BeginPlay记录；调用方继续原canonical路由，不修改资产bool或当前子刀业务事实。
#endif
    if (!Definition || !Definition->bUseFullContextForComboContinuation || !Snapshot.bPrimarySequence
        || !Definition->Attacks.IsValidIndex(Snapshot.Attack) || !Definition->Attacks.IsValidIndex(Snapshot.ComboSourceAttack)
        || Snapshot.Attack==Snapshot.ComboSourceAttack || !FMath::IsFinite(ChildProgress) || !FMath::IsFinite(Snapshot.EntryPhase))
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_FULL_CONTEXT_NOT_SELECTED optin/child/source/phase")); return nullptr; }
    const FDemoMeleeAttack& Child=Definition->Attacks[Snapshot.Attack]; // 当前刀业务事实保持；所有UObject均由组件Definition强引用，本游戏线程调用内借用。
    const FDemoMeleeAttack& Parent=Definition->Attacks[Snapshot.ComboSourceAttack];
    if (Parent.ComboTargetId!=Child.Id || Snapshot.EntryPhase<0.f || Snapshot.EntryPhase>=1.f
        || !FMath::IsNearlyEqual(Snapshot.EntryPhase,Parent.ComboEntryPhase,KINDA_SMALL_NUMBER) || ChildProgress+KINDA_SMALL_NUMBER<Snapshot.EntryPhase)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_FULL_CONTEXT_NOT_SELECTED target/entry")); return nullptr; }
    UAnimSequence* Full=bFirstPerson?Parent.ComboFirstPerson.Get():Parent.ComboThirdPerson.Get(); // 已审父完整上下文，不创建/拼接资源，不重读当前Notify或烘焙碰撞。
    const UAnimSequence* ParentCanonical=bFirstPerson?Parent.FirstPerson.Get():Parent.ThirdPerson.Get();
    const UAnimSequence* ChildCanonical=bFirstPerson?Child.FirstPerson.Get():Child.ThirdPerson.Get();
    if (!Full || !ParentCanonical || !ChildCanonical) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_FULL_CONTEXT_NOT_SELECTED missing view source")); return nullptr; }
    const float ParentExitSeconds=ParentCanonical->GetPlayLength()*Parent.ComboEndPhase; // 父标准源秒，不能按完整资源总长重新归一化。
    const float RequiredSeconds=ParentExitSeconds+ChildCanonical->GetPlayLength()*(1.f-Snapshot.EntryPhase); // 含当前目标从Entry到完整Recovery的源秒。
    if (Full->GetPlayLength()+.002f<RequiredSeconds) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_FULL_CONTEXT_NOT_SELECTED full source shorter than child recovery")); return nullptr; } // 与Validate相同的原生长度读回容差；不截短目标恢复来强行复用。
    Time=FMath::Min(Full->GetPlayLength(),ParentExitSeconds+ChildCanonical->GetPlayLength()*(FMath::Clamp(ChildProgress,Snapshot.EntryPhase,1.f)-Snapshot.EntryPhase));
    return Full; // 不检查bStartedFromCombo：晚到事实可保来源映射，普通混合是否跳过仍由原IsContinuousPoseTransition独立判断。
}
float UDemoMeleeComponent::GetIdlePoseTime(bool bFirstPerson) const
{
    DEMO_LOG_TICK();
    if (!IsInGameThread()) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_IDLE_CLOCK_REJECT game thread required")); return 0.f; }
    const ADemoCharacter* Pawn=Character(); // 同步借用，不向动画工作线程发布Pawn或ASC引用。
    const FDemoMeleeState& Visual=PresentationState(); // 仅比较身份，不能以Idle时钟推动真实State或GA。
    const bool bEquipped=IsEquipped(); // 与GetPose相同装备真值；复制/重连晚到时保持既有授权边界。
    const uint32 CancelledSerial=State.LastCancelledSerial; // 权威取消持久事实仅作一次相位重置，不以等于当前Serial永久禁止合法Idle。
    const bool bIdentityChanged=!bIdleClockObserved || bIdleClockEquipped!=bEquipped || IdleClockDefinition.Get()!=Definition.Get()
        || IdlePoseSerial!=Visual.Serial || IdlePoseCancelledSerial!=CancelledSerial; // 包括整刀被低频采样漏过的情况。
    bIdleClockObserved=true; bIdleClockEquipped=bEquipped; IdleClockDefinition=Definition.Get();
    IdlePoseSerial=Visual.Serial; IdlePoseCancelledSerial=CancelledSerial;
    const bool bValid=GetWorld() && Pawn && bEquipped && !Pawn->IsCoopEliminated(); // 死亡/卸下只清表现epoch，不改变恢复门或输入状态。
    const double Now=bValid?PresentationTime():0.; // 避免失去World后进入拥有者预测时钟分支。
    if (!bValid || !FMath::IsFinite(Now) || Now<0. || IsAttacking())
    {
        if (IdlePoseEpoch>=0.) UE_LOG(LogFPSDemo,Log,TEXT("MELEE_IDLE_CLOCK_RESET serial=%u equipped=%d valid=%d"),Visual.Serial,bEquipped,bValid);
        IdlePoseEpoch=-1.; return 0.f; // 攻击期间基础Idle固定中立帧，不把攻击PoseTime误当待机源秒。
    }
    if (IdlePoseEpoch<0. || bIdentityChanged || Now<IdlePoseEpoch)
    {
        IdlePoseEpoch=Now; // 两视角同帧后续查询身份不变，严格共享这个epoch。
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_IDLE_CLOCK_BEGIN serial=%u cancelled=%u epoch=%.6f identity_changed=%d"),Visual.Serial,CancelledSerial,IdlePoseEpoch,bIdentityChanged);
    }
    const UAnimSequence* Idle=bFirstPerson?Definition->FirstPersonIdle.Get():Definition->ThirdPersonIdle.Get(); // 两序列长度可不同，起点仍是同一次收招。
    const double Length=Idle?Idle->GetPlayLength():0.; // 秒；无/坏资源只返回0，由既有配置校验和显示门处理。
    return FMath::IsFinite(Length) && Length>0.?static_cast<float>(FMath::Fmod(FMath::Max(0.,Now-IdlePoseEpoch),Length)):0.f;
}
UAnimSequence* UDemoMeleeComponent::GetPose(bool bFirstPerson,float& Time,float& Progress,FName& Action) const
{
    DEMO_LOG_TICK(); Time=0.f; Progress=0.f; Action=TEXT("MeleeHold");
    const float IdleTime=GetIdlePoseTime(bFirstPerson); // 即使已收起也观察并清缓存；不能让下次同Serial重装备继承旧呼吸相位。
    if (!IsEquipped()) return nullptr;
    UAnimSequence* Pose=bFirstPerson?Definition->FirstPersonIdle.Get():Definition->ThirdPersonIdle.Get(); // 两套资源同一骨架，视角只改变表现。
    // SequenceEvaluator显式时钟；每次收招从与源Recovery相匹配的Idle0开始，不跳入全局World的任意呼吸相位。
    const double Now=PresentationTime(); // 同一查询只取一次时钟，保证边界与取样一致。
    Time=IdleTime;
    const FDemoMeleeState& Visual=PresentationState(); // 本机预测时钟只影响表现，扫掠不会使用此快照。
    float ContinuationTime=0.f,ContinuationProgress=0.f; // 宽限探测失败不能清掉正常Idle呼吸时钟。
    if (Definition->Attacks.IsValidIndex(Visual.Attack) && Now<SegmentEndTime(Visual))
    {
        const FDemoMeleeAttack& Attack=Definition->Attacks[Visual.Attack]; // 保留作者关键姿态，用DA折线统一映射时序。
        const float RawProgress=Attack.SampleProgress(static_cast<float>(Now-Visual.StartTime)); // 网络估计时钟可能暂时落后实际入口，但不能倒采下一刀Windup。
        const float Entry=FMath::Clamp(Visual.EntryPhase,0.f,1.f); // Entry是实际入口；普通纠正可关闭连续混合标志，但不能因此重新允许采回前摇。普通独立刀Entry恒为0。
        Progress=FMath::Max(Entry,RawProgress);
        if (RawProgress<Entry && LoggedEntryClampSerial!=Visual.Serial)
        {
            LoggedEntryClampSerial=Visual.Serial;
            UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_ENTRY_CLOCK_CLAMP serial=%u request=%u phase=%.6f entry=%.6f"),Visual.Serial,Visual.OwnerRequestId,RawProgress,Entry); // 每刀一次诊断，正常逐帧入口仍TICK。
        }
        UAnimSequence* Canonical=bFirstPerson?Attack.FirstPerson.Get():Attack.ThirdPerson.Get(); // 比例始终基于原Standalone，不能按短变体再次归一化。
        Pose=Visual.bComboExitCommitted?(bFirstPerson?Attack.ComboFirstPerson.Get():Attack.ComboThirdPerson.Get()):Canonical;
        Action=Attack.Id;
        Time=Pose && Canonical?FMath::Min(Pose->GetPlayLength(),Canonical->GetPlayLength()*Progress):0.f;
        float FullContextTime=0.f; // 纯表现源秒；子刀Progress/Action及统一业务起点不变，不把父资源总长写回任何攻击时钟。
        if (!Visual.bComboExitCommitted)
            if (UAnimSequence* FullContext=GetFullContextComboPose(Visual,bFirstPerson,Progress,FullContextTime))
            { Pose=FullContext; Time=FullContextTime; } // 未承诺下一刀继续父整段；已承诺则保上方当前刀自己的前缀/Antic，停止点击才沿原完整Recovery。
    }
    else if (GetCommittedComboContinuation(Visual,Now,bFirstPerson,ContinuationTime,ContinuationProgress))
    {
        const FDemoMeleeAttack& Parent=Definition->Attacks[Visual.Attack];
        Pose=bFirstPerson?Parent.ComboFirstPerson.Get():Parent.ComboThirdPerson.Get(); Action=Parent.Id;
        Time=ContinuationTime; Progress=ContinuationProgress; // 不落Idle/撤Slot；子刀发布后按真实来源事实选择父完整上下文或原canonical，不另起表现时钟。
    }
    return Pose;
}
void UDemoMeleeComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,TickFunction);
    if (!Character() || !Definition) return;
    GetIdlePoseTime(false); // 只观察身份/清活动刀epoch，远端无FP实例且TP图暂未求值时也能捕获卸下/起刀边沿；不自动播放动画。
    if (!GetOwner()->HasAuthority() && bUsePrediction && PredictedState.Attack!=INDEX_NONE)
    {
        const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>(); // 每帧重验本地阶段，复制变化立即撤回预测。
        if (!Character()->CanUsePlayerSkills() || !GameState || GameState->Phase!=PredictedPhase || GameState->LevelNumber!=PredictedLevel)
            CancelAttack(); // 同时可靠通知服务器，保留既有攻击冷却。
    }
    if (GetOwner()->HasAuthority() && State.Attack!=INDEX_NONE)
    {
        if (!IsAttackContextValid()) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_CANCEL stale phase/level/session or action gate")); CancelAttack(); }
        else
        {
            const FDemoMeleeAttack& Attack=Definition->Attacks[State.Attack]; // 同一动作只读窗口，跨帧补扫。
            const float Progress=Attack.SampleProgress(static_cast<float>(ServerTime()-State.StartTime)); // 与表现同一作者phase映射，缩前摇不能把视觉留在旧时间。
            const float From=FMath::Max(PreviousProgress,static_cast<float>(FrozenHitWindow.X)); // 本刀冻结的通知/旧配置窗口，过窗后不扫。
            const float To=FMath::Min(Progress,static_cast<float>(FrozenHitWindow.Y)); // 大帧仍覆盖跨过的窗口末端。
            if (To>From) SweepAttack(From,To);
            PreviousProgress=Progress;
        }
    }
    ConsumeBufferedInput(); // 完整GA结束且冷却到期才能消费，跨阶段/取消队列已清除。
    UpdatePresentation();
    if (FirstPersonArms)
    {
        FirstPersonArms->TickAnimation(DeltaTime,false); FirstPersonArms->RefreshBoneTransforms(); // 本地PostUpdate仅一次求值；收起时也清空旧攻击缓存，重新装备不能从上一次被取消的半挥姿势跳回。附棍读取同帧右手。
    }
    const FDemoMeleeState& Visual=PresentationState(); // 拥有者挥声随预测，旁观者随权威；命中音仍只来自服务器实际伤害回执。
    if (Definition->Attacks.IsValidIndex(Visual.Attack) && !bSwingPlayed)
    {
        const FDemoMeleeAttack& Attack=Definition->Attacks[Visual.Attack]; // 声音与当前视角动画共享作者相位。
        const double Elapsed=PresentationTime()-Visual.StartTime; // 当前表现动作年龄秒，拥有者不受同步时钟校正和确认回包倒放。
        if (Attack.SampleProgress(static_cast<float>(Elapsed))>=Attack.SwingSoundPhase)
        {
            bSwingPlayed=true;
            if (Elapsed<Attack.Duration*.75)
            {
                DemoWeaponAudio::PlayAtLocation(this,Attack.SwingSound,GetOwner()->GetActorLocation(),TEXT("MeleeSwing"));
                PlaySwingCameraFeedback(Visual.Attack); // 共享已去重挥声相位，连招确认/显式求Pose不会重复触发。
            }
        }
    }
}
void UDemoMeleeComponent::CaptureTraceFrame(FTransform& Baked,FTransform& Actual) const
{
    DEMO_LOG_TICK();
    const FDemoMeleeAttack& Attack=Definition->Attacks[State.Attack]; // 只在同一合法Sweep/调试同步栈借用。
    const float Phase=Attack.SampleProgress(static_cast<float>(ServerTime()-State.StartTime));
    const float Position=FMath::Clamp(Phase,0.f,1.f)*(Attack.WeaponFrames.Num()-1);
    const int32 Index=FMath::Min(FMath::FloorToInt(Position),Attack.WeaponFrames.Num()-2);
    Baked.Blend(Attack.WeaponFrames[Index],Attack.WeaponFrames[Index+1],Position-Index);
    Actual=Definition->GripOffset*Character()->GetMesh()->GetSocketTransform(Definition->HandSocket,RTS_World); // 服务器真实已求值TP，既有Tick prerequisite及AlwaysRefreshBones保持。
}

void UDemoMeleeComponent::TracePose(float Phase,FVector& Base,FVector& Tip,const FTransform* CachedBaked,const FTransform* CachedActual) const
{
    DEMO_LOG_TICK();
    const FDemoMeleeAttack& Attack=Definition->Attacks[State.Attack]; // 配置验证后只读，伤害仍只消费权威State。
    const TArray<FDemoMeleeTracePose>& Poses=Attack.TracePoses; // 固定子步弧线；不是在低帧时只连两个棍尖。
    const float Position=FMath::Clamp(Phase,0.f,1.f)*(Poses.Num()-1); // 浮点样本位置。
    const int32 Index=FMath::Min(FMath::FloorToInt(Position),Poses.Num()-2); // 保证末帧也有合法邻居。
    Base=FMath::Lerp(Poses[Index].Base,Poses[Index+1].Base,Position-Index);
    Tip=FMath::Lerp(Poses[Index].Tip,Poses[Index+1].Tip,Position-Index);
    if (Definition->bAlignTraceToEvaluatedGrip)
    {
        FTransform LocalBaked,LocalActual; // 调试单采样才捕获；Sweep传入一次缓存，保持原两步变换，不合并以避免非均匀缩放改变数学语义。
        if (!CachedBaked || !CachedActual) { CaptureTraceFrame(LocalBaked,LocalActual); CachedBaked=&LocalBaked; CachedActual=&LocalActual; }
        Base=CachedActual->TransformPosition(CachedBaked->InverseTransformPosition(Base));
        Tip=CachedActual->TransformPosition(CachedBaked->InverseTransformPosition(Tip));
        return; // 跨窗子步沿同一完整作者弧线；本帧基底近似固定，不宣称历史姿态回溯。
    }
    const FRotator View=Character()->GetBaseAimRotation(); // 旧策略只使用服务器控制视线，不接受客户端任意起点。
    const FRotator Pitch(FMath::Clamp(FRotator::NormalizeAxis(View.Pitch),-Definition->AimPitchLimit,Definition->AimPitchLimit),0,0);
    const FRotator Yaw(0,View.Yaw,0);
    Base=GetOwner()->GetActorLocation()+Yaw.RotateVector(Definition->AimPivot+Pitch.RotateVector(Base-Definition->AimPivot));
    Tip=GetOwner()->GetActorLocation()+Yaw.RotateVector(Definition->AimPivot+Pitch.RotateVector(Tip-Definition->AimPivot));
}
#if WITH_EDITOR
bool UDemoMeleeComponent::DebugSampleActiveTracePose(float Phase,FVector& Base,FVector& Tip) const
{
    DEMO_LOG_TICK();
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Definition || !Definition->Attacks.IsValidIndex(State.Attack) || !FMath::IsFinite(Phase)) return false;
    TracePose(Phase,Base,Tip);
    return !Base.ContainsNaN() && !Tip.ContainsNaN();
}
#endif
void UDemoMeleeComponent::SweepAttack(float From,float To)
{
    DEMO_LOG_TICK();
    const int32 Steps=FMath::Clamp(FMath::CeilToInt((To-From)*240.f),1,240); // phase子步，低帧率覆盖真实弧线，最大预算有界。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoMelee),false,GetOwner()); // 真实PhysicsAsset部位通道，忽略自身及其枪械。
    if (Character()->GetWeaponComponent()->GetActiveWeapon()) Query.AddIgnoredActor(Character()->GetWeaponComponent()->GetActiveWeapon());
    FTransform BakedFrame,ActualFrame; // 同次Sweep所有子步复用当前TP握点锚；不跨帧缓存，不修改原历史曲线/补扫/墙体规则。
    const bool bUseGrip=Definition->bAlignTraceToEvaluatedGrip;
    if (bUseGrip) CaptureTraceFrame(BakedFrame,ActualFrame);
    FVector OldBase,OldTip; // 本次窗口初始世界棍段，位移来自实际权威角色。
    TracePose(From,OldBase,OldTip,bUseGrip?&BakedFrame:nullptr,bUseGrip?&ActualFrame:nullptr);
    for (int32 Step=1;Step<=Steps && IsAttackContextValid();++Step) // 每个子步重验，死亡/清关同步撤销后立刻停止，非Combat只能环境反馈。
    {
        FVector Base,Tip; // 当前子步棍段，生命周期仅循环。
        TracePose(FMath::Lerp(From,To,static_cast<float>(Step)/Steps),Base,Tip,bUseGrip?&BakedFrame:nullptr,bUseGrip?&ActualFrame:nullptr);
        const int32 Segments=FMath::Clamp(FMath::CeilToInt(FVector::Distance(Base,Tip)/(Definition->TraceRadius*1.5f)),1,64); // 沿整棍密采，防止只扫棍头漏过近距离目标。
        for (int32 Segment=0;Segment<=Segments && IsAttackContextValid();++Segment) // GE可在段内同步推进阶段，后继棍段必须重新校验。
        {
            const float Alpha=static_cast<float>(Segment)/Segments; // 棍段0..1采样位置。
            const FVector Start=FMath::Lerp(OldBase,OldTip,Alpha), End=FMath::Lerp(Base,Tip,Alpha); // 同一棍点上/下子步轨迹。
            FHitResult Hit; // 最近阻挡才参与结算，不使用穿过墙体的Overlap集合。
            if (GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,DemoEnemyHitZones::TraceChannel,FCollisionShape::MakeSphere(Definition->TraceRadius),Query))
            {
                FHitResult Cover; // 胸口到接触点的遮挡复核，棍弧从墙后开始时也不能隔墙伤害。
                const FVector Origin=GetOwner()->GetActorLocation()+FVector(0,0,35); // 胶囊胸口，非用户提供的任意点。
                const bool bBlocked=GetWorld()->LineTraceSingleByChannel(Cover,Origin,Hit.ImpactPoint,DemoEnemyHitZones::TraceChannel,Query) && Cover.GetActor()!=Hit.GetActor();
                if (!bBlocked && FVector::Distance(GetOwner()->GetActorLocation(),Hit.ImpactPoint)<=Definition->MaxReach) ApplyHit(Hit);
            }
        }
        OldBase=Base; OldTip=Tip;
    }
}
void UDemoMeleeComponent::ApplyHit(const FHitResult& Hit)
{
    DEMO_LOG_TICK(); // 同一挥击每个子步/棍段都可能回调；保留入口埋点，实际伤害另记一次Log，避免碰撞采样刷屏。
    if (IDemoDamageableTarget* Objective = Cast<IDemoDamageableTarget>(Hit.GetActor())) // 设施命中独立处理，不参与敌人弱点/击退/击杀天赋。
    {
        if (!IsAttackContextValid() || AttackPhase != EDemoPhase::Combat || !Session.IsValid() || !Session->IsActive()
            || !Character() || !Definition || !Definition->Attacks.IsValidIndex(State.Attack)
            || HitTargets.Contains(Hit.GetActor()) || HitTargets.Num() >= Definition->Attacks[State.Attack].MaxTargets)
        { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("MELEE_OBJECTIVE_REJECT phase/session/dedup")); return; }
        HitTargets.Add(Hit.GetActor()); // GE前闩锁，本挥击每目标一次；设施与敌人共享MaxTargets预算。
        FDemoTargetDamageRequest Request; // 当前服务器招式快照，不从客户端提供坐标/伤害。
        Request.Session = Session.Get(); Request.SourceAvatar = Character(); Request.SourceASC = Character()->GetAbilitySystemComponent();
        Request.AttackId = FGuid(GetOwner()->GetUniqueID(), State.Serial, 0, 1); Request.WeaponId = Definition->WeaponId;
        Request.Damage = FrozenDamage; Request.Hit = Hit; Request.bMelee = true;
        const FDemoTargetDamageResult Result = Objective->ApplyTargetDamage(Request); // 设施实际扣血后统一提交HUD/金属池，不能再叠旧硬表面RPC声音。
        if (Result.ActualDamage > 0.f) bWorldImpactPlayed = true; // 保留原挥击环境声闩锁，后续棍段擦墙不再重复硬表面音。
        return;
    }
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(Hit.GetActor()); // 只攻击真实敌人，队友/环境不扣血。
    if (!Enemy)
    { if (!bWorldImpactPlayed && Hit.GetActor() && !Hit.GetActor()->IsA<ADemoCharacter>()) { bWorldImpactPlayed=true; MulticastWorldImpact(Hit.ImpactPoint); } return; } // 墙面沿用环境声，怪物实际伤害走共享池。
    if (!IsAttackContextValid() || AttackPhase!=EDemoPhase::Combat || !Session.IsValid() || !Session->IsActive() || !Enemy->IsAlive() || !Enemy->IsCombatSessionValid()
        || HitTargets.Contains(Enemy) || HitTargets.Num()>=Definition->Attacks[State.Attack].MaxTargets)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_HIT_REJECT phase/session/target/dedup")); return; } // 同一窗口多个棍段可能拒绝同一目标，禁止Log级刷屏。
    EDemoEnemyHitRegion Region; // 仅真实部位解析成功后使用，未配置骨拒绝伤害。
    float Multiplier=0.f; // 目标资产的区域倍率，不能由客户端指定。
    const UDemoEnemyHitProfile* Profile=Enemy->GetHitProfile(); // 敌人定义拥有的只读骨白名单。
    UAbilitySystemComponent* SourceASC=Character()->GetAbilitySystemComponent(); // 当前PS拥有，Avatar必须仍是攻击Pawn。
    UAbilitySystemComponent* TargetASC=Enemy->GetAbilitySystemComponent(); // 敌人自身ASC。
    if (!Profile || !Profile->ResolveHit(Hit,Region,Multiplier) || !SourceASC || !TargetASC || SourceASC->GetAvatarActor()!=Character())
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_HIT_REJECT invalid bone/ASC target=%s bone=%s"),*GetNameSafe(Enemy),*Hit.BoneName.ToString()); return; }
    HitTargets.Add(Enemy); // 先闩锁，GE同步死亡/关卡/委托重入不能重复击打。
    const float Before=Enemy->GetHealth(); // 实际损失回执尊重Boss无敌与过量伤害。
    const bool bFrozenBefore = TargetASC->HasMatchingGameplayTag(DemoAmmoTags::Frozen); // 保存命中前状态，GE/旧天赋在扣血后解除冻结不影响本次冰裂资格。
    const float RawSourceDamage = FrozenDamage; // 核心范围伤害以近战源端基数为准，不继承第一个受害者弱点倍率。
    const uint32 TargetId=Enemy->GetUniqueID(), Serial=State.Serial; // GE前冻结纯值，死亡可能马上清关。
    const float KnockbackDistance=Definition->Attacks[State.Attack].KnockbackDistance; // GE前冻结招式距离cm，避免同步清关后State.Attack已重置。
    const float KnockbackDuration=Definition->Attacks[State.Attack].KnockbackDuration; // 秒，组件后续仅消费值，不持有DA数组元素。
    FVector KnockbackDirection=(Enemy->GetActorLocation()-GetOwner()->GetActorLocation()).GetSafeNormal2D(); // 从攻击者向目标平面推开，不能让客户端指定方向。
    if (KnockbackDirection.IsNearlyZero()) KnockbackDirection=GetOwner()->GetActorForwardVector().GetSafeNormal2D(); // 重叠中心时采用服务器朝向，禁止NaN冲量。
    FGameplayEffectContextHandle Context=SourceASC->MakeEffectContext(); // 来源仍是原玩家，经济击杀归属复用现有ASC流程。
    Context.AddHitResult(Hit,true); Context.AddSourceObject(this);
    FGameplayEffectSpecHandle Spec=SourceASC->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(),1.f,Context); // 独立Spec，不改GE CDO。
    if (!Spec.IsValid()) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_HIT_REJECT invalid GE")); return; }
    Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-FrozenDamage*Multiplier);
    SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(),TargetASC);
    const float Actual=IsValid(Enemy)?FMath::Clamp(Before-Enemy->GetHealth(),0.f,Before):0.f; // 只报告实际HP改变。
    if (Actual>0.f)
    {
        bool bCoreHandledKnockback = false; // 冰裂接管成功才替代普通击退，未选核心/冷却/失败仍保留原武器规则。
        if (UDemoRunBuildComponent* Core = UDemoRunBuildComponent::FindForPawn(Character()))
            bCoreHandledKnockback = Core->NotifyMeleeHit(Enemy, RawSourceDamage, Actual, bFrozenBefore, IsValid(Enemy) && Enemy->GetHealth() <= 0.f, KnockbackDirection);
        if (!bCoreHandledKnockback && IsValid(Enemy) && Enemy->IsAlive() && !Enemy->IsBoss() && KnockbackDistance>0.f) // Boss完全免击退；致死/无敌/全盾吸收不推动，声音与实际伤害回执保持原规则。
            if (UDemoEnemyKnockbackComponent* Knockback=Enemy->FindComponentByClass<UDemoEnemyKnockbackComponent>()) // 敌人拥有的移动执行器，只在同步栈内借用。
                Knockback->Apply(KnockbackDirection,KnockbackDistance,KnockbackDuration);
        // 只有实际HP损失才提交火花与金属声；共享池负责近战保留预算，禁止旧RPC再播一层。
        if (UDemoHitFeedbackComponent* Feedback=GetOwner()->FindComponentByClass<UDemoHitFeedbackComponent>()) // 原射手拥有者回执。
        {
            FDemoHitFeedbackEvent Receipt; // 无目标引用，不受尸体复制到达顺序影响。
            Receipt.ShotId=FGuid(GetOwner()->GetUniqueID(),Serial,0,1); Receipt.TargetId=TargetId; Receipt.ImpactPoint=Hit.ImpactPoint;
            Receipt.ImpactNormal=Hit.ImpactNormal; Receipt.bMelee=true; // 冻结真实棍段接触点/法线，不采用击退后的敌人中心。
            Receipt.Damage=Actual; Receipt.bCore=Region==EDemoEnemyHitRegion::Core; Receipt.bKilled=IsValid(Enemy)&&Enemy->GetHealth()<=0.f;
            Feedback->Confirm(Receipt,Definition->EnemyImpactSound);
        }
    }
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_HIT serial=%u target=%u bone=%s actual=%.2f elapsed=%.3f"),Serial,TargetId,*Hit.BoneName.ToString(),Actual,ServerTime()-State.StartTime); // 同步服务器起招到扣血秒数，便于区分前摇、轨迹接触和网络回执延迟；包体仍裁剪Log。
}
void UDemoMeleeComponent::UpdatePresentation()
{
    DEMO_LOG_TICK();
    UpdateCameraFeedback(); // 装备/取消/OnRep和Tick均走同一生命周期；不依赖FP模型是否创建。
    ADemoCharacter* Pawn=Character(); // 当前Pawn借用，FP只在本地拥有者创建；TP在全部端求值。
    if (!Pawn || !Definition || !Pawn->GetMesh()->GetSkeletalMeshAsset()) return;
    const bool bSharedBody=Pawn->GetPlayerPresentation() && Pawn->GetPlayerPresentation()->UsesFullBodyFirstPerson() && !Definition->bIndependentFirstPersonDisplay; // 用户允许近战例外；只改变显示，TP/服务器继续原手骨与轨迹。
    if (State.bEquipped && !ThirdPersonBat)
    {
        ThirdPersonBat=NewObject<UStaticMeshComponent>(Pawn,TEXT("MeleeThirdPersonBat")); Pawn->AddInstanceComponent(ThirdPersonBat);
        ThirdPersonBat->SetupAttachment(Pawn->GetMesh(),Definition->HandSocket);
        ThirdPersonBat->SetStaticMesh(Definition->Mesh); ThirdPersonBat->SetRelativeTransform(Definition->GripOffset);
        ThirdPersonBat->SetCollisionEnabled(ECollisionEnabled::NoCollision); ThirdPersonBat->SetOwnerNoSee(true);
        ThirdPersonBat->SetCastShadow(false); ThirdPersonBat->SetCastHiddenShadow(false); // 注册前关闭默认投影；下方唯一装备/生命门统一开启，避免晚初始化或收起时留下隐藏刀影。
        ThirdPersonBat->RegisterComponent(); // 世界观察者保留真实hand_r挂点；本人单一门选择原TP/正常尺度代理，约0.2相机空间刀仍不投影。
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_TP_READY owner=%s socket=%s"),*Pawn->GetName(),*Definition->HandSocket.ToString());
    }
    if (State.bEquipped && Pawn->IsLocallyControlled() && !bSharedBody && !FirstPersonArms)
    {
        const UDemoPlayerAppearance* Appearance=Pawn->GetPlayerPresentation()?Pawn->GetPlayerPresentation()->GetAppearance():nullptr; // 外观组件保活，等待其BeginPlay完成后重试，不在Tick触发IO。
        USkeletalMesh* MeleeArms=Definition->FirstPersonArmsMesh?Definition->FirstPersonArmsMesh.Get():Appearance?Appearance->ArmsMesh.Get():nullptr; // 定义优先；不在Tick加载或推断兼容骨架。
        if (!MeleeArms) return;
        FirstPersonArms=NewObject<USkeletalMeshComponent>(Pawn,TEXT("MeleeFirstPersonArms")); Pawn->AddInstanceComponent(FirstPersonArms);
        FirstPersonArms->SetupAttachment(Pawn->GetFirstPersonCameraComponent());
        FTransform RenderTransform=Definition->FirstPersonOffset; // 完整未缩作者框；只在相机直接子件压缩，刀GripOffset/骨姿不再乘一次。
        const float RenderScale=Pawn->GetPlayerPresentation()?Pawn->GetPlayerPresentation()->GetFirstPersonRenderScale():1.f; // 本人显示值，不参与服务器近战扫掠。
        RenderTransform.SetTranslation(Definition->FirstPersonOffset.GetTranslation()*RenderScale);
        RenderTransform.SetScale3D(Definition->FirstPersonOffset.GetScale3D()*RenderScale);
        FirstPersonArms->SetRelativeTransform(RenderTransform);
        FirstPersonArms->SetSkeletalMesh(MeleeArms); FirstPersonArms->SetAnimInstanceClass(Definition->FirstPersonAnimClass);
        FirstPersonArms->SetOnlyOwnerSee(true); FirstPersonArms->SetCollisionEnabled(ECollisionEnabled::NoCollision); FirstPersonArms->CastShadow=false;
        FirstPersonArms->SetVisibleInRayTracing(false); // 专用相机空间剑手不进入世界HWRT反射，完整TP仍承担世界表示。
        FirstPersonArms->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        FirstPersonArms->RegisterComponent(); FirstPersonArms->SetComponentTickEnabled(false); // 由本组件PostUpdate统一求值，ASC仍绑定原枪械Mesh1P。
        for (int32 Slot=0;!Definition->FirstPersonArmsMesh && Slot<FirstPersonArms->GetNumMaterials();++Slot) // 独立网格使用自身材质；仅旧共享网格继承Pawn的MID槽位。
            FirstPersonArms->SetMaterial(Slot,Pawn->GetMesh1P()->GetMaterial(Slot));
        for (int32 Slot=0;Slot<Definition->FirstPersonMaterials.Num() && Slot<FirstPersonArms->GetNumMaterials();++Slot) // 武器定义保活覆盖材质；不修改共享网格/原材质包。
            if (Definition->FirstPersonMaterials[Slot]) FirstPersonArms->SetMaterial(Slot,Definition->FirstPersonMaterials[Slot]);
        FirstPersonBat=NewObject<UStaticMeshComponent>(Pawn,TEXT("MeleeFirstPersonBat")); Pawn->AddInstanceComponent(FirstPersonBat);
        FirstPersonBat->SetupAttachment(FirstPersonArms,Definition->HandSocket); FirstPersonBat->SetStaticMesh(Definition->Mesh);
        FirstPersonBat->SetRelativeTransform(Definition->GripOffset); FirstPersonBat->SetOnlyOwnerSee(true);
        FirstPersonBat->SetVisibleInRayTracing(false); // 本人刀与TP刀分开，避免反射出现第二份相机空间武器。
        FirstPersonBat->SetCollisionEnabled(ECollisionEnabled::NoCollision); FirstPersonBat->CastShadow=false; FirstPersonBat->RegisterComponent();
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_FP_READY owner=%s graph=%s offset=%s"),*Pawn->GetName(),*GetNameSafe(Definition->FirstPersonAnimClass),*Definition->FirstPersonOffset.ToHumanReadableString());
    }
    if (ThirdPersonBat)
    {
        const bool bShowThirdPersonWeapon=State.bEquipped && !Pawn->IsCoopEliminated(); // 沿用原TP实体的权威装备/阵亡显隐门；不是本地输入或FP是否已创建的推断。
        ThirdPersonBat->SetHiddenInGame(!bShowThirdPersonWeapon);
        ThirdPersonBat->SetOwnerNoSee(!bSharedBody); // 同一世界武器保持真实hand_r与GripOffset；独立FP模式下只隐藏其本人实体。
        RefreshOwnerShadowPolicy(); // 自己的唯一写入点；同帧代理切换也走这里，不由两个Tick轮流抢写。
    }
    if (FirstPersonArms)
    {
        FTransform RenderTransform=Definition->FirstPersonOffset; // Appearance/占有权可能晚到；每次从未缩作者框重建期望值，不能只在第一次创建缩放。
        const float RenderScale=Pawn->GetPlayerPresentation()?Pawn->GetPlayerPresentation()->GetFirstPersonRenderScale():1.f;
        RenderTransform.SetTranslation(Definition->FirstPersonOffset.GetTranslation()*RenderScale);
        RenderTransform.SetScale3D(Definition->FirstPersonOffset.GetScale3D()*RenderScale);
        if (!FirstPersonArms->GetRelativeTransform().Equals(RenderTransform)) FirstPersonArms->SetRelativeTransform(RenderTransform); // 值未变化不刷新变换，无累乘/动画重新取样。
        FirstPersonArms->SetHiddenInGame(bSharedBody || !State.bEquipped || Pawn->IsCoopEliminated() || !Pawn->IsLocallyControlled(),true);
    }
}
void UDemoMeleeComponent::OnRep_State()
{
    DEMO_LOG_CALL();
    if(State.LastCancelledSerial!=0 && !CancelledCameraSerials.Contains(State.LastCancelledSerial))
    {
        if(CancelledCameraSerials.Num()>=16) CancelledCameraSerials.RemoveAt(0);
        CancelledCameraSerials.Add(State.LastCancelledSerial); // 即使取消后立即新刀、复制被合并，也只禁旧刀HP回执，不清新刀反馈。
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_CAMERA_AUTHORITY_CANCEL serial=%u current=%u"),State.LastCancelledSerial,State.Serial);
    }
    RememberCameraAuthority(State); // 属性和可靠起刀确认均可提供事实，HP批次早到时稍后再有限消费。
    if (!bUsePrediction && SoundSerial!=State.Serial) { SoundSerial=State.Serial; bSwingPlayed=false; } // 拥有者确认不重播已预测的挥声。
    const bool bAuthorityCancelled=State.Serial!=0 && State.LastCancelledSerial==State.Serial; // 只有当前刀命中持久撤销标识才属于显式取消，历史父刀不影响新请求。
    if (bUsePrediction && (!State.bEquipped || (State.OwnerRequestId==PredictedState.OwnerRequestId && State.Attack==INDEX_NONE && (IsAttacking() || bAuthorityCancelled))
        || (bHasPendingConfirmedComboStart && State.OwnerRequestId==PendingConfirmedComboStart.OwnerRequestId && State.Attack==INDEX_NONE)))
        CancelPredictedPresentation(!State.bEquipped || bAuthorityCancelled); // 自然结束保有限末刀HP；显式GAS取消即使本机已Idle也撤包络，不复活动作。
    if (bUsePrediction && !bPredictionCancelled && State.OwnerRequestId==PredictedState.OwnerRequestId && State.Attack==PredictedState.Attack && State.Attack!=INDEX_NONE)
    {
        if (State.bComboExitCommitted) PredictedState.bComboExitCommitted=true; // 旧未承诺复制不能撤回本人尚待独立回执的点击。
        PredictedState.NextHorizontalCursor=State.NextHorizontalCursor;
        LocalRecoveryUntil=SegmentEndTime(PredictedState);
    }
    GetIdlePoseTime(false); // 属性到达并完成原预测收敛后观察一次；远端暂停求值时收到卸下/取消也不会保留旧epoch。
    UpdatePresentation();
}
void UDemoMeleeComponent::MulticastWorldImpact_Implementation(FVector_NetQuantize Location)
{
    DEMO_LOG_CALL();
    if (Definition) DemoWeaponAudio::PlayAtLocation(this,Definition->WorldImpactSound.Get(),Location,TEXT("MeleeWorld")); // 只保留环境音；怪物声音由实际扣血回执送入固定池。
}
// OutLifetimeProps为引擎复制注册输出；DOREPLIFETIME宏要求该参数名，修复编译而不改变复制字段/语义。
void UDemoMeleeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UDemoMeleeComponent,State); }
void UDemoMeleeComponent::ServerSetEquipped_Implementation(bool bEquip) { DEMO_LOG_CALL(); SetEquipped(bEquip); }
void UDemoMeleeComponent::ServerCancelAttack_Implementation() { DEMO_LOG_CALL(); CancelAttack(); }
void UDemoMeleeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); CancelAttack(); Session.Reset();
    ClearCameraFeedback(); // Cancel回调可能刷新表现，最终EndPlay再次幂等撤销本实例。
    if (FirstPersonBat) FirstPersonBat->DestroyComponent(); // 子组件先释放，Pawn旅行不遗留相机空间外观。
    if (FirstPersonArms) FirstPersonArms->DestroyComponent();
    if (ThirdPersonBat) ThirdPersonBat->DestroyComponent();
    Super::EndPlay(Reason);
}
