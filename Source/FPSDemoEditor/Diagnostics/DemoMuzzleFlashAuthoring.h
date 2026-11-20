// 职责：四枪MuzzleFlash mesh-only插槽的Editor制作桥及只读真实RenderLOD0导出，不进入游戏模块。
// 使用：Python先ExportWeaponReference标定真实枪管出口，再SetMeshMuzzleFlashSocket；资产保存由调用方明确执行。
// 依赖UE5.4.4 SkeletalMesh/AddSocket(false)及实际LOD0缓冲；不改物理Muzzle、Skeleton、参考姿态、权重或动画。
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoMuzzleFlashAuthoring.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimSequence;
class UParticleSystemComponent;

/** 仅四个当前正式Config.Mesh允许进入；几何/Socket事实是制作输入，不能代替实际动画或RHI验收。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoMuzzleFlashAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Editor游戏线程只读导出Mesh实际renderLOD0：组件参考厘米位置、原三角索引、section/material槽、
     * 原始uint16权重/section骨映射、参考骨组件矩阵、逆绑定矩阵和mesh-only/Skeleton各自Socket定义。
     * SavedOutputJSON须为Saved/MuzzleFlashSockets20261117/下尚不存在的.json；不保存或重建输入资产。
     * 不按AABB/旧Muzzle自动选出口，不声称含动画、材质WPO或最终RHI蒙皮。成功完整落盘返回true。
     */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Muzzle Flash")
    static bool ExportWeaponReference(USkeletalMesh* Mesh,const FString& SavedOutputJSON);

    /** Editor游戏线程仅创建/更新本Mesh自己拥有的固定名MuzzleFlash。
     * BoneName为该Mesh实际父骨；ComponentTransform是参考姿态组件空间厘米/旋转/正缩放，反求骨局部值。
     * 新Socket Outer=Mesh且AddSocket(false)，既有同名只查mesh-only；不改旧Muzzle或Skeleton。
     * 只标脏、不保存，调用方负责备份/保存/新进程读回；不自动推断枪管出口或骨名。非法输入返回false。
     */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Muzzle Flash")
    static bool SetMeshMuzzleFlashSocket(USkeletalMesh* Mesh,FName BoneName,const FTransform& ComponentTransform);

    /** Editor游戏线程返回当前mesh-only和Skeleton全部Socket事实JSON及各自参考组件变换；失败返回空串。
     * 仅在内存构造返回值，不写文件/保存资产，不把参考姿态变换称为实际游戏中的Socket位置。
     */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Muzzle Flash")
    static FString ReadSocketFacts(USkeletalMesh* Mesh);

    /** 只允许Editor World中调用方自建的Transient Actor组件；Source须用白名单Mesh，Flash实际挂其MuzzleFlash。
     * 接管该Transient PSC的自动Tick并保留手动模式，显式同步步进；调用方负责后续采样/销毁，不代表普通World调度验收。
     * Sequence为同Skeleton真实机械动画，PoseTime为素材秒；不触发Notify地显式采样并刷新骨/子变换。
     * Step为0..0.25秒本次PSC显式推进量；返回真实Socket/PSC世界TRS、附着误差、活跃粒子数JSON。
     * 无BeginPlay/World Tick/伤害/保存；本最小接口不导出CPU粒子位置，不把组件重合冒充全部粒子或RHI验收。
     */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Muzzle Flash")
    static FString SampleFlashFrame(USkeletalMeshComponent* Source,UAnimSequence* Sequence,float PoseTime,UParticleSystemComponent* Flash,float Step);
};
