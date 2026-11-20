// 职责：UE5.4离线枪械后坐候选的120Hz压缩配置、Mesh Space增量、Montage和惰性Slot接线。
// 使用：Tools/Unreal/import_fire_recoil_r01.py；所有修改均同步Editor游戏线程，调用者负责门禁、备份及精确保存。
// 依赖现有Manny/主图身份；不改参考骨架、骨长、蒙皮或运行时玩法，不创建Lyra插件依赖。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoFireRecoilAuthoring.generated.h"

class UAnimSequence;
class UAnimMontage;
class UAnimBlueprint;
class USkeleton;

/** 精确白名单的Editor作者桥接；源码存在或函数返回成功不等于候选已通过Blender/游戏验收。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoFireRecoilAuthoring : public UObject
{
    GENERATED_BODY()
public:
    /** 在controller已设120Hz且Bracket仍打开时同步目标采样率；必须先于短时长/keys写入和close，不触发PostEditChange/压缩或保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Fire Recoil")
    static int32 PrepareRecoilSampling(UAnimSequence* Sequence);

    /** 配置新序列的120Hz采样及增量基准；bAdditive=false为绝对Hold，true需同枪当前正式Hold0。仅候选/本修订新正式序列，不保存资产。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Fire Recoil")
    static int32 FinalizeRecoilSequence(UAnimSequence* Sequence,UAnimSequence* ReferenceHold,bool bAdditive);

    /** 创建或更新本修订完整单段DemoGunRecoil Montage；Mesh Space源必须已有明确Hold0基准，1x无裁剪，无RootMotion。调用者保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Fire Recoil")
    static UAnimMontage* CreateRecoilMontage(const FString& PackagePath,UAnimSequence* Sequence);

    /** 仅在实际SK_Mannequin新增DemoGunRecoil→DemoGunRecoilGroup元数据；禁止覆盖已存在的其他组，不触碰骨/参考。需调用者先备份并显式保存Skeleton。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Fire Recoil")
    static bool RegisterRecoilSlot(USkeleton* Skeleton);

    /** 在正式同身份主图Grounding→Aim之间插入CS转Local→惰性Slot→CS，未播放时透传；不改状态机/CDO/引用。调用者先备份、门禁后保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Fire Recoil")
    static bool InstallRecoilSlot(UAnimBlueprint* Blueprint);

    /** 只读返回真实序列/参考/采样率/Montage结构、Slot组JSON；用于新进程验证，不执行或保存任何动画。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Fire Recoil")
    static FString InspectRecoilAssets(UAnimSequence* Sequence,UAnimMontage* Montage);

    /** 只读完整骨名/父链/RefTQS、预览Mesh、兼容列表与Slot组JSON；注册前后调用，仅允许实际共享Manny Skeleton。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Fire Recoil")
    static FString InspectSkeletonContract(USkeleton* Skeleton);

    /** 只读主图所有节点GUID/类型/引脚默认值与连接，以及父类/Skeleton身份；用于限定仅新三节点和原两端Pose连线发生变化。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Fire Recoil")
    static FString InspectRecoilGraph(UAnimBlueprint* Blueprint);
};
