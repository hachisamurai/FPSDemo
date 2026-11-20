// 职责：保留历史WPO隐藏枚举桥，并为新版整体Stencil外轮廓创建/核验真实后处理材质图。
// 依赖UE5.4.4 MaterialEditor与MaterialTemplate公开HLSL约定；无游戏/包保存/全局CVar修改，不手写uasset。
#include "Diagnostics/DemoGunOutlineMaterialLibrary.h"
#include "Materials/Material.h"
#include "MaterialDomain.h" // UE5.4 Material.h仅前置声明EMaterialDomain，作者使用真实MD_PostProcess定义。
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Engine/BlendableInterface.h" // UE5.4的EBlendableLocation实际定义在此；没有单独的BlendableLocation.h。
#include "MaterialEditingLibrary.h"
#include "Debug/DemoStaticLog.h"

namespace
{
    /** 当前功能覆盖完整不透明玩家视图；显式保Alpha=1，防PP默认Alpha0导致SceneCapture导出透明黑预览。 */
    bool ConnectSilhouetteOpaqueAlpha(UMaterial* Material)
    {
        DEMO_LOG_STATIC_CALL(Material);
        UMaterialExpressionConstant* Alpha=Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionConstant::StaticClass(),-300,500)); // 同材质拥有的唯一不透明输出，不改变RGB或白边选择。
        if (!Alpha) { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_ALPHA_REJECT node_factory")); return false; }
        Alpha->R=1.f; Material->BlendableOutputAlpha=true;
        return UMaterialEditingLibrary::ConnectMaterialProperty(Alpha,TEXT(""),MP_Opacity);
    }
}

bool UDemoGunOutlineMaterialLibrary::ConnectWorldPositionOffset(UMaterial* Material,UMaterialExpression* Expression)
{
    DEMO_LOG_STATIC_CALL(Material);
    if (!IsValid(Material)||!IsValid(Expression)||Expression->GetTypedOuter<UMaterial>()!=Material)
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_OUTLINE_WPO_REJECT invalid/cross_material")); return false; }
    return UMaterialEditingLibrary::ConnectMaterialProperty(Expression,TEXT(""),MP_WorldPositionOffset);
}

UMaterialExpression* UDemoGunOutlineMaterialLibrary::GetWorldPositionOffsetInput(UMaterial* Material)
{
    DEMO_LOG_STATIC_CALL(Material);
    if (!IsValid(Material)) { UE_LOG(LogFPSDemo,Log,TEXT("GUN_OUTLINE_WPO_READ_REJECT missing_material")); return nullptr; }
    return UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_WorldPositionOffset);
}

/** 保留A03实际代码作精确升级白名单；不把任意非空Custom图当本工具所有，不保存旧版本或放宽遮挡。 */
static FString GetLegacySilhouetteShaderCode()
{
    DEMO_LOG_STATIC_CALL(nullptr);
    // 本地MaterialTemplate.ush的GetViewportUV/ViewportUVToSceneTextureUV负责输出viewport与各buffer映射。
    // 通过真实SceneTexture节点接入Custom输入，让编译器记录SceneDepth/CustomDepth/Stencil/输入色资源需求。
    // 两个半径各8方向是有界二值mask膨胀；不计算枪身内部深度差或法线差。真实背景孔仍可能出现内沿。
    return TEXT(R"SHADER(
// UE5.4.4 postprocess output viewport pixels; never assume SceneTexture UV equals ViewportUV.
float CenterSelected = abs(StencilUsage.r - 253.0) < 0.5 ? 1.0 : 0.0;
if (CenterSelected > 0.5)
{
    return SceneColor.rgb;
}
float2 OutputTexel = GetSceneTextureViewSize(PPI_PostProcessInput0).zw;
float Radius = max(OutlineWidthPx, 0.0);
float Epsilon = max(DepthEpsilonCm, 0.0);
float CurrentSceneDepth = SceneDepthUsage.r;
float Edge = 0.0;
const float2 Directions[8] = {
    float2(1.0,0.0), float2(-1.0,0.0), float2(0.0,1.0), float2(0.0,-1.0),
    float2(0.70710678,0.70710678), float2(-0.70710678,0.70710678),
    float2(0.70710678,-0.70710678), float2(-0.70710678,-0.70710678)
};
[unroll] for (int Ring = 1; Ring <= 2; ++Ring)
{
    [unroll] for (int Direction = 0; Direction < 8; ++Direction)
    {
        float2 NeighborViewportUV = ViewportUV + Directions[Direction] * OutputTexel * Radius * (0.5 * Ring);
        if (all(NeighborViewportUV >= 0.0) && all(NeighborViewportUV < 1.0))
        {
            float2 StencilUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(NeighborViewportUV, PPI_CustomStencil), PPI_CustomStencil);
            float NeighborStencil = SceneTextureLookup(StencilUV, PPI_CustomStencil, false).r;
            if (abs(NeighborStencil - 253.0) < 0.5)
            {
                float2 CustomUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(NeighborViewportUV, PPI_CustomDepth), PPI_CustomDepth);
                float2 SceneUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(NeighborViewportUV, PPI_SceneDepth), PPI_SceneDepth);
                float CustomZ = SceneTextureLookup(CustomUV, PPI_CustomDepth, false).r;
                float NeighborSceneZ = SceneTextureLookup(SceneUV, PPI_SceneDepth, false).r;
                // Both comparisons are linear centimetres. Reject hidden source AND a nearer wall at the output pixel.
                float Visible = (CustomZ <= NeighborSceneZ + Epsilon && CustomZ <= CurrentSceneDepth + Epsilon) ? 1.0 : 0.0;
                Edge = max(Edge, Visible);
            }
        }
    }
}
return lerp(SceneColor.rgb, float3(1.0,1.0,1.0), Edge);
)SHADER");
}

/** A01已作者代码作为精确升级入口；保留坐标对齐，后续只修跨射线比较的几何语义。 */
static FString GetJitterAlignedSilhouetteShaderCode()
{
    DEMO_LOG_STATIC_CALL(nullptr);
    // CustomDepth/Stencil在启动配置中禁用投影抖动，而主SceneDepth仍采用当前帧投影。
    // UE5.4.4 SceneVisibility把像素jitter变为(2Jx/W,-2Jy/H)写入View.TemporalAAJitter；
    // NDC到viewport UV为(.5,-.5)，因此同一未抖动射线须在主深度中加此偏移。
    // 两处遮挡采样均换域，保0.1cm容差/253/纯白2px，不用epsilon补偿错位；SceneColor不偏移。
    FString Code=GetLegacySilhouetteShaderCode(); // 精确旧图派生，节点与输入合同不变。
    Code.ReplaceInline(TEXT("float CurrentSceneDepth = SceneDepthUsage.r;"),TEXT(
        "// CustomDepthTemporalAAJitter=0 is required for this after-TSR material.\n"
        "float2 SceneDepthJitterUV = View.TemporalAAJitter.xy * float2(0.5, -0.5);\n"
        "float2 CurrentDepthUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(ViewportUV + SceneDepthJitterUV, PPI_SceneDepth), PPI_SceneDepth);\n"
        "float CurrentSceneDepth = SceneTextureLookup(CurrentDepthUV, PPI_SceneDepth, false).r;"));
    Code.ReplaceInline(TEXT("ViewportUVToSceneTextureUV(NeighborViewportUV, PPI_SceneDepth)"),
        TEXT("ViewportUVToSceneTextureUV(NeighborViewportUV + SceneDepthJitterUV, PPI_SceneDepth)"));
    return Code;
}

/** 保留A02单侧平面候选的精确代码，供既有已保存九节点安全升级；不是新材质权限兜底。 */
static FString GetOneSidedPlaneShaderCode()
{
    DEMO_LOG_STATIC_CALL(nullptr);
    // 固定mask仍在上下外沿残留变化：直接把邻像素CustomZ与中心SceneZ相比，会把同一斜面误作前景。
    // 从稳定CustomDepth的同Stencil一侧邻点求局部device-Z平面（平面三角形的屏幕device-Z为仿射），
    // 将源深度投影到两次主深度实际point采样的同一射线，再以原0.1cm比较。不加容差/不省略遮挡。
    FString Code=GetJitterAlignedSilhouetteShaderCode(); // 九节点/七输入/2px/纯白与资源集合完全保持。
    Code.ReplaceInline(TEXT("// Both comparisons are linear centimetres. Reject hidden source AND a nearer wall at the output pixel.\n                float Visible = (CustomZ <= NeighborSceneZ + Epsilon && CustomZ <= CurrentSceneDepth + Epsilon) ? 1.0 : 0.0;"),TEXT(R"PLANE(
// Compare the SAME ray, not the depth of two different points on a sloped surface.
                float2 BufferSize = View.BufferSizeAndInvSize.xy;
                float2 BufferTexel = View.BufferSizeAndInvSize.zw;
                float2 SourceGrid = floor(CustomUV * BufferSize);
                float2 SourceCenterUV = (SourceGrid + 0.5) * BufferTexel;
                float SourceDeviceZ = ConvertToDeviceZ(CustomZ);
                float2 SourceSlope = float2(0.0, 0.0);
                float2 CustomOrigin = ViewportUVToSceneTextureUV(float2(0.0,0.0), PPI_CustomDepth);
                float2 CustomSpan = ViewportUVToSceneTextureUV(float2(1.0,1.0), PPI_CustomDepth) - CustomOrigin;
                [unroll] for (int Axis = 0; Axis < 2; ++Axis)
                {
                    float2 StepUV = Axis == 0 ? float2(BufferTexel.x,0.0) : float2(0.0,BufferTexel.y);
                    float2 PlusUV = SourceCenterUV + StepUV;
                    float2 MinusUV = SourceCenterUV - StepUV;
                    bool PlusInside = all(PlusUV >= CustomOrigin + 0.5 * BufferTexel) && all(PlusUV < CustomOrigin + CustomSpan);
                    bool MinusInside = all(MinusUV >= CustomOrigin + 0.5 * BufferTexel) && all(MinusUV < CustomOrigin + CustomSpan);
                    bool PlusSelected = PlusInside && abs(SceneTextureLookup(PlusUV, PPI_CustomStencil, false).r - 253.0) < 0.5;
                    bool MinusSelected = MinusInside && abs(SceneTextureLookup(MinusUV, PPI_CustomStencil, false).r - 253.0) < 0.5;
                    float PlusSlope = PlusSelected ? ConvertToDeviceZ(SceneTextureLookup(PlusUV, PPI_CustomDepth, false).r) - SourceDeviceZ : 0.0;
                    float MinusSlope = MinusSelected ? SourceDeviceZ - ConvertToDeviceZ(SceneTextureLookup(MinusUV, PPI_CustomDepth, false).r) : 0.0;
                    // Prefer the smaller one-sided change to avoid extrapolating across a bevel/disjoint part.
                    // If neither axis neighbor exists, zero slope conservatively retains the original comparison.
                    SourceSlope[Axis] = PlusSelected && MinusSelected ? (abs(PlusSlope) <= abs(MinusSlope) ? PlusSlope : MinusSlope) : PlusSelected ? PlusSlope : MinusSlope;
                }
                float2 SceneOrigin = ViewportUVToSceneTextureUV(float2(0.0,0.0), PPI_SceneDepth);
                float2 SceneSpan = ViewportUVToSceneTextureUV(float2(1.0,1.0), PPI_SceneDepth) - SceneOrigin;
                float2 SceneQueryUV[2] = { SceneUV, CurrentDepthUV };
                float SceneQueryZ[2] = { NeighborSceneZ, CurrentSceneDepth };
                float Visible = 1.0;
                [unroll] for (int TestRay = 0; TestRay < 2; ++TestRay)
                {
                    // Point sampling addresses this exact main-depth pixel center. Undo main jitter before
                    // mapping its ray into the stable custom-depth grid; viewport/buffer scale applied once.
                    float2 ScenePixelUV = (floor(SceneQueryUV[TestRay] * BufferSize) + 0.5) * BufferTexel;
                    float2 StableViewport = (ScenePixelUV - SceneOrigin) / SceneSpan - SceneDepthJitterUV;
                    float2 CompareGrid = ViewportUVToSceneTextureUV(StableViewport, PPI_CustomDepth) * BufferSize - 0.5;
                    float ProjectedDeviceZ = SourceDeviceZ + dot(SourceSlope, CompareGrid - SourceGrid);
                    float ProjectedSourceZ = (ProjectedDeviceZ > 0.0 && ProjectedDeviceZ <= 1.0) ? ConvertFromDeviceZ(ProjectedDeviceZ) : CustomZ;
                    Visible = min(Visible, ProjectedSourceZ <= SceneQueryZ[TestRay] + Epsilon ? 1.0 : 0.0);
                }
)PLANE"));
    return Code;
}

/** A03已退回的混合轴包络，仅作精确旧包升级身份；反例及原包保留，不再作为当前输出。 */
static FString GetRejectedEnvelopeShaderCode()
{
    DEMO_LOG_STATIC_CALL(nullptr);
    // 历史A03的两个轴可能来自不同面，组合所得平面不存在；最近实测点不能界定查询射线。
    // 原HLSL逐字保留用于白名单升级，不能因单Cube通过或低XOR复用其遮挡正确性结论。
    FString Code=GetOneSidedPlaneShaderCode(); // 输入/节点/颜色/宽度/SceneDepth两门不变。
    Code.ReplaceInline(TEXT("float2 SourceSlope = float2(0.0, 0.0);"),TEXT("float2 SourceSlopeLow = float2(0.0, 0.0);\n                float2 SourceSlopeHigh = float2(0.0, 0.0);\n                float NearestMeasuredSourceZ = CustomZ;"));
    Code.ReplaceInline(TEXT("float PlusSlope = PlusSelected ? ConvertToDeviceZ(SceneTextureLookup(PlusUV, PPI_CustomDepth, false).r) - SourceDeviceZ : 0.0;\n                    float MinusSlope = MinusSelected ? SourceDeviceZ - ConvertToDeviceZ(SceneTextureLookup(MinusUV, PPI_CustomDepth, false).r) : 0.0;"),TEXT(
        "float PlusZ = PlusSelected ? SceneTextureLookup(PlusUV, PPI_CustomDepth, false).r : CustomZ;\n"
        "                    float MinusZ = MinusSelected ? SceneTextureLookup(MinusUV, PPI_CustomDepth, false).r : CustomZ;\n"
        "                    NearestMeasuredSourceZ = min(NearestMeasuredSourceZ, min(PlusZ, MinusZ));\n"
        "                    float PlusSlope = PlusSelected ? ConvertToDeviceZ(PlusZ) - SourceDeviceZ : 0.0;\n"
        "                    float MinusSlope = MinusSelected ? SourceDeviceZ - ConvertToDeviceZ(MinusZ) : 0.0;"));
    Code.ReplaceInline(TEXT("SourceSlope[Axis] = PlusSelected && MinusSelected ? (abs(PlusSlope) <= abs(MinusSlope) ? PlusSlope : MinusSlope) : PlusSelected ? PlusSlope : MinusSlope;"),TEXT(
        "SourceSlopeLow[Axis] = PlusSelected && MinusSelected ? min(PlusSlope, MinusSlope) : PlusSelected ? PlusSlope : MinusSlope;\n"
        "                    SourceSlopeHigh[Axis] = PlusSelected && MinusSelected ? max(PlusSlope, MinusSlope) : PlusSelected ? PlusSlope : MinusSlope;"));
    Code.ReplaceInline(TEXT("float ProjectedDeviceZ = SourceDeviceZ + dot(SourceSlope, CompareGrid - SourceGrid);"),TEXT(
        "float2 RayDelta = CompareGrid - SourceGrid;\n"
        "                    float2 FrontSlope = float2(RayDelta.x >= 0.0 ? SourceSlopeHigh.x : SourceSlopeLow.x, RayDelta.y >= 0.0 ? SourceSlopeHigh.y : SourceSlopeLow.y);\n"
        "                    float ProjectedDeviceZ = SourceDeviceZ + dot(FrontSlope, RayDelta);"));
    Code.ReplaceInline(TEXT("Visible = min(Visible, ProjectedSourceZ <= SceneQueryZ[TestRay] + Epsilon ? 1.0 : 0.0);"),TEXT(
        "ProjectedSourceZ = max(ProjectedSourceZ, NearestMeasuredSourceZ);\n"
        "                    Visible = min(Visible, ProjectedSourceZ <= SceneQueryZ[TestRay] + Epsilon ? 1.0 : 0.0);"));
    Code.ReplaceInline(TEXT("// Prefer the smaller one-sided change to avoid extrapolating across a bevel/disjoint part."),TEXT("// Keep both observed one-sided surface slopes; bound the near envelope by real source samples."));
    return Code;
}

FString UDemoGunOutlineMaterialLibrary::GetSilhouetteShaderCode()
{
    DEMO_LOG_STATIC_CALL(nullptr);
    // A04明确回退：A01原CustomZ双门是不可绕过的必要条件，局部重投影只可额外拒绝。
    // 同Stencil不证明同面，A02估计跨折面时可丢失白沿，但绝不能把原门拒绝改为通过。
    // 因此接受集合严格包含于A01；这不是任意几何/薄前景正确性的证明，观感须独立实测。
    FString Code=GetOneSidedPlaneShaderCode(); // 保持九节点/七输入、2px/纯白、0.1cm与原双门。
    Code.ReplaceInline(TEXT("float Visible = 1.0;"),TEXT(
        "// Original dual gate is mandatory. Estimated planes may only reject, never grant visibility.\n"
        "                float Visible = (CustomZ <= NeighborSceneZ + Epsilon && CustomZ <= CurrentSceneDepth + Epsilon) ? 1.0 : 0.0;"));
    Code.ReplaceInline(TEXT("// If neither axis neighbor exists, zero slope conservatively retains the original comparison."),
        TEXT("// Missing support uses zero slope; same stencil never certifies the same triangle. Original dual gate remains mandatory."));
    return Code;
}

bool UDemoGunOutlineMaterialLibrary::ConfigureSilhouetteMaterial(UMaterial* Material)
{
    DEMO_LOG_STATIC_CALL(Material);
    if (!IsValid(Material))
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_CONFIG_REJECT invalid_material")); return false; }
    if (UMaterialEditingLibrary::GetNumMaterialExpressions(Material)!=0)
    {
        UMaterialExpressionCustom* Existing=Cast<UMaterialExpressionCustom>(UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_EmissiveColor)); // 仅升级精确已知8/9节点；A03九节点仅改Custom代码。
        const FString Current=GetSilhouetteShaderCode();
        const FString Aligned=GetJitterAlignedSilhouetteShaderCode(); // A01坐标对齐候选，同样只允许精确已知代码升级。
        const FString OneSided=GetOneSidedPlaneShaderCode(); // A02局部单侧重投影候选，原报告/包备份保留。
        const FString RejectedEnvelope=GetRejectedEnvelopeShaderCode(); // A03已证非保守，仅允许精确升级离开，不允许当前作者生成。
        const FString Legacy=GetLegacySilhouetteShaderCode(); // 旧正式A03代码，不把新代码误用作旧包身份。
        const FString Previous=Legacy.Replace(TEXT("GetSceneTextureViewSize(PPI_PostProcessInput0).zw"),TEXT("PostProcessOutput_ViewportSizeInverse"));
        const int32 Count=UMaterialEditingLibrary::GetNumMaterialExpressions(Material); // 8为旧Alpha缺失图，9为当前正式图。
        if (!Existing||(Existing->Code!=Previous&&Existing->Code!=Legacy&&Existing->Code!=Aligned&&Existing->Code!=OneSided&&Existing->Code!=RejectedEnvelope&&Existing->Code!=Current)||(Count!=8&&Count!=9))
        { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_CONFIG_REJECT unknown_nonempty_graph")); return false; }
        const FString BeforeCode=Existing->Code; // 只供内存失败恢复；作者失败不保存/不整包回滚。
        Existing->Modify(); Existing->Code=Current;
        if (Count==8&&!ConnectSilhouetteOpaqueAlpha(Material)) return false;
        if (!AuditSilhouetteMaterial(Material)) { Existing->Code=BeforeCode; UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_CONFIG_REJECT custom_code_restored_no_save")); return false; }
        UMaterialEditingLibrary::RecompileMaterial(Material);
        return AuditSilhouetteMaterial(Material);
    }
    Material->MaterialDomain=MD_PostProcess;
    Material->BlendableLocation=BL_SceneColorAfterTonemapping; // 5.4新名；旧BL_AfterTonemapping已Hidden/deprecated。
    Material->BlendMode=BLEND_Opaque;
    Material->SetShadingModel(MSM_Unlit);
    Material->TwoSided=false;
    Material->bDisableDepthTest=false;
    Material->bEnableStencilTest=false; // 必须处理轮廓外的背景像素，不能先用硬件StencilTest剔除它们。
    Material->bIsBlendable=true;

    UMaterialExpressionSceneTexture* SceneColor=Cast<UMaterialExpressionSceneTexture>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionSceneTexture::StaticClass(),-900,-350)); // 输入色资源节点。
    UMaterialExpressionSceneTexture* SceneDepth=Cast<UMaterialExpressionSceneTexture>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionSceneTexture::StaticClass(),-900,-180)); // 当前像素线性深度，cm。
    UMaterialExpressionSceneTexture* CustomDepth=Cast<UMaterialExpressionSceneTexture>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionSceneTexture::StaticClass(),-900,-10)); // 真实资源依赖，不以Custom字符串隐式请求纹理。
    UMaterialExpressionSceneTexture* Stencil=Cast<UMaterialExpressionSceneTexture>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionSceneTexture::StaticClass(),-900,160)); // 本机clone专用253，源枪Stencil不改。
    UClass* ViewportClass=FindObject<UClass>(nullptr,TEXT("/Script/Engine.MaterialExpressionScreenPosition")); // 此UE类没有DLL导出，走引擎反射UClass，不调用未导出的StaticClass。
    UMaterialExpression* Viewport=ViewportClass?UMaterialEditingLibrary::CreateMaterialExpression(Material,ViewportClass,-900,330):nullptr; // 输出0是实际ViewportUV。
    UMaterialExpressionScalarParameter* Width=Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionScalarParameter::StaticClass(),-650,470)); // 输出像素半径参数。
    UMaterialExpressionScalarParameter* Epsilon=Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionScalarParameter::StaticClass(),-650,610)); // 仅深度浮点容差，不放宽交互LOS。
    UMaterialExpressionCustom* Composite=Cast<UMaterialExpressionCustom>(UMaterialEditingLibrary::CreateMaterialExpression(Material,UMaterialExpressionCustom::StaticClass(),-300,0)); // 唯一最终RGB合成节点。
    if (!SceneColor||!SceneDepth||!CustomDepth||!Stencil||!Viewport||!Width||!Epsilon||!Composite)
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_CONFIG_REJECT node_factory")); return false; }
    SceneColor->SceneTextureId=PPI_PostProcessInput0; SceneColor->bFiltered=false;
    SceneDepth->SceneTextureId=PPI_SceneDepth; SceneDepth->bFiltered=false;
    CustomDepth->SceneTextureId=PPI_CustomDepth; CustomDepth->bFiltered=false;
    Stencil->SceneTextureId=PPI_CustomStencil; Stencil->bFiltered=false;
    Width->ParameterName=TEXT("OutlineWidthPx"); Width->DefaultValue=2.f;
    Epsilon->ParameterName=TEXT("DepthEpsilonCm"); Epsilon->DefaultValue=.1f;
    Composite->Description=TEXT("Hub silhouette outside mask only, white after tonemapping");
    Composite->OutputType=CMOT_Float3; Composite->Code=GetSilhouetteShaderCode();
    Composite->Inputs.Reset();
    const FName InputNames[]={TEXT("SceneColor"),TEXT("SceneDepthUsage"),TEXT("CustomDepthUsage"),TEXT("StencilUsage"),TEXT("ViewportUV"),TEXT("OutlineWidthPx"),TEXT("DepthEpsilonCm")}; // 顺序是作者/读回稳定合同。
    UMaterialExpression* Inputs[]={SceneColor,SceneDepth,CustomDepth,Stencil,Viewport,Width,Epsilon}; // 当前Material拥有的同步节点借用，不跨帧缓存。
    for (int32 Index=0;Index<UE_ARRAY_COUNT(InputNames);++Index)
    {
        FCustomInput Input; Input.InputName=InputNames[Index]; Composite->Inputs.Add(Input); // 真实连线在下方经官方作者接口建立。
    }
    // 新Custom构造器已创建唯一主输出；其GetOutputType读取OutputType，不调用MinimalAPI未导出的RebuildOutputs。
    for (int32 Index=0;Index<UE_ARRAY_COUNT(InputNames);++Index)
    {
        const FString OutputName=Index==4?TEXT("ViewportUV"):TEXT(""); // ScreenPosition的真实命名输出，其他节点取0。
        if (!UMaterialEditingLibrary::ConnectMaterialExpressions(Inputs[Index],OutputName,Composite,InputNames[Index].ToString()))
        { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_CONFIG_REJECT input=%s"),*InputNames[Index].ToString()); return false; }
    }
    if (!UMaterialEditingLibrary::ConnectMaterialProperty(Composite,TEXT(""),MP_EmissiveColor))
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_CONFIG_REJECT emissive_output")); return false; }
    if (!ConnectSilhouetteOpaqueAlpha(Material)) return false;
    UMaterialEditingLibrary::RecompileMaterial(Material);
    return AuditSilhouetteMaterial(Material);
}

bool UDemoGunOutlineMaterialLibrary::AuditSilhouetteMaterial(UMaterial* Material)
{
    DEMO_LOG_STATIC_CALL(Material);
    if (!IsValid(Material)||Material->MaterialDomain!=MD_PostProcess||Material->BlendableLocation!=BL_SceneColorAfterTonemapping
        ||Material->BlendMode!=BLEND_Opaque||Material->TwoSided||Material->bEnableStencilTest||!Material->bIsBlendable||!Material->BlendableOutputAlpha)
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_AUDIT_REJECT render_domain/location/stencil_test")); return false; }
    UMaterialExpressionCustom* Composite=Cast<UMaterialExpressionCustom>(UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_EmissiveColor)); // 实际输出，不以节点名推断。
    if (!Composite||Composite->Code!=GetSilhouetteShaderCode()||Composite->OutputType!=CMOT_Float3
        ||Composite->Inputs.Num()!=7||UMaterialEditingLibrary::GetNumMaterialExpressions(Material)!=9)
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_AUDIT_REJECT custom_code/output/count")); return false; }
    const FName InputNames[]={TEXT("SceneColor"),TEXT("SceneDepthUsage"),TEXT("CustomDepthUsage"),TEXT("StencilUsage"),TEXT("ViewportUV"),TEXT("OutlineWidthPx"),TEXT("DepthEpsilonCm")}; // 与配置函数同合同，逐项读真实连接。
    const ESceneTextureId Textures[]={PPI_PostProcessInput0,PPI_SceneDepth,PPI_CustomDepth,PPI_CustomStencil}; // 4个资源必须经SceneTexture节点触发编译使用标记。
    for (int32 Index=0;Index<UE_ARRAY_COUNT(InputNames);++Index)
    {
        const FCustomInput& Input=Composite->Inputs[Index]; // 同步只读反射输入，不能写回原材质。
        UMaterialExpression* Node=Input.Input.Expression;
        if (Input.InputName!=InputNames[Index]||!Node||Node->GetTypedOuter<UMaterial>()!=Material||Input.Input.OutputIndex!=0)
        { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_AUDIT_REJECT input_identity index=%d"),Index); return false; }
        if (Index<UE_ARRAY_COUNT(Textures))
        {
            const UMaterialExpressionSceneTexture* Texture=Cast<UMaterialExpressionSceneTexture>(Node);
            if (!Texture||Texture->SceneTextureId!=Textures[Index]||Texture->bFiltered)
            { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_AUDIT_REJECT scene_texture index=%d"),Index); return false; }
        }
        else if (Index==4)
        {
            UClass* ViewportClass=FindObject<UClass>(nullptr,TEXT("/Script/Engine.MaterialExpressionScreenPosition")); // 同配置路径，拒绝仅同名但不属于实际引擎类的节点。
            if (!ViewportClass||!Node->IsA(ViewportClass))
            { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_AUDIT_REJECT viewport_uv")); return false; }
        }
        else
        {
            const UMaterialExpressionScalarParameter* Parameter=Cast<UMaterialExpressionScalarParameter>(Node);
            const FName ExpectedName=Index==5?TEXT("OutlineWidthPx"):TEXT("DepthEpsilonCm"); // 像素半径与线性cm容差不混用。
            const float ExpectedValue=Index==5?2.f:.1f;
            if (!Parameter||Parameter->ParameterName!=ExpectedName||!FMath::IsNearlyEqual(Parameter->DefaultValue,ExpectedValue,1.e-6f))
            { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_AUDIT_REJECT scalar_parameter index=%d"),Index); return false; }
        }
    }
    if (UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_WorldPositionOffset)
        ||UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_OpacityMask))
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_AUDIT_REJECT old_hull_input")); return false; }
    const UMaterialExpressionConstant* Alpha=Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_Opacity)); // 当前完整不透明视图输出合同，不假设未连Opacity的默认值。
    if (!Alpha||!FMath::IsNearlyEqual(Alpha->R,1.f,1.e-6f))
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SILHOUETTE_AUDIT_REJECT opaque_alpha")); return false; }
    return true;
}
