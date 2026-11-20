// 职责：隔离TP近战候选Montage/Slot建图及骨姿回读；仅对白名单RAW序列同步120/240Hz采样。
// 用法：Tools/Unreal/import_tp_bat_rebuild.py、import_kevin_2h_manny_r02.py、import_blade_posture_r04.py、import_blade_torso_r05.py；依赖Engine/AnimGraph/UnrealEd，不进入运行时包体。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoMeleeAnimationAuthoring.generated.h"

class UAnimSequence;
class UAnimMontage;
class UAnimBlueprint;
class USkeletalMesh;

UCLASS()
class FPSDEMOEDITOR_API UDemoMeleeAnimationAuthoring : public UObject
{
    GENERATED_BODY()
public:
    /** 为同Skeleton的完整候选Sequence创建DefaultSlot单段Montage，原速/无裁剪；仅新白名单包，调用者负责保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Melee")
    static UAnimMontage* CreateFullBodyMontage(const FString& PackagePath, UAnimSequence* Sequence);

    /** 仅恢复本工具初次生成的“空DefaultSlot+有效DefaultSlot”候选；不改片段或其他包，调用者先校验备份。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Melee")
    static bool RepairDuplicatePreviewSlot(UAnimMontage* Montage);

    /** 完成DataModel写入通知，令压缩采样同步实际120/240Hz源并禁止抽帧，返回运行fps或拒绝时0。
     * 仅游戏线程接受原TPRebuild20261030、Kevin2H_MannyR02/Manny、MannyBladePostureR04/Manny目录及R02唯一Probe/A_Previous120HzCompatibility精确对象；
     * R05仅额外接受MannyBladeTorsoR05/Manny下A_BladeTorsoR05_Idle、A_BladeTorsoR05_Attack两个精确包，不放开该目录其他资产或正式资源；
     * R06仅额外接受MannyBladeTorsoR06/Manny下A_BladeTorsoR06_Idle、A_BladeTorsoR06_Attack两个精确包，供独立尾段回收修订，不放开整个目录或正式资产；
     * R07仅额外接受MannyBladeClearanceR07/Manny下A_BladeClearanceR07_Idle、A_BladeClearanceR07_Attack两个精确包，供本体避让修订，正式动画仍不允许；
     * 不放开其他Probe/候选根目录，不授权创建Montage/图、不改共享Skeleton；调用者负责保存本Sequence。
     */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Melee")
    static int32 FinalizeCandidateTracks(UAnimSequence* Sequence);

    /** 创建Idle→DefaultSlot→Output的专用全身预览图；不修改正式主图或Skeleton，不包含运行时IK。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Melee")
    static UAnimBlueprint* CreatePreviewGraph(const FString& PackagePath, USkeletalMesh* Mesh, UAnimSequence* Idle);

    /** Editor游戏线程隔离求值实际Montage/Slot及可选原生PP；Time秒，返回组件空间独立副本，不触发游戏/物理。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Melee")
    TMap<FName,FTransform> SampleMontage(USkeletalMesh* Mesh, UAnimBlueprint* Blueprint, UAnimMontage* Montage, float Time, bool bPostProcess);
};
