// 职责：玩家受击的权威值回执与本机视图协议；只描述已发生损血，不授权伤害、不进存档。
#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "DemoPlayerDamageTypes.generated.h"
class ADemoCharacter;
UENUM() enum class EDemoPlayerDamageKind : uint8 { Unknown,Projectile,Melee,Charge,Area,Global,Periodic,Environment };
UENUM() enum class EDemoPlayerDamageDirection : uint8 { Unknown,Directional,Omni };

USTRUCT()
struct FDemoPlayerDamageCue
{
    GENERATED_BODY()
    UPROPERTY() uint32 Sequence=0; // 每个拥有者PC服务器单调序号，表现去重，不影响HP复制。
    UPROPERTY() uint32 AvatarGeneration=0; // 当前PC的Avatar代次，重生/换Pawn拒绝旧回执。
    UPROPERTY() uint32 PhaseRevision=0; // 冻结GS公开代次，跨关迟到不重播。
    UPROPERTY() float ServerTime=0.f; // 命中时World游戏秒，非墙钟。
    UPROPERTY() float HealthLost=0.f; // 当次实际写入Old/New产生的损失HP，非请求伤害。
    UPROPERTY() float HealthAfter=0.f; // 当次写入结果，后续治疗不能抹掉这次事实。
    UPROPERTY() float MaxHealth=1.f; // 命中时生命上限，强度分级基数。
    UPROPERTY() EDemoPlayerDamageKind Kind=EDemoPlayerDamageKind::Unknown;
    UPROPERTY() EDemoPlayerDamageDirection DirectionMode=EDemoPlayerDamageDirection::Unknown;
    UPROPERTY() FVector_NetQuantizeNormal WorldDirection=FVector::ZeroVector; // 朝向来袭方向，不追踪敌人位置。
    UPROPERTY() bool bFatal=false; // 原次写入HP归零；死亡界面优先于装饰效果。
    UPROPERTY() bool bRescued=false; // GuardHealth确实新触发保命，不从HP==1猜测。
    UPROPERTY() bool bEvaded=false; // 原攻击规则确认躲避，仅结果提示、不冒充HP损失。
};

USTRUCT()
struct FDemoPlayerDamageAvatar
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<ADemoCharacter> Pawn=nullptr; // 可靠Owner绑定快照；未映射时允许null，不据此猜测当前Pawn。
    UPROPERTY() uint32 Generation=0; // 同一可靠消息的代次，不能把乱序字段拼成新身份。
    UPROPERTY() bool bHasPawn=false; // 服务器确实绑定活人；true+null表示引用尚未映射，需显式握手补发。
};

/** 本机固定容量方向槽；UI只借用，不发送网络、不持有来源Actor。 */
struct FDemoPlayerDamageIndicator
{
    FVector WorldDirection=FVector::ZeroVector; // 真实来向，HUD按当前相机Yaw投影。
    float EndTime=0.f; // 本地World秒，暂停不推进。
    float Strength=0.f; // 0..1已衰减显示强度。
    float InitialStrength=0.f; // 服务器事件初始展示幅度，用于有限线性淡出。
    bool bOmni=false; // 环形/未知提示，不伪造方位。
};
/** 本机展示快照，血条真值仍由原ASC持有。 */
struct FDemoPlayerDamageView
{
    TArray<FDemoPlayerDamageIndicator> Indicators; // 最多4条，仅活动本机状态。
    float EdgeStrength=0.f; // 0..1已衰减外缘强度，已乘设备设置。
    float HealthTrail=0.f; // 残影HP，不写回ASC。
    float HealthTrailMax=1.f; // 残影绘制比例基数HP。
    float LowHealthAlpha=0.f; // 0..1危险状态。
    float RollDegrees=0.f; // 最终视觉Roll度，狙击/死亡/菜单归零。
    bool bCritical=false; // 低血状态带滞回。
    bool bSlowed=false; // 只读真实Slowed Tag。
    bool bVisible=false; // 当前本人可见战斗HUD，观战不可见。
    FName Outcome; // Rescue/Evaded，纯结果提示。
    float OutcomeEndTime=0.f; // 本地结果提示截止秒。
};
