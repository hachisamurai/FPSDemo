#include "Player/DemoPlayerController.h"
#include "Animation/DemoPlayerAimSettings.h"
#include "Characters/DemoCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Debug/DemoLog.h"

void ADemoPlayerController::UpdateRotation(float DeltaTime)
{
    DEMO_LOG_TICK();
    // 在Super消费鼠标/手柄/后坐增量前设置原生限制，真正限制ControlRotation，而不只把渲染相机截断。
    if (PlayerCameraManager && Cast<ADemoCharacter>(GetPawn()))
    {
        const UDemoPlayerAimSettings* Settings=GetDefault<UDemoPlayerAimSettings>(); // 只读配置CDO，游戏线程同步借用，所有武器共用同一镜头边界。
        PlayerCameraManager->ViewPitchMin=-Settings->LookDownDegrees;
        PlayerCameraManager->ViewPitchMax=Settings->LookUpDegrees;
    }
    Super::UpdateRotation(DeltaTime);
    const bool bAtLimit=PlayerCameraManager && (FMath::IsNearlyEqual(FRotator::NormalizeAxis(GetControlRotation().Pitch),PlayerCameraManager->ViewPitchMin,.05f)
        || FMath::IsNearlyEqual(FRotator::NormalizeAxis(GetControlRotation().Pitch),PlayerCameraManager->ViewPitchMax,.05f)); // 只在进入/离开边界时记录事件，不把逐帧调用提高到Display。
    if (bAtLimit!=bWasAtAimPitchLimit)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_VIEW_LIMIT atLimit=%d pitch=%.2f"),bAtLimit,GetControlRotation().Pitch);
        bWasAtAimPitchLimit=bAtLimit;
    }
}
