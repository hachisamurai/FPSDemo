#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoEnemyDefinition.generated.h"

/** 出生尺寸与受击/表现必须使用同一份配置；不存生命、伤害或攻击冷却。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoEnemyBodyDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Body", meta=(ClampMin="1", ClampMax="120")) float CollisionRadius=48.f; // 厘米；当前NavMesh代理半径120，超限必须先提供相应导航数据。
    UPROPERTY(EditAnywhere, Category="Body", meta=(ClampMin="0.01")) float FallbackScale=1.f; // 仅缺资源诊断球的比例，骨骼已按厘米制作，不继承此倍率。
    UPROPERTY(EditAnywhere, Category="Body") TObjectPtr<class UDemoEnemyAppearance> Appearance; // 共享只读骨架/动画资源，定义资产强持有并参与Cook。
    UPROPERTY(EditAnywhere, Category="Body") TObjectPtr<class UDemoEnemyHitProfile> HitProfile; // 与实际PhysicsAsset校验使用的精确骨名映射；不制造额外命中球。
    /** 验证有限尺寸、资源和真实骨骼受击契约；Error供开关前诊断，不更改资产。 */
    bool Validate(FString& Error) const;
};

/** 可被旧DT行或现有Enemy蓝图引用的新定义；空引用回退原生CDO，旧资产无需迁移。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoEnemyDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 保留既有Chaser/Warden/HitZone资产路径，原生默认无需新增二进制资产。 */
    UDemoEnemyDefinition();
    /** bBoss仅在出生解析时选择配置；返回值为只读借用，实例需复制并保活资源。 */
    const FDemoEnemyBodyDefinition& ResolveBody(bool bBoss) const;
    UPROPERTY(EditAnywhere, Category="Definition") FDemoEnemyBodyDefinition MinionBody; // 普通体型；旧数据未引用定义时48cm半径。
    UPROPERTY(EditAnywhere, Category="Definition") FDemoEnemyBodyDefinition BossBody; // Boss体型；原生115cm半径，与生成重叠查询一致。
};
