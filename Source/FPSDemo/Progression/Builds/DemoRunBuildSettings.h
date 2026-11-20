// 职责：三核心的集中可调数值。使用：项目设置/Game配置覆盖默认值，服务器冻结发射值；依赖DeveloperSettings，不改变原15天赋目录。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DemoRunBuildSettings.generated.h"
UCLASS(Config=Game,DefaultConfig,meta=(DisplayName="Demo Run Builds"))
class FPSDEMO_API UDemoRunBuildSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="1",ClampMax="10")) int32 IceHits=3; // 同玩家/目标不同ShotId直击阈值。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0.1",ClampMax="10")) float IceChainSeconds=3.f; // 计数续接间隔秒。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0.1",ClampMax="3")) float FreezeSeconds=.8f; // 小怪短冻结秒，不延长已有冻结。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="1",ClampMax="30")) float FreezeCooldown=4.f; // 目标共享冻结/易碎门禁秒。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0",ClampMax="2")) float BossBonus=.15f; // 下一次对该Boss直击增伤比例。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0.1",ClampMax="10")) float BossMarkSeconds=2.f; // 发起者独立Boss标记秒。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0.1",ClampMax="10")) float ShatterCooldown=3.f; // 基础破冰个人冷却秒。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0.1",ClampMax="10")) float ShatterCooldownA=2.5f; // A分支冷却秒。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="1",ClampMax="600")) float PushDistance=450.f; // 基础权威击退厘米。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="1",ClampMax="600")) float PushDistanceA=550.f; // A分支击退厘米。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0.05",ClampMax="2")) float PushSeconds=.3f; // 碰撞/终点至多一次爆裂。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="1",ClampMax="1500")) float BurstRadius=320.f; // 基础范围厘米。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="1",ClampMax="1500")) float BurstRadiusB=240.f; // B分支范围厘米。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0",ClampMax="5")) float BurstRatio=.6f; // 近战源端基数比例，无受害者部位倍率。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0",ClampMax="5")) float BurstRatioB=1.f; // B分支比例。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="1",ClampMax="6")) int32 BurstTargets=6; // 范围硬上限6，距离/唯一ID排序。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0",ClampMax="0.9")) float BurstSlow=.2f; // C分支小怪减速比例，不叠层。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0.1",ClampMax="10")) float BurstSlowSeconds=1.5f; // C分支减速秒。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0",ClampMax="100")) float BurstHeal=5.f; // D分支成功命中其他敌人恢复HP。
    UPROPERTY(Config,EditAnywhere,Category="Ice",meta=(ClampMin="0.1",ClampMax="30")) float BurstHealCooldown=6.f; // D分支个人恢复CD秒。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="1",ClampMax="2000")) float DashMinimumDistance=250.f; // Combat冲刺实际平面净位移厘米。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0",ClampMax="3")) float AssaultBonus=.3f; // 基础直击增伤。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0",ClampMax="3")) float AssaultBonusA=.45f; // A分支单枪增伤。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0",ClampMax="3")) float AssaultBonusB=.2f; // B分支两枪增伤。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0.1",ClampMax="10")) float AssaultSeconds=3.f; // 基础/B窗口秒（放宽以兼容拉栓狙与双管）。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0.1",ClampMax="10")) float AssaultSecondsA=1.8f; // A窗口秒（允许重武器冲刺后重新瞄准）。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0",ClampMax="1")) float AssaultMoveBonus=.15f; // C分支每冲刺一次击杀移速加法。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0.1",ClampMax="10")) float AssaultMoveSeconds=1.5f; // C移速秒。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0.1",ClampMax="1")) float AssaultSpreadScale=.65f; // D散布倍率。
    UPROPERTY(Config,EditAnywhere,Category="Assault",meta=(ClampMin="0.1",ClampMax="1")) float AssaultRecoilScale=.8f; // D视觉后坐倍率。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="1",ClampMax="10")) int32 PrecisionHits=3; // 首次有效敌人弱点命中阈值。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0.1",ClampMax="10")) float PrecisionSeconds=4.5f; // 基础层数超时秒。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0.1",ClampMax="10")) float PrecisionSecondsA=6.5f; // A层数超时秒。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0.1",ClampMax="10")) float PrecisionSecondsB=4.f; // B层数超时秒（兼容慢射速狙击续链）。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0.1",ClampMax="10")) float PrecisionChargeSeconds=3.5f; // 锁定射击消费窗口秒（覆盖拉栓后重新开镜）。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0",ClampMax="3")) float PrecisionBonus=.5f; // 基础首次有效弱点增伤。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0",ClampMax="3")) float PrecisionBonusA=.4f; // A增伤比例。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0",ClampMax="3")) float PrecisionBonusB=.7f; // B增伤比例。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0",ClampMax="3")) float ExposeBonus=.1f; // D公开标记下一名合法射手直击增伤。
    UPROPERTY(Config,EditAnywhere,Category="Precision",meta=(ClampMin="0.1",ClampMax="10")) float ExposeSeconds=2.f; // D公开标记有效秒。
    UPROPERTY(Config,EditAnywhere,Category="Adaptation",meta=(ClampMin="0.2",ClampMax="5")) float SlowWeaponIntervalSeconds=1.f; // 慢射速重武器判定间隔阈值秒（如狙击1.33s），弱点计2层。
    UPROPERTY(Config,EditAnywhere,Category="Adaptation",meta=(ClampMin="2",ClampMax="32")) int32 IcePelletDoubleThreshold=4; // 冰裂单枪同目标弹丸命中达到此值共积2层。
    UPROPERTY(Config,EditAnywhere,Category="Adaptation",meta=(ClampMin="2",ClampMax="32")) int32 IcePelletFullThreshold=6; // 冰裂单枪同目标弹丸命中达到此值共积3层（贴脸瞬冻）。
    /** 编辑器配置也可能手写非法NaN；开战授予前检查全部有限数值和硬边界。 */
    bool Validate() const;
};
