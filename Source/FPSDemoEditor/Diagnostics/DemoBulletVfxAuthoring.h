// 职责：给BulletVFX20261119的Python作者提供原生Cascade模板桥；不保存包、不启动Game World。
// 依赖：UE5.4.4 Engine/MaterialEditor与项目入口日志；仅Editor模块，不增加运行时依赖。
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoBulletVfxAuthoring.generated.h"
class UMaterial;
class UParticleSystem;
/** 纯表现CPU短闪模板作者；材质图与包保护由外层脚本管理。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoBulletVfxAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** System为调用者独占新建空模板，Material为程序化深度测试材质；创建150ms/18cm/单粒沿Actor本地+X以60cm/s运动的速度对齐模板，不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Bullet VFX")
    static bool ConfigureContactSystem(UParticleSystem* System,UMaterial* Material);
    /** 作者/新进程读回检查真实Emitter/分布/材质基本边界，输出JSON；不证明实际游戏或RHI观感。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Bullet VFX")
    static bool AuditContactSystem(UParticleSystem* System,UMaterial* Material,FString& ReportJson);
};
