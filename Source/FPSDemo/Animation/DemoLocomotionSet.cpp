// 职责：从移动配置插值实测步长；动画实例每帧调用，所有输入为只读值，不访问Actor或World。
#include "Animation/DemoLocomotionSet.h"
#include "Debug/DemoLog.h"

float UDemoLocomotionSet::GetStride(float DirectionDegrees, float Speed) const
{
    DEMO_LOG_TICK();
    if (Strides.IsEmpty()) return 300.f;
    const float Angle=FMath::UnwindDegrees(DirectionDegrees); // 输入归一化，±180在环绕邻区连续。
    const float RunAlpha=FMath::Clamp((Speed-WalkSpeed)/FMath::Max(1.f,RunSpeed-WalkSpeed),0.f,1.f); // 两环间步长随速度变化。
    for (int32 Index=0; Index<Strides.Num(); ++Index)
    {
        const FDemoGaitStride& A=Strides[Index]; // 配置数组当前左端点，同步借用。
        const FDemoGaitStride& B=Strides[(Index+1)%Strides.Num()]; // 最后一项环绕至首项。
        const float End=Index+1<Strides.Num()?B.Direction:B.Direction+360.f; // 连续区间上界。
        const float Query=Angle<A.Direction?Angle+360.f:Angle; // 仅环绕区间使用+360表达同一方向。
        if (Query>=A.Direction && Query<=End)
        {
            const float Weight=FMath::Clamp((Query-A.Direction)/FMath::Max(1.f,End-A.Direction),0.f,1.f); // 相邻方向比例。
            return FMath::Max(1.f,FMath::Lerp(FMath::Lerp(A.WalkCentimeters,A.RunCentimeters,RunAlpha),
                FMath::Lerp(B.WalkCentimeters,B.RunCentimeters,RunAlpha),Weight));
        }
    }
    return FMath::Max(1.f,FMath::Lerp(Strides[0].WalkCentimeters,Strides[0].RunCentimeters,RunAlpha));
}
