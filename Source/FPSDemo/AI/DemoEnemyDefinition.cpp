#include "AI/DemoEnemyDefinition.h"
#include "Animation/DemoEnemyAppearance.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"

UDemoEnemyDefinition::UDemoEnemyDefinition()
{
    DEMO_LOG_CALL();
    static ConstructorHelpers::FObjectFinder<UDemoEnemyAppearance> Minion(TEXT("/Game/Enemies/Breach/Animation/DA_Chaser")); // 原生CDO硬引用，保持既有Cook依赖。
    static ConstructorHelpers::FObjectFinder<UDemoEnemyAppearance> Boss(TEXT("/Game/Enemies/Breach/Animation/DA_Warden")); // 独立Boss骨架，不按普通模型放大。
    static ConstructorHelpers::FObjectFinder<UDemoEnemyHitProfile> Zones(TEXT("/Game/Data/Combat/DA_EnemyHitZones")); // 旧区域配置仍为默认真值。
    MinionBody.Appearance=Minion.Object; MinionBody.HitProfile=Zones.Object;
    BossBody.Appearance=Boss.Object; BossBody.HitProfile=Zones.Object; BossBody.CollisionRadius=115.f; BossBody.FallbackScale=2.4f;
}
const FDemoEnemyBodyDefinition& UDemoEnemyDefinition::ResolveBody(bool bBoss) const
{
    DEMO_LOG_CALL(); return bBoss?BossBody:MinionBody;
}
bool FDemoEnemyBodyDefinition::Validate(FString& Error) const
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    const UDemoEnemyHitProfile* Rules=DemoEnemyHitZones::SelectValidProfile(HitProfile); // 同步借用；缺失配置沿用既有原生严格骨名回退。
    if (!FMath::IsFinite(CollisionRadius)||CollisionRadius<1.f||CollisionRadius>120.f||!FMath::IsFinite(FallbackScale)||FallbackScale<=0.f
        ||!Appearance||!Appearance->Mesh||!Appearance->AnimClass||!Appearance->Mesh->GetPhysicsAsset()||!Rules||!Rules->ValidateSkeleton(Appearance->Mesh))
    { Error=TEXT("敌人定义尺寸、模型/动画、PhysicsAsset或受击骨名非法"); UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_DEFINITION_REJECT %s"),*Error); return false; }
    return true;
}
