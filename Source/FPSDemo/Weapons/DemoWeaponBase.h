#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/DemoWeaponConfig.h"
#include "DemoWeaponBase.generated.h"
class ADemoCharacter;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/** 可派生蓝图武器。配置由CDO提供，弹药/冷却仅由当前实例维护；技能仍在玩家ASC。 */
UCLASS(Abstract, Blueprintable)
class FPSDEMO_API ADemoWeaponBase : public AActor
{
    GENERATED_BODY()
public:
    /** 创建无碰撞第一人称网格并提供可运行的模板资源默认值。 */
    ADemoWeaponBase();
    /** Transform是编辑器/生成时构造位置；使蓝图配置Mesh在编辑器预览中生效。 */
    virtual void OnConstruction(const FTransform& Transform) override;
    /** EndPlayReason为卸载原因；移除武器动画计时器，不操作新Avatar。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    // 每个派生BP在Class Defaults独立配置；运行时蓝图不能任意改基础定义。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapon") FDemoWeaponConfig Config;
    /** Character为持有者，必须权威有效；仅首次装备集合初始化时赋初始弹药，成功返回true。 */
    bool InitializeForOwner(ADemoCharacter* Character);
    /** bEquipped控制可见与挂接；不补弹、不重置冷却。 */
    void SetEquipped(bool bEquipped);
    /** bScoped控制当前武器网格显示，不改变装备或弹药状态。 */
    void SetScopedVisual(bool bScoped);
    /** 只读当前弹匣/备用/有效容量，供GAS成本、HUD和蓝图查询；整数单位发。 */
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetAmmo() const;
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetReserveAmmo() const;
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetCapacity() const;
    /** 单次触发的弹丸数及每颗升级后伤害，用于UI和命中结算。 */
    virtual int32 GetPelletCount() const;
    float GetDamagePerPellet() const;
    /** 有效弹药、当前装备、生命和战斗阶段共同决定能否支付一次开火成本。 */
    bool CanPayShotCost() const;
    /** 当前武器自己的剩余开火冷却秒数；切换不清零。 */
    float GetFireCooldownRemaining() const;
    /** GAS ApplyCost入口，成功扣弹后生成一次执行许可，失败记录原因。 */
    void PayShotCost();
    /** GAS ApplyCooldown入口，根据RPM记录下次开火World时间；不创建共享CDO修改。 */
    void CommitFireCooldown();
    /** GAS提交后的射击入口；消费一次许可后命中/播放表现，重复调用不会免费开火。 */
    void ExecuteCommittedShot();
    /** 装填需要缺弹且有备用；重复BeginReload不重启。 */
    bool CanReload() const;
    bool BeginReload();
    /** AbilityTask完成时调用；只允许当前装备、存活且仍Combat的同一实例补弹。 */
    bool CompleteReload();
    /** 取消只移除状态和动画，绝不补弹。 */
    void CancelReload();
    /** 是否处于本实例装填状态，只读。 */
    UFUNCTION(BlueprintPure, Category="Weapon") bool IsReloading() const;
    /** Delta为有符号弹匣变动量；仅权威补给/升级/测试调用，结果钳制到[0,Capacity]。 */
    void ModifyAmmo(int32 Delta);
    /** 安全区/清关补给；装满弹匣并恢复初始备用，不影响开火冷却。 */
    void Refill();
    /** SavedAmmo/SavedReserve为检查点整数发数；仅权威初始化后调用，钳制到当前配置。 */
    void RestoreAmmo(int32 SavedAmmo, int32 SavedReserve);
    /** 返回实际枪口世界位置，缺Socket时使用已记录的相机偏移回退。 */
    FVector GetMuzzleLocation() const;
protected:
    /** 执行派生弹道；默认一条射线，霰弹派生类覆盖弹丸数。仅由提交入口调用。 */
    virtual void PerformBallistics();
    /** C++完成扣弹和伤害后调用的蓝图表现事件，禁止在此再次扣弹/施加同次伤害。 */
    UFUNCTION(BlueprintImplementableEvent, Category="Weapon|Cosmetic") void OnWeaponShot();
    /** bNowEquipped为本次状态；C++先完成挂接/可见性再派发表现。 */
    UFUNCTION(BlueprintImplementableEvent, Category="Weapon|Cosmetic") void OnWeaponEquipped(bool bNowEquipped);
    // 角色弱引用不延长Pawn生命周期；组件在EndPlay销毁全部武器。
    TWeakObjectPtr<ADemoCharacter> Wielder;
private:
    /** 构造/装备初始化时按配置选择静态或骨骼表现，保留旧根组件以兼容已保存蓝图。 */
    void ApplyVisualMesh();
    /** 动画恢复Timer回调；只有当前装备且未装填时播放Idle，镜内模型仍保持隐藏。 */
    void RestoreIdle();
    // Actor持有的兼容根组件；静态模式清空其骨骼资源，仅承担挂点变换，无碰撞。
    UPROPERTY(VisibleAnywhere, Category="Weapon") TObjectPtr<USkeletalMeshComponent> WeaponMesh;
    // Actor 持有的静态外观子组件，单位厘米、局部恒等变换；仅拥有者可见，不参与碰撞/阴影。
    UPROPERTY(VisibleAnywhere, Category="Weapon") TObjectPtr<UStaticMeshComponent> StaticWeaponMesh;
    // 单实例弹药唯一真值，不复制：当前实现明确限于单人权威World。
    int32 Ammo = 0;
    int32 ReserveAmmo = 0;
    // 开火时间戳为World秒，离开装备状态仍保留，OpenLevel销毁。
    float NextFireTime = 0.f;
    // 配置仅初始化一次；装备/装填/执行许可互相分离，不通过重新生成来切枪。
    bool bInitialized = false;
    bool bIsEquipped = false;
    bool bReloading = false;
    bool bShotPaid = false;
    // 每枪递增种子，确保同枪霰弹每次不同，且同次命中统计只生成一次。
    int32 ShotSequence = 0;
    // 可取消UObject动画回调，无裸指针Lambda，不跨World。
    FTimerHandle PoseTimer;
};

/** 步枪和手枪共用射线实现，以蓝图配置区分自动/半自动和参数。 */
UCLASS(Blueprintable)
class FPSDEMO_API ADemoHitscanWeapon : public ADemoWeaponBase
{
    GENERATED_BODY()
};
/** 霰弹：一次成本、多颗射线，按目标聚合GE。 */
UCLASS(Blueprintable)
class FPSDEMO_API ADemoShotgunWeapon : public ADemoHitscanWeapon
{
    GENERATED_BODY()
public:
    /** 给原生/新BP合理的半自动霰弹默认值，可继续覆盖。 */
    ADemoShotgunWeapon();
    /** 当前配置的弹丸数量，所有伤害与HUD都用同一读取接口。 */
    virtual int32 GetPelletCount() const override;
};
/** 狙击：单发射线与两档Scope，窗口/镜头由装备组件和Aim GA维护。 */
UCLASS(Blueprintable)
class FPSDEMO_API ADemoSniperWeapon : public ADemoHitscanWeapon
{
    GENERATED_BODY()
public:
    /** 半自动、慢射速、高伤害和瞄准支持的默认值。 */
    ADemoSniperWeapon();
};
