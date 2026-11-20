// 职责：命中血雾、敌人弹簧倾斜及本地击杀冲击的只读表现配置。使用：World/组件读取CDO；资源仅构造预载，依赖Editor创建材质，不改变伤害、AI、音频或存档。
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DemoHitFxSettings.generated.h"

/** 本地命中视觉参数；Config=Game供DefaultGame.ini调参，不复制或持久化玩家进度。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Demo Hit FX"))
class FPSDEMO_API UDemoHitFxSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** CDO/实例构造时硬预载固定Sphere与血雾材质；缺材质保持空引用，由World池一次告警后禁用血雾，命中路径不加载。 */
    UDemoHitFxSettings();

    // 本机血肉命中血雾开关；Boss/设施仍由表面事件排除，不影响音频。
    UPROPERTY(Config, EditAnywhere, Category="Enabled") bool bFleshImpactMist = true;
    // 本机存活小怪弹簧倾斜开关；Boss、专服与近战击退不执行。
    UPROPERTY(Config, EditAnywhere, Category="Enabled") bool bHitReact = true;
    // 本机小怪尸体布娃娃/运动学回退开关；不改变服务器死亡结算。
    UPROPERTY(Config, EditAnywhere, Category="Enabled") bool bKillImpulse = true;

    // CDO强引用的Editor作者材质，固定/Game/VFX/Impacts/M_FleshMist；随依赖进入Cook，空引用只禁用血雾。
    UPROPERTY(VisibleAnywhere, Category="Mist|Resources") TObjectPtr<class UMaterialInterface> MistMaterial;
    // CDO强引用的引擎100cm平面；所有无碰撞雾片共用，透明区域呈现喷丝和碎滴之间的空隙。
    UPROPERTY(VisibleAnywhere, Category="Mist|Resources") TObjectPtr<class UStaticMesh> MistMesh;
    // 本World独立血雾簇上限4..32；不占用金属火花/近战保留槽，池满丢弃。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="4", ClampMax="32")) int32 MistSlots = 16;
    // 每簇固定团数预算1..16；总实例MistSlots*MistPuffsMax在World初始化后不增长。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="1", ClampMax="16")) int32 MistPuffsMax = 9;
    // 血雾最大可见距离cm；仅本地观众距离裁剪，不影响伤害，默认与现有火花2500cm一致。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="100", ClampMax="10000")) float MaxDistance = 2500.f;
    // 身体/头部近距基础团数；LOD和重武器加成后仍受MistPuffsMax限制。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="1", ClampMax="16")) int32 BodyPuffs = 6;
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="1", ClampMax="16")) int32 HeadPuffs = 9;
    // 重武器近距团数额外加成0..4；封顶后不会为单次命中额外分配实例。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="0", ClampMax="4")) int32 HeavyExtraPuffs = 1;
    // 超过最大距离此比例进入中距LOD，0..1；远距门限必须大于该值。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="0", ClampMax="1")) float MistLodNearFraction = .32f;
    // 超过最大距离此比例进入远距LOD，0..1；超过MaxDistance完全裁剪。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="0", ClampMax="1")) float MistLodFarFraction = .64f;
    // 中距/远距最终团数1..16；不再叠加重武器团数，仍受MistPuffsMax限制。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="1", ClampMax="16")) int32 MistMidPuffs = 4;
    UPROPERTY(Config, EditAnywhere, Category="Mist|Budget", meta=(ClampMin="1", ClampMax="16")) int32 MistFarPuffs = 2;

    // 身体起始半径随机范围cm，X最小/Y最大；Sphere基准直径100cm，缩放使用直径/100。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Shape") FVector2D BodyStartRadius = FVector2D(4.f, 6.f);
    // 头部起始半径随机范围cm，X最小/Y最大。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Shape") FVector2D HeadStartRadius = FVector2D(5.f, 7.f);
    // 身体结束半径随机范围cm，X最小/Y最大；生命周期内按二次缓出增长。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Shape") FVector2D BodyEndRadius = FVector2D(8.f, 13.f);
    // 头部结束半径随机范围cm，X最小/Y最大。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Shape") FVector2D HeadEndRadius = FVector2D(10.f, 16.f);
    // 身体寿命随机范围秒，X最小/Y最大；无贴花、无地面血迹。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Motion") FVector2D BodyLife = FVector2D(.24f, .31f);
    // 头部寿命随机范围秒，X最小/Y最大。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Motion") FVector2D HeadLife = FVector2D(.27f, .35f);
    // 身体初速度随机范围cm/s，X最小/Y最大；方向为接触面外法线圆锥。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Motion") FVector2D BodySpeed = FVector2D(155.f, 275.f);
    // 头部初速度随机范围cm/s，X最小/Y最大。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Motion") FVector2D HeadSpeed = FVector2D(185.f, 325.f);
    // 身体/头部外喷圆锥半角度，0..89；不做碰撞或骨骼物理。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Motion", meta=(ClampMin="0", ClampMax="89")) float BodyConeDeg = 55.f;
    UPROPERTY(Config, EditAnywhere, Category="Mist|Motion", meta=(ClampMin="0", ClampMax="89")) float HeadConeDeg = 65.f;
    // 重武器起始/结束半径倍率，无量纲正数，只影响本地血雾。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Heavy", meta=(ClampMin="0.1", ClampMax="3")) float HeavyStartScale = 1.15f;
    UPROPERTY(Config, EditAnywhere, Category="Mist|Heavy", meta=(ClampMin="0.1", ClampMax="3")) float HeavyEndScale = 1.25f;
    // 重武器寿命额外秒数，0..0.1。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Heavy", meta=(ClampMin="0", ClampMax="0.1")) float HeavyLifeBonus = .02f;
    // 重武器初速度倍率，无量纲正数。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Heavy", meta=(ClampMin="0.1", ClampMax="3")) float HeavySpeedScale = 1.2f;
    // 血雾世界Z加速度cm/s²，负值下落；仅解析表现，不参与引擎物理。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Motion", meta=(ClampMin="-1000", ClampMax="0")) float MistGravity = -90.f;
    // 命中点沿外法线的起点偏移cm，避免团体埋进模型。
    UPROPERTY(Config, EditAnywhere, Category="Mist|Motion", meta=(ClampMin="0", ClampMax="10")) float MistNormalOffset = 1.5f;

    // 二阶弹簧固有频率Hz，1..30；阻尼比0.1..2，默认欠阻尼回弹，仅处理组件相对变换。
    UPROPERTY(Config, EditAnywhere, Category="React|Spring", meta=(ClampMin="1", ClampMax="30")) float ReactSpringHz = 9.f;
    UPROPERTY(Config, EditAnywhere, Category="React|Spring", meta=(ClampMin="0.1", ClampMax="2")) float ReactDamping = .55f;
    // 轻武器身体/头部基础倾斜峰值度数；重武器用ReactHeavyDeg，最终受ReactMaxDeg封顶。
    UPROPERTY(Config, EditAnywhere, Category="React|Kick", meta=(ClampMin="0", ClampMax="12")) float ReactBodyDeg = 4.f;
    UPROPERTY(Config, EditAnywhere, Category="React|Kick", meta=(ClampMin="0", ClampMax="12")) float ReactHeadDeg = 7.f;
    // 头部额外后仰度数；与主倾斜合成后仍受总角度封顶。
    UPROPERTY(Config, EditAnywhere, Category="React|Kick", meta=(ClampMin="0", ClampMax="12")) float ReactHeadBackDeg = 3.f;
    // 霰弹/狙击基础倾斜峰值度数；不授予Gameplay硬直或修改AI。
    UPROPERTY(Config, EditAnywhere, Category="React|Kick", meta=(ClampMin="0", ClampMax="12")) float ReactHeavyDeg = 9.f;
    // 身体/头部轻武器后退位移cm；重武器另配，均仅本地网格偏移。
    UPROPERTY(Config, EditAnywhere, Category="React|Kick", meta=(ClampMin="0", ClampMax="5")) float ReactBodyOffset = 1.5f;
    UPROPERTY(Config, EditAnywhere, Category="React|Kick", meta=(ClampMin="0", ClampMax="5")) float ReactHeavyOffset = 4.f;
    // 连续命中总倾斜硬上限度数/总位移硬上限cm；存活部位查询会随网格轻微变化，禁用骨骼模拟。
    UPROPERTY(Config, EditAnywhere, Category="React|Spring", meta=(ClampMin="0", ClampMax="12")) float ReactMaxDeg = 12.f;
    UPROPERTY(Config, EditAnywhere, Category="React|Spring", meta=(ClampMin="0", ClampMax="5")) float ReactMaxOffset = 5.f;
    // 同敌人快速命中门限秒；门限内按ReactRapidScale缩减新冲量，不改变伤害/RPC。
    UPROPERTY(Config, EditAnywhere, Category="React|Spring", meta=(ClampMin="0", ClampMax="0.25")) float ReactMinInterval = .05f;
    // 快速命中新冲量比例0..1，默认40%；已有弹簧状态继续演进。
    UPROPERTY(Config, EditAnywhere, Category="React|Spring", meta=(ClampMin="0", ClampMax="1")) float ReactRapidScale = .4f;
    // 重武器沿原服务器Montage路径尝试受击动作的最短间隔秒；不在客户端激活GA。
    UPROPERTY(Config, EditAnywhere, Category="React|Kick", meta=(ClampMin="0", ClampMax="2")) float HeavyStaggerCooldown = .35f;

    // 尸体身体/头部/重武器速度变化量cm/s；只对本地死亡网格，重武器优先于头部。
    UPROPERTY(Config, EditAnywhere, Category="Kill|Impulse", meta=(ClampMin="0", ClampMax="1500")) float KillBodySpeed = 350.f;
    UPROPERTY(Config, EditAnywhere, Category="Kill|Impulse", meta=(ClampMin="0", ClampMax="1500")) float KillHeadSpeed = 450.f;
    UPROPERTY(Config, EditAnywhere, Category="Kill|Impulse", meta=(ClampMin="0", ClampMax="1500")) float KillHeavySpeed = 650.f;
    // 子弹单位方向叠加的世界向上分量，无量纲，归一化后施加速度变化。
    UPROPERTY(Config, EditAnywhere, Category="Kill|Impulse", meta=(ClampMin="0", ClampMax="1")) float KillUpBias = .25f;
    // 权威尸体回收寿命秒，1.65..3；击败事件即时移出存活集合，展示寿命不延迟奖励/清关。
    UPROPERTY(Config, EditAnywhere, Category="Kill|Lifetime", meta=(ClampMin="1.65", ClampMax="3")) float CorpseLifeSeconds = 2.2f;
    // 初始物理验证允许最大位移cm；NaN或超限立即禁用模拟并转运动学回退。
    UPROPERTY(Config, EditAnywhere, Category="Kill|Fallback", meta=(ClampMin="50", ClampMax="300")) float KillMaxTravel = 300.f;
    // 运动学推倒演进秒数；只改本地尸体网格，碰撞不进入武器/玩家查询。
    UPROPERTY(Config, EditAnywhere, Category="Kill|Fallback", meta=(ClampMin="0.05", ClampMax="1")) float FallbackSeconds = .25f;
    // 运动学身体/头部轻武器与重武器最大平移cm；不改变Actor权威位置。
    UPROPERTY(Config, EditAnywhere, Category="Kill|Fallback", meta=(ClampMin="0", ClampMax="60")) float FallbackBodyDistance = 25.f;
    UPROPERTY(Config, EditAnywhere, Category="Kill|Fallback", meta=(ClampMin="0", ClampMax="60")) float FallbackHeavyDistance = 60.f;
    // 运动学轻/重武器最终倾斜度数；无布娃娃时保持原死亡动画并叠加此组件级推倒。
    UPROPERTY(Config, EditAnywhere, Category="Kill|Fallback", meta=(ClampMin="0", ClampMax="25")) float FallbackBodyDeg = 15.f;
    UPROPERTY(Config, EditAnywhere, Category="Kill|Fallback", meta=(ClampMin="0", ClampMax="25")) float FallbackHeavyDeg = 25.f;
};
