// 职责：Lyra派生Niagara流线的本地生命周期。Cue与复制凭据共用去重入口；依赖实际Body采样和服务器时间，不改变人物材质或骨骼。
#include "VFX/DemoDashTrailComponent.h"
#include "VFX/DemoDashFXProfile.h"
#include "Characters/DemoCharacter.h"
#include "AbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Debug/DemoLog.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h" // FSkeletalMeshLODInfo完整定义；非Editor构建不能依赖编辑器头文件间接引入。
#include "Misc/App.h"

double UDemoDashTrailComponent::FlowServerTime() const
{
    DEMO_LOG_TICK();
    const UWorld* World=GetWorld(); // 借用World/GS，不保留跨World时钟。
    const AGameStateBase* State=World?World->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.;
}

void UDemoDashTrailComponent::RefreshApprovedDash()
{
    DEMO_LOG_CALL();
    if (!HasBegunPlay() || !FlowProfile || FlowProfile->bLegacyAfterimages || GetNetMode()==NM_DedicatedServer || !FApp::CanEverRender()) return;
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 确保PS仍绑定该Avatar，远端无Controller也可播放。
    if (!Pawn || !BoundASC.IsValid() || BoundASC->GetAvatarActor()!=Pawn || Pawn->IsCoopEliminated() || BoundASC->HasMatchingGameplayTag(DemoTags::Dead))
    { StopFlow(true); return; }
    const FDemoDashPresentation& Dash=Pawn->GetDashPresentation(); // 唯一权威方向/时钟来源；Cue早到时等待OnRep补齐。
    if (Dash.ServerStartTime<0.f || Dash.ServerStartTime<=LastFlowStart) return;
    const double Now=FlowServerTime(); // 不从网络到达时重新计算完整持续时间。
    const float Elapsed=FMath::Max(0.f,static_cast<float>(Now-Dash.ServerStartTime));
    if (!FMath::IsFinite(Dash.ForceSeconds) || Dash.ForceSeconds<.1f || Dash.ForceSeconds>.5f || Elapsed>=Dash.ForceSeconds)
    {
        LastFlowStart=Dash.ServerStartTime;
        UE_LOG(LogFPSDemo,Log,TEXT("DASH_FLOW_EXPIRED pawn=%s age=%.3f duration=%.3f"),*Pawn->GetName(),Elapsed,Dash.ForceSeconds);
        return;
    }
    if (!FlowProfile->System || !FlowProfile->Material || !Pawn->GetMesh()->GetSkeletalMeshAsset())
    { UE_LOG(LogFPSDemo,Warning,TEXT("DASH_FLOW_REJECT missing system/material/body")); return; }
    const USkeletalMesh* Mesh=Pawn->GetMesh()->GetSkeletalMeshAsset(); // Niagara WholeMeshLOD=-1固定采样最低LOD，不增加高精LOD的CPU常驻数据。
    const FSkeletalMeshLODInfo* SamplingLOD=Mesh->GetLODInfo(Mesh->GetLODNum()-1); // Mesh拥有，未启用CPU访问的后续外观明确拒绝，避免打包后静默失效。
    if (!SamplingLOD || !SamplingLOD->bAllowCPUAccess)
    { LastFlowStart=Dash.ServerStartTime; UE_LOG(LogFPSDemo,Warning,TEXT("DASH_FLOW_REJECT lowest LOD requires CPU access mesh=%s"),*Mesh->GetPathName()); return; }
    StopFlow(true); // 上一事件已结束才复用同一个组件，绝不把旧流线连到新起点。
    if (!FlowSystem)
    {
        FlowSystem=NewObject<UNiagaraComponent>(Pawn,TEXT("DashFlow")); // 本地装饰对象；无碰撞、不复制、不进入导航。
        Pawn->AddInstanceComponent(FlowSystem);
        FlowSystem->SetAutoActivate(false); FlowSystem->SetAutoDestroy(false);
        FlowSystem->SetupAttachment(Pawn->GetMesh());
        FlowSystem->SetAsset(FlowProfile->System);
        FlowSystem->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        FlowSystem->SetCanEverAffectNavigation(false);
        FlowSystem->SetCastShadow(false);
        FlowSystem->RegisterComponent();
        FlowSystem->AddTickPrerequisiteComponent(this); // PostUpdate之后采样本帧已求值的Body，避免旧肩部位置。
        FlowMaterial=UMaterialInstanceDynamic::Create(FlowProfile->Material,this);
    }
    LastFlowStart=Dash.ServerStartTime;
    FlowEndTime=Dash.ServerStartTime+Dash.ForceSeconds;
    FlowFadeAge=-1.f; FlowBlockedAge=0.f; FlowStartupAge=0.f;
    LastFramePosition=Pawn->GetMesh()->GetComponentLocation();
    UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(FlowSystem,TEXT("User.SkeletalMesh"),Pawn->GetMesh());
    FlowSystem->SetVariableVec3(TEXT("User.DashDirection"),FVector(Dash.WorldDirection));
    FlowSystem->SetVariableFloat(TEXT("User.DashDuration"),Dash.ForceSeconds-Elapsed);
    FlowSystem->SetVariableFloat(TEXT("User.LifetimeMultiplier"),1.f);
    FlowSystem->SetVariableFloat(TEXT("User.TrailWidth"),FMath::Clamp(FlowProfile->Width,.2f,8.f));
    FlowSystem->SetVariableInt(TEXT("User.PointCount"),FMath::Clamp(FlowProfile->PointCount,8,48));
    FlowSystem->SetVariableMaterial(TEXT("User.FlowMaterial"),FlowMaterial);
    FlowMaterial->SetScalarParameterValue(TEXT("FlowFade"),1.f);
    ApplyFlowParameters();
    bFlowActive=true;
    SetComponentTickEnabled(true);
    FlowSystem->Activate(true);
    if (FlowProfile->Sound && Elapsed<.15f) // 很迟的消息不补播起步声音；重复事件由LastFlowStart去重。
    {
        const float Volume=FMath::Clamp(FlowProfile->Volume,0.f,1.f); // 声音只取美术增益，不改变玩家设备音量。
        if (Pawn->IsLocallyControlled()) FlowAudio=UGameplayStatics::SpawnSound2D(this,FlowProfile->Sound,Volume);
        else FlowAudio=UGameplayStatics::SpawnSoundAttached(FlowProfile->Sound,Pawn->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,Volume,1.f,0.f,FlowProfile->Attenuation);
    }
    UE_LOG(LogFPSDemo,Log,TEXT("DASH_FLOW_START pawn=%s local=%d start=%.3f age=%.3f duration=%.3f direction=%s points=%d"),*Pawn->GetName(),Pawn->IsLocallyControlled(),LastFlowStart,Elapsed,Dash.ForceSeconds,*FVector(Dash.WorldDirection).ToString(),FlowProfile->PointCount);
}

void UDemoDashTrailComponent::ApplyFlowParameters()
{
    DEMO_LOG_CALL();
    if (!FlowProfile || !FlowMaterial || !FlowSystem) return;
    const APawn* Pawn=Cast<APawn>(GetOwner()); // 拥有者较弱，其他观察者看到完整强度。
    const float Strength=FMath::Clamp(FlowProfile->Strength,0.f,2.f)*(Pawn && Pawn->IsLocallyControlled()?FMath::Clamp(FlowProfile->OwnerStrength,0.f,1.f):1.f);
    const FLinearColor Color=FMath::IsFinite(TrailColor.R)&&FMath::IsFinite(TrailColor.G)&&FMath::IsFinite(TrailColor.B)&&FMath::IsFinite(TrailColor.A)
        ?FLinearColor(FMath::Clamp(TrailColor.R,0.f,4.f),FMath::Clamp(TrailColor.G,0.f,4.f),FMath::Clamp(TrailColor.B,0.f,4.f),FMath::Clamp(TrailColor.A,0.f,1.f))
        :FLinearColor(.12f,.85f,.72f,.65f); // 配置默认色同样检查，不能向Niagara传非有限值。
    FlowSystem->SetVariableLinearColor(TEXT("User.TeamColor"),Color);
    FlowMaterial->SetScalarParameterValue(TEXT("FlowStrength"),Strength);
    FlowMaterial->SetScalarParameterValue(TEXT("FlowNear"),FMath::Clamp(FlowProfile->CameraNear,0.f,150.f));
    FlowMaterial->SetScalarParameterValue(TEXT("FlowFar"),FMath::Max(FlowProfile->CameraNear+1.f,FMath::Clamp(FlowProfile->CameraFar,1.f,250.f)));
}

void UDemoDashTrailComponent::TickFlow(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (!bFlowActive || !FlowProfile) { SetComponentTickEnabled(false); return; }
    const ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 死亡、换Avatar立即清理，不等待剩余粒子寿命。
    if (!Pawn || Pawn->IsCoopEliminated() || !BoundASC.IsValid() || BoundASC->GetAvatarActor()!=Pawn || BoundASC->HasMatchingGameplayTag(DemoTags::Dead))
    { StopFlow(true); return; }
    const FVector Position=Pawn->GetMesh()->GetComponentLocation(); // 观察者采样CMC平滑后的真实Body位置。
    const float Distance=FVector::Distance(Position,LastFramePosition); // cm，传送即刻断开旧轨迹。
    LastFramePosition=Position;
    if (Distance>ResetDistance) { UE_LOG(LogFPSDemo,Log,TEXT("DASH_FLOW_TELEPORT pawn=%s"),*Pawn->GetName()); StopFlow(true); return; }
    if (FlowFadeAge<0.f)
    {
        FlowStartupAge+=DeltaSeconds;
        FlowBlockedAge=Distance<FMath::Max(.2f,30.f*DeltaSeconds)?FlowBlockedAge+DeltaSeconds:0.f;
        const bool bBlocked=FlowStartupAge>.08f && FlowBlockedAge>=FMath::Clamp(FlowProfile->BlockedGrace,.03f,.15f); // 预留网络起步缓冲，已发生位移才按每帧速度判断。
        if (FlowServerTime()>=FlowEndTime || bBlocked)
        {
            UE_LOG(LogFPSDemo,Log,TEXT("DASH_FLOW_EMISSION_END pawn=%s reason=%s age=%.3f"),*Pawn->GetName(),bBlocked?TEXT("blocked"):TEXT("duration"),static_cast<float>(FlowServerTime()-LastFlowStart));
            StopFlow(false);
        }
    }
    if (FlowFadeAge>=0.f)
    {
        FlowFadeAge+=FMath::Max(0.f,DeltaSeconds);
        const float Fade=1.f-FMath::Clamp(FlowFadeAge/FMath::Clamp(FlowProfile->FadeSeconds,.05f,.3f),0.f,1.f); // 美术收尾时间与GAS窗口独立。
        FlowMaterial->SetScalarParameterValue(TEXT("FlowFade"),Fade*Fade);
        if (Fade<=0.f) StopFlow(true);
    }
}

void UDemoDashTrailComponent::StopFlow(bool bImmediate)
{
    DEMO_LOG_CALL();
    if (bImmediate)
    {
        if (FlowSystem) FlowSystem->DeactivateImmediate();
        if (IsValid(FlowAudio)) FlowAudio->Stop();
        FlowAudio=nullptr;
        if (bFlowActive) UE_LOG(LogFPSDemo,Log,TEXT("DASH_FLOW_CLEAN pawn=%s"),*GetNameSafe(GetOwner()));
        bFlowActive=false; FlowFadeAge=-1.f; SetComponentTickEnabled(false);
    }
    else if (bFlowActive && FlowFadeAge<0.f)
    {
        FlowFadeAge=0.f;
        if (FlowSystem) FlowSystem->Deactivate(); // Complete响应保留既有粒子；MID收尾后强制清理，绝不无限残留。
    }
}
