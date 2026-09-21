#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Game/DemoCombatConfig.h"
#include "DemoPlayerProfile.generated.h"

/** 永久通关凭据；字符串ID不随枚举顺序、蓝图路径或显示语言改变。离线记录不等同于服务器认证。 */
USTRUCT()
struct FDemoClearRecord
{
    GENERATED_BODY()
    // 每轮挑战的GUID，StartRun或胜利续玩分配；重复胜利通知按此去重，不重复授予进度。
    UPROPERTY() FString RunId;
    // 稳定协议键 easy/normal/hard；每种难度只解锁对应主武器。
    UPROPERTY() FString DifficultyId;
    // UTC ISO8601，供展示和未来审计；客户端时间不能作为服务器授权依据。
    UPROPERTY() FString CompletedUtc;
};

/** 本地持久数据/上传快照；不含Actor、软资源路径、局内金币、弹药或GAS临时属性。 */
USTRUCT()
struct FDemoPlayerProfileData
{
    GENERATED_BODY()
    // V2分离服务器确认难度和待上传通关；V1记录作为待上传迁移，未来版本保护只读。
    UPROPERTY() int32 SchemaVersion = 2;
    // 首次创建的本地GUID，不是认证账号；未来服务端需单独绑定登录身份。
    UPROPERTY() FString ProfileId;
    // 单调递增的本地内容修订号；JSON使用int32以避免JS大整数精度问题。
    UPROPERTY() int32 LocalRevision = 0;
    // 最后成功上传的本地修订；小于LocalRevision表示待同步，确认接口不覆盖新修改。
    UPROPERTY() int32 SyncedRevision = 0;
    // 服务端返回的不透明并发控制令牌，离线初始为空。
    UPROPERTY() FString ServerRevision;
    // 最后一次本地内容变更的UTC ISO8601时间，不用作冲突优先级。
    UPROPERTY() FString UpdatedUtc;
    // 待服务器确认的十关通关记录；确认并落盘后移除，避免每次无限上传历史。
    UPROPERTY() TArray<FDemoClearRecord> Clears;
    // 服务器已确认的难度，最多三种；与本地待上传记录共同派生永久解锁。
    UPROPERTY() TArray<FString> CloudClearedDifficulties;
    // 最近128个已确认RunId，防止胜利通知在收到HTTP确认后重复进入队列；服务端仍永久去重。
    UPROPERTY() TArray<FString> RecentAcceptedRuns;
    // 最近一次主动装备偏好修改的本地修订，下载不能覆盖尚未确认的偏好。
    UPROPERTY() int32 PreferenceRevision = 0;
    // 偏好独立确认水位；通关分批确认不能误认为尚未上传的装备偏好也已同步。
    UPROPERTY() int32 SyncedPreferenceRevision = 0;
    // 稳定键 pistol/rifle/shotgun/sniper；加载时仅从上述事实重新推导。
    UPROPERTY() TArray<FName> UnlockedWeaponIds;
    // 最近终端主武器偏好，仅供UI提示；新档只生成手枪，已有检查点按其库存恢复，通关续玩保留装备。
    UPROPERTY() FName LastSelectedPrimary;
};

/** UE二进制文件外壳，未来上传只用JSON数据结构，不上传sav或UObject元数据。 */
UCLASS()
class FPSDEMO_API UDemoProfileSave : public USaveGame
{
    GENERATED_BODY()
public:
    // 由存档对象拥有的值快照，UGameplayStatics负责序列化。
    UPROPERTY() FDemoPlayerProfileData Data;
};

/** GameInstance生命周期保存解锁；所有接口在游戏线程调用，跨地图不持有Pawn。 */
UCLASS()
class FPSDEMO_API UDemoPlayerProfile : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    /** Collection为引擎子系统容器；Editor每个GI创建独立临时档，非Editor构建才加载正式本地档。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** 正式游戏退出重试保存；停止Editor试玩删除本GI档案及备份，清空武器解锁/通关记录。 */
    virtual void Deinitialize() override;
    /** 返回本GI拥有的只读快照；调用者不得保存跨GI引用。 */
    const FDemoPlayerProfileData& GetData() const;
    /** Id是稳定目录键；无效武器始终false，手枪始终解锁。 */
    bool IsUnlocked(FName Id) const;
    /** RunId来自权威GameMode；只在最终Victory且对应难度时接受，保存失败仍保留内存进度。 */
    bool RecordVictory(const FString& RunId, EDemoDifficulty Difficulty);
    /** Id为已成功在终端装备的主武器，保存选择偏好，不生成武器。 */
    void RememberPrimary(FName Id);
    /** 同步保存主槽并保留上次有效备份；失败返回false并提供可显示的状态。 */
    bool SaveProfile();
    /** 显式重新读取磁盘以恢复进度；有未落盘内存变更时拒绝，避免旧档覆盖当前进度。 */
    bool ReloadFromDisk();
    /** 本地保存/加载反馈，只读；错误不会伪装成已同步服务器。 */
    const FString& GetStatus() const;
    /** 返回实际槽名供诊断与测试；正式固定DemoPlayerProfile，Editor/自动化为独立GUID。 */
    const FString& GetSlotName() const;
    /** OutJson为上传DTO，含协议版本/幂等请求ID/基准服务端修订/完整数据；仅导出，不发送网络。 */
    bool BuildUploadJson(FString& OutJson) const;
    /** 网络适配器游戏线程回调：校验ProfileId/UploadedRevision/ServerRevision，旧回复不能覆盖新内容。 */
    bool AcknowledgeUpload(const FString& ProfileId, int32 UploadedRevision, const FString& ServerRevision);
    /** Cloud为投影；AcceptedRuns/AckRevision来自匹配回执、下载传0；bAckPreference仅确认确实提交的偏好。 */
    bool ApplyCloud(const TSharedPtr<class FJsonObject>& Cloud, const TArray<FString>& AcceptedRuns, int32 AckRevision, bool bAckPreference = false);
    /** 本地档可写且未确认修改存在时才上传；bNeedsSave必须先落盘，不能只上传内存进度。 */
    bool CanSync() const;
private:
    /** 从通关记录重建解锁、修正失效选择，拒绝损坏ID/版本/修订字段；Data为可规范化值。 */
    bool Normalize(FDemoPlayerProfileData& Data) const;
    /** 内容变更时推进修订和UTC并立即尝试落盘；失败留下可重试状态。 */
    void CommitChange();
    // 本GI内的唯一可变档案；不复制，当前实现仅支持单人本地。
    FDemoPlayerProfileData Profile;
    // 真实持久化槽，备份后缀_Backup；不接受外部任意路径。
    FString SlotName;
    // 菜单反馈状态，不持久化到档案或上传。
    FString Status;
    // 损坏且无备份/未来版本时保护原文件，禁止静默覆盖。
    bool bReadOnly = false;
    // 写盘失败仍保留内存，退出可重试；与网络未同步状态无关。
    bool bNeedsSave = false;
    // 自动化进程单独存档并在退出删除，避免测试胜利污染玩家成就。
    bool bTestSlot = false;
    // WITH_EDITOR的每个GI独立本地档；跨地图保留，停止PIE或退出Editor游戏时删除。
    bool bEditorSession = false;
};
