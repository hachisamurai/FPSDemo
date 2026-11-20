// 职责：通过UE5.4.4原生Cascade接口创建无碰撞单次子弹接触/弹射短闪模板；由Python外层备份、图作者和定向保存。
// 使用：仅Editor显式新建空系统后调用；CPU Sprite 150ms/18cm/1粒/本地+X 60cm/s方向短闪，外层实例MID赋弹种色与形状。
// 参考：本项目MuzzleFlash R02和本地Engine CreateLODLevel/分布接口；Epic引擎许可，无外部代码/素材或插件。
// 边界：只读审查不修复/保存；本模板不包含Gameplay、碰撞或网络状态，生命周期归运行时清理Actor。
#include "Diagnostics/DemoBulletVfxAuthoring.h"
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
    constexpr float FlashLifeSeconds=.15f; // 粒子/Emitter单次寿命秒；接触后150ms淡灭，实际调度/观感待游戏验收。
    constexpr float FlashBoundsCm=32.f; // 半边长cm：18cm方Sprite半对角约12.73cm + 最大9cm位移，小于32cm。
    constexpr float FlashSpeedCmPerSecond=60.f; // 纯表现粒子沿接触Actor本地+X运动，150ms最大9cm；不是实体弹速度。
    constexpr float FlashSizeCm=18.f; // 世界接触点Sprite作者尺寸cm；不继承本人近枪桥缩放。

    /** Error由当前同步审查借用；所有拒绝明确记录原因，不改受审资产或默认门限。 */
    bool Reject(FString& Error,const TCHAR* Reason)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        Error=Reason;
        UE_LOG(LogFPSDemo,Warning,TEXT("BULLET_VFX_AUTHOR_REJECT %s"),Reason);
        return false;
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

    /** 只读核作者材质渲染边界；Python新进程另核每个图连接/参数/代码，桥不补图、不保存。 */
    bool AuditMaterial(UMaterial* Material,FString& Error)
    {
        DEMO_LOG_STATIC_CALL(Material);
        if(!IsValid(Material)||Material->MaterialDomain!=MD_Surface||Material->BlendMode!=BLEND_Additive
            ||!Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit)||!Material->TwoSided||Material->bDisableDepthTest
            ||Material->bUseMaterialAttributes||!UMaterialEditingLibrary::HasMaterialUsage(Material,MATUSAGE_ParticleSprites)
            ||!UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_EmissiveColor)
            ||!UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material,MP_Opacity))
            return Reject(Error,TEXT("material domain/blend/shading/depth/usage/outputs"));
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
            ||!Required->EmitterRotation.IsNearlyZero(1.e-6f)||Required->ScreenAlignment!=PSA_Velocity||Required->EmitterLoops!=1
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
            ||!IsVector(Velocity->StartVelocity,Velocity,FVector(FlashSpeedCmPerSecond,0.f,0.f))||!IsFloat(Velocity->StartVelocityRadial,Velocity,0.f))
            return Reject(Error,TEXT("system lifetime/size/initial location/velocity"));
        if(System->WarmupTime!=0.f||System->WarmupTickRate!=0.f||!System->bAutoDeactivate||System->Delay!=0.f||System->bUseDelayRange
            ||!System->bUseFixedRelativeBoundingBox||!System->FixedRelativeBoundingBox.IsValid
            ||!System->FixedRelativeBoundingBox.Min.Equals(FVector(-FlashBoundsCm),1.e-6f)||!System->FixedRelativeBoundingBox.Max.Equals(FVector(FlashBoundsCm),1.e-6f))
            return Reject(Error,TEXT("system warmup/delay/autodeactivate/fixed local bounds"));
        return true;
    }
}

bool UDemoBulletVfxAuthoring::ConfigureContactSystem(UParticleSystem* System,UMaterial* Material)
{
    DEMO_LOG_STATIC_CALL(System);
    FString Error; // 新系统只接受本版已核材质；失败不覆盖既有Emitter或共享素材。
    if(!IsValid(System)||System->Emitters.Num()!=0)
    { UE_LOG(LogFPSDemo,Warning,TEXT("BULLET_VFX_SYSTEM_REJECT requires caller-owned new empty system")); return false; }
    if(!AuditMaterial(Material,Error)) return false;
    System->Modify();
    UParticleSpriteEmitter* Emitter=NewObject<UParticleSpriteEmitter>(System,NAME_None,RF_Transactional); // 模板强持有单CPU发射器，非运行时粒子组件。
    System->Emitters.Add(Emitter);
    Emitter->EmitterName=TEXT("BulletContact"); Emitter->EmitterRenderMode=ERM_Normal; Emitter->bUseLegacySpawningBehavior=false;
    if(Emitter->CreateLODLevel(0,true)!=0||Emitter->LODLevels.Num()!=1)
    { UE_LOG(LogFPSDemo,Warning,TEXT("BULLET_VFX_SYSTEM_REJECT native_CreateLODLevel")); return false; }
    UParticleLODLevel* LOD=Emitter->LODLevels[0]; // CreateLODLevel原生生成Required/Spawn并设置ConvertedModules。
    UParticleModuleRequired* Required=LOD->RequiredModule;
    UParticleModuleSpawn* Spawn=LOD->SpawnModule;
    if(!Required||!Spawn)
    { UE_LOG(LogFPSDemo,Warning,TEXT("BULLET_VFX_SYSTEM_REJECT missing_required_spawn")); return false; }
    Required->Material=Material; Required->bUseLocalSpace=true; Required->EmitterOrigin=FVector::ZeroVector; Required->EmitterRotation=FRotator::ZeroRotator;
    Required->ScreenAlignment=PSA_Velocity; Required->EmitterDuration=FlashLifeSeconds; Required->EmitterDurationLow=FlashLifeSeconds;
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
    SetVector(Velocity,Velocity->StartVelocity,FVector(FlashSpeedCmPerSecond,0.f,0.f)); SetFloat(Velocity,Velocity->StartVelocityRadial,0.f); Velocity->bInWorldSpace=false; Velocity->bApplyOwnerScale=false;
    LOD->Modules={Lifetime,Size,Location,Velocity}; LOD->TypeDataModule=nullptr; LOD->EventGenerator=nullptr;
    for(UParticleModule* Module:LOD->Modules) { Module->bEnabled=true; Module->LODValidity=1; } // 有界4模块，不增加碰撞/灯/噪声等隐性位移。
    System->WarmupTime=0.f; System->WarmupTickRate=0.f; System->bAutoDeactivate=true; System->Delay=0.f; System->DelayLow=0.f; System->bUseDelayRange=false;
    System->bUseFixedRelativeBoundingBox=true; System->FixedRelativeBoundingBox=FBox(FVector(-FlashBoundsCm),FVector(FlashBoundsCm)); // 覆盖任意相机朝向18cm方Sprite半对角及全寿命9cm方向位移。
    System->LODDistances={0.f}; System->LODSettings.SetNum(1); System->LODMethod=PARTICLESYSTEMLODMETHOD_DirectSet;
    System->UpdateAllModuleLists(); System->BuildEmitters(); System->MarkPackageDirty(); // 原生更新缓存/编译模块，保存由Python主程序显式完成。
    return AuditSystem(System,Material,Error);
}

bool UDemoBulletVfxAuthoring::AuditContactSystem(UParticleSystem* System,UMaterial* Material,FString& ReportJson)
{
    DEMO_LOG_STATIC_CALL(System);
    FString Error; // 当前只读审查输出，无失败自动修图或资产写回。
    const bool bPassed=AuditMaterial(Material,Error)&&AuditSystem(System,Material,Error);
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>(); // 仅内存JSON；调用者决定证据文件名和保存包白名单。
    Report->SetStringField(TEXT("schema"),TEXT("BulletVfx20261119"));
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
        Report->SetNumberField(TEXT("fixed_bounds_half_extent_cm"),FlashBoundsCm);
        Report->SetStringField(TEXT("screen_alignment"),TEXT("PSA_Velocity"));
        Report->SetNumberField(TEXT("local_velocity_x_cm_per_second"),FlashSpeedCmPerSecond);
        Report->SetNumberField(TEXT("maximum_particle_travel_cm"),FlashSpeedCmPerSecond*FlashLifeSeconds);
        Report->SetNumberField(TEXT("life_seconds"),FlashLifeSeconds); Report->SetNumberField(TEXT("size_cm"),FlashSizeCm);
        Report->SetStringField(TEXT("material_mode"),TEXT("Surface/Unlit/Additive/normal-depth/procedural-UV-and-particle-age"));
    }
    Report->SetStringField(TEXT("scope"),TEXT("Read-only authored template/graph contract; no socket/RHI/game/notify/network execution"));
    ReportJson.Reset();
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&ReportJson); // 同步生命周期，函数返回前完成串行化。
    if(!FJsonSerializer::Serialize(Report,Writer))
    { UE_LOG(LogFPSDemo,Warning,TEXT("BULLET_VFX_AUDIT_REJECT JSON serialization")); return false; }
    return bPassed;
}
