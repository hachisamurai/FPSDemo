// 职责：R05 120Hz/R07 240Hz长枪离线候选采样桥接；由已核源门的Python导入器调用，不保存或激活正式资产。
// 依赖UE5.4 AnimDataModel/Controller及Manny骨架；不修改骨/蒙皮/Gameplay时钟，不进入游戏模块。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoLonggunElbowAuthoring.generated.h"

class UAnimSequence;

/** 只为本批固定候选同步DataModel/平台率，R07提高密度处理实测子帧误差，禁止操作正式序列。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoLonggunElbowAuthoring : public UObject
{
    GENERATED_BODY()
public:
    /** 导入器bracket中调用；同步固定候选族120/240Hz、禁用抽帧，返回实际平台Hz或失败0。片长/轨道由调用方独立验证。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Longgun Elbow")
    static int32 PrepareSampling(UAnimSequence* Sequence);
    /** Candidate为当前正式Source的隔离克隆，TracksJson为已审三左臂固定族采样Q；直接原模型取键密采，保留片长/T/S/未编辑原整数键。返回回读JSON，失败空串，不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Longgun Elbow")
    static FString BakeReviewedKeys(UAnimSequence* Candidate,UAnimSequence* Source,const FString& TracksJson);
};
