// 职责：业务值契约与回执。使用：跨模块传递已验证值；网络进度必须通过PublicSnapshot脱敏。
#pragma once
#include "CoreMinimal.h"
#include "Game/DemoCombatConfig.h"
#include "Game/DemoUpgradeProgress.h"
#include "Economy/Gambling/DemoGambleTypes.h" // 赌博与个人钱包同一事务快照。
#include "Progression/Builds/DemoRunBuildTypes.h" // 核心/分支是本轮个人选择，不并入钱包或临时GAS计数。
#include "Contracts/DemoWeaponOwnership.h" // P0本槽来源账本；旧UnlockedWeaponIds只作派生缓存。
#include "DemoGameplayContracts.generated.h"

/** 业务提交与磁盘提交分开，客户端不能把内存成功误报为已保存。 */
UENUM()
enum class EDemoCommandStatus : uint8 { Rejected, AppliedPendingSave, Committed };

/** RPC 只承载结果值；身份由拥有该 RPC 的控制器确定。 */
USTRUCT()
struct FPSDEMO_API FDemoCommandResult
{
    GENERATED_BODY()
    // 默认拒绝，避免空结果被 UI 当作成功。
    UPROPERTY() EDemoCommandStatus Status = EDemoCommandStatus::Rejected;
    // 稳定原因码用于 UI 分支，Message 仅展示，不解析中文判断状态。
    UPROPERTY() FName Code;
    // 当前操作的可读反馈，不携带账户凭据或内部路径。
    UPROPERTY() FString Message;
    // 客户端请求序号只关联回执，不作为业务权限或服务器事务版本。
    UPROPERTY() int32 RequestId = 0;
    UPROPERTY() FDemoCasinoResult CasinoResult; // V7金币玩法已保存结果，绝不包含未揭雷位。
    UPROPERTY() FDemoGambleReveal GambleReveal; // 仅已落盘抽取携带权威奖池/停靠ID；其余命令为空，不改变V6存档。
    /** 高频只读查询，已应用但尚未落盘也属于业务成功。 */
    bool IsApplied() const;
    /** Code/Message 是借用文本值，构造明确拒绝结果。 */
    static FDemoCommandResult Reject(FName Code, const FString& Message);
    /** bSaved 区分落盘和待重试；Message 为本次操作的展示摘要。 */
    static FDemoCommandResult Applied(bool bSaved, const FString& Message);
};

/** 武器领域值，不依赖 SaveGame、Actor 或某个 UI。 */
USTRUCT()
struct FPSDEMO_API FDemoWeaponSnapshot
{
    GENERATED_BODY()
    // 固定目录 ID，恢复时由服务器重新查定义及解锁。
    UPROPERTY() FName Id;
    // 单位发；最终按该武器配置与角色容量钳制。
    UPROPERTY() int32 Ammo = 0;
    // 单位发；与弹匣分离，不能通过恢复产生负弹药。
    UPROPERTY() int32 Reserve = 0;
};

/** 每名玩家独立的装备快照；主武器选择跨死亡保留。 */
USTRUCT()
struct FPSDEMO_API FDemoLoadoutSnapshot
{
    GENERATED_BODY()
    // 只记录实际拥有的武器；目录不因快照新增型号。
    UPROPERTY() TArray<FDemoWeaponSnapshot> Weapons;
    // None 代表尚未选择主武器，而非装备失败。
    UPROPERTY() FName PrimaryId;
    // 1 主武器、2 手枪，默认新玩家持手枪。
    UPROPERTY() int32 ActiveSlot = 2;
    // 弹药解锁属于本人栏位，客户端不能提交任意解锁名单。
    UPROPERTY() TArray<FName> UnlockedAmmoIds = {FName(TEXT("normal"))};
    // 装配的稳定 ID，须包含在解锁集合中。
    UPROPERTY() FName SelectedAmmoId = TEXT("normal");
    // 恢复容量用的合计加值（发）；由权威成长快照产生。
    UPROPERTY() float MagazineBonus = 0.f;
};

/** PlayerState 持有的个人经济值；ASC 仍拥有实际战斗属性。 */
USTRUCT()
struct FPSDEMO_API FDemoPlayerProgressSnapshot
{
    GENERATED_BODY()
    UPROPERTY() FDemoWeaponOwnership Ownership; // 本槽本成员唯一永久枪权，OwnerOnly复制；不按新通关增加。
    UPROPERTY() FDemoGambleProgress Gambling; // 本人临时天赋、永久遗物、保底和幂等序号。
    // 金币和永久成长跨本轮结束保留，余额限制为 0..1 亿。
    UPROPERTY() int32 Coins = 0;
    // 银币仅本轮有效，独立钱包，其他玩家购买不会影响此余额。
    UPROPERTY() int32 SilverCoins = 0;
    // 本人累计击杀统计，不用于推算币余额。
    UPROPERTY() int32 Kills = 0;
    // 来源账本决定独立价格，不能从 GAS 合计反推。
    UPROPERTY() FDemoUpgradeProgress UpgradeProgress;
    // 每人独立挑战资格：0 未开火、1 仅手枪、2 已失格。
    UPROPERTY() int32 PistolChallenge = 0;
    // 本人已完成无尽关数纪录，结束挑战保留。
    UPROPERTY() int32 BestEndlessLevel = 0;
    // Ownership派生的有效权缓存；禁止由后续通关或库存Actor反推永久权。
    UPROPERTY() TArray<FName> UnlockedWeaponIds = {FName(TEXT("pistol"))};
    // 地狱和无尽的永久准入权限，与当前队伍阶段分开。
    UPROPERTY() bool bHellUnlocked = false;
    // 已通关地狱后为 true，不由选中难度推导。
    UPROPERTY() bool bEndlessUnlocked = false;
    // 本关免费奖励是否已选择；每人领取一次后由流程统一推进。
    UPROPERTY() int32 LastRewardLevel = 0;
    // 服务器单调版本，只用来使旧报价失效，不代替重新计算价格。
    UPROPERTY() int32 Revision = 0;
};

/** 已完成战役的个人事实；仅服务器结算器创建，客户端只接收拥有者回执。 */
USTRUCT()
struct FPSDEMO_API FDemoRunCompletion
{
    GENERATED_BODY()
    // 同轮稳定 GUID；Profile 幂等去重，不能以重连重复发放。
    UPROPERTY() FString RunId;
    // 完成时冻结的难度，不能读取下一轮可变 GameState。
    UPROPERTY() EDemoDifficulty Difficulty = EDemoDifficulty::Normal;
    // 固定战役需完成十关；无尽纪录不伪造完整通关。
    UPROPERTY() int32 CompletedLevel = 0;
    // 服务器确认的本人全程手枪资格，队友使用主武器不影响本人。
    UPROPERTY() int32 PistolChallenge = 0;
    // 用于拒绝将无尽某一关当成战役最终结算。
    UPROPERTY() bool bEndless = false;
};

/** 房主检查点中的个人成员值；不保存网络 Actor、ASC 指针或客户端任意提交的属性。 */
USTRUCT()
struct FPSDEMO_API FDemoCoopPlayerCheckpoint
{
    GENERATED_BODY()
    // V8本人本轮核心/互斥分支；由PS组件捕获，临时触发窗口与计数不持久化。
    UPROPERTY() FDemoRunBuildSnapshot Build;
    // 固定队伍关联键，首次握手登记；可信 LAN 使用，不替代平台认证。
    UPROPERTY() FString PlayerKey;
    // 服务器独立经济来源及个人通关权限。
    UPROPERTY() FDemoPlayerProgressSnapshot Progress;
    // 主副武器和弹药字段保持稳定目录 ID。
    UPROPERTY() FDemoLoadoutSnapshot Loadout;
    // 以下是 GAS 基础值（排除瞬时 GE），每关保存时生命为正。
    UPROPERTY() float MaxHealth = 100.f;
    UPROPERTY() float Health = 100.f; // HP，死者清关复活后保存，不能恢复死亡 Avatar。
    UPROPERTY() float DamageBonus = 0.f; // 每枪额外 HP，加到武器配置基础伤害。
    UPROPERTY() float MagazineBonus = 0.f; // 全武器容量加值（发）。
    UPROPERTY() float HealAmount = 35.f; // 本轮治疗量 HP。
    UPROPERTY() float DashSpeed = 1300.f; // 本轮冲刺速度 cm/s。
};
