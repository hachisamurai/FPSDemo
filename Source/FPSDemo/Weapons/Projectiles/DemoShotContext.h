#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GAS/Ammo/DemoAmmoEffectSnapshot.h"
#include "DemoShotContext.generated.h"

class ADemoCharacter;
class AActor;
class UAbilitySystemComponent;
class UDemoEnemyHitProfile;
struct FDemoWeaponConfig;

/** 一次开火的不可变数值和共享去重；全部在飞子弹强持有，源Pawn/ASC只弱引用，不进入存档。 */
UCLASS()
class FPSDEMO_API UDemoShotContext : public UObject
{
    GENERATED_BODY()
public:
    /** Source借用当前Avatar；WeaponId为稳定目录ID；Weapon/Profile/Ammo值复制；DamagePerPellet已包含升级分摊，尚未乘穿透倍率。 */
    bool Initialize(ADemoCharacter* Source, FName WeaponId, const FDemoWeaponConfig& Weapon, const UDemoEnemyHitProfile* Profile,
        int32 AmmoType, const FDemoAmmoEffectSnapshot& Ammo, float DamagePerPellet);
    /** 高频/命中前守卫；必须同World、同源Avatar、同RunId关号且仍存活Combat，换枪不影响。 */
    bool IsAttackValid() const;
    /** Target借用实际扣血对象；同一枪同一对象仅首次返回true，调用先于同步音效/动画回调。 */
    bool TryMarkFeedback(AActor* Target);
    /** Target借用存活且已实际受伤的对象；元素提交前去重，避免同步GAS叠层重入。 */
    bool TryMarkElement(AActor* Target);
    /** 以下返回冻结值或弱对象借用；指针不可跨异步缓存，调用时重新验证IsAttackValid。 */
    FName GetWeaponId() const;
    const FGuid& GetShotId() const;
    int32 GetLevelNumber() const;
    float GetRange() const;
    int32 GetAmmoType() const;
    const FDemoAmmoEffectSnapshot& GetAmmoSnapshot() const;
    const UDemoEnemyHitProfile* GetHitProfile() const;
    ADemoCharacter* GetSourcePawn() const;
    UAbilitySystemComponent* GetSourceASC() const;
    /** ImpactPoint为实际碰撞点世界厘米；使用发射相机位置计算首敌衰减，第二敌沿用该结果。 */
    float GetFirstHitBaseDamage(const FVector& ImpactPoint) const;
private:
    // 唯一枪标识；不依赖每个武器从0开始的计数，供日志/反馈关联。
    FGuid ShotId;
    // 轮次和关号值冻结，拒绝复用地图中旧子弹的延迟回调。
    FString RunId;
    int32 LevelNumber = 0; // 正关号，Initialize成功前为0。
    FName StableWeaponId; // 目录稳定ID；源武器销毁不影响已经飞出的伤害。
    int32 SelectedAmmoType = 0; // 0普通/1火焰/2冰霜/3穿透。
    float PerPelletDamage = 0.f; // 已应用升级分摊及穿透首段倍率的HP基数。
    float Range = 0.f; // 最大实际路径长度cm。
    float FalloffStart = 0.f; // 相机起点到命中点的衰减起始cm。
    float MinimumDamageMultiplier = 1.f; // 最远距离倍率0..1。
    FVector CameraOrigin = FVector::ZeroVector; // 发射时相机世界坐标cm，不随角色移动。
    TWeakObjectPtr<ADemoCharacter> SourcePawn; // 不延长Avatar生命周期。
    TWeakObjectPtr<UAbilitySystemComponent> SourceASC; // PlayerState拥有ASC，仍核对它的当前Avatar。
    UPROPERTY() FDemoAmmoEffectSnapshot AmmoSnapshot; // 子弹独立值快照，持续状态另行复制。
    UPROPERTY() TObjectPtr<UDemoEnemyHitProfile> HitProfileSnapshot; // 本Context拥有的区域白名单/倍率副本，不引用可变配置资产。
    TSet<TWeakObjectPtr<AActor>> FeedbackTargets; // 弱对象集合只去重同一枪，不保活敌人。
    TSet<TWeakObjectPtr<AActor>> ElementTargets; // 跨帧弹丸共享，独立于每颗物理穿透去重。
    bool bInitialized = false; // 初始化成功后锁定，不允许把旧枪Context重用于新一枪。
};
