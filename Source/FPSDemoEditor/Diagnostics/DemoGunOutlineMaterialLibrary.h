// 职责：提供纯Editor枪墙材质作者桥：保留历史WPO隐藏引脚，新增整体Stencil外膨胀后处理图的创建/审查。
// 使用：对应outline/silhouette作者脚本调用；只修改传入的新材质图，不保存包或改全局设置，Game不编译本模块。
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoGunOutlineMaterialLibrary.generated.h"

class UMaterial;
class UMaterialExpression;

/** Python无法访问带Hidden元数据的MP_WorldPositionOffset；此Editor桥复用原生公开材质接口。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoGunOutlineMaterialLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** 作者建图时将Expression输出0连到同一Material的WPO；无效/跨材质节点拒绝，返回实际引擎连接结果，不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Gun Outline")
    static bool ConnectWorldPositionOffset(UMaterial* Material,UMaterialExpression* Expression);
    /** 作者/新进程读回时返回材质真实WPO输入；空材质/未连接返回空，不以节点名猜测连线。 */
    UFUNCTION(BlueprintPure,Category="Demo|Editor|Gun Outline")
    static UMaterialExpression* GetWorldPositionOffsetInput(UMaterial* Material);
    /** 作者创建九节点AfterTonemap整体mask或精确升级已知旧8/9节点；253/2px/0.1cm不变，主深度对齐非抖动CustomDepth。未知图拒绝；不保存。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Gun Outline")
    static bool ConfigureSilhouetteMaterial(UMaterial* Material);
    /** 作者及新进程只读核材质域/位置、未启用硬件StencilTest、Custom代码与全部真实依赖连线；只证明图合同，不替代RHI或游戏验收。 */
    UFUNCTION(BlueprintPure,Category="Demo|Editor|Gun Outline")
    static bool AuditSilhouetteMaterial(UMaterial* Material);
    /** 返回本版HLSL供Python核SHA；要求启动CustomDepthTemporalAAJitter=0，主SceneDepth换域对齐，保中心原色/双遮挡/纯白外沿。 */
    UFUNCTION(BlueprintPure,Category="Demo|Editor|Gun Outline")
    static FString GetSilhouetteShaderCode();
};
