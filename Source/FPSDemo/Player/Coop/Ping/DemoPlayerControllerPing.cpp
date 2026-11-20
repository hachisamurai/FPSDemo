// 职责：G快捷标记的本地Enhanced Input与拥有者RPC适配。使用：PC装配/卸载映射，GS组件负责权威射线、全队复制及过期。
#include "Player/DemoPlayerController.h"
#include "Player/Coop/Ping/DemoTeamPingComponent.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Debug/DemoLog.h"

void ADemoPlayerController::InstallTeamPingInput()
{
    DEMO_LOG_CALL(); RemoveTeamPingInput();
    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent); // 当前PC的输入栈，只绑定本模块动作。
    ULocalPlayer* Local = GetLocalPlayer(); // 远端服务器PC没有本地键盘，不生成映射。
    UEnhancedInputLocalPlayerSubsystem* Subsystem = Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr; // 本LocalPlayer独立上下文。
    if (!IsLocalController() || !Input || !Subsystem)
    { UE_LOG(LogFPSDemo, Log, TEXT("TEAM_PING_INPUT unavailable for nonlocal/uninitialized controller")); return; }
    TeamPingAction = NewObject<UInputAction>(this); TeamPingAction->ValueType = EInputActionValueType::Boolean;
    TeamPingMapping = NewObject<UInputMappingContext>(this); TeamPingMapping->MapKey(TeamPingAction, EKeys::G);
    TeamPingBindingHandle = Input->BindAction(TeamPingAction, ETriggerEvent::Started, this, &ADemoPlayerController::RequestTeamPing).GetHandle();
    TeamPingInput = Input; bTeamPingBound = true;
    FModifyContextOptions Options; // 装配时忽略仍按住的G，防重建输入时误发一次标记。
    Options.bIgnoreAllPressedKeysUntilRelease = true;
    Subsystem->AddMappingContext(TeamPingMapping, 2, Options);
}

void ADemoPlayerController::RemoveTeamPingInput()
{
    DEMO_LOG_CALL();
    if (TeamPingInput.IsValid() && bTeamPingBound) TeamPingInput->RemoveBindingByHandle(TeamPingBindingHandle);
    if (ULocalPlayer* Local = GetLocalPlayer()) // EndPlay只借用仍有效的LocalPlayer，不跨World保活。
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) // 当前本地映射容器。
            if (TeamPingMapping) Subsystem->RemoveMappingContext(TeamPingMapping);
    TeamPingInput.Reset(); bTeamPingBound = false; TeamPingBindingHandle = 0;
    TeamPingMapping = nullptr; TeamPingAction = nullptr; NextLocalTeamPingAt = 0.0;
}

void ADemoPlayerController::RequestTeamPing()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 只使用本World复制状态，不从UI猜测阶段。
    const ADemoCharacter* PingAvatar = Cast<ADemoCharacter>(GetPawn()); // 本人真实Pawn，观战队友不能被借用来标记。
    const double Now = FPlatformTime::Seconds(); // 本地单调秒，暂停不积累额外请求。
    if (!IsLocalController() || !IsCoopRegistered() || !State || State->Phase != EDemoPhase::Combat
        || !IsValid(PingAvatar) || !PingAvatar->CanUseCombatAbilities() || HasBlockingOverlay() || Now < NextLocalTeamPingAt)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("TEAM_PING_INPUT rejected phase/registration/avatar/menu/rate")); return; }
    NextLocalTeamPingAt = Now + 1.0;
    ServerTeamPing(); // 无参数意图，由连接拥有权保证this不能指向另一玩家PC。
}

void ADemoPlayerController::ServerTeamPing_Implementation()
{
    DEMO_LOG_TICK(); // 不可信网络输入保留高频埋点，拒绝洪泛不得写默认业务日志。
    ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 服务器当前World唯一公开状态容器。
    if (!HasAuthority() || !State || !State->GetTeamPingComponent())
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("TEAM_PING_RPC rejected authority/state readiness")); return; }
    State->GetTeamPingComponent()->RequestPing(this);
}
