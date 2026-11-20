// 职责：非敌人受伤目标的最小适配契约。使用真实子弹/近战碰撞提交权威请求；依赖现有CombatSession和GAS，不生成敌人奖励。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Engine/EngineTypes.h"
#include "GAS/Ammo/DemoAmmoEffectSnapshot.h"
#include "DemoDamageableTarget.generated.h"

class UAbilitySystemComponent;
class UDemoCombatSession;

/** 同步借用的伤害请求；对象指针不得缓存，DOT另复制必要数值且受本场Session撤销。 */
struct FPSDEMO_API FDemoTargetDamageRequest
{
    UDemoCombatSession* Session = nullptr; // 发起攻击时冻结的会话，目标必须与其完全一致。
    UAbilitySystemComponent* SourceASC = nullptr; // 发起者PlayerState持有，必须仍授权同一Avatar。
    AActor* SourceAvatar = nullptr; // 实际攻击玩家，本次同步调用借用。
    FGuid AttackId; // 整枪/挥击关联ID；目标不借此授予敌人弱点或击杀奖励。
    FName WeaponId; // 稳定武器目录ID，近战使用Melee；只用于追踪。
    float Damage = 0.f; // 基础直击HP，已含允许的源端增益；设施无部位倍率。
    FHitResult Hit; // 服务器真实碰撞结果；目标必须就是接收Actor。
    int32 AmmoType = 0; // 0普通/1火/2冰/3穿透；设施接受火DOT，免疫所有控制。
    FDemoAmmoEffectSnapshot Ammo; // 开火冻结的元素数值；近战不使用。
    bool bApplyElement = false; // 同枪同目标只首次实际扣血后提交元素，不按散弹数量叠层。
    bool bMelee = false; // 服务器近战适配器显式设置；仅选择表现池预算，不能通过此字段绕过伤害门禁。
    bool bRedesignedFeedback = false; // 发射时冻结的候选音画选择；近战默认false，不修改设施伤害或奖励。
};

/** 不依赖Actor存活的伤害回执；击碎设施不等于敌人死亡，调用方不得据此发币或叠天赋。 */
struct FPSDEMO_API FDemoTargetDamageResult
{
    bool bAccepted = false; // 真实几何/身份有效，不等于一定产生HP损失。
    float ActualDamage = 0.f; // GE前后实际HP差，过量伤害已钳制。
    bool bDestroyed = false; // 本次GE首次击碎目标；只供目标系统统计。
};

/** 仅原生实现，蓝图可配置设施Actor的数据但不能改写服务器伤害门禁。 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UDemoDamageableTarget : public UInterface
{
    GENERATED_BODY()
};

class FPSDEMO_API IDemoDamageableTarget
{
    GENERATED_BODY()
public:
    /** Request为同栈借用的服务器真实命中；实现须验证会话/Active并通过GAS结算，返回值不授予敌人奖励。 */
    virtual FDemoTargetDamageResult ApplyTargetDamage(const FDemoTargetDamageRequest& Request) = 0;
};
