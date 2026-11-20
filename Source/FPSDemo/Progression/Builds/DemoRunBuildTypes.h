// 职责：本轮核心的可保存选择和每枪不可变数值协议。使用：PS组件保存选择，ShotContext保存射击值；不包含钱包、Actor或ASC所有权。
#pragma once
#include "CoreMinimal.h"
#include "DemoRunBuildTypes.generated.h"

namespace DemoRunBuild
{
    /** 可信核心目录；UI/Flow/检查点共用，索引越界返回None。 */
    FPSDEMO_API FName CoreAt(int32 Index);
    FPSDEMO_API int32 CoreCount();
    FPSDEMO_API bool IsCore(FName Id);
    /** Id为核心B01..B09或其A..D分支；返回统一目录名称，未知ID为空，不由HUD硬编码。 */
    FPSDEMO_API FText Title(FName Id);
    /** 返回当前集中Settings数值生成的简短玩法说明；服务器用于选择投影，客户端不据此裁决。 */
    FPSDEMO_API FText Describe(FName Id);
}

/** 核心身份与互斥分支；安全检查点保存，计数/充能/目标引用绝不保存。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoRunBuildSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName CoreId; // None尚未选择；B01..B09中央目录，新增ID不重解释旧分支。
    UPROPERTY(BlueprintReadOnly) FName Branch3; // 第一组完整ID，例如B01-A/B01-B；只能属于当前核心。
    UPROPERTY(BlueprintReadOnly) FName Branch7; // 第二组完整ID，例如B01-C/B01-D；不能绕过第一组领取。
    UPROPERTY(BlueprintReadOnly) int32 PendingBranchStage=0; // 0无待选，3/7等待本人领取；选择一次后清零。
    UPROPERTY(BlueprintReadOnly) int32 Revision=0; // 非负选择修订号，保存/UI确认使用，不是网络协议版本。
    UPROPERTY(BlueprintReadOnly) bool bLocked=false; // 提交本轮出发后锁核心；个人死亡不解锁。
    /** 纯值合法性检查；拒绝未知ID、跨核心分支、重复待选及无核心残留。 */
    bool Validate() const;
};

/** 服务器一次扣弹的核心快照；每颗弹丸共享，同枪穿透不会再次消费窗口。 */
USTRUCT()
struct FPSDEMO_API FDemoRunBuildShotSnapshot
{
    GENERATED_BODY()
    UPROPERTY() FName CoreId; // 发射时身份，切阶段/换配置不能改变已经发射的来源。
    UPROPERTY() FName Branch3; // 发射时第一组选择。
    UPROPERTY() FName Branch7; // 发射时第二组选择。
    UPROPERTY() float DirectMultiplier=1.f; // 仅原始直击乘区，派生弹射/爆炸/DOT不得继承。
    UPROPERTY() float PrecisionBonus=0.f; // 只给本枪首次有效敌人弱点命中，0表示普通枪。
    UPROPERTY() float SpreadScale=1.f; // 发射准备使用的锥角倍率；稳定分支为0.65。
    UPROPERTY() float RecoilScale=1.f; // 纯视觉后坐倍率，权威快照随开火RPC传递。
    UPROPERTY() int32 ChargeGeneration=0; // 突击冲刺批次，用于每次冲刺最多一次击杀移速。
    UPROPERTY() bool bAssaultCharged=false; // 已冻结突击充能，不依赖命中时剩余窗口。
    UPROPERTY() bool bPrecisionCharged=false; // 已冻结锁定射击；此枪不再次积层。
    UPROPERTY() int64 CommitSerial=0; // 服务器真实付费发射全序，跨玩家比较新枪与标记，绝不持久化。
    UPROPERTY() int32 ExpansionGeneration=0; // 扩展核心本次凭据批次，拒绝旧弹消费新供弹。
    UPROPERTY() bool bRhythmCharged=false; // 此枪最终发射时冻结节律供弹资格。
    UPROPERTY() FVector AimDirection=FVector::ForwardVector; // 最终发射的服务器瞄准方向，延迟命中不受后来转身改变。
    UPROPERTY() float FireInterval=0.f; // 发射时冻结的武器间隔秒，区分慢射速重武器（如拉栓狙）。
    UPROPERTY() int32 PlannedProjectiles=1; // 发射时冻结的本枪弹体总数，用于多弹丸与单发分流。
    UPROPERTY() bool bBoltAction=false; // 发射时冻结是否逐发拉栓，供B04-B切换强化狙分支。
    UPROPERTY() bool bCloseQuartersEmpowered=false; // 发射时冻结B08-A散布收束凭据。
    UPROPERTY() bool bMarksmanCharged=false; // 发射时冻结B09满蓄贯杀凭据。
    UPROPERTY() float MarksmanBaseBonus=0.f; // B09满蓄基础直击增伤比例。
    UPROPERTY() float MarksmanExtraBonus=0.f; // B09远距或弱点额外直击增伤比例。
};

/** 本人HUD的服务器计数；ServerEndTime使用World游戏秒，暂停不扣时间，不存档。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoRunBuildRuntimeView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 PrecisionStacks=0; // 精准0..2，达到3转为充能。
    UPROPERTY(BlueprintReadOnly) int32 ChargedShots=0; // 突击0..2或精准0..1。
    UPROPERTY(BlueprintReadOnly) float ServerEndTime=0.f; // 计数/充能截止，UI只显示不裁决。
    UPROPERTY(BlueprintReadOnly) FName ActionState; // 扩展核心动作提示稳定键，拥有者只读。
    UPROPERTY(BlueprintReadOnly) int32 ActionStacks=0; // 当前命中/充能/预算值，不进入检查点。
    UPROPERTY(BlueprintReadOnly) int32 ActionCapacity=0; // 当前显示阈值或预算上限。
    UPROPERTY(BlueprintReadOnly) float CooldownEndTime=0.f; // 扩展核心冷却截止World秒。
};
