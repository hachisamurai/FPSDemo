// 职责：玩家Avatar及输入/组件协调。使用：PS提供ASC，战斗退出和死亡撤销短期状态；VFX组件消费同一ASC，设备视角与业务状态分离。
#include "Characters/DemoCharacter.h"
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackComponent.h"
#include "Progression/Builds/DemoRunBuildComponent.h" // 个人死亡撤销核心瞬态，不改变固定队伍检查点选择。
#include "VFX/DemoDashTrailComponent.h" // 全身姿态残影归Pawn，ASC就绪后直接绑定，不依赖初始武器复制完成。
#include "Weapons/Melee/DemoMeleeComponent.h" // 独立近战槽，枪械库存与ASC动画通道保持原职责。
#include "Combat/Feedback/DemoHitFeedbackComponent.h" // Pawn拥有瞬时命中回执；不把反馈状态写入PS成长或存档。
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/DemoTalentComponent.h" // 长时天赋取消集中走PS句柄管理器，不扩展角色内的具体天赋分支。
#include "GAS/Gambling/DemoCombatTalentComponent.h" // Pawn拥有触发窗口，账本仍属于PS。
// 对应头文件先包含，确保UBT独立编译检查本类头文件自包含；功能逻辑不变。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Player/DemoPlayerProgressComponent.h" // Avatar初始化需要完整个人策略类型。
#include "Economy/DemoUpgradeCatalog.h"
#include "Economy/DemoShopComponent.h" // 独立物理设备租约只拒绝技能，不伪造菜单/暂停，不改相机或移动。
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Player/DemoPlayerCommands.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Animation/DemoPlayerPresentationComponent.h" // 全身模型和独立TP枪，无第一人称成本/动画所有权。
#include "Animation/DemoOwnerShadowComponent.h" // 本机正常尺度最终姿态阴影，不改相机、服务器骨姿或ASC。
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
#include "Interaction/HubShops/DemoShopFocusComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h" // 同一IA_Fire先交PC主键归属，Operate按钮不得穿透为枪/近战。
#include "Game/FPSDemoGameMode.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/Abilities/DemoPlayerAbilityTypes.h"
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
	bReplicates = true; SetReplicateMovement(true); // CharacterMovement 处理拥有者输入与服务器校正。
	GetReplicatedMovement_Mutable().RotationQuantizationLevel=ERotatorQuantization::ShortComponents; // 头身分离后身体也使用16位角压缩，避免原byte约1.4度步距让远端阈值/枪向跳动。
	// FaceRotation统一静止阈值/移动跟随；不能启用按速度朝向，否则横移和后退会背离相机，保留原生移动复制与平滑。
	bUseControllerRotationYaw=true; GetCharacterMovement()->bOrientRotationToMovement=false; GetCharacterMovement()->bUseControllerDesiredRotation=false;
	// ACharacter原生Mesh改为完整人类骨骼体；资源由Appearance闭包加载，原占位圆柱不再创建。
	GetMesh()->SetOwnerNoSee(true); GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlayerPresentation = CreateDefaultSubobject<UDemoPlayerPresentationComponent>(TEXT("PlayerPresentation"));
	CreateDefaultSubobject<UDemoOwnerShadowComponent>(TEXT("OwnerShadow")); // 随Avatar生命周期，来源完整且本人视点时才接管原隐藏投影。
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
	CreateDefaultSubobject<UDemoCombatTalentComponent>(TEXT("CombatTalents")); // 无Tick；重生清除回血窗口与冲刺凭据。
	CreateDefaultSubobject<UDemoHitFeedbackComponent>(TEXT("HitFeedback")); // 默认子对象提供拥有者RPC；服务器聚合、仅射手显示，不依赖当前切换武器。
	MeleeComponent=CreateDefaultSubobject<UDemoMeleeComponent>(TEXT("MeleeComponent")); // 同一个既有子对象；反射持有让子蓝图配置覆盖可保存，不新增组件或改变近战状态复制。
	DashTrailComponent=CreateDefaultSubobject<UDemoDashTrailComponent>(TEXT("DashTrail")); // 消费实际GAS窗口冻结全身姿态；配色来自残影材质/组件，无独立Actor或伤害逻辑。
}

void ADemoCharacter::PossessedBy(AController* NewController)
{
	DEMO_LOG_CALL(); Super::PossessedBy(NewController); InitializeAbilitySystem();
	if (HasAuthority() && OutfitColorIndex >= 4)
	{
		TArray<uint8> Available = {0,1,2,3}; // 本World活跃Pawn已用颜色排除，2～4人通常可分配不同色。
		for (TActorIterator<ADemoCharacter> It(GetWorld()); It; ++It) // World拥有Pawn，仅在服务器Possess同步借用。
			if (*It != this && It->GetController() && It->OutfitColorIndex < 4) Available.Remove(It->OutfitColorIndex);
		OutfitColorIndex = Available.IsEmpty() ? 0 : Available[FMath::RandRange(0,Available.Num()-1)]; // 超过四色明确回退，不影响入队。
		if (Available.IsEmpty()) UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_COLOR palette capacity reached; using teal"));
		OnRep_OutfitColor(); ForceNetUpdate();
	}
}
void ADemoCharacter::OnRep_PlayerState() { DEMO_LOG_CALL(); Super::OnRep_PlayerState(); InitializeAbilitySystem(); }
void ADemoCharacter::OnRep_Controller() { DEMO_LOG_CALL(); Super::OnRep_Controller(); InitializeAbilitySystem(); }

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
	DashTrailComponent->BindAbilitySystem(BoundASC.Get()); // 支持PS/Controller乱序到达；相同ASC重复初始化不叠加委托或重播窗口。
    if(HasAuthority()) DemoGambleEffects::Sync(BoundASC.Get(),State->GetProgressComponent()->GetSnapshot().Gambling); // 新Avatar恢复同一PS策略。
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
    // 最后一步才通知流程；监听者可能同步读档，之后不能再重置属性或库存。
    if (BoundASC->GetAvatarActor() == this && WeaponComponent->GetActiveWeapon()) OnDemoAvatarReady.Broadcast();
    if (WeaponComponent->GetActiveWeapon()) WeaponComponent->GetActiveWeapon()->RefreshReplicatedPresentation(); // 拥有者/ASC先后顺序不决定表现能否绑定成功。
}

void ADemoCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DEMO_LOG_CALL();
	OnCharacterMovementUpdated.RemoveDynamic(this,&ADemoCharacter::OnViewMovementUpdated); // 当前Pawn结束后不再消费移动回调，避免卸载/复用时重复绑定。
	DashTrailComponent->BindAbilitySystem(nullptr); // 先清本Pawn表现，旧PS移除标签/清ActorInfo时不再触发特效回调。
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
UDemoPlayerPresentationComponent* ADemoCharacter::GetPlayerPresentation() const { DEMO_LOG_TICK(); return PlayerPresentation; }
uint8 ADemoCharacter::GetOutfitColorIndex() const { DEMO_LOG_TICK(); return OutfitColorIndex; }
void ADemoCharacter::OnRep_OutfitColor() { DEMO_LOG_CALL(); if (PlayerPresentation) PlayerPresentation->ApplyOutfitColor(OutfitColorIndex); }
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
	// 保留战斗专用动作边界；备战练枪使用独立CanUseFirearmActions，不能给伤害或开镜扩权。
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	const ADemoPlayerController* PC = Cast<ADemoPlayerController>(Controller);
	return State && State->Phase == EDemoPhase::Combat && PC && !bCoopEliminated && (PC->IsLocalController() || PC->IsCoopRegistered())
        && (!IsLocallyControlled() || (!PC->IsUpgradeMenuOpen() && !PC->HasBlockingOverlay())) && !GetWorld()->IsPaused(); // 服务器不依赖远端未复制的菜单状态。
}
bool ADemoCharacter::CanUseFirearmActions() const
{
	DEMO_LOG_TICK();
	const ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 只借用当前World阶段，不缓存上一关许可。
	return State && (State->Phase == EDemoPhase::Combat || State->Phase == EDemoPhase::Intermission) && CanUsePlayerSkills();
}
bool ADemoCharacter::CanUsePlayerSkills() const
{
	DEMO_LOG_TICK();
	const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 借用当前阶段，读档后重新解析，不缓存旧World。
	const ADemoPlayerController* PC = Cast<ADemoPlayerController>(Controller); // 本Avatar的控制器；菜单与原生输入共用阻塞条件。
	const UDemoAttributeSet* Attributes = GetDemoAttributes(); // 只读生命；冷却/Dead标签仍由GAS权威检查。
	const AFPSDemoGameMode* Mode=HasAuthority()?GetWorld()->GetAuthGameMode<AFPSDemoGameMode>():nullptr; // 远端不能以本机bOperating作为服务器安全门。
	const bool bPhysicalOperator=(IsLocallyControlled() && PC && PC->GetMenuFlow()->IsCasinoDeviceOperating()) || (Mode && Mode->GetShopSystem() && Mode->GetShopSystem()->IsPhysicalCasinoOperator(this)); // 本地立即挡输入，服务器沿自己冻结的短租约挡真实技能入口；不影响站位/转头。
	return State && (State->Phase == EDemoPhase::Combat || State->Phase == EDemoPhase::Intermission || State->Phase == EDemoPhase::Hub)
		&& PC && (PC->IsLocalController() || PC->IsCoopRegistered()) && Attributes && Attributes->GetHealth() > 0.f && !bCoopEliminated
        && (!IsLocallyControlled() || (!PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen() && !PC->HasBlockingOverlay()))
		&& !bPhysicalOperator && !GetWorld()->IsPaused(); // 远端物理操作沿服务器租约拒绝；普通远端菜单仍保原语义，不刷新冷却。
}

void ADemoCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
	DEMO_LOG_CALL();
	// 父类模板需要 BP 设置动作，本 Pawn 显式加载同一套资产，不调用模板绑定以避免重复。
	UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(Input);
	if (Enhanced)
	{
		// Controller的映射上下文保活动作；移动Action另由Pawn强引用，供Shift在同帧读取轴值，视角Action只用于绑定。
		MoveInputAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/FirstPerson/Input/Actions/IA_Move.IA_Move")); // 保存实际Action；Shift从EnhancedPlayerInput读当帧轴，不用服务器不会更新的LastMovementInputVector。
		UInputAction* LookActionAsset = LoadObject<UInputAction>(nullptr, TEXT("/Game/FirstPerson/Input/Actions/IA_Look.IA_Look"));
		if (MoveInputAction) Enhanced->BindAction(MoveInputAction, ETriggerEvent::Triggered, this, &ADemoCharacter::MoveDemo);
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
			Enhanced->BindAction(Fire,ETriggerEvent::Completed,this,&ADemoCharacter::FireReleased);
			Enhanced->BindAction(Fire,ETriggerEvent::Canceled,this,&ADemoCharacter::FireReleased);
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
    const ADemoPlayerController* InteractionPC=Cast<ADemoPlayerController>(Controller); // PC拥有Enhanced交互动作，避免同一E双触发。
    const UDemoShopFocusComponent* Focus=InteractionPC?InteractionPC->FindComponentByClass<UDemoShopFocusComponent>():nullptr;
    if (!Focus || !Focus->HasEnhancedInteraction()) Input->BindKey(GetDefault<UDemoHubShopInputSettings>()->FallbackKey, IE_Pressed, this, &ADemoCharacter::InteractPressed);
	// 数字3由Controller.SelectThird按菜单/正常游玩分发，避免第三项奖励吞键或一键同时选奖励与装备近战。
}

void ADemoCharacter::MoveDemo(const FInputActionValue& Value)
{
	DEMO_LOG_TICK();
	// 二维轴X左右/Y前后，按真实相机水平朝向移动；头身分离后不能再以身体为WASD坐标系。菜单仍由IgnoreMoveInput阻塞。
	const FVector2D Axis = Value.Get<FVector2D>(); // 当前Enhanced Input二维轴，生命周期仅本次回调。
	const FRotator View=GetMovementViewRotation(); // 同冲刺共用去俯仰的输入坐标系。
	if (Controller) { AddMovementInput(View.Vector(), Axis.Y); AddMovementInput(View.RotateVector(FVector::RightVector), Axis.X); }
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
void ADemoCharacter::FirePressed()
{
    DEMO_LOG_CALL();
    if (ADemoPlayerController* PC=Cast<ADemoPlayerController>(Controller))
        if (UDemoCasinoDeviceInputComponent* DeviceInput=PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>()) if (DeviceInput->TryRoutePrimaryPressed()) return;
    WeaponComponent->StartFire(); // 没有设备/模态消费时保持原靶场、近战与枪械GAS入口。
}
void ADemoCharacter::FireReleased()
{
    DEMO_LOG_CALL();
    if (ADemoPlayerController* PC=Cast<ADemoPlayerController>(Controller))
        if (UDemoCasinoDeviceInputComponent* DeviceInput=PC->FindComponentByClass<UDemoCasinoDeviceInputComponent>()) if (DeviceInput->TryRoutePrimaryReleased()) return;
    StopFiring(); // 只有本次武器归属或未捕获的旧输入才停武器，数字释放不逐次发RPC。
}
void ADemoCharacter::StopFiring() { DEMO_LOG_CALL(); WeaponComponent->StopFire(); }
void ADemoCharacter::ReloadPressed() { DEMO_LOG_CALL(); WeaponComponent->RequestReload(); }
void ADemoCharacter::AimPressed() { DEMO_LOG_CALL(); WeaponComponent->CycleAim(); }
void ADemoCharacter::EquipPrimaryPressed() { DEMO_LOG_CALL(); WeaponComponent->EquipSlot(1); }
void ADemoCharacter::EquipSecondaryPressed() { DEMO_LOG_CALL(); WeaponComponent->EquipSlot(2); }
void ADemoCharacter::CyclePrimaryPressed() { DEMO_LOG_CALL(); WeaponComponent->CyclePrimary(); }
UDemoWeaponComponent* ADemoCharacter::GetWeaponComponent() const { DEMO_LOG_TICK(); return WeaponComponent; }
void ADemoCharacter::HealPressed() { DEMO_LOG_CALL(); RequestAbility(UDemoHealAbility::StaticClass()); }
void ADemoCharacter::RequestAbility(TSubclassOf<UGameplayAbility> AbilityClass)
{
	DEMO_LOG_CALL();
	if (BoundASC.IsValid()) BoundASC->ActivateDemoAbility(AbilityClass);
}

void ADemoCharacter::PerformShot()
{
	DEMO_LOG_CALL();
	// 保留旧角色调用接口；这里只转发实体发射，武器拒绝没有准备/成本凭据的调用，不能退回射线扣血。
	if (ADemoWeaponBase* Weapon = WeaponComponent->GetActiveWeapon()) Weapon->ExecuteCommittedShot();
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
void ADemoCharacter::OnCombatSessionEnded()
{
    DEMO_LOG_CALL();
    GetWeaponComponent()->CancelActions(); ClearAttackStatuses(); // 角色自行协调组件，Flow不再逐个调用武器和状态接口。
}
void ADemoCharacter::ClearAttackStatuses()
{
	DEMO_LOG_CALL();
	if (!HasAuthority() || !BoundASC.IsValid() || BoundASC->GetAvatarActor()!=this)
    { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_CLEANUP rejected authority/obsolete avatar")); return; }
    if(APlayerController* FeedbackPC=Cast<APlayerController>(GetController()); FeedbackPC&&FeedbackPC->GetPawn()==this)
        if(UDemoPlayerDamageFeedbackComponent* Feedback=UDemoPlayerDamageFeedbackComponent::Find(FeedbackPC)) Feedback->ResetForLifecycle(); // 通过原Avatar门禁才清本人的装饰；旧Pawn晚清理不能撤销新Pawn反馈。
    if(ADemoPlayerState* OwnerState=GetPlayerState<ADemoPlayerState>()) // 当前PS借用；旧Avatar晚结束不会取消新Avatar的任务。
        OwnerState->GetTalentComponent()->CancelActiveBehaviors();
	// 临时筛选本功能的短期状态；保留冷却与升级效果，名称避开 AActor::Tags 以通过遮蔽检查。
	FGameplayTagContainer AttackStatusTags; // 本Avatar短期状态筛选，保留被动天赋权限/数值与普通技能冷却。
	AttackStatusTags.AddTag(DemoTags::Slowed);
    AttackStatusTags.AddTag(DemoTalentEvents::CombatTransient); // 独立计时猎杀移速层不跨关、死亡或重生残留。
	AttackStatusTags.AddTag(DemoTags::DashEvading);
	BoundASC->RemoveActiveEffectsWithGrantedTags(AttackStatusTags);
}
bool ADemoCharacter::ApplyAbilityReward(int32 Choice)
{
	DEMO_LOG_CALL();
    const UDemoUpgradeCatalog* Catalog=UDemoUpgradeCatalog::Get(); // 单一配置源，UI显示同一服务器投影。
    const FDemoUpgradeDefinition* Item=Catalog?Catalog->Find(true,Choice):nullptr; // 旧编号仅映射稳定策略，不读取展示文字授权。
    if (!HasAuthority() || !GetDemoAttributes() || !Item) { UE_LOG(LogFPSDemo,Warning,TEXT("REWARD_APPLY rejected authority/config")); return false; }
    // 已购值高于新配置上限时保留原值；奖励只饱和增量，绝不反向降低玩家属性。
    if (Choice==0) return DemoEffects::Apply(BoundASC.Get(),BoundASC.Get(),UDemoPowerEffect::StaticClass(),FMath::Clamp(Item->Cap-GetDemoAttributes()->GetWeaponDamageBonus(),0.f,Item->Increment));
    if (Choice==1) HealAmount += FMath::Clamp(Item->Cap-HealAmount,0.f,Item->Increment);
    else DashSpeed += FMath::Clamp(Item->Cap-DashSpeed,0.f,Item->Increment);
    ForceNetUpdate(); return true;
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
		if(UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(this)) Build->NotifyAvatarDefeated(); // 仅撤当场窗口，清关复活保留本轮选择。
		SetCoopEliminated(true); // 阵亡不销毁库存，队友清关后由服务器复活。
		if (AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) Mode->NotifyPlayerDied(this);
	}
}
void ADemoCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	DEMO_LOG_CALL();
	// 保留 Pawn/Controller 供终局 UI 重试，不调用会销毁 Pawn 的父实现。
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	if (HasAuthority() && GetDemoASC()) GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), 0.f); // 统一触发生命回调，不能仍被计为存活队员。
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
    if (ADemoPlayerController* OwnerPC=Cast<ADemoPlayerController>(Controller))
        if (UDemoShopFocusComponent* Focus=OwnerPC->FindComponentByClass<UDemoShopFocusComponent>()) if (Focus->TryConsumeInteraction()) return;
	// 菜单打开时 E 交给控制器关闭，Reward 菜单必须完成选择。
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(Controller);
	if (PC && PC->IsUpgradeMenuOpen()) { PC->CloseUpgradeMenu(); return; }
	if (ADemoInteractable* Target = FindInteractable()) Target->Interact(this);
}
void ADemoCharacter::TriggerWorldInteraction() { DEMO_LOG_CALL(); InteractPressed(); }

bool ADemoCharacter::ResetTemporaryRunAttributes()
{
    DEMO_LOG_CALL();
    const ADemoPlayerState* State=GetPlayerState<ADemoPlayerState>(); // 只读当前Pawn本人永久来源，不借房主或其他玩家基线。
    const UDemoPlayerProgressComponent* Progress=State?State->GetProgressComponent():nullptr; // 本人PS运行账本，只在同步重置栈借用。
    UDemoAbilitySystemComponent* ASC=GetDemoASC(); // 本次同步栈借用PS持有的ASC，旧Avatar不得重置已经绑定到新Pawn的属性。
    if (!HasAuthority() || !State || !Progress || !ASC || ASC!=State->GetDemoASC() || ASC->GetAvatarActor()!=this || !Progress->GetSnapshot().UpgradeProgress.Validate())
    { UE_LOG(LogFPSDemo,Warning,TEXT("RUN_GROWTH_ATTRIBUTES_RESET_REJECT owner/avatar/permanent ledger pawn=%s"),*GetName()); return false; }
    const FDemoUpgradeProgress Permanent=Progress->GetSnapshot().UpgradeProgress; // 同步冻结本人来源值；下方GAS属性通知不能改本次重置目标。
    const float PermanentMaxHealth=100.f+Permanent.PermanentHealth; // HP，永久金币生命增量不含初始100；银币/免费奖励合计不能反推永久来源。
    const float PreviousMaxHealth=GetDemoAttributes()->GetMaxHealth(); // 只作可审计前后对比，不参与永久值计算。
    const float PreviousDamage=GetDemoAttributes()->GetWeaponDamageBonus(); // 原伤害加值，仅日志对比，不用它倒推金币来源。
    const float PreviousMagazine=GetDemoAttributes()->GetMagazineBonus(); // 原容量加值，单位发，仅日志对比。
    // 同Pawn终局重置属于运行期属性初始化，不伪造缺槽身份/枪权的V10 SaveGame，也不放宽正常RestoreRunProgress验证。
    ASC->SetNumericAttributeBase(UDemoAttributeSet::GetMaxHealthAttribute(),PermanentMaxHealth);
    ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),PermanentMaxHealth);
    ASC->SetNumericAttributeBase(UDemoAttributeSet::GetWeaponDamageBonusAttribute(),Permanent.PermanentDamage);
    ASC->SetNumericAttributeBase(UDemoAttributeSet::GetMagazineBonusAttribute(),Permanent.PermanentMagazine);
    HealAmount=35.f; DashSpeed=1300.f; // 整轮免费技能成长归零；保留ASC已存在的正常技能冷却和其他玩家属性。
    UE_LOG(LogFPSDemo,Log,TEXT("RUN_GROWTH_ATTRIBUTES_RESET pawn=%s maxHealth=%.1f->%.1f permanentHealth=%.1f damage=%.1f->%.1f magazine=%.0f->%.0f"),
        *GetName(),PreviousMaxHealth,GetDemoAttributes()->GetMaxHealth(),Permanent.PermanentHealth,PreviousDamage,GetDemoAttributes()->GetWeaponDamageBonus(),PreviousMagazine,GetDemoAttributes()->GetMagazineBonus());
    return true;
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

void ADemoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADemoCharacter, bCoopEliminated);
    DOREPLIFETIME(ADemoCharacter, DashPresentation);
    DOREPLIFETIME(ADemoCharacter, OutfitColorIndex); // 一次外观分配复制给所有观察者，客户端不独立随机。
    DOREPLIFETIME_CONDITION(ADemoCharacter, RemoteViewYaw, COND_SkipOwner); // 自主端直接读ControlRotation；其他端需独立视线才能显示阈值内转头。
    DOREPLIFETIME_CONDITION(ADemoCharacter, HealAmount, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(ADemoCharacter, DashSpeed, COND_OwnerOnly);
}
bool ADemoCharacter::IsCoopEliminated() const { DEMO_LOG_TICK(); return bCoopEliminated; }
void ADemoCharacter::SetCoopEliminated(bool bEliminated)
{
    DEMO_LOG_CALL();
    if (!HasAuthority()) { UE_LOG(LogFPSDemo, Log, TEXT("Elimination update rejected: authority")); return; }
    bCoopEliminated = bEliminated;
    if(APlayerController* FeedbackPC=Cast<APlayerController>(GetController()); FeedbackPC&&FeedbackPC->GetPawn()==this)
        if(UDemoPlayerDamageFeedbackComponent* Feedback=UDemoPlayerDamageFeedbackComponent::Find(FeedbackPC))
        { if(bEliminated) Feedback->ResetForLifecycle(true); else Feedback->RefreshAvatarBinding(); } // 只更新仍属于本人的Avatar；同Pawn复活也换代，旧回执不得重播。
    if(bEliminated) ClearAttackStatuses(); // 个体死亡也清除独立移速层；不能等待全队清关或自然到期。
    if (GetDemoASC()) GetDemoASC()->SetLooseGameplayTagCount(DemoTags::Dead, bEliminated ? 1 : 0);
    OnRep_Eliminated(); ForceNetUpdate();
}
void ADemoCharacter::OnRep_Eliminated()
{
    DEMO_LOG_CALL();
    DashTrailComponent->RefreshFromAbilitySystem(); // 死亡标签可能是服务器LooseTag，复制阵亡边沿也必须立即清本端尾迹，不等待0.6秒兜底。
    GetCapsuleComponent()->SetCollisionEnabled(bCoopEliminated ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    GetMesh()->SetVisibility(!bCoopEliminated); // 完整身体沿用原死亡隐藏；TP枪由表现组件读取同一生死状态。
    if (bCoopEliminated) { GetCharacterMovement()->RemoveRootMotionSource(TEXT("DemoDash")); GetCharacterMovement()->StopMovementImmediately(); GetCharacterMovement()->DisableMovement(); WeaponComponent->CancelActions(); } // 死亡立即撤销尚未到期的闪避源，复活不恢复旧推力。
    else GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(GetController()); PC && PC->IsLocalController() && PC->GetMenuFlow()) PC->GetMenuFlow()->RefreshFromReplicatedState(); // 复制时序不依赖菜单快照与生命先后。
}
