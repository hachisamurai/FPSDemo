#include "Animation/DemoEnemyAnimInstance.h"
#include "AI/DemoEnemy.h"
#include "AbilitySystemComponent.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Debug/DemoLog.h"

void UDemoEnemyAnimInstance::NativeInitializeAnimation()
{
    DEMO_LOG_CALL(); Super::NativeInitializeAnimation();
    Enemy=Cast<ADemoEnemy>(GetOwningActor());
    PreviousLocation=Enemy.IsValid()?Enemy->GetActorLocation():FVector::ZeroVector;
    Speed=0; bMoving=false; bMovementFrozen=false;
}
void UDemoEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::NativeUpdateAnimation(DeltaSeconds);
    if (!Enemy.IsValid()) { Speed=0; bMoving=false; return; }
    const FVector Location=Enemy->GetActorLocation(); // 游戏线程当前位置，仅保存值供图读取。
    const float Distance=FVector::Dist2D(Location,PreviousLocation); // 平面移动忽略 Boss 俯冲高度。
    PreviousLocation=Location;
    bMovementFrozen=Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Frozen);
    Speed=DeltaSeconds>SMALL_NUMBER&&Distance<1500.f&&Enemy->IsAlive()&&!bMovementFrozen?Distance/DeltaSeconds:0.f;
    bMoving=Speed>(bMoving?5.f:12.f);
}
