#pragma once
#include "CoreMinimal.h"
#include "Characters/FPSDemoCharacter.h"
#include "AbilitySystemInterface.h"
#include "DemoCharacter.generated.h"

class UDemoWeaponComponent;
class UDemoAbilitySystemComponent;
class UDemoAttributeSet;
class UGameplayAbility;
class USoundBase;
class UAnimMontage;
class UAnimSequence;
struct FOnAttributeChangeData;

/** 保留模板手臂和摄像机，新增 GAS Avatar 生命周期、技能输入和安全区交互。 */
UCLASS()
class FPSDEMO_API ADemoCharacter : public AFPSDemoCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	/** 加载模板手臂并创建装备组件；枪械配置由派生武器蓝图负责。 */
	ADemoCharacter();
	/** NewController 为权威端新控制器；Possess 完成后绑定 PlayerState ASC。 */
	virtual void PossessedBy(AController* NewController) override;
	/** 客户端 PlayerState 到达时补做 Avatar 初始化，不授予服务器技能。 */
	virtual void OnRep_PlayerState() override;
	/** EndPlayReason 为卸载原因；解绑属性委托、射击计时器和旧 Avatar。 */
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
	/** 武器技能仅Combat且无菜单/暂停时可用；冲刺和治疗单独使用CanUsePlayerSkills。 */
	bool CanUseCombatAbilities() const;
	/** 冲刺/治疗允许Combat、清关Intermission和Hub；存活且关闭奖励/终端/出发/暂停菜单才可用，HUD共用。 */
	bool CanUsePlayerSkills() const;
	/** Fire GA Commit成功后的转发入口，当前武器执行弹道与表现。 */
	void PerformShot();
	/** 返回Pawn拥有的装备组件借用引用，用于GA/HUD和蓝图查询。 */
	UFUNCTION(BlueprintPure, Category="Weapon") UDemoWeaponComponent* GetWeaponComponent() const;
	/** GAS移动倍率或开镜状态变化时重算步速，不修改属性基础值。 */
	void RefreshMovementSpeed();
	/** Dash GA Commit成功后再次校验CanUsePlayerSkills，沿水平输入方向冲刺，无输入则使用朝向。 */
	void PerformDash();
	/** Boss全图结算查询当前GAS冲刺窗口；只表示该类攻击可被躲避，不免疫普通攻击。 */
	bool IsDashEvading() const;
	/** 退出战斗/卸载Avatar时移除短期躲避与减速；不移除技能冷却或局内升级。 */
	void ClearAttackStatuses();
	/** Choice 为 0..2 的清关奖励，分别攻击 +10、治疗 +20、冲刺速度 +350 cm/s。 */
	void ApplyAbilityReward(int32 Choice);
	/** 返回当前治疗点数，局内奖励可提升，供 Heal GA 使用。 */
	float GetHealAmount() const;
	/** 返回本局冲刺速度（cm/s），供奖励面板显示真实升级前后值；不开放写入。 */
	float GetDashSpeed() const;
	/** Data为已校验检查点，恢复局内GAS基础数值与Pawn技能成长；仅权威新World Hub初始化调用。 */
	void RestoreRunProgress(const class UDemoRunSave& Data);
	/** 停止持续射击，终局/传送/打开菜单时调用。 */
	void StopFiring();
	/** 查找半径 250cm 内最近的交互物，只供本地提示与交互。 */
	class ADemoInteractable* FindInteractable() const;
	// 最近命中/受伤的 World 时间（秒），供 HUD 短暂反馈；不复制，不拥有对象。
	float LastHitTime = -10.f;
	float LastDamageTime = -10.f;
private:
	/** Data 是GAS移动倍率变更；同步步速，GE到期自然恢复，不跨帧持有参数。 */
	void OnMoveSpeedChanged(const FOnAttributeChangeData& Data);
	// 移动倍率属性委托绑定句柄，和健康委托一样随Avatar解绑。
	FDelegateHandle MoveSpeedChangedHandle;
	// 常规步速 cm/s，临时减速只乘此基线，不影响冲刺升级的初速度。
	static constexpr float BaseWalkSpeed = 650.f;
	/** Pawn 与 PS 就绪后设置 Owner/Avatar，绑定健康委托；可重复调用。 */
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
	/** Enhanced Input Started转发装备组件，空弹请求同一装填GA。 */
	void FirePressed();
	/** Enhanced Input武器事件包装；数字1/2切槽、B仅备战切型号、右键循环瞄准。 */
	void AimPressed();
	void EquipPrimaryPressed();
	void EquipSecondaryPressed();
	void CyclePrimaryPressed();
	/** 输入只请求 GAS，不直接修改弹药/速度/健康。 */
	void ReloadPressed();
	void DashPressed();
	void HealPressed();
	/** E 键请求最近交互对象；终端/入口由对象与 GameMode 双重校验。 */
	void InteractPressed();
	/** AbilityClass 是要激活的原生技能类型；初始化/菜单检查集中处理。 */
	void RequestAbility(TSubclassOf<UGameplayAbility> AbilityClass);
	// ASC 弱引用不接管 PS 生命周期；换 Avatar 或 EndPlay 时解绑。
	TWeakObjectPtr<UDemoAbilitySystemComponent> BoundASC;
	// 当前属性回调注册句柄，用于精确解绑，避免重复死亡通知。
	FDelegateHandle HealthChangedHandle;
	// 角色持有装备组件，组件创建/清理武器Actor；不在角色重复保存弹药。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Weapon", meta=(AllowPrivateAccess="true")) TObjectPtr<UDemoWeaponComponent> WeaponComponent;
	// 治疗技能恢复点数，默认 35；每次医疗清关奖励 +20，不复制（单人范围）。
	float HealAmount = 35.f;
	// 冲刺水平初速度 cm/s，默认 1300；每次机动清关奖励 +350。
	float DashSpeed = 1300.f;
};
