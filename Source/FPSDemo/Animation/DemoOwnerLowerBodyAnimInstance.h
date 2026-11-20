// 职责：本人下身独立基础移动实例；Presentation在本地分区上Configure，资源沿Locomotion保活，不继承TP上身/Dash/GAS图。
// 使用：原生Class直接配给Owner分区；纯值快照在Proxy::PreUpdate的游戏线程生成，工作线程只通过UE标准节点求值。
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DemoOwnerLowerBodyAnimInstance.generated.h"

class ADemoCharacter;
class UDemoLocomotionSet;
class UBlendSpace;
class UAnimSequence;
class USkeleton;
struct FDemoOwnerLowerBodyProxy;

/** GT输入审计副本，非骨姿/RHI验收；不包含Pawn/World引用，不参与网络或Gameplay。 */
struct FPSDEMO_API FDemoOwnerLowerBodyAudit
{
    bool bResourcesReady=false; // 当前配置与Owner Skeleton完整匹配，false时输出参考姿态。
    bool bFalling=false; // 当前真实CMC falling；不消费TP Dash/GAS标签。
    int32 State=0; // 本实例0地面/1起跳/2空中循环/3落地，不是TP LocomotionState。
    float GaitPhase=0.f; // 独立连续0..1步相，移动真实速度驱动，不从TP读取。
    float Speed=0.f; // 实际平面速度cm/s，未改变CMC。
    FVector GroundCoordinates=FVector::ZeroVector; // 平滑后BS输入cm/s，X右/Y前；只饱和显示坐标。
    float AirWeight=0.f; // 地面到空中链的最终混合权重0..1。
    float AirSegmentBlend=1.f; // 前空中段到当前段的短衔接权重0..1。
    float AirTime=0.f; // 当前段显式取样秒，不触发Notify/root motion。
    FName AirSequence; // 当前已保活资源名，仅用于诊断，不从名字加载。
};

/** 本人可见腿脚的窄图：DefaultGround + rifle基准跳跃；不播放Montage/LinkedLayer/武器姿态，不改骨/权重。 */
UCLASS(Transient,NotBlueprintable)
class FPSDEMO_API UDemoOwnerLowerBodyAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    /** 创建无RootMotion实例；代理由引擎管理，无额外线程或动画蓝图生成类。 */
    UDemoOwnerLowerBodyAnimInstance();
    /** GT绑定现有Set.DefaultGround和固定rifle基准BodyJumpStart/Loop/Land；同资源重复调用不重置步相，不写资产。 */
    void Configure(UDemoLocomotionSet* InSet);
    /** Owner网格重建时由引擎调用；同Pawn重初始化保留本实例连续时钟，新Pawn重置，组件生命周期由Presentation负责。 */
    virtual void NativeInitializeAnimation() override;
    /** 创建/销毁本实例的小图代理；不将TP主实例或共享代理挂到Owner分区。 */
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
    /** GT只读最后PreUpdate输入副本；不是最终渲染骨矩阵，调用方须另取实际Owner骨姿验收。 */
    FDemoOwnerLowerBodyAudit ReadAudit() const;
private:
    friend struct FDemoOwnerLowerBodyProxy; // 仅本类原生代理在PreUpdate游戏线程复制资源和纯值。
    /** 仅Proxy::PreUpdate调用：采真实CMC，推进自身步相/跳落时钟并保留上段取样供短衔接。 */
    void CaptureFrame(float DeltaSeconds);
    /** 配置/网格重建时核全部同Skeleton非Additive资源；缺失只在资格边沿输出错误，不从Tick同步加载。 */
    void ValidateResources();
    UPROPERTY(Transient) TObjectPtr<UDemoLocomotionSet> Locomotion; // 原Appearance拥有的资源闭包，本实例也强持有，非复制。
    UPROPERTY(Transient) TObjectPtr<UBlendSpace> Ground; // 固定DefaultGround，不随当前武器或TP动作切换。
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> JumpStart; // 同Skeleton已烘焙完整起跳，非Additive。
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> JumpLoop; // 同Skeleton完整空中循环。
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> Land; // 同Skeleton完整落地段。
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> CurrentAir; // 当前段资源，供工作线程标准Evaluator只读取样。
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> PreviousAir; // 换段时冻结的前段资源，不能在短混合期间被GC。
    UPROPERTY(Transient) TObjectPtr<USkeleton> ResourceSkeleton; // 实际Owner网格当前Skeleton，不以TP骨表代替。
    TWeakObjectPtr<ADemoCharacter> Pawn; // 本实例所属Pawn弱引用，只在GT访问，不延长Avatar寿命。
    FDemoOwnerLowerBodyAudit Frame; // GT快照，代理PreUpdate同栈复制；工作线程不访问此UObject。
    float AirElapsed=0.f,LandElapsed=0.f; // 本实例跳/落游戏秒；初始无落地过程，暂停dt0不推进。
    float CurrentAirTime=0.f,PreviousAirTime=0.f,SegmentElapsed=.08f; // 当前/前段秒及换段短混时钟，不与TP共享。
    bool bWasFalling=false,bLanding=false; // 本实例实际CMC边沿；空中Dash仍空中，地面Dash仍地面。
    bool bInitialized=false,bConfigured=false,bLoggedResourceState=false; // 初始化/依赖就绪/资格日志闩锁，不复制。
};
