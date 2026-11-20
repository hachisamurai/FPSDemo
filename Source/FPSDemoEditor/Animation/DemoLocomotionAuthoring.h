// 职责：离线创建移动BlendSpace并增量接入已有动画图；只接受LocomotionV2候选，调用者负责备份、验收和保存。
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoLocomotionAuthoring.generated.h"
class UAnimSequence;
class UBlendSpace;
class UAnimBlueprint;

UCLASS()
class FPSDEMOEDITOR_API UDemoLocomotionAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** PackagePath限定新移动目录；Clips与Coordinates等长，坐标X右/Y前cm/s；返回包拥有的可编辑BlendSpace，不保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Movement")
    static UBlendSpace* BuildSpace(const FString& PackagePath,const TArray<UAnimSequence*>& Clips,const TArray<FVector>& Coordinates);
    /** Body为当前Manny图，FirstPerson为固定主图；增量插入移动层，保留武器Slot、近战和瞄准；调用者备份并保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Movement")
    static bool InstallGraphs(UAnimBlueprint* Body,UAnimBlueprint* FirstPerson,UBlendSpace* DefaultSpace,UAnimSequence* DefaultFP);
    /** Body为已安装地面移动的图；DefaultMotion是安全完整姿态，LegacyClips固定九状态且必须非增量。接入完整空中/闪避与中性枪管标定，调用者备份后保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Movement")
    static bool InstallLyraMotion(UAnimBlueprint* Body,UAnimSequence* DefaultMotion,const TArray<UAnimSequence*>& LegacyClips);
};
