#include "Characters/DemoCharacter.h"
#include "Animation/DemoWeaponAnimationComponent.h"
// 对应头文件先于依赖，保证UE独立编译能检查本类声明自包含。
#include "Weapons/Ammo/DemoAmmoComponent.h" // Pawn持有装配组件，统一保留一处依赖入口。
#include "Save/DemoRunSave.h"
#include "Settings/DemoGameUserSettings.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Audio/DemoWeaponAudio.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerController.h"
#include "AI/DemoEnemy.h"
#include "Interaction/DemoInteractable.h"
#include "Game/FPSDemoGameMode.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoGameplayAbility.h"
#include "GAS/DemoTags.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "InputAction.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADemoCharacter::ADemoCharacter()
{
	DEMO_LOG_CALL();
	// 倍率委托以同一基线恢复，避免减速结束后丢失默认步速。
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	// 模板 Pawn 胶囊默认忽略 Visibility；敌人近战视线必须能命中玩家且仍被墙体阻挡。
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// 角色只拥有手臂；具体枪械Mesh/动画/音效由武器实例和BP定义提供。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Arms(TEXT("/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms"));
	GetMesh1P()->SetSkeletalMesh(Arms.Object);
	// 保留肩部在镜头下方的模板组件位置；新动画单独前移/抬高手腕和稳定枪锚点，避免整个肩部进入近裁剪面。
	// 离线IK维持真实臂长，运行时轻摆仍整体作用；单位cm，不改相机、胶囊或瞄准方向。
	GetMesh1P()->SetRelativeLocation(FVector(-10.f, 0.f, -147.f));
	WeaponComponent = CreateDefaultSubobject<UDemoWeaponComponent>(TEXT("WeaponComponent"));
	WeaponAnimationComponent = CreateDefaultSubobject<UDemoWeaponAnimationComponent>(TEXT("WeaponAnimationComponent"));
	CreateDefaultSubobject<UDemoAmmoComponent>(TEXT("AmmoComponent")); // Pawn持有弹药组件，PS ASC仍由PlayerState拥有。
}

void ADemoCharacter::PossessedBy(AController* NewController) { DEMO_LOG_CALL(); Super::PossessedBy(NewController); InitializeAbilitySystem(); }
void ADemoCharacter::OnRep_PlayerState() { DEMO_LOG_CALL(); Super::OnRep_PlayerState(); InitializeAbilitySystem(); }

void ADemoCharacter::InitializeAbilitySystem()
{
	DEMO_LOG_CALL();
	// PS 在 Possess/复制完成后才可靠，初始化前允许短暂为空。
	ADemoPlayerState* State = GetPlayerState<ADemoPlayerState>();
	if (!State) { UE_LOG(LogFPSDemo, Warning, TEXT("ASC initialization deferred: no DemoPlayerState")); return; }
	if (BoundASC.IsValid())
	{
		BoundASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		BoundASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute()).Remove(MoveSpeedChangedHandle);
	}
	BoundASC = State->GetDemoASC();
	BoundASC->InitAbilityActorInfo(State, this);
	// 主图必须先于初始手枪装备就绪；ASCMontage显式绑定Mesh1P而非ACharacter默认Mesh。
	if (!WeaponAnimationComponent->InitializeArms()) UE_LOG(LogFPSDemo, Error, TEXT("First-person animation initialization failed; generate reload assets"));
	HealthChangedHandle = BoundASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).AddUObject(this, &ADemoCharacter::OnHealthChanged);
	MoveSpeedChangedHandle = BoundASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute()).AddUObject(this, &ADemoCharacter::OnMoveSpeedChanged);
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * GetDemoAttributes()->GetMoveSpeedMultiplier();
	if (HasAuthority())
	{
		BoundASC->GrantStartupAbilities();
		if (!WeaponComponent->InitializeLoadout()) UE_LOG(LogFPSDemo, Error, TEXT("Loadout initialization failed; create weapon Blueprint assets"));
	}
}

void ADemoCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
	StopFiring();
	WeaponComponent->CancelActions();
	if (BoundASC.IsValid())
	{
		// 先移除短期状态让属性委托恢复基线，再精确解绑，避免状态随PlayerState遗留到新Avatar。
		ClearAttackStatuses();
		BoundASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute()).Remove(MoveSpeedChangedHandle);
		BoundASC->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		if (BoundASC->GetAvatarActor() == this) { BoundASC->CancelAllAbilities(); BoundASC->ClearActorInfo(); }
	}
	Super::EndPlay(EndPlayReason);
}

UAbilitySystemComponent* ADemoCharacter::GetAbilitySystemComponent() const { DEMO_LOG_TICK(); return GetDemoASC(); }
UDemoWeaponAnimationComponent* ADemoCharacter::GetWeaponAnimationComponent() const { DEMO_LOG_TICK(); return WeaponAnimationComponent; }
UDemoAbilitySystemComponent* ADemoCharacter::GetDemoASC() const { DEMO_LOG_TICK(); return BoundASC.Get(); }
const UDemoAttributeSet* ADemoCharacter::GetDemoAttributes() const
{
	DEMO_LOG_TICK();
	// 只借用属性，Owner 为 PS。
	const ADemoPlayerState* State = GetPlayerState<ADemoPlayerState>();
	return State ? State->GetAttributes() : nullptr;
}
bool ADemoCharacter::CanUseCombatAbilities() const
{
	DEMO_LOG_TICK();
	// 状态与菜单共同限制输入，安全区角色即使面朝战斗区也不能开火。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	const ADemoPlayerController* PC = Cast<ADemoPlayerController>(Controller);
	return State && State->Phase == EDemoPhase::Combat && PC && !PC->IsUpgradeMenuOpen() && !PC->HasBlockingOverlay() && !GetWorld()->IsPaused();
}
bool ADemoCharacter::CanUsePlayerSkills() const
{
	DEMO_LOG_TICK();
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 借用当前阶段，读档后重新解析，不缓存旧World。
	const ADemoPlayerController* PC = Cast<ADemoPlayerController>(Controller); // 本Avatar的控制器；菜单与原生输入共用阻塞条件。
	const UDemoAttributeSet* Attributes = GetDemoAttributes(); // 只读生命；冷却/Dead标签仍由GAS权威检查。
	return State && (State->Phase == EDemoPhase::Combat || State->Phase == EDemoPhase::Intermission || State->Phase == EDemoPhase::Hub)
		&& PC && Attributes && Attributes->GetHealth() > 0.f && !PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen()
		&& !PC->HasBlockingOverlay() && !GetWorld()->IsPaused(); // Victory/Reward/Defeat/Lobby均为模态页面，不接受技能穿透。
}

void ADemoCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
	DEMO_LOG_CALL();
	// 父类模板需要 BP 设置动作，本 Pawn 显式加载同一套资产，不调用模板绑定以避免重复。
	UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(Input);
	if (Enhanced)
	{
		// 动作由 Controller 的映射上下文保活；只用于建立本角色绑定。
		UInputAction* MoveActionAsset = LoadObject<UInputAction>(nullptr, TEXT("/Game/FirstPerson/Input/Actions/IA_Move.IA_Move"));
		UInputAction* LookActionAsset = LoadObject<UInputAction>(nullptr, TEXT("/Game/FirstPerson/Input/Actions/IA_Look.IA_Look"));
		if (MoveActionAsset) Enhanced->BindAction(MoveActionAsset, ETriggerEvent::Triggered, this, &ADemoCharacter::MoveDemo);
		if (LookActionAsset) Enhanced->BindAction(LookActionAsset, ETriggerEvent::Triggered, this, &ADemoCharacter::LookDemo);
		// 六个武器Action由IMC_Combat持有；只在输入组件初始化时借用，不每帧查找资源。
		UInputAction* Fire = LoadObject<UInputAction>(nullptr,TEXT("/Game/Weapons/Input/IA_Fire.IA_Fire"));
		UInputAction* Reload = LoadObject<UInputAction>(nullptr,TEXT("/Game/Weapons/Input/IA_Reload.IA_Reload"));
		UInputAction* Aim = LoadObject<UInputAction>(nullptr,TEXT("/Game/Weapons/Input/IA_Aim.IA_Aim"));
		UInputAction* Primary = LoadObject<UInputAction>(nullptr,TEXT("/Game/Weapons/Input/IA_EquipPrimary.IA_EquipPrimary"));
		UInputAction* Secondary = LoadObject<UInputAction>(nullptr,TEXT("/Game/Weapons/Input/IA_EquipSecondary.IA_EquipSecondary"));
		UInputAction* Cycle = LoadObject<UInputAction>(nullptr,TEXT("/Game/Weapons/Input/IA_CyclePrimary.IA_CyclePrimary"));
		if (Fire)
		{
			Enhanced->BindAction(Fire,ETriggerEvent::Started,this,&ADemoCharacter::FirePressed);
			Enhanced->BindAction(Fire,ETriggerEvent::Completed,this,&ADemoCharacter::StopFiring);
			Enhanced->BindAction(Fire,ETriggerEvent::Canceled,this,&ADemoCharacter::StopFiring);
		}
		if (Reload) Enhanced->BindAction(Reload,ETriggerEvent::Started,this,&ADemoCharacter::ReloadPressed);
		if (Aim) Enhanced->BindAction(Aim,ETriggerEvent::Started,this,&ADemoCharacter::AimPressed);
		if (Primary) Enhanced->BindAction(Primary,ETriggerEvent::Started,this,&ADemoCharacter::EquipPrimaryPressed);
		if (Secondary) Enhanced->BindAction(Secondary,ETriggerEvent::Started,this,&ADemoCharacter::EquipSecondaryPressed);
		if (Cycle) Enhanced->BindAction(Cycle,ETriggerEvent::Started,this,&ADemoCharacter::CyclePrimaryPressed);
		if (!Fire || !Reload || !Aim || !Primary || !Secondary || !Cycle) UE_LOG(LogFPSDemo, Error,TEXT("Missing combat InputAction assets"));
	}
	else UE_LOG(LogFPSDemo, Error, TEXT("EnhancedInputComponent missing: movement unavailable"));
	Input->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ADemoCharacter::JumpPressed);
	Input->BindKey(EKeys::SpaceBar, IE_Released, this, &ADemoCharacter::JumpReleased);
	Input->BindKey(EKeys::LeftShift, IE_Pressed, this, &ADemoCharacter::DashPressed);
	Input->BindKey(EKeys::Q, IE_Pressed, this, &ADemoCharacter::HealPressed);
	Input->BindKey(EKeys::E, IE_Pressed, this, &ADemoCharacter::InteractPressed);
}

void ADemoCharacter::MoveDemo(const FInputActionValue& Value)
{
	DEMO_LOG_TICK();
	// 二维轴 X 为左右、Y 为前后；Controller IgnoreMoveInput 在菜单/终局拦截。
	const FVector2D Axis = Value.Get<FVector2D>();
	if (Controller) { AddMovementInput(GetActorForwardVector(), Axis.Y); AddMovementInput(GetActorRightVector(), Axis.X); }
}
void ADemoCharacter::LookDemo(const FInputActionValue& Value)
{
	DEMO_LOG_TICK();
	// 视角轴沿用模板灵敏度，菜单通过 Controller IgnoreLookInput 拦截。
	const FVector2D Axis = Value.Get<FVector2D>();
	// Settings由Engine持有；未指定自定义类时保持1倍，避免无法转动视角。
	const UDemoGameUserSettings* Settings = Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings());
	const float Sensitivity = Settings ? Settings->GetMouseSensitivity() : 1.f; // 本机输入倍率。
	AddControllerYawInput(Axis.X * Sensitivity);
	AddControllerPitchInput(Axis.Y * Sensitivity);
}
void ADemoCharacter::JumpPressed() { DEMO_LOG_CALL(); if (Controller && !Controller->IsMoveInputIgnored()) Jump(); }
void ADemoCharacter::JumpReleased() { DEMO_LOG_CALL(); StopJumping(); }
// 所有武器按键经Enhanced Input转发，状态校验和计时归装备组件/GAS。
void ADemoCharacter::FirePressed() { DEMO_LOG_CALL(); WeaponComponent->StartFire(); }
void ADemoCharacter::StopFiring() { DEMO_LOG_CALL(); WeaponComponent->StopFire(); }
void ADemoCharacter::ReloadPressed() { DEMO_LOG_CALL(); WeaponComponent->RequestReload(); }
void ADemoCharacter::AimPressed() { DEMO_LOG_CALL(); WeaponComponent->CycleAim(); }
void ADemoCharacter::EquipPrimaryPressed() { DEMO_LOG_CALL(); WeaponComponent->EquipSlot(1); }
void ADemoCharacter::EquipSecondaryPressed() { DEMO_LOG_CALL(); WeaponComponent->EquipSlot(2); }
void ADemoCharacter::CyclePrimaryPressed() { DEMO_LOG_CALL(); WeaponComponent->CyclePrimary(); }
UDemoWeaponComponent* ADemoCharacter::GetWeaponComponent() const { DEMO_LOG_TICK(); return WeaponComponent; }
void ADemoCharacter::DashPressed() { DEMO_LOG_CALL(); RequestAbility(UDemoDashAbility::StaticClass()); }
void ADemoCharacter::HealPressed() { DEMO_LOG_CALL(); RequestAbility(UDemoHealAbility::StaticClass()); }
void ADemoCharacter::RequestAbility(TSubclassOf<UGameplayAbility> AbilityClass)
{
	DEMO_LOG_CALL();
	if (BoundASC.IsValid()) BoundASC->ActivateDemoAbility(AbilityClass);
}

void ADemoCharacter::PerformShot()
{
	DEMO_LOG_CALL();
	// 保留角色侧GA调用接口，实际射线、成本许可和表现全部转交当前武器。
	if (ADemoWeaponBase* Weapon = WeaponComponent->GetActiveWeapon()) Weapon->ExecuteCommittedShot();
}

void ADemoCharacter::PerformDash()
{
	DEMO_LOG_CALL();
	if (!HasAuthority() || !BoundASC.IsValid() || !CanUsePlayerSkills())
	{
		UE_LOG(LogFPSDemo, Log, TEXT("Dash action rejected: authority/ASC/skill phase/menu"));
		return;
	}
	// 只有成功Commit后调用此动作；短时窗口和4秒冷却分离，早冲刺会在Boss结算前过期。
	DemoEffects::Apply(BoundASC.Get(), BoundASC.Get(), UDemoDashEvadeEffect::StaticClass(), 0.f);
	// 使用水平输入方向，避免看向天空即可飞出竞技场。
	FVector Direction = GetLastMovementInputVector().GetSafeNormal2D();
	if (Direction.IsNearlyZero()) Direction = GetActorForwardVector().GetSafeNormal2D();
	LaunchCharacter(Direction * DashSpeed + FVector(0,0,120.f), true, true);
}
bool ADemoCharacter::IsDashEvading() const
{
	DEMO_LOG_TICK();
	return BoundASC.IsValid() && BoundASC->HasMatchingGameplayTag(DemoTags::DashEvading);
}
void ADemoCharacter::OnMoveSpeedChanged(const FOnAttributeChangeData& Data)
{
	DEMO_LOG_CALL();
	// GAS减速与武器开镜速度组合，退出其中任一状态不会覆盖另一个状态。
	RefreshMovementSpeed();
	UE_LOG(LogFPSDemo, Log, TEXT("MOVE_SPEED multiplier=%.2f walk=%.2f"), Data.NewValue, GetCharacterMovement()->MaxWalkSpeed);
}
void ADemoCharacter::RefreshMovementSpeed()
{
	DEMO_LOG_CALL();
	// Attributes仅本次借用；ASC初始化前按1倍，开镜倍率由装备组件只读提供。
	const UDemoAttributeSet* Attributes = GetDemoAttributes();
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * (Attributes ? Attributes->GetMoveSpeedMultiplier() : 1.f)
		* (WeaponComponent ? WeaponComponent->GetAimMoveMultiplier() : 1.f);
}
void ADemoCharacter::ClearAttackStatuses()
{
	DEMO_LOG_CALL();
	if (!HasAuthority() || !BoundASC.IsValid()) return;
	// 临时筛选本功能的短期状态；保留冷却与升级效果，名称避开 AActor::Tags 以通过遮蔽检查。
	FGameplayTagContainer AttackStatusTags;
	AttackStatusTags.AddTag(DemoTags::Slowed);
	AttackStatusTags.AddTag(DemoTags::DashEvading);
	BoundASC->RemoveActiveEffectsWithGrantedTags(AttackStatusTags);
}
void ADemoCharacter::ApplyAbilityReward(int32 Choice)
{
	DEMO_LOG_CALL();
	if (!HasAuthority()) return;
	// 无尽奖励达到存档数值边界后饱和；仍允许领取推进，不能产生无法恢复的检查点。
	if (Choice == 0) DemoEffects::Apply(BoundASC.Get(), BoundASC.Get(), UDemoPowerEffect::StaticClass(), FMath::Clamp(10000.f-GetDemoAttributes()->GetWeaponDamageBonus(),0.f,10.f));
	else if (Choice == 1) HealAmount = FMath::Min(100000.f,HealAmount+20.f);
	else if (Choice == 2) DashSpeed = FMath::Min(100000.f,DashSpeed+350.f);
}
float ADemoCharacter::GetHealAmount() const { DEMO_LOG_TICK(); return HealAmount; }
float ADemoCharacter::GetDashSpeed() const { DEMO_LOG_TICK(); return DashSpeed; }

void ADemoCharacter::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	DEMO_LOG_CALL();
	if (Data.NewValue < Data.OldValue) LastDamageTime = GetWorld()->GetTimeSeconds();
	if (Data.NewValue <= 0.f && HasAuthority())
	{
		// 死亡先阻止新技能，再通知状态机；不在此属性回调中销毁 Pawn/ASC。
		BoundASC->AddLooseGameplayTag(DemoTags::Dead);
		StopFiring();
		if (AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) Mode->NotifyPlayerDied();
	}
}
void ADemoCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	DEMO_LOG_CALL();
	// 保留 Pawn/Controller 供终局 UI 重试，不调用会销毁 Pawn 的父实现。
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	if (AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) Mode->NotifyPlayerDied();
}

ADemoInteractable* ADemoCharacter::FindInteractable() const
{
	DEMO_LOG_TICK();
	// 只寻找当前区域仍有效的一对终端；切换战斗时旧终端销毁，无跨帧裸指针缓存。
	ADemoInteractable* Closest = nullptr;
	float Distance = 250.f;
	// It 遍历当前 World 的交互对象，距离计算单位厘米。
	for (TActorIterator<ADemoInteractable> It(GetWorld()); It; ++It)
	{
		const float CandidateDistance = FVector::Dist(GetActorLocation(), It->GetActorLocation());
		if (CandidateDistance < Distance) { Distance = CandidateDistance; Closest = *It; }
	}
	return Closest;
}
void ADemoCharacter::InteractPressed()
{
	DEMO_LOG_CALL();
	// 菜单打开时 E 交给控制器关闭，Reward 菜单必须完成选择。
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(Controller);
	if (PC && PC->IsUpgradeMenuOpen()) { PC->CloseUpgradeMenu(); return; }
	if (ADemoInteractable* Target = FindInteractable()) Target->Interact(this);
}

void ADemoCharacter::RestoreRunProgress(const UDemoRunSave& Data)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !GetDemoASC() || !Data.Validate()) { UE_LOG(LogFPSDemo, Warning, TEXT("Restore progress rejected")); return; }
    // 存档恢复是受控基础值初始化，先设置生命上限再恢复生命，不恢复短期标签/冷却。
    GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetMaxHealthAttribute(), Data.MaxHealth);
    GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), Data.Health);
    GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetWeaponDamageBonusAttribute(), Data.DamageBonus);
    GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetMagazineBonusAttribute(), Data.MagazineBonus);
    HealAmount = Data.HealAmount;
    DashSpeed = Data.DashSpeed;
}
