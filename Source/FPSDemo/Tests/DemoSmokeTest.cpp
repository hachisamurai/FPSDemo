#include "Tests/DemoSmokeTest.h"
#include "Tests/DemoProjectileFixtures.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "UI/DemoHUD.h"
#include "AI/DemoEnemy.h"
#include "Interaction/DemoInteractable.h"
#include "Game/FPSDemoGameMode.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoGameplayAbility.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/AudioComponent.h"
#include "UObject/UObjectIterator.h"

int32 ADemoSmokeTest::CountWeaponVoices(FName EventName) const
{
	DEMO_LOG_CALL();
	// 只统计本测试 World 的瞬时播放组件，排除 Editor/其他世界和未播放组件。
	int32 Count = 0;
	// It 借用全局 UObject 迭代器；不保存音频组件裸指针，不延长其自动销毁生命周期。
	for (TObjectIterator<UAudioComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->ComponentHasTag(EventName) && It->IsPlaying()) ++Count;
	}
	return Count;
}

namespace
{
	// 专用进程跨 OpenLevel：0 通关，1 困难自然死亡，2 安全区重开/异常销毁，3 掉出世界，4 最终安全区检查。
	int32 SmokeRun = 0;
}
ADemoSmokeTest::ADemoSmokeTest()
{
	DEMO_LOG_CALL();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
}
bool ADemoSmokeTest::Check(bool bCondition, const TCHAR* Message)
{
	DEMO_LOG_CALL();
	UE_LOG(LogFPSDemo, Display, TEXT("SMOKE %s: %s"), bCondition ? TEXT("PASS") : TEXT("FAIL"), Message);
	if (!bCondition) { bFailed = true; FPlatformMisc::RequestExitWithStatus(false,1); }
	return bCondition;
}
void ADemoSmokeTest::Advance(int32 Next, float Delay)
{
	DEMO_LOG_CALL();
	Step = Next;
	NextStepTime = GetWorld()->GetTimeSeconds() + Delay;
}
void ADemoSmokeTest::Capture(const FString& Name)
{
	DEMO_LOG_CALL();
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoCapture")))
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/Demo")/Name+TEXT(".png"),false,false);
}
void ADemoSmokeTest::ClearCurrentLevel()
{
	DEMO_LOG_CALL();
	// 同步遍历期间死亡只 SetLifeSpan，不会立即使迭代器失效。
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
	AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive()) continue;
		DemoEffects::Apply(Player->GetDemoASC(), It->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -10000.f);
		// 故意重复上报死亡，确认服务器集合确实阻止重复计奖。
		const int32 SilverAfterDeath = State->SilverCoins; // 捕获真实击杀钱包，重复回调不能增加银币。
		const int32 GoldAfterDeath = State->Coins; // 击杀本身不能发清关金币，清关在下一帧结算。
		Mode->NotifyEnemyKilled(*It);
		if (!Check(State->SilverCoins == SilverAfterDeath && State->Coins == GoldAfterDeath,TEXT("duplicate kill does not award either currency"))) return;
	}
}
void ADemoSmokeTest::Tick(float DeltaSeconds)
{
	DEMO_LOG_TICK();
	Super::Tick(DeltaSeconds);
	if (bFailed) return;
	if (GetWorld()->GetTimeSeconds() > 80.f) { Check(false,TEXT("world test timeout")); return; }
	if (GetWorld()->GetTimeSeconds() < NextStepTime) return;
	// 每步骤重新取当前 World 对象，OpenLevel 后禁止继续使用前一局引用。
	AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
	ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
	ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
	ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0));
	if (!Check(Mode && State && Player && PC && Player->GetDemoASC() && Player->GetDemoAttributes(),TEXT("runtime objects initialized"))) return;
	UDemoAbilitySystemComponent* ASC = Player->GetDemoASC();
	// 本步读取当前装备；默认手枪基础伤害由BP维护，终端伤害升级独立加5，弹药由武器实例持有。
	ADemoWeaponBase* Weapon = Player->GetWeaponComponent()->GetActiveWeapon();
	if (!Check(Weapon != nullptr, TEXT("weapon loadout initialized"))) return;
	const float AuthoredDamage=Weapon->GetClass()->GetDefaultObject<ADemoWeaponBase>()->Config.BaseDamage; // 当前真实手枪资产的基线，不能将旧20伤害固定进成长/重开断言。
	const UDemoAttributeSet* Attributes = Player->GetDemoAttributes();
	if (SmokeRun > 0)
	{
		if (Step == 0)
		{
			// 实际死亡的两个重载直达安全区；系统错误回大厅，胜利先验证Hub再由测试显式旅行准备独立夹具。
			const bool bDeathReturn = SmokeRun == 2 || SmokeRun == 4;
			if (!Check(State->Phase == (bDeathReturn ? EDemoPhase::Hub : EDemoPhase::Lobby) && State->LevelNumber == 0 && State->Coins == 0 && State->SilverCoins == 0 && State->TotalKills == 0
				&& State->Purchases == 0 && Attributes->GetHealth() == 100.f && FMath::IsNearlyEqual(Weapon->GetDamagePerPellet(),AuthoredDamage,.01f)
				&& Weapon->GetAmmo() == 12.f && Weapon->GetCapacity() == 12.f && Attributes->GetMaxHealth() == 100.f
				&& Player->GetHealAmount() == 35.f && !ASC->HasMatchingGameplayTag(DemoTags::Dead),TEXT("OpenLevel clears growth/silver/death tag but preserves earned gold"))) return;
			if (bDeathReturn && !Check(State->Difficulty == EDemoDifficulty::Hard && !PC->IsMoveInputIgnored() && !PC->bShowMouseCursor
				&& Mode->GetShopTerminal() && Mode->GetNextLevelTerminal() && FVector::Dist2D(Player->GetActorLocation(),Mode->GetAreaCenter(-1)) < 1000.f,
				TEXT("death return preserves Hard difficulty and restores safe-hub input/terminals"))) return;
			if (SmokeRun == 4)
			{
				UE_LOG(LogFPSDemo, Display, TEXT("DEMO_SMOKE_SUCCESS: GAS, ten levels, arena shop, player death/fall to fresh safe hub preserving Hard, system-error lobby restart"));
				FPlatformMisc::RequestExitWithStatus(false,0);
				SetActorTickEnabled(false);
				return;
			}
			if (!bDeathReturn) { Mode->StartRun(); PC->OnRunReady(); Mode->SelectDifficulty(EDemoDifficulty::Hard); } // 隔离GAS/战役专项，存档菜单另行验证；难度在初始Hub选择。
			Mode->StartNextLevel();
			if (SmokeRun == 3)
			{
				// 引擎掉出世界入口会禁用移动；重开必须恢复新 Pawn 的移动和无死亡标签。
				Player->FellOutOfWorld(*GetDefault<UDamageType>());
			}
			else if (SmokeRun == 2)
			{
				// 异常移除活怪必须失败，不能伪装成击杀奖励或永久卡关。
				TActorIterator<ADemoEnemy> It(GetWorld());
				if (It) It->Destroy();
			}
			else
			{
				// 死亡前先实际获得奖励和升级；不能仅在默认属性上声称已验证“清空成长”。
				ClearCurrentLevel();
				Advance(2,0.3f);
				return;
			}
			Advance(1,SmokeRun == 1 ? 5.f : 0.2f);
			return;
		}
		if (SmokeRun == 1 && Step == 2)
		{
			if (!Check(State->Phase == EDemoPhase::Reward && State->Coins == 0, TEXT("first-level clear earns silver but no gold"))) return;
			PC->SelectUpgrade(1); // 免费奖励改变 Pawn 的治疗升级，验证它不会跨死亡保留。
			Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
			Mode->GetShopTerminal()->Interact(Player);
			PC->SelectUpgrade(0); // 真实购买改变GAS伤害，留下30银币供死亡清空验证。
			PC->CloseUpgradeMenu();
			if (!Check(State->Coins == 0 && State->SilverCoins == 30 && FMath::IsNearlyEqual(Weapon->GetDamagePerPellet(),AuthoredDamage+5.f,.01f) && Player->GetHealAmount() == 55.f, TEXT("death-reset setup has coins and both GAS/Pawn upgrades"))) return;
			Mode->StartNextLevel();
			// 真实 Tick/GE 杀死第二关玩家；各怪分散，避免相互遮挡近战视线。
			int32 EnemyIndex = 0; // 活怪环形槽位。
			for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It)
			{
				if (!It->IsAlive()) continue;
				const float Angle = 2.f*PI*EnemyIndex++ / State->EnemiesRemaining; // 弧度，用本关配置人数均分。
				It->SetActorLocation(Player->GetActorLocation()+FVector(160*FMath::Cos(Angle),160*FMath::Sin(Angle),0),false,nullptr,ETeleportType::TeleportPhysics);
			}
			Advance(1,5.f);
			return;
		}
		if (State->Phase != EDemoPhase::Defeat) { NextStepTime = GetWorld()->GetTimeSeconds()+0.5f; return; }
		if (!Check(State->Coins == 0 && State->SilverCoins == 0 && !Mode->GetShopTerminal() && !Mode->GetNextLevelTerminal()
			&& !ASC->ActivateDemoAbility(UDemoFireAbility::StaticClass()) && State->bReturnToHubOnRestart == (SmokeRun != 2),TEXT("defeat blocks skills/terminals and distinguishes actual death"))) return;
		++SmokeRun;
		Mode->RestartDemo();
		SetActorTickEnabled(false);
		return;
	}
	switch (Step)
	{
	case 0:
		if (!Check(State->Phase == EDemoPhase::Lobby && PC->bShowMouseCursor && PC->IsMoveInputIgnored(), TEXT("startup waits in lobby"))) return;
		Mode->StartRun(); // GAS用例直接建立隔离新局，存档菜单由Session专项覆盖；普通首关基础60HP。
		PC->OnRunReady();
		ExpectedCampaignKills=0;
		for (int32 Number=1;Number<=DemoCombatConfig::LevelCount;++Number) // 基于正式配置验证整轮总计，配置调整不能遗留旧Boss数量预期。
		{
			FDemoLevelRow Level; FDemoDifficultyRow Difficulty; FString Error; // 同步只读行副本及失败原因，不更改资产或规则。
			if (!Check(Mode->GetLevelConfig(Number,Level,Difficulty,Error),TEXT("campaign expectation reads validated level configuration"))) return;
			ExpectedCampaignKills+=Level.EnemyCount+(Level.BossRow.IsNone()?0:1); ExpectedVictoryGold=Difficulty.VictoryGoldReward;
		}
		if (!Check(State->Phase == EDemoPhase::Hub && State->LevelNumber == 0 && State->EnemiesRemaining == 0 && Attributes->GetHealth() == 100.f,TEXT("safe hub starts without enemies or damage"))) return;
		if (!Check(ASC->GetOwnerActor() == Player->GetPlayerState() && ASC->GetAvatarActor() == Player && ASC->GetActivatableAbilities().Num() == 5,TEXT("PlayerState owns ASC, Pawn is Avatar, five abilities granted including aim"))) return;
		if (!Check(!ASC->ActivateDemoAbility(UDemoFireAbility::StaticClass()) && Weapon->GetAmmo() == 12.f,TEXT("safe hub rejects combat without spending ammo"))) return;
		Capture(TEXT("01-SafeHub"));
		Advance(12,0.3f);
		break;
	case 12:
		// 截图必须留出独立帧，避免请求安全区截图后同帧已传送进战斗区。
		Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
		Mode->GetShopTerminal()->Interact(Player);
		if (!Check(PC->IsUpgradeMenuOpen() && !Mode->PurchaseUpgrade(0,Player) && State->Coins == 0,TEXT("terminal opens UI; insufficient coins rejected"))) return;
		PC->CloseUpgradeMenu();
		Mode->StartNextLevel();
		if (!Check(State->EnemiesRemaining == 5,TEXT("sector one contains five minions"))) return;
		// 首个用例固定敌人，让实体弹有真实飞行时间；自然AI在第二局单独验证。
		for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) { It->SetActorTickEnabled(false); if (!ShotTarget.IsValid()) ShotTarget = *It; }
		ShotTarget->SetActorLocation(Player->GetActorLocation()+FVector(500,0,60));
		PC->SetControlRotation(FRotator::ZeroRotator);
		Advance(1,0.3f);
		break;
	case 1:
		ShotHealthBefore=ShotTarget->GetHealth(); // 保存伤前值，提交调用栈内必须仍未造成命中。
		if (!Check(ASC->ActivateDemoAbility(UDemoFireAbility::StaticClass()),TEXT("fire ability commits"))) return;
		if (!Check(Weapon->GetAmmo()==11.f && ShotTarget->GetHealth()==ShotHealthBefore,TEXT("fire consumes one round before projectile arrival without instant damage"))) return;
		if (!Check(!ASC->ActivateDemoAbility(UDemoFireAbility::StaticClass()) && Weapon->GetAmmo()==11.f,TEXT("fire cooldown blocks immediate repeat without extra cost"))) return;
		ShotDeadline=GetWorld()->GetTimeSeconds()+1.5f; Advance(13,.02f); break;
	case 13:
		if (DemoProjectileFixtures::CountLive(GetWorld())>0)
		{
			if (!Check(GetWorld()->GetTimeSeconds()<ShotDeadline,TEXT("smoke projectile bounded flight"))) return;
			return;
		}
		// 等到真实碰撞后验证当前武器值；资产数值调优不会被旧默认20硬编码误判。
		if (!Check(Weapon->GetAmmo()==11.f && FMath::IsNearlyEqual(ShotTarget->GetHealth(),ShotHealthBefore-Weapon->GetDamagePerPellet(),.02f),TEXT("arrived projectile deals one GAS hit and spends exactly one round"))) return;
		// 显式音频验证需要真实 AudioDevice（不要传 -nosound）；不仅检查“调用过播放函数”。
		if (FParse::Param(FCommandLine::Get(), TEXT("DemoAudioValidation")))
		{
			if (!Check(CountWeaponVoices(TEXT("Weapon.Fire")) == 1 && CountWeaponVoices(TEXT("Weapon.Hit")) == 1,
				TEXT("audio: committed shot and actual damage create two playing spatial cues"))) return;
			DemoEffects::Apply(ASC,ShotTarget->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),5.f);
			DemoEffects::Apply(ASC,ShotTarget->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),0.f);
			if (!Check(CountWeaponVoices(TEXT("Weapon.Hit")) == 1,TEXT("audio: healing and zero damage do not create hit voices"))) return;
			// 新生成的低生命对象在听者附近配置，初始化不能被当成伤害；不更改已注册怪物奖励。
			FDemoEnemySpawnStats AudioStats; // 临时实例快照，不注册进关卡，不发金币。
			AudioStats.Health = 25.f;
			ADemoEnemy* AudioProbe = GetWorld()->SpawnActor<ADemoEnemy>(Player->GetActorLocation() + FVector(0,300,0), FRotator::ZeroRotator); // World 持有。
			if (!Check(AudioProbe != nullptr, TEXT("audio initialization probe spawned"))) return;
			AudioProbe->Configure(AudioStats);
			AudioProbe->Destroy();
			if (!Check(CountWeaponVoices(TEXT("Weapon.Hit")) == 1,TEXT("audio: lower initial health configuration does not play hit cue"))) return;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("DemoAudioValidation"))
			&& !Check(CountWeaponVoices(TEXT("Weapon.Fire")) == 1,TEXT("audio: rejected shot creates no extra fire voice"))) return;
		DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),-40.f);
		if (!Check(ASC->ActivateDemoAbility(UDemoHealAbility::StaticClass()) && Attributes->GetHealth() == 95.f
			&& !ASC->ActivateDemoAbility(UDemoHealAbility::StaticClass()),TEXT("heal GE restores 35 and cooldown blocks repeat"))) return;
		if (!Check(ASC->ActivateDemoAbility(UDemoDashAbility::StaticClass()) && ASC->GetCooldownRemaining(DemoTags::DashCooldown) > 3.f
			&& !ASC->ActivateDemoAbility(UDemoDashAbility::StaticClass()),TEXT("dash owns independent cooldown"))) return;
		Player->GetCharacterMovement()->StopMovementImmediately();
		if (!Check(ASC->ActivateDemoAbility(UDemoReloadAbility::StaticClass()) && ASC->HasMatchingGameplayTag(DemoTags::Reloading)
			&& !ASC->ActivateDemoAbility(UDemoFireAbility::StaticClass()),TEXT("reload task owns blocking tag"))) return;
		Capture(TEXT("02-Combat"));
		Advance(2,1.7f);
		break;
	case 2:
		if (FParse::Param(FCommandLine::Get(), TEXT("DemoAudioValidation"))
			&& !Check(CountWeaponVoices(TEXT("Weapon.Fire")) == 0 && CountWeaponVoices(TEXT("Weapon.Hit")) == 0,
				TEXT("audio: short one-shot voices finish and do not loop"))) return;
		if (!Check(Weapon->GetAmmo() == 12.f && !ASC->HasMatchingGameplayTag(DemoTags::Reloading),TEXT("real WaitDelay completes reload and clears tag"))) return;
		Weapon->ModifyAmmo(-100); // 通过受控实例接口清空弹匣，验证GAS拒绝成本。
		if (!Check(Weapon->GetAmmo() == 0.f && !ASC->ActivateDemoAbility(UDemoFireAbility::StaticClass()),TEXT("ammo clamps at zero and cost rejects shot"))) return;
		ASC->ActivateDemoAbility(UDemoReloadAbility::StaticClass());
		ASC->CancelAllAbilities();
		Advance(3,1.7f);
		break;
	case 3:
		if (!Check(Weapon->GetAmmo() == 0.f && !ASC->HasMatchingGameplayTag(DemoTags::Reloading),TEXT("cancelled reload never grants delayed ammo"))) return;
		ClearCurrentLevel();
		Advance(4,0.3f);
		break;
	case 4:
		if (!Check(State->Phase == EDemoPhase::Reward && State->Coins == 0 && PC->IsRewardMenu(),TEXT("first clear awards no gold and one reward menu"))) return;
		Capture(TEXT("03-Reward"));
		Advance(5,0.3f);
		break;
	case 5:
		PC->GetHUD<ADemoHUD>()->NotifyHitBoxClick(TEXT("Upgrade1"));
		if (!Check(State->Phase == EDemoPhase::Intermission && Player->GetHealAmount() == 55.f && !Mode->ChooseReward(0),TEXT("HUD reward remains in cleared arena and blocks duplicates"))) return;
		Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
		Mode->GetShopTerminal()->Interact(Player);
		Capture(TEXT("04-Shop"));
		Advance(6,0.3f);
		break;
	case 6:
		// 移出有效距离，即便菜单仍打开且钱足够也不能购买。
		Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-1000,0,100)); // 仍在本关范围内，只离开终端购买距离。
		if (!Check(!Mode->PurchaseUpgrade(0,Player) && State->Coins == 0,TEXT("shop enforces distance at transaction time"))) return;
		Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(-180,0,20));
		PC->GetHUD<ADemoHUD>()->NotifyHitBoxClick(TEXT("Upgrade0"));
		if (!Check(State->Coins == 0 && State->SilverCoins == 30 && FMath::IsNearlyEqual(Weapon->GetDamagePerPellet(),AuthoredDamage+5.f,.01f),TEXT("first shop purchase costs 20 and adds 5 damage"))) return;
		PC->SelectUpgrade(2);
		if (!Check(State->Coins == 0 && State->SilverCoins == 10 && Weapon->GetCapacity() == 16.f && !Mode->PurchaseUpgrade(1,Player),TEXT("different attribute still costs 20, capacity increases, overspend rejected"))) return;
		PC->CloseUpgradeMenu();
		Mode->StartNextLevel();
		if (!Check(State->LevelNumber == 2 && State->EnemiesRemaining == 6,TEXT("level two has six level-2 minions"))) return;
		ClearCurrentLevel();
		Advance(7,0.3f);
		break;
	case 7:
		if (!Check(State->Phase == EDemoPhase::Reward && State->Coins == 0 && State->SilverCoins == 82,TEXT("second clear grants six times level-2 coin reward"))) return;
		PC->SelectUpgrade(2);
		Mode->StartNextLevel();
		Advance(14,0.3f);
		break;
	case 14:
		// 按真实配置推进第3..10关；第10关留下Boss验证不能提前结束，第5关只有混编小怪。
		if (!Check(State->Phase == EDemoPhase::Combat, TEXT("remaining campaign level starts combat"))) return;
		if (State->LevelNumber != 10) { ClearCurrentLevel(); Advance(15,0.3f); break; } // 第5关也是普通清关，只第10关等待独立Boss存活断言。
		// 仅击杀小怪，验证 Boss 活着时不会提前胜利。
		for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It)
		{
			if (It->IsBoss()) { ShotTarget = *It; It->SetActorTickEnabled(false); }
			else if (It->IsAlive()) DemoEffects::Apply(ASC,It->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10000.f);
		}
		Advance(8,0.3f);
		break;
	case 8:
		if (!Check(State->Phase == EDemoPhase::Combat && State->EnemiesRemaining == 1 && ShotTarget.IsValid() && ShotTarget->GetMonsterLevel() == State->LevelNumber,TEXT("level-10 Boss alive prevents early clear"))) return;
		Capture(TEXT("05-Boss"));
		Advance(9,0.3f);
		break;
	case 9:
		ClearCurrentLevel();
		Advance(15,0.3f);
		break;
	case 15:
		if (State->LevelNumber == DemoCombatConfig::LevelCount) { Advance(10); break; }
		if (!Check(State->Phase == EDemoPhase::Reward, TEXT("each of first nine clears offers one reward"))) return;
		PC->SelectUpgrade(0);
		Mode->StartNextLevel();
		Advance(14,0.3f);
		break;
	case 10:
		// 金币仅整轮通关按难度配置发放；第10关唯一Boss计入总数，银币与临时成长在最终胜利清空。
		if (!Check(State->Phase == EDemoPhase::Victory && State->TotalKills == ExpectedCampaignKills && State->Coins == ExpectedVictoryGold && State->SilverCoins == 0 && !Mode->HasRunUpgrades(),TEXT("ten levels: configured victory gold, single final Boss and exact kill count"))) return;
		if (!Check(!ASC->ActivateDemoAbility(UDemoFireAbility::StaticClass()),TEXT("victory blocks combat"))) return;
		Capture(TEXT("06-Victory"));
		Advance(11,0.3f);
		break;
	case 11:
		Mode->RestartDemo();
		if (!Check(State->Phase==EDemoPhase::Hub && State->Coins==ExpectedVictoryGold && State->SilverCoins==0 && State->TotalKills==ExpectedCampaignKills && !PC->IsMoveInputIgnored(),TEXT("victory continues in safe hub without losing economy"))) return;
		++SmokeRun;
		Mode->TravelToRun(false); // 后续死亡用例需要独立默认角色，显式回大厅建立测试夹具，不再依赖旧胜利重载行为。
		SetActorTickEnabled(false);
		break;
	default: Check(false,TEXT("invalid smoke step")); break;
	}
}
