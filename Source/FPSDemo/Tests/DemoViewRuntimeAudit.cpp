// 职责：复录正式Owner LOD策略的真实第一人称；入口-DemoViewGhostAudit -DemoViewRuntimeAudit。只驱动输入/记录实际状态，不临时修补LOD或关闭渲染效果。
#include "Tests/DemoViewGhostAudit.h"
#include "Animation/AnimInstance.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Camera/PlayerCameraManager.h" // 读最终CalcCamera结果，不能以组件默认PostProcess值代替实际镜头。
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "HAL/IConsoleManager.h"
#include "Player/DemoPlayerController.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "UnrealClient.h"

void ADemoViewGhostAudit::TickRuntimeReview(float DeltaSeconds)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    const double T=FMath::Max(0.,static_cast<double>(Frame)/60.); // 真正60Hz游戏时间，录像同率不插帧。
    float Pitch=-25,Yaw=0; // 前2秒沿用旧对照输入；后续延长移动及姿态检查，保留原模糊/TSR/投影。
    if (T>.4 && T<1.4) { Pitch=-25-30*FMath::Sin((T-.4)*PI*2); Yaw=55*FMath::Sin((T-.4)*PI*4); }
    else if (T>=1.4 && T<2.) { Pitch=-50; Yaw=25; }
    else if (T>=2.) { Pitch=-25+30*FMath::Sin((T-2.)*PI); Yaw=55*FMath::Sin((T-2.)*PI*.8); }
    Controller->SetControlRotation(FRotator(Pitch,Yaw,0)); Controller->UpdateRotation(DeltaSeconds);
    if (T>=2. && T<4.) Player->AddMovementInput(FVector::RightVector,T<3.?1.f:-1.f); // 正式CharacterMovement消费，非手设网格位移。
    if (Frame==150) Player->Jump();
    if (Frame==170) Player->StopJumping();
    UDemoMeleeComponent* Melee=Player->FindComponentByClass<UDemoMeleeComponent>(); // 正式DA和GAS输入，不注入候选动画。
    if (Melee && Frame==240) Melee->SetEquipped(true);
    if (Melee && (Frame==265 || Frame==345)) Melee->RequestAttack(Frame==265?0:1);
    if (Melee && Frame==420) Melee->SetEquipped(false);
    if (Frame>=0)
    {
        USkeletalMeshComponent* Body=Player->GetMesh(); // 实际本帧Leader求值结果，不用Skeleton资产总数代替Compact骨数。
        USkeletalMeshComponent* OwnerPartition=Player->GetPlayerPresentation()->GetFirstPersonBodyMesh();
        const UAnimInstance* Anim=Body->GetAnimInstance(); const FVector P=Player->GetActorLocation();
        const FMinimalViewInfo& View=Controller->PlayerCameraManager->GetCameraCachePOV(); // 本帧真实视图缓存，仅本同步调用借用。
        const int32 Bones=Anim?Anim->GetRequiredBones().GetCompactPoseNumBones():0;
        Rows.Add(FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%d,%.6f"),Frame,T,Pitch,Yaw,Body->GetPredictedLODLevel(),OwnerPartition->GetPredictedLODLevel(),Bones,
            Body->GetForcedLOD(),OwnerPartition->GetForcedLOD(),IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))->GetInt(),IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt(),
            OwnerPartition->IsVisible()&&!OwnerPartition->bHiddenInGame,Melee&&Melee->IsEquipped(),P.X,P.Y,P.Z,View.PostProcessSettings.MotionBlurAmount,View.PostProcessSettings.bOverride_MotionBlurAmount,View.PostProcessBlendWeight));
        if (Frame==110 || Frame==230 || Frame==350) { DumpSkin(OwnerPartition,*FString::Printf(TEXT("F%03dOwner"),Frame)); }
        if (Frame>5 && (Body->GetPredictedLODLevel()!=0 || OwnerPartition->GetPredictedLODLevel()!=0 || Bones!=Body->GetNumBones()))
        { Finish(false,TEXT("production policy lost complete owner/leader bones")); return; }
        if (!FScreenshotRequest::IsScreenshotRequested()) FScreenshotRequest::RequestScreenshot(Directory/FString::Printf(TEXT("Runtime_%04d.png"),Frame),false,false);
    }
    if (++Frame>=480) Finish(true,TEXT("8-second production-policy render captured; visual/temporal artifact review still required"));
#endif
}
