#pragma once
#include "CoreMinimal.h"
#include "DemoPlayerSole.generated.h"

/** 从实际Shoe材质槽提取的鞋底角点；仅含foot/ball双骨蒙皮，不使用胶囊底部冒充鞋底。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoPlayerSolePoint
{
    GENERATED_BODY()
    // 参考姿势下该角点相对本侧foot骨的厘米位置；Editor从真实LOD0顶点提取。
    UPROPERTY(EditAnywhere, Category="Player") FVector FootLocalPosition = FVector::ZeroVector;
    // 同一参考角点相对ball骨的厘米位置；运行时使用其真实最终姿势保留脚趾蒙皮。
    UPROPERTY(EditAnywhere, Category="Player") FVector BallLocalPosition = FVector::ZeroVector;
    // 归一化foot权重0..1，ball权重为1-此值；提取时拒绝其他骨影响而非默默近似。
    UPROPERTY(EditAnywhere, Category="Player") float FootWeight = 1.f;
};
