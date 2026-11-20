#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoPlayerWeaponCorrectionAuthoring.generated.h"

class UAnimMontage;
class UAnimSequence;

/** Editor专用桥接：完整复制原Montage并只替换动画引用，保留原槽、分段、曲线、通知及混合参数。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoPlayerWeaponCorrectionAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Source为只读原Montage；Replacement同骨架同秒数且120Hz；PackagePath只能位于Player修正目录，调用者负责最终保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static UAnimMontage* CopyMontage(UAnimMontage* Source, UAnimSequence* Replacement, const FString& PackagePath);
    /** Montage为已完成PostLoad的只读资源；返回稳定JSON结构签名，排除唯一允许替换的Sequence引用，空串表示拒绝未就绪资源。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static FString GetMontageStructureSignature(const UAnimMontage* Montage);
    /** Montage为只读当前配置；仅完整、原速、单次的单槽单段返回其Sequence，供Python获取未公开的Slot结构。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Player")
    static UAnimSequence* GetFullMontageSequence(const UAnimMontage* Montage);
};
