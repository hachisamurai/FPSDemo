// 职责：World拥有的无碰撞弹种亮条及接触短闪。使用：真实世界段或已确认Owner近枪视觉段；实例MID着色，session/阶段结束统一清理。 本人首段使用发射帧最终可见枪口。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Ammo/DemoAmmoVisualStyle.h"
#include "DemoProjectileTracer.generated.h"

class ADemoWeaponBase;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UFXSystemComponent;
class UAudioComponent;
struct FDemoProjectileConfig;



/** World拥有的纯表现亮条，无碰撞/伤害/武器引用；实体弹立即销毁后仍可短暂淡出，避免短弹道完全没有渲染帧。 */
UCLASS(NotBlueprintable, Transient)
class FPSDEMO_API ADemoProjectileTracer : public AActor
{
    GENERATED_BODY()
public:
    /** 创建沿X轴缩放的基础Cube；准备期间隐藏，不在构造时制造静止亮块。 */
    ADemoProjectileTracer();
    /** Config为枪型，Origin是本段真实起点cm，Style是冻结弹种副本；bRicochet仅增强宽度/核心/淡出，失败返回false。 */
    bool Initialize(const FDemoProjectileConfig& Config, const FVector& Origin, const FDemoAmmoVisualStyle& Style = FDemoAmmoVisualStyle(), bool bRicochet = false);
    /** Position为真实扫掠后的球心，TravelledDistance是累计厘米；只画真实后方段，不提前描出剩余射程。 */
    void UpdateTravel(const FVector& Position, float TravelledDistance);
    /** 已确认的局部视觉段；仅画Start到End这段，避免FP有界曲线被长条跨弯连接，不改变Range。 */
    void UpdateSegment(const FVector& Start,const FVector& End);
    /** 将短闪附到本地可见Gun的校准Socket。PhysicalOrigin和Radius用于遮挡复核，MuzzleScale为本人比例，Length为已受路程与准星留白约束的世界厘米；失败回退世界段。 */
    bool BindLaunchDisplay(ADemoWeaponBase* Weapon,UPrimitiveComponent* Gun,const FVector& PhysicalOrigin,float Radius,float MuzzleScale,float Length);
    /** 更新本地横截面尺寸0<s<=1；长轴仍由UpdateTravel/Segment的已映射端点决定，不累乘Transform。 */
    void SetCrossSectionScale(float Scale);
    /** 冻结末端开始独立淡出；FadeOverride<0保留，非负仅延长至夹值0..1秒，0不缩短；不重置已累计淡出，没移动过直接销毁。 */
    void Finish(float FadeOverride = -1.f);
    /** World可空；在真实Point厘米朝Direction生成目录接触短闪或枪型弹射短闪，Dedicated/空资源返回空。Actor拥有FX/音并统一清理。 */
    static ADemoProjectileTracer* SpawnContact(UWorld* World, const FDemoProjectileConfig& Config, const FDemoAmmoVisualStyle& Style, const FVector& Point, const FVector& Direction, bool bRicochet);
    /** 本地权威战斗会话结束时立即销毁独立淡出表现，客户端由阶段复制清理。 */
    void CancelForSession();
    /** World借用，可空；Flow清场/旅行时立即销毁所有活动与淡出亮条，不等待表现寿命。 */
    static void CancelAll(UWorld* World);
    /** 生命周期查询供原弹EndPlay决定是否保留已经开始的独立淡出，不提供伤害权限。 */
    bool IsFinishing() const;
    /** Finish后常规段按World秒淡出，出膛短窗跟随枪管并复核遮挡；火光独立挂接。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 生命周期兜底回调；即使原弹异常丢失也最终释放纯表现Actor。 */
    virtual void LifeSpanExpired() override;
    /** EndPlayReason为地图/清场/寿命原因；只清除本表现引用，不访问原弹或武器。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(VisibleAnywhere, Category="Tracer") TObjectPtr<UStaticMeshComponent> Mesh; // 本Actor根，100cm引擎立方体，无碰撞无投影。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material; // 本实例MID，弹种色/图案/核心/淡出，不改变共享资产。
    UPROPERTY(Transient) TObjectPtr<UFXSystemComponent> ContactEffect; // 可空短闪组件，本Actor拥有，无复制/碰撞；EndPlay立即撤销粒子。
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ContactSound; // 可空弹射短音，本Actor拥有，清场停止，不发送伤害事件。
    FVector Origin = FVector::ZeroVector; // 发射起点世界cm，约束亮条不能延伸到枪口后。
    TWeakObjectPtr<ADemoWeaponBase> LaunchWeapon; // 仅首段首个短显示窗口核枪口，不驱动飞行末端或其他段。
    FVector PhysicalMuzzle = FVector::ZeroVector; // 仅局部遮挡比较，来自本弹Launch，世界cm。
    float MuzzleQueryRadius = 1.f; // 沿用本弹安全半径cm，不写入物理。
    bool bMuzzleDisplay = false; // 此Actor只承载出膛首段，窗满即撤；其余飞行/弹射段独立淡出。
    FVector LastPosition = FVector::ZeroVector; // 最近实际球心位置，更新方向用，不跟踪敌人。
    float Width = 3.f; // 冻结的视觉宽度cm，与伤害球无关。
    float CrossSectionScale = 1.f; // 本机FP尺寸归并进度，仅乘Width；不复制、不影响长度/端点。
    float MaximumLength = 120.f; // 冻结的最大亮条cm。
    float FadeSeconds = .06f; // 冻结的淡出World秒，暂停随World冻结。
    float FadeElapsed = 0.f; // Finish后累计World秒。
    float LastOpacity = 1.f; // 单调淡出上界；迟到弹射延长入射段时不能反向增亮或重闪，不复制。
    uint64 FinishFrame = 0; // 游戏帧序号；完成帧及下一帧不推进淡出，避免低帧率首Tick把短尾迹直接删掉。
    bool bInitialized = false; // Initialize只接受一次，防复用改变既有弹道表现。
    bool bHasTravel = false; // 必须真实移动后才允许显现/保留。
    bool bFinishing = false; // Finish幂等闩锁，淡出期间不再接收路径更新。

    float ViewWidthCap = TNumericLimits<float>::Max(); // 2026-11-19 VIEW-SAFE：按本机镜头距离得到的横截面上限cm，SetCrossSectionScale也不得超过；不复制。

    /**
     * 2026-11-19 VIEW-SAFE（CS2式细亮线）：把Start→End段摆放为Cube。调用时机：UpdateTravel/UpdateSegment每次更新。
     * 本机有相机时：距镜头<NearCm的近端被裁掉（FP枪缩放后枪口离眼仅十几厘米，透视会把亮条放大成楔子/方块）；
     * 横截面再按“段上离镜头最近点距离×最大视角”封顶，使近处不超过约5px。返回false表示整段过近已隐藏。
     * 纯表现，不改Origin/路径/伤害；Dedicated无相机时按原宽度。
     */
    bool PlaceSegment(FVector Start, const FVector& End);
    /** 工厂生成后调用一次；Point/Direction已经工厂验证，资源为空返回false；本Actor承接Session清理与固定0.22秒上限。 */
    bool InitializeContact(const FDemoProjectileConfig& Config, const FDemoAmmoVisualStyle& Style, const FVector& Point, const FVector& Direction, bool bRicochet);

};
