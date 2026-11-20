#include "Animation/DemoPlayerEyesComponent.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/DemoLog.h"
#include "GameFramework/Controller.h"

UDemoPlayerEyesComponent::UDemoPlayerEyesComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick=true; PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UDemoPlayerEyesComponent::Initialize(UDemoPlayerAppearance* Settings)
{
    DEMO_LOG_CALL();
    if (LeftEye || RightEye) { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_EYES duplicate initialization refused")); return; }
    Appearance=Settings;
    ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwner()); // 当前Pawn同步借用，Owner卸载前组件EndPlay销毁网格。
    if (!Character || !Appearance || !Appearance->EyeMesh || !Character->GetMesh()->DoesSocketExist(TEXT("head")))
    { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_EYES unavailable: requires configured EyeMesh and original head bone")); SetComponentTickEnabled(false); return; }
    if (Appearance->LeftEyeRest.ContainsNaN() || Appearance->RightEyeRest.ContainsNaN()
        || !FMath::IsFinite(Appearance->EyeMaxYaw) || Appearance->EyeMaxYaw<0.f || Appearance->EyeMaxYaw>45.f
        || !FMath::IsFinite(Appearance->EyeMaxPitch) || Appearance->EyeMaxPitch<0.f || Appearance->EyeMaxPitch>30.f
        || !FMath::IsFinite(Appearance->EyeFollowSpeed) || Appearance->EyeFollowSpeed<=0.f)
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_EYES invalid asset transforms/limits/speed")); SetComponentTickEnabled(false); return; }
    AddTickPrerequisiteComponent(Character->GetMesh()); // 避免读取上一帧头部骨姿态，不依赖其他Actor的相机Tick。
    LeftEye=CreateEye(true); RightEye=CreateEye(false);
    SetComponentTickEnabled(true);
    UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_EYES_READY mesh=%s yaw=%.2f pitch=%.2f speed=%.2f"),*GetNameSafe(Appearance->EyeMesh),Appearance->EyeMaxYaw,Appearance->EyeMaxPitch,Appearance->EyeFollowSpeed);
}
UStaticMeshComponent* UDemoPlayerEyesComponent::CreateEye(bool bLeft)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character=CastChecked<ADemoCharacter>(GetOwner()); // Initialize已验证的同Pawn，不绑定旁观目标。
    UStaticMeshComponent* Eye=NewObject<UStaticMeshComponent>(Character,bLeft?TEXT("PlayerLeftEye"):TEXT("PlayerRightEye")); // Pawn拥有，本组件保活并负责卸载。
    Character->AddInstanceComponent(Eye);
    Eye->SetMobility(EComponentMobility::Movable); // 眼球每帧旋转，显式可移动避免组件默认值改变后拒绝更新。
    Eye->SetupAttachment(Character->GetMesh(),TEXT("head"));
    Eye->SetStaticMesh(Appearance->EyeMesh);
    Eye->SetRelativeTransform(bLeft?Appearance->LeftEyeRest:Appearance->RightEyeRest);
    Eye->SetCollisionEnabled(ECollisionEnabled::NoCollision); Eye->SetGenerateOverlapEvents(false);
    Eye->SetCanEverAffectNavigation(false); Eye->SetOwnerNoSee(true); Eye->SetOnlyOwnerSee(false);
    Eye->SetVisibility(Character->GetMesh()->IsVisible()); Eye->SetHiddenInGame(Character->GetMesh()->bHiddenInGame); // 迟到初始化也继承死亡隐藏；Body的SetVisibility默认不会向子组件传播。
    Eye->RegisterComponent();
    return Eye;
}
FVector UDemoPlayerEyesComponent::ReadAimDirection(const ADemoCharacter* Character)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); // 静态纯读函数没有this，不用成员宏。
    if (!Character) return FVector::ForwardVector;
    // APawn::GetBaseAimRotation有Controller时读取GetPlayerViewPoint，会串到观战/验收Camera；直接ControlRotation才是该Pawn输入瞄准。
    const AController* Controller=Character->GetController(); // 远端模拟代理通常为空，服务器上的远端PC可有效。
    return (Controller?Controller->GetControlRotation():Character->GetBaseAimRotation()).Vector().GetSafeNormal();
}
FRotator UDemoPlayerEyesComponent::SolveTarget(const FTransform& RestWorld,const FVector& WorldDirection,float YawLimit,float PitchLimit)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const FVector Local=RestWorld.InverseTransformVectorNoScale(WorldDirection).GetSafeNormal(); // 去掉Body/真实head/rest旋转，只求眼球相对静止方向。
    const FRotator Angles=Local.IsNearlyZero()?FRotator::ZeroRotator:Local.Rotation().GetNormalized(); // 无效零向量降级正视，不产生NaN。
    return FRotator(FMath::Clamp(static_cast<float>(Angles.Pitch),-PitchLimit,PitchLimit),FMath::Clamp(static_cast<float>(Angles.Yaw),-YawLimit,YawLimit),0.f);
}
void UDemoPlayerEyesComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,TickFunction);
    ADemoCharacter* Character=Cast<ADemoCharacter>(GetOwner()); // 每帧游戏线程借用，跨World不缓存裸Pawn指针。
    if (!Character || !Appearance || !LeftEye || !RightEye) return;
    AimDirection=ReadAimDirection(Character);
    const FTransform HeadWorld=Character->GetMesh()->GetSocketTransform(TEXT("head"),RTS_World); // 已经过最终Body动画/足IK的head世界姿态。
    const float Alpha=1.f-FMath::Exp(-Appearance->EyeFollowSpeed*FMath::Max(DeltaTime,0.f)); // 帧率无关指数跟随，0..1不会越过角度限制。
    for (int32 Index=0;Index<2;++Index) // 固定0左/1右，不按组件注册顺序猜眼别。
    {
        const FTransform& Rest=Index==0?Appearance->LeftEyeRest:Appearance->RightEyeRest; // 资产只读借用，位置与原始静止框架保持。
        TargetAngles[Index]=SolveTarget(Rest*HeadWorld,AimDirection,Appearance->EyeMaxYaw,Appearance->EyeMaxPitch);
        CurrentAngles[Index].Pitch=FMath::Lerp(CurrentAngles[Index].Pitch,TargetAngles[Index].Pitch,Alpha);
        CurrentAngles[Index].Yaw=FMath::Lerp(CurrentAngles[Index].Yaw,TargetAngles[Index].Yaw,Alpha);
        UStaticMeshComponent* Eye=Index==0?LeftEye.Get():RightEye.Get(); // 本Pawn拥有的真实渲染组件。
        Eye->SetRelativeRotation((Rest.GetRotation()*CurrentAngles[Index].Quaternion()).GetNormalized());
        Eye->SetHiddenInGame(Character->GetMesh()->bHiddenInGame); // 随正式身体隐藏状态，不用改相机或缩眼球躲穿模。
        Eye->SetVisibility(Character->GetMesh()->IsVisible()); // OnRep_Eliminated通过SetVisibility隐藏身体，独立眼球必须同步避免尸体消失后悬浮。
    }
}
void UDemoPlayerEyesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if (LeftEye) LeftEye->DestroyComponent();
    if (RightEye) RightEye->DestroyComponent();
    Super::EndPlay(EndPlayReason);
}
UStaticMeshComponent* UDemoPlayerEyesComponent::GetEyeMesh(bool bLeft) const { DEMO_LOG_TICK(); return bLeft?LeftEye.Get():RightEye.Get(); }
FRotator UDemoPlayerEyesComponent::GetTargetRotation(bool bLeft) const { DEMO_LOG_TICK(); return TargetAngles[bLeft?0:1]; }
FRotator UDemoPlayerEyesComponent::GetCurrentRotation(bool bLeft) const { DEMO_LOG_TICK(); return CurrentAngles[bLeft?0:1]; }
FVector UDemoPlayerEyesComponent::GetAimDirection() const { DEMO_LOG_TICK(); return AimDirection; }
