// 职责：Editor资产导入/插槽配置桥接；Python调用，依赖Engine原生骨架和渲染数据，不在游戏运行时写资产。
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoSkeletalAssetLibrary.generated.h"

class USkeletalMesh;

/** 仅编辑器使用的骨骼资产桥接；UE5.4 Python 不开放 Skeleton.Sockets 写入，使用原生资产接口创建。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoSkeletalAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /**
     * 导入脚本在网格构建完成后调用。Mesh 为借用资产，SocketName/BoneName 为稳定挂点/父骨名；
     * ComponentTransform 是参考姿势下模型空间变换（厘米/度，正缩放）。同名更新，无效输入返回 false。
     * 修改由 Skeleton 持有，调用者负责保存；不创建游戏 Actor、不改变碰撞或动画播放。
     */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Skeletal Assets")
    static bool SetSkeletonSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, const FTransform& ComponentTransform);
    /**
     * Editor/Python配置武器时调用；Mesh借用当前实际使用的网格，BoneRelativeTransform是父骨局部厘米/度/正缩放。
     * PreviewAsset可空，仅供动画编辑器预览；创建/更新Skeleton插槽，不改骨骼参考姿势、动画或蒙皮，调用方负责保存。
     */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Skeletal Assets")
    static bool SetSkeletonSocketLocal(USkeletalMesh* Mesh, FName SocketName, FName BoneName,
        const FTransform& BoneRelativeTransform, UObject* PreviewAsset);
    /** Mesh 为借用的已构建资产，ExpectedWeightedBones 为实际带几何的骨数；检查 LOD0 每点恰有一个有效权重及骨索引。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Skeletal Assets")
    static bool ValidateRigidSkin(USkeletalMesh* Mesh, int32 ExpectedWeightedBones);
};
