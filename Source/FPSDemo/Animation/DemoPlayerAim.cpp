// 职责：将视角转换为全身图的安全瞄准缓存，装填退出沿源混合的同一时钟恢复。使用：PlayerAnimInstance每帧调用，枪械与近战采用各自配置，工作线程不读Pawn。
#include "Animation/DemoPlayerAnimInstance.h"
#include "Weapons/Melee/DemoMeleeComponent.h" // 近战使用同一有界上身俯仰，避免套用长枪托避让。
#include "Animation/DemoPlayerAimSettings.h"
#include "Animation/DemoPlayerAppearance.h" // 每枪的作者骨盆和共同上胸瞄准策略。
#include "Animation/DemoPlayerEyesComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"

namespace
{
    /** Action/Phase是当前或冻结的真实TP动作值；与作者单手装填端点契约相同，禁止持枪时抬起自然左臂。 */
    float PistolAimSupport(FName Action,float Phase)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        if (Action==TEXT("Reload")) return FMath::SmoothStep(0.f,.10f,Phase)*(1.f-FMath::SmoothStep(.72f,1.f,Phase));
        if (Action==TEXT("EmptyReload")) return FMath::SmoothStep(0.f,.08f,Phase)*(1.f-FMath::SmoothStep(.84f,1.f,Phase));
        return 0.f;
    }
}
void UDemoPlayerAnimInstance::ResetReloadAimRecovery(const TCHAR* Reason)
{
    DEMO_LOG_CALL();
    if(ReloadAimReleaseRemaining>0.f || !ReloadAimReleaseWeapon.IsNone())
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_AIM_RELEASE_RESET weapon=%s remaining=%.6f reason=%s"),*ReloadAimReleaseWeapon.ToString(),ReloadAimReleaseRemaining,Reason);
    ReloadAimReleaseRemaining=ReloadAimReleaseDuration=0.f;ReloadAimReleaseWeapon=NAME_None;bReloadAimReleaseInterrupted=false;
}

float UDemoPlayerAnimInstance::UpdateReloadAimInfluence(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if(ReloadAimReleaseWeapon!=UpperWeaponId)
    { ResetReloadAimRecovery(TEXT("weapon changed"));ReloadAimReleaseWeapon=UpperWeaponId; }
    const bool bCurrentReload=UpperBodyAction==TEXT("Reload") || UpperBodyAction==TEXT("EmptyReload"); // 真实源操作仍拥有原上身混合时钟。
    const bool bPreviousReload=PreviousUpperBodyAction==TEXT("Reload") || PreviousUpperBodyAction==TEXT("EmptyReload");
    const float SourceInfluence=FMath::Lerp(bPreviousReload?1.f:0.f,bCurrentReload?1.f:0.f,UpperBodyBlendAlpha); // 与原身体/机械混合同值，不能从墙钟重建phase。
    if(bCurrentReload)
    { ReloadAimReleaseRemaining=ReloadAimReleaseDuration=0.f;bReloadAimReleaseInterrupted=false;return SourceInfluence; } // 新装填接管；取消旧释放，沿真实新混合而非排队延迟。
    if(bPreviousReload && UpperBodyBlendAlpha<.999f)
    {
        if (ReloadAimReleaseDuration<=0.f)
            UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_AIM_RELEASE_BEGIN weapon=%s previous=%s next=%s duration=%.6f blend=%.6f"),*UpperWeaponId.ToString(),*PreviousUpperBodyAction.ToString(),*UpperBodyAction.ToString(),UpperBlendDuration,UpperBodyBlendAlpha); // 只在原退出边沿记录，正常Hold/同帧Fire/Bolt共用同一归属。
        ReloadAimReleaseDuration=UpperBlendDuration;
        ReloadAimReleaseRemaining=FMath::Max(0.f,UpperBlendDuration-UpperBlendSeconds);
        bReloadAimReleaseInterrupted=false;return SourceInfluence; // 保存已经推进到本帧的原退出剩余时间，下一帧不得重复推进本帧。
    }
    if(ReloadAimReleaseRemaining>0.f && ReloadAimReleaseDuration>0.f)
    {
        if(!bPreviousReload && !bReloadAimReleaseInterrupted && ReloadAimReleaseRemaining>FMath::Max(0.f,DeltaSeconds)+KINDA_SMALL_NUMBER)
        {
            bReloadAimReleaseInterrupted=true;
            UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_AIM_RELEASE_PRESERVED weapon=%s next=%s previous=%s remaining=%.6f duration=%.6f"),*UpperWeaponId.ToString(),*UpperBodyAction.ToString(),*PreviousUpperBodyAction.ToString(),ReloadAimReleaseRemaining,ReloadAimReleaseDuration);
        }
        ReloadAimReleaseRemaining=FMath::Max(0.f,ReloadAimReleaseRemaining-FMath::Max(0.f,DeltaSeconds));
        const float Progress=1.f-ReloadAimReleaseRemaining/ReloadAimReleaseDuration; // 原截止时间不变；Bolt/Fire的phase和机械已在各自正式流程立即推进。
        if(ReloadAimReleaseRemaining<=0.f)
        { UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_AIM_RELEASE_FINISHED weapon=%s action=%s"),*UpperWeaponId.ToString(),*UpperBodyAction.ToString());ReloadAimReleaseDuration=0.f; }
        return 1.f-FMath::SmoothStep(0.f,1.f,Progress);
    }
    return SourceInfluence;
}

void UDemoPlayerAnimInstance::UpdateAim(float DeltaSeconds,const UDemoPlayerAppearance* Appearance,bool bPistol)
{
    DEMO_LOG_TICK();
    bCalibratedBoreAim=false; AimAlignmentWeight=0.f; AimBoreInChest=FVector::ZeroVector; bMuzzleFocusActive=false; // 每帧清理，切近战/死亡或早退不继承上一把枪标定/焦点。
    const FDemoPlayerWeaponPoses* GunPoses=Appearance?Appearance->WeaponPoses.Find(UpperWeaponId):nullptr; // 当前只读配置，仅游戏线程借用。
    AuthoredAimProfile=GunPoses?GunPoses->AuthoredAim.Get():nullptr; // 实例强引用供节点发布，空值继续旧资产。
    AimReferencePelvis=GunPoses && GunPoses->bOverrideReferencePelvis?GunPoses->ReferencePelvis:Appearance?Appearance->ReferencePelvis:FTransform::Identity;
    bAimWeaponActive=Character.IsValid() && !Character->IsCoopEliminated() && Appearance && UpperBodyWeight>0.f;
    // 水平转头不依赖武器，也不在换弹时归零；阵亡/缺资源才停用全部跟随。
    if (!Character.IsValid() || Character->IsCoopEliminated() || !Appearance) { if(ReloadAimReleaseRemaining>0.f || !ReloadAimReleaseWeapon.IsNone())ResetReloadAimRecovery(TEXT("Avatar eliminated or Appearance missing")); AuthoredAimProfile=nullptr; AimPitch=0.f; HeadYaw=0.f; ShotgunAimRecovery=1.f; bAimInitialized=false; return; } // 换Avatar/阵亡清一次，避免死亡待机每帧刷Reset调用日志。
    const UDemoPlayerAimSettings* Settings=GetDefault<UDemoPlayerAimSettings>(); // 配置CDO只在游戏线程借用，不从AnyThread骨控制读取。
    const float ViewPitch=FMath::Clamp(static_cast<float>(UDemoPlayerEyesComponent::ReadAimDirection(Character.Get()).Rotation().Pitch),-Settings->LookDownDegrees,Settings->LookUpDegrees); // 同眼球使用Pawn自身控制方向，观战Camera不能污染目标瞄准。
    const float ReloadWeight=UpdateReloadAimInfluence(DeltaSeconds); // 源操作及其原释放时钟共同拥有影响；新动作不能仅通过覆盖Previous标签提前放开Aim。
    const bool bShotgunRecovery=UpperWeaponId==TEXT("shotgun"); // 长枪托在混合握姿中直接抬至中高角度会扫过领口；只对已复现的散弹枪串行恢复表现。
    const float PreviousRecovery=ShotgunAimRecovery; // 仅用于开始/结束日志边沿，逐帧权重仍保留入口Tick日志。
    ShotgunAimRecovery=bShotgunRecovery?(ReloadWeight>0.f?0.f:FMath::Min(1.f,ShotgunAimRecovery+FMath::Max(0.f,DeltaSeconds)/.12f)):1.f; // 先等手与机械动作混合完成，再抬枪；切枪清理，不延长实际Reload或弹药结算。
    if (bShotgunRecovery && ((PreviousRecovery>=1.f && ShotgunAimRecovery==0.f) || (PreviousRecovery<1.f && ShotgunAimRecovery>=1.f)))
        UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_SHOTGUN_AIM_RECOVERY pawn=%s weight=%.3f reloadWeight=%.3f"),*Character->GetName(),ShotgunAimRecovery,ReloadWeight);
    const float NeutralPitch=GunPoses && GunPoses->bRigidChestAim?FMath::Clamp(GunPoses->NeutralAimPitch,-20.f,20.f):0.f; // 独立作者上身与现有移动骨盆的角度差；仅整体瞄准校准，取弹/装匣仍回原源姿态。
    const float ReloadAlignment=FMath::Pow(FMath::Clamp(1.f-ReloadWeight,0.f,1.f),Settings->ReloadAimRestoreExponent); // 默认1复用已有SmoothStep；旧Pow4把30Hz恢复的约62%整枪转向压在一帧，身体/机械却已混入约93%。
    const float AimRestore=bShotgunRecovery?FMath::SmoothStep(0.f,1.f,ShotgunAimRecovery):ReloadAlignment; // 同一权重同时驱动旧俯仰和作者Aim；霰弹保留已证实需要的枪托避让串行流程。
    const float Target=(ViewPitch-NeutralPitch)*AimRestore; // 只改恢复形状，不新增低通/时长或改变相机、输入、GAS与机械phase。
    const float Follow=Character->IsLocallyControlled() || !bAimInitialized?1.f:1.f-FMath::Exp(-Settings->RemoteFollowRate*FMath::Max(0.f,DeltaSeconds)); // 本机无输入延迟，远端只平滑角度量化。
    const UDemoMeleeComponent* ViewMelee=Character->FindComponentByClass<UDemoMeleeComponent>(); // 仅当前Pawn装备状态，不能让观察机位置决定其近战胸腰转向。
    const FRotator View=ViewMelee && ViewMelee->IsEquipped()?UDemoPlayerEyesComponent::ReadAimDirection(Character.Get()).Rotation():Character->GetBaseAimRotation(); // 近战同Pitch使用既有输入方向；枪械保原消费路径。
    const float RelativeYaw=FMath::Clamp(FMath::FindDeltaAngleDegrees(static_cast<float>(Character->GetActorRotation().Yaw),static_cast<float>(View.Yaw)),-Settings->HeadYawLimit,Settings->HeadYawLimit); // 跨±180走最短角；复制先后到达也不能让头超安全范围。
    HeadYaw=FMath::Lerp(HeadYaw,RelativeYaw,Follow);
    AimUpAxis=GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
    AimPitch=FMath::Lerp(AimPitch,bAimWeaponActive?Target:0.f,Follow); bAimInitialized=true; // 无武器仍可转头，但不倾斜躯干或解握枪姿态。
    AimRightAxis=GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(Character->GetActorRightVector()).GetSafeNormal();
    AimTorsoFraction=bShotgunRecovery && AimPitch>0.f?1.f:Settings->TorsoFraction; // 散弹枪抬头时整块上胸与枪共同转动；低头沿原肩臂解算，避免整块胸部前折让左小臂进入下胸。0度两种映射均为恒等，切换无位置跳变。
    AimGripOffset=bPistol?GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(Character->GetActorForwardVector())
        *Settings->PistolLowAimOffset*FMath::SmoothStep(15.f,FMath::Max(16.f,Settings->LookDownDegrees),-AimPitch):FVector::ZeroVector; // 低头时枪柄离衣身少量前移，腕随枪、肩固定，枪口角度不变。
    if (!bPistol && !bShotgunRecovery) // 散弹枪共同刚体框架不再叠加独立握枪位移，否则会再次破坏肩袖/枪托间隙。
    {
        const float LowAimWeight=FMath::SmoothStep(0.f,55.f,AimPitch); // 通用高举位移仍沿0..55度渐进，保留三枪已验握姿及原臂长可达域。
        const float StockClearance=UpperWeaponId==TEXT("sniper")?FMath::SmoothStep(10.f,20.f,AimPitch):0.f; // 只保留原狙击枪长枪托的中段避让，步枪无需该曲线。
        const float ForwardClearance=StockClearance*(1.f-FMath::SmoothStep(20.f,28.f,AimPitch)); // 前伸先回收，内收稍后退出，避免约24度时左腕在完全伸直边界外。
        const float CenterClearance=StockClearance*(1.f-FMath::SmoothStep(20.f,30.f,AimPitch)); // 保留内收余量至30度，两条曲线端点斜率为零，连续回到通用握姿。
        const float ForwardOffset=40.f*FMath::Lerp(LowAimWeight,FMath::SmoothStep(0.f,30.f,AimPitch),ForwardClearance)+.5f*LowAimWeight; // cm；额外最多0.5cm避开射击恢复时胸前衣缝；双腕随枪重新解算，不拉长手臂。
        const float CenterOffset=20.f*FMath::Lerp(LowAimWeight,FMath::SmoothStep(0.f,20.f,AimPitch),CenterClearance); // cm；中段最多内收20cm，避免简单叠加超过握枪范围而让枪体碰到右小臂；双腕与枪共享位移。
        AimGripOffset=GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(
            Character->GetActorForwardVector()*ForwardOffset-Character->GetActorRightVector()*CenterOffset); // 枪与双腕共享同一组件空间位移，不改枪口角度或单独移动手指。
    }
    AimSupportWeight=bPistol?FMath::Lerp(PistolAimSupport(PreviousUpperBodyAction,PreviousActionProgress),PistolAimSupport(UpperBodyAction,ActionProgress),UpperBodyBlendAlpha):1.f;
    if (GunPoses && GunPoses->bRigidChestAim)
    {
        // 新枪两手与机械已经配对烘焙；只允许整体瞄准，旧模型的40cm位移或单手策略会破坏这些接触。
        AimTorsoFraction=1.f; AimGripOffset=FVector::ZeroVector; AimSupportWeight=1.f;
        bCalibratedBoreAim=!GunPoses->BoreDirectionInChest.IsNearlyZero();
        AimBoreInChest=GunPoses->BoreDirectionInChest.GetSafeNormal();
        AimTargetDirection=GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(UDemoPlayerEyesComponent::ReadAimDirection(Character.Get())).GetSafeNormal();
        AimAlignmentWeight=AimRestore; // 换弹保持源动作，结束/取消沿同一源释放恢复；不重复Pow4，射击偏移由原动画保留。
    }
    if (const UDemoMeleeComponent* Melee=Character->FindComponentByClass<UDemoMeleeComponent>(); Melee && Melee->IsEquipped())
    {
        // 总俯仰仍沿原DA范围；显式分布式近战由最终节点PreUpdate发布策略、让整条spine分摊，旧DA仍用整上胸。
        // 不运行枪械掌位偏移/手枪IK，也不以相机角度扩大伤害范围；真手重锚轨迹沿服务器最终TP握点。
        AimPitch=FMath::Clamp(ViewPitch,-Melee->GetDefinition()->AimPitchLimit,Melee->GetDefinition()->AimPitchLimit);
        bCalibratedBoreAim=false; AimAlignmentWeight=0.f; // 枪管标定不适用于棍子轨迹。
        AuthoredAimProfile=nullptr; // 近战有独立作者动作，不消费上一把枪的瞄准库。
        AimTorsoFraction=1.f; AimGripOffset=FVector::ZeroVector; AimSupportWeight=1.f;
    }
    UpdateMuzzleFocus(DeltaSeconds); // 原作者姿态负责大范围Aim；这里只发布准星焦点，枪/双腕在同一最终节点小幅汇聚。
    const int32 Band=AimPitch < -1.f?-1:AimPitch>1.f?1:0; // 只记录抬枪/压枪方向变化，高频细值仍可通过动画缓存/专项读取。
    if (Band!=AimLogBand)
    { UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_AIM_DIRECTION pawn=%s local=%d pitch=%.2f target=%.2f support=%.2f"),*Character->GetName(),Character->IsLocallyControlled(),AimPitch,Target,AimSupportWeight); AimLogBand=Band; }
}
