// 职责：离线创建共享腕部PostProcess AnimBP并绑定到同骨架网格；依赖AnimGraph模块，调用者负责备份与保存。
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoPlayerWristAuthoring.generated.h"
class USkeletalMesh;
class UAnimBlueprint;
class UAnimSequence;

/** 仅Editor资产制作桥接；不替换角色主动画实例或GAS Montage链。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoPlayerWristAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Mesh提供目标Skeleton；bOriginalMannequinTwist仅用于获批FourView新蒙皮，生成独立图，不污染旧腕桥图；调用者保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Player")
    static UAnimBlueprint* BuildGraph(USkeletalMesh* Mesh, bool bOriginalMannequinTwist=false);
    /** Mesh与Blueprint须同骨架且已编译；只设置PostProcess类，拒绝覆盖其他后处理类，不写盘。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Player")
    static bool Assign(USkeletalMesh* Mesh,UAnimBlueprint* Blueprint);
    /** Mesh须已指定新roll后处理，Sequence为已烘焙的HumanRig关节测试；在临时预览World逐帧比较前后，不保存资产。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Player")
    static bool ValidateModelMatched(USkeletalMesh* Mesh,UAnimSequence* Sequence);
};
