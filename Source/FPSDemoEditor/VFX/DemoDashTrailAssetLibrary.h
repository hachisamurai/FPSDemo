// 职责：补齐Python未暴露的Niagara User参数默认值/编译接口。使用：离线建图脚本调用，不进入游戏包；依赖Niagara。
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoDashTrailAssetLibrary.generated.h"

class UNiagaraSystem;
class UMaterialInterface;
class USkeletalMesh;

/** 编辑器同步制作桥接；只操作脚本明确传入的系统，不遍历或覆盖其他资产。 */
UCLASS()
class UDemoDashTrailAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** System为当前新建系统；建图前注册四个User参数，默认青蓝/8cm/.22秒/60个每秒，保证预览和组件类型一致。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|VFX")
    static void SetDashDefaults(UNiagaraSystem* System);
    /** System为已完成建图的资产；同步完成Niagara编译，返回图是否有效；失败时脚本必须拒绝保存，不要求NullRHI进程能够渲染。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|VFX")
    static bool CompileDashSystem(UNiagaraSystem* System);
    /** System为已复制Lyra候选，Material为独立流线材质；PreviewMesh可空以去除全部默认网格引用，运行时必须提供实际Body组件。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|VFX")
    static bool AdaptLyraDash(UNiagaraSystem* System, UMaterialInterface* Material, USkeletalMesh* PreviewMesh);
};
