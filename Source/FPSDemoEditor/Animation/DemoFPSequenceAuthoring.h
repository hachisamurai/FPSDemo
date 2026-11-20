// 职责：Editor FP动作烘焙接口；由Python导入器提供已校验接触数据，调用者负责保存资产，运行包不包含作者逻辑。
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
    /** 从现有狙击Idle/Fire/Empty的已校准接触轨迹制作独立逐发动作；Arms/Gun为借用网格，RightElbowOffset为原标定厘米极点。
     * 仅复用Empty末段右手/bolt，左手保持Idle支撑，弹匣保持参考姿势；输出1秒归一化素材，由运行时60/RPM变速。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|FP Animation")
    static bool BuildSniperBoltCycle(class USkeletalMesh* Arms, class USkeletalMesh* Gun, FVector RightElbowOffset);
    /** Model为四枪之一；Arms/WeaponMesh和Clips只在同步制作借用；Grip输入为枪局部厘米坐标。
     * 左腕/转腕/15根指骨从Art/Player/Animations/left_hand_contact_tracks.json读取；缺失或帧数错误拒绝制作。
     * SupportGrip和MagazineGrip保留作者API兼容，当前左手实际轨迹以经过网格校验的接触文件为准。
     * RightElbowOffset为组件空间相对右肩的厘米极点；BoltReachClearance为右手拉栓过渡向外绕行厘米数。
     * AnchorOffset为模板GripPoint到枪锚点的组件空间厘米偏移，只移枪并重解双臂，不平移肩根。
     * 右腕/朝向/15根指骨从right_hand_grip_tracks.json读取；RightGrip保留旧入口并由导入器同步传入。
     * reload_contact只允许同腕/掌旋转的避让手型，换弹暂离扳机；缺失或腕姿不一致拒绝制作。
     * Duration为玩法默认秒数，动作无循环；输出固定在/Game/Weapons/Animations，调用者负责保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|FP Animation")
    static bool Build(const FString& Model, class USkeletalMesh* Arms, class USkeletalMesh* WeaponMesh,
        const TMap<FName,class UAnimSequence*>& Clips, FVector SupportGrip, FVector MagazineGrip, FVector BoltGrip,
        FVector RightGrip, FVector RightElbowOffset, float BoltReachClearance, float Duration, FVector AnchorOffset);
};
