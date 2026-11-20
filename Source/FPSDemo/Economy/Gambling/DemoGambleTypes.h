// 职责：抽取账本与展示值结构。使用：保存稳定ID/等级并经天赋目录查询策略；Validate在事务提交和读档时拒绝非法值。
#pragma once
#include "CoreMinimal.h"
#include "Economy/Gambling/DemoCasinoTypes.h" // V7待结货箱，网络必须脱敏。
#include "DemoGambleTypes.generated.h"

/** 同一玩家同一检查点的赌博账本；不持Actor，随个人钱包OwnerOnly复制。 */
USTRUCT()
struct FPSDEMO_API FDemoGambleProgress
{
    GENERATED_BODY()
    UPROPERTY() TArray<FName> Talents; // 本轮已获得的目录ID，每种只存一次；没有槽位限制，死亡/结束清空。
    UPROPERTY() TArray<int32> TalentRanks; // 与Talents对齐的等级，正整数；空数组兼容旧V6档（已拥有均为1级），OwnerOnly复制。
    /** Id为目录键；未拥有返回0，旧档缺少等级返回1，不修改只读快照。 */
    int32 Rank(FName Id) const;
    /** Id为本次中奖目录键；首次获得1级，重复增加1级；溢出/非法账本拒绝且不改数据。 */
    bool UpgradeTalent(FName Id);
    UPROPERTY() TArray<FName> Relics; // 永久遗物白名单，随当前存档成员保留。
    UPROPERTY() FName EquippedRelic; // None为不装备；必须已经解锁，Hub才可更换。
    UPROPERTY() TArray<int32> Failures = {0,0,0}; // V6旧路线计数0..100，仅保留存档兼容；V7不读作概率、不更新。
    UPROPERTY() int32 Node = 0; // V6旧节点字段，保留读写/复制兼容；新交易不更新，也不据此限制抽取。
    UPROPERTY() int32 Attempts = 0; // V6旧次数字段0..100，仅兼容旧档；不再累计，避免第101次触发旧存档范围校验。
    UPROPERTY() int32 Serial = 0; // 成功提交的赌博/装配版本，确认请求必须匹配，跨重连保留。
    UPROPERTY() FDemoArcadeRound Arcade; // V8反应堆/骰阵/靶场未结局面，永久保留至结算，与Mine互斥。
    UPROPERTY() FDemoMineRound Mine; // V7唯一未结金币货箱；死亡/通关保留，公开副本移除雷位。
    UPROPERTY() bool bRescueUsed = false; // 医疗器本轮已触发；换遗物不重置，结束挑战才重置。
    UPROPERTY() FString LastResult; // 最近已提交结果，重连可查看，不含随机种子。
    UPROPERTY() FDemoCasinoResult LastSlotsReceipt; // 最近一次已保存Mode1事实，随个人槽OwnerOnly/磁盘；默认严格空，其他六玩法不覆盖，禁止解析LastResult重建。
    /** 仅验证最近老虎机耐久回执：canonical-empty或三个合法符号/已提交Serial/Paid，无其他模式字段；不开奖或发奖。 */
    bool ValidateLastSlotsReceipt() const;
    /** 白名单、去重、范围和装备从属关系，非法值不进入钱包或云存档。 */
    bool Validate() const;
    /** 结束挑战清临时收益/旧兼容字段/医疗消耗；永久遗物、旧兼容计数、未结货箱/序号/最近老虎机耐久回执保留。 */
    void ResetRun();
};

/** 奖池卡片的纯展示值；服务器生成，UI不解析中文结果、不持有Gameplay对象。 */
USTRUCT()
struct FPSDEMO_API FDemoGambleReward
{
    GENERATED_BODY()
    UPROPERTY() FName Id; // 稳定奖励ID；no_reward为失败（金币可部分返还），attribute_0..2为属性，gold_route_0..2为金币，其余天赋ID。
    UPROPERTY() FString Title; // 本次报价的服务器展示名，滚动期间不随新报价替换。
    UPROPERTY() FString Description; // 权威配置生成的效果说明，仅菜单/回执复制，不进入存档。
};

/** 仅随成功落盘的命令回执发送；不进入存档，不接受客户端提交。 */
USTRUCT()
struct FPSDEMO_API FDemoGambleReveal
{
    GENERATED_BODY()
    UPROPERTY() int32 Serial = INDEX_NONE; // 已提交账本序号；-1代表无抽取（装配/拒绝/回滚）。
    UPROPERTY() int32 Choice = INDEX_NONE; // 0..6抽取项目，用于与本地等待意图核对。
    UPROPERTY() TArray<FDemoGambleReward> Rewards; // 本次扣款前的完整实际候选池，必要时含未中奖卡。
    UPROPERTY() FName ResultId; // 已提交结果，动画只能停靠此ID，不能再次随机。
};

/** 服务端报价投影；客户端无需加载相同配置也能显示真实概率和价格。 */
USTRUCT()
struct FPSDEMO_API FDemoGambleOffer
{
    GENERATED_BODY()
    UPROPERTY() int32 Choice = 0; // 0..2属性、3天赋、4..6金币娱乐入口、7卸下、8..11遗物购买/装备。
    UPROPERTY() int32 Cost = 0; // 0..3银币，其余金币；遗物已拥有时装配免费。
    UPROPERTY() float Chance = 0.f; // 当前实际成功率，金币路线含本人情报加成；遗物购买为1。
    UPROPERTY() FString Title; // 权威展示名。
    UPROPERTY() FString Detail; // 权威效果/金币到账及净损益/情报/奖池说明。
    UPROPERTY() FString BlockReason; // 非空禁用；服务器仍重新验证。
    UPROPERTY() TArray<FDemoGambleReward> Rewards; // 当前可抽取的完整卡片池；不是按卡片数量计算概率。
};

namespace DemoGamble
{
    /** 返回可信GA目录大小；没有天赋携带数量限制，旧索引只追加不重排。 */
    FPSDEMO_API int32 TalentCount();
    /** Index按固定协议顺序，非法返回None；不得重排现有ID。 */
    FPSDEMO_API FName Talent(int32 Index);
    /** Index=0..3固定永久遗物，未知返回None；不与临时天赋目录混用。 */
    FPSDEMO_API FName Relic(int32 Index);
    /** Id为固定目录键，返回显示名或空；不做权限判断。 */
    FPSDEMO_API FString Name(FName Id);
    /** Id为天赋白名单，返回当前服务器配置的收益说明，便于下注前知晓候选效果。 */
    FPSDEMO_API FString DescribeTalent(FName Id, int32 Rank = 1); // Rank为要展示的正整数等级；非正值不显示效果。
    /** Rank为天赋等级；1级保持原效果，之后对数增长避免无限抽取产生指数弹体/数值爆炸。0返回0。 */
    FPSDEMO_API float Strength(int32 Rank);
    /** Base为1级比例0..1；Rank为已拥有等级；递增接近1，供装填/贯穿共用。 */
    FPSDEMO_API float Fraction(float Base, int32 Rank);
}
