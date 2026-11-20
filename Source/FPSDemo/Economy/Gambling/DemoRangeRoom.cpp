// 职责：在同World构造封闭训练房间、灯光与射击线屏障。使用：仅Range拥有，销毁不处理奖励或存档。
#include "Economy/Gambling/DemoRangeRoom.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"

ADemoRangeRoom::ADemoRangeRoom()
{
    DEMO_LOG_CALL(); bReplicates=true; bOnlyRelevantToOwner=true; SetReplicateMovement(true);
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube")); // 引擎资产经硬引用Cook。
    const FVector Positions[]={FVector(450,0,-25),FVector(450,-550,250),FVector(450,550,250),FVector(-300,0,250),FVector(1200,0,250),FVector(450,0,525),FVector(350,0,45)}; // 地面、侧墙、后墙、靶后墙、顶棚、射击线台面。
    const FVector Scales[]={FVector(16,12,.5),FVector(16,.5,5),FVector(16,.5,5),FVector(.5,12,5),FVector(.5,12,5),FVector(16,12,.5),FVector(.2,10,.15)}; // Cube默认100cm，各值为尺度。
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Positions);++Index)
    {
        auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("RoomSurface%d"),Index)); // Actor默认子对象，在每个客户端构造同一几何。
        Mesh->SetupAttachment(RootComponent); Mesh->SetStaticMesh(Cube.Object); Mesh->SetRelativeLocation(Positions[Index]); Mesh->SetRelativeScale3D(Scales[Index]);
        Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetCanEverAffectNavigation(false); Mesh->SetGenerateOverlapEvents(false);
    }
    auto* ShootingLine=CreateDefaultSubobject<UBoxComponent>(TEXT("ShootingLine")); // 阻止玩家进入靶区或通过冲刺贴脸打分，射线可以穿过。
    ShootingLine->SetupAttachment(RootComponent); ShootingLine->SetRelativeLocation(FVector(350,0,250)); ShootingLine->SetBoxExtent(FVector(10,525,250));
    ShootingLine->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); ShootingLine->SetCollisionResponseToAllChannels(ECR_Ignore); ShootingLine->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block); ShootingLine->SetCanEverAffectNavigation(false);
    for(int32 Index=0;Index<2;++Index)
    {
        auto* Light=CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("RangeLight%d"),Index)); // 随Owner房间复制生成；不依赖远处主场景烘焙光。
        Light->SetupAttachment(RootComponent); Light->SetRelativeLocation(FVector(Index*700,0,420)); Light->SetIntensity(12000); Light->SetAttenuationRadius(1700); Light->SetCastShadows(false);
    }
}
ADemoRangeRoom* ADemoRangeRoom::Create(APlayerController* Owner)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] RangeRoom.Create"));
    if(!Owner||!Owner->HasAuthority()) return nullptr;
    auto* World=Owner->GetWorld(); const auto* Config=GetDefault<ADemoRangeRoom>(); // 借用当前World和不可变布局设置。
    for(int32 Slot=0;Slot<32;++Slot) // 有界寻找空位；不会无限生成或覆盖其他玩家房间。
    {
        const FVector Origin=Config->AreaOrigin+FVector(0,Slot*FMath::Max(3000.f,Config->RoomSpacing),0); bool Occupied=false; // 房间占用不持久化，空位可复用。
        for(TActorIterator<ADemoRangeRoom> It(World);It;++It) if(!It->IsActorBeingDestroyed()&&FVector::DistSquared(It->GetActorLocation(),Origin)<FMath::Square(2500.f)) { Occupied=true; break; }
        if(Occupied||World->OverlapBlockingTestByChannel(Origin+FVector(450,0,250),FQuat::Identity,ECC_WorldStatic,FCollisionShape::MakeBox(FVector(850,650,350)))) continue;
        FActorSpawnParameters Parameters; Parameters.Owner=Owner; Parameters.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 预检查已确认无场景重叠。
        return World->SpawnActor<ADemoRangeRoom>(StaticClass(),Origin,FRotator::ZeroRotator,Parameters);
    }
    UE_LOG(LogFPSDemo,Warning,TEXT("RANGE_ROOM unavailable: all candidate locations blocked")); return nullptr;
}
FVector ADemoRangeRoom::Entrance(float HalfHeight) const
{ DEMO_LOG_CALL(); return GetActorLocation()+FVector(0,0,HalfHeight+3.f); }
