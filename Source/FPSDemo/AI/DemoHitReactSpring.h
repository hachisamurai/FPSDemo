// 职责：敌人受击倾斜的纯值二阶弹簧。使用：本地HitReact及Editor合同共用Kick/Step；只读配置，不持有UObject、World、碰撞或网络权限。
#pragma once

#include "CoreMinimal.h"
#include "Combat/Feedback/DemoHitFxSettings.h"
#include "Debug/DemoStaticLog.h"

/** 世界轴角（度）及位移（cm）的解析阻尼弹簧；逐次采样硬限幅，长帧不使用不稳定的显式欧拉积分。 */
struct FPSDEMO_API FDemoHitReactSpring
{
    FVector AngleDegrees = FVector::ZeroVector; // 世界轴角向量，模长为倾斜度数；只在本地表现实例存活期间持有。
    FVector AngularVelocity = FVector::ZeroVector; // 世界轴角速度，度/秒，不复制、不写骨骼物理。
    FVector OffsetCm = FVector::ZeroVector; // 世界位移cm，叠加于表现转向后的当前网格基准。
    FVector OffsetVelocity = FVector::ZeroVector; // 世界位移速度cm/s，仅解析弹簧值。

    /** Direction为实际子弹世界方向，零/非法拒绝；Facing为当前网格世界朝向供头部额外后仰，Strength用于短间隔40%冲量。 */
    bool Kick(const FVector& Direction, bool bCore, bool bHeavy, const UDemoHitFxSettings& Settings,
        float Strength = 1.f, const FVector& Facing = FVector::ForwardVector)
    {
        DEMO_LOG_STATIC_TICK(nullptr); // 纯值非UObject，使用项目静态入口宏保留同类别限时日志。
        if (Direction.ContainsNaN() || Direction.IsNearlyZero() || !FMath::IsFinite(Strength) || Strength <= 0.f)
        { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_REACT_SPRING_REJECT nodir/strength")); return false; }
        const FVector Unit = Direction.GetSafeNormal(); // 冻结实际飞行方向，向后位移不受当前玩家/换枪影响。
        const FVector Facing2D = Facing.ContainsNaN() ? FVector::ForwardVector : Facing.GetSafeNormal2D(); // 当前可见朝向，只用于头部后仰轴。
        FVector Axis = FVector::CrossProduct(FVector::UpVector, Unit).GetSafeNormal(); // 上方叉乘飞行方向，使身体顶端远离来弹。
        if (Axis.IsNearlyZero()) Axis = FVector::CrossProduct(FVector::UpVector, Facing2D).GetSafeNormal(); // 垂直来弹仍使用当前身体平面轴。
        const float Peak = FMath::Clamp(bHeavy ? Settings.ReactHeavyDeg : bCore ? Settings.ReactHeadDeg : Settings.ReactBodyDeg, 0.f, 12.f); // 度，分档原始峰值。
        FVector AnglePeak = Axis * Peak; // 向量合成后再限制总角，不能分别限轴造成对角越界。
        if (bCore) AnglePeak -= FVector::CrossProduct(FVector::UpVector, Facing2D).GetSafeNormal() * FMath::Clamp(Settings.ReactHeadBackDeg, 0.f, 12.f);
        const float OffsetPeak = FMath::Clamp(bHeavy ? Settings.ReactHeavyOffset : Settings.ReactBodyOffset, 0.f, 5.f); // cm，重武器优先。
        const double Omega = 2. * PI * SafeHz(Settings); // 弧度/秒，仅为解析固有频率，角位移仍使用度。
        const double Zeta = SafeDamping(Settings); // 无量纲阻尼；欠阻尼/临界/过阻尼均支持。
        const double VelocityFactor = ImpulseFactor(Omega, Zeta); // 将目标静止单冲量峰值换算成初始速度。
        AngularVelocity += AnglePeak * VelocityFactor * FMath::Clamp(Strength, 0.f, 1.f);
        OffsetVelocity += Unit * OffsetPeak * VelocityFactor * FMath::Clamp(Strength, 0.f, 1.f);
        AngularVelocity = AngularVelocity.GetClampedToMaxSize(FMath::Clamp(Settings.ReactMaxDeg, 0.f, 12.f) * VelocityFactor);
        OffsetVelocity = OffsetVelocity.GetClampedToMaxSize(FMath::Clamp(Settings.ReactMaxOffset, 0.f, 5.f) * VelocityFactor);
        return true;
    }

    /** DeltaSeconds为World秒；无输入推进到基准，非法/负时间拒绝，位置与速度同解以保持低帧稳定。 */
    void Step(float DeltaSeconds, const UDemoHitFxSettings& Settings)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f)
        { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_REACT_SPRING_REJECT invalid delta")); return; }
        const double Omega = 2. * PI * SafeHz(Settings); // 解析频率rad/s，限制非法ini避免NaN传播到组件。
        const double Zeta = SafeDamping(Settings); // 阻尼比，允许作者配置过阻尼。
        AdvancePair(AngleDegrees, AngularVelocity, DeltaSeconds, Omega, Zeta);
        AdvancePair(OffsetCm, OffsetVelocity, DeltaSeconds, Omega, Zeta);
        LimitPair(AngleDegrees, AngularVelocity, FMath::Clamp(Settings.ReactMaxDeg, 0.f, 12.f));
        LimitPair(OffsetCm, OffsetVelocity, FMath::Clamp(Settings.ReactMaxOffset, 0.f, 5.f));
        if (IsSettled()) Reset(); // 精确零作为组件停止Tick/归还最新变换的依据。
    }

    /** 角度<0.05°且角速度<0.5°/s，同时位移/速度已消退才为true；仅供本地表现停止Tick。 */
    bool IsSettled() const
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        return AngleDegrees.SizeSquared() < .05 * .05 && AngularVelocity.SizeSquared() < .5 * .5
            && OffsetCm.SizeSquared() < .01 * .01 && OffsetVelocity.SizeSquared() < .5 * .5;
    }

    /** 死亡/会话清理/静止时清空纯值，不会改Actor、网格或伤害。 */
    void Reset()
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        AngleDegrees = AngularVelocity = OffsetCm = OffsetVelocity = FVector::ZeroVector;
    }

private:
    /** 配置只读；非有限频率回默认9Hz，防作者ini非法值污染网格。 */
    static double SafeHz(const UDemoHitFxSettings& Settings)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        return FMath::IsFinite(Settings.ReactSpringHz) ? FMath::Clamp(Settings.ReactSpringHz, 1.f, 30.f) : 9.;
    }
    /** 配置只读；非有限阻尼回默认0.55，允许0.1..2的完整解析域。 */
    static double SafeDamping(const UDemoHitFxSettings& Settings)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        return FMath::IsFinite(Settings.ReactDamping) ? FMath::Clamp(Settings.ReactDamping, .1f, 2.f) : .55;
    }
    /** Omega/Zeta为已清洗频率/阻尼；返回单位静止位移峰值所需速度，不分配内存。 */
    static double ImpulseFactor(double Omega, double Zeta)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (Zeta < 1. - 1.e-4) return Omega * FMath::Exp(Zeta * FMath::Acos(Zeta) / FMath::Sqrt(1. - Zeta * Zeta));
        if (Zeta <= 1. + 1.e-4) return Omega * FMath::Exp(1.);
        const double Root = FMath::Sqrt(Zeta * Zeta - 1.); // 过阻尼两个实特征根，均为负数。
        const double R1 = -Omega * (Zeta - Root), R2 = -Omega * (Zeta + Root); // 单位1/s。
        const double PeakTime = FMath::Loge(R2 / R1) / (R1 - R2); // 单位冲量响应速度归零时刻s。
        return (R1 - R2) / (FMath::Exp(R1 * PeakTime) - FMath::Exp(R2 * PeakTime));
    }
    /** Position/Velocity为同一轴组的in/out；Delta为非负秒数，Omega/Zeta已清洗，闭式解不依赖帧分区。 */
    static void AdvancePair(FVector& Position, FVector& Velocity, double Delta, double Omega, double Zeta)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        const FVector Before = Position; // 旧位置独立保存，速度公式不能使用已更新位置。
        const double A = Omega * Zeta; // 指数衰减系数1/s。
        if (Zeta > 1. + 1.e-4)
        {
            const double Root = FMath::Sqrt(Zeta * Zeta - 1.); // 两实根避免对过阻尼使用欠阻尼近似。
            const double R1 = -Omega * (Zeta - Root), R2 = -Omega * (Zeta + Root);
            const FVector Term1 = (Velocity - Before * R2) * FMath::Exp(R1 * Delta); // 第一特征模态。
            const FVector Term2 = (Before * R1 - Velocity) * FMath::Exp(R2 * Delta); // 第二特征模态。
            Position = (Term1 + Term2) / (R1 - R2);
            Velocity = (Term1 * R1 + Term2 * R2) / (R1 - R2);
            return;
        }
        double Cosine = 1., SineOverFrequency = Delta; // 临界阻尼极限，不除以接近零的频率。
        if (Zeta < 1. - 1.e-4)
        {
            const double Damped = Omega * FMath::Sqrt(1. - Zeta * Zeta); // 欠阻尼角频率rad/s。
            Cosine = FMath::Cos(Damped * Delta); SineOverFrequency = FMath::Sin(Damped * Delta) / Damped;
        }
        const double Decay = FMath::Exp(-A * Delta); // 单帧精确指数衰减，长帧不爆炸。
        Position = ((Cosine + A * SineOverFrequency) * Before + SineOverFrequency * Velocity) * Decay;
        Velocity = (-Omega * Omega * SineOverFrequency * Before + (Cosine - A * SineOverFrequency) * Velocity) * Decay;
    }
    /** Max为总向量模长上限；到边界时仅移除向外速度，避免连射累计后在边界持续粘连。 */
    static void LimitPair(FVector& Position, FVector& Velocity, double Max)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (Position.ContainsNaN() || Velocity.ContainsNaN() || !FMath::IsFinite(Max) || Max <= 0.)
        { Position = Velocity = FVector::ZeroVector; return; }
        if (Position.SizeSquared() <= Max * Max) return;
        const FVector Outward = Position.GetSafeNormal(); // 当前越界方向，反向回弹速度仍保留。
        Position = Outward * Max;
        const double OutwardSpeed = FVector::DotProduct(Velocity, Outward); // 同位置单位/秒。
        if (OutwardSpeed > 0.) Velocity -= Outward * OutwardSpeed;
    }
};
