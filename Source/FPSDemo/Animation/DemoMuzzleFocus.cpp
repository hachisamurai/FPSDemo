// 职责：为既有作者瞄准层发布准星焦点与真实枪口局部偏移；UpdateAim在游戏线程调用。参考Lyra5.4相机焦点/武器朝向分工，依赖现有命中通道，不复制代码或引入插件。
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoPlayerAnimProxy.h"
#include "Animation/DemoPlayerAimSettings.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Characters/DemoCharacter.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/World.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Misc/CommandLine.h" // 仅Editor诊断构造读取显式开关，不在动画工作线程解析命令行。
#include "Misc/Parse.h"

FDemoPlayerAnimProxy::FDemoPlayerAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); // 非UObject代理不能使用依赖this->GetName的宏。
#if WITH_EDITOR
    IdleAudit.bFirePoseEnabled=FParse::Param(FCommandLine::Get(),TEXT("DemoFireArmJitterAudit")); // 独立Editor诊断，仅复制本帧完整骨数据。
    IdleAudit.bEnabled=IdleAudit.bFirePoseEnabled || FParse::Param(FCommandLine::Get(),TEXT("DemoIdleBreakAudit")); // 默认关闭，不改变姿态或随机待机状态。
#endif
}

void FDemoPlayerAnimProxy::CaptureMuzzleFocus(const UDemoPlayerAnimInstance& Source)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
#if WITH_EDITOR
    if (IdleAudit.bFirePoseEnabled)
    {
        // GT在派发本帧任务前清空。缺节点/NoAim必须留下空数组，不能把上一帧当成本帧证据。
        IdleAudit.FirePoseNames.Reset(); IdleAudit.FireBase.Reset(); IdleAudit.FireUpper.Reset();
        IdleAudit.FireBeforeAim.Reset(); IdleAudit.FireAfterAim.Reset();
    }
#endif
    FocusPoint=Source.MuzzleFocusPoint; MuzzleOffset=Source.MuzzleFocusOffset; MaxDegrees=Source.MuzzleFocusMaxDegrees;
    bPreserveGunRecoilArmDelta=Source.bUseAdditiveGunRecoil; // 同帧策略来自实例，不由节点PreUpdate采上一帧，也不从Montage权重反推开火。
    bFocusActive=Source.bMuzzleFocusActive; ProfileIdentity=Source.AuthoredAimProfile.Get();
    const ADemoCharacter* Pawn=Cast<ADemoCharacter>(Source.TryGetPawnOwner()); // 仅游戏线程借用；代理只保存下面的纯值。
    bEvaluatedEye=false; // 稳定与旧Head策略都必须发布完整焦点快照，不能关此标志便漏掉其它输入。
    if (bFocusActive && Pawn)
    {
        MeshToWorld=Source.GetSkelMeshComponent()->GetComponentTransform();
        ViewFrame=Pawn->CaptureFullBodyViewFrame();
        bEvaluatedEye=ViewFrame.bUseAnimatedHead; // 仅显式旧策略才在工作线程用当前最终head重新投影；稳定模式沿用冻结原点。
        ViewOrigin=MeshToWorld.InverseTransformPosition(Pawn->GetSharedViewLocation());
        ViewDirection=MeshToWorld.InverseTransformVectorNoScale(Pawn->GetBaseAimRotation().Vector()).GetSafeNormal();
        SurfacePoint=Source.MuzzleFocusSurfacePoint; SurfaceNormal=Source.MuzzleFocusSurfaceNormal;
        const ADemoWeaponBase* Weapon=Pawn->GetWeaponComponent()->GetActiveWeapon(); // bFocusActive已经验证，仍防御切枪失效。
        const float Range=Weapon?Weapon->Config.Range:10000.f; // 当前真实射程cm，和查询一致。
        DistanceLimits=FVector2D(FMath::Min(GetDefault<UDemoPlayerAimSettings>()->MuzzleFocusMinDistance,Range),Range);
    }
}

FAnimInstanceProxy* UDemoPlayerAnimInstance::CreateAnimInstanceProxy()
{
    DEMO_LOG_CALL();
    return new FDemoPlayerAnimProxy(this); // 引擎拥有生命周期，DestroyAnimInstanceProxy在任务结束后唯一释放。
}

void UDemoPlayerAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    DEMO_LOG_CALL();
    delete static_cast<FDemoPlayerAnimProxy*>(InProxy); // 只接收Create返回的本实例代理；无后台线程/额外资源。
}

#if WITH_EDITOR
FDemoIdleBreakPoseAudit UDemoPlayerAnimInstance::ReadIdleBreakAudit() const
{
    DEMO_LOG_TICK();
    return GetProxyOnGameThread<FDemoPlayerAnimProxy>().IdleAudit; // 原生protected接口由本类封装；完成任务后复制，测试不保留内部引用。
}
#endif

void UDemoPlayerAnimInstance::UpdateMuzzleFocus(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    bMuzzleFocusActive=false;
    const UDemoPlayerAimSettings* Settings=GetDefault<UDemoPlayerAimSettings>(); // 当前进程只读CDO，工作线程不查询配置对象。
    ADemoCharacter* Pawn=Character.Get(); // 实例所属Avatar，仅当前游戏线程回调借用。
    ADemoWeaponBase* Weapon=Pawn && Pawn->GetWeaponComponent()?Pawn->GetWeaponComponent()->GetActiveWeapon():nullptr; // 装备真值由既有复制组件提供。
    const UDemoPlayerPresentationComponent* Presentation=Pawn?Pawn->GetPlayerPresentation():nullptr; // 所有端同一全身展示武器，不取观战者CameraManager。
    USkeletalMeshComponent* Gun=Presentation?Presentation->GetThirdPersonWeaponMesh():nullptr; // World拥有；仅取组件局部机械Socket，禁止用上一帧世界枪口推导目标。
    if (!Settings->bEnableMuzzleFocus || !AuthoredAimProfile || !bAimWeaponActive || !Pawn || Pawn->IsCoopEliminated()
        || !Weapon || Weapon->GetWeaponId()!=UpperWeaponId || !Gun || Gun->GetSkeletalMeshAsset()!=Weapon->Config.Mesh || !Gun->DoesSocketExist(Weapon->Config.MuzzleSocket))
    {
        MuzzleFocusWeapon=NAME_None; MuzzleFocusDistance=0.f; // 切枪/近战/失效即失去旧焦点历史；原方向校准继续保底。
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MUZZLE_FOCUS_SKIPPED disabled/unavailable current weapon"));
        return;
    }
    const float Range=Weapon->Config.Range; // 当前枪真实射程cm，视觉射线与无散布的开火中心射线一致。
    if (!FMath::IsFinite(Range) || Range<=0.f) { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FOCUS_REJECT invalid weapon range")); return; }
    const FVector Origin=Pawn->GetSharedViewLocation(),AimRayDirection=Pawn->GetBaseAimRotation().Vector(); // 稳定眼位与本Pawn控制方向，服务器不依赖CalcCamera；避免遮蔽实例的移动Direction成员。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoMuzzleFocus),false,Pawn); // 与实体弹准备相同简单PhysicsAsset/场景通道；只读碰撞，不结算伤害。
    Query.AddIgnoredActor(Weapon);
    FHitResult Hit; // 查询结果只在本函数有效；不在动画任务保留Actor、Component或HitResult。
    GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+AimRayDirection*Range,DemoEnemyHitZones::TraceChannel,Query);
    MuzzleFocusSurfacePoint=GetSkelMeshComponent()->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint); // 无命中时此点不会使用。
    MuzzleFocusSurfaceNormal=Hit.bBlockingHit?GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal():FVector::ZeroVector;
    const float DesiredDistance=FMath::Clamp(static_cast<float>(Hit.bBlockingHit?Hit.Distance:Range),FMath::Min(Settings->MuzzleFocusMinDistance,Range),Range); // 极近遮挡不让枪反转；仍交正式开火扫掠挡墙。
    const float Follow=1.f-FMath::Exp(-Settings->MuzzleFocusDistanceFollowRate*FMath::Max(0.f,DeltaSeconds)); // 只平滑深度，鼠标改变方向不被额外低通。
    MuzzleFocusDistance=MuzzleFocusWeapon==UpperWeaponId?FMath::Lerp(MuzzleFocusDistance,DesiredDistance,Follow):DesiredDistance;
    MuzzleFocusWeapon=UpperWeaponId;
    MuzzleFocusPoint=GetSkelMeshComponent()->GetComponentTransform().InverseTransformPosition(Origin+AimRayDirection*MuzzleFocusDistance);
    MuzzleFocusOffset=Gun->GetSocketTransform(Weapon->Config.MuzzleSocket,RTS_Component).GetLocation(); // 机械网格原点就是最终ik_hand_gun，不含角色/相机平滑。
    MuzzleFocusMaxDegrees=Settings->MuzzleFocusMaxDegrees;
    bMuzzleFocusActive=!MuzzleFocusPoint.ContainsNaN() && !MuzzleFocusOffset.ContainsNaN();
    if (!bMuzzleFocusActive) UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FOCUS_REJECT non-finite local snapshot"));
}
