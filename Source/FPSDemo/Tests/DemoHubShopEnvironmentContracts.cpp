// 职责：用独立瞬态Editor World验证持久Hub环境闭包选择、可逆抑制与Blockout锚点合同。
// 使用：Automation RunTests FPSDemo.HubShops.Environment；不BeginPlay/PIE/联网/保存，不运行游戏模式或RHI。
// 依赖本项目生产ResolveForWorld/Configure与UE5.4.4 CreateWorld/InitializationValues；不复制解析算法，不修改共享DA/Engine网格。
#include "Misc/AutomationTest.h"
#include "Interaction/HubShops/DemoHubShopEnvironment.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "World/DemoRuntimeArenaGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Debug/DemoStaticLog.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace
{
    const FName ContractRootTag(TEXT("HubShopEnv.Root")); // 外部作者合同固定域，不从生产私有变量取值自证。
    const FName ContractMemberTag(TEXT("HubShopEnv.Member")); // 只给本测试明确创建的环境成员添加。

    /** 本同步自动化栈独占的无场景Editor World；析构清理全部Actor，不接触当前Editor地图/GWorld。 */
    struct FHubEnvironmentFixture
    {
        UWorld* World=nullptr; // CreateWorld以Root保活；析构DestroyWorld释放，不创建GI或BeginPlay。
        TStrongObjectPtr<UDemoHubShopDefinition> Definition; // 本实例独立DA，不用/改项目资产或CDO。
        UStaticMesh* Cube=nullptr; // 借用只读Engine Cube，实际成员组件强持有；禁止改共享材质槽。
        ADemoHubShopEnvironmentRoot* Root=nullptr; // 本World普通持久层根夹具，World拥有。
        AStaticMeshActor* MeshA=nullptr; // 第一普通成员，默认可见/有碰撞，用于实际抑制验证。
        AStaticMeshActor* MeshB=nullptr; // 第二普通成员，用于重复ID/引用与混合原状态。
        APointLight* Point=nullptr; // 点光普通成员，只核组件合同/状态，不创建渲染场景。
        ARectLight* Rect=nullptr; // 矩形灯普通成员，与点光共同覆盖灯资源分支。
        bool bMembersAttached=false; // 仅四个合法成员KeepWorld直接挂根成功时夹具就绪，不自动挂额外/保护Actor。
        FTransform HubFrame=FTransform(FRotator(0,17,0),FVector(120,-340,50)); // 非零世界框，防只在Identity下误过。

        /** 创建无Render/Physics/AI/Nav/Audio/FX的Editor World及四个实际成员，不调用任何项目启动流程。 */
        FHubEnvironmentFixture()
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            const UWorld::InitializationValues Init=UWorld::InitializationValues().InitializeScenes(false)
                .AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
                .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
                .EnableTraceCollision(false).CreateFXSystem(false).SetTransactional(false); // 仅对象/注册合同，不做物理或可视验收。
            const FName WorldName=MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("HubEnvironmentContracts")); // 独占瞬态对象名，不覆盖编辑器现World。
            World=UWorld::CreateWorld(EWorldType::Editor,false,WorldName,GetTransientPackage(),true,ERHIFeatureLevel::Num,&Init);
            if(!World) return;
            World->SetFlags(RF_Transient);
            Definition.Reset(NewObject<UDemoHubShopDefinition>(World,NAME_None,RF_Transient));
            Definition->bUseAuthoredEnvironment=true;
            Definition->AuthoredEnvironmentId=TEXT("TA_Hub_Contract");
            Definition->AuthoredEnvironmentRevision=7;
            Definition->AuthoredEnvironmentItemCount=4;
            Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
            Root=Spawn<ADemoHubShopEnvironmentRoot>(); MeshA=SpawnMesh(); MeshB=SpawnMesh();
            Point=Spawn<APointLight>(); Rect=Spawn<ARectLight>();
            if(!Root||!MeshA||!MeshB||!Point||!Rect||!Cube) return;
            Root->EnvironmentId=Definition->AuthoredEnvironmentId; Root->Revision=Definition->AuthoredEnvironmentRevision;
            Point->Tags.Add(ContractMemberTag); Rect->Tags.Add(ContractMemberTag);
            bMembersAttached=MeshA->AttachToActor(Root,FAttachmentTransformRules::KeepWorldTransform)
                &&MeshB->AttachToActor(Root,FAttachmentTransformRules::KeepWorldTransform)
                &&Point->AttachToActor(Root,FAttachmentTransformRules::KeepWorldTransform)
                &&Rect->AttachToActor(Root,FAttachmentTransformRules::KeepWorldTransform); // 不改实际世界位姿；验证生产Static根兼容普通静态成员。
            Root->Entries={Entry(TEXT("mesh_a"),MeshA),Entry(TEXT("mesh_b"),MeshB),Entry(TEXT("point"),Point),Entry(TEXT("rect"),Rect)};
        }

        /** 任意提前返回均销毁本夹具World；不向引擎登记当前World或保留外部异步引用。 */
        ~FHubEnvironmentFixture()
        {
            DEMO_LOG_STATIC_CALL(World);
            Definition.Reset();
            if(World) World->DestroyWorld(false);
        }

        /** 在本World PersistentLevel创建普通RF_Transient Actor；T为项目/引擎已存在类，无BeginPlay。 */
        template<typename T> T* Spawn()
        {
            DEMO_LOG_STATIC_CALL(World);
            if(!World) return nullptr;
            FActorSpawnParameters Params; // 只用于本隔离PersistentLevel，不覆盖实际Map/包。
            Params.OverrideLevel=World->PersistentLevel; Params.ObjectFlags=RF_Transient;
            Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            return World->SpawnActor<T>(T::StaticClass(),HubFrame,Params);
        }

        /** 创建带实际只读Cube资源的普通StaticMeshActor；无Physics Scene仍保真实注册/碰撞配置值。 */
        AStaticMeshActor* SpawnMesh()
        {
            DEMO_LOG_STATIC_CALL(World);
            AStaticMeshActor* Actor=Spawn<AStaticMeshActor>(); // World拥有，不跨夹具保存。
            if(Actor)
            {
                Actor->Tags.Add(ContractMemberTag);
                Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
                Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
                Actor->RegisterAllComponents();
            }
            return Actor;
        }

        /** 冻结一条外部作者数据，Id/Actor只有值语义，不按数组位置或Label生成业务身份。 */
        static FDemoHubShopEnvironmentEntry Entry(FName Id,AActor* Actor)
        {
            DEMO_LOG_STATIC_CALL(Actor);
            FDemoHubShopEnvironmentEntry Result; Result.EntryId=Id; Result.Actor=Actor; return Result;
        }

        /** 判断夹具必要对象均已创建；资源缺失是真实失败，不能跳过合同记成功。 */
        bool IsReady() const
        {
            DEMO_LOG_STATIC_CALL(World);
            return World&&Definition.IsValid()&&Cube&&Root&&MeshA&&MeshB&&Point&&Rect&&bMembersAttached;
        }
    };

    /** Test为当前自动化、Fixture为独占World；执行生产选择并校验稳定拒绝码，不复写校验算法。 */
    void ExpectFallback(FAutomationTestBase& Test,FHubEnvironmentFixture& Fixture,const TCHAR* Label,const TCHAR* Reason)
    {
        DEMO_LOG_STATIC_CALL(Fixture.World);
        FString Error; // 每次由生产解析写入，不沿用历史结果。
        Test.TestFalse(Label,ADemoHubShopEnvironmentRoot::ResolveForWorld(Fixture.World,Fixture.Definition.Get(),Fixture.HubFrame,Error));
        Test.TestEqual(FString(Label)+TEXT(" reason"),Error,FString(Reason));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoHubEnvironmentClosureContracts,"FPSDemo.HubShops.Environment.ClosureAndBoundaries",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

/** Parameters未使用；在独立Editor World验证实际闭包拒绝/选择，不启动Gameplay或写资产。 */
bool FDemoHubEnvironmentClosureContracts::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    FHubEnvironmentFixture Fixture; // 本函数独占，同步析构清理，任何失败提前返回均不污染当前编辑器World。
    if(!TestTrue(TEXT("Isolated Editor fixture and Engine Cube exist"),Fixture.IsReady())) return false;
    UDemoHubShopDefinition* Data=Fixture.Definition.Get(); // 仅本测试强引用实例；不是默认对象/项目DA。
    FString Error; // 当前生产函数输出，每次重置由被测方法负责。
    TestFalse(TEXT("No BeginPlay"),Fixture.World->HasBegunPlay());
    TestFalse(TEXT("No Game World"),Fixture.World->IsGameWorld());
    TestNull(TEXT("No project GameInstance"),Fixture.World->GetGameInstance());
    TestNull(TEXT("No render Scene"),Fixture.World->Scene);
    TestNull(TEXT("No physics Scene"),Fixture.World->GetPhysicsScene());
    if(!TestTrue(TEXT("Ordinary shop definition valid"),Data->Validate(Error))) return false;
    TestTrue(TEXT("Four-entry closure selected, no fixed 350 requirement"),ADemoHubShopEnvironmentRoot::ResolveForWorld(Fixture.World,Data,Fixture.HubFrame,Error));
    TestTrue(TEXT("Successful selection clears reason"),Error.IsEmpty());

    Data->bUseAuthoredEnvironment=false;
    ExpectFallback(*this,Fixture,TEXT("Disabled environment falls back"),TEXT("disabled_or_missing_definition"));
    TestFalse(TEXT("Direct root validation also rejects disabled environment"),Fixture.Root->ValidateEnvironment(Data,Fixture.HubFrame,Error));
    TestTrue(TEXT("Disabled fallback hides dedicated mesh"),Fixture.MeshA->IsHidden());
    TestFalse(TEXT("Disabled fallback disables dedicated actor collision"),Fixture.MeshA->GetActorEnableCollision());
    Data->bUseAuthoredEnvironment=true;
    Fixture.Root->Tags.Remove(ContractRootTag);
    ExpectFallback(*this,Fixture,TEXT("No root"),TEXT("missing_duplicate_or_wrong_root_class"));
    Fixture.Root->Tags.Add(ContractRootTag);
    ADemoHubShopEnvironmentRoot* Duplicate=Fixture.Spawn<ADemoHubShopEnvironmentRoot>(); // 独立重复根，测试后撤专用Tag而非使用销毁时序猜迭代结果。
    if(!TestNotNull(TEXT("Duplicate root fixture"),Duplicate)) return false;
    ExpectFallback(*this,Fixture,TEXT("Duplicate roots"),TEXT("missing_duplicate_or_wrong_root_class"));
    Duplicate->Tags.Remove(ContractRootTag);
    Fixture.Root->Revision+=1;
    ExpectFallback(*this,Fixture,TEXT("Revision mismatch"),TEXT("root_identity_or_revision"));
    Fixture.Root->Revision=Data->AuthoredEnvironmentRevision;
    Data->AuthoredEnvironmentItemCount=5;
    ExpectFallback(*this,Fixture,TEXT("Item count mismatch"),TEXT("entry_count"));
    Data->AuthoredEnvironmentItemCount=4;
    Fixture.Root->Entries[1].EntryId=Fixture.Root->Entries[0].EntryId;
    ExpectFallback(*this,Fixture,TEXT("Duplicate entry ID"),TEXT("entry_id_actor_type_or_duplicate"));
    Fixture.Root->Entries[1].EntryId=TEXT("mesh_b");
    Fixture.Root->Entries[1].Actor=Fixture.MeshA;
    ExpectFallback(*this,Fixture,TEXT("Same actor referenced twice"),TEXT("entry_id_actor_type_or_duplicate"));
    Fixture.Root->Entries[1].Actor=Fixture.MeshB;
    Fixture.MeshA->GetStaticMeshComponent()->SetStaticMesh(nullptr);
    ExpectFallback(*this,Fixture,TEXT("Missing mesh"),TEXT("entry_mesh_missing_or_unregistered"));
    UStaticMesh* NoMaterialMesh=NewObject<UStaticMesh>(Fixture.World,NAME_None,RF_Transient); // 独立无渲染数据夹具，只测真实材质槽读取；不改Engine Cube。
    NoMaterialMesh->GetStaticMaterials().Add(FStaticMaterial());
    Fixture.MeshA->GetStaticMeshComponent()->SetStaticMesh(NoMaterialMesh);
    ExpectFallback(*this,Fixture,TEXT("Missing material in real slot"),TEXT("entry_material_missing"));
    Fixture.MeshA->GetStaticMeshComponent()->SetStaticMesh(Fixture.Cube);
    AStaticMeshActor* Extra=Fixture.SpawnMesh(); // 有专用Tag但不在Entries，完整集合必须拒绝。
    if(!TestNotNull(TEXT("Unlisted member fixture"),Extra)) return false;
    ExpectFallback(*this,Fixture,TEXT("Unlisted member"),TEXT("unlisted_member"));
    Extra->Tags.Remove(ContractMemberTag);
    Fixture.MeshA->Tags.Remove(ContractMemberTag); // 独立父根归属必须保证丢Tag仍能被回退抑制。
    ExpectFallback(*this,Fixture,TEXT("Member tag missing"),TEXT("entry_world_level_tag_replication_load_policy_or_transform"));
    TestTrue(TEXT("Attached member without tag is still hidden"),Fixture.MeshA->IsHidden());
    TestFalse(TEXT("Attached member without tag still has collision suppressed"),Fixture.MeshA->GetActorEnableCollision());
    const FDemoHubShopEnvironmentEntry FirstEntry=Fixture.Root->Entries[0]; // 只冻结值，恢复时不依赖ActorLabel/新顺序身份。
    Fixture.Root->Entries.RemoveAt(0); Data->AuthoredEnvironmentItemCount=3;
    ExpectFallback(*this,Fixture,TEXT("Attachment preserves ownership after tag and entry loss"),TEXT("unlisted_member"));
    TestFalse(TEXT("Unlisted untagged attached member stays collision-disabled"),Fixture.MeshA->GetActorEnableCollision());
    Fixture.Root->Entries.Insert(FirstEntry,0); Data->AuthoredEnvironmentItemCount=4; Fixture.MeshA->Tags.Add(ContractMemberTag);

    Fixture.Root->bIsEditorOnlyActor=true;
    ExpectFallback(*this,Fixture,TEXT("Editor-only root"),TEXT("root_level_tag_replication_or_load_policy"));
    Fixture.Root->bIsEditorOnlyActor=false; Fixture.Root->bNetLoadOnClient=false;
    ExpectFallback(*this,Fixture,TEXT("Client-excluded root"),TEXT("root_level_tag_replication_or_load_policy"));
    Fixture.Root->bNetLoadOnClient=true; Fixture.MeshA->bIsEditorOnlyActor=true;
    ExpectFallback(*this,Fixture,TEXT("Editor-only member"),TEXT("entry_world_level_tag_replication_load_policy_or_transform"));
    Fixture.MeshA->bIsEditorOnlyActor=false; Fixture.MeshA->bNetLoadOnClient=false;
    ExpectFallback(*this,Fixture,TEXT("Client-excluded member"),TEXT("entry_world_level_tag_replication_load_policy_or_transform"));
    Fixture.MeshA->bNetLoadOnClient=true;

    AStaticMeshActor* ProtectedFloor=Fixture.SpawnMesh(); // 模拟原Hub地板误加MemberTag，原保护域必须胜过新标签。
    if(!TestNotNull(TEXT("Protected original floor fixture"),ProtectedFloor)) return false;
    ProtectedFloor->Tags.Add(TEXT("DemoArea-1")); ProtectedFloor->Tags.Add(TEXT("DemoAuthoredBlockout"));
    ExpectFallback(*this,Fixture,TEXT("Protected original actor is not a member"),TEXT("unlisted_member"));
    TestFalse(TEXT("Mis-tagged original floor remains visible"),ProtectedFloor->IsHidden());
    TestTrue(TEXT("Mis-tagged original floor keeps collision"),ProtectedFloor->GetActorEnableCollision());
    TestTrue(TEXT("Mis-tagged original component remains visible"),ProtectedFloor->GetStaticMeshComponent()->IsVisible());
    ProtectedFloor->Tags.Remove(ContractMemberTag);
    AStaticMeshActor* WrongRoot=Fixture.SpawnMesh(); // 普通环境被错标Root，没有Member标签时也必须在回退中抑制。
    if(!TestNotNull(TEXT("Wrong class root fixture"),WrongRoot)) return false;
    WrongRoot->Tags.Remove(ContractMemberTag); WrongRoot->Tags.Add(ContractRootTag);
    ExpectFallback(*this,Fixture,TEXT("Ordinary mesh cannot be a root"),TEXT("missing_duplicate_or_wrong_root_class"));
    TestTrue(TEXT("Wrong root mesh is hidden before legacy fallback"),WrongRoot->IsHidden());
    TestFalse(TEXT("Wrong root mesh collision suppressed"),WrongRoot->GetActorEnableCollision());
    WrongRoot->Tags.Remove(ContractRootTag);

    ADemoRuntimeArenaGeometry* Derived=Fixture.Spawn<ADemoRuntimeArenaGeometry>(); // 已有真正StaticMeshActor派生类，无需新增UCLASS伪夹具。
    if(!TestNotNull(TEXT("Existing derived mesh actor fixture"),Derived)) return false;
    Derived->SetReplicates(false); Derived->Tags.Add(ContractMemberTag); // 排除复制位作为拒绝原因，只检验精确普通类边界。
    Fixture.Root->Entries.Add(FHubEnvironmentFixture::Entry(TEXT("derived"),Derived)); Data->AuthoredEnvironmentItemCount=5;
    ExpectFallback(*this,Fixture,TEXT("Gameplay-capable derived mesh rejected"),TEXT("entry_id_actor_type_or_duplicate"));
    TestFalse(TEXT("Rejected derived actor is not hidden by environment fallback"),Derived->IsHidden());
    TestTrue(TEXT("Rejected derived actor keeps actor collision"),Derived->GetActorEnableCollision());
    Fixture.Root->Entries.Pop(); Data->AuthoredEnvironmentItemCount=4; Derived->Tags.Remove(ContractMemberTag);

    Data->AuthoredEnvironmentRootTag=TEXT("WrongScope"); // 仅环境元数据坏，不能把原商店定义/交互整体禁用。
    TestTrue(TEXT("Bad environment metadata does not invalidate base shop"),Data->Validate(Error));
    TestFalse(TEXT("Bad environment metadata separately rejected"),Data->ValidateAuthoredEnvironment(Error));
    TestFalse(TEXT("Bad environment metadata resolves to fallback"),ADemoHubShopEnvironmentRoot::ResolveForWorld(Fixture.World,Data,Fixture.HubFrame,Error));
    Data->AuthoredEnvironmentRootTag=ContractRootTag;
    TestTrue(TEXT("Valid closure can be selected after all rejected variants"),ADemoHubShopEnvironmentRoot::ResolveForWorld(Fixture.World,Data,Fixture.HubFrame,Error));
    TestFalse(TEXT("No BeginPlay after contract checks"),Fixture.World->HasBegunPlay());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoHubEnvironmentReversibleContracts,"FPSDemo.HubShops.Environment.ReversibleFallbackAndAnchors",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

/** Parameters未使用；验证两轮原状态恢复与实际Blockout旧/新外观选择，测试不声称运行/照明/多人验收。 */
bool FDemoHubEnvironmentReversibleContracts::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    FHubEnvironmentFixture Fixture;
    if(!TestTrue(TEXT("Reversible fixture ready"),Fixture.IsReady())) return false;
    UDemoHubShopDefinition* Data=Fixture.Definition.Get(); // 本测试独立实例，生命周期由Fixture强引用覆盖。
    UPointLightComponent* Point=Fixture.Point->FindComponentByClass<UPointLightComponent>(); // 真实注册组件，非自造状态替身。
    URectLightComponent* Rect=Fixture.Rect->FindComponentByClass<URectLightComponent>();
    if(!TestNotNull(TEXT("Actual point component"),Point)||!TestNotNull(TEXT("Actual rect component"),Rect)) return false;
    Fixture.MeshB->SetActorHiddenInGame(true); Fixture.MeshB->SetActorEnableCollision(false);
    Fixture.MeshB->GetStaticMeshComponent()->SetVisibility(false);
    Fixture.MeshB->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("NoCollision"));
    Point->SetVisibility(false); Point->SetIntensity(777.f); Rect->SetIntensity(1234.f);
    const FName Profile=Fixture.MeshA->GetStaticMeshComponent()->GetCollisionProfileName(); // 抑制不应重写Primitive碰撞配置。
    FString Error;
    Data->bUseAuthoredEnvironment=false;
    ExpectFallback(*this,Fixture,TEXT("First suppression"),TEXT("disabled_or_missing_definition"));
    ExpectFallback(*this,Fixture,TEXT("Repeated suppression is idempotent"),TEXT("disabled_or_missing_definition"));
    TestFalse(TEXT("Visible rect light suppressed"),Rect->IsVisible());
    Data->bUseAuthoredEnvironment=true;
    TestTrue(TEXT("First restore selected"),ADemoHubShopEnvironmentRoot::ResolveForWorld(Fixture.World,Data,Fixture.HubFrame,Error));
    TestFalse(TEXT("Originally visible actor restored visible"),Fixture.MeshA->IsHidden());
    TestTrue(TEXT("Originally colliding actor collision restored"),Fixture.MeshA->GetActorEnableCollision());
    TestTrue(TEXT("Originally hidden actor stays hidden"),Fixture.MeshB->IsHidden());
    TestFalse(TEXT("Originally disabled collision stays disabled"),Fixture.MeshB->GetActorEnableCollision());
    TestFalse(TEXT("Originally invisible mesh stays invisible"),Fixture.MeshB->GetStaticMeshComponent()->IsVisible());
    TestFalse(TEXT("Originally invisible point light stays invisible"),Point->IsVisible());
    TestTrue(TEXT("Originally visible rect light restored"),Rect->IsVisible());
    TestEqual(TEXT("Point intensity preserved"),Point->Intensity,777.f);
    TestEqual(TEXT("Rect intensity preserved"),Rect->Intensity,1234.f);
    TestEqual(TEXT("Collision profile preserved"),Fixture.MeshA->GetStaticMeshComponent()->GetCollisionProfileName(),Profile);

    Fixture.MeshA->SetActorHiddenInGame(true); Fixture.MeshA->SetActorEnableCollision(false);
    Fixture.MeshA->GetStaticMeshComponent()->SetVisibility(false);
    Fixture.MeshB->SetActorHiddenInGame(false); Fixture.MeshB->SetActorEnableCollision(true);
    Fixture.MeshB->GetStaticMeshComponent()->SetVisibility(true);
    Point->SetVisibility(true); Point->SetIntensity(999.f); Rect->SetVisibility(false); // 第二合法周期的新原值不能被第一轮旧快照覆盖。
    Data->bUseAuthoredEnvironment=false;
    ExpectFallback(*this,Fixture,TEXT("Second suppression"),TEXT("disabled_or_missing_definition"));
    Data->bUseAuthoredEnvironment=true;
    TestTrue(TEXT("Second restore selected"),ADemoHubShopEnvironmentRoot::ResolveForWorld(Fixture.World,Data,Fixture.HubFrame,Error));
    TestTrue(TEXT("Second-cycle hidden value retained"),Fixture.MeshA->IsHidden());
    TestFalse(TEXT("Second-cycle collision value retained"),Fixture.MeshA->GetActorEnableCollision());
    TestFalse(TEXT("Second-cycle component invisible value retained"),Fixture.MeshA->GetStaticMeshComponent()->IsVisible());
    TestFalse(TEXT("Second-cycle formerly hidden actor now visible"),Fixture.MeshB->IsHidden());
    TestTrue(TEXT("Second-cycle collision true restored"),Fixture.MeshB->GetActorEnableCollision());
    TestTrue(TEXT("Second-cycle point visible true restored"),Point->IsVisible());
    TestFalse(TEXT("Second-cycle rect visible false restored"),Rect->IsVisible());
    TestEqual(TEXT("Second-cycle intensity retained"),Point->Intensity,999.f);
    TestEqual(TEXT("Original NoCollision profile not rewritten"),Fixture.MeshB->GetStaticMeshComponent()->GetCollisionProfileName(),FName(TEXT("NoCollision")));

    ADemoHubShopBlockout* Blockout=Fixture.Spawn<ADemoHubShopBlockout>(); // 只有本测试Actor，不触发WorldSubsystem/Game生命周期。
    if(!TestNotNull(TEXT("Actual Blockout fixture"),Blockout)) return false;
    Data->bUseAuthoredEnvironment=false;
    if(!TestTrue(TEXT("Legacy Configure remains usable"),Blockout->Configure(Data,43))) return false;
    TestFalse(TEXT("Legacy selection reported"),Blockout->IsUsingAuthoredEnvironment());
    TestTrue(TEXT("Legacy has appearance pieces plus anchors"),Blockout->GetGeometryCount()>16);
    TestEqual(TEXT("Definition has 16 stable anchors"),Data->Anchors.Num(),16);
    for(const FDemoHubShopAnchor& Anchor:Data->Anchors)
    {
        FTransform Actual; // 每项从实际构建组件读世界变换，不用待写作者目标假装读回。
        TestTrue(FString(TEXT("Legacy anchor "))+Anchor.AnchorId.ToString(),Blockout->GetAnchorTransform(Anchor.AnchorId,Actual));
        TestTrue(TEXT("Legacy anchor uses actual HubFrame"),Actual.Equals(Anchor.Transform*Fixture.HubFrame,.01f));
    }
    Data->bUseAuthoredEnvironment=true;
    const FName ValidEnvironmentId=Data->AuthoredEnvironmentId; // 故意破坏仅环境元数据，检验Configure仍能生成legacy与锚点。
    Data->AuthoredEnvironmentId=NAME_None;
    TestTrue(TEXT("Bad environment metadata does not disable actual Blockout Configure"),Blockout->Configure(Data,43));
    TestFalse(TEXT("Bad metadata Blockout reports fallback"),Blockout->IsUsingAuthoredEnvironment());
    TestTrue(TEXT("Bad metadata still has legacy geometry and anchors"),Blockout->GetGeometryCount()>16);
    Data->AuthoredEnvironmentId=ValidEnvironmentId;
    if(!TestTrue(TEXT("Authored Configure remains usable"),Blockout->Configure(Data,43))) return false;
    TestTrue(TEXT("Complete environment selected by actual Blockout"),Blockout->IsUsingAuthoredEnvironment());
    TestTrue(TEXT("Success has no stale selection error"),Blockout->GetEnvironmentSelectionError().IsEmpty());
    TestEqual(TEXT("Only 16 logical anchors, legacy appearance removed"),Blockout->GetGeometryCount(),16);
    TestEqual(TEXT("World epoch preserved"),Blockout->GetAuthorEpoch(),43);
    for(const FDemoHubShopAnchor& Anchor:Data->Anchors)
    {
        FTransform Actual;
        TestTrue(FString(TEXT("Authored anchor "))+Anchor.AnchorId.ToString(),Blockout->GetAnchorTransform(Anchor.AnchorId,Actual));
        TestTrue(TEXT("Authored anchor uses same actual HubFrame"),Actual.Equals(Anchor.Transform*Fixture.HubFrame,.01f));
    }
    TestFalse(TEXT("Contract never entered BeginPlay"),Fixture.World->HasBegunPlay());
    return true;
}
#endif
