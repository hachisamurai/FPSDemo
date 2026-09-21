#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoWeaponComponent.generated.h"
class ADemoWeaponBase;
class ADemoCharacter;

/** Pawn拥有的两槽装备协调器；武器Actor各自保存弹药，不为切枪重新Spawn。 */
UCLASS(ClassGroup=(Demo), meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoWeaponComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 设置四个蓝图软类引用的默认路径；配置仍可由派生角色覆盖。 */
    UDemoWeaponComponent();
    /** EndPlayReason为Pawn/World卸载原因；取消能力/计时器并销毁所有库存Actor。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** ASC/Avatar就绪后仅创建并装备手枪；缓存目录CDO但不生成未选择的主武器。 */
    bool InitializeLoadout();
    /** Slot=1主武器、2副武器；存活且无菜单时可切换，成功返回true。 */
    UFUNCTION(BlueprintCallable, Category="Weapon") bool EquipSlot(int32 Slot);
    /** Index=0步枪/1散弹/2狙击；必须在终端武器页、范围内且永久解锁才生成/装备。 */
    UFUNCTION(BlueprintCallable, Category="Weapon") bool SelectPrimary(int32 Index);
    /** 兼容旧B键映射，仅记录拒绝；主武器改为终端明确选择，不能快捷键绕过。 */
    void CyclePrimary();
    /** CatalogIndex=0手枪/1步枪/2散弹/3狙击；返回缓存类CDO供只读展示，不创建库存Actor。 */
    const ADemoWeaponBase* GetCatalogWeapon(int32 CatalogIndex) const;
    /** Enhanced Input Started入口；一次按下为空弹时转发RequestReload，半自动不安排重试。 */
    void StartFire();
    /** Completed/Canceled、切枪和菜单入口；只停止连射，不取消手动装填。 */
    void StopFire();
    /** R键/空弹共用入口；bFromEmpty只用于日志，行为与装填权限完全相同。 */
    bool RequestReload(bool bFromEmpty = false);
    /** 右键Started循环一级/二级/退镜；Aim GA仍负责标签和取消生命周期。 */
    void CycleAim();
    /** Aim GA成功激活时进入一级镜，失败返回false。 */
    bool BeginAim();
    /** 显式请求取消Aim GA；若GAS尚未初始化也恢复本地视角。 */
    void StopAim();
    /** Aim GA的EndAbility调用；恢复进入前FOV、手臂/当前枪和移动速度。 */
    void EndAim();
    /** 切换/死亡/菜单/非战斗统一入口；停止连射、取消Reload/Aim，不取消Dash/Heal冷却。 */
    void CancelActions();
    /** 清关补给所有已持有实例；Delta为容量升级给予每把武器的额外弹药。 */
    void RefillAll();
    void AddAmmoToAll(int32 Delta);
    /** Data为GameMode拥有的待保存/已验证检查点；导出实际库存，恢复再次校验永久解锁。 */
    void CaptureLoadout(class UDemoRunSave& Data) const;
    bool RestoreLoadout(const class UDemoRunSave& Data);
    /** 返回当前武器借用引用，初始化失败时可为空；蓝图不可直接替换指针。 */
    UFUNCTION(BlueprintPure, Category="Weapon") ADemoWeaponBase* GetActiveWeapon() const;
    /** 返回当前槽1/2、主武器型号索引0..N-1及Scope等级0/1/2，只读。 */
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetActiveSlot() const;
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetPrimaryIndex() const;
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetScopeLevel() const;
    /** 当前开镜对步速的倍率，未开镜返回1，供角色与GAS减速组合。 */
    float GetAimMoveMultiplier() const;
    /** Weapon为同步校验的借用对象，保证技能/Task仍作用于当前装备。 */
    bool IsEquipped(const ADemoWeaponBase* Weapon) const;
    // 主武器型号列表；由组件强持有加载后的Actor，软类引用供Cooker收集BP依赖。
    UPROPERTY(EditDefaultsOnly, Category="Loadout") TArray<TSoftClassPtr<ADemoWeaponBase>> PrimaryWeaponClasses;
    // 固定副武器默认手枪，派生角色可更换但仍占数字2。
    UPROPERTY(EditDefaultsOnly, Category="Loadout") TSoftClassPtr<ADemoWeaponBase> SecondaryWeaponClass;
private:
    /** 自动武器定时回调；重新验证武器/Combat，不将按住输入转换为每帧射击。 */
    void TryFireHeld();
    /** Character为Owner的检查型访问，初始化期间允许为空，不保存跨World裸指针。 */
    ADemoCharacter* GetCharacter() const;
    /** WeaponClass为合法派生类；创建库存实例并初始化Owner，失败销毁并返回nullptr。 */
    ADemoWeaponBase* SpawnWeapon(TSubclassOf<ADemoWeaponBase> WeaponClass);
    /** Level=1/2；只更新镜头/模型和步速，不直接改变GAS拥有标签。 */
    void SetScopeLevel(int32 Level);
    // 当前World的库存强引用；仅曾在终端装备的主武器有实例，切回时保留弹药防止刷补给。
    UPROPERTY() TArray<TObjectPtr<ADemoWeaponBase>> PrimaryWeapons;
    // 四项已加载的蓝图类，用于只读CDO显示；强引用确保目录数据存活，不等于玩家持有武器。
    UPROPERTY() TArray<TSubclassOf<ADemoWeaponBase>> CatalogClasses;
    UPROPERTY() TObjectPtr<ADemoWeaponBase> SecondaryWeapon;
    UPROPERTY() TObjectPtr<ADemoWeaponBase> ActiveWeapon;
    // 单人本地装备/镜头状态；不宣称实现多人预测或完整复制。
    // 新局固定副武器槽2；INDEX_NONE表示尚未在终端装备任何主武器。
    int32 ActiveSlot = 2;
    int32 PrimaryIndex = INDEX_NONE;
    int32 ScopeLevel = 0;
    // 进入瞄准前的FOV度数，用于退出精确恢复，不把武器FOV写回角色默认值。
    float UnscopedFOV = 90.f;
    // 输入按住标志与自动射击Timer归本组件；切换后不继承旧武器的连射。
    bool bFireHeld = false;
    FTimerHandle FireTimer;
};
