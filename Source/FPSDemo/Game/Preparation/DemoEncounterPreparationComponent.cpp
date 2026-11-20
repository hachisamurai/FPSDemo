// 职责：同一可取消屏障依次准备服务器地图/导航、客户端地图/资源/落点与引擎可见性ACK，最后由原Flow重验出发；不持有奖励或存档权限。
#include "Game/Preparation/DemoEncounterPreparationComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/Coop/Ready/DemoTeamReadyComponent.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Spawning/DemoSpawnPlan.h"
#include "AI/DemoEnemy.h"
#include "Animation/DemoEnemyAppearance.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

UDemoEncounterPreparationComponent::UDemoEncounterPreparationComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }

bool UDemoEncounterPreparationComponent::CollectResources(const FDemoSpawnPlan& Plan, TArray<FSoftObjectPath>& Out, FString& Error) const
{
    DEMO_LOG_CALL();
    TArray<const UObject*> Sources; // 只借用已通过服务端Plan校验的资产，未把客户端路径接入生成器。
    for (const FDemoSpawnGroup& Group : Plan.Groups)
    { Sources.Add(Group.EnemyClass.Get()); Sources.Add(Group.Body.Appearance.Get()); Sources.Add(Group.Body.HitProfile.Get()); }
    const AFPSDemoGameMode* Mode = CastChecked<AFPSDemoGameMode>(GetOwner()); // 同GM目录，最多四位玩家。
    for (ADemoCharacter* Member : Mode->GetCoopRoster()->GetPlayers())
        for (int32 Index = 0; Index < 4; ++Index) // 四枪目录预载类与子弹，材质/动画硬引用闭包由引擎包加载维护。
            if (const ADemoWeaponBase* Weapon = Member->GetWeaponComponent()->GetCatalogWeapon(Index))
            { Sources.Add(Weapon->GetClass()); Sources.Add(Weapon->Config.ProjectileClass.Get()); }
    Out.Reset();
    for (const UObject* Source : Sources)
    {
        if (!IsValid(Source)) continue; // 可选原生类不需要资源路径；必需敌人外观已经过Plan.Validate。
        const FSoftObjectPath Path(Source); // 原生/Script类随模块加载，只发送可Cook的/Game内容。
        if (!Path.GetLongPackageName().StartsWith(TEXT("/Game/"))) continue;
        if (!Path.IsValid() || Path.ToString().Len() > 256 || !Path.GetSubPathString().IsEmpty())
        { Error = TEXT("本关资源入口不符合预载契约"); Out.Reset(); return false; }
        Out.AddUnique(Path);
        if (Out.Num() > 64) { Error = TEXT("本关资源入口超过64项预算，请拆分内容包"); Out.Reset(); return false; }
    }
    return true;
}

bool UDemoEncounterPreparationComponent::EnsureReady(const FString& Key, const FDemoSpawnPlan& Plan, int32 ArenaIndex, FName RoomId, FDemoAreaSnapshot& Area, FString& Error)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode = Cast<AFPSDemoGameMode>(GetOwner()); // 依赖均为当前权威World，禁止迟到请求在新World消费旧Key。
    ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr;
    if (bStopping || !Mode || !Mode->HasAuthority() || !State || Key.IsEmpty()) { Error = TEXT("入场准备服务不可用"); return false; }
    if (bActive && ActiveKey == Key)
    {
        if (bReadyToCommit && AreAllClientsReady()) { Area = PreparedArea; return true; }
        Error = State->Preparation.Status; return false;
    }
    if (bActive) Cancel(TEXT("出发内容发生变化，请重新准备"));
    PendingResources.Reset(); RoomRequest = FDemoRoomStreamRequest(); bRequestsSent = false;
    if (!CollectResources(Plan, PendingResources, Error)) return false;
    const TArray<ADemoPlayerController*> Controllers = Mode->GetCoopRoster()->GetControllers(); // 冻结在线身份，旧PC/空Avatar不通过。
    if (Controllers.IsEmpty() || Controllers.Num() > 4) { Error = TEXT("入场人数必须为1至4人"); return false; }
    for (ADemoPlayerController* Player : Controllers) if (!IsValid(Player) || !Cast<ADemoCharacter>(Player->GetPawn())) { Error = TEXT("等待队员角色初始化"); return false; }
    if (Generation == MAX_int32) { Error = TEXT("本World准备代数耗尽，请返回大厅重新载入"); return false; } // 不复用已取消实例名；正常游玩无法达到该次数。
    ++Generation; Stage = Plan.Level; ReadyRevision = State->TeamReady.Revision; PhaseRevision = State->PhaseRevision;
    UDemoRoomStreamingSubsystem* Rooms = GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>(); // 与客户端同接口，Listen本地不重复拥有第二实例。
    if (!RoomId.IsNone())
    {
        if (!Rooms || !GetDefault<UDemoRoomStreamingSettings>()->BuildRequest(RoomId, Generation, ArenaIndex, RoomRequest, Error)) return false;
        for (int32 Index = 0; Index < 3; ++Index) // 重试代数不决定槽位；始终避开当前正在站立的房间。
        {
            FDemoAreaSnapshot Current; FString ActiveError; // 同步借用当前已激活房间，不借场景Actor。
            if (Rooms->GetActiveArea(Index, Current, ActiveError))
            {
                if (!GetDefault<UDemoRoomStreamingSettings>()->UseOtherSlot(Current.Transform,RoomRequest,Error)) return false; // 同一安装配置选空槽，不以旧150m中点猜大图位置。
                break;
            }
        }
        if (!Rooms->BeginPrepare(RoomRequest, Error)) return false;
    }
    else if (!GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>()->ResolveArea(ArenaIndex, PreparedArea, Error)) return false;
    ActiveKey = Key; Members.Reset(); Avatars.Reset(); RequiredTerminals.Reset(); PendingFailure = NAME_None; bActive = true; bReadyToCommit = false; Deadline = FPlatformTime::Seconds() + 30.;
    for (ADemoPlayerController* Player : Controllers)
    {
        Members.Add(Player, false); Avatars.Add(Player, Player->GetPawn());
        if (Mode->GetTerminalSystem()->ValidateSession(Cast<ADemoCharacter>(Player->GetPawn()), false).IsEmpty()) RequiredTerminals.Add(Player);
    }
    Publish(TEXT("正在准备服务器地图、导航和入场资源"));
    const EDemoRoomLoadStatus ServerStatus = AdvanceServerPreparation(Error); // 未显示的服务器地图绝不发送客户端实例请求。
    if (ServerStatus == EDemoRoomLoadStatus::Failed) { Cancel(Error); return false; }
    Watchdog = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDemoEncounterPreparationComponent::Poll), .1f); // 不在同步Client RPC中直接重入Flow，至少等下一次Ticker。
    Error = State->Preparation.Status;
    UE_LOG(LogFPSDemo, Log, TEXT("ENCOUNTER_PREPARE begin generation=%d stage=%d players=%d resources=%d room=%s"), Generation, Stage, Members.Num(), PendingResources.Num(), *RoomId.ToString());
    if (AreAllClientsReady()) { bReadyToCommit = true; Area = PreparedArea; Publish(TEXT("入场条件已就绪，正在提交战斗")); return true; } // 当前调用直接返回，不在ClientRPC栈重入Flow。
    return false;
}

EDemoRoomLoadStatus UDemoEncounterPreparationComponent::AdvanceServerPreparation(FString& Error)
{
    DEMO_LOG_TICK();
    if (bRequestsSent) return EDemoRoomLoadStatus::Ready;
    if (!RoomRequest.RoomId.IsNone())
    {
        UDemoRoomStreamingSubsystem* Rooms = GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>(); // World唯一地图生命周期所有者。
        const EDemoRoomLoadStatus Result = Rooms->PollPrepared(PreparedArea, Error); // 锚点/导航仅权威侧裁决，失败由上层统一取消。
        if (Result != EDemoRoomLoadStatus::Ready) return Result;
    }
    bRequestsSent = true; // 先锁一次性发送；Listen同步ACK不会重入发送循环。
    Publish(TEXT("服务器房间就绪，等待全队地图、资源与落点确认"));
    AFPSDemoGameMode* Mode = CastChecked<AFPSDemoGameMode>(GetOwner()); // 同步借用已冻结目录。
    int32 MemberIndex = 0; // 使用与Flow传送相同的Pawn顺序及120cm间距。
    for (ADemoCharacter* Pawn : Mode->GetCoopRoster()->GetPlayers())
    {
        ADemoPlayerController* Player = CastChecked<ADemoPlayerController>(Pawn->GetController()); // 已经过成员与Pawn检查。
        FDemoEncounterPreloadRequest Request; // 权威地图身份和各人落点，客户端仅消费。
        Request.Generation = Generation; Request.Stage = Stage; Request.Resources = PendingResources; Request.Room = RoomRequest;
        Request.LandingPoint = PreparedArea.GetCombatLandingPoint(MemberIndex++);
        Player->ClientPrepareEncounter(Request);
    }
    return EDemoRoomLoadStatus::Ready;
}

bool UDemoEncounterPreparationComponent::AreAllClientsReady() const
{
    DEMO_LOG_TICK();
    if (!bRequestsSent || !PendingFailure.IsNone() || Members.IsEmpty()) return false;
    const UDemoRoomStreamingSubsystem* Rooms = GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>(); // 引擎Level可见性确认独立于客户端业务ACK。
    for (const auto& Entry : Members)
        if (!Entry.Key.IsValid() || !Entry.Value || (!RoomRequest.RoomId.IsNone() && (!Rooms || !Rooms->IsConnectionReady(Entry.Key.Get())))) return false;
    return true;
}

void UDemoEncounterPreparationComponent::ReportReady(ADemoPlayerController* Player, int32 InGeneration, bool bReady, FName Code)
{
    DEMO_LOG_CALL();
    bool* Ready = Members.Find(Player); // 仅查冻结身份，客户端无法替其他连接报告。
    if (bStopping || !bActive || !bRequestsSent || InGeneration != Generation || !Ready || *Ready)
    { UE_LOG(LogFPSDemo, Log, TEXT("ENCOUNTER_PREPARE ignored stale/foreign/duplicate report")); return; }
    if (!bReady)
    {
        PendingFailure = Code == TEXT("LandingTimeout") ? FName(TEXT("LandingTimeout")) : FName(TEXT("ResourcesFailed")); // 只保留固定分类，不公开客户端任意错误字符串。
        Publish(TEXT("资源准备失败，正在取消本次出发")); return; // 下一Ticker统一取消，不能在同步Listen回执中重入EnsureReady发送循环。
    }
    *Ready = true;
    Publish(TEXT("等待全队资源与入场地面确认")); // 只在实际成员确认时复制，客户端不上传大文本。
}

bool UDemoEncounterPreparationComponent::Poll(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    AFPSDemoGameMode* Mode = Cast<AFPSDemoGameMode>(GetOwner()); // 当前World同步借用，组件EndPlay先撤销Ticker。
    ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr;
    if (bStopping || !bActive || !Mode || !State || GetWorld()->bIsTearingDown) { Watchdog.Reset(); return false; }
    if (!PendingFailure.IsNone()) { Cancel(PendingFailure == TEXT("LandingTimeout") ? TEXT("队员入场地面未就绪，已取消出发；可重试或返回检查点") : TEXT("队员资源准备失败，已取消出发；请检查安装资源后重试")); Watchdog.Reset(); return false; }
    if (FPlatformTime::Seconds() >= Deadline) { Cancel(TEXT("等待队员资源确认超过30秒，已取消出发；保留当前检查点，可重试")); Watchdog.Reset(); return false; }
    const TArray<ADemoPlayerController*> Controllers = Mode->GetCoopRoster()->GetControllers(); // 掉线或临时新成员均取消，不能悄悄重算人数倍率。
    bool bContextValid = (State->Phase == EDemoPhase::Hub || State->Phase == EDemoPhase::Intermission) && State->PhaseRevision == PhaseRevision && State->TeamReady.Revision == ReadyRevision && Controllers.Num() == Members.Num();
    for (ADemoPlayerController* Player : Controllers)
    {
        if (!IsValid(Player)) { bContextValid = false; continue; }
        const TWeakObjectPtr<APawn>* Original = Avatars.Find(Player); // Pawn被替换也必须重新检查落点，不沿用旧确认。
        bContextValid &= IsValid(Player) && Members.Contains(Player) && Original && Original->IsValid() && Original->Get() == Player->GetPawn();
        if (RequiredTerminals.Contains(Player)) bContextValid &= Mode->GetTerminalSystem()->ValidateSession(Cast<ADemoCharacter>(Player->GetPawn()), false).IsEmpty();
    }
    if (!bContextValid) { Cancel(TEXT("成员、装备、准备或终端状态变化，已取消资源等待，请重新确认出发")); Watchdog.Reset(); return false; }
    FString PreparationError; // 可恢复等待与确定失败分开，不逐帧覆盖公开状态或重复写盘。
    const EDemoRoomLoadStatus ServerStatus = AdvanceServerPreparation(PreparationError);
    if (ServerStatus == EDemoRoomLoadStatus::Failed) { Cancel(PreparationError); Watchdog.Reset(); return false; }
    if (ServerStatus == EDemoRoomLoadStatus::Pending || !AreAllClientsReady()) return true;
    FString ReadyError; // 加载ACK不能代替原生命/GAS/核心/领奖门禁，全部就绪后仍重验。
    if (!Mode->GetTeamReadySystem()->ValidateDeparture(ReadyError)) { Cancel(ReadyError); Watchdog.Reset(); return false; }
    if (bReadyToCommit) return true; // 保存/目标预检失败后继续监视取消与超时，但不能10Hz重试写盘或重入开战。
    bReadyToCommit = true; Publish(TEXT("全队资源就绪，正在提交战斗"));
    const int32 CommittingGeneration = Generation; // Flow重验可能拒绝或换新请求，不能把新Ticker当旧句柄清除。
    Mode->GetRunFlow()->StartNextLevel(); // 重新走完整配置/保存/目标准备事务，EnsureReady只认同Key；失败仍留备战供重试。
    if (Generation != CommittingGeneration) return false; // 新请求已经独立注册Ticker，此处只结束旧回调。
    if (bActive) return true; // 仍留备战时保留原上下文看门狗，用户可重试/取消，不让加入门禁永远挂住。
    Watchdog.Reset(); return false;
}

void UDemoEncounterPreparationComponent::Publish(const FString& Status)
{
    DEMO_LOG_CALL();
    ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr;
    if (bStopping || !State || GetWorld()->bIsTearingDown) return;
    FDemoEncounterPreparationView View; // 全队公开有限状态，不携带私有资源目录/经济或账号键。
    View.Generation = Generation; View.Stage = Stage; View.TotalPlayers = Members.Num(); View.bActive = bActive; View.bReadyToCommit = bReadyToCommit; View.Status = Status.Left(128);
    for (const auto& Entry : Members) if (Entry.Value) ++View.ReadyPlayers;
    State->Preparation = MoveTemp(View); State->ForceNetUpdate();
}

void UDemoEncounterPreparationComponent::Cancel(const FString& Reason)
{
    DEMO_LOG_CALL();
    if (Watchdog.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Watchdog); Watchdog.Reset(); }
    if (!bActive) return;
    bActive = false; bReadyToCommit = false;
    if (!bStopping && GetWorld() && !GetWorld()->bIsTearingDown)
        for (const auto& Entry : Members) if (Entry.Key.IsValid()) Entry.Key->ClientCancelEncounterPreload(Generation); // World拆卸由各PC.EndPlay本地清理，不在网络销毁栈发送新RPC。
    if (!RoomRequest.RoomId.IsNone() && GetWorld())
        if (UDemoRoomStreamingSubsystem* Rooms = GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>()) Rooms->CancelPrepared(); // 仅取消候选，不卸载玩家仍停留的已激活房间。
    if (!bStopping)
    {
        Publish(Reason);
        if (ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>()) State->FailureReason = Reason.Left(128);
    }
    Members.Reset(); Avatars.Reset(); RequiredTerminals.Reset(); ActiveKey.Empty(); PendingFailure = NAME_None; PendingResources.Reset(); RoomRequest = FDemoRoomStreamRequest(); bRequestsSent = false;
    UE_LOG(LogFPSDemo, Log, TEXT("ENCOUNTER_PREPARE cancelled generation=%d reason=%s"), Generation, *Reason.Left(128));
}

bool UDemoEncounterPreparationComponent::Consume()
{
    DEMO_LOG_CALL();
    if (!bActive || !bReadyToCommit || !AreAllClientsReady()) { UE_LOG(LogFPSDemo, Warning, TEXT("ENCOUNTER_CONSUME rejected incomplete barrier")); return false; }
    if (!RoomRequest.RoomId.IsNone())
    {
        if (!GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>()->CommitPrepared()) return false; // 房间失效不得继续开战，原Flow撤销候选目标。
        for (const auto& Entry : Members) if (Entry.Key.IsValid()) Entry.Key->ClientCommitEncounterPreload(Generation); // 客户端同样保留脚下旧碰撞直到位置复制到达。
    }
    if (Watchdog.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Watchdog); Watchdog.Reset(); }
    bActive = false; bReadyToCommit = false; Publish(FString()); // 客户端资源句柄保留至下一次请求/World结束；旧ACK被bActive拒绝。
    Members.Reset(); Avatars.Reset(); RequiredTerminals.Reset(); ActiveKey.Empty(); PendingFailure = NAME_None; PendingResources.Reset(); RoomRequest = FDemoRoomStreamRequest(); bRequestsSent = false;
    return true;
}

bool UDemoEncounterPreparationComponent::IsPending() const { DEMO_LOG_TICK(); return bActive; }
void UDemoEncounterPreparationComponent::EndPlay(const EEndPlayReason::Type Reason) { DEMO_LOG_CALL(); bStopping = true; Cancel(TEXT("World结束")); Super::EndPlay(Reason); }
