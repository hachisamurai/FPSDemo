// 职责：弹体纯表现数学/只读网络事实合同。使用：Editor Automation FPSDemo.ProjectilePresentation；不生成World、不冒充真实射击/多人/RHI验收。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Weapons/Projectiles/DemoProjectilePresentation.h"
#include "Debug/DemoLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoProjectilePresentationGeometry,"FPSDemo.ProjectilePresentation.GeometryInvariants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoProjectilePresentationGeometry::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] ProjectilePresentation.GeometryInvariants"));
    const FVector Physical(12345.,-9842.,20127.),Offset(17.,-31.,22.); // 非零大世界原点，检查相对变换不会依赖坐标原点。
    const FQuat Rotation(FVector(1.,2.,3.).GetSafeNormal(),.73); // 任意非轴对齐旋转，不能只在朝+X例子通过。
    const FVector Translation(-20100.,3928.,998.);
    float LastMagnitude=static_cast<float>(Offset.Size())+1.f; // 归并过程不得放大偏移或先缩后涨。
    for (int32 Index=0;Index<=100;++Index)
    {
        const float Distance=Index*1.2f; // cm；同一实际飞长可对应完全不同弹速/帧率。
        const FVector Value=DemoProjectilePresentation::Map(Physical,Offset,Distance,120.f);
        const float Magnitude=static_cast<float>(FVector::Distance(Value,Physical)); // 测试容差为厘米，显式从LWC测量转float。
        TestTrue(TEXT("Correction stays bounded and decreases"),Magnitude<=LastMagnitude+1.e-5f && Magnitude<=Offset.Size()+1.e-5);
        LastMagnitude=Magnitude;
        TestTrue(TEXT("World translation covariance"),DemoProjectilePresentation::Map(Physical+Translation,Offset,Distance,120.f).Equals(Value+Translation,1.e-7));
        TestTrue(TEXT("World rotation covariance"),DemoProjectilePresentation::Map(Rotation.RotateVector(Physical),Rotation.RotateVector(Offset),Distance,120.f).Equals(Rotation.RotateVector(Value),1.e-7));
        TestTrue(TEXT("World observers with zero offset are identity"),DemoProjectilePresentation::Map(Physical,FVector::ZeroVector,Distance,120.f).Equals(Physical,0.));
    }
    TestTrue(TEXT("Initial mesh center starts at frozen visual muzzle"),DemoProjectilePresentation::Map(Physical,Offset,0.f,120.f).Equals(Physical+Offset,0.));
    TestTrue(TEXT("Past handoff has exactly authoritative position"),DemoProjectilePresentation::Map(Physical,Offset,10000.f,120.f).Equals(Physical,0.));
    TestTrue(TEXT("Invalid blend configuration degrades without displacement"),DemoProjectilePresentation::Map(Physical,Offset,30.f,0.f).Equals(Physical,0.));
    // Same sample after 15/30/60Hz partitions: time/fps never enters the mapping contract.
    for (int32 Rate : {15,30,60})
    {
        float OriginalFloatDistance=0.f; double AccumulatedDistance=0.; // 保留A01失败的float积分诊断，合同输入用double累计后一次cast，测试相同飞长而非积分器舍入。
        for (int32 Frame=0;Frame<Rate;++Frame) { OriginalFloatDistance+=60.f/Rate; AccumulatedDistance+=60./Rate; }
        const float Distance=static_cast<float>(AccumulatedDistance);
        const FVector Expected=DemoProjectilePresentation::Map(Physical,Offset,60.f,120.f);
        UE_LOG(LogFPSDemo,Log,TEXT("PROJECTILE_PARTITION_DIAGNOSTIC rate=%d original_float_distance=%.12f original_output_error=%.12f double_sum=%.15f tested_distance=%.12f tested_error=%.12f"),
            Rate,OriginalFloatDistance,FVector::Distance(DemoProjectilePresentation::Map(Physical,Offset,OriginalFloatDistance,120.f),Expected),AccumulatedDistance,Distance,FVector::Distance(DemoProjectilePresentation::Map(Physical,Offset,Distance,120.f),Expected));
        TestTrue(TEXT("Equal actual flown distance input"),FMath::IsNearlyEqual(Distance,60.f,1.e-6f));
        TestTrue(TEXT("Equivalent flown distance is independent of frame partition"),DemoProjectilePresentation::Map(Physical,Offset,Distance,120.f).Equals(Expected,1.e-6));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoProjectilePresentationSizes,"FPSDemo.ProjectilePresentation.VisualSizeInvariants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoProjectilePresentationSizes::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] ProjectilePresentation.VisualSizeInvariants"));
    const FVector Start(19231.,-483.,20777.),End(19294.,-447.,20801.); // 已映射的非轴对齐LWC视觉段，两端不能再围绕原点或中心缩小。
    const FVector Delta=End-Start;
    const float Length=static_cast<float>(Delta.Size());
    for (float Initial : {.2f,.5f,1.f})
    {
        float Previous=Initial; // 按真实距离递进，与任何显示帧率/弹速无关。
        for (int32 Index=0;Index<=120;++Index)
        {
            const float Size=DemoProjectilePresentation::RenderScale(Initial,static_cast<float>(Index),120.f);
            TestTrue(TEXT("Visible size only grows toward world size"),Size>=Previous-1.e-7f && Size>=Initial && Size<=1.f);
            Previous=Size;
            const FVector Scale=DemoProjectilePresentation::TracerMeshScale(Length,3.f,Size);
            const FTransform Transform(Delta.Rotation(),(Start+End)*.5,Scale); // 生产Cube为100cm，独立求实际端面中心而非复述getter结果。
            TestTrue(TEXT("FP width scale preserves mapped rear endpoint"),Transform.TransformPosition(FVector(-50.,0.,0.)).Equals(Start,1.e-4));
            TestTrue(TEXT("FP width scale preserves mapped front endpoint"),Transform.TransformPosition(FVector(50.,0.,0.)).Equals(End,1.e-4));
            TestTrue(TEXT("Only cross section inherits FP scale"),FMath::IsNearlyEqual(FVector::Distance(Transform.TransformPosition(FVector(0.,-50.,0.)),Transform.TransformPosition(FVector(0.,50.,0.))),3.*Size,1.e-5));
        }
        TestEqual(TEXT("First live Mesh size equals frozen muzzle size"),DemoProjectilePresentation::RenderScale(Initial,0.f,120.f),Initial);
        TestEqual(TEXT("At and beyond merge the world size is exact"),DemoProjectilePresentation::RenderScale(Initial,120.f,120.f),1.f);
        TestEqual(TEXT("Overshoot never grows beyond world size"),DemoProjectilePresentation::RenderScale(Initial,10000.f,120.f),1.f);
    }
    TestEqual(TEXT("Invalid negative size degrades to world"),DemoProjectilePresentation::RenderScale(-.2f,0.f,120.f),1.f);
    TestEqual(TEXT("Invalid merge configuration degrades to world"),DemoProjectilePresentation::RenderScale(.2f,30.f,0.f),1.f);
    TestEqual(TEXT("Invalid distance does not shrink world representation"),DemoProjectilePresentation::RenderScale(.2f,-1.f,120.f),1.f);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoProjectilePresentationBudget,"FPSDemo.ProjectilePresentation.BudgetLateActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoProjectilePresentationBudget::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] ProjectilePresentation.BudgetLateActor"));
    FDemoProjectileCosmeticBudget Budget; // 同一生产预算状态，不使用另一个镜像实现。
    TestTrue(TEXT("Last available identity may be tracked"),Budget.AllowNew(FDemoProjectileCosmeticBudget::Capacity-1));
    TestFalse(TEXT("Unknown terminal at full capacity closes unknown visual eligibility"),Budget.AllowNew(FDemoProjectileCosmeticBudget::Capacity));
    TestTrue(TEXT("Suppression retained for late Actor"),Budget.bRejectUnknown);
    TestFalse(TEXT("Pruning older entries cannot resurrect an untracked terminal Actor"),Budget.AllowNew(0));
    Budget=FDemoProjectileCosmeticBudget(); // 对应明确phase/Owner清理，不是普通剪枝。
    TestTrue(TEXT("Only explicit new phase resets the conservative budget"),Budget.AllowNew(0));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoProjectilePresentationFacts,"FPSDemo.ProjectilePresentation.NetworkFacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoProjectilePresentationFacts::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] ProjectilePresentation.NetworkFacts"));
    FDemoProjectileMuzzleReceipt Receipt; Receipt.Created=10.f; // 初建桥的合法0.5秒窗口完整覆盖，之后不再占用整枪预算。
    TestFalse(TEXT("Valid delayed launch keeps its frozen muzzle"),Receipt.IsExpired(10.5f));
    TestTrue(TEXT("Long fight does not retain completed shot muzzles for full projectile lifetime"),Receipt.IsExpired(11.1f));
    TestTrue(TEXT("Old World clock cannot retain a foreign muzzle receipt"),Receipt.IsExpired(0.f));
    FDemoProjectilePresentationSample Sample; // 没有Actor指针的短命弹事实也可校验结构；World资格由接收端另查。
    Sample.Launch.ShotId=FGuid::NewGuid(); Sample.Launch.Pellet=0; Sample.Launch.Level=1; Sample.Launch.Phase=1;
    Sample.Sequence=1; Sample.bFirst=true; Sample.End=FVector(10.,0.,0.); Sample.EndDistance=10.f;
    TestTrue(TEXT("Confirmed first segment is structurally valid"),DemoProjectilePresentation::IsValid(Sample));
    Sample.Sequence=2;
    TestFalse(TEXT("A later packet cannot claim first segment"),DemoProjectilePresentation::IsValid(Sample));
    Sample.bFirst=false;
    TestTrue(TEXT("Lost first packet does not invalidate later world segment"),DemoProjectilePresentation::IsValid(Sample));
    Sample.StartDistance=11.f;
    TestFalse(TEXT("Distance cannot run backwards"),DemoProjectilePresentation::IsValid(Sample));
    Sample.StartDistance=10.f; Sample.Start=Sample.End; Sample.bBoundary=true; Sample.bTerminal=true;
    TestTrue(TEXT("Zero length terminal fact remains valid for late Actor suppression"),DemoProjectilePresentation::IsValid(Sample));
    Sample.Launch.Pellet=64;
    TestFalse(TEXT("Out of bounded pellet identity rejected"),DemoProjectilePresentation::IsValid(Sample));
    TSet<FDemoProjectilePresentationKey> Keys; // 确保霰弹64粒同枪身份不会互相去重。
    for (int32 Index=0;Index<64;++Index) Keys.Add({Sample.Launch.ShotId,Index});
    TestEqual(TEXT("All pellets keep distinct identities"),Keys.Num(),64);
    Keys.Add({FGuid::NewGuid(),0});
    TestEqual(TEXT("Another shot with same pellet index remains distinct"),Keys.Num(),65);
    return !HasAnyErrors();
}
#endif
