#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoProjectileTracer.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FDemoProjectileConfig;

/** World拥有的纯表现亮条，无碰撞/伤害/武器引用；实体弹立即销毁后仍可短暂淡出，避免短弹道完全没有渲染帧。 */
UCLASS(NotBlueprintable, Transient)
class FPSDEMO_API ADemoProjectileTracer : public AActor
{
    GENERATED_BODY()
public:
    /** 创建沿X轴缩放的基础Cube；准备期间隐藏，不在构造时制造静止亮块。 */
    ADemoProjectileTracer();
    /** Config复制本次表现数值/材质，Origin是实际安全枪口世界厘米；无外部对象生命周期依赖，失败返回false。 */
    bool Initialize(const FDemoProjectileConfig& Config, const FVector& Origin);
    /** Position为真实扫掠后的球心，TravelledDistance是累计厘米；只画真实后方段，不提前描出剩余射程。 */
    void UpdateTravel(const FVector& Position, float TravelledDistance);
    /** 正常撞击/射程耗尽后调用一次，冻结末端并开始独立淡出；没移动过或淡出为0直接销毁。 */
    void Finish();
    /** World借用，可空；Flow清场/旅行时立即销毁所有活动与淡出亮条，不等待表现寿命。 */
    static void CancelAll(UWorld* World);
    /** 生命周期查询供原弹EndPlay决定是否保留已经开始的独立淡出，不提供伤害权限。 */
    bool IsFinishing() const;
    /** DeltaSeconds为World帧秒；仅Finish后启用Tick，线性驱动材质TracerOpacity，不做运动或查询。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 生命周期兜底回调；即使原弹异常丢失也最终释放纯表现Actor。 */
    virtual void LifeSpanExpired() override;
    /** EndPlayReason为地图/清场/寿命原因；只清除本表现引用，不访问原弹或武器。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(VisibleAnywhere, Category="Tracer") TObjectPtr<UStaticMeshComponent> Mesh; // 本Actor根，100cm引擎立方体，无碰撞无投影。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material; // 本实例材质，仅修改TracerOpacity，不改变共享资产。
    FVector Origin = FVector::ZeroVector; // 发射起点世界cm，约束亮条不能延伸到枪口后。
    FVector LastPosition = FVector::ZeroVector; // 最近实际球心位置，更新方向用，不跟踪敌人。
    float Width = 3.f; // 冻结的视觉宽度cm，与伤害球无关。
    float MaximumLength = 120.f; // 冻结的最大亮条cm。
    float FadeSeconds = .06f; // 冻结的淡出World秒，暂停随World冻结。
    float FadeElapsed = 0.f; // Finish后累计World秒。
    uint64 FinishFrame = 0; // 游戏帧序号；完成帧及下一帧不推进淡出，避免低帧率首Tick把短尾迹直接删掉。
    bool bInitialized = false; // Initialize只接受一次，防复用改变既有弹道表现。
    bool bHasTravel = false; // 必须真实移动后才允许显现/保留。
    bool bFinishing = false; // Finish幂等闩锁，淡出期间不再接收路径更新。
};
