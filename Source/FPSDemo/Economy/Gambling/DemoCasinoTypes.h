// 职责：金币娱乐的公开配置/命令/回执与货箱存档值。使用：规则模块处理值，复制前必须调用PublicCopy移除未公开雷位。
#pragma once
#include "CoreMinimal.h"
#include "DemoCasinoTypes.generated.h"

/** 权威服务器配置；菜单传只读副本，客户端配置不能决定结果。 */
USTRUCT()
struct FPSDEMO_API FDemoCasinoRules
{
    GENERATED_BODY()
    /** 初始化默认概率与倍率；INI可覆盖，加载后必须Validate。 */
    FDemoCasinoRules();
    UPROPERTY() int32 MinBet=1; // 最低金币下注，正整数；默认1，不设置玩法下注上限。
    UPROPERTY() int32 BetStep=1; // UI金额加减步长，正整数；运算用int64，输入不要求整步。
    UPROPERTY() TArray<int32> Presets={10,20,50,100}; // 四个快捷下注额，均>=MinBet，不限制手动输入。
    UPROPERTY() TArray<double> NumberWeights; // 00..99各自开奖权重，非负有限且总和>0。
    UPROPERTY() TArray<double> NumberMultipliers; // 索引k-1为覆盖k个数字总到账倍率，k=1..50。
    UPROPERTY() TArray<double> SymbolWeights={1,1,1,1,1}; // 普通/火焰/冰冻/穿透/金币；各轴独立采用同一权重。
    UPROPERTY() TArray<double> TripleMultipliers={4,6,8,10,18}; // 三连的总到账倍率，索引同符号。
    UPROPERTY() double PairMultiplier=1.2; // 恰好一对总到账倍率，三连不叠加。
    UPROPERTY() TArray<int32> BombChoices={4,6,8}; // 三个难度的雷数，1..24且各不相同。
    UPROPERTY() double MineReturn=.95; // 货箱累计概率公式的返还系数，(0,1]。
    UPROPERTY() TArray<double> ReactorChances={.85,.8,.75,.7,.65}; // 各次充能条件成功率；1..10级，(0,1]，随局冻结。
    UPROPERTY() double ReactorReturn=.95; // 累计返还系数，(0,1]；无奖金封顶。
    UPROPERTY() TArray<double> DiceWeights={1,1,1,1,1,1}; // 六面战术骰权重，独立投掷；随局冻结。
    UPROPERTY() TArray<double> DiceMultipliers={0,.1,.4,.8,2,2,4,12}; // 无/一对/两对/三同/顺子/葫芦/四同/五同总到账倍率。
    UPROPERTY() TArray<int32> ArenaHealth={12,14}; // 两只机械兽初始生命，1..60；精确状态递推求赔率。
    UPROPERTY() TArray<int32> ArenaDamage={3,2}; // 每次命中扣血，1..60，交替攻击，随机先手各50%。
    UPROPERTY() TArray<double> ArenaHit={.8,.9}; // 两方命中率，(0,1]，不参考玩家输赢。
    UPROPERTY() double ArenaReturn=.94; // 赔率=返还系数/所选方精确胜率，(0,1]。
    UPROPERTY() int32 RangeSeconds=30; // 靶场真实时间限制，5..120秒，开始时冻结。
    UPROPERTY() int32 RangeShots=20; // 固定训练射击次数，1..100；不消耗玩家武器弹药。
    UPROPERTY() TArray<int32> RangeScores={0,5,10,15,20}; // 五档最低积分，递增；10个靶，身体1/头部2分。
    UPROPERTY() TArray<double> RangeMultipliers={0,.5,1,1.5,2.5}; // 五档总到账倍率，技能挑战可由熟练玩家稳定获利。
    /** 检查字段及数字/老虎机期望值；非法配置拒绝下注，Error为可展示原因。 */
    bool Validate(FString& Error) const;
    /** 完整公开配置的稳定指纹；确认后配置发生改变必须重新确认。 */
    uint32 Fingerprint() const;
};

/** 一次玩家意图；没有种子、开奖结果或服务器私有信息。 */
USTRUCT()
struct FPSDEMO_API FDemoCasinoIntent
{
    GENERATED_BODY()
    UPROPERTY() int32 Mode=0; // 0数字/1老虎机/2货箱/3反应堆/4战术骰/5斗兽场/6靶场。
    UPROPERTY() int32 Operation=0; // 0下注，1翻格/充能/重掷，2领取，3恢复靶场。
    UPROPERTY() int32 Stake=20; // 下注金币；翻格/领取必须等于原箱Stake。
    UPROPERTY() int32 Selection=0; // 数字：0连续区间，1奇数，2偶数。
    UPROPERTY() int32 Low=0; // 连续区间下界，00..99。
    UPROPERTY() int32 High=49; // 连续区间上界，>=Low且覆盖<=50。
    UPROPERTY() int32 Bombs=4; // 开箱所选雷数，必须属于配置。
    UPROPERTY() int32 Tile=INDEX_NONE; // 翻格索引0..24。
    UPROPERTY() int32 KeepMask=0; // 战术骰保留位0..4；不允许客户端提交骰面。
    UPROPERTY() uint32 RulesKey=0; // 下注确认时公开规则指纹；已开箱操作使用冻结金额不依赖新配置。
};

/** 可选扩展的唯一持续娱乐局面；均为已公开事实，无未来骰面/充能结果。由个人账本持有并复制给拥有者。 */
USTRUCT()
struct FPSDEMO_API FDemoArcadeRound
{
    GENERATED_BODY()
    UPROPERTY() int32 Mode=INDEX_NONE; // -1为空；3反应堆、4战术骰、6靶场。
    UPROPERTY() int32 Stake=0; // 原始下注正整数，未结期间不可修改。
    UPROPERTY() int32 Debited=0; // 实扣，Editor免扣为0；用于准确净收益。
    UPROPERTY() int32 Step=0; // 反应堆已成功次数；骰为0等待保留/重掷，靶场已命中靶数。
    UPROPERTY() TArray<int32> Values; // 骰5面1..6；靶场[最大枪数,五档积分]；反应堆为空。
    UPROPERTY() TArray<double> Chances; // 反应堆冻结每步成功率或骰六面权重；靶场为空。
    UPROPERTY() TArray<int32> Payouts; // 按级别/组合/档位冻结总到账；只允许可表示的非负int32。
    UPROPERTY() int64 DeadlineMs=0; // 靶场UTC截止毫秒，关闭/读档不重置；其他玩法0。
    UPROPERTY() int32 Seed=0; // 靶场公开布局种子，无经济随机权限；其他玩法0。
    UPROPERTY() int32 Shots=0; // 靶场服务器接受的枪数，未命中也消耗。
    UPROPERTY() int32 Heads=0; // 靶场头部命中数，0..Step。
    /** 只检查保存结构与冻结值，不用新配置重定价；空局字段必须全空。 */
    bool Validate() const;
};

/** 唯一未结货箱；普通快照严禁携带BombMask，公开副本只包含已开安全格。 */
USTRUCT()
struct FPSDEMO_API FDemoMineRound
{
    GENERATED_BODY()
    UPROPERTY() int32 Stake=0; // 0表示无待结货箱；非0为实际下注报价（Editor免扣仍保持）。
    UPROPERTY() int32 Debited=0; // 实际扣款0..Stake；Editor无限金币可为0，领取反馈按此计算真实净收益。
    UPROPERTY() int32 Bombs=0; // 冻结雷数1..24。
    UPROPERTY() int32 BombMask=0; // 25位服务端私有雷位，仅存档；PublicCopy置0。
    UPROPERTY() int32 OpenMask=0; // 已开安全格25位，公开给拥有者。
    UPROPERTY() TArray<int32> Payouts; // k个安全格的冻结总到账，索引k-1，长度25-Bombs。
    /** 非空货箱必须完整且无已开雷；空箱全部字段为空。供权威保存/恢复验证。 */
    bool Validate() const;
    /** 返回移除雷位的副本，供所有RPC和PS复制使用；不能写回权威账本。 */
    FDemoMineRound PublicCopy() const;
    /** 计算当前已开安全格数量，0..24；不修改状态。 */
    int32 OpenCount() const;
    /** 返回当前可领取金币，未开安全格为0；不发奖。 */
    int32 Available() const;
};

/** 已保存结果回执；未完成的货箱只返回公开进度，不包含雷位。 */
USTRUCT()
struct FPSDEMO_API FDemoCasinoResult
{
    GENERATED_BODY()
    UPROPERTY() int32 Mode=INDEX_NONE; // -1表示不是Casino回执。
    UPROPERTY() int32 Serial=INDEX_NONE; // 已保存个人序号，用于UI关联本次请求。
    UPROPERTY() int32 Number=INDEX_NONE; // 已开奖数字；其他模式-1。
    UPROPERTY() TArray<int32> Symbols; // 已保存三轴3项0..4或五骰5项1..6。
    UPROPERTY() int32 Paid=0; // 本次真正到账金币，不含尚未领取货箱金额。
    UPROPERTY() int32 Tile=INDEX_NONE; // 本次揭开格；没有翻格为-1。
    UPROPERTY() bool bBomb=false; // 本次翻格是否爆炸；不公开其他雷位。
    UPROPERTY() TArray<int32> Battle; // 斗兽场已保存回放：正数A攻击、负数B攻击，绝对值=伤害+1（含未命中）。
    UPROPERTY() int32 Winner=INDEX_NONE; // 斗兽场胜者0/1；骰最终组合档，其他玩法-1。
};

/** 同步报价展示，不传入服务器结算作为事实。 */
struct FPSDEMO_API FDemoCasinoQuote
{
    double Chance=0; // 数字命中/老虎机非零/开箱第一格安全概率。
    double Multiplier=0; // 数字命中倍率；老虎机组合和货箱逐格另展示。
    int32 Payout=0; // 数字命中到账/老虎机最高到账/货箱第一格到账，按倍率取整，溢出时Error非空。
    int32 MaximumPayout=0; // 所有可达结果最大到账；货箱最后一级，用于扣款前保护钱包溢出。
    FString Error; // 非空禁用，权威提交仍重算。
};
