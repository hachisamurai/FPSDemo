#include "Tests/DemoEnemyAttackTest.h"
#include "AI/DemoEnemy.h"
#include "Animation/DemoEnemyPresentation.h"
#include "Animation/DemoEnemyAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "Game/FPSDemoGameMode.h"
#include "Characters/DemoCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Camera/CameraActor.h" // 可渲染回归使用独立观测相机，不修改正式玩家镜头配置。
#include "Camera/CameraComponent.h"
#include "GameFramework/HUD.h"

void ADemoEnemyAttackTest::TickAnimationTest()
{
    DEMO_LOG_TICK();
    // 连续驱动0.2秒真实位置变化；单次位移只让一帧bMoving为真，低频断言会错过，不能据此误判图失效。
    if (!bFailed&&Step==5&&Shooter.IsValid()) Shooter->AddActorWorldOffset(FVector(120.f*GetWorld()->GetDeltaSeconds(),0,0));
    if (bFailed||GetWorld()->GetTimeSeconds()<NextTime) return;
    if (GetWorld()->GetTimeSeconds()>60) { Check(false,TEXT("animation bounded runtime")); return; }
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前真实规则对象，仅测试使用。
    ADemoCharacter* Player=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 单帧借用，启动流程可能更换Pawn。
    UDemoEnemyPresentation* View=Shooter.IsValid()?Shooter->FindComponentByClass<UDemoEnemyPresentation>():nullptr; // Enemy拥有组件。
    USkeletalMeshComponent* Mesh=View?View->GetMesh():nullptr; // 真实游戏骨骼，不使用EditorPose模拟。
    UDemoEnemyAnimInstance* Anim=Mesh?Cast<UDemoEnemyAnimInstance>(Mesh->GetAnimInstance()):nullptr; // 图实例来自已保存ABP。
    UDemoEnemyPresentation* BossView=Boss.IsValid()?Boss->FindComponentByClass<UDemoEnemyPresentation>():nullptr; // 不和普通骨架交叉播放。
    switch (Step)
    {
    case 0:
    {
        SetActorTickInterval(0.f); // 动画专项逐帧采样，原攻击/俯冲专项仍保留各自测试频率。
        if (!Check(Mode&&Player&&Mode->StartRun(),TEXT("animation isolated hub initialized"))) return;
        Player=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // StartRun后重新取得实际Pawn。
        Player->GetCharacterMovement()->DisableMovement();
        const FVector Center=Mode->GetAreaCenter(0); // 使用真实白模竞技场地板，不在地图原点虚空拍摄。
        Player->SetActorLocation(Center+FVector(-1000,0,150));
        Player->GetController()->SetControlRotation(FRotator(0,0,0));
        Shooter=SpawnEnemy(Center+FVector(-200,-180,130),false);
        Boss=SpawnEnemy(Center+FVector(100,220,130),true);
        if (!Check(Shooter.IsValid()&&Boss.IsValid(),TEXT("two live rig actors configured"))) return;
        // 在截图数秒前切换相机，给TAA/自动曝光稳定时间；不能在切镜头同帧截图造成运动模糊。
        if (FApp::CanEverRender())
        {
            const FVector Target=Center+FVector(0,0,180); // 构图对准两敌人中间，保留完整背环。
            const FVector CameraLocation=Target+FVector(-950,0,120); // 世界厘米，相机不参与碰撞/导航。
            ACameraActor* Camera=GetWorld()->SpawnActor<ACameraActor>(CameraLocation,(Target-CameraLocation).Rotation()); // 测试World持有，退出统一销毁。
            Camera->GetCameraComponent()->SetFieldOfView(65.f);
            APlayerController* Controller=CastChecked<APlayerController>(Player->GetController()); // 真实本地观测控制器。
            Controller->SetViewTarget(Camera);
            if (Controller->GetHUD()) Controller->GetHUD()->bShowHUD=false;
        }
        Advance(.5f); break;
    }
    case 1:
        if (!Check(Anim&&BossView&&BossView->GetMesh()->GetAnimInstance(),TEXT("both generated AnimBPs instantiated"))) return;
        if (!Check(Shooter->GetAbilitySystemComponent()->AbilityActorInfo->GetAnimInstance()==Anim,TEXT("ASC uses configured primary AnimInstance"))) return;
        AnimationBone=Mesh->GetSocketTransform(TEXT("forearm_l"),RTS_Component).GetLocation();
        if (!Check(View->Play(TEXT("Fire")),TEXT("GAS fire starts"))) return;
        Advance(.12f); break;
    case 2:
        if (!Check(Anim&&Anim->IsAnyMontagePlaying(),TEXT("real Montage playing through ASC"))) return;
        if (!Check(FVector::Dist(AnimationBone,Mesh->GetSocketTransform(TEXT("forearm_l"),RTS_Component).GetLocation())>.2f,TEXT("Slot changes actual evaluated bones"))) return;
        if (!Check(!View->Play(TEXT("Hit")),TEXT("hit does not interrupt attack"))) return;
        Advance(.6f); break;
    case 3:
        if (!Check(!Anim->IsAnyMontagePlaying(),TEXT("task completes and returns to base graph"))) return;
        if (!Check(View->Play(TEXT("Fire")),TEXT("same Montage can replay"))) return;
        Advance(.6f); break;
    case 4:
        Shooter->AddActorWorldOffset(FVector(20,0,0)); // 真实位置变化让AActor差分速度驱动移动混合。
        Advance(.2f); break;
    case 5:
        if (!Check(Anim->bMoving,TEXT("AActor motion drives C++ locomotion cache"))) return;
        Shooter->GetAbilitySystemComponent()->AddLooseGameplayTag(DemoAmmoTags::Frozen); // 仅测试本地状态读取，未声称loose tag有网络复制。
        Shooter->AddActorWorldOffset(FVector(20,0,0));
        if (!Check(View->Play(TEXT("Fire")),TEXT("frozen enemy can still animate attack"))) return;
        Advance(.12f); break;
    case 6:
        if (!Check(Anim->bMovementFrozen&&!Anim->bMoving&&Anim->IsAnyMontagePlaying(),TEXT("freeze suppresses movement but preserves attack Slot"))) return;
        Shooter->GetAbilitySystemComponent()->RemoveLooseGameplayTag(DemoAmmoTags::Frozen);
        if (!Check(BossView->Play(TEXT("Global")),TEXT("Boss global Montage starts"))) return;
        if (!Check(!BossView->Play(TEXT("Rage")),TEXT("rage queued behind cast"))) return;
        Advance(.4f); break;
    case 7:
        if (!Check(BossView->GetMesh()->GetAnimInstance()->GetCurrentActiveMontage()
            &&BossView->GetMesh()->GetAnimInstance()->GetCurrentActiveMontage()->GetFName()==TEXT("AM_Warden_Global"),TEXT("cast retains ownership despite rage"))) return;
        // GPU运行时保存真实UE场景；NullRHI仍执行同一骨骼/任务验证，但不会声称产生截图。
        if (FApp::CanEverRender()) FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/EnemyAnimationRuntime.png"),false,false);
        Advance(2.25f); break;
    case 8:
        if (!Check(BossView->GetMesh()->GetAnimInstance()->GetCurrentActiveMontage()
            &&BossView->GetMesh()->GetAnimInstance()->GetCurrentActiveMontage()->GetFName()==TEXT("AM_Warden_Rage"),TEXT("queued rage plays after cast"))) return;
        Boss->CancelPendingAttacks();
        if (!Check(!BossView->GetMesh()->GetAnimInstance()->Montage_IsPlaying(nullptr),TEXT("explicit cancel stops cast/task"))) return;
        DemoEffects::Apply(Shooter->GetAbilitySystemComponent(),Shooter->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-1000.f);
        if (!Check(!Shooter->IsAlive()&&!Shooter->GetActorEnableCollision()&&!Shooter->IsHidden(),TEXT("death immediately removes gameplay collision but keeps visible corpse"))) return;
        Advance(.3f); break;
    case 9:
        if (!Check(Shooter.IsValid()&&Anim&&Anim->GetCurrentActiveMontage()&&Anim->GetCurrentActiveMontage()->GetFName()==TEXT("AM_Chaser_Death"),TEXT("death Montage survives attack cancellation"))) return;
        Advance(1.5f); break;
    case 10:
        if (!Check(!Shooter.IsValid(),TEXT("corpse and animation lifecycle cleaned"))) return;
        Boss->Destroy();
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_ENEMY_ANIMATION_SUCCESS"));
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
}
