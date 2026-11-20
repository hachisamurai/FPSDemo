// 职责：任务目标的配置值与公开复制快照。使用：Flow冻结配置，GM目标组件执行，GameState复制快照；不保存半场进度。
#pragma once
#include "CoreMinimal.h"
#include "DemoObjectiveTypes.generated.h"

/** 目标决定完成条件，不改变敌人奖励或原有刷怪器所有权。 */
UENUM(BlueprintType)
enum class EDemoObjectiveKind : uint8 { KillAll, Sabotage, RotatingControl };

/** 目标是战局下属状态，Resolving只提交候选，由Flow在帧末统一裁决团灭优先。 */
UENUM(BlueprintType)
enum class EDemoObjectiveState : uint8 { Unprepared, Preparing, Active, Resolving, Completed, Cancelled, SystemError };

/** 控制区当前状态；公开UI不用读取服务器敌人容器或重新判定占区。 */
UENUM(BlueprintType)
enum class EDemoControlStatus : uint8 { Inactive, Empty, Contested, Capturing, Relocating };

/** 每场按值冻结的目标参数；世界位置由房间系统/Flow给出，不允许客户端传坐标。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoObjectiveConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDemoObjectiveKind Kind = EDemoObjectiveKind::KillAll; // 必须配置的本场完成规则。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DeviceHealth = 600.f; // 每设施HP，Flow已乘等级/难度/人数倍率。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ControlSeconds = 30.f; // 运行期默认每点累计30游戏秒；Flow仍按冻结战役覆盖，旧战役20秒不变。离开/敌人占区暂停不回退。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float TransitionSeconds = 5.f; // 点间迁移预告秒数，不计入占领。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ControlRadius = 450.f; // 水平判定半径cm；垂直差超过250cm不算占区。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxSampleDelta = .2f; // 每次0.1s采样最多补计秒数，卡顿不能跳完目标。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> DeviceAnchors; // 两个设施地面原点cm，缺失明确拒绝，不默默改任务。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> ControlAnchors; // 一个或三个控制点地面原点cm，按数组次序激活；Flow决定新单点或旧冻结三点，不改存档候选点。
    UPROPERTY(Transient) FName SourceLevelPackage; // 本次房间准备冻结的Level包身份；只用于服务器地板归属复核，不进入战役资产/存档/网络；旧常驻模板为空。
};

/** GameState持有并复制的纯值；最多4Hz发布变化，不复制设施伤害来源/私有玩家数据。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoObjectiveSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) EDemoObjectiveKind Kind = EDemoObjectiveKind::KillAll; // 本场目标类型。
    UPROPERTY(BlueprintReadOnly) EDemoObjectiveState State = EDemoObjectiveState::Unprepared; // 仅服务器写入的目标状态。
    UPROPERTY(BlueprintReadOnly) EDemoControlStatus ControlStatus = EDemoControlStatus::Inactive; // 控制点占区状态。
    UPROPERTY(BlueprintReadOnly) int32 Completed = 0; // 破坏完成设施数或完成控制点数。
    UPROPERTY(BlueprintReadOnly) int32 Total = 0; // 设施2/控制1或3，取本场真实锚点数；歼灭人数由Remaining展示。
    UPROPERTY(BlueprintReadOnly) int32 Remaining = -1; // 待生成+存活敌人数；-1表示尚未取得刷怪事实，禁止零值提前清关。
    UPROPERTY(BlueprintReadOnly) FVector ActiveLocation = FVector::ZeroVector; // 当前目标/下个控制点世界cm，HUD导航使用。
    UPROPERTY(BlueprintReadOnly) float Radius = 0.f; // 控制区半径cm，其他任务0。
    UPROPERTY(BlueprintReadOnly) float ProgressSeconds = 0.f; // 当前点累计秒数，仅服务器确定。
    UPROPERTY(BlueprintReadOnly) float RequiredSeconds = 0.f; // 每点要求秒数。
    UPROPERTY(BlueprintReadOnly) float TransitionRemaining = 0.f; // 下一点激活的剩余游戏秒，暂停冻结。
    UPROPERTY(BlueprintReadOnly) float ServerTime = 0.f; // 本快照World服务器秒数，可用于UI平滑但不能裁决。
};
