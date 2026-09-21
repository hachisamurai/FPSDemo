// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "Characters/FPSDemoCharacter.h"
#include "TP_PickUpComponent.generated.h"

// Declaration of the delegate that will be called when someone picks this up
// The character picking this up is the parameter sent with the notification
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPickUp, AFPSDemoCharacter*, PickUpCharacter);

UCLASS(Blueprintable, BlueprintType, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FPSDEMO_API UTP_PickUpComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	
	/** Delegate to whom anyone can subscribe to receive this event */
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnPickUp OnPickUp;

	/** 创建旧模板拾取范围；新 Demo 初始已有枪械，不依赖此入口。 */
	UTP_PickUpComponent();
protected:

	/** Called when the game starts */
	virtual void BeginPlay() override;

	/** OverlappedComponent 为己方范围，OtherActor/OtherComp 为进入对象，OtherBodyIndex 为物理体索引，
	 * bFromSweep 表示扫掠进入，SweepResult 为扫掠信息；回调不拥有这些引用。 */
	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
