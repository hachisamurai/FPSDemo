// 职责：无业务副作用的奖励带状态。使用：Begin校验权威结果，Update逐帧或Finish立即停靠；不执行随机、扣费或保存。
#pragma once
#include "CoreMinimal.h"
#include "Economy/Gambling/DemoGambleTypes.h"

/** MenuFlow拥有的本地纯UI状态；真实时间驱动，与游戏暂停、随机数、经济事务无关。 */
struct FPSDEMO_API FDemoGambleReel
{
    /** Reveal为已提交回执，Message为最终提示，Now为单调秒；非法池/结果拒绝，不伪造替代中奖。 */
    bool Begin(const FDemoGambleReveal& Reveal, const FString& Message, double Now);
    /** Now为单调秒；返回本次是否首次到达终点，掉帧时精确钳到目标，不累计误差。 */
    bool Update(double Now);
    /** 关闭/换关/卸载只撤销本地动画，不撤销已提交费用和奖励。 */
    void Reset();
    /** 只停靠已校验的服务器结果；未Begin或已经停靠时幂等返回false，不生成奖励。 */
    bool Finish();
    TArray<FDemoGambleReward> Cards; // 本次不可变奖池副本，所有候选每轮各展示一次，无Actor生命周期问题。
    FString ResultMessage; // 滚动结束才展示的回执文本。
    FName ResultId; // 测试/显示使用的权威停靠ID。
    float Position = 0.f; // 中央指针对应的连续卡片序号，单位卡宽，终点为整数。
    int32 Target = 0; // 至少完整滚动四轮后命中结果的绝对序号。
    double StartSeconds = 0.; // FPlatformTime秒，不使用暂停的World时间。
    bool bRolling = false; // true期间禁止重复抽取；false且Cards非空表示结果已展示。
    static constexpr float Duration = 3.2f; // UI展示秒数；不延迟服务器结算或保存。
};
