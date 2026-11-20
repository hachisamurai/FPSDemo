// 职责：从单一弹药目录取视觉快照并应用实例MID。使用：发射/复制表现路径调用；所有回退保持纯视觉，不授权伤害或阻断射击。
#include "Weapons/Ammo/DemoAmmoVisualStyle.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Projectiles/DemoProjectileConfig.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Particles/ParticleSystemComponent.h"
#include "Debug/DemoLog.h"

FDemoAmmoVisualStyle DemoAmmoVisuals::Default(uint8 Index)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    FDemoAmmoVisualStyle Result; // 原生普通回退，独立于任何目录实例的合法性。
    switch (Index)
    {
    case 0: break;
    case 1: Result.TracerColor=FLinearColor(12.f,2.5f,.3f,1.f); Result.WidthScale=1.1f; Result.LengthScale=.85f; Result.Shape=1; break;
    case 2: Result.TracerColor=FLinearColor(1.5f,6.f,12.f,1.f); Result.WidthScale=.85f; Result.LengthScale=.9f; Result.FadeScale=1.15f; Result.Shape=2; break;
    case 3: Result.TracerColor=FLinearColor(8.f,2.f,12.f,1.f); Result.WidthScale=.6f; Result.LengthScale=1.5f; Result.FadeScale=1.2f; Result.Shape=3; break;
    default: UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_DEFAULT invalid index=%u; normal fallback"), Index); break;
    }
    Result.MeshTint=Result.TracerColor;
    return Result;
}

bool DemoAmmoVisuals::IsValid(const FDemoAmmoVisualStyle& Style)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    const FLinearColor Colors[]={Style.TracerColor,Style.MeshTint}; // 颜色限定在可维护HDR范围，不做会改变色相的单通道修补。
    for (const FLinearColor& Color:Colors)
        if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B) || !FMath::IsFinite(Color.A)
            || Color.R<0.f || Color.G<0.f || Color.B<0.f || Color.R>64.f || Color.G>64.f || Color.B>64.f
            || FMath::Max3(Color.R,Color.G,Color.B)<=0.f || !FMath::IsNearlyEqual(Color.A,1.f))
        { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_INVALID color must be finite nonzero RGB 0..64 and alpha=1")); return false; }
    const bool bValid=Style.Shape<=3 && FMath::IsFinite(Style.CoreIntensity) && Style.CoreIntensity>=.1f && Style.CoreIntensity<=8.f // 视觉参数独立白名单，不调用任何Gameplay平衡校验。
        && FMath::IsFinite(Style.WidthScale) && Style.WidthScale>=.25f && Style.WidthScale<=2.f
        && FMath::IsFinite(Style.LengthScale) && Style.LengthScale>=.25f && Style.LengthScale<=2.f
        && FMath::IsFinite(Style.FadeScale) && Style.FadeScale>=.25f && Style.FadeScale<=2.f;
    if (!bValid) UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_INVALID shape/core/width/length/fade outside visual ranges"));
    return bValid;
}

FDemoAmmoVisualStyle DemoAmmoVisuals::Resolve(const APawn* Shooter, uint8 AmmoType)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs shooter=%s ammo=%u"), __FUNCTION__, *GetNameSafe(Shooter), AmmoType);
    const UDemoAmmoComponent* Ammo=Shooter ? Shooter->FindComponentByClass<UDemoAmmoComponent>() : nullptr; // 只借用有效Pawn组件，不保持射手生命周期。
    const UDemoAmmoCatalog* Catalog=Ammo ? Ammo->GetCatalog() : nullptr; // 不读当前Selected；在飞切弹不影响冻结AmmoType。
    return ResolveCatalog(Catalog,AmmoType);
}

FDemoAmmoVisualStyle DemoAmmoVisuals::ResolveCatalog(const UDemoAmmoCatalog* Catalog, uint8 AmmoType)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs catalog=%s ammo=%u"), __FUNCTION__,*GetNameSafe(Catalog),AmmoType);
    if (AmmoType>3 || !Catalog || Catalog->HasAnyFlags(RF_ClassDefaultObject) || Catalog->Entries.Num()!=4)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_FALLBACK ammo=%u catalog=%s reason=index/catalog"), AmmoType,*GetNameSafe(Catalog));
        return Default();
    }
    const FDemoAmmoEntry& Entry=Catalog->Entries[AmmoType]; // 只校验视觉项身份，不将平衡/价格验证加入射击路径。
    if (Entry.Id!=UDemoAmmoCatalog::IdAt(AmmoType) || !IsValid(Entry.VisualStyle))
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_FALLBACK ammo=%u catalog=%s reason=style/stable-id"), AmmoType,*GetNameSafe(Catalog));
        return Default();
    }
    return Entry.VisualStyle;
}

FDemoAmmoTracerSettings DemoAmmoVisuals::TracerSettings(const FDemoProjectileConfig& Config, const FDemoAmmoVisualStyle& Style, bool bRicochet)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs ricochet=%d"), __FUNCTION__,bRicochet);
    const FDemoAmmoVisualStyle Safe=IsValid(Style) ? Style : Default(); // 不将坏外观传播为NaN；不读写任何Gameplay字段。
    if (!IsValid(Style)) UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_SETTINGS invalid style; normal fallback"));
    FDemoAmmoTracerSettings Result; // 值快照不保持目录/Config生命周期。
    Result.Color=Safe.TracerColor;
    Result.CoreIntensity=Safe.CoreIntensity*(bRicochet ? Config.RicochetCoreBoost : 1.f);
    Result.Width=FMath::Clamp(Config.TracerWidth*Safe.WidthScale*(bRicochet ? Config.RicochetTracerWidthScale : 1.f),.1f,50.f);
    Result.Length=bRicochet ? 1000.f : FMath::Clamp(Config.TracerLength*Safe.LengthScale,1.f,1000.f); // 出射路径可覆盖800cm默认索敌半径，绝不跨过本段Origin。
    Result.FadeSeconds=FMath::Clamp((bRicochet ? Config.RicochetTracerFadeSeconds : Config.TracerFadeSeconds)*Safe.FadeScale,0.f,1.f);
    return Result;
}

void DemoAmmoVisuals::ApplyMesh(UMeshComponent* Mesh, const FDemoAmmoVisualStyle& Style, UMaterialInterface* VisualMaterial)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs mesh=%s"), __FUNCTION__, *GetNameSafe(Mesh));
    if (!Mesh) { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_MESH absent; skip tint")); return; }
    const FDemoAmmoVisualStyle Safe=IsValid(Style) ? Style : Default(); // 实例入口也防御坏值，绝不写入共享资产。
    for (int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
    {
        UMaterialInstanceDynamic* MID=Mesh->CreateDynamicMaterialInstance(Slot,VisualMaterial); // Mesh拥有MID，随本弹组件销毁，不保活Shooter。
        if (!MID) { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_MESH MID failed slot=%d"),Slot); continue; }
        MID->SetVectorParameterValue(TEXT("TracerColor"),Safe.MeshTint);
        MID->SetVectorParameterValue(TEXT("MeshTint"),Safe.MeshTint);
        MID->SetScalarParameterValue(TEXT("CoreIntensity"),Safe.CoreIntensity);
        MID->SetScalarParameterValue(TEXT("TracerOpacity"),1.f);
        MID->SetScalarParameterValue(TEXT("TracerShape"),Safe.Shape);
    }
}

void DemoAmmoVisuals::ApplyEffect(UFXSystemComponent* Effect, const FDemoAmmoVisualStyle& Style, float CoreBoost)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs effect=%s"), __FUNCTION__, *GetNameSafe(Effect));
    if (!Effect) { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_FX absent; skip tint")); return; }
    const FDemoAmmoVisualStyle Safe=IsValid(Style) ? Style : Default(); // 每实例参数，Niagara可将User参数绑定到Renderer，CPU sprite用下面MID。
    const float Boost=FMath::IsFinite(CoreBoost) ? FMath::Clamp(CoreBoost,1.f,8.f) : 1.f; // 外观增亮只缩放强度，不更换色相。
    Effect->SetColorParameter(TEXT("AmmoColor"),Safe.TracerColor);
    Effect->SetColorParameter(TEXT("User.AmmoColor"),Safe.TracerColor);
    Effect->SetFloatParameter(TEXT("CoreIntensity"),Safe.CoreIntensity*Boost);
    Effect->SetFloatParameter(TEXT("TracerShape"),Safe.Shape);
    for (int32 Slot=0;Slot<Effect->GetNumMaterials();++Slot)
    {
        UMaterialInstanceDynamic* MID=Effect->CreateDynamicMaterialInstance(Slot); // 组件拥有，不共享其他弹丸或原系统材质。
        if (!MID) { UE_LOG(LogFPSDemo, Warning, TEXT("AMMO_VISUAL_FX MID failed slot=%d"),Slot); continue; }
        MID->SetVectorParameterValue(TEXT("TracerColor"),Safe.TracerColor);
        MID->SetScalarParameterValue(TEXT("CoreIntensity"),Safe.CoreIntensity*Boost);
        MID->SetScalarParameterValue(TEXT("TracerShape"),Safe.Shape);
        MID->SetScalarParameterValue(TEXT("TracerOpacity"),1.f);
    }
}
