#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Game/DemoGameState.h"
#include "DemoRunSave.generated.h"

/** 检查点的武器值快照；只保存稳定目录ID和弹药，不序列化Actor/CDO。 */
USTRUCT()
struct FDemoSavedWeapon
{
    GENERATED_BODY()
    // pistol/rifle/shotgun/sniper；读取时再次校验永久解锁和目录资源。
    UPROPERTY() FName Id;
    // 弹匣与备用单位发，恢复时按实际BP容量钳制。
    UPROPERTY() int32 Ammo = 0;
    UPROPERTY() int32 Reserve = 0; // 备用弹药发数，与弹匣分开恢复；无限备用武器仍保存数值供诊断。
};

/** 每个游戏栏位保存检查点及永久弹药解锁；账号武器解锁仍由DemoPlayerProfile管理。 */
UCLASS()
class FPSDEMO_API UDemoRunSave : public USaveGame
{
    GENERATED_BODY()
public:
    // V4加入同槽金币购买的弹药解锁/选择；旧档默认普通弹，未来版本仍保护。
    UPROPERTY() int32 Version = 4;
    UPROPERTY() TArray<FName> UnlockedAmmoIds = {FName(TEXT("normal"))}; // 与Coins同一快照，不跨槽共享。
    UPROPERTY() FName SelectedAmmoId = TEXT("normal"); // 已解锁目录ID，死亡/通关保留。
    // 首次创建时的本地时间，不随读取/重开变化；仅展示，不作为唯一标识。
    UPROPERTY() FDateTime CreatedLocal;
    // 最近检查点的UTC时间，便于备份诊断。
    UPROPERTY() FDateTime SavedUtc;
    // 本次战役唯一GUID，跨读档保持，避免重复通关永久奖励。
    UPROPERTY() FString RunId;
    // 仅Hub/Reward/Intermission/Victory可存储；Victory读入自动转续玩Hub，Combat恢复进入前检查点。
    UPROPERTY() EDemoPhase Phase = EDemoPhase::Hub;
    // 初次安全区默认普通，出发终端选择后整局锁定。
    UPROPERTY() EDemoDifficulty Difficulty = EDemoDifficulty::Normal;
    // 已完成关卡0..10；0安全区，10仅Victory。
    UPROPERTY() int32 CompletedLevel = 0;
    // 金币余额在通关/死亡/主动返回后保留；Editor停止仍随本GI临时档清理。
    UPROPERTY() int32 Coins = 0;
    UPROPERTY() int32 SilverCoins = 0; // 击杀银币，关间使用；通关/死亡/放弃归零。
    UPROPERTY() int32 Kills = 0; // 本局累计击杀，恢复统计而不重新发放金币。
    UPROPERTY() int32 Purchases = 0; // 两币购买总次数，必须等于下方两项之和。
    UPROPERTY() int32 GoldPurchases = 0; // 永久金币总次数，必须等于账本GoldLevels总和。
    UPROPERTY() int32 SilverPurchases = 0; // 本轮银币总次数，必须等于账本SilverLevels总和。
    UPROPERTY() FDemoUpgradeProgress UpgradeProgress; // 来源账本随栏位持久化，不能根据GAS合计倒推。
    // GAS永久局内基础值，排除Boss减速、冲刺/装填/射击冷却等短期状态。
    UPROPERTY() float MaxHealth = 100.f; // 生命上限HP，新局100，必须先于当前生命恢复。
    UPROPERTY() float Health = 100.f; // 检查点当前HP，要求0<Health<=MaxHealth。
    UPROPERTY() float DamageBonus = 0.f; // 全武器伤害加成，霰弹由武器层分摊，不保存单把最终伤害。
    UPROPERTY() float MagazineBonus = 0.f; // 全武器弹匣容量加成发数，恢复时影响弹药钳制。
    // 当前Pawn技能成长，35HP治疗/1300cm/s冲刺是新局基线。
    UPROPERTY() float HealAmount = 35.f;
    UPROPERTY() float DashSpeed = 1300.f; // 冲刺初速度cm/s，只存永久局内成长，不存当前移动速度。
    // 实际已持有的武器快照；None主武器表示仅手枪，ActiveSlot合法1/2。
    UPROPERTY() TArray<FDemoSavedWeapon> Weapons;
    UPROPERTY() FName PrimaryId; // 当前主武器目录ID；None表示未在终端装备主武器。
    UPROPERTY() int32 ActiveSlot = 2; // 当前持用1主/2副；新局默认手枪槽2。
    /** 完整校验持久字段的枚举、数值和阶段一致性；失败不能恢复/覆盖原存档。 */
    bool Validate() const;
    /** 接受合法V1..V4；旧档迁移购买来源和普通弹默认值，未来版本拒绝。 */
    bool UpgradeLegacy();
    /** 结束挑战时保留永久账本、设置满永久生命和基础技能；只改成长/价格，不动币余额与阶段。 */
    void ResetTemporaryGrowth();
};

/** GI级三个检查点栏位；同步小型SaveGame写盘，跨地图只持有值对象。 */
UCLASS()
class FPSDEMO_API UDemoRunSaves : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    /** Collection为引擎容器；Editor构建每个GI使用临时GUID槽，打包游戏才读取正式档。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** 停止PIE/退出Editor游戏时删除本GI临时主档及备份；正式游戏文件保留。 */
    virtual void Deinitialize() override;
    /** 重新读取三个槽及备份；仅在打开选择页调用，不在DrawHUD读磁盘。 */
    void RefreshSlots();
    /** Index=0..2，只读当前GI缓存；损坏/空槽返回nullptr，区分请用SlotExists。 */
    const UDemoRunSave* GetSlot(int32 Index) const;
    /** Index=0..2；损坏或仅剩备份也视为占用，禁止以新建流程覆盖。 */
    bool SlotExists(int32 Index) const;
    /** Index=0..2；空槽新建并落盘，失败保留原界面且不进入游戏。 */
    bool CreateSlot(int32 Index);
    /** Index=0..2，重新读盘并标记一次恢复请求；损坏槽不能当空槽覆盖。 */
    bool SelectSlot(int32 Index);
    /** GameMode新World领取一次读档请求，返回GI持有的借用快照。 */
    const UDemoRunSave* ConsumePending();
    /** Snapshot为借用新检查点；写盘成功后复制到GI缓存，失败返回false并显示状态。 */
    bool Store(UDemoRunSave* Snapshot);
    /** Difficulty/Gold是当前难度/金币，Progress是权威来源账本；仅清空临时成长，成功才允许旅行。 */
    bool ResetActive(EDemoDifficulty Difficulty, int32 Gold, const FDemoUpgradeProgress& Progress);
    /** 当前槽0..2或INDEX_NONE，以及用户可读保存结果；无槽测试允许只运行World。 */
    int32 GetActiveSlot() const;
    /** 返回GI持有的反馈摘要，只读借用；用于HUD和离开失败提示。 */
    const FString& GetStatus() const;
    /** Index有效时给出固定本地槽名；不接受用户路径。 */
    FString SlotName(int32 Index) const;
    /** Index=0..2导出已验证缓存；空/损坏槽返回null，JSON只包含跨端值字段。 */
    TSharedPtr<class FJsonObject> ExportCloud(int32 Index) const;
    /** 下载指定Index的检查点；Data为协议快照，先校验再备份落盘，不切换当前角色。 */
    bool ImportCloud(int32 Index, const TSharedPtr<class FJsonObject>& Data);
    /** GI内本地写盘序列，用于UI及时显示待同步；不作为网络修订或跨进程身份。 */
    uint64 GetChangeSerial() const;
private:
    /** Message为用户可读结果；bError指定日志级别，统一保证UI和诊断日志一致。 */
    void ReportStatus(const FString& Message, bool bError = false);
    // GI强持有的三个已载入值对象，不持有World/Pawn。
    UPROPERTY() TArray<TObjectPtr<UDemoRunSave>> Slots;
    // 无活动槽的开发World也通过GI传递永久成长；仅一次重开，不写文件，停止试玩销毁。
    UPROPERTY() TObjectPtr<UDemoRunSave> DetachedRestart;
    // 存在但损坏的文件仍算占用，绝不自动覆盖。
    TArray<bool> Exists;
    // 同步请求状态只在本GI有效；新游戏进程从大厅重新选择。
    int32 ActiveSlot = INDEX_NONE;
    bool bPending = false; // 一次OpenLevel恢复意图，由新GameMode消费。
    // 稳定正式前缀或本GI临时GUID前缀；跨OpenLevel保持，永不操作其他GI/正式文件。
    FString Prefix;
    bool bTest = false; // 只有隔离测试实例退出时可以自动删除自己的文件。
    bool bEditorSession = false; // WITH_EDITOR覆盖PIE/SIE/Editor Standalone；停止游戏即清理，此标记不序列化。
    // HUD反馈：失败必须可见，不能把内存缓存误报为持久化成功。
    FString Status;
    uint64 ChangeSerial = 0; // 仅成功创建/保存/导入时递增，读取/绘制不改值。
};
