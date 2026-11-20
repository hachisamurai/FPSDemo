// 职责：核验可选身体后坐资源契约；使用当前Appearance配置与实际身体Skeleton，只读校验，不加载新框架或修改资产。
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Debug/DemoLog.h"

bool FDemoPlayerWeaponPoses::ValidateRecoilMontage(const USkeleton* ExpectedSkeleton,FString& Error) const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    Error.Empty();
    if (!RecoilMontage) return true; // 旧资产不必新增字段；空值显式选择原完整Fire策略。
    if (!Hold || !ExpectedSkeleton || Hold->GetSkeleton()!=ExpectedSkeleton || RecoilMontage->GetSkeleton()!=ExpectedSkeleton)
        Error=TEXT("recoil Hold/Montage/body Skeleton mismatch");
    else if (!AuthoredAim || !bRigidChestAim)
        Error=TEXT("recoil requires the authored Hold/aim frame");
    else if (!FMath::IsFinite(RecoilPlayRate) || RecoilPlayRate<.25f || RecoilPlayRate>4.f
        || !FMath::IsFinite(RecoilBlendInSeconds) || RecoilBlendInSeconds<0.f || RecoilBlendInSeconds>.2f
        || !FMath::IsFinite(RecoilRetriggerBlendSeconds) || RecoilRetriggerBlendSeconds<.005f || RecoilRetriggerBlendSeconds>.2f
        || !FMath::IsFinite(RecoilBlendOutSeconds) || RecoilBlendOutSeconds<0.f || RecoilBlendOutSeconds>.3f)
        Error=TEXT("recoil rate/blend invalid");
    else if (RecoilMontage->SlotAnimTracks.Num()!=1 || RecoilMontage->SlotAnimTracks[0].SlotName!=TEXT("DemoGunRecoil")
        || RecoilMontage->GetGroupName()!=TEXT("DemoGunRecoilGroup") || !FMath::IsNearlyEqual(RecoilMontage->RateScale,1.f,.0001f)
        || RecoilMontage->BlendModeIn!=EMontageBlendMode::Standard || RecoilMontage->BlendModeOut!=EMontageBlendMode::Standard
        || RecoilMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1)
        Error=TEXT("recoil requires one independent DemoGunRecoilGroup/DemoGunRecoil full segment and standard cross-fade");
    else
    {
        const FAnimSegment& Segment=RecoilMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0]; // 同步借用，资产不被改写。
        const UAnimSequence* Sequence=Cast<UAnimSequence>(Segment.GetAnimReference()); // 候选源必须已完成绑定/中性姿态验收。
        if (!Sequence || !Sequence->IsValidAdditive() || Sequence->GetAdditiveAnimType()!=AAT_RotationOffsetMeshSpace
            || Sequence->HasRootMotion() || Sequence->GetPlayLength()<=0.f || Sequence->GetSkeleton()!=ExpectedSkeleton
            || !FMath::IsNearlyZero(Segment.StartPos,.0001f) || !FMath::IsNearlyZero(Segment.AnimStartTime,.0001f)
            || !FMath::IsNearlyEqual(Segment.AnimEndTime,Sequence->GetPlayLength(),.001f)
            || Segment.LoopingCount!=1 || !FMath::IsNearlyEqual(Segment.AnimPlayRate,1.f,.0001f))
            Error=TEXT("recoil source must be complete mesh-space additive, no RootMotion/loop/trim");
    }
    return Error.IsEmpty();
}

float FDemoPlayerWeaponPoses::GetRecoilPresentationSeconds() const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    return RecoilMontage && FMath::IsFinite(RecoilPlayRate) && RecoilPlayRate>=.25f && RecoilPlayRate<=4.f
        ? RecoilMontage->GetPlayLength()/RecoilPlayRate:0.f;
}
