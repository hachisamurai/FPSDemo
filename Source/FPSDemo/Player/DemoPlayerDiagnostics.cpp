// 职责：Editor本地试玩的低频帧耗时、内存与移动证据。使用：PC PlayerTick调用；依赖平台内存快照/CMC/游戏视口，不读取磁盘、不探测网络、不改变移动。
#if WITH_EDITOR
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Debug/DemoLog.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/DemoGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"

void ADemoPlayerController::RecordRuntimeDiagnostics(float DeltaTime)
{
    DEMO_LOG_TICK();
    if (!IsLocalController()) return;
    const double Now = FPlatformTime::Seconds(); // 不受World暂停或最大Delta钳制影响。
    const double Gap = LastRuntimeTickAt > 0.0 ? Now - LastRuntimeTickAt : 0.0; // 相邻PC Tick实际墙钟间隔，首帧没有可比较样本。
    LastRuntimeTickAt = Now; WorstRuntimeGap = FMath::Max(WorstRuntimeGap, Gap);
    const bool bHitch = Gap >= .15 && Now - LastRuntimeHitchAt >= 2.0; // 150ms视为长间隔，最多每两秒一条警告。
    if (!bHitch && Now - LastRuntimeSampleAt < 1.0) return;
    const ADemoCharacter* ObservedPawn = Cast<ADemoCharacter>(GetPawn()); // 当前Avatar借用；大厅/旅行可能为空，不遮蔽Controller的Pawn成员。
    const UCharacterMovementComponent* Movement = ObservedPawn ? ObservedPawn->GetCharacterMovement() : nullptr; // CMC真值，不以动画播放推断角色移动。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 用阶段/关号区分加载停顿与普通战斗停顿。
    const ULocalPlayer* Local = GetLocalPlayer(); // 只观察本机视口，专服/远端控制器不执行。
    const FViewport* Viewport = Local && Local->ViewportClient ? Local->ViewportClient->Viewport : nullptr; // 本次查询借用，不跨窗口销毁缓存。
    const FPlatformMemoryStats Memory = FPlatformMemory::GetStats(); // 每秒一次轻量系统快照，数值为字节，不创建全进程列表。
    constexpr double MiB = 1024.0 * 1024.0; // 日志统一MiB，避免把物理占用和进程提交量混为一谈。
    const FVector Velocity = ObservedPawn ? ObservedPawn->GetVelocity() : FVector::ZeroVector; // cm/s，可能包含正常冲刺、击退或下落。
    const FVector Acceleration = Movement ? Movement->GetCurrentAcceleration() : FVector::ZeroVector; // cm/s²，帮助区分有输入加速和惯性/外力。
    const FVector LastInput = ObservedPawn ? ObservedPawn->GetLastMovementInputVector() : FVector::ZeroVector; // 引擎上次消费的意图，不声称是物理键盘即时状态。
    const uint32 Keys = (IsInputKeyDown(EKeys::W) ? 1u : 0u) | (IsInputKeyDown(EKeys::A) ? 2u : 0u)
        | (IsInputKeyDown(EKeys::S) ? 4u : 0u) | (IsInputKeyDown(EKeys::D) ? 8u : 0u); // 引擎W/A/S/D缓存位掩码，重映射/手柄需结合LastInput理解。
    UE_LOG(LogFPSDemo, Log, TEXT("RUNTIME_SAMPLE phase=%d level=%d dtMs=%.2f maxGapMs=%.2f paused=%d focus=%d menu=%d moveIgnored=%d registered=%d mode=%d speedLimit=%.1f keys=%u input=%s velocity=%s acceleration=%s workingMiB=%.1f commitMiB=%.1f availPhysicalMiB=%.1f availVirtualMiB=%.1f"),
        State ? static_cast<int32>(State->Phase) : -1, State ? State->LevelNumber : 0, DeltaTime * 1000.f, WorstRuntimeGap * 1000.0,
        GetWorld()->IsPaused(), Viewport && Viewport->HasFocus(), bAppliedMenuInput, IsMoveInputIgnored(), IsCoopRegistered(),
        Movement ? static_cast<int32>(Movement->MovementMode) : -1, Movement ? Movement->MaxWalkSpeed : 0.f, Keys,
        *LastInput.ToString(), *Velocity.ToString(), *Acceleration.ToString(), Memory.UsedPhysical / MiB, Memory.UsedVirtual / MiB, Memory.AvailablePhysical / MiB, Memory.AvailableVirtual / MiB);
    if (bHitch)
    {
        LastRuntimeHitchAt = Now;
        UE_LOG(LogFPSDemo, Warning, TEXT("RUNTIME_HITCH gapMs=%.2f phase=%d level=%d commitMiB=%.1f; inspect adjacent RUNTIME_SAMPLE (focus/pause/loading may also explain gaps)"),
            Gap * 1000.0, State ? static_cast<int32>(State->Phase) : -1, State ? State->LevelNumber : 0, Memory.UsedVirtual / MiB);
    }
    LastRuntimeSampleAt = Now; WorstRuntimeGap = 0.0;
}
#endif
