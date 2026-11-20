// 职责：权威胜利位置快照、两终端事务及公共交互校验；摆放查询归DemoTerminalPlacement，不决定奖励/价格/阶段。
// 依赖：原Flow复活前桥、Roster稳定身份、真实终端Box及本World Area快照；无常驻Tick或额外网络协议。
#include "Interaction/DemoTerminalComponent.h"
#include "Interaction/DemoInteractable.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoAttributeSet.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/DemoPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"
#include "EngineUtils.h"
UDemoTerminalComponent::UDemoTerminalComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
void UDemoTerminalComponent::CaptureVictoryPlacementOrigin(bool bRetryAfterClear)
{
    DEMO_LOG_CALL();
    VictoryOrigins.Reset(); bVictoryCaptured=false; VictoryLevel=INDEX_NONE; VictoryPhaseRevision=0;
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 仅当前权威Combat的真实清关调用可冻结。
    const AFPSDemoGameMode* Mode=Cast<AFPSDemoGameMode>(GetOwner());
    UDemoCoopRosterComponent* Roster=Mode?Mode->GetCoopRoster():nullptr;
    const bool bCanCapture=State && (bRetryAfterClear ? (State->Phase==EDemoPhase::Reward || State->Phase==EDemoPhase::Intermission)
        : State->Phase==EDemoPhase::Combat); // 第二次及以后只服务备战终端，不重入击杀或奖励。
    if (bStopped || !Mode || !Mode->HasAuthority() || !bCanCapture || !Roster)
    { UE_LOG(LogFPSDemo,Log,TEXT("TERMINAL_VICTORY_CAPTURE rejected lifecycle/authority/phase/roster")); return; }
    bVictoryCaptured=true; VictoryLevel=State->LevelNumber; VictoryPhaseRevision=State->PhaseRevision;
    for (const ADemoCharacter* Player:Roster->GetPlayers(true)) // 必须在RefillPlayer前；已阵亡或未登记的远端不成为摆放基准。
    {
        const ADemoPlayerController* PC=Player?Cast<ADemoPlayerController>(Player->GetController()):nullptr;
        const UCapsuleComponent* Capsule=Player?Player->GetCapsuleComponent():nullptr;
        if (!Player || Player->GetWorld()!=GetWorld() || !Player->HasAuthority() || !PC || PC->GetPawn()!=Player || !Capsule || VictoryOrigins.Num()>=4) continue;
        FDemoTerminalPlacementOrigin Origin; // 所有数据由服务器实际Pawn读取，后续复活/菜单不会改变胜利事实。
        Origin.PlayerKey=Roster->GetPlayerKey(Player); Origin.bLeader=Roster->IsLeader(PC);
        if (Origin.PlayerKey.IsEmpty() && !Origin.bLeader) { UE_LOG(LogFPSDemo,Log,TEXT("TERMINAL_VICTORY_CAPTURE skip unregistered survivor=%s"),*GetNameSafe(Player)); continue; }
        if (Origin.PlayerKey.IsEmpty()) Origin.PlayerKey=TEXT("LOCAL_LEADER"); // 本机握手晚到仍有唯一房主身份；不授予远端冒充权限。
        Origin.Location=Player->GetActorLocation(); Origin.Forward=Player->GetActorForwardVector().GetSafeNormal2D();
        Origin.CapsuleRadius=Capsule->GetScaledCapsuleRadius(); Origin.CapsuleHalfHeight=Capsule->GetScaledCapsuleHalfHeight();
        Origin.MaxStepHeight=Player->GetCharacterMovement()?Player->GetCharacterMovement()->MaxStepHeight:45.f;
        if (Origin.Location.ContainsNaN() || Origin.Forward.IsNearlyZero() || !FMath::IsFinite(Origin.CapsuleRadius) || Origin.CapsuleRadius<=0.f
            || !FMath::IsFinite(Origin.CapsuleHalfHeight) || Origin.CapsuleHalfHeight<Origin.CapsuleRadius || !FMath::IsFinite(Origin.MaxStepHeight) || Origin.MaxStepHeight<=0.f)
        { UE_LOG(LogFPSDemo,Warning,TEXT("TERMINAL_VICTORY_CAPTURE skip invalid actual capsule/pose key=%s"),*Origin.PlayerKey); continue; }
        VictoryOrigins.Add(MoveTemp(Origin));
    }
    VictoryOrigins.Sort([](const FDemoTerminalPlacementOrigin& A,const FDemoTerminalPlacementOrigin& B) // 无捕获、同步值排序；房主生存优先，其余稳定Roster键，不依赖Actor遍历/客户端Camera顺序。
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] TerminalVictoryOriginSort")); return A.bLeader!=B.bLeader?A.bLeader:A.PlayerKey.Compare(B.PlayerKey,ESearchCase::CaseSensitive)<0; });
    UE_LOG(LogFPSDemo,Log,TEXT("TERMINAL_VICTORY_CAPTURE level=%d revision=%u survivors=%d preferred=%s"),VictoryLevel,VictoryPhaseRevision,VictoryOrigins.Num(),VictoryOrigins.IsEmpty()?TEXT("none"):*VictoryOrigins[0].PlayerKey);
}
bool UDemoTerminalComponent::CreateTerminals(const FDemoAreaSnapshot& Area, bool bHubAppearance)
{
	DEMO_LOG_CALL();
    if (bStopped || !GetOwner() || !GetOwner()->HasAuthority()) { UE_LOG(LogFPSDemo,Log,TEXT("TERMINAL_CREATE rejected lifecycle/authority")); return false; }
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // Hub/读档没有本次Combat捕获，不改为追随玩家的另一套安全区布局。
    const bool bCombatClear=State && State->Phase==EDemoPhase::Combat && State->LevelNumber>0;
    const bool bMatchingCapture=bVictoryCaptured && State && VictoryLevel==State->LevelNumber && VictoryPhaseRevision==State->PhaseRevision;
    TArray<FDemoTerminalPlacementOrigin> Origins=bMatchingCapture?MoveTemp(VictoryOrigins):TArray<FDemoTerminalPlacementOrigin>(); // 同关同阶段的原地清关或延迟摆放快照，只消费一次。
    ClearTerminals();
    if (bCombatClear && (!bMatchingCapture || Origins.IsEmpty()))
    { UE_LOG(LogFPSDemo,Error,TEXT("TERMINAL_CREATE missing current pre-refill victory survivors; refusing remote authored-center fallback")); return false; }
    const ADemoInteractable* Default=GetDefault<ADemoInteractable>(); // 使用真实终端原生CDO碰撞尺寸，不复制另一份60/60/80常数。
    const UBoxComponent* Box=Default?Cast<UBoxComponent>(Default->GetRootComponent()):nullptr;
    FDemoTerminalPlacementPair Pair; FString Error; // 两个位置先全部通过同步安全检查，才创建任何终端Actor。
    if (!Box) { UE_LOG(LogFPSDemo,Error,TEXT("TERMINAL_CREATE missing root collision box")); return false; }
    if (!DemoTerminalPlacement::ResolvePair(*GetWorld(),Area,Box->GetScaledBoxExtent(),Origins,Pair,Error))
    {
        if (Origins.IsEmpty()) { UE_LOG(LogFPSDemo,Warning,TEXT("TERMINAL_CREATE authored placement rejected: %s"),*Error); return false; }
        UE_LOG(LogFPSDemo,Warning,TEXT("TERMINAL_NEAR_UNAVAILABLE level=%d room=%s origin=%s reason=%s"),State->LevelNumber,
            *Area.Geometry.ExplicitSourceLevelPackage.ToString(),*Origins[0].Location.ToCompactString(),*Error);
        // 当前房间的作者交互点也走同源地面、四角支撑和占位预检；不传送玩家，不切换房间或借用Hub。
        if (!DemoTerminalPlacement::ResolvePair(*GetWorld(),Area,Box->GetScaledBoxExtent(),{},Pair,Error))
        { UE_LOG(LogFPSDemo,Warning,TEXT("TERMINAL_CREATE same-room fallback blocked: %s"),*Error); return false; }
    }
    FActorSpawnParameters Spawn; // 不允许引擎把已核地面/墙/通路的点挪到另一楼层或障碍背后。
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
    // 2026-11-20用户要求局内终端换成局外模型：局内外统一用Hub外观类，原Actor通道随生成消息传类，客户端无需新字段；bHubAppearance只进日志。
    UClass* TerminalClass=ADemoHubInteractable::StaticClass();
    const FRotator Facing(0.f,Pair.Yaw,0.f); // 清关时为90°吸附的朝向（正方形根盒占位不变），Hub作者锚点为0；随Actor初始复制。
    UE_LOG(LogFPSDemo,Log,TEXT("TERMINAL_CREATE class=%s physical_hub=%d yaw=%.0f"),*TerminalClass->GetName(),bHubAppearance,Pair.Yaw);
    ShopTerminal=GetWorld()->SpawnActor<ADemoInteractable>(TerminalClass,Pair.Shop,Facing,Spawn);
    if (IsValid(ShopTerminal)) NextLevelTerminal=GetWorld()->SpawnActor<ADemoInteractable>(TerminalClass,Pair.Next,Facing,Spawn);
	if (!IsValid(ShopTerminal) || !IsValid(NextLevelTerminal))
	{
		UE_LOG(LogFPSDemo, Error, TEXT("Terminals strict spawn failed; rolling back pair attempts=%d"),Pair.Attempts);
		ClearTerminals();
		return false;
	}
	ShopTerminal->Configure(true);
	NextLevelTerminal->Configure(false);
	UE_LOG(LogFPSDemo,Log,TEXT("TERMINALS READY victory=%d key=%s attempts=%d shop=%s next=%s"),bCombatClear,*Pair.PlayerKey,Pair.Attempts,*ShopTerminal->GetActorLocation().ToString(),*NextLevelTerminal->GetActorLocation().ToString());
	return true;
}
void UDemoTerminalComponent::ClearTerminals()
{
	DEMO_LOG_CALL();
    ++Generation; // 先废弃窗口版本，Destroy期间的同步回调也不能提交旧交易。
    Sessions.Empty(); // 同栈撤销全队旧交互，客户端迟到请求只得到明确拒绝。
    VictoryOrigins.Reset(); VictoryLevel=INDEX_NONE; VictoryPhaseRevision=0; bVictoryCaptured=false; // 切战斗/旅行/失败一并撤销旧位置，创建时已先消费到局部纯值。
	// Actor::Destroy 延迟释放 UObject；当前 Interact 调用栈可以安全返回，但旧终端已不再有效。
	if (IsValid(ShopTerminal)) ShopTerminal->Destroy();
	if (IsValid(NextLevelTerminal)) NextLevelTerminal->Destroy();
	ShopTerminal = nullptr;
	NextLevelTerminal = nullptr;
}
ADemoInteractable* UDemoTerminalComponent::GetShopTerminal() const { DEMO_LOG_TICK(); return ShopTerminal; }
ADemoInteractable* UDemoTerminalComponent::GetNextLevelTerminal() const { DEMO_LOG_TICK(); return NextLevelTerminal; }
uint64 UDemoTerminalComponent::GetGeneration() const { DEMO_LOG_TICK(); return Generation; }
void UDemoTerminalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{ DEMO_LOG_CALL(); Shutdown(); Super::EndPlay(EndPlayReason); }
void UDemoTerminalComponent::Shutdown()
{ DEMO_LOG_CALL(); if (bStopped) return; bStopped = true; ClearTerminals(); } // 先关闭服务，再使现存窗口版本失效。
FString UDemoTerminalComponent::ValidateInteraction(const ADemoCharacter* Player, const ADemoInteractable* Terminal, uint64 ExpectedGeneration) const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前权威阶段，不相信打开窗口时的快照。
    if (bStopped || !GetOwner()->HasAuthority() || GetWorld()->IsPaused() || !IsValid(Player) || Player->GetWorld() != GetWorld()
        || !Player->GetController() || Player->GetController()->GetPawn() != Player || !State || !IsValid(Terminal)
        || (Terminal != ShopTerminal && Terminal != NextLevelTerminal) || (ExpectedGeneration && Generation != ExpectedGeneration))
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("TERMINAL_REJECT stale/owner/world/pause")); return TEXT("终端或玩家尚未就绪，或交互已过期"); }
    if (!Player->GetDemoAttributes() || Player->GetDemoAttributes()->GetHealth() <= 0)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("TERMINAL_REJECT dead")); return TEXT("当前玩家无法操作终端"); }
    if (State->Phase != EDemoPhase::Hub && State->Phase != EDemoPhase::Intermission)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("TERMINAL_REJECT phase")); return TEXT("请在备战阶段与终端交互"); }
    if (FVector::Dist(Player->GetActorLocation(), Terminal->GetActorLocation()) > 250.f)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("TERMINAL_REJECT range")); return TEXT("距离过远，请返回终端附近"); }
    return FString();
}
bool UDemoTerminalComponent::BeginInteraction(ADemoCharacter* Player, ADemoInteractable* Terminal)
{
    DEMO_LOG_CALL();
    const FString Error = ValidateInteraction(Player, Terminal); // 只从当前权威对象签发会话。
    if (!Error.IsEmpty()) { UE_LOG(LogFPSDemo, Log, TEXT("Interaction rejected: %s"), *Error); return false; }
    Sessions.Add(Player, TPair<TWeakObjectPtr<ADemoInteractable>, uint64>(Terminal, Generation));
    return true;
}
void UDemoTerminalComponent::EndInteraction(ADemoCharacter* Player) { DEMO_LOG_CALL(); Sessions.Remove(Player); }
FString UDemoTerminalComponent::ValidateSession(const ADemoCharacter* Player, bool bShop) const
{
    DEMO_LOG_TICK();
    const auto* Session = Sessions.Find(TWeakObjectPtr<ADemoCharacter>(const_cast<ADemoCharacter*>(Player))); // 只读会话，无权限来自客户端页面。
    if (!Session || Session->Key.Get() != (bShop ? ShopTerminal.Get() : NextLevelTerminal.Get()))
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("Terminal session absent/type mismatch")); return TEXT("请重新与对应终端交互"); }
    return ValidateInteraction(Player, Session->Key.Get(), Session->Value);
}
