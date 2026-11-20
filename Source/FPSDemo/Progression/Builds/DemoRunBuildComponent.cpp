// 职责：核心选择事务与GAS事件桥接。使用：权威Flow生命周期驱动；客户端只读本人复制，保存由外部Assembler处理。
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Builds/DemoRunBuildSettings.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "GAS/Talents/Builds/DemoCrossfireBuildRuntime.h"
#include "GAS/Talents/Builds/DemoAnchorBuildRuntime.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "GAS/Talents/DemoTalentComponent.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Player/DemoPlayerState.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoCombatSession.h"
#include "AbilitySystemComponent.h"
#include "Templates/UnrealTemplate.h" // 同步事件嵌套深度用RAII恢复，取消/拒绝也不会泄漏池槽。
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Debug/DemoEditorCombatPrototype.h" // 只观察已确认伤害；候选GA未启用时无监听者，不改变正式行为。
#endif

namespace
{
    /** 游戏线程全序仅比较动作先后；跨World共用仍单调，不保存、不接受客户端输入。 */
    int64 NextBuildActionSerial()
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] NextBuildActionSerial"));
        static int64 Serial=0; // 进程内权威游戏线程拥有，生命周期远超单场但不携带玩法状态。
        check(IsInGameThread()); return ++Serial;
    }
}

UDemoRunBuildComponent::UDemoRunBuildComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(true); }
const FDemoRunBuildSnapshot& UDemoRunBuildComponent::GetSnapshot() const { DEMO_LOG_TICK(); return Snapshot; }
const FDemoRunBuildRuntimeView& UDemoRunBuildComponent::GetRuntimeView() const { DEMO_LOG_TICK(); return RuntimeView; }
bool UDemoRunBuildComponent::HasSelectedCore() const { DEMO_LOG_TICK(); return !PublicCoreId.IsNone(); }
bool UDemoRunBuildComponent::IsEncounterEnabled() const { DEMO_LOG_TICK(); return bEncounterEnabled; }
UAbilitySystemComponent* UDemoRunBuildComponent::GetASC() const
{
    DEMO_LOG_TICK(); const ADemoPlayerState* State=Cast<ADemoPlayerState>(GetOwner()); // 本人PS拥有ASC，组件不缓存可替换Avatar。
    return State?State->GetAbilitySystemComponent():nullptr;
}
UDemoRunBuildComponent* UDemoRunBuildComponent::FindForPawn(const ADemoCharacter* Pawn)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] UDemoRunBuildComponent::FindForPawn")); const APlayerState* State=IsValid(Pawn)?Pawn->GetPlayerState():nullptr; // 只沿真实所有权解析，不允许Player0回退。
    return State?State->FindComponentByClass<UDemoRunBuildComponent>():nullptr;
}
void UDemoRunBuildComponent::PublishSelection()
{
    DEMO_LOG_CALL(); PublicCoreId=Snapshot.CoreId;
    if(GetOwner()) GetOwner()->ForceNetUpdate();
    UE_LOG(LogFPSDemo,Log,TEXT("BUILD_SELECTION core=%s branch3=%s branch7=%s pending=%d revision=%d locked=%d"),*Snapshot.CoreId.ToString(),*Snapshot.Branch3.ToString(),*Snapshot.Branch7.ToString(),Snapshot.PendingBranchStage,Snapshot.Revision,Snapshot.bLocked);
}
bool UDemoRunBuildComponent::TrySelectCore(FName CoreId)
{
    DEMO_LOG_CALL(); const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 权威阶段快照。
    FDemoRunBuildSnapshot Candidate; Candidate.CoreId=CoreId; Candidate.Revision=Snapshot.Revision<MAX_int32?Snapshot.Revision+1:MAX_int32; // 新核心清本轮分支，不操作旧无限天赋。
    if(!GetOwner()->HasAuthority()||!State||State->Phase!=EDemoPhase::Hub||Snapshot.bLocked||CoreId.IsNone()||!Candidate.Validate())
    { UE_LOG(LogFPSDemo,Log,TEXT("BUILD_SELECT_REJECT authority/hub/locked/id")); return false; }
    EndEncounter(); Snapshot=Candidate; PublishSelection(); return true;
}
bool UDemoRunBuildComponent::OfferBranch(int32 Stage)
{
    DEMO_LOG_CALL(); FDemoRunBuildSnapshot Candidate=Snapshot; Candidate.PendingBranchStage=Stage; // 只修改候选，失败不吞旧资格。
    if(!GetOwner()->HasAuthority()||!Snapshot.bLocked||Snapshot.PendingBranchStage!=0||(Stage!=3&&Stage!=7)||!Candidate.Validate())
    { UE_LOG(LogFPSDemo,Log,TEXT("BUILD_OFFER_REJECT stage=%d locked/pending/sequence"),Stage); return false; }
    Snapshot=Candidate; Snapshot.Revision=FMath::Min<int64>(static_cast<int64>(Snapshot.Revision)+1,MAX_int32); PublishSelection(); return true;
}
bool UDemoRunBuildComponent::TrySelectBranch(FName BranchId)
{
    DEMO_LOG_CALL(); const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 关间领取不能在飞弹期间修改快照。
    FDemoRunBuildSnapshot Candidate=Snapshot; // 提交前验证完整互斥关系。
    if(Candidate.PendingBranchStage==3) Candidate.Branch3=BranchId; else if(Candidate.PendingBranchStage==7) Candidate.Branch7=BranchId;
    if(!GetOwner()->HasAuthority()||!State||State->Phase!=EDemoPhase::Reward||BranchId.IsNone()||Snapshot.PendingBranchStage==0)
    { UE_LOG(LogFPSDemo,Log,TEXT("BUILD_BRANCH_REJECT authority/phase/offer")); return false; }
    Candidate.PendingBranchStage=0;
    if(!Candidate.Validate()) { UE_LOG(LogFPSDemo,Log,TEXT("BUILD_BRANCH_REJECT invalid branch=%s"),*BranchId.ToString()); return false; }
    Candidate.Revision=FMath::Min<int64>(static_cast<int64>(Candidate.Revision)+1,MAX_int32); Snapshot=Candidate; PublishSelection(); return true;
}
UDemoRunBuildAbility* UDemoRunBuildComponent::EnsureBehavior()
{
    DEMO_LOG_CALL(); UAbilitySystemComponent* ASC=GetASC(); // 原PS ASC，不创建额外组件。
    UDemoTalentComponent* Talents=GetOwner()->FindComponentByClass<UDemoTalentComponent>(); // 原句柄所有者。
    if(!ASC||!Talents||!GetOwner()->HasAuthority()) return nullptr;
    const FGameplayAbilitySpecHandle Handle=Talents->EnsureRunBuildAbility(ASC,this); // 幂等授予，保留其他GA冷却。
    FGameplayAbilitySpec* Spec=ASC->FindAbilitySpecFromHandle(Handle); // 不跨GiveAbility保留旧Spec裸指针。
    Behavior=Spec?Cast<UDemoRunBuildAbility>(Spec->GetPrimaryInstance()):nullptr;
    return Behavior.Get();
}
bool UDemoRunBuildComponent::PrepareEncounter(FString& Error)
{
    DEMO_LOG_CALL(); Error.Reset();
    UAbilitySystemComponent* ASC=GetASC(); // 原PS拥有者，预检不初始化或替换Avatar。
    if(!GetOwner()||!GetOwner()->HasAuthority()) Error=TEXT("核心预检需要服务器权限");
    else if(Snapshot.CoreId.IsNone()||!Snapshot.Validate()) Error=TEXT("请先选择有效的本轮核心");
    else if(Snapshot.PendingBranchStage!=0) Error=TEXT("请先领取本关核心分支");
    else if(!GetDefault<UDemoRunBuildSettings>()->Validate()) Error=TEXT("核心数值配置无效，请检查Demo Run Builds设置");
    else if(!GetDefault<UDemoExpandedBuildSettings>()->Validate()) Error=TEXT("扩展核心数值配置无效，请检查Demo Expanded Builds设置");
    else if(!ASC||!ASC->IsOwnerActorAuthoritative()||ASC->GetOwnerActor()!=GetOwner()||!IsValid(ASC->GetAvatarActor())) Error=TEXT("玩家GAS或角色尚未就绪");
    else if(!EnsureBehavior()) Error=TEXT("核心GAS行为授予失败");
    if(!Error.IsEmpty()) { UE_LOG(LogFPSDemo,Warning,TEXT("BUILD_PREPARE_REJECT %s"),*Error); return false; }
    return true; // 只建立依赖；不启动事件、不改选择/计数/冷却，不持有不存在的会话。
}
void UDemoRunBuildComponent::BeginEncounter()
{
    DEMO_LOG_CALL();
    UDemoCombatSession* Session=UDemoCombatSession::FindActive(GetWorld()); // Flow已提交同场凭据；不能在Hub预开行为。
    if(bEncounterEnabled&&EnabledSession.Get()==Session&&Session) return; // 显式初始化幂等，不因重复准备重置当前充能。
    EndEncounter();
    FString Error; // 最终阶段提交仍保留依赖检查；出发前的可恢复错误应已经在Prepare暴露。
    if(!Session||!PrepareEncounter(Error))
    { UE_LOG(LogFPSDemo,Warning,TEXT("BUILD_BEGIN_REJECT authority/selection/pending/config/GAS")); return; }
    bEncounterEnabled=true; EnabledSession=Session; Snapshot.bLocked=true; PublishSelection();
    if(!Session->RegisterCleanup(this,FSimpleDelegate::CreateUObject(this,&UDemoRunBuildComponent::EndEncounter))) EndEncounter(); // 会话撤销同栈清理，弱UObject绑定不跨World；注册失败不能留下开启状态。
    else if(Behavior.IsValid()) Behavior->OnEncounterStarted(this);
}
void UDemoRunBuildComponent::EndEncounter()
{
    DEMO_LOG_CALL(); if(!GetOwner()||!GetOwner()->HasAuthority()) return;
    bEncounterEnabled=false; EnabledSession.Reset(); if(Behavior.IsValid()) Behavior->ResetRuntime(); RuntimeView=FDemoRunBuildRuntimeView(); GetOwner()->ForceNetUpdate();
}
void UDemoRunBuildComponent::ResetRun()
{
    DEMO_LOG_CALL(); if(!GetOwner()->HasAuthority()) return;
    EndEncounter(); const int32 Revision=FMath::Min<int64>(static_cast<int64>(Snapshot.Revision)+1,MAX_int32); // 重置也保持单调修订号。
    Snapshot=FDemoRunBuildSnapshot(); Snapshot.Revision=Revision; PublishSelection();
}
bool UDemoRunBuildComponent::RestoreSnapshot(const FDemoRunBuildSnapshot& Value)
{
    DEMO_LOG_CALL(); if(!GetOwner()->HasAuthority()||!Value.Validate()) { UE_LOG(LogFPSDemo,Warning,TEXT("BUILD_RESTORE_REJECT invalid snapshot")); return false; }
    EndEncounter(); Snapshot=Value; PublishSelection(); return true;
}
void UDemoRunBuildComponent::SetRuntimeView(const FDemoRunBuildRuntimeView& Value)
{
    DEMO_LOG_TICK(); if(!GetOwner()->HasAuthority()) return;
    if(RuntimeView.PrecisionStacks==Value.PrecisionStacks&&RuntimeView.ChargedShots==Value.ChargedShots&&FMath::IsNearlyEqual(RuntimeView.ServerEndTime,Value.ServerEndTime)&&RuntimeView.ActionState==Value.ActionState&&RuntimeView.ActionStacks==Value.ActionStacks&&RuntimeView.ActionCapacity==Value.ActionCapacity&&FMath::IsNearlyEqual(RuntimeView.CooldownEndTime,Value.CooldownEndTime)) return;
    RuntimeView=Value; GetOwner()->ForceNetUpdate();
    OnRep_RuntimeView(); // Listen房主与服务器同栈同步B09-C开镜移速。
}
void UDemoRunBuildComponent::OnRep_RuntimeView()
{
    DEMO_LOG_TICK();
    if(Snapshot.CoreId!=TEXT("B09")||Snapshot.Branch7!=TEXT("B09-C")) return;
    UAbilitySystemComponent* ASC=GetASC();
    if(ADemoCharacter* Character=ASC?Cast<ADemoCharacter>(ASC->GetAvatarActor()):nullptr)
        if(Character->GetWeaponComponent()&&Character->GetWeaponComponent()->GetScopeLevel()>0) Character->RefreshMovementSpeed();
}
float UDemoRunBuildComponent::GetAimMoveMitigation() const
{
    DEMO_LOG_TICK();
    if(Snapshot.CoreId!=TEXT("B09")||Snapshot.Branch7!=TEXT("B09-C")) return 0.f;
    const bool bHalfFocused=RuntimeView.ActionState==TEXT("Marksman.Ready")
        ||(RuntimeView.ActionState==TEXT("Marksman.Charging")&&RuntimeView.ActionCapacity>0&&RuntimeView.ActionStacks*2>=RuntimeView.ActionCapacity);
    return bHalfFocused?FMath::Clamp(GetDefault<UDemoExpandedBuildSettings>()->MarksmanScopeMoveMitigationC,0.f,1.f):0.f;
}
void UDemoRunBuildComponent::Dispatch(UDemoRunBuildEventContext* Event)
{
    DEMO_LOG_TICK(); UAbilitySystemComponent* ASC=GetASC(); // 每次重新解析，绝不向其他ASC广播。
    if(!Event||!GetOwner()->HasAuthority()||!bEncounterEnabled||!ASC||!Behavior.IsValid()) return;
    FGameplayEventData Data; Data.EventTag=DemoRunBuildTags::Action; Data.Instigator=ASC->GetAvatarActor(); Data.OptionalObject=Event; // 事件只同步借用，调用者强引用保活。
    ASC->HandleGameplayEvent(Data.EventTag,&Data);
}
UDemoRunBuildEventContext* UDemoRunBuildComponent::AcquireEvent()
{
    DEMO_LOG_TICK();
    if(!GetOwner()->HasAuthority()||!bEncounterEnabled) return nullptr;
    if(EventDepth>=8) { UE_LOG(LogFPSDemo,Warning,TEXT("BUILD_EVENT_REJECT excessive synchronous recursion")); return nullptr; }
    if(EventPool.Num()<=EventDepth) EventPool.Add(NewObject<UDemoRunBuildEventContext>(this)); // 只有首次达到本嵌套深度时分配，后续数万次直击重用。
    UDemoRunBuildEventContext* Event=EventPool[EventDepth]; // PS强持有，调用方只借用到DepthGuard退出。
    Event->Action=EDemoRunBuildAction::PreviewShot; Event->Shot=FDemoRunBuildShotSnapshot(); Event->ShotId.Invalidate(); Event->Enemy.Reset(); Event->Weapon.Reset(); Event->Pawn.Reset(); Event->Direction=FVector::ZeroVector; Event->Damage=0.f; Event->Duration=0.f; Event->Multiplier=1.f; Event->bWeakPoint=false; Event->bKilled=false; Event->bFirstEnemy=false; Event->bSameFirstEnemy=false; Event->bFirstTarget=false; Event->bSecondary=false; Event->bFrozenBefore=false; Event->bAccepted=false;
    Event->ActualDamage=0.f; Event->Sequence=0; Event->Serial=0; Event->Location=FVector::ZeroVector; Event->HitLocation=FVector::ZeroVector; Event->Distance=0.f; Event->HitDistance=0.f; Event->OverkillDamage=0.f; Event->TargetPelletsHit=0; Event->ShotPelletsHit=0; Event->ShotTotalDamage=0.f; Event->bShotHadWeakPoint=false; Event->bSuccess=false; Event->bHadEnemyHit=false; Event->SourceEnemy.Reset();
    return Event;
}
FDemoRunBuildShotSnapshot UDemoRunBuildComponent::PreviewShot(ADemoWeaponBase* FiringWeapon)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return FDemoRunBuildShotSnapshot(); // 当前同步池槽借用。
    const TGuardValue<int32> DepthGuard(EventDepth,EventDepth+1); // 只在本调用栈占槽，重入使用下一槽，所有退出路径自动恢复。
    if(!FiringWeapon)
        if(const ADemoCharacter* Character=Cast<ADemoCharacter>(GetASC()?GetASC()->GetAvatarActor():nullptr))
            FiringWeapon=Character->GetWeaponComponent()?Character->GetWeaponComponent()->GetActiveWeapon():nullptr;
    Event->Action=EDemoRunBuildAction::PreviewShot; Event->Weapon=FiringWeapon; Event->Shot.CoreId=Snapshot.CoreId; Event->Shot.Branch3=Snapshot.Branch3; Event->Shot.Branch7=Snapshot.Branch7;
    if(FiringWeapon)
    {
        Event->Shot.FireInterval=FiringWeapon->GetFireInterval(); // 发射冻结实际射速间隔，供慢射速重武器适配。
        Event->Shot.PlannedProjectiles=FMath::Max(1,FiringWeapon->GetProjectilesPerShot()); // 发射冻结多弹丸数。
        Event->Shot.bBoltAction=FiringWeapon->Config.bCycleBoltAfterShot; // 发射冻结拉栓特性。
    }
    Dispatch(Event); return Event->Shot;
}
void UDemoRunBuildComponent::CommitShot(FDemoRunBuildShotSnapshot& Shot,const FGuid& ShotId,ADemoWeaponBase* FiringWeapon)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> DepthGuard(EventDepth,EventDepth+1); // 本次发射占用一槽，委托重入不能覆写其参数。
    Shot.CommitSerial=NextBuildActionSerial(); if(const ADemoCharacter* Source=Cast<ADemoCharacter>(GetASC()?GetASC()->GetAvatarActor():nullptr)) Shot.AimDirection=Source->GetBaseAimRotation().Vector();
    Event->Action=EDemoRunBuildAction::CommitShot; Event->Shot=Shot; Event->ShotId=ShotId; Event->Serial=Shot.CommitSerial; Event->Weapon=FiringWeapon; Dispatch(Event); Shot=Event->Shot;
}
float UDemoRunBuildComponent::GetDirectMultiplier(const FDemoRunBuildShotSnapshot& Shot,const FGuid& ShotId,ADemoEnemy* Enemy,bool bWeakPoint,bool bFirstEnemy,bool bSameFirstEnemy,float HitDistance,bool bSecondary)
{
    DEMO_LOG_TICK(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return 1.f;
    const TGuardValue<int32> DepthGuard(EventDepth,EventDepth+1); // 同步前置查询，无命中事实不消费标记。
    Event->Action=EDemoRunBuildAction::BeforeHit; Event->Shot=Shot; Event->ShotId=ShotId; Event->Enemy=Enemy; Event->bWeakPoint=bWeakPoint; Event->bFirstEnemy=bFirstEnemy; Event->bSameFirstEnemy=bSameFirstEnemy; Event->HitDistance=HitDistance; Event->bSecondary=bSecondary; Dispatch(Event); return Event->Multiplier;
}
void UDemoRunBuildComponent::NotifyDirectHit(const FDemoRunBuildShotSnapshot& Shot,const FGuid& ShotId,ADemoEnemy* Enemy,float Damage,float OverkillDamage,const FVector& HitPoint,float HitDistance,bool bWeakPoint,bool bKilled,bool bFirstEnemy,bool bSameFirstEnemy,bool bFirstTarget,int32 TargetPelletsHit,int32 ShotPelletsHit,bool bSecondary,ADemoWeaponBase* FiringWeapon)
{
    DEMO_LOG_TICK(); if(!FMath::IsFinite(Damage)||Damage<=0.f) return; // 散弹每颗有回执，保留埋点但默认不重复打印已有伤害日志。
#if WITH_EDITOR
    // 源Pawn从本人PS ASC解析；在正式行为Dispatch门禁前广播，停用正式核心的候选仍能观察真实扣血。
    if(Snapshot.CoreId!=TEXT("B05")) if(UAbilitySystemComponent* SourceASC=GetASC()) DemoEditorCombatPrototype::DirectConfirmed().Broadcast(Cast<ADemoCharacter>(SourceASC->GetAvatarActor()),Enemy,ShotId,Damage,bKilled,bSecondary,bFirstTarget);
#endif
    UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> DepthGuard(EventDepth,EventDepth+1); // 嵌套死亡只能借用下一个槽，不覆盖当前实际HP回执。
    Event->Action=EDemoRunBuildAction::AfterHit; Event->Shot=Shot; Event->ShotId=ShotId; Event->Enemy=Enemy; Event->Damage=Damage; Event->OverkillDamage=FMath::Max(0.f,OverkillDamage); Event->HitLocation=HitPoint; Event->HitDistance=FMath::Max(0.f,HitDistance); Event->bWeakPoint=bWeakPoint; Event->bKilled=bKilled; Event->bFirstEnemy=bFirstEnemy; Event->bSameFirstEnemy=bSameFirstEnemy; Event->bFirstTarget=bFirstTarget; Event->TargetPelletsHit=TargetPelletsHit; Event->ShotPelletsHit=ShotPelletsHit; Event->bSecondary=bSecondary; Event->Weapon=FiringWeapon; Event->Direction=Shot.AimDirection;
    UDemoCrossfireBuildRuntime::NotifyTeamGun(this,*Event); UDemoAnchorBuildRuntime::NotifyTeamHit(this,*Event); Dispatch(Event);
}
void UDemoRunBuildComponent::NotifyDashStarted(ADemoCharacter* Pawn,float Duration)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> DepthGuard(EventDepth,EventDepth+1); // 成功权威RootMotion后的起点请求，本同步栈借用。
    Event->Action=EDemoRunBuildAction::DashStarted; Event->Pawn=Pawn; Event->Duration=Duration; Dispatch(Event);
}
bool UDemoRunBuildComponent::NotifyMeleeHit(ADemoEnemy* Enemy,float RawDamage,float ActualDamage,bool bFrozenBefore,bool bKilled,const FVector& Direction)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    // 调用者已确认实际HP损失；原型只挂破势，不接管普通近战击退，也不修改正式核心选择/保存。
    if(Snapshot.CoreId!=TEXT("B05")) if(UAbilitySystemComponent* SourceASC=GetASC()) DemoEditorCombatPrototype::MeleeConfirmed().Broadcast(Cast<ADemoCharacter>(SourceASC->GetAvatarActor()),Enemy,RawDamage,bKilled);
#endif
    UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return false;
    const TGuardValue<int32> DepthGuard(EventDepth,EventDepth+1); // 原目标死亡可同步清Debuff，使用独立槽里的命中前状态。
    Event->Action=EDemoRunBuildAction::Melee; Event->Enemy=Enemy; Event->Damage=RawDamage; Event->ActualDamage=ActualDamage; Event->Serial=NextBuildActionSerial(); Event->bFrozenBefore=bFrozenBefore; Event->bKilled=bKilled; Event->Direction=Direction;
    UDemoCrossfireBuildRuntime::NotifyTeamMelee(this,*Event); Dispatch(Event); return Event->bAccepted;
}
void UDemoRunBuildComponent::NotifyAvatarDefeated()
{
    DEMO_LOG_CALL(); if(!GetOwner()||!GetOwner()->HasAuthority()) return;
    if(Behavior.IsValid()) Behavior->ResetRuntime(); RuntimeView=FDemoRunBuildRuntimeView(); GetOwner()->ForceNetUpdate(); // 不EndEncounter/ResetRun，不删除个人选择或队伍参战资格。
}
void UDemoRunBuildComponent::NotifyReloadStarted(ADemoWeaponBase* Weapon,int32 SessionSequence)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> Guard(EventDepth,EventDepth+1); // 同步借用池槽，递归事件使用下一槽。
    Event->Action=EDemoRunBuildAction::ReloadStarted; Event->Weapon=Weapon; Event->Sequence=SessionSequence; Dispatch(Event);
}
bool UDemoRunBuildComponent::NotifyReloadPressed(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return false;
    const TGuardValue<int32> Guard(EventDepth,EventDepth+1); // 同步R请求只返回服务器判定。
    Event->Action=EDemoRunBuildAction::ReloadPressed; Event->Weapon=Weapon; Dispatch(Event); return Event->bAccepted;
}
void UDemoRunBuildComponent::NotifyReloadFinished(ADemoWeaponBase* Weapon,int32 SessionSequence,bool bSuccess)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> Guard(EventDepth,EventDepth+1); // 最终GA结束事实，逐颗阶段不提交。
    Event->Action=EDemoRunBuildAction::ReloadFinished; Event->Weapon=Weapon; Event->Sequence=SessionSequence; Event->bSuccess=bSuccess; Dispatch(Event);
}
void UDemoRunBuildComponent::NotifyWeaponChanged(ADemoWeaponBase* OldWeapon,ADemoWeaponBase* NewWeapon)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> Guard(EventDepth,EventDepth+1); // 策略记录的原枪与提交后新枪比较，不赋予旧弹新所有权。
    Event->Action=EDemoRunBuildAction::WeaponChanged; Event->Weapon=NewWeapon; Dispatch(Event);
}
void UDemoRunBuildComponent::NotifyShotResolved(const FDemoRunBuildShotSnapshot& Shot,const FGuid& ShotId,bool bHadEnemyHit,bool bHadWeakPoint,int32 TotalPelletsHit,float TotalDamage,ADemoEnemy* PrimaryEnemy,const FVector& PrimaryImpact,ADemoWeaponBase* FiringWeapon)
{
    DEMO_LOG_TICK(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> Guard(EventDepth,EventDepth+1); // 全弹丸终结只一次，不能由下一枪猜测是否落空。
    Event->Action=EDemoRunBuildAction::ShotResolved; Event->Shot=Shot; Event->ShotId=ShotId; Event->bHadEnemyHit=bHadEnemyHit; Event->bShotHadWeakPoint=bHadWeakPoint; Event->ShotPelletsHit=TotalPelletsHit; Event->ShotTotalDamage=TotalDamage; Event->Enemy=PrimaryEnemy; Event->HitLocation=PrimaryImpact; Event->Weapon=FiringWeapon; Event->Direction=Shot.AimDirection; Dispatch(Event);
}
void UDemoRunBuildComponent::NotifyPingAccepted(FVector Location)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event||Location.ContainsNaN()) return;
    const TGuardValue<int32> Guard(EventDepth,EventDepth+1); // 标点已成功，部署失败只影响锚点。
    Event->Action=EDemoRunBuildAction::PingAccepted; Event->Location=Location; Dispatch(Event);
}
void UDemoRunBuildComponent::NotifyPlayerDamaged(ADemoEnemy* SourceEnemy,float ActualDamage)
{
    DEMO_LOG_CALL(); if(!IsValid(SourceEnemy)||!SourceEnemy->IsCombatSessionValid()||SourceEnemy->GetWorld()!=GetWorld()||!FMath::IsFinite(ActualDamage)||ActualDamage<=0.f) return;
    UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> Guard(EventDepth,EventDepth+1); // 确认真实损失后事件，来源不是客户端声明。
    Event->Action=EDemoRunBuildAction::PlayerDamaged; Event->SourceEnemy=SourceEnemy; Event->Damage=ActualDamage; Dispatch(Event);
}
void UDemoRunBuildComponent::NotifyMarkedTargetKilled(ADemoEnemy* Enemy,UDemoRunBuildComponent* Killer)
{
    DEMO_LOG_CALL(); UDemoRunBuildEventContext* Event=AcquireEvent(); if(!Event) return;
    const TGuardValue<int32> Guard(EventDepth,EventDepth+1); // 标定目标死亡由TargetState清理标记后触发，不重入。
    Event->Action=EDemoRunBuildAction::MarkedTargetKilled; Event->Enemy=Enemy; Event->bSuccess=Killer!=nullptr; Dispatch(Event);
}
void UDemoRunBuildComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UDemoRunBuildComponent,PublicCoreId); DOREPLIFETIME_CONDITION(UDemoRunBuildComponent,Snapshot,COND_OwnerOnly); DOREPLIFETIME_CONDITION(UDemoRunBuildComponent,RuntimeView,COND_OwnerOnly);
}
void UDemoRunBuildComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); EndEncounter(); Behavior.Reset(); Super::EndPlay(Reason);
}
