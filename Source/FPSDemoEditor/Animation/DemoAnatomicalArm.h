// 职责：离线持枪烘焙的肘平面选择；由FP/TP作者调用，保留原骨长、腕握点和五指接触，不进入运行时Gameplay。
#pragma once
#include "CoreMinimal.h"
#include "Debug/DemoLog.h"

namespace DemoAnatomicalArm
{
    /** Shoulder/Elbow/Wrist为原链，Target/Rotation为新腕姿，PalmLocal为腕到中指根的手局部方向。
     * PreferredPole保留作者原极点倾向；在合法肘圆内兼顾直腕与下沉肘，不移动肩根或伸长骨骼。 */
    inline FVector ChoosePole(const FTransform& Shoulder,const FTransform& Elbow,const FTransform& Wrist,
        const FVector& Target,const FQuat& Rotation,const FVector& PalmLocal,const FVector& PreferredPole)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 静态值工具无this，使用同类别的高频入口埋点。
        const FVector Start=Shoulder.GetLocation(); // 本帧固定肩根，厘米。
        const float UpperLength=FVector::Dist(Start,Elbow.GetLocation()); // 原上臂长度。
        const float LowerLength=FVector::Dist(Elbow.GetLocation(),Wrist.GetLocation()); // 原前臂长度。
        const FVector Delta=Target-Start; const float Distance=Delta.Size(); // 原骨长定义的合法肘圆轴。
        if (Distance<=FMath::Abs(UpperLength-LowerLength)+.01f || Distance>=UpperLength+LowerLength-.01f || PalmLocal.IsNearlyZero())
        { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("ANATOMICAL_POLE_FALLBACK unreachable or missing palm axis distance=%.3f"),Distance); return PreferredPole; }
        const FVector Axis=Delta/Distance; // 肩腕轴。
        const FVector Palm=Rotation.RotateVector(PalmLocal).GetSafeNormal(); // 新握点要求的掌纵向，不猜骨本地X轴。
        FVector Bend=PreferredPole-Start; Bend=(Bend-Axis*FVector::DotProduct(Bend,Axis)).GetSafeNormal(); // 连续原极点倾向。
        if (Bend.IsNearlyZero()) Bend=FVector::CrossProduct(Axis,FVector::UpVector).GetSafeNormal();
        if (Bend.IsNearlyZero()) Bend=FVector::RightVector; // 仅正竖直臂的退化兜底。
        const float Along=(UpperLength*UpperLength+Distance*Distance-LowerLength*LowerLength)/(2.f*Distance);
        const FVector Centre=Start+Axis*Along; // 固定骨长肘圆中心。
        const float Radius=FMath::Sqrt(FMath::Max(0.f,UpperLength*UpperLength-Along*Along));
        const FVector Tangent=FVector::CrossProduct(Axis,Bend).GetSafeNormal(); // 肘圆第二基轴。
        const FVector Preferred=Centre+Bend*Radius; // 在合法肘圆上的原极点投影。
        FVector Best=Preferred; float BestCost=TNumericLimits<float>::Max(); // 只在本帧求解，不积累前帧误差。
        for (int32 Degrees=-180;Degrees<=180;Degrees+=3)
        {
            const float Angle=FMath::DegreesToRadians(static_cast<float>(Degrees)); // 有限离线搜索步长。
            const FVector Trial=Centre+Radius*(Bend*FMath::Cos(Angle)+Tangent*FMath::Sin(Angle));
            const float WristAngle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct((Target-Trial)/LowerLength,Palm),-1.f,1.f)));
            const float Raised=FMath::Max(0.f,Trial.Z-Start.Z+3.f); // 肘应低于肩，厘米余量。
            const float AboveHand=FMath::Max(0.f,Trial.Z-Target.Z+3.f); // 避免为了直腕把肘抬到手上方。
            const float Cost=.04f*WristAngle*WristAngle+1.2f*Raised*Raised+2.f*AboveHand*AboveHand+.008f*FVector::DistSquared(Trial,Preferred);
            if (Cost<BestCost) { Best=Trial; BestCost=Cost; }
        }
        return Best; // TwoBoneIK接受合法肘点作为平面极点，腕位置/方向仍由调用者精确保持。
    }
}
