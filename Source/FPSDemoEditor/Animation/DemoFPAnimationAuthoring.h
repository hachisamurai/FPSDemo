// 职责：Editor生成FP主图/Linked Layers与单段Montage；输入为已验收资产，调用者负责备份和保存。
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/DemoFPAnimInstance.h"
#include "DemoFPAnimationAuthoring.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimMontage; // 返回包拥有的单段FPAction动画，不赋予玩法权限。

/** 编辑器专用第一人称动画建图；只维护 /Game/Weapons/Animations 的脚本拥有资产。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoFPAnimationAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** ArmsMesh为模板手臂；FallbackIdle须使用相同Skeleton。创建真实ALI、主图、公共层和四个配置子类，调用方负责保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Animation")
    static bool Build(USkeletalMesh* ArmsMesh, UAnimSequence* FallbackIdle);

    /** WeaponName仅允许Pistol/Rifle/Shotgun/Sniper；AnimSet为已导入资源集合，只写指定子AnimBP的默认配置并重新编译。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Animation")
    static bool ConfigureLayer(const FString& WeaponName, const FDemoWeaponAnimationSet& AnimSet);
    /** PackagePath限定/Game/FPSPackAssets/Integration/；Sequence须已验收。只封装FPAction单段，不烘焙或修改任何骨骼轨道。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Animation")
    static UAnimMontage* CreateSequenceMontage(const FString& PackagePath, UAnimSequence* Sequence);
};
