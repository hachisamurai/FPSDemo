// 职责：Mode1可信控件白模及服务器真命中验证；查询件固定在注册Device上，Listen本机表现层无权移动这些组件。
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Characters/DemoCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

UDemoCasinoControlsComponent::UDemoCasinoControlsComponent()
{ DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; }
void UDemoCasinoControlsComponent::Clear()
{
    DEMO_LOG_CALL();
    for (UBoxComponent* Part:Queries) if (Part) Part->DestroyComponent();
    for (UActorComponent* Part:Parts) if (Part) Part->DestroyComponent();
    Queries.Reset(); Parts.Reset(); Bindings.Reset(); NeutralParts.Reset(); ModelProfile=nullptr; BuiltRevision=0; BuiltFingerprint=0; // LastModel身份保留，不能同版本静默换内容。
}
void UDemoCasinoControlsComponent::EndPlay(const EEndPlayReason::Type Reason)
{ DEMO_LOG_CALL(); Clear(); Super::EndPlay(Reason); }
bool UDemoCasinoControlsComponent::IsReady() const
{ DEMO_LOG_TICK(); return BuiltRevision!=0 && Bindings.Num()==15 && Queries.Num()==Bindings.Num() && (!bModelRequested || ModelProfile!=nullptr); }
uint32 UDemoCasinoControlsComponent::GetBindingRevision() const
{ DEMO_LOG_TICK(); return IsReady()?BuiltRevision:0; }
bool UDemoCasinoControlsComponent::IsModelRequested() const
{ DEMO_LOG_TICK(); return bModelRequested; }
const UDemoCasinoDeviceProfile* UDemoCasinoControlsComponent::GetModelProfile() const
{ DEMO_LOG_TICK(); return ModelProfile; }
UStaticMeshComponent* UDemoCasinoControlsComponent::GetNeutralPart(int32 Index) const
{ DEMO_LOG_TICK(); return NeutralParts.IsValidIndex(Index)?NeutralParts[Index].Get():nullptr; }
void UDemoCasinoControlsComponent::Configure(int32 DeviceMode)
{
    DEMO_LOG_CALL();
    ADemoCasinoDevice* Device=Cast<ADemoCasinoDevice>(GetOwner()); // 注册Actor所有权，不能在本机PC上建立权威查询替身。
    const UDemoCasinoPhysicalSettings* Settings=GetDefault<UDemoCasinoPhysicalSettings>(); // 仅Refresh/作者构造读资源，不在Tick加载。
    bModelRequested=DeviceMode==1 && !Settings->SlotsModelProfile.IsNull();
    if (!Device || DeviceMode!=1) { Clear(); return; }
    if (bModelRequested)
    {
        UDemoCasinoDeviceProfile* Candidate=Settings->SlotsModelProfile.LoadSynchronous(); FString Error; // 明确路径无效时不落回旧坐标。
        if (!Candidate || !Candidate->Validate(Error)) { Clear(); UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_MODEL_REJECT %s"),*Error); return; }
        const uint32 Revision=static_cast<uint32>(Candidate->BindingRevision),Fingerprint=Candidate->ContentFingerprint();
        if (LastModelRevision==Revision && (LastModelFingerprint!=Fingerprint || LastModelId!=Candidate->ProfileId))
        { Clear(); UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_MODEL_REJECT changed content must increase binding revision")); return; }
        if (IsReady() && ModelProfile==Candidate && BuiltRevision==Revision && BuiltFingerprint==Fingerprint) return;
        Clear(); ModelProfile=Candidate; Bindings=Candidate->Controls; BuiltRevision=Revision; BuiltFingerprint=Fingerprint;
        LastModelRevision=Revision; LastModelFingerprint=Fingerprint; LastModelId=Candidate->ProfileId; BuildModel();
        UE_LOG(LogFPSDemo,Log,TEXT("CASINO_MODEL_READY profile=%s revision=%u parts=%d"),*Candidate->ProfileId.ToString(),Revision,NeutralParts.Num()); return;
    }
    if (!DemoCasinoControls::IsSlotsPrototypeEnabled(GetWorld())) { Clear(); return; }
    if (IsReady() && !ModelProfile && BuiltRevision==DemoCasinoControls::BindingRevision()) return;
    Clear();
    for (int32 Digit=0;Digit<10;++Digit)
    {
        FDemoCasinoControlBinding Binding; // 十个独立稳定键；位置cm在柜体前面，不藏在BlockAll壳内。
        Binding.ControlId=FName(*FString::Printf(TEXT("amount.digit.%d"),Digit)); Binding.Action=EDemoCasinoControlAction::AmountDigit; Binding.Index=Digit; Binding.Label=FString::FromInt(Digit);
        Binding.Position=Digit==0?FVector(70,0,35):FVector(70,-30+((Digit-1)%3)*30,80-((Digit-1)/3)*15); Bindings.Add(Binding);
    }
    FDemoCasinoControlBinding ClearKey; ClearKey.ControlId=TEXT("amount.clear"); ClearKey.Action=EDemoCasinoControlAction::AmountClear; ClearKey.Label=TEXT("CLR"); ClearKey.Position=FVector(70,-30,35); Bindings.Add(ClearKey);
    FDemoCasinoControlBinding Back; Back.ControlId=TEXT("amount.backspace"); Back.Action=EDemoCasinoControlAction::AmountBackspace; Back.Label=TEXT("DEL"); Back.Position=FVector(70,30,35); Bindings.Add(Back);
    FDemoCasinoControlBinding Commit; Commit.ControlId=TEXT("amount.commit"); Commit.Action=EDemoCasinoControlAction::AmountCommit; Commit.Label=TEXT("SET BET"); Commit.Position=FVector(70,0,17); Commit.Extent=FVector(1.5,35,6); Bindings.Add(Commit);
    FDemoCasinoControlBinding Pull; Pull.ControlId=TEXT("slots.pull"); Pull.Action=EDemoCasinoControlAction::PullSlots; Pull.Label=TEXT("PULL / BET"); Pull.Position=FVector(70,73,140); Pull.Extent=FVector(8,18,23); Bindings.Add(Pull);
    FDemoCasinoControlBinding Exit; Exit.ControlId=TEXT("device.exit"); Exit.Action=EDemoCasinoControlAction::Exit; Exit.Label=TEXT("EXIT"); Exit.Position=FVector(70,73,80); Exit.Extent=FVector(8,18,8); Bindings.Add(Exit);
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")); // 仅Configure游戏线程一次加载引擎既有白模，不在Tick读盘。
    for (const FDemoCasinoControlBinding& Binding:Bindings)
    {
        UBoxComponent* Query=NewObject<UBoxComponent>(Device,NAME_None,RF_Transient); Device->AddInstanceComponent(Query); Query->SetupAttachment(Device->GetRootComponent()); Query->SetRelativeLocationAndRotation(Binding.Position,Binding.Rotation); Query->SetBoxExtent(Binding.Extent);
        Query->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Query->SetCollisionResponseToAllChannels(ECR_Ignore); Query->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); Query->SetGenerateOverlapEvents(false); Query->SetCanEverAffectNavigation(false); Query->SetHiddenInGame(true); Query->ComponentTags.Add(Binding.ControlId); Query->RegisterComponent(); Queries.Add(Query);
        UStaticMeshComponent* Face=NewObject<UStaticMeshComponent>(Device,NAME_None,RF_Transient); Device->AddInstanceComponent(Face); Face->SetupAttachment(Device->GetRootComponent()); Face->SetStaticMesh(Cube); Face->SetRelativeLocation(Binding.Position-FVector(1,0,0)); Face->SetRelativeScale3D(FVector(2,Binding.Extent.Y*2,Binding.Extent.Z*2)/100.); Face->SetCollisionEnabled(ECollisionEnabled::NoCollision); Face->SetCastShadow(false); Face->RegisterComponent(); Parts.Add(Face); // 可见前面与薄查询中心同一X平面，不用厚盒补偿近距离俯视视差。
        UTextRenderComponent* Text=NewObject<UTextRenderComponent>(Device,NAME_None,RF_Transient); Device->AddInstanceComponent(Text); Text->SetupAttachment(Device->GetRootComponent()); Text->SetRelativeLocation(Binding.Position+FVector(2,0,0)); Text->SetText(FText::FromString(Binding.Label)); Text->SetHorizontalAlignment(EHTA_Center); Text->SetVerticalAlignment(EVRTA_TextCenter); Text->SetWorldSize(7); Text->SetTextRenderColor(FColor(255,195,75)); Text->SetCollisionEnabled(ECollisionEnabled::NoCollision); Text->SetCastShadow(false); Text->RegisterComponent(); Parts.Add(Text);
    }
    BuiltRevision=DemoCasinoControls::BindingRevision(); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_PHYSICAL_CONTROLS_READY device=%s controls=%d revision=%u"),*Device->DeviceId.ToString(),Bindings.Num(),BuiltRevision);
}
void UDemoCasinoControlsComponent::BuildModel()
{
    DEMO_LOG_CALL(); ADemoCasinoDevice* Device=CastChecked<ADemoCasinoDevice>(GetOwner());
    for (const FDemoCasinoModelPart& Part:ModelProfile->Parts)
    {
        UStaticMeshComponent* Mesh=NewObject<UStaticMeshComponent>(Device,NAME_None,RF_Transient); Device->AddInstanceComponent(Mesh); Mesh->SetupAttachment(Device->GetRootComponent());
        Mesh->SetStaticMesh(Part.Mesh); Mesh->SetRelativeTransform(Part.RestTransform); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetGenerateOverlapEvents(false); Mesh->SetCanEverAffectNavigation(false);
        for (int32 Slot=0;Slot<Part.Materials.Num();++Slot) if (Part.Materials[Slot]) Mesh->SetMaterial(Slot,Part.Materials[Slot]);
        Mesh->RegisterComponent(); NeutralParts.Add(Mesh); Parts.Add(Mesh); // 公共中性姿态；私人反馈只替换本人视图，不改本组件。
    }
    for (const FDemoCasinoControlBinding& Binding:Bindings)
    {
        UBoxComponent* Query=NewObject<UBoxComponent>(Device,NAME_None,RF_Transient); Device->AddInstanceComponent(Query); Query->SetupAttachment(Device->GetRootComponent());
        Query->SetRelativeLocationAndRotation(Binding.Position,Binding.Rotation); Query->SetBoxExtent(Binding.Extent); Query->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Query->SetCollisionResponseToAllChannels(ECR_Ignore); Query->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); Query->SetGenerateOverlapEvents(false); Query->SetCanEverAffectNavigation(false); Query->SetHiddenInGame(true); Query->RegisterComponent(); Queries.Add(Query);
    }
}
const FDemoCasinoControlBinding* UDemoCasinoControlsComponent::FindHit(const UPrimitiveComponent* Component) const
{
    DEMO_LOG_TICK(); if (!IsReady()) return nullptr;
    for (int32 Index=0;Index<Queries.Num();++Index) if (Queries[Index]==Component) return &Bindings[Index]; return nullptr;
}
const FDemoCasinoControlBinding* UDemoCasinoControlsComponent::FindControl(FName ControlId) const
{ DEMO_LOG_TICK(); if (IsReady()) for (const FDemoCasinoControlBinding& Binding:Bindings) if (Binding.ControlId==ControlId) return &Binding; return nullptr; }
bool UDemoCasinoControlsComponent::GetControlPoint(FName ControlId,FVector& Point) const
{
    DEMO_LOG_TICK(); if (!IsReady()) return false;
    for (int32 Index=0;Index<Bindings.Num();++Index) if (Bindings[Index].ControlId==ControlId && Queries[Index]) { Point=Queries[Index]->GetComponentLocation(); return true; } return false;
}
bool UDemoCasinoControlsComponent::ValidateControlHit(ADemoCharacter* Pawn,FName ControlId,FString& Error) const
{
    DEMO_LOG_CALL(); Error=TEXT("请对准当前设备的实际操作件");
    const UDemoHubShopWorldSubsystem* Registry=GetWorld()?GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>():nullptr;
    const UDemoHubShopDefinition* Definition=Registry?Registry->GetDefinition():nullptr;
    if (!Pawn || !Pawn->HasAuthority() || !Definition || !FindControl(ControlId)) return false;
    const FVector Eyes=Pawn->GetSharedViewLocation(); const FRotator Aim=Pawn->GetViewRotation(); // 只取权威Avatar共享眼位/原控制视向，与本机画面一致；不收客户端位置/命中，不放宽首次阻挡。
    if (Eyes.ContainsNaN() || Aim.ContainsNaN()) return false;
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CasinoPhysicalControl),false,Pawn); // 不忽略墙/队友/其它控件，也不忽略本Device。
    if (!GetWorld()->LineTraceSingleByChannel(Hit,Eyes,Eyes+Aim.Vector()*Definition->InteractionDistanceCm,ECC_Visibility,Query)) return false;
    const FDemoCasinoControlBinding* Binding=FindHit(Hit.GetComponent());
    if (!Binding || Binding->ControlId!=ControlId) return false;
    Error.Empty(); return true;
}
