#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoFPSequenceAuthoring.generated.h"

/** 将机械轨迹及Blender左手接触轨迹烘焙成可编辑Sequence/Montage；游戏只播放，不读取作者JSON。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoFPSequenceAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Model为四枪之一；Arms/WeaponMesh和Clips只在同步制作借用；Grip输入为枪局部厘米坐标。
     * 左腕/转腕/15根指骨从Art/Player/Animations/left_hand_contact_tracks.json读取；缺失或帧数错误拒绝制作。
     * SupportGrip和MagazineGrip保留作者API兼容，当前左手实际轨迹以经过网格校验的接触文件为准。
     * RightElbowOffset为组件空间相对右肩的厘米极点；BoltReachClearance为右手拉栓过渡向外绕行厘米数。
     * Duration为玩法默认秒数，动作无循环；输出固定在/Game/Weapons/Animations，调用者负责保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|FP Animation")
    static bool Build(const FString& Model, class USkeletalMesh* Arms, class USkeletalMesh* WeaponMesh,
        const TMap<FName,class UAnimSequence*>& Clips, FVector SupportGrip, FVector MagazineGrip, FVector BoltGrip,
        FVector RightGrip, FVector RightElbowOffset, float BoltReachClearance, float Duration);
};
