// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Debug/DemoLog.h"
#include "FPSDemoProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;

UCLASS(config=Game)
class AFPSDemoProjectile : public AActor
{
	GENERATED_BODY()

	/** Sphere collision component */
	UPROPERTY(VisibleDefaultsOnly, Category=Projectile)
	USphereComponent* CollisionComp;

	/** Projectile movement component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Movement, meta = (AllowPrivateAccess = "true"))
	UProjectileMovementComponent* ProjectileMovement;

public:
	/** 旧模板物理投射物构造；GAS Demo 射击使用 Hitscan，不生成此类。 */
	AFPSDemoProjectile();

	/** 旧模板碰撞回调。HitComp 为己方碰撞体，OtherActor/OtherComp 为被击中对象，
	 * NormalImpulse 为碰撞冲量，Hit 为命中信息；指针仅回调期间借用。 */
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	/** Returns CollisionComp subobject **/
	USphereComponent* GetCollisionComp() const { DEMO_LOG_TICK(); return CollisionComp; }
	/** Returns ProjectileMovement subobject **/
	UProjectileMovementComponent* GetProjectileMovement() const { DEMO_LOG_TICK(); return ProjectileMovement; }
};
