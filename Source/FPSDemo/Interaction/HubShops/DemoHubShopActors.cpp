// 职责：创建P0真实枪械陈列、七设备静态轮廓及两屋白模；依赖原武器CDO/Engine几何/World注册。无交易、开奖、老板伤害或相机控制。
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h" // 仅CasinoDevice安装CP-01控件，GunDisplay/Blockout无行为变化。
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h" // 仅CasinoDevice消费正式实测模型框，不改变GunDisplay/R03布局。
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopEnvironment.h" // 仅Blockout选择持久环境；GunDisplay/Casino逻辑保持。
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Weapons/DemoWeaponBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h" // HUB-INTERIOR-EXPOSURE-20261119：两店室内有界曝光补偿。
#include "Interaction/HubShops/DemoHubLightingSettings.h"
#include "World/Rooms/DemoHubAtmosphereComponent.h" // 原常驻根的私有有界PP消费者，非流送房间/动态终端。
#include "Debug/DemoLog.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ADemoGunDisplay::ADemoGunDisplay()
{
    DEMO_LOG_CALL(); bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); PrimaryActorTick.bCanEverTick=false;
    Root=CreateDefaultSubobject<USceneComponent>(TEXT("DisplayRoot")); SetRootComponent(Root);
    FocusBounds=CreateDefaultSubobject<UBoxComponent>(TEXT("FocusBounds")); FocusBounds->SetupAttachment(Root); FocusBounds->SetBoxExtent(FVector(90,35,45));
    FocusBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly); FocusBounds->SetCollisionResponseToAllChannels(ECR_Ignore); FocusBounds->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); FocusBounds->SetGenerateOverlapEvents(false);
    InfoAnchor=CreateDefaultSubobject<USceneComponent>(TEXT("InfoAnchor")); InfoAnchor->SetupAttachment(Root);
    StaticDisplay=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticDisplay")); StaticDisplay->SetupAttachment(Root); StaticDisplay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SkeletalDisplay=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalDisplay")); SkeletalDisplay->SetupAttachment(Root); SkeletalDisplay->SetCollisionEnabled(ECollisionEnabled::NoCollision); SkeletalDisplay->SetComponentTickEnabled(false); SkeletalDisplay->bPauseAnims=true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SupportAsset(TEXT("/Engine/BasicShapes/Cube")); // 只为R03临时托钩持有基础几何，不修改武器资产。
    SupportCube=SupportAsset.Object;
}
bool ADemoGunDisplay::Configure(UDemoHubShopDefinition* InDefinition,FName InDisplayId)
{
    DEMO_LOG_CALL();
    if(!HasAuthority()||!InDefinition||(HasActorBegunPlay()&&DisplayId!=InDisplayId)) { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_DISPLAY_CONFIG_REJECT authority/definition/immutable id")); return false; }
    Definition=InDefinition; DisplayId=InDisplayId; const bool bReady=RefreshDisplay(); // 没有钱包或武器实例副作用。
    if(HasActorBegunPlay()) OnRep_Config(); ForceNetUpdate(); return bReady;
}
bool ADemoGunDisplay::RefreshDisplay()
{
    DEMO_LOG_CALL(); bDisplayReady=false; ClearSupports();
    const FDemoHubDisplayBinding* Binding=Definition?Definition->FindDisplay(DisplayId):nullptr; // 同步借用DA作者值。
    const FDemoHubWeaponOffer* Offer=Binding?Definition->FindOffer(Binding->OfferId):nullptr; // 不用本机可改价格制作报价。
    FString Error; const ADemoWeaponBase* CDO=Offer?Definition->ResolveWeaponCDO(Offer->WeaponId,Error):nullptr; // 只读取默认表现。
    if(!Binding||!Offer||!CDO)
    { StaticDisplay->SetVisibility(false); SkeletalDisplay->SetVisibility(false); UE_LOG(LogFPSDemo,Log,TEXT("GUN_DISPLAY_NOT_READY id=%s reason=%s"),*DisplayId.ToString(),*Error); return false; }
    OfferId=Binding->OfferId; ShopId=Binding->ShopId; FocusBounds->SetRelativeLocation(FVector::ZeroVector); FocusBounds->SetBoxExtent(Binding->FocusExtent); InfoAnchor->SetRelativeLocation(Binding->InfoOffset);
    StaticDisplay->SetStaticMesh(CDO->Config.StaticMesh); SkeletalDisplay->SetSkeletalMesh(CDO->Config.StaticMesh?nullptr:CDO->Config.Mesh.Get());
    SkeletalDisplay->bComponentUseFixedSkelBounds=Definition->LayoutRevision==3; SkeletalDisplay->InvalidateCachedBounds(); // R03无动画陈列固定图形资源bounds，避免Editor物理资产AABB混入枪架测量；旧0仍原策略。
    StaticDisplay->SetVisibility(CDO->Config.StaticMesh!=nullptr); SkeletalDisplay->SetVisibility(CDO->Config.StaticMesh==nullptr&&CDO->Config.Mesh!=nullptr);
    FTransform ModelTransform=Binding->ModelTransform; // 仅本Actor派生值；DA保存作者朝向/缩放，不回写武器或配置。
    FBox MountedBounds(ForceInit); // R03仅选中实际mesh的局部AABB，绝不读取包含Focus的ActorBounds。
    if(Definition->LayoutRevision==3)
    {
        StaticDisplay->SetBoundsScale(1.f); SkeletalDisplay->SetBoundsScale(1.f); // 明确展示bounds不额外膨胀，查询盒余量由下方2cm独立配置。
        const FBox RawBounds=CDO->Config.StaticMesh?CDO->Config.StaticMesh->GetBoundingBox():CDO->Config.Mesh->GetBounds().GetBox(); // 真网格资源的基础包围盒，支持静态与无动画骨骼陈列。
        MountedBounds=RawBounds.TransformBy(ModelTransform.ToMatrixWithScale());
        if(!MountedBounds.IsValid||MountedBounds.GetSize().GetMin()<=0)
        { StaticDisplay->SetVisibility(false); SkeletalDisplay->SetVisibility(false); UE_LOG(LogFPSDemo,Warning,TEXT("GUN_DISPLAY_BOUNDS_REJECT id=%s"),*DisplayId.ToString()); return false; }
        const double MeshMaxY=Definition->GunShopCenter.Y+Definition->GunShopInteriorSize.Y*.5-7.; // 北墙内面-4cm背板厚度-3cm枪墙净空，R03为2093cm。
        const FVector Center=MountedBounds.GetCenter(); // 真实mesh旋转后中心，X/Z对齐槽根而非原武器握点。
        const FVector Adjustment(-Center.X,MeshMaxY-Binding->Transform.GetLocation().Y-MountedBounds.Max.Y,-Center.Z); // 枪根保持作者槽位；只平移真实模型。
        ModelTransform.AddToTranslation(Adjustment); MountedBounds=RawBounds.TransformBy(ModelTransform.ToMatrixWithScale());
        FocusBounds->SetRelativeLocation(MountedBounds.GetCenter()); FocusBounds->SetBoxExtent(MountedBounds.GetExtent()+FVector(2)); // 每侧仅2cm余量，25cm枪间净距不会被原90cm半宽吃掉。
        InfoAnchor->SetRelativeLocation(MountedBounds.GetCenter()+Binding->InfoOffset); // 屋内侧浮签跟随真实枪中心。
        const double SupportBackY=Definition->GunShopCenter.Y+Definition->GunShopInteriorSize.Y*.5-4.-Binding->Transform.GetLocation().Y; // 背板前面，托钩与共享背板接合。
        const double SupportFrontY=MountedBounds.Min.Y-2.; // 托钩前唇位于枪前方2cm；不加入查询或Pawn碰撞。
        for(int32 Index=0;Index<2;++Index)
        {
            const double SupportX=MountedBounds.GetCenter().X+(Index==0?-1.:1.)*MountedBounds.GetExtent().X*.55; // 两个独立托点，不按Actor聚合宽度伸到邻枪。
            const double SupportSurfaceZ=MountedBounds.GetCenter().Z+(Index==0?Binding->SupportHeightOffsetsCm.X:Binding->SupportHeightOffsetsCm.Y); // 各枪枪托/护木高度来自真实近景作者校准，整枪最低点通常是弹匣或握把。
            AddSupport(FVector(SupportX,(SupportBackY+SupportFrontY)*.5,SupportSurfaceZ-2),FVector(4,SupportBackY-SupportFrontY,4));
            AddSupport(FVector(SupportX,SupportFrontY,SupportSurfaceZ+2),FVector(4,4,8));
        }
        UE_LOG(LogFPSDemo,Log,TEXT("GUN_DISPLAY_WALL_FIT id=%s layout=3 local_min=%s local_max=%s model=%s"),*DisplayId.ToString(),*MountedBounds.Min.ToString(),*MountedBounds.Max.ToString(),*ModelTransform.ToString());
    }
    StaticDisplay->SetRelativeTransform(ModelTransform); SkeletalDisplay->SetRelativeTransform(ModelTransform);
    bDisplayReady=StaticDisplay->GetStaticMesh()!=nullptr||SkeletalDisplay->GetSkeletalMeshAsset()!=nullptr;
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_DISPLAY_READY id=%s offer=%s mesh=%s"),*DisplayId.ToString(),*OfferId.ToString(),*GetNameSafe(GetDisplayMesh())); return bDisplayReady;
}
void ADemoGunDisplay::ClearSupports()
{ DEMO_LOG_CALL(); for(UStaticMeshComponent* Support:Supports) if(Support) Support->DestroyComponent(); Supports.Reset(); }
void ADemoGunDisplay::AddSupport(const FVector& Center,const FVector& Size)
{
    DEMO_LOG_CALL(); if(!SupportCube||!Root||Size.GetMin()<=0) { UE_LOG(LogFPSDemo,Warning,TEXT("GUN_SUPPORT_REJECT id=%s"),*DisplayId.ToString()); return; }
    UStaticMeshComponent* Support=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient); // Actor拥有临时托钩，反复构造不向Map累加序列化副本。
    AddInstanceComponent(Support); Support->ComponentTags.Add(TEXT("HubGunSupportR03")); Support->SetupAttachment(Root); Support->SetMobility(EComponentMobility::Movable); Support->SetStaticMesh(SupportCube); Support->SetRelativeLocation(Center); Support->SetRelativeScale3D(Size/100.); Support->SetCollisionEnabled(ECollisionEnabled::NoCollision); Support->SetCastShadow(false); Support->RegisterComponent(); Supports.Add(Support);
}
void ADemoGunDisplay::OnConstruction(const FTransform& Transform)
{ DEMO_LOG_CALL(); Super::OnConstruction(Transform); if(Definition&&!DisplayId.IsNone()) RefreshDisplay(); }
void ADemoGunDisplay::BeginPlay()
{ DEMO_LOG_CALL(); Super::BeginPlay(); OnRep_Config(); }
void ADemoGunDisplay::OnRep_Config()
{ DEMO_LOG_CALL(); if(RefreshDisplay()) if(UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()) Registry->RegisterDisplay(this); }
void ADemoGunDisplay::EndPlay(const EEndPlayReason::Type Reason)
{ DEMO_LOG_CALL(); if(GetWorld()) if(UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()) Registry->UnregisterDisplay(this); Super::EndPlay(Reason); }
// OutLifetimeProps由引擎收集复制字段；DOREPLIFETIME宏固定引用此参数名，不能简写为Out。
void ADemoGunDisplay::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ADemoGunDisplay,Definition); DOREPLIFETIME(ADemoGunDisplay,DisplayId); DOREPLIFETIME(ADemoGunDisplay,OfferId); DOREPLIFETIME(ADemoGunDisplay,ShopId); }
FVector ADemoGunDisplay::GetFocusPoint() const { DEMO_LOG_TICK(); return FocusBounds->GetComponentLocation(); }
FVector ADemoGunDisplay::GetInfoPoint() const { DEMO_LOG_TICK(); return InfoAnchor->GetComponentLocation(); }
UPrimitiveComponent* ADemoGunDisplay::GetDisplayMesh() const { DEMO_LOG_TICK(); return StaticDisplay->GetStaticMesh()?static_cast<UPrimitiveComponent*>(StaticDisplay.Get()):static_cast<UPrimitiveComponent*>(SkeletalDisplay.Get()); }
bool ADemoGunDisplay::GetDisplayModelBounds(FVector& OutMin,FVector& OutMax) const
{
    DEMO_LOG_TICK(); OutMin=FVector::ZeroVector; OutMax=FVector::ZeroVector;
    const UPrimitiveComponent* Model=GetDisplayMesh(); // 只读当前选中的真实mesh组件，不包含Focus或托钩。
    if(!bDisplayReady||!Model||!Model->IsVisible()) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("GUN_MODEL_BOUNDS_REJECT id=%s reason=not_ready_or_visible"),*DisplayId.ToString()); return false; }
    FBox AssetBounds(ForceInit); // 与挂墙拟合使用相同图形资源包络；旧SkinnedComponent::CalcBounds可能采用较小PhysicsAsset，不能拿它当完整枪宽。
    if(StaticDisplay->GetStaticMesh()) AssetBounds=StaticDisplay->GetStaticMesh()->GetBoundingBox();
    else if(SkeletalDisplay->GetSkeletalMeshAsset()) AssetBounds=SkeletalDisplay->GetSkeletalMeshAsset()->GetBounds().GetBox();
    const FBox ModelBounds=AssetBounds.TransformBy(Model->GetComponentTransform().ToMatrixWithScale()); // 真正选中的资产AABB随实际展示变换，不是逐顶点表面距离，也不读Focus/Actor聚合/物理bounds。
    if(!ModelBounds.IsValid||ModelBounds.Min.ContainsNaN()||ModelBounds.Max.ContainsNaN()) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("GUN_MODEL_BOUNDS_REJECT id=%s reason=invalid_bounds"),*DisplayId.ToString()); return false; }
    OutMin=ModelBounds.Min; OutMax=ModelBounds.Max; return true;
}
UBoxComponent* ADemoGunDisplay::GetFocusBounds() const { DEMO_LOG_TICK(); return FocusBounds; }
bool ADemoGunDisplay::IsDisplayReady() const { DEMO_LOG_TICK(); return bDisplayReady; }

ADemoCasinoDevice::ADemoCasinoDevice()
{
    DEMO_LOG_CALL(); bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); PrimaryActorTick.bCanEverTick=false;
    PhysicalControls=CreateDefaultSubobject<UDemoCasinoControlsComponent>(TEXT("PhysicalControls")); // 固定查询与本机私有动画分开，由Mode/独立开关决定是否实际构造。
    Root=CreateDefaultSubobject<USceneComponent>(TEXT("DeviceRoot")); SetRootComponent(Root);
    Collision=CreateDefaultSubobject<UBoxComponent>(TEXT("DeviceCollision")); Collision->SetupAttachment(Root); Collision->SetRelativeLocation(FVector(0,0,85)); Collision->SetBoxExtent(FVector(55,55,85)); Collision->SetCollisionProfileName(TEXT("BlockAll")); Collision->SetGenerateOverlapEvents(false);
    FocusAnchor=CreateDefaultSubobject<USceneComponent>(TEXT("InteractionAnchor")); FocusAnchor->SetupAttachment(Root); FocusAnchor->SetRelativeLocation(FVector(60,0,125));
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ModeLabel")); Label->SetupAttachment(Root); Label->SetRelativeLocation(FVector(60,0,182)); Label->SetWorldSize(18); Label->SetHorizontalAlignment(EHTA_Center); Label->SetTextRenderColor(FColor::Cyan);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube")); // Engine几何硬引用，不下载商业资产。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere"));
    Cube=CubeAsset.Object; Cylinder=CylinderAsset.Object; Sphere=SphereAsset.Object;
}
bool ADemoCasinoDevice::Configure(UDemoHubShopDefinition* InDefinition,FName InDeviceId)
{
    DEMO_LOG_CALL();
    if(!HasAuthority()||!InDefinition||(HasActorBegunPlay()&&DeviceId!=InDeviceId)) { UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_DEVICE_CONFIG_REJECT authority/definition/immutable id")); return false; }
    Definition=InDefinition; DeviceId=InDeviceId; const bool bReady=RefreshDevice(); // 只选旧Mode，不开始下注。
    if(HasActorBegunPlay()) OnRep_Config(); ForceNetUpdate(); return bReady;
}
void ADemoCasinoDevice::ClearPieces()
{ DEMO_LOG_CALL(); for(UStaticMeshComponent* Piece:Pieces) if(Piece) Piece->DestroyComponent(); Pieces.Reset(); BuiltMode=INDEX_NONE; }
UStaticMeshComponent* ADemoCasinoDevice::AddPiece(UStaticMesh* Piece,const FVector& Position,const FVector& Size)
{
    DEMO_LOG_CALL(); if(!Piece||!Root) return nullptr;
    UStaticMeshComponent* Mesh=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient); // Actor拥有，作者Map仅存配置；每端重建，不把面板副本写回地图累积。
    AddInstanceComponent(Mesh); Mesh->SetupAttachment(Root); Mesh->SetStaticMesh(Piece); Mesh->SetRelativeLocation(Position); Mesh->SetRelativeScale3D(Size/100.); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCastShadow(false); Mesh->RegisterComponent(); Pieces.Add(Mesh); return Mesh;
}
bool ADemoCasinoDevice::RefreshDevice()
{
    DEMO_LOG_CALL(); const FDemoHubCasinoBinding* Binding=Definition?Definition->FindDevice(DeviceId):nullptr; // 目录唯一映射。
    if(!Binding||Binding->Mode<0||Binding->Mode>6||UDemoHubShopDefinition::ModeForGameId(Binding->GameId)!=Binding->Mode)
    { ClearPieces(); PhysicalControls->Configure(INDEX_NONE); Mode=INDEX_NONE; Label->SetText(FText::FromString(TEXT("DEVICE UNAVAILABLE"))); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_NOT_READY id=%s"),*DeviceId.ToString()); return false; }
    Mode=Binding->Mode;
    PhysicalControls->Configure(Mode); // 同Mode控件绑定版本改变仍独立重建，不被下面P0 BuiltMode早退吞掉。
    if (PhysicalControls->IsModelRequested())
    {
        ClearPieces(); // 只撤本Actor旧P0图形，不销毁Controls自有正式模型。
        const UDemoCasinoDeviceProfile* Profile=PhysicalControls->GetModelProfile();
        if (!Profile || !PhysicalControls->IsReady())
        { Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision); Label->SetVisibility(true); Label->SetText(FText::FromString(TEXT("MODEL UNAVAILABLE"))); return false; }
        Collision->SetRelativeTransform(Profile->CollisionTransform); Collision->SetBoxExtent(Profile->CollisionExtent); Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        FocusAnchor->SetRelativeTransform(Profile->FocusTransform); Label->SetRelativeTransform(Profile->InfoTransform); Label->SetVisibility(false); // 正式标识由模型自身提供，Info仍供既有Focus卡用。
        BuiltMode=Mode; return true;
    }
    Collision->SetRelativeTransform(FTransform(FVector(0,0,85))); Collision->SetBoxExtent(FVector(55,55,85)); Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    FocusAnchor->SetRelativeTransform(FTransform(FVector(60,0,125))); Label->SetRelativeTransform(FTransform(FVector(60,0,182))); Label->SetVisibility(true); // 仅明确旧P0/CP01回退保留历史坐标。
    const TCHAR* Labels[]={TEXT("NUMBERS"),TEXT("SLOTS"),TEXT("MINE CRATE"),TEXT("REACTOR"),TEXT("TACTICAL DICE"),TEXT("MECH ARENA"),TEXT("BOUNTY RANGE")}; // 英文离线标牌；中文由HUD运行字体提供。
    Label->SetText(FText::FromString(Labels[Mode]));
    if(BuiltMode==Mode&&!Pieces.IsEmpty()) return true;
    ClearPieces(); AddPiece(Cube,FVector(0,0,65),FVector(95,100,130)); AddPiece(Cube,FVector(0,0,145),FVector(95,110,10));
    if(Mode==0||Mode==2) // 数字灯格/5x5翻板的静态轮廓，不揭晓任何随机结果。
    {
        const int32 Count=Mode==2?5:4; // 只决定可识别面板网格。
        for(int32 Row=0;Row<Count;++Row) for(int32 Column=0;Column<Count;++Column)
            AddPiece(Cube,FVector(52,-40+Column*(80.f/(Count-1)),105+Row*13),FVector(8,12,9));
    }
    else if(Mode==1) // 三轴和独立手柄静态轮廓；没有结果生成/拉动事务。
    { for(int32 Reel=0;Reel<3;++Reel) AddPiece(Cylinder,FVector(53,-28+Reel*28,130),FVector(22,22,32)); AddPiece(Cube,FVector(0,68,150),FVector(12,10,70)); AddPiece(Sphere,FVector(0,68,185),FVector(22)); }
    else if(Mode==3) { AddPiece(Cylinder,FVector(0,0,200),FVector(60,60,100)); AddPiece(Sphere,FVector(0,0,255),FVector(40)); }
    else if(Mode==4) { for(int32 Die=0;Die<5;++Die) AddPiece(Cube,FVector(10,-40+Die*20,160),FVector(15)); }
    else if(Mode==5) { AddPiece(Sphere,FVector(0,-28,170),FVector(32)); AddPiece(Sphere,FVector(0,28,170),FVector(32)); AddPiece(Cube,FVector(-35,0,180),FVector(8,100,60)); }
    else { AddPiece(Cube,FVector(-35,-40,200),FVector(12,12,100)); AddPiece(Cube,FVector(-35,40,200),FVector(12,12,100)); AddPiece(Cube,FVector(-35,0,250),FVector(12,92,12)); AddPiece(Sphere,FVector(-35,0,205),FVector(30)); }
    BuiltMode=Mode; UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_READY id=%s mode=%d pieces=%d"),*DeviceId.ToString(),Mode,Pieces.Num()); return !Pieces.IsEmpty();
}
void ADemoCasinoDevice::OnConstruction(const FTransform& Transform) { DEMO_LOG_CALL(); Super::OnConstruction(Transform); if(Definition&&!DeviceId.IsNone()) RefreshDevice(); }
void ADemoCasinoDevice::BeginPlay() { DEMO_LOG_CALL(); Super::BeginPlay(); OnRep_Config(); }
void ADemoCasinoDevice::OnRep_Config() { DEMO_LOG_CALL(); if(RefreshDevice()) if(UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()) Registry->RegisterDevice(this); }
void ADemoCasinoDevice::EndPlay(const EEndPlayReason::Type Reason) { DEMO_LOG_CALL(); if(GetWorld()) if(UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()) Registry->UnregisterDevice(this); Super::EndPlay(Reason); }
// OutLifetimeProps为引擎复制字段输出，名称遵循DOREPLIFETIME宏契约；不改变设备复制范围。
void ADemoCasinoDevice::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ADemoCasinoDevice,Definition); DOREPLIFETIME(ADemoCasinoDevice,DeviceId); DOREPLIFETIME(ADemoCasinoDevice,Mode); }
FVector ADemoCasinoDevice::GetFocusPoint() const { DEMO_LOG_TICK(); return FocusAnchor->GetComponentLocation(); }
FVector ADemoCasinoDevice::GetInfoPoint() const { DEMO_LOG_TICK(); const UDemoCasinoDeviceProfile* Profile=PhysicalControls->GetModelProfile(); return Profile?GetActorTransform().TransformPosition(Profile->InfoTransform.GetLocation()):FocusAnchor->GetComponentLocation()+FVector(0,0,35); }
UBoxComponent* ADemoCasinoDevice::GetFocusBounds() const { DEMO_LOG_TICK(); return Collision; }
bool ADemoCasinoDevice::IsDeviceReady() const { DEMO_LOG_TICK(); return PhysicalControls->IsModelRequested()?Mode==1&&BuiltMode==Mode&&PhysicalControls->IsReady():Mode>=0&&Mode<=6&&BuiltMode==Mode&&!Pieces.IsEmpty(); }

ADemoHubShopBlockout::ADemoHubShopBlockout()
{
    DEMO_LOG_CALL(); bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); PrimaryActorTick.bCanEverTick=false;
    Root=CreateDefaultSubobject<USceneComponent>(TEXT("HubShopRoot")); SetRootComponent(Root);
    HubAtmosphere=CreateDefaultSubobject<UDemoHubAtmosphereComponent>(TEXT("HubAtmosphere")); // 无Tick/复制，Begin/EndPlay自动管理原Volume认领字段。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube")); // 固定引擎资产，不手写uasset。
    Cube=CubeAsset.Object;
}
bool ADemoHubShopBlockout::Configure(UDemoHubShopDefinition* InDefinition,int32 InWorldEpoch)
{
    DEMO_LOG_CALL(); if(!HasAuthority()||!InDefinition||InWorldEpoch<=0) { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_BLOCKOUT_CONFIG_REJECT")); return false; }
    Definition=InDefinition; WorldEpoch=static_cast<uint32>(InWorldEpoch); const bool bReady=RebuildGeometry(); ForceNetUpdate(); return bReady;
}
void ADemoHubShopBlockout::AddBlock(const FVector& Center,const FVector& Size,bool bCollision)
{
    DEMO_LOG_CALL(); UStaticMeshComponent* Mesh=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient); // Actor拥有；地图只存可信DA，重新读取后用同路构造临时几何。
    AddInstanceComponent(Mesh); Mesh->SetupAttachment(Root); Mesh->SetMobility(EComponentMobility::Movable); Mesh->SetStaticMesh(Cube); Mesh->SetRelativeLocation(Center); Mesh->SetRelativeScale3D(Size/100.); Mesh->SetCollisionEnabled(bCollision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision); if(bCollision) Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->RegisterComponent(); Pieces.Add(Mesh);
}
void ADemoHubShopBlockout::AddRoom(const FVector& Center,const FVector& Size)
{
    DEMO_LOG_CALL(); const double HalfWidth=Size.X*.5,HalfDepth=Size.Y*.5,Height=Size.Z; // 内净尺寸cm，墙放在净尺寸外20cm。
    const double Side=(Size.Y-Definition->EntranceClearWidthCm)*.5; // 西墙两段，正中实际净宽由DA配置；旧360/R03为500cm。
    // 地板复用原Hub平台，避免叠一层共面网格造成Z闪；独立审查Map由作者脚本创建一块同尺寸庭院地板。
    AddBlock(Center+FVector(HalfWidth+10,0,Height*.5),FVector(20,Size.Y+40,Height));
    AddBlock(Center+FVector(0,HalfDepth+10,Height*.5),FVector(Size.X+40,20,Height));
    AddBlock(Center+FVector(0,-HalfDepth-10,Height*.5),FVector(Size.X+40,20,Height));
    AddBlock(Center+FVector(-HalfWidth-10,-(Definition->EntranceClearWidthCm+Side)*.5,Height*.5),FVector(20,Side,Height));
    AddBlock(Center+FVector(-HalfWidth-10,(Definition->EntranceClearWidthCm+Side)*.5,Height*.5),FVector(20,Side,Height));
    AddBlock(Center+FVector(-HalfWidth-10,0,Height+10),FVector(20,Definition->EntranceClearWidthCm,20)); // 门楣在净高之上，无P1封门碰撞。
    UPointLightComponent* Light=NewObject<UPointLightComponent>(this,NAME_None,RF_Transient); // 每屋一个白模照明候选，非最终灯光/性能验收。
    AddInstanceComponent(Light); Light->SetupAttachment(Root); Light->SetRelativeLocation(Center+FVector(0,0,Height-50)); Light->SetIntensity(4000); Light->SetAttenuationRadius(1400); Light->SetCastShadows(false); Light->RegisterComponent(); Pieces.Add(Light);
}
void ADemoHubShopBlockout::AddInteriorExposure(const FVector& Center,const FVector& Size,float Bias,const TCHAR* Label)
{
    DEMO_LOG_CALL();
    // 设计意图：TA室内暖光(3400K、6000～8500lm面光)在全局Bias1下把浅色墙/台面推到发白；只在两店内尺寸盒内降低曝光补偿，
    // 不改主图灯光资产、不改室外与Hub SpeedDown Volume。编辑器OnConstruction与专用服务器不生成。
    UWorld* World=GetWorld();
    if(!World||!World->IsGameWorld()||IsNetMode(NM_DedicatedServer)) { UE_LOG(LogFPSDemo,Log,TEXT("HUB_INTERIOR_EXPOSURE_SKIP room=%s reason=NoRenderGameWorld"),Label); return; }
    if(Center.ContainsNaN()||Size.ContainsNaN()||Size.GetMin()<=0.||!FMath::IsFinite(Bias)) { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_INTERIOR_EXPOSURE_REJECT room=%s reason=InvalidGeometryOrBias"),Label); return; }
    const UDemoHubLightingSettings* Lighting=GetDefault<UDemoHubLightingSettings>();
    UBoxComponent* Box=NewObject<UBoxComponent>(this,NAME_None,RF_Transient); // Hub局部轴对齐的内净空间壳，无碰撞/重叠/导航影响。
    AddInstanceComponent(Box); Box->SetupAttachment(Root); Box->SetCollisionEnabled(ECollisionEnabled::NoCollision); Box->SetGenerateOverlapEvents(false); Box->SetCanEverAffectNavigation(false); Box->SetHiddenInGame(true);
    Box->SetRelativeLocation(Center+FVector(0,0,Size.Z*.5)); Box->SetBoxExtent(Size*.5,false); Box->RegisterComponent(); ExposurePieces.Add(Box);
    UPostProcessComponent* PP=NewObject<UPostProcessComponent>(this,NAME_None,RF_Transient); // 父级Shape决定范围；无碰撞时引擎按Box AABB距离判定。
    AddInstanceComponent(PP); PP->SetupAttachment(Box); PP->bUnbound=false; PP->bEnabled=true; PP->BlendWeight=1.f;
    PP->BlendRadius=FMath::Max(0.f,Lighting->BlendRadiusCm); PP->Priority=Lighting->Priority;
    PP->Settings.bOverride_AutoExposureBias=true; PP->Settings.AutoExposureBias=Bias; // 唯一覆盖字段。
    PP->RegisterComponent(); ExposurePieces.Add(PP);
    UE_LOG(LogFPSDemo,Log,TEXT("HUB_INTERIOR_EXPOSURE_READY room=%s center=%s extent=%s bias=%g blend=%g priority=%g"),Label,*Box->GetComponentLocation().ToString(),*Box->GetScaledBoxExtent().ToString(),Bias,PP->BlendRadius,PP->Priority);
}
bool ADemoHubShopBlockout::RebuildGeometry()
{
    DEMO_LOG_CALL(); FString Error; // 作者失败不留下可用假结构。
    for(USceneComponent* Piece:Pieces) if(Piece) Piece->DestroyComponent(); Pieces.Reset(); AnchorComponents.Reset();
    for(USceneComponent* Piece:ExposurePieces) if(Piece) Piece->DestroyComponent(); ExposurePieces.Reset(); // 曝光组件与几何同步撤销，避免重复叠加。
    bUsingAuthoredEnvironment=false; EnvironmentSelectionError.Reset();
    if(!Definition||!Cube||!Definition->Validate(Error))
    {
        ADemoHubShopEnvironmentRoot::ResolveForWorld(GetWorld(),nullptr,GetActorTransform(),EnvironmentSelectionError);
        UE_LOG(LogFPSDemo,Log,TEXT("HUB_BLOCKOUT_NOT_READY %s"),*Error); return false;
    }
    bUsingAuthoredEnvironment=ADemoHubShopEnvironmentRoot::ResolveForWorld(GetWorld(),Definition,GetActorTransform(),EnvironmentSelectionError);
    if(!bUsingAuthoredEnvironment) // 仅纯外观走legacy；独立Root/WorldEpoch和下面16逻辑锚点不受环境选择控制。
    {
    AddRoom(Definition->GunShopCenter,Definition->GunShopInteriorSize); AddRoom(Definition->CasinoCenter,Definition->CasinoInteriorSize);
    if(Definition->LayoutRevision==3)
    {
        AddBlock(Definition->GunShopCenter+FVector(550,-380,52.5),FVector(300,70,105)); // R03仅南东远角瘦柜台，固定400cm主道及北枪墙前方不摆大掩体。
        Pieces.Last()->ComponentTags.Add(TEXT("HubGunCounterR03"));
        AddBlock(Definition->GunShopCenter+FVector(0,Definition->GunShopInteriorSize.Y*.5-2,155),FVector(460,4,130),false); // 北墙内面2100，背板前面2096；墙体仍负责阻挡，背板不复制新碰撞。
        Pieces.Last()->ComponentTags.Add(TEXT("HubGunBackboardR03"));
        UE_LOG(LogFPSDemo,Log,TEXT("HUB_BLOCKOUT_LAYOUT_R03 floor_cm=%s counter=slim north_backboard_front_y=%.1f"),*Definition->HubFloorSizeCm.ToString(),Definition->GunShopCenter.Y+Definition->GunShopInteriorSize.Y*.5-4.);
    }
    else
    {
        AddBlock(Definition->GunShopCenter+FVector(0,-400,55),FVector(260,120,110)); // 保留旧R02几何，仅供明确LayoutRevision0的历史资产读回。
        AddBlock(Definition->GunShopCenter+FVector(450,400,55),FVector(260,120,110));
        AddBlock(Definition->GunShopCenter+FVector(670,450,55),FVector(150,220,110));
    }
    UTextRenderComponent* GunSign=NewObject<UTextRenderComponent>(this,NAME_None,RF_Transient); // 真实入口英文标牌，中文本地UI另用运行字体。
    AddInstanceComponent(GunSign); GunSign->SetupAttachment(Root); GunSign->SetRelativeLocation(Definition->GunShopCenter+FVector(-Definition->GunShopInteriorSize.X*.5-30,0,310)); GunSign->SetRelativeRotation(FRotator(0,180,0)); GunSign->SetWorldSize(35); GunSign->SetHorizontalAlignment(EHTA_Center); GunSign->SetText(FText::FromString(TEXT("GUN SHOP"))); GunSign->SetTextRenderColor(FColor::Orange); GunSign->RegisterComponent(); Pieces.Add(GunSign);
    UTextRenderComponent* CasinoSign=NewObject<UTextRenderComponent>(this,NAME_None,RF_Transient); // 娱乐屋独立入口，不随枪店未来封门。
    AddInstanceComponent(CasinoSign); CasinoSign->SetupAttachment(Root); CasinoSign->SetRelativeLocation(Definition->CasinoCenter+FVector(-Definition->CasinoInteriorSize.X*.5-30,0,310)); CasinoSign->SetRelativeRotation(FRotator(0,180,0)); CasinoSign->SetWorldSize(32); CasinoSign->SetHorizontalAlignment(EHTA_Center); CasinoSign->SetText(FText::FromString(TEXT("GOLD ARCADE"))); CasinoSign->SetTextRenderColor(FColor::Cyan); CasinoSign->RegisterComponent(); Pieces.Add(CasinoSign);
    }
    {
        const UDemoHubLightingSettings* Lighting=GetDefault<UDemoHubLightingSettings>(); // 只读CDO配置，各端同一打包值；授权/旧白模两路都生效。
        if(Lighting->bInteriorExposureEnabled)
        {
            AddInteriorExposure(Definition->GunShopCenter,Definition->GunShopInteriorSize,Lighting->GunShopExposureBias,TEXT("Gun"));
            AddInteriorExposure(Definition->CasinoCenter,Definition->CasinoInteriorSize,Lighting->CasinoExposureBias,TEXT("Casino"));
        }
        else UE_LOG(LogFPSDemo,Log,TEXT("HUB_INTERIOR_EXPOSURE_SKIP reason=Disabled"));
    }
    for(const FDemoHubShopAnchor& Anchor:Definition->Anchors)
    {
        USceneComponent* Point=NewObject<USceneComponent>(this,NAME_None,RF_Transient); // 逻辑稳定ID存Tag/索引；唯一对象名避免同帧重建覆盖尚未GC的组件。
        AddInstanceComponent(Point); Point->ComponentTags.Add(Anchor.AnchorId); Point->SetupAttachment(Root); Point->SetRelativeTransform(Anchor.Transform); Point->RegisterComponent(); Pieces.Add(Point); AnchorComponents.Add(Anchor.AnchorId,Point);
    }
    UE_LOG(LogFPSDemo,Log,TEXT("HUB_BLOCKOUT_READY geometry=%d anchors=%d entrance_cm=%.1f authored_environment=%d"),Pieces.Num(),AnchorComponents.Num(),Definition->EntranceClearWidthCm,bUsingAuthoredEnvironment); return true;
}
void ADemoHubShopBlockout::OnConstruction(const FTransform& Transform) { DEMO_LOG_CALL(); Super::OnConstruction(Transform); if(Definition) RebuildGeometry(); }
void ADemoHubShopBlockout::BeginPlay() { DEMO_LOG_CALL(); Super::BeginPlay(); OnRep_Definition(); OnRep_WorldEpoch(); }
void ADemoHubShopBlockout::OnRep_Definition() { DEMO_LOG_CALL(); RebuildGeometry(); }
void ADemoHubShopBlockout::OnRep_WorldEpoch() { DEMO_LOG_CALL(); if(GetWorld()) if(UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()) Registry->ReceiveWorldEpoch(this,WorldEpoch); }
void ADemoHubShopBlockout::EndPlay(const EEndPlayReason::Type Reason) { DEMO_LOG_CALL(); if(GetWorld()) if(UDemoHubShopWorldSubsystem* Registry=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()) Registry->ReceiveWorldEpoch(this,0); Super::EndPlay(Reason); }
// OutLifetimeProps为引擎复制字段输出，名称遵循DOREPLIFETIME宏契约；不改变布局复制范围。
void ADemoHubShopBlockout::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ADemoHubShopBlockout,Definition); DOREPLIFETIME(ADemoHubShopBlockout,WorldEpoch); }
void ADemoHubShopBlockout::SetWorldEpoch(uint32 InWorldEpoch) { DEMO_LOG_CALL(); if(HasAuthority()) { WorldEpoch=InWorldEpoch; ForceNetUpdate(); } }
int32 ADemoHubShopBlockout::GetGeometryCount() const { DEMO_LOG_TICK(); return Pieces.Num(); }
int32 ADemoHubShopBlockout::GetAuthorEpoch() const { DEMO_LOG_TICK(); return static_cast<int32>(WorldEpoch); }
bool ADemoHubShopBlockout::IsUsingAuthoredEnvironment() const { DEMO_LOG_TICK(); return bUsingAuthoredEnvironment; }
FString ADemoHubShopBlockout::GetEnvironmentSelectionError() const { DEMO_LOG_TICK(); return EnvironmentSelectionError; }
bool ADemoHubShopBlockout::GetAnchorTransform(FName AnchorId,FTransform& OutTransform) const { DEMO_LOG_TICK(); const TWeakObjectPtr<USceneComponent>* Point=AnchorComponents.Find(AnchorId); if(!Point||!Point->IsValid()) return false; OutTransform=Point->Get()->GetComponentTransform(); return true; }
