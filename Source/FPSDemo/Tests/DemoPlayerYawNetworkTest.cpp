// 职责：双进程LAN水平视角专项；在原十九阶段之后验证自由转头、越界完整对齐、反向与角度换界，读取真实头骨/枪口。
#include "Tests/DemoPlayerNetworkTest.h"
#include "Animation/DemoPlayerAimSettings.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

FRotator ADemoPlayerNetworkTest::GetYawStageAim() const
{
    DEMO_LOG_TICK();
    const float Inputs[]={0,20,45,60,25,-30,-60,-179,179,-160}; // 以45度默认契约独立列出边界/反向/跨±180序列，不能调用生产解算器生成期望。
    return FRotator(0,(Stage<29?HostYawBaseline:RemoteYawBaseline)+Inputs[FMath::Clamp((Stage-19)%10,0,9)],0);
}

bool ADemoPlayerNetworkTest::ObserveYaw()
{
    DEMO_LOG_TICK();
    if (!bYawAudit || Stage<19 || Stage>38 || GetWorld()->GetTimeSeconds()-StageStart<1.75f) return true;
    const float ExpectedBodyOffsets[]={0,0,0,60,60,-30,-30,-179,-179,-179}; // 独立手算新规则：越界收敛到视线，之后阈值内保持该身体角；179/-179仅2度差不触发再次转身。
    ADemoCharacter* Subject=GetSubject(); // 两端独立观察同一真实Pawn，不以服务器报告替代客户端渲染结果。
    const UDemoPlayerAnimInstance* Anim=Subject?Cast<UDemoPlayerAnimInstance>(Subject->GetMesh()->GetAnimInstance()):nullptr; // 当前生产实例，只读。
    const USkeletalMeshComponent* Gun=Subject?Subject->GetPlayerPresentation()->GetThirdPersonWeaponMesh():nullptr; // 最终PostUpdate挂点后的真正第三人称枪。
    const ADemoWeaponBase* Weapon=Subject?Subject->GetWeaponComponent()->GetActiveWeapon():nullptr; // 真实枪口Socket配置。
    if (!Anim || !Gun || !Weapon || !FMath::IsNearlyEqual(GetDefault<UDemoPlayerAimSettings>()->HeadYawLimit,45.f)) return false; // 本边界计划只为明确的默认45度配置，其他配置不能套用通过结论。
    const float ExpectedBody=(Stage<29?HostYawBaseline:RemoteYawBaseline)+ExpectedBodyOffsets[(Stage-19)%10]; // 独立手工轨迹加实际初始朝向。
    const float BodyError=FMath::Abs(FMath::FindDeltaAngleDegrees(ExpectedBody,static_cast<float>(Subject->GetActorRotation().Yaw))); // 测真实胶囊，不只测HeadYaw缓存。
    const float ViewError=FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(GetYawStageAim().Yaw),static_cast<float>(Subject->GetBaseAimRotation().Yaw))); // 无Controller远端必须看到独立水平视线复制。
    const FReferenceSkeleton& Reference=Subject->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton(); // 实际共享骨架，计算head本地参考前向而非硬编码骨轴。
    int32 Bone=Reference.FindBoneIndex(TEXT("head")); // 仅测试同步查询；缺骨直接失败。
    if (Bone==INDEX_NONE) return false;
    FTransform ReferenceHead=Reference.GetRefBonePose()[Bone]; // 累乘父链到Mesh组件空间，用于将模型+Y前向转换到head局部。
    while ((Bone=Reference.GetParentIndex(Bone))!=INDEX_NONE) ReferenceHead=ReferenceHead*Reference.GetRefBonePose()[Bone];
    const FVector HeadForward=Subject->GetMesh()->GetSocketTransform(TEXT("head"),RTS_World).TransformVectorNoScale(ReferenceHead.InverseTransformVectorNoScale(FVector::RightVector)); // 真正求值头骨，未连接动画图时会明确失败。
    const float HeadError=FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(GetYawStageAim().Yaw),static_cast<float>(HeadForward.Rotation().Yaw))); // 零Pitch水平计划，避免把原头部俯仰余量当水平误差。
    const float GunError=FMath::Abs(FMath::FindDeltaAngleDegrees(ExpectedBody,static_cast<float>(Gun->GetSocketRotation(Weapon->Config.MuzzleSocket).Yaw))); // 用户要求阈值内只转头，TP枪口水平必须继续随身体。
    ++YawSamples; MaximumYawBodyError=FMath::Max(MaximumYawBodyError,BodyError); MaximumYawHeadError=FMath::Max(MaximumYawHeadError,HeadError); MaximumYawGunError=FMath::Max(MaximumYawGunError,GunError);
    const bool Passed=BodyError<.2f && ViewError<.2f && HeadError<2.f && GunError<2.f && FMath::Abs(Anim->HeadYaw)<=45.01f; // 网络短角量化仅0.0055度；头/枪保留真实基础动画小幅姿势余量。
    if (!Passed) UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_YAW_NETWORK_INVALID stage=%d body=%.3f view=%.3f head=%.3f gun=%.3f cache=%.3f"),Stage,BodyError,ViewError,HeadError,GunError,Anim->HeadYaw);
    return Passed;
}
