// 职责：保存Blender验收并经UE导入回读的多方向瞄准姿态；只读数据资产，不拥有角色/网络状态。依赖同一Manny骨架。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoAuthoredAimProfile.generated.h"

class UAnimSequence;

/** 一格瞄准姿态；LocalPose按Profile.BoneNames排列，父子平移为原骨长，缩放恒为1。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoAuthoredAimSample
{
    GENERATED_BODY()
    // UE回读的局部骨变换，厘米；仅导入器生成，运行时不修改共享资产。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pose") TArray<FTransform> LocalPose;
};

/** 单枪可配置的瞄准库：按Pitch行、Yaw列排列；ReferencePose来自未修改的TP持枪，用于保留Lyra移动补偿。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoAuthoredAimProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    // 可在编辑器预览的源姿态图集；第0帧原Hold，后续帧对应Samples；不能顺序播放作运动动画。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Source") TObjectPtr<UAnimSequence> PoseAtlas;
    // Blender源文件和SHA256；导入器写入，便于追溯和重烘。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Source") FString SourceRevision;
    // 递增角度轴，度；Yaws包含0并覆盖身体自由观察阈值。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grid") TArray<float> Yaws;
    // 递增俯仰角，度；包含相机的-60/+55端点。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grid") TArray<float> Pitches;
    // 局部数组的稳定骨名表；LOD在节点中重建映射，不能把Mesh索引当Skeleton索引。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grid") TArray<FName> BoneNames;
    // 原始Hold的局部骨变换，数组长度与BoneNames相同。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grid") TArray<FTransform> ReferencePose;
    // Pitch外循环、Yaw内循环；数组数必须等于两轴数量之积。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grid") TArray<FDemoAuthoredAimSample> Samples;
    // 原枪真实枪管单位向量，位于ik_hand_gun局部；来自UE真实机械Root/枪口回读。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Calibration") FVector BoreInAnchor=FVector::ForwardVector;
    // 原枪机匣上方向在同一锚点局部；与枪管一起约束完整朝向，不能只校准枪口方向。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Calibration") FVector UpInAnchor=FVector::UpVector;
    // 与第一人称一致的机匣侧倾，度；手枪1.5，长枪0。只在最终小角度校准使用。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Calibration", meta=(ClampMin="-10",ClampMax="10")) float CantDegrees=0.f;
};
