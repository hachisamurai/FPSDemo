// 职责：仅编辑器迁入Lyra图的临时父类和检查桥接；正式图最终使用运行模块DemoLyraAnimInstance。
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GameplayEffectTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoLyraAuthoring.generated.h"
class UAnimBlueprint;
class USkeleton;

/** 原始包首次读取时保留原生属性，避免丢失CDO；不得作为正式角色AnimClass。 */
UCLASS(Transient)
class UDemoLyraImportInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    // 与原始原生字段同名，仅供载入后重设父类；正式图不依赖此Editor类。
    UPROPERTY(EditDefaultsOnly) FGameplayTagBlueprintPropertyMap GameplayTagPropertyMap;
    // 原始图字段，cm；迁移后由运行适配实例计算。
    UPROPERTY(BlueprintReadOnly) float GroundDistance=0.f;
};

/** 白名单编辑器导入接口；调用者负责新目录、备份、保存和运行验收。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoLyraAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** 仅当前导入进程注册Lyra旧父类到临时父类的重定向；必须在加载原包之前调用。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static void PrepareImport();
    /** 只修改迁入的原始主图，重命名冲突变量并接Demo父类；返回实际编译结果，不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static bool AdaptBase(UAnimBlueprint* Blueprint);
    /** Imported为新Lyra候选Skeleton，Current为正式Manny；按骨名核对参考姿态，只向Imported登记兼容，不改Current。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static bool ValidateSkeletonAndDeclareCompatibility(USkeleton* Imported,USkeleton* Current);
    /** Native为已适配原生主图，Current为只读V4模板；复制现有武器/瞄准末端，移动输入由Lyra原生图供给，不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static bool InstallWeaponOverlay(UAnimBlueprint* Native,UAnimBlueprint* Current);
    /** 清除原Lyra/临时导入父类的AnimGetter引用并重新编译；仅迁入库与候选层，保存由调用者负责。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static bool FinalizeImportedGraph(UAnimBlueprint* Blueprint);
    /** 仅独立LyraReady候选父图：把Demo持续举枪意图接入原生HipFire覆盖条件，保留原权重/Mask；编译但不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static bool ConnectWeaponReadyPose(UAnimBlueprint* Blueprint);
    /** 仅Ready_ItemBase：原CanPlayIdleBreak结果与主图资格AND；同时复用原进入/提前退出逻辑，真实编译但不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static bool ConnectNativeIdleBreakGate(UAnimBlueprint* Blueprint);
    /** 仅Ready三家族/四枪已知派生：父图接口改后真实编译并返回Status，不改引用/默认值，不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static bool CompileNativeIdleBreakDependent(UAnimBlueprint* Blueprint);
    /** 当前正式Lyra主图增量插入专用近战Slot，位于WeaponAnchor前/上身分层内；验证root/pelvis不被分层覆盖，只编译不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Lyra") static bool InstallMeleeUpperBodySlot(UAnimBlueprint* Blueprint);
};
