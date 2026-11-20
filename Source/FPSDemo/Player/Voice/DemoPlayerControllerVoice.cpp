// 职责：PC的按T说话输入适配。Setup/EndPlay管理本地Enhanced Input上下文，实际设备与网络交给PS.Voice，不依赖枪械或Pawn存活。
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/Voice/DemoVoiceComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Debug/DemoLog.h"

void ADemoPlayerController::InstallVoiceInput()
{
    DEMO_LOG_CALL();
    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent); // 当前PC输入栈，默认配置已使用EnhancedInput。
    ULocalPlayer* Local = GetLocalPlayer(); // 远端服务器PC没有LocalPlayer，不创建键盘动作。
    UEnhancedInputLocalPlayerSubsystem* Subsystem = Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr; // 本机独立映射容器。
    if (!Input || !Subsystem) { UE_LOG(LogFPSDemo, Log, TEXT("VOICE_INPUT unavailable for nonlocal/uninitialized controller")); return; }
    VoiceAction = NewObject<UInputAction>(this); VoiceAction->ValueType = EInputActionValueType::Boolean;
    VoiceMapping = NewObject<UInputMappingContext>(this); VoiceMapping->MapKey(VoiceAction, EKeys::T);
    Input->BindAction(VoiceAction, ETriggerEvent::Started, this, &ADemoPlayerController::VoicePressed);
    Input->BindAction(VoiceAction, ETriggerEvent::Completed, this, &ADemoPlayerController::VoiceReleased);
    Input->BindAction(VoiceAction, ETriggerEvent::Canceled, this, &ADemoPlayerController::VoiceReleased);
    Subsystem->AddMappingContext(VoiceMapping, 2);
}

void ADemoPlayerController::RemoveVoiceInput()
{
    DEMO_LOG_CALL(); VoiceReleased();
    ULocalPlayer* Local = GetLocalPlayer(); // 结束阶段只借用尚存LocalPlayer，失效时PS继续负责关麦。
    if (Local)
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) // 当前本地映射容器，不保活。
            if (VoiceMapping) Subsystem->RemoveMappingContext(VoiceMapping);
    VoiceMapping = nullptr; VoiceAction = nullptr;
}

void ADemoPlayerController::VoicePressed()
{
    DEMO_LOG_CALL();
    if (ADemoPlayerState* State = GetPlayerState<ADemoPlayerState>()) // PS可能晚于输入初始化，不缓存旧旅行PS。
        if (UDemoVoiceComponent* Voice = State->GetVoiceComponent()) Voice->StartTalking(); // 同步借用本人组件。
}

void ADemoPlayerController::VoiceReleased()
{
    DEMO_LOG_CALL();
    if (!IsLocalController()) return; // 服务器上的远端PC没有采集权，不清除房主正在听到的队友嘴型。
    if (ADemoPlayerState* State = GetPlayerState<ADemoPlayerState>()) // 完成/取消/菜单均可重复进入，Stop本身幂等。
        if (UDemoVoiceComponent* Voice = State->GetVoiceComponent()) Voice->StopTalking(); // 只停止本人采集，不能关闭队友语音。
}
