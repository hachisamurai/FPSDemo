// 职责：校验持久环境唯一根、稳定成员和真实资源，原子选择TA外观或抑制TA后回退白模。
// 不写地图、不改原37Actor/Gameplay/Profile/材质；专用Tag由TA在显式作者窗口设置。
#include "Interaction/HubShops/DemoHubShopEnvironment.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"

namespace
{
    const FName RootTag(TEXT("HubShopEnv.Root")); // 固定专用命名域，DA不可改成其他Gameplay/原Actor标签。
    const FName MemberTag(TEXT("HubShopEnv.Member")); // 只有TA显式加入此域的普通环境才允许抑制。

    /** 限定普通环境类型；不能让误标的Pawn/Gameplay Actor被回退逻辑隐藏或关碰撞。 */
    bool IsEnvironmentActor(const AActor* Actor)
    {
        DEMO_LOG_STATIC_CALL(Actor);
        if(!IsValid(Actor)) return false;
        // 原Hub/白模区域与审查平台即使误贴Member也受保护；不按临时Label或数组索引猜旧Actor。
        for(FName Tag:Actor->Tags)
        {
            const FString Name=Tag.ToString(); // 同步检查既有作者命名域，不保留全局Actor缓存。
            if(Name.StartsWith(TEXT("DemoArea"))||Name.StartsWith(TEXT("Whitebox"))||Tag==TEXT("DemoAuthoredBlockout")||Tag==TEXT("HubShopsReviewFloor")) return false;
        }
        return Actor->GetClass()==AStaticMeshActor::StaticClass()||Actor->GetClass()==APointLight::StaticClass()||Actor->GetClass()==ARectLight::StaticClass(); // 不接受带额外玩法的Blueprint/原生派生类。
    }

    /** 仅修改专用域普通环境；Transient组件跨同World重建保留原状态，World销毁自然清理。 */
    void SetMemberSuppressed(AActor* Actor,bool bSuppressed)
    {
        DEMO_LOG_STATIC_CALL(Actor);
        if(!IsEnvironmentActor(Actor)||(!Actor->ActorHasTag(MemberTag)&&!Actor->ActorHasTag(RootTag)&&!Cast<ADemoHubShopEnvironmentRoot>(Actor->GetAttachParentActor())))
        { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ENV_SUPPRESS_REJECT untrusted_actor %s"),*GetNameSafe(Actor)); return; }
        UDemoHubShopEnvironmentVisibilityState* State=Actor->FindComponentByClass<UDemoHubShopEnvironmentVisibilityState>(); // 仅当前成员拥有，无全局裸引用。
        if(!State&&bSuppressed)
        {
            State=NewObject<UDemoHubShopEnvironmentVisibilityState>(Actor,NAME_None,RF_Transient);
            Actor->AddInstanceComponent(State); State->RegisterComponent();
        }
        if(State) State->SetSuppressed(bSuppressed);
    }
}

void UDemoHubShopEnvironmentVisibilityState::SetSuppressed(bool bInSuppressed)
{
    DEMO_LOG_CALL(); AActor* Actor=GetOwner(); // 仅同步当前World使用，Owner销毁后不再访问。
    if(!IsValid(Actor)||bSuppressed==bInSuppressed) return;
    if(bInSuppressed&&!bCaptured)
    {
        bWasHidden=Actor->IsHidden(); bHadCollision=Actor->GetActorEnableCollision();
        TInlineComponentArray<USceneComponent*> Components(Actor); // 包含Primitive/Light，不改变Profile/强度/阴影参数。
        for(USceneComponent* Component:Components) ComponentVisibility.Emplace(Component,Component->IsVisible());
        bCaptured=true;
    }
    if(!bCaptured) return;
    Actor->SetActorHiddenInGame(bInSuppressed?true:bWasHidden);
    Actor->SetActorEnableCollision(bInSuppressed?false:bHadCollision);
    for(const TPair<TWeakObjectPtr<USceneComponent>,bool>& Saved:ComponentVisibility)
        if(USceneComponent* Component=Saved.Key.Get()) Component->SetVisibility(bInSuppressed?false:Saved.Value,false);
    bSuppressed=bInSuppressed;
    if(!bSuppressed) { ComponentVisibility.Reset(); bCaptured=false; } // 下一轮从合法状态重新捕获，不能回滚到更早周期的旧作者值。
    UE_LOG(LogFPSDemo,Log,TEXT("HUB_ENV_MEMBER_STATE actor=%s suppressed=%d"),*Actor->GetName(),bSuppressed);
}

ADemoHubShopEnvironmentRoot::ADemoHubShopEnvironmentRoot()
{
    DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=false; bReplicates=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("EnvironmentRoot")));
    GetRootComponent()->SetMobility(EComponentMobility::Static); // 持久不移动父框，允许Static Mesh/Stationary Light按KeepWorld直接挂接。
    Tags.Add(RootTag); // 普通独立标记，不触发WorldSubsystem预放Blockout拒绝分支。
}

bool ADemoHubShopEnvironmentRoot::ValidateEnvironment(const UDemoHubShopDefinition* Definition,const FTransform& HubFrame,FString& Error) const
{
    DEMO_LOG_CALL(); Error.Reset();
    if(!Definition||!Definition->ValidateAuthoredEnvironment(Error))
    { if(Error.IsEmpty()) Error=TEXT("missing_definition"); return false; }
    if(!Definition->bUseAuthoredEnvironment)
    { Error=TEXT("environment_disabled"); UE_LOG(LogFPSDemo,Log,TEXT("HUB_ENV_VALIDATE_REJECT environment_disabled")); return false; }
    if(!GetWorld()||GetLevel()!=GetWorld()->PersistentLevel||!ActorHasTag(RootTag)||ActorHasTag(MemberTag)||GetIsReplicated()||IsEditorOnly()||!bNetLoadOnClient)
        Error=TEXT("root_level_tag_replication_or_load_policy");
    else if(EnvironmentId!=Definition->AuthoredEnvironmentId||Revision!=Definition->AuthoredEnvironmentRevision)
        Error=TEXT("root_identity_or_revision");
    else if(HubFrame.ContainsNaN()||!GetActorTransform().Equals(HubFrame,.01f))
        Error=TEXT("root_not_actual_hub_frame");
    else if(Entries.Num()!=Definition->AuthoredEnvironmentItemCount)
        Error=TEXT("entry_count");
    TSet<FName> Ids; TSet<const AActor*> Actors; // 本次解析完整闭包；序号/ActorLabel不是身份。
    if(Error.IsEmpty()) for(const FDemoHubShopEnvironmentEntry& Entry:Entries)
    {
        AActor* Actor=Entry.Actor; // 地图强引用，只在当前线程同步验证。
        if(Entry.EntryId.IsNone()||Ids.Contains(Entry.EntryId)||!IsEnvironmentActor(Actor)||Actors.Contains(Actor))
        { Error=TEXT("entry_id_actor_type_or_duplicate"); break; }
        if(Actor->GetWorld()!=GetWorld()||Actor->GetLevel()!=GetLevel()||Actor->GetAttachParentActor()!=this||!Actor->ActorHasTag(MemberTag)||Actor->ActorHasTag(RootTag)||Actor->GetIsReplicated()||Actor->IsEditorOnly()||!Actor->bNetLoadOnClient||Actor->GetActorTransform().ContainsNaN()||Actor->GetActorScale3D().GetAbs().GetMin()<=SMALL_NUMBER)
        { Error=TEXT("entry_world_level_tag_replication_load_policy_or_transform"); break; }
        Ids.Add(Entry.EntryId); Actors.Add(Actor);
        if(const AStaticMeshActor* MeshActor=Cast<AStaticMeshActor>(Actor))
        {
            const UStaticMeshComponent* Mesh=MeshActor->GetStaticMeshComponent(); // 材质槽读取实际有效材质含资产默认，不只看Override数组。
            if(!Mesh||!Mesh->IsRegistered()||!Mesh->GetStaticMesh()||Mesh->GetNumMaterials()<1)
            { Error=TEXT("entry_mesh_missing_or_unregistered"); break; }
            for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot) if(!Mesh->GetMaterial(Slot)) { Error=TEXT("entry_material_missing"); break; }
        }
        else
        {
            const UPointLightComponent* Point=Actor->FindComponentByClass<UPointLightComponent>(); // Point/Rect只校验真实组件/有限参数，不重写TA光照。
            const URectLightComponent* Rect=Actor->FindComponentByClass<URectLightComponent>();
            const ULocalLightComponent* Light=Rect?static_cast<const ULocalLightComponent*>(Rect):Point;
            if(!Light||!Light->IsRegistered()||!FMath::IsFinite(Light->Intensity)||Light->Intensity<0||!FMath::IsFinite(Light->AttenuationRadius)||Light->AttenuationRadius<=0
                ||(Rect&&(!FMath::IsFinite(Rect->SourceWidth)||Rect->SourceWidth<=0||!FMath::IsFinite(Rect->SourceHeight)||Rect->SourceHeight<=0)))
                Error=TEXT("entry_light_missing_or_invalid");
        }
        if(!Error.IsEmpty()) break;
    }
    if(Error.IsEmpty()) for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        if((It->ActorHasTag(MemberTag)||It->GetAttachParentActor()==this)&&!Actors.Contains(*It)) { Error=TEXT("unlisted_member"); break; }
    }
    if(!Error.IsEmpty()) UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ENV_VALIDATE_REJECT root=%s reason=%s"),*GetName(),*Error);
    return Error.IsEmpty();
}

bool ADemoHubShopEnvironmentRoot::ResolveForWorld(UWorld* World,const UDemoHubShopDefinition* Definition,const FTransform& HubFrame,FString& Error)
{
    DEMO_LOG_STATIC_CALL(World); Error.Reset();
    if(!World) { Error=TEXT("missing_world"); UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ENV_SELECT_REJECT missing_world")); return false; }
    TArray<ADemoHubShopEnvironmentRoot*> Roots; TArray<AActor*> Members; // 一次扫描同时收齐重复/残缺集合，未完成校验前不启用任何成员。
    for(TActorIterator<AActor> It(World);It;++It)
    {
        if(It->ActorHasTag(RootTag)) Roots.Add(Cast<ADemoHubShopEnvironmentRoot>(*It)); // 错误类型也占一个根名额，明确拒绝而非忽略。
        if(It->ActorHasTag(MemberTag)||(It->ActorHasTag(RootTag)&&IsEnvironmentActor(*It))||Cast<ADemoHubShopEnvironmentRoot>(It->GetAttachParentActor())) Members.Add(*It); // Tag与直接父根提供独立归属：丢Tag/漏Entries仍能抑制；原保护Actor始终排除。
    }
    bool bSelected=false; // 失败默认回退，不因DA全局开关在未作者地图删除房间。
    if(!Definition||!Definition->bUseAuthoredEnvironment) Error=TEXT("disabled_or_missing_definition");
    else if(!Definition->ValidateAuthoredEnvironment(Error)) { /* 配置失败保留原因，基础商店仍可用。 */ }
    else if(Roots.Num()!=1||!Roots[0]) Error=TEXT("missing_duplicate_or_wrong_root_class");
    else bSelected=Roots[0]->ValidateEnvironment(Definition,HubFrame,Error);
    for(AActor* Member:Members) SetMemberSuppressed(Member,!bSelected); // 先抑制全部专用TA成员，再让调用者构建legacy，避免两套碰撞同时启用。
    if(bSelected)
    { UE_LOG(LogFPSDemo,Log,TEXT("HUB_ENV_SELECTED id=%s revision=%d members=%d"),*Definition->AuthoredEnvironmentId.ToString(),Definition->AuthoredEnvironmentRevision,Members.Num()); }
    else if(!Definition||!Definition->bUseAuthoredEnvironment)
    { UE_LOG(LogFPSDemo,Log,TEXT("HUB_ENV_LEGACY_FALLBACK reason=%s roots=%d members=%d"),*Error,Roots.Num(),Members.Num()); }
    else
    { UE_LOG(LogFPSDemo,Warning,TEXT("HUB_ENV_LEGACY_FALLBACK reason=%s roots=%d members=%d"),*Error,Roots.Num(),Members.Num()); }
    return bSelected;
}
