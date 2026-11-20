// 职责：正式模型闭包验证与作者变更指纹；复用原15个控件语义，未知/不完整资产失效而非生成假柜体或另起经济规则。
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Debug/DemoLog.h"

namespace
{
    /** T为实测根框；单位缩放/有限值是Query和装配共同约束，避免继承单位错误。 */
    bool ValidFrame(const FTransform& T)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] CasinoProfile.ValidFrame")); return !T.ContainsNaN() && T.GetScale3D().Equals(FVector::OneVector,.0001) && T.GetRotation().IsNormalized(); }
    /** 精确作者框指纹，不使用ToString小数截断；只检测同进程版本内容变化。 */
    uint32 HashFrame(const FTransform& T)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] CasinoProfile.HashFrame")); const FQuat Q=T.GetRotation();
        return HashCombine(HashCombine(GetTypeHash(T.GetTranslation()),GetTypeHash(T.GetScale3D())),HashCombine(HashCombine(GetTypeHash(Q.X),GetTypeHash(Q.Y)),HashCombine(GetTypeHash(Q.Z),GetTypeHash(Q.W))));
    }
}
bool UDemoCasinoDeviceProfile::Validate(FString& Error) const
{
    DEMO_LOG_CALL(); const bool bValid=ValidateDefinition(Error); // 失败也统一输出Error，内部每个有界拒绝分支都从此出口记录。
    if (!bValid) UE_LOG(LogFPSDemo,Log,TEXT("CASINO_MODEL_VALIDATION_REJECT profile=%s reason=%s"),*ProfileId.ToString(),*Error);
    return bValid;
}
bool UDemoCasinoDeviceProfile::ValidateDefinition(FString& Error) const
{
    DEMO_LOG_CALL(); Error=TEXT("正式老虎机Profile身份/源SHA/实测部件不完整");
    if (ProfileId.IsNone() || BindingRevision<3 || SourceSHA256.Len()!=64 || SourceSHA256==FString::ChrN(64,TEXT('0')) || Parts.Num()<19 || Parts.Num()>64 || Controls.Num()!=15) return false;
    for (TCHAR C:SourceSHA256) if (!FChar::IsHexDigit(C)) return false;
    if (!ValidFrame(FocusTransform) || !ValidFrame(InfoTransform) || !ValidFrame(CollisionTransform) || !ValidFrame(ScreenTransform)
        || CollisionExtent.ContainsNaN() || CollisionExtent.GetMin()<=0. || CollisionExtent.GetMax()>500.
        || !FMath::IsFinite(ScreenTextSizeCm) || ScreenTextSizeCm<.5f || ScreenTextSizeCm>20.f
        || !FMath::IsFinite(RevealSeconds) || RevealSeconds<.1f || RevealSeconds>3.f
        || !FMath::IsFinite(MechanicalSeconds) || MechanicalSeconds<.05f || MechanicalSeconds>2.f) return false;
    TSet<FName> Ids; // 精确控件白名单，客户端字符串不能添加动作。
    for (const FDemoCasinoControlBinding& C:Controls)
    {
        EDemoCasinoControlAction Action; int32 Index; // 从固定语义解析，不信作者填错的Action/Index。
        if (!DemoCasinoControls::ResolveSemantic(C.ControlId,Action,Index) || Action!=C.Action || Index!=C.Index || Ids.Contains(C.ControlId)
            || C.Position.ContainsNaN() || C.Rotation.ContainsNaN() || C.Extent.ContainsNaN() || C.Extent.GetMin()<=0. || C.Extent.GetMax()>100.)
        { Error=FString::Printf(TEXT("正式控件非法/重复: %s"),*C.ControlId.ToString()); return false; }
        Ids.Add(C.ControlId);
    }
    TSet<FName> PartIds,UsedControls; TSet<int32> Reels; bool bCabinet=false,bLever=false; // 本地验证集合，不持有World或私人数据。
    for (const FDemoCasinoModelPart& P:Parts)
    {
        if (P.PartId.IsNone() || PartIds.Contains(P.PartId) || !P.Mesh || !ValidFrame(P.RestTransform) || P.Materials.Num()>16)
        { Error=FString::Printf(TEXT("正式部件缺网格/身份/单位框: %s"),*P.PartId.ToString()); return false; }
        PartIds.Add(P.PartId);
        if (P.Kind==EDemoCasinoModelPartKind::Static) { bCabinet|=P.PartId==TEXT("cabinet"); continue; }
        if (P.MotionAxis.ContainsNaN() || !FMath::IsNearlyEqual(P.MotionAxis.SizeSquared(),1.,.001) || !FMath::IsFinite(P.MotionAmount)) return false;
        if (P.Kind==EDemoCasinoModelPartKind::Reel)
        {
            if (P.ReelIndex<0 || P.ReelIndex>2 || Reels.Contains(P.ReelIndex) || P.SymbolStopAngles.Num()!=5 || !P.ControlId.IsNone()) return false;
            for (float Angle:P.SymbolStopAngles) if (!FMath::IsFinite(Angle) || FMath::Abs(Angle)>3600.f) return false;
            for (int32 A=0;A<5;++A) for (int32 B=A+1;B<5;++B)
                if (FMath::Abs(FMath::FindDeltaAngleDegrees(P.SymbolStopAngles[A],P.SymbolStopAngles[B]))<.001f) { Error=TEXT("三轴不同符号的stop角不能mod360重合"); return false; }
            Reels.Add(P.ReelIndex); continue;
        }
        EDemoCasinoControlAction Action; int32 Index;
        if (!DemoCasinoControls::ResolveSemantic(P.ControlId,Action,Index) || UsedControls.Contains(P.ControlId)) return false;
        if (P.Kind==EDemoCasinoModelPartKind::Lever)
        { if (Action!=EDemoCasinoControlAction::PullSlots || FMath::Abs(P.MotionAmount)<.1f || FMath::Abs(P.MotionAmount)>120.f) return false; bLever=true; }
        else if (P.Kind==EDemoCasinoModelPartKind::Key)
        { if (Action==EDemoCasinoControlAction::PullSlots || P.MotionAmount<.001f || P.MotionAmount>2.f) return false; }
        else return false;
        UsedControls.Add(P.ControlId);
    }
    if (!bCabinet || !bLever || Reels.Num()!=3 || UsedControls.Num()!=15) { Error=TEXT("正式模型必须有柜体、三轴、杆和14独立键帽"); return false; }
    Error.Empty(); return true;
}
uint32 UDemoCasinoDeviceProfile::ContentFingerprint() const
{
    DEMO_LOG_CALL();
    uint32 Key=HashCombine(GetTypeHash(ProfileId),GetTypeHash(SourceSHA256)); // 同版源SHA是作者闭包身份，不把浮点作者矩阵当授权密钥。
    Key=HashCombine(Key,GetTypeHash(BindingRevision));
    for (const FDemoCasinoControlBinding& C:Controls)
    { Key=HashCombine(Key,GetTypeHash(C.ControlId)); Key=HashCombine(Key,GetTypeHash(C.Position)); Key=HashCombine(Key,HashFrame(FTransform(C.Rotation))); Key=HashCombine(Key,GetTypeHash(C.Extent)); Key=HashCombine(Key,HashCombine(GetTypeHash(int32(C.Action)),GetTypeHash(C.Index))); Key=HashCombine(Key,GetTypeHash(C.Label)); }
    for (const FDemoCasinoModelPart& P:Parts)
    { Key=HashCombine(Key,GetTypeHash(P.PartId)); Key=HashCombine(Key,GetTypeHash(GetPathNameSafe(P.Mesh.Get()))); Key=HashCombine(Key,HashFrame(P.RestTransform)); Key=HashCombine(Key,GetTypeHash(P.MotionAxis)); Key=HashCombine(Key,GetTypeHash(P.MotionAmount)); Key=HashCombine(Key,GetTypeHash(int32(P.Kind))); Key=HashCombine(Key,HashCombine(GetTypeHash(P.ControlId),GetTypeHash(P.ReelIndex))); for(float Angle:P.SymbolStopAngles) Key=HashCombine(Key,GetTypeHash(Angle)); for(UMaterialInterface* Material:P.Materials) Key=HashCombine(Key,GetTypeHash(GetPathNameSafe(Material))); }
    Key=HashCombine(Key,HashFrame(FocusTransform)); Key=HashCombine(Key,HashFrame(CollisionTransform)); Key=HashCombine(Key,GetTypeHash(CollisionExtent));
    Key=HashCombine(Key,HashFrame(InfoTransform)); Key=HashCombine(Key,HashFrame(ScreenTransform));
    return HashCombine(Key,HashCombine(GetTypeHash(ScreenTextSizeCm),HashCombine(GetTypeHash(RevealSeconds),GetTypeHash(MechanicalSeconds))));
}
