#pragma once
#include "CoreMinimal.h"
#include "DemoBossDiveSettings.generated.h"

/** 升空俯冲的模板配置；固定3秒无敌与50伤害不随二阶段/难度缩放。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoBossDiveSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") bool bEnabled=true; // 只有Boss生产生成启用，小怪忽略。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float Height=600.f; // 升高cm，[300,1000]，顶棚阻挡拒绝施法。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float RiseSeconds=.6f; // 升空秒，[0.3,2]，期间可受伤。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float DiveSeconds=.6f; // 俯冲目标秒，[0.3,2]，还受最大速度限制。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float MaxDiveSpeed=4000.f; // cm/s，[1000,6000]，远距离不能一帧穿越。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float Radius=300.f; // 落地一次伤害水平半径cm，[150,500]。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float Knockback=900.f; // 水平击退速度cm/s，[0,1600]。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float KnockUp=220.f; // 向上击退速度cm/s，[0,500]。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float RecoverySeconds=1.f; // 落地可受伤硬直秒，[0.5,3]。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float Cooldown=16.f; // 结束后开始冷却秒，[8,60]，二阶段乘战术倍率。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dive") float InitialDelay=8.f; // 出生首次等待秒，[3,60]，避免入场连发大招。
    /** 校验JSON/C++输入有限值和合法范围，不依赖编辑器元数据。 */
    bool IsValid() const;
};
