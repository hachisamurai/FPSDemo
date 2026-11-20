// 职责：在Editor隔离World驱动真实Movement/GAS/装备事务并记录FP/TP最终骨姿与截图；显式命令行启用，测试结果与视觉复核分开，不修改持久进度。
#include "Tests/DemoPlayerAnimationTest.h"
#include "Tests/DemoTestProgress.h"
#include "Animation/AnimSequence.h" // GetNameSafe/BodyMotionPose需要完整UObject派生类型，不能依赖Editor PCH间接包含。
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Animation/DemoPlayerAnimInstance.h" // 读取真实运行图选态，不向动画实例注入测试状态。
#include "Animation/DemoPlayerAimSettings.h" // 读取配置边界作独立输入越界断言，不修改生产阈值。
#include "Animation/DemoPlayerPresentationComponent.h" // 骨采样显式排在展示枪更新之后，避免一帧挂点滞后造成误判。
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DirectionalLight.h" // 隔离验收World补光，避免原白模逆光遮蔽面部和肩腕接触。
#include "Components/DirectionalLightComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h" // 验收平台LoadObject需要完整StaticClass声明。
#include "Engine/GameInstance.h" // 获取Editor临时档案子系统的模板入口。
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/Abilities/DemoPlayerAbilityTypes.h"
#include "Interaction/DemoInteractable.h"
#include "Interaction/DemoTerminalComponent.h" // 专項使用正式终端服务创建隔离换枪终端，不旅行销毁验收World。
#include "World/DemoEncounterAreaSubsystem.h" // 终端生成仍解析实际场景锚点和地面。
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerState.h" // 专项临时授权只查询本Pawn所属PS，不读取/伪造永久Ownership。
#include "Player/DemoPlayerProgressComponent.h"
#include "Progression/Builds/DemoRunBuildComponent.h" // 新战役出发必须选择核心；夹具通过正式权威选择接口完成准备。
#include "UI/Flow/DemoMenuFlowComponent.h" // 测试阶段同步后先刷新UI投影，再发起终端操作，避免跨阶段旧菜单缓存。
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponCatalog.h" // 固定目录身份；新专项权限由现有GI调试覆盖和PS查询共同验证。

namespace
{
    /** Transform为骨/组件值副本；列向量4x4包含缩放，Blender校验器不得直接使用UE行向量FMatrix布局。 */
    TSharedPtr<FJsonValue> PlayerAuditMatrix(const FTransform& Transform)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 非UObject工具函数没有this，保留同级高频埋点。
        const FVector Origin = Transform.GetTranslation(); // 目标空间平移列，单位cm。
        const FVector X = Transform.TransformVector(FVector::ForwardVector); // 含缩放的X基向量。
        const FVector Y = Transform.TransformVector(FVector::RightVector); // 含缩放的Y基向量。
        const FVector Z = Transform.TransformVector(FVector::UpVector); // 含缩放的Z基向量。
        const double Values[16] = {X.X,Y.X,Z.X,Origin.X,X.Y,Y.Y,Z.Y,Origin.Y,X.Z,Y.Z,Z.Z,Origin.Z,0,0,0,1}; // 四行连续布局，解析后reshape(4,4)。
        TArray<TSharedPtr<FJsonValue>> Cells; // 本矩阵独占JSON数值数组，随报告释放。
        for (double Value : Values) Cells.Add(MakeShared<FJsonValueNumber>(Value)); // 仅序列化有限矩阵，非渲染线程访问。
        return MakeShared<FJsonValueArray>(Cells);
    }

    /** Mesh是当前World已求值组件；bReference读取绑定Pose；bCompact仅保留肩部专项关节，限制长录制内存。 */
    TSharedPtr<FJsonObject> PlayerAuditBones(const USkeletalMeshComponent* Mesh, bool bReference, bool bCompact=false)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 纯序列化函数没有this，不伪造对象日志。
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>(); // 每骨一个组件空间列向量矩阵。
        const FReferenceSkeleton& Reference = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton(); // 网格自身骨序，不能按共享Skeleton额外虚拟骨索引。
        TArray<FTransform> Transforms = bReference ? Reference.GetRefBonePose() : Mesh->GetComponentSpaceTransforms(); // 值副本，避免后续骨刷新影响已记录帧。
        for (int32 Index=0; Index<Reference.GetNum(); ++Index) // UE原始骨索引，父骨永远先于子骨。
        {
            const int32 Parent = Reference.GetParentIndex(Index); // -1根骨，自身局部即组件空间。
            if (bReference && Parent != INDEX_NONE) Transforms[Index] = Transforms[Index] * Transforms[Parent];
            const FName Bone=Reference.GetBoneName(Index); // 缓存当前骨名；过滤不改变参考父链计算。
            if (bCompact && Bone!=TEXT("root") && Bone!=TEXT("pelvis") && !Bone.ToString().StartsWith(TEXT("spine_"))
                && !Bone.ToString().StartsWith(TEXT("clavicle_")) && !Bone.ToString().StartsWith(TEXT("upperarm_"))
                && !Bone.ToString().StartsWith(TEXT("lowerarm_")) && !Bone.ToString().StartsWith(TEXT("hand_"))
                && !Bone.ToString().StartsWith(TEXT("foot_")) && Bone!=TEXT("ik_hand_gun")) continue;
            Result->SetField(Bone.ToString(),PlayerAuditMatrix(Transforms[Index]));
        }
        return Result;
    }
}

ADemoPlayerAnimationTest::ADemoPlayerAnimationTest()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork; // 最终骨矩阵在动画Tick之后读取。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusRecord"))) PrimaryActorTick.TickGroup=TG_PrePhysics; // 录制输入须先于身体/Movement；实际抓帧另在World PostActorTick，避免人为制造一帧视角滞后。
    bCapture = FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerCapture"));
    bFirstPersonCapture = FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerFirstPersonCapture")); // 仅改变验收观察点，仍独立采集同帧FP/TP完整矩阵。
    bMotionOnly = FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerMotionOnly"));
    bStartJitterAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoStartJitterAudit"));
    bAimContractAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoAimContractAudit")) || FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimAudit")); // 显式隔离诊断，低头专项复用真实输入而补足侧转组合。
    FParse::Value(FCommandLine::Get(),TEXT("DemoMotionIsolation="),MotionIsolation);
    if (MotionIsolation!=TEXT("Current") && MotionIsolation!=TEXT("NoAim") && MotionIsolation!=TEXT("Native") && MotionIsolation!=TEXT("Ready")) MotionIsolation.Empty(); // 输出不接受任意路径。
    bShoulderShadowAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoShoulderShadowCapture"));
    bDirectionalAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoDirectionalMovementTest")); // 新移动专项走独立真实输入计划。
    bPistolRunArmAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoPistolRunArmTest")); // 独立真实移动与装填优先级专项。
#if WITH_EDITOR
    bFireArmJitterAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoFireArmJitterAudit")); // 显式入口只采样正式武器/骨图，不改变动画驱动。
    bFireArmJitterCapture=bFireArmJitterAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoFireArmJitterCapture"));
    if (bFireArmJitterAudit) FParse::Value(FCommandLine::Get(),TEXT("DemoFireArmJitterFPS="),FireArmJitterSampleFPS); // 仅专属Editor夹具读取，不注册包体调试接口。
    bFireRecoilAimCorners=bFireArmJitterAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoFireRecoilAimCorners"));
    bFireRecoilOperationBoundary=bFireArmJitterAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoFireRecoilOperationBoundary")); // 只扩显式专项输入，不给游戏增加可绕过门禁的调试接口。
    if (bFireArmJitterAudit) FParse::Value(FCommandLine::Get(),TEXT("DemoFireRecoilRateScale="),FireRecoilTestRateScale);
    if (bFireArmJitterAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoReloadHoldAudit")))
    {
        FParse::Value(FCommandLine::Get(),TEXT("DemoReloadHoldOnlyStage="),ReloadHoldOnlyStage); // 只选隔离诊断阶段，不跳过生产授权/装填或改Pose。
        if (ReloadHoldOnlyStage!=INDEX_NONE)
        {
            FParse::Value(FCommandLine::Get(),TEXT("DemoReloadHoldTailSeconds="),ReloadHoldTailSeconds); // 仅单例延长真实观察，不伪造动画时钟。
            if (!FMath::IsFinite(ReloadHoldTailSeconds) || ReloadHoldTailSeconds<0.f || ReloadHoldTailSeconds>12.f)
            { UE_LOG(LogFPSDemo,Warning,TEXT("RELOAD_HOLD_TAIL_INVALID; original duration retained")); ReloadHoldTailSeconds=0.f; }
            bNativeIdleInterruptAudit=ReloadHoldOnlyStage%8==0 && ReloadHoldTailSeconds>=6.f && FParse::Param(FCommandLine::Get(),TEXT("DemoNativeIdleInterruptAudit")); // 只有明确中性Hold单例长尾接受真实输入试验。
        }
    }
    bCapture|=bFireArmJitterCapture; // 沿用现有隔离补光/相机初始化，实际截图由开火专项逐帧请求。
#endif
    bMovementViewAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerMoveYawTest")); // 本次移动/衣领专项，输出与原始Yaw证据隔离。
    bYawAudit=bMovementViewAudit || FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerYawTest")); // 水平专项不覆盖已冻结的俯仰验收报告。
    bAimAudit=bYawAudit || FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerAimTest")); // 水平专项继续保留原极限俯仰/握枪断言。
    bLyraAimAudit=FParse::Param(FCommandLine::Get(),TEXT("DemoLyraAimTest")); bAimAudit|=bLyraAimAudit;
    bAimAudit|=bAimContractAudit;
}

bool ADemoPlayerAnimationTest::Check(bool Condition, const FString& Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_ANIMATION_TEST %s stage=%s: %s"),Condition?TEXT("PASS"):TEXT("FAIL"),*StageName,*Message);
    if (!Condition && !bFailed)
    {
        bFailed = true;
        if (Report) Report->SetStringField(TEXT("failure"),Message);
        SaveAndExit(false);
    }
    return Condition;
}

bool ADemoPlayerAnimationTest::InitializeFixture()
{
    DEMO_LOG_CALL();
#if !WITH_EDITOR
    return Check(false,TEXT("player art acceptance requires isolated WITH_EDITOR progress"));
#else
    if (bAwaitingFixtureCombat)
    {
        const ADemoGameState* Current=GetWorld()->GetGameState<ADemoGameState>(); // 关卡真实异步提交后才允许正式开火/换弹测试。
        if (!Current || Current->Phase!=EDemoPhase::Combat || Current->LevelNumber!=1) return false;
        bAwaitingFixtureCombat=false;
        for (TActorIterator<ADemoEnemy> Enemy(GetWorld());Enemy;++Enemy) { Enemy->SetActorTickEnabled(false); Enemy->SetActorEnableCollision(false); }
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_ANIMATION_FIXTURE_COMBAT_READY async streaming completed"));
        return true;
    }
    Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("version"),1);
    Report->SetStringField(TEXT("matrix_format"),TEXT("row-major flattened 4x4 COLUMN-vector transform, UE centimetres"));
    ADemoPlayerController* Controller = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本进程唯一正式控制器。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前规则与安全区终端归World所有。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 临时夹具改变阶段，不改正式关卡配置。
    UDemoPlayerProfile* Profile = GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // Editor临时权限，与玩家持久档案隔离。
    if (!Check(Controller && Mode && State && Profile,TEXT("isolated services available"))) return false;
    const bool bTemporaryArtLoadout=bFireArmJitterAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoReloadHoldAudit")); // 仅本次显式Editor装填专项，不改变其它历史夹具授权方式。
    if (bTemporaryArtLoadout)
    {
        if (!Check(GetWorld()->GetNetMode()==NM_Standalone,TEXT("reload hold fixture uses isolated standalone editor authority"))) return false;
        Profile->DebugUnlockAllForSession(); // 现有每GI临时权限，停止试玩清空；不制造通关/金币/Ownership或保存记录。
        Report->SetStringField(TEXT("fixture_weapon_authorization"),TEXT("existing Editor GI DebugUnlockAll; validated PS query; transient loadout transaction only"));
    }
    else
    {
        State->Phase=EDemoPhase::Victory; State->LevelNumber=10;
        for (int32 Index=0; Index<3; ++Index) // 历史其它夹具保留原通关适配；不将其当新枪店永久权来源。
        {
            State->Difficulty=static_cast<EDemoDifficulty>(Index);
            Profile->RecordVictory(FGuid::NewGuid().ToString(),State->Difficulty);
            DemoTestProgress::ImportLocalPermissions(State);
        }
    }
    State->Phase=EDemoPhase::Lobby; State->LevelNumber=0; State->Difficulty=EDemoDifficulty::Normal;
    if (!Check(Mode->StartRun(),TEXT("real safe hub initialized"))) return false;
    Controller->OnRunReady();
    Player=Cast<ADemoCharacter>(Controller->GetPawn());
    if (!Check(Player.IsValid() && Player->GetMesh()->GetSkeletalMeshAsset() && Player->GetMesh()->GetAnimInstance(),TEXT("full body mesh and runtime animation instance ready"))) return false;
    State->Phase=EDemoPhase::Combat;
    for (TActorIterator<ADemoEnemy> Enemy(GetWorld()); Enemy; ++Enemy) // 敌人隔离仅限本测试World，避免攻击打断观察。
    { Enemy->SetActorTickEnabled(false); Enemy->SetActorEnableCollision(false); }
    // 白模主场景位于Z=10000cm，验收平台放在其上方，避免落入已有地基或封闭碰撞。
    const float GroundHeight=20000.f; // 临时平台顶面世界厘米，独立记录给足底几何检查。
    AStaticMeshActor* Platform = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0,0,GroundHeight-10.f),FRotator::ZeroRotator); // Cube原高100cm、缩放.2，中心距顶面10cm。
    Platform->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable); // 测试World运行中设置尺寸，避免Static组件拒绝变换。
    Platform->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Platform->SetActorScale3D(bDirectionalAudit?FVector(400,400,.2):FVector(100,100,.2)); // 连续换向/长装填不复位位置；40k厘米平台避免后续跑动离开夹具地面。
    PlatformSpawn=FVector(0,0,GroundHeight+Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f);
    Report->SetNumberField(TEXT("ground_world_z_cm"),GroundHeight);
    Report->SetStringField(TEXT("body_mesh"),Player->GetMesh()->GetSkeletalMeshAsset()->GetPathName());
    Report->SetObjectField(TEXT("body_reference_component_bones"),PlayerAuditBones(Player->GetMesh(),true));
    Report->SetStringField(TEXT("fp_mesh"),Player->GetMesh1P()->GetSkeletalMeshAsset()->GetPathName());
    Report->SetObjectField(TEXT("fp_reference_component_bones"),PlayerAuditBones(Player->GetMesh1P(),true));
    Camera=GetWorld()->SpawnActor<ACameraActor>();
    Camera->GetCameraComponent()->SetFieldOfView(60.f); // 扩大取景让完整鞋底避开原HUD，55度试拍仍压住鞋尖，保留真实界面与Owner可见性。
    if (bCapture && FApp::CanEverRender()) // 仅渲染验收场景补光；不保存关卡，也不影响骨矩阵或正式地图光照。
    {
        ADirectionalLight* Fill=GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector,FRotator(-35.f,140.f,0.f)); // World拥有瞬时中性前侧补光，随测试World销毁。
        if (!Check(Fill!=nullptr,TEXT("temporary visual inspection fill light ready"))) return false;
        Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Fill->GetLightComponent()->SetIntensity(3.f); // lux；只补回逆光细节，不修改材质色值或隐藏穿模。
        Report->SetNumberField(TEXT("inspection_fill_light_lux"),3.f); // 报告明确说明图片含验收补光，不冒充正式关卡光照。
        if(bShoulderShadowAudit)
        {
            for(TActorIterator<ADirectionalLight> Light(GetWorld());Light;++Light) // 仅隔离World让太阳从玩家身后照射，影子位于第一人称可见前方。
            { Light->GetLightComponent()->SetMobility(EComponentMobility::Movable); Light->SetActorRotation(FRotator(-25.f,0.f,0.f)); Light->GetLightComponent()->SetIntensity(4.f); }
            Report->SetStringField(TEXT("shadow_fixture"),TEXT("isolated sunlight pitch -25 yaw 0; owner camera pitch -20; original world assets unchanged"));
        }
    }
    Controller->SetViewTarget(bFirstPersonCapture ? static_cast<AActor*>(Player.Get()) : static_cast<AActor*>(Camera.Get())); // 使用原Owner可见性选择FP/TP，不强行显示或隐藏网格。
    AddTickPrerequisiteComponent(Player->GetPlayerPresentation()); // 同一PostUpdateWork组内显式等展示枪更新，记录同帧双网格。
    Controller->SetControlRotation(FRotator::ZeroRotator);
    Player->SetActorRotation(FRotator::ZeroRotator);
    if (!EquipWeapon(0)) return false;
    State->Phase=EDemoPhase::Hub; // 射击上下文要求真实战斗Session；仅伪造Combat枚举不足以启动子弹。
    UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Player.Get()); // 本次Editor隔离PS拥有的选择组件，不接触持久存档。
    if (!Check(Build && (Build->HasSelectedCore() || Build->TrySelectCore(TEXT("B01"))),TEXT("fixture core selected through authoritative build API"))) return false;
    Mode->StartNextLevel(); // 正式入口为void，必须观察实际Combat阶段和关号，不把调用等同于成功。
    if (State->Phase!=EDemoPhase::Combat || State->LevelNumber!=1)
    {
        bAwaitingFixtureCombat=true; // 新战役房间流送不能在调用当帧断言已经Combat；下一Tick读真实结果。
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_ANIMATION_FIXTURE_COMBAT_WAIT waiting real room commit"));
        return false;
    }
    if (!Check(State->Phase==EDemoPhase::Combat && State->LevelNumber==1,TEXT("real first-level combat session initialized"))) return false;
    for (TActorIterator<ADemoEnemy> Enemy(GetWorld()); Enemy; ++Enemy) // 真开关后冻结新生敌人，仅隔离观察，保留合法关卡凭据。
    { Enemy->SetActorTickEnabled(false); Enemy->SetActorEnableCollision(false); }
    return true;
#endif
}

bool ADemoPlayerAnimationTest::EquipWeapon(int32 Index)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* Controller=Cast<ADemoPlayerController>(Player->GetController()); // 借用当前Pawn对应控制器。
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 正式终端从当前模式读取。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 暂切Hub以通过正式交易检查，随后恢复Combat。
    if (!Check(Controller && Mode && State,TEXT("weapon selection services ready"))) return false;
    const FName ExpectedIds[]={TEXT("pistol"),TEXT("rifle"),TEXT("shotgun"),TEXT("sniper")}; // 固定四枪身份，先校索引才允许授权/目录查询。
    if (!Check(Index>=0 && Index<UE_ARRAY_COUNT(ExpectedIds),TEXT("weapon fixture catalog index valid"))) return false;
    bool bTemporaryArtLoadout=false; // 非Editor始终false；仅显式隔离专项改用现有GI临时权限，不为生产终端增加绕过。
#if WITH_EDITOR
    bTemporaryArtLoadout=bFireArmJitterAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoReloadHoldAudit"));
    if (bTemporaryArtLoadout)
    {
        UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 当前GI临时开关，既非武器拥有权也非跨局记录。
        const ADemoPlayerState* PS=Player->GetPlayerState<ADemoPlayerState>(); // 当前本机Pawn真实授权组件。
        UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
        UDemoWeaponComponent* Equipment=Player->GetWeaponComponent(); // 完整权威库存事务校验器，不直接Spawn或改库存指针。
        if (!Check(GetWorld()->GetNetMode()==NM_Standalone && Player->HasAuthority() && Controller->IsLocalController() && Profile && Profile->IsDebugUnlockAllForSession() && Progress && Equipment,TEXT("isolated editor temporary weapon permission ready"))) return false;
        TArray<FName> Allowed; // 仅从现有Profile/PS临时权限查询派生，不把任意请求名当授权。
        for (int32 CatalogIndex=0;CatalogIndex<DemoWeaponCatalog::Num();++CatalogIndex)
        {
            const FName Id=DemoWeaponCatalog::IdAt(CatalogIndex); // 当前固定目录键。
            if (Profile->IsUnlocked(Id) && Progress->IsWeaponUnlocked(Id)) Allowed.Add(Id);
        }
        if (!Check(Allowed.Contains(ExpectedIds[Index]),TEXT("requested weapon authorized by existing editor permission"))) return false;
        FDemoLoadoutSnapshot Target; Equipment->CaptureSnapshot(Target); // 保留真实库存及弹药类型；仅补本次已授权目标的空弹药值。
        bool bHasTarget=false;
        for (const FDemoWeaponSnapshot& Item:Target.Weapons) bHasTarget|=Item.Id==ExpectedIds[Index];
        if (!bHasTarget)
        {
            FDemoWeaponSnapshot Added; Added.Id=ExpectedIds[Index]; // 初次只建零弹药临时实例；各案例随后原本就显式准备隔离弹药。
            Target.Weapons.Add(Added);
        }
        Target.ActiveSlot=Index==0?2:1; if (Index>0) Target.PrimaryId=ExpectedIds[Index];
        State->Phase=EDemoPhase::Hub; // 原库存恢复API仅安全阶段可用，仍走其完整预检和回滚，不改商店授权或来源账本。
        Controller->GetMenuFlow()->RefreshFromReplicatedState();
        if (!Check(Equipment->PrepareSnapshot(Target,Allowed),TEXT("temporary authorized loadout prepared through existing inventory transaction"))) return false;
        if (!Equipment->ApplyPreparedSnapshot())
        {
            Equipment->FinishSnapshot(false); // 原失败回滚销毁候选、保留旧实例；拒绝后不伪称装备成功。
            return Check(false,TEXT("temporary authorized loadout installation failed"));
        }
        if (!Check(Equipment->FinishSnapshot(true),TEXT("temporary authorized loadout committed without persistent ownership"))) return false;
        UE_LOG(LogFPSDemo,Log,TEXT("RELOAD_HOLD_FIXTURE_EQUIP id=%s authorization=editor_gi temporary_inventory=1 persistent_ownership_modified=0"),*ExpectedIds[Index].ToString());
    }
#endif
    if (!bTemporaryArtLoadout)
    {
    if (!Mode->GetShopTerminal()) // 正式开战已销毁终端；夹具只重建实际Hub终端，不调用会旅行销毁本测试的回Hub流程。
    {
        FDemoAreaSnapshot Area; // 当前World安全区值快照，沿用正式对地生成和交互校验。
        FString Error; // 锚点解析失败原因，写入独立测试断言。
        if (!Check(GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>()->ResolveArea(-1,Area,Error),TEXT("fixture hub area resolves: ")+Error)) return false;
        if (!Check(Mode->GetTerminalSystem()->CreateTerminals(Area),TEXT("fixture real terminal service creates selection pair"))) return false;
    }
    if (!Check(Mode->GetShopTerminal()!=nullptr,TEXT("weapon terminal ready"))) return false;
    State->Phase=EDemoPhase::Hub;
    Controller->GetMenuFlow()->RefreshFromReplicatedState(); // 同步夹具改变阶段需显式通知现有UI，不能等下一次交易回执清理刚开的终端。
    Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
    Mode->GetShopTerminal()->Interact(Player.Get());
    Controller->SetTerminalWeaponPage(true);
    Controller->InspectWeapon(Index==0?1:Index);
    Controller->EquipInspectedWeapon();
    Controller->CloseWeaponTip(); Controller->CloseUpgradeMenu();
    if (Index==0 && !Check(Player->GetWeaponComponent()->EquipSlot(2),TEXT("secondary pistol equipped"))) return false;
    }
    State->Phase=EDemoPhase::Combat;
    Weapon=Player->GetWeaponComponent()->GetActiveWeapon();
    if (!Check(Weapon.IsValid(),TEXT("real inventory weapon equipped"))) return false;
    if (!Check(Weapon->GetWeaponId()==ExpectedIds[Index],TEXT("selected weapon matches requested identity"))) return false;
    Weapon->Config.bInfiniteReserve=false; // 与原武器专项一致，仅隔离Actor改有限备用，保证重复结算可被弹药断言发现。
    if (Stage>=7 && !bTemporaryArtLoadout) // 旧夹具终端流程保持；新临时库存不创建/清理枪店或终端。
    {
        Mode->GetTerminalSystem()->ClearTerminals();
    }
    return true;
}

bool ADemoPlayerAnimationTest::BeginStage()
{
    DEMO_LOG_CALL();
    static const TCHAR* MovementNames[]={TEXT("Idle"),TEXT("Walk"),TEXT("Run"),TEXT("Jump"),TEXT("Sprint"),TEXT("Strafe"),TEXT("Backwards")}; // 7项Movement真实输入用例。
    static const TCHAR* WeaponNames[]={TEXT("Pistol"),TEXT("Rifle"),TEXT("Shotgun"),TEXT("Sniper")}; // 与正式目录手枪+三主枪索引相符。
    static const TCHAR* ActionNames[]={TEXT("Idle"),TEXT("Tactical"),TEXT("Empty"),TEXT("Fire"),TEXT("Cancel")}; // 每枪均检查稳定持枪、两类装填、射击/拉栓及取消。
    StageName=Stage<7 ? FString(MovementNames[Stage]) : FString(WeaponNames[(Stage-7)/5])+TEXT("_")+ActionNames[(Stage-7)%5];
    Player->GetWeaponComponent()->CancelActions();
    if (Stage>=7 && (Stage-7)%5==0 && !EquipWeapon((Stage-7)/5)) return false;
    Player->GetCharacterMovement()->StopMovementImmediately();
    Player->ConsumeMovementInputVector(); // PostUpdate阶段末仍可能排队上一阶段输入；独立用例复位时一并清空，防止跑步W残留把原地跳误变成前跳。
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics);
    Player->StopJumping();
    CastChecked<ADemoPlayerController>(Player->GetController())->SetViewTarget(bFirstPersonCapture ? static_cast<AActor*>(Player.Get()) : static_cast<AActor*>(Camera.Get())); // 阶段切换恢复所选观察点；第一人称继续使用正式相机组件。
    StageStart=GetWorld()->GetTimeSeconds(); StageDuration=2.f;
    bLyraJumped=false;
    if (bYawAudit && Stage>=7 && (Stage-7)%5==0) StageDuration=bMovementViewAudit?8.8f:3.2f; // 衣领专项增加-45..45七个中间头角；每格0.8秒，后四格仍保留完整俯仰组合。
    StageSamples=0; MaximumSpeed=0; MaximumFootTravel=0;
    ObservedStates.Reset();
    bSawFalling=false; bSawDash=false; bCancelled=false;
    bAimRaised=false; AimMinimumObserved=MuzzleMinimumObserved=90.f; AimMaximumObserved=MuzzleMaximumObserved=-90.f; AimInvalidSamples=0; // 当前阶段动画和真实枪口两种证据独立统计。
    if (bYawAudit) { Player->SetActorRotation(FRotator::ZeroRotator); YawCorner=0; AimRequestedYaw=Stage>=7 && ((Stage-7)%5==2 || (Stage-7)%5==3)?60.f:-60.f; } // 单机夹具复位后只走真实FaceRotation；网络测试不复位身体。
    if (bMovementViewAudit && Stage>=7 && (Stage-7)%5==0) { AimRequestedYaw=-45.f; RequestAimAuditPitch(0.f); } // 静止扫头从-45开始，身体初始0；不能只验阈值端点。
    else if (bAimAudit) RequestAimAuditPitch(Stage>=7 && ((Stage-7)%5==2 || (Stage-7)%5==3)?120.f:-120.f); // 两种装填分别在低头/抬头极限，Hold中途反向。
    NextSample=StageStart+.05f;
    NextCapture=StageStart+.05f; // 留出真实相机更新帧，避免把本阶段截图命名给上一阶段缓存的View。
    if (Stage==3) Player->Jump(); // 真实CharacterMovement跳跃，不伪造IsFalling字段。
    if (Stage==4 && !Check(Player->GetDemoASC()->ActivateDemoAbility(UDemoDashAbility::StaticClass()),TEXT("GAS dash activated"))) return false;
    if (Stage>=7)
    {
        const int32 Action=(Stage-7)%5; // 当前枪动作0..4，所有时序都从实际武器获取。
        if (Action==1 || Action==2 || Action==4)
        {
            StartAmmo=Action==2?0:1;
            const int32 Reserve=Weapon->Config.bReloadOneRoundAtATime?1:2; // 管式只录一颗，盒式只补两发便于独立结算断言。
            Weapon->RestoreAmmo(StartAmmo,Reserve);
            ExpectedAmmo=FMath::Min(Weapon->GetCapacity(),StartAmmo+Reserve);
            ExpectedReserve=Reserve-(ExpectedAmmo-StartAmmo);
            Player->GetWeaponComponent()->RequestReload();
            if (!Check(Weapon->IsReloading(),TEXT("real GAS reload started"))) return false;
            StageDuration=Weapon->GetReloadDuration()+.5f;
        }
        else if (Action==3)
        {
            StartAmmo=FMath::Min(3,Weapon->GetCapacity()); Weapon->RestoreAmmo(StartAmmo,2); // 双管夹具遵守正式容量。
            Player->GetWeaponComponent()->StartFire(); Player->GetWeaponComponent()->StopFire();
            StageDuration=FMath::Max(2.f,60.f/Weapon->Config.RoundsPerMinute+.5f);
        }
    }
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_ANIMATION_STAGE name=%s duration=%.3f"),*StageName,StageDuration);
    return true;
}

USkeletalMeshComponent* ADemoPlayerAnimationTest::FindPresentationWeapon() const
{
    DEMO_LOG_TICK();
    TArray<USkeletalMeshComponent*> Components; // 同步借用Pawn拥有的显示组件，不延长其生命周期。
    Player->GetComponents(Components);
    for (USkeletalMeshComponent* Component : Components) // Body、FP均排除；展示枪必须匹配真实武器配置。
        if (Component!=Player->GetMesh() && Component!=Player->GetMesh1P() && Component->GetSkeletalMeshAsset()==Weapon->Config.Mesh) return Component;
    return nullptr;
}

void ADemoPlayerAnimationTest::RecordFrame()
{
    DEMO_LOG_TICK();
    USkeletalMeshComponent* Body=Player->GetMesh(); // 动画本帧完成，矩阵立即复制入JSON。
    USkeletalMeshComponent* Gun=FindPresentationWeapon(); // 当前第三人称机械组件，空值仍保存骨帧并在阶段末拒绝。
    TSharedPtr<FJsonObject> Frame=MakeShared<FJsonObject>(); // 一帧纯值遥测，不持有UObject。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoFullBodyChecks")))
    {
        const UDemoPlayerPresentationComponent* Presentation=Player->GetPlayerPresentation(); // 验收只读真实已初始化策略，不强写显示/骨姿。
        const USkeletalMeshComponent* OwnerBody=Presentation?Presentation->GetFirstPersonBodyMesh():nullptr;
        const bool bShared=Presentation && Presentation->UsesFullBodyFirstPerson() && OwnerBody && OwnerBody->LeaderPoseComponent.Get()==Body;
        Frame->SetBoolField(TEXT("full_body_same_pose"),bShared);
        Frame->SetBoolField(TEXT("legacy_arms_hidden"),Player->GetMesh1P()->bHiddenInGame);
        Frame->SetBoolField(TEXT("independent_fp_weapons"),Presentation && Presentation->UsesIndependentFirstPersonWeapons());
        Frame->SetBoolField(TEXT("owner_body_hidden"),!OwnerBody || OwnerBody->bHiddenInGame);
        if (!Check(bShared && Player->GetMesh1P()->bHiddenInGame==Presentation->UsesSharedBodyWeaponDisplay(),TEXT("owner lower body follows TP while gun arms use their configured independent/shared policy"))) return;
        const APlayerController* ViewController=Cast<APlayerController>(Player->GetController()); // 本帧借用真实拥有者，初始化函数的局部Controller不属于本采样作用域。
        const bool bOwnerFP=Presentation->UsesIndependentFirstPersonWeapons() && Player->IsLocallyControlled() && ViewController && ViewController->GetViewTarget()==Player.Get(); // 观察相机不借用隐藏的本人FP枪。
        const USkeletalMeshComponent* VisualGun=bOwnerFP?Weapon->GetWeaponSkeletalMesh():Gun; // 当前观察者实际枪口组件，物理枪仍独立核世界TP。
        const FName ExpectedVisualSocket=bOwnerFP&&VisualGun&&VisualGun->DoesSocketExist(Weapon->Config.MuzzleFlashSocket)
            ?Weapon->Config.MuzzleFlashSocket:Weapon->Config.MuzzleSocket; // FP表现源取新口心，legacy/TP仍旧Muzzle；下方物理断言独立保持。
        if (VisualGun && VisualGun->DoesSocketExist(ExpectedVisualSocket))
        {
            const float Error=FVector::Distance(Weapon->GetVisualMuzzleLocation(),VisualGun->GetSocketLocation(ExpectedVisualSocket)); // 只验表现getter；不把独立FP视觉坐标当物理弹源。
            Frame->SetNumberField(TEXT("shared_visible_muzzle_error_cm"),Error);
            if (!Check(Error<.01f,TEXT("observed weapon socket is the visual muzzle source"))) return;
        }
        if (Gun && Gun->DoesSocketExist(Weapon->Config.MuzzleSocket))
        {
            const float PhysicalError=FVector::Distance(Weapon->GetMuzzleLocation(),Gun->GetSocketLocation(Weapon->Config.MuzzleSocket)); // 未经安全扫掠收回前的世界物理Socket，不随FP模式改变。
            Frame->SetNumberField(TEXT("world_physical_muzzle_error_cm"),PhysicalError);
            if (!Check(PhysicalError<.01f,TEXT("server physical muzzle remains the TP world socket"))) return;
        }
    }
    Frame->SetStringField(TEXT("stage"),StageName);
    Frame->SetNumberField(TEXT("seconds"),GetWorld()->GetTimeSeconds()-StageStart);
    Frame->SetNumberField(TEXT("speed_cm_s"),Player->GetVelocity().Size2D());
    Frame->SetBoolField(TEXT("falling"),Player->GetCharacterMovement()->IsFalling());
    Frame->SetBoolField(TEXT("dash"),Player->IsDashEvading());
    // 移动失败必须能区分输入门禁、Movement关闭和实际动画问题；只记录正式组件真值。
    Frame->SetBoolField(TEXT("move_input_ignored"),Player->IsMoveInputIgnored());
    Frame->SetNumberField(TEXT("movement_mode"),Player->GetCharacterMovement()->MovementMode);
    if (bAimContractAudit) RecordAimContractState(Frame);
    if (bStartJitterAudit) RecordStartJitterState(Frame); // 只在显式诊断中记录原生图变量，避免默认逐帧日志。
    Frame->SetNumberField(TEXT("max_walk_speed"),Player->GetCharacterMovement()->MaxWalkSpeed);
    Frame->SetNumberField(TEXT("acceleration"),Player->GetCharacterMovement()->GetCurrentAcceleration().Size());
    Frame->SetNumberField(TEXT("pending_input"),Player->GetPendingMovementInputVector().Size());
    Frame->SetBoolField(TEXT("movement_active"),Player->GetCharacterMovement()->IsActive());
    UDemoPlayerAnimInstance* Animation=Cast<UDemoPlayerAnimInstance>(Body->GetAnimInstance()); // 同一帧实际实例，只读查询原生状态机与动作；不写图状态。
    if (Animation)
    {
        Frame->SetStringField(TEXT("animation_class"),Animation->GetClass()->GetPathName()); // 区分正式V4缓存与正在求值的Lyra图，不能用兼容字段冒充原生图证据。
        const int32 Machine=Animation->GetStateMachineIndex(TEXT("LocomotionSM")); // 原Lyra状态机名称；旧图没有该状态机则记录None。
        Frame->SetStringField(TEXT("native_locomotion_state"),Machine!=INDEX_NONE?Animation->GetCurrentStateName(Machine).ToString():TEXT("None"));
        Frame->SetStringField(TEXT("active_montage"),GetNameSafe(Animation->GetCurrentActiveMontage()));
        const FVector RootScale=Body->GetSocketTransform(TEXT("root"),RTS_Component).GetScale3D(); // 实际最终骨姿，不能用源FBX或组件Scale代替。
        Frame->SetNumberField(TEXT("root_scale_error"),(RootScale-FVector::OneVector).GetAbsMax());
        Frame->SetNumberField(TEXT("body_motion_state"),Animation->BodyMotionState);
        Frame->SetNumberField(TEXT("body_motion_phase"),Animation->BodyMotionPhase);
        Frame->SetNumberField(TEXT("upper_blend_alpha"),Animation->UpperBodyBlendAlpha);
        Frame->SetNumberField(TEXT("aim_alignment_weight"),Animation->AimAlignmentWeight);
        Frame->SetStringField(TEXT("body_motion_asset"),GetNameSafe(Animation->BodyMotionPose));
        if (!RootScale.Equals(FVector::OneVector,.001)) { Frames.Add(MakeShared<FJsonValueObject>(Frame)); Check(false,TEXT("final evaluated root scale must stay one in every transition")); return; }
        Frame->SetNumberField(TEXT("locomotion_state"),Animation->LocomotionState);
        Frame->SetNumberField(TEXT("locomotion_time"),Animation->LocomotionTime);
        Frame->SetStringField(TEXT("upper_body_action"),Animation->UpperBodyAction.ToString());
        Frame->SetNumberField(TEXT("upper_body_time"),Animation->UpperBodyTime);
        Frame->SetNumberField(TEXT("action_progress"),Animation->ActionProgress);
        if (bDirectionalAudit)
        {
            Frame->SetNumberField(TEXT("gait_phase"),Animation->GaitPhase);
            Frame->SetNumberField(TEXT("ground_right"),Animation->GroundRight);
            Frame->SetNumberField(TEXT("ground_forward"),Animation->GroundForward);
            Frame->SetNumberField(TEXT("ground_weight"),Animation->GroundPoseWeight);
            Frame->SetNumberField(TEXT("operation_weight"),Animation->ActionOverlayWeight);
            Frame->SetNumberField(TEXT("maximum_contact_drift_cm"),MaximumContactDrift);
        }
        if (bPistolRunArmAudit) // 同帧原生权重与最终骨姿势一起记录，防止仅参数正确却未接入动画图。
        {
            Frame->SetNumberField(TEXT("run_left_arm_weight"),Animation->RunLeftArmWeight);
            Frame->SetNumberField(TEXT("run_left_arm_time"),Animation->RunLeftArmTime);
            Frame->SetNumberField(TEXT("upper_blend_alpha"),Animation->UpperBodyBlendAlpha);
            Frame->SetStringField(TEXT("previous_upper_action"),Animation->PreviousUpperBodyAction.ToString());
            Frame->SetBoolField(TEXT("weapon_reloading"),Weapon->IsReloading());
        }
        if (bAimAudit)
        {
            const UDemoPlayerAimSettings* Settings=GetDefault<UDemoPlayerAimSettings>(); // 只读实际配置，与故意越界的输入分开记录。
            const float ControlPitch=FRotator::NormalizeAxis(Player->GetController()->GetControlRotation().Pitch); // 生产UpdateRotation后的真实控制方向。
            const float MuzzlePitch=Gun?Gun->GetSocketRotation(Weapon->Config.MuzzleSocket).Vector().Rotation().Pitch:0.f; // 实际求值后的TP枪口方向，非动画变量。
            AimMinimumObserved=FMath::Min(AimMinimumObserved,Animation->AimPitch); AimMaximumObserved=FMath::Max(AimMaximumObserved,Animation->AimPitch);
            MuzzleMinimumObserved=FMath::Min(MuzzleMinimumObserved,MuzzlePitch); MuzzleMaximumObserved=FMath::Max(MuzzleMaximumObserved,MuzzlePitch);
            if (ControlPitch < -Settings->LookDownDegrees-.01f || ControlPitch>Settings->LookUpDegrees+.01f || !Animation->bAimWeaponActive
                || !Player->GetMesh1P()->IsAttachedTo(Player->GetFirstPersonCameraComponent())) ++AimInvalidSamples;
            Frame->SetNumberField(TEXT("aim_requested_pitch"),AimRequestedPitch); Frame->SetNumberField(TEXT("control_pitch"),ControlPitch);
            Frame->SetNumberField(TEXT("aim_pitch"),Animation->AimPitch); Frame->SetNumberField(TEXT("tp_muzzle_pitch"),MuzzlePitch);
            Frame->SetNumberField(TEXT("camera_pitch"),Player->GetFirstPersonCameraComponent()->GetComponentRotation().Pitch);
            Frame->SetNumberField(TEXT("aim_support_weight"),Animation->AimSupportWeight);
            if (Gun && Animation->bCalibratedBoreAim)
            {
                const FVector Bore=Gun->GetSocketRotation(Weapon->Config.MuzzleSocket).Vector(); // 实际渲染枪管，独立于生产节点的胸部标定。
                const FVector Target=Player->GetBaseAimRotation().Vector(); // 本Pawn输入，测试旁观相机不是目标。
                const float Error=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Bore,Target),-1.,1.))); // 三维角差同时覆盖Yaw/Pitch。
                Frame->SetNumberField(TEXT("muzzle_view_error_degrees"),Error);
                Frame->SetBoolField(TEXT("legacy_view_direction_assertion_applicable"),!FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview"))); // 当前汇聚追相机焦点，旧纯视线1度合同不适合作为本项诊断退出条件；原始角差仍保存。
                if (!FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview")) && Animation->UpperBodyAction==TEXT("Hold") && Animation->UpperBodyBlendAlpha>.999f && Animation->AimAlignmentWeight>.999f && GetWorld()->GetTimeSeconds()-StageStart>.2f && Error>1.f)
                { Frames.Add(MakeShared<FJsonValueObject>(Frame)); Check(false,TEXT("settled hold muzzle must match view within one degree including jump/land")); return; }
            }
            if (bYawAudit)
            {
                const float BodyYaw=FRotator::NormalizeAxis(Player->GetActorRotation().Yaw),ControlYaw=FRotator::NormalizeAxis(Player->GetController()->GetControlRotation().Yaw); // 实际Actor与Controller角度，不使用动画缓存反推。
                const bool bMoveStage=Stage==1 || Stage==2 || Stage==4 || Stage==5 || Stage==6; // 独立阶段计划：WASD/冲刺横向移动必须对齐，原地垂直跳不算水平移动。
                const bool bCenterSweep=bMovementViewAudit && Stage>=7 && (Stage-7)%5==0 && YawCorner<7; // 七个静止头角全在阈值内，身体应保持0。
                const float ExpectedBody=bCenterSweep?0.f:AimRequestedYaw; // 越过45度后最终完全对齐；只在阈值内扫头时保持身体0。
                const float ExpectedHead=bCenterSweep?AimRequestedYaw:0.f; // 身体完成跟转后不再永久残留45度夹角。
                const bool bYawSettled=bMoveStage || GetWorld()->GetTimeSeconds()-StageStart-.8f*YawCorner>.65f; // 平滑转身需过渡时间；中间帧仍落报告，专项DemoViewTurnTest独立验证不能瞬移。
                const bool YawValid=FMath::Abs(FMath::FindDeltaAngleDegrees(AimRequestedYaw,ControlYaw))<.1f
                    && FMath::Abs(FMath::FindDeltaAngleDegrees(ExpectedBody,BodyYaw))<.1f && FMath::Abs(Animation->HeadYaw-ExpectedHead)<.1f; // 首个采样已在生产Tick之后；不能只看设置成功。
                if (bYawSettled && !YawValid) { ++AimInvalidSamples; UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_YAW_POSE_INVALID stage=%s control=%.3f body=%.3f head=%.3f"),*StageName,ControlYaw,BodyYaw,Animation->HeadYaw); }
                Frame->SetBoolField(TEXT("yaw_settled"),bYawSettled); // 报告区分过渡帧和终点断言，不能把未断言的中间帧声称为通过。
                Frame->SetNumberField(TEXT("control_yaw"),ControlYaw); Frame->SetNumberField(TEXT("body_yaw"),BodyYaw); Frame->SetNumberField(TEXT("head_yaw"),Animation->HeadYaw); // 后续几何与截图须用同一实测帧。
            }
        }
    }
    Frame->SetObjectField(TEXT("body_component_bones"),PlayerAuditBones(Body,false,bStartJitterAudit && !MotionIsolation.IsEmpty()));
    Frame->SetField(TEXT("body_component_to_world"),PlayerAuditMatrix(Body->GetComponentTransform()));
    USkeletalMeshComponent* Arms=Player->GetMesh1P(); // 同时记录真实新FP网格，不能以旧原臂报告替代。
    USkeletalMeshComponent* FPGun=Weapon->GetWeaponSkeletalMesh(); // 第一人称Actor真实机械网格，姿势与TP独立。
    if (!bStartJitterAudit || MotionIsolation.IsEmpty()) Frame->SetObjectField(TEXT("fp_component_bones"),PlayerAuditBones(Arms,false)); // 肩部隔离不重复保存无关FP手指，完整动作回归保持全骨。
    Frame->SetField(TEXT("fp_component_to_world"),PlayerAuditMatrix(Arms->GetComponentTransform()));
    if (FPGun && FPGun->GetSkeletalMeshAsset())
    {
        Frame->SetField(TEXT("fp_weapon_component_to_player"),PlayerAuditMatrix(FPGun->GetComponentTransform().GetRelativeTransform(Arms->GetComponentTransform())));
        Frame->SetObjectField(TEXT("fp_mechanical_component_bones"),PlayerAuditBones(FPGun,false));
    }
    if (Gun && Gun->GetSkeletalMeshAsset())
    {
        Frame->SetStringField(TEXT("weapon_mesh"),Gun->GetSkeletalMeshAsset()->GetPathName());
        Frame->SetField(TEXT("weapon_component_to_body"),PlayerAuditMatrix(Gun->GetComponentTransform().GetRelativeTransform(Body->GetComponentTransform())));
        Frame->SetObjectField(TEXT("mechanical_component_bones"),PlayerAuditBones(Gun,false));
        Frame->SetObjectField(TEXT("mechanical_reference_component_bones"),PlayerAuditBones(Gun,true));
    }
    Frames.Add(MakeShared<FJsonValueObject>(Frame));
    const FVector Foot=Body->GetSocketTransform(TEXT("foot_l"),RTS_Component).GetLocation(); // 去掉Actor移动，只衡量腿部真实动画。
    if (StageSamples==0) FirstFoot=Foot;
    else MaximumFootTravel=FMath::Max(MaximumFootTravel,static_cast<float>(FVector::Distance(Foot,FirstFoot)));
    ++StageSamples;
}

bool ADemoPlayerAnimationTest::FinishStage()
{
    DEMO_LOG_CALL();
    if (bAimAudit)
    {
        if (!Check(AimInvalidSamples==0,TEXT("camera pitch bounded and FP arms remain camera children; live upper-body aim enabled"))) return false;
        if (Stage>=7 && (Stage-7)%5==0 && !Check(AimMaximumObserved-AimMinimumObserved>110.f && MuzzleMinimumObserved < -57.f && MuzzleMaximumObserved>52.f,
            FString::Printf(TEXT("actual hold muzzle follows both pitch limits: aim=%.2f/%.2f muzzle=%.2f/%.2f"),AimMinimumObserved,AimMaximumObserved,MuzzleMinimumObserved,MuzzleMaximumObserved))) return false;
    }
    if (!Check(StageSamples>=8,TEXT("real evaluated pose samples recorded"))) return false;
    if (!Check(FindPresentationWeapon()!=nullptr,TEXT("third-person weapon follows full body component"))) return false;
    if ((Stage==1 || Stage==2 || Stage==5 || Stage==6) && !Check(MaximumSpeed>80.f && MaximumFootTravel>4.f,TEXT("movement drives articulated legs, not static pose"))) return false;
    if (Stage==1 && !Check(ObservedStates.Contains(1),TEXT("actual animation graph entered Walk"))) return false;
    if (Stage==2 && !Check(ObservedStates.Contains(2),TEXT("actual animation graph entered Run"))) return false;
    if (Stage==3 && !Check(bSawFalling && !Player->GetCharacterMovement()->IsFalling(),TEXT("jump took off and landed on real collision floor"))) return false;
    if (Stage==3 && !Check(ObservedStates.Contains(3) && ObservedStates.Contains(4) && ObservedStates.Contains(5),TEXT("jump start/air/land states all evaluated"))) return false;
    if (Stage==4 && !Check(bSawDash && MaximumSpeed>800.f,TEXT("sprint presentation observed during actual GAS dash"))) return false;
    if (Stage==4 && !Check(ObservedStates.Contains(6),TEXT("actual animation graph entered Sprint"))) return false;
    if (Stage>=7)
    {
        const int32 Action=(Stage-7)%5; // 当前动作决定独立期望，不从动画状态读取弹药结果。
        if ((Action==1 || Action==2) && !Check(!Weapon->IsReloading() && Weapon->GetAmmo()==ExpectedAmmo && Weapon->GetReserveAmmo()==ExpectedReserve,TEXT("reload finishes with unique GAS ammo settlement"))) return false;
        if (Action==3 && !Check(Weapon->GetAmmo()==StartAmmo-1,TEXT("fire consumes exactly one round"))) return false;
        if (Action==4 && !Check(bCancelled && !Weapon->IsReloading() && Weapon->GetAmmo()==StartAmmo,TEXT("early cancel retains ammo and releases animation"))) return false;
    }
    return true;
}

FString ADemoPlayerAnimationTest::GetReviewRoot() const
{
    DEMO_LOG_TICK();
    if (bFireArmJitterAudit && FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowOperations")) && FParse::Param(FCommandLine::Get(),TEXT("DemoReloadHoldAudit")))
        return FPaths::ProjectSavedDir()/TEXT("ReloadHoldHandoff20261114")/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 四枪新计划独立身份/时间戳，不覆盖或混入原R07十八例历史证据。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowR07")) && FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview")))
        return FPaths::ProjectSavedDir()/TEXT("GunHoldElbow20261109/CandidateR07")/(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowOperations"))?TEXT("Operations"):bFireArmJitterAudit?TEXT("Fire"):bAimContractAudit?TEXT("Static"):TEXT("Actions"))/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 新源/恢复代码独立冻结，不把旧R05/正式或过期待机数据当新版证据。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowSkinProbe")))
        return FPaths::ProjectSavedDir()/TEXT("GunHoldElbow20261109/A07CachedSkinProbe"); // 仅六秒正式扫瞄鉴别蒙皮缓存，不重采A06全部18例。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowAimOnly")) && FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview")))
        return FPaths::ProjectSavedDir()/TEXT("GunHoldElbow20261109/AimOnlyR05ReuseOperations")/(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowOperations"))?TEXT("Operations"):bFireArmJitterAudit?TEXT("Fire"):bAimContractAudit?TEXT("Static"):TEXT("Actions"))/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 明示新库/原操作组合证据，不覆盖基线或整段新操作候选。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoGunHoldElbowReview")))
        return FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowCandidate"))?TEXT("GunHoldElbow20261109/CandidateR05"):TEXT("GunHoldElbow20261109/A06RuntimeBaseline"))/(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowOperations"))?TEXT("Operations"):bFireArmJitterAudit?TEXT("Fire"):bAimContractAudit?TEXT("Static"):TEXT("Actions"))/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 当前正式三枪独立诊断，不覆盖历史待机/后坐/瞄准证据。
    if (bFireArmJitterAudit)
        return FireArmJitterDirectory.IsEmpty()?FPaths::ProjectSavedDir()/TEXT("AnimationAudit/FireArmJitter20261108/Runtime")/(MotionIsolation.IsEmpty()?TEXT("Current"):MotionIsolation):FireArmJitterDirectory; // 初始化失败也与历史动画证据隔离。
    FString IdleReviewId; // 显式Editor夹具证据批次名；仅允许单个文件名，不改变动画配置、Gameplay或历史报告。
    if ((FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakFixTest")) || FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakFixReview"))) && FParse::Value(FCommandLine::Get(),TEXT("DemoIdleBreakReviewId="),IdleReviewId))
    {
        if (IdleReviewId.IsEmpty() || IdleReviewId.Len()>64 || IdleReviewId.Contains(TEXT(".")) || IdleReviewId!=FPaths::MakeValidFileName(IdleReviewId))
        {
            UE_LOG(LogFPSDemo,Warning,TEXT("IDLE_REVIEW rejected invalid evidence id; using isolated rejected directory"));
            return FPaths::ProjectSavedDir()/TEXT("MuzzleIdleBreakReview/RejectedEvidenceId");
        }
        const FString Root=FPaths::ProjectSavedDir()/TEXT("MuzzleIdleBreakReview")/IdleReviewId; // 批次由运行脚本固定，保留旧修复版本及其哈希证据。
        const FString View=bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP"); // 两种真实视角分别记录，不混用报告。
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakFixReview")))
            return Root/TEXT("Regression")/(FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusAudit"))?TEXT("Focus"):TEXT("Actions"))/View;
        int32 Index=1; FParse::Value(FCommandLine::Get(),TEXT("DemoIdleBreakWeapon="),Index); // 原待机夹具继续负责验证0..3武器范围。
        FString Mode=TEXT("Natural"); FParse::Value(FCommandLine::Get(),TEXT("DemoIdleBreakInterrupt="),Mode); // 有限模式仍由Tick入口校验。
        return Root/FString::Printf(TEXT("Weapon%d"),Index)/View/Mode;
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakFixTest")))
    {
        int32 Index=1; FParse::Value(FCommandLine::Get(),TEXT("DemoIdleBreakWeapon="),Index); // 本诊断仅0..3合法，Tick入口会拒绝其他输入。
        FString Mode=TEXT("Natural"); FParse::Value(FCommandLine::Get(),TEXT("DemoIdleBreakInterrupt="),Mode); // 用于已验证的测试场景输出，不作为游戏动作来源。
        return FPaths::ProjectSavedDir()/TEXT("MuzzleIdleBreakFix20261030/R02LocalPose")/FString::Printf(TEXT("Weapon%d"),Index)/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP"))/Mode;
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakFixReview")))
        return FPaths::ProjectSavedDir()/TEXT("MuzzleIdleBreakFix20261030/R02LocalPose/Regression")/(FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusAudit"))?TEXT("Focus"):TEXT("Actions"))/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 不覆盖修复前32案/27动作证据。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakAudit")))
        return FPaths::ProjectSavedDir()/TEXT("MuzzleIdleBreak20261030")/(MotionIsolation.IsEmpty()?FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusBaseline"))?TEXT("FocusOff"):TEXT("Current"):MotionIsolation); // 诊断版本独立保存，不覆盖准星焦点已有验收。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusRecord")))
        return FPaths::ProjectSavedDir()/TEXT("MuzzleFocus20261030/Video")/(FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusBaseline"))?TEXT("Before"):TEXT("After")); // 视频证据独立，不能覆盖32案/27动作原验收。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusAudit")))
        return FPaths::ProjectSavedDir()/TEXT("MuzzleFocus20261030")/(FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusBaseline"))?TEXT("Baseline"):TEXT("Formal"))/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 新需求证据隔离，不能覆盖历史瞄准库验收。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusRegression")))
        return FPaths::ProjectSavedDir()/TEXT("MuzzleFocus20261030/Actions")/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 现有真实27阶段移动/换弹/开火回归只换报告目录，不开启候选资产。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoFullBodyChecks")))
        return FPaths::ProjectSavedDir()/TEXT("FullBodyView20261029/Final")/(bAimContractAudit?TEXT("Static"):TEXT("Actions"))/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 最终眼位与附件互斥独立证据，保留首轮Static作对照。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimCandidate")) || FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimAudit")))
        return FPaths::ProjectSavedDir()/TEXT("LowAimReview")/(FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimCandidate"))?TEXT("Candidate"):TEXT("Formal"))/(bAimContractAudit?TEXT("Static"):TEXT("Actions"))/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 保护旧证据；候选/正式、静态/完整动作分别记录。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoAuthoredAimFormal"))) return FPaths::ProjectSavedDir()/TEXT("AimContractAudit/Formal")/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 仅改变证据路径；不选择候选或修改生产引用，保护冻结的修复前报告。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoAuthoredAimCandidate")) && !bAimContractAudit) return FPaths::ProjectSavedDir()/TEXT("AimContractAudit/AuthoredCandidate/Actions")/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 完整动作回归隔离于静态专项和旧正式记录。
    if (bAimContractAudit) return FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("DemoAuthoredAimCandidate"))?TEXT("AimContractAudit/AuthoredCandidate"):TEXT("AimContractAudit/Baseline"))/(bFirstPersonCapture?TEXT("OwnerFP"):TEXT("ExternalTP")); // 候选证据独立保存，不能覆盖已冻结的修复前记录。
    if (!MotionIsolation.IsEmpty()) return FPaths::ProjectSavedDir()/TEXT("LyraResidualMotion")/MotionIsolation/(bCapture?TEXT("Rendered"):TEXT("Numeric"));
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderCandidate")) || FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderLegacy")) || FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderFormal")))
    {
        // 三种实现身份、起步渲染与数值轮各自隔离；不得覆盖旧Lyra迁移证据或新旧对照。
        const FString Variant=FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderLegacy"))?TEXT("Legacy"):FParse::Param(FCommandLine::Get(),TEXT("DemoLyraShoulderFormal"))?TEXT("Formal"):TEXT("Candidate");
        const FString Pass=bStartJitterAudit?(bCapture?(bShoulderShadowAudit?TEXT("StartShadowFP"):bFirstPersonCapture?TEXT("StartRenderedFP"):TEXT("StartRendered")):FParse::Param(FCommandLine::Get(),TEXT("DemoShoulderLowFPS"))?TEXT("Start30"):TEXT("Start60")):TEXT("Regression");
        return FPaths::ProjectSavedDir()/TEXT("LyraShoulderAcceptance")/Variant/Pass;
    }
    if (bStartJitterAudit) return FPaths::ProjectSavedDir()/TEXT("LyraStartJitterAudit"); // 独立问题证据，不覆盖先前验收。
    // 正式引用冒烟仅改变证据目录，不选择候选资产；不得覆盖已冻结的候选或V4报告。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoNativeAcceptance")))
    {
        return FPaths::ProjectSavedDir()/TEXT("LyraNativeFormalAcceptance");
    }
    return FParse::Param(FCommandLine::Get(),TEXT("DemoLyraNativeCandidate"))?FPaths::ProjectSavedDir()/TEXT("LyraNativeAcceptance"):FPaths::ProjectSavedDir();
}

void ADemoPlayerAnimationTest::SaveAndExit(bool bSuccess)
{
    DEMO_LOG_CALL();
    SetActorTickEnabled(false);
    const FString Directory=GetReviewRoot()/(bDirectionalAudit?TEXT("PlayerLocomotionV2Acceptance"):bPistolRunArmAudit?TEXT("PlayerPistolRunAcceptance"):bMovementViewAudit?TEXT("PlayerMovementViewAcceptance"):bYawAudit?TEXT("PlayerYawAcceptance"):bAimAudit?(FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerAimReview"))?TEXT("PlayerAimAcceptance/Review"):TEXT("PlayerAimAcceptance")):TEXT("PlayerAcceptance")); // 各专项原始姿态隔离，不能覆盖旧几何输入及哈希。
    IFileManager::Get().MakeDirectory(*Directory,true);
    if (Report)
    {
        Report->SetBoolField(TEXT("runtime_passed"),bSuccess);
        Report->SetBoolField(TEXT("visual_and_geometry_review_pending"),true); // 日志运行成功不能代替主Agent视觉/穿模验收。
        Report->SetArrayField(TEXT("frames"),Frames);
        FString Json; // 一次序列化，避免逐帧文件IO影响动画采样间隔。
        const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json); // 当前游戏线程写内存文本。
        if (!FJsonSerializer::Serialize(Report.ToSharedRef(),Writer) || !FFileHelper::SaveStringToFile(Json,*(Directory/((bAimAudit || bDirectionalAudit) && bFirstPersonCapture?TEXT("runtime_pose_samples_fp.json"):TEXT("runtime_pose_samples.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) bSuccess=false;
    }
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_ANIMATION_ACCEPTANCE_%s frames=%d"),bSuccess?TEXT("SUCCESS"):TEXT("FAILED"),Frames.Num());
    FPlatformMisc::RequestExitWithStatus(false,bSuccess?0:1);
}

void ADemoPlayerAnimationTest::RequestAimAuditPitch(float Pitch)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(Player->GetController()); // 本隔离World唯一真实控制器；不改摄像机或骨Pose。
    AimRequestedPitch=Pitch;
    PC->SetControlRotation(FRotator(Pitch,bYawAudit?AimRequestedYaw:bLyraAimAudit && Stage>=7?((Stage-7)/5%2?-30.f:30.f):0.f,0)); PC->UpdateRotation(0.f); // 水平专项走真实Controller；Lyra专项的30度处于身体阈值内但枪必须跟随。
    NextSample=GetWorld()->GetTimeSeconds()+.001f; NextCapture=FMath::Max(NextCapture,NextSample); // 测试在PostUpdate写输入，必须等下一次真实动画求值；不能将本帧旧Pose与刚写的新ControlRotation判成生产延迟。
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_AIM_TEST_REQUEST requested=%.1f actual=%.1f stage=%s"),Pitch,PC->GetControlRotation().Pitch,*StageName);
}

void ADemoPlayerAnimationTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    const float Now=GetWorld()->GetTimeSeconds(); // World秒，不用系统时间驱动技能。
    if (bFailed || Now<1.f) return;
    if (Now>150.f) { Check(false,TEXT("runtime acceptance timed out")); return; }
    if (bFireArmJitterAudit) { TickFireArmJitterAudit(DeltaSeconds); return; } // 优先于其它录制参数，严格走独立六例，不借旧15Hz动作断言冒充开火诊断。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakAudit"))) { TickIdleBreakAudit(Now); return; } // 自然随机待机复现；不套用32案或6秒视频计划。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusRecord"))) { TickMuzzleFocusRecording(Now); return; } // 只在显式Editor录制中使用预物理输入/末尾抓帧。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMuzzleFocusAudit"))) { TickMuzzleFocusAudit(Now); return; } // 专项只读真实骨姿和碰撞，不改正式瞄准输入缓存。
    if (bAimContractAudit) { TickAimContractAudit(Now); return; }
    if (bStartJitterAudit) { TickStartJitterAudit(Now); return; }
    if (bDirectionalAudit) { TickDirectionalAudit(Now,DeltaSeconds); return; }
    if (bPistolRunArmAudit) { TickPistolRunArmAudit(Now); return; } // 复用真实夹具和采样，独立覆盖跑动装填而不改变原四枪用例。
    if (Stage<0)
    {
        if (!InitializeFixture()) return;
        Stage=0; if (!BeginStage()) return;
    }
    if (!Player.IsValid() || !Weapon.IsValid()) { Check(false,TEXT("active pawn or weapon disappeared")); return; } // 高频有效路径保持Tick日志，避免每帧Display淹没关键证据。
    if (bYawAudit && Stage>=7 && (Stage-7)%5==0 && YawCorner<(bMovementViewAudit?10:3) && Now>StageStart+.8f*(YawCorner+1))
    {
        ++YawCorner;
        const int32 Corner=bMovementViewAudit?YawCorner-7:YawCorner; // 静止七格后才进入原四角，避免中间角度遗漏衣领穿插。
        AimRequestedYaw=bMovementViewAudit && YawCorner<7?-45.f+15.f*YawCorner:(Corner%2==0?-60.f:60.f);
        RequestAimAuditPitch(bMovementViewAudit && YawCorner<7?0.f:Corner<2?-120.f:120.f); NextSample=Now+.05f;
    } // PostUpdate输入切换后留下一真实动画帧，避免将上一Pose误记为本角；不跳过任何已求值的新姿态。
    if (bAimAudit && !bYawAudit && Stage>=7 && (Stage-7)%5==0 && !bAimRaised && Now>StageStart+1.f)
    { bAimRaised=true; RequestAimAuditPitch(120.f); } // Hold同一源姿势真实经过两端阈值，不能靠不同动画资源的枪口朝向制造通过。
    const FRotator InputFrame(0,bYawAudit?AimRequestedYaw:0.f,0); // 独立计划视线Yaw；走/跑/横移/后退按输入坐标系构造，不能继续固定世界X。
    if (Stage==1) Player->AddMovementInput(InputFrame.RotateVector(FVector::ForwardVector),.25f);
    if (Stage==2) Player->AddMovementInput(InputFrame.RotateVector(FVector::ForwardVector),1.f);
    if (Stage==5) Player->AddMovementInput(InputFrame.RotateVector(FVector::RightVector),.4f);
    if (Stage==6) Player->AddMovementInput(InputFrame.RotateVector(-FVector::ForwardVector),.4f);
    if (Stage==3 && Now>StageStart+.12f) Player->StopJumping();
    if (bLyraAimAudit && Stage>=7 && (Stage-7)%5==0)
    {
        if (!bLyraJumped && Now>StageStart+.2f) { bLyraJumped=true; Player->Jump(); }
        if (bLyraJumped && Now>StageStart+.32f) Player->StopJumping(); // 真实CMC起落，不能向图注入状态以假通过。
    }
    if (Stage>=7 && (Stage-7)%5==4 && !bCancelled && Now>=StageStart+.10f)
    { Player->GetWeaponComponent()->CancelActions(); bCancelled=true; }
    MaximumSpeed=FMath::Max(MaximumSpeed,static_cast<float>(Player->GetVelocity().Size2D()));
    bSawFalling|=Player->GetCharacterMovement()->IsFalling(); bSawDash|=Player->IsDashEvading();
    if (const UDemoPlayerAnimInstance* Animation=Cast<UDemoPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance())) // 只读本帧实际选择，不每帧输出Display。
        ObservedStates.Add(Animation->LocomotionState);
    const FVector Target=Player->GetActorLocation()+FVector(0,0,8); // 摄影机瞄准胸腹中段，60度视野给完整头脚和原HUD留空间。
    const FVector CameraPosition=Target+FVector(310,-330,115); // 前侧45度展示持枪/腿部，世界位置随玩家平移。
    Camera->SetActorLocationAndRotation(CameraPosition,(Target-CameraPosition).Rotation());
    if (Now+0.0001f>=NextSample) { RecordFrame(); NextSample=Now+1.f/((bMotionOnly || (bMovementViewAudit && Stage==19))?60.f:15.f); } // 散弹枪空仓恢复曾有单帧枪托/袖口接触，专项在此阶段逐帧采样；0.1ms仅容忍float时间量化，不补造未求值帧。
    if (bCapture && FApp::CanEverRender() && Now>=NextCapture && !FScreenshotRequest::IsScreenshotRequested())
    {
        const FString Image=GetReviewRoot()/(bMovementViewAudit?TEXT("PlayerMovementViewAcceptance"):bYawAudit?TEXT("PlayerYawAcceptance"):bAimAudit?(FParse::Param(FCommandLine::Get(),TEXT("DemoPlayerAimReview"))?TEXT("PlayerAimAcceptance/Review"):TEXT("PlayerAimAcceptance")):TEXT("PlayerAcceptance"))/(bFirstPersonCapture?TEXT("FramesFP"):TEXT("Frames"))/FString::Printf(TEXT("%02d_%s_%05d.png"),Stage,*StageName,ScreenshotIndex++); // 专项/最终复核/观察方向分开，动作名为内部常量。
        FScreenshotRequest::RequestScreenshot(Image,false,false);
        NextCapture=Now+(bMotionOnly?1.f/30.f:.125f)-.0001f; // 移动专项30Hz截图供连续观看，完整四枪8Hz；同样只取实际渲染帧。
    }
    if (Now>=StageStart+StageDuration)
    {
        if (!FinishStage()) return;
        ++Stage;
        if (Stage>=(bMotionOnly?7:27)) SaveAndExit(true);
        else BeginStage();
    }
}
