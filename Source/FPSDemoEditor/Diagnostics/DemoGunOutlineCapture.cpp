// 职责：实现纯Editor轮廓捕获的ViewOwner桥及反射读回；脚本负责独占输出、正常RHI捕获与瞬态清理。
// 依赖UE5.4.4 SceneCapture默认子对象NewSceneCaptureComponent2D；不运行BeginPlay/交易、不保存Map/DA。
#include "Diagnostics/DemoGunOutlineCapture.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h" // 仅Editor捕获Blendable接口；不改变游戏相机PP资产。

UDemoGunOutlineCaptureComponent2D::UDemoGunOutlineCaptureComponent2D(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DEMO_LOG_CALL();
    bCaptureEveryFrame = false;
    bCaptureOnMovement = false;
}

const AActor* UDemoGunOutlineCaptureComponent2D::GetViewOwner() const
{
    DEMO_LOG_TICK();
    const AActor* CaptureActor = GetOwner(); // 当前Capture拥有该默认子组件；只同步借用，不跨线程缓存Pawn。
    return CaptureActor ? CaptureActor->GetOwner() : nullptr; // SceneCaptureRendering将此结果写入FSceneView::ViewActor。
}

void UDemoGunOutlineCaptureComponent2D::UpdateSceneCaptureContents(FSceneInterface* Scene)
{
    DEMO_LOG_TICK();
    if (AuditBlendable)
    {
        TScriptInterface<IBlendableInterface> Blendable; // 只在当前同步提交栈借用；原生PP设置持有对象引用。
        Blendable.SetObject(AuditBlendable.Get());
        Blendable.SetInterface(Cast<IBlendableInterface>(AuditBlendable.Get()));
        PostProcessSettings.AddBlendable(Blendable, 1.f); // renderer可能先整体替换Settings；此处在Super真正提交前幂等补唯一项。
        PostProcessBlendWeight = 1.f;
    }
    Super::UpdateSceneCaptureContents(Scene);
}

void UDemoGunOutlineCaptureComponent2D::SetAuditBlendable(UMaterialInterface* Material)
{
    DEMO_LOG_CALL();
    if (AuditBlendable == Material) return;
    if (AuditBlendable)
    {
        TScriptInterface<IBlendableInterface> Previous; // 撤销本对象原有专项项，不清空其它Blendables或PostProcessSettings。
        Previous.SetObject(AuditBlendable.Get());
        Previous.SetInterface(Cast<IBlendableInterface>(AuditBlendable.Get()));
        PostProcessSettings.RemoveBlendable(Previous);
    }
    AuditBlendable = Material;
}

UMaterialInterface* UDemoGunOutlineCaptureComponent2D::GetAuditBlendable() const
{
    DEMO_LOG_TICK();
    return AuditBlendable.Get();
}

ADemoGunOutlineCapture::ADemoGunOutlineCapture(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UDemoGunOutlineCaptureComponent2D>(TEXT("NewSceneCaptureComponent2D")))
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false;
    SetReplicates(false); // 本Editor辅助Actor不提供任何网络语义；正式游戏没有此Editor模块类。
}

AActor* ADemoGunOutlineCapture::GetOutlineViewOwner() const
{
    DEMO_LOG_TICK();
    const USceneCaptureComponent2D* Capture = GetCaptureComponent2D(); // 读实际被默认子对象替换的组件，不伪造Owner结果。
    return Capture ? const_cast<AActor*>(Capture->GetViewOwner()) : nullptr; // Blueprint反射需非const对象返回，调用仍只读。
}

void ADemoGunOutlineCapture::SetOutlinePostProcess(UMaterialInterface* Material)
{
    DEMO_LOG_CALL();
    UDemoGunOutlineCaptureComponent2D* Capture = Cast<UDemoGunOutlineCaptureComponent2D>(GetCaptureComponent2D()); // 必须是实际默认子对象替换类。
    if (!Capture) { UE_LOG(LogFPSDemo, Warning, TEXT("GUN_SILHOUETTE_CAPTURE_REJECT missing_override_component")); return; }
    Capture->SetAuditBlendable(Material);
}

UMaterialInterface* ADemoGunOutlineCapture::GetOutlinePostProcess() const
{
    DEMO_LOG_TICK();
    const UDemoGunOutlineCaptureComponent2D* Capture = Cast<UDemoGunOutlineCaptureComponent2D>(GetCaptureComponent2D()); // 非拥有同步借用。
    return Capture ? Capture->GetAuditBlendable() : nullptr;
}

ADemoGunOutlineCapture* ADemoGunOutlineCapture::SpawnForViewer(APawn* Viewer,FVector Location,FRotator Rotation)
{
    DEMO_LOG_STATIC_CALL(Viewer);
    UWorld* World=IsValid(Viewer)?Viewer->GetWorld():nullptr; // 只借用当前Editor Pawn World，不创建/查找Game World。
    if (!World||World->WorldType!=EWorldType::Editor||Location.ContainsNaN()||Rotation.ContainsNaN())
    { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_OUTLINE_CAPTURE_REJECT requires_valid_pure_editor_world")); return nullptr; }
    FActorSpawnParameters Params; // 独立瞬态测试Actor，Owner就是渲染ViewActor；无地图事务/资产写入。
    Params.Owner=Viewer; Params.ObjectFlags=RF_Transient; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<ADemoGunOutlineCapture>(Location,Rotation,Params);
}
