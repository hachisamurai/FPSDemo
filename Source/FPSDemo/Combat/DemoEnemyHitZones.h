#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "DemoEnemyHitZones.generated.h"

class USkeletalMesh;

/** 稳定受击区域协议；只改变整条生命的伤害，不建立断肢或独立部位生命。 */
UENUM(BlueprintType)
enum class EDemoEnemyHitRegion : uint8
{
    Body, // 机身、装甲、推进器和Boss护板，默认承受完整伤害。
    Arm,  // 上臂、前臂、武器/手腕/机械爪，默认承受较低伤害。
    Core  // 独立核心碰撞体，必须先穿过真实护板遮挡才可命中。
};

/** 一项可编辑倍率及其明确骨名白名单；不使用前缀或父骨骼推断，防止新骨漏配。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoEnemyHitZoneRule
{
    GENERATED_BODY()

    // 稳定区域标识；配置必须恰好包含Body、Arm、Core各一次。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hit Zones") EDemoEnemyHitRegion Region = EDemoEnemyHitRegion::Body;
    // 无单位乘数，有限且0..100；0允许显式配置免疫部位，但不会触发元素叠层。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hit Zones", meta=(ClampMin="0", ClampMax="100")) float DamageMultiplier = 1.f;
    // 对应PhysicsAsset BodyName/参考骨架的精确骨名；允许两种骨架共享同一白名单。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hit Zones") TArray<FName> BoneNames;
};

/** Cook后的唯一受击配置；JSON仅作为编辑源，运行时不读取工程文件路径。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoEnemyHitProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 初始化两套现有骨架的完整默认白名单；配置资产缺失时同一CDO可安全回退。 */
    UDemoEnemyHitProfile();
    /** 验证区域、倍率和骨名唯一性；失败日志指出字段，不静默接受半份配置。 */
    UFUNCTION(BlueprintCallable, Category="Hit Zones") bool Validate() const;
    /** Mesh为只读资产；每个参考骨必须明确映射或显式禁用，禁止漏配后默认机身。 */
    UFUNCTION(BlueprintCallable, Category="Hit Zones") bool ValidateSkeleton(const USkeletalMesh* Mesh) const;
    /** Bone为资产/调试查询的精确骨名；OutRegion/OutMultiplier仅成功时有效，本函数不证明发生了真实命中。 */
    UFUNCTION(BlueprintCallable, Category="Hit Zones") bool ResolveBone(FName Bone, EDemoEnemyHitRegion& OutRegion, float& OutMultiplier) const;
    /** Hit为真实WeaponTrace结果；OutRegion/OutMultiplier仅成功有效，保留旧蓝图/测试三参数接口。 */
    UFUNCTION(BlueprintCallable, Category="Hit Zones") bool ResolveHit(const FHitResult& Hit, EDemoEnemyHitRegion& OutRegion, float& OutMultiplier) const;
    /** Hit为真实阻挡，QueryChannel只允许WeaponTrace或PlayerProjectile；输出仅成功有效，C++显式传通道避免UHT隐藏枚举默认值。 */
    bool ResolveHitForChannel(const FHitResult& Hit, EDemoEnemyHitRegion& OutRegion, float& OutMultiplier, ECollisionChannel QueryChannel) const;

    // 三项规则随UDataAsset一起Cook；运行时只读，不修改CDO或已加载配置。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hit Zones") TArray<FDemoEnemyHitZoneRule> Regions;
    // 仅变换用途的骨必须显式标记，不允许绑定伤害刚体；root/aim_yaw在两套现有骨架中均无蒙皮。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hit Zones") TArray<FName> NonHittableBones;
};

namespace DemoEnemyHitZones
{
    // 与DefaultEngine.ini中WeaponTrace保持一致；只负责瞄准/受击资产查询，实体飞行伤害改用下方对象通道。
    constexpr ECollisionChannel TraceChannel = ECC_GameTraceChannel2;
    // 与DefaultEngine.ini中PlayerProjectile对象通道一致；敌人根球忽略、真实骨骼刚体阻挡。
    constexpr ECollisionChannel ProjectileChannel = ECC_GameTraceChannel3;
    /** Configured为可空的只读资产；缺失/结构非法时统一回退原生规则，原生也损坏才返回nullptr；骨架漏配仍由调用方拒绝。 */
    FPSDEMO_API const UDemoEnemyHitProfile* SelectValidProfile(const UDemoEnemyHitProfile* Configured);
    /** Region为已验证区域；返回稳定日志标识，未知枚举返回Invalid而不猜测部位。 */
    FPSDEMO_API const TCHAR* RegionName(EDemoEnemyHitRegion Region);
}
