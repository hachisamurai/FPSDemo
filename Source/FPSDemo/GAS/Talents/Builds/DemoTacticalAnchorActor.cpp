// 职责：B06锚点纯表现复制实现。充能柱高度/颜色表达进度，规则与寿命全部由策略裁决，退休寿命只负责释放一次性视觉Actor。
#include "GAS/Talents/Builds/DemoTacticalAnchorActor.h"
#include "GAS/Talents/Builds/DemoAnchorBuildRuntime.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Player/DemoPlayerState.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"

ADemoTacticalAnchorActor::ADemoTacticalAnchorActor()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = true; // 小队最多四个活跃锚点，所有参战者必须看见共享区域；不携带私人经济信息。
    SetReplicateMovement(false); // 只在生成位置固定存在；迁移先撤旧Actor再创建新Actor。
    NetUpdateFrequency = 4.f;
    Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AnchorDisc"));
    SetRootComponent(Disc);
    ChargeColumn = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AnchorCharge"));
    ChargeColumn->SetupAttachment(Disc);
    ChargeColumn->SetAbsolute(false, false, true); // 地盘的半径/厚度缩放不能把充能柱压扁。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); // 复用引擎可Cook基础网格。
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Game/LevelPrototyping/Materials/M_Solid")); // 复用项目颜色参数材质，缺失可保留引擎默认网格。
    for (UStaticMeshComponent* Mesh : { Disc.Get(), ChargeColumn.Get() }) // 两个本Actor默认子对象，只配置渲染，不参与物理/导航。
    {
        if (Cylinder.Succeeded()) Mesh->SetStaticMesh(Cylinder.Object);
        if (BaseMaterial.Succeeded()) Mesh->SetMaterial(0, BaseMaterial.Object);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->CastShadow = false;
    }
}

void ADemoTacticalAnchorActor::BeginPlay()
{
    DEMO_LOG_CALL();
    Super::BeginPlay();
    DiscMaterial = Disc->CreateAndSetMaterialInstanceDynamic(0);
    ColumnMaterial = ChargeColumn->CreateAndSetMaterialInstanceDynamic(0);
    OnRep_Presentation();
}

void ADemoTacticalAnchorActor::Configure(UDemoAnchorBuildRuntime* Runtime, ADemoPlayerState* OwnerPS, float Radius, float EndsAt)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !IsValid(Runtime) || !IsValid(OwnerPS) || !FMath::IsFinite(Radius) || Radius <= 0.f || !FMath::IsFinite(EndsAt))
    { UE_LOG(LogFPSDemo, Warning, TEXT("BUILD_ANCHOR_VISUAL_CONFIG_REJECT authority/runtime/owner/value")); return; }
    ServerRuntime = Runtime; AnchorOwner = OwnerPS; AreaRadius = Radius; EndServerTime = EndsAt;
    OnRep_Presentation(); ForceNetUpdate();
}

void ADemoTacticalAnchorActor::SetCharge(int32 Count, int32 Threshold, bool bReady)
{
    DEMO_LOG_TICK();
    if (!HasAuthority() || bPulse || Threshold < 1 || Count < 0 || Count > Threshold)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_VISUAL_CHARGE_REJECT authority/retired/value")); return; }
    if (ChargeCount == Count && ChargeCapacity == Threshold && bCharged == bReady) return;
    ChargeCount = Count; ChargeCapacity = Threshold; bCharged = bReady;
    OnRep_Presentation(); ForceNetUpdate();
}

void ADemoTacticalAnchorActor::PulseAndRetire()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || bPulse)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_VISUAL_PULSE_REJECT authority/already retired")); return; }
    ServerRuntime.Reset(); bPulse = true; // 先断路由，再发布；同帧新命中不能为退休表现充能。
    OnRep_Presentation(); ForceNetUpdate();
    SetLifeSpan(.25f); // 仅表现释放；策略已经消费凭据并清除了活跃Actor引用。
}

UDemoAnchorBuildRuntime* ADemoTacticalAnchorActor::GetRuntime() const
{
    DEMO_LOG_TICK();
    return HasAuthority() && !bPulse ? ServerRuntime.Get() : nullptr;
}

void ADemoTacticalAnchorActor::OnRep_Presentation()
{
    DEMO_LOG_TICK();
    const float Fraction = FMath::Clamp(static_cast<float>(ChargeCount) / FMath::Max(1, ChargeCapacity), 0.f, 1.f); // 纯表现归一化，不回写充能。
    const FLinearColor Color = bPulse ? FLinearColor(1.f, .95f, .6f) : bCharged ? FLinearColor(1.f, .65f, .1f)
        : FLinearColor(.08f + Fraction * .25f, .3f + Fraction * .35f, .7f + Fraction * .25f); // 蓝色积蓄、金色就绪、浅色脉冲，不冒用火/冰元素。
    Disc->SetWorldScale3D(FVector(AreaRadius / 50.f, AreaRadius / 50.f, bPulse ? .07f : .025f));
    ChargeColumn->SetWorldScale3D(FVector(.1f, .1f, .15f + Fraction * .9f));
    ChargeColumn->SetWorldLocation(GetActorLocation() + FVector(0.f, 0.f, 8.f + Fraction * 45.f));
    if (DiscMaterial) DiscMaterial->SetVectorParameterValue(TEXT("Color"), Color);
    if (ColumnMaterial) ColumnMaterial->SetVectorParameterValue(TEXT("Color"), bCharged ? FLinearColor::White : Color);
}

void ADemoTacticalAnchorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL();
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADemoTacticalAnchorActor, AnchorOwner);
    DOREPLIFETIME(ADemoTacticalAnchorActor, AreaRadius);
    DOREPLIFETIME(ADemoTacticalAnchorActor, EndServerTime);
    DOREPLIFETIME(ADemoTacticalAnchorActor, ChargeCount);
    DOREPLIFETIME(ADemoTacticalAnchorActor, ChargeCapacity);
    DOREPLIFETIME(ADemoTacticalAnchorActor, bCharged);
    DOREPLIFETIME(ADemoTacticalAnchorActor, bPulse);
}
