// 职责：Hub bounded Profile运行消费与冲突安全恢复；使用原Volume Brush/priority，不写CVar、灯、描边、存档或网络。
#include "World/Rooms/DemoHubAtmosphereComponent.h"
#include "World/Rooms/DemoRoomAtmosphereProfile.h"
#include "Interaction/HubShops/DemoHubLightingSettings.h"
#include "Components/BrushComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Debug/DemoLog.h"

UDemoHubAtmosphereComponent::UDemoHubAtmosphereComponent()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; SetIsReplicatedByDefault(false);
}

void UDemoHubAtmosphereComponent::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay(); ApplyHubAtmosphere();
}

void UDemoHubAtmosphereComponent::ApplyHubAtmosphere()
{
    DEMO_LOG_CALL();
    UWorld* World=GetWorld(); // 只借所属World，不能从全局/其他PIE解析Hub。
    if (!World || !World->IsGameWorld() || World->GetNetMode()==NM_DedicatedServer || !GetOwner() || GetOwner()->GetLevel()!=World->PersistentLevel)
    { UE_LOG(LogFPSDemo,Log,TEXT("HUB_ATMO_SKIP reason=NotPersistentRenderGameWorld")); return; }
    const UDemoHubLightingSettings* Settings=GetDefault<UDemoHubLightingSettings>(); // 本地安装目录，不接受复制资源路径。
    if (Settings->HubAtmosphereProfile.IsNull())
    { UE_LOG(LogFPSDemo,Log,TEXT("HUB_ATMO_SKIP reason=UnconfiguredKeepOriginal")); return; }
    if (bOwnsFields) { UE_LOG(LogFPSDemo,Log,TEXT("HUB_ATMO_SKIP reason=AlreadyApplied")); return; }
    int32 Consumers=0; // 包含已注册、尚未BeginPlay的同World组件，拒绝两个所有者争写。
    for (TObjectIterator<UDemoHubAtmosphereComponent> It; It; ++It)
        if (It->GetWorld()==World && It->IsRegistered()) ++Consumers;
    const FSoftObjectPath Path=Settings->HubAtmosphereProfile.ToSoftObjectPath(); // 精确安装资产，无子对象或任意外部路径。
    if (Consumers!=1 || Settings->HubVolumeObjectName.IsNone() || !Path.IsValid() || !Path.GetLongPackageName().StartsWith(TEXT("/Game/")) || !Path.GetSubPathString().IsEmpty())
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ATMO_REJECT reason=ConsumerOrConfiguredIdentity consumers=%d"),Consumers); return; }
    APostProcessVolume* Volume=nullptr; int32 Matches=0; // tag与名字均需唯一；绝不选第一个PP或改流送Level。
    for (TActorIterator<APostProcessVolume> It(World); It; ++It)
        if (It->GetLevel()==World->PersistentLevel && It->ActorHasTag(TEXT("Demo.Hub.ExposureTransition"))) { ++Matches; Volume=*It; }
    if (Matches!=1 || !Volume || Volume->GetClass()!=APostProcessVolume::StaticClass() || Volume->GetFName()!=Settings->HubVolumeObjectName
        || !Volume->GetBrushComponent() || Volume->bUnbound || !Volume->bEnabled || !FMath::IsNearlyEqual(Volume->Priority,10.f)
        || !FMath::IsNearlyEqual(Volume->BlendRadius,300.f) || !FMath::IsNearlyEqual(Volume->BlendWeight,1.f)
        || !Volume->Settings.bOverride_AutoExposureSpeedDown || !FMath::IsNearlyEqual(Volume->Settings.AutoExposureSpeedDown,6.f)
        || Settings->HubVolumeCenterCm.ContainsNaN() || Settings->HubVolumeExtentCm.ContainsNaN() || Settings->HubVolumeExtentCm.GetMin()<=0
        || !Volume->GetBrushComponent()->Bounds.Origin.Equals(Settings->HubVolumeCenterCm,0.05)
        || !Volume->GetBrushComponent()->Bounds.BoxExtent.Equals(Settings->HubVolumeExtentCm,0.05))
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ATMO_REJECT reason=OriginalVolumeIdentityOrBrushDrift matches=%d volume=%s"),Matches,*GetPathNameSafe(Volume)); return; }
    Profile=Settings->HubAtmosphereProfile.LoadSynchronous(); // GT一次加载；UPROPERTY持有；不在每帧/相机切换时重载。
    FString Error; // 同步数据失败原因，不改变原PP或玩法。
    if (!Profile || Profile->LocalFog.bEnabled || (Profile->bEnabled && !Profile->BuildSettings(Applied,Error)))
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ATMO_REJECT reason=ProfileInvalidOrForbiddenHubFog asset=%s detail=%s"),*Path.ToString(),*Error); Profile=nullptr; return; }
    if (!Profile->bEnabled) { UE_LOG(LogFPSDemo,Log,TEXT("HUB_ATMO_SKIP reason=ProfileDisabledKeepOriginal")); Profile=nullptr; return; }
    if (Applied.bOverride_AutoExposureSpeedDown && !FMath::IsNearlyEqual(Applied.AutoExposureSpeedDown,6.f))
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ATMO_REJECT reason=ProtectedHubSpeedDown6 profile=%s"),*Profile->GetPathName()); Profile=nullptr; return; }
    Baseline=Volume->Settings; // 原对象快照，但恢复只认Applied中的字段；不改Brush/优先级/混合/其他Blendable。
    if (!UDemoRoomAtmosphereProfile::ApplyOwnedOverrides(Applied,Volume->Settings)) { Profile=nullptr; return; }
    Target=Volume; bOwnsFields=true;
    UE_LOG(LogFPSDemo,Log,TEXT("HUB_ATMO_READY volume=%s profile=%s center=%s extent=%s priority=10 blend_cm=300 room_spatial_fields_ignored=1 interior_bias_layer_unchanged=1"),
        *Volume->GetPathName(),*Profile->GetPathName(),*Volume->GetBrushComponent()->Bounds.Origin.ToString(),*Volume->GetBrushComponent()->Bounds.BoxExtent.ToString());
}

void UDemoHubAtmosphereComponent::RestoreHubAtmosphere()
{
    DEMO_LOG_CALL();
    if (!bOwnsFields) return;
    const bool bRestored=!Target.IsValid() || UDemoRoomAtmosphereProfile::RestoreOwnedOverrides(Applied,Baseline,Target->Settings); // 原件死亡无须恢复；有效对象只恢复未冲突字段。
    UE_LOG(LogFPSDemo,Log,TEXT("HUB_ATMO_RESTORE volume=%s all_owned_fields_restored=%d no_global_parameters_written=1"),*GetPathNameSafe(Target.Get()),bRestored);
    bOwnsFields=false; Target.Reset(); Profile=nullptr; Applied=FPostProcessSettings(); Baseline=FPostProcessSettings();
}

void UDemoHubAtmosphereComponent::EndPlay(EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); RestoreHubAtmosphere(); Super::EndPlay(Reason);
}
