// 职责：B05脱敏目标槽与独立本地屏幕标签。依赖原敌人/PS/Session/R04字体；不使用元素材质，不改变Actor旋转或伤害。
#include "GAS/Talents/Builds/DemoCrossfireTargetState.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "AI/DemoEnemy.h"
#include "Player/DemoPlayerState.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UI/DemoUIStyle.h"
#include "Debug/DemoStaticLog.h"

void UDemoCrossfireTargetWidget::NativeOnInitialized()
{
    DEMO_LOG_CALL();
    Super::NativeOnInitialized();
    EnsureLabel();
}
void UDemoCrossfireTargetWidget::EnsureLabel()
{
    DEMO_LOG_CALL();
    if (Label) return;
    if (!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CrossfireLabel"));
    Label->SetFont(DemoUI::TextFont(16,TEXT("Medium")));
    Label->SetColorAndOpacity(DemoUI::Gold); Label->SetJustification(ETextJustify::Center);
    Label->SetShadowColorAndOpacity(FLinearColor(0,0,0,.95f)); Label->SetShadowOffset(FVector2D(1.f,1.f));
    Label->SetVisibility(ESlateVisibility::HitTestInvisible); Label->SetAutoWrapText(false);
    WidgetTree->RootWidget=Label;
    SetVisibility(ESlateVisibility::HitTestInvisible); // 标记不抢鼠标/手柄焦点或阻止射击输入。
}
void UDemoCrossfireTargetWidget::SetFeedbackText(const FText& Text)
{
    DEMO_LOG_TICK();
    if (!Label) EnsureLabel();
    if (Label && !Label->GetText().EqualTo(Text)) Label->SetText(Text);
}
UDemoCrossfireTargetState::UDemoCrossfireTargetState()
{
    DEMO_LOG_CALL();
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickInterval=.1f; // 每目标最多4来源，只在公开标记活跃时启用。
}
UDemoCrossfireTargetState* UDemoCrossfireTargetState::Find(ADemoEnemy* Target,bool bCreate)
{
    DEMO_LOG_STATIC_TICK(Target);
    if (!IsValid(Target)) return nullptr;
    UDemoCrossfireTargetState* State=Target->FindComponentByClass<UDemoCrossfireTargetState>(); // 同敌人共用一个组件，来源槽由PS精确区分。
    if (!State && bCreate && Target->HasAuthority() && Target->IsAlive() && Target->IsCombatSessionValid())
    {
        State=NewObject<UDemoCrossfireTargetState>(Target,TEXT("CrossfireTargetState"));
        Target->AddInstanceComponent(State); State->RegisterComponent();
    }
    return State;
}
void UDemoCrossfireTargetState::BeginPlay()
{
    DEMO_LOG_CALL();
    Super::BeginPlay();
    if (ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()))
    {
        FVector Center,Extent; Enemy->GetActorBounds(false,Center,Extent); // 首次尚未创建本组件Widget，不把反馈几何递归计入高度。
        MarkerOffset=Center-Enemy->GetActorLocation()+FVector(0,0,FMath::Clamp(Extent.Z,40.,500.)+25.f);
        Enemy->OnDefeated.AddUObject(this,&UDemoCrossfireTargetState::OnTargetDefeated);
    }
    OnRep_Sources();
}
bool UDemoCrossfireTargetState::SetSource(UDemoRunBuildComponent* Source,float EndTime,bool bReverse)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 只公开真实受击目标，不支持任意Actor挂标。
    ADemoPlayerState* PS=Source?Cast<ADemoPlayerState>(Source->GetOwner()):nullptr; // 脱敏来源只取本人PS，无账号/存档正文。
    if (!Enemy || !Enemy->HasAuthority() || !Enemy->IsAlive() || !Enemy->IsCombatSessionValid() || !Source || !Source->IsEncounterEnabled()
        || !PS || PS->GetWorld()!=GetWorld() || !FMath::IsFinite(EndTime) || EndTime<=ServerTime())
    { UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_PUBLIC_REJECT authority/target/source/expiry")); return false; }
    PruneSources();
    for (FDemoCrossfirePublicSource& Existing:Sources)
        if (Existing.OwnerState==PS)
        { Existing.EndTime=EndTime; Existing.bReverse=bReverse; OnRep_Sources(); GetOwner()->ForceNetUpdate(); return true; }
    if (Sources.Num()>=4) { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_PUBLIC_REJECT target-source-limit=4")); return false; }
    FDemoCrossfirePublicSource& Added=Sources.AddDefaulted_GetRef(); // 一个来源只占一个槽，复制不包含内部伤害预算。
    Added.OwnerState=PS; Added.EndTime=EndTime; Added.bReverse=bReverse;
    OnRep_Sources(); GetOwner()->ForceNetUpdate();
    return true;
}
void UDemoCrossfireTargetState::RemoveSource(UDemoRunBuildComponent* Source)
{
    DEMO_LOG_CALL();
    if (!GetOwner()->HasAuthority() || !Source) return;
    const ADemoPlayerState* PS=Cast<ADemoPlayerState>(Source->GetOwner()); // 按原PS精确撤自己，不让队友消费清掉其他来源。
    bool bChanged=false; // 一次修改只发布一次，无Source时幂等。
    for (int32 Index=Sources.Num()-1;Index>=0;--Index)
        if (Sources[Index].OwnerState==PS) { Sources.RemoveAt(Index); bChanged=true; }
    if (bChanged) { OnRep_Sources(); GetOwner()->ForceNetUpdate(); }
}
void UDemoCrossfireTargetState::PruneSources()
{
    DEMO_LOG_TICK();
    if (!GetOwner()->HasAuthority()) return;
    const ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 目标撤销/死亡立即清公开事实，不能把标记带入下一场。
    bool bChanged=false;
    for (int32 Index=Sources.Num()-1;Index>=0;--Index)
    {
        const ADemoPlayerState* PS=Sources[Index].OwnerState; // 复制引用不延长断线Avatar的有效权限。
        const UDemoRunBuildComponent* Build=PS?PS->GetRunBuildComponent():nullptr;
        const ADemoCharacter* Pawn=Build && Build->GetASC()?Cast<ADemoCharacter>(Build->GetASC()->GetAvatarActor()):nullptr;
        if (!Enemy || !Enemy->IsAlive() || !Enemy->IsCombatSessionValid() || Sources[Index].EndTime<=ServerTime() || !Build || !Build->IsEncounterEnabled()
            || !Pawn || Pawn->GetWorld()!=GetWorld() || Pawn->IsCoopEliminated() || !Pawn->GetDemoAttributes() || Pawn->GetDemoAttributes()->GetHealth()<=0.f)
        { Sources.RemoveAt(Index); bChanged=true; }
    }
    if (bChanged) { OnRep_Sources(); GetOwner()->ForceNetUpdate(); }
}
void UDemoCrossfireTargetState::OnTargetDefeated(ADemoEnemy* Target)
{
    DEMO_LOG_CALL();
    if (Target!=GetOwner()) return;
    if (GetOwner()->HasAuthority()) { Sources.Reset(); GetOwner()->ForceNetUpdate(); }
    DestroyWidgets(); SetComponentTickEnabled(false); // 不触碰Runtime私有mark，原致死攻击回执仍可正确消费凭据。
}
void UDemoCrossfireTargetState::OnRep_Sources()
{
    DEMO_LOG_CALL();
    SetComponentTickEnabled(!Sources.IsEmpty());
    if (Sources.IsEmpty()) DestroyWidgets();
    else RefreshWidgets();
}
float UDemoCrossfireTargetState::ServerTime() const
{
    DEMO_LOG_TICK();
    const AGameStateBase* State=GetWorld()?GetWorld()->GetGameState<AGameStateBase>():nullptr; // 每台机器使用同步World秒，不读本地时间。
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0.f;
}
void UDemoCrossfireTargetState::RefreshWidgets()
{
    DEMO_LOG_TICK();
    if (!GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer) return;
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner());
    UGameInstance* GI=GetWorld()->GetGameInstance(); // 本World已有本机玩家目录，不查询Player0或把标签归给来源玩家视口。
    if (!Enemy || !Enemy->IsAlive() || !GI) { DestroyWidgets(); return; }
    const FDemoCrossfirePublicSource* Earliest=nullptr; int32 ActiveCount=0; // 同目标只展示最早有效来源，避免4行遮住敌人。
    for (const FDemoCrossfirePublicSource& Source:Sources)
        if (Source.EndTime>ServerTime() && Source.OwnerState)
        { ++ActiveCount; if (!Earliest || Source.EndTime<Earliest->EndTime || (Source.EndTime==Earliest->EndTime && Source.OwnerState->GetPlayerId()<Earliest->OwnerState->GetPlayerId())) Earliest=&Source; }
    if (!Earliest) { DestroyWidgets(); return; } // 未解析PS/迟到过期时不显示虚构身份，下个有限Tick可恢复。
    const FString Name=Earliest->OwnerState->GetPlayerName().Left(10); // 固定短名，防长名把目标反馈撑满视口。
    const FString Count=ActiveCount>1?FString::Printf(TEXT(" ×%d"),ActiveCount):FString();
    const FText Text=FText::FromString(FString::Printf(TEXT("破势·%s%s\n%s  %.1fs"),Earliest->bReverse?TEXT("近战"):TEXT("射击"),*Count,*Name,FMath::Max(0.f,Earliest->EndTime-ServerTime()))); // 仅公开截止，没有伤害/私人状态。
    for (ULocalPlayer* LocalPlayer:GI->GetLocalPlayers())
    {
        APlayerController* PC=LocalPlayer?LocalPlayer->GetPlayerController(GetWorld()):nullptr; // 每个本机视口独立相机与遮挡，不共享ActorRotation。
        if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager) continue;
        UWidgetComponent* Marker=nullptr;
        for (UWidgetComponent* Existing:Markers) if (IsValid(Existing) && Existing->GetOwnerPlayer()==LocalPlayer) { Marker=Existing; break; }
        if (!Marker)
        {
            Marker=NewObject<UWidgetComponent>(Enemy); Enemy->AddInstanceComponent(Marker); Marker->SetupAttachment(Enemy->GetRootComponent());
            Marker->SetWidgetSpace(EWidgetSpace::Screen); Marker->SetDrawAtDesiredSize(true); Marker->SetPivot(FVector2D(.5f,1.f));
            Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision); Marker->SetOwnerPlayer(LocalPlayer); Marker->SetRelativeLocation(MarkerOffset);
            Marker->RegisterComponent();
            Marker->SetWidget(CreateWidget<UDemoCrossfireTargetWidget>(PC,UDemoCrossfireTargetWidget::StaticClass()));
            Markers.Add(Marker);
        }
        if (UDemoCrossfireTargetWidget* Widget=Cast<UDemoCrossfireTargetWidget>(Marker->GetUserWidgetObject())) Widget->SetFeedbackText(Text);
        const FVector Eye=PC->PlayerCameraManager->GetCameraLocation(); // 仅显示检查，不改变服务器命中或破势权限。
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CrossfireMarkerVisibility),false,PC->GetPawn()); Query.AddIgnoredActor(Enemy);
        FHitResult Wall; const bool bBlocked=GetWorld()->LineTraceSingleByChannel(Wall,Eye,Enemy->GetActorLocation(),ECC_Visibility,Query);
        Marker->SetVisibility(!bBlocked && FVector::DistSquared(Eye,Enemy->GetActorLocation())<=FMath::Square(5000.f)); // 局部目标反馈不透墙揭示敌人，最远50m。
    }
    for (int32 Index=Markers.Num()-1;Index>=0;--Index)
        if (!IsValid(Markers[Index]) || !GI->GetLocalPlayers().Contains(Markers[Index]->GetOwnerPlayer()))
        { if (IsValid(Markers[Index])) Markers[Index]->DestroyComponent(); Markers.RemoveAt(Index); } // 退出分屏/本机玩家移除不遗留标签。
}
void UDemoCrossfireTargetState::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    DEMO_LOG_TICK();
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if (GetOwner()->HasAuthority()) PruneSources();
    if (!Sources.IsEmpty()) RefreshWidgets();
}
void UDemoCrossfireTargetState::DestroyWidgets()
{
    DEMO_LOG_CALL();
    for (UWidgetComponent* Marker:Markers) if (IsValid(Marker)) Marker->DestroyComponent();
    Markers.Reset();
}
void UDemoCrossfireTargetState::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    SetComponentTickEnabled(false); DestroyWidgets();
    if (ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner())) Enemy->OnDefeated.RemoveAll(this);
    Super::EndPlay(Reason);
}
void UDemoCrossfireTargetState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL();
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDemoCrossfireTargetState,Sources);
}
