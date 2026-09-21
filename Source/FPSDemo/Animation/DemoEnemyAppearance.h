#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/AnimInstance.h"
#include "DemoEnemyAppearance.generated.h"

/** 一套骨架的外观契约；只存表现资源，不复制怪物属性或伤害时钟。 */
UCLASS()
class FPSDEMO_API UDemoEnemyAppearance : public UDataAsset
{
    GENERATED_BODY()
public:
    // 强引用确保 Cooker 收集模型；仅由出生外观配置选择，不在运行中换骨架。
    UPROPERTY(EditAnywhere, Category="Appearance") TObjectPtr<USkeletalMesh> Mesh;
    // 薄 AnimBP 必须继承 UDemoEnemyAnimInstance，并使用 FullBody Slot。
    UPROPERTY(EditAnywhere, Category="Appearance") TSubclassOf<UAnimInstance> AnimClass;
    // 稳定键 Melee/Fire/Hit/Death/Area/Global/Rise/Hover/Dive/Recovery/Rage；缺失时记录并保留玩法。
    UPROPERTY(EditAnywhere, Category="Appearance") TMap<FName,TObjectPtr<class UAnimMontage>> Actions;
};
