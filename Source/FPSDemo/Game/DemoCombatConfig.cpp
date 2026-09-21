#include "Game/DemoCombatConfig.h"
#include "Debug/DemoLog.h"

namespace
{
	/** Value 为配置数值；Min/Max 指定合法闭区间，同时拒绝 NaN/无穷大。 */
	bool InRange(float Value, float Min, float Max)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("%hs value=%f"), __FUNCTION__, Value);
		return FMath::IsFinite(Value) && Value >= Min && Value <= Max;
	}
}

FName DemoCombatConfig::DifficultyName(EDemoDifficulty Difficulty)
{
	UE_LOG(LogFPSDemo, VeryVerbose, TEXT("%hs difficulty=%d"), __FUNCTION__, static_cast<int32>(Difficulty));
	switch (Difficulty)
	{
	case EDemoDifficulty::Easy: return TEXT("Easy");
	case EDemoDifficulty::Normal: return TEXT("Normal");
	case EDemoDifficulty::Hard: return TEXT("Hard");
	default:
		UE_LOG(LogFPSDemo, Warning, TEXT("Invalid difficulty enum=%d"), static_cast<int32>(Difficulty));
		return NAME_None;
	}
}

FName DemoCombatConfig::LevelName(int32 Number)
{
	UE_LOG(LogFPSDemo, VeryVerbose, TEXT("%hs number=%d"), __FUNCTION__, Number);
	return FName(*FString::Printf(TEXT("Level%02d"), Number));
}

bool DemoCombatConfig::Validate(const UDataTable* Enemies, const UDataTable* Difficulties, const UDataTable* Levels, FString& Error)
{
	UE_LOG(LogFPSDemo, Log, TEXT("%hs"), __FUNCTION__);
	Error.Empty();
	if (!Enemies || !Difficulties || !Levels || Enemies->GetRowStruct() != FDemoEnemyRow::StaticStruct()
		|| Difficulties->GetRowStruct() != FDemoDifficultyRow::StaticStruct() || Levels->GetRowStruct() != FDemoLevelRow::StaticStruct())
	{
		Error = TEXT("Missing combat DataTable or incorrect row type; import Content/Data tables");
		UE_LOG(LogFPSDemo, Warning, TEXT("Config rejected: %s"), *Error);
		return false;
	}
	if (Difficulties->GetRowNames().Num() != 3 || Levels->GetRowNames().Num() != LevelCount)
	{
		Error = TEXT("Expected three difficulty rows and ten level rows");
		UE_LOG(LogFPSDemo, Warning, TEXT("Config rejected: %s"), *Error);
		return false;
	}
	// Name 是模板行键，Row 在本次校验期间由表持有，不保存裸指针。
	for (const FName Name : Enemies->GetRowNames())
	{
		const FDemoEnemyRow* Row = Enemies->FindRow<FDemoEnemyRow>(Name, TEXT("ValidateEnemy"));
		if (!Row || !InRange(Row->BaseHealth,1,100000) || !InRange(Row->BaseAttackPower,0,10000)
			|| !InRange(Row->MoveSpeed,1,1200) || !Row->Attack.IsValid() || !Row->Tactics.IsValid() || !Row->DiveSlam.IsValid()
			|| (Row->Tactics.bEnabled && Row->Tactics.MaximumRange > Row->Attack.ProjectileRange)) // 战术理想范围必须能实际发射。
		{
			Error = FString::Printf(TEXT("Invalid enemy row %s: health/attack/speed/tactics/dive; tactical maximum must fit projectile range"), *Name.ToString());
			UE_LOG(LogFPSDemo, Warning, TEXT("Config rejected: %s"), *Error);
			return false;
		}
	}
	// Index 只用于遍历稳定难度枚举，避免依赖 DataTable 的内部行顺序。
	for (int32 Index = 0; Index < 3; ++Index)
	{
		// Name 是枚举稳定行键；Row 只在本循环借用难度表持有的行。
		const FName Name = DifficultyName(static_cast<EDemoDifficulty>(Index));
		const FDemoDifficultyRow* Row = Difficulties->FindRow<FDemoDifficultyRow>(Name,TEXT("ValidateDifficulty"),false);
		if (!Row || !InRange(Row->HealthMultiplier,0.1f,10.f) || !InRange(Row->AttackMultiplier,0.1f,10.f)
			|| Row->VictoryGoldReward < 0 || Row->VictoryGoldReward > 1000000) // 缺迁移/负奖励拒绝出发，0金币配置合法。
		{
			Error = FString::Printf(TEXT("Invalid difficulty row %s: multipliers [0.1,10], VictoryGoldReward [0,1000000]; migrate reward table if -1"),*Name.ToString());
			UE_LOG(LogFPSDemo, Warning, TEXT("Config rejected: %s"), *Error);
			return false;
		}
	}
	// Number 是逻辑关卡序号，与 ArenaIndex 分离，胜利只能在第十关触发。
	for (int32 Number = 1; Number <= LevelCount; ++Number)
	{
		// Row 只用于完整性校验，MonsterLevel 强制与 Number 相同，杜绝跨级掉落。
		const FDemoLevelRow* Row = Levels->FindRow<FDemoLevelRow>(LevelName(Number),TEXT("ValidateLevel"),false);
		if (!Row || Row->MonsterLevel != Number || Row->ArenaIndex < 0 || Row->ArenaIndex > 2 || Row->EnemyCount < 1 || Row->EnemyCount > 30
			|| Row->EnemyCoinReward < 0 || Row->EnemyCoinReward > 10000 || Row->BossCoinReward < 0 || Row->BossCoinReward > 10000
			|| !InRange(Row->HealthMultiplier,0.1f,10.f) || !InRange(Row->AttackMultiplier,0.1f,10.f))
		{
			Error = FString::Printf(TEXT("Invalid Level%02d: missing row or invalid monster level/arena/count/multipliers/coins"),Number);
			UE_LOG(LogFPSDemo, Warning, TEXT("Config rejected: %s"), *Error);
			return false;
		}
		// 普通怪/Boss 引用必须和行为类型一致，错误配置不能生成一半才失败。
		const FDemoEnemyRow* Minion = Enemies->FindRow<FDemoEnemyRow>(Row->EnemyRow,TEXT("ValidateMinionRef"),false);
		const FDemoEnemyRow* Boss = Row->BossRow.IsNone() ? nullptr : Enemies->FindRow<FDemoEnemyRow>(Row->BossRow,TEXT("ValidateBossRef"),false);
		if (!Minion || Minion->bBoss || (!Row->BossRow.IsNone() && (!Boss || !Boss->bBoss)))
		{
			Error = FString::Printf(TEXT("Invalid Level%02d: enemy/Boss reference missing or wrong type"),Number);
			UE_LOG(LogFPSDemo, Warning, TEXT("Config rejected: %s"), *Error);
			return false;
		}
	}
	return true;
}

FDemoEnemySpawnStats DemoCombatConfig::Resolve(const FDemoEnemyRow& Enemy, const FDemoDifficultyRow& Difficulty, const FDemoLevelRow& Level)
{
	UE_LOG(LogFPSDemo, Log, TEXT("%hs HP=%.2f*%.2f*%.2f ATK=%.2f*%.2f*%.2f"), __FUNCTION__,
		Enemy.BaseHealth,Level.HealthMultiplier,Difficulty.HealthMultiplier,Enemy.BaseAttackPower,Level.AttackMultiplier,Difficulty.AttackMultiplier);
	// Stats 以值返回，将难度在生成时冻结；GAS 初始化后运行时仍统一经属性结算。
	FDemoEnemySpawnStats Stats;
	// 先得出等级基础值，再施加本局难度；中间不取整，保留 GAS 浮点精度。
	const float LevelHealth = Enemy.BaseHealth * Level.HealthMultiplier;
	const float LevelAttack = Enemy.BaseAttackPower * Level.AttackMultiplier;
	Stats.Health = FMath::Max(1.f, LevelHealth * Difficulty.HealthMultiplier);
	Stats.AttackPower = LevelAttack * Difficulty.AttackMultiplier;
	Stats.MoveSpeed = Enemy.MoveSpeed;
	// 攻击频率不随难度倍率重复缩放，用户直接在怪物模板中调节。
	Stats.Attack = Enemy.Attack;
	Stats.Tactics = Enemy.Tactics; // 配置行为不受难度生命/伤害倍率再次放大。
	Stats.DiveSlam=Enemy.DiveSlam; Stats.bUseDive=Enemy.bBoss&&Enemy.DiveSlam.bEnabled; // 只给Boss授予俯冲。
	Stats.bUseTactics = Enemy.Tactics.bEnabled;
	Stats.bBoss = Enemy.bBoss;
	Stats.CoinReward = Enemy.bBoss ? Level.BossCoinReward : Level.EnemyCoinReward;
	Stats.MonsterLevel = Level.MonsterLevel;
	UE_LOG(LogFPSDemo, Log, TEXT("Resolved Lv%d boss=%d HP=%.2f ATK=%.2f silver=%d"), Stats.MonsterLevel, Stats.bBoss, Stats.Health, Stats.AttackPower, Stats.CoinReward);
	return Stats;
}
