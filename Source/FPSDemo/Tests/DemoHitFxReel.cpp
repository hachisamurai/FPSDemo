// 职责：显式 Editor 独立游戏录制敌人命中视效。复用金属命中夹具与实际战役、手枪 GA、敌人和 RHI，不保存资产。
#include "Tests/DemoMetalImpactTest.h"
#if WITH_EDITOR
#include "Tests/DemoProjectileFixtures.h"
#include "AI/DemoEnemy.h"
#include "Animation/DemoEnemyPresentation.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h" // 演示录制可隐藏路线/命中数字，保留正常游戏默认 HUD。
#include "Player/DemoPlayerController.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Save/DemoRunSave.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Debug/DemoLog.h"

void ADemoMetalImpactTest::TickHitFxReel()
{
    DEMO_LOG_TICK();
    UWorld* World=GetWorld(); // 当前隔离 World；任何普通 PIE 都不得被夹具请求退出。
    if (World->WorldType!=EWorldType::Game || GetNetMode()!=NM_Standalone || !FParse::Param(FCommandLine::Get(),TEXT("game")))
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("HITFX_REEL rejected outside standalone Editor game"));
        SetActorTickEnabled(false);
        return;
    }
    const double Now=World->GetTimeSeconds(); // 以真实 World 帧而不是编码器墙钟驱动。
    if (Now>40.) { Check(false,TEXT("hit feedback recording timed out")); return; }
    if (Now<NextAt) return;
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 单人视口控制器。
    auto* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 当前实际手枪持有者。
    auto* Mode=World->GetAuthGameMode<AFPSDemoGameMode>(); // 真实关卡准备/提交。
    auto* State=World->GetGameState<ADemoGameState>(); // 等待实际 Combat，禁止直接改阶段。
    if (!PC || !Player || !Mode || !State) return;
    if (Step==0)
    {
        auto* Flow=PC->GetMenuFlow(); // 处理本机新手页，使录制中不遮住战斗。
        Flow->UpdateRealtime(0.f);
        if (Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) Flow->DispatchUIAction(EDemoUIAction::OverlayBack);
        auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 仅 Editor GI 的临时槽。
        if (!Check(Saves && Saves->CreateSlot(0) && Mode->StartRun(),TEXT("reel local slot and StartRun"))) return;
        Saves->ConsumePending();
        PC->OnRunReady();
        Advance(.4f);
        return;
    }
    if (Step==1)
    {
        auto* Build=UDemoRunBuildComponent::FindForPawn(Player); // 用正式核心解锁出发门禁。
        if (!Check(Build && (Build->HasSelectedCore() || Build->TrySelectCore(TEXT("B01"))),TEXT("reel core selected"))) return;
        Mode->StartNextLevel();
        Advance(.15f);
        return;
    }
    if (Step==2)
    {
        if (State->Phase!=EDemoPhase::Combat) return;
        for (TActorIterator<ADemoEnemy> It(World); It; ++It)
        {
            if (!It->IsAlive() || It->IsBoss()) continue;
            It->SetActorTickEnabled(false); // 停止追击以便固定机位；动画/血量/命中组件独立正常运行。
            if (!Target.IsValid() || It->GetDistanceTo(Player)<Target->GetDistanceTo(Player)) Target=*It;
        }
        if (!Target.IsValid()) return;
        auto* Mesh=Target->FindComponentByClass<USkeletalMeshComponent>(); // 生产网格的真实胸部及头部骨。
        if (!Check(Mesh && Mesh->DoesSocketExist(TEXT("core")),TEXT("reel visible chest"))) return;
        const FVector Chest=Mesh->GetSocketLocation(TEXT("core")); // 世界厘米，摄像机从玩家来向观察。
        const FVector Front=(Player->GetActorLocation()-Chest).GetSafeNormal2D(); // 镜头在胸前约三米。
        const FVector Eye=Chest+Front*310.f+FVector(0,0,30.f);
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->AddActorWorldOffset(Eye-Player->GetSharedViewLocation(),false,nullptr,ETeleportType::TeleportPhysics);
        Target->FindComponentByClass<UDemoEnemyPresentation>()->SetFacingIntent(Front);
        PC->SetViewTarget(Player);
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoHitFxCleanView")) && PC->GetHUD())
            PC->GetHUD()->bShowHUD=false; // 仅此显式录屏进程，不写玩家设置。
        PC->SetControlRotation((Chest-Eye).Rotation());
        Player->GetWeaponComponent()->EquipSlot(2);
        ReelDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HitFxReel")/FString::Printf(TEXT("%u"),FPlatformProcess::GetCurrentProcessId()));
        IFileManager::Get().MakeDirectory(*ReelDirectory,true);
        BeforeHealth=Target->GetHealth();
        UE_LOG(LogFPSDemo,Display,TEXT("HITFX_REEL target=%s hp=%.2f directory=%s"),*Target->GetName(),BeforeHealth,*ReelDirectory);
        Advance(.7f);
        return;
    }
    if (Step==3 && Target.IsValid() && Target->IsAlive())
    {
        auto* Mesh=Target->FindComponentByClass<USkeletalMeshComponent>(); // 实际骨骼随动画更新，先机身后头部。
        const FName Bone=ReelFrame<135 ? FName(TEXT("core")) : FName(TEXT("head"));
        PC->SetControlRotation((Mesh->GetSocketLocation(Bone)-Player->GetSharedViewLocation()).Rotation());
        if (ReelFrame==48 || (ReelFrame>=108 && (ReelFrame-108)%24==0))
        {
            UE_LOG(LogFPSDemo,Display,TEXT("HITFX_REEL fire frame=%d hp=%.2f bone=%s"),ReelFrame,Target->GetHealth(),*Bone.ToString());
            DemoHitZoneTest::Fire(Player);
        }
        if (ReelFrame>450) { Check(false,TEXT("reel failed to defeat target via actual shots")); return; }
    }
    else if (Step==3)
    {
        if (Target.IsValid()) Target->SetActorTickEnabled(true); // 权威死亡后重新启用原尸体更新。
        UE_LOG(LogFPSDemo,Display,TEXT("HITFX_REEL defeat frame=%d"),ReelFrame);
        ImpactAt=Now;
        Step=4;
    }
    if (Step==4 && Now-ImpactAt>1.7)
    {
        UE_LOG(LogFPSDemo,Display,TEXT("HITFX_REEL_END frames=%d"),ReelFrame);
        SetActorTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false,0);
        return;
    }
    const FString Path=ReelDirectory/FString::Printf(TEXT("Frame-%04d.png"),ReelFrame++);
    FScreenshotRequest::RequestScreenshot(Path,false,false); // 真正的渲染视口，不向画面重绘命中特效。
}
#endif
