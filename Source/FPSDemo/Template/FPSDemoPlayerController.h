// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSDemoPlayerController.generated.h"

class UInputMappingContext;

/**
 *
 */
UCLASS()
class FPSDEMO_API AFPSDemoPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Context to be used for player input */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputMappingContext* InputMappingContext;

	// Begin Actor interface
protected:

	/** 旧模板映射注册入口；打印调用日志，新 Demo 不实例化此控制器。 */
	virtual void BeginPlay() override;

	// End Actor interface
};
