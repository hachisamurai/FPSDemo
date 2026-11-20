// 职责：蓝图弹体运动/枪型外观/可空弹射反馈配置。使用：权威准备校验；弹种取独立目录，所有视觉字段不改变速度、球半径或伤害。
#pragma once

#include "CoreMinimal.h"
#include "DemoProjectileConfig.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;
class UFXSystemAsset;

/** 子弹蓝图的运动/表现定义；伤害和射程由武器快照提供，不形成第二份平衡数据。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoProjectileConfig
{
    GENERATED_BODY()

    // CDO硬引用外观，随子弹类Cook；允许为空，碰撞不依赖外观包围盒。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TObjectPtr<UStaticMesh> Mesh;
    // 各材质槽的可空覆盖，索引对应Mesh槽；不修改共享资源。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;
    // 外观相对根球变换，厘米/度；仅改变Mesh，不能改变实际碰撞半径。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") FTransform VisualTransform = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(.02f));
    // 本人独立FP向世界弹道归并的真实飞长cm，10..500；只在本地策略许可时使用，旧BP自动取默认值。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="10", ClampMax="500")) float OwnerMuzzleBlendDistance = 120.f;
    // 初始恒定世界速度cm/s，100..200000；首版无重力、无追踪。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="100", ClampMax="200000")) float InitialSpeed = 30000.f;
    // 扫掠球半径cm，0.1..20；枪口安全检查必须使用同一数值。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="0.1", ClampMax="20")) float CollisionRadius = 1.f;
    // 激活后的兜底World寿命秒，0.05..60；还受武器射程限制，暂停随World冻结。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin="0.05", ClampMax="60")) float MaxLifeSeconds = 8.f;
    // 可空Niagara拖尾；附本地CosmeticRoot继承尺寸一次。Local-space可随父变换，world-space粒子/ribbon须作者显式使用缩放，组件scale不保证其尺寸；结束撤销。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") TObjectPtr<UNiagaraSystem> TrailEffect;
    // 可空撞墙Niagara；只有真实非敌人阻挡命中才播放，寿命结束不伪造撞击。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") TObjectPtr<UNiagaraSystem> WallImpactEffect;
    // 可空空间音；音频资源自身配置衰减，敌人肉体音仍由敌人健康回调处理。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") TObjectPtr<USoundBase> WallImpactSound;

    // 默认开启独立无碰撞亮条；只显示真实已经扫掠的路径，不改变弹速、弹径或命中时间。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer") bool bEnableTracer = true;
    // 可空亮条材质；推荐Unlit/Additive且Scalar TracerOpacity控制淡出。空值使用引擎基础材质，不阻止射击。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer") TObjectPtr<UMaterialInterface> TracerMaterial;
    // 亮条横截面宽度cm，0.1..50；仅表现，和CollisionRadius完全独立。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(ClampMin="0.1", ClampMax="50")) float TracerWidth = 3.f;
    // 亮条最大长度cm，1..1000；实际长度还受已飞过的距离约束，前端不超过当前球心。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(ClampMin="1", ClampMax="1000")) float TracerLength = 120.f;
    // 原弹结束后独立表现的淡出World秒，0..1；0立即清除，默认0.06让短于一帧的命中也留下可见轨迹。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tracer", meta=(ClampMin="0", ClampMax="1")) float TracerFadeSeconds = .06f;

    // 可空Niagara/Cascade短闪，成功弹射真实接触点播放；实例由Tracer拥有，空值只保留分段亮条，不阻断射击。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ricochet") TObjectPtr<UFXSystemAsset> RicochetEffect = nullptr;
    // 可空空间短音，不增加Gameplay事件；随短闪Actor清理，默认静音，试听后再配置。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ricochet") TObjectPtr<USoundBase> RicochetSound = nullptr;
    // 出射亮条横截面倍率0.1..4，最终宽度不超过50cm；独立于CollisionRadius，旧BP以1.5增强转折可读性。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ricochet", meta=(ClampMin="0.1",ClampMax="4")) float RicochetTracerWidthScale = 1.5f;
    // 出射亮条核心亮度倍率1..8，只乘HDR强度不换弹种色相；不影响伤害倍率。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ricochet", meta=(ClampMin="1",ClampMax="8")) float RicochetCoreBoost = 1.5f;
    // 出射段独立淡出World秒0..1，0直接销毁；本地/远端共享值，非网络状态。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ricochet", meta=(ClampMin="0",ClampMax="1")) float RicochetTracerFadeSeconds = .24f;
    // 成功弹射时入射段独立淡出World秒0..1，保留折线；不延长原弹Gameplay寿命。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ricochet", meta=(ClampMin="0",ClampMax="1")) float IncomingTracerFadeOnRicochet = .2f;

    /** Error输出非法字段；每次准备及装备预检调用，蓝图编辑器Clamp不能替代运行时验证。 */
    bool Validate(FString& Error) const;
};

/** 单颗弹丸准备数据；不保存瞄准目标，发射后不会自动追踪。 */
USTRUCT()
struct FPSDEMO_API FDemoProjectileLaunchData
{
    GENERATED_BODY()
    // 同一ShotContext内唯一的0..63序号（含双弹），只用于关联和诊断，不作为跨枪唯一ID。
    UPROPERTY() int32 PelletIndex = INDEX_NONE;
    // 准备位置的世界厘米坐标；用于初始化累计路径，不重新查询枪口。
    UPROPERTY() FVector Origin = FVector::ZeroVector;
    // 从安全枪口到准星瞄准点的世界单位向量；无目标Actor引用。
    UPROPERTY() FVector Direction = FVector::ForwardVector;
};
