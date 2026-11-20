// 职责：BULLET-VFX-20261119冻结事实/丢包去重/分段几何/样式配置合同。使用：Editor Automation FPSDemo.BulletVFX；不替代PIE、听感、色弱、多人与Cook验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Weapons/Projectiles/DemoProjectilePresentation.h"
#include "Weapons/Projectiles/DemoProjectileConfig.h"
#include "Weapons/Projectiles/DemoProjectileTracer.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/DemoWeaponConfig.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"
#include <limits>

namespace DemoBulletVfxContracts
{
    /** 本同步几何合同独占的瞬态EditorPreview World；无Scene/Physics/FX/GI/BeginPlay，不触及用户地图。 */
    struct FTracerFixture
    {
        UWorld* World=nullptr; // CreateWorld以Root保活，析构DestroyWorld释放全部Actor/组件。
        bool bOwnsContext=false; // 只为本World登记上下文，清理后精确移除，不改GWorld。
        /** 仅创建可注册静态Mesh组件的对象World，不初始化渲染、粒子、导航或任何游戏流程。 */
        FTracerFixture()
        {
            UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.TracerFixture.Create"));
            const UWorld::InitializationValues Init=UWorld::InitializationValues().InitializeScenes(false)
                .CreateFXSystem(false).CreatePhysicsScene(false).ShouldSimulatePhysics(false)
                .AllowAudioPlayback(false).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false)
                .EnableTraceCollision(false).SetTransactional(false); // 只有本地对象/组件变换，不模拟真实弹或实际视觉。
            const FName Name=MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("BulletTracerGeometryContracts")); // 独占Transient名，不读写正式包。
            World=UWorld::CreateWorld(EWorldType::EditorPreview,false,Name,GetTransientPackage(),true,ERHIFeatureLevel::Num,&Init);
            if (World)
            {
                World->SetFlags(RF_Transient);
                if (GEngine)
                {
                    FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::EditorPreview); // 保留至World完整清理，避免销毁时缺上下文。
                    Context.SetCurrentWorld(World); bOwnsContext=true;
                }
            }
        }
        /** 所有提前返回均清理本World；不Tick/BeginPlay，不留下异步任务或粒子。 */
        ~FTracerFixture()
        {
            UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.TracerFixture.Destroy"));
            if (World)
            {
                World->DestroyWorld(false);
                if (bOwnsContext && GEngine) GEngine->DestroyWorldContext(World);
            }
        }
    };
    /** 纯值已提交事实夹具；无Shooter仍是合法网络结构，World/Phase/时效门由Weapon现有接收端另行负责。 */
    FDemoProjectilePresentationSample MakeSample()
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.MakeSample"));
        FDemoProjectilePresentationSample Sample; // 本次测试独立值，不保活Actor或影响真实弹药。
        Sample.Launch.ShotId=FGuid::NewGuid(); Sample.Launch.Pellet=0;
        Sample.Launch.Phase=4; Sample.Launch.Level=2; Sample.Launch.AmmoType=1;
        Sample.Sequence=3; Sample.Start=FVector(100.,0.,0.); Sample.End=FVector(100.,30.,0.);
        Sample.StartDistance=100.f; Sample.EndDistance=130.f;
        Sample.RicochetPoint=FVector(100.,0.,0.); Sample.RicochetDirection=FVector::YAxisVector;
        return Sample;
    }
}

/** Editor Automation入口：破坏冻结字段逐项检查只读网络合同；不执行RPC或授权命中。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoBulletVfxFacts,"FPSDemo.BulletVFX.NetworkFacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoBulletVfxFacts::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.NetworkFacts"));
    FDemoProjectilePresentationSample Sample=DemoBulletVfxContracts::MakeSample(); // 从同一个合法样本逐项破坏网络合同。
    for (uint8 Ammo=0;Ammo<4;++Ammo)
    {
        Sample.Launch.AmmoType=Ammo;
        TestTrue(TEXT("All four frozen ammo indices are valid without a Shooter reference"),DemoProjectilePresentation::IsValid(Sample.Launch));
        TestTrue(TEXT("All four indices remain valid inside sample"),DemoProjectilePresentation::IsValid(Sample));
    }
    Sample.Launch.AmmoType=4;
    TestFalse(TEXT("Unsupported ammo is rejected by launch validation"),DemoProjectilePresentation::IsValid(Sample.Launch));
    TestFalse(TEXT("Unsupported ammo is rejected by sample validation"),DemoProjectilePresentation::IsValid(Sample));
    Sample.Launch.AmmoType=1; Sample.bRicochetSegment=true;
    TestTrue(TEXT("Successful ricochet accepts true surface point and unit outgoing direction"),DemoProjectilePresentation::IsValid(Sample));
    TestFalse(TEXT("Lost original boundary packet is represented by an ordinary later sample"),Sample.bBoundary);
    TestTrue(TEXT("Persistent ricochet fact closes FP bridge even if boundary packet was lost"),DemoProjectilePresentation::IsWorldBoundary(Sample));
    Sample.bRicochetSegment=false;
    TestFalse(TEXT("Ordinary pre-contact travel retains its existing bridge policy"),DemoProjectilePresentation::IsWorldBoundary(Sample));
    Sample.bBoundary=true;
    TestTrue(TEXT("Explicit physical contact remains a world boundary"),DemoProjectilePresentation::IsWorldBoundary(Sample));
    Sample.bBoundary=false; Sample.bRicochetSegment=true;
    Sample.RicochetPoint.X=std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Infinite ricochet point rejected"),DemoProjectilePresentation::IsValid(Sample));
    Sample.RicochetPoint=FVector(100.,0.,0.); Sample.RicochetDirection=FVector::ZeroVector;
    TestFalse(TEXT("Zero ricochet direction rejected"),DemoProjectilePresentation::IsValid(Sample));
    Sample.RicochetDirection=FVector::YAxisVector; Sample.bEnvironmentImpact=true;
    Sample.EnvironmentNormal=FVector::ZeroVector;
    TestFalse(TEXT("Environment contact requires a unit normal"),DemoProjectilePresentation::IsValid(Sample));
    Sample.EnvironmentNormal=FVector::UpVector; Sample.EnvironmentPoint.Z=std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN environment contact rejected"),DemoProjectilePresentation::IsValid(Sample));
    Sample.EnvironmentPoint=FVector::ZeroVector; Sample.bPiercingExit=true;
    Sample.PiercingExitDirection=FVector::ZeroVector;
    TestFalse(TEXT("Piercing exit requires unit direction"),DemoProjectilePresentation::IsValid(Sample));
    Sample.PiercingExitDirection=FVector::ForwardVector;
    TestTrue(TEXT("Independent valid contact facts may share the terminal sample"),DemoProjectilePresentation::IsValid(Sample));
    Sample.PiercingExitPoint.Y=std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Nonfinite piercing exit rejected"),DemoProjectilePresentation::IsValid(Sample));
    return !HasAnyErrors();
}

/** Editor Automation入口：以同生产键和状态模拟丢首包/重复/异弹丸，限制为消费去重合同。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoBulletVfxReceipts,"FPSDemo.BulletVFX.LossAndDeduplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoBulletVfxReceipts::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.LossAndDeduplication"));
    TMap<FDemoProjectilePresentationKey,FDemoProjectileCosmeticState> States; // 同生产ShotId+Pellet键，不把同枪多弹丸合并。
    FDemoProjectilePresentationSample Later=DemoBulletVfxContracts::MakeSample(); // sequence=3代表首个出射包已丢失。
    const FDemoProjectilePresentationKey OriginalKey={Later.Launch.ShotId,Later.Launch.Pellet}; // 后续加键可能重分配TMap，绝不跨插入保存值引用。
    FDemoProjectileCosmeticState& State=States.FindOrAdd(OriginalKey);
    TestFalse(TEXT("Incoming packet does not consume future ricochet opportunity"),State.ConsumeRicochet(Later));
    Later.bRicochetSegment=true;
    TestTrue(TEXT("Later outgoing sample compensates missing first packet"),State.ConsumeRicochet(Later));
    TestFalse(TEXT("Duplicate sample cannot replay spark"),State.ConsumeRicochet(Later));
    ++Later.Sequence; Later.bTerminal=true;
    TestFalse(TEXT("Terminal repetition cannot replay spark"),State.ConsumeRicochet(Later));
    TestFalse(TEXT("Absent environment fact leaves opportunity open"),State.ConsumeEnvironmentImpact(Later));
    Later.bEnvironmentImpact=true;
    TestTrue(TEXT("First available environment contact is displayed"),State.ConsumeEnvironmentImpact(Later));
    TestFalse(TEXT("Environment contact terminal duplicate suppressed"),State.ConsumeEnvironmentImpact(Later));
    TestFalse(TEXT("Absent piercing fact leaves opportunity open"),State.ConsumePiercingExit(Later));
    Later.bPiercingExit=true;
    TestTrue(TEXT("Actual exit can arrive later"),State.ConsumePiercingExit(Later));
    TestFalse(TEXT("Exit terminal duplicate suppressed"),State.ConsumePiercingExit(Later));
    Later.Launch.Pellet=1;
    TestTrue(TEXT("Same shot other pellet has independent spark"),States.FindOrAdd({Later.Launch.ShotId,Later.Launch.Pellet}).ConsumeRicochet(Later));
    Later.Launch.ShotId=FGuid::NewGuid(); Later.Launch.Pellet=0;
    TestTrue(TEXT("Different shot same pellet has independent spark"),States.FindOrAdd({Later.Launch.ShotId,Later.Launch.Pellet}).ConsumeRicochet(Later));
    return !HasAnyErrors();
}

/** Editor Automation入口：独立从Cube世界端点核分段Origin/1000cm硬界，不替代渲染或运动测试。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoBulletVfxSegments,"FPSDemo.BulletVFX.RicochetSegments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoBulletVfxSegments::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.RicochetSegments"));
    const FVector Muzzle(10000.,-400.,300.),Contact(10100.,-400.,300.); // 非原点、90度转角；累计路径已含100cm入射段。
    const FVector OutDirection=FVector::YAxisVector;
    TestEqual(TEXT("Incoming segment stops exactly at boundary"),DemoProjectilePresentation::SegmentLength(Muzzle,Contact,100.f,300.f),100.f);
    TestEqual(TEXT("New segment is initially zero even after previous distance"),DemoProjectilePresentation::SegmentLength(Contact,Contact,100.f,1000.f),0.f);
    for (float Travel : {1.f,15.f,80.f,799.f,1500.f})
    {
        const FVector End=Contact+OutDirection*Travel; // 只代表已真实飞过的末端，未授权任何物理移动。
        const float Length=DemoProjectilePresentation::SegmentLength(Contact,End,100.f+Travel,1000.f);
        const FVector Rear=End-OutDirection*Length;
        TestTrue(TEXT("Outgoing rear cannot cross its contact origin"),FVector::DotProduct(Rear-Contact,OutDirection)>=-1.e-6);
        TestTrue(TEXT("No outgoing segment exceeds 1000 cm"),Length<=1000.f);
        const FTransform MeshTransform(OutDirection.Rotation(),End-OutDirection*(Length*.5f),DemoProjectilePresentation::TracerMeshScale(Length,6.f,1.f));
        TestTrue(TEXT("Outgoing cube endpoint remains actual flown endpoint"),MeshTransform.TransformPosition(FVector(50.,0.,0.)).Equals(End,1.e-4));
        TestTrue(TEXT("Outgoing rear endpoint stays on outgoing ray"),FMath::Abs(MeshTransform.TransformPosition(FVector(-50.,0.,0.)).X-Contact.X)<1.e-4);
    }
    TestEqual(TEXT("Actual flown distance is an independent upper bound"),DemoProjectilePresentation::SegmentLength(Contact,Contact+OutDirection*500.f,5.f,1000.f),5.f);
    TestEqual(TEXT("Invalid path distance hides visual rather than leaking geometry"),DemoProjectilePresentation::SegmentLength(Contact,Contact+OutDirection,-1.f,1000.f),0.f);
    DemoBulletVfxContracts::FTracerFixture Fixture; // 下面读实际生产Actor/组件变换，能够捕获UpdateTravel误用上一Delta方向而不是当前段Origin。
    if (!TestNotNull(TEXT("Independent geometry World exists"),Fixture.World)) return false;
    FActorSpawnParameters Spawn; Spawn.OverrideLevel=Fixture.World->PersistentLevel; Spawn.ObjectFlags=RF_Transient; // World独占Actor，无碰撞生成策略，不接触正式地图。
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADemoProjectileTracer* Tracer=Fixture.World->SpawnActor<ADemoProjectileTracer>(ADemoProjectileTracer::StaticClass(),FTransform(FRotator::ZeroRotator,Contact),Spawn);
    if (!TestNotNull(TEXT("Production tracer Actor created"),Tracer)) return false;
    FDemoProjectileConfig Config; // 默认无接触资源；只激活真实Tracer几何，不生成任何FX或伤害Actor。
    if (!TestTrue(TEXT("Production ricochet initialization accepts actual contact Origin"),Tracer->Initialize(Config,Contact,DemoAmmoVisuals::Default(),true))) return false;
    UStaticMeshComponent* Mesh=Tracer->FindComponentByClass<UStaticMeshComponent>(); // 实际组件读回，不能用目标/计算helper替代其Transform。
    if (!TestNotNull(TEXT("Production Cube component exists"),Mesh)) return false;
    TestEqual(TEXT("Production visual mesh has no collision"),Mesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Geometry fixture never began gameplay"),Fixture.World->HasBegunPlay());
    const FVector ActualEnds[]={Contact+FVector(-1.,1.,0.),Contact+FVector(-1.,3.,0.)}; // 模拟真实表面与球心微差，第二段Delta为+Y而Origin方向不同。
    const float Travelled[]={FMath::Sqrt(2.f),4.f}; // 累计已飞距离；第二条4cm足以覆盖当前Origin到End的sqrt10cm。
    for (int32 Index=0;Index<2;++Index)
    {
        Tracer->UpdateTravel(ActualEnds[Index],Travelled[Index]);
        const FTransform Actual=Mesh->GetComponentTransform(); // 生产函数实际写入后的组件世界矩阵，单位Cube半边长50cm。
        TestTrue(TEXT("Actual production outgoing rear is contact, not a previous-delta ray"),Actual.TransformPosition(FVector(-50.,0.,0.)).Equals(Contact,1.e-4));
        TestTrue(TEXT("Actual production outgoing front is actual flown End"),Actual.TransformPosition(FVector(50.,0.,0.)).Equals(ActualEnds[Index],1.e-4));
    }
    Tracer->Finish(); // 进入独立淡出后，清场仍必须立即撤销，不能依赖短视觉寿命。
    ADemoProjectileTracer::CancelAll(Fixture.World);
    TestTrue(TEXT("CancelAll immediately destroys a finishing production tracer"),!IsValid(Tracer) || Tracer->IsActorBeingDestroyed());
    return !HasAnyErrors();
}

/** Editor Automation入口：消费生产参数组合/缓存，核色相、第二形状通道和只读输入；不把值检查当World回归。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoBulletVfxStyles,"FPSDemo.BulletVFX.StyleAndGameplayIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoBulletVfxStyles::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.StyleAndGameplayIsolation"));
    FDemoProjectileConfig Config; Config.TracerWidth=4.f; Config.TracerLength=200.f; Config.TracerFadeSeconds=.1f; // 手枪候选视觉基线，物理数据保留默认。
    const float Speed=Config.InitialSpeed,Radius=Config.CollisionRadius,Life=Config.MaxLifeSeconds; // 调用生产组合函数前记录物理输入。
    FDemoWeaponConfig Weapon; const float Damage=Weapon.BaseDamage,Range=Weapon.Range; // 独立伤害定义，无可写指针交给视觉函数。
    for (uint8 Index=0;Index<4;++Index)
    {
        const FDemoAmmoVisualStyle Style=DemoAmmoVisuals::Default(Index); // 原生默认是目录首次创建和防御回退的正式来源。
        TestTrue(TEXT("Every authored default visual style is finite and valid"),DemoAmmoVisuals::IsValid(Style));
        const FDemoAmmoTracerSettings Incoming=DemoAmmoVisuals::TracerSettings(Config,Style,false);
        const FDemoAmmoTracerSettings Outgoing=DemoAmmoVisuals::TracerSettings(Config,Style,true);
        TestTrue(TEXT("Ricochet preserves exact ammo hue"),Outgoing.Color==Incoming.Color); // FLinearColor::Equals用严格<，Tolerance=0连自身也false；operator==才是逐通道零差合同。
        TestTrue(TEXT("Ricochet increases core brightness"),Outgoing.CoreIntensity>Incoming.CoreIntensity);
        TestTrue(TEXT("Ricochet widens the same ammo shape"),Outgoing.Width>Incoming.Width);
        TestTrue(TEXT("Ricochet fade outlasts ordinary segment"),Outgoing.FadeSeconds>Incoming.FadeSeconds);
        TestTrue(TEXT("Needle lengths still respect hard 1000 cm cap"),Outgoing.Length<=1000.f && Incoming.Length<=1000.f);
        for (uint8 Other=Index+1;Other<4;++Other)
        {
            const FDemoAmmoVisualStyle B=DemoAmmoVisuals::Default(Other);
            TestFalse(TEXT("Ammo palettes differ"),Style.TracerColor.Equals(B.TracerColor));
            TestTrue(TEXT("Ammo also differs beyond color"),Style.Shape!=B.Shape && (Style.WidthScale!=B.WidthScale || Style.LengthScale!=B.LengthScale || Style.FadeScale!=B.FadeScale));
        }
    }
    TestEqual(TEXT("Visual combination preserves projectile speed"),Config.InitialSpeed,Speed);
    TestEqual(TEXT("Visual combination preserves collision radius"),Config.CollisionRadius,Radius);
    TestEqual(TEXT("Visual combination preserves gameplay lifetime"),Config.MaxLifeSeconds,Life);
    TestEqual(TEXT("Visual code does not write weapon damage"),Weapon.BaseDamage,Damage);
    TestEqual(TEXT("Visual code does not write weapon range"),Weapon.Range,Range);
    FDemoAmmoVisualStyle Invalid=DemoAmmoVisuals::Default(1); Invalid.TracerColor.R=std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("NaN style invalid"),DemoAmmoVisuals::IsValid(Invalid));
    TestTrue(TEXT("Bad style alone falls back to normal visual"),DemoAmmoVisuals::TracerSettings(Config,Invalid,false).Color.Equals(DemoAmmoVisuals::Default().TracerColor));
    TestTrue(TEXT("Missing Shooter/catalog safely falls back to normal"),DemoAmmoVisuals::Resolve(nullptr,1).TracerColor.Equals(DemoAmmoVisuals::Default().TracerColor));
    TestTrue(TEXT("Out-of-range ammo safely falls back to normal"),DemoAmmoVisuals::Resolve(nullptr,255).TracerColor.Equals(DemoAmmoVisuals::Default().TracerColor));
    UDemoAmmoCatalog* LocalCatalog=NewObject<UDemoAmmoCatalog>(GetTransientPackage()); // 本测试拥有的临时目录，只破坏此副本，不保存/修改正式资产。
    const FLinearColor NormalColor=DemoAmmoVisuals::Default().TracerColor; // 回退必须使用代码普通基线，不能依赖坏目录的普通项。
    TestTrue(TEXT("Valid catalog selects the frozen fire index"),DemoAmmoVisuals::ResolveCatalog(LocalCatalog,1).TracerColor.Equals(LocalCatalog->Entries[1].VisualStyle.TracerColor));
    TestTrue(TEXT("Direct missing catalog uses ordinary style"),DemoAmmoVisuals::ResolveCatalog(nullptr,1).TracerColor.Equals(NormalColor));
    TestTrue(TEXT("Direct out-of-range catalog index uses ordinary style"),DemoAmmoVisuals::ResolveCatalog(LocalCatalog,255).TracerColor.Equals(NormalColor));
    LocalCatalog->Entries[1].VisualStyle.CoreIntensity=std::numeric_limits<float>::infinity();
    TestTrue(TEXT("Invalid catalog visual config safely falls back"),DemoAmmoVisuals::ResolveCatalog(LocalCatalog,1).TracerColor.Equals(NormalColor));
    LocalCatalog->Entries[2].Id=TEXT("unknown-ammo");
    TestTrue(TEXT("Wrong stable ID safely falls back without selecting by display name"),DemoAmmoVisuals::ResolveCatalog(LocalCatalog,2).TracerColor.Equals(NormalColor));
    LocalCatalog->Entries.RemoveAt(3);
    TestTrue(TEXT("Incomplete catalog safely falls back"),DemoAmmoVisuals::ResolveCatalog(LocalCatalog,0).TracerColor.Equals(NormalColor));
    FDemoProjectilePresentationLaunch Launch; Launch.AmmoType=1; // 此测试仅覆盖生产客户端冻结值缓存，不冒充ShotContext真实World开火验收。
    FDemoProjectileCosmeticState State; const FLinearColor First=State.ResolveVisualStyle(Launch).TracerColor;
    Launch.AmmoType=2;
    TestTrue(TEXT("Once resolved, later ammo inputs cannot recolor in-flight state"),State.ResolveVisualStyle(Launch).TracerColor==First); // 精确逐通道比较，不增大容差放过色差。
    TestTrue(TEXT("Fallback is latched even if Shooter was unavailable"),State.bVisualStyleResolved);
    return !HasAnyErrors();
}

/** Editor Automation入口：新配置字段的闭区间/NaN检查，只有列明九个错误为预期。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoBulletVfxConfig,"FPSDemo.BulletVFX.ConfigRanges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoBulletVfxConfig::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.ConfigRanges"));
    struct FFieldRange { float FDemoProjectileConfig::* Field; float Min; float Max; }; // 测试只写独立Config实例，字段上下界为设计合同而非读取Clamp元数据。
    const FFieldRange Ranges[]={{&FDemoProjectileConfig::RicochetTracerWidthScale,.1f,4.f},{&FDemoProjectileConfig::RicochetCoreBoost,1.f,8.f},
        {&FDemoProjectileConfig::RicochetTracerFadeSeconds,0.f,1.f},{&FDemoProjectileConfig::IncomingTracerFadeOnRicochet,0.f,1.f}};
    AddExpectedError(TEXT("PROJECTILE_CONFIG_REJECT"),EAutomationExpectedErrorFlags::Contains,9); // 四字段低/高越界加NaN均应明确拒绝，不吞其他Error。
    FString Error; // 生产Validate返回的可诊断原因。
    for (const FFieldRange& Range:Ranges)
    {
        FDemoProjectileConfig Config;
        Config.*Range.Field=Range.Min; TestTrue(TEXT("Inclusive lower bound accepted"),Config.Validate(Error));
        Config.*Range.Field=Range.Max; TestTrue(TEXT("Inclusive upper bound accepted"),Config.Validate(Error));
        Config.*Range.Field=Range.Min-.01f; TestFalse(TEXT("Below lower bound rejected"),Config.Validate(Error));
        Config.*Range.Field=Range.Max+.01f; TestFalse(TEXT("Above upper bound rejected"),Config.Validate(Error));
    }
    FDemoProjectileConfig Config; Config.RicochetCoreBoost=std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("NaN boost rejected"),Config.Validate(Error));
    Config=FDemoProjectileConfig();
    TestTrue(TEXT("Empty optional FX and sounds remain legal"),Config.Validate(Error) && !Config.RicochetEffect && !Config.RicochetSound);
    return !HasAnyErrors();
}

/** Editor Automation入口：作者保存后的正式Catalog数据合同，独立新进程执行；不验证视觉效果。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoBulletVfxCatalog,"FPSDemo.BulletVFX.AuthoredCatalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoBulletVfxCatalog::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] BulletVFX.AuthoredCatalog"));
    const UDemoAmmoCatalog* Catalog=LoadObject<UDemoAmmoCatalog>(nullptr,TEXT("/Game/Data/Ammo/DA_AmmoCatalog.DA_AmmoCatalog")); // 独立磁盘资产验收合同，必须在作者脚本成功且新进程读回后运行。
    if (!TestNotNull(TEXT("Formal ammo catalog exists"),Catalog) || !TestEqual(TEXT("Stable four entries remain"),Catalog->Entries.Num(),4)) return false;
    for (int32 Index=0;Index<4;++Index)
    {
        const FDemoAmmoEntry& Entry=Catalog->Entries[Index];
        TestEqual(TEXT("Stable ammo order unchanged"),Entry.Id,UDemoAmmoCatalog::IdAt(Index));
        TestTrue(TEXT("Authored style valid"),DemoAmmoVisuals::IsValid(Entry.VisualStyle));
        for (int32 Other=Index+1;Other<4;++Other)
        {
            const FDemoAmmoVisualStyle& B=Catalog->Entries[Other].VisualStyle;
            TestFalse(TEXT("Saved styles have distinct palettes"),Entry.VisualStyle.TracerColor.Equals(B.TracerColor));
            TestTrue(TEXT("Saved styles have distinct shape channels"),Entry.VisualStyle.Shape!=B.Shape);
        }
    }
    return !HasAnyErrors();
}
#endif
