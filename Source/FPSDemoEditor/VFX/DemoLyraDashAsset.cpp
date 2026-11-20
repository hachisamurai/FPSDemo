// 职责：适配Lyra Niagara候选为项目独立资产；使用公开UE5.4图API绑定参数，源Lyra只读，运行时不依赖Editor/Lyra插件。
#include "VFX/DemoDashTrailAssetLibrary.h"
#include "Debug/DemoLog.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeAssignment.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Engine/Texture.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UObjectHash.h"

bool UDemoDashTrailAssetLibrary::AdaptLyraDash(UNiagaraSystem* System,UMaterialInterface* Material,USkeletalMesh* PreviewMesh)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!System || !Material || !System->GetPathName().StartsWith(TEXT("/Game/VFX/Player/Dash/")))
    { UE_LOG(LogFPSDemo,Error,TEXT("DASH_FLOW_AUTHOR rejected invalid/outside candidate")); return false; }
    System->Modify();
    if (UMaterial* Base=Cast<UMaterial>(Material))
    {
        UTexture* Noise=LoadObject<UTexture>(nullptr,TEXT("/Game/VFX/Player/Dash/T_DashFlowNoise.T_DashFlowNoise")); // 原生遍历完整表达式，包含不再连到输出的旧SceneColor噪声分支，避免遗留外部纹理引用。
        if (!Noise) return false;
        for (UMaterialExpression* Expression:Base->GetExpressions())
            if (UMaterialExpressionTextureSample* Sample=Cast<UMaterialExpressionTextureSample>(Expression); Sample && Sample->Texture && Sample->Texture->GetName()==TEXT("textureNoise")) Sample->Texture=Noise;
        Base->PostEditChange(); Base->MarkPackageDirty();
    }
    FNiagaraUserRedirectionParameterStore& Parameters=System->GetExposedParameters(); // 当前复制系统拥有，绝不修改引擎模块脚本。
    const FNiagaraVariable Width(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.TrailWidth")); // cm，供原Ribbon宽度模块读取。
    const FNiagaraVariable Count(FNiagaraTypeDefinition::GetIntDef(),TEXT("User.PointCount")); // 采样轨迹数，替代原32..42随机密度。
    const FNiagaraVariable MaterialVariable(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()),TEXT("User.FlowMaterial")); // 每Pawn MID覆盖。
    Parameters.SetParameterValue(1.6f,Width,true);
    Parameters.SetParameterValue(24,Count,true);
    Parameters.AddParameter(MaterialVariable);
    Parameters.SetParameterValue(.3f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.DashDuration")),true);
    Parameters.SetParameterValue(1.f,FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.LifetimeMultiplier")),true);
    Parameters.SetParameterValue(FLinearColor(.12f,.85f,.72f,.65f),FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.TeamColor")),true);
    TArray<UObject*> InnerObjects; // 包括图节点默认DI与编译脚本副本；只清理当前候选System拥有的对象。
    GetObjectsWithOuter(System,InnerObjects,true);
    for (UObject* Interface:InnerObjects) // 仅改暴露参数会遗漏内嵌DI并间接带入整个Quinn资源闭包。
    {
        if (UNiagaraDataInterfaceSkeletalMesh* Mesh=Cast<UNiagaraDataInterfaceSkeletalMesh>(Interface))
        { Mesh->Modify(); Mesh->DefaultMesh=PreviewMesh; Mesh->PreviewMesh=PreviewMesh; Mesh->WholeMeshLOD=INDEX_NONE; Mesh->SourceMode=ENDISkeletalMesh_SourceMode::Source; } // 最低LOD减轻表面采样开销；正式Body该LOD必须保留CPU数据。
    }
    const TSet<FNiagaraVariable> Known={Width,Count}; // 显式类型约束，不能把Int参数误连成Float。
    int32 WidthLinks=0,CountLinks=0,Ribbons=0; // 最小结构契约；不匹配版本拒绝保存。
    for (FNiagaraEmitterHandle& Handle:System->GetEmitterHandles())
    {
        FVersionedNiagaraEmitterData* Data=Handle.GetEmitterData(); // System拥有内嵌Emitter版本，游戏线程离线修改。
        if (!Data) return false;
        for (UNiagaraRendererProperties* Renderer:Data->GetRenderers())
        {
            Renderer->Modify();
            if (UNiagaraRibbonRendererProperties* Ribbon=Cast<UNiagaraRibbonRendererProperties>(Renderer))
            { Ribbon->Material=Material; Ribbon->MaterialUserParamBinding.Parameter=MaterialVariable; ++Ribbons; }
            else Renderer->SetIsEnabled(false); // DashPoints只提供采样数据，不渲染默认白色Sprite。
        }
        UNiagaraScriptSource* Source=Cast<UNiagaraScriptSource>(Data->GraphSource); // 借用该发射器图，不递归修改共享模块。
        if (!Source || !Source->NodeGraph) return false;
        UNiagaraGraph* Graph=Source->NodeGraph;
        const TArray<TObjectPtr<UEdGraphNode>> Nodes=Graph->Nodes; // 图工具会添加节点，快照避免遍历数组失效。
        for (UEdGraphNode* Node:Nodes)
        {
            for (UEdGraphPin* Pin:Node->Pins)
            {
                if (Pin->Direction==EGPD_Input && Pin->PinName==TEXT("SpawnBurst_Instantaneous.Spawn Count"))
                {
                    Pin->BreakAllPinLinks(); // 仅断开旧随机值；不可达候选节点由编译器丢弃，不修改源资产。
                    FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(*Pin,FNiagaraParameterHandle(Count.GetName()),Known);
                    ++CountLinks; break;
                }
            }
            UNiagaraNodeAssignment* Assignment=Cast<UNiagaraNodeAssignment>(Node); // 原Lyra更新堆栈已有RibbonWidth赋值模块。
            if (!Assignment) continue;
            for (const FNiagaraVariable& Target:Assignment->GetAssignmentTargets())
            {
                if (Target.GetName()!=TEXT("Particles.RibbonWidth")) continue;
                const FName InputName(*(Assignment->GetFunctionName()+TEXT(".Particles.RibbonWidth"))); // 实际模块实例别名，不能写死SetVariables序号。
                UEdGraphPin& Pin=FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(*Assignment,FNiagaraParameterHandle(InputName),FNiagaraTypeDefinition::GetFloatDef(),FGuid(),FGuid());
                Pin.BreakAllPinLinks();
                FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(Pin,FNiagaraParameterHandle(Width.GetName()),Known);
                ++WidthLinks;
            }
        }
        Data->GraphSource->MarkNotSynchronized(TEXT("FPSDemo dash adaptation")); // 通过公共基类虚接口分派，UE5.4具体Source/Graph函数未导出。
    }
    System->MarkPackageDirty();
    UE_LOG(LogFPSDemo,Display,TEXT("DASH_FLOW_AUTHOR width=%d count=%d renderers=%d"),WidthLinks,CountLinks,Ribbons);
    return WidthLinks==1 && CountLinks==1 && Ribbons==1 && CompileDashSystem(System);
}
