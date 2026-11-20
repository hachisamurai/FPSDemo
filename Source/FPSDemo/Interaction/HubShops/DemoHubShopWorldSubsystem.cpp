// 职责：本World商店注册/初次距角LOS与双端物理设备存续几何、一次Hub生成；复用原区域/阶段，任何实体不获经济/伤害权限，不改相机或玩家进度。
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h" // 已获物理租约的几何只借用可信Profile，不把控件或钱包搬进World注册服务。
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h" // 非Unity中核Controller当前Pawn，不能仅依赖Character前置声明。
#include "TimerManager.h"

bool UDemoHubShopWorldSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ DEMO_LOG_CALL(); return Type==EWorldType::Game||Type==EWorldType::PIE; }
void UDemoHubShopWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Super::Initialize(Collection); Collection.InitializeDependency<UDemoEncounterAreaSubsystem>();
    FString Error; Definition=GetDefault<UDemoHubShopSettings>()->LoadDefinition(Error); // World强引用冻结资源，不在焦点轮询读盘。
    if(GetWorld()->GetNetMode()!=NM_Client)
    {
        static uint32 NextWorldEpoch=1; // 游戏线程进程内World初始代数；跨World仍另核Pawn/Actor归属。
        WorldEpoch=NextWorldEpoch; NextWorldEpoch=NextWorldEpoch>=static_cast<uint32>(MAX_int32-1024)?1:NextWorldEpoch+1024;
    }
}
void UDemoHubShopWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    DEMO_LOG_CALL(); Super::OnWorldBeginPlay(InWorld);
    if(InWorld.GetNetMode()==NM_Client||!Definition||!Definition->bPurchaseRoomEnabled) return;
    InWorld.GetTimerManager().SetTimer(PollTimer,this,&UDemoHubShopWorldSubsystem::TryCreateBlockout,.5f,true);
}
void UDemoHubShopWorldSubsystem::Deinitialize()
{
    DEMO_LOG_CALL(); bStopping=true; if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(PollTimer);
    ClearGenerated(); Displays.Reset(); Devices.Reset(); ConflictedDisplays.Reset(); ConflictedDevices.Reset(); Blockout.Reset(); Definition=nullptr; WorldEpoch=0; Super::Deinitialize();
}
void UDemoHubShopWorldSubsystem::ClearGenerated()
{ DEMO_LOG_CALL(); for(const TWeakObjectPtr<AActor>& Actor:Generated) if(Actor.IsValid()) Actor->Destroy(); Generated.Reset(); }
const UDemoHubShopDefinition* UDemoHubShopWorldSubsystem::GetDefinition() const { DEMO_LOG_TICK(); return Definition; }
uint32 UDemoHubShopWorldSubsystem::GetWorldEpoch() const { DEMO_LOG_TICK(); return WorldEpoch; }
ADemoGunDisplay* UDemoHubShopWorldSubsystem::FindDisplay(FName DisplayId) const
{ DEMO_LOG_TICK(); if(ConflictedDisplays.Contains(DisplayId)) return nullptr; const TWeakObjectPtr<ADemoGunDisplay>* Found=Displays.Find(DisplayId); return Found&&Found->IsValid()&&Found->Get()->IsDisplayReady()?Found->Get():nullptr; }
ADemoCasinoDevice* UDemoHubShopWorldSubsystem::FindDevice(FName DeviceId) const
{ DEMO_LOG_TICK(); if(ConflictedDevices.Contains(DeviceId)) return nullptr; const TWeakObjectPtr<ADemoCasinoDevice>* Found=Devices.Find(DeviceId); return Found&&Found->IsValid()&&Found->Get()->IsDeviceReady()?Found->Get():nullptr; }
void UDemoHubShopWorldSubsystem::BumpEpoch()
{
    DEMO_LOG_CALL(); if(bStopping||!GetWorld()||GetWorld()->GetNetMode()==NM_Client) return;
    WorldEpoch=WorldEpoch>=static_cast<uint32>(MAX_int32)?1:WorldEpoch+1; if(Blockout.IsValid()) Blockout->SetWorldEpoch(WorldEpoch);
}
void UDemoHubShopWorldSubsystem::RegisterDisplay(ADemoGunDisplay* Display)
{
    DEMO_LOG_CALL(); const FDemoHubDisplayBinding* Binding=Definition&&Display?Definition->FindDisplay(Display->DisplayId):nullptr; // 只认可信DA身份。
    if(bStopping||!IsValid(Display)||Display->GetWorld()!=GetWorld()||!Display->IsDisplayReady()||!Binding||Binding->OfferId!=Display->OfferId||Binding->ShopId!=Display->ShopId)
    { UE_LOG(LogFPSDemo,Log,TEXT("HUB_DISPLAY_REGISTER_REJECT invalid/identity/world")); return; }
    TWeakObjectPtr<ADemoGunDisplay>* Previous=Displays.Find(Display->DisplayId); // 当前注册值同步借用。
    if(Previous&&Previous->Get()==Display) return;
    if(Previous&&Previous->IsValid()) { ConflictedDisplays.Add(Display->DisplayId); BumpEpoch(); UE_LOG(LogFPSDemo,Warning,TEXT("HUB_DISPLAY_ID_CONFLICT id=%s"),*Display->DisplayId.ToString()); return; }
    Displays.Add(Display->DisplayId,Display); BumpEpoch();
}
void UDemoHubShopWorldSubsystem::UnregisterDisplay(ADemoGunDisplay* Display)
{
    DEMO_LOG_CALL(); if(!Display) return; const TWeakObjectPtr<ADemoGunDisplay>* Previous=Displays.Find(Display->DisplayId); // 不撤销同名其他实例。
    if(Previous&&Previous->Get()==Display) { Displays.Remove(Display->DisplayId); BumpEpoch(); }
}
void UDemoHubShopWorldSubsystem::RegisterDevice(ADemoCasinoDevice* Device)
{
    DEMO_LOG_CALL(); const FDemoHubCasinoBinding* Binding=Definition&&Device?Definition->FindDevice(Device->DeviceId):nullptr; // 原Mode身份只由DA签发。
    if(bStopping||!IsValid(Device)||Device->GetWorld()!=GetWorld()||!Device->IsDeviceReady()||!Binding||Binding->Mode!=Device->Mode)
    { UE_LOG(LogFPSDemo,Log,TEXT("HUB_DEVICE_REGISTER_REJECT invalid/identity/world")); return; }
    TWeakObjectPtr<ADemoCasinoDevice>* Previous=Devices.Find(Device->DeviceId); // 有界当前注册项。
    if(Previous&&Previous->Get()==Device) return;
    if(Previous&&Previous->IsValid()) { ConflictedDevices.Add(Device->DeviceId); BumpEpoch(); UE_LOG(LogFPSDemo,Warning,TEXT("HUB_DEVICE_ID_CONFLICT id=%s"),*Device->DeviceId.ToString()); return; }
    Devices.Add(Device->DeviceId,Device); BumpEpoch();
}
void UDemoHubShopWorldSubsystem::UnregisterDevice(ADemoCasinoDevice* Device)
{
    DEMO_LOG_CALL(); if(!Device) return; const TWeakObjectPtr<ADemoCasinoDevice>* Previous=Devices.Find(Device->DeviceId); // 持续局属于原个人账本，不在此删除。
    if(Previous&&Previous->Get()==Device) { Devices.Remove(Device->DeviceId); BumpEpoch(); }
}
void UDemoHubShopWorldSubsystem::ReceiveWorldEpoch(ADemoHubShopBlockout* Source,uint32 Epoch)
{
    DEMO_LOG_CALL(); if(bStopping||!IsValid(Source)||Source->GetWorld()!=GetWorld()||(Blockout.IsValid()&&Blockout.Get()!=Source)) return;
    Blockout=Source;
    if(GetWorld()->GetNetMode()==NM_Client) WorldEpoch=Epoch; // 客户端自己的注册通知不能提升/猜测服务器代数。
    if(Epoch==0) { WorldEpoch=0; Blockout.Reset(); }
}
void UDemoHubShopWorldSubsystem::GatherNearby(const FVector& Position,float Radius,TArray<ADemoGunDisplay*>& OutDisplays,TArray<ADemoCasinoDevice*>& OutDevices) const
{
    DEMO_LOG_TICK(); OutDisplays.Reset(); OutDevices.Reset();
    if(bStopping||!Definition||!Definition->bPurchaseRoomEnabled||!WorldEpoch||Position.ContainsNaN()||!FMath::IsFinite(Radius)||Radius<=0) return;
    const double RadiusSquared=FMath::Square(FMath::Min(Radius,500.f)); // 同一作者上限，不接受任意世界大扫描。
    for(const auto& Entry:Displays) if(ADemoGunDisplay* Display=FindDisplay(Entry.Key)) if(FVector::DistSquared(Position,Display->GetFocusPoint())<=RadiusSquared) OutDisplays.Add(Display);
    for(const auto& Entry:Devices) if(ADemoCasinoDevice* Device=FindDevice(Entry.Key)) if(FVector::DistSquared(Position,Device->GetFocusPoint())<=RadiusSquared) OutDevices.Add(Device);
}
bool UDemoHubShopWorldSubsystem::ValidateInteractionContext(const ADemoCharacter* Pawn,const AActor* Target,uint32 ExpectedEpoch,bool bRequireAuthority,FString& Error,FName& Code) const
{
    DEMO_LOG_TICK(); Error.Reset(); Code=NAME_None;
    const UWorld* World=GetWorld(); // 本服务只检查自身World，卸载晚到调用安全拒绝，不查Player0。
    const ADemoGameState* State=World?World->GetGameState<ADemoGameState>():nullptr; // 逻辑Hub，不按物理地图位置放行。
    if(bStopping||!World||!Definition||!Definition->bPurchaseRoomEnabled||!IsValid(Pawn)||Pawn->GetWorld()!=World||!Pawn->GetController()||Pawn->GetController()->GetPawn()!=Pawn||!IsValid(Target)||Target->GetWorld()!=World||(bRequireAuthority&&(World->GetNetMode()==NM_Client||!Pawn->HasAuthority()))) { Code=TEXT("NotReady"); Error=TEXT("商店交互已过期或玩家尚未就绪"); }
    else if(!ExpectedEpoch||ExpectedEpoch!=WorldEpoch) { Code=TEXT("StaleEpoch"); Error=TEXT("商店交互已过期，请重新对准目标"); }
    else if(!State||State->Phase!=EDemoPhase::Hub||State->Preparation.bActive||World->IsPaused()) { Code=TEXT("WrongPhase"); Error=TEXT("请在安全区空闲时操作商店"); }
    else if(Pawn->IsCoopEliminated()||!Pawn->GetDemoAttributes()||Pawn->GetDemoAttributes()->GetHealth()<=0) { Code=TEXT("NotAlive"); Error=TEXT("当前玩家无法操作商店"); }
    return Error.IsEmpty();
}
bool UDemoHubShopWorldSubsystem::ValidateUse(const ADemoCharacter* Pawn,const AActor* Target,uint32 ExpectedEpoch,FString& Error,FName* OutCode) const
{
    DEMO_LOG_CALL();
    FName Code; // 初次准入保持原权威限制/稳定失败码，存续检查不会放宽本入口。
    ValidateInteractionContext(Pawn,Target,ExpectedEpoch,true,Error,Code);
    FVector Focus; // 仅从注册Actor取得可信世界点。
    if(Error.IsEmpty())
    {
        if(const ADemoGunDisplay* Display=Cast<ADemoGunDisplay>(Target)) { if(FindDisplay(Display->DisplayId)!=Display) { Code=TEXT("UnknownDisplay"); Error=TEXT("枪墙展示已失效"); } else Focus=Display->GetFocusPoint(); }
        else if(const ADemoCasinoDevice* Device=Cast<ADemoCasinoDevice>(Target)) { if(FindDevice(Device->DeviceId)!=Device) { Code=TEXT("UnknownDevice"); Error=TEXT("娱乐设备已失效"); } else Focus=Device->GetFocusPoint(); }
        else { Code=TEXT("UnknownDisplay"); Error=TEXT("目标不是已注册的商店实体"); }
    }
    if(Error.IsEmpty())
    {
        const FVector Eyes=Pawn->GetSharedViewLocation(); const FRotator Rotation=Pawn->GetViewRotation(); // 与实际CalcCamera共用当前Avatar纯值眼位；保原Pawn控制视向，不读上一帧Camera或客户端上传原点。
        const FVector Delta=Focus-Eyes; const double Distance=Delta.Size(); // 世界cm，零距离不构造NaN方向。
        if(Eyes.ContainsNaN()||Rotation.ContainsNaN()||Focus.ContainsNaN()||!FMath::IsFinite(Distance)) { Code=TEXT("NotReady"); Error=TEXT("交互视点尚未就绪"); }
        else if(Distance>Definition->InteractionDistanceCm) { Code=TEXT("TooFar"); Error=TEXT("距离过远，请靠近设备"); }
        else
        {
            FCollisionQueryParams DirectQuery(SCENE_QUERY_STAT(HubShopDirect),false,Pawn); // 权威视线只忽略本人，不能跳过墙/队友伪造直接命中。
            FHitResult Direct; const bool bDirectTarget=GetWorld()->LineTraceSingleByChannel(Direct,Eyes,Eyes+Rotation.Vector()*Definition->InteractionDistanceCm,ECC_Visibility,DirectQuery)&&Direct.GetActor()==Target; // 只有服务器自己的首次阻挡确为该注册目标才绕中心角。
            if(!bDirectTarget&&Distance>KINDA_SMALL_NUMBER&&FVector::DotProduct(Rotation.Vector(),Delta/Distance)<FMath::Cos(FMath::DegreesToRadians(Definition->ReleaseHalfAngleDeg+Definition->ServerAngleToleranceDeg))) { Code=TEXT("NotFacingTarget"); Error=TEXT("请对准当前商店目标"); }
            else
            {
                FCollisionQueryParams Query(SCENE_QUERY_STAT(HubShopUse),false,Pawn); Query.AddIgnoredActor(Target); // 直接命中仍复核锚点LOS；其他Pawn/墙仍阻挡。
                FHitResult Cover; if(GetWorld()->LineTraceSingleByChannel(Cover,Eyes,Focus,ECC_Visibility,Query)) { Code=TEXT("NoLineOfSight"); Error=TEXT("交互目标被遮挡"); }
            }
        }
    }
    if(OutCode) *OutCode=Code;
    if(!Error.IsEmpty()) UE_LOG(LogFPSDemo,Log,TEXT("HUB_SHOP_USE_REJECT target=%s epoch=%u/%u reason=%s"),*GetNameSafe(Target),ExpectedEpoch,WorldEpoch,*Error);
    return Error.IsEmpty();
}
bool UDemoHubShopWorldSubsystem::ValidateCasinoMaintain(const ADemoCharacter* Pawn,const ADemoCasinoDevice* Device,uint32 ExpectedEpoch,FString& Error,FName* OutCode) const
{
    DEMO_LOG_TICK();
    FName Code; // 本地Tick和服务器心跳用同一失败码；Token/操作者仍由原租约服务独立检查。
    ValidateInteractionContext(Pawn,Device,ExpectedEpoch,false,Error,Code);
    const UDemoCasinoControlsComponent* Controls=Error.IsEmpty()?Device->FindComponentByClass<UDemoCasinoControlsComponent>():nullptr;
    const UDemoCasinoDeviceProfile* Profile=Controls?Controls->GetModelProfile():nullptr; // 正式Profile为可信静态几何；旧显式原型可为空。
    if(Error.IsEmpty()&&(FindDevice(Device->DeviceId)!=Device||!Controls||!Controls->IsReady())) { Code=TEXT("UnknownDevice"); Error=TEXT("当前实体设备已离开或控件尚未就绪"); }
    if(Error.IsEmpty())
    {
        const FVector Eyes=Pawn->GetSharedViewLocation(); // 当前Avatar同一纯值眼位，不取Aim方向，更不接受客户端上传坐标。
        const FVector Focus=Device->GetFocusPoint(); // 当前已注册设备的操作可达锚点；看向键/屏不影响这个LOS。
        const FTransform Frame=Device->GetActorTransform(); // 正式设备模型约定+X为操作前侧，保其实际World旋转/位置。
        const FVector Center=Profile?Frame.TransformPosition(Profile->CollisionTransform.GetTranslation()):Device->GetActorLocation(); // 实测柜体中心界定前侧半空间，原型用自身中心。
        const FVector Front=Frame.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal(); // 单位向量，距离仍按世界cm而非重复乘模型比例。
        const double Distance=FVector::Distance(Eyes,Focus); // 原InteractionDistanceCm球形范围与柜体前侧相交即当前操作区。
        if(Eyes.ContainsNaN()||Focus.ContainsNaN()||Center.ContainsNaN()||Front.IsNearlyZero()||!FMath::IsFinite(Distance)) { Code=TEXT("NotReady"); Error=TEXT("设备交互几何尚未就绪"); }
        else if(Distance>Definition->InteractionDistanceCm) { Code=TEXT("TooFar"); Error=TEXT("已离开机台操作范围"); }
        else if(FVector::DotProduct(Eyes-Center,Front)<-KINDA_SMALL_NUMBER) { Code=TEXT("WrongSide"); Error=TEXT("请回到机台前侧操作"); }
        else
        {
            FCollisionQueryParams Query(SCENE_QUERY_STAT(CasinoMaintainLOS),false,Pawn); Query.AddIgnoredActor(Device); // 只忽略本人及目标自体，墙/其他设备/队友仍是实际遮挡。
            FHitResult Cover; // 首个阻挡用于有限拒绝诊断，绝不穿透改选别的控件。
            if(GetWorld()->LineTraceSingleByChannel(Cover,Eyes,Focus,ECC_Visibility,Query))
            {
                Code=TEXT("NoLineOfSight"); Error=TEXT("机台操作面被遮挡");
                UE_LOG(LogFPSDemo,Log,TEXT("CASINO_MAINTAIN_OCCLUDED device=%s blocker=%s component=%s"),*Device->DeviceId.ToString(),*GetNameSafe(Cover.GetActor()),*GetNameSafe(Cover.GetComponent()));
            }
        }
    }
    if(OutCode) *OutCode=Code;
    if(!Error.IsEmpty()) UE_LOG(LogFPSDemo,Log,TEXT("CASINO_MAINTAIN_REJECT device=%s code=%s reason=%s"),*GetNameSafe(Device),*Code.ToString(),*Error);
    return Error.IsEmpty();
}
void UDemoHubShopWorldSubsystem::TryCreateBlockout()
{
    DEMO_LOG_TICK(); if(bStopping||bCreated||!Definition) return;
    UDemoEncounterAreaSubsystem* Areas=GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>(); // 原服务负责创建Hub，不由本服务绕过其准备。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 首次逻辑Hub才进入，不在大厅生成可交互设备。
    if(!Areas||!Areas->IsReady()||!State||State->Phase!=EDemoPhase::Hub) return;
    GetWorld()->GetTimerManager().ClearTimer(PollTimer);
    FDemoAreaSnapshot Area; FString Error; // 冻结当前实际作者Hub框，既有地板/终端全部保持。
    if(!Areas->ResolveArea(-1,Area,Error)) { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_BLOCKOUT_CREATE_REJECT %s"),*Error); return; }
    for(TActorIterator<ADemoHubShopBlockout> It(GetWorld());It;++It)
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_BLOCKOUT_CREATE_REJECT unexpected preplaced root; use authored review map only for inspection")); return; } // 正式主图由服务一次生成，防重复作者/运行两套碰撞。
    const FTransform HubFrame=Area.Transform; // 忽略原区域缩放的统一地面框，单位cm。
    ADemoHubShopBlockout* RootActor=GetWorld()->SpawnActorDeferred<ADemoHubShopBlockout>(ADemoHubShopBlockout::StaticClass(),HubFrame,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(RootActor) { Generated.Add(RootActor); Blockout=RootActor; }
    if(!RootActor||!RootActor->Configure(Definition,static_cast<int32>(WorldEpoch))) { ClearGenerated(); UE_LOG(LogFPSDemo,Warning,TEXT("HUB_BLOCKOUT_CREATE_REJECT root/configuration")); return; }
    RootActor->FinishSpawning(HubFrame);
    for(const FDemoHubDisplayBinding& Binding:Definition->Displays)
    {
        const FTransform Transform=Binding.Transform*HubFrame; // 当前Hub真实世界位置，不用默认全局坐标。
        ADemoGunDisplay* Display=GetWorld()->SpawnActorDeferred<ADemoGunDisplay>(ADemoGunDisplay::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Display) { ClearGenerated(); UE_LOG(LogFPSDemo,Warning,TEXT("HUB_BLOCKOUT_CREATE_REJECT display allocation")); return; }
        Generated.Add(Display); const bool bReady=Display->Configure(Definition,Binding.DisplayId); Display->FinishSpawning(Transform);
        if(!bReady) UE_LOG(LogFPSDemo,Warning,TEXT("HUB_BLOCKOUT_DISPLAY_DISABLED id=%s"),*Binding.DisplayId.ToString()); // 单枪资源失败只禁此展示，七娱乐入口继续。
    }
    for(const FDemoHubCasinoBinding& Binding:Definition->Devices)
    {
        const FTransform Transform=Binding.Transform*HubFrame; // 与作者保存的预览Map用同一局部变换。
        ADemoCasinoDevice* Device=GetWorld()->SpawnActorDeferred<ADemoCasinoDevice>(ADemoCasinoDevice::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Device) { ClearGenerated(); UE_LOG(LogFPSDemo,Warning,TEXT("HUB_BLOCKOUT_CREATE_REJECT device allocation")); return; }
        Generated.Add(Device); Device->Configure(Definition,Binding.DeviceId); Device->FinishSpawning(Transform);
    }
    bCreated=true; UE_LOG(LogFPSDemo,Log,TEXT("HUB_SHOPS_WORLD_READY epoch=%u displays=%d devices=%d frame=%s"),WorldEpoch,Displays.Num(),Devices.Num(),*HubFrame.GetLocation().ToString());
}
