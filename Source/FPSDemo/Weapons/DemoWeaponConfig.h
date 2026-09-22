#pragma once
#include "CoreMinimal.h"
#include "DemoWeaponConfig.generated.h"
class USkeletalMesh;
class UStaticMesh;
class USoundBase;
class UAnimSequence;
class UParticleSystem;
class UTexture2D;
class UDemoWeaponLayerAnimInstance;
class ADemoProjectileBase;

/** Trigger方式只决定输入调度，真正射速由每把武器实例的冷却校验保证。 */
UENUM(BlueprintType)
enum class EDemoFireMode : uint8 { SemiAutomatic, Automatic };

/** 蓝图Class Defaults中的武器定义；硬引用资源由CDO/实例保活，Icon软引用由HUD按需加载并缓存，不包含当前弹药。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoWeaponConfig
{
    GENERATED_BODY()
    // HUD名称与唯一用途标识；显示文本不作为伤害或输入逻辑分支。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity") FText DisplayName;
    // 终端卡片Icon的类型安全软引用，允许为空；HUD按需异步加载并保活，失败显示轮廓，不影响装备。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UI", meta=(DisplayName="Weapon Icon")) TSoftObjectPtr<UTexture2D> Icon;
    // 四枪机械骨骼网格；与AnimSet配套枪Sequence同Skeleton。旧武器未迁移时仍允许模板网格。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TObjectPtr<USkeletalMesh> Mesh;
    // 可选硬引用静态武器，由 CDO 保活；非空时优先显示它并清空组件上的旧骨骼枪，避免重叠。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TObjectPtr<UStaticMesh> StaticMesh;
    // 四枪分别引用配置子AnimBP；主Mesh1P AnimClass固定，此类仅通过Linked Anim Layers切换。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation") TSubclassOf<UDemoWeaponLayerAnimInstance> WeaponAnimLayerClass;
    // 手枪待机隐藏左臂；换弹临时恢复双手，完成/取消按当前装备重建。长枪默认false。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") bool bHideSupportArm = false;
    // 玩家手臂挂点；旧武器默认GripPoint，新四枪配置ik_hand_gun稳定锚点，缺失拒绝装备。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") FName AttachSocket = TEXT("GripPoint");
    // 挂接后的局部变换，厘米/度；不修改共享Mesh资源。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") FTransform AttachOffset = FTransform::Identity;
    // 枪Mesh上的枪口；缺失时使用相机前70cm的原模板近似位置并记录日志。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") FName MuzzleSocket = TEXT("Muzzle");
    // 单次/按住连射，松开或取消输入时都停止调度。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") EDemoFireMode FireMode = EDemoFireMode::Automatic;
    // 实体子弹硬类引用，由武器CDO负责Cook依赖；蓝图只覆盖飞行/表现数据，伤害仍来自本武器。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") TSubclassOf<ADemoProjectileBase> ProjectileClass;
    // 每分钟射击次数 [1,1200]；唯一射速定义，间隔=60/RPM。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin="1", ClampMax="1200")) float RoundsPerMinute = 333.33334f;
    // 单颗弹丸基础伤害 [0,10000]；玩家全局加成按弹丸数量分摊。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire", meta=(ClampMin="0")) float BaseDamage = 25.f;
    // 瞄准查询及实体子弹最大飞行距离cm [100,100000]；查询不造成伤害。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") float Range = 10000.f;
    // 腰射散布锥半角（度）[0,45]；步枪/手枪默认0保持模板命中体验。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") float SpreadHalfAngle = 0.f;
    // 霰弹每次射击弹丸数[1,32]；兼容名Hitscan的单发武器只生成一颗实体子弹。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shotgun", meta=(ClampMin="1", ClampMax="32")) int32 PelletCount = 8;
    // 开始衰减距离cm，[0,Range]；默认等于Range即不衰减。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") float FalloffStart = 10000.f;
    // 最大射程处伤害倍率[0,1]；衰减区线性插值。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fire") float MinimumDamageMultiplier = 1.f;
    // 基础弹匣容量[1,1000]，最终容量加上玩家本局MagazineBonus。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo") int32 MagazineCapacity = 12;
    // 首次生成装弹量[0,MagazineCapacity]，切换武器不会重置。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo") int32 InitialAmmo = 12;
    // 每次成功开火消耗[1,MagazineCapacity]；霰弹枪一次只消耗1发。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo") int32 AmmoPerShot = 1;
    // 有限备用弹药初始值[0,100000]；无限模式下保留数值但不消耗。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo") int32 InitialReserve = 60;
    // 默认无限备用，保持Demo既有玩法；改false即可验证真实弹匣转移。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo") bool bInfiniteReserve = true;
    // 整弹匣装填时间秒[0.1,15]，AbilityTask等待；取消不会提前补弹。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ammo") float ReloadSeconds = 1.4f;
    // 每枪一次Cue；霰弹不按弹丸数重复播放，随机与衰减仍由Cue负责。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") TObjectPtr<USoundBase> FireSound;
    // 可选枪口粒子资源；不承担命中判定，缺失仅跳过表现。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") TObjectPtr<UParticleSystem> MuzzleEffect;
    // 旧武器兼容资源；WeaponAnimLayerClass非空时完全由子类AnimSet供给，不再播放单序列覆盖主图。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") TObjectPtr<UAnimSequence> IdleAnimation;
    // 未迁移武器的开火单序列；新四枪使用AnimSet.FireMontage。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") TObjectPtr<UAnimSequence> FireAnimation;
    // 未迁移武器的换弹单序列；新四枪使用AnimSet.TacticalReload/EmptyReload。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") TObjectPtr<UAnimSequence> ReloadAnimation;
    // 每次开火视角上抬角度[0,10]；测试/默认步枪保持0，派生BP可调节。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Feedback") float RecoilPitch = 0.f;
    // 狙击枪默认启用，其他武器默认关闭；右键通过GAS Aim切换状态。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scope") bool bSupportsScope = false;
    // 两档实际摄像机FOV度数，0<二级<一级<179，默认40/10。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scope") float ScopeFOV = 40.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scope") float DeepScopeFOV = 10.f;
    // 开镜时散布半角度数[0,45]，默认0提供精确中心瞄准。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scope") float ScopedSpreadHalfAngle = 0.f;
    // 开镜步速倍率[0.1,1]，与Boss减速相乘；退出恢复原倍率，不修改GAS基础属性。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scope") float AimMoveMultiplier = .5f;
    // 狙击枪默认开枪退镜，下一次右键重新进入一级镜。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scope") bool bUnscopeAfterShot = true;
    /** Error输出非法字段说明；编辑器数值限制不能代替运行时有限值与资源校验。 */
    bool Validate(FString& Error) const;
};
