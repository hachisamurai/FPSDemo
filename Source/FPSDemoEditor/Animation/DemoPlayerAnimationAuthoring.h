// 职责：Editor内生成/增量维护可编辑玩家AnimBP及动作层；Python调用后自行保存目标包，不修改共享Skeleton。
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoPlayerAnimationAuthoring.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimBlueprint;

/** 只在Editor生成玩家薄AnimBP与独立TP动作，不修改原FP素材/共享Skeleton参考骨。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoPlayerAnimationAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Body/Arms是已导入同Skeleton的真实网格；Clips须包含九个固定移动键，所有资源写入/Game/Player专属目录。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static bool Build(USkeletalMesh* Body, USkeletalMesh* Arms, const TMap<FName,UAnimSequence*>& Clips);
    /** Body须为当前Appearance的原骨架全身；仅重烘四个既有手枪TP序列，不保存包或修改FP/其他枪/DA配置。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static bool RebuildPistolSingleHand(USkeletalMesh* Body);
    /** 仅按当前FP配置重烘四枪TP持枪/射击/换弹/拉栓序列；不重建AnimBP、DA或共享Skeleton，调用者白名单保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static bool RebuildFirearmPoses(USkeletalMesh* Body);
    /** 基于当前TP手枪握姿制作左臂圆弧，Run只提供周期；增量接入既有图，仅修改新序列、全身BP与DA，调用方负责保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static bool RebuildPistolRunArm();
    /** 只在已有全身AnimBP最终地面节点后安装/更新瞄准层，不重烘动画/骨架/外观；调用方保存唯一BP包。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static bool RebuildAimLayer();
    /** 在已有移动/上身图与CS控制前插入可见的StationaryMeleeFullBody混合节点；复用同一动作Pose，不重建其余图或写默认武器配置。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static bool RebuildMeleeFullBodyLayer();
    /** Body为新模型；只更新现有Appearance和全身图的真实鞋底样本/预览，不重烘或覆盖已在Blender制作的动画；调用者备份保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static bool RefreshModelGrounding(USkeletalMesh* Body);
    /** 为原版Manny配置已复制的专用图及正式Appearance；Body/Blueprint/Clips由同步Python调用提供，九动作固定顺序，不重烘武器动作或修改绑定骨。调用者备份并保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static bool ConfigureMannequin(USkeletalMesh* Body, UAnimBlueprint* Blueprint, const TArray<UAnimSequence*>& Clips);
    /** Model仅限Pistol/Rifle/Shotgun/Sniper；Mesh及DefaultClip是已验收实际枪资产，重建原TP机械薄图，调用者保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static UClass* BuildWeaponMechanicalGraph(const FString& Model, USkeletalMesh* Mesh, UAnimSequence* DefaultClip);
};
