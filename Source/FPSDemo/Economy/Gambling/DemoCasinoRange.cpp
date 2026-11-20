// 职责：赏金靶场真实空间训练目标与个人钱包事务。使用：与独立RangeShot GA配合；所有数值从服务器已保存局面读取。
#include "Economy/Gambling/DemoCasinoRange.h"
#include "Economy/Gambling/DemoRangeRoom.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Debug/DemoLog.h"
namespace
{
    /** UTC毫秒与存档同一时基；读档不能重启挑战计时。 */
    int64 NowMs() { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return (FDateTime::UtcNow()-FDateTime(1970,1,1)).GetTicks()/ETimespan::TicksPerMillisecond; }
    /** 只取当前Avatar的拥有者账本；不缓存跨地图PS。 */
    UDemoPlayerProgressComponent* RangeLedger(ADemoCharacter* Player)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); const auto* PS=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr; /* 此栈借用PS。 */ return PS?PS->GetProgressComponent():nullptr; }
    /** Mesh借用当前训练组件；M_Solid只有一个向量颜色参数，读取真实参数名，不能沿用敌人旧代码中无效的Color名字。 */
    void ColorTarget(UStaticMeshComponent* Mesh,const FLinearColor& Color)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        auto* Material=Mesh->CreateAndSetMaterialInstanceDynamic(0); // 组件持有，仅生成阶段调用一次。
        if(!Material) return;
        TArray<FMaterialParameterInfo> Parameters; TArray<FGuid> Ids; // 同步读取现有材质参数，不改源资产。
        Material->GetAllVectorParameterInfo(Parameters,Ids);
        if(Parameters.Num()==1) { Material->SetVectorParameterValue(Parameters[0].Name,Color); UE_LOG(LogFPSDemo,Log,TEXT("RANGE_COLOR parameter=%s"),*Parameters[0].Name.ToString()); }
        else UE_LOG(LogFPSDemo,Warning,TEXT("RANGE_COLOR expected one M_Solid vector parameter, got %d"),Parameters.Num());
    }
}
ADemoCasinoRange::ADemoCasinoRange()
{
    DEMO_LOG_CALL();
    bReplicates=true; bOnlyRelevantToOwner=true; SetReplicateMovement(true); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; PrimaryActorTick.TickInterval=0; // TickInterval按游戏秒调度，会让暂停中的UTC到期检查停止；节流移到Tick内。
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrainingBody")); RootComponent=Body;
    Head=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrainingHead")); Head->SetupAttachment(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere")); // 引擎正式静态网格，Cook通过硬引用包含。
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Solid(TEXT("/Game/LevelPrototyping/Materials/M_Solid")); // 复用已有单向量参数材质；ColorTarget读取真实参数名，避免默认棋盘材质不能染色。
    for(auto* Mesh:{Body.Get(),Head.Get()})
    {
        Mesh->SetStaticMesh(Sphere.Object); Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Mesh->SetCollisionResponseToAllChannels(ECR_Ignore); Mesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); Mesh->SetGenerateOverlapEvents(false); Mesh->SetCastShadow(false);
        Mesh->SetCanEverAffectNavigation(false); if(Solid.Object) Mesh->SetMaterial(0,Solid.Object); // 个人训练不修改全局NavMesh。
    }
    Body->SetWorldScale3D(FVector(.48,.48,.72)); Head->SetRelativeLocation(FVector(0,0,65)); Head->SetRelativeScale3D(FVector(.55,.55,.37));
}
ADemoCasinoRange* ADemoCasinoRange::Find(const ADemoCharacter* Player)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    if(Player&&Player->GetWorld()) for(TActorIterator<ADemoCasinoRange> It(Player->GetWorld());It;++It) if(It->GetOwner()==Player->GetController()&&!It->IsActorBeingDestroyed()) return *It;
    return nullptr;
}
bool ADemoCasinoRange::Start(ADemoCharacter* Player)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    const auto* Ledger=RangeLedger(Player); // 必须已有持久局面，不替代下注事务。
    if(!Player||!Player->HasAuthority()||!Player->GetController()||!Ledger||Ledger->GetSnapshot().Gambling.Arcade.Mode!=6) return false;
    if(Find(Player)) return true;
    FActorSpawnParameters Parameters; Parameters.Owner=Player->GetController(); Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 仅本人网络相关。
    auto* Range=Player->GetWorld()->SpawnActor<ADemoCasinoRange>(ADemoCasinoRange::StaticClass(),FTransform::Identity,Parameters); // Actor实例由World管理。
    if(!Range) { UE_LOG(LogFPSDemo,Warning,TEXT("RANGE_SPAWN failed")); return false; }
    Range->Shooter=Player;
    if(!Range->EnterRoom()) { Range->Destroy(); return false; }
    FVector Eye; FRotator Aim; Player->GetActorEyesViewPoint(Eye,Aim); // 采用角色真实视线位置，靶台面向启动方向。
    Aim.Pitch=0; Aim.Roll=0; Range->Anchor=FTransform(Aim,Eye);
    for(auto* Mesh:{Range->Body.Get(),Range->Head.Get()}) ColorTarget(Mesh,Mesh==Range->Head.Get()?FLinearColor(1,.12,.05):FLinearColor(.05,.5,1)); // 同步材质颜色；形状与尺寸仍区分部位。
    Range->RefreshTarget(); return true;
}
void ADemoCasinoRange::RefreshTarget()
{
    DEMO_LOG_CALL();
    const auto* Ledger=RangeLedger(Shooter.Get()); if(!Ledger) return;
    const auto& Round=Ledger->GetSnapshot().Gambling.Arcade; // 只借用已提交值。
    if(Round.Mode!=6) return;
    DeadlineMs=Round.DeadlineMs; Hits=Round.Step; Score=Round.Step+Round.Heads; ShotsLeft=Round.Values[0]-Round.Shots;
    FRandomStream Layout(Round.Seed); FVector Offset; // 布局只依赖已保存种子和命中数，恢复不重抽。
    for(int32 Index=0;Index<=Round.Step;++Index) Offset=FVector(550+Layout.FRand()*200,Layout.FRandRange(-180,180),Layout.FRandRange(10,90)); // 房间内抬高训练目标，避免首靶被第一人称枪模遮住；种子/索引和计分规则不变。
    SetActorLocation(Anchor.TransformPosition(Offset)); ForceNetUpdate();
}
void ADemoCasinoRange::Fire()
{
    DEMO_LOG_CALL();
    auto* Player=Shooter.Get(); auto* Ledger=RangeLedger(Player); auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 每次射击重新验证存活上下文。
    const auto* State=GetWorld()->GetGameState<ADemoGameState>();
    if(!HasAuthority()||!Player||!Player->CanUsePlayerSkills()||!Ledger||!Mode||!State||State->Phase!=EDemoPhase::Hub||RetireAt>0||FPlatformTime::Seconds()<NextShotAt) { UE_LOG(LogFPSDemo,Log,TEXT("RANGE_SHOT rejected context/cooldown")); return; }
    const auto Before=Ledger->GetSnapshot(); auto Next=Before; auto& Round=Next.Gambling.Arcade; // 单次候选，失败回滚。
    if(Round.Mode!=6) return;
    if(NowMs()>=Round.DeadlineMs||Round.Shots>=Round.Values[0]||Round.Step>=10) { Finish(); return; }
    NextShotAt=FPlatformTime::Seconds()+.15;
    FVector Eye; FRotator Aim; Player->GetActorEyesViewPoint(Eye,Aim); // 服务器持有的控制旋转，客户端只传开火意图。
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CasinoRange),false,Player); // 遮挡物先命中时不会穿墙加分。
    GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Aim.Vector()*2000,ECC_Visibility,Query);
    ++Round.Shots;
    const bool TargetHit=Hit.GetActor()==this,HeadHit=TargetHit&&Hit.GetComponent()==Head; // 独立部位碰撞，训练靶不受战斗天赋/弹药影响。
    if(TargetHit) { ++Round.Step; if(HeadHit) ++Round.Heads; }
    if(!Ledger->Commit(Next,false)||!Mode->SaveCheckpoint()) { Ledger->Commit(Before,false); Feedback=TEXT("保存失败，本发未生效"); UE_LOG(LogFPSDemo,Warning,TEXT("RANGE_SHOT rollback")); return; }
    Ledger->PublishCommitted(); Feedback=TargetHit?(HeadHit?TEXT("头部命中 +2"):TEXT("身体命中 +1")):TEXT("脱靶 · 已消耗1发"); RefreshTarget();
    if(auto* PC=Cast<APlayerController>(Player->GetController())) if(const auto* Weapon=Player->GetWeaponComponent()->GetActiveWeapon()) PC->ClientPlaySound(Weapon->Config.FireSound,.65f); // 训练仅本人确认音，真实扣弹/世界射击表现仍归原武器流程。
    if(Round.Step>=10||Round.Shots>=Round.Values[0]) Finish();
}
void ADemoCasinoRange::Finish()
{
    DEMO_LOG_CALL();
    auto* Ledger=RangeLedger(Shooter.Get()); auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 离线/旅行后不访问旧账本。
    if(!HasAuthority()||!Ledger||!Mode||RetireAt>0) return;
    const auto Before=Ledger->GetSnapshot(); auto Next=Before; // 保留完整私有其他玩法数据以便回滚。
    if(Next.Gambling.Arcade.Mode!=6) { Destroy(); return; }
    FDemoCasinoIntent Intent; Intent.Mode=6; Intent.Operation=2; Intent.Stake=Next.Gambling.Arcade.Stake; // 只领取服务器记录的成绩。
    FDemoCasinoResult Result; FRandomStream Unused(0); FString Error; // 计分结算不进行随机。
    if(!DemoCasino::AdvanceArcade(Next.Gambling.Arcade,Intent,Next.Coins,Result,Unused,Error)||Next.Gambling.Serial==MAX_int32) { Feedback=Error; return; }
    ++Next.Gambling.Serial; Next.Gambling.LastResult=FString::Printf(TEXT("靶场完成：%d分，到账%d金币"),Before.Gambling.Arcade.Step+Before.Gambling.Arcade.Heads,Result.Paid);
    if(!Ledger->Commit(Next,false)||!Mode->SaveCheckpoint()) { Ledger->Commit(Before,false); Feedback=TEXT("正在重试保存成绩"); UE_LOG(LogFPSDemo,Warning,TEXT("RANGE_FINISH rollback")); return; }
    Ledger->PublishCommitted(); Feedback=Next.Gambling.LastResult; RetireAt=FPlatformTime::Seconds()+3.; Body->SetCollisionEnabled(ECollisionEnabled::NoCollision); Head->SetCollisionEnabled(ECollisionEnabled::NoCollision); SetActorHiddenInGame(true); ForceNetUpdate();
    UE_LOG(LogFPSDemo,Display,TEXT("RANGE_SETTLED score=%d paid=%d gold=%d"),Score,Result.Paid,Next.Coins);
}
void ADemoCasinoRange::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if(!Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0))) // 客户端生成后只初始化一次MID；服务器Start已经设置，不能每帧创建材质。
        for(auto* Mesh:{Body.Get(),Head.Get()}) ColorTarget(Mesh,Mesh==Head.Get()?FLinearColor(1,.12,.05):FLinearColor(.05,.5,1));
    if(!HasAuthority()) return;
    const double Now=FPlatformTime::Seconds(); // 暂停不冻结检查，避免只有关闭菜单才结算/退场。
    if(Now<NextAuthorityCheck) return; NextAuthorityCheck=Now+.1;
    if(RetireAt>0) { if(Now>=RetireAt) Destroy(); return; }
    const auto* Ledger=RangeLedger(Shooter.Get()); const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 生命周期和阶段阻止旧靶场跨战斗。
    if(!Ledger||!State||State->Phase!=EDemoPhase::Hub||Ledger->GetSnapshot().Gambling.Arcade.Mode!=6) { Destroy(); return; }
    if(NowMs()>=DeadlineMs) Finish();
}
void ADemoCasinoRange::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    if(HasAuthority())
    {
        if(Reason==EEndPlayReason::Destroyed) ReturnFromRoom(); // 旅行/World清理不改变新地图出生状态。
        if(IsValid(Room)) Room->Destroy();
    }
    Super::EndPlay(Reason);
}
void ADemoCasinoRange::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADemoCasinoRange,DeadlineMs); DOREPLIFETIME(ADemoCasinoRange,Score); DOREPLIFETIME(ADemoCasinoRange,ShotsLeft); DOREPLIFETIME(ADemoCasinoRange,Hits); DOREPLIFETIME(ADemoCasinoRange,Feedback);
}
