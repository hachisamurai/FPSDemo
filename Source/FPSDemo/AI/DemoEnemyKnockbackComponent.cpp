// 职责：短时击退的碰撞、落脚支持和生命周期执行。使用：Enemy在AI前驱动；依赖现有平面悬浮敌人根碰撞，位置由Actor复制，客户端不预测伤害/击退。
#include "AI/DemoEnemyKnockbackComponent.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyNavigation.h"
#include "AI/DemoEnemyRoster.h"
#include "AI/DemoCloseCombat.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

UDemoEnemyKnockbackComponent::UDemoEnemyKnockbackComponent()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false;
}
bool UDemoEnemyKnockbackComponent::Apply(const FVector& Direction,float Distance,float Duration,FDemoKnockbackFinished Finished)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 本次同步借用；组件随敌人销毁，不保存来源玩家或跨World引用。
    if (!Enemy || !Enemy->CanReceiveKnockback() || Direction.ContainsNaN() || Direction.GetSafeNormal2D().IsNearlyZero()
        || !FMath::IsFinite(Distance) || Distance<=0.f || Distance>600.f || !FMath::IsFinite(Duration) || Duration<.05f || Duration>2.f)
    { UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_KNOCKBACK_REJECT authority/life/session/boss/parameters")); return false; }
    // 仅中断当前动作，不能调用全量CancelPendingAttacks移除Boss子单位或改变战役存活计数。
    if (UDemoEnemyRoster* Roster=Enemy->FindComponentByClass<UDemoEnemyRoster>()) Roster->Cancel(false); // 上述目标门禁已排除全部Boss；仅中断小怪当前读条/危险物，保留召唤归属及成长。
    if (UDemoCloseCombat* Close=Enemy->FindComponentByClass<UDemoCloseCombat>()) Close->Cancel(TEXT("Knockback")); // 旧近战/冲刺窗口不能在后退期间继续命中。
    if (UDemoEnemyNavigation* Navigation=Enemy->FindComponentByClass<UDemoEnemyNavigation>()) Navigation->Stop(TEXT("Knockback")); // 结束后从新位置重规划，不沿旧路径瞬间追回。
    if (!IsValid(Enemy) || !Enemy->CanReceiveKnockback()) // 取消GA可同步派发回调，不能在已死亡/换关对象上重新开启位移。
    { UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_KNOCKBACK_REJECT context changed during interrupt")); return false; }
    if(bActive) Cancel(TEXT("Replaced")); // 旧冰裂不能偷借新的推力终点；替换是取消而非碰撞完成。
    if(!IsValid(Enemy)||!Enemy->CanReceiveKnockback()) { UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_KNOCKBACK_REJECT context changed during replacement callback")); return false; }
    PushDirection=Direction.GetSafeNormal2D(); PushDistance=Distance; PushDuration=Duration; Elapsed=0.f; bActive=true; Completion=MoveTemp(Finished);
    UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_KNOCKBACK_BEGIN actor=%s distance=%.1f duration=%.2f direction=%s"),*Enemy->GetName(),PushDistance,PushDuration,*PushDirection.ToString());
    return true;
}
bool UDemoEnemyKnockbackComponent::Advance(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (!bActive) return false;
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 当前Actor，所有移动只发生在其权威实例。
    if (!Enemy || !Enemy->CanReceiveKnockback()) { Cancel(TEXT("InvalidContext")); return false; }
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0.f) { Cancel(TEXT("InvalidDelta")); return true; }
    if (DeltaSeconds==0.f) return true; // 暂停/零步长不偷偷执行位移或恢复AI。
    const float OldProgress=Elapsed/PushDuration; // 上帧归一化时刻，0..1。
    Elapsed=FMath::Min(Elapsed+DeltaSeconds,PushDuration);
    const float NewProgress=Elapsed/PushDuration; // 长帧截到截止时间，不多执行尾部位移。
    const float Travel=PushDistance*(FMath::Square(1.f-OldProgress)-FMath::Square(1.f-NewProgress)); // 二次缓出曲线积分差，cm，先快后慢。
    const int32 Steps=FMath::Clamp(FMath::CeilToInt(Travel/15.f),1,40); // 每段最多15cm，总配置上限600cm；长帧也逐段确认地面支持。
    const FVector Offset=PushDirection*(Travel/Steps); // 本帧等距子步，碰墙后立即丢弃余量。
    const UPrimitiveComponent* Root=Cast<UPrimitiveComponent>(Enemy->GetRootComponent()); // 根Sphere用于移动碰撞，不拿受击骨骼当物理运动根。
    if (!Root || !Root->IsQueryCollisionEnabled()) { Cancel(TEXT("MissingCollision")); return true; }
    UDemoEnemyNavigation* Navigation=Enemy->FindComponentByClass<UDemoEnemyNavigation>(); // 同追击的楼梯支撑，击退不能把敌人留在楼梯顶高度。
    if (!Navigation) { Cancel(TEXT("MissingNavigation")); return true; }
    const float GroundReach=FMath::Max(Root->Bounds.BoxExtent.Z+80.f,220.f); // cm，兼容既有导航220cm垂直搜索及110cm悬浮出生；只查支持，不改变高度。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoEnemyKnockbackGround),false,Enemy); // 只读本World，忽略本敌人，不忽略墙/其他碰撞物。
    for (int32 Step=0;Step<Steps && bActive && IsValid(Enemy) && Enemy->CanReceiveKnockback();++Step) // Sweep可同步触发死亡/阶段回调；每段复核，取消后不能继续移动尸体。
    {
        const FVector Destination=Enemy->GetActorLocation()+Offset; // 本段目标只作查询，真正运动必须经过根碰撞Sweep。
        FHitResult Ground; // 世界地面支持，允许静态或可查询动态地面，不依赖客户端导航构建。
        if (!GetWorld()->LineTraceSingleByChannel(Ground,Destination,Destination-FVector(0,0,GroundReach),ECC_Visibility,Query)
            || Ground.ImpactNormal.Z<.5f)
        { Cancel(TEXT("NoGroundSupport")); break; }
        FHitResult Block; // 连续扫掠防穿墙/敌人，禁止Teleport或直接设置终点。
        if (!Navigation->MoveGrounded(Offset,Block)) { Cancel(Block.bBlockingHit?TEXT("Blocked"):TEXT("NoGroundSupport")); break; }
    }
    if (bActive && (!IsValid(Enemy) || !Enemy->CanReceiveKnockback())) Cancel(TEXT("ContextChangedDuringSweep"));
    if (IsValid(Enemy)) Enemy->ForceNetUpdate(); // 复用既有bReplicateMovement，不广播逐帧RPC或让客户端再计算一遍冲量。
    if (bActive && Elapsed>=PushDuration) Cancel(TEXT("Completed"));
    return true; // 末帧同样由击退独占；下一帧才允许追击，避免同帧向前运动抵消打击反馈。
}
void UDemoEnemyKnockbackComponent::Cancel(FName Reason)
{
    DEMO_LOG_CALL();
    if (bActive) UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_KNOCKBACK_END actor=%s reason=%s elapsed=%.3f"),*GetNameSafe(GetOwner()),*Reason.ToString(),Elapsed);
    FDemoKnockbackFinished Finished=MoveTemp(Completion); // 先摘下回执，伤害/死亡同步重入也只能发送一次。
    Completion.Unbind();
    bActive=false; Elapsed=0.f; PushDistance=0.f; PushDirection=FVector::ZeroVector;
    Finished.ExecuteIfBound(Cast<ADemoEnemy>(GetOwner()),Reason); // UObject弱绑定持有者自行复核Session；取消回执不能默认当爆裂。
}
