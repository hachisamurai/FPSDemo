#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoPlayerEyesComponent.generated.h"

class ADemoCharacter;
class UDemoPlayerAppearance;
class UStaticMeshComponent;

/** 只读真实角色瞄准方向的双眼表现；无骨架改动、碰撞、玩法真值或独立复制流。 */
UCLASS(ClassGroup=(Demo), NotBlueprintable)
class FPSDEMO_API UDemoPlayerEyesComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** PostUpdate在Body最终骨姿态后求局部眼角，不在动画工作线程访问控制器。 */
    UDemoPlayerEyesComponent();
    /** Settings为Pawn所属外观资产，只读强引用到组件卸载；由Presentation在注册后调用一次。 */
    void Initialize(UDemoPlayerAppearance* Settings);
    /** DeltaTime为World秒；TickType/TickFunction来自引擎，仅影响平滑表现。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    /** EndPlayReason为World/Actor卸载原因；销毁本组件创建的两枚静态眼球。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** bLeft选择左右；返回Pawn拥有的借用组件，用于验收可见性/姿态，不作为Gameplay接口。 */
    UStaticMeshComponent* GetEyeMesh(bool bLeft) const;
    /** bLeft选择左右；输出上次最终Body姿态计算的受限目标角（度），无网络真值含义。 */
    FRotator GetTargetRotation(bool bLeft) const;
    /** bLeft选择左右；输出实际已应用的平滑角（度），不含作者rest朝向。 */
    FRotator GetCurrentRotation(bool bLeft) const;
    /** 返回本帧来自该Pawn自身的单位世界瞄准向量，绝不查询旁观CameraManager。 */
    FVector GetAimDirection() const;
    /** RestWorld为眼静止框架，WorldDirection为实际瞄准单位向量，Yaw/PitchLimit为非负角度上限；纯值解算供运行时和边界验收。 */
    static FRotator SolveTarget(const FTransform& RestWorld, const FVector& WorldDirection, float YawLimit, float PitchLimit);
    /** Character仅同步借用；有控制器直接读ControlRotation，无控制器用ActorYaw+RemoteViewPitch，不读取ViewTarget。 */
    static FVector ReadAimDirection(const ADemoCharacter* Character);
private:
    /** bLeft为眼别；只创建同一Pawn拥有的无碰撞OwnerNoSee网格，rest来自已校验Appearance。 */
    UStaticMeshComponent* CreateEye(bool bLeft);
    // 本Pawn共享外观配置的强引用；仅初始化设置，不改写其材质/网格/Skeleton。
    UPROPERTY(Transient) TObjectPtr<UDemoPlayerAppearance> Appearance;
    // 双眼组件由Pawn拥有并由本对象保活；本地和远端各自创建，不复制组件Transform。
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LeftEye;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RightEye;
    // 索引0左/1右，度；受限目标和指数平滑值只存在本端，用于无越界过渡及只读验收。
    FRotator TargetAngles[2] = {FRotator::ZeroRotator,FRotator::ZeroRotator};
    FRotator CurrentAngles[2] = {FRotator::ZeroRotator,FRotator::ZeroRotator};
    // 最终采样瞄准向量；初始+X只是未更新前显示值，不会发回服务器。
    FVector AimDirection = FVector::ForwardVector;
};
