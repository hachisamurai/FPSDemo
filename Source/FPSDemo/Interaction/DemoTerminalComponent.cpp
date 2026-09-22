#include "Interaction/DemoTerminalComponent.h"
#include "Interaction/DemoInteractable.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoAttributeSet.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"
UDemoTerminalComponent::UDemoTerminalComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
bool UDemoTerminalComponent::CreateTerminals(const FDemoAreaSnapshot& Area)
{
	DEMO_LOG_CALL();
    if (bStopped || !GetOwner()->HasAuthority()) { UE_LOG(LogFPSDemo, Log, TEXT("TERMINAL_CREATE rejected lifecycle/authority")); return false; } // 旧World不再生成交互Actor。
    const FVector Center = Area.Transform.GetLocation(); // 本次日志用区域中心，实际位置来自独立锚点。
	ClearTerminals();
	// Spawn 允许引擎在中心附近微调，避免清关时玩家正好站在终端位置被物体包住。
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	// 分别检测实际地面；白模安全区有约 5cm 高的台面，固定 Z=80 会穿入台面而被拒绝生成。
	for (int32 Index = 0; Index < 2; ++Index) // 0 升级，1 下一关；由下方成功检查保证成对有效。
	{
		const FVector Anchor = Index == 0 ? Area.ShopAnchor : Area.NextAnchor; // 当前终端期望的地面 XY。
		FHitResult GroundHit; // 垂直射线仅用于定位地面，不把玩家胶囊当作支撑台。
		FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(DemoTerminalGround), false);
		GroundQuery.AddIgnoredActor(UGameplayStatics::GetPlayerPawn(this, 0));
		if (!GetWorld()->LineTraceSingleByChannel(GroundHit, Anchor + FVector(0,0,400), Anchor - FVector(0,0,100), ECC_Visibility, GroundQuery))
		{
			UE_LOG(LogFPSDemo, Error, TEXT("No ground below terminal index=%d center=%s"), Index, *Center.ToString());
			ClearTerminals();
			return false;
		}
		const FVector Location = GroundHit.ImpactPoint + FVector(0,0,81); // 根碰撞半高 80cm，再留 1cm 接触容差。
		ADemoInteractable* Terminal = GetWorld()->SpawnActor<ADemoInteractable>(ADemoInteractable::StaticClass(), Location, FRotator::ZeroRotator, Spawn); // World 持有，立即登记便于失败回滚。
		if (Index == 0) ShopTerminal = Terminal;
		else NextLevelTerminal = Terminal;
	}
	if (!IsValid(ShopTerminal) || !IsValid(NextLevelTerminal))
	{
		UE_LOG(LogFPSDemo, Error, TEXT("Terminals spawn failed near %s; rolling back pair"), *Center.ToString());
		ClearTerminals();
		return false;
	}
	ShopTerminal->Configure(true);
	NextLevelTerminal->Configure(false);
	UE_LOG(LogFPSDemo, Log, TEXT("TERMINALS READY center=%s shop=%s next=%s"), *Center.ToString(), *ShopTerminal->GetActorLocation().ToString(), *NextLevelTerminal->GetActorLocation().ToString());
	return true;
}
void UDemoTerminalComponent::ClearTerminals()
{
	DEMO_LOG_CALL();
    ++Generation; // 先废弃窗口版本，Destroy期间的同步回调也不能提交旧交易。
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
