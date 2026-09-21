#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AI/DemoEnemyAttackSettings.h"
#include "AI/DemoEnemyTactics.h"
#include "AI/DemoBossDiveSettings.h"
#include "AI/DemoCloseCombat.h"
#include "DemoCombatConfig.generated.h"

/** 难度枚举稳定映射 Easy/Normal/Hard/Hell 行名；不以 UI 显示文本作为配置键。 */
UENUM(BlueprintType)
enum class EDemoDifficulty : uint8 { Easy, Normal, Hard, Hell }; // 追加枚举以保持旧档0..2含义。

/** 怪物模板，基础属性不包含关卡成长或全局难度，避免重复相乘。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoEnemyRow : public FTableRowBase
{
	GENERATED_BODY()
	// 生命点数，合法范围 [1,100000]；生成时写入 GAS MaxHealth/Health。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy", meta=(ClampMin="1")) float BaseHealth = 60.f;
	// 单次攻击基础点数 [0,10000]，倍率计算后写入 GAS AttackPower。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy", meta=(ClampMin="0")) float BaseAttackPower = 9.f;
	// 水平移动速度 cm/s，范围 [1,1200]；难度不改变速度。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy") float MoveSpeed = 235.f;
	// 是否启用大体积和范围预警攻击；关卡 BossRow 必须引用此值为 true 的行。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy") bool bBoss = false;
	// 独立攻击时钟与弹速/减速配置；出生时冻结，不受关卡或难度伤害倍率影响。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy") FDemoEnemyAttackSettings Attack;
	// 角色/拉距/有限预判/Boss二阶段参数，与生命伤害倍率独立，出生时冻结。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy") FDemoEnemyTacticsSettings Tactics;
	// Boss独立升空俯冲配置，不改变既有全图与地面技能；小怪模板应关闭。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy") FDemoBossDiveSettings DiveSlam;
	// Melee/Charger共用前摇与冲刺参数，只有对应Tactics角色使用。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy") FDemoCloseCombatSettings CloseCombat;
};

/** 全局难度倍率表，四个固定行名 Easy、Normal、Hard、Hell。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoDifficultyRow : public FTableRowBase
{
	GENERATED_BODY()
	// 攻击欲望倍率[0.25,4]；间隔除以此值，预警/技能持续时间不缩短，旧三档默认1。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Difficulty", meta=(ClampMin="0.25", ClampMax="4")) float AttackFrequencyMultiplier = 1.f;
	// 无量纲血量倍率，范围 [0.1,10]；普通默认为 1。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Difficulty") float HealthMultiplier = 1.f;
	// 无量纲攻击倍率，范围 [0.1,10]；与血量倍率独立调整。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Difficulty") float AttackMultiplier = 1.f;
	// 完成整轮十关才发一次金币，合法0..1000000；-1标记旧资产尚未迁移，拒绝静默套用错误奖励。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rewards", meta=(ClampMin="0", ClampMax="1000000")) int32 VictoryGoldReward = -1;
};

/** 无尽关卡由公式生成；小怪基数来自Level01，Boss模板/银币基数来自Level10，不要求无限张表行。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoEndlessRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Endless", meta=(ClampMin="1", ClampMax="3")) float HealthGrowth = 1.18f; // 每关生命乘数，第一关指数0。
	UPROPERTY(EditAnywhere, Category="Endless", meta=(ClampMin="1", ClampMax="3")) float DamageGrowth = 1.10f; // 每关基础伤害乘数，不修改固定伤害技能。
	UPROPERTY(EditAnywhere, Category="Endless", meta=(ClampMin="1", ClampMax="3")) float CountGrowth = 1.12f; // 基础小怪数量乘此值的关数次幂，最终四舍五入。
	UPROPERTY(EditAnywhere, Category="Endless", meta=(ClampMin="1", ClampMax="3")) float SilverGrowth = 1.12f; // 每只怪物银币掉落乘数，最终四舍五入。
	UPROPERTY(EditAnywhere, Category="Endless", meta=(ClampMin="4", ClampMax="16")) int32 MaxAlive = 12; // 至少4个位置供Boss与三种小怪同时存在，其余敌人排队补充。
};

/** 每关一个配置行 Level01..Level10；逻辑关卡数量和物理白模区域数量分开。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoLevelRow : public FTableRowBase
{
	GENERATED_BODY()
	// 怪物等级 1..10，必须等于 LevelXX 的关卡号；用作实例标识和掉落配置索引。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level") int32 MonsterLevel = 1;
	// 引用现有白模区域 0..2，不是逻辑关卡编号；允许复用布局。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level") int32 ArenaIndex = 0;
	// 小怪数值模板，必须bBoss=false；实际兵种由关号随机池指定，保留该模板的攻击参数。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level") FName EnemyRow = TEXT("Drone");
	// 本关小怪数量 [1,30]；另可增加一只 Boss。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level") int32 EnemyCount = 5;
	// 十关战役只允许Level10配置Boss模板，其他行必须None；无尽每十关复用此模板。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level") FName BossRow = NAME_None;
	// 本关基础生命成长倍率 [0.1,10]；与选择的难度相乘，小怪/Boss 共用。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level") float HealthMultiplier = 1.f;
	// 本关基础攻击成长倍率 [0.1,10]，不改攻击频率。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level") float AttackMultiplier = 1.f;
	// 该等级每只小怪击杀银币[0,10000]；保留旧字段名兼容已制作DataTable，不受难度倍率影响。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level", meta=(DisplayName="Enemy Silver Reward")) int32 EnemyCoinReward = 10;
	// 每只Boss击杀银币[0,10000]；保留旧序列化键，缺Boss时不用。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Level", meta=(DisplayName="Boss Silver Reward")) int32 BossCoinReward = 50;
};

/** 生成前冻结的值对象，不持有 DataTable 行指针；怪物活着期间不会受换难度影响。 */
struct FPSDEMO_API FDemoEnemySpawnStats
{
	FDemoCloseCombatSettings CloseCombat; // 生产Resolve冻结冲刺冷却难度修正值，其余前摇不缩短。
	FDemoBossDiveSettings DiveSlam; // 新技能配置值快照，不持有表引用。
	bool bUseDive=false; // 生产Resolve按Boss模板开启；旧直接生成测试不自动获得新技能。
	FDemoEnemyTacticsSettings Tactics; // 模板战术值快照，不保存DataTable引用。
	bool bUseTactics = false; // Resolve生产入口启用；旧直接生成调用者保持既有攻击用例语义。
	int32 FormationSlot = 0; // 刷怪组件在逐怪生成时填写的非负序号，用于混编与左右分工。
	// 来源模板的攻击快照，不持有数据表引用，保留默认值兼容旧调用者。
	FDemoEnemyAttackSettings Attack;
	// 最终生命/攻击点数，配置校验和倍率计算完成后交给怪物。
	float Health = 1.f;
	float AttackPower = 0.f;
	// 模板基础移动速度cm/s；运行时另乘战术角色/二阶段及GAS移速，不乘关卡/难度。
	float MoveSpeed = 235.f;
	// 行为类型及单次银币奖励，生命周期为当前敌人实例；CoinReward保留旧接口名兼容调用者。
	bool bBoss = false;
	int32 CoinReward = 0;
	// 本实例等级，来自生成关卡配置；死亡时不再查询可能已修改的表。
	int32 MonsterLevel = 1;
};

/** 公共配置校验/解析，不拥有输入 DataTable，所有功能入口记录调用。 */
namespace DemoCombatConfig
{
	// 当前战役必须包含的关卡数；配置表行名连续，避免静默漏关。
	constexpr int32 LevelCount = 10;
	/** Difficulty 为稳定枚举；非法枚举返回 None，调用者必须拒绝。 */
	FPSDEMO_API FName DifficultyName(EDemoDifficulty Difficulty);
	/** Number 为 1..10 的逻辑关卡号，返回固定配置键。 */
	FPSDEMO_API FName LevelName(int32 Number);
	/** 三张表为借用引用；Error 返回具体字段/行错误。完全校验成功才允许开始一局。 */
	FPSDEMO_API bool Validate(const UDataTable* Enemies, const UDataTable* Difficulties, const UDataTable* Levels, FString& Error);
	/** Enemy/Difficulty/Level 为已校验行快照；返回最终值，不更改原表或共享模板。 */
	FPSDEMO_API FDemoEnemySpawnStats Resolve(const FDemoEnemyRow& Enemy, const FDemoDifficultyRow& Difficulty, const FDemoLevelRow& Level);
}
