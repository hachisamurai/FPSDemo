// 职责：只在构造阶段预载命中视觉固定资源。使用：DemoHitFxSettings CDO供World池/敌人组件只读；依赖UE基础Plane与Editor创建的M_FleshMist。
#include "Combat/Feedback/DemoHitFxSettings.h"
#include "Debug/DemoLog.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UDemoHitFxSettings::UDemoHitFxSettings()
{
    DEMO_LOG_CALL();
    ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(TEXT("/Game/VFX/Impacts/M_FleshMist.M_FleshMist"), LOAD_Quiet | LOAD_NoWarn); // CDO强引用预载；首次作者创建前允许缺失，由池一次诊断，不在每击中刷警告。
    MistMaterial = Material.Get();
    ConstructorHelpers::FObjectFinderOptional<UStaticMesh> SprayCard(TEXT("/Engine/BasicShapes/Plane.Plane")); // 100cm双面材质载体，实际轮廓来自透明喷溅纹理。
    MistMesh = SprayCard.Get();
}
