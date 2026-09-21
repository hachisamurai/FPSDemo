// Copyright Epic Games, Inc. All Rights Reserved.


#include "Template/FPSDemoPlayerController.h"
#include "Debug/DemoLog.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"

void AFPSDemoPlayerController::BeginPlay()
{
	// 旧模板控制器保留调用追踪；新 Demo 使用 ADemoPlayerController。
	DEMO_LOG_CALL();
	Super::BeginPlay();

	// get the enhanced input subsystem
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		// add the mapping context so we get controls
		Subsystem->AddMappingContext(InputMappingContext, 0);
	}
}
