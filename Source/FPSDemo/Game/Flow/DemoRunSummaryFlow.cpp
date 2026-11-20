// 职责：Flow把结束事实送往各PS战报组件。使用：胜利/团灭/系统错误、清理成长之前调用；依赖原Roster和PC只读菜单出口，不新增奖励或保存。
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Progression/Summary/DemoRunSummaryComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

void UDemoRunFlowComponent::CaptureRunSummaries(EDemoRunOutcome Outcome, const FString& Reason)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 同栈借用，组件只复制纯值。
    AFPSDemoGameMode* Mode = Cast<AFPSDemoGameMode>(GetOwner()); // 唯一权威组合根，客户端没有调用路径。
    if (bStopping || !Mode || !Mode->HasAuthority() || !State)
    { UE_LOG(LogFPSDemo, Log, TEXT("RUN_SUMMARY rejected stopped/authority/state")); return; }
    for (ADemoCharacter* Member : Mode->GetCoopRoster()->GetPlayers()) // 包含已阵亡的本场参战者，各自读取本人的成长。
    {
        ADemoPlayerState* Player = IsValid(Member) ? Member->GetPlayerState<ADemoPlayerState>() : nullptr; // 不保存跨Pawn裸指针。
        if (!Player || !Player->GetRunSummaryComponent()) continue;
        if (Player->GetRunSummaryComponent()->Capture(CampaignRunId, *State, Outcome, Reason))
            if (ADemoPlayerController* Controller = Cast<ADemoPlayerController>(Member->GetController())) Controller->PublishMenuSnapshot();
    }
}
