// 职责：PC本机服务管理私有金额屏、每键下压回弹及复用技美纯白PP的真实轮廓；Primitive归当前Pawn，全部NoCollision/不复制，不改共享Query或经济事实。
#include "Interaction/HubShops/Casino/DemoCasinoLocalViewComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlsComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Interaction/HubShops/DemoShopFocusComponent.h" // 复用既有本人Stencil253遮罩工厂，枪墙Operate时撤焦；不另造材质或共享网格写入者。
#include "Interaction/HubShops/Rendering/DemoLocalOutlineComponent.h" // 本PC唯一捕获/合成持有者，本服务只拥有自己的请求与原253mask。
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Economy/Gambling/DemoGambleTypes.h" // typed耐久回执校验；不读取/反解析LastResult文本。
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "MaterialDomain.h" // Material.h仅前置声明枚举，域检查必须显式包含PostProcess定义。
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h" // OnlyOwnerSee检查Primitive拥有者链是否包含当前Pawn ViewActor，不能将Primitive直接归PC导致完全不可见。
#include "Debug/DemoLog.h"

UDemoCasinoLocalViewComponent::UDemoCasinoLocalViewComponent()
{ DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; }
void UDemoCasinoLocalViewComponent::Open(ADemoCasinoDevice* Target,const FDemoGambleProgress& SavedProgress)
{
    DEMO_LOG_CALL(); Close();
    ADemoPlayerController* Controller=Cast<ADemoPlayerController>(GetOwner());
    if (!Controller || !Controller->IsLocalController() || !Controller->GetPawn() || !Target) return;
    APawn* PrimitiveOwner=Controller->GetPawn(); // 仅此Avatar生命周期；PC组件仍统一销毁，不改变共享Device归属或服务器查询件。
    const UDemoCasinoControlsComponent* Controls=Target->FindComponentByClass<UDemoCasinoControlsComponent>();
    ModelProfile=Controls?const_cast<UDemoCasinoDeviceProfile*>(Controls->GetModelProfile()):nullptr; PrimitiveAvatar=PrimitiveOwner;
    Device=Target; LastResult=FDemoCasinoResult(); RevealStarted=0.; LastText.Empty(); ReelAngles.Init(0.f,3); ReelStartAngles=ReelAngles;
    if (SavedProgress.ValidateLastSlotsReceipt() && SavedProgress.LastSlotsReceipt.Mode==1)
    {
        LastResult=SavedProgress.LastSlotsReceipt; // 原服务器已保存事实，首次进入/重进/读档都只定位结果，不重新开奖/播放一次新下注。
        RevealStarted=FPlatformTime::Seconds()-(ModelProfile?ModelProfile->RevealSeconds:.8f)-1.;
        if (ModelProfile)
            for (const FDemoCasinoModelPart& Part:ModelProfile->Parts)
                if (Part.Kind==EDemoCasinoModelPartKind::Reel && ReelAngles.IsValidIndex(Part.ReelIndex))
                    ReelAngles[Part.ReelIndex]=Part.SymbolStopAngles[LastResult.Symbols[Part.ReelIndex]];
        ReelStartAngles=ReelAngles;
        UE_LOG(LogFPSDemo,Log,TEXT("CASINO_SAVED_SLOTS_RESTORED serial=%d no_new_transaction=1"),LastResult.Serial);
    }
    Screen=NewObject<UTextRenderComponent>(PrimitiveOwner,NAME_None,RF_Transient); PrimitiveOwner->AddInstanceComponent(Screen); Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision); Screen->SetCastShadow(false); Screen->SetOnlyOwnerSee(true); Screen->SetVisibleInRayTracing(false); Screen->SetWorldSize(8); Screen->SetHorizontalAlignment(EHTA_Center); Screen->SetTextRenderColor(FColor(255,210,105)); Screen->RegisterComponent();
    if (ModelProfile)
    {
        Screen->SetWorldSize(ModelProfile->ScreenTextSizeCm); Screen->SetVerticalAlignment(EVRTA_TextCenter); // 同版16×8cm面板居中容纳三行大字，字号仍由正式Profile配置；不移动源挂点或Query。
        ModelParts.SetNum(ModelProfile->Parts.Num());
        for (int32 Index=0;Index<ModelProfile->Parts.Num();++Index)
        {
            const FDemoCasinoModelPart& Part=ModelProfile->Parts[Index]; // 同版rest与真实pivot，不复用原型Cube/固定坐标。
            if (Part.Kind==EDemoCasinoModelPartKind::Static) continue;
            UStaticMeshComponent* Neutral=Controls->GetNeutralPart(Index);
            if (!Neutral) { UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_MODEL_VIEW_REJECT missing neutral part")); Close(); return; }
            const TWeakObjectPtr<UPrimitiveComponent> HiddenEntry(Neutral); // 仅本人PC逐视图隐藏，不改Listen共享Actor可见性/Owner/材质。
            if (!Controller->HiddenPrimitiveComponents.Contains(HiddenEntry)) { Controller->HiddenPrimitiveComponents.Add(HiddenEntry); HiddenByThisView.Add(HiddenEntry); }
            UStaticMeshComponent* Mesh=NewObject<UStaticMeshComponent>(PrimitiveOwner,NAME_None,RF_Transient); PrimitiveOwner->AddInstanceComponent(Mesh); Mesh->SetStaticMesh(Part.Mesh);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetGenerateOverlapEvents(false); Mesh->SetCanEverAffectNavigation(false); Mesh->SetOnlyOwnerSee(true); Mesh->SetCastShadow(false); Mesh->SetVisibleInRayTracing(false);
            for (int32 Slot=0;Slot<Part.Materials.Num();++Slot) if (Part.Materials[Slot]) Mesh->SetMaterial(Slot,Part.Materials[Slot]);
            Mesh->RegisterComponent(); ModelParts[Index]=Mesh;
        }
        // 键帽导入材质没有可用Tint参数；真实边沿复用目录技美PP，只在首次合法焦点加载，不建立ASCII替代边框。
        Refresh(); return;
    }
    Lever=NewObject<UStaticMeshComponent>(PrimitiveOwner,NAME_None,RF_Transient); PrimitiveOwner->AddInstanceComponent(Lever); Lever->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"))); Lever->SetCollisionEnabled(ECollisionEnabled::NoCollision); Lever->SetCastShadow(false); Lever->SetOnlyOwnerSee(true); Lever->SetVisibleInRayTracing(false); Lever->SetWorldScale3D(FVector(.06,.12,.45)); Lever->RegisterComponent(); Refresh();
}
void UDemoCasinoLocalViewComponent::Close()
{
    DEMO_LOG_CALL();
    ClearFocusOutline(); // 必须先撤附着遮罩/相机MID，再销毁其源活动件与Avatar借用。
    if (ADemoPlayerController* Controller=Cast<ADemoPlayerController>(GetOwner()))
        for (const TWeakObjectPtr<UPrimitiveComponent>& Entry:HiddenByThisView) Controller->HiddenPrimitiveComponents.RemoveSingle(Entry); // 只撤自己插入的一项，不清相机或其它系统的隐藏表。
    HiddenByThisView.Reset(); for (UStaticMeshComponent* Part:ModelParts) if (Part) Part->DestroyComponent(); ModelParts.Reset(); ModelProfile=nullptr; PrimitiveAvatar.Reset();
    KeyPresses.Reset(); FocusedControl=NAME_None; bFocusedCanActivate=false; bWarnedKeyTiming=false; // 私有部件销毁，原公共件一直在Rest，退出不残留半按键。
    FocusOutlineMaterial=nullptr; FocusOutlineMaskMaterial=nullptr; bFocusOutlineLoadAttempted=false; // 仅下一次Open重读两条可信材质引用，失焦不重复加载。
    if (Screen) Screen->DestroyComponent(); if (Lever) Lever->DestroyComponent(); Screen=nullptr; Lever=nullptr; Device.Reset(); LastText.Empty();
}
const FDemoCasinoResult& UDemoCasinoLocalViewComponent::GetLastResult() const
{ DEMO_LOG_TICK(); return LastResult; }
#if WITH_EDITOR
bool UDemoCasinoLocalViewComponent::ReadModelPartPoseForAudit(FName PartId,FTransform& WorldTransform) const
{
    DEMO_LOG_TICK();
    if (!ModelProfile || !Device.IsValid()) return false;
    for (int32 Index=0;Index<ModelProfile->Parts.Num();++Index)
        if (ModelProfile->Parts[Index].PartId==PartId && ModelParts.IsValidIndex(Index) && IsValid(ModelParts[Index]))
        { WorldTransform=ModelParts[Index]->GetComponentTransform(); return true; } // 只拷组件最终变换，不用期望stop重新求值掩盖错误。
    return false;
}
bool UDemoCasinoLocalViewComponent::ReadFocusedOutlineForAudit(FName& OutControlId,FString& OutMaterialPath,FTransform& OutWorldTransform) const
{
    DEMO_LOG_TICK(); OutControlId=NAME_None; OutMaterialPath.Reset(); OutWorldTransform=FTransform::Identity;
    const ADemoPlayerController* Controller=Cast<ADemoPlayerController>(GetOwner()); // 审查读当前真实Owner/ViewTarget，不借用Player0或只读期望配置。
    const ADemoCharacter* Viewer=Controller?Cast<ADemoCharacter>(Controller->GetPawn()):nullptr;
    const UCameraComponent* Camera=FocusOutlineCamera.Get(); // 只认当次实际挂载Camera与其有效权重，不把已创建MID当作正在生效。
    const UDemoLocalOutlineComponent* Renderer=Controller?Controller->FindComponentByClass<UDemoLocalOutlineComponent>():nullptr;
    const UMaterialInstanceDynamic* Composite=Renderer?Renderer->GetCompositeForOwner(this):nullptr; // 读取Renderer实际当前MID；resize换实例不把旧缓存当活跃项。
    if (!Controller || !Controller->IsLocalController() || !Viewer || Viewer!=PrimitiveAvatar.Get() || Controller->GetViewTarget()!=Viewer
        || !Camera || Camera!=Viewer->GetFirstPersonCameraComponent() || Camera->PostProcessBlendWeight<=0.f
        || !Device.IsValid() || !ModelProfile || !bFocusedCanActivate || FocusedControl.IsNone() || OutlinedControl!=FocusedControl
        || !IsValid(FocusOutline) || !FocusOutline->IsRegistered() || !FocusOutlineSource.IsValid() || !FocusOutlineSource->IsRegistered()
        || FocusOutline->GetOwner()!=Viewer || FocusOutline->GetAttachParent()!=FocusOutlineSource.Get()
        || !FocusOutline->bRenderCustomDepth || FocusOutline->CustomDepthStencilValue!=253 || !FocusOutline->bOnlyOwnerSee
        || FocusOutline->bRenderInMainPass || FocusOutline->bRenderInDepthPass || FocusOutline->bHiddenInGame
        || !IsValid(FocusOutlineMaterial) || !Renderer || !Renderer->OwnsOutlineTarget(this,FocusOutline) || !IsValid(Composite) || Composite->Parent!=FocusOutlineMaterial) return false;
    for (const FWeightedBlendable& Entry:Camera->PostProcessSettings.WeightedBlendables.Array)
        if (Entry.Object==Composite && Entry.Weight>0.f)
        {
            OutControlId=OutlinedControl; OutMaterialPath=FocusOutlineMaterial->GetPathName(); OutWorldTransform=FocusOutline->GetComponentTransform();
            return true; // 返回实际遮罩输出；夹具可与真实源组件做同帧位置/旋转/缩放对比。
        }
    return false;
}
#endif
void UDemoCasinoLocalViewComponent::AcceptCommittedResult(const FDemoCasinoResult& Result)
{
    DEMO_LOG_CALL();
    if (!Device.IsValid() || Result.Mode!=1 || Result.Symbols.Num()!=3 || Result.Serial<0 || Result.Serial<=LastResult.Serial) return;
    for (int32 Symbol:Result.Symbols) if (Symbol<0 || Symbol>4) { UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_PHYSICAL_REVEAL_REJECT symbol range")); return; }
    ReelStartAngles=ReelAngles; LastResult=Result; RevealStarted=FPlatformTime::Seconds(); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_PHYSICAL_REVEAL serial=%d symbols=3"),Result.Serial); Refresh(); // 没有动画Notify或第二次经济调用。
}
void UDemoCasinoLocalViewComponent::PressControl(FName ControlId)
{
    DEMO_LOG_CALL();
    if (!ModelProfile || !Device.IsValid() || ControlId.IsNone()) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_KEY_FEEDBACK_SKIP reason=NoCurrentModelDeviceOrControl")); return; }
    bool bKnownKey=false; // 只有已验证Profile的真实Key可产生本机平移；Pull/未知ID不提前拉杆。
    for (const FDemoCasinoModelPart& Part:ModelProfile->Parts)
        if (Part.Kind==EDemoCasinoModelPartKind::Key && Part.ControlId==ControlId) { bKnownKey=true; break; }
    if (!bKnownKey) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_KEY_FEEDBACK_SKIP control=%s reason=NotKey"),*ControlId.ToString()); return; }
    const double Now=FPlatformTime::Seconds(); // 纯视觉单调秒，普通数字编辑仍由输入组件同步完成，不等曲线或RPC。
    const FDemoCasinoKeyPressState* Previous=KeyPresses.Find(ControlId);
    FDemoCasinoKeyPressState Press; Press.StartFraction=Previous?EvaluateKeyPress(*Previous,Now):0.f; Press.StartedAt=Now;
    const bool bTimingValid=FMath::IsFinite(KeyDownSeconds) && KeyDownSeconds>=.01f && KeyDownSeconds<=.25f
        && FMath::IsFinite(KeyHoldSeconds) && KeyHoldSeconds>=0.f && KeyHoldSeconds<=.2f
        && FMath::IsFinite(KeyReturnSeconds) && KeyReturnSeconds>=.01f && KeyReturnSeconds<=.5f;
    if (!bTimingValid && !bWarnedKeyTiming) { UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_KEY_TIMING_INVALID using bounded visual defaults/clamps")); bWarnedKeyTiming=true; }
    Press.DownSeconds=FMath::IsFinite(KeyDownSeconds)?FMath::Clamp(KeyDownSeconds,.01f,.25f):.04f;
    Press.HoldSeconds=FMath::IsFinite(KeyHoldSeconds)?FMath::Clamp(KeyHoldSeconds,0.f,.2f):.01f;
    Press.ReturnSeconds=FMath::IsFinite(KeyReturnSeconds)?FMath::Clamp(KeyReturnSeconds,.01f,.5f):.1f;
    KeyPresses.Add(ControlId,Press); // 已知14个Key有界；同键替换起点保持位置连续，不影响其他键的回弹。
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_KEY_PRESS control=%s start_fraction=%.3f active_keys=%d"),*ControlId.ToString(),Press.StartFraction,KeyPresses.Num());
    RefreshModelParts(); // 更新同帧私有机械输出；每次仍从Profile.Rest求值，不累加组件位置。
}
float UDemoCasinoLocalViewComponent::EvaluateKeyPress(const FDemoCasinoKeyPressState& Press,double Now) const
{
    DEMO_LOG_TICK();
    const float Elapsed=FMath::Max(0.f,float(Now-Press.StartedAt)); // 单调时钟回退防护，不改变任何输入/交易时钟。
    if (Elapsed<Press.DownSeconds)
    {
        const float Alpha=FMath::Clamp(Elapsed/Press.DownSeconds,0.f,1.f); const float Smooth=Alpha*Alpha*(3.f-2.f*Alpha);
        return FMath::Lerp(Press.StartFraction,1.f,Smooth); // 重按时从当前分数继续，两个端点速度为0。
    }
    if (Elapsed<Press.DownSeconds+Press.HoldSeconds) return 1.f;
    const float Alpha=FMath::Clamp((Elapsed-Press.DownSeconds-Press.HoldSeconds)/Press.ReturnSeconds,0.f,1.f);
    return 1.f-Alpha*Alpha*(3.f-2.f*Alpha); // 分段结束精确0，后续直接使用未修改Rest。
}
void UDemoCasinoLocalViewComponent::SetFocusedControl(FName ControlId,bool bCanActivate)
{
    DEMO_LOG_TICK();
    const FDemoCasinoControlBinding* Binding=nullptr; // 只接受当前Profile白名单；失效/切机ID不保留亮框。
    if (ModelProfile && Device.IsValid()) for (const FDemoCasinoControlBinding& Candidate:ModelProfile->Controls)
        if (Candidate.ControlId==ControlId) { Binding=&Candidate; break; }
    const FName Desired=Binding?ControlId:NAME_None; const bool bEnabled=Binding && bCanActivate;
    if (Desired!=FocusedControl || bEnabled!=bFocusedCanActivate)
    {
        FocusedControl=Desired; bFocusedCanActivate=bEnabled;
        UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_FOCUS control=%s enabled=%d"),*FocusedControl.ToString(),bFocusedCanActivate);
    }
    RefreshFocusOutline();
}
void UDemoCasinoLocalViewComponent::ClearFocusOutline()
{
    DEMO_LOG_TICK();
    if (FocusOutline || FocusOutlineBlendable || !OutlinedControl.IsNone())
        UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_OUTLINE_CLEAR control=%s"),*OutlinedControl.ToString());
    ADemoPlayerController* Controller=Cast<ADemoPlayerController>(GetOwner());
    if (UDemoLocalOutlineComponent* Renderer=Controller?Controller->FindComponentByClass<UDemoLocalOutlineComponent>():nullptr) Renderer->ClearOutlineTarget(this); // 先撤本Requester的捕获/合成，再销mask；不能清枪Focus的新目标。
    if (IsValid(FocusOutline)) FocusOutline->DestroyComponent();
    FocusOutline=nullptr; FocusOutlineBlendable=nullptr; FocusOutlineCamera.Reset(); FocusOutlineSource.Reset(); OutlinedControl=NAME_None;
}
void UDemoCasinoLocalViewComponent::RefreshFocusOutline()
{
    DEMO_LOG_TICK();
    ADemoPlayerController* Controller=Cast<ADemoPlayerController>(GetOwner());
    ADemoCharacter* Viewer=Controller?Cast<ADemoCharacter>(Controller->GetPawn()):nullptr; // Primitive与相机都必须属于当前本机Avatar。
    UCameraComponent* Camera=Viewer?Viewer->GetFirstPersonCameraComponent():nullptr;
    UDemoLocalOutlineComponent* Renderer=Controller?Controller->FindComponentByClass<UDemoLocalOutlineComponent>():nullptr;
    UStaticMeshComponent* Source=nullptr; // 只选当前Profile映射到本机真实活动件的Mesh；绝不描Query或整机。
    if (Controller && Controller->IsLocalController() && Viewer && Viewer==PrimitiveAvatar.Get() && Controller->GetViewTarget()==Viewer
        && IsValid(Camera) && ModelProfile && Device.IsValid() && bFocusedCanActivate && !FocusedControl.IsNone())
        for (int32 Index=0;Index<ModelProfile->Parts.Num();++Index)
        {
            const FDemoCasinoModelPart& Part=ModelProfile->Parts[Index]; // Profile已在Open前校验唯一ControlId与真实刚性部件。
            if (Part.ControlId==FocusedControl && (Part.Kind==EDemoCasinoModelPartKind::Key || Part.Kind==EDemoCasinoModelPartKind::Lever)
                && ModelParts.IsValidIndex(Index) && IsValid(ModelParts[Index]) && ModelParts[Index]->IsRegistered() && ModelParts[Index]->GetStaticMesh())
            { Source=ModelParts[Index]; break; }
        }
    if (!Source) { ClearFocusOutline(); return; }
    if (IsValid(FocusOutline) && FocusOutline->IsRegistered() && FocusOutlineSource.Get()==Source && FocusOutline->GetOwner()==Viewer
        && FocusOutline->GetAttachParent()==Source && OutlinedControl==FocusedControl && FocusOutlineCamera.Get()==Camera && Renderer && Renderer->OwnsOutlineTarget(this,FocusOutline))
    { FocusOutlineBlendable=Renderer->GetCompositeForOwner(this); return; } // 同请求即保持mask，RT/resize暂关闭不触发重造；附着继续跟实际键运动。
    ClearFocusOutline();
    if (!bFocusOutlineLoadAttempted)
    {
        bFocusOutlineLoadAttempted=true;
        const UDemoHubShopWorldSubsystem* World=GetWorld()?GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>():nullptr;
        const UDemoHubShopDefinition* Data=World?World->GetDefinition():nullptr; // 只读可信World目录，不接收客户端路径或硬写旧CDO资源。
        FocusOutlineMaterial=Data?Data->HighlightMaterial.LoadSynchronous():nullptr; FocusOutlineMaskMaterial=Data?Data->HighlightMaskMaterial.LoadSynchronous():nullptr;
        if (!FocusOutlineMaterial || !FocusOutlineMaterial->GetMaterial() || FocusOutlineMaterial->GetMaterial()->MaterialDomain!=MD_PostProcess
            || !FocusOutlineMaskMaterial || !FocusOutlineMaskMaterial->GetMaterial() || FocusOutlineMaskMaterial->GetMaterial()->MaterialDomain!=MD_PostProcess)
        {
            UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_CONTROL_OUTLINE_UNAVAILABLE reason=MissingOrNonPostProcessMaterial composite=%s capture_mask=%s"),Data?*Data->HighlightMaterial.ToSoftObjectPath().ToString():TEXT("NoHubDefinition"),Data?*Data->HighlightMaskMaterial.ToSoftObjectPath().ToString():TEXT("NoHubDefinition"));
            FocusOutlineMaterial=nullptr; FocusOutlineMaskMaterial=nullptr; // 本Open缺任一层保留原点/提示/按压，不回退旧A04或ASCII框。
        }
    }
    if (!FocusOutlineMaterial || !FocusOutlineMaskMaterial || !Renderer) return;
    FocusOutline=UDemoShopFocusComponent::CreateLocalGunSilhouetteMask(Viewer,Source);
    if (!FocusOutline) { FocusOutlineBlendable=nullptr; UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_CONTROL_OUTLINE_UNAVAILABLE reason=CreateMask")); return; }
    FocusOutline->SetCanEverAffectNavigation(false); FocusOutline->SetVisibleInRayTracing(false);
    FocusOutline->AttachToComponent(Source,FAttachmentTransformRules::KeepWorldTransform);
    FocusOutline->SetRelativeTransform(FTransform::Identity); // 同源网格/局部框/单位缩放，机械键运动时只跟父变换，不独立计算预测位置。
    const bool Accepted=Renderer->SetOutlineTarget(this,Viewer,Camera,Source,FocusOutline,FocusOutlineMaterial,FocusOutlineMaskMaterial);
    if (!Accepted&&!Renderer->OwnsOutlineTarget(this,FocusOutline)) { Renderer->ClearOutlineTarget(this); FocusOutline->DestroyComponent(); FocusOutline=nullptr; UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_CONTROL_OUTLINE_UNAVAILABLE reason=RendererTargetRejected")); return; }
    FocusOutlineBlendable=Renderer->GetCompositeForOwner(this); // 仅缓存本请求实际Composite，PP写入/移除完全归Renderer。
    FocusOutlineCamera=Camera; FocusOutlineSource=Source; OutlinedControl=FocusedControl;
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_OUTLINE_SET control=%s source=%s material=%s stencil=253"),*OutlinedControl.ToString(),*Source->GetName(),*FocusOutlineMaterial->GetPathName());
}
void UDemoCasinoLocalViewComponent::Refresh()
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* Controller=Cast<ADemoPlayerController>(GetOwner());
    if (!Device.IsValid() || !Controller || !Controller->IsLocalController() || !Screen || (!ModelProfile&&!Lever))
    {
        if (Screen || FocusOutline || FocusOutlineBlendable) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_LOCAL_VIEW_CLOSED reason=InvalidDeviceControllerOrModel")); Close(); }
        return; // 外层Input通常先撤操作；自身也不能因源Actor销毁/Controller失效而遗留私有PP。
    }
    if (PrimitiveAvatar.Get()!=Controller->GetPawn() || Controller->GetViewTarget()!=PrimitiveAvatar.Get()) { Close(); return; }
    const UDemoMenuFlowComponent* Flow=Controller->GetMenuFlow();
    const FString Amount=Flow->GetCasinoDeviceAmountText().IsEmpty()?TEXT("0"):Flow->GetCasinoDeviceAmountText(); // 编辑为空时显示0；真正最低额仍由SET BET验证。
    FString Text=FString::Printf(TEXT("BET %d\nIN %s"),Flow->GetCasinoDraft().Stake,*Amount); // 小屏仅金额/状态，钱包与中文原因由原本人HUD显示，不放Serial等实现细节。
    const bool bRolling=LastResult.Mode==1 && FPlatformTime::Seconds()-RevealStarted<(ModelProfile?ModelProfile->RevealSeconds:.8f);
    if (Flow->IsGambleWaiting()) Text+=TEXT("\nSAVING...");
    else if (bRolling) Text+=TEXT("\nSPINNING...");
    else if (LastResult.Mode==1)
    {
        Text+=FString::Printf(TEXT("\nRETURN %d"),LastResult.Paid); // 原到账额含本金；结果由三个真实滚轮表示，不额外塞进小屏。
    }
    else Text+=TEXT("\nSET BET / PULL");
    if (Text!=LastText) { Screen->SetText(FText::FromString(Text)); LastText=Text; }
    if (ModelProfile)
    { Screen->SetWorldTransform(ModelProfile->ScreenTransform*Device->GetActorTransform()); RefreshModelParts(); RefreshFocusOutline(); return; } // 正式挂点保持同版测量，轮廓/按键都仅更新本人层。
    const FTransform Frame=Device->GetActorTransform(); Screen->SetWorldLocationAndRotation(Frame.TransformPosition(FVector(82,0,180)),Frame.GetRotation()); // 面板低于旧182cm顶牌附近，约170cm前站位对准140cm拉杆时仍可见；实际RHI可读性另验。
    const double Phase=LastResult.Mode==1?FMath::Clamp((FPlatformTime::Seconds()-RevealStarted)/.35,0.,1.):1.;
    const float Angle=float(FMath::Sin(Phase*PI)*55.); // 只发生在成功保存后的本机NoCollision杆，Query盒与锚点始终固定。
    Lever->SetWorldLocationAndRotation(Frame.TransformPosition(FVector(86,73,140)),(FQuat(Frame.TransformVectorNoScale(FVector::RightVector),FMath::DegreesToRadians(Angle))*Frame.GetRotation()));
}
void UDemoCasinoLocalViewComponent::RefreshModelParts()
{
    DEMO_LOG_TICK(); if (!ModelProfile || !Device.IsValid()) return;
    const double Now=FPlatformTime::Seconds(),Elapsed=Now-RevealStarted; // 单调视觉时钟，无经济/保存输入。
    for (auto It=KeyPresses.CreateIterator();It;++It)
        if (Now-It.Value().StartedAt>=It.Value().DownSeconds+It.Value().HoldSeconds+It.Value().ReturnSeconds)
        { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_KEY_RELEASE control=%s"),*It.Key().ToString()); It.RemoveCurrent(); } // 有界边沿日志与清理，下一段必用Rest；不会重触发数字。
    for (int32 Index=0;Index<ModelParts.Num();++Index)
    {
        UStaticMeshComponent* Mesh=ModelParts[Index]; if (!Mesh) continue;
        const FDemoCasinoModelPart& Part=ModelProfile->Parts[Index]; FTransform Local=Part.RestTransform; float Angle=0.f; // 每次从真实rest求值，不能累积漂移。
        if (Part.Kind==EDemoCasinoModelPartKind::Key)
        {
            const FDemoCasinoKeyPressState* Press=KeyPresses.Find(Part.ControlId);
            const float Fraction=Press?EvaluateKeyPress(*Press,Now):0.f; // 同键当前脉冲或精确Rest，不读取别键的起点。
            Local.AddToTranslation(Part.RestTransform.TransformVectorNoScale(Part.MotionAxis*(Part.MotionAmount*Fraction))); // 复用A06实测0.25cm/局部-X，不额外乘网格缩放。
        }
        else if (Part.Kind==EDemoCasinoModelPartKind::Lever && LastResult.Mode==1)
            Angle=Part.MotionAmount*FMath::Sin(FMath::Clamp(float(Elapsed/ModelProfile->MechanicalSeconds),0.f,1.f)*PI);
        else if (Part.Kind==EDemoCasinoModelPartKind::Reel)
        {
            const int32 Reel=Part.ReelIndex; Angle=Part.SymbolStopAngles[0]; // 未揭晓中性普通弹，非随机未来结果。
            if (LastResult.Mode==1 && LastResult.Symbols.Num()==3)
            {
                const float Start=ReelStartAngles[Reel],Target=Part.SymbolStopAngles[LastResult.Symbols[Reel]];
                float Delta=FMath::Fmod(Target-Start,360.f); if (Delta<0.f) Delta+=360.f;
                const float Fraction=FMath::Clamp(float(Elapsed/(ModelProfile->RevealSeconds*(.7f+.15f*Reel))),0.f,1.f);
                const float Ease=1.f-FMath::Pow(1.f-Fraction,3.f); // 纯机械减速，不根据动画决定任何奖面。
                Angle=Fraction>=1.f?Target:Start+(720.f+Delta)*Ease;
            }
            ReelAngles[Reel]=Angle;
        }
        if (Part.Kind==EDemoCasinoModelPartKind::Reel || Part.Kind==EDemoCasinoModelPartKind::Lever)
            Local.SetRotation(Part.RestTransform.GetRotation()*FQuat(Part.MotionAxis,FMath::DegreesToRadians(Angle)));
        Mesh->SetWorldTransform(Local*Device->GetActorTransform()); // 只动Pawn拥有的NoCollision件，Query/Focus仍根静态。
    }
}
void UDemoCasinoLocalViewComponent::EndPlay(const EEndPlayReason::Type Reason)
{ DEMO_LOG_CALL(); Close(); Super::EndPlay(Reason); }
