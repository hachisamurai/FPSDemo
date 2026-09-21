#include "Tests/DemoUIValidation.h"
#include "UI/DemoHUD.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "Debug/DemoLog.h"
#include "AbilitySystemComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

void ADemoUIValidation::TickEnemyStatus(ADemoPlayerController* PC, AFPSDemoGameMode* Mode, ADemoCharacter* Player, ADemoHUD* HUD)
{
	DEMO_LOG_CALL();
	ADemoEnemy* Enemy = StatusEnemy.Get(); // 本步临时借用，死亡后不能继续调用；case0首次建立夹具。
	FVector2D Anchor; // 生产投影/可见性接口输出，不在测试中复刻绘制公式。
	switch (Step)
	{
	case 0:
		Mode->StartRun();
		PC->OnRunReady();
		Mode->StartNextLevel();
		Advance(11); // 出发会传送Pawn；至少等一帧让PlayerCameraManager更新，不能拿大厅旧相机摆放夹具。
		break;
	case 11:
		for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 冻结AI并移走其他怪，保持截图/视线可重复。
		{
			It->SetActorTickEnabled(false);
			It->SetActorLocation(It->GetActorLocation() + FVector(0, 0, 10000.f));
			if (!StatusEnemy.IsValid()) StatusEnemy = *It;
		}
		Enemy = StatusEnemy.Get();
		if (!Check(Enemy != nullptr, TEXT("enemy status test has real campaign enemy"))) return;
		{
			FVector ViewLocation; // 当前真实第一人称相机世界位置，厘米。
			PC->GetPlayerViewPoint(ViewLocation, StatusViewRotation);
			StatusEnemyLocation = ViewLocation + StatusViewRotation.Vector() * 650.f - FVector(0, 0, 80.f);
		}
		Enemy->SetActorLocation(StatusEnemyLocation);
		// 只在隔离测试里直接授予ASC标签，验证显示契约；不会伪造已完成的元素伤害/叠层实现。
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), Enemy->GetMaxHealth() * .5f);
		Enemy->GetAbilitySystemComponent()->AddLooseGameplayTag(DemoTags::Burn);
		Enemy->GetAbilitySystemComponent()->AddLooseGameplayTag(DemoTags::Frozen);
		UE_LOG(LogFPSDemo, Log, TEXT("ENEMY_STATUS_FIXTURE enemy=%s view=%s HP=%.1f/%.1f"), *StatusEnemyLocation.ToString(), *StatusViewRotation.ToString(), Enemy->GetHealth(), Enemy->GetMaxHealth());
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/EnemyStatus/00-Setup.png"), false, false); // 夹具初始画面用于诊断场景遮挡。
		Advance(1);
		break;
	case 1:
		if (!Check(Enemy && HUD->GetEnemyStatusScreenAnchor(Enemy, Anchor) && FMath::IsNearlyEqual(Enemy->GetHealth(), Enemy->GetMaxHealth() * .5f), TEXT("visible enemy with half-health bar and both GAS tags"))) return;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/EnemyStatus/01-Both.png"), false, false);
		Advance(2);
		break;
	case 2:
		// 同时改变摄像机Yaw/Pitch/Roll和敌人朝向，下一帧观察UI是否仍水平；不移动投影目标。
		PC->SetControlRotation(StatusViewRotation + FRotator(-6.f, 18.f, 20.f));
		Enemy->SetActorRotation(FRotator(20.f, 170.f, 35.f));
		Advance(3);
		break;
	case 3:
		if (!Check(Enemy && HUD->GetEnemyStatusScreenAnchor(Enemy, Anchor), TEXT("rotated camera and actor keep status projection visible"))) return;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/EnemyStatus/02-Rotated.png"), false, false);
		Advance(4);
		break;
	case 4:
		PC->SetControlRotation(StatusViewRotation);
		Enemy->GetAbilitySystemComponent()->RemoveLooseGameplayTag(DemoTags::Burn);
		Enemy->GetAbilitySystemComponent()->RemoveLooseGameplayTag(DemoTags::Frozen);
		Enemy->GetAbilitySystemComponent()->AddLooseGameplayTag(DemoTags::Chill);
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), Enemy->GetMaxHealth());
		Advance(5);
		break;
	case 5:
		if (!Check(Enemy && Enemy->GetHealth() == Enemy->GetMaxHealth() && !Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoTags::Burn), TEXT("healing fills health and removed burn does not persist"))) return;
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/EnemyStatus/03-Chill.png"), false, false);
		Advance(6);
		break;
	case 6:
		Enemy->GetAbilitySystemComponent()->RemoveLooseGameplayTag(DemoTags::Chill);
		Advance(7);
		break;
	case 7:
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/EnemyStatus/04-Clear.png"), false, false);
		Advance(8);
		break;
	case 8:
		Enemy->SetActorLocation(StatusEnemyLocation - StatusViewRotation.Vector() * 1300.f);
		if (!Check(!HUD->GetEnemyStatusScreenAnchor(Enemy, Anchor), TEXT("behind-camera enemy is culled"))) return;
		Enemy->SetActorLocation(StatusEnemyLocation + StatusViewRotation.Vector() * 10000.f);
		if (!Check(!HUD->GetEnemyStatusScreenAnchor(Enemy, Anchor), TEXT("distant enemy is culled"))) return;
		Enemy->SetActorLocation(StatusEnemyLocation);
		{
			AActor* Blocker = GetWorld()->SpawnActor<AActor>(); // 隔离场景的不可见碰撞盒，仅用于验证不透墙。
			StatusOccluder = Blocker;
			UBoxComponent* Box = NewObject<UBoxComponent>(Blocker); // Actor持有注册组件，随测试遮挡Actor销毁。
			Blocker->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(80.f));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Box->RegisterComponent();
			Blocker->SetActorLocation(StatusEnemyLocation - StatusViewRotation.Vector() * 300.f + FVector(0, 0, 40.f));
		}
		Advance(9);
		break;
	case 9:
		if (!Check(!HUD->GetEnemyStatusScreenAnchor(Enemy, Anchor), TEXT("visibility blocker hides enemy status"))) return;
		StatusOccluder->Destroy();
		Advance(10);
		break;
	case 10:
		if (!Check(HUD->GetEnemyStatusScreenAnchor(Enemy, Anchor), TEXT("status returns when occluder removed"))) return;
		Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), 0.f);
		if (!Check(!HUD->GetEnemyStatusScreenAnchor(Enemy, Anchor), TEXT("dead enemy hides status immediately before delayed destroy"))) return;
		UE_LOG(LogFPSDemo, Display, TEXT("DEMO_ENEMY_STATUS_SUCCESS: health, tag transitions, camera rotation, occlusion, distance, death"));
		FPlatformMisc::RequestExitWithStatus(false, 0);
		bFailed = true; // 正常退出已请求，阻止最后几帧重复操作死亡对象。
		break;
	}
}
