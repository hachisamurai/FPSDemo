// 职责：Owner受击表现的发送/接收/代次隔离与固定本机资源；HP和死亡始终由原GAS/Flow裁决。
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackComponent.h"
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackProfile.h"
#include "Combat/Feedback/Player/DemoDamageCameraModifier.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Settings/DemoGameUserSettings.h"
#include "AbilitySystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Debug/DemoLog.h"
namespace
{
    /** CDO保活的固定小组选择相邻不重复；失败只静音，不替换为别类声音。 */
    USoundWave* PickDamageSound(const TArray<TObjectPtr<USoundWave>>& Group,int32& Last)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] PickDamageSound"));
        if(Group.Num()!=3||Group.Contains(nullptr)) return nullptr; // 本批三独立源合同；不完整组初始化诊断一次，命中时安全静音。
        int32 Index=FMath::RandRange(0,Group.Num()-1); // 声音随机不影响服务器业务。
        if(Group.Num()>1&&Index==Last) Index=(Index+1+FMath::RandRange(0,Group.Num()-2))%Group.Num();
        Last=Index; return Group[Index];
    }
}
UDemoPlayerDamageFeedbackComponent::UDemoPlayerDamageFeedbackComponent()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(true);
    ProfileAsset=TSoftObjectPtr<UDemoPlayerDamageFeedbackProfile>(FSoftObjectPath(TEXT("/Game/Feedback/Player/DA_PlayerDamageFeedback.DA_PlayerDamageFeedback")));
}
UDemoPlayerDamageFeedbackComponent* UDemoPlayerDamageFeedbackComponent::Find(const APlayerController* Controller)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] UDemoPlayerDamageFeedbackComponent::Find")); return IsValid(Controller)?Controller->FindComponentByClass<UDemoPlayerDamageFeedbackComponent>():nullptr;
}
ADemoPlayerController* UDemoPlayerDamageFeedbackComponent::PC() const { DEMO_LOG_TICK(); return Cast<ADemoPlayerController>(GetOwner()); }
float UDemoPlayerDamageFeedbackComponent::ServerNow() const
{
    DEMO_LOG_TICK(); const AGameStateBase* State=GetWorld()?GetWorld()->GetGameState():nullptr; return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0.f;
}
const FDemoPlayerDamageView& UDemoPlayerDamageFeedbackComponent::GetView() const { DEMO_LOG_TICK(); return View; }
void UDemoPlayerDamageFeedbackComponent::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    if(PC()&&PC()->IsLocalController())
    {
        Profile=ProfileAsset.LoadSynchronous(); // 初始化一次，战斗命中无读盘。
        if(!Profile||!Profile->Validate()) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_DAMAGE_PROFILE missing/invalid; native visuals and silent audio fallback")); Profile=GetMutableDefault<UDemoPlayerDamageFeedbackProfile>(); }
        if(Profile->Light.Num()!=3||Profile->Heavy.Num()!=3||Profile->Pain.Num()!=3||Profile->Light.Contains(nullptr)||Profile->Heavy.Contains(nullptr)||Profile->Pain.Contains(nullptr)||!Profile->LowHealth)
            UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_DAMAGE_AUDIO incomplete groups Light=%d Heavy=%d Pain=%d Loop=%s; affected groups safely silent"),Profile->Light.Num(),Profile->Heavy.Num(),Profile->Pain.Num(),*GetNameSafe(Profile->LowHealth)); // 本次初始化只诊断一次，不按每发损血重复warn。
        EnsureLocalResources();
    }
    RefreshAvatarBinding(); GetWorld()->GetTimerManager().SetTimer(MaintenanceTimer,this,&UDemoPlayerDamageFeedbackComponent::Maintain,.1f,true);
}
void UDemoPlayerDamageFeedbackComponent::RefreshAvatarBinding()
{
    DEMO_LOG_TICK(); if(!PC()||!PC()->HasAuthority()) return; // 0.1秒维护会调用，源码保留埋点但默认不刷屏。
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(PC()->GetPawn()); // 只沿本控制器，阵亡不绑定旧观战Pawn。
    if(Pawn&&Pawn->IsCoopEliminated()) Pawn=nullptr;
    if(Avatar.Pawn==Pawn) return;
    Avatar.Pawn=Pawn; Avatar.bHasPawn=Pawn!=nullptr; ++Avatar.Generation; if(!Avatar.Generation) ++Avatar.Generation;
    UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_DAMAGE_BINDING changed generation=%u pawn=%s"),Avatar.Generation,*GetNameSafe(Pawn)); // 只在生命周期真实改变时输出业务事件。
    Pending.Reset(); GetWorld()->GetTimerManager().ClearTimer(FlushTimer); ClientBindAvatar(Avatar);
}
uint32 UDemoPlayerDamageFeedbackComponent::GenerationFor(const ADemoCharacter* Pawn)
{
    DEMO_LOG_TICK(); if(!PC()||!PC()->HasAuthority()||PC()->GetPawn()!=Pawn) return 0;
    RefreshAvatarBinding(); return Avatar.Pawn==Pawn?Avatar.Generation:0;
}
void UDemoPlayerDamageFeedbackComponent::ClientBindAvatar_Implementation(const FDemoPlayerDamageAvatar& Binding)
{
    DEMO_LOG_CALL(); if(!PC()||!PC()->IsLocalController()||static_cast<int32>(Binding.Generation-Avatar.Generation)<0) return;
    Avatar=Binding; ClearPresentation(); // bHasPawn=true但引用未到，不把当前旧Pawn拼成新绑定；保留有界未来回执。
    if(Avatar.bHasPawn&&!Avatar.Pawn) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PLAYER_DAMAGE_BINDING waiting mapped own Pawn generation=%u"),Avatar.Generation);
}
void UDemoPlayerDamageFeedbackComponent::ServerRequestBinding_Implementation(ADemoCharacter* ObservedPawn)
{
    DEMO_LOG_CALL(); if(!PC()||!PC()->HasAuthority()) return;
    const float Time=GetWorld()->GetTimeSeconds(); // 服务器自行节流，不相信客户端计时或Pawn身份声明。
    if(Time-LastBindingReply<.25f) return; LastBindingReply=Time;
    if(!ObservedPawn||PC()->GetPawn()!=ObservedPawn||Avatar.Pawn!=ObservedPawn||!Avatar.bHasPawn||ObservedPawn->IsCoopEliminated())
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PLAYER_DAMAGE_BINDING_REJECT stale/nonowned Pawn")); return; }
    ClientBindAvatar(Avatar); // 当前PC的Actor引用已经完成往返映射，补发不改变代次或玩法。
}
void UDemoPlayerDamageFeedbackComponent::ResetForLifecycle(bool bInvalidateAvatar)
{
    DEMO_LOG_CALL(); Pending.Reset(); Deferred.Reset(); ClearPresentation();
    if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(FlushTimer);
    if(bInvalidateAvatar&&PC()&&PC()->HasAuthority()) { Avatar.Pawn=nullptr; Avatar.bHasPawn=false; ++Avatar.Generation; if(!Avatar.Generation) ++Avatar.Generation; ClientBindAvatar(Avatar); }
}
void UDemoPlayerDamageFeedbackComponent::Submit(const FDemoPlayerDamageCue& Fact)
{
    DEMO_LOG_CALL(); if(!PC()||!PC()->HasAuthority()||!Fact.AvatarGeneration||!FMath::IsFinite(Fact.HealthLost)||(!Fact.bEvaded&&!Fact.bRescued&&Fact.HealthLost<=0.f)) return;
    FDemoPlayerDamageCue Cue=Fact; Cue.Sequence=++NextSequence; if(!Cue.Sequence) Cue.Sequence=++NextSequence; // 在原PC内全序，与Avatar代次一起防重播。
    const float Time=ServerNow();
    if(Pending.IsEmpty()&&Time-LastSend>=.05f) { LastSend=Time; TArray<FDemoPlayerDamageCue> Batch; Batch.Add(Cue); ClientDamage(Batch); return; }
    for(FDemoPlayerDamageCue& Existing:Pending)
        if(Existing.AvatarGeneration==Cue.AvatarGeneration&&Existing.PhaseRevision==Cue.PhaseRevision&&Existing.Kind==Cue.Kind&&Existing.bEvaded==Cue.bEvaded&&Existing.DirectionMode==Cue.DirectionMode&&Cue.ServerTime-Existing.ServerTime<=.05f&&(Cue.DirectionMode!=EDemoPlayerDamageDirection::Directional||FVector::DotProduct(Existing.WorldDirection,Cue.WorldDirection)>.92f))
        { Existing.HealthLost=FMath::Min(1.e6f,Existing.HealthLost+Cue.HealthLost); Existing.HealthAfter=Cue.HealthAfter; Existing.Sequence=Cue.Sequence; Existing.bFatal|=Cue.bFatal; Existing.bRescued|=Cue.bRescued; return; }
    if(Pending.Num()>=8)
    {
        int32 Weakest=0; float WeakestScore=10.f; // 达上限只替换较弱装饰事件，不提前Flush打穿50ms发送预算。
        for(int32 I=0;I<Pending.Num();++I)
        {
            const float Score=FMath::Clamp(Pending[I].HealthLost/FMath::Max(1.f,Pending[I].MaxHealth),0.f,1.f)+(Pending[I].bRescued?4.f:Pending[I].bEvaded?2.f:0.f); // 救援/躲避结果比普通接触优先。
            if(Score<WeakestScore) { Weakest=I; WeakestScore=Score; }
        }
        const float IncomingScore=FMath::Clamp(Cue.HealthLost/FMath::Max(1.f,Cue.MaxHealth),0.f,1.f)+(Cue.bRescued?4.f:Cue.bEvaded?2.f:0.f);
        if(IncomingScore>WeakestScore) Pending[Weakest]=Cue;
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PLAYER_DAMAGE_COSMETIC compressed pending cap=8")); return;
    }
    Pending.Add(Cue);
    if(!GetWorld()->GetTimerManager().IsTimerActive(FlushTimer)) GetWorld()->GetTimerManager().SetTimer(FlushTimer,this,&UDemoPlayerDamageFeedbackComponent::Flush,.05f,false);
}
void UDemoPlayerDamageFeedbackComponent::Flush()
{
    DEMO_LOG_TICK(); GetWorld()->GetTimerManager().ClearTimer(FlushTimer); LastSend=ServerNow();
    if(Pending.IsEmpty()) return;
    const int32 Count=FMath::Min(4,Pending.Num()); TArray<FDemoPlayerDamageCue> Batch; Batch.Append(Pending.GetData(),Count); Pending.RemoveAt(0,Count,EAllowShrinking::No); ClientDamage(Batch); // 一个发送时隙最多一批，不随伤害数量提前发送。
    if(!Pending.IsEmpty()) GetWorld()->GetTimerManager().SetTimer(FlushTimer,this,&UDemoPlayerDamageFeedbackComponent::Flush,.05f,false);
}
void UDemoPlayerDamageFeedbackComponent::NotifyEvaded()
{
    DEMO_LOG_CALL(); if(!PC()||!PC()->HasAuthority()) return;
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(PC()->GetPawn()); const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    if(!Pawn||Pawn->IsCoopEliminated()||!State||State->Phase!=EDemoPhase::Combat) return;
    FDemoPlayerDamageCue Cue; Cue.AvatarGeneration=GenerationFor(Pawn); Cue.PhaseRevision=State->PhaseRevision; Cue.ServerTime=ServerNow(); Cue.bEvaded=true; Cue.Kind=EDemoPlayerDamageKind::Global; Submit(Cue);
}
bool UDemoPlayerDamageFeedbackComponent::AcceptSequence(uint32 Sequence)
{
    DEMO_LOG_TICK(); if(!Sequence) return false;
    const int32 Delta=static_cast<int32>(Sequence-LastSequence); // uint32回绕安全的有界比较。
    if(!Seen||Delta>0) { Seen=!Seen||Delta>=64?1:(Seen<<Delta)|1; LastSequence=Sequence; return true; }
    const int64 Age=-static_cast<int64>(Delta); if(Age>=64||(Seen&(uint64(1)<<Age))) return false;
    Seen|=uint64(1)<<Age; return true;
}
bool UDemoPlayerDamageFeedbackComponent::CanPresent() const
{
    DEMO_LOG_TICK(); const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; const ADemoCharacter* Pawn=Avatar.Pawn;
    const UGameViewportClient* Viewport=GetWorld()?GetWorld()->GetGameViewport():nullptr; // 本机失焦即撤装饰和循环，不影响服务器继续战斗。
    if(!FApp::HasFocus()||!Viewport||!Viewport->Viewport||!Viewport->Viewport->HasFocus()) return false;
    return PC()&&PC()->IsLocalController()&&Pawn&&PC()->GetPawn()==Pawn&&PC()->GetViewTarget()==Pawn&&!Pawn->IsCoopEliminated()&&Pawn->GetDemoAttributes()&&Pawn->GetDemoAttributes()->GetHealth()>0.f&&State&&State->Phase==EDemoPhase::Combat&&!PC()->HasBlockingOverlay()&&!PC()->IsUpgradeMenuOpen()&&!PC()->IsPauseMenuOpen()&&!PC()->IsNextLevelConfirmationOpen();
}
void UDemoPlayerDamageFeedbackComponent::ClientDamage_Implementation(const TArray<FDemoPlayerDamageCue>& Batch)
{
    DEMO_LOG_CALL(); if(!PC()||!PC()->IsLocalController()||Batch.Num()>4) return;
    for(const FDemoPlayerDamageCue& Cue:Batch) Receive(Cue,true);
    Animate(); // 整批结束后只刷新一次，延迟队列消费不能递归进入Animate。
}
void UDemoPlayerDamageFeedbackComponent::Receive(const FDemoPlayerDamageCue& Cue,bool bAllowDefer)
{
    DEMO_LOG_TICK(); const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); if(!State||!Profile) return;
    if(!FMath::IsFinite(Cue.ServerTime)||!FMath::IsFinite(Cue.HealthLost)||!FMath::IsFinite(Cue.HealthAfter)||!FMath::IsFinite(Cue.MaxHealth)||Cue.WorldDirection.ContainsNaN()||ServerNow()-Cue.ServerTime>.5f||Cue.ServerTime-ServerNow()>.3f)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PLAYER_DAMAGE_CUE_REJECT invalid/expired/future timestamp")); return; }
    const int32 AvatarDelta=static_cast<int32>(Cue.AvatarGeneration-Avatar.Generation),PhaseDelta=static_cast<int32>(Cue.PhaseRevision-State->PhaseRevision);
    if(AvatarDelta>0||PhaseDelta>0||!Avatar.Pawn)
    { if(bAllowDefer&&Deferred.Num()<4) { FDeferred& Item=Deferred.AddDefaulted_GetRef(); Item.Cue=Cue; Item.Until=GetWorld()->GetTimeSeconds()+.2f; EnsureAnimation(); } return; }
    if(AvatarDelta<0||PhaseDelta<0||!CanPresent()||Cue.bFatal||!AcceptSequence(Cue.Sequence))
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PLAYER_DAMAGE_CUE_REJECT old/inactive/fatal/duplicate sequence=%u"),Cue.Sequence); return; }
    if(ObservedPhase!=State->PhaseRevision) { ObservedPhase=State->PhaseRevision; ClearPresentation(); } // 新阶段首笔先清旧包络，低频维护不能随后误清首笔。
    const float Time=GetWorld()->GetTimeSeconds(); View.bVisible=true;
    if(Cue.bEvaded) { View.Outcome=TEXT("Evaded"); View.OutcomeEndTime=Time+.9f; EnsureAnimation(); return; }
    if(Cue.bRescued&&Cue.HealthLost<=0.f) { View.Outcome=TEXT("Rescue"); View.OutcomeEndTime=Time+1.2f; EnsureAnimation(); return; } // 1HP触发救援只有结果提示，不能播放假受伤。
    if(Cue.HealthLost<=0.f||Cue.MaxHealth<=0.f) return;
    const float Fraction=Cue.HealthLost/Cue.MaxHealth; const int32 Tier=Fraction>=Profile->HeavyFraction?2:Fraction>=Profile->LightFraction?1:0;
    const bool bPeriodic=Cue.Kind==EDemoPlayerDamageKind::Periodic; const float Peak=(Tier==2?Profile->HeavyEdge:Tier==1?Profile->MediumEdge:Profile->LightEdge)*(bPeriodic?.4f:1.f);
    const float RemainingEdge=EdgePeak*FMath::Clamp(1.f-(Time-EdgeStarted)/Profile->EdgeSeconds,0.f,1.f); // 旧峰值先衰减，不能把早先重击永久继承给后来轻击。
    EdgePeak=FMath::Min(.25f,FMath::Max(RemainingEdge,Peak)); EdgeStarted=Time;
    const float Side=PC()->GetControlRotation().RotateVector(FVector::RightVector).Dot(Cue.WorldDirection);
    RollPeak=bPeriodic||Cue.DirectionMode!=EDemoPlayerDamageDirection::Directional?0.f:-FMath::Sign(Side)*(Tier==2?Profile->HeavyRoll:Profile->LightRoll); RollStarted=Time;
    FDemoPlayerDamageIndicator Indicator; Indicator.WorldDirection=Cue.WorldDirection; Indicator.bOmni=Cue.DirectionMode!=EDemoPlayerDamageDirection::Directional; Indicator.EndTime=Time+Profile->IndicatorSeconds; Indicator.InitialStrength=Indicator.Strength=Tier==2?1.f:.75f;
    if(!bPeriodic) // DOT仅低强外缘/实血与低血状态，不反复重启完整来向环/镜头/痛哼。
    {
        int32 Replace=INDEX_NONE; for(int32 I=0;I<View.Indicators.Num();++I) if(View.Indicators[I].bOmni==Indicator.bOmni&&(Indicator.bOmni||FVector::DotProduct(View.Indicators[I].WorldDirection,Indicator.WorldDirection)>.9f)) { Replace=I; break; }
        if(Replace!=INDEX_NONE) View.Indicators[Replace]=Indicator; else { if(View.Indicators.Num()>=4) View.Indicators.RemoveAt(0); View.Indicators.Add(Indicator); }
    }
    TrailBase=View.HealthTrail=FMath::Max(View.HealthTrail,FMath::Clamp(Cue.HealthAfter+Cue.HealthLost,0.f,Cue.MaxHealth)); View.HealthTrailMax=Cue.MaxHealth; TrailStarted=Time;
    if(Cue.bRescued) { View.Outcome=TEXT("Rescue"); View.OutcomeEndTime=Time+1.2f; }
    PlayContact(Cue,Tier); EnsureAnimation();
}
void UDemoPlayerDamageFeedbackComponent::EnsureLocalResources()
{
    DEMO_LOG_TICK(); if(!PC()||!PC()->IsLocalController()) return; // 幂等资源检查高频调用，不能默认打印每次维护。
    if(!bResourcesReady)
    {
        for(int32 I=0;I<4;++I) // 固定2接触＋1人声＋1循环；不按每次受击创建组件。
        {
            UAudioComponent* Audio=NewObject<UAudioComponent>(GetOwner()); Audio->bAutoActivate=false; Audio->bAutoDestroy=false; Audio->bAllowSpatialization=false; Audio->bStopWhenOwnerDestroyed=true; Audio->RegisterComponent();
            if(I<2) Contacts.Add(Audio); else if(I==2) Voice=Audio; else Breathing=Audio;
        }
        bResourcesReady=true;
    }
    if(!CameraModifier&&PC()->PlayerCameraManager) CameraModifier=Cast<UDemoDamageCameraModifier>(PC()->PlayerCameraManager->AddNewCameraModifier(UDemoDamageCameraModifier::StaticClass()));
}
void UDemoPlayerDamageFeedbackComponent::PlayContact(const FDemoPlayerDamageCue& Cue,int32 Tier)
{
    DEMO_LOG_TICK(); EnsureLocalResources(); const float Time=GetWorld()->GetTimeSeconds();
    if(Time-LastContact>=.1f)
    {
        USoundWave* Sound=Tier==2?PickDamageSound(Profile->Heavy,LastHeavy):PickDamageSound(Profile->Light,LastLight);
        if(Sound) for(UAudioComponent* Audio:Contacts) if(Audio&&!Audio->IsPlaying()) { Audio->SetSound(Sound); Audio->SetVolumeMultiplier(Profile->ContactVolume*(Cue.Kind==EDemoPlayerDamageKind::Periodic?.4f:1.f)); Audio->SetPitchMultiplier(FMath::FRandRange(.97f,1.03f)); Audio->Play(); LastContact=Time; break; }
    }
    const UDemoGameUserSettings* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings());
    if(Tier>0&&Cue.Kind!=EDemoPlayerDamageKind::Periodic&&Time-LastVoice>=1.f&&Voice&&(!Settings||Settings->IsHurtVoiceEnabled())&&FMath::FRand()<Profile->PainChance)
        if(USoundWave* Sound=PickDamageSound(Profile->Pain,LastPain)) { Voice->SetSound(Sound); Voice->SetVolumeMultiplier(Profile->VoiceVolume); Voice->Play(); LastVoice=Time; }
}
void UDemoPlayerDamageFeedbackComponent::EnsureAnimation()
{
    DEMO_LOG_TICK(); if(!GetWorld()->GetTimerManager().IsTimerActive(AnimationTimer)) GetWorld()->GetTimerManager().SetTimer(AnimationTimer,this,&UDemoPlayerDamageFeedbackComponent::Animate,.02f,true);
}
void UDemoPlayerDamageFeedbackComponent::Maintain()
{
    DEMO_LOG_TICK(); if(!PC()) return;
    if(PC()->HasAuthority()) RefreshAvatarBinding();
    if(!PC()->IsLocalController()) return;
    if((!Avatar.Generation||(Avatar.bHasPawn&&(!Avatar.Pawn||Avatar.Pawn!=PC()->GetPawn())))&&PC()->GetPawn()&&GetWorld()->GetTimeSeconds()>=NextBindingRequest)
    { NextBindingRequest=GetWorld()->GetTimeSeconds()+.25f; ServerRequestBinding(Cast<ADemoCharacter>(PC()->GetPawn())); } // 本PC观察到的Pawn必须服务器复核，不能直接拼入新代次。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    if(State&&ObservedPhase!=State->PhaseRevision) { ObservedPhase=State->PhaseRevision; ClearPresentation(); }
    if(!CanPresent()) { Animate(); return; } // 即使Avatar复制尚未到，也按0.2秒上限推进/清理等待队列。
    EnsureLocalResources(); Animate();
}
void UDemoPlayerDamageFeedbackComponent::Animate()
{
    DEMO_LOG_TICK(); const float Time=GetWorld()->GetTimeSeconds();
    TArray<FDeferred> Waiting=MoveTemp(Deferred); Deferred.Reset(); // 接收可重入，只遍历冻结的至多4项，不让RPC栈改数组。
    for(const FDeferred& Item:Waiting) if(Time<=Item.Until) { const int32 Delta=static_cast<int32>(Item.Cue.AvatarGeneration-Avatar.Generation); const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); if(Delta>0||!Avatar.Pawn||(State&&static_cast<int32>(Item.Cue.PhaseRevision-State->PhaseRevision)>0)) Deferred.Add(Item); else Receive(Item.Cue,false); }
    if(!CanPresent()||!Profile) { ClearPresentation(); if(Deferred.IsEmpty()) GetWorld()->GetTimerManager().ClearTimer(AnimationTimer); return; }
    const UDemoAttributeSet* Attributes=Avatar.Pawn->GetDemoAttributes(); const float Health=Attributes->GetHealth(),MaxHealth=FMath::Max(1.f,Attributes->GetMaxHealth()),Fraction=Health/MaxHealth;
    bLow=Fraction<=(bLow?Profile->LowExit:Profile->LowEnter); bCritical=Fraction<=(bCritical?Profile->CriticalExit:Profile->CriticalEnter);
    View.bVisible=true; View.bCritical=bCritical; View.LowHealthAlpha=bCritical?1.f:bLow?.5f:0.f; View.bSlowed=Avatar.Pawn->GetAbilitySystemComponent()&&Avatar.Pawn->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoTags::Slowed);
    const UDemoGameUserSettings* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings());
    if(Voice&&Voice->IsPlaying()&&Settings&&!Settings->IsHurtVoiceEnabled()) Voice->Stop(); // 修改设备开关后无需等待当前人声自然结束。
    View.EdgeStrength=EdgePeak*FMath::Clamp(1.f-(Time-EdgeStarted)/Profile->EdgeSeconds,0.f,1.f)*(Settings?Settings->GetDamageFlashScale():1.f);
    View.RollDegrees=FMath::Clamp(RollPeak*FMath::Clamp(1.f-(Time-RollStarted)/Profile->RollSeconds,0.f,1.f),-.4f,.4f)*(Settings?Settings->GetDamageShakeScale():1.f);
    if(const UDemoWeaponComponent* Equipment=Avatar.Pawn->GetWeaponComponent()) if(Equipment->GetScopeLevel()>0) View.RollDegrees*=Equipment->GetActiveWeapon()&&Equipment->GetActiveWeapon()->GetWeaponId()==TEXT("sniper")?0.f:.35f;
    View.HealthTrailMax=MaxHealth; View.HealthTrail=FMath::Max(Health,FMath::Lerp(TrailBase,Health,FMath::Clamp((Time-TrailStarted-Profile->TrailHoldSeconds)/Profile->TrailFadeSeconds,0.f,1.f)));
    for(int32 I=View.Indicators.Num()-1;I>=0;--I) { auto& Item=View.Indicators[I]; if(Time>=Item.EndTime) View.Indicators.RemoveAt(I); else Item.Strength=Item.InitialStrength*FMath::Clamp((Item.EndTime-Time)/Profile->IndicatorSeconds,0.f,1.f); }
    if(Time>=View.OutcomeEndTime) View.Outcome=NAME_None;
    if(Breathing)
    {
        const bool bWants=bLow&&Profile->LowHealth&&(!Settings||Settings->IsLowHealthAudioEnabled());
        if(bWants&&!Breathing->IsPlaying()) { Breathing->SetSound(Profile->LowHealth); Breathing->SetVolumeMultiplier(Profile->LowHealthVolume); Breathing->Play(); }
        else if(!bWants&&Breathing->IsPlaying()) Breathing->Stop();
    }
    if(View.Indicators.IsEmpty()&&View.EdgeStrength<=0.f&&FMath::Abs(View.RollDegrees)<.001f&&View.Outcome.IsNone()&&!bLow&&Deferred.IsEmpty()&&FMath::IsNearlyEqual(View.HealthTrail,Health,.1f)) GetWorld()->GetTimerManager().ClearTimer(AnimationTimer);
}
void UDemoPlayerDamageFeedbackComponent::ClearPresentation()
{
    DEMO_LOG_TICK(); View=FDemoPlayerDamageView(); EdgePeak=RollPeak=TrailBase=0.f; bLow=bCritical=false;
    for(UAudioComponent* Audio:Contacts) if(Audio&&Audio->IsPlaying()) Audio->Stop();
    if(Voice&&Voice->IsPlaying()) Voice->Stop(); if(Breathing&&Breathing->IsPlaying()) Breathing->Stop();
    if(GetWorld()&&Deferred.IsEmpty()) GetWorld()->GetTimerManager().ClearTimer(AnimationTimer);
}
void UDemoPlayerDamageFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); ResetForLifecycle(); if(GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer); GetWorld()->GetTimerManager().ClearTimer(AnimationTimer); GetWorld()->GetTimerManager().ClearTimer(FlushTimer); }
    if(CameraModifier&&PC()&&PC()->PlayerCameraManager) PC()->PlayerCameraManager->RemoveCameraModifier(CameraModifier); CameraModifier=nullptr;
    for(UAudioComponent* Audio:Contacts) if(Audio) Audio->DestroyComponent(); Contacts.Reset(); if(Voice) Voice->DestroyComponent(); if(Breathing) Breathing->DestroyComponent(); Voice=nullptr; Breathing=nullptr; Super::EndPlay(Reason);
}
