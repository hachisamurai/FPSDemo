#include "Interaction/DemoInteractable.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Debug/DemoLog.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"

ADemoInteractable::ADemoInteractable()
{
	DEMO_LOG_CALL();
	// 用简单碰撞体作为根让 SpawnActor 的位置微调可靠工作；显示模型不承担出生检测。
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("TerminalCollision"));
	Collision->InitBoxExtent(FVector(60,60,80));
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	SetRootComponent(Collision);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Terminal"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 基础形状由引擎提供，项目不会依赖生成的二进制资产。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
	Mesh->SetStaticMesh(Cylinder.Object);
	Mesh->SetRelativeScale3D(FVector(1.2f,1.2f,1.6f));
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Mesh);
	Label->SetRelativeLocation(FVector(0,0,110));
	Label->SetRelativeRotation(FRotator(0,180,0));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(28.f);
}
void ADemoInteractable::Configure(bool bShop)
{
	DEMO_LOG_CALL();
	bIsShop = bShop;
	Label->SetText(FText::FromString(bIsShop ? TEXT("UPGRADE TERMINAL") : TEXT("ENTER NEXT SECTOR")));
	Label->SetTextRenderColor(bIsShop ? FColor::Cyan : FColor::Orange);
}
void ADemoInteractable::Interact(ADemoCharacter* Player)
{
	DEMO_LOG_CALL();
	// 角色/GM 校验不能只依赖客户端 HUD 提示。
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
	ADemoPlayerController* Controller = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 借用拥有者；奖励/商店/出发确认互斥，避免重复输入跳过流程。
	if (!IsValid(this) || !Player || !Mode || !State || !Controller || Controller->HasBlockingOverlay() || Controller->IsUpgradeMenuOpen() || Controller->IsNextLevelConfirmationOpen()
		|| (State->Phase != EDemoPhase::Hub && State->Phase != EDemoPhase::Intermission)
		|| (bIsShop ? Mode->GetShopTerminal() : Mode->GetNextLevelTerminal()) != this
		|| FVector::Dist(Player->GetActorLocation(), GetActorLocation()) > 250.f)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("Interaction rejected: stale terminal, invalid phase/player/menu/range"));
		return;
	}
	// 出发交互仅提交确认意图；玩家明确选择“是”后才由控制器调用权威推进。
	if (!bIsShop) Controller->OpenNextLevelConfirmation(this);
	else Controller->OpenUpgradeMenu(false);
}
// HUD 使用运行时中文字体；场景 TextRender 标牌保留英文以兼容其离线字体。
FString ADemoInteractable::GetPrompt() const { DEMO_LOG_TICK(); return bIsShop ? TEXT("[E] 打开升级终端") : TEXT("[E] 确认前往下一关"); }
