// 职责：一次开火的冻结伤害与来源凭据。使用：实体弹以阶段代数控制飞行，以CombatSession独立控制伤害；备战练枪没有伤害许可，不得跨阶段复用。
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GAS/Ammo/DemoAmmoEffectSnapshot.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "DemoShotContext.generated.h"

class ADemoCharacter;
class AActor;
class UAbilitySystemComponent;
class UDemoEnemyHitProfile;
struct FDemoWeaponConfig;

/** 一次开火的不可变数值和共享去重；Combat允许伤害、Intermission仅实体飞行，源Pawn/ASC/GS弱引用，不进入存档。 */
UCLASS()
class FPSDEMO_API UDemoShotContext : public UObject
{
    GENERATED_BODY()
public:
    /** Source借用当前Avatar；Combat/Intermission均冻结阶段代数，只有Combat绑定伤害会话。WeaponId为稳定目录ID；Weapon/Profile/Ammo值复制；DamagePerPellet已包含升级分摊，尚未乘穿透倍率。 */
    bool Initialize(ADemoCharacter* Source, FName WeaponId, const FDemoWeaponConfig& Weapon, const UDemoEnemyHitProfile* Profile,
        int32 AmmoType, const FDemoAmmoEffectSnapshot& Ammo, float DamagePerPellet);
    /** 实体运动与表现的高频守卫：必须同Avatar/GS阶段代数/关号且存活；备战枪可飞行但没有伤害凭据。 */
    bool IsFlightValid() const;
    /** 高频/命中前伤害守卫；除IsFlightValid外必须是Combat发射并匹配原CombatSession；换枪不影响。 */
    bool IsAttackValid() const;
    /** 返回发射时冻结的公开阶段代数；客户端用它拒绝下一阶段迟到的弹体和弹迹，不授权伤害。 */
    uint32 GetPhaseRevision() const;
    /** Target借用实际扣血对象；同一枪同一对象仅首次返回true，调用先于同步音效/动画回调。 */
    bool TryMarkFeedback(AActor* Target);
    /** 新候选目标表现按普通/弱点/致死单调升档；最多128目标，重复普通不重播，纯表现不授予伤害。 */
    bool TryUpgradeFeedback(AActor* Target, bool bWeakPoint, bool bFatal);
    /** 返回发射时冻结的AB选择；后续切枪/切开关/最后一杀切阶段不能改变已在飞子弹的表现。 */
    bool UsesRedesignedFeedback() const;
    /** Target借用存活且已实际受伤的对象；元素提交前去重，避免同步GAS叠层重入。 */
    bool TryMarkElement(AActor* Target);
    /** 以下返回冻结值或弱对象借用；指针不可跨异步缓存，调用时重新验证IsAttackValid。 */
    FName GetWeaponId() const;
    /** 返回本枪唯一ID只读引用，用于关联日志和命中反馈。 */
    const FGuid& GetShotId() const;
    /** 返回发射关号，调试时与CombatSession一起核对旧弹。 */
    int32 GetLevelNumber() const;
    /** 返回发射时冻结的最大飞行路径厘米数。 */
    float GetRange() const;
    /** 发射时冻结的天赋倍率；不读取当前已换装的来源。 */
    float GetFrozenDamageMultiplier() const;
    /** Health/MaxHealth为命中前目标HP；只放大直接命中，不执行斩杀或处理持续伤害。 */
    float GetConditionalDamageMultiplier(float Health,float MaxHealth) const;
    /** 冻结的单次弹射倍率/搜敌半径cm；倍率为0代表本枪没有弹射天赋。 */
    float GetRicochetRatio() const;
    /** 返回本枪弹射索敌半径cm，不能由后续装备改变。 */
    float GetRicochetRadius() const;
    /** ActualDamage为Health GE实际损失HP；bCore保留弱点协议名，现在仅真实 head 命中为true，胸部core为false；仅直击调用、跨弹丸共享返弹闩锁。 */
    void NotifyDirectHit(float ActualDamage,bool bCore);
    /** 返回发射装配弹药类型0..3，命中不读取当前装配。 */
    int32 GetAmmoType() const;
    /** 返回被冻结的元素参数，只借用到当前命中结束。 */
    const FDemoAmmoEffectSnapshot& GetAmmoSnapshot() const;
    /** 返回本Context保活的部位规则副本，缺目标规则时兼容使用。 */
    const UDemoEnemyHitProfile* GetHitProfile() const;
    /** 解析发射Pawn弱引用；调用方仍须IsAttackValid验证生命/会话。 */
    ADemoCharacter* GetSourcePawn() const;
    /** 解析原PS的ASC弱引用；不把旧弹转交给重生后的Avatar。 */
    UAbilitySystemComponent* GetSourceASC() const;
    /** ImpactPoint为实际碰撞点世界厘米；使用发射相机位置计算首敌衰减，第二敌沿用该结果。 */
    float GetFirstHitBaseDamage(const FVector& ImpactPoint) const;
    /** 返回本枪借用的原会话，设施必须与其比较身份；撤销后IsAttackValid拒绝任何伤害。 */
    class UDemoCombatSession* GetCombatSession() const;
    /** 成功扣弹且通过最终发射检查时调用一次；失败准备/换弹不消费核心窗口；WITH_EDITOR通知候选真实发射ShotId，不改网络值。 */
    void CommitRunBuildShot();
    /** Prepare后登记全部弹丸，终结减计数；付费提交且全部终结才发一次整枪命中/落空事件。 */
    void RegisterProjectile();
    void ResolveProjectile();
    /** GE可能同步外部销毁弹体；命中事实发布前暂缓整枪终结，配对使用，不持有Actor。 */
    void BeginImpactResolution();
    void EndImpactResolution();
    /** 发射准备使用的冻结散布倍率；仅突击稳定分支改变。 */
    float GetRunBuildSpreadScale() const;
    /** 成功开火同步给拥有者的纯视觉相机后坐倍率。 */
    float GetRunBuildRecoilScale() const;
    /** Enemy为空表示设施，仅突击直伤有效；HitPoint用于计算距离，bSecondary排除折向弹射，调用本身不消费命中。 */
    float GetRunBuildDamageMultiplier(class ADemoEnemy* Enemy,bool bWeakPoint,const FVector& HitPoint,bool bSecondary=false);
    /** 真实扣血后调用，先闩锁同枪首敌/首目标并累计整枪弹丸与实伤，再进入GAS事件。 */
    void NotifyRunBuildHit(class ADemoEnemy* Enemy,float ActualDamage,float OverkillDamage,const FVector& HitPoint,bool bWeakPoint,bool bKilled,bool bSecondary=false);
private:
    // 唯一枪标识；不依赖每个武器从0开始的计数，供日志/反馈关联。
    FGuid ShotId;
    // 轮次和关号值冻结，拒绝复用地图中旧子弹的延迟回调。
    FString RunId;
    int32 LevelNumber = 0; // 正关号，Initialize成功前为0。
    uint32 PhaseRevision = 0; // 当前World的公开阶段代数，跨同关阶段往返也不能重新启用旧弹。
    bool bCombatShot = false; // 仅Combat发射置true；Intermission永不因进入下一关而取得伤害/天赋权限。
    FName StableWeaponId; // 目录稳定ID；源武器销毁不影响已经飞出的伤害。
    int32 SelectedAmmoType = 0; // 0普通/1火焰/2冰霜/3穿透。
    float PerPelletDamage = 0.f; // 已应用升级分摊及穿透首段倍率的HP基数。
    float ExecuteThreshold=0.f; // 发射冻结破绽HP比例，0为无增伤。
    float ExecuteMultiplier=1.f; // 发射冻结等级增伤，命中检查目标状态。
    float FrozenDamageMultiplier = 1.f; // 发射冻结的猎手倍率，命中时仅目标冻结才使用。
    float RicochetRatio=0.f; // 发射冻结等级后的非负倍率，可超过1；每颗弹仅允许一次折向，不能再次穿透。
    float RicochetRadius=0.f; // 折向时搜索当前敌人位置，半径世界cm。
    float SiphonRatio=0.f; // 发射冻结的实际直击回血比例，DOT不使用此上下文回调。
    float SiphonBudget=0.f; // 发射冻结每秒回血HP上限，同Pawn共享已用预算。
    int32 PrecisionRounds=0; // 发射时等级决定一次返还发数，未拥有为0。
    bool bPrecisionConsumed=false; // 整枪最多返一次；满匣命中也消费机会。
    TWeakObjectPtr<class ADemoWeaponBase> FiringWeapon; // 只返还原枪，不将旧弹奖励赠给当前切换枪。
    float Range = 0.f; // 最大实际路径长度cm。
    float FalloffStart = 0.f; // 相机起点到命中点的衰减起始cm。
    float MinimumDamageMultiplier = 1.f; // 最远距离倍率0..1。
    FVector CameraOrigin = FVector::ZeroVector; // 发射时相机世界坐标cm，不随角色移动。
    TWeakObjectPtr<ADemoCharacter> SourcePawn; // 不延长Avatar生命周期。
    TWeakObjectPtr<UAbilitySystemComponent> SourceASC; // PlayerState拥有ASC，仍核对它的当前Avatar。
    TWeakObjectPtr<class ADemoGameState> SourceState; // 同World公开阶段，避免逐帧跨层访问GameMode或抓取下一World状态。
    TWeakObjectPtr<class UDemoCombatSession> CombatSession; // RunFlow拥有的单次可撤销凭据；命中不再跨层查询GameMode/可变阶段。
    UPROPERTY() FDemoAmmoEffectSnapshot AmmoSnapshot; // 子弹独立值快照，持续状态另行复制。
    UPROPERTY() TObjectPtr<UDemoEnemyHitProfile> HitProfileSnapshot; // 本Context拥有的区域白名单/倍率副本，不引用可变配置资产。
    TSet<TWeakObjectPtr<AActor>> FeedbackTargets; // 弱对象集合只去重同一枪，不保活敌人。
    TMap<TWeakObjectPtr<AActor>, uint8> FeedbackGrades; // 候选每枪每目标最高等级0/1/2，最多128，尸体不保活。
    bool bRedesignedFeedback = false; // Initialize从可信候选目录冻结，不由客户端请求决定。
    TSet<TWeakObjectPtr<AActor>> ElementTargets; // 跨帧弹丸共享，独立于每颗物理穿透去重。
    bool bInitialized = false; // 初始化成功后锁定，不允许把旧枪Context重用于新一枪。
    UPROPERTY() FDemoRunBuildShotSnapshot RunBuildShot; // 发射时冻结核心/分支和充能，所有弹丸共享。
    TWeakObjectPtr<class UDemoRunBuildComponent> RunBuild; // 原PS组件，IsAttackValid先检查原Avatar/会话。
    TSet<TWeakObjectPtr<AActor>> RunBuildTargets; // 每枪每目标有效直击一次，弹射完全排除。
    TMap<TWeakObjectPtr<AActor>,int32> RunBuildTargetPellets; // 本枪对每个敌人实际扣血的弹丸计数，供B01/B06/B08判定。
    TWeakObjectPtr<class ADemoEnemy> RunBuildFirstEnemy; // 首个实际扣血敌人，允许散弹多颗弱点弹丸作用于同一首敌但不跨穿透第二敌。
    FVector RunBuildPrimaryImpact=FVector::ZeroVector; // 本枪主受击点世界cm，供整枪终结震波定位。
    int32 RunBuildTotalPelletsHit=0; // 本枪累计真实扣血弹丸数。
    float RunBuildTotalDamage=0.f; // 本枪累计真实扣血总和HP。
    bool bRunBuildAnyWeakPoint=false; // 本枪是否存在真实弱点扣血弹丸。
    bool bRunBuildFirstEnemyConsumed=false; // 首次真正扣血才消费，免伤/设施不占用精准首敌。
    bool bRunBuildCommitted=false; // 真正发射最多消费一次充能，失败准备不可调用。
    int32 PendingProjectiles=0; // 准备时登记，不在激活循环逐个登记，防首颗同步命中误报整枪结束。
    bool bResolutionSent=false; // 整枪终结幂等闩锁，无Timer/循环强引用。
    int32 ImpactDepth=0; // 当前同步HP结算深度，终结必须等真实命中回执发布。
    /** 仅在计数归零且没有未完成GE栈时同步发布一次。 */
    void TryResolveShot();
};
