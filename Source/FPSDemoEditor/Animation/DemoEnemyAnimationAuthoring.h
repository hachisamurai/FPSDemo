#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoEnemyAnimationAuthoring.generated.h"

/** 编辑器专用可重复建图入口；只修改 /Game/Enemies/Breach/Animation 的脚本拥有资产。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoEnemyAnimationAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Model限定Chaser/Warden；Mesh与Clips均为已导入同骨架资产，创建薄AnimBP/Montage/DA，调用方保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Animation")
    static bool Build(const FString& Model,class USkeletalMesh* Mesh,const TMap<FName,class UAnimSequence*>& Clips);
};
