// 职责：以UE5.4原生Standalone节点组成Owner下身小图；游戏线程采CMC和本地时钟，AnyThread不访问World/Pawn，不提取RootMotion/Notify。
// 参考：本机AnimInstanceProxy.GetCustomRootNode/PreUpdate和BlendSpacePlayer_Standalone/SequenceEvaluator_Standalone；仅复用API，无新插件/生成类依赖。
#include "Animation/DemoOwnerLowerBodyAnimInstance.h"
#include "Animation/DemoLocomotionSet.h"
#include "Characters/DemoCharacter.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Debug/DemoLog.h"

namespace
{
    constexpr float OwnerAirEntrySeconds=.18f; // 复用已烘焙JumpStart的现有语义窗口，不改变实际跳跃。
    constexpr float OwnerAirSegmentBlendSeconds=.08f; // 相邻完整空中段只短混姿态，不改变动作路径/骨骼。
    constexpr float OwnerGroundAirBlendSeconds=.10f; // 地空显示混合秒，不拖延CMC落地或技能。

    /** 原生proxy没有generated node data；只适配Standalone节点的显式相位，采样/混合仍全部由UE原生父类执行。 */
    struct FOwnerGroundEvaluator final : FAnimNode_BlendSpacePlayer_Standalone
    {
        float Phase=0.f; // GT快照的0..1步相，worker只读，不自行dt累计。
        virtual bool IsEvaluator() const override { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] OwnerLowerBody.Ground.IsEvaluator")); return true; }
        virtual bool ShouldTeleportToTime() const override { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] OwnerLowerBody.Ground.Teleport")); return true; }
        virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override
        {
            UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] OwnerLowerBody.Ground.Update"));
            SetAccumulatedTime(Phase); // 显式相位先写，再走原生TickRecord/BlendSample流程，不能只Evaluate漏掉Update。
            FAnimNode_BlendSpacePlayer_Standalone::UpdateAssetPlayer(Context);
        }
    };
}

/** 引擎管理的本实例图；GT PreUpdate后节点仅访问已保活资源与本帧纯值，禁止回读另一实例/TP最终Pose。 */
struct FDemoOwnerLowerBodyProxy final : FAnimInstanceProxy
{
    FOwnerGroundEvaluator Ground; // 完整DefaultGround，拥有自己的BlendSample缓存与步相。
    FAnimNode_SequenceEvaluator_Standalone Air,OldAir; // 显式秒取样，DoNotSync/Teleport隔绝Notify和RootMotion。
    FAnimNode_TwoWayBlend AirBlend,Root; // AirBlend仅换段短混；Root仅地空混合，没有任何Slot或骨控制。
    explicit FDemoOwnerLowerBodyProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] OwnerLowerBody.Proxy.Constructor"));
        Ground.SetResetPlayTimeWhenBlendSpaceChanges(false); Ground.SetPlayRate(0.f); Ground.SetGroupMethod(EAnimSyncMethod::DoNotSync);
        Air.SetTeleportToExplicitTime(true); OldAir.SetTeleportToExplicitTime(true);
        Air.SetGroupMethod(EAnimSyncMethod::DoNotSync); OldAir.SetGroupMethod(EAnimSyncMethod::DoNotSync);
        AirBlend.A.SetLinkNode(&OldAir); AirBlend.B.SetLinkNode(&Air);
        Root.A.SetLinkNode(&Ground); Root.B.SetLinkNode(&AirBlend); // 直接原生FPoseLink，Root生命周期由FAnimInstanceProxy维护。
    }
    virtual FAnimNode_Base* GetCustomRootNode() override
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] OwnerLowerBody.Proxy.Root")); return &Root;
    }
    virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] OwnerLowerBody.Proxy.Nodes"));
        OutNodes.Add(&Ground); OutNodes.Add(&Air); OutNodes.Add(&OldAir); OutNodes.Add(&AirBlend); OutNodes.Add(&Root);
    }
    virtual void PreUpdate(UAnimInstance* Instance,float DeltaSeconds) override
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] OwnerLowerBody.Proxy.PreUpdate"));
        FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
        UDemoOwnerLowerBodyAnimInstance* Source=Cast<UDemoOwnerLowerBodyAnimInstance>(Instance); // 本回调为GT，工作线程不保留Source/Pawn指针。
        if (!Source) return;
        Source->CaptureFrame(DeltaSeconds); // 在同一PreUpdate内先采样再复制，避开NativeUpdate晚于PreUpdate造成一帧滞后。
        Ground.SetBlendSpace(Source->Frame.bResourcesReady?Source->Ground.Get():nullptr);
        Ground.SetPosition(Source->Frame.GroundCoordinates); Ground.Phase=Source->Frame.GaitPhase;
        Air.SetSequence(Source->Frame.bResourcesReady?Source->CurrentAir.Get():nullptr); Air.SetExplicitTime(Source->CurrentAirTime);
        OldAir.SetSequence(Source->Frame.bResourcesReady?Source->PreviousAir.Get():nullptr); OldAir.SetExplicitTime(Source->PreviousAirTime);
        AirBlend.Alpha=Source->Frame.AirSegmentBlend; Root.Alpha=Source->Frame.AirWeight;
    }
};

UDemoOwnerLowerBodyAnimInstance::UDemoOwnerLowerBodyAnimInstance()
{
    DEMO_LOG_CALL(); SetRootMotionMode(ERootMotionMode::NoRootMotionExtraction); // 不提取也不反向驱动CMC；实例只做可见姿态。
}
void UDemoOwnerLowerBodyAnimInstance::Configure(UDemoLocomotionSet* InSet)
{
    DEMO_LOG_CALL();
    const USkeletalMeshComponent* Component=GetSkelMeshComponent();
    USkeleton* Skeleton=Component && Component->GetSkeletalMeshAsset()?Component->GetSkeletalMeshAsset()->GetSkeleton():nullptr; // 当前Owner自己的Skeleton。
    if (bConfigured && Locomotion==InSet && ResourceSkeleton==Skeleton) return; // 同资源重绑不重置步相/跳跃，网格RequiredBones由引擎重缓存。
    Locomotion=InSet; ResourceSkeleton=Skeleton; bConfigured=true;
    const FDemoLocomotionProfile* Base=InSet?InSet->Profiles.Find(TEXT("rifle")):nullptr; // 固定既有基准配置；不随装备/近战/TP Dash切换。
    Ground=InSet?InSet->DefaultGround.Get():nullptr;
    JumpStart=Base?Base->BodyJumpStart.Get():nullptr; JumpLoop=Base?Base->BodyJumpLoop.Get():nullptr; Land=Base?Base->BodyLand.Get():nullptr;
    CurrentAir=JumpLoop; PreviousAir=JumpLoop; CurrentAirTime=PreviousAirTime=0.f; SegmentElapsed=OwnerAirSegmentBlendSeconds;
    ValidateResources();
}
void UDemoOwnerLowerBodyAnimInstance::NativeInitializeAnimation()
{
    DEMO_LOG_CALL(); Super::NativeInitializeAnimation();
    ADemoCharacter* NextPawn=Cast<ADemoCharacter>(TryGetPawnOwner()); // 只读本实例Owner，不要求先找到TP AnimInstance。
    if (!bInitialized || Pawn.Get()!=NextPawn)
    {
        Frame=FDemoOwnerLowerBodyAudit(); AirElapsed=LandElapsed=0.f; CurrentAirTime=PreviousAirTime=0.f;
        SegmentElapsed=OwnerAirSegmentBlendSeconds; bWasFalling=bLanding=false;
    }
    Pawn=NextPawn; bInitialized=true;
    if (bConfigured)
    {
        const USkeletalMeshComponent* Component=GetSkelMeshComponent();
        ResourceSkeleton=Component && Component->GetSkeletalMeshAsset()?Component->GetSkeletalMeshAsset()->GetSkeleton():nullptr;
        ValidateResources(); // 相同Pawn重初始化保留时钟，换网格重新校验，不将TP Leader恢复回来。
    }
}
void UDemoOwnerLowerBodyAnimInstance::ValidateResources()
{
    DEMO_LOG_CALL();
    bool Valid=Locomotion && ResourceSkeleton && Ground && Ground->GetSkeleton()==ResourceSkeleton && !Ground->IsValidAdditive()
        && !Ground->GetBlendSamples().IsEmpty() && !Locomotion->Strides.IsEmpty() && FMath::IsFinite(Locomotion->RunSpeed) && Locomotion->RunSpeed>0.f
        && FMath::IsFinite(Locomotion->WalkSpeed) && Locomotion->WalkSpeed>0.f;
    if (Locomotion)
        for (const FDemoGaitStride& Stride : Locomotion->Strides) // 同源步长只允许有限正数；非法输入不能传播到显式相位。
            Valid=Valid && FMath::IsFinite(Stride.Direction) && FMath::IsFinite(Stride.WalkCentimeters) && FMath::IsFinite(Stride.RunCentimeters)
                && Stride.WalkCentimeters>0.f && Stride.RunCentimeters>0.f;
    for (const UAnimSequence* Sequence : {JumpStart.Get(),JumpLoop.Get(),Land.Get()}) // 三段缺一不混另一套动作或Additive参考。
        Valid=Valid && Sequence && Sequence->GetSkeleton()==ResourceSkeleton && !Sequence->IsValidAdditive() && Sequence->GetPlayLength()>UE_SMALL_NUMBER;
    if (Ground)
        for (const FBlendSample& Sample : Ground->GetBlendSamples())
            Valid=Valid && Sample.Animation && Sample.Animation->GetSkeleton()==ResourceSkeleton && !Sample.Animation->IsValidAdditive();
    if (!bLoggedResourceState || Frame.bResourcesReady!=Valid)
    {
        if (Valid) { UE_LOG(LogFPSDemo,Log,TEXT("OWNER_LOWERBODY_RESOURCES ready=1 ground=%s profile=rifle"),*GetNameSafe(Ground)); }
        else { UE_LOG(LogFPSDemo,Error,TEXT("OWNER_LOWERBODY_RESOURCES ready=0 reason=missing/incompatible/non-full-pose resources; reference pose until configured")); }
        bLoggedResourceState=true;
    }
    Frame.bResourcesReady=Valid;
}
void UDemoOwnerLowerBodyAnimInstance::CaptureFrame(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    ADemoCharacter* Character=Pawn.Get();
    const UCharacterMovementComponent* Movement=Character?Character->GetCharacterMovement():nullptr; // 唯一实际移动真值，不读TP State/Gait/Slots/技能Tag。
    if (!Frame.bResourcesReady || !Movement)
    {
        Frame.GroundCoordinates=FVector::ZeroVector; Frame.AirWeight=0.f; Frame.AirSegmentBlend=1.f;
        return; // Ground/Air资源为空时标准节点输出RefPose；无Pawn时保持已配置中立地面，不同步加载或反复Error。
    }
    const float Step=FMath::IsFinite(DeltaSeconds)?FMath::Max(0.f,DeltaSeconds):0.f; // 游戏秒，暂停不推进；低帧不人为丢弃实际时长。
    const FVector Velocity=Movement->Velocity.ContainsNaN()?FVector::ZeroVector:Movement->Velocity;
    const FVector Local=Character->GetActorTransform().InverseTransformVectorNoScale(Velocity); // BS契约为身体右/前，不借相机pitch扭腿。
    Frame.Speed=static_cast<float>(Velocity.Size2D()); Frame.bFalling=Movement->IsFalling();
    const FVector Desired=FVector(Local.Y,Local.X,0.f).GetClampedToMaxSize(Locomotion->RunSpeed); // 仅显示坐标饱和，实际Dash速度/位置不改。
    const float BlendSeconds=FMath::IsFinite(Locomotion->DirectionBlendSeconds)?FMath::Max(.01f,Locomotion->DirectionBlendSeconds):.1f;
    Frame.GroundCoordinates=FMath::VInterpTo(Frame.GroundCoordinates,Desired,Step,1.f/BlendSeconds);
    if (Frame.Speed<1.f && Frame.GroundCoordinates.SizeSquared()<1.f) Frame.GroundCoordinates=FVector::ZeroVector;
    if (!Frame.bFalling && Frame.Speed>1.f)
    {
        const float Direction=FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Local.Y),static_cast<float>(Local.X))); // 与既有Stride表同一前/右定义。
        const float Stride=Locomotion->GetStride(Direction,Frame.Speed); // 复用已作者测量的方向步长，只GT读取DA。
        const float CyclesPerSecond=FMath::Clamp(Frame.Speed/FMath::Max(1.f,Stride),0.f,4.f); // 极高速只限定本机步频4Hz，不声称无滑步或改变移动。
        Frame.GaitPhase=FMath::Fmod(Frame.GaitPhase+Step*CyclesPerSecond,1.f);
    }
    if (Frame.bFalling)
    {
        if (!bWasFalling) AirElapsed=0.f;
        AirElapsed+=Step; bLanding=false;
    }
    else if (bWasFalling) { LandElapsed=0.f; bLanding=true; }
    else if (bLanding) LandElapsed+=Step;
    const float LandingSeconds=FMath::IsFinite(Locomotion->LandingSeconds)?FMath::Max(.05f,Locomotion->LandingSeconds):.3f;
    if (LandElapsed>=LandingSeconds) bLanding=false;
    const int32 NextState=Frame.bFalling?(AirElapsed<OwnerAirEntrySeconds && Velocity.Z>0.f?1:2):bLanding?3:0;
    UAnimSequence* NextAir=NextState==1?JumpStart.Get():NextState==2?JumpLoop.Get():NextState==3?Land.Get():CurrentAir.Get(); // 地面保持最后段，淡出期间不能换参考Pose。
    if (NextState!=Frame.State)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("OWNER_LOWERBODY_STATE from=%d to=%d speed=%.3f falling=%d"),Frame.State,NextState,Frame.Speed,Frame.bFalling);
        if (NextState!=0 && NextAir!=CurrentAir)
        { PreviousAir=CurrentAir; PreviousAirTime=CurrentAirTime; CurrentAir=NextAir; SegmentElapsed=0.f; }
        Frame.State=NextState;
    }
    if (CurrentAir)
    {
        const float Length=CurrentAir->GetPlayLength(); // 已在配置校验中确保正长度。
        if (NextState==1) CurrentAirTime=Length*FMath::Clamp(AirElapsed/OwnerAirEntrySeconds,0.f,1.f);
        else if (NextState==2) CurrentAirTime=FMath::Fmod(FMath::Max(0.f,AirElapsed-OwnerAirEntrySeconds),Length);
        else if (NextState==3) CurrentAirTime=Length*FMath::Clamp(LandElapsed/LandingSeconds,0.f,1.f);
    }
    SegmentElapsed+=Step; Frame.AirSegmentBlend=FMath::Clamp(SegmentElapsed/OwnerAirSegmentBlendSeconds,0.f,1.f);
    Frame.AirWeight=FMath::FInterpConstantTo(Frame.AirWeight,NextState==0?0.f:1.f,Step,1.f/OwnerGroundAirBlendSeconds);
    Frame.AirTime=CurrentAirTime; Frame.AirSequence=CurrentAir?CurrentAir->GetFName():NAME_None;
    bWasFalling=Frame.bFalling;
}
FAnimInstanceProxy* UDemoOwnerLowerBodyAnimInstance::CreateAnimInstanceProxy()
{
    DEMO_LOG_CALL(); return new FDemoOwnerLowerBodyProxy(this); // 引擎在安全生命周期边界分配/销毁，实例不共享节点缓存。
}
void UDemoOwnerLowerBodyAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    DEMO_LOG_CALL(); delete static_cast<FDemoOwnerLowerBodyProxy*>(InProxy);
}
FDemoOwnerLowerBodyAudit UDemoOwnerLowerBodyAnimInstance::ReadAudit() const
{
    DEMO_LOG_TICK(); return Frame; // 最后GT输入快照，不能拿它代替实际组件骨矩阵或最终像素。
}
