// 职责：新敌人的可复制预警、弹道与区域攻击；Roster GA生成，GAS结算生命，会话结束同栈撤销。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoRosterHazard.generated.h"
class ADemoEnemy;
class ADemoCharacter;
class UStaticMeshComponent;
/** 一个弹幕/齐射共享一份命中账本，服务端先登记后结算，避免GE重入重复伤害。 */
UCLASS()
class FPSDEMO_API UDemoRosterSalvo : public UObject
{
    GENERATED_BODY()
public:
    // Weak集合不延长玩家生命；各在飞危险物以UPROPERTY保活账本直到全部销毁。
    TSet<TWeakObjectPtr<ADemoCharacter>> HitPlayers;
};
UENUM()
enum class EDemoHazardShape : uint8 { Marker, Shot, Mortar, Lightning, Lane, Ring };
/** 原子复制的视觉参数；客户端只画预警，不运行碰撞和生命结算。 */
USTRUCT()
struct FDemoHazardVisual
{
    GENERATED_BODY()
    UPROPERTY() EDemoHazardShape Shape=EDemoHazardShape::Marker; // 几何/运动协议，非客户端权限。
    UPROPERTY() FVector Origin=FVector::ZeroVector; // 发射/圆心世界cm。
    UPROPERTY() FVector Target=FVector::ZeroVector; // 固定落点/线段终点世界cm，锁定后不跟踪。
    UPROPERTY() float Started=0.f; // 服务器World秒，暂停时两端同步冻结。
    UPROPERTY() float Warning=1.2f; // 无伤害预警时长秒。
    UPROPERTY() float Radius=220.f; // 圈半径/电弧半宽cm；Ring最终半径。
    UPROPERTY() FLinearColor Color=FLinearColor(0.f,.5f,1.f); // 阵营内技能识别色。
};
UCLASS()
class FPSDEMO_API ADemoRosterHazard : public AActor
{
    GENERATED_BODY()
public:
    /** CDO硬引用引擎形状及现有发光材质，确保Cook可收集。 */
    ADemoRosterHazard();
    /** Source为本次会话来源；Visual为冻结几何；Salvo共享防重复账本；Damage为最终非负点数。
     * Deferred完成前调用；Marker不伤害，Shot从Origin按Target方向飞行，Mortar先预警再抛射。 */
    void Initialize(ADemoEnemy* Source,const FDemoHazardVisual& Visual,UDemoRosterSalvo* Salvo,float Damage);
    /** 清理入口供会话及敌人取消调用；立即失效，不产生爆炸。 */
    void Cancel();
    /** DeltaSeconds是World帧秒，权威更新真实扫掠，客户只渲染。 */
    virtual void Tick(float DeltaSeconds) override;
    /** OutLifetimeProps注册视觉快照，移动由Actor复制。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    /** 刷新复制预警，不创建客户端计时器或伤害。 */
    UFUNCTION() void OnRep_Visual();
    /** Position为爆心/圆心世界cm；PreviousRadius/CurrentRadius用于环扫过区间，其他形状不用。 */
    void ResolveArea(const FVector& Position,float PreviousRadius=0.f,float CurrentRadius=0.f);
    /** Player为当前存活参战者，From为遮挡检测起点，bDash允许专用冲刺躲避；先去重后提交GE。 */
    void HitPlayer(ADemoCharacter* Player,const FVector& From,bool bDash);
    /** From/To为本帧亚步弹道世界点；真正球体扫掠，墙体可阻挡。 */
    bool SweepProjectile(const FVector& From,const FVector& To);
    // 客户只收到几何；来源、伤害和账本保留权威端。
    UPROPERTY(ReplicatedUsing=OnRep_Visual) FDemoHazardVisual Visual;
    UPROPERTY() TObjectPtr<UDemoRosterSalvo> Ledger;
    TWeakObjectPtr<ADemoEnemy> Shooter; // 来源死亡/会话结束立即无效。
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ShapeMesh; // 本地无碰撞预警主体。
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RingSegments; // 32段环形带，避免整张圆盘误导命中范围。
    float DamagePoints=0.f; // 初始化时快照最终伤害，不受玩家升级影响。
    float PreviousFlight=0.f; // 已扫掠到的飞行秒，亚步最大20ms。
    float PreviousRing=0.f; // 上帧冲击环半径cm，快速帧率变化也不漏扫。
    bool bConsumed=false; // 同栈取消/命中重入门禁。
    TMap<TWeakObjectPtr<ADemoCharacter>,float> PreviousDistances; // 环前一帧相对距离，玩家反向穿环也能命中。
};
