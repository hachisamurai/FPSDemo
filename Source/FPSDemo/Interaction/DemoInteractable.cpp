// 职责：备战终端的本地模型、编辑器标牌与交互意图。使用原生生成类和原有类型复制选择显示。
#include "Interaction/DemoInteractable.h"
#include "Interaction/DemoTerminalPlacement.h"
#include "Engine/StaticMesh.h"
#include "Misc/PackageName.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Debug/DemoLog.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "Player/DemoPlayerCommands.h"

ADemoInteractable::ADemoInteractable()
{
	DEMO_LOG_CALL();
	bReplicates = true; bAlwaysRelevant = true; SetReplicateMovement(true); // 安全终端必须由房主生成后同步给队员。
	// 用简单碰撞体作为根让 SpawnActor 的位置微调可靠工作；显示模型不承担出生检测。
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("TerminalCollision"));
	Collision->InitBoxExtent(FVector(60,60,80));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	SetRootComponent(Collision);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Terminal"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false); // 两件艺术网格自带UCX，导航与交互阻挡统一由根盒控制。
	// 普通战区的既有外观，也是Hub资产缺失时仍可定位交互盒的后备模型。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
	Mesh->SetStaticMesh(Cylinder.Object);
	Mesh->SetRelativeScale3D(FVector(1.2f,1.2f,1.6f));
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	// 固定在原来的Actor局部高度与比例，切换网格时不会随其落地、旋转。
	Label->SetupAttachment(Collision);
	Label->SetRelativeLocation(FVector(0,0,176));
	Label->SetRelativeScale3D(FVector(1.2f,1.2f,1.6f));
	Label->SetRelativeRotation(FRotator(0,180,0));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(28.f);
	// 2026-11-19文字减负：英文标牌与HUD中文短名重复且中英混排，游戏内默认隐藏；编辑器视口仍可见便于摆放，组件保留供将来换中文离线字体/图标。
	Label->SetHiddenInGame(true);
}
ADemoHubInteractable::ADemoHubInteractable()
{
	DEMO_LOG_CALL();
	// 首次构造时按已挂载的长包名冻结本进程的选择；W2 导入完毕后由新进程读回。
	static constexpr TCHAR UpgradeR01[] = TEXT("/Game/Environment/LightingRefactor20261119/Models_R01/SM_EM_UpgradeMonolith");
	static constexpr TCHAR DepartureR01[] = TEXT("/Game/Environment/LightingRefactor20261119/Models_R01/SM_EM_DepartureGate");
	static const bool bHasR01Pair = FPackageName::DoesPackageExist(UpgradeR01) && FPackageName::DoesPackageExist(DepartureR01);
	if (bHasR01Pair)
	{
		// FObjectFinder 自动补同名对象后缀。只有两个对象都确认为静态网格才切换整对。
		static ConstructorHelpers::FObjectFinder<UStaticMesh> UpgradeMesh(UpgradeR01);
		static ConstructorHelpers::FObjectFinder<UStaticMesh> DepartureMesh(DepartureR01);
		if (UpgradeMesh.Succeeded() && DepartureMesh.Succeeded())
		{
			UpgradeModel = UpgradeMesh.Object;
			DepartureModel = DepartureMesh.Object;
			return;
		}
	}

	// W2 未完整提供时继续加载原 A01 组合，日志仅在类默认对象构造时记录一次。
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		UE_LOG(LogFPSDemo, Log, TEXT("TERMINAL_VIEW_FALLBACK using A01 pair: %s"),
			bHasR01Pair ? TEXT("R01 mesh load failed") : TEXT("R01 package pair unavailable"));
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Upgrade(TEXT("/Game/Environment/EnvHubL01_20261119/Models_A01/SM_EM_UpgradeMonolith.SM_EM_UpgradeMonolith"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Departure(TEXT("/Game/Environment/EnvHubL01_20261119/Models_A01/SM_EM_DepartureGate.SM_EM_DepartureGate"));
	UpgradeModel = Upgrade.Object;
	DepartureModel = Departure.Object;
}
void ADemoInteractable::BeginPlay()
{
	DEMO_LOG_CALL();
	Super::BeginPlay();
	RefreshTerminalView();
}
void ADemoInteractable::Configure(bool bShop)
{
	DEMO_LOG_CALL();
	if (!HasAuthority()) { UE_LOG(LogFPSDemo, Warning, TEXT("Terminal Configure rejected on client")); return; }
	bIsShop = bShop;
	RefreshTerminalView();
	ForceNetUpdate();
}
void ADemoInteractable::OnRep_TerminalKind()
{
	DEMO_LOG_CALL();
	RefreshTerminalView();
}
void ADemoInteractable::RefreshTerminalView()
{
	DEMO_LOG_CALL();
	if (IsA<ADemoHubInteractable>())
	{
		UStaticMesh* Model = bIsShop ? UpgradeModel.Get() : DepartureModel.Get(); // 使用网格自身LOD与材质槽，只改变可见子组件。
		if (Model && Mesh->GetStaticMesh() != Model)
		{
			const float BaseZ = -Collision->GetUnscaledBoxExtent().Z - DemoTerminalPlacement::ContactLift - Model->GetBoundingBox().Min.Z; // Actor原点在地面上方82cm。
			Mesh->SetStaticMesh(Model);
			Mesh->SetRelativeTransform(FTransform(FRotator(0,90,0), FVector(0,0,BaseZ))); // 原尺寸；本地+Y的操作面转向Hub出生点所在的西侧。
			UE_LOG(LogFPSDemo, Log, TEXT("TERMINAL_VIEW_APPLIED actor=%s shop=%d authority=%d mesh=%s z=%.2f"), *GetName(), bIsShop, HasAuthority(), *Model->GetPathName(), BaseZ);
		}
		else if (!Model)
		{
			const UStaticMeshComponent* Fallback = GetDefault<ADemoInteractable>()->Mesh; // 使用原生圆柱默认值，不能沿用上一种终端外观。
			Mesh->SetStaticMesh(Fallback->GetStaticMesh());
			Mesh->SetRelativeTransform(Fallback->GetRelativeTransform());
			UE_LOG(LogFPSDemo, Warning, TEXT("TERMINAL_VIEW missing model shop=%d actor=%s"), bIsShop, *GetName());
		}
	}
	Label->SetText(FText::FromString(bIsShop ? TEXT("UPGRADE TERMINAL") : TEXT("ENTER NEXT SECTOR")));
	Label->SetTextRenderColor(bIsShop ? FColor::Cyan : FColor::Orange);
}
void ADemoInteractable::Interact(ADemoCharacter* Player)
{
	DEMO_LOG_CALL();
	ADemoPlayerController* Controller = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // RPC通过拥有者，而非无网络所有权的终端Actor。
	if (!Controller || !Controller->IsLocalController() || Controller->HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Interaction rejected: no local owner/input")); return; }
	FDemoGameplayCommandRequest Request; // 仅传网络对象意图；服务器重新核验注册终端、范围、生命和阶段。
	Request.Command = EDemoGameplayCommand::BeginInteraction; Request.Terminal = this;
	Controller->SubmitGameplayCommand(Request);
}
// HUD 使用运行时中文字体；场景 TextRender 标牌只能用英文离线字体，2026-11-19起游戏内默认隐藏，终端名由HUD中文短名唯一显示。
FString ADemoInteractable::GetPrompt() const { DEMO_LOG_TICK(); return bIsShop ? TEXT("[E] 打开升级终端") : TEXT("[E] 确认前往下一关"); }
void ADemoInteractable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ADemoInteractable, bIsShop); }
