// 职责：在独立Transient Editor World调用生产火光工厂，核四枪真实Mesh Socket、机械Pose、附着与CPU粒子生命周期。
// 使用：资产作者保存后，正常可渲染Editor执行Automation RunTests FPSDemo.Weapons.MuzzleFlash.Attachment；不PIE/BeginPlay/-game。
// 依赖真实四枪CDO、MuzzleFlash mesh-only、正式短闪模板及SampleFlashFrame；Scene/FX真实初始化，Physics关闭，不做图片/听感/玩家相机/多人验收。
// 显式组件模拟先撤销该Transient PSC的自动manager调度，不Deactivate/改模板/全局CVars；不验证游戏中的GT/WorldManager调度。
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Weapons/DemoWeaponBase.h"
#include "Animation/DemoFPAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Diagnostics/DemoMuzzleFlashAuthoring.h"
#include "Diagnostics/DemoMuzzleFlashVfxAuthoring.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "ParticleEmitterInstances.h"
#include "ParticleHelper.h"
#include "Materials/Material.h"
#include "AssetCompilingManager.h"
#include "SceneInterface.h"
#include "Misc/App.h"
#include "Debug/DemoStaticLog.h"

namespace
{
    const FName ContractFlashSocketName(TEXT("MuzzleFlash")); // 外部资产合同固定名，不能把原物理Muzzle当测试替代输入。
    const FName PhysicalSocketName(TEXT("Muzzle")); // 原物理插槽必须仍存在；所有测试只读该作者对象。
    constexpr float ParticleStep=.005f; // 5ms显式CPU步进，小于正式40ms粒子寿命；不推进整个World或游戏计时。

    /** 独占可创建FX的Editor World；无Physics/GI/BeginPlay，析构清理全部Transient Actor及PSC。 */
    struct FFlashAttachmentFixture
    {
        UWorld* World=nullptr; // CreateWorld加Root持有，仅本同步测试拥有，DestroyWorld后不保留引用。
        bool bOwnsWorldContext=false; // 只登记本夹具的Editor context，清理本World后精确移除，不修改当前编辑器context。

        /** 真实Scene+FX初始化是PSC能产粒子的必要条件；不以NullRHI/无FX下的0粒冒充自然完成。 */
        FFlashAttachmentFixture()
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            const UWorld::InitializationValues Init=UWorld::InitializationValues().InitializeScenes(true)
                .CreateFXSystem(true).CreatePhysicsScene(false).ShouldSimulatePhysics(false)
                .AllowAudioPlayback(false).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false)
                .EnableTraceCollision(false).SetTransactional(false); // 只为真实Cascade组件初始化Scene，不启动任何游戏/物理流程。
            const FName Name=MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("MuzzleFlashAttachmentContracts")); // 不碰当前Editor地图/GWorld。
            World=UWorld::CreateWorld(EWorldType::Editor,false,Name,GetTransientPackage(),true,ERHIFeatureLevel::Num,&Init);
            if(World)
            {
                World->SetFlags(RF_Transient);
                if(GEngine)
                {
                    FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Editor); // 本World独占上下文，供原生Actor销毁/清理定位，无GI/BeginPlay。
                    Context.SetCurrentWorld(World); bOwnsWorldContext=true;
                }
            }
        }

        /** 任意失败路径均销毁独占World；不会用主动Destroy去促成下面的自然完成断言。 */
        ~FFlashAttachmentFixture()
        {
            DEMO_LOG_STATIC_CALL(World);
            if(World)
            {
                World->DestroyWorld(false); // Context保留至原生World清理完毕，避免Actor清理缺context。
                if(bOwnsWorldContext&&GEngine) GEngine->DestroyWorldContext(World);
            }
        }

        /** 每枪/scale创建独立Actor及真实骨骼组件；输入Mesh只读借用，Actor/组件均Transient。 */
        USkeletalMeshComponent* SpawnSource(USkeletalMesh* Mesh)
        {
            DEMO_LOG_STATIC_CALL(World);
            if(!World||!Mesh) return nullptr;
            FActorSpawnParameters Params; // Actor只存在本PersistentLevel，禁止正式Map保存/碰撞或网络。
            Params.OverrideLevel=World->PersistentLevel; Params.ObjectFlags=RF_Transient;
            Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            AActor* Actor=World->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,Params); // World拥有，到case末尾或fixture析构清理。
            if(!Actor) return nullptr;
            USkeletalMeshComponent* Source=NewObject<USkeletalMeshComponent>(Actor,NAME_None,RF_Transient);
            Actor->SetRootComponent(Source); Actor->AddInstanceComponent(Source); Actor->SetActorEnableCollision(false);
            Source->SetMobility(EComponentMobility::Movable); Source->SetSkeletalMesh(Mesh);
            Source->SetCollisionEnabled(ECollisionEnabled::NoCollision); Source->SetGenerateOverlapEvents(false);
            Source->SetDisablePostProcessBlueprint(true); Source->SetForcedLOD(1);
            Source->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Source->RegisterComponent();
            return Source;
        }
    };

    /** 当前保存CDO的真正机械Fire路径；逐发拉栓枪沿生产PlayFire选择BoltCycle，不猜历史动画包名。 */
    UAnimSequence* ResolveMechanicalFire(const ADemoWeaponBase* Weapon)
    {
        DEMO_LOG_STATIC_CALL(Weapon);
        const UDemoWeaponLayerAnimInstance* Layer=Weapon&&Weapon->Config.WeaponAnimLayerClass
            ?Cast<UDemoWeaponLayerAnimInstance>(Weapon->Config.WeaponAnimLayerClass->GetDefaultObject()):nullptr; // 只借实际配置类CDO，不创建或修改AnimBP。
        if(!Layer) return nullptr;
        return Weapon->Config.bCycleBoltAfterShot?Layer->AnimSet.BoltCycle.WeaponSequence.Get():Layer->AnimSet.FireWeaponSequence.Get();
    }

    /** 采样之后核真实组件世界TRS和零Relative；不以目标Socket值回填PSC或只比较作者输入。 */
    void CheckAttachment(FAutomationTestBase& Test,USkeletalMeshComponent* Source,UParticleSystemComponent* Flash,const FString& Context)
    {
        DEMO_LOG_STATIC_TICK(Source);
        const FTransform Socket=Source->GetSocketTransform(ContractFlashSocketName,RTS_World); // 本帧实际骨求值后的原生Socket位置/旋转/scale。
        const FTransform Actual=Flash->GetComponentTransform(); // 真实PSC读回，不读取计算helper缓存。
        Test.TestTrue(Context+TEXT(" world Socket/PSC TRS match"),Actual.Equals(Socket,.001));
        Test.TestTrue(Context+TEXT(" relative transform remains identity"),Flash->GetRelativeTransform().Equals(FTransform::Identity,.0001));
        Test.TestTrue(Context+TEXT(" parent is actual gun component"),Flash->GetAttachParent()==Source);
        Test.TestTrue(Context+TEXT(" named Flash socket remains attached"),Flash->GetAttachSocketName()==ContractFlashSocketName);
        Test.TestTrue(Context+TEXT(" OnlyOwnerSee copied from visible source"),Flash->bOnlyOwnerSee==Source->bOnlyOwnerSee);
        Test.TestTrue(Context+TEXT(" OwnerNoSee copied from visible source"),Flash->bOwnerNoSee==Source->bOwnerNoSee);
        Test.TestTrue(Context+TEXT(" no collision"),Flash->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        Test.TestFalse(Context+TEXT(" no component replication"),Flash->GetIsReplicated());
        Test.TestFalse(Context+TEXT(" no physics simulation"),Flash->IsSimulatingPhysics());
    }

    /** 完成真实PSC异步屏障后读CPU活跃粒子；检查本地中心/速度、寿命与当前Socket，不把最终0粒当曾初始化。 */
    int32 ObserveParticles(FAutomationTestBase& Test,USkeletalMeshComponent* Source,UParticleSystemComponent* Flash,
        const FString& Context,bool& bSawYoung,bool& bSawMiddle,bool& bSawLate)
    {
        DEMO_LOG_STATIC_TICK(Flash);
        const int32 Count=Flash->GetNumActiveParticles(); // 引擎内部ForceAsyncWorkCompletion(STALL)，之后才允许读EmitterInstances/Particle。
        Test.TestTrue(Context+TEXT(" bounded real particle count"),Count>=0&&Count<=1);
        if(Count==0) return 0;
        if(!Test.TestEqual(Context+TEXT(" one actual CPU emitter instance"),Flash->EmitterInstances.Num(),1)) return Count;
        FParticleEmitterInstance* Instance=Flash->EmitterInstances[0]; // 本次GT屏障后的同步借用，不跨Tick保留粒子地址。
        if(!Test.TestNotNull(Context+TEXT(" CPU emitter instance exists"),Instance)) return Count;
        const UParticleLODLevel* LOD=Instance->CurrentLODLevel; // 当前实际运行LOD，不能只核模板另一LOD。
        if(!Test.TestTrue(Context+TEXT(" active emitter really uses local space"),LOD&&LOD->RequiredModule&&LOD->RequiredModule->bUseLocalSpace)) return Count;
        for(int32 Index=0;Index<Instance->ActiveParticles;++Index)
        {
            FBaseParticle* Particle=Instance->GetParticle(Index); // GetParticle用active索引映射ParticleIndices，不能直接把序号当存储槽。
            if(!Test.TestNotNull(Context+TEXT(" active CPU particle exists"),Particle)) continue;
            Test.TestTrue(Context+TEXT(" local particle centre remains muzzle origin"),Particle->Location.IsNearlyZero(.001));
            Test.TestTrue(Context+TEXT(" base and actual velocity remain zero"),Particle->BaseVelocity.IsNearlyZero(.001f)&&Particle->Velocity.IsNearlyZero(.001f));
            Test.TestTrue(Context+TEXT(" actual lifetime is 55ms"),FMath::IsNearlyEqual(Particle->OneOverMaxLifetime,1.f/.055f,.01f));
            const FVector Centre(Instance->SimulationToWorld.TransformPosition(Particle->Location)); // 引擎实际simulation空间矩阵，不假设PSC坐标永远等于粒子空间。
            Test.TestTrue(Context+TEXT(" particle world centre follows actual animated socket"),Centre.Equals(Source->GetSocketLocation(ContractFlashSocketName),.002));
            bSawYoung|=Particle->RelativeTime<.4f; bSawMiddle|=Particle->RelativeTime>=.4f&&Particle->RelativeTime<.8f;
            bSawLate|=Particle->RelativeTime>=.8f; // 相对寿命接近/超过1后下次Kill才移除，不要求恰40ms销毁。
        }
        return Count;
    }

    /** 单枪/比例用真实生产工厂，从三个机械时刻开始并持续小步采到自然销毁；只改本case瞬态组件。 */
    bool RunAttachmentCase(FAutomationTestBase& Test,FFlashAttachmentFixture& Fixture,const ADemoWeaponBase* Weapon,
        UAnimSequence* Mechanical,const FString& Model,float Scale)
    {
        DEMO_LOG_STATIC_CALL(Weapon);
        USkeletalMeshComponent* Source=Fixture.SpawnSource(Weapon->Config.Mesh); // 正式Mesh只读，实际组件/Actor本case独占。
        if(!Test.TestNotNull(Model+TEXT(" transient source exists"),Source)) return false;
        AActor* Actor=Source->GetOwner(); // World拥有；只在全部自然生命周期断言完成后主动清理Actor。
        const FString Context=FString::Printf(TEXT("%s scale%.1f"),*Model,Scale);
        Source->SetOnlyOwnerSee(Scale<1.f); Source->SetOwnerNoSee(Scale>=1.f); // 两种真实源flag合同；不声称验证OnlyOwnerSee的实际视图渲染。
        Source->SetWorldTransform(FTransform(FRotator(5,30,0),FVector(200,-40,80),FVector(Scale)));
        Source->SetAnimationMode(EAnimationMode::AnimationSingleNode); Source->SetAnimation(Mechanical); Source->SetPosition(0.f,false);
        Source->TickAnimation(0.f,false); Source->RefreshBoneTransforms(); Source->UpdateChildTransforms();
        UParticleSystemComponent* Flash=ADemoWeaponBase::SpawnAttachedMuzzleFlash(Weapon->Config.MuzzleEffect,Source,Weapon->Config.MuzzleFlashSocket);
        if(!Test.TestNotNull(Context+TEXT(" production attached factory creates PSC"),Flash)) { Actor->Destroy(); return false; }
        TWeakObjectPtr<UParticleSystemComponent> WeakFlash=Flash; // 不保活自动DestroyComponent；原始地址仅用于末尾组件列表包含比较。
        const bool bFactoryManaged=Flash->IsTickManaged(); const int32 FactoryManagerHandle=Flash->GetManagerHandle(); // 工厂原始调度状态，不能把事后手动模式冒充生产默认。
        Flash->SetComponentTickEnabled(false); // 原生公开接口只撤销此PSC的manager/自动调度；否则手调TickComponent会跳过Compute/Finalize。
        Test.TestFalse(Context+TEXT(" explicit simulation removes only this PSC from managed ticking"),Flash->IsTickManaged());
        Test.TestFalse(Context+TEXT(" automatic scheduling is disabled for explicit fixture steps"),Flash->IsComponentTickEnabled());
        Test.TestTrue(Context+TEXT(" detaching tick scheduling does not deactivate the flash"),Flash->IsActive());
        Test.TestTrue(Context+TEXT(" engine autoDestroy enabled"),Flash->bAutoDestroy);
        Test.TestTrue(Context+TEXT(" actual PSC registered"),Flash->IsRegistered());
        bool bSawYoung=false,bSawMiddle=false,bSawLate=false; // 必须观测实际活跃粒子的早/中/末寿命，不能由模板声明推断。
        int32 LiveSamples=0; // 真实Count>0的检查次数，末尾报告范围，失败不记成有限通过。
        float FirstSimulationAge=-1.f,LastSimulationAge=-1.f; // 实例真实SecondsSinceCreation，证明显式调用确实进入模拟而非只更新组件变换。
        for(int32 Frame=0;Frame<3;++Frame)
        {
            Source->SetWorldTransform(FTransform(FRotator(Frame*7.f,30.f+Frame*53.f,Frame*3.f),FVector(200+Frame*120,-40+Frame*73,80+Frame*9),FVector(Scale)));
            const float PoseTime=Mechanical->GetPlayLength()*(Frame*.5f); // 真正素材0/50%/100%三个时刻，不称中点就是后坐峰值。
            const FString FrameJson=UDemoMuzzleFlashAuthoring::SampleFlashFrame(Source,Mechanical,PoseTime,Flash,ParticleStep); // 实际机械Pose/附着/PSC推进，未BeginPlay。
            Test.TestFalse(Context+TEXT(" real frame sampler returns facts"),FrameJson.IsEmpty());
            if(!Test.TestTrue(Context+TEXT(" PSC survives three early5ms samples"),WeakFlash.IsValid())) { Actor->Destroy(); return false; }
            CheckAttachment(Test,Source,Flash,Context);
            const int32 Count=ObserveParticles(Test,Source,Flash,Context,bSawYoung,bSawMiddle,bSawLate);
            if(Flash->EmitterInstances.Num()>0&&Flash->EmitterInstances[0])
            {
                LastSimulationAge=Flash->EmitterInstances[0]->SecondsSinceCreation;
                if(FirstSimulationAge<0.f) FirstSimulationAge=LastSimulationAge;
            }
            Test.TestEqual(Context+TEXT(" genuine burst particle exists in early frame"),Count,1);
            if(Count>0) ++LiveSamples;
        }
        // 只用显式组件Tick积累真实粒子时间。Source持续平移/转向直至自然完成，不对PSC调用Deactivate/Destroy。
        for(int32 StepIndex=0;StepIndex<27&&WeakFlash.IsValid();++StepIndex)
        {
            Source->AddWorldOffset(FVector(13,-7,2)); Source->AddWorldRotation(FRotator(1,17,0)); Source->UpdateChildTransforms();
            Flash->TickComponent(ParticleStep,LEVELTICK_All,nullptr); // 空TickFunction使用原生同步分支，不启动整个World/玩法或假冒实际GT帧调度。
            if(!WeakFlash.IsValid()) break; // 自然DestroyComponent后绝不再读旧组件/粒子地址。
            Flash->GetNumActiveParticles(); // 真正异步/Finalize屏障可能在此完成自动销毁。
            if(!WeakFlash.IsValid()) break;
            CheckAttachment(Test,Source,Flash,Context);
            if(ObserveParticles(Test,Source,Flash,Context,bSawYoung,bSawMiddle,bSawLate)>0) ++LiveSamples;
            if(Flash->EmitterInstances.Num()>0&&Flash->EmitterInstances[0]) LastSimulationAge=Flash->EmitterInstances[0]->SecondsSinceCreation;
        }
        Test.TestTrue(Context+TEXT(" actual emitter simulation time advanced"),FirstSimulationAge>0.f&&LastSimulationAge>FirstSimulationAge);
        Test.TestTrue(Context+TEXT(" observed real particle young/middle/late lifetime phases"),bSawYoung&&bSawMiddle&&bSawLate);
        Test.TestTrue(Context+TEXT(" genuine active particles were observed before completion"),LiveSamples>=3);
        Test.TestFalse(Context+TEXT(" factory PSC naturally auto-destroys by150ms explicit simulation"),WeakFlash.IsValid());
        TInlineComponentArray<UActorComponent*> Components(Actor); // 当前真实Owner组件列表，不能只看最终粒子数0就称已经销毁。
        Test.TestFalse(Context+TEXT(" naturally completed PSC removed from owner component list"),Components.Contains(Flash));
        Test.AddInfo(FString::Printf(TEXT("MUZZLE_FLASH_ATTACHMENT_CASE model=%s scale=%.1f source=%s sequence=%s factoryManaged=%d factoryManagerHandle=%d firstSimAge=%.6f lastLiveSimAge=%.6f liveSamples=%d young=%d middle=%d late=%d autoDestroyed=%d scope=EditorCPUExplicitStepsNoGameScheduling"),
            *Model,Scale,*GetPathNameSafe(Weapon->Config.Mesh),*GetPathNameSafe(Mechanical),bFactoryManaged,FactoryManagerHandle,FirstSimulationAge,LastSimulationAge,LiveSamples,bSawYoung,bSawMiddle,bSawLate,!WeakFlash.IsValid()));
        Actor->Destroy(); // 生命周期断言之后才正常清理case；失败自然完成仍保留失败记录。
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoMuzzleFlashAttachmentContracts,"FPSDemo.Weapons.MuzzleFlash.Attachment",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

/** Parameters未使用；资产未创建/未绑定即失败，既不补资产也不把缺资源作为跳过通过。 */
bool FDemoMuzzleFlashAttachmentContracts::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    if(!FApp::CanEverRender())
    { AddError(TEXT("Requires a rendering-capable Editor with Scene/FX initialization; NullRHI/headless zero particles cannot prove attached flash lifetime.")); return false; }
    FFlashAttachmentFixture Fixture; // 本同步函数独占，不创建GI/BeginPlay/PIE/World Tick，不操作当前Editor地图。
    if(!TestNotNull(TEXT("Independent Editor World"),Fixture.World)||!TestNotNull(TEXT("Real Scene for native PSC initialization"),Fixture.World->Scene)) return false;
    if(!TestNotNull(TEXT("Native Scene FXSystem exists; missing FX cannot pass by producing0particles"),Fixture.World->Scene->GetFXSystem())) return false;
    TestFalse(TEXT("No BeginPlay"),Fixture.World->HasBegunPlay()); TestFalse(TEXT("No Game World"),Fixture.World->IsGameWorld());
    TestTrue(TEXT("Fixture owns a real Editor WorldContext for native cleanup"),Fixture.bOwnsWorldContext&&GEngine&&GEngine->GetWorldContextFromWorld(Fixture.World));
    TestNull(TEXT("No GameInstance"),Fixture.World->GetGameInstance()); TestNull(TEXT("No Physics Scene"),Fixture.World->GetPhysicsScene());
    const TCHAR* Models[]={TEXT("Pistol"),TEXT("Rifle"),TEXT("Shotgun"),TEXT("Sniper")}; // 固定四个实际正式BP，不用测试定义代替某枪CDO。
    for(const TCHAR* Model:Models)
    {
        const FString ClassPath=FString::Printf(TEXT("/Game/Weapons/Blueprints/BP_Weapon_%s.BP_Weapon_%s_C"),Model,Model); // 保存的GeneratedClass真实路径。
        const UClass* Class=LoadClass<ADemoWeaponBase>(nullptr,*ClassPath);
        const ADemoWeaponBase* Weapon=Class?Cast<ADemoWeaponBase>(Class->GetDefaultObject()):nullptr; // 只读CDO，不生成武器/射击/伤害。
        if(!TestNotNull(FString(Model)+TEXT(" real weapon CDO"),Weapon)) continue;
        USkeletalMesh* Mesh=Weapon->Config.Mesh; // 当前真实Config.Mesh；不借旧Mesh或制作源替代。
        if(!TestNotNull(FString(Model)+TEXT(" actual skeletal Mesh"),Mesh)||!TestNotNull(FString(Model)+TEXT(" configured real muzzle effect"),Weapon->Config.MuzzleEffect.Get())) continue;
        if(!TestNotNull(FString(Model)+TEXT(" actual Mesh Skeleton exists"),Mesh->GetSkeleton())) continue;
        FAssetCompilingManager::Get().FinishAllCompilation(); // 只等加载中资源稳定，不请求重烘焙、重建或保存。
        TestTrue(FString(Model)+TEXT(" physical socket remains Muzzle"),Weapon->Config.MuzzleSocket==PhysicalSocketName);
        TestTrue(FString(Model)+TEXT(" flash socket is independently MuzzleFlash"),Weapon->Config.MuzzleFlashSocket==ContractFlashSocketName);
        const USkeletalMeshSocket* OriginalMuzzle=Mesh->FindSocket(PhysicalSocketName); // 新火光不能复用/覆盖原物理Socket。
        if(!TestNotNull(FString(Model)+TEXT(" original physical Muzzle author socket still exists"),OriginalMuzzle)) continue;
        const FTransform MuzzleLocalBefore=OriginalMuzzle->GetSocketLocalTransform(); const FName MuzzleBoneBefore=OriginalMuzzle->BoneName; // 输入保护事实，逐枪case后再次读实际对象。
        const int32 SkeletonSocketsBefore=Mesh->GetSkeleton()->Sockets.Num(); // 测试/工厂不能向共享Skeleton新增Socket。
        int32 FlashCount=0; const USkeletalMeshSocket* FlashSocket=nullptr; // 仅mesh-only集合，不靠FindSocket的Skeleton回退冒成功。
        for(const USkeletalMeshSocket* Socket:Mesh->GetMeshOnlySocketList()) if(Socket&&Socket->SocketName==ContractFlashSocketName) { ++FlashCount; FlashSocket=Socket; }
        if(!TestEqual(FString(Model)+TEXT(" exactly one mesh-only Flash socket"),FlashCount,1)||!TestNotNull(FString(Model)+TEXT(" actual mesh-owned Flash socket"),FlashSocket)) continue;
        TestTrue(FString(Model)+TEXT(" Flash Socket Outer is this Mesh"),FlashSocket->GetOuter()==Mesh);
        UAnimSequence* Mechanical=ResolveMechanicalFire(Weapon); // 从真实AnimSet读取生产选择，狙击逐发路径也覆盖。
        if(!TestNotNull(FString(Model)+TEXT(" actual mechanical Fire sequence"),Mechanical)) continue;
        if(!TestTrue(FString(Model)+TEXT(" mechanical sequence matches actual Mesh Skeleton and has duration"),Mechanical->GetSkeleton()==Mesh->GetSkeleton()&&Mechanical->GetPlayLength()>0.f)) continue;
        UParticleSystem* System=Weapon->Config.MuzzleEffect; // 当前CDO配置模板，无新建临时假模板。
        UMaterial* Material=System->Emitters.Num()>0&&System->Emitters[0]&&System->Emitters[0]->LODLevels.Num()>0&&System->Emitters[0]->LODLevels[0]&&System->Emitters[0]->LODLevels[0]->RequiredModule
            ?Cast<UMaterial>(System->Emitters[0]->LODLevels[0]->RequiredModule->Material):nullptr; // 实际模板Required材质，不猜包名。
        FString AssetFacts; // 同模块只读模板/材质全模块合同；不创建或自动修复正式资源。
        if(!TestTrue(FString(Model)+TEXT(" actual local-space/zero-origin/zero-velocity/55ms template contract"),UDemoMuzzleFlashVfxAuthoring::AuditFlashSystem(System,Material,AssetFacts)))
        { AddInfo(AssetFacts); continue; }
        RunAttachmentCase(*this,Fixture,Weapon,Mechanical,Model,1.f);
        RunAttachmentCase(*this,Fixture,Weapon,Mechanical,Model,.2f);
        TestTrue(FString(Model)+TEXT(" original Muzzle object/parent/transform protected"),Mesh->FindSocket(PhysicalSocketName)==OriginalMuzzle&&OriginalMuzzle->BoneName==MuzzleBoneBefore&&OriginalMuzzle->GetSocketLocalTransform().Equals(MuzzleLocalBefore,.000001));
        TestEqual(FString(Model)+TEXT(" shared Skeleton Socket count unchanged"),Mesh->GetSkeleton()->Sockets.Num(),SkeletonSocketsBefore);
    }
    return true;
}
#endif
