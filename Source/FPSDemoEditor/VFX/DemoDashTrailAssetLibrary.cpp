// 职责：离线制作闪避系统的参数与编译数据；由Python明确调用，依赖Niagara原生编辑器接口，不修改引擎源码。
#include "VFX/DemoDashTrailAssetLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Debug/DemoLog.h"

void UDemoDashTrailAssetLibrary::SetDashDefaults(UNiagaraSystem* System)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); // 静态制作入口无this，沿用同类别与CALL级别。
    if (!System) { UE_LOG(LogFPSDemo,Error,TEXT("DASH_TRAIL_AUTHOR missing system")); return; }
    System->Modify();
    FNiagaraUserRedirectionParameterStore& Parameters=System->GetExposedParameters(); // System拥有的可序列化默认值，仅同步Editor制作访问。
    Parameters.SetParameterValue(FLinearColor(.08f,.65f,1.f,.8f), FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.TrailColor")),true);
    Parameters.SetParameterValue(8.f, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.TrailWidth")),true);
    Parameters.SetParameterValue(.22f, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.ParticleLifetime")),true);
    Parameters.SetParameterValue(60.f, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.SpawnRate")),true);
    System->MarkPackageDirty();
}

bool UDemoDashTrailAssetLibrary::CompileDashSystem(UNiagaraSystem* System)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); // 静态制作入口无this，不能使用对象版宏。
    if (!System) { UE_LOG(LogFPSDemo,Error,TEXT("DASH_TRAIL_AUTHOR compile rejected: null system")); return false; }
    System->RequestCompile(true);
    System->WaitForCompilationComplete(false,false); // CPU Ribbon，无GPU任务；游戏线程等待离线编译，不启动World或技能。
    // NullRHI制作进程的IsReadyToRun固定为false；图有效性与未完成编译才是这里的保存条件。
    if (!System->IsValid() || System->HasOutstandingCompilationRequests())
    { UE_LOG(LogFPSDemo,Error,TEXT("DASH_TRAIL_AUTHOR compilation failed: %s"),*System->GetPathName()); return false; }
    UE_LOG(LogFPSDemo,Log,TEXT("DASH_TRAIL_AUTHOR compiled %s"),*System->GetPathName());
    return true;
}
