// 职责：检查点持久化与GI槽生命周期。使用：V10同槽个人枪权/身份、V9冻结内容及原核心/娱乐；Editor每GI临时数据停止清理。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Game/DemoGameState.h"
#include "Contracts/DemoGameplayContracts.h"
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

/** 每槽保存个人永久枪权/弹药及整轮检查点；Profile只提供一次冻结账号Legacy，不拥有本槽Paid权。 */
UCLASS()
class FPSDEMO_API UDemoRunSave : public USaveGame
{
    GENERATED_BODY()
public:
    /** SaveGame加载前用旧V9缺省解释被旧CDO省略的版本tag；新保存强制完整tag，防CDO升级误认旧档。 */
    virtual void Serialize(FArchive& Ar) override;
    // 可选LAN/EOS合作扩展；空数组是原V5单人档，最多四个成员，同一文件原子保存。
    UPROPERTY() TArray<FDemoCoopPlayerCheckpoint> TeamMembers;
    // 首次公网上线绑定房主PUID的规范GUID；空值代表未绑定，不能拿此字段当云API认证Token。
    UPROPERTY() FString OnlineOwnerKey;
    // 显式存档结构版本；保留旧挑战/弹药/成长账本迁移，未来结构与内容均保护原文件。
    UPROPERTY() int32 Version = 10; // V10同槽拥有权/身份/收据；V9冻结内容原文保持。
    UPROPERTY() FString SlotStorageKey; // 栏位实例永久GUID，删除重建换代，不使用RunId代替。
    UPROPERTY() FString OwnerPlayerKey; // 根个人镜像所属固定房主规范GUID，与成员0相同。
    UPROPERTY() FDemoWeaponOwnership Ownership; // 根房主镜像；成员各自独立，不与账号付费权合并。
    UPROPERTY() FName CampaignId; // None为旧十关/无尽；steam_demo为新版十关内容，不接收资产路径。
    UPROPERTY() int32 ContentRevision = 0; // None时0；steam_demo为正整数并与冻结快照一致，不要求等于当前作者版本。
    UPROPERTY() int32 RunSeed = 0; // 非负int32确定性房间计划种子，整轮和读档保持，None时0。
    UPROPERTY() FString ContentSnapshot; // V9根共享紧凑JSON，至多64KiB UTF8；本轮配置不随DataAsset修改，旧模式必须空。
    UPROPERTY() FString ContentFingerprint; // sha1:加40位小写HEX，对快照UTF8原文做一致性检查；不是身份凭证或反作弊签名。
    UPROPERTY() FDemoRunBuildSnapshot Build; // 房主/单人本轮核心与分支；合作各成员另存独立Build，不存战斗计数。
    UPROPERTY() FDemoGambleProgress Gambling; // 同槽永久遗物与临时天赋；团队各成员另有独立副本。
    UPROPERTY() bool bEndless = false; // 无尽模式使用Hell基线，Reward/Intermission允许超过第十关。
    UPROPERTY() int32 BestEndlessLevel = 0; // 本槽最高已完成关数，跨死亡/放弃保留。
    UPROPERTY() int32 PistolChallenge = 0; // 0未射击/1只射手枪/2失格；旧进行中战役迁移为2。
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
    // 已完成关卡：固定战役0..10，无尽非负递增；0安全区，Victory仅固定战役10。
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
    UPROPERTY() FName PrimaryId; // 本槽已装备主武器目录ID，死亡/放弃保留；None表示从未装备主武器。
    UPROPERTY() int32 ActiveSlot = 2; // 当前持用1主/2副；新局默认手枪槽2。
    /** 完整校验持久字段的枚举、数值和阶段一致性；失败不能恢复/覆盖原存档。 */
    bool Validate() const;
    /** 验证V1..V10；旧数值迁移至V9，GI按已持久Profile整槽冻结后升V10，未来结构拒绝。 */
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
    /** Index为0..2的Hub槽，OwnerKey来自已登录EOS；首次绑定允许单人槽，拒绝猜测迁移多人LAN身份。 */
    bool BindOnlineOwner(int32 Index, const FString& OwnerKey);
    /** GameMode新World领取一次读档请求，返回GI持有的借用快照。 */
    const UDemoRunSave* ConsumePending();
    /** Snapshot为借用新检查点；写盘成功后复制到GI缓存，失败返回false并显示状态。 */
    bool Store(UDemoRunSave* Snapshot);
    /** Difficulty/Gold/Progress为权威永久进度；Loadout是本次借用的重开装备值快照，空时保留原槽库存。只清临时成长，落盘成功才旅行。 */
    bool ResetActive(EDemoDifficulty Difficulty, int32 Gold, const FDemoUpgradeProgress& Progress, const UDemoRunSave* Loadout = nullptr);
    /** 当前槽0..2或INDEX_NONE，以及用户可读保存结果；无槽测试允许只运行World。 */
    int32 GetActiveSlot() const;
#if WITH_EDITOR
    /** 重置成功或系统异常恢复有效检查点前，暂存本机试用枪和手持槽，仅供同一 GI 重载。 */
    void CaptureEditorRestartWeapon(const class UDemoWeaponComponent& Inventory);
    /** 正式检查点提交后按匹配的 RunId 恢复一次调试配装。 */
    void RestoreEditorRestartWeapon(const FString& RunId, class UDemoWeaponComponent& Inventory);
#endif
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
    /** ChallengeStatus=0..2，Best为已完成无尽关数；只更新资格/纪录，不把半场战斗写入检查点。 */
    bool StoreChallenge(int32 ChallengeStatus, int32 Best);
    /** PlayerKey 是服务器登记的成员键；只更新已存检查点的资格，绝不保存半场钱包。 */
    bool StorePlayerChallenge(const FString& PlayerKey, int32 ChallengeStatus, int32 Best);
private:
    /** Message为用户可读结果；bError指定日志级别，统一保证UI和诊断日志一致。 */
    void ReportStatus(const FString& Message, bool bError = false);
#if WITH_EDITOR
    FString EditorRestartRun; // 目标检查点的 RunId，绑定同一 GI 的 OpenLevel 前后的 Pawn。
    FName EditorRestartPrimary; // 本 GI 暂存的借用型号，不复制、不写入存档。
    int32 EditorRestartSlot = 2; // 重载前的手持槽，借用主枪时也可能正在使用手枪。
#endif
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
    bool bStoring = false; // 同一GI整槽同步提交锁；重入拒绝，不允许旧候选覆盖已提交事务。
    // HUD反馈：失败必须可见，不能把内存缓存误报为持久化成功。
    FString Status;
    uint64 ChangeSerial = 0; // 仅成功创建/保存/导入时递增，读取/绘制不改值。
};
