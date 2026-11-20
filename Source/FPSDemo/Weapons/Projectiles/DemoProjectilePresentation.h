// 职责：实体弹冻结弹种/接触网络事实、单弹表现去重和有界FP/分段数学。使用：Weapon/Projectile共享；不复制ShotContext、不授权运动或伤害。 本人首段使用发射帧最终可见枪口。
#pragma once
#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Weapons/Ammo/DemoAmmoVisualStyle.h"
#include "DemoProjectilePresentation.generated.h"
class APawn;
class ADemoProjectileBase;
class ADemoProjectileTracer;

/** 提交后冻结的单弹表现描述；Actor初始复制和真实段RPC使用同一身份，不进入存档。 */
USTRUCT()
struct FDemoProjectilePresentationLaunch
{
    GENERATED_BODY()
    UPROPERTY() FGuid ShotId; // 服务器已有整枪GUID，不接受客户端生成的伤害凭据。
    UPROPERTY() int32 Pellet = INDEX_NONE; // 同枪弹丸0..63；与ShotId共同去重。
    UPROPERTY() TObjectPtr<APawn> Shooter = nullptr; // 发射Avatar只读网络引用，晚到时暂不显示活弹，不改Owner。
    UPROPERTY() FVector_NetQuantize Origin = FVector::ZeroVector; // 安全扫掠后的真实根球起点cm，不能重新读Socket代替。
    UPROPERTY() FVector_NetQuantizeNormal Direction = FVector::ForwardVector; // 冻结世界初始方向，不用于客户端碰撞。
    UPROPERTY() uint32 Phase = 0; // 发射阶段代数，拒绝旧World/旧关表现。
    UPROPERTY() int32 Level = 0; // 发射关号，不由当前装备补签。
    UPROPERTY() float ServerTime = 0.f; // 成功提交的服务器World秒，判断晚到，非Wall-clock。
    UPROPERTY() bool bScoped = false; // 发射时镜内策略；自动退镜不能重开在飞弹的FP桥。
    UPROPERTY() uint8 AmmoType = 0; // 发射ShotContext冻结的0普通/1火焰/2冰冻/3穿透；纯表现网络字段，不读当前装备、不入存档。
};

/** 实际飞过的有序段；只给表现使用，路径/距离来自权威RecordTravel。 */
USTRUCT()
struct FDemoProjectilePresentationSample
{
    GENERATED_BODY()
    UPROPERTY() FDemoProjectilePresentationLaunch Launch; // 短命Actor未建立通道也有完整只读身份。
    UPROPERTY() uint32 Sequence = 0; // 单弹服务器单调序号；0非法，旧/重复包不重放。
    UPROPERTY() FVector_NetQuantize Start = FVector::ZeroVector; // 已发上段末端cm。
    UPROPERTY() FVector_NetQuantize End = FVector::ZeroVector; // 本段真实球心/接触端点cm，绝不为准星预测。
    UPROPERTY() float StartDistance = 0.f; // 实际累计路径起始cm，含穿透/折线。
    UPROPERTY() float EndDistance = 0.f; // 实际累计路径末端cm，只增不减。
    UPROPERTY() float ServerTime = 0.f; // 本次发布World秒，沿用原0.5秒时效门。
    UPROPERTY() bool bFirst = false; // 服务器首次发送标记；首包丢失不把后续包当首段。
    UPROPERTY() bool bBoundary = false; // 真实接触/折向边界立即收敛到世界弹道，不跨拐角重接枪口。
    UPROPERTY() bool bTerminal = false; // 命中/寿命结束终态，迟到活弹不能重开桥接。
    UPROPERTY() bool bRicochetSegment = false; // 成功弹射后的每个段/终包持续携带，首出射包丢失时后包仍能补显一次火花。
    UPROPERTY() FVector_NetQuantize RicochetPoint = FVector::ZeroVector; // 真实Hit.ImpactPoint世界cm，也是出射亮条视觉Origin，不改变根球位置。
    UPROPERTY() FVector_NetQuantizeNormal RicochetDirection = FVector::ForwardVector; // 成功弹射时新Velocity单位方向，后包不从最后一段推测方向。
    UPROPERTY() bool bEnvironmentImpact = false; // 真实非Enemy阻挡/设施表面接触，仅表现事实，不表示扣血成功；终包重复补丢包。
    UPROPERTY() FVector_NetQuantize EnvironmentPoint = FVector::ZeroVector; // 环境表面真实接触点世界cm，不由客户端射线重建。
    UPROPERTY() FVector_NetQuantizeNormal EnvironmentNormal = FVector::UpVector; // 真实表面单位法向，客户端只定向接触特效。
    UPROPERTY() bool bPiercingExit = false; // 首敌实际穿出已经确认；后续段/终包重复携带，只读表现，不影响穿透判定。
    UPROPERTY() FVector_NetQuantize PiercingExitPoint = FVector::ZeroVector; // 已飞过真实路径的首敌表面出口世界cm；查询失败时不伪造。
    UPROPERTY() FVector_NetQuantizeNormal PiercingExitDirection = FVector::ForwardVector; // 穿出时世界单位飞行方向，客户端无需碰撞查询。
};

/** 本World纯值键；不保活Actor，不与同枪其他弹丸互相覆盖。 */
struct FDemoProjectilePresentationKey
{
    FGuid Shot; // 冻结整枪身份。
    int32 Pellet = INDEX_NONE; // 本枪单弹序号。
    bool operator==(const FDemoProjectilePresentationKey& Other) const; // 同步容器比较，无UObject依赖。
    friend uint32 GetTypeHash(const FDemoProjectilePresentationKey& Key); // TMap键散列，不接受网络权限。
};

/** 武器有界本地表中的一次整枪视觉快照；不会由服务器复制本机FP坐标。 */
struct FDemoProjectileMuzzleReceipt
{
    FVector Origin = FVector::ZeroVector; // 发射时暂取FP枪口，最多在同一渲染帧相机完成后校准一次，随后固定世界cm。
    uint64 CaptureFrame = 0; // 本观察者游戏帧号；绝不借用后续帧的枪姿。
    float RenderScale = 1.f; // 同F0冻结的本人显示尺寸0<s<=1；仅本地cache，世界/Scope/外视点为1，不复制。
    float Created = 0.f; // 本地World秒，供有界过期清理。
    bool bAllowed = false; // 本人/配置/当前武器/Scope共同许可，一次冻结。
    /** 仅初建桥需要F0；1秒覆盖0.5秒合法launch窗口，不能按60秒弹寿命占住256枪缓存。 */
    bool IsExpired(float Now) const;
};

/** 单弹纯表现进度；终态和拒绝只收紧，旧包不能重开。 */
struct FDemoProjectileCosmeticState
{
    TWeakObjectPtr<ADemoProjectileBase> Actor; // 已到达的同身份活弹弱引用；网络边界立即同步本地显隐，不逐帧遍历World。
    TWeakObjectPtr<ADemoProjectileTracer> IncomingBoundaryTracer; // 最新真实边界的入射淡出条弱引用；弹射事实晚到时延长其淡出，不保活Actor。
    TWeakObjectPtr<ADemoProjectileTracer> LaunchTracer; // 附在枪管的出膛短闪，同帧最终遮挡可撤销，原弹可早已结束。
    TWeakObjectPtr<ADemoProjectileTracer> FollowTracer; // 2026-11-19 OWNER-FOLLOW远端本人：FP桥接期间复用的单条移动亮条，桥结束/边界/终态时Finish；弱引用不保活。
    FVector LaunchPhysicalOrigin = FVector::ZeroVector; // 服务器冻结的起点cm，供局部遮挡复核。
    float LaunchRadius = 1.f; // 此弹碰撞半径cm的只读副本，用于既有视觉安全查询。
    FVector Offset = FVector::ZeroVector; // F0-P0世界cm，仅本World渲染。
    float Created = 0.f; // 本World创建秒，60秒弹寿命外兜底清理。
    float InitialRenderScale = 1.f; // 许可桥的发射尺寸，只从同枪本地receipt复制一次，不随切枪重新读取。
    float FurthestDistance = 0.f; // 已见真实累计飞长cm的最大值，乱序根复制不能让归并尺寸倒退。
    float ConvergeDistance = 0.f; // 2026-11-19 BULLET-VFX/OWNER-CONVERGE：本人FP视觉线与物理弹道汇合的真实飞长cm（准星指向的首个阻挡深度）；仅本机渲染，0表示未建桥，不复制。
    uint32 Phase = 0; // 供换关清理。
    uint32 Sequence = 0; // 最近接受的网络段号，权威本地不使用网络回包。
    bool bBridge = false; // 初始资格确定后只可能关闭。
    bool bTerminal = false; // 已收到终态；复制Actor晚到不得回放。
    bool bRicochetShown = false; // 此ShotId+Pellet已播弹射闪；按阶段清表重置，不因首出射包丢失锁死。
    bool bEnvironmentImpactShown = false; // 此ShotId+Pellet已播环境接触；同一终包重传不双播。
    bool bPiercingExitShown = false; // 此ShotId+Pellet已播穿出闪；与弹射/环境接触各自独立去重。
    FDemoAmmoVisualStyle VisualStyle; // 首个合法表现包解析后冻结的本机样式值；Shooter晚到不会改已有弹色，不复制。
    bool bVisualStyleResolved = false; // 仅初始化一次；Shooter空也冻结内置普通回退，不能在飞行中升级样式。
    /** 首个合法表现事实解析一次样式并返回本状态拥有的只读值；后续包/射手晚到/切装配不重新解析。 */
    const FDemoAmmoVisualStyle& ResolveVisualStyle(const FDemoProjectilePresentationLaunch& Launch);
    /** 现有Phase/Level/0.5s/Sequence接收门之后调用；成功消费才返回true，不改终态/序号，不授予伤害。 */
    bool ConsumeRicochet(const FDemoProjectilePresentationSample& Sample);
    /** 同上，环境事实按本单弹幂等消费；Sample无事实不占用补显机会。 */
    bool ConsumeEnvironmentImpact(const FDemoProjectilePresentationSample& Sample);
    /** 同上，穿出事实独立幂等消费；查询未确认的包不占用补显机会。 */
    bool ConsumePiercingExit(const FDemoProjectilePresentationSample& Sample);
};

/** 每阶段有界缓存的保守闩锁：满表后未知身份不得因为腾空而重新显现；已有身份仍可正常结束。 */
struct FDemoProjectileCosmeticBudget
{
    static constexpr int32 Capacity=4096; // 单武器本World的最大已知弹身份，不随包内容增长。
    bool bRejectUnknown=false; // 只收紧，phase/Owner/World Clear才构造新预算，不复制。
    /** 仅未知key调用；Count为现有表实际数量，返回能否分配，满表时锁存拒绝而非丢失终态。 */
    bool AllowNew(int32 Count);
};

namespace DemoProjectilePresentation
{
    /** 纯表现向量映射：已飞距离s>=BlendDistance时严格归回权威P；有限边界外保留P，不移动碰撞。 */
    FVector Map(const FVector& Physical, const FVector& Offset, float Distance, float BlendDistance);
    /**
     * 本人FP收敛映射：V=P+Offset*(1-s/D)，对直线物理弹道给出从可见枪口F0到汇合点P(D)的直线视觉弹道。
     * Distance=s为真实已飞cm，ConvergeDistance=D为汇合深度cm；s>=D或非法输入返回P，不移动碰撞。
     */
    FVector Converge(const FVector& Physical, const FVector& Offset, float Distance, float ConvergeDistance);
    /** 同一真实飞长将初始尺寸0<s<=1归并到1；边界/非法值降级1，不返回物理Sphere使用。 */
    float RenderScale(float InitialScale, float Distance, float BlendDistance);
    /** 100cm立方体Tracer的局部缩放；长轴只取已映射段长度，s仅改变横截面，不能再次缩端点。 */
    FVector TracerMeshScale(float LengthCm, float WidthCm, float SizeScale);
    /** 实际分段尾长cm；Origin必须是当前直段起点（弹射后为接触点），不跨弯且不超过1000cm，非法返回0。 */
    float SegmentLength(const FVector& Origin, const FVector& Position, float TravelledDistance, float MaximumLength);
    /** 检查冻结发射描述：弹种0..3、有限坐标/单位方向及有界身份；Shooter可空，不查询当前装备。 */
    bool IsValid(const FDemoProjectilePresentationLaunch& Launch);
    /** 检查只读网络事实的结构与单调距离；活动接触字段须有限/单位方向，不校验/授予伤害，不引用World。 */
    bool IsValid(const FDemoProjectilePresentationSample& Sample);
    /** 真实接触或已折向段都必须使用世界坐标；首个Boundary包丢失时后续弹射事实也不得保留FP偏移。 */
    bool IsWorldBoundary(const FDemoProjectilePresentationSample& Sample);
}
