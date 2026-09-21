#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoEnemyProjectile.generated.h"

class ADemoEnemy;
class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UPrimitiveComponent;

/** 权威直线飞行物；球体扫掠阻挡墙和玩家，命中一次后销毁，不跟踪、不造成友伤。 */
UCLASS()
class FPSDEMO_API ADemoEnemyProjectile : public AActor
{
    GENERATED_BODY()
public:
    /** 创建可见球体和无重力ProjectileMovement，Actor寿命6秒。 */
    ADemoEnemyProjectile();
    /** Source为存活敌人，Direction为单位发射方向，Speed单位cm/s；发射时冻结GAS攻击点数/关卡。 */
    void Initialize(ADemoEnemy* Source, const FVector& Direction, float Speed);
    /** DeltaSeconds为帧间秒数；来源死亡/换关/终局立即撤销，禁止飞行物进入安全区伤人。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** OnComponentHit同步游戏线程回调；组件/Actor指针只在调用期借用，Hit为扫掠结果，Impulse不用于伤害。 */
    UFUNCTION() void OnImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
        FVector NormalImpulse, const FHitResult& Hit);
    /** 同步判断来源/关卡/Combat有效性；每次碰撞重新确认，不能只依赖Tick的前帧检查。 */
    bool IsAttackValid() const;
    // 根碰撞半径12cm；ProjectileMovement扫掠该组件，避免高速弹穿过薄墙。
    UPROPERTY() TObjectPtr<USphereComponent> Collision;
    // World持有Actor，Actor持有外观/运动组件；外观不参与额外碰撞。
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY() TObjectPtr<UProjectileMovementComponent> Movement;
    // 不延长敌人/ASC生命周期；敌人死亡或销毁则撤销在途弹。
    TWeakObjectPtr<ADemoEnemy> Shooter;
    // 发射时的攻击点数快照，非负；命中用来源ASC生成GE，不读取已改变的攻击力。
    float Damage = 0.f;
    // 发射关卡编号，防止复用竞技场时旧弹命中新关玩家。
    int32 FiredLevel = 0;
    // 在应用GE之前置位，抵御同帧多组件碰撞/死亡回调重入。
    bool bConsumed = false;
};
