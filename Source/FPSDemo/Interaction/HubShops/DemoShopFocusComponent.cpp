// 职责：PC本人枪墙焦点/只读UMG/私有整体遮罩纯白外轮廓与可靠交互意图。只在本机Camera描二值mask外沿，不画枪身接缝；服务端仍独立重验交易。
#include "Interaction/HubShops/DemoShopFocusComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Interaction/HubShops/Rendering/DemoLocalOutlineComponent.h" // PC唯一生产渲染持有者，目标/Requester隔离，不直接操作相机PP。
#include "UI/DemoShopFocusCard.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Economy/DemoShopComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Save/DemoRunSave.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h" // 可信目录必须为PostProcess域，拒绝旧背面壳材质重新描出内部线。
#include "MaterialDomain.h" // Material.h只前置声明枚举；运行时须显式包含真实PostProcess域定义。
#include "Camera/CameraComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h" // 静态视觉工厂用显式Pawn上下文，不能在静态函数使用this入口宏。

UDemoShopFocusComponent::UDemoShopFocusComponent()
{ DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.bTickEvenWhenPaused=true; SetIsReplicatedByDefault(true); }
ADemoPlayerController* UDemoShopFocusComponent::PC() const { DEMO_LOG_TICK(); return Cast<ADemoPlayerController>(GetOwner()); }
float UDemoShopFocusComponent::NoticeSeconds() const
{
    DEMO_LOG_TICK();
    const UDemoHubShopWorldSubsystem* World=GetWorld()?GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>():nullptr;
    const UDemoHubShopDefinition* Data=World?World->GetDefinition():nullptr;
    return Data?Data->ErrorSeconds:2.f;
}
void UDemoShopFocusComponent::BeginPlay() { DEMO_LOG_CALL(); Super::BeginPlay(); }
bool UDemoShopFocusComponent::HasEnhancedInteraction() const { DEMO_LOG_TICK(); return bInputInstalled; }
bool UDemoShopFocusComponent::InstallInput(UInputComponent* Input)
{
    DEMO_LOG_CALL();
    if (bInputInstalled) return true;
    if (!PC() || !PC()->IsLocalController()) return false;
    UEnhancedInputComponent* Enhanced=Cast<UEnhancedInputComponent>(Input); // 已有PC EnhancedInput组件，不另建键轮询器。
    const UDemoHubShopInputSettings* Settings=GetDefault<UDemoHubShopInputSettings>();
    InteractAction=Settings->InteractAction.LoadSynchronous(); Mapping=Settings->Mapping.LoadSynchronous();
    ULocalPlayer* Local=PC()->GetLocalPlayer();
    UEnhancedInputLocalPlayerSubsystem* InputSystem=Local?Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
    if (!Enhanced || !InteractAction || !Mapping || !InputSystem) { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_INPUT missing author Action/Mapping; legacy configurable key remains")); return false; }
    InputSystem->AddMappingContext(Mapping,1); Enhanced->BindAction(InteractAction,ETriggerEvent::Started,this,&UDemoShopFocusComponent::OnInteract);
    bInputInstalled=true; return true;
}
FString UDemoShopFocusComponent::InteractionKeyLabel() const
{
    DEMO_LOG_TICK();
    if (Mapping) for (const FEnhancedActionKeyMapping& Item:Mapping->GetMappings()) if (Item.Action==InteractAction) return Item.Key.GetDisplayName().ToString();
    return GetDefault<UDemoHubShopInputSettings>()->FallbackKey.GetDisplayName().ToString();
}
void UDemoShopFocusComponent::OnInteract()
{
    DEMO_LOG_CALL();
    if (ADemoCharacter* Player=PC()?Cast<ADemoCharacter>(PC()->GetPawn()):nullptr) Player->TriggerWorldInteraction(); // 原Character统一分发，避免同E又买枪又开终端。
}

void UDemoShopFocusComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTick)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,ThisTick);
    if (!PC() || !PC()->IsLocalController()) { SetFocus(nullptr); return; }
    if (PC()->IsPaused()) { SetFocus(nullptr); Candidate.Reset(); return; } // 单机真暂停仍只做清理，不推进扫描/报价/心跳或World时钟。
    const double Now=GetWorld()->GetTimeSeconds(); // 扫描、滞回与租约均World游戏秒，暂停不偷耗时。
    UDemoHubShopWorldSubsystem* World=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    const UDemoHubShopDefinition* Data=World?World->GetDefinition():nullptr;
    const FDemoPlayerMenuSnapshot& Menu=PC()->GetMenuSnapshot(); // 仅本人可靠设备会话，心跳不延长原玩法UTC截止。
    if (Menu.CasinoDeviceToken.IsValid() && PC()->GetMenuFlow()->IsCasinoDevicePage() && Now>=NextHeartbeatAt)
    { NextHeartbeatAt=Now+(Data?Data->DeviceHeartbeatSeconds:2.f); ServerDeviceHeartbeat(Menu.CasinoDeviceId,Menu.CasinoDeviceToken,Menu.HubWorldEpoch); }
    if (const UDemoCasinoDeviceInputComponent* Physical=PC()->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); Physical && Physical->IsOperating()) { SetFocus(nullptr); Candidate.Reset(); return; } // 独立Operate自身保活/选控件，不同时显示旧整机E卡或发双心跳。
    ADemoCharacter* Player=Cast<ADemoCharacter>(PC()->GetPawn());
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    if (FocusAvatar.Get()!=Player) { SetFocus(nullptr); Candidate.Reset(); FocusAvatar=Player; }
    if (!Focus.IsValid() && Card) SetFocus(nullptr); // 被瞄准Actor卸载后弱引用为空，也须撤旧卡与轮廓。
    if (Highlight && (!IsValid(Highlight) || !HighlightedDisplay.IsValid() || !HighlightSource.IsValid() || Highlight->GetOwner()!=Player || !OutlineCamera.IsValid())) UpdateGunHighlight(nullptr);
    if (!Player || !Data || !State || State->Phase!=EDemoPhase::Hub || !Player->CanUsePlayerSkills() || !PC()->IsCoopRegistered())
    { SetFocus(nullptr); Candidate.Reset(); return; }
    if (!bHighlightLoadAttempted)
    {
        bHighlightLoadAttempted=true; HighlightMaterial=Data->HighlightMaterial.LoadSynchronous(); HighlightMaskMaterial=Data->HighlightMaskMaterial.LoadSynchronous(); // 同一可信DA一次加载两层，不逐帧读盘。
        if (!HighlightMaterial || !HighlightMaterial->GetMaterial() || HighlightMaterial->GetMaterial()->MaterialDomain!=MD_PostProcess
            || !HighlightMaskMaterial || !HighlightMaskMaterial->GetMaterial() || HighlightMaskMaterial->GetMaterial()->MaterialDomain!=MD_PostProcess)
        {
            UE_LOG(LogFPSDemo,Warning,TEXT("HUB_GUN_OUTLINE_MATERIAL_UNAVAILABLE composite=%s capture_mask=%s reason=MissingOrNonPostProcess"),*Data->HighlightMaterial.ToSoftObjectPath().ToString(),*Data->HighlightMaskMaterial.ToSoftObjectPath().ToString());
            HighlightMaterial=nullptr; HighlightMaskMaterial=nullptr; // 缺任一层不回退旧A04或每帧重读，交互/报价仍正常。
        }
    }
    if (Now>=NextScanAt)
    {
        NextScanAt=Now+1./Data->ScanHz;
        FVector Eye; FRotator Rotation; PC()->GetPlayerViewPoint(Eye,Rotation); // 本机只用于候选，服务器独立采自己Pawn眼位。
        const FVector Forward=Rotation.Vector();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(HubShopFocus),false,Player);
        FHitResult Direct; GetWorld()->LineTraceSingleByChannel(Direct,Eye,Eye+Forward*Data->InteractionDistanceCm,ECC_Visibility,Query);
        // 同步捕获本帧眼位/Query/Data；只筛注册候选，绝不缓存裸指针或在回调中交易。
        const auto Eligible=[this,Player,&Eye,&Forward,&Query,Data](AActor* Target,float Limit,float& Angle,float& Distance)->bool
        {
            DEMO_LOG_TICK();
            const ADemoGunDisplay* Gun=Cast<ADemoGunDisplay>(Target); const ADemoCasinoDevice* Device=Cast<ADemoCasinoDevice>(Target);
            if ((!Gun || !Gun->IsDisplayReady()) && (!Device || !Device->IsDeviceReady())) return false;
            const FVector Point=Gun?Gun->GetFocusPoint():Device->GetFocusPoint(); // 作者实际交互锚点，非Actor地板中心。
            const FVector Offset=Point-Eye; Distance=Offset.Size();
            Angle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward,Offset.GetSafeNormal()),-1.,1.)));
            if (Distance>Data->InteractionDistanceCm || Angle>Limit) return false;
            FCollisionQueryParams LOS=Query; LOS.AddIgnoredActor(Target); FHitResult Hit;
            return !GetWorld()->LineTraceSingleByChannel(Hit,Eye,Point,ECC_Visibility,LOS); // 墙和其他玩家照常阻挡，不延迟遮挡取消。
        };
        float HeldAngle=0.f,HeldDistance=0.f; // 当前焦点滞回校验，失效当次立即清理。
        if (Focus.IsValid() && !Eligible(Focus.Get(),Direct.GetActor()==Focus.Get()?180.f:Data->ReleaseHalfAngleDeg,HeldAngle,HeldDistance)) SetFocus(nullptr);
        TArray<ADemoGunDisplay*> Guns; TArray<ADemoCasinoDevice*> Devices; World->GatherNearby(Eye,Data->InteractionDistanceCm,Guns,Devices);
        AActor* Best=nullptr; float BestAngle=100.f,BestDistance=MAX_flt; bool bBestDirect=false; // 有界三枪七设备候选确定排序。
        // 捕获本帧排序结果及Eligible，仅同步调用；优先实际准星碰撞，其次角度/距离/稳定身份。
        const auto Consider=[&](AActor* Item)
        {
            DEMO_LOG_TICK(); float Angle=0.f,Distance=0.f;
            const bool bDirect=Direct.GetActor()==Item;
            if (!Eligible(Item,bDirect?180.f:Data->FocusHalfAngleDeg,Angle,Distance)) return; // 真实首次准星命中优先；只有软候选受中心4度约束。
            const FName Id=Cast<ADemoGunDisplay>(Item)?CastChecked<ADemoGunDisplay>(Item)->DisplayId:CastChecked<ADemoCasinoDevice>(Item)->DeviceId;
            const FName BestId=Cast<ADemoGunDisplay>(Best)?CastChecked<ADemoGunDisplay>(Best)->DisplayId:Cast<ADemoCasinoDevice>(Best)?CastChecked<ADemoCasinoDevice>(Best)->DeviceId:NAME_None;
            if (!Best || (bDirect&&!bBestDirect) || (bDirect==bBestDirect && (Angle<BestAngle || (FMath::IsNearlyEqual(Angle,BestAngle)&& (Distance<BestDistance || (FMath::IsNearlyEqual(Distance,BestDistance)&&Id.LexicalLess(BestId)))))))
            { Best=Item; BestAngle=Angle; BestDistance=Distance; bBestDirect=bDirect; }
        };
        for (ADemoGunDisplay* Item:Guns) Consider(Item); for (ADemoCasinoDevice* Item:Devices) Consider(Item);
        if (Best!=Focus.Get())
        {
            if (Candidate.Get()!=Best) { Candidate=Best; CandidateSince=Now; }
            if (Best && Now-CandidateSince>=Data->FocusStableSeconds) SetFocus(Best);
        }
        else Candidate.Reset();
        ADemoGunDisplay* AimedGun=Cast<ADemoGunDisplay>(Direct.GetActor()); // 准星首次命中既有紧包络查询盒；不把4/6度软候选当瞄准。
        float AimedAngle=0.f,AimedDistance=0.f; // 沿既有距离/LOS与稳定注册再核，仅用于本机视觉，不作购买授权。
        if (!AimedGun || World->FindDisplay(AimedGun->DisplayId)!=AimedGun || !Eligible(AimedGun,180.f,AimedAngle,AimedDistance)) AimedGun=nullptr;
        UpdateGunHighlight(AimedGun);
    }
    if (ADemoGunDisplay* Gun=Cast<ADemoGunDisplay>(Focus.Get())) if (Now>=NextQuoteAt && PendingOperation.IsEmpty())
    { NextQuoteAt=Now+1.; ServerQueryGun(Gun->DisplayId,World->GetWorldEpoch()); }
    UpdateCard();
}

void UDemoShopFocusComponent::SetFocus(AActor* Target)
{
    DEMO_LOG_TICK(); // 失焦门禁可逐帧调用；保留限时埋点，实际切换才输出默认业务日志。
    if (!Target) UpdateGunHighlight(nullptr);
    if (Focus.Get()==Target && (Target || !Card)) return;
    UE_LOG(LogFPSDemo,Log,TEXT("HUB_FOCUS changed old=%s target=%s"),*GetNameSafe(Focus.Get()),*GetNameSafe(Target));
    if (Card) { Card->RemoveFromParent(); Card=nullptr; }
    Focus=Target; CurrentQuote=FDemoHubGunQuote(); NextQuoteAt=0.;
    if (!Target || !PC() || !PC()->IsLocalController()) return;
    Card=CreateWidget<UDemoShopFocusCard>(PC(),UDemoShopFocusCard::StaticClass());
    if (Card) Card->AddToPlayerScreen(35);
}
void UDemoShopFocusComponent::UpdateGunHighlight(ADemoGunDisplay* Display)
{
    DEMO_LOG_TICK();
    APawn* Viewer=PC()?PC()->GetPawn():nullptr; // 当前本人Avatar拥有可见Primitive，不能挂在默认隐藏的Controller上。
    UPrimitiveComponent* Source=IsValid(Display)?Display->GetDisplayMesh():nullptr;
    UDemoLocalOutlineComponent* Renderer=PC()?PC()->FindComponentByClass<UDemoLocalOutlineComponent>():nullptr;
    if (!Viewer || !Source || !HighlightMaterial || !HighlightMaskMaterial || !Renderer) Display=nullptr;
    if (!Display && Renderer) Renderer->ClearOutlineTarget(this); // 仅撤本请求，Operate的新LocalView目标不能被枪Focus空值清理。
    if (HighlightedDisplay.Get()==Display && (Display?(IsValid(Highlight)&&HighlightSource.Get()==Source&&Highlight->GetOwner()==Viewer&&OutlineCamera.IsValid()&&Renderer->OwnsOutlineTarget(this,Highlight)):(!Highlight&&!SilhouetteBlendable&&!OutlineCamera.IsValid())))
    { if(Display) SilhouetteBlendable=Renderer->GetCompositeForOwner(this); return; } // resize/RT暂不可见仍持同一请求，不循环销毁mask；诊断缓存随Renderer更新。
    UE_LOG(LogFPSDemo,Log,TEXT("HUB_GUN_OUTLINE changed old=%s target=%s"),*GetNameSafe(HighlightedDisplay.Get()),*GetNameSafe(Display));
    if (Renderer) Renderer->ClearOutlineTarget(this); // 先撤采集/合成借用，才销毁原本人Mask；不触碰其他Requester的资源。
    if (IsValid(Highlight)) Highlight->DestroyComponent();
    SilhouetteBlendable=nullptr; OutlineCamera.Reset();
    Highlight=nullptr; HighlightedDisplay.Reset(); HighlightSource.Reset();
    if (!Display) return;
    BuildHighlight(Display);
    if (Highlight) { HighlightedDisplay=Display; HighlightSource=Source; }
}
void UDemoShopFocusComponent::BuildHighlight(ADemoGunDisplay* Display)
{
    DEMO_LOG_CALL();
    UPrimitiveComponent* Source=Display?Display->GetDisplayMesh():nullptr;
    const UDemoHubShopWorldSubsystem* World=GetWorld()?GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>():nullptr;
    const UDemoHubShopDefinition* Data=World?World->GetDefinition():nullptr;
    UDemoLocalOutlineComponent* Renderer=PC()?PC()->FindComponentByClass<UDemoLocalOutlineComponent>():nullptr;
    if (!Source || !HighlightMaterial || !HighlightMaskMaterial || !Renderer || !PC() || !PC()->GetPawn() || !Data) { UE_LOG(LogFPSDemo,Log,TEXT("HUB_HIGHLIGHT unavailable source/composite/capture_mask/renderer/avatar/definition")); return; }
    ADemoCharacter* Viewer=Cast<ADemoCharacter>(PC()->GetPawn()); // 正式当前Avatar已有实际第一人称Camera；不碰主程序相机计算。
    UCameraComponent* Camera=Viewer?Viewer->GetFirstPersonCameraComponent():nullptr;
    if (!Camera || !HighlightMaterial->GetMaterial() || HighlightMaterial->GetMaterial()->MaterialDomain!=MD_PostProcess || !HighlightMaskMaterial->GetMaterial() || HighlightMaskMaterial->GetMaterial()->MaterialDomain!=MD_PostProcess)
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_SILHOUETTE_REJECT camera/postprocess_material")); return; }
    Highlight=CreateLocalGunSilhouetteMask(Viewer,Source);
    if (!Highlight) return;
    const bool Accepted=Renderer->SetOutlineTarget(this,Viewer,Camera,Source,Highlight,HighlightMaterial,HighlightMaskMaterial); // 生命周期/可见性归唯一Renderer；此处不造第二个MID或相机PP写入者。
    if (!Accepted&&!Renderer->OwnsOutlineTarget(this,Highlight)) { Renderer->ClearOutlineTarget(this); Highlight->DestroyComponent(); Highlight=nullptr; UE_LOG(LogFPSDemo,Warning,TEXT("HUB_SILHOUETTE_REJECT renderer_target")); return; }
    SilhouetteBlendable=Renderer->GetCompositeForOwner(this); OutlineCamera=Camera;
}
UPrimitiveComponent* UDemoShopFocusComponent::CreateLocalGunSilhouetteMask(APawn* ViewerPawn,UPrimitiveComponent* Source)
{
    DEMO_LOG_STATIC_CALL(ViewerPawn);
    if (!IsValid(ViewerPawn)||!IsValid(Source)||ViewerPawn->GetWorld()!=Source->GetWorld()||ViewerPawn->GetNetMode()==NM_DedicatedServer)
    { UE_LOG(LogFPSDemo,Log,TEXT("HUB_GUN_OUTLINE_REJECT source/viewer/world")); return nullptr; }
    UPrimitiveComponent* Outline=nullptr; // 本次创建的本人瞬态副本，成功后由ViewerPawn拥有、Focus组件按瞄准生命周期清理。
    if (UStaticMeshComponent* Static=Cast<UStaticMeshComponent>(Source))
    {
        if (!Static->GetStaticMesh()) { UE_LOG(LogFPSDemo,Log,TEXT("HUB_GUN_OUTLINE_REJECT missing_static_mesh")); return nullptr; }
        UStaticMeshComponent* Clone=NewObject<UStaticMeshComponent>(ViewerPawn,NAME_None,RF_Transient); Clone->SetStaticMesh(Static->GetStaticMesh()); Outline=Clone;
    }
    else if (USkeletalMeshComponent* Skin=Cast<USkeletalMeshComponent>(Source))
    {
        if (!Skin->GetSkeletalMeshAsset()) { UE_LOG(LogFPSDemo,Log,TEXT("HUB_GUN_OUTLINE_REJECT missing_skeletal_mesh")); return nullptr; }
        USkeletalMeshComponent* Clone=NewObject<USkeletalMeshComponent>(ViewerPawn,NAME_None,RF_Transient);
        Clone->SetSkeletalMesh(Skin->GetSkeletalMeshAsset()); Clone->bComponentUseFixedSkelBounds=true;
        Clone->bPauseAnims=true; Clone->SetComponentTickEnabled(false); Clone->SetLeaderPoseComponent(Skin,true,false); Outline=Clone;
    }
    if (!Outline) { UE_LOG(LogFPSDemo,Log,TEXT("HUB_GUN_OUTLINE_REJECT unsupported_source")); return nullptr; }
    Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision); Outline->SetGenerateOverlapEvents(false); Outline->SetCastShadow(false); Outline->SetIsReplicated(false);
    Outline->SetOnlyOwnerSee(true); Outline->SetRenderInMainPass(false); Outline->SetRenderInDepthPass(false);
    Outline->SetRenderCustomDepth(true); Outline->SetCustomDepthStencilValue(253); // 本功能专用值，只写本人无复制副本；项目一次启用Stencil，不逐次切全局CVar。
    Outline->SetHiddenInGame(false); Outline->SetWorldTransform(Source->GetComponentTransform());
    Outline->SetBoundsScale(Source->BoundsScale); // 无WPO/膨胀，遮罩与原枪包络一致。
    for (int32 Index=0;Index<Source->GetNumMaterials();++Index) Outline->SetMaterial(Index,Source->GetMaterial(Index)); // 只借原材质做遮罩深度，不向可见通道染色。
    ViewerPawn->AddInstanceComponent(Outline); Outline->RegisterComponent();
    return Outline;
}
void UDemoShopFocusComponent::UpdateCard()
{
    DEMO_LOG_TICK();
    if (!Card || !Focus.IsValid() || !PC()) return;
    const UDemoHubShopDefinition* Data=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()->GetDefinition();
    ADemoGunDisplay* Gun=Cast<ADemoGunDisplay>(Focus.Get()); ADemoCasinoDevice* Device=Cast<ADemoCasinoDevice>(Focus.Get());
    if (!Data || (!Gun&&!Device)) return;
    const double Now=GetWorld()->GetTimeSeconds();
    FString First,Second,Third; // 三行投影，无列表/鼠标或本人战斗加成。
    if (Gun)
    {
        First=CurrentQuote.Name.IsEmpty()?TEXT("正在读取商品…"):CurrentQuote.Name+TEXT(" · ")+CurrentQuote.Type;
        Second=CurrentQuote.Pellets>1?FString::Printf(TEXT("伤害 %g × %d  ·  %g RPM"),double(CurrentQuote.Damage),CurrentQuote.Pellets,double(CurrentQuote.RPM)):FString::Printf(TEXT("伤害 %g  ·  %g RPM"),double(CurrentQuote.Damage),double(CurrentQuote.RPM));
        Third=CurrentQuote.bOwned?FString::Printf(TEXT("[%s] 免费装备 · 保留当前弹量"),*InteractionKeyLabel()):FString::Printf(TEXT("%d 金币 · [%s] 购买并装备"),CurrentQuote.Price,*InteractionKeyLabel());
        if (!PendingOperation.IsEmpty()) Third=TEXT("正在保存购买…");
        else if (!CurrentQuote.Code.IsNone()) Third=CurrentQuote.Message;
    }
    else { First=DemoCasino::ModeName(Device->Mode); Second=TEXT("独立娱乐设备 · 原玩法规则"); Third=FString::Printf(TEXT("[%s] 进入"),*InteractionKeyLabel()); }
    if (!Notice.IsEmpty() && Now<NoticeUntil) Third=Notice;
    const float DPI=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(PC())); int32 Width=0,Height=0; PC()->GetViewportSize(Width,Height);
    const float Scale=FMath::Max(.4f,float(Height)/1080.f)/DPI; const FVector2D Size=Data->CardSizePx*Scale;
    FVector2D Screen; const FVector Point=Gun?Gun->GetInfoPoint():Device->GetInfoPoint();
    if (!PC()->ProjectWorldLocationToScreen(Point,Screen,true)) { Card->SetVisibility(ESlateVisibility::Collapsed); return; }
    Screen/=DPI; const FVector2D View(float(Width)/DPI,float(Height)/DPI); const float Margin=Data->ScreenMarginPx*Scale;
    FVector2D Position(Screen.X+28*Scale,Screen.Y-Size.Y*.5f);
    if (Position.X+Size.X>View.X-Margin) Position.X=Screen.X-Size.X-28*Scale;
    const FVector2D Center=View*.5f;
    if (Position.X<Center.X+32*Scale && Position.X+Size.X>Center.X-32*Scale && Position.Y<Center.Y+32*Scale && Position.Y+Size.Y>Center.Y-32*Scale)
        Position.X=Center.X+56*Scale+Size.X<View.X-Margin?Center.X+56*Scale:Center.X-Size.X-56*Scale;
    Position.X=FMath::Clamp(Position.X,Margin,FMath::Max(Margin,View.X-Size.X-Margin)); Position.Y=FMath::Clamp(Position.Y,Margin,FMath::Max(Margin,View.Y-Size.Y-Margin));
    Card->SetVisibility(ESlateVisibility::HitTestInvisible); Card->SetLines(First,Second,Third,Size); Card->SetPositionInViewport(Position,false);
}

bool UDemoShopFocusComponent::TryConsumeInteraction()
{
    DEMO_LOG_CALL();
    if (PC() && PC()->IsLocalController()) if (UDemoCasinoDeviceInputComponent* Physical=PC()->FindComponentByClass<UDemoCasinoDeviceInputComponent>()) if (Physical->TryConsumeInteraction()) return true; // 在无旧Focus早退前处理，批准Operate时原Focus已撤。
    if (!PC() || !PC()->IsLocalController() || !Focus.IsValid()) return false;
    ADemoCharacter* Player=Cast<ADemoCharacter>(PC()->GetPawn());
    if (!Player || !Player->CanUsePlayerSkills()) return false;
    if (!PendingOperation.IsEmpty() || bOpeningDevice) return true;
    UDemoHubShopWorldSubsystem* World=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    if (ADemoGunDisplay* Gun=Cast<ADemoGunDisplay>(Focus.Get()))
    {
        if (CurrentQuote.DisplayId!=Gun->DisplayId || !CurrentQuote.SlotEpoch.IsValid() || !CurrentQuote.Code.IsNone())
        { Notice=CurrentQuote.Message.IsEmpty()?TEXT("正在等待服务器报价"):CurrentQuote.Message; NoticeUntil=GetWorld()->GetTimeSeconds()+NoticeSeconds(); NextQuoteAt=0.; return true; }
        FDemoHubGunIntent Intent; Intent.OperationId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); Intent.DisplayId=Gun->DisplayId; Intent.OfferId=CurrentQuote.OfferId;
        Intent.WorldEpoch=CurrentQuote.WorldEpoch; Intent.SlotEpoch=CurrentQuote.SlotEpoch; Intent.QuoteKey=CurrentQuote.QuoteKey; Intent.ExpectedPrice=CurrentQuote.Price; Intent.ExpectedShopSerial=CurrentQuote.ShopSerial;
        PendingOperation=Intent.OperationId; ServerPurchaseGun(Intent); return true;
    }
    if (ADemoCasinoDevice* Device=Cast<ADemoCasinoDevice>(Focus.Get())) { bOpeningDevice=true; ServerOpenDevice(Device->DeviceId,World->GetWorldEpoch()); return true; }
    return false;
}
FGuid UDemoShopFocusComponent::ResolveSlotEpoch()
{
    DEMO_LOG_CALL();
    const UDemoRunSaves* Saves=GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>();
    const UDemoRunSave* Slot=Saves?Saves->GetSlot(Saves->GetActiveSlot()):nullptr;
    if (!Slot) { SlotEpoch.Invalidate(); SlotStorageKey.Empty(); return SlotEpoch; }
    if (SlotStorageKey!=Slot->SlotStorageKey || !SlotEpoch.IsValid() || EpochAvatar.Get()!=PC()->GetPawn()) { SlotStorageKey=Slot->SlotStorageKey; EpochAvatar=PC()->GetPawn(); SlotEpoch=FGuid::NewGuid(); }
    return SlotEpoch;
}
void UDemoShopFocusComponent::ServerQueryGun_Implementation(FName DisplayId,uint32 WorldEpoch)
{
    DEMO_LOG_CALL();
    const double Now=GetWorld()->GetTimeSeconds(); if (Now-LastServerQueryAt<.05) return; LastServerQueryAt=Now;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    if (Mode&&PC()) ClientGunQuote(Mode->GetShopSystem()->QueryGunOffer(Cast<ADemoCharacter>(PC()->GetPawn()),DisplayId,WorldEpoch,ResolveSlotEpoch()));
}
void UDemoShopFocusComponent::ClientGunQuote_Implementation(const FDemoHubGunQuote& Quote)
{
    DEMO_LOG_CALL();
    const ADemoGunDisplay* Gun=Cast<ADemoGunDisplay>(Focus.Get());
    const UDemoHubShopWorldSubsystem* World=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    if (PC()&&PC()->IsLocalController()&&Gun&&Gun->DisplayId==Quote.DisplayId&&World&&World->GetWorldEpoch()==Quote.WorldEpoch) CurrentQuote=Quote;
}
void UDemoShopFocusComponent::ServerPurchaseGun_Implementation(const FDemoHubGunIntent& Intent)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    if (Mode&&PC()) ClientGunResult(Mode->GetShopSystem()->PurchaseOrEquipGun(Cast<ADemoCharacter>(PC()->GetPawn()),Intent,ResolveSlotEpoch()));
    else { FDemoHubGunResult Rejected; /* 生命周期拒绝也必须结束本机等待，不让可靠请求永远挂起。 */ Rejected.OperationId=Intent.OperationId; Rejected.Result=FDemoCommandResult::Reject(TEXT("NotReady"),TEXT("玩家或商店已离开当前World")); ClientGunResult(Rejected); }
}
void UDemoShopFocusComponent::ClientGunResult_Implementation(const FDemoHubGunResult& Result)
{
    DEMO_LOG_CALL();
    if (!PC() || !PC()->IsLocalController() || Result.OperationId!=PendingOperation) return;
    PendingOperation.Empty(); Notice=Result.Result.Message; NoticeUntil=GetWorld()->GetTimeSeconds()+NoticeSeconds(); NextQuoteAt=0.;
    const UDemoHubShopWorldSubsystem* World=GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>();
    if (const ADemoGunDisplay* Gun=Cast<ADemoGunDisplay>(Focus.Get())) if (Result.Quote.DisplayId==Gun->DisplayId&&World&&World->GetWorldEpoch()==Result.Quote.WorldEpoch) CurrentQuote=Result.Quote; // 成交事实仍可提示，旧World报价不能覆盖新焦点。
    if (UDemoUIManager* UI=PC()->FindComponentByClass<UDemoUIManager>()) UI->PresentCommandResult(EDemoGameplayCommand::EquipWeapon,Result.Result); // 沿原本人短通知，无第二次业务执行。
}
void UDemoShopFocusComponent::ServerOpenDevice_Implementation(FName DeviceId,uint32 WorldEpoch)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    if (!Mode || !PC()) return;
    const FDemoCommandResult Result=Mode->GetShopSystem()->OpenCasinoDevice(Cast<ADemoCharacter>(PC()->GetPawn()),DeviceId,WorldEpoch);
    ClientDeviceOpened(Result,Mode->BuildPlayerMenuSnapshot(PC()));
}
void UDemoShopFocusComponent::ClientDeviceOpened_Implementation(const FDemoCommandResult& Result,const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL(); bOpeningDevice=false;
    const ADemoCasinoDevice* Device=Cast<ADemoCasinoDevice>(Focus.Get());
    if (!Result.IsApplied()) { Notice=Result.Message; NoticeUntil=GetWorld()->GetTimeSeconds()+NoticeSeconds(); return; }
    if (!PC() || !PC()->IsLocalController() || !Device || Snapshot.CasinoDeviceId!=Device->DeviceId || PC()->HasBlockingOverlay())
    { ServerCloseDevice(Snapshot.CasinoDeviceToken); return; }
    PC()->AcceptHubShopMenuSnapshot(Snapshot);
    if (Snapshot.CasinoControlsRevision)
    {
        UDemoCasinoDeviceInputComponent* Physical=PC()->FindComponentByClass<UDemoCasinoDeviceInputComponent>();
        if (!Physical || !Physical->BeginOperate(Snapshot)) { ServerCloseDevice(Snapshot.CasinoDeviceToken); Notice=TEXT("实体操作件尚未就绪，请重新交互"); NoticeUntil=GetWorld()->GetTimeSeconds()+NoticeSeconds(); }
    }
    else PC()->GetMenuFlow()->OpenCasinoDevicePage(Snapshot); // 原Page租约保持P0兼容，客户端不能擅降级Physical租约绕验证。
    SetFocus(nullptr);
}
void UDemoShopFocusComponent::ServerDeviceHeartbeat_Implementation(FName DeviceId,const FGuid& Token,uint32 WorldEpoch)
{
    DEMO_LOG_CALL();
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    if (Mode&&PC()&&!Mode->GetShopSystem()->RenewCasinoDevice(Cast<ADemoCharacter>(PC()->GetPawn()),DeviceId,Token,WorldEpoch)) PC()->PublishMenuSnapshot();
}
void UDemoShopFocusComponent::ServerCloseDevice_Implementation(const FGuid& Token)
{
    DEMO_LOG_CALL();
    if(!Token.IsValid()) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_CLOSE rejected empty token")); return; }
    if (AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) if (PC()) Mode->GetShopSystem()->CloseCasinoDevice(Cast<ADemoCharacter>(PC()->GetPawn()),Token);
}
void UDemoShopFocusComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); SetFocus(nullptr);
    if (PC()&&PC()->GetLocalPlayer()) if (UEnhancedInputLocalPlayerSubsystem* Input=PC()->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) if (Mapping) Input->RemoveMappingContext(Mapping);
    if (GetOwner()->HasAuthority()) if (AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) if (PC()) Mode->GetShopSystem()->CloseCasinoDevice(Cast<ADemoCharacter>(PC()->GetPawn()));
    Super::EndPlay(Reason);
}
