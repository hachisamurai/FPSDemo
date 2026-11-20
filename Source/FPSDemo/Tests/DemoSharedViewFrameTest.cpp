// 职责：验证共享稳定眼位不受动画head影响、跟随角色平移且任意配置被约束于安全胶囊；不加载资产/World或联网。
// 使用：Automation RunTests FPSDemo.View.SharedStableEye；依赖生产纯值Resolve与独立点到轴段几何预期，不能替代实机/联机/艺术验收。
#include "Characters/DemoSharedViewFrame.h"
#include "Misc/AutomationTest.h"
#include "Debug/DemoLog.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoSharedViewFrameTest,"FPSDemo.View.SharedStableEye",EAutomationTestFlags::ApplicationContextMask|EAutomationTestFlags::EngineFilter)

/** Parameters为引擎测试参数；构造数值快照检查稳定/旧策略与半球安全边界，不改变正式配置或场景。 */
bool FDemoSharedViewFrameTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FDemoSharedViewFrameTest::RunTest"));
    FDemoSharedViewFrame Frame; Frame.bUseAnimatedHead=false; Frame.Center=FVector(1200,-50,300); // 非零角色位置，确保不是恰好在原点才正确。
    Frame.StableEye=Frame.Center+FVector(8,0,75); Frame.Radius=34.f; Frame.CapsuleHalfHeight=91.f; Frame.HeightLimits=FVector2D(-66,91);
    const FVector Baseline=Frame.Resolve(Frame.Center); // Head不属于稳定策略输入，随后用极端动画位置作反例。
    TestTrue(TEXT("Stable eye preserves calibrated default"),Baseline.Equals(Frame.StableEye,.000001));
    TestTrue(TEXT("Animated head displacement cannot move stable eye"),Baseline.Equals(Frame.Resolve(FVector(1e8,-1e7,8e7)),.000001));
    const FVector Move(27,-18,130); Frame.Center+=Move; Frame.StableEye+=Move;
    TestTrue(TEXT("Physical character translation moves eye without animation dependency"),Frame.Resolve(FVector::ZeroVector).Equals(Baseline+Move,.000001));
    bool bInside=true; // 独立几何：点到胶囊中间轴段的距离不超过Radius，覆盖高Z+大水平配置而非仅默认值。
    for(double X:{-500.,-30.,0.,30.,500.}) for(double Z:{-500.,-91.,-75.,0.,75.,91.,500.})
    {
        Frame.StableEye=Frame.Center+FVector(X,90,Z);
        const FVector Relative=Frame.Resolve(FVector::ZeroVector)-Frame.Center;
        const FVector AxisPoint(0,0,FMath::Clamp(Relative.Z,-57.,57.)); // H-R=57，胶囊中间轴段；与生产半球横半径公式独立。
        bInside&=FVector::Distance(Relative,AxisPoint)<=34.000001 && Relative.Z>=-66.000001 && Relative.Z<=91.000001;
    }
    TestTrue(TEXT("Large configured offsets remain in shrunken capsule including endcaps"),bInside);
    Frame.bUseAnimatedHead=true; Frame.EyeOffset=FVector(4,0,10);
    TestTrue(TEXT("Explicit legacy mode still follows head for same-binary AB"),!Frame.Resolve(Frame.Center).Equals(Frame.Resolve(Frame.Center+FVector(10,0,0)),.001));
    return !HasAnyErrors();
}
#endif
