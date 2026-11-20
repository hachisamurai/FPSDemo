// 职责：Steam Demo内容契约与确定性房间计划。用法：Flow在准备阶段读取DataAsset；依赖目标类型/原生成计划，不拥有World或玩家。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Game/Objectives/DemoObjectiveTypes.h"
#include "DemoCampaignDefinition.generated.h"

/** 策划配置的一关；稳定房间ID与旧白模区域显式对应，正式流送资源随后替换适配。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoCampaignStage
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Campaign") EDemoObjectiveKind Objective = EDemoObjectiveKind::KillAll; // 完成条件，不影响原5/10兵种周期。
    UPROPERTY(EditAnywhere, Category="Campaign") TArray<FName> RoomPool; // 非空、无重复稳定ID，计划随机只在此池内。
};

/** 一次入场固定的人数系数；伤害与攻击频率不随人数改变。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoPartyScaling
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Party") float MinionHealth = 1.f; // 原等级/难度之后的HP倍率。
    UPROPERTY(EditAnywhere, Category="Party") float MinionCount = 1.f; // 与任务倍率合并后仅舍入一次。
    UPROPERTY(EditAnywhere, Category="Party") float BossHealth = 1.f; // 不改变Boss数量。
    UPROPERTY(EditAnywhere, Category="Party") float DeviceHealth = 1.f; // 设施独立HP倍率。
    UPROPERTY(EditAnywhere, Category="Party") int32 MaxAlive = 8; // 所有小怪、Boss和召唤共享上限。
};

/** 只含值的公开计划；不含玩家经济、Actor或任意客户端路径。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoCampaignPlan
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Campaign") FName CampaignId; // None代表旧战役。
    UPROPERTY(BlueprintReadOnly, Category="Campaign") int32 ContentRevision = 0; // 作者内容修订；V9由完整快照恢复，schema版本决定代码兼容性。
    UPROPERTY(BlueprintReadOnly, Category="Campaign") int32 RunSeed = 0; // 随检查点持久化，重复进入同关不重抽。
    UPROPERTY(BlueprintReadOnly, Category="Campaign") int32 Stage = 0; // 1..10逻辑关号。
    UPROPERTY(BlueprintReadOnly, Category="Campaign") FName RoomId; // 稳定内容ID，与数组下标分离。
    UPROPERTY(BlueprintReadOnly, Category="Campaign") int32 Variant = 0; // 0/1白模装饰变体，当前不改变碰撞。
    UPROPERTY(BlueprintReadOnly, Category="Campaign") EDemoObjectiveKind Objective = EDemoObjectiveKind::KillAll; // 目标规则。
    UPROPERTY(BlueprintReadOnly, Category="Campaign") int32 PartySize = 1; // 入场冻结1..4；中途掉线不缩血。
    UPROPERTY(BlueprintReadOnly, Category="Campaign") int32 ArenaIndex = 0; // 仅旧地图适配0..2，不作为新内容身份。
};

/** 作者配置与冻结快照共用的值模型；启动须加载正式资产，不以原生默认值掩盖资产缺失。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoCampaignDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 仅构建默认数据，不加载地图、不启动目标、不改存档。 */
    UDemoCampaignDefinition();
    UPROPERTY(EditAnywhere, Category="Campaign") FName CampaignId = TEXT("steam_demo"); // V8稳定内容ID。
    UPROPERTY(EditAnywhere, Category="Campaign", meta=(ClampMin="1")) int32 ContentRevision = 1; // 作者正整数修订；V9整份配置冻结，不再以修订1硬编码阻止旧档。
    UPROPERTY(EditAnywhere, Category="Campaign") TArray<FDemoCampaignStage> Stages; // 恰好十行，保留最终通关规则。
    UPROPERTY(EditAnywhere, Category="Campaign") TMap<FName,int32> RoomAreas; // 8个战斗RoomId到已验证旧白模区域的适配。
    UPROPERTY(EditAnywhere, Category="Campaign") TMap<FName,FName> RoomBossRows; // 终局房间ID到DT_Enemies的Boss行，保证稳定房间与Boss身份一致。
    UPROPERTY(EditAnywhere, Category="Campaign") TArray<FDemoPartyScaling> Party; // 顺序对应1..4人。
    UPROPERTY(EditAnywhere, Category="Objective", meta=(ClampMin="1",ClampMax="3")) float ObjectiveCountMultiplier = 1.2f; // 破坏/控制有限敌人预算。
    UPROPERTY(EditAnywhere, Category="Objective", meta=(ClampMin="1")) float DeviceBaseHealth = 600.f; // HP，另乘本关等级、难度和人数。
    UPROPERTY(EditAnywhere, Category="Objective") TArray<FVector> LocalDeviceAnchors; // 当前白模区域局部cm，恰好两个设施地面锚点；Flow变换到世界并检查地面。
    UPROPERTY(EditAnywhere, Category="Objective") TArray<FVector> LocalControlAnchors; // 当前白模区域局部cm，恰好三个控制点，顺序为激活次序。
    UPROPERTY(EditAnywhere, Category="Objective", meta=(ClampMin="1",ClampMax="600")) float ControlSeconds = 20.f; // 每点累计占领游戏秒，范围与目标协调器一致。
    UPROPERTY(EditAnywhere, Category="Objective", meta=(ClampMin="1",ClampMax="30")) float TransitionSeconds = 5.f; // 控制点预告1..30游戏秒，配置和实际执行采用同一边界。
    UPROPERTY(EditAnywhere, Category="Objective", meta=(ClampMin="100",ClampMax="2000")) float ControlRadius = 450.f; // 控制区域水平半径cm，垂直边界仍由目标组件统一处理。
    /** 全量校验包含稳定ID、区域、倍率和十关规则；Error给出明确失败原因。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Content")
    bool Validate(FString& Error) const;
    /** Seed/Stage/Players均为服务器冻结值；纯值结果，失败不提交半份计划。 */
    bool BuildPlan(int32 Seed, int32 Stage, int32 Players, FDemoCampaignPlan& Out, FString& Error) const;
    /** 开始新轮时生成schema1紧凑JSON及sha1指纹；只输出有限纯值，不含对象或资源路径，失败清空输出。 */
    bool ExportSnapshot(FString& OutJson, FString& OutFingerprint, FString& Error) const;
    /** 读档时校验有界规范JSON/指纹并创建Outer拥有的独立配置；失败返回nullptr，绝不按字符串加载资产。 */
    static UDemoCampaignDefinition* FromSnapshot(UObject* Outer, const FString& Json, const FString& Fingerprint, FString& Error);
    /** V8升级专用：显式冻结已发布v1白模配置；不读取当前作者资产，也不依赖未来构造默认值。 */
    static bool ExportLegacyV1(FString& OutJson, FString& OutFingerprint, FString& Error);
    /** 只检查可解析的schemaVersion整数是否大于当前1；供存档保护未来版本，损坏数据交给完整校验。 */
    static bool HasUnsupportedSnapshotSchema(const FString& Json);
};
