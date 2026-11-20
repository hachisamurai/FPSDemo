// 职责：Pawn两槽装备与武器动作协调。使用：输入/GAS/库存事务调用；依赖武器Actor和PS授权，命中回执由独立HitFeedback组件负责。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Contracts/DemoGameplayContracts.h"
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
    /** OutLifetimeProps注册库存Actor引用和槽位；服务器生成，客户端只执行复制表现。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
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
    /** Enhanced Input Started入口；近战槽转发DA招式0；枪械限Combat/Intermission且无菜单，空弹转发RequestReload，半自动不安排重试。 */
    void StartFire();
    /** Completed/Canceled、切枪和菜单入口；只停止连射，不取消手动装填。 */
    void StopFire();
    /** R键/空弹共用入口；Combat/Intermission共用本机和服务器门禁，bFromEmpty只用于日志；近战期间拒绝给隐藏枪补弹。 */
    bool RequestReload(bool bFromEmpty = false);
    /** B04成功首枪提交栈内请求至多一次追加枪；Weapon必须仍装备且输入仍按住，后续重走原GAS/扣弹/射速/枪机门禁。 */
    bool QueueCoreFollowup(ADemoWeaponBase* Weapon);
    /** 仅服务器同步发射栈为true；B04第二枪不能再次消费短连凭据或续排第三枪。 */
    bool IsCoreFollowupExecuting() const;
    /** 右键Started在近战槽请求DA招式1；枪械继续循环一级/二级/退镜，由Aim GA管理生命周期。 */
    void CycleAim();
    /** Aim GA成功激活时进入一级镜，失败返回false。 */
    bool BeginAim();
    /** 显式请求取消Aim GA；若GAS尚未初始化也恢复本地视角。 */
    void StopAim();
    /** Aim GA的EndAbility调用；恢复进入前FOV、手臂/当前枪和移动速度。 */
    void EndAim();
    /** 切换/死亡/菜单/离开原战斗或备战阶段的统一入口；停止连射并取消Reload/Aim/近战，保留技能及近战冷却截止。 */
    void CancelActions();
    /** 处理本人库存的全部主枪与手枪。清关按原规则补给，bFromReserveOnly 在成功入关时只从现有备弹装填。 */
    void RefillAll(bool bFromReserveOnly = false);
    /** Before为空时将遗物卸下产生的超容弹转备用；非空按本次交易旧快照回滚弹量，不重建Actor。 */
    void ReconcileGambleCapacity(const FDemoLoadoutSnapshot* Before = nullptr);
    void AddAmmoToAll(int32 Delta);
    /** Data是调用方拥有的值快照；bResetAmmo仅死亡/放弃重开时为true，按Data内永久弹匣加成补给，不读取死亡Pawn的临时加成。 */
    void CaptureLoadout(class UDemoRunSave& Data, bool bResetAmmo = false) const;
    /** Data为旧格式检查点；转换领域值后由本玩家PS验证权限，新档合法空库存先补默认手枪值。 */
    bool RestoreLoadout(const class UDemoRunSave& Data);
    /** Data为普通领域值；bResetAmmo仅重开补给，按Data.MagazineBonus计算；Editor临时借用枪不进入持久库存，保留原合法主槽。 */
    void CaptureSnapshot(FDemoLoadoutSnapshot& Data, bool bResetAmmo = false) const;
    /** Data由服务器会话/检查点提供；校验个人解锁和数据完整性，不接受客户端任意快照。 */
    bool RestoreSnapshot(const FDemoLoadoutSnapshot& Data);
    /** Data/UnlockedWeapons为权威目标值；预生成并校验全部装备，不改复制库存和玩家属性。 */
    bool PrepareSnapshot(const FDemoLoadoutSnapshot& Data, const TArray<FName>& UnlockedWeapons);
    /** 已安装目标玩家属性/权限后提交准备好的库存；旧Actor保留到整队确认。 */
    bool ApplyPreparedSnapshot();
    /** bCommit=true销毁旧库存；false恢复旧实例并销毁新库存。幂等，返回回滚是否成功。 */
    bool FinishSnapshot(bool bCommit);
    /** WeaponId为已授权商品；只暂存增量候选与目标值，保存前不改旧Actor/装备/弹量。bAllowInitialSupply只允许尚未领取的首次初始化弹药。 */
    bool PrepareShopSelection(FName WeaponId, bool bAllowInitialSupply);
    /** 仅在整槽本地耐久成功后调用；安装增量并发布装备。表现失败保留已买库存，返回false供“装备同步中”提示，不能退款删权。 */
    bool PublishShopSelection();
    /** 保存前失败/卸载撤销候选；不重建旧枪、不回退射击时钟或恢复已取消装填。 */
    void AbortShopSelection();
    /** 返回当前武器借用引用，初始化失败时可为空；蓝图不可直接替换指针。 */
    UFUNCTION(BlueprintPure, Category="Weapon") ADemoWeaponBase* GetActiveWeapon() const;
    /** 返回当前槽1/2、主武器型号索引0..N-1及Scope等级0/1/2，只读。 */
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetActiveSlot() const;
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetPrimaryIndex() const;
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetScopeLevel() const;
    /** 当前开镜对步速的倍率，未开镜返回1，供角色与GAS减速组合。 */
    float GetAimMoveMultiplier() const;
    /** Weapon为同步校验的借用对象；近战槽激活时即使枪仍在库存也返回false，阻止隐藏枪的GA提交。 */
    bool IsEquipped(const ADemoWeaponBase* Weapon) const;
    // 主武器型号列表；由组件强持有加载后的Actor，软类引用供Cooker收集BP依赖。
    UPROPERTY(EditDefaultsOnly, Category="Loadout") TArray<TSoftClassPtr<ADemoWeaponBase>> PrimaryWeaponClasses;
    // 固定副武器默认手枪，派生角色可更换但仍占数字2。
    UPROPERTY(EditDefaultsOnly, Category="Loadout") TSoftClassPtr<ADemoWeaponBase> SecondaryWeaponClass;
private:
    /** 拥有者输入请求；服务器重新检查本Pawn行为权限，不接受目标玩家或伤害参数。 */
    UFUNCTION(Server, Reliable) void ServerStartFire();
    UFUNCTION(Server, Reliable) void ServerStopFire();
    /** bFromEmpty只记录触发来源，不允许客户端决定补弹数量。 */
    UFUNCTION(Server, Reliable) void ServerRequestReload(bool bFromEmpty);
    /** Slot只接受1/2，服务器根据自己的库存查询Actor。 */
    UFUNCTION(Server, Reliable) void ServerEquipSlot(int32 Slot);
    /** 请求循环本人的Scope状态，镜头仅在拥有者本地应用。 */
    UFUNCTION(Server, Reliable) void ServerCycleAim();
    /** 菜单/输入取消只撤销本人动作，不能提交伤害或影响队友。 */
    UFUNCTION(Server, Reliable) void ServerCancelActions();
    friend class UDemoShopComponent; // 仅商店授权事务可提交主枪选择；旧UI入口必须转发Shop。
#if WITH_EDITOR
    friend class UDemoRunSaves; // 仅借用已有调试入口重建本机死亡前配装，不能授予正式所有权。
#endif
    /** WeaponId为商店已授权稳定键；只执行库存/动画提交，不查询菜单、不读账号、不自行保存。 */
    bool ExecutePrimarySelection(FName WeaponId, int32 HeldSlot=1); // HeldSlot 控制手持主/副槽，两槽状态一次提交。
#if WITH_EDITOR
    /** 仅Shop完成终端门禁后允许本机Standalone的GI测试权限临时装备；不授永久权/保存，重复选枪沿用原Actor弹量。 */
    bool ExecuteEditorTestPrimarySelection(FName WeaponId, int32 HeldSlot=1); // 终端选枪默认持主槽，重开时可保留手枪。
#endif
    /** 客户端装备引用到达后刷新动画/镜头；不回放权威库存生成或GAS成本。 */
    UFUNCTION() void OnRep_Loadout();
    /** Scope只复制给拥有者，远端客户端不能控制其他玩家摄像机。 */
    UFUNCTION() void OnRep_Scope();
    /** 服务器及客户端首次展示均加载只读BP类；不创建库存Actor。 */
    void LoadCatalogClasses();
    /** Weapon是已创建的库存，Slot为1/2、NewPrimaryIndex为主枪目录索引；统一初始/读档/终端/按键提交并在链接失败回滚。 */
    bool CommitEquip(ADemoWeaponBase* Weapon, int32 Slot, int32 NewPrimaryIndex);
    /** 自动武器定时回调；重新验证武器/Combat，不将按住输入转换为每帧射击。 */
    void TryFireHeld();
    /** Character为Owner的检查型访问，初始化期间允许为空，不保存跨World裸指针。 */
    ADemoCharacter* GetCharacter() const;
    /** WeaponClass为合法派生类，WeaponId为目录稳定键；权威创建后绑定身份再初始化Owner，失败销毁。 */
    ADemoWeaponBase* SpawnWeapon(TSubclassOf<ADemoWeaponBase> WeaponClass, FName WeaponId);
    /** Level=1/2；只更新镜头/模型和步速，不直接改变GAS拥有标签。 */
    void SetScopeLevel(int32 Level);
    // 当前World的库存强引用；仅曾在终端装备的主武器有实例，切回时保留弹药防止刷补给。
    UPROPERTY(ReplicatedUsing=OnRep_Loadout) TArray<TObjectPtr<ADemoWeaponBase>> PrimaryWeapons;
    // 四项已加载的蓝图类，用于只读CDO显示；强引用确保目录数据存活，不等于玩家持有武器。
    UPROPERTY() TArray<TSubclassOf<ADemoWeaponBase>> CatalogClasses;
    UPROPERTY(ReplicatedUsing=OnRep_Loadout) TObjectPtr<ADemoWeaponBase> SecondaryWeapon;
    UPROPERTY(ReplicatedUsing=OnRep_Loadout) TObjectPtr<ADemoWeaponBase> ActiveWeapon;
    // 服务器槽位真值；Scope复制给拥有者驱动本地摄像机，不作客户端伤害预测。
    // 新局固定副武器槽2；INDEX_NONE表示尚未在终端装备任何主武器。
    UPROPERTY(ReplicatedUsing=OnRep_Loadout) int32 ActiveSlot = 2;
    UPROPERTY(ReplicatedUsing=OnRep_Loadout) int32 PrimaryIndex = INDEX_NONE;
    UPROPERTY(ReplicatedUsing=OnRep_Scope) int32 ScopeLevel = 0;
    int32 PresentedScopeLevel = 0; // 本地已应用的镜头等级，避免复制顺序重复覆盖原FOV。
    // 进入瞄准前的FOV度数，用于退出精确恢复，不把武器FOV写回角色默认值。
    float UnscopedFOV = 90.f;
    // 输入按住标志与自动射击Timer归本组件；切换后不继承旧武器的连射。
    bool bFireHeld = false;
    // B04短连序列只借用当前枪；释放、菜单、切枪、死亡及离场随StopFire清除，不复制/不保存。
    TWeakObjectPtr<ADemoWeaponBase> CoreFollowupWeapon;
    bool bCoreFollowupQueued = false; // 仅允许下一次合法发射，不产生独立循环或额外射击RPC。
    bool bCoreFollowupExecuting = false; // 同步GAS调用栈标记；离栈立即清除，不是可复制的玩家输入。
    FTimerHandle FireTimer;
    UPROPERTY(Transient) TObjectPtr<class UDemoLoadoutTransaction> RestoreTransaction; // 只在服务器同步恢复栈中存在，EndPlay兜底撤销。
    UPROPERTY(Transient) TObjectPtr<ADemoWeaponBase> ShopCandidate; // 本次同步增量准备，旧实例只借用、新实例隐藏且不复制。
    FDemoLoadoutSnapshot ShopTarget; // 保存捕获期间返回的完整目标领域值，未耐久不写入复制库存。
    int32 ShopCandidateIndex = INDEX_NONE; // 目标主枪目录下标，INDEX_NONE表示无准备事务。
    bool bShopCandidateNew = false; // 只有本次新建的候选允许Abort销毁；旧实例必须保留。
#if WITH_EDITOR
    TSet<FName> EditorTestWeaponIds; // 本Pawn由测试入口借用的型号；未真实拥有时Capture剔除，不复制/保存/编入包体。
    FName EditorTestCheckpointPrimaryId; // 最近一次借用前的合法检查点主枪；临时主枪不污染原选择，换Pawn自然清空。
#endif
};
