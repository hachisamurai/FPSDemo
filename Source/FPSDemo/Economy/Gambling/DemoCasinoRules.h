// 职责：金币玩法的纯规则接口。使用：事务持有FRandomStream，结果只计算一次；UI仅调用Quote，不能发奖。
#pragma once
#include "Economy/Gambling/DemoCasinoTypes.h"
namespace DemoCasino
{
    /** Text为键入/粘贴的十进制正整数；只受int32存储范围约束，失败不修改Stake，Error可展示。余额仍由事务验证。 */
    FPSDEMO_API bool ParseStake(const FString& Text,int32& Stake,FString& Error);
    /** Rules已校验；按Intent计算实际概率和完整到账金额，不产生随机。 */
    FPSDEMO_API FDemoCasinoQuote Quote(const FDemoCasinoRules& Rules,const FDemoCasinoIntent& Intent);
    /** 已校验权重数组，Random为调用者独占随机流；返回一次按权重采样的索引。 */
    FPSDEMO_API int32 Draw(const TArray<double>& Weights,FRandomStream& Random);
    /** Symbols为固定三个0..4索引；计算唯一匹配组合倍率，非法返回0。 */
    FPSDEMO_API double SlotMultiplier(const FDemoCasinoRules& Rules,const TArray<int32>& Symbols);
    /** Number为00..99，Intent已经通过报价验证；匹配区间/奇偶。 */
    FPSDEMO_API bool Matches(const FDemoCasinoIntent& Intent,int32 Number);
    /** Stake正数，Multiplier非负有限；正常按乘积向下取整，无业务封顶。溢出/非法返回INDEX_NONE，调用者必须拒绝且不扣款。 */
    FPSDEMO_API int32 Pay(int32 Stake,double Multiplier);
    /** 生成均匀固定棋盘及全部冻结到账；Intent已合法，Random仅本次事务持有。 */
    FPSDEMO_API FDemoMineRound OpenMine(const FDemoCasinoRules& Rules,const FDemoCasinoIntent& Intent,FRandomStream& Random);
    /** 对候选Mine翻格/领取；失败不修改Mine或Gold，成功只改候选并填Result，外层负责保存与回滚。 */
    FPSDEMO_API bool AdvanceMine(FDemoMineRound& Mine,const FDemoCasinoIntent& Intent,int32& Gold,FDemoCasinoResult& Result,FString& Error);
    /** Index固定0..4符号的展示名；不赋予弹药解锁。 */
    FPSDEMO_API FString SymbolName(int32 Index);
    /** UI稳定入口映射；旧遗物7..11保留，新玩法使用12..15，非法返回-1。 */
    FPSDEMO_API int32 ModeForChoice(int32 Choice);
    FPSDEMO_API int32 ChoiceForMode(int32 Mode);
    /** Mode固定0..6标题，不含结果或权限。 */
    FPSDEMO_API FString ModeName(int32 Mode);
    /** 追加玩法配置校验；Error失败原因，返回false时禁止新下注。 */
    FPSDEMO_API bool ValidateArcadeRules(const FDemoCasinoRules& Rules,FString& Error);
    /** 新玩法3..6报价及最大可达收益；Rules已合法，不修改状态。 */
    FPSDEMO_API FDemoCasinoQuote QuoteArcade(const FDemoCasinoRules& Rules,const FDemoCasinoIntent& Intent);
    /** 生成持续玩法初局；只在通过报价后使用，Random为服务端独占流。 */
    FPSDEMO_API FDemoArcadeRound OpenArcade(const FDemoCasinoRules& Rules,const FDemoCasinoIntent& Intent,FRandomStream& Random);
    /** 一次充能/保留重掷/领取；仅改候选，失败不扣款；成功由外层原子保存。 */
    FPSDEMO_API bool AdvanceArcade(FDemoArcadeRound& Round,const FDemoCasinoIntent& Intent,int32& Gold,FDemoCasinoResult& Result,FRandomStream& Random,FString& Error);
    /** 五枚1..6骰面分类，唯一最高组合；非法返回-1。 */
    FPSDEMO_API int32 DiceTier(const TArray<int32>& Dice);
    FPSDEMO_API FString DiceTierName(int32 Tier);
    /** 精确有限生命状态递推，返回A获胜概率；Rules已通过验证。 */
    FPSDEMO_API double ArenaChance(const FDemoCasinoRules& Rules);
    /** 真实随机战斗回放与胜者；非法生命契约返回false，外层不扣币且不公开候选。 */
    FPSDEMO_API bool RunArena(const FDemoCasinoRules& Rules,FRandomStream& Random,FDemoCasinoResult& Result);
}
