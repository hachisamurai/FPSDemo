// 职责：武器开火/弹药事务及只读弹种/分段表现缓存；GAS驱动准备提交，FP桥先解析资格，Actor与RPC再共用冻结样式，不授予客户端伤害。 本人首段使用发射帧最终可见枪口。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/Ticker.h" // 仅微光存活期间检查本机质量/可见性；暂停时仍能撤装饰，不新增武器Actor Tick。
#include "Weapons/DemoWeaponConfig.h"
#include "Weapons/DemoWeaponDefinition.h"
#include "Weapons/Projectiles/DemoProjectilePresentation.h"
#include "DemoWeaponBase.generated.h"
class ADemoCharacter;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UDemoEnemyHitProfile;
class ADemoProjectileBase;
class UDemoShotContext;
class UPrimitiveComponent;
class ADemoProjectileTracer;
class UParticleSystemComponent;
struct FDemoProjectileConfig;
namespace Scalability { struct FQualityLevels; } // 质量变化回调只读完整定义由cpp包含，不改变全局质量。

/** 一次成功射击冻结的服务器游戏时钟；保留历史类型名，现供所有枪TP后坐及狙击FP/TP拉栓共享。 */
USTRUCT()
struct FDemoBoltCycleState
{
    GENERATED_BODY()
    // 本枪成功射击序号；0表示尚未开火，切枪保留以防重复播放。
    UPROPERTY() int32 Sequence = 0;
    // 与开火冷却提交共用的服务器World秒数；客户端通过GameState对时。
    UPROPERTY() float ServerStartTime = 0.f;
    // 提交时冻结的60/(RPM×GAS射速倍率)秒数；修改配置/天赋不改变已开始周期。
    UPROPERTY() float Duration = 0.f;
};

/** 可派生蓝图武器。配置由CDO提供，弹药/冷却仅由当前实例维护；技能仍在玩家ASC。 */
UCLASS(Abstract, Blueprintable)
class FPSDEMO_API ADemoWeaponBase : public AActor
{
    GENERATED_BODY()
public:
    /** ExpectedSequence为GAS当前周期；仅达到配置插入时刻且凭据仍有效才结算一颗，重复/过期调用拒绝。 */
    bool InsertReloadShell(int32 ExpectedSequence);
    /** 服务器到达插入点时提交同一序号的一匣或一颗，拒绝重复、过早或陈旧调用。 */
    bool CommitReloadInsertion(int32 ExpectedSequence);
    /** 完整动作中的权威插入时刻（相对开始的秒数）。两视角、节律和GA读取相同的动作数据。 */
    float GetReloadInsertionSeconds() const;
    /** 权威输入检查整匣是否已插入且正在回握；此时开火或开镜可接管动画。 */
    bool IsInMagazineRecovery() const;
    /** 返回本次成功射击的只读时钟副本；不由表现反向修改服务器冷却。 */
    FDemoBoltCycleState GetBoltCycleState() const;
    /** 返回既有成功射击序号/时钟的表现副本；普通三枪TP不另建可影响弹药或射速的状态。 */
    FDemoBoltCycleState GetShotPresentationState() const;
    /** 返回本次拉栓剩余游戏秒数；服务端/拥有者客户端均使用同一服务器截止时刻。 */
    float GetBoltCycleRemaining() const;
    /** 创建无碰撞第一人称网格并提供可运行的模板资源默认值。 */
    ADemoWeaponBase();
    /** Transform是编辑器/生成时构造位置；使蓝图配置Mesh在编辑器预览中生效。 */
    virtual void OnConstruction(const FTransform& Transform) override;
    /** EndPlayReason为卸载原因；移除武器动画计时器，不操作新Avatar。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** OutLifetimeProps注册权威弹药/装备状态；各玩家HUD只读取自己的Actor复制值。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    /** Owner复制可能晚于组件引用；重取持有者并补齐当前网格表现。 */
    virtual void OnRep_Owner() override;
    /** Definition由权威目录提供；仅初始化前绑定一次稳定身份，不从当前槽号推导。 */
    bool BindDefinition(const FDemoWeaponDefinition& Definition);
    /** 返回本实例冻结的定义ID；UI/存档/挑战/ShotContext使用同一身份。 */
    UFUNCTION(BlueprintPure, Category="Weapon") FName GetWeaponId() const;
    /** 装备引用或Owner到达客户端时调用；只重建视觉，不生成库存、不扣弹。 */
    void RefreshReplicatedPresentation();
    /** Projectile必须为本枪权威弹；Sample是已飞行的有序只读事实，不授予客户端碰撞/伤害。 */
    void BroadcastProjectileTravel(const ADemoProjectileBase* Projectile, const FDemoProjectilePresentationSample& Sample);
    /** 活弹Mesh/Trail和本地Tracer共用的纯表现映射；Physical/Distance来自真实根，Boundary收紧桥接，不移动Actor。 */
    FVector ResolveProjectileVisual(const FDemoProjectilePresentationLaunch& Launch, const FVector& Physical, float Distance,
        const FDemoProjectileConfig& ProjectileConfig, bool bBoundary = false);
    /** Resolve之后读取同身份已收紧的本地尺寸；Scope/边界/外视点/未知身份为1，不修改碰撞或新增wire字段。 */
    float GetProjectileVisualScale(const FDemoProjectilePresentationLaunch& Launch, const FDemoProjectileConfig& ProjectileConfig) const;
    /** 客户端活弹在原FP位置资格解析之后调用；与RPC共用ShotId+Pellet冻结样式，空Shooter仍安全回退，不改变桥资格/复制或伤害。 */
    FDemoAmmoVisualStyle ResolveProjectileStyle(const FDemoProjectilePresentationLaunch& Launch);
    /** 将本人首段短闪绑定到枪口；TravelCm为已确认路程上界，过期或缺枪返回false，调用方回退世界段。 */
    bool RegisterLaunchTracer(const FDemoProjectilePresentationLaunch& Launch, ADemoProjectileTracer* FirstTracer,float TravelCm);
    /** 首段短暂显现时只读取本人当前可见枪口；物理起点/半径用于遮挡复核，切枪/开镜/遮挡立即撤下该视觉段。 */
    bool TryGetLaunchDisplayMuzzle(const FVector& PhysicalOrigin,float Radius,FVector& OutMuzzle) const;
    /** World换关/死亡清场撤销本武器所有本地发射记录，不修改在飞弹伤害或成本。 */
    void ClearProjectilePresentation();
    /** 客户端终态段可早于Actor初始复制；true阻止晚到活弹Mesh/Trail重显，不影响权威碰撞。 */
    bool IsProjectilePresentationTerminal(const FDemoProjectilePresentationLaunch& Launch) const;
    /** 复制活弹引用就绪后绑定弱身份；不创建/改变物理弹，不保活Actor。 */
    void RegisterProjectilePresentation(ADemoProjectileBase* Projectile,const FDemoProjectilePresentationLaunch& Launch);
    // 每个派生BP在Class Defaults独立配置；运行时蓝图不能任意改基础定义。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapon") FDemoWeaponConfig Config;
    // 三个受击区域的资产强引用，由CDO提供Cook依赖；运行时只读，可由派生武器显式覆盖平衡配置。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Weapon|Damage") TObjectPtr<UDemoEnemyHitProfile> EnemyHitProfile;
    /** Character为权威持有者；首次初始化弹药。bPublishReplication=false仅商店候选，落盘前保持未复制/隐藏，不执行装备表现。 */
    bool InitializeForOwner(ADemoCharacter* Character, bool bPublishReplication = true);
    /** bEquipped控制可见与挂接；不补弹、不重置冷却。 */
    void SetEquipped(bool bEquipped);
    /** bScoped控制当前武器网格显示，不改变装备或弹药状态。 */
    void SetScopedVisual(bool bScoped);
    /** 只读当前弹匣/备用/有效容量，供GAS成本、HUD和蓝图查询；整数单位发。 */
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetAmmo() const;
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetReserveAmmo() const;
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetCapacity() const;
    /** 单次触发的基础弹丸数及每颗升级后伤害，用于UI和命中结算。 */
    virtual int32 GetPelletCount() const;
    /** 固定容量多弹丸武器（如双管散弹）将未生效的弹匣容量成长折算为额外弹丸数0..16，不改变双管物理容量。 */
    int32 GetBonusPelletCount() const;
    /** 当前GAS容量折算与天赋修正后的实际弹体数1..64；GetPelletCount仍是基础弹丸数用于升级分摊。 */
    int32 GetProjectilesPerShot() const;
    /** 当前RPM经GAS修正后的射击间隔秒，不改变已开始冷却、半自动或换弹规则。 */
    float GetFireInterval() const;
    float GetDamagePerPellet() const;
    /** 有效弹药、当前装备、生命及Combat/Intermission枪械许可共同决定能否支付一次开火成本。 */
    bool CanPayShotCost() const;
    /** 当前武器自己的剩余开火冷却秒数；切换不清零。 */
    float GetFireCooldownRemaining() const;
    /** GAS提交前准备整枪弹丸及快照；只允许当前装备，任何准备失败不扣弹/冷却。 */
    bool PrepareShot();
    /** 验证准备时武器、发射阶段代数/关号和全部弹丸仍可飞行；伤害资格独立，不能重新绑定当前装备。 */
    bool IsPreparedShotValid() const;
    /** GA取消/生成失败/旅行共用幂等撤销；只回滚本次未发射凭据，飞行中的子弹不受换枪影响。 */
    void CancelPreparedShot();
    /** GAS ApplyCost入口，只针对已准备的本实例扣弹，失败记录原因。 */
    void PayShotCost();
    /** GAS ApplyCooldown入口，根据RPM记录下次开火World时间；不创建共享CDO修改。 */
    void CommitFireCooldown();
    /** GAS提交后登记挑战并激活已准备弹丸；此处不结算伤害，重复调用不能免费开火。 */
    void ExecuteCommittedShot();
    /** 装填需要缺弹且有备用；重复BeginReload不重启。 */
    bool CanReload() const;
    bool BeginReload();
    /** 回握结束时收起动作；两种装填都必须已经提交，结束回调不再次转移弹药。 */
    bool CompleteReload();
    /** 中断状态与动画，保留插入点已经提交的弹药。 */
    void CancelReload();
    /** 是否处于本实例装填状态，只读。 */
    UFUNCTION(BlueprintPure, Category="Weapon") bool IsReloading() const;
    /** 本枪动作序号在BeginReload递增，GA回调必须匹配；不作为跨World网络标识。 */
    UFUNCTION(BlueprintPure, Category="Weapon") int32 GetReloadSequence() const;
    /** 本次激活冻结的时长秒数和空仓版本；中途配置/弹药变更不能切换动作。 */
    UFUNCTION(BlueprintPure, Category="Weapon") float GetReloadDuration() const;
    UFUNCTION(BlueprintPure, Category="Weapon") bool IsEmptyReload() const;
    /** 当前装填服务器世界开始秒数；客户端仅用于对齐表现，不影响完成Timer。 */
    float GetReloadServerStartTime() const;
    /** 动画组件只借用Actor拥有的骨骼根；不能通过此接口修改弹药或装备所有权。 */
    USkeletalMeshComponent* GetWeaponSkeletalMesh() const;
    /** 本人静态枪借用引用，兼容枪可为空；纯表现投影映射不得改变库存或物理枪口。 */
    UStaticMeshComponent* GetWeaponStaticMesh() const;
    /** 本人镜体借用引用，仅映射正常尺度世界投影，不授予开镜/装备权限。 */
    UStaticMeshComponent* GetWeaponOpticMesh() const;
    /** Delta为有符号弹匣变动量；仅权威补给/升级/测试调用，结果钳制到[0,Capacity]。 */
    void ModifyAmmo(int32 Delta);
    /** Fraction为容量比例0..1；冲刺完成时有限备用转入，装填中/未装备拒绝，返回实际转入发数。 */
    int32 TransferReserveFraction(float Fraction);
    /** 核心精准回收的有限备用弹转移；Rounds为请求发数，原枪可已切走，绝不创建免费弹或超过容量。 */
    int32 TransferReserveRounds(int32 Rounds);
    /** 安全区/清关时恢复补给；bFromReserveOnly 用于成功入关，只从当前备弹填充缺口。 */
    void Refill(bool bFromReserveOnly = false);
    /** SavedAmmo/SavedReserve为检查点整数发数；仅权威初始化后调用，钳制到当前配置。 */
    void RestoreAmmo(int32 SavedAmmo, int32 SavedReserve);
    /** 统一物理世界枪口cm：只读TP枪Socket，缺失使用稳定共享视点回退；不按本人FP/Scope选源。Prepare另做原安全球扫掠。 */
    FVector GetMuzzleLocation() const;
    /** 当前观察者声光/实体弹表现首源cm；本人当前独立FP且非Scope时优先已校准MuzzleFlash、旧资产回退Muzzle。
     * 非FP仍原物理世界源；只供本机冻结表现，不改变F0时机，绝不能作为伤害/SpawnTransform输入。
     */
    FVector GetVisualMuzzleLocation() const;
    /** 从世界眼位Eye沿单位Direction取首个挡点，未命中返回本枪射程端。本人构图用准星中心；服务器发射传各弹丸原有散布方向。 */
    FVector TraceAimPoint(const FVector& Eye,const FVector& Direction) const;
    /** 只读本观察者真实可见枪的MuzzleFlash组件；Scope本人/切枪/缺专用Socket拒绝，不作物理源。 */
    UPrimitiveComponent* GetMuzzleFlashComponent(bool bScopedAtFire) const;
    /** 纯表现工厂：零相对变换附着真实MuzzleFlash，跟随全生命周期并继承scale一次；不允许缺Socket退原点。
     * Template/Source为当前World借用对象，Socket为可信资源名；返回Source拥有的自动销毁PSC，不授予射击/伤害权限。
     * 静态接口供同一生产路径的Editor附件回归调用；不保存/复制选择、不产生Gameplay Actor。
     */
    UFUNCTION(BlueprintCallable,Category="Weapon|Cosmetic")
    static UParticleSystemComponent* SpawnAttachedMuzzleFlash(UParticleSystem* Template,UPrimitiveComponent* Source,FName Socket);

protected:
    /** 提交后仅激活已经准备好的实体子弹；唯一伤害入口位于实际碰撞回调。 */
    virtual void PerformBallistics();
    /** C++完成扣弹并发射后调用的蓝图表现事件；禁止在此再次扣弹或提前施加伤害。 */
    UFUNCTION(BlueprintImplementableEvent, Category="Weapon|Cosmetic") void OnWeaponShot();
    /** bNowEquipped为本次状态；C++先完成挂接/可见性再派发表现。 */
    UFUNCTION(BlueprintImplementableEvent, Category="Weapon|Cosmetic") void OnWeaponEquipped(bool bNowEquipped);
    // 角色弱引用不延长Pawn生命周期；组件在EndPlay销毁全部武器。
    TWeakObjectPtr<ADemoCharacter> Wielder;
private:
    /** 成功射击广播只播一次原粒子与可选装饰微光；专用Socket跟随，Low不创建微光，不改弹道或判定。 */
    void PlayMuzzleFlash(bool bScopedAtFire);
    /** 换枪、本人开镜及EndPlay清理本枪创建的闪光；不碰其他武器/场景粒子。 */
    void ClearMuzzleFlashes();
    /** 短光到期/失源/降Low时撤组件、Timer、质量委托和弱Ticker；幂等，EndPlay也调用。 */
    void ReleaseMuzzleLight();
    /** 本机应用质量设置时GT调用；Low立即清已有装饰光，不影响原粒子/危险预警。 */
    void OnMuzzleLightQualityChanged(const Scalability::FQualityLevels& Quality);
    /** 微光存活期间CoreTicker弱绑定；覆盖直接sg写入和暂停/隐藏源，返回false即撤销，不计算伤害。 */
    bool PollMuzzleLightEligibility(float DeltaSeconds);
    UPROPERTY(Transient) TObjectPtr<class UPointLightComponent> MuzzleLight; // 每把可见枪最多一个，世界厘米半径，不复制。
    FTimerHandle MuzzleLightExpiry; // World游戏时间，与既有55ms短闪一致；连射只重置本枪截止时间。
    FDelegateHandle MuzzleLightQualityHandle; // 仅光存活期间监听质量事件；释放光/EndPlay同时撤销。
    FTSTicker::FDelegateHandle MuzzleLightMonitor; // 同GT核心Ticker，无Lambda捕获；弱UObject绑定，光释放即移除。
    TArray<TWeakObjectPtr<UParticleSystemComponent>> MuzzleFlashes; // Source拥有的自动销毁短闪弱引用，不复制/存档，不保活已完成PSC。
    /** 收到成功射击状态后，仅狙击拥有者补齐FP拉栓；TP各观察者直接按同一时钟取样。 */
    UFUNCTION() void OnRep_BoltCycle();
    // 最近一次实际开火时钟；所有枪向相关观察者复制以对齐TP后坐/拉栓，换枪保留，Pawn销毁后自然失效。
    UPROPERTY(ReplicatedUsing=OnRep_BoltCycle) FDemoBoltCycleState BoltCycleState;
    // 冷却提交时暂存的周期，仅Execute成功后发布；失败事务不能播放后坐/拉栓。
    FDemoBoltCycleState PreparedBoltCycle;
    /** bIsEquipped/装填状态由服务器复制后仅刷新本玩家或远端武器表现。 */
    UFUNCTION() void OnRep_WeaponState();
    /** 服务器实际发射后的不可靠广播；Muzzle为权威枪口回退值，本地优先可见外观枪口播放声/光，绝不用于命中计算。 */
    /** RecoilScale为本枪冻结的0.1..1纯视觉相机后坐倍率，默认1；协议版本由房间模块统一检查。 */
    /** bRedesigned冻结本次反馈选择；近枪预载Wave独立随机Pitch0.95..1.05/±1.5dB，其他枪沿自己的Cue，不改变后坐或成本，客户端不能提交此值。 */
    UFUNCTION(NetMulticast, Unreliable) void MulticastShotPresentation(FVector_NetQuantize Muzzle, FGuid ShotId, bool bScopedAtFire, float ServerFireTime, uint32 PhaseRevision, int32 Level, float RecoilScale=1.f,bool bRedesigned=false);
    /** Class只读CDO；Sample关联活弹和短命弹，阶段/时效/序号验证后只做表现。协议29，不复制ShotContext。 */
    UFUNCTION(NetMulticast, Unreliable) void MulticastProjectileTravel(TSubclassOf<ADemoProjectileBase> Class, const FDemoProjectilePresentationSample& Sample);
    /** 成功提交/成功开火RPC调用一次；冻结本人F0及Scope策略，拒绝后续消息追随新枪。 */
    void CaptureProjectileMuzzle(const FGuid& ShotId, bool bScopedAtFire);
    /** 同一个资格供视觉getter/冻结入口使用：当前本人ViewTarget、独立配置、匹配FP资源和非Scope。 */
    bool CanUseIndependentVisualMuzzle() const;
    /** 沿用原同半径局部遮挡/200cm门，纯表现修正不可绕墙；失败永久关闭本弹桥接。 */
    bool IsVisualBridgeClear(const FVector& Visual, const FVector& Physical, float Radius) const;
    /**
     * 本人FP弹道收敛深度：沿冻结的物理起点/方向做一次纯表现球扫，返回首个阻挡的真实飞长cm（无阻挡取本枪射程）。
     * 调用时机：同ShotId+Pellet首次建立FP桥时一次；结果夹在[OwnerMuzzleBlendDistance, Range]。
     * 限制：只读查询，不施伤、不改变物理弹；动态目标晚于查询移动时由真实接触边界收紧。
     */
    float ComputeVisualConvergeDistance(const FDemoProjectilePresentationLaunch& Launch, const FDemoProjectileConfig& ProjectileConfig) const;
    /** 新发射/新弹身份时最多每秒清理一次，限制本World缓存；不逐帧遍历World。 */
    void PruneProjectilePresentation();
    /** 构造/装备初始化时按配置选择静态或骨骼表现，保留旧根组件以兼容已保存蓝图。 */
    void ApplyVisualMesh();
    /** 旧武器Timer及新框架清理入口；仅当前装备且未装填时恢复姿势，新四枪不替换主图动画模式。 */
    void RestoreIdle();
    // Actor持有的兼容根组件；静态模式清空其骨骼资源，仅承担挂点变换，无碰撞。
    UPROPERTY(VisibleAnywhere, Category="Weapon") TObjectPtr<USkeletalMeshComponent> WeaponMesh;
    // Actor 持有的静态外观子组件，单位厘米、局部恒等变换；仅拥有者可见，不参与碰撞/阴影。
    UPROPERTY(VisibleAnywhere, Category="Weapon") TObjectPtr<UStaticMeshComponent> StaticWeaponMesh;
    // 仅拥有者可见的可选镜体，无碰撞；随本枪Root和Actor的装备显隐，无独立复制。
    UPROPERTY(VisibleAnywhere, Category="Weapon") TObjectPtr<UStaticMeshComponent> OpticMesh;
    // 身份在权威生成时冻结；网络仅传稳定键，客户端不能改成另一种挑战身份。
    UPROPERTY(Replicated) FName DefinitionId;
    // 单实例弹药服务器唯一真值，仅向拥有者复制，队友无需收到他人的备用弹统计。
    UPROPERTY(Replicated) int32 Ammo = 0;
    UPROPERTY(Replicated) int32 ReserveAmmo = 0;
    // 开火时间戳为World秒，离开装备状态仍保留，OpenLevel销毁。
    float NextFireTime = 0.f;
    // 配置仅初始化一次；装备/装填/执行许可互相分离，不通过重新生成来切枪。
    UPROPERTY(Replicated) bool bInitialized = false;
    UPROPERTY(ReplicatedUsing=OnRep_WeaponState) bool bIsEquipped = false;
    UPROPERTY(ReplicatedUsing=OnRep_WeaponState) bool bReloading = false;
    // 本武器单调动作序号，回调同武器也必须匹配，取消后再激活不会接收旧回调。
    UPROPERTY(ReplicatedUsing=OnRep_WeaponState) int32 ReloadSequence = 0; // 连续逐颗装填可能bReloading始终为true，序号变化仍需客户端续播下一周期。
    // 本周期的插入点是否已提交；服务器私有，Begin重置，取消不回滚，客户端从原有复制弹药读取结果。
    bool bReloadAmmoInserted = false;
    /** 同步转移最多 Rounds 发到当前弹匣，遵守容量与有限备弹，返回实际装入量；调用者负责权威与上下文校验。 */
    int32 LoadFromReserve(int32 Rounds);
    // 仅本次BeginReload写入的秒数与空仓标识；Complete/Cancel后保留供审计，下次开始覆盖。
    UPROPERTY(Replicated) float ReloadDuration = 0.f;
    UPROPERTY(Replicated) bool bEmptyReload = false;
    UPROPERTY(Replicated) float ReloadServerStartTime = 0.f; // GameState同步时钟，客户端扣除网络到达延迟显示当前换弹阶段。
    bool bShotPaid = false;
    // 准备期间强持有整枪上下文/Actor；提交完成释放引用，飞行弹丸自己保活Context。
    UPROPERTY() TObjectPtr<UDemoShotContext> PreparedContext;
    UPROPERTY() TArray<TObjectPtr<ADemoProjectileBase>> PreparedProjectiles;
    // 本次凭据冻结成本与冷却前值；取消只能撤销尚未实际发射的这一笔。
    int32 PreparedAmmoCost = 0;
    float PreparedPreviousFireTime = 0.f;
    bool bPreparedCooldownApplied = false;
    // 构造蓝图同步回调期间阻止递归Prepare；不跨异步线程访问。
    bool bPreparingShot = false;
    // 每枪递增种子，确保同枪霰弹每次不同，且同次命中统计只生成一次。
    int32 ShotSequence = 0;
    // 准备冻结的安全生成点，仅成功提交后发布；不再次查询可能已运动的Socket。
    FVector PreparedMuzzle = FVector::ZeroVector;
    // 本机World有界快照：不复制FP坐标、不保活弹体；换关/Avatar/武器结束清空。
    TMap<FGuid, FDemoProjectileMuzzleReceipt> ProjectileMuzzles;
    TArray<FGuid> PendingMuzzleShots; // 仅本武器本帧获独立FP资格的整枪ID，霰弹共用一次最终Socket快照。
    FDelegateHandle MuzzleFrameHandle; // 游戏线程一次性World回调，清场/销毁时精确解绑。
    /** 相机/骨骼完成后处理本World本帧成功发射，TickType/Delta为引擎通知参数，不驱动物理。 */
    void FinalizeMuzzleFrame(UWorld* World,ELevelTick TickType,float DeltaSeconds);
    TMap<FDemoProjectilePresentationKey, FDemoProjectileCosmeticState> ProjectileCosmetics;
    FDemoProjectileCosmeticBudget ProjectileCosmeticBudget; // 压力降级不能丢弃已收到的未知终态；phase/Owner边沿复位。
    float NextPresentationPrune = 0.f; // 本World秒；清理非Gameplay时钟。
    // 可取消UObject动画回调，无裸指针Lambda，不跨World。
    FTimerHandle PoseTimer;
};

/** 保留已保存BP原生父类名称；步枪和手枪现均使用实体子弹，蓝图区分自动/半自动。 */
UCLASS(Blueprintable)
class FPSDEMO_API ADemoHitscanWeapon : public ADemoWeaponBase
{
    GENERATED_BODY()
};
/** 霰弹：一次成本、多颗实体弹丸；跨帧独立伤害、共享每枪元素/反馈去重。 */
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
/** 狙击：单颗高速实体子弹与两档Scope，窗口/镜头由装备组件和Aim GA维护。 */
UCLASS(Blueprintable)
class FPSDEMO_API ADemoSniperWeapon : public ADemoHitscanWeapon
{
    GENERATED_BODY()
public:
    /** 半自动、慢射速、高伤害和瞄准支持的默认值。 */
    ADemoSniperWeapon();
};
