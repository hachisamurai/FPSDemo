// 职责：玩家配色、TP动画、本人枪臂瞄准及原世界影子的单写策略；正常尺度代理成功才撤销本人原隐藏投影，ASC仍绑定Mesh1P。
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Animation/DemoOwnerBodyLODComponent.h" // 本机Leader完整骨骼LOD保护，独立PrePhysics Tick先于Body求值。
#include "Animation/DemoOwnerLowerBodyAnimInstance.h" // 本人下身不继承TP Dash/武器层；原生窄图仅复用已烘焙移动资源。
#include "Animation/DemoLocomotionSet.h"
#include "Player/DemoPlayerState.h" // 声音身份属于PS，换Pawn仍借用同一语音幅度。
#include "Player/Voice/DemoVoiceComponent.h"
#include "Engine/SkeletalMesh.h" // 初始化检查真实Morph存在，不能只写入无效果的曲线名。
#include "Engine/SkinnedAssetCommon.h" // FSkeletalMaterial完整定义；Owner分区独立槽不能假定与TP等长。
#include "Weapons/Melee/DemoMeleeComponent.h" // 独立装备状态决定TP枪是否应可见。
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/DemoPlayerEyesComponent.h" // 表情使用真实head局部瞄准，不修改共享Skeleton。
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoLyraAnimInstance.h" // Lyra动态层在骨骼求值之外挂接，确保PropertyAccess先执行下一帧PreUpdate。
#include "Animation/DemoPlayerAssetPreload.h" // GI加载与Pawn候选选择共享同一路径，避免预加载了错误版本。
#include "Animation/DemoFPAnimInstance.h"
#include "Animation/DemoPlayerWeaponAnimInstance.h" // 第三人称机械使用与身体相同的phase及取消混合。
#include "Animation/DemoWeaponAnimationComponent.h" // 新肤臂替换后重新验证主实例/ASC动画绑定。
#include "Animation/AnimSequence.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h" // 组件生命周期和实际展示变换属于当前World。
#include "GameFramework/CharacterMovementComponent.h" // 独立Owner下身在真实CMC更新之后采样速度/空中状态。
#include "Debug/DemoLog.h"
#include "Misc/CommandLine.h" // 仅WITH_EDITOR候选验收入口，不改变打包资产选择。
#include "Misc/Parse.h"
#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
namespace
{
    // 仅Editor目标：实机调试CS2式viewmodel偏移，"X Y Z"相机局部cm（各轴|v|<=30），空串用组件默认。非Editor包体不注册。
    TAutoConsoleVariable<FString> CVarDemoViewmodelOffset(TEXT("Demo.Feel.ViewmodelOffset"), TEXT(""),
        TEXT("Editor only: first-person viewmodel offset 'X Y Z' cm in camera space (forward right up); empty uses default."), ECVF_Cheat);
    // 仅Editor目标：FP-HANDAIM A/B，-1用组件默认，0强制关闭，1强制开启。非Editor包体不注册。
    TAutoConsoleVariable<int32> CVarDemoHandAim(TEXT("Demo.Feel.HandAim"), -1,
        TEXT("Editor only: first-person template-style hand aim IK. -1 default, 0 off, 1 on."), ECVF_Cheat);
    /** FP-HANDAIM视角惯性半衰期覆盖（秒）；<0用组件默认，0无惯性，上限0.2。仅Editor手感调参，不进包体。 */
    TAutoConsoleVariable<float> CVarDemoHandAimLag(TEXT("Demo.Feel.HandAimLag"), -1.f,
        TEXT("Editor only: FP hand aim view-lag half-life seconds. <0 default (0.03), 0 none, max 0.2."), ECVF_Cheat);
    /** FP-HANDAIM最大修正角覆盖（度）；<=0用组件默认，上限30。仅Editor调参。 */
    TAutoConsoleVariable<float> CVarDemoHandAimMaxDeg(TEXT("Demo.Feel.HandAimMaxDeg"), -1.f,
        TEXT("Editor only: FP hand aim max correction degrees. <=0 default (1.5), max 30."), ECVF_Cheat);
}
#endif
#include "Animation/DemoFPHandAim.h" // FP-HANDAIM输入结构；代理只在动画实例内创建。
#include "Engine/SkeletalMeshSocket.h" // 读取握点socket相对骨的作者变换。

UDemoPlayerPresentationComponent::UDemoPlayerPresentationComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    AppearanceAsset = TSoftObjectPtr<UDemoPlayerAppearance>(FSoftObjectPath(TEXT("/Game/Player/DA_PlayerAppearance.DA_PlayerAppearance"))); // CDO不加载尚未生成的制作资产，正常Cook收集软引用。
}
bool UDemoPlayerPresentationComponent::PrepareFirstPersonMesh()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    ApplyEditorAppearanceOverride(); // Possess可能早于BeginPlay，不能先把正式外观缓存为实际候选。
#endif
    if (!Appearance) Appearance=AppearanceAsset.LoadSynchronous(); // 首次Possess或BeginPlay加载一次；Pawn持有强引用。
    ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwner()); // 只在游戏线程同步借用当前Owner。
    if (!Character || !Appearance || !Appearance->ArmsMesh)
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_FP_PREPARE_MISSING appearance/arms/owner")); return false; }
    USkeletalMeshComponent* Arms=Character->GetMesh1P(); // 原Mesh1P仍是GAS动画目标，不创建替身主实例。
    if (!bCachedAuthoredArmsTransform)
    {
        AuthoredArmsTransform=Arms->GetRelativeTransform(); // 仅首次取未缩模板；之后始终从该作者基线重建，避免累乘。
        bCachedAuthoredArmsTransform=true;
    }
    if (Appearance->bOverrideArmsTransform) AuthoredArmsTransform=Appearance->ArmsTransform;
    if (Arms->GetSkeletalMeshAsset()!=Appearance->ArmsMesh)
    {
        Arms->SetSkeletalMesh(Appearance->ArmsMesh);
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_FP_PREPARED mesh=%s"),*GetNameSafe(Appearance->ArmsMesh));
    }
    FTransform RenderTransform=AuthoredArmsTransform; // 顶层相机子件统一缩location和scale一次，枪/弹匣/瞄镜保留自己的作者相对值。
    const float RenderScale=GetFirstPersonRenderScale();
    RenderTransform.SetTranslation(AuthoredArmsTransform.GetTranslation()*RenderScale);
    RenderTransform.SetScale3D(AuthoredArmsTransform.GetScale3D()*RenderScale);
    Arms->SetRelativeTransform(RenderTransform);
    return true;
}
#if WITH_EDITOR
void UDemoPlayerPresentationComponent::ApplyEditorAppearanceOverride()
{
    DEMO_LOG_CALL();
    const FSoftObjectPath Override=DemoAppearanceAssets::EditorOverridePath(); // 唯一解析与GI相同，无效表示保留当前配置。
    if(Override.IsValid() && AppearanceAsset.ToSoftObjectPath()!=Override)
    {
        AppearanceAsset=TSoftObjectPtr<UDemoPlayerAppearance>(Override);
        Appearance=nullptr; // 只在真正改变路径时撤销早期准备的旧实例引用，不改共享资产。
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_APPEARANCE_CANDIDATE_EDITOR_ONLY appearance=%s"),*AppearanceAsset.ToString());
    }
}
#endif
void UDemoPlayerPresentationComponent::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
#if WITH_EDITOR
    ApplyEditorAppearanceOverride();
#endif
    Appearance = AppearanceAsset.LoadSynchronous(); // 只在初始化加载一次，Tick不触发IO，强引用保活整个Cook闭包。
    ADemoCharacter* Character = Cast<ADemoCharacter>(GetOwner()); // Owner仅本函数同步借用。
    if (!Character || !Appearance || !Appearance->BodyMesh || !Appearance->ArmsMesh || !Appearance->AnimClass)
    { UE_LOG(LogFPSDemo, Error, TEXT("PLAYER_APPEARANCE_MISSING body/arms/AnimClass; run import_breach_player.py")); return; }
    UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_APPEARANCE_CONSUMED pawn=%s asset=%s"),*Character->GetPathName(),*Appearance->GetPathName()); // 实际Pawn强引用身份；GI预载成功不能替代本条消费证据。
    USkeletalMeshComponent* Body = Character->GetMesh(); // ACharacter已有Mesh承担队友全身动画，ASC仍使用Mesh1P。
    Body->SetSkeletalMesh(Appearance->BodyMesh);
    Body->SetRelativeTransform(Appearance->BodyOffset);
    // ACharacter在PostInitializeComponents已缓存模板Mesh的0/单位旋转；BeginPlay换全身后必须同时刷新网络平滑目标。
    // 否则Listen Server的远端Pawn及客户端模拟代理会被SmoothClientPosition恢复到胶囊中心，并丢失模型+Y转角色+X的-90度校正。
    // 只在外观初始化更新一次，不在Tick强改Mesh变换，因此保留CharacterMovement的正常插值与网络校正。
    Character->CacheInitialMeshOffset(Body->GetRelativeLocation(),Body->GetRelativeRotation());
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_BODY_BASE_READY pawn=%s role=%d offset=%s rotation=%s"),*Character->GetName(),static_cast<int32>(Character->GetLocalRole()),*Character->GetBaseTranslationOffset().ToString(),*Character->GetBaseRotationOffset().Rotator().ToString());
    Body->SetAnimInstanceClass(Appearance->AnimClass);
    if(UDemoLyraAnimInstance* Lyra=Cast<UDemoLyraAnimInstance>(Body->GetAnimInstance())) Lyra->RefreshWeaponLayer(); // SetClass已安装主实例，禁止在NativeUpdate中途新建Linked实例。
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetOwnerNoSee(true); Body->CastShadow = true; Body->bCastHiddenShadow=true; // 默认原TP回退；代理成功后由RefreshWorldShadowPolicy撤销本人原隐藏投影。
    Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    bFullBodyViewReady=Appearance->bFullBodyFirstPerson && Appearance->FirstPersonBodyMesh
        && Appearance->FirstPersonBodyMesh->GetSkeleton()==Appearance->BodyMesh->GetSkeleton(); // 只消费相同骨架的离线本人可见分区；稳定眼位不能只依赖去head防自体遮挡。
    if (Appearance->bFullBodyFirstPerson && !bFullBodyViewReady)
        UE_LOG(LogFPSDemo,Error,TEXT("FULL_BODY_VIEW_REJECT missing/incompatible owner mesh; using legacy arms"));
    if (bFullBodyViewReady && GetNetMode()!=NM_DedicatedServer)
    {
        FirstPersonBody=NewObject<USkeletalMeshComponent>(Character,TEXT("PlayerFirstPersonBody")); Character->AddInstanceComponent(FirstPersonBody);
        FirstPersonBody->SetupAttachment(Body); FirstPersonBody->SetSkeletalMesh(Appearance->FirstPersonBodyMesh);
        FirstPersonBody->SetOnlyOwnerSee(true); FirstPersonBody->SetOwnerNoSee(false);
        FirstPersonBody->SetCollisionEnabled(ECollisionEnabled::NoCollision); FirstPersonBody->CastShadow=false;
        FirstPersonBody->SetVisibleInRayTracing(false); // 本人分区不进入HWRT场景，完整TP保留世界反射，避免与下身重复；不宣称新版原生FP自阴影集成。
        FirstPersonBody->bUseBoundsFromLeaderPoseComponent=true; // 默认Leader共用完整Body包围盒；独立下身策略随后改用自身bounds。
        FirstPersonBody->RegisterComponent(); FirstPersonBody->SetLeaderPoseComponent(Body); // 先建立兼容Leader，随后可选独立本人基础下身图；真实TP姿态与骨架不变。
        FirstPersonBody->SetComponentTickEnabled(false);
        UDemoOwnerBodyLODComponent* LODPolicy=NewObject<UDemoOwnerBodyLODComponent>(Character,TEXT("PlayerOwnerBodyLOD")); // 同Pawn拥有，卸载自动恢复并清理。
        Character->AddInstanceComponent(LODPolicy); LODPolicy->RegisterComponent(); LODPolicy->Initialize(Body,FirstPersonBody);
        RefreshOwnerBodyPosePolicy(); // 默认LeaderPose不变；独立本人下身有自己的姿态/LOD/bounds与Tick，不能仍读TP Dash骨缓冲。
        UE_LOG(LogFPSDemo,Log,TEXT("FULL_BODY_VIEW_READY owner=%s mesh=%s leader=%s"),*Character->GetName(),*GetNameSafe(Appearance->FirstPersonBodyMesh),*Body->GetName());
    }
    if (Appearance->HeadMesh)
    {
        Head=NewObject<USkeletalMeshComponent>(Character,TEXT("PlayerSharedHead")); Character->AddInstanceComponent(Head);
        Head->SetupAttachment(Body); Head->SetSkeletalMesh(Appearance->HeadMesh); Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Head->SetOwnerNoSee(true); Head->CastShadow=true; Head->bCastHiddenShadow=true;
        Head->RegisterComponent(); Head->SetLeaderPoseComponent(Body); // 不创建动画实例，眼、颈、躯干来自同帧身体骨。
        bHasMouthMorph = Appearance->HeadMesh->FindMorphTarget(MouthMorphName) != nullptr;
        if (!bHasMouthMorph) UE_LOG(LogFPSDemo, Warning, TEXT("VOICE_MOUTH_MORPH_MISSING head=%s morph=%s; run import_player_voice_head.py"), *GetNameSafe(Appearance->HeadMesh), *MouthMorphName.ToString());
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_SHARED_HEAD_READY mesh=%s"),*GetNameSafe(Appearance->HeadMesh));
    }
    else UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_INTEGRATED_HEAD mesh=%s; owner-hidden full body uses no separate head"),*GetNameSafe(Appearance->BodyMesh));
    if (Appearance->EyeMesh) // 原版Manny没有独立眼球；空配置是合法外观，不创建无效组件或每帧修正。
    {
        Eyes=NewObject<UDemoPlayerEyesComponent>(Character,TEXT("PlayerEyes")); // 同Pawn拥有的可选眼球表现，游戏线程初始化。
        Character->AddInstanceComponent(Eyes); Eyes->RegisterComponent(); Eyes->Initialize(Appearance);
        AddTickPrerequisiteComponent(Eyes); // 配置存在时仍保证Presentation读取同帧眼球。
    }
    else UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_EYES_DISABLED appearance=%s"),*GetNameSafe(Appearance));
    Character->GetMesh1P()->SetSkeletalMesh(Appearance->ArmsMesh); // FP网格与新Linked Layers必须整体匹配，ASC仍绑定该组件。
    Character->GetMesh1P()->SetVisibleInRayTracing(false); // 相机空间双臂不作为世界反射几何，队友与镜面使用完整TP。
    PrepareFirstPersonMesh(); // 同一未缩基线幂等应用；不让BeginPlay把早期Possess已确定的FP比例重置回1。
    if (Character->GetWeaponAnimationComponent()) Character->GetWeaponAnimationComponent()->InitializeArms(true); // Possess可早于BeginPlay；外观完整组件已改变后重取轻摆基线。
    Character->GetMesh1P()->SetHiddenInGame(false,false); // 恢复现有四枪FP动作；Scope/近战互斥由Tick统一控制，保留GAS主实例。
    for (USkeletalMeshComponent* Mesh : {Body, Character->GetMesh1P()}) // 逐Mesh建独立MID，不污染其他玩家配色。
        for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) // 当前材质槽索引，所有材质可安全忽略不存在的参数。
            if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(Slot)) OutfitMaterials.Add(Material);
    RefreshOwnerPartitionMaterials(); // Owner可包含独立近身裁剪槽，不能按TP槽索引替换其材质图。
    ThirdPersonWeapon = NewObject<USkeletalMeshComponent>(Character, TEXT("PlayerThirdPersonWeapon"));
    Character->AddInstanceComponent(ThirdPersonWeapon); ThirdPersonWeapon->RegisterComponent();
    ThirdPersonWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ThirdPersonWeapon->SetOwnerNoSee(true); ThirdPersonWeapon->SetHiddenInGame(true); // 拥有者只渲染真正的FP枪，远端继续显示全身挂点。
    ThirdPersonWeapon->SetComponentTickEnabled(false); // 只由本Presentation在身体求值后同步驱动，不允许引擎先Tick产生一帧机械延迟。
    ThirdPersonWeapon->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    ThirdPersonStaticWeapon = NewObject<UStaticMeshComponent>(Character, TEXT("PlayerThirdPersonStaticWeapon"));
    Character->AddInstanceComponent(ThirdPersonStaticWeapon); ThirdPersonStaticWeapon->RegisterComponent();
    ThirdPersonStaticWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ThirdPersonStaticWeapon->SetOwnerNoSee(true); ThirdPersonStaticWeapon->SetHiddenInGame(true); // 静态枪采用相同视角互斥策略。
    ThirdPersonOptic=NewObject<UStaticMeshComponent>(Character,TEXT("PlayerThirdPersonOptic"));
    Character->AddInstanceComponent(ThirdPersonOptic); ThirdPersonOptic->RegisterComponent();
    ThirdPersonOptic->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ThirdPersonOptic->SetOwnerNoSee(true); ThirdPersonOptic->SetHiddenInGame(true);
    AddTickPrerequisiteComponent(Character->GetMesh1P()); // 枪臂构图读取当前已求值的手骨。
    AddTickPrerequisiteComponent(Body); // PostUpdate在主网格刷新之后读取最终骨，不使用上一帧手位。
    if (Appearance->Locomotion) Character->GetMesh1P()->AddTickPrerequisiteComponent(Body); // FP移动读取同帧全身步相，显式顺序不依赖组件注册偶然先后；均随本Pawn销毁。
    ApplyOutfitColor(Character->GetOutfitColorIndex());
    UE_LOG(LogFPSDemo, Display, TEXT("PLAYER_APPEARANCE_READY body=%s arms=%s"), *GetNameSafe(Appearance->BodyMesh), *GetNameSafe(Appearance->ArmsMesh));
}
void UDemoPlayerPresentationComponent::RefreshWeapon(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_CALL();
    PresentedWeapon = Weapon;
    if (!ThirdPersonWeapon || !ThirdPersonStaticWeapon) return;
    ThirdPersonWeapon->SetSkeletalMesh(Weapon && !Weapon->Config.StaticMesh ? Weapon->Config.Mesh.Get() : nullptr);
    ThirdPersonStaticWeapon->SetStaticMesh(Weapon ? Weapon->Config.StaticMesh.Get() : nullptr);
    if (ThirdPersonOptic)
    {
        ThirdPersonOptic->SetStaticMesh(Weapon?Weapon->Config.OpticMesh.Get():nullptr);
        ThirdPersonOptic->AttachToComponent(ThirdPersonWeapon,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Weapon?Weapon->Config.OpticSocket:NAME_None);
        ThirdPersonOptic->SetRelativeTransform(Weapon?Weapon->Config.OpticOffset:FTransform::Identity);
    }
    const FDemoPlayerWeaponPoses* Poses=Appearance&&Weapon?Appearance->WeaponPoses.Find(Weapon->GetWeaponId()):nullptr; // 当前枪专用Skeleton图，不把其他枪图用到此网格。
    ThirdPersonWeapon->SetAnimInstanceClass(Poses?Poses->MechanicalAnimClass.Get():nullptr);
    UE_LOG(LogFPSDemo, Log, TEXT("PLAYER_TP_WEAPON asset=%s"), *GetNameSafe(Weapon));
}
void UDemoPlayerPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime, TickType, TickFunction);
    ADemoCharacter* Character = Cast<ADemoCharacter>(GetOwner()); // 游戏线程重新借用Owner，无跨线程对象查询。
    if (!Character || !Appearance || !ThirdPersonWeapon || !ThirdPersonStaticWeapon) return;
    if(UDemoLyraAnimInstance* Lyra=Cast<UDemoLyraAnimInstance>(Character->GetMesh()->GetAnimInstance())) Lyra->RefreshWeaponLayer(); // Body是Tick先决条件，此时其工作线程求值已结束。
    UpdateSpeechMouth(DeltaTime); // 必须在武器隐藏/近战的早退之前，讲话不依赖当前装备。
    ADemoWeaponBase* Weapon = Character->GetWeaponComponent() ? Character->GetWeaponComponent()->GetActiveWeapon() : nullptr; // 复制库存身份是唯一装备真值。
    if (Weapon != PresentedWeapon.Get()) RefreshWeapon(Weapon);
    USkeletalMeshComponent* Body = Character->GetMesh(); // 当前身体网格，求值已在本Tick之前完成。
    if (Head) { Head->SetHiddenInGame(Body->bHiddenInGame); Head->SetVisibility(Body->IsVisible()); } // 死亡/复活同步分区，OwnerNoSee仍保留第一人称过滤。
    const bool bScoped=Character->IsLocallyControlled() && Character->GetWeaponComponent() && Character->GetWeaponComponent()->GetScopeLevel()>0; // 镜内只遮自身模型，其他观察者继续看到持枪全身。
    Body->SetOwnerNoSee(true); // 完整网格负责远端/全身影子；本人分区可独立基础移动，不能再次显示TP躯干。
    const bool bSharedWeaponDisplay=UsesSharedBodyWeaponDisplay(); // 分区存在与本人武器策略独立；腿脚跟TP不要求手臂也跟TP闪避。
    ThirdPersonWeapon->SetOwnerNoSee(!bSharedWeaponDisplay || bScoped); ThirdPersonStaticWeapon->SetOwnerNoSee(!bSharedWeaponDisplay || bScoped);
    if (ThirdPersonOptic) ThirdPersonOptic->SetOwnerNoSee(!bSharedWeaponDisplay || bScoped); // Scope只遮本人，不影响远端武器。
    const UDemoPlayerAnimInstance* Anim = Cast<UDemoPlayerAnimInstance>(Body->GetAnimInstance()); // 只读游戏线程动作缓存。
    const UDemoMeleeComponent* Melee=Character->FindComponentByClass<UDemoMeleeComponent>(); // 当前Pawn状态，只在游戏线程借用。
    if (FirstPersonBody)
    {
        const UDemoMeleeDefinition* MeleeDefinition=Melee && Melee->IsEquipped()?Melee->GetDefinition():nullptr; // 仅借用当前装备配置，不消费旧库存。
        USkeletalMesh* Desired=Appearance->FirstPersonBodyMesh; // 独立FP枪械时为闭合下身；历史共享模式为原分区，不能恢复完整TP躯干。
        if (MeleeDefinition && MeleeDefinition->bIndependentFirstPersonDisplay && MeleeDefinition->OwnerBodyWithoutArmsMesh
            && MeleeDefinition->OwnerBodyWithoutArmsMesh->GetSkeleton()==Appearance->BodyMesh->GetSkeleton()) Desired=MeleeDefinition->OwnerBodyWithoutArmsMesh;
        if (FirstPersonBody->GetSkeletalMeshAsset()!=Desired)
        {
            FirstPersonBody->SetSkeletalMesh(Desired); // 只在装备边沿切几何分区；姿态源由下方策略统一重绑，不能强制恢复TP Leader。
            if (UDemoOwnerBodyLODComponent* Policy=Character->FindComponentByClass<UDemoOwnerBodyLODComponent>()) Policy->ValidateCoverage(); // 只在资源变化时复核，不逐帧扫描骨表。
            RefreshOwnerPartitionMaterials(); // 依据新分区自己的默认材质缓存MID；保留近身裁剪，切回旧分区也不残留多出的覆盖槽。
            ApplyOutfitColor(Character->GetOutfitColorIndex()); // 首次创建的Owner MID应用当前PS颜色，后续换色仍统一更新缓存。
            UE_LOG(LogFPSDemo,Log,TEXT("OWNER_BODY_PARTITION pawn=%s mesh=%s independent_melee=%d"),*Character->GetName(),*GetNameSafe(Desired),MeleeDefinition && MeleeDefinition->bIndependentFirstPersonDisplay);
        }
        RefreshOwnerBodyPosePolicy(); // Possess/引用晚到或换Mesh可重建AnimInstance；缓存实例边沿配置，不每帧加载/重播动作。
        FirstPersonBody->SetHiddenInGame(Body->bHiddenInGame || bScoped || Character->IsCoopEliminated()); FirstPersonBody->SetVisibility(Body->IsVisible());
    }
    const bool bHideFP=bSharedWeaponDisplay || bScoped || Character->IsCoopEliminated() || (Melee && Melee->IsEquipped()); // 独立FP枪臂与近战剑手互斥，ASC/Notify所有权不变。
    Character->GetMesh1P()->SetHiddenInGame(bHideFP,bSharedWeaponDisplay); // 共享模式连同FP弹匣隐藏；独立FP保持弹匣原操作阶段显隐。
    if (Weapon) Weapon->SetActorHiddenInGame(bHideFP); // WeaponBase已设置OnlyOwnerSee，远端不会看到相机空间枪。
    const bool bVisible = Weapon && Anim && Anim->GetUpperWeaponId()==Weapon->GetWeaponId() && Anim->UpperBodyWeight > 0.f && !Character->IsCoopEliminated() && !(Melee && Melee->IsEquipped()); // 装备状态和骨姿身份均就绪才显示新枪，避免近战切枪当帧挂到旧ik_hand_gun出现下垂闪现。
    ThirdPersonWeapon->SetHiddenInGame(!bVisible || !Weapon->Config.Mesh || Weapon->Config.StaticMesh);
    ThirdPersonStaticWeapon->SetHiddenInGame(!bVisible || !Weapon->Config.StaticMesh);
    if (ThirdPersonOptic) ThirdPersonOptic->SetHiddenInGame(!bVisible || !Weapon->Config.OpticMesh);
    RefreshWorldShadowPolicy(); // 先提交装备显隐，再更新唯一投影策略，避免隐藏旧武器仍投幽灵影。
    AimFirstPersonMuzzle(bHideFP ? nullptr : Weapon);
    if (!bVisible) return;
    // 最终Aim节点已把独立IK枪带入当前pelvis并同步肩臂俯仰；这里直接读最终世界骨，不能再次施加骨盆变换。
    WeaponAnchor = Body->GetSocketTransform(TEXT("ik_hand_gun"), RTS_World);
    ThirdPersonWeapon->SetWorldTransform(WeaponAnchor); ThirdPersonStaticWeapon->SetWorldTransform(WeaponAnchor);
    if (ThirdPersonWeapon->GetAnimInstance() && ThirdPersonWeapon->GetSkeletalMeshAsset())
    {
        // 两网格时间由身体当前缓存决定，零Delta只求值不重复累计；Editor/低FPS均使用同一显式phase。
        ThirdPersonWeapon->TickAnimation(0.f, false); ThirdPersonWeapon->RefreshBoneTransforms();
    }
}
bool UDemoPlayerPresentationComponent::AimsFirstPersonBarrelAtCrosshair() const
{
    DEMO_LOG_TICK();
    return bAimFirstPersonBarrelAtCrosshair; // 纯配置读取；CS2式默认false。
}
// FP-HANDAIM：只发布纯值，真正骨姿修正在FDemoFPHandAimProxy工作线程完成。目标/枪口均换算到Mesh1P组件空间（已含渲染缩放的逆），
// 因此缩放渲染下渲染出的枪管延长线仍穿过真实准星目标。PostUpdate调用，输入在下一次动画求值生效（相机子件，仅深度变化有一帧差）。
void UDemoPlayerPresentationComponent::PublishFirstPersonHandAim(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_TICK();
    ADemoCharacter* Pawn=CastChecked<ADemoCharacter>(GetOwner());
    USkeletalMeshComponent* Arms=Pawn->GetMesh1P();
    UDemoFPAnimInstance* Anim=Arms?Cast<UDemoFPAnimInstance>(Arms->GetAnimInstance()):nullptr;
    // 原因去重日志：只在门结果变化时输出一次，便于实机定位为何未激活；Lambda按引用捕获本帧局部Why，同步调用不逃逸。
    FName Why=TEXT("Active"); // 本帧发布结论；按门顺序取第一个失败原因。
    ON_SCOPE_EXIT
    {
        if (Why!=LastHandAimReason)
        {
            UE_LOG(LogFPSDemo,Log,TEXT("FP_HAND_AIM_PUBLISH reason=%s weapon=%s anim=%s"),*Why.ToString(),*GetNameSafe(Weapon),*GetNameSafe(Anim));
            LastHandAimReason=Why;
        }
    };
    if (!Anim) { Why=TEXT("NoDemoFPAnimInstance"); return; } // 近战手臂等其它实例不消费本输入。
    FDemoFPHandAimInput In; // 默认未激活。
    bool bEnabled=bAimFirstPersonHandsAtCrosshair && !bAimFirstPersonBarrelAtCrosshair;
#if WITH_EDITOR
    const int32 Override=CVarDemoHandAim.GetValueOnGameThread();
    if (Override==0 || Override==1) bEnabled=Override==1 && !bAimFirstPersonBarrelAtCrosshair;
#endif
    APlayerController* PC=Cast<APlayerController>(Pawn->GetController());
    if (!bEnabled) Why=bAimFirstPersonBarrelAtCrosshair?TEXT("LegacyRigidMuzzleAimOn"):TEXT("Disabled");
    else if (!Weapon) Why=TEXT("NoWeaponOrFPHidden");
    else if (Weapon->IsReloading()) Why=TEXT("Reloading");
    else if (!PC || !PC->IsLocalController()) Why=TEXT("NotLocalController");
    else if (PC->GetViewTarget()!=Pawn) Why=TEXT("ViewTargetNotPawn");
    else if (!UsesIndependentFirstPersonWeapons()) Why=TEXT("SharedBodyWeaponDisplay");
    UPrimitiveComponent* Gun=Why==TEXT("Active")?Weapon->GetMuzzleFlashComponent(false):nullptr;
    if (Why==TEXT("Active") && !Gun) Why=TEXT("NoVisibleFPGunComponent");
    // 挂接点两种形态：旧枪GripPoint是hand_r上的socket；新四枪ik_hand_gun是独立骨（不属于手臂链），必须分别解析。
    const FName Attach=Gun?Weapon->Config.AttachSocket:NAME_None;
    const USkeletalMeshSocket* GripSocket=Gun?Arms->GetSocketByName(Attach):nullptr;
    const bool bAttachIsBone=Gun && !GripSocket && Arms->GetBoneIndex(Attach)!=INDEX_NONE;
    if (Gun && !GripSocket && !bAttachIsBone) Why=TEXT("NoAttachSocketOrBoneOnArms");
    else if (Gun && !Gun->DoesSocketExist(Weapon->Config.MuzzleFlashSocket)) Why=TEXT("NoMuzzleSocketOnGun");
    if (Why==TEXT("Active"))
    {
        In.AnchorBone=GripSocket?GripSocket->BoneName:Attach;
        In.GripInHand=GripSocket?GripSocket->GetSocketLocalTransform():FTransform::Identity;
        const FTransform SocketWorld=Arms->GetSocketTransform(Attach,RTS_World); // socket与骨名均可查询。
        In.MuzzleInHand=Gun->GetSocketTransform(Weapon->Config.MuzzleFlashSocket,RTS_World).GetRelativeTransform(SocketWorld)*In.GripInHand; // 枪刚性挂在挂接点，帧间恒定。
        // 右手：socket所属骨若以_r结尾即为右手（转手即转枪）；独立锚骨时用hand_r跟随。左手取对侧_l；不存在则只做右手。
        FString Right=In.AnchorBone.ToString();
        if (!Right.EndsWith(TEXT("_r"))) Right=TEXT("hand_r");
        if (Arms->GetBoneIndex(*Right)==INDEX_NONE) { Why=TEXT("NoRightHandBone"); Anim->SetFirstPersonHandAim(FDemoFPHandAimInput()); return; }
        In.HandBone=*Right;
        FString Support=Right; Support.LeftChopInline(2); Support+=TEXT("_l");
        if (Arms->GetBoneIndex(*Support)!=INDEX_NONE) In.SupportHandBone=*Support;
        FVector Eye; FRotator View; PC->GetPlayerViewPoint(Eye,View); // 最终视图（含本机相机修改器）。
        const FVector Hit=Weapon->TraceAimPoint(Eye,View.Vector());
        const float MinDistance=FMath::Max(FirstPersonHandAimMinDistance,20.f);
        const float Distance=FMath::Max(FVector::Dist(Eye,Hit),MinDistance); // 贴墙外推，避免手腕急转。
        // 视角惯性（A09，用户2026-11-20要求从A10/A11弹簧摇摆回退到此版）：枪臂刚性挂相机，静态对准仅≈0.6°不可见；
        // 瞄准方向以短半衰期追随真实视角，并把“落后→真实”增量再施加一次，转视角时枪口朝转动方向领先，停转归零，不改变弹道。
        float LagHalfLife=FirstPersonHandAimViewLagHalfLife; float MaxDeg=FirstPersonHandAimMaxDegrees;
#if WITH_EDITOR
        if (CVarDemoHandAimLag.GetValueOnGameThread()>=0.f) LagHalfLife=CVarDemoHandAimLag.GetValueOnGameThread();
        if (CVarDemoHandAimMaxDeg.GetValueOnGameThread()>0.f) MaxDeg=CVarDemoHandAimMaxDeg.GetValueOnGameThread();
#endif
        LagHalfLife=FMath::Clamp(LagHalfLife,0.f,.2f); MaxDeg=FMath::Clamp(MaxDeg,0.f,30.f);
        const FQuat ViewQuat=View.Quaternion();
        const float Dt=GetWorld()?GetWorld()->GetDeltaSeconds():0.f; // 暂停时为0，惯性不推进。
        if (!bLaggedAimViewValid || LagHalfLife<=0.f) LaggedAimView=ViewQuat;
        else LaggedAimView=FQuat::Slerp(LaggedAimView,ViewQuat,Dt>0.f?1.f-FMath::Exp2(-Dt/LagHalfLife):0.f).GetNormalized();
        if (FMath::RadiansToDegrees(LaggedAimView.AngularDistance(ViewQuat))>MaxDeg*1.5f) // 超大转角（瞬转/重生）时把落后量钳在可见上限附近，避免长时间偏离。
            LaggedAimView=FQuat::Slerp(ViewQuat,LaggedAimView,MaxDeg*1.5f/FMath::Max(FMath::RadiansToDegrees(LaggedAimView.AngularDistance(ViewQuat)),KINDA_SMALL_NUMBER)).GetNormalized();
        bLaggedAimViewValid=true;
        const FQuat LagToView=(ViewQuat*LaggedAimView.Inverse()).GetNormalized(); // 世界空间：落后朝向到真实朝向的增量旋转。
        const FVector Target=Eye+(LagToView*ViewQuat).GetForwardVector()*Distance; // 增量再施加一次=朝转动方向领先（用户确认方向）。
        In.TargetCS=Arms->GetComponentTransform().InverseTransformPosition(Target);
        In.MaxDegrees=MaxDeg; In.SmoothingHalfLife=FirstPersonHandAimHalfLife;
        In.bActive=!In.TargetCS.ContainsNaN() && !In.MuzzleInHand.ContainsNaN();
        if (!In.bActive) Why=TEXT("NaNTargetOrMuzzle");
    }
    if (!In.bActive) bLaggedAimViewValid=false; // 未激活（换弹/隐藏/切枪）时丢弃惯性，下次从真实视角起步。
    Anim->SetFirstPersonHandAim(In);
}
// 只改变枪臂共同父级的构图，原动画照常写手臂相对姿态。每帧先还原到作者空间计算，避免累积。
void UDemoPlayerPresentationComponent::AimFirstPersonMuzzle(ADemoWeaponBase* Weapon)
{
    DEMO_LOG_TICK();
    ADemoCharacter* Pawn=CastChecked<ADemoCharacter>(GetOwner());
    APlayerController* PC=Cast<APlayerController>(Pawn->GetController());
    PublishFirstPersonHandAim(Weapon); // FP-HANDAIM：与下方构图平移独立；旧刚体模式下内部发布未激活。
    if (!bAimFirstPersonBarrelAtCrosshair)
    {
        // CS2式构图：枪臂不旋转，只按viewmodel偏移整体平移；弹道由武器表现层从可见枪口收敛到准星命中点。
        const bool bOwnerFP=Weapon && PC && PC->IsLocalController() && PC->GetViewTarget()==Pawn && UsesIndependentFirstPersonWeapons();
        FVector Offset=bOwnerFP?FirstPersonViewmodelOffset:FVector::ZeroVector; // 相机局部cm；非本人FP视图回到作者原位。
#if WITH_EDITOR
        if (bOwnerFP)
        {
            const FString Override=CVarDemoViewmodelOffset.GetValueOnGameThread(); // "X Y Z"厘米；空串使用组件默认值。
            TArray<FString> Parts;
            if (Override.ParseIntoArrayWS(Parts)==3)
            {
                const FVector Parsed(FCString::Atof(*Parts[0]),FCString::Atof(*Parts[1]),FCString::Atof(*Parts[2]));
                if (!Parsed.ContainsNaN() && Parsed.GetAbsMax()<=30.f) Offset=Parsed; // 有界调试值，越界忽略不改构图。
            }
        }
#endif
        if (!Offset.IsZero() && !AimPivot)
        {
            USkeletalMeshComponent* Arms=Pawn->GetMesh1P();
            UCameraComponent* PivotCamera=Pawn->GetFirstPersonCameraComponent();
            if (Arms->GetAttachParent()!=PivotCamera) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("FP_VIEWMODEL_OFFSET_SKIP unexpected arms parent")); return; }
            AimPivot=NewObject<USceneComponent>(Pawn);
            AimPivot->SetupAttachment(PivotCamera);
            AimPivot->RegisterComponent();
            Arms->AttachToComponent(AimPivot,FAttachmentTransformRules::KeepRelativeTransform);
        }
        if (AimPivot && !AimPivot->GetRelativeTransform().Equals(FTransform(Offset)))
        {
            AimPivot->SetRelativeTransform(FTransform(Offset));
            UE_LOG(LogFPSDemo,Verbose,TEXT("FP_VIEWMODEL_OFFSET applied=%s"),*Offset.ToString());
        }
        return;
    }
    UPrimitiveComponent* Gun=Weapon && !Weapon->IsReloading() && PC && PC->IsLocalController()
        && PC->GetViewTarget()==Pawn && UsesIndependentFirstPersonWeapons() ? Weapon->GetMuzzleFlashComponent(false) : nullptr;
    if (!Gun)
    {
        if (AimPivot && !AimPivot->GetRelativeTransform().Equals(FTransform::Identity))
        {
            AimPivot->SetRelativeTransform(FTransform::Identity);
            UE_LOG(LogFPSDemo,Verbose,TEXT("FP_MUZZLE_AIM_RESET view/equipment"));
        }
        return;
    }
    UCameraComponent* Camera=Pawn->GetFirstPersonCameraComponent();
    if (!AimPivot)
    {
        USkeletalMeshComponent* Arms=Pawn->GetMesh1P();
        if (Arms->GetAttachParent()!=Camera)
        { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("FP_MUZZLE_AIM_SKIP unexpected arms parent")); return; }
        AimPivot=NewObject<USceneComponent>(Pawn);
        AimPivot->SetupAttachment(Camera);
        AimPivot->RegisterComponent();
        Arms->AttachToComponent(AimPivot,FAttachmentTransformRules::KeepRelativeTransform);
    }
    const FTransform& CameraFrame=Camera->GetComponentTransform();
    const FTransform Bore=Gun->GetSocketTransform(Weapon->Config.MuzzleFlashSocket).GetRelativeTransform(AimPivot->GetComponentTransform());
    FVector Eye; FRotator View; // 使用最终视图，包括本机相机修改器；下面全部在原相机局部坐标计算。
    PC->GetPlayerViewPoint(Eye,View);
    const FVector EyeLocal=CameraFrame.InverseTransformPosition(Eye);
    const FVector Forward=CameraFrame.InverseTransformVectorNoScale(View.Vector());
    const FVector Aim=CameraFrame.InverseTransformPosition(Weapon->TraceAimPoint(Eye,View.Vector()));
    const FVector Axis=Bore.GetUnitAxis(EAxis::X); // 已标定的四枪口环法向。
    const FVector Muzzle=Bore.GetLocation();
    const FVector Grip=AimPivot->GetComponentTransform().InverseTransformPosition(
        Pawn->GetMesh1P()->GetSocketLocation(Weapon->Config.AttachSocket)); // 沿用本枪实际装备握点，使肩托和双手保留作者画面位置。
    const FVector GripToMuzzle=Muzzle-Grip;
    const FVector GripToAim=Aim-Grip;
    const double Axial=FVector::DotProduct(GripToMuzzle,Axis);
    // 求原枪管上到握点距离等于“握点到目标”的前向点，随后绕握点刚体旋转，让该点与目标重合。
    const double Disc=GripToAim.SizeSquared()-GripToMuzzle.SizeSquared()+Axial*Axial;
    const double BarrelTravel=Disc>0. ? FMath::Sqrt(Disc)-Axial : -1.;
    if (BarrelTravel>0. && FVector::DotProduct(GripToAim,Forward)>0.)
    {
        const FQuat Rotate=FQuat::FindBetweenNormals((GripToMuzzle+Axis*BarrelTravel).GetSafeNormal(),GripToAim.GetSafeNormal());
        AimPivot->SetRelativeLocationAndRotation(Grip-Rotate.RotateVector(Grip),Rotate);
        return;
    }
    // 极近遮挡没有可绕握点的前向解，沿原观察射线收回口心；相机完全进入表面时使用作者姿态。
    const double Depth=FVector::DotProduct(Muzzle-EyeLocal,Forward);
    const double HitDepth=FVector::DotProduct(Aim-EyeLocal,Forward);
    if (Depth<=UE_KINDA_SMALL_NUMBER || HitDepth<=UE_KINDA_SMALL_NUMBER)
    { AimPivot->SetRelativeTransform(FTransform::Identity); UE_LOG(LogFPSDemo,VeryVerbose,TEXT("FP_MUZZLE_AIM_SKIP eye inside obstruction")); return; }
    const FVector Retracted=EyeLocal+(Muzzle-EyeLocal)*FMath::Min(1.,HitDepth*.5/Depth);
    const FQuat Rotate=FQuat::FindBetweenNormals(Axis,(Aim-Retracted).GetSafeNormal());
    AimPivot->SetRelativeLocationAndRotation(Retracted-Rotate.RotateVector(Muzzle),Rotate);
}

void UDemoPlayerPresentationComponent::RefreshOwnerBodyPosePolicy()
{
    DEMO_LOG_TICK();
    ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwner()); // 仅游戏线程借用当前Pawn；独立表示不改主Body/ASC。
    if (!Character || !Appearance || !FirstPersonBody) return;
    UClass* DesiredClass=Character->IsLocallyControlled() && UsesIndependentFirstPersonWeapons()?Appearance->OwnerLowerBodyAnimClass.Get():nullptr; // 非本机继续无额外动画求值的Leader表示。
    if (DesiredClass)
    {
        if (!bOwnerLowerPoseActive)
        {
            bSavedOwnerPostProcessDisabled=FirstPersonBody->GetDisablePostProcessBlueprint(); bOwnerLowerPoseActive=true; // 在实例重建/引用晚到前保存一次，不从已禁用值重采。
        }
        if (!FirstPersonBody->GetDisablePostProcessBlueprint()) FirstPersonBody->SetDisablePostProcessBlueprint(true); // 窄图不能再被Mesh上的TP PostProcess修改；完整Body完全保留原图。
        const bool HadLeader=FirstPersonBody->LeaderPoseComponent.IsValid(); // 不能在独立实例建好后仍让渲染读主Body骨缓冲。
        if (HadLeader) FirstPersonBody->SetLeaderPoseComponent(nullptr,true);
        FirstPersonBody->bUseBoundsFromLeaderPoseComponent=false;
        FirstPersonBody->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        FirstPersonBody->PrimaryComponentTick.TickGroup=TG_PrePhysics;
        FirstPersonBody->AddTickPrerequisiteComponent(Character->GetCharacterMovement());
        AddTickPrerequisiteComponent(FirstPersonBody); // 展示与验收必须等自己的本帧骨求值完成，不能只等TP Body。
        if (!FirstPersonBody->GetAnimInstance() || FirstPersonBody->GetAnimInstance()->GetClass()!=DesiredClass)
            FirstPersonBody->SetAnimInstanceClass(DesiredClass);
        UDemoOwnerLowerBodyAnimInstance* Instance=Cast<UDemoOwnerLowerBodyAnimInstance>(FirstPersonBody->GetAnimInstance()); // Component拥有，弱缓存只识别是否重建。
        if (Instance && (ConfiguredOwnerLowerInstance.Get()!=Instance || ConfiguredOwnerLowerSet.Get()!=Appearance->Locomotion.Get()))
        {
            Instance->Configure(Appearance->Locomotion);
            ConfiguredOwnerLowerInstance=Instance; ConfiguredOwnerLowerSet=Appearance->Locomotion.Get();
            UE_LOG(LogFPSDemo,Log,TEXT("OWNER_LOWER_POSE_READY pawn=%s class=%s independent=1"),*Character->GetName(),*GetNameSafe(DesiredClass));
            FirstPersonBody->TickAnimation(0.f,false); FirstPersonBody->RefreshBoneTransforms(); // 仅初始化边沿同步建有效姿态，晚Possess不能显示一帧未配置参考姿态。
        }
        FirstPersonBody->SetComponentTickEnabled(true);
    }
    else
    {
        if (bOwnerLowerPoseActive)
        {
            FirstPersonBody->SetAnimInstanceClass(nullptr); ConfiguredOwnerLowerInstance.Reset(); ConfiguredOwnerLowerSet.Reset();
            FirstPersonBody->SetDisablePostProcessBlueprint(bSavedOwnerPostProcessDisabled); bOwnerLowerPoseActive=false;
            RemoveTickPrerequisiteComponent(FirstPersonBody);
            UE_LOG(LogFPSDemo,Log,TEXT("OWNER_LOWER_POSE_LEADER pawn=%s independent=0"),*Character->GetName());
        }
        if (FirstPersonBody->LeaderPoseComponent.Get()!=Character->GetMesh()) FirstPersonBody->SetLeaderPoseComponent(Character->GetMesh(),true);
        FirstPersonBody->bUseBoundsFromLeaderPoseComponent=true;
        FirstPersonBody->SetComponentTickEnabled(false);
    }
}
void UDemoPlayerPresentationComponent::RefreshOwnerPartitionMaterials()
{
    DEMO_LOG_CALL();
    const USkeletalMesh* Partition=FirstPersonBody?FirstPersonBody->GetSkeletalMeshAsset():nullptr; // 本Pawn当前渲染资源，游戏线程同步借用。
    if (!Partition) { UE_LOG(LogFPSDemo,Verbose,TEXT("OWNER_MATERIALS_SKIP no_owner_partition")); return; }
    FirstPersonBody->EmptyOverrideMaterials(); // 新旧分区槽数可不同；先清旧覆盖，不让上一把武器的材质落到新区域。
    const TArray<FSkeletalMaterial>& Slots=Partition->GetMaterials(); // 默认槽语义来自作者资源，不能假定与完整Body等长/同序。
    for (int32 Slot=0;Slot<Slots.Num();++Slot)
    {
        UMaterialInterface* Source=Slots[Slot].MaterialInterface; // 资产持有原材质，Owner近身版本是独立Parent，不修改TP材质。
        if (!Source) { UE_LOG(LogFPSDemo,Warning,TEXT("OWNER_MATERIALS_MISSING mesh=%s slot=%d"),*GetNameSafe(Partition),Slot); continue; }
        UMaterialInstanceDynamic* Cached=nullptr; // 只按同一Pawn内精确Parent身份复用，不能跨玩家共享配色或把MI继承链当同材质。
        for (UMaterialInstanceDynamic* Material:OutfitMaterials)
            if (Material && Material->Parent==Source) { Cached=Material; break; }
        if (!Cached)
        {
            Cached=FirstPersonBody->CreateDynamicMaterialInstance(Slot,Source); // 仅每个不同Parent首次使用时分配，帧更新与反复切枪不新增MID。
            if (Cached) OutfitMaterials.Add(Cached);
            else UE_LOG(LogFPSDemo,Error,TEXT("OWNER_MATERIALS_CREATE_FAILED mesh=%s slot=%d source=%s"),*GetNameSafe(Partition),Slot,*GetNameSafe(Source));
        }
        if (Cached) FirstPersonBody->SetMaterial(Slot,Cached);
    }
    UE_LOG(LogFPSDemo,Log,TEXT("OWNER_MATERIALS_READY mesh=%s slots=%d pawn_mid_cache=%d"),*GetNameSafe(Partition),Slots.Num(),OutfitMaterials.Num());
}
void UDemoPlayerPresentationComponent::ApplyOutfitColor(uint8 PaletteIndex)
{
    DEMO_LOG_CALL();
    if (!Appearance) return;
    if (Appearance->OutfitPalette.Num()!=4)
    { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_COLOR invalid palette count=%d expected=4"),Appearance->OutfitPalette.Num()); return; }
    const uint8 ValidIndex = PaletteIndex < 4 ? PaletteIndex : 0; // 非法/未复制索引降级配置的首色，后续OnRep仍可重试。
    if (PaletteIndex >= 4) UE_LOG(LogFPSDemo, Log, TEXT("PLAYER_COLOR deferred/invalid index=%d fallback=0"), PaletteIndex);
    for (UMaterialInstanceDynamic* Material : OutfitMaterials) // MID归本Pawn，销毁时由GC回收。
        if (Material) Material->SetVectorParameterValue(Appearance->OutfitParameter, Appearance->OutfitPalette[ValidIndex]);
}
void UDemoPlayerPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    SetOwnerShadowProxyActive(false); // 先恢复尚存活的原世界表示，组件结束顺序不能残留撤销的隐藏投影授权。
    if (IsValid(AimPivot))
    {
        ADemoCharacter* Pawn=CastChecked<ADemoCharacter>(GetOwner());
        if (IsValid(Pawn->GetMesh1P()) && IsValid(Pawn->GetFirstPersonCameraComponent()) && Pawn->GetMesh1P()->GetAttachParent()==AimPivot)
            Pawn->GetMesh1P()->AttachToComponent(Pawn->GetFirstPersonCameraComponent(),FAttachmentTransformRules::KeepRelativeTransform);
        AimPivot->DestroyComponent();
        AimPivot=nullptr;
    }
    if (ThirdPersonWeapon) ThirdPersonWeapon->DestroyComponent();
    if (ThirdPersonStaticWeapon) ThirdPersonStaticWeapon->DestroyComponent();
    if (ThirdPersonOptic) ThirdPersonOptic->DestroyComponent();
    if (Eyes) Eyes->DestroyComponent(); // 会调用其EndPlay清理两枚静态眼球，避免旅行残留。
    if (Head) Head->DestroyComponent(); // 无独立动画/网络状态，仅释放同骨渲染组件。
    if (FirstPersonBody) FirstPersonBody->DestroyComponent(); // 仅释放本Pawn的显示分区，真正Body生命周期仍属于Character。
    PresentedWeapon.Reset(); OutfitMaterials.Reset();
    ConfiguredOwnerLowerInstance.Reset(); ConfiguredOwnerLowerSet.Reset();
    Super::EndPlay(EndPlayReason);
}
UDemoPlayerAppearance* UDemoPlayerPresentationComponent::GetAppearance() const { DEMO_LOG_TICK(); return Appearance; }
bool UDemoPlayerPresentationComponent::UsesFullBodyFirstPerson() const { DEMO_LOG_TICK(); return bFullBodyViewReady; }
bool UDemoPlayerPresentationComponent::UsesIndependentFirstPersonWeapons() const { DEMO_LOG_TICK(); return Appearance && Appearance->bUseIndependentFirstPersonWeapons; }
float UDemoPlayerPresentationComponent::GetFirstPersonRenderScale() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwner()); // 只读取当前Pawn本机控制权；远端完整TP不参与压缩。
    return UsesIndependentFirstPersonWeapons() && Character && Character->IsLocallyControlled() && FMath::IsFinite(Appearance->FirstPersonRenderScale)
        ? FMath::Clamp(Appearance->FirstPersonRenderScale,.1f,1.f):1.f;
}
float UDemoPlayerPresentationComponent::GetFirstPersonNearClipCm() const
{
    DEMO_LOG_TICK();
    return Appearance && FMath::IsFinite(Appearance->FirstPersonNearClipCm)?FMath::Clamp(Appearance->FirstPersonNearClipCm,.5f,10.f):1.f;
}
FTransform UDemoPlayerPresentationComponent::GetAuthoredFirstPersonArmsTransform() const
{
    DEMO_LOG_TICK();
    return AuthoredArmsTransform; // 由Prepare确定，无从当前缩后/轻摆后的组件变换反算，防误缓存和除法漂移。
}
bool UDemoPlayerPresentationComponent::UsesSharedBodyWeaponDisplay() const { DEMO_LOG_TICK(); return bFullBodyViewReady && !UsesIndependentFirstPersonWeapons(); }
USkeletalMeshComponent* UDemoPlayerPresentationComponent::GetFirstPersonBodyMesh() const { DEMO_LOG_TICK(); return FirstPersonBody; }
USkeletalMeshComponent* UDemoPlayerPresentationComponent::GetOwnerShadowHeadMesh() const { DEMO_LOG_TICK(); return Head; }
bool UDemoPlayerPresentationComponent::IsOwnerShadowProxyActive() const { DEMO_LOG_TICK(); return bOwnerShadowProxyActive; }
void UDemoPlayerPresentationComponent::SetOwnerShadowProxyActive(bool bActive)
{
    DEMO_LOG_TICK();
    if (bOwnerShadowProxyActive==bActive) return; // 普通逐帧提交幂等，不重复DirtyRenderState或刷业务日志。
    bOwnerShadowProxyActive=bActive;
    RefreshWorldShadowPolicy();
    if (UDemoMeleeComponent* Melee=GetOwner()?GetOwner()->FindComponentByClass<UDemoMeleeComponent>():nullptr)
        Melee->RefreshOwnerShadowPolicy(); // 近战自己的单写入口立即切换，不等下一帧造成两份刀影。
    UE_LOG(LogFPSDemo,Log,TEXT("OWNER_SHADOW_POLICY owner=%s proxy=%d"),*GetNameSafe(GetOwner()),bActive);
}
void UDemoPlayerPresentationComponent::RefreshWorldShadowPolicy()
{
    DEMO_LOG_TICK();
    ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwner()); // 仅GT借用当前Avatar，无跨Pawn缓存。
    if (!Character) return;
    const bool bOriginalHiddenShadow=!bOwnerShadowProxyActive && !Character->IsCoopEliminated(); // 阵亡退出代理时也不保留被HiddenInGame遮住的原全身幽灵影。
    if (USkeletalMeshComponent* Body=Character->GetMesh()) Body->SetCastHiddenShadow(bOriginalHiddenShadow);
    if (IsValid(Head)) Head->SetCastHiddenShadow(bOriginalHiddenShadow);
    for (UPrimitiveComponent* Primitive : {static_cast<UPrimitiveComponent*>(ThirdPersonWeapon.Get()),static_cast<UPrimitiveComponent*>(ThirdPersonStaticWeapon.Get()),static_cast<UPrimitiveComponent*>(ThirdPersonOptic.Get())})
    {
        if (!IsValid(Primitive)) continue;
        const bool bVisible=!Primitive->bHiddenInGame && !Character->IsCoopEliminated(); // 已有装备/动作就绪门；世界相机仍需正常TP投影。
        Primitive->SetCastShadow(bVisible);
        Primitive->SetCastHiddenShadow(bVisible && !bOwnerShadowProxyActive);
    }
}
USkeletalMeshComponent* UDemoPlayerPresentationComponent::GetThirdPersonWeaponMesh() const { DEMO_LOG_TICK(); return ThirdPersonWeapon; }
UStaticMeshComponent* UDemoPlayerPresentationComponent::GetThirdPersonStaticWeaponMesh() const { DEMO_LOG_TICK(); return ThirdPersonStaticWeapon; }
UStaticMeshComponent* UDemoPlayerPresentationComponent::GetThirdPersonOpticMesh() const { DEMO_LOG_TICK(); return ThirdPersonOptic; }
FTransform UDemoPlayerPresentationComponent::GetWeaponAnchorTransform() const { DEMO_LOG_TICK(); return WeaponAnchor; }
UDemoPlayerEyesComponent* UDemoPlayerPresentationComponent::GetEyes() const { DEMO_LOG_TICK(); return Eyes; }

void UDemoPlayerPresentationComponent::UpdateSpeechMouth(float DeltaTime)
{
    DEMO_LOG_TICK();
    if (!Head || !bHasMouthMorph) return;
    const ADemoCharacter* Character = Cast<ADemoCharacter>(GetOwner()); // 游戏线程同步借用当前Avatar。
    const ADemoPlayerState* State = Character ? Character->GetPlayerState<ADemoPlayerState>() : nullptr; // 客户端PS可晚到，未就绪闭嘴。
    const UDemoVoiceComponent* Voice = State ? State->GetVoiceComponent() : nullptr; // 每位讲话者独立解码/RMS，不读取Player0。
    const float Target = Voice && Character && !Character->IsCoopEliminated() ? Voice->GetMouthOpenAmount() : 0.f; // 观战仍可说话，但死亡身体不动嘴。
    MouthOpenAmount = FMath::FInterpTo(MouthOpenAmount, Target, DeltaTime, Target > MouthOpenAmount ? 24.f : 32.f);
    if (Target == 0 && MouthOpenAmount < .005f) MouthOpenAmount = 0; // 有限时间完全闭合，不留浮点尾巴。
    Head->SetMorphTarget(MouthMorphName, MouthOpenAmount); // UE5.4 UpdateFollowerComponent会合并Follower自身Morph，仍复用Body骨姿。
}
