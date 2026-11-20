// 职责：离线评估完整骨骼组件输出，供 Python 比较原始动画与 Manny 原生 PostProcess。
// 用法：创建临时 Probe 后调用 SampleSequence；依赖 Editor FPreviewScene，不写入任何资产或进入运行时。
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoAnimationPoseProbe.generated.h"

class USkeletalMesh;
class UAnimSequence;

/** 同步、只读的动画诊断对象；采样组件仅存活于一次调用，不保留场景或输入资产。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoAnimationPoseProbe : public UObject
{
    GENERATED_BODY()
public:
    /**
     * Editor 游戏线程调用；Mesh/Sequence 为借用的同 Skeleton 资产，Time 为序列内秒数。
     * bPostProcess 决定是否执行 Mesh 自带后处理；不更改 Mesh 设置。返回 LOD0 组件空间厘米变换，失败返回空表。
     * 仅评估单段动画及其后处理，不声称覆盖游戏 AnimBP 的混合、网络或 IK。
     */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Animation Diagnostics")
    TMap<FName, FTransform> SampleSequence(USkeletalMesh* Mesh, UAnimSequence* Sequence, float Time, bool bPostProcess);
    /** 游戏线程只读诊断：借用同Skeleton网格/序列，Time为源秒，Stride为1..1024的LOD0顶点采样步长。
     * 返回实际线性蒙皮后的组件空间厘米点；不经过材质/WPO，不保存资产，失败返回空数组。
     * 用于区分骨骼正确但渲染几何错误与材质/可见性问题，不等同完整RHI验收。
     */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Animation Diagnostics")
    TArray<FVector> SampleSkinnedPositions(USkeletalMesh* Mesh, UAnimSequence* Sequence, float Time, int32 Stride);
    /** Editor游戏线程只读导出本项目原SKM_Manny的实际renderLOD0：位置、三角索引、section BoneMap、
     * 全部有效原始uint16权重及引擎InvMaxRawBoneWeightFloat、参考组件姿态和真实RefBasesInvMatrix。
     * OutputFile只能位于Saved/MeleeElbowClearance20261108且为新JSON文件；不重归一化、不改资产/运行蒙皮。
     * 用于校准本轮独立RuntimeCache，不能覆盖Blender/source权重；成功返回true，任何缺失/越界返回false。
     */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Animation Diagnostics")
    bool ExportMannyMeleeRenderLOD0(USkeletalMesh* Mesh, const FString& OutputFile);
};
