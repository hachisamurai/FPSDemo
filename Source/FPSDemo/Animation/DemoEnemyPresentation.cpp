// 职责：执行敌人动画、Overlay短闪与Facing前后的本地受击变换租约。使用：Enemy确认命中后提交表现；依赖ASC、独立MID和HitReact，不修改骨姿、AI、伤害或共享材质。
#include "Animation/DemoEnemyPresentation.h"
#include "AI/DemoEnemyHitReactComponent.h" // Facing前剥离旧偏移，Facing后叠加当前弹簧，避免互相覆盖或恢复陈旧朝向。
#include "Combat/Feedback/DemoHitFlash.h" // 设施和敌人共用脉冲，不让两种目标的反馈曲线逐渐分叉。
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/DemoEnemyAppearance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GAS/Abilities/DemoEnemyAnimationAbility.h"
#include "Debug/DemoLog.h"
#include "PhysicsEngine/PhysicsAsset.h" // 配置阶段检查实际受击资产，缺失时不退回宽松整球伤害。
#include "PhysicsEngine/BodySetup.h" // UE5.4的骨骼BodySetup定义随PhysicsAsset引入；共用实际凸体数据绘制调试线。
#include "Combat/DemoEnemyHitZones.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "AI/DemoEnemy.h" // 只读宿主冻结Boss身份；首批本体短闪不覆盖任何Boss预警。
#include "GAS/DemoTags.h" // 冰霜/冻结身份色优先于新命中Overlay。
#include "GAS/Abilities/DemoBossDiveAbility.h" // 只读统一Casting标签，不取消技能或调整其时钟。
#include "UObject/ConstructorHelpers.h" // 只在CDO构造加载候选硬引用，命中回调不读磁盘。

// 默认关闭，仅本机绘制，不复制/写存档；1显示当前姿势真实凸体边线与区域名。
static TAutoConsoleVariable<int32> CVarDemoHitZones(TEXT("Demo.Combat.DebugHitZones"),0,TEXT("Draw animated weapon query hulls by hit region (0/1)."));

UDemoEnemyPresentation::UDemoEnemyPresentation()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=true;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Overlay(TEXT("/Game/Materials/Feedback/M_Feel_HitOverlay.M_Feel_HitOverlay"), LOAD_Quiet | LOAD_NoWarn); // 作者脚本首次运行前允许缺失，普通小怪使用明确降级。
    ConfirmedOverlayMaterial = Overlay.Get();
}
bool UDemoEnemyPresentation::Configure(const FDemoEnemyBodyDefinition& Definition,USkeletalMeshComponent* Mesh,UAbilitySystemComponent* ASC)
{
    DEMO_LOG_CALL();
    FString Error; // 资源契约失败阻止出生激活，不留下不可命中的活怪。
    if (!GetOwner()||!Mesh||!ASC||!Definition.Validate(Error))
    { UE_LOG(LogFPSDemo,Error,TEXT("ENEMY_ANIM_CONFIG rejected %s"),*Error); return false; }
    StopConfirmedImpactFlash(); ConfirmedOverlayMID = nullptr; bConfirmedOverlayAttempted = false; // 合法重配置先归还旧网格Overlay；不会留下跨Avatar的MID/租约。
    Body=Mesh; Appearance=Definition.Appearance; HitProfile=Definition.HitProfile; AbilitySystem=ASC; bOwnerAlive=true;
    Body->SetSkeletalMesh(Appearance->Mesh);
    Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly); // 定义已验证真实PhysicsAsset，后续握手仍由Actor总碰撞开关屏蔽未激活对象。
    Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    Body->SetAnimInstanceClass(Appearance->AnimClass);
    Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Body->bEnableUpdateRateOptimizations=false; // 保留不可见目标真实骨骼命中，不使用过期参考姿势。
    Body->AddTickPrerequisiteActor(GetOwner()); AddTickPrerequisiteActor(GetOwner());
    ASC->RefreshAbilityActorInfo();
    if (GetOwner()->HasAuthority()&&!AnimationAbility.IsValid()) AnimationAbility=ASC->GiveAbility(FGameplayAbilitySpec(UDemoEnemyAnimationAbility::StaticClass(),1));
    UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_ANIM_READY mesh=%s anim=%s"),*GetNameSafe(Appearance->Mesh),*GetNameSafe(Body->GetAnimInstance()));
    return Body->GetAnimInstance()!=nullptr;
}
void UDemoEnemyPresentation::SetFacingIntent(const FVector& Direction)
{
    DEMO_LOG_TICK(); FacingIntent=Direction.ContainsNaN()?FVector::ZeroVector:Direction.GetSafeNormal2D();
}
void UDemoEnemyPresentation::SetAlive(bool bAlive)
{
    DEMO_LOG_CALL(); bOwnerAlive=bAlive; if (!bAlive) FacingIntent=FVector::ZeroVector;
}
bool UDemoEnemyPresentation::Play(FName Action,float Seconds)
{
    DEMO_LOG_CALL();
    UAbilitySystemComponent* ASC=AbilitySystem.Get(); // 注入ASC仅在同步调用内借用，生命周期由宿主控制。
    if (!GetOwner()||!GetOwner()->HasAuthority()||!ASC||(!bOwnerAlive&&Action!="Death")||!Appearance||!Body||!Body->GetAnimInstance()
        ||!AnimationAbility.IsValid()||!FMath::IsFinite(Seconds)||Seconds<0)
    { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_ANIM_REJECT action=%s dependency/duration"),*Action.ToString()); return false; }
    const TObjectPtr<UAnimMontage>* Found=Appearance->Actions.Find(Action); // 资产映射不热变更，缺失不影响权威伤害。
    if (!Found||!*Found) { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_ANIM_MISSING %s"),*Action.ToString()); return false; }
    const int32 Wanted=Action=="Death"?100:Action=="Hit"?10:Action=="Rage"?20:(Action=="Melee"||Action=="Fire")?40:60; // 优先级不改变玩法互斥。
    // 连射命中不重复从头重启同一个受击Montage；保留完整动作，也不能抢断高优先级Boss施法。
    if (Body->GetAnimInstance()->IsAnyMontagePlaying()&&(Wanted<Priority||(Action=="Hit"&&Priority==10)))
    {
        if (Action=="Rage") bRageQueued=true;
        UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_ANIM_DEFER %s priority=%d current=%d"),*Action.ToString(),Wanted,Priority); return false;
    }
    ASC->CancelAbilityHandle(AnimationAbility);
    RequestedMontage=*Found; RequestedRate=Seconds>0?RequestedMontage->GetPlayLength()/Seconds:1.f; Priority=Wanted;
    if (Action=="Death") bRageQueued=false;
    const bool bStarted=ASC->TryActivateAbility(AnimationAbility); // 同步激活后Task自行持有动画。
    UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_ANIM_PLAY %s rate=%.3f started=%d"),*Action.ToString(),RequestedRate,bStarted);
    return bStarted;
}
void UDemoEnemyPresentation::Cancel()
{
    DEMO_LOG_CALL();
    if (UAbilitySystemComponent* ASC=AbilitySystem.Get()) // EndPlay期间仅借用尚有效的注入ASC。
        if (AnimationAbility.IsValid()) ASC->CancelAbilityHandle(AnimationAbility);
    Priority=0; bRageQueued=false;
}
void UDemoEnemyPresentation::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    UDemoEnemyHitReactComponent* HitReact=GetOwner()?GetOwner()->FindComponentByClass<UDemoEnemyHitReactComponent>():nullptr; // 同Actor本帧借用，不保存跨World对象。
    if(HitReact) HitReact->RemoveAppliedOffset(); // Facing必须读干净基准，不能把上帧倾斜再次插值/积累。
    UpdateHitFlash(DeltaTime); // 必须在死亡提前返回前淡出，最后一击也能完成短闪。
    UpdateConfirmedImpactFlash(DeltaTime); // 同一既有Tick推进，不新增Timer或组件；动态材质仅统一参数，不复制逐帧值。
    DrawHitZones(); // 守卫内部只在控制台显式开启时读取凸体数据。
    if (!bOwnerAlive||!Body||!Body->GetAnimInstance()) return; // 死亡已卸载旧偏移，不再叠加存活弹簧。
    if (bRageQueued&&!Body->GetAnimInstance()->IsAnyMontagePlaying()) { bRageQueued=false; Play(TEXT("Rage")); }
    if (!FacingIntent.IsNearlyZero())
    {
        const FRotator Facing(0,FacingIntent.Rotation().Yaw,0); // 仅插值视觉朝向，不能改根碰撞或技能锁向。
        Body->SetWorldRotation(FMath::RInterpTo(Body->GetComponentRotation(),Facing,DeltaTime,8.f));
    }
    if(HitReact) HitReact->ApplyOffset(); // 当前Facing之上叠加，独立组件Tick已先推进本帧解析弹簧。
}
void UDemoEnemyPresentation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); StopConfirmedImpactFlash(); ConfirmedOverlayMID = nullptr; UpdateHitFlash(FlashRemaining); FlashMaterial.Reset(); Cancel(); Super::EndPlay(EndPlayReason);
}

void UDemoEnemyPresentation::FlashHit(bool bCore, bool bFatal)
{
    DEMO_LOG_CALL();
    if (bOwnsConfirmedOverlay) StopConfirmedImpactFlash(); // 切回旧A/B模式先释放新Overlay，避免灯带与新脉冲继续叠两份。
    if (GetNetMode() == NM_DedicatedServer || !Body || !FMath::IsFinite(HitFlashSeconds) || HitFlashSeconds <= 0.f)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("ENEMY_HIT_FLASH skipped disabled/server/mesh")); return; }
    const int32 Slot = Body->GetMaterialIndex(HitFlashSlot); // 可配置稳定槽名，不依赖导出材质顺序。
    if (Slot == INDEX_NONE) { UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_HIT_FLASH skipped missing slot=%s"), *HitFlashSlot.ToString()); return; }
    UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Slot)); // 网格自己的实例；禁止修改共享材质CDO。
    if (!Material) Material = Body->CreateDynamicMaterialInstance(Slot);
    FLinearColor CurrentTint; // 只接受模型真正提供的参数，缺参数不伪装为白色初值。
    if (!Material || !Material->GetVectorParameterValue(FMaterialParameterInfo(HitFlashParameter), CurrentTint))
    { UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_HIT_FLASH skipped missing tint parameter")); return; }
    if (FlashMaterial.Get() != Material || FlashRemaining <= 0.f || !CurrentTint.Equals(FlashAppliedTint)) FlashBaseTint = CurrentTint;
    FlashMaterial = Material;
    FlashDuration = FMath::Clamp(HitFlashSeconds, .01f, .4f); FlashRemaining = FlashDuration;
    FlashPeakTint = DemoHitFlash::PeakTint(bCore, bFatal); // 共用HDR短脉冲，静态角色颜色在结束后精确恢复。
    FlashAppliedTint = CurrentTint; // Update先识别其他系统的改色；初始值是刚读取的材质状态。
    UpdateHitFlash(0.f);
    UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_HIT_FLASH core=%d fatal=%d seconds=%.2f"), bCore, bFatal, FlashDuration);
}

void UDemoEnemyPresentation::UpdateHitFlash(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (FlashRemaining <= 0.f || !Body || GetNetMode() == NM_DedicatedServer) return;
    const int32 Slot = Body->GetMaterialIndex(HitFlashSlot); // 复制角色色可能替换MID，不能继续操作脱离网格的旧实例。
    UMaterialInstanceDynamic* Material = Slot != INDEX_NONE ? Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Slot)) : nullptr; // 网格拥有，本帧借用。
    if (!Material) { FlashRemaining = 0.f; FlashMaterial.Reset(); UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_HIT_FLASH cancelled material replacement")); return; }
    const FLinearColor CurrentTint = Material->K2_GetVectorParameterValue(HitFlashParameter); // 外部狂暴/身份复制改色成为新的恢复基线。
    if (FlashMaterial.Get() != Material || !CurrentTint.Equals(FlashAppliedTint)) FlashBaseTint = CurrentTint;
    FlashMaterial = Material;
    FlashRemaining = FMath::Max(0.f, FlashRemaining - FMath::Max(0.f, DeltaSeconds));
    const float Strength = DemoHitFlash::Strength(FlashRemaining, FlashDuration); // 共用二次曲线衰减，不增加新的组件Tick或定时器。
    FlashAppliedTint = FMath::Lerp(FlashBaseTint, FlashPeakTint, Strength);
    Material->SetVectorParameterValue(HitFlashParameter, FlashAppliedTint);
}

bool UDemoEnemyPresentation::IsConfirmedImpactProtected() const
{
    DEMO_LOG_TICK();
    const ADemoEnemy* Enemy = Cast<ADemoEnemy>(GetOwner()); // 仅同步借用宿主公开身份，不查询玩家、目标或AI攻击组件。
    const UAbilitySystemComponent* ASC = AbilitySystem.Get(); // Configure注入的宿主弱引用，不延长Actor生命周期。
    return (Enemy && Enemy->IsBoss()) || (ASC && (ASC->HasMatchingGameplayTag(DemoTags::Frozen)
        || ASC->HasMatchingGameplayTag(DemoTags::Chill) || ASC->HasMatchingGameplayTag(DemoBossTags::Casting)));
}

bool UDemoEnemyPresentation::EnsureConfirmedOverlayMaterial()
{
    DEMO_LOG_CALL();
    if (ConfirmedOverlayMID) return true;
    if (bConfirmedOverlayAttempted) return false;
    bConfirmedOverlayAttempted = true;
    FLinearColor Color; float Opacity = 0.f; // 只检查真实声明参数，不把不存在的材质接口当作白色/0强度。
    if (!ConfirmedOverlayMaterial || ConfirmedOverlayMaterial->GetBlendMode() != BLEND_Translucent
        || !ConfirmedOverlayMaterial->GetShadingModels().HasShadingModel(MSM_Unlit)
        || !ConfirmedOverlayMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("FeelColor")), Color)
        || !ConfirmedOverlayMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("FeelOpacity")), Opacity)
        || !FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B) || !FMath::IsFinite(Color.A)
        || !FMath::IsFinite(Opacity) || Opacity < 0.f || Opacity > 1.f)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("ENEMY_CONFIRMED_FLASH degraded: missing candidate overlay/parameters; run Tools/Unreal/create_combat_feel_overlay.py and reload Editor"));
        return false;
    }
    ConfirmedOverlayMID = UMaterialInstanceDynamic::Create(ConfirmedOverlayMaterial, this); // 一个组件拥有一个MID，原Armor/Steel/EnemyTint及共享材质图完全不变。
    if (!ConfirmedOverlayMID) { UE_LOG(LogFPSDemo, Warning, TEXT("ENEMY_CONFIRMED_FLASH degraded: MID allocation failed")); return false; }
    ConfirmedOverlayMID->SetScalarParameterValue(TEXT("FeelOpacity"), 0.f); // 未持有Overlay前保持不可见，首帧参数先于SetOverlayMaterial写入。
    return true;
}

void UDemoEnemyPresentation::FlashConfirmedImpact(bool bCore, bool bFatal)
{
    DEMO_LOG_CALL();
    if (GetNetMode() == NM_DedicatedServer || !Body)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("ENEMY_CONFIRMED_FLASH skipped server/mesh")); return; }
    if (IsConfirmedImpactProtected())
    { StopConfirmedImpactFlash(); UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_CONFIRMED_FLASH skipped protected Boss/element/cast state")); return; }
    UMaterialInterface* Current = Body->GetOverlayMaterial(); // UE5.4原生Overlay独立于材质槽；已有默认Overlay同样视为其他系统所有。
    if (Current && (!bOwnsConfirmedOverlay || Current != ConfirmedOverlayMID))
    { StopConfirmedImpactFlash(); UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_CONFIRMED_FLASH skipped external overlay")); return; }
    if (!EnsureConfirmedOverlayMaterial()) { FlashHit(bCore, bFatal); return; } // 首批缺作者材质时明确保留旧灯带，不能伪称本体短闪已运行。
    const int32 IncomingPriority = bFatal ? 2 : bCore ? 1 : 0; // 同窗口高等级优先，散弹后到弱点/致死只升级视觉。
    if (ConfirmedFlashRemaining > 0.f && IncomingPriority < ConfirmedFlashPriority)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("ENEMY_CONFIRMED_FLASH suppressed lower priority")); return; }
    const float Seconds = bFatal ? ConfirmedFatalSeconds : bCore ? ConfirmedCoreSeconds : ConfirmedHitSeconds; // 秒，资产/蓝图可分别调节，不改变玩法时钟。
    const float Opacity = bFatal ? ConfirmedFatalOpacity : bCore ? ConfirmedCoreOpacity : ConfirmedHitOpacity; // 线性透明度，不作为本体材质永久参数。
    if (!FMath::IsFinite(Seconds) || !FMath::IsFinite(Opacity) || Seconds <= 0.f || Opacity <= 0.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_CONFIRMED_FLASH skipped disabled/nonfinite profile")); return; }
    if (!bOwnsConfirmedOverlay) PreviousOverlayMaterial = Body->OverlayMaterial; // 保存原始覆盖值（不是Get默认回退），归还时精确恢复；此分支原Overlay必须为空。
    if (FlashRemaining > 0.f) { UpdateHitFlash(FlashRemaining); FlashMaterial.Reset(); } // 升级到新A/B模式时完成旧灯带尾，不叠加两种方案。
    ConfirmedFlashDuration = FMath::Clamp(Seconds, .01f, .3f); ConfirmedFlashRemaining = ConfirmedFlashDuration;
    ConfirmedFlashPeakOpacity = FMath::Clamp(Opacity, 0.f, .3f); ConfirmedFlashPriority = IncomingPriority;
    const FLinearColor Tint = bFatal ? FLinearColor(1.f, .30f, .18f) : bCore ? FLinearColor(1.f, .73f, .18f) : FLinearColor(.85f, .94f, 1.f); // 克制色调，不染永久BaseColor或Boss预警。
    ConfirmedOverlayMID->SetVectorParameterValue(TEXT("FeelColor"), Tint);
    ConfirmedOverlayMID->SetScalarParameterValue(TEXT("FeelOpacity"), ConfirmedFlashPeakOpacity);
    Body->SetOverlayMaterial(ConfirmedOverlayMID); bOwnsConfirmedOverlay = true; // 参数写入后才占用；重复同MID不会重复创建渲染材质。
    UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_CONFIRMED_FLASH priority=%d seconds=%.3f opacity=%.3f"), IncomingPriority, ConfirmedFlashDuration, ConfirmedFlashPeakOpacity);
}

void UDemoEnemyPresentation::UpdateConfirmedImpactFlash(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (!bOwnsConfirmedOverlay || ConfirmedFlashRemaining <= 0.f) return;
    if (!Body || !ConfirmedOverlayMID || Body->GetOverlayMaterial() != ConfirmedOverlayMID || IsConfirmedImpactProtected())
    { StopConfirmedImpactFlash(); UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_CONFIRMED_FLASH released changed overlay/protected state")); return; }
    if (!FMath::IsFinite(DeltaSeconds)) { UE_LOG(LogFPSDemo, Warning, TEXT("ENEMY_CONFIRMED_FLASH ignored nonfinite delta")); return; }
    ConfirmedFlashRemaining = FMath::Max(0.f, ConfirmedFlashRemaining - FMath::Max(0.f, DeltaSeconds));
    if (ConfirmedFlashRemaining <= 0.f) { StopConfirmedImpactFlash(); return; }
    const float Strength = DemoHitFlash::Strength(ConfirmedFlashRemaining, ConfirmedFlashDuration); // 复用原二次衰减；小透明度保留底层装甲纹理和元素读性。
    ConfirmedOverlayMID->SetScalarParameterValue(TEXT("FeelOpacity"), ConfirmedFlashPeakOpacity * Strength);
}

void UDemoEnemyPresentation::StopConfirmedImpactFlash()
{
    DEMO_LOG_CALL();
    if (ConfirmedOverlayMID) ConfirmedOverlayMID->SetScalarParameterValue(TEXT("FeelOpacity"), 0.f); // 只清本组件拥有的MID，外部材质参数不读写。
    if (bOwnsConfirmedOverlay && Body && Body->GetOverlayMaterial() == ConfirmedOverlayMID)
        Body->SetOverlayMaterial(PreviousOverlayMaterial); // 外部抢占之后不把其Overlay重置为空；上一原始覆盖值由UPROPERTY保活。
    PreviousOverlayMaterial = nullptr; bOwnsConfirmedOverlay = false; ConfirmedFlashRemaining = 0.f; ConfirmedFlashPeakOpacity = 0.f; ConfirmedFlashPriority = 0;
}
UAnimMontage* UDemoEnemyPresentation::GetRequestedMontage() const { DEMO_LOG_TICK(); return RequestedMontage; }
float UDemoEnemyPresentation::GetRequestedRate() const { DEMO_LOG_TICK(); return RequestedRate; }
USkeletalMeshComponent* UDemoEnemyPresentation::GetMesh() const { DEMO_LOG_TICK(); return Body; }

void UDemoEnemyPresentation::DrawHitZones() const
{
    DEMO_LOG_TICK();
    if (CVarDemoHitZones.GetValueOnGameThread()==0||!Body||!Body->GetPhysicsAsset()) return;
    const UDemoEnemyHitProfile* Rules=DemoEnemyHitZones::SelectValidProfile(HitProfile); // 调试开启时与碰撞/伤害共用回退，不显示无效配置的倍率。
    if (!Rules) return;
    for (const USkeletalBodySetup* Setup:Body->GetPhysicsAsset()->SkeletalBodySetups) // 资产只读，绘制随动画求值后的骨变换。
    {
        if (!Setup) continue;
        EDemoEnemyHitRegion Region; float Multiplier=0.f; // 当前区域与倍率，用于诊断显示而非再次结算。
        if (!Rules->ResolveBone(Setup->BoneName,Region,Multiplier)) continue;
        const FColor Color=Region==EDemoEnemyHitRegion::Core?FColor::Red:Region==EDemoEnemyHitRegion::Arm?FColor::Cyan:FColor::Green; // 三类稳定调试色。
        const FTransform Bone=Body->GetSocketTransform(Setup->BoneName,RTS_World); // 当前动画姿势，不使用参考姿势画静止碰撞。
        for (const FKConvexElem& Hull:Setup->AggGeom.ConvexElems) // 零件可拆成多个凸片；环与凹护板的真实开口保持空隙。
        {
            const FTransform Transform=Hull.GetTransform()*Bone; // 凸体局部→骨→世界，厘米制。
            for (int32 Index=0;Index+2<Hull.IndexData.Num();Index+=3) // 复用烹饪后的凸体三角边索引，不猜测拓扑。
                for (int32 Edge=0;Edge<3;++Edge)
                {
                    const int32 A=Hull.IndexData[Index+Edge],B=Hull.IndexData[Index+(Edge+1)%3]; // 索引来自资产，调试也防御不完整数据。
                    if (Hull.VertexData.IsValidIndex(A)&&Hull.VertexData.IsValidIndex(B))
                        DrawDebugLine(GetWorld(),Transform.TransformPosition(Hull.VertexData[A]),Transform.TransformPosition(Hull.VertexData[B]),Color,false,0.f,0,.7f);
                }
        }
        DrawDebugString(GetWorld(),Bone.GetLocation(),FString::Printf(TEXT("%s %s x%.2f"),*Setup->BoneName.ToString(),DemoEnemyHitZones::RegionName(Region),Multiplier),nullptr,Color,0.f,true,.7f);
    }
}
