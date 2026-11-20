// 职责：以UE5.4.4 CreateLODLevel/原生模块和MaterialEditingLibrary制作可读回的真正枪口短闪资源。
// 边界：只配置传入空资产；文件保存/备份/来源SHA/四枪MuzzleFlash mesh-only Socket与运行附着由外层作者及运行代码负责。
// 参考：本地ParticleComponents.cpp::CreateLODLevel、MaterialEditingLibrary；Epic引擎许可，仅接口复用，无外部代码/素材。
// NetworkPredictionExtras Proto有枪口贴图/网格但未采用，避免其插件依赖/素材闭包；本资源全部程序化，非旧首段Tracer。
#include "Diagnostics/DemoMuzzleFlashVfxAuthoring.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "MaterialEditingLibrary.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Location/ParticleModuleLocation.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionVectorConstant.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Debug/DemoStaticLog.h"

namespace
{
    constexpr float FlashLifeSeconds=.055f; // 粒子/Emitter单次寿命秒；兼顾55ms自然闪灭与高速连射不叠糊。
    constexpr float FlashSizeCm=14.f; // 未继承父组件缩放前的Sprite作者尺寸cm；FP 0.2缩放后约2.8cm，清晰匹配枪口。

    /** 返回本资源独立HLSL；UV为原生Sprite TexCoord0，Age为原生ParticleRelativeTime的0..1，不读取屏幕/世界位置。 */
    FString FlashShaderCode()
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        return TEXT(R"SHADER(
// Native particle UV and relative lifetime only; multi-lobe organic muzzle burst with soft quad-border falloff.
float2 p = (UV - 0.5) * 2.0;
float r2 = dot(p, p);
float r = sqrt(r2);
float2 d = p / max(r, 0.0001);
float cos3 = d.x * (4.0 * d.x * d.x - 3.0);
float lobe6 = pow(saturate(0.5 + 0.5 * (2.0 * cos3 * cos3 - 1.0)), 1.8);
float lobe2 = pow(saturate(abs(d.x * d.y) * 2.0), 1.4);
float expand = 0.72 + 0.28 * saturate(Age * 1.4);
float maxR = (0.34 + 0.42 * lobe6 + 0.16 * lobe2) * expand;
float t = saturate(1.0 - r / max(maxR, 0.001));
float flame = t * t * (3.0 - 2.0 * t);
float core = pow(saturate(1.0 - r / 0.36), 2.4);
float halo = pow(saturate(1.0 - r / 0.92), 2.8) * 0.38;
float life = pow(saturate(1.0 - Age), 1.45);
float edgeMask = saturate((0.96 - r) / 0.12);
float alpha = saturate((flame * 0.72 + core + halo) * edgeMask) * life;
float3 col = lerp(float3(1.0, 0.28, 0.04), float3(1.0, 0.94, 0.78), saturate(core * 1.2 + flame * 0.35));
return float4(col * (9.0 * life), alpha);
)SHADER");
    }

    /** Error由当前同步审查借用；所有拒绝明确记录原因，不改受审资产或默认门限。 */
    bool Reject(FString& Error,const TCHAR* Reason)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        Error=Reason;
        UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_AUTHOR_REJECT %s"),Reason);
        return false;
    }

    /** 仅按实际引擎UClass创建本Material节点；ParticleRelativeTime未导出StaticClass，禁止直接链接其C++类型。 */
    UMaterialExpression* AddExpression(UMaterial* Material,const TCHAR* ClassPath,int32 X,int32 Y)
    {
        DEMO_LOG_STATIC_CALL(Material);
        UClass* ExpressionClass=FindObject<UClass>(nullptr,ClassPath); // 已加载Engine反射类；不启动插件或装载外部代码。
        if(!ExpressionClass||!ExpressionClass->IsChildOf(UMaterialExpression::StaticClass()))
        { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_AUTHOR_REJECT missing_expression %s"),ClassPath); return nullptr; }
        return UMaterialEditingLibrary::CreateMaterialExpression(Material,ExpressionClass,X,Y);
    }

    /** 为Owner模块设置独占常量分布并更新原生LookupTable；单位由调用者对应Rate/寿命/比例字段定义。 */
    void SetFloat(UParticleModule* Owner,FRawDistributionFloat& Target,float Value)
    {
        DEMO_LOG_STATIC_CALL(Owner);
        UDistributionFloatConstant* Constant=NewObject<UDistributionFloatConstant>(Owner,NAME_None,RF_Transactional); // 分布随本模块序列化，不引用引擎默认对象。
        Constant->Constant=Value; Constant->bIsDirty=true;
        Target.Distribution=Constant;
        Target.Initialize();
    }

    /** 为Owner模块设置独占向量常量并更新LookupTable；位置/尺寸为cm，速度为cm/s，无随机作者偏移。 */
    void SetVector(UParticleModule* Owner,FRawDistributionVector& Target,const FVector& Value)
    {
        DEMO_LOG_STATIC_CALL(Owner);
        UDistributionVectorConstant* Constant=NewObject<UDistributionVectorConstant>(Owner,NAME_None,RF_Transactional); // 仅新模板子对象，无共享分布写入。
        Constant->Constant=Value; Constant->bLockAxes=false; Constant->LockedAxes=EDVLF_None; Constant->bIsDirty=true;
        Target.Distribution=Constant;
        Target.Initialize();
    }

    /** 只读核分布实际类型/归属/常量；不调用可能重建Lookup的GetValue，不把父类随机分布当常量。 */
    bool IsFloat(const FRawDistributionFloat& Target,const UParticleModule* Owner,float Expected)
    {
        DEMO_LOG_STATIC_CALL(Owner);
        const UDistributionFloatConstant* Constant=Cast<UDistributionFloatConstant>(Target.Distribution);
        return Constant&&Constant->GetClass()==UDistributionFloatConstant::StaticClass()&&Constant->GetOuter()==Owner
            &&FMath::IsNearlyEqual(Constant->Constant,Expected,1.e-6f);
    }

    /** 只读核向量常量和模块所有权；所有轴都核，不用总长度代替位置/速度零偏移。 */
    bool IsVector(const FRawDistributionVector& Target,const UParticleModule* Owner,const FVector& Expected)
    {
        DEMO_LOG_STATIC_CALL(Owner);
        const UDistributionVectorConstant* Constant=Cast<UDistributionVectorConstant>(Target.Distribution);
        return Constant&&Constant->GetClass()==UDistributionVectorConstant::StaticClass()&&Constant->GetOuter()==Owner
            &&!Constant->bLockAxes&&Constant->LockedAxes==EDVLF_None&&Constant->Constant.Equals(Expected,1.e-6f);
    }

    /** 只读核5节点、每条实际连接与HLSL；Error输出具体合同失败，不修改现有材质或接受同名替代图。 */
    bool AuditMaterial(UMaterial* Material,FString& Error)
    {
        DEMO_LOG_STATIC_CALL(Material);
        if(!IsValid(Material)||Material->MaterialDomain!=MD_Surface||Material->BlendMode!=BLEND_Additive
            ||!Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit)||!Material->TwoSided||Material->bDisableDepthTest
            ||Material->bUseMaterialAttributes||!UMaterialEditingLibrary::HasMaterialUsage(Material,MATUSAGE_ParticleSprites)
            ||UMaterialEditingLibrary::GetNumMaterialExpressions(Material)!=5)
            return Reject(Error,TEXT("material domain/blend/shading/depth/usage/node_count"));
        UMaterialExpressionComponentMask* RGB=Cast<UMaterialExpressionComponentMask>(UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_EmissiveColor)); // 实际根输入，不用节点命名猜连接。
        UMaterialExpressionComponentMask* Alpha=Cast<UMaterialExpressionComponentMask>(UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_Opacity));
        if(!RGB||!Alpha||RGB==Alpha||RGB->GetTypedOuter<UMaterial>()!=Material||Alpha->GetTypedOuter<UMaterial>()!=Material
            ||!RGB->R||!RGB->G||!RGB->B||RGB->A||Alpha->R||Alpha->G||Alpha->B||!Alpha->A)
            return Reject(Error,TEXT("material RGB/alpha masks"));
        UMaterialExpressionCustom* Star=Cast<UMaterialExpressionCustom>(RGB->Input.Expression); // 同一个float4输出，RGB/alpha分离后接各自原生属性。
        if(!Star||Alpha->Input.Expression!=Star||RGB->Input.OutputIndex!=0||Alpha->Input.OutputIndex!=0
            ||Star->GetTypedOuter<UMaterial>()!=Material||Star->Code!=FlashShaderCode()||Star->OutputType!=CMOT_Float4
            ||Star->Inputs.Num()!=2||Star->AdditionalOutputs.Num()!=0||Star->AdditionalDefines.Num()!=0||Star->IncludeFilePaths.Num()!=0)
            return Reject(Error,TEXT("material custom code/inputs/output"));
        const FCustomInput& UVInput=Star->Inputs[0]; // UV固定TexCoord0，不能替换为屏幕/世界空间坐标。
        const FCustomInput& AgeInput=Star->Inputs[1]; // Age由Sprite原生相对寿命驱动，不用World时间永久发光。
        const UMaterialExpressionTextureCoordinate* UV=Cast<UMaterialExpressionTextureCoordinate>(UVInput.Input.Expression);
        UClass* AgeClass=FindObject<UClass>(nullptr,TEXT("/Script/Engine.MaterialExpressionParticleRelativeTime")); // 未导出StaticClass；只核实际反射类。
        if(UVInput.InputName!=TEXT("UV")||AgeInput.InputName!=TEXT("Age")||UVInput.Input.OutputIndex!=0||AgeInput.Input.OutputIndex!=0
            ||!UV||UV->GetTypedOuter<UMaterial>()!=Material||UV->CoordinateIndex!=0||UV->UTiling!=1.f||UV->VTiling!=1.f||UV->UnMirrorU||UV->UnMirrorV
            ||!AgeClass||!AgeInput.Input.Expression||AgeInput.Input.Expression->GetClass()!=AgeClass||AgeInput.Input.Expression->GetTypedOuter<UMaterial>()!=Material)
            return Reject(Error,TEXT("material native UV/particle-relative-time connections"));
        if(UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_WorldPositionOffset)
            ||UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_OpacityMask))
            return Reject(Error,TEXT("material unexpected WPO/opacity-mask"));
        return true;
    }

    /** 只读核单Emitter/LOD和完整4模块白名单；Error输出失败原因，禁止碰撞/灯/连续烟的额外模块混入。 */
    bool AuditSystem(UParticleSystem* System,UMaterial* Material,FString& Error)
    {
        DEMO_LOG_STATIC_CALL(System);
        if(!IsValid(System)||System->Emitters.Num()!=1||!System->Emitters[0]||System->Emitters[0]->GetClass()!=UParticleSpriteEmitter::StaticClass())
            return Reject(Error,TEXT("system requires one native CPU sprite emitter"));
        const UParticleEmitter* Emitter=System->Emitters[0]; // System强持有，同步只读；不创建实际粒子实例。
        if(Emitter->GetOuter()!=System||Emitter->LODLevels.Num()!=1||!Emitter->LODLevels[0]||Emitter->EmitterRenderMode!=ERM_Normal||Emitter->bUseLegacySpawningBehavior)
            return Reject(Error,TEXT("system emitter ownership/LOD/render mode"));
        const UParticleLODLevel* LOD=Emitter->LODLevels[0]; // 原生CreateLODLevel产物，不读取Python受保护包装字段。
        const UParticleModuleRequired* Required=LOD->RequiredModule;
        const UParticleModuleSpawn* Spawn=LOD->SpawnModule;
        if(LOD->GetOuter()!=Emitter||LOD->Level!=0||!LOD->bEnabled||LOD->TypeDataModule||LOD->EventGenerator||LOD->Modules.Num()!=4
            ||!Required||!Spawn||Required->GetOuter()!=System||Spawn->GetOuter()!=System||!Required->bEnabled||!Spawn->bEnabled)
            return Reject(Error,TEXT("system LOD/module ownership/count/enabled"));
        if(Required->Material!=Material||!Required->bUseLocalSpace||!Required->EmitterOrigin.IsNearlyZero(1.e-6f)
            ||!Required->EmitterRotation.IsNearlyZero(1.e-6f)||Required->ScreenAlignment!=PSA_Square||Required->EmitterLoops!=1
            ||!FMath::IsNearlyEqual(Required->EmitterDuration,FlashLifeSeconds,1.e-6f)||Required->bEmitterDurationUseRange
            ||Required->EmitterDelay!=0.f||Required->bEmitterDelayUseRange||!Required->bKillOnDeactivate||Required->bKillOnCompleted
            ||Required->BurstList.Num()!=0||!IsFloat(Required->SpawnRate,Required,0.f))
            return Reject(Error,TEXT("system required localspace/zero origin/duration/loop/material"));
        if(!IsFloat(Spawn->Rate,Spawn,0.f)||!IsFloat(Spawn->RateScale,Spawn,1.f)||!IsFloat(Spawn->BurstScale,Spawn,1.f)
            ||Spawn->ParticleBurstMethod!=EPBM_Instant||Spawn->bApplyGlobalSpawnRateScale||Spawn->BurstList.Num()!=1
            ||Spawn->BurstList[0].Count!=1||Spawn->BurstList[0].CountLow!=-1||Spawn->BurstList[0].Time!=0.f)
            return Reject(Error,TEXT("system rate0/single instantaneous burst1"));
        const UParticleModuleLifetime* Lifetime=Cast<UParticleModuleLifetime>(LOD->Modules[0]); // 固定顺序使审查可定位，不能仅查存在某一类。
        const UParticleModuleSize* Size=Cast<UParticleModuleSize>(LOD->Modules[1]);
        const UParticleModuleLocation* Location=Cast<UParticleModuleLocation>(LOD->Modules[2]);
        const UParticleModuleVelocity* Velocity=Cast<UParticleModuleVelocity>(LOD->Modules[3]);
        for(const UParticleModule* Module:LOD->Modules)
            if(!Module||Module->GetOuter()!=System||!Module->bEnabled||Module->LODValidity!=1)
                return Reject(Error,TEXT("system authored module ownership/enabled/LOD validity"));
        if(!Lifetime||Lifetime->GetClass()!=UParticleModuleLifetime::StaticClass()||!IsFloat(Lifetime->Lifetime,Lifetime,FlashLifeSeconds)
            ||!Size||Size->GetClass()!=UParticleModuleSize::StaticClass()||!IsVector(Size->StartSize,Size,FVector(FlashSizeCm))
            ||!Location||Location->GetClass()!=UParticleModuleLocation::StaticClass()||!IsVector(Location->StartLocation,Location,FVector::ZeroVector)
            ||Location->DistributeOverNPoints!=0.f||Location->DistributeThreshold!=0.f
            ||!Velocity||Velocity->GetClass()!=UParticleModuleVelocity::StaticClass()||Velocity->bInWorldSpace||Velocity->bApplyOwnerScale
            ||!IsVector(Velocity->StartVelocity,Velocity,FVector::ZeroVector)||!IsFloat(Velocity->StartVelocityRadial,Velocity,0.f))
            return Reject(Error,TEXT("system lifetime/size/initial location/velocity"));
        if(System->WarmupTime!=0.f||System->WarmupTickRate!=0.f||!System->bAutoDeactivate||System->Delay!=0.f||System->bUseDelayRange
            ||!System->bUseFixedRelativeBoundingBox||!System->FixedRelativeBoundingBox.IsValid
            ||!System->FixedRelativeBoundingBox.Min.Equals(FVector(-FlashSizeCm),1.e-6f)||!System->FixedRelativeBoundingBox.Max.Equals(FVector(FlashSizeCm),1.e-6f))
            return Reject(Error,TEXT("system warmup/delay/autodeactivate/fixed local bounds"));
        return true;
    }
}

bool UDemoMuzzleFlashVfxAuthoring::ConfigureFlashMaterial(UMaterial* Material)
{
    DEMO_LOG_STATIC_CALL(Material);
    if(!IsValid(Material)||UMaterialEditingLibrary::GetNumMaterialExpressions(Material)!=0)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_MATERIAL_REJECT requires caller-owned new empty graph")); return false; }
    Material->Modify();
    Material->MaterialDomain=MD_Surface; Material->BlendMode=BLEND_Additive; Material->SetShadingModel(MSM_Unlit);
    Material->TwoSided=true; Material->bDisableDepthTest=false; Material->bUseMaterialAttributes=false;
    UMaterialExpressionTextureCoordinate* UV=Cast<UMaterialExpressionTextureCoordinate>(AddExpression(Material,TEXT("/Script/Engine.MaterialExpressionTextureCoordinate"),-700,-120)); // 原生Sprite UV，无外部贴图。
    UMaterialExpression* Age=AddExpression(Material,TEXT("/Script/Engine.MaterialExpressionParticleRelativeTime"),-700,100); // 无DLL导出的类仅通过反射持有基类。
    UMaterialExpressionCustom* Star=Cast<UMaterialExpressionCustom>(AddExpression(Material,TEXT("/Script/Engine.MaterialExpressionCustom"),-400,-40));
    UMaterialExpressionComponentMask* RGB=Cast<UMaterialExpressionComponentMask>(AddExpression(Material,TEXT("/Script/Engine.MaterialExpressionComponentMask"),-100,-120));
    UMaterialExpressionComponentMask* Alpha=Cast<UMaterialExpressionComponentMask>(AddExpression(Material,TEXT("/Script/Engine.MaterialExpressionComponentMask"),-100,100));
    if(!UV||!Age||!Star||!RGB||!Alpha)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_MATERIAL_REJECT expression_factory")); return false; }
    UV->CoordinateIndex=0; UV->UTiling=1.f; UV->VTiling=1.f; UV->UnMirrorU=false; UV->UnMirrorV=false;
    Star->Code=FlashShaderCode(); Star->Description=TEXT("MuzzleFlash R02 procedural soft star; native particle lifetime"); Star->OutputType=CMOT_Float4;
    Star->Inputs.SetNum(2); Star->Inputs[0].InputName=TEXT("UV"); Star->Inputs[1].InputName=TEXT("Age");
    Star->AdditionalOutputs.Reset(); Star->AdditionalDefines.Reset(); Star->IncludeFilePaths.Reset();
    RGB->R=true; RGB->G=true; RGB->B=true; RGB->A=false;
    Alpha->R=false; Alpha->G=false; Alpha->B=false; Alpha->A=true;
    if(!UMaterialEditingLibrary::ConnectMaterialExpressions(UV,TEXT(""),Star,TEXT("UV"))
        ||!UMaterialEditingLibrary::ConnectMaterialExpressions(Age,TEXT(""),Star,TEXT("Age"))
        ||!UMaterialEditingLibrary::ConnectMaterialExpressions(Star,TEXT(""),RGB,TEXT(""))
        ||!UMaterialEditingLibrary::ConnectMaterialExpressions(Star,TEXT(""),Alpha,TEXT(""))
        ||!UMaterialEditingLibrary::ConnectMaterialProperty(RGB,TEXT(""),MP_EmissiveColor)
        ||!UMaterialEditingLibrary::ConnectMaterialProperty(Alpha,TEXT(""),MP_Opacity))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_MATERIAL_REJECT connection")); return false; }
    bool bNeedsRecompile=false; // 仅本传入新材质用途，无共享材质或全局渲染状态更改。
    if(!UMaterialEditingLibrary::SetMaterialUsage(Material,MATUSAGE_ParticleSprites,bNeedsRecompile))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_MATERIAL_REJECT particle_sprite_usage")); return false; }
    UMaterialEditingLibrary::RecompileMaterial(Material);
    Material->MarkPackageDirty();
    FString Error; // 最终读真实图，失败不保存/自动清空部分图。
    return AuditMaterial(Material,Error);
}

bool UDemoMuzzleFlashVfxAuthoring::ConfigureFlashSystem(UParticleSystem* System,UMaterial* Material)
{
    DEMO_LOG_STATIC_CALL(System);
    FString Error; // 新系统只接受本版已核材质；失败不覆盖既有Emitter或共享素材。
    if(!IsValid(System)||System->Emitters.Num()!=0)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SYSTEM_REJECT requires caller-owned new empty system")); return false; }
    if(!AuditMaterial(Material,Error)) return false;
    System->Modify();
    UParticleSpriteEmitter* Emitter=NewObject<UParticleSpriteEmitter>(System,NAME_None,RF_Transactional); // 模板强持有单CPU发射器，非运行时粒子组件。
    System->Emitters.Add(Emitter);
    Emitter->EmitterName=TEXT("MuzzleFlash"); Emitter->EmitterRenderMode=ERM_Normal; Emitter->bUseLegacySpawningBehavior=false;
    if(Emitter->CreateLODLevel(0,true)!=0||Emitter->LODLevels.Num()!=1)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SYSTEM_REJECT native_CreateLODLevel")); return false; }
    UParticleLODLevel* LOD=Emitter->LODLevels[0]; // CreateLODLevel原生生成Required/Spawn并设置ConvertedModules。
    UParticleModuleRequired* Required=LOD->RequiredModule;
    UParticleModuleSpawn* Spawn=LOD->SpawnModule;
    if(!Required||!Spawn)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SYSTEM_REJECT missing_required_spawn")); return false; }
    Required->Material=Material; Required->bUseLocalSpace=true; Required->EmitterOrigin=FVector::ZeroVector; Required->EmitterRotation=FRotator::ZeroRotator;
    Required->ScreenAlignment=PSA_Square; Required->EmitterDuration=FlashLifeSeconds; Required->EmitterDurationLow=FlashLifeSeconds;
    Required->bEmitterDurationUseRange=false; Required->EmitterLoops=1; Required->EmitterDelay=0.f; Required->EmitterDelayLow=0.f; Required->bEmitterDelayUseRange=false;
    Required->bKillOnDeactivate=true; Required->bKillOnCompleted=false; Required->bEnabled=true;
    Required->ParticleBurstMethod=EPBM_Instant; Required->BurstList.Reset(); SetFloat(Required,Required->SpawnRate,0.f); // 清旧Required兼容生成入口，实际Burst归Spawn模块。
    Spawn->bEnabled=true; Spawn->ParticleBurstMethod=EPBM_Instant; Spawn->bApplyGlobalSpawnRateScale=false;
    SetFloat(Spawn,Spawn->Rate,0.f); SetFloat(Spawn,Spawn->RateScale,1.f); SetFloat(Spawn,Spawn->BurstScale,1.f);
    Spawn->BurstList.Reset(); FParticleBurst Burst; Burst.Count=1; Burst.CountLow=-1; Burst.Time=0.f; Spawn->BurstList.Add(Burst); // 本次瞬发仅1粒，不持续产生烟或光。
    UParticleModuleLifetime* Lifetime=NewObject<UParticleModuleLifetime>(System,NAME_None,RF_Transactional); // 每粒寿命秒，组件完成由原生Cascade判定。
    UParticleModuleSize* Size=NewObject<UParticleModuleSize>(System,NAME_None,RF_Transactional);
    UParticleModuleLocation* Location=NewObject<UParticleModuleLocation>(System,NAME_None,RF_Transactional);
    UParticleModuleVelocity* Velocity=NewObject<UParticleModuleVelocity>(System,NAME_None,RF_Transactional);
    SetFloat(Lifetime,Lifetime->Lifetime,FlashLifeSeconds); SetVector(Size,Size->StartSize,FVector(FlashSizeCm));
    SetVector(Location,Location->StartLocation,FVector::ZeroVector); Location->DistributeOverNPoints=0.f; Location->DistributeThreshold=0.f;
    SetVector(Velocity,Velocity->StartVelocity,FVector::ZeroVector); SetFloat(Velocity,Velocity->StartVelocityRadial,0.f); Velocity->bInWorldSpace=false; Velocity->bApplyOwnerScale=false;
    LOD->Modules={Lifetime,Size,Location,Velocity}; LOD->TypeDataModule=nullptr; LOD->EventGenerator=nullptr;
    for(UParticleModule* Module:LOD->Modules) { Module->bEnabled=true; Module->LODValidity=1; } // 有界4模块，不增加碰撞/灯/噪声等隐性位移。
    System->WarmupTime=0.f; System->WarmupTickRate=0.f; System->bAutoDeactivate=true; System->Delay=0.f; System->DelayLow=0.f; System->bUseDelayRange=false;
    System->bUseFixedRelativeBoundingBox=true; System->FixedRelativeBoundingBox=FBox(FVector(-FlashSizeCm),FVector(FlashSizeCm)); // 覆盖任意相机朝向5cm方Sprite的局部包围。
    System->LODDistances={0.f}; System->LODSettings.SetNum(1); System->LODMethod=PARTICLESYSTEMLODMETHOD_DirectSet;
    System->UpdateAllModuleLists(); System->BuildEmitters(); System->MarkPackageDirty(); // 原生更新缓存/编译模块，保存由Python主程序显式完成。
    return AuditSystem(System,Material,Error);
}

bool UDemoMuzzleFlashVfxAuthoring::AuditFlashSystem(UParticleSystem* System,UMaterial* Material,FString& ReportJson)
{
    DEMO_LOG_STATIC_CALL(System);
    FString Error; // 当前只读审查输出，无失败自动修图或资产写回。
    const bool bPassed=AuditMaterial(Material,Error)&&AuditSystem(System,Material,Error);
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 仅内存JSON；调用者决定证据文件名和保存包白名单。
    Report->SetStringField(TEXT("schema"),TEXT("MuzzleFlashVfxR02"));
    Report->SetBoolField(TEXT("passed"),bPassed);
    Report->SetStringField(TEXT("reason"),Error);
    Report->SetStringField(TEXT("system"),GetPathNameSafe(System)); Report->SetStringField(TEXT("material"),GetPathNameSafe(Material));
    Report->SetNumberField(TEXT("emitter_count"),IsValid(System)?System->Emitters.Num():0);
    Report->SetNumberField(TEXT("material_expression_count"),IsValid(Material)?UMaterialEditingLibrary::GetNumMaterialExpressions(Material):0);
    if(bPassed)
    {
        Report->SetNumberField(TEXT("lod_count"),System->Emitters[0]->LODLevels.Num());
        Report->SetBoolField(TEXT("local_space"),System->Emitters[0]->LODLevels[0]->RequiredModule->bUseLocalSpace);
        Report->SetNumberField(TEXT("burst_count"),System->Emitters[0]->LODLevels[0]->SpawnModule->BurstList[0].Count);
        Report->SetNumberField(TEXT("life_seconds"),FlashLifeSeconds); Report->SetNumberField(TEXT("size_cm"),FlashSizeCm);
        Report->SetStringField(TEXT("material_mode"),TEXT("Surface/Unlit/Additive/normal-depth/procedural-UV-and-particle-age"));
    }
    Report->SetStringField(TEXT("scope"),TEXT("Read-only authored template/graph contract; no socket/RHI/game/notify/network execution"));
    ReportJson.Reset();
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&ReportJson); // 同步生命周期，函数返回前完成串行化。
    if(!FJsonSerializer::Serialize(Report,Writer))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_AUDIT_REJECT JSON serialization")); return false; }
    return bPassed;
}
