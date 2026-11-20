// 职责：转移控制标记的复制表现。依赖引擎基础圆柱与现有M_Solid，所有规则和计时仍归服务器目标协调器。
#include "Game/Objectives/DemoControlPoint.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"

ADemoControlPoint::ADemoControlPoint()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = true;
    Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ControlDisc"));
    SetRootComponent(Disc);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); // 引擎可Cook资源，非运行时生成二进制。
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Game/LevelPrototyping/Materials/M_Solid")); // 缺失仅影响颜色，不阻止必要文字/圆盘。
    if (Cylinder.Succeeded()) Disc->SetStaticMesh(Cylinder.Object);
    if (BaseMaterial.Succeeded()) Disc->SetMaterial(0, BaseMaterial.Object);
    Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Disc->SetCanEverAffectNavigation(false);
    Disc->CastShadow = false;
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ControlLabel"));
    Label->SetupAttachment(Disc);
    Label->SetAbsolute(true, true, true); // 圆盘高度缩放为0.04，文字位置也必须使用世界坐标，避免被压到地面。
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetWorldSize(32.f);
}

void ADemoControlPoint::BeginPlay()
{
    DEMO_LOG_CALL();
    Super::BeginPlay();
    Material = Disc->CreateAndSetMaterialInstanceDynamic(0);
    OnRep_State();
}

void ADemoControlPoint::Configure(int32 Index, float Radius)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || Index < 1 || Index > 3 || !FMath::IsFinite(Radius) || Radius <= 0.f)
    { UE_LOG(LogFPSDemo, Warning, TEXT("CONTROL_MARKER_CONFIG_REJECT authority/value")); return; }
    Number = Index; AreaRadius = Radius;
    OnRep_State();
    ForceNetUpdate();
}

void ADemoControlPoint::UpdatePresentation(EDemoControlStatus Status, float Seconds, bool bCompleted)
{
    DEMO_LOG_TICK();
    if (!HasAuthority()) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("CONTROL_MARKER_UPDATE_REJECT client")); return; }
    const float Rounded = FMath::RoundToFloat(FMath::Max(0.f, Seconds) * 10.f) / 10.f; // 可见读条0.1s精度，不复制小浮点抖动。
    if (State == Status && RemainingSeconds == Rounded && bDone == bCompleted) return;
    State = Status; RemainingSeconds = Rounded; bDone = bCompleted;
    OnRep_State();
    ForceNetUpdate();
}

void ADemoControlPoint::OnRep_State()
{
    DEMO_LOG_TICK();
    Disc->SetWorldScale3D(FVector(AreaRadius / 50.f, AreaRadius / 50.f, .04f));
    Label->SetWorldLocation(GetActorLocation() + FVector(0.f, 0.f, 120.f)); // 固定高于当前点1.2m，所有客户端按复制Actor位置恢复。
    const FLinearColor Color = bDone ? FLinearColor::Green : State == EDemoControlStatus::Capturing ? FLinearColor(.1f, .7f, 1.f)
        : State == EDemoControlStatus::Contested ? FLinearColor::Red : State == EDemoControlStatus::Inactive ? FLinearColor(.15f, .15f, .15f) : FLinearColor(1.f, .7f, .05f); // 纯表现颜色不修改任务状态。
    if (Material) Material->SetVectorParameterValue(TEXT("Color"), Color);
    const TCHAR* StatusText = bDone ? TEXT("DONE") : State == EDemoControlStatus::Capturing ? TEXT("CAPTURING") : State == EDemoControlStatus::Contested ? TEXT("CONTESTED")
        : State == EDemoControlStatus::Relocating ? TEXT("MOVE TO NEXT") : State == EDemoControlStatus::Empty ? TEXT("ENTER ZONE") : TEXT("WAITING"); // 英文占位避免字体资产缺中文字形，正式本地化由UI资产完成。
    Label->SetText(FText::FromString(FString::Printf(TEXT("POINT %d - %s\n%.1fs"), Number, StatusText, RemainingSeconds)));
    Label->SetTextRenderColor(Color.ToFColor(true));
}

void ADemoControlPoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL();
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADemoControlPoint, Number); DOREPLIFETIME(ADemoControlPoint, AreaRadius);
    DOREPLIFETIME(ADemoControlPoint, State); DOREPLIFETIME(ADemoControlPoint, RemainingSeconds); DOREPLIFETIME(ADemoControlPoint, bDone);
}
