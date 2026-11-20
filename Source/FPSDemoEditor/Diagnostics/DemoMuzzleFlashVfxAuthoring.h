// 职责：离线创建/审查独立程序化枪口短闪材质与Cascade模板；Python先创建新资产，再显式调用本桥。
// 依赖：UE5.4.4 Engine/MaterialEditor原生接口；不保存包、不启动World、不依赖NetworkPredictionExtras素材或运行模块。
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoMuzzleFlashVfxAuthoring.generated.h"

class UMaterial;
class UParticleSystem;

/** 仅Editor作者入口；产物是普通Surface材质/CPU Cascade，不包含玩法、伤害、RPC或存档状态。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoMuzzleFlashVfxAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Material必须是调用者新建且空图的独占材质；创建5节点软星芒/粒子寿命淡出，Additive/Unlit/正常深度；失败保部分图，不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Muzzle Flash")
    static bool ConfigureFlashMaterial(UMaterial* Material);

    /** System必须是调用者新建且无Emitter的独占模板，Material须符合本图合同；创建1粒/1次/0.04秒/5cm本地空间短闪，不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Muzzle Flash")
    static bool ConfigureFlashSystem(UParticleSystem* System,UMaterial* Material);

    /** 作者及新进程只读核实际模板/材质；ReportJson输出简短字段和拒绝原因，返回实际合同结果；不证明Socket附着、RHI或游戏触发。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Muzzle Flash")
    static bool AuditFlashSystem(UParticleSystem* System,UMaterial* Material,FString& ReportJson);
};
