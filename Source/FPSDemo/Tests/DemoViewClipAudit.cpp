// 职责：对原LOD修复对照补足全尺寸停帧与近裁剪检查。通过DemoViewGhostAudit -DemoViewClipAudit显式运行；只修改本Editor诊断进程输入，不保存Content/相机配置。
#include "Tests/DemoViewGhostAudit.h"
#include "Animation/AnimInstance.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Player/DemoPlayerController.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "UnrealClient.h"

void ADemoViewGhostAudit::TickClipReview(float DeltaSeconds)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    const float Pitches[]={0,-25,-50,-60,30,55}; // 实际相机许可上下限及原录像经过的中间角，不通过骨骼角度模拟镜头。
    const float Yaws[]={0,-45,45}; // 身体自由转头范围边界；每姿态先复位身体，随后让正式头身逻辑处理。
    const int32 Pose=Frame<0?0:Frame/60,Within=Frame<0?-1:Frame%60;
    if (Pose>=18) { Finish(true,TEXT("18 full-size static views captured; requires visual/contact review")); return; }
    const float Pitch=Pitches[Pose%6],Yaw=Yaws[Pose/6];
    if (Within==0) Player->SetActorRotation(FRotator::ZeroRotator); // 每格同一身体基准；只改Actor输入基底，不修改骨姿。
    Controller->SetControlRotation(FRotator(Pitch,Yaw,0)); Controller->UpdateRotation(DeltaSeconds);
    USkeletalMeshComponent* OwnerMesh=Player->GetPlayerPresentation()->GetFirstPersonBodyMesh(); // 主图/PP已在本PostUpdate前完成。
    if (Frame>=0)
    {
        FVector Eye; FRotator View; Controller->GetPlayerViewPoint(Eye,View); // 记录真实CameraManager缓存，不把刚请求的控制角冒充已渲染视角。
        const float Fov=Controller->PlayerCameraManager?Controller->PlayerCameraManager->GetFOVAngle():0.f;
        Rows.Add(FString::Printf(TEXT("%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d"),Pose,Frame,Pitch,Yaw,View.Pitch,View.Yaw,Eye.X,Eye.Y,Eye.Z,Fov,Player->GetMesh()->GetPredictedLODLevel(),OwnerMesh->GetPredictedLODLevel()));
        if (Frame%3==0 && !FScreenshotRequest::IsScreenshotRequested()) FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Clip_%04d.png"),Frame/3),false,false); // 实际20Hz取样，不插帧、不改变正常游戏时钟。
        if (Within==50) ProbeClipPose(Pose); // 输入稳定0.83秒后测同一最终蒙皮，转角瞬时旧相机只在CSV保留不作静态判断。
    }
    if (++Frame>=1080) Finish(true,TEXT("18 full-size static views captured; requires visual/contact review"));
#endif
}

void ADemoViewGhostAudit::ProbeClipPose(int32 Pose)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    USkeletalMeshComponent* Mesh=Player->GetPlayerPresentation()->GetFirstPersonBodyMesh();
    const FSkeletalMeshRenderData* Render=Mesh && Mesh->GetSkeletalMeshAsset()?Mesh->GetSkeletalMeshAsset()->GetResourceForRendering():nullptr;
    if (!Render || Render->LODRenderData.IsEmpty()) { Finish(false,TEXT("clip probe missing owner LOD0")); return; }
    const FSkeletalMeshLODRenderData& LOD=Render->LODRenderData[0]; FSkinWeightVertexBuffer* Weights=Mesh->GetSkinWeightBuffer(0);
    if (!Weights) { Finish(false,TEXT("clip probe missing CPU weights")); return; }
    FVector Eye; FRotator View; Controller->GetPlayerViewPoint(Eye,View); // 同一真实拥有者镜头，不用控制器期望角构造虚拟视点。
    const float Fov=Controller->PlayerCameraManager->GetFOVAngle(); int32 Width=0,Height=0; Controller->GetViewportSize(Width,Height);
    const double Near=GNearClippingPlane,Side=FMath::Tan(FMath::DegreesToRadians(Fov*.5f)),Top=Side*Height/FMath::Max(1,Width);
    TArray<FMatrix44f> Matrices; Mesh->GetCurrentRefToLocalMatrices(Matrices,0);
    TArray<FVector> Points; Points.Reserve(LOD.GetNumVertices()); const FQuat Inverse=View.Quaternion().Inverse();
    for (uint32 I=0;I<LOD.GetNumVertices();++I)
    {
        const FVector P(USkinnedMeshComponent::GetSkinnedVertexPosition(Mesh,I,LOD,*Weights,Matrices));
        Points.Add(Inverse.RotateVector(Mesh->GetComponentTransform().TransformPosition(P)-Eye)); // UE相机+X前/+Y右/+Z上，cm。
    }
    const FRawStaticIndexBuffer16or32Interface* Indices=LOD.MultiSizeIndexContainer.GetIndexBuffer();
    TArray<FString> Hits; Hits.Add(TEXT("triangle,bone,screen_y0,screen_z0,screen_y1,screen_z1,normal_dot_view"));
    int32 Count=0,ArmCount=0,TorsoCount=0;
    for (int32 I=0;I<Indices->Num();I+=3)
    {
        const FVector P[3]={Points[Indices->Get(I)],Points[Indices->Get(I+1)],Points[Indices->Get(I+2)]};
        if (FMath::Min3(P[0].X,P[1].X,P[2].X)>=Near || FMath::Max3(P[0].X,P[1].X,P[2].X)<=Near) continue;
        FVector Cross[2]; int32 Found=0;
        for (int32 E=0;E<3 && Found<2;++E)
        {
            const FVector A=P[E],B=P[(E+1)%3];
            if ((A.X<Near)!=(B.X<Near)) Cross[Found++]=FMath::Lerp(A,B,(Near-A.X)/(B.X-A.X));
        }
        if (Found!=2) continue;
        double Low=0,High=1; bool bInside=true; // 近面交线还须穿过真实视锥矩形，不能将屏幕外颈部裁切当成遮挡。
        for (int32 Axis=1;Axis<3;++Axis)
        {
            const double Limit=Near*(Axis==1?Side:Top),A=Cross[0][Axis],D=Cross[1][Axis]-A;
            if (FMath::Abs(D)<1.e-8) { if (FMath::Abs(A)>Limit) bInside=false; continue; }
            const double T0=(-Limit-A)/D,T1=(Limit-A)/D;
            Low=FMath::Max(Low,FMath::Min(T0,T1)); High=FMath::Min(High,FMath::Max(T0,T1));
        }
        if (!bInside || Low>High) continue;
        const uint32 V=Indices->Get(I); int32 Section=0,SectionVertex=0; LOD.GetSectionFromVertexIndex(V,Section,SectionVertex);
        uint16 MaxWeight=0; int32 Bone=0;
        for (uint32 W=0;W<Weights->GetMaxBoneInfluences();++W)
            if (Weights->GetBoneWeight(V,W)>MaxWeight) { MaxWeight=Weights->GetBoneWeight(V,W); Bone=LOD.RenderSections[Section].BoneMap[Weights->GetBoneIndex(V,W)]; }
        const FString Name=Mesh->GetBoneName(Bone).ToString(); // 只用作区域归因，非蒙皮修改建议。
        ++Count; if (Name.Contains(TEXT("arm")) || Name.Contains(TEXT("hand")) || Name.Contains(TEXT("wrist"))) ++ArmCount;
        if (Name.Contains(TEXT("neck")) || Name.Contains(TEXT("spine")) || Name.Contains(TEXT("clavicle"))) ++TorsoCount;
        const FVector A=FMath::Lerp(Cross[0],Cross[1],Low),B=FMath::Lerp(Cross[0],Cross[1],High);
        Hits.Add(FString::Printf(TEXT("%d,%s,%.7f,%.7f,%.7f,%.7f,%.7f"),I/3,*Name,A.Y/(Near*Side),A.Z/(Near*Top),B.Y/(Near*Side),B.Z/(Near*Top),FVector::DotProduct(FVector::CrossProduct(P[1]-P[0],P[2]-P[0]).GetSafeNormal(),P[0].GetSafeNormal())));
    }
    if (ClipProbeRows.IsEmpty()) ClipProbeRows.Add(TEXT("pose,view_pitch,view_yaw,near_cm,fov,leader_lod,follower_lod,near_cut_triangles,arm_triangles,torso_triangles"));
    ClipProbeRows.Add(FString::Printf(TEXT("%d,%.5f,%.5f,%.5f,%.5f,%d,%d,%d,%d,%d"),Pose,View.Pitch,View.Yaw,Near,Fov,Player->GetMesh()->GetPredictedLODLevel(),Mesh->GetPredictedLODLevel(),Count,ArmCount,TorsoCount));
    FFileHelper::SaveStringArrayToFile(Hits,*(Directory/FString::Printf(TEXT("P%02d_NearCuts.csv"),Pose)));
    UE_LOG(LogFPSDemo,Log,TEXT("VIEW_CLIP_PROBE pose=%d pitch=%.3f yaw=%.3f near=%.3f triangles=%d arms=%d torso=%d"),Pose,View.Pitch,View.Yaw,Near,Count,ArmCount,TorsoCount);
    if (Count>0 || Pose==0 || Pose==5) { DumpSkin(Mesh,*FString::Printf(TEXT("P%02dOwner"),Pose)); }
#endif
}
