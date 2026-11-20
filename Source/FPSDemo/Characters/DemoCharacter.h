// 职责：玩家Avatar及输入/组件协调。PS提供ASC；闪避Cue和批准凭据驱动独立特效，不修改角色骨骼/材质。
#pragma once
#include "CoreMinimal.h"
#include "Characters/FPSDemoCharacter.h"
#include "Characters/DemoSharedViewFrame.h" // Camera与最终Aim节点共用同一纯值眼位规则，避免各写一份胶囊夹限。
#include "AbilitySystemInterface.h"
#include "GameplayCueInterface.h" // 原生路由闪避Cue，其他Cue继续交给标准接口。
#include "Engine/NetSerialization.h" // 一次冲刺只发送量化单位水平向量，不接受客户端速度、距离或瞬移坐标。
#include "DemoCharacter.generated.h"

class UDemoWeaponComponent;
class UDemoMeleeComponent; // 已有独立近战默认子对象；反射成员让子蓝图保存其配置覆盖。
class UDemoWeaponAnimationComponent;
class UDemoPlayerPresentationComponent; // Pawn拥有的全身/队友展示枪组件，ASC仍使用Mesh1P。
class UDemoDashTrailComponent; // Pawn拥有的纯表现组件，PS仍持有ASC与技能窗口。
class UDemoAbilitySystemComponent;
class UDemoAttributeSet;
class UGameplayAbility;
class USoundBase;
class UAnimMontage;
class UAnimSequence;
class UInputAction; // 本机移动Action强引用供Shift读取当帧Enhanced Input值。
struct FOnAttributeChangeData;
// 游戏线程就绪事件；无Pawn参数，订阅方必须验证当前Avatar身份，支持已就绪后的立即查询。
DECLARE_MULTICAST_DELEGATE(FDemoAvatarReady);

/** 一次已提交闪避的表现凭据；与玩法GE分离，原子复制给拥有者和观察者。 */
USTRUCT()
struct FDemoDashPresentation
{
    GENERATED_BODY()
    // GameState服务器时钟秒；负值表示当前Avatar尚未执行闪避。
    UPROPERTY() float ServerStartTime=-100.f;
    // 已提交方向相对触发时身体的角度，度；之后转动相机不重选另一招式。
    UPROPERTY() float LocalDirectionDegrees=0.f;
    // 服务器批准的世界水平方向/速度/时长；拥有者建立同名CMC源参与移动重演，模拟代理仍交给CMC复制。
    UPROPERTY() FVector_NetQuantizeNormal WorldDirection=FVector::ForwardVector;
    UPROPERTY() float ForceSpeed=0.f;
    UPROPERTY() float ForceSeconds=0.f;
};

/** 保留模板手臂和摄像机，新增 GAS Avatar 生命周期、技能输入和安全区交互。 */
UCLASS()
class FPSDEMO_API ADemoCharacter : public AFPSDemoCharacter, public IAbilitySystemInterface, public IGameplayCueInterface
{
	GENERATED_BODY()
public:
	FDemoAvatarReady OnDemoAvatarReady; // GAS和初始装备准备后广播，组件结束时由订阅方解绑。
	/** 加载模板手臂，创建装备、命中反馈与闪避拖影组件；表现不参与GAS成本，颜色由拖影组件配置。 */
	ADemoCharacter();
	/** BeginPlay绑定移动完成回调，让起步当帧使用实际加速度/速度更新朝向；绑定仅归当前Pawn实例。 */
	virtual void BeginPlay() override;
    /** DeltaTime为帧秒、OutResult为实际视图；共享眼位不继承动画旋转，按配置关模糊；独立FP压缩时仅本人当前整个view覆写near，保留FOV/控制角/全局设置。 */
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    /** 返回共享世界基础眼位；默认全枪/Scope/近战稳定胶囊策略，显式兼容模式才读取head。相机/权威瞄准共用，不依赖渲染或本机模型。 */
    FVector GetSharedViewLocation() const;
    /** 游戏线程冻结基础眼位策略/偏移/胶囊界限；稳定模式不依赖骨姿，动画代理只保存纯值，旧head策略保同帧重投影。 */
    FDemoSharedViewFrame CaptureFullBodyViewFrame() const;
    /** CMC实际开始跳跃后记录角色/视角真值，仅状态边沿，避免把按键当成跳跃成功。 */
    virtual void OnJumped_Implementation() override;
    /** Hit为本次落地碰撞，记录视角偏移供诊断；保留引擎落地与预测逻辑。 */
    virtual void Landed(const FHitResult& Hit) override;
	/** NewControlRotation来自本机PC/服务器ServerMove；DeltaTime为本次移动秒数。静止越过头部阈值后平滑完整对齐，移动立即对齐；模拟代理沿原生Actor复制。 */
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime=0.f) override;
	/** 返回本Pawn真正视线；有Controller直接读输入，否则组合复制水平角与原生RemoteViewPitch，观战Camera不作为来源。 */
	virtual FRotator GetBaseAimRotation() const override;
	/** 返回当前视线的纯水平旋转，用于WASD和冲刺；身体与头部分离后不能再用ActorForward推导输入方向。 */
	FRotator GetMovementViewRotation() const;
	/** OutLifetimeProps 注册队友可见的阵亡状态及拥有者的技能成长。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** bEliminated 由服务器死亡/清关决定；保留 Pawn 和 PS 以恢复库存和成长。 */
	void SetCoopEliminated(bool bEliminated);
	/** 只读复制状态，死亡观战不将控制器与原 Pawn 的经济关系拆开。 */
	bool IsCoopEliminated() const;
	/** NewController 为权威端新控制器；Possess 完成后绑定 PlayerState ASC。 */
	virtual void PossessedBy(AController* NewController) override;
	/** 客户端 PlayerState 到达时补做 Avatar 初始化，不授予服务器技能。 */
	virtual void OnRep_PlayerState() override;
	/** Controller 与武器可能乱序到达，补做本地装备/动画绑定，不重授予能力。 */
	virtual void OnRep_Controller() override;
	/** EndPlayReason为卸载原因；先解绑并销毁本Pawn拖影，再清属性委托、射击计时器和旧Avatar。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Input为角色输入组件；移动/视角及六个武器动作走Enhanced Input，其余技能/交互保留键绑定。 */
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
	/** DamageType 为引擎跌落伤害类型；走统一失败流程，避免无 Pawn 后无法重开。 */
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;
	/** 标准 GAS 接口，ASC 实际归 PlayerState 所有。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	/** 返回借用 ASC 引用，可能在 Possess 前为空。 */
	UDemoAbilitySystemComponent* GetDemoASC() const;
	/** 返回借用只读属性集合，调用方应处理初始化前的空值。 */
	const UDemoAttributeSet* GetDemoAttributes() const;
	/** 战斗专用门禁，供开镜/标记等原有动作使用；不因备战练枪扩大攻击伤害或其他技能的阶段范围。 */
	bool CanUseCombatAbilities() const;
	/** 开火/手动及空仓换弹允许Combat和清关Intermission；Hub、菜单、死亡和暂停仍阻塞，伤害另由CombatSession授权。 */
	bool CanUseFirearmActions() const;
	/** 冲刺/治疗允许Combat、清关Intermission和Hub；存活且关闭奖励/终端/出发/暂停及独立实体设备操作才可用，HUD共用；物理操作服务器另查其私有租约，未锁移动/视角。 */
	bool CanUsePlayerSkills() const;
	/** 旧调用兼容转发：仅已有Prepare/Commit凭据的当前武器可发射；正式Fire GA直接使用其绑定实例。 */
	void PerformShot();
	/** 返回Pawn拥有的装备组件借用引用，用于GA/HUD和蓝图查询。 */
	UFUNCTION(BlueprintPure, Category="Weapon") UDemoWeaponComponent* GetWeaponComponent() const;
	/** Pawn拥有的第一人称动画协调器；装备/GA仅借用，不给动画层弹药所有权。 */
	UFUNCTION(BlueprintPure, Category="Weapon") UDemoWeaponAnimationComponent* GetWeaponAnimationComponent() const;
	/** 返回全身表现组件借用引用，供原生AnimInstance及几何验收读取。 */
	UDemoPlayerPresentationComponent* GetPlayerPresentation() const;
	/** 服务器分配的服装主色0..3；255表示尚未初始化，表现暂用青色。 */
	uint8 GetOutfitColorIndex() const;
	/** GAS移动倍率或开镜状态变化时重算步速，不修改属性基础值。 */
	void RefreshMovementSpeed();
	/** 拥有者按下Shift时截取当帧移动轴，单次RPC连同方向请求服务器GAS；无输入按当前朝向，不做本地位移预测。 */
	void RequestDash();
	/** Dash GA Commit后校验门禁并提交限时CMC水平RootMotionSource；优先同步请求方向，程序化调用回退本机输入/权威加速度。 */
	void PerformDash();
    /** 游戏线程只读最近已提交闪避的复制凭据；动画用同步时钟取样，不驱动位移/免伤。 */
    const FDemoDashPresentation& GetDashPresentation() const;
    /** GAS在Avatar上分发Cue；Self/Tag/Event/Parameters由ASC提供，Dash只唤醒表现，移动仍由批准凭据负责。 */
    virtual void HandleGameplayCue(UObject* Self, FGameplayTag Tag, EGameplayCueEvent::Type Event, const FGameplayCueParameters& Parameters) override;
	/** Boss全图结算查询当前GAS冲刺窗口；只表示该类攻击可被躲避，不免疫普通攻击。 */
	bool IsDashEvading() const;
	/** 当前Avatar死亡/退出战斗时取消天赋长时任务并移除短期状态；保留授予、冷却与成长，旧Avatar不能清理新Avatar。 */
	void ClearAttackStatuses();
    /** 本关会话撤销时取消武器动作与短期受击状态；保留技能冷却及永久/临时成长。 */
    void OnCombatSessionEnded();
	/** Choice为稳定奖励策略0..2，增量/上限来自目录；失败返回false，调用方恢复领取资格。 */
	bool ApplyAbilityReward(int32 Choice);
	/** 返回当前治疗点数，局内奖励可提升，供 Heal GA 使用。 */
	float GetHealAmount() const;
	/** 返回本局冲刺速度（cm/s），供奖励面板显示真实升级前后值；不开放写入。 */
	float GetDashSpeed() const;
	/** 整轮结束时从本人PS永久账本恢复GAS/技能基线并补满永久生命；仅当前权威Avatar调用，不读写存档、不清钱包/解锁/冷却。
	 * 调用方先撤销本轮核心/天赋并重置临时账本；成功返回true，ASC归属或永久账本无效时拒绝且不写属性。 */
	bool ResetTemporaryRunAttributes();
	/** Data为已校验检查点，恢复局内GAS基础数值与Pawn技能成长；仅权威新World Hub初始化调用。 */
	void RestoreRunProgress(const class UDemoRunSave& Data);
	/** 停止持续射击，终局/传送/打开菜单时调用。 */
	void StopFiring();
	/** 查找半径 250cm 内最近的交互物，只供本地提示与交互。 */
	class ADemoInteractable* FindInteractable() const;
    /** 本人PC交互Action统一入口；先消费枪墙/设备，再沿原终端E逻辑，不改相机或角色动画。 */
    void TriggerWorldInteraction();
	// 最近命中的本机World秒，仅兼容诊断读取；实际分级HUD由Pawn的HitFeedback组件持有，不复制/存档。
	float LastHitTime = -10.f;
	// 最近受伤的本机World秒，仍供HUD受伤提示；与射手命中回执分离。
	float LastDamageTime = -10.f;
private:
    // 只在成功提交技能时更新；所有端共享同一动画起点/方向，不逐帧复制骨或时间。
    UPROPERTY(ReplicatedUsing=OnRep_DashPresentation) FDemoDashPresentation DashPresentation;
    /** 所有观察者刷新去重特效；仅拥有者建立CMC源，不施加GE或消费冷却。 */
    UFUNCTION() void OnRep_DashPresentation();
    /** 根据已批准凭据建立或替换CMC限时源；Elapsed为网络已过秒数，服务器调用0，过期请求拒绝。 */
    void ApplyApprovedDashMovement(float Elapsed);
	// 服务器从已有ServerMove/本机PC控制方向更新的绝对Yaw，压缩到uint16约0.0055度；仅复制非拥有者，不发送额外输入RPC。
	UPROPERTY(Replicated) uint16 RemoteViewYaw=0;
	// 本端阈值状态仅供日志；不参与转向解算，不复制、不跨Pawn保留，-1左极限/0自由/1右极限。
	int32 HeadYawLogBand=0;
	// 只用于记录静止自由观察/移动跟随模式切换；不复制、不驱动解算，换Pawn自然清零。
	bool bLoggedMovingView=false;
	// 本Pawn的静止转身门闩；越界触发后保持至完整对齐，防止刚回45度就停止。拥有者/权威各自求解，模拟代理只收Actor旋转。
	bool bTurningBodyToView=false;
	// PC与Movement同帧可能多次调用FaceRotation；仅对静止指数插值去重，移动和近战即时对齐仍随真实状态执行。
	uint64 LastBodyTurnFrame=MAX_uint64;
	/** DeltaSeconds为本次已完成移动秒数，OldLocation/OldVelocity是旧值，仅保留委托签名；用更新后的真实状态处理起步/刹停，不读取旧状态推测输入。 */
	UFUNCTION() void OnViewMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	/** 同步阵亡碰撞/移动并清理拖影；不假设Dead标签与阵亡复制顺序，不在客户端发奖、结算或写存档。 */
	UFUNCTION() void OnRep_Eliminated();
	UPROPERTY(ReplicatedUsing=OnRep_Eliminated) bool bCoopEliminated = false; // 全队可见的生死状态，清关复活后复用本 Avatar。
	/** 复制颜色到达或全身网格后初始化时，只更新每Pawn独立材质。 */
	UFUNCTION() void OnRep_OutfitColor();
	// 本Pawn存活期间稳定；死亡复活复用Pawn，旅行新Pawn重新按队伍空位分配，不是永久形象存档。
	UPROPERTY(ReplicatedUsing=OnRep_OutfitColor) uint8 OutfitColorIndex = 255;
	// 完整模型/上身动作/展示枪生命周期归Pawn；不拥有经济、库存或ASC。
	UPROPERTY(VisibleAnywhere, Category="Player") TObjectPtr<UDemoPlayerPresentationComponent> PlayerPresentation;
	// 本Pawn的全身姿态残影；默认从MI_DashAfterimage读取颜色，组件可覆盖，角色只协调ASC绑定与阵亡清理。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Player|VFX", meta=(AllowPrivateAccess="true")) TObjectPtr<UDemoDashTrailComponent> DashTrailComponent;
	/** Data 是GAS移动倍率变更；同步步速，GE到期自然恢复，不跨帧持有参数。 */
	void OnMoveSpeedChanged(const FOnAttributeChangeData& Data);
	// 移动倍率属性委托绑定句柄，和健康委托一样随Avatar解绑。
	FDelegateHandle MoveSpeedChangedHandle;
	// 常规步速 cm/s，临时减速只乘此基线，不影响冲刺升级的初速度。
	static constexpr float BaseWalkSpeed = 650.f;
	/** Pawn与PS就绪后设置Owner/Avatar，绑定健康及拖影委托；可重复调用，表现不等待初始武器复制。 */
	void InitializeAbilitySystem();
	/** Data 包含旧/新健康值，仅本次回调借用；归零请求 GameMode 失败。 */
	void OnHealthChanged(const FOnAttributeChangeData& Data);
	/** Value 为模板二维移动输入，在 UI/终局时忽略。 */
	void MoveDemo(const FInputActionValue& Value);
	/** Value 为模板二维视角输入，在 UI 时忽略。 */
	void LookDemo(const FInputActionValue& Value);
	/** 跳跃按下/释放包装，以保持所有输入入口可追踪。 */
	void JumpPressed();
	void JumpReleased();
	/** 同一Enhanced Input Started先交PC设备/模态归属，未消费才走原武器/近战，空弹仍请求同一装填GA。 */
	void FirePressed();
	/** 同IA_Fire Completed/Canceled，仅释放本次输入归属；设备键不触发武器Stop RPC，公共StopFiring仍为强制兜底。 */
	void FireReleased();
	/** Enhanced Input武器事件包装；数字1/2切槽、B仅备战切型号、右键循环瞄准。 */
	void AimPressed();
	void EquipPrimaryPressed();
	void EquipSecondaryPressed();
	void CyclePrimaryPressed();
	/** 武器输入只请求装备组件/GAS，不直接修改弹药。 */
	void ReloadPressed();
	/** Shift经RequestDash快照当前输入并请求服务器GA，不能仅远程激活无方向参数的技能。 */
	void DashPressed();
	/** 治疗输入仍通过通用ASC激活，不携带移动意图。 */
	void HealPressed();
	/** E 键请求最近交互对象；终端/入口由对象与 GameMode 双重校验。 */
	void InteractPressed();
	/** AbilityClass 是要激活的原生技能类型；初始化/菜单检查集中处理。 */
	void RequestAbility(TSubclassOf<UGameplayAbility> AbilityClass);
	/** Direction为本机按键时量化单位水平向量；拥有者RPC到达后同一调用栈验证并激活ServerOnly Dash GA。 */
	UFUNCTION(Server, Reliable) void ServerRequestDash(FVector_NetQuantizeNormal Direction);
	/** Direction为仅表示方向的不可信请求；归一化/验证后同步借给GA，任何拒绝返回都不遗留下一次意图。 */
	void ActivateRequestedDash(const FVector& Direction);
	/** 读取当前移动Action，零输入用前向；无本机Input时仅为程序化GA读取服务器Movement加速度，绝不拿速度猜按键。 */
	FVector ResolveDashDirection() const;
	// Pawn输入初始化时持有实际IA_Move；只本机读取，不复制，不按键重复LoadObject，也不假定当前键位永远是WASD。
	UPROPERTY(Transient) TObjectPtr<UInputAction> MoveInputAction;
	// 仅服务器ActivateRequestedDash同步调用栈有效；不是复制状态，不跨帧保留，不改变PlayerState ASC所有权。
	FVector ActiveDashDirection=FVector::ZeroVector;
	// 与上方方向一起由TGuardValue恢复；GA冷却/门禁失败也不能把旧输入留给下次程序化激活。
	bool bHasActiveDashDirection=false;
	// ASC 弱引用不接管 PS 生命周期；换 Avatar 或 EndPlay 时解绑。
	TWeakObjectPtr<UDemoAbilitySystemComponent> BoundASC;
	// 当前属性回调注册句柄，用于精确解绑，避免重复死亡通知。
	FDelegateHandle HealthChangedHandle;
	// 角色持有装备组件，组件创建/清理武器Actor；不在角色重复保存弹药。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapon", meta=(AllowPrivateAccess="true")) TObjectPtr<UDemoWeaponComponent> WeaponComponent;
	// Pawn拥有的唯一近战默认子对象；反射引用用于编辑并序列化子蓝图的DefinitionAsset覆盖，不能替换组件本身。
	// 指针不复制、不作为存档；近战状态继续由组件自身复制，保持原MeleeComponent对象名和生命周期。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Melee", meta=(AllowPrivateAccess="true")) TObjectPtr<UDemoMeleeComponent> MeleeComponent;
	// 本Pawn生命周期内管理固定主AnimBP与武器Linked Layer、机械音/枪械动作；不跨World复制。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapon", meta=(AllowPrivateAccess="true")) TObjectPtr<UDemoWeaponAnimationComponent> WeaponAnimationComponent;
	// 治疗点数只复制拥有者供 HUD，权威 GA 读取服务器值。
	UPROPERTY(Replicated) float HealAmount = 35.f;
	// 冲刺水平初速度 cm/s，默认 1300；每次机动清关奖励 +350。
	UPROPERTY(Replicated) float DashSpeed = 1300.f; // 拥有者可见的技能成长，客户端不提交任意速度。
#if WITH_EDITOR
    // 仅显式-game/Standalone动画验收AB旧头骨眼位，BeginPlay冻结；普通PIE/联机/所有非Editor目标不存在此入口。
    bool bEditorLegacyHeadViewAB=false;
#endif
};
