// 职责：近战专属AnimBP建图及真实导入动画轨迹烘焙。使用：import_baseball_bat.py调用，Editor模块依赖AnimGraph/AnimPose，禁止运行时写资产。
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoMeleeAuthoring.generated.h"
class USkeletalMesh;
class UAnimSequence;
class UDemoMeleeDefinition;
class UAimOffsetBlendSpace1D;
UCLASS()
class FPSDEMOEDITOR_API UDemoMeleeAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** 新建隔离FP惯性收招图：Idle+相对Look与原攻击分支通过标准Inertialization交接，调用者保存并显式选用。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Melee") static UClass* BuildFirstPersonRecoveryGraph(const FString& PackagePath,USkeletalMesh* Arms,UAnimSequence* Idle,UAimOffsetBlendSpace1D* Offset);
    /** 新候选目录内创建标准1D MeshSpace瞄准资源；Clips按下/中/上排列且已设置同一Idle基底，角度为当前相机范围，调用者负责保存。已有包拒覆盖。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Melee") static UAimOffsetBlendSpace1D* CreateFirstPersonLookOffset(const FString& PackagePath,const TArray<UAnimSequence*>& Clips,float DownDegrees,float UpDegrees);
    /** 新候选主图：保留原Pose双路过渡后叠标准AimOffset；LookOffset/LookPitch/LookWeight仅读原生GT缓存，不修改任何旧图/通知/服务器扫掠。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Melee") static UClass* BuildFirstPersonLookGraph(const FString& PackagePath,USkeletalMesh* Arms,UAnimSequence* Idle,UAimOffsetBlendSpace1D* Offset);
    /** Arms是现有同Skeleton的FP网格，Idle提供图默认姿态；仅写/Game/Weapons/Melee/Animations内三份专属图。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Melee") static bool BuildGraphs(USkeletalMesh* Arms,UAnimSequence* Idle);
    /** 只新建隔离SOMA候选主图，直接复用原Pose过渡节点；同骨架，不修改正式图或Skeleton，返回编译通过的Class，调用者保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Melee") static UClass* BuildSOMAFirstPersonGraph(USkeletalMesh* Arms,UAnimSequence* Idle);
    /** Definition是新近战DA；Origin/Axis为TP握姿棍原点/单位轴（UE组件空间），支持真实手骨/骨架Socket，按120Hz烘焙所有攻击。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Melee") static bool BakeCollision(UDemoMeleeDefinition* Definition,FVector Origin,FVector Axis);
    /** Definition为设计师当前配置；保留GripOffset，从现有TP动作与HandSocket骨/插槽重烘轨迹；调握点后EditorUtility/Python调用，不改动画。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Melee") static bool RebakeConfiguredCollision(UDemoMeleeDefinition* Definition);
    /** 按实际Appearance身体相对胶囊变换重烘；保留握点。上身Montage用中立Root/骨盆+作者对DA所选Ref/Idle的脊柱动态重建，
      * 不带入作者全身蹲伏/根位移；步态造成的小幅差仍需实际移动对照。BodyTransform单位cm，不写运行时骨骼或动画。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Melee") static bool RebakeConfiguredCollisionWithBody(UDemoMeleeDefinition* Definition,const FTransform& BodyTransform);
};
