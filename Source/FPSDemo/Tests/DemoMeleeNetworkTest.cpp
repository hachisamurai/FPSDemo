// 职责：独立LAN近战/皮肤及可选金属回执验收；正常UI完成核心/准备/异步出发后，再运行原高空隔离观测，不代远端提交准备意图。
// 使用/依赖：Editor显式双端参数及临时进度；NetworkProbeSetup只读门分开准备/动作预算，本机池是真实多播证据，不能据此验房间导航。
#include "Tests/DemoMeleeNetworkTest.h"
#include "Tests/DemoNetworkProbeSetup.h"
#include "Combat/Feedback/DemoMetalImpactSubsystem.h" // 可选专项检查同一击中在拥有者/旁观者均只产生一次火花和声音。
#include "Tests/DemoProjectileFixtures.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "GAS/DemoAbilitySystemComponent.h" // 仅专项服务器用临时阻塞标签验证真实GAS拒绝与预测回滚。
#include "GAS/DemoTags.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoMeleeAnimInstance.h" // 独立FP本地求值验收，不用TP成功代替拥有者图。
#include "AI/DemoEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/DemoPlayerController.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Debug/DemoLog.h"
#include "Camera/CameraActor.h" // 观察端独立镜头，不修改被测Pawn相机或手骨。
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

ADemoMeleeNetworkTest::ADemoMeleeNetworkTest()
{
    DEMO_LOG_CALL(); bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
#if WITH_EDITOR
    SetupWaitSeconds=DemoNetworkProbeSetup::ReadTimeoutSeconds(); // 非Editor不解析准备参数，也不新增可运行的试玩入口。
#endif
}
ADemoCharacter* ADemoMeleeNetworkTest::Subject() const { DEMO_LOG_TICK(); return Stage<2 || Stage>=4?RemotePawn.Get():HostPawn.Get(); } // 取消/拒绝必须由真实远端拥有者触发。
void ADemoMeleeNetworkTest::ResetStage(int32 Next,ADemoCharacter* Host,ADemoCharacter* Remote)
{
    DEMO_LOG_CALL(); Stage=Next; HostPawn=Host; RemotePawn=Remote; StageTime=GetWorld()->GetTimeSeconds();
    if(Next==0) { StartedAt=FPlatformTime::Seconds(); bInitialStagePending=false; } // 两端原动作计时只从实际首阶段开始，不能被UI准备耗尽。
    bEquipped=bPressed=bSawAttack=bSawPose=bLocalDone=bRemoteDone=false;
    bSawFirstPersonPose=false; // 新阶段重新要求采到当前输入拥有者的FP资源/时钟。
    bImmediateResponse=false; bNoRewind=true; bRollbackObserved=bLateRevival=false; LastOwnedProgress=-1.f; LocalInputTime=0.; // 不复用上一次请求的预测观测。
    LastOwnedSerial=0; OwnedAttackStarts=0; bBufferedPress=bResetTarget=false; // 仅阶段1额外验证拥有者连续输入与网络抖动。
    bSkinCameraReady=bSkinGun=bSkinIdle=bSkinAttack=false; // 只有当前端负责自己的截图，不使用复制状态锁存。
    if (UDemoMetalImpactSubsystem* Pool=GetWorld()->GetSubsystem<UDemoMetalImpactSubsystem>()) // 阶段开始本地快照，不跨World保存组件引用。
    { MetalBefore=Pool->GetStats().Played; AudioBefore=Pool->GetStats().AudioPlayed; }
    if (Subject() && Subject()->IsLocallyControlled()) Subject()->GetController()->SetControlRotation(FRotator::ZeroRotator);
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_NETWORK_STAGE endpoint=%s stage=%d setup_contract=2 setup_mode=manual_formal_ui"),HasAuthority()?TEXT("host"):TEXT("client"),Stage);
}
void ADemoMeleeNetworkTest::BeginStage(int32 Next)
{
    DEMO_LOG_CALL(); ResetStage(Next,HostPawn.Get(),RemotePawn.Get());
    if (Target.IsValid()) Target->Destroy();
    for (ADemoCharacter* Pawn:{HostPawn.Get(),RemotePawn.Get()}) // 两端分别使用真实拥有者；夹具仅摆放位置，不执行远端攻击。
    {
        Pawn->GetWeaponComponent()->EquipSlot(2);
        Pawn->GetCharacterMovement()->StopMovementImmediately(); Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        const float ObserverDistance=FParse::Param(FCommandLine::Get(),TEXT("DemoMetalImpactNetwork"))?600.f:3000.f; // 金属专项让旁观者位于25m可见/听范围，仍远于180cm近战伤害距离。
        Pawn->SetActorLocation(FVector(Pawn==Subject()?0:ObserverDistance,0,20000),false,nullptr,ETeleportType::TeleportPhysics); Pawn->SetActorRotation(FRotator::ZeroRotator); Pawn->ForceNetUpdate();
    }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 隔离高空靶，保留真实PhysicsAsset。
    ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),Subject()->GetActorLocation()+FVector(95,0,0),FRotator(0,180,0),Params); // 当前阶段独立Actor，HP无需测试回写。
    FDemoEnemySpawnStats Stats; Stats.Health=1000.f; Stats.AttackPower=0.f; // 只防止靶反击/死亡，伤害由生产GAS结算。
    Enemy->Configure(Stats); Enemy->SetActorTickEnabled(false); Target=Enemy;
    if (Next==5) RemotePawn->GetDemoASC()->AddLooseGameplayTag(DemoTags::Dead); // 不改HP/阶段且不复制此本地标签，迫使客户端先预测再由服务器GAS拒绝；测试结束移除。
    DemoHitZoneTest::Pose(Enemy,TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f);
    ClientBegin(Next,HostPawn.Get(),RemotePawn.Get());
}
void ADemoMeleeNetworkTest::ClientBegin_Implementation(int32 Next,ADemoCharacter* Host,ADemoCharacter* Remote)
{
    DEMO_LOG_CALL();
    if (!FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeNetworkClient"))) { ServerResult(Next,false); Finish(false); return; }
#if WITH_EDITOR
    if(Next==0)
    {
        HostPawn=Host; RemotePawn=Remote; // 只保存服务器本条计划引用，不用当前Player0猜测尚未映射的另一Pawn。
        if(!HostPawn.IsValid()||!RemotePawn.IsValid()||!DemoNetworkProbeSetup::CanStartLocally(this,false))
        { bInitialStagePending=true; UE_LOG(LogFPSDemo,Log,TEXT("MELEE_NETWORK_SETUP waiting local phase/pawns/menu replication")); return; }
    }
#endif
    ResetStage(Next,Host,Remote);
}
void ADemoMeleeNetworkTest::ServerResult_Implementation(int32 Completed,bool Passed)
{
    DEMO_LOG_CALL();
    if (Completed!=Stage || bFinished) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NETWORK_STALE_RESULT")); return; }
    if (!Passed) { ClientFinish(false); Finish(false); return; } bRemoteDone=true;
}
void ADemoMeleeNetworkTest::Finish(bool Passed)
{
    DEMO_LOG_CALL(); if (bFinished) return; bFinished=true; bPassed=Passed; ExitAt=FPlatformTime::Seconds()+(HasAuthority()?3.:2.); // 客户端先结束，避免连接关闭销毁其退出定时所属Actor。
    UE_LOG(LogFPSDemo,Display,TEXT("MELEE_NETWORK_%s endpoint=%s stage=%d setup_contract=2 setup_mode=manual_formal_ui"),Passed?TEXT("SUCCESS"):TEXT("FAIL"),HasAuthority()?TEXT("host"):TEXT("client"),Stage);
}
void ADemoMeleeNetworkTest::ClientFinish_Implementation(bool Passed) { DEMO_LOG_CALL(); Finish(Passed); }
void ADemoMeleeNetworkTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (bFinished) { if (FPlatformTime::Seconds()>ExitAt) FPlatformMisc::RequestExitWithStatus(false,bPassed?0:1); return; }
#if !WITH_EDITOR
    Finish(false); return; // 非Editor不可执行此试玩夹具，不建立云测试旁路。
#else
    if (!StartedAt) StartedAt=FPlatformTime::Seconds();
    const double TimeBudget=Stage<0?SetupWaitSeconds:90.; // 准备期两端同样有界，进入动作后保留原90秒上限。
    if (FPlatformTime::Seconds()-StartedAt>=TimeBudget)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NETWORK_TIMEOUT phase=%s budget=%.1f"),Stage<0?TEXT("formal_ui_setup"):TEXT("observation"),TimeBudget); if (HasAuthority()) ClientFinish(false); Finish(false); return; }
    if (GetWorld()->GetTimeSeconds()<1.f) return;
    if (Stage<0)
    {
        if (!HasAuthority())
        {
            if(bInitialStagePending&&HostPawn.IsValid()&&RemotePawn.IsValid()&&DemoNetworkProbeSetup::CanStartLocally(this,false)) ResetStage(0,HostPawn.Get(),RemotePawn.Get());
            return; // 只等原首阶段消息及本机复制/UI就绪，不新增服务器驱动或重试RPC。
        }
        AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 真实模式管理队伍、关卡和临时存档。
        ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 不写Phase，只等正式旅行完成。
        UDemoRoomSubsystem* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // Editor LAN使用原房间接口，不创建云子系统。
        if (!Mode || !State || !Rooms) return;
        if (Rooms->IsPublic()) { Finish(false); return; }
        if (GetNetMode()==NM_Standalone)
        { if (!bHostRequested && State->Phase==EDemoPhase::Lobby) { bHostRequested=true; if (!Rooms->HostSlot(0,true)) Finish(false); } return; }
        if (GetNetMode()!=NM_ListenServer || Rooms->GetState()!=EDemoRoomState::Hosting) return;
        if (!bHostRequested) { bHostRequested=true; UE_LOG(LogFPSDemo,Display,TEXT("MELEE_NETWORK_HOST_READY setup_contract=2 setup_mode=manual_formal_ui: both endpoints select core, open next terminal, Ready, host Start; waiting real first KillAll Combat")); }
        const TArray<ADemoCharacter*> Members=Mode->GetCoopRoster()->GetPlayers(); // 正式登记成功的真实玩家集合。
        if(Members.Num()>2) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NETWORK_SETUP_REJECT requires exactly two registered players")); ClientFinish(false); Finish(false); return; }
        if (Members.Num()!=2) return;
        for (ADemoCharacter* Member:Members) if (Member->IsLocallyControlled()) HostPawn=Member; else RemotePawn=Member;
        ADemoPlayerController* RemotePC=RemotePawn.IsValid()?Cast<ADemoPlayerController>(RemotePawn->GetController()):nullptr; // 可靠测试RPC唯一拥有者。
        if (!HostPawn.IsValid() || !RemotePC || !RemotePC->IsCoopRegistered()) return;
        if(GetOwner()!=RemotePC) { SetOwner(RemotePC); ForceNetUpdate(); } // 幂等绑定测试回执拥有者，不在每Tick重复强制网络更新。
        FString SetupReason; // 共用门给出当前真实阶段/会话/目标或准备超时的有限诊断，不按即时StartLevel回执判断失败。
        const DemoNetworkProbeSetup::EResult Setup=DemoNetworkProbeSetup::Poll(this,false,FPlatformTime::Seconds()-StartedAt,SetupWaitSeconds,SetupReason);
        if(Setup==DemoNetworkProbeSetup::EResult::Waiting) return;
        if(Setup==DemoNetworkProbeSetup::EResult::Failed)
        { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NETWORK_SETUP_REJECT %s"),*SetupReason); ClientFinish(false); Finish(false); return; }
        if(!DemoNetworkProbeSetup::CanStartLocally(this,false)) return; // 与客户端对称，房主菜单实际关闭后才开始本端动作计时。
        for (TActorIterator<ADemoEnemy> Enemy(GetWorld());Enemy;++Enemy) { Enemy->SetActorTickEnabled(false); Enemy->SetActorEnableCollision(false); } // 保留自然敌人注册避免自动清关，只隔离测试干扰。
        BeginStage(0); return;
    }
    ADemoCharacter* Pawn=Subject(); // 两端观察同一玩家，只有IsLocallyControlled端能够发输入。
    UDemoMeleeComponent* Melee=Pawn?Pawn->FindComponentByClass<UDemoMeleeComponent>():nullptr; // 真实生产组件，缺失视为失败。
    const bool bSecondary=Stage%2!=0 && Melee && Melee->GetDefinition() && Melee->GetDefinition()->Attacks.IsValidIndex(1); // 单横扫资产的第2轮验证重复主攻击；多招式资产仍验证右键。
    const double Elapsed=GetWorld()->GetTimeSeconds()-StageTime; // 本端相对时间，不拿两个进程本地秒作差。
    CaptureSkinViews(Elapsed); // 在真实输入之前安排待机画面，动作截图从生产phase读取。
    if (Stage==1 && HasAuthority() && !bResetTarget && Elapsed>1.55 && Target.IsValid())
    { bResetTarget=true; Target->SetActorLocation(Pawn->GetActorLocation()+FVector(95,0,0)); } // 首击击退已结束，再放回真实靶以验证第二招的独立结算。
    if (Pawn && Melee && Pawn->IsLocallyControlled())
    {
        ADemoPlayerController* PC=Cast<ADemoPlayerController>(Pawn->GetController()); // 拥有者控制器输入正常进入服务器RPC。
        if (!bEquipped && Elapsed>.4) { bEquipped=true; PC->InputKey(FInputKeyParams(EKeys::Three,IE_Pressed,1.)); }
        if (!bPressed && Elapsed>.75)
        {
            bPressed=true; LocalInputTime=GetWorld()->GetTimeSeconds(); PC->InputKey(FInputKeyParams(EKeys::Three,IE_Released,0.));
            if (!bSecondary) Pawn->GetWeaponComponent()->StartFire(); else Pawn->GetWeaponComponent()->CycleAim(); // 通过正式输入，不直接改生产预测状态。
            bImmediateResponse=Melee->IsAttacking(); // 同一调用栈即看到动作，不能用稍后复制成功替代。
            UE_LOG(LogFPSDemo,Display,TEXT("MELEE_IMMEDIATE endpoint=%s stage=%d same_frame=%d"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,bImmediateResponse);
            if (Stage==4) { Melee->CancelAttack(); bRollbackObserved=!Melee->IsAttacking(); } // 请求与取消可靠有序，随后旧确认不得复活动作。
        }
        if (bPressed && Stage==4 && Melee->IsAttacking()) bLateRevival=true;
        if (Stage==1 && bPressed && !bBufferedPress && GetWorld()->GetTimeSeconds()-LocalInputTime>Melee->GetDefinition()->Attacks[0].Duration-.07f)
        { bBufferedPress=true; Melee->RequestAttack(0); } // 只产生一次本机尾部点击，不让服务器代点；网络稍早抵达时应等冷却而非丢失。
        if (bPressed && Stage==5 && !Melee->IsAttacking() && GetWorld()->GetTimeSeconds()-LocalInputTime<Melee->GetDefinition()->Attacks[0].Duration*.9)
            bRollbackObserved=true; // 必须在自然播完前观察到拒绝回待机，不能靠时长到期冒充回滚。
    }
    if (Melee && Melee->IsAttacking())
    {
        if (Pawn->IsLocallyControlled())
        {
            float SourceTime=0.f,Progress=0.f; FName Action; // 本端预测/确认后的实际播放值，单位作者phase。
            Melee->GetPose(true,SourceTime,Progress,Action);
            if (LastOwnedSerial!=Melee->GetAttackSerial()) { LastOwnedSerial=Melee->GetAttackSerial(); LastOwnedProgress=-1.f; ++OwnedAttackStarts; } // 仅新输入序号允许新相位，不放过同一请求确认倒放。
            if (LastOwnedProgress>=0.f && Progress+.0001f<LastOwnedProgress) bNoRewind=false;
            LastOwnedProgress=Progress; // 网络时钟修正/确认不能倒放本机动作。
        }
        bSawAttack=true;
        const UDemoPlayerAnimInstance* Animation=Cast<UDemoPlayerAnimInstance>(Pawn->GetMesh()->GetAnimInstance()); // 真实已求值TP图，不以复制枚举替代可见动作证据。
        if (Animation && Animation->UpperBodyAction==(bSecondary?TEXT("BatVertical"):TEXT("BatHorizontal"))) bSawPose=true; // 单横扫重复播放也必须重新看到正确姿态。
        if (Pawn->IsLocallyControlled())
        {
            TInlineComponentArray<USkeletalMeshComponent*> Meshes(Pawn); // 只读本机渲染组件，不给远端伪造FP网格。
            for (const USkeletalMeshComponent* Mesh:Meshes)
                if (Mesh->GetFName()==TEXT("MeleeFirstPersonArms"))
                    if (const UDemoMeleeAnimInstance* FP=Cast<UDemoMeleeAnimInstance>(Mesh->GetAnimInstance())) // 当前真实图缓存，非测试注入。
                    {
                        float Time=0.f,Progress=0.f; FName Action; // 同一生产复制时钟输出，用于拒绝旧Pose或本地自行重计时。
                        const UAnimSequence* Expected=Melee->GetPose(true,Time,Progress,Action); // 此视角的正式DA资源。
                        if (!Mesh->bHiddenInGame && FP->Pose==Expected && FP->Action==Action && FMath::Abs(FP->Progress-Progress)<.1f) bSawFirstPersonPose=true;
                    }
        }
    }
    if (!bLocalDone && Elapsed>3.5) // 阶段1等待两个完整0.95秒动作及模拟网络延迟。
    {
        const bool DamageOK=!HasAuthority() || (Target.IsValid() && (Stage>=4?Target->GetHealth()==1000.f:Stage==1?(Target->GetHealth()<965.f && Target->GetHealth()>=859.99f):(Target->GetHealth()<1000.f && Target->GetHealth()>=1000.f-(bSecondary?110.01f:70.01f)))); // 缓冲两击、普通一击，取消/拒绝零伤害。
        const UDemoMetalImpactSubsystem* Pool=GetWorld()->GetSubsystem<UDemoMetalImpactSubsystem>(); // 本端独立统计，旁观者不借用服务器池。
        const uint64 ExpectedEffects=Stage>=4?0:Stage==1?2:1; // 缓冲连击每招一次；取消/拒绝不产生实际命中反馈。
        const bool MetalOK=!FParse::Param(FCommandLine::Get(),TEXT("DemoMetalImpactNetwork")) || (Pool && Pool->GetStats().Played==MetalBefore+ExpectedEffects && Pool->GetStats().AudioPlayed==AudioBefore+ExpectedEffects);
        const bool PredictionOK=!Pawn->IsLocallyControlled() || (bImmediateResponse && bNoRewind && !bLateRevival && (Stage<4 || bRollbackObserved) && (Stage!=1 || OwnedAttackStarts==2)); // 本机缓冲必须真实开始第二次，不仅服务器HP变化。
        const bool Passed=(Stage>=4 || (bSawAttack && bSawPose && (!Pawn->IsLocallyControlled() || bSawFirstPersonPose))) && Melee && Melee->IsEquipped() && !Melee->IsAttacking() && DamageOK && MetalOK && PredictionOK;
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_PREDICTION_RESULT stage=%d immediate=%d monotonic=%d rollback=%d revived=%d starts=%d passed=%d"),Stage,bImmediateResponse,bNoRewind,bRollbackObserved,bLateRevival,OwnedAttackStarts,PredictionOK);
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoMetalImpactNetwork"))) UE_LOG(LogFPSDemo,Display,TEXT("METAL_NETWORK_RESULT endpoint=%s stage=%d visual_delta=%llu audio_delta=%llu passed=%d"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,Pool?Pool->GetStats().Played-MetalBefore:0,Pool?Pool->GetStats().AudioPlayed-AudioBefore:0,MetalOK);
        bLocalDone=true;
        UE_LOG(LogFPSDemo,Display,TEXT("MELEE_NETWORK_RESULT endpoint=%s stage=%d attack=%d pose=%d damage=%d hp=%.1f passed=%d"),HasAuthority()?TEXT("host"):TEXT("client"),Stage,bSawAttack,bSawPose,DamageOK,Target.IsValid()?Target->GetHealth():-1.f,Passed);
        if (!HasAuthority()) ServerResult(Stage,Passed); else if (!Passed) { ClientFinish(false); Finish(false); return; }
    }
    if (HasAuthority() && bLocalDone && bRemoteDone)
    { if (Stage==5) { RemotePawn->GetDemoASC()->RemoveLooseGameplayTag(DemoTags::Dead); ClientFinish(true); Finish(true); } else BeginStage(Stage+1); } // 清除仅测试服务器门禁，绝不留下生产死亡状态。
#endif
}

void ADemoMeleeNetworkTest::CaptureSkinViews(double Elapsed)
{
    DEMO_LOG_TICK();
    if (!FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerSkinCapture"))) return;
    ADemoCharacter* Pawn=Subject(); ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 当前被测角色与本端观察者不是同一所有者时拍TP。
    if (!Pawn || !PC || Elapsed<.15) return;
    UDemoMeleeComponent* Melee=Pawn->FindComponentByClass<UDemoMeleeComponent>(); // 读取真实复制装备与动作，不注入测试动画。
    if (!Melee) return;
    if (!bSkinCameraReady)
    {
        if (Pawn->IsLocallyControlled()) PC->SetViewTarget(Pawn);
        else
        {
            if (!SkinCamera.IsValid()) SkinCamera=GetWorld()->SpawnActor<ACameraActor>(); // 本端World持有，退出随World清理。
            SkinCamera->SetActorLocation(Pawn->GetActorLocation()+FVector(240,-90,45)); SkinCamera->SetActorRotation((Pawn->GetActorLocation()+FVector(0,0,30)-SkinCamera->GetActorLocation()).Rotation()); SkinCamera->GetCameraComponent()->SetFieldOfView(55.f); PC->SetViewTarget(SkinCamera.Get());
        }
        bSkinCameraReady=true; return; // 镜头切换后等下一帧再截图。
    }
    const TCHAR* Label=nullptr; // 每帧最多一张真实截图，未满足状态时不拍空白或旧装备。
    if (!bSkinGun && !Melee->IsEquipped() && Elapsed>.25 && Elapsed<.4) { Label=TEXT("PistolIdle"); bSkinGun=true; }
    if (!bSkinIdle && Melee->IsEquipped() && !Melee->IsAttacking() && Elapsed>.6 && Elapsed<.75) { Label=TEXT("BatIdle"); bSkinIdle=true; }
    float Time=0.f,Progress=0.f; FName Action; // GetPose输出真实服务器phase，匹配两种不同持续时间的动作。
    Melee->GetPose(false,Time,Progress,Action);
    if (!bSkinAttack && Melee->IsAttacking() && Progress>.55) { Label=TEXT("Attack"); bSkinAttack=true; }
    if (Label)
    {
        const FString Name=FString::Printf(TEXT("Skin-LAN-%s-%d-%s-%s.png"),HasAuthority()?TEXT("Host"):TEXT("Client"),Stage,Pawn->IsLocallyControlled()?TEXT("FP"):TEXT("TP"),Label); // 双端/每阶段独立命名，不能互相覆盖Saved。
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("PlayerSkinRegression")/Name,false,false);
        UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_SKIN_CAPTURE %s lod=%d"),*Name,Pawn->GetMesh()->GetPredictedLODLevel());
    }
}
