// 职责：命中视觉的生产弹簧/血雾预算/事件布局与真实Enemy尸体碰撞合同。使用：Editor Automation RunTests FPSDemo.HitFx；依赖实际配置与只读生产网格，不启动PIE、GI、云存档或游戏模式。
// 本合同证明有限值、预算、回退及查询隔离；不代表实际渲染、合格ragdoll资产、实战观感、2～4人或Cook/Shipping验收。
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoHitReactSpring.h"
#include "AI/DemoEnemyDefinition.h"
#include "Animation/DemoEnemyAppearance.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Combat/Feedback/DemoFleshMist.h"
#include "Combat/Feedback/DemoMetalImpactSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodySetup.h" // UE5.4将USkeletalBodySetup声明放在BodySetup.h，不能用新版本路径。
#include "Player/Coop/DemoCoopProtocol.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Debug/DemoStaticLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoHitFxSpringContract,"FPSDemo.HitFx.Spring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters由Automation提供；对同一生产解析弹簧施加20次冲量/650RPM时序，测真实输出而非求解目标，不改CDO。 */
bool FDemoHitFxSpringContract::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    const UDemoHitFxSettings& Settings=*GetDefault<UDemoHitFxSettings>(); // 当前进程只读配置；本任务默认9Hz/0.55阻尼下验0.4秒恢复。
    FDemoHitReactSpring Burst; // 独占纯值状态，同帧20次重武器/头部测试总模长封顶。
    for(int32 Index=0;Index<20;++Index)
        TestTrue(TEXT("Finite confirmed shot accepts a kick"),Burst.Kick(FVector(-1,0,0),true,true,Settings));
    double PeakDegrees=0.,PeakOffset=0.; // 从每次真实Step后向量测量，不能读取目标峰值替代。
    for(int32 Frame=0;Frame<400;++Frame)
    {
        Burst.Step(.001f,Settings);
        PeakDegrees=FMath::Max(PeakDegrees,Burst.AngleDegrees.Size()); PeakOffset=FMath::Max(PeakOffset,Burst.OffsetCm.Size());
        TestTrue(TEXT("Twenty stacked kicks never exceed total angle limit"),Burst.AngleDegrees.Size()<=Settings.ReactMaxDeg+1.e-6);
        TestTrue(TEXT("Twenty stacked kicks never exceed total offset limit"),Burst.OffsetCm.Size()<=Settings.ReactMaxOffset+1.e-6);
    }
    TestTrue(TEXT("Kick creates a measurable visible response"),PeakDegrees>1. && PeakOffset>.1);
    TestTrue(TEXT("No input for 0.4s restores the baseline"),Burst.IsSettled() && Burst.AngleDegrees.IsNearlyZero(.05) && Burst.OffsetCm.IsNearlyZero(.01));
    TestTrue(TEXT("Settled state is exact zero rather than a persistent drift"),Burst.AngleDegrees.IsZero() && Burst.AngularVelocity.IsZero() && Burst.OffsetCm.IsZero() && Burst.OffsetVelocity.IsZero());
    TestFalse(TEXT("Unknown zero shot direction cannot react"),Burst.Kick(FVector::ZeroVector,false,false,Settings));
    TestTrue(TEXT("Rejected zero direction leaves exact baseline"),Burst.IsSettled());

    for(int32 Rate:{15,30,60})
    {
        FDemoHitReactSpring Automatic; // 每种帧率独立状态，无跨测试保留。
        const double FrameSeconds=1./Rate,ShotInterval=60./650.; // 实际650RPM单枪间隔秒；帧内精确分割至每个确认时刻。
        double Time=0.,NextShot=0.; int32 Shots=0; // 本测试时序，不使用World或真实伤害。
        while(Shots<20)
        {
            const double FrameEnd=Time+FrameSeconds; // 本显示帧终点。
            while(Shots<20 && NextShot<=FrameEnd+1.e-10)
            {
                Automatic.Step(static_cast<float>(FMath::Max(0.,NextShot-Time)),Settings); Time=NextShot;
                Automatic.Kick(FVector(-1,.25,0).GetSafeNormal(),Shots%3==0,Shots%4==0,Settings); ++Shots; NextShot=Shots*ShotInterval;
                TestTrue(TEXT("650RPM angle finite and bounded at shot boundaries"),!Automatic.AngleDegrees.ContainsNaN() && Automatic.AngleDegrees.Size()<=Settings.ReactMaxDeg+1.e-6);
                TestTrue(TEXT("650RPM displacement finite and bounded at shot boundaries"),!Automatic.OffsetCm.ContainsNaN() && Automatic.OffsetCm.Size()<=Settings.ReactMaxOffset+1.e-6);
            }
            Automatic.Step(static_cast<float>(FMath::Max(0.,FrameEnd-Time)),Settings); Time=FrameEnd;
            TestTrue(TEXT("650RPM 15/30/60Hz visible frame angle remains bounded"),Automatic.AngleDegrees.Size()<=Settings.ReactMaxDeg+1.e-6);
            TestTrue(TEXT("650RPM 15/30/60Hz visible frame offset remains bounded"),Automatic.OffsetCm.Size()<=Settings.ReactMaxOffset+1.e-6);
        }
        Automatic.Step(.4f,Settings);
        TestTrue(TEXT("650RPM stops and settles within 0.4s at each frame rate"),Automatic.IsSettled() && Automatic.AngleDegrees.IsZero() && Automatic.OffsetCm.IsZero());
        UE_LOG(LogFPSDemo,Log,TEXT("HITFX_SPRING_CONTRACT rate=%d shots=%d settled=%d"),Rate,Shots,Automatic.IsSettled());
    }
    FDemoHitReactSpring LongFrame; LongFrame.Kick(FVector::ForwardVector,false,true,Settings); // 单次0.25秒长帧不能导致欧拉爆炸。
    LongFrame.Step(.25f,Settings);
    TestTrue(TEXT("Quarter second long frame stays finite and bounded"),!LongFrame.AngleDegrees.ContainsNaN() && !LongFrame.AngularVelocity.ContainsNaN()
        && LongFrame.AngleDegrees.Size()<=Settings.ReactMaxDeg+1.e-6 && LongFrame.OffsetCm.Size()<=Settings.ReactMaxOffset+1.e-6);
    const FVector Before=LongFrame.AngleDegrees; // 无效时间应保持上次实际值。
    LongFrame.Step(-1.f,Settings);
    TestTrue(TEXT("Negative time is rejected without changing state"),LongFrame.AngleDegrees.Equals(Before,0.));
    FDemoHitReactSpring Full,Rapid; // 门限内40%冲量使用同一生产Kick强度入口；时间门禁本身由组件World时钟负责。
    Full.Kick(FVector::ForwardVector,false,false,Settings); Rapid.Kick(FVector::ForwardVector,false,false,Settings,Settings.ReactRapidScale);
    Full.Step(.01f,Settings); Rapid.Step(.01f,Settings);
    TestTrue(TEXT("Rapid interval input scales the additional impulse to configured 40 percent"),Rapid.AngleDegrees.Equals(Full.AngleDegrees*Settings.ReactRapidScale,1.e-6)
        && Rapid.OffsetCm.Equals(Full.OffsetCm*Settings.ReactRapidScale,1.e-6));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoHitFxMistLodContract,"FPSDemo.HitFx.MistLOD",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters不作路径；使用实际生产LOD函数检查档位、门限两侧及非法输入，不创建表现对象。 */
bool FDemoHitFxMistLodContract::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    const UDemoHitFxSettings& Settings=*GetDefault<UDemoHitFxSettings>(); // 本任务ini默认值；审查另行核配置与CDO一致。
    TestEqual(TEXT("Body near puffs"),DemoHitFx::ComputeMistPuffCount(1,false,0.f,Settings),6);
    TestEqual(TEXT("Head near puffs"),DemoHitFx::ComputeMistPuffCount(2,false,0.f,Settings),9);
    TestEqual(TEXT("Heavy body near puffs"),DemoHitFx::ComputeMistPuffCount(1,true,0.f,Settings),7);
    TestEqual(TEXT("Heavy head is capped rather than allocating a tenth puff"),DemoHitFx::ComputeMistPuffCount(2,true,0.f,Settings),9);
    const float Near=Settings.MaxDistance*Settings.MistLodNearFraction,Far=Settings.MaxDistance*Settings.MistLodFarFraction; // 从配置取门限，测试两侧严格大于的约定。
    TestEqual(TEXT("Exactly near boundary keeps near tier"),DemoHitFx::ComputeMistPuffCount(1,false,FMath::Square(Near),Settings),6);
    TestEqual(TEXT("Just beyond near boundary selects four"),DemoHitFx::ComputeMistPuffCount(1,false,FMath::Square(Near+.1f),Settings),4);
    TestEqual(TEXT("Exactly far boundary keeps middle tier"),DemoHitFx::ComputeMistPuffCount(2,true,FMath::Square(Far),Settings),4);
    TestEqual(TEXT("Beyond far boundary selects two even for heavy head"),DemoHitFx::ComputeMistPuffCount(2,true,FMath::Square(Far+.1f),Settings),2);
    TestEqual(TEXT("Exactly maximum distance remains visible"),DemoHitFx::ComputeMistPuffCount(1,false,FMath::Square(Settings.MaxDistance),Settings),2);
    TestEqual(TEXT("Beyond maximum distance is culled"),DemoHitFx::ComputeMistPuffCount(1,false,FMath::Square(Settings.MaxDistance+1.f),Settings),0);
    TestEqual(TEXT("No flesh tier creates no puffs"),DemoHitFx::ComputeMistPuffCount(0,false,0.f,Settings),0);
    TestEqual(TEXT("Unknown flesh tier is rejected"),DemoHitFx::ComputeMistPuffCount(3,true,0.f,Settings),0);
    TestEqual(TEXT("Negative squared distance is rejected"),DemoHitFx::ComputeMistPuffCount(1,false,-1.f,Settings),0);
    TStrongObjectPtr<UDemoHitFxSettings> SmallBudget(NewObject<UDemoHitFxSettings>(GetTransientPackage(),NAME_None,RF_Transient)); // 独占配置副本，不能写共享CDO/ini。
    SmallBudget->MistPuffsMax=3;
    TestEqual(TEXT("Smaller installed per-slot budget caps heavy near tier"),DemoHitFx::ComputeMistPuffCount(2,true,0.f,*SmallBudget),3);
    TestEqual(TEXT("Smaller installed budget also caps middle LOD"),DemoHitFx::ComputeMistPuffCount(1,false,FMath::Square(Near+1.f),*SmallBudget),3);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoHitFxMistPoolContract,"FPSDemo.HitFx.MistPool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters不作资源；调用生产固定槽Spawn/Step验证满池丢弃、回收重用、淡出及10000次压力下容量/地址不变。 */
bool FDemoHitFxMistPoolContract::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    const UDemoHitFxSettings& Settings=*GetDefault<UDemoHitFxSettings>(); // CDO只读，所有随机运动仍使用实际生产路径。
    FDemoFleshMistState State; State.Initialize(16,9); // 本任务默认独立预算，不能借用金属保留槽。
    const SIZE_T BeforeBytes=State.Slots.GetAllocatedSize()+State.Transforms.GetAllocatedSize()+State.Fades.GetAllocatedSize(); // 完整三个动态缓冲已分配量。
    const void* const SlotAddress=State.Slots.GetData(); const void* const TransformAddress=State.Transforms.GetData(); const void* const FadeAddress=State.Fades.GetData(); // 同步地址仅作是否发生重分配的证据，绝不解引用旧指针。
    const FVector Contact(12345,-6789,1200),Normal=FVector::UpVector; // 非原点世界接触点验证解析位置不回到原点。
    for(int32 Index=0;Index<16;++Index)
        TestEqual(TEXT("Each free slot admits a body cluster"),State.Spawn(Contact,Normal,1,false,0.f,Settings),6);
    TestEqual(TEXT("Sixteen clusters fill the independent pool"),State.Active,16);
    for(int32 Index=0;Index<10000;++Index) State.Spawn(Contact,Normal,2,true,0.f,Settings); // 池满事件不能抢占旧簇、创建对象或扩容。
    TestEqual(TEXT("Every full-pool event is counted as dropped"),State.Dropped,static_cast<uint64>(10000));
    TestEqual(TEXT("Full-pool drops preserve active cluster count"),State.Active,16);
    State.Step(.01f,Settings);
    TestTrue(TEXT("First active puff uses the actual frozen world contact"),FVector::Dist(State.Transforms[0].GetLocation(),Contact)<20.);
    TestTrue(TEXT("Visible initial fade stays in unit interval"),State.Fades[0]>0.f && State.Fades[0]<=1.f);
    const float FirstFade=State.Fades[0]; State.Step(.01f,Settings);
    TestTrue(TEXT("Fade decreases with World age"),State.Fades[0]<FirstFade);
    State.Step(1.f,Settings);
    TestEqual(TEXT("Long frame releases all expired clusters"),State.Active,0);
    for(int32 Index=0;Index<State.Transforms.Num();++Index)
    {
        TestTrue(TEXT("Expired instances retain stable indexes at zero scale"),State.Transforms[Index].GetScale3D().IsZero());
        TestEqual(TEXT("Expired per-instance fade is zero"),State.Fades[Index],0.f);
    }
    for(int32 Index=0;Index<1000;++Index) // 重复释放/复用也不能增长数组；每回用原第一个空槽。
    {
        State.Spawn(Contact,Normal,2,true,0.f,Settings); State.Step(1.f,Settings);
    }
    TestEqual(TEXT("Fixed slot count remains sixteen"),State.Slots.Num(),16);
    TestEqual(TEXT("Transform count remains sixteen times nine"),State.Transforms.Num(),144);
    TestEqual(TEXT("Custom data count remains exactly the instance count"),State.Fades.Num(),144);
    TestTrue(TEXT("Repeated admission and expiry do not reallocate any buffer"),State.Slots.GetData()==SlotAddress && State.Transforms.GetData()==TransformAddress && State.Fades.GetData()==FadeAddress);
    TestTrue(TEXT("Allocated memory remains constant after full-pool pressure and reuse"),BeforeBytes==State.Slots.GetAllocatedSize()+State.Transforms.GetAllocatedSize()+State.Fades.GetAllocatedSize());
    TestEqual(TEXT("Successful reuse does not change full-pool drop count"),State.Dropped,static_cast<uint64>(10000));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoHitFxEventProtocolContract,"FPSDemo.HitFx.EventProtocol",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters无副作用；检查生产表面函数与真实UHT字段/RPC参数，当前v33包含本任务v32和并行弹体布局，不能降版。 */
bool FDemoHitFxEventProtocolContract::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    TestEqual(TEXT("Confirmed flesh body selects tier one"),DemoHitFx::ComputeFleshTier(EDemoImpactSurface::Flesh,true,false),static_cast<uint8>(1));
    TestEqual(TEXT("Confirmed flesh head selects tier two"),DemoHitFx::ComputeFleshTier(EDemoImpactSurface::Flesh,true,true),static_cast<uint8>(2));
    TestEqual(TEXT("Boss armor cannot create flesh mist even at head"),DemoHitFx::ComputeFleshTier(EDemoImpactSurface::Armor,true,true),static_cast<uint8>(0));
    TestEqual(TEXT("Objective device cannot create flesh mist"),DemoHitFx::ComputeFleshTier(EDemoImpactSurface::Device,true,true),static_cast<uint8>(0));
    TestEqual(TEXT("Unselected visual route creates no flesh mist"),DemoHitFx::ComputeFleshTier(EDemoImpactSurface::Flesh,false,true),static_cast<uint8>(0));
    TestEqual(TEXT("Current protocol includes v32 hit FX and parallel v33 projectile changes"),FString(DemoCoopProtocol::Version),FString(TEXT("33")));
    const UScriptStruct* EventType=FDemoMetalImpactEvent::StaticStruct(); // 真实UHT布局，不能以源码字符串替代网络字段存在。
    TestNotNull(TEXT("FleshTier is a reflected byte in the multicast event"),FindFProperty<FByteProperty>(EventType,TEXT("FleshTier")));
    TestNotNull(TEXT("bHeavyWeapon is a reflected bool in the multicast event"),FindFProperty<FBoolProperty>(EventType,TEXT("bHeavyWeapon")));
    const UFunction* Rpc=ADemoEnemy::StaticClass()->FindFunctionByName(TEXT("MulticastHitFeedback")); // 只读反射，不调用网络或修改字段。
    if(TestNotNull(TEXT("Original multicast remains present"),Rpc))
    {
        TestTrue(TEXT("Hit visual feedback retains unreliable multicast semantics"),Rpc->HasAnyFunctionFlags(FUNC_NetMulticast) && !Rpc->HasAnyFunctionFlags(FUNC_NetReliable));
        const FStructProperty* Direction=FindFProperty<FStructProperty>(Rpc,TEXT("ShotDirection")); // 本次新增的量化单位方向。
        if(TestNotNull(TEXT("ShotDirection is present in actual RPC parameters"),Direction))
            TestEqual(TEXT("Direction uses quantized normal network struct"),Direction->Struct->GetFName(),FName(TEXT("Vector_NetQuantizeNormal")));
        TestNotNull(TEXT("Heavy weapon grade is present in actual RPC parameters"),FindFProperty<FBoolProperty>(Rpc,TEXT("bHeavyWeapon")));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoHitFxCorpseCollision,"FPSDemo.HitFx.CorpseCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters不启动游戏；friend只设置本地fatal夹具事实并调用真实入口/下一帧目标回调，使用生产查询专用网格验证安全fallback，不冒充ragdoll/真实GAS击杀验收。 */
bool FDemoHitFxCorpseCollision::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    const UWorld::InitializationValues Init=UWorld::InitializationValues().InitializeScenes(false).AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).EnableTraceCollision(false).CreateFXSystem(false).SetTransactional(false); // 仅注册/响应值与运动学合同，无模拟场景、GI、游戏模式或存档。
    UWorld* World=UWorld::CreateWorld(EWorldType::Editor,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("HitFxCorpseContracts")),GetTransientPackage(),true,ERHIFeatureLevel::Num,&Init); // CreateWorld Root保活至同步DestroyWorld。
    if(!TestNotNull(TEXT("Isolated native Editor World exists"),World)) return false;
    FActorSpawnParameters Spawn; Spawn.OverrideLevel=World->PersistentLevel; Spawn.ObjectFlags=RF_Transient; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 只生成独占夹具Actor，不覆盖当前Editor地图。
    ADemoEnemy* Enemy=World->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),FTransform(FRotator(0,23,0),FVector(1200,-300,800)),Spawn); // 非原点/非零朝向，防仅Identity下通过。
    if(!TestNotNull(TEXT("Real native Enemy fixture exists"),Enemy)) { World->DestroyWorld(false); return false; }
    const UDemoEnemyAppearance* Appearance=GetDefault<UDemoEnemyDefinition>()->MinionBody.Appearance; // 实际生产默认小怪资源，只读，不改PhysicsAsset。
    if(!TestNotNull(TEXT("Production minion appearance exists"),Appearance) || !TestNotNull(TEXT("Production minion mesh exists"),Appearance?Appearance->Mesh.Get():nullptr))
    { World->DestroyWorld(false); return false; }
    Enemy->AnimatedBody->SetSkeletalMesh(Appearance->Mesh);
    const UPhysicsAsset* Physics=Enemy->AnimatedBody->GetPhysicsAsset(); // 实际查询专用资产决定安全fallback，而非测试伪造可模拟资产。
    if(TestNotNull(TEXT("Production minion PhysicsAsset exists"),Physics))
    {
        TestTrue(TEXT("Production asset has hit bodies"),Physics->SkeletalBodySetups.Num()>0);
        bool bKinematic=false; // 从保存的实际刚体配置验证fallback依据，不只看最终mode。
        for(const USkeletalBodySetup* Body:Physics->SkeletalBodySetups) bKinematic|=Body && Body->PhysicsType==PhysType_Kinematic;
        TestTrue(TEXT("Production hit-zone asset is query-oriented kinematic"),bKinematic);
    }
    const FTransform ActorBefore=Enemy->GetActorTransform(),MeshBefore=Enemy->AnimatedBody->GetRelativeTransform(); // 授权前的真实对象值。
    Enemy->ApplyLocalDeathImpulse(FVector::ForwardVector,false,false);
    TestTrue(TEXT("Public visual entry rejects calls without fatal receipt"),Enemy->LocalDeathMode==EDemoLocalDeathMode::None);
    Enemy->bDead=true; Enemy->bLocalFatalReceipt=true; Enemy->SetActorEnableCollision(false); // 仅此隔离测试friend事实；不触发伤害/奖励或生产调试接口。
    Enemy->ApplyLocalDeathImpulse(FVector::ForwardVector,false,false);
    TestTrue(TEXT("Authorized fatal receipt schedules the original next-frame entry"),Enemy->LocalDeathMode==EDemoLocalDeathMode::Pending);
    TestFalse(TEXT("Fatal permission is consumed once"),Enemy->bLocalFatalReceipt);
    Enemy->StartLocalDeathImpulse(); // 调用生产UObject下一帧回调目标，内部清Timer；此同步合同不推进AI/World。
    TestTrue(TEXT("Production query-only PhysicsAsset uses kinematic fallback"),Enemy->LocalDeathMode==EDemoLocalDeathMode::Fallback);
    for(ECollisionChannel Channel:{DemoEnemyHitZones::TraceChannel,DemoEnemyHitZones::ProjectileChannel,ECC_Pawn,ECC_Visibility,ECC_Camera})
        TestTrue(TEXT("Corpse ignores weapon, pawn, visibility and camera channels"),Enemy->AnimatedBody->GetCollisionResponseToChannel(Channel)==ECR_Ignore);
    TestTrue(TEXT("Fallback body has no collision"),Enemy->AnimatedBody->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Fallback never starts skeletal simulation"),Enemy->AnimatedBody->IsSimulatingPhysics());
    TestFalse(TEXT("Corpse mesh cannot affect navigation"),Enemy->AnimatedBody->CanEverAffectNavigation());
    TInlineComponentArray<UPrimitiveComponent*> Primitives(Enemy); // 真实组件集合，检查恢复Actor开关也不会复活球根/预警碰撞。
    for(const UPrimitiveComponent* Primitive:Primitives)
        if(Primitive!=Enemy->AnimatedBody) TestTrue(TEXT("Other enemy primitives remain non-colliding"),Primitive->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    Enemy->AdvanceLocalDeathImpulse(.25f);
    TestTrue(TEXT("Default fallback finishes and holds after 0.25 seconds"),Enemy->LocalDeathMode==EDemoLocalDeathMode::Holding);
    TestTrue(TEXT("Cosmetic fall never moves the authoritative Actor root"),Enemy->GetActorTransform().Equals(ActorBefore,1.e-7));
    TestTrue(TEXT("Fallback visibly moves the actual mesh"),FVector::Dist(Enemy->AnimatedBody->GetRelativeLocation(),MeshBefore.GetLocation())>20.);
    Enemy->StopLocalDeathImpulse();
    TestTrue(TEXT("Local cleanup restores original mesh transform"),Enemy->AnimatedBody->GetRelativeTransform().Equals(MeshBefore,1.e-6));
    TestTrue(TEXT("Cleanup keeps authoritative death fact"),Enemy->bDead);
    Enemy->bBossEnemy=true; Enemy->bLocalFatalReceipt=true;
    Enemy->BoneImpactSurfaces.Add(TEXT("head"),EDemoImpactSurface::Flesh); // 即使外部错误配置Boss肉体覆盖，当前明确规则仍须硬表面。
    TestTrue(TEXT("Boss identity defeats a flesh bone override"),Enemy->GetImpactSurface(TEXT("head"))==EDemoImpactSurface::Armor);
    Enemy->ApplyLocalDeathImpulse(FVector::ForwardVector,true,true);
    TestTrue(TEXT("Boss fatal receipt never starts local death impulse"),Enemy->LocalDeathMode==EDemoLocalDeathMode::None);
    TestTrue(TEXT("Boss rejection leaves mesh unchanged"),Enemy->AnimatedBody->GetRelativeTransform().Equals(MeshBefore,1.e-6));
    Enemy->bBossEnemy=false; Enemy->bLocalFatalReceipt=true; Enemy->ApplyLocalDeathImpulse(FVector::ZeroVector,false,false);
    TestTrue(TEXT("Unknown fatal direction cannot manufacture pushback"),Enemy->LocalDeathMode==EDemoLocalDeathMode::None);
    TestFalse(TEXT("Contract never entered gameplay or created save services"),World->HasBegunPlay());
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
