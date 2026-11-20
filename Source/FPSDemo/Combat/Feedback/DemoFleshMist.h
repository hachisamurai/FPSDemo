// 职责：固定血雾槽与解析运动的纯值实现；World池和自动化共用，无UObject/命中时分配、碰撞或伤害权限。
#pragma once
#include "CoreMinimal.h"
#include "Combat/Feedback/DemoHitFxSettings.h"
#include "Combat/Feedback/DemoCombatFeelSettings.h"
#include "Debug/DemoStaticLog.h"

namespace DemoHitFx
{
    /** Surface为服务器冻结表面，bEnabled为本次视觉许可，bHead为真实头部；硬表面恒0。 */
    inline uint8 ComputeFleshTier(EDemoImpactSurface Surface, bool bEnabled, bool bHead)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        return bEnabled && Surface == EDemoImpactSurface::Flesh ? (bHead ? 2 : 1) : 0;
    }
    /** 返回本距离最终团数；重武器只加近距，头部9+1仍封顶；非法/超距/未知等级返回0。 */
    inline int32 ComputeMistPuffCount(uint8 Tier, bool bHeavy, float DistanceSquared, const UDemoHitFxSettings& S)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        const float Max = FMath::IsFinite(S.MaxDistance) ? FMath::Clamp(S.MaxDistance,100.f,10000.f) : 2500.f; // 世界cm。
        if (Tier == 0 || Tier > 2 || !FMath::IsFinite(DistanceSquared) || DistanceSquared < 0.f || DistanceSquared > FMath::Square(Max)) return 0;
        const float Near = FMath::IsFinite(S.MistLodNearFraction) ? FMath::Clamp(S.MistLodNearFraction,0.f,1.f) : .32f; // 有限配置门限。
        const float Far = FMath::IsFinite(S.MistLodFarFraction) ? FMath::Clamp(S.MistLodFarFraction,Near,1.f) : FMath::Max(Near,.64f);
        const int32 Count = DistanceSquared > FMath::Square(Max*Far) ? S.MistFarPuffs :
            DistanceSquared > FMath::Square(Max*Near) ? S.MistMidPuffs : FMath::Clamp(Tier == 2 ? S.HeadPuffs : S.BodyPuffs,1,16) + (bHeavy ? FMath::Clamp(S.HeavyExtraPuffs,0,4) : 0);
        return FMath::Clamp(Count,1,FMath::Clamp(S.MistPuffsMax,1,16));
    }
    /** 有限区间随机取样，单位由调用方定义；反序自动排序，非法范围回退Fallback，不分配。 */
    inline float SampleRange(const FVector2D& Range, float Fallback, float Min, float Max)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (Range.ContainsNaN()) return Fallback;
        return FMath::FRandRange(FMath::Clamp(static_cast<float>(FMath::Min(Range.X,Range.Y)),Min,Max),
            FMath::Clamp(static_cast<float>(FMath::Max(Range.X,Range.Y)),Min,Max));
    }
    /** 配置标量有限检查/夹取；仅视觉输入，不更改CDO。 */
    inline float Finite(float Value, float Fallback, float Min, float Max)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        return FMath::IsFinite(Value) ? FMath::Clamp(Value,Min,Max) : Fallback;
    }
}

/** 一团血雾，单位cm/cm每秒/秒；每槽编译期最多16团，不在触发时new。 */
struct FDemoFleshPuff
{
    FVector Velocity = FVector::ZeroVector; // 外法线圆锥初速，世界cm/s。
    FQuat Orientation = FQuat::Identity; // 出生时冻结雾片世界姿态；长轴沿喷射方向的投影，双面可见。
    float StartRadius = 1.5f; // 起始半径cm。
    float EndRadius = 6.f; // 结束半径cm。
    float Life = .2f; // 正寿命秒。
};
/** 一簇血雾的固定纯值，空闲Age=-1；无目标引用。 */
struct FDemoFleshMistSlot
{
    FVector Origin = FVector::ZeroVector; // 冻结接触点+外偏cm。
    float Age = -1.f; // World秒龄，不使用实时时钟。
    int32 Count = 0; // 当前LOD团数，不超过冻结PuffsPerSlot。
    FDemoFleshPuff Puffs[16]; // 与配置硬上限对应，不扩容。
};
/** 与实际ISM共用的固定状态/缓冲；Initialize仅World开始调用，测试复用相同生产路径。 */
struct FDemoFleshMistState
{
    TArray<FDemoFleshMistSlot> Slots; // 一次性分配4..32簇。
    TArray<FTransform> Transforms; // 固定Slots*PuffsPerSlot，零缩放隐藏。
    TArray<float> Fades; // 与实例同长，CustomData0淡出系数。
    int32 PuffsPerSlot = 9; // 初始化冻结的1..16实例步幅。
    int32 Active = 0; // 当前活跃簇，不超过Slots.Num。
    uint64 Dropped = 0; // 仅池满丢弃，不抢占、不增长。
    /** 初始化预分配全部数组；只可在World开始/独立测试初始化调用。 */
    void Initialize(int32 Capacity, int32 Puffs)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        PuffsPerSlot = FMath::Clamp(Puffs,1,16);
        Slots.Init(FDemoFleshMistSlot(),FMath::Clamp(Capacity,4,32));
        Transforms.Init(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),Slots.Num()*PuffsPerSlot);
        Fades.Init(0.f,Transforms.Num()); Active=0; Dropped=0;
    }
    /** 已距离裁剪的纯值输入；返回团数，0=无团/满池；命中时无数组增长或UObject创建。 */
    int32 Spawn(const FVector& Point, const FVector& Normal, uint8 Tier, bool bHeavy, float DistanceSquared, const UDemoHitFxSettings& S)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        const int32 Count = FMath::Min(PuffsPerSlot,DemoHitFx::ComputeMistPuffCount(Tier,bHeavy,DistanceSquared,S)); // 同一生产LOD用于合同。
        if (Count==0 || Point.ContainsNaN() || Normal.ContainsNaN()) return 0;
        int32 Free=INDEX_NONE; // 只查既有空闲槽，不抢占。
        for(int32 Index=0;Index<Slots.Num();++Index) if(Slots[Index].Age<0.f) {Free=Index;break;}
        if(Free==INDEX_NONE) {++Dropped; return 0;}
        FDemoFleshMistSlot& Slot=Slots[Free]; // 固定数组同步借用。
        const FVector Out=Normal.GetSafeNormal(.001f,FVector::UpVector); // 零法线安全退向上。
        Slot.Origin=Point+Out*DemoHitFx::Finite(S.MistNormalOffset,1.5f,0.f,10.f); Slot.Age=0.f; Slot.Count=Count;
        const bool bHead=Tier==2; // 仅视觉分档。
        FVector Tangent, Bitangent; // 接触切平面的两个单位轴，避免面向玩家时所有雾团投影重叠。
        Out.FindBestAxisVectors(Tangent, Bitangent);
        const float Phase=FMath::FRandRange(0.f,2.f*PI); // 每次接触随机旋转整簇；仍以命中点为起点。
        const float Cone=FMath::DegreesToRadians(DemoHitFx::Finite(bHead?S.HeadConeDeg:S.BodyConeDeg,bHead?65.f:55.f,0.f,89.f));
        for(int32 Index=0;Index<Count;++Index)
        {
            FDemoFleshPuff& Puff=Slot.Puffs[Index]; // 本槽固定存储，无临时数组。
            Puff.StartRadius=DemoHitFx::SampleRange(bHead?S.HeadStartRadius:S.BodyStartRadius,2.f,.1f,30.f)*(bHeavy?DemoHitFx::Finite(S.HeavyStartScale,1.15f,.1f,3.f):1.f);
            Puff.EndRadius=DemoHitFx::SampleRange(bHead?S.HeadEndRadius:S.BodyEndRadius,10.f,.1f,50.f)*(bHeavy?DemoHitFx::Finite(S.HeavyEndScale,1.25f,.1f,3.f):1.f);
            Puff.Life=DemoHitFx::SampleRange(bHead?S.HeadLife:S.BodyLife,.2f,.02f,1.f)+(bHeavy?DemoHitFx::Finite(S.HeavyLifeBonus,.02f,0.f,.1f):0.f);
            const float Azimuth=Phase+2.f*PI*(Index+FMath::FRandRange(-.23f,.23f))/Count; // 每团占独立扇区，保留自然抖动。
            const float Angle=Cone*FMath::FRandRange(.48f,1.f); // 侧向散开，留出命中中心的可读轮廓。
            const FVector Direction=Out*FMath::Cos(Angle)+(Tangent*FMath::Cos(Azimuth)+Bitangent*FMath::Sin(Azimuth))*FMath::Sin(Angle);
            const FVector Side=-Tangent*FMath::Sin(Azimuth)+Bitangent*FMath::Cos(Azimuth); // 扇区横轴，用于错开雾片所在平面。
            const FVector CardNormal=(Out+Side*FMath::FRandRange(-.85f,.85f)).GetSafeNormal();
            Puff.Orientation=FRotationMatrix::MakeFromZX(CardNormal,Direction).ToQuat();
            Puff.Velocity=Direction*
                DemoHitFx::SampleRange(bHead?S.HeadSpeed:S.BodySpeed,100.f,0.f,1000.f)*(bHeavy?DemoHitFx::Finite(S.HeavySpeedScale,1.2f,.1f,3.f):1.f);
        }
        ++Active; return Count;
    }
    /** World秒推进，固定数组写入淡出和半径解析值；低帧直接越过寿命释放，不补粒子。 */
    void Step(float DeltaSeconds, const UDemoHitFxSettings& S)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if(!FMath::IsFinite(DeltaSeconds)||DeltaSeconds<0.f) return;
        const FTransform Hidden(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector); // 空闲索引不移除。
        const float Gravity=DemoHitFx::Finite(S.MistGravity,-90.f,-1000.f,0.f); // 世界Z加速度cm/s²。
        for(int32 Index=0;Index<Slots.Num();++Index)
        {
            FDemoFleshMistSlot& Slot=Slots[Index]; // 帧内借用。
            if(Slot.Age<0.f) continue;
            Slot.Age+=DeltaSeconds;
            bool bAlive=false; // 整簇最后一团消失后释放。
            for(int32 PuffIndex=0;PuffIndex<PuffsPerSlot;++PuffIndex)
            {
                const int32 Instance=Index*PuffsPerSlot+PuffIndex; // 固定ISM下标。
                const FDemoFleshPuff& Puff=Slot.Puffs[PuffIndex];
                if(PuffIndex>=Slot.Count||Slot.Age>=Puff.Life) {Transforms[Instance]=Hidden;Fades[Instance]=0.f;continue;}
                bAlive=true;
                const float T=Slot.Age/Puff.Life; // 0..1，寿命已保证正数。
                Fades[Instance]=1.f-T*T; // 前半程先读到血色，散开后再淡出，零寿命时仍由上方隐藏。
                const float Radius=FMath::Lerp(Puff.StartRadius,Puff.EndRadius,1.f-FMath::Square(1.f-T)); // 半径cm。
                const FVector Center=Slot.Origin+Puff.Velocity*Slot.Age+FVector(0,0,.5f*Gravity*FMath::Square(Slot.Age)); // 冻结外喷轨迹，重力仍只影响表现。
                Transforms[Instance]=FTransform(Puff.Orientation,Center,FVector(2.15f,1.1f,1.f)*(2.f*Radius/100.f)); // 使用细丝与碎滴遮罩打破完整外轮廓。
            }
            if(!bAlive) {Slot.Age=-1.f;--Active;}
        }
    }
};

