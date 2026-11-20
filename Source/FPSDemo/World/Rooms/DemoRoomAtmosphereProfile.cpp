// 职责：验证房间Profile并只拷贝指定PP白名单；Anchor同步消费，不加载地图/写CVar，不允许混入描边Blendable。
#include "World/Rooms/DemoRoomAtmosphereProfile.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "UObject/UnrealType.h"

UDemoRoomAtmosphereProfile::UDemoRoomAtmosphereProfile()
{
    DEMO_LOG_CALL();
    // 候选保留已接受的四字段曝光；仅克制Bloom/Vignette默认勾选，调色和Lumen由作者显式开启。
    Overrides.bOverride_AutoExposureMinBrightness=true; Overrides.AutoExposureMinBrightness=3.5f;
    Overrides.bOverride_AutoExposureMaxBrightness=true; Overrides.AutoExposureMaxBrightness=8.f;
    Overrides.bOverride_AutoExposureSpeedUp=true; Overrides.AutoExposureSpeedUp=6.f;
    Overrides.bOverride_AutoExposureSpeedDown=true; Overrides.AutoExposureSpeedDown=6.f;
    Overrides.bOverride_BloomIntensity=true; Overrides.BloomIntensity=0.15f;
    Overrides.bOverride_BloomThreshold=true; Overrides.BloomThreshold=3.f;
    Overrides.bOverride_VignetteIntensity=true; Overrides.VignetteIntensity=0.12f;
}

bool UDemoRoomAtmosphereProfile::BuildSettings(FPostProcessSettings& Out, FString& Error) const
{
    DEMO_LOG_CALL(); Error.Reset();
    FPostProcessSettings Candidate; // 全新值，任何失败都不输出部分覆写。
    TSet<FName> Allowed; // 当前有限白名单；在此处构造，不跨帧缓存作者数据。
    // 每个宏只展开字段声明与同步验证，不引入回调；上下界是本项目艺术/性能合同，不是UE硬件承诺。
#define ROOM_FLOAT(Field, Low, High) \
    Allowed.Add(FName(TEXT("bOverride_" #Field))); \
    if (Overrides.bOverride_##Field) { \
        if (!FMath::IsFinite(Overrides.Field) || Overrides.Field < Low || Overrides.Field > High) { \
            Error=TEXT(#Field " outside finite room budget"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT profile=%s reason=%s"),*GetName(),*Error); return false; } \
        Candidate.bOverride_##Field=true; Candidate.Field=Overrides.Field; }
    ROOM_FLOAT(AutoExposureMinBrightness,-10.f,20.f)
    ROOM_FLOAT(AutoExposureMaxBrightness,-10.f,20.f)
    ROOM_FLOAT(AutoExposureBias,-5.f,5.f)
    ROOM_FLOAT(AutoExposureSpeedUp,0.1f,20.f)
    ROOM_FLOAT(AutoExposureSpeedDown,0.1f,20.f)
    ROOM_FLOAT(LocalExposureHighlightContrastScale,0.f,1.f)
    ROOM_FLOAT(LocalExposureShadowContrastScale,0.f,1.f)
    ROOM_FLOAT(LocalExposureDetailStrength,0.f,2.f)
    ROOM_FLOAT(BloomIntensity,0.f,0.5f)
    ROOM_FLOAT(BloomThreshold,0.f,20.f)
    ROOM_FLOAT(WhiteTemp,1500.f,15000.f)
    ROOM_FLOAT(WhiteTint,-1.f,1.f)
    ROOM_FLOAT(VignetteIntensity,0.f,0.25f)
    ROOM_FLOAT(LumenSceneLightingQuality,0.25f,2.f)
    ROOM_FLOAT(LumenFinalGatherQuality,0.25f,2.f)
    ROOM_FLOAT(LumenReflectionQuality,0.25f,2.f)
    ROOM_FLOAT(LumenMaxTraceDistance,100.f,50000.f)
    ROOM_FLOAT(LumenSkylightLeaking,0.f,0.02f)
    ROOM_FLOAT(LumenFullSkylightLeakingDistance,0.1f,2000.f)
#undef ROOM_FLOAT
    // RGB/主通道限0.8..1.2，保持预警颜色可读；三色区各支持Saturation/Gain微调。
#define ROOM_VECTOR(Field) \
    Allowed.Add(FName(TEXT("bOverride_" #Field))); \
    if (Overrides.bOverride_##Field) { \
        for (int32 Index=0; Index<4; ++Index) { \
            if (!FMath::IsFinite(Overrides.Field[Index]) || Overrides.Field[Index]<0.8 || Overrides.Field[Index]>1.2) { \
                Error=TEXT(#Field " outside [0.8,1.2]"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=%s"),*Error); return false; } } \
        Candidate.bOverride_##Field=true; Candidate.Field=Overrides.Field; }
    ROOM_VECTOR(ColorSaturationShadows)
    ROOM_VECTOR(ColorSaturationMidtones)
    ROOM_VECTOR(ColorSaturationHighlights)
    ROOM_VECTOR(ColorGainShadows)
    ROOM_VECTOR(ColorGainMidtones)
    ROOM_VECTOR(ColorGainHighlights)
#undef ROOM_VECTOR
    Allowed.Add(TEXT("bOverride_AutoExposureMethod"));
    if (Overrides.bOverride_AutoExposureMethod)
    {
        if (Overrides.AutoExposureMethod>=AEM_MAX)
        { Error=TEXT("Invalid metering mode"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=%s"),*Error); return false; }
        Candidate.bOverride_AutoExposureMethod=true; Candidate.AutoExposureMethod=Overrides.AutoExposureMethod;
    }
    // 反射只在一次建档检查：未知启用位拒绝，避免作者误以为任意PP字段会生效。
    for (TFieldIterator<FBoolProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
    {
        const FBoolProperty* Property=*It; // 反射拥有，仅当前栈借用。
        if (Property->GetName().StartsWith(TEXT("bOverride_")) && Property->GetPropertyValue_InContainer(&Overrides) && !Allowed.Contains(Property->GetFName()))
        { Error=FString::Printf(TEXT("Unsupported override %s"),*Property->GetName()); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=%s"),*Error); return false; }
    }
    if (Overrides.bOverride_AutoExposureMinBrightness!=Overrides.bOverride_AutoExposureMaxBrightness)
    { Error=TEXT("MinEV100/MaxEV100 overrides must be enabled together"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=%s"),*Error); return false; }
    if (!Overrides.WeightedBlendables.Array.IsEmpty()
        || (Overrides.bOverride_AutoExposureMinBrightness && Overrides.bOverride_AutoExposureMaxBrightness && Overrides.AutoExposureMinBrightness>Overrides.AutoExposureMaxBrightness)
        || !FMath::IsFinite(BoundsMarginCm) || BoundsMarginCm<0 || BoundsMarginCm>2000
        || !FMath::IsFinite(BlendRadiusCm) || BlendRadiusCm<0 || BlendRadiusCm>2000
        || !FMath::IsFinite(Priority) || Priority<0 || Priority>9)
    { Error=TEXT("Invalid EV ordering, bounds, priority or forbidden blendables"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=%s"),*Error); return false; }
    const FDemoRoomLocalFogSettings& Fog=LocalFog; // 仅启用时验证，球体包含另由真实房间Bounds核对。
    if (Fog.bEnabled && (Fog.CenterOffsetCm.ContainsNaN() || !FMath::IsFinite(Fog.RadiusCm) || Fog.RadiusCm<10 || Fog.RadiusCm>3000
        || !FMath::IsFinite(Fog.RadialExtinction) || Fog.RadialExtinction<0 || Fog.RadialExtinction>0.2f
        || !FMath::IsFinite(Fog.HeightExtinction) || Fog.HeightExtinction<0 || Fog.HeightExtinction>0.2f
        || !FMath::IsFinite(Fog.HeightFalloff) || Fog.HeightFalloff<1 || Fog.HeightFalloff>5000
        || !FMath::IsFinite(Fog.HeightOffset) || FMath::Abs(Fog.HeightOffset)>2
        || !FMath::IsFinite(Fog.PhaseG) || Fog.PhaseG<0 || Fog.PhaseG>0.9f
        || !FMath::IsFinite(Fog.Albedo.R) || !FMath::IsFinite(Fog.Albedo.G) || !FMath::IsFinite(Fog.Albedo.B)
        || Fog.Albedo.R<0 || Fog.Albedo.R>1 || Fog.Albedo.G<0 || Fog.Albedo.G>1 || Fog.Albedo.B<0 || Fog.Albedo.B>1))
    { Error=TEXT("Invalid local fog finite range"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=%s"),*Error); return false; }
    Out=Candidate; return true;
}

namespace
{
/** GT字段认领白名单；BuildSettings决定值/勾选是否合法，此处拒绝任意反射字段注入。 */
bool IsOwnedAtmosphereField(FName Name)
{
    DEMO_LOG_STATIC_TICK(nullptr);
    static const TSet<FName> Names = { TEXT("AutoExposureMethod"), TEXT("AutoExposureMinBrightness"), TEXT("AutoExposureMaxBrightness"), TEXT("AutoExposureBias"), TEXT("AutoExposureSpeedUp"), TEXT("AutoExposureSpeedDown"), TEXT("LocalExposureHighlightContrastScale"), TEXT("LocalExposureShadowContrastScale"), TEXT("LocalExposureDetailStrength"), TEXT("BloomIntensity"), TEXT("BloomThreshold"), TEXT("WhiteTemp"), TEXT("WhiteTint"), TEXT("VignetteIntensity"), TEXT("LumenSceneLightingQuality"), TEXT("LumenFinalGatherQuality"), TEXT("LumenReflectionQuality"), TEXT("LumenMaxTraceDistance"), TEXT("LumenSkylightLeaking"), TEXT("LumenFullSkylightLeakingDistance"), TEXT("ColorSaturationShadows"), TEXT("ColorSaturationMidtones"), TEXT("ColorSaturationHighlights"), TEXT("ColorGainShadows"), TEXT("ColorGainMidtones"), TEXT("ColorGainHighlights") }; // 编译期固定的纯值字段名，不保存UObject/反射引用。
    return Names.Contains(Name);
}
}

bool UDemoRoomAtmosphereProfile::ApplyOwnedOverrides(const FPostProcessSettings& Validated, FPostProcessSettings& Target)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    // 先完整验证字段集合再写入，避免未知override导致半份应用。参数必须来自同版BuildSettings输出。
    for (TFieldIterator<FBoolProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
    {
        const FBoolProperty* Flag=*It; // 引擎反射拥有，同步栈内借用。
        const FString Name=Flag->GetName(); // override对应同结构值字段名。
        if (Name.StartsWith(TEXT("bOverride_")) && Flag->GetPropertyValue_InContainer(&Validated))
        {
            const FName ValueName(*Name.RightChop(10)); // 首轮同时核值字段存在，失败时不应用前面的部分字段。
            if (!IsOwnedAtmosphereField(ValueName) || !FindFProperty<FProperty>(FPostProcessSettings::StaticStruct(),ValueName))
            { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ATMO_REJECT reason=UnvalidatedField field=%s"),*Name); return false; }
        }
    }
    if (!Validated.WeightedBlendables.Array.IsEmpty())
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ATMO_REJECT reason=ForbiddenBlendables")); return false; }
    for (TFieldIterator<FBoolProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
    {
        const FBoolProperty* Flag=*It;
        const FString Name=Flag->GetName();
        if (!Name.StartsWith(TEXT("bOverride_")) || !Flag->GetPropertyValue_InContainer(&Validated)) continue;
        const FProperty* Value=FindFProperty<FProperty>(FPostProcessSettings::StaticStruct(),FName(*Name.RightChop(10))); // 同结构的已核白名单值。
        if (!Value) { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ATMO_REJECT reason=MissingValueProperty field=%s"),*Name); return false; }
        Value->CopyCompleteValue_InContainer(&Target,&Validated);
        Flag->SetPropertyValue_InContainer(&Target,true);
    }
    return true;
}

bool UDemoRoomAtmosphereProfile::RestoreOwnedOverrides(const FPostProcessSettings& Applied, const FPostProcessSettings& Baseline, FPostProcessSettings& Target)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    bool bNoConflict=true; // 逐字段恢复可独立完成；冲突字段保持外部当前值，不整份覆盖。
    for (TFieldIterator<FBoolProperty> It(FPostProcessSettings::StaticStruct()); It; ++It)
    {
        const FBoolProperty* Flag=*It;
        const FString Name=Flag->GetName();
        if (!Name.StartsWith(TEXT("bOverride_")) || !Flag->GetPropertyValue_InContainer(&Applied)) continue;
        const FName ValueName(*Name.RightChop(10)); // 不接受外部指定的任意PP字段。
        const FProperty* Value=FindFProperty<FProperty>(FPostProcessSettings::StaticStruct(),ValueName);
        if (!IsOwnedAtmosphereField(ValueName) || !Value || !Flag->GetPropertyValue_InContainer(&Target)
            || !Value->Identical_InContainer(&Target,&Applied))
        { bNoConflict=false; UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ATMO_RESTORE reason=ExternalFieldConflict field=%s action=KeepCurrent"),*Name); continue; }
        Value->CopyCompleteValue_InContainer(&Target,&Baseline);
        Flag->SetPropertyValue_InContainer(&Target,Flag->GetPropertyValue_InContainer(&Baseline));
    }
    return bNoConflict;
}
