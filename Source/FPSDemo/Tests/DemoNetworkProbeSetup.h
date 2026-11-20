// 职责：Editor双端探针共用的只读准备门；由真实UI完成核心/Ready/出发，不代远端玩家提交业务意图。
// 使用：等待阶段调用Poll，Ready后进入原观测；纯Evaluate供无World合同测试，非Editor不提供此工具入口。
#pragma once
#include "CoreMinimal.h"

#if WITH_EDITOR
class AActor;
namespace DemoNetworkProbeSetup
{
    inline constexpr int32 ContractVersion=2; // 半自动正式UI准备、独立准备时钟；不代表已通过LAN验收。
    enum class EResult : uint8 { Waiting,Ready,Failed }; // Waiting不视作出发失败，Failed保留明确诊断。
    enum class EPhase : uint8 { Lobby,Hub,Combat,Other }; // 测试值分类，不反向写GameState阶段。
    struct FSnapshot
    {
        bool bListenServer=false; // 来自当前World网络模式，不接收客户端声明。
        int32 RegisteredPlayers=0; // 正式Roster中已登记的当前Pawn数量，本探针只支持恰好2人。
        EPhase Phase=EPhase::Lobby; // 当前服务端公开阶段的只读投影。
        int32 Level=0; // 真实关号，Combat只允许首关，不能用旧场次继续观察。
        bool bHubOnly=false; // 仅原DashFlowHub模式，保持安全区技能观测边界。
        bool bCurrentSession=false; // 当前Flow的活跃Session与RunId/Level匹配。
        bool bObjectiveActive=false; // 目标已真正激活，不以Phase一个字段猜测准备完成。
        bool bKillAll=false; // 原Player清关复活仅杀自然敌人，其余探针也排除自然完成的控制目标。
    };
    /** 纯值准备裁决；Elapsed/Timeout为本端单调墙钟秒，超时区间[30,600]，不创建World/Timer或修改游戏。 */
    FPSDEMO_API EResult Evaluate(const FSnapshot& Snapshot,double ElapsedSeconds,double TimeoutSeconds,FString& Reason);
    /** 构造期一次读取-DemoNetworkSetupTimeout=<30..600秒>，缺省180；非法值记录一次并回退，不读取凭据。 */
    FPSDEMO_API double ReadTimeoutSeconds();
    /** 仅权威探针等待时调用，读取正式Roster/Flow/Objective；不选择核心、不设Ready、不调用StartNextLevel。 */
    FPSDEMO_API EResult Poll(const AActor* Probe,bool bHubOnly,double ElapsedSeconds,double TimeoutSeconds,FString& Reason);
    /** 首个测试计划RPC可能先于GS/UI复制；本机阶段、活Pawn及菜单真正就绪才开始本端动作计时，不关闭菜单。 */
    FPSDEMO_API bool CanStartLocally(const AActor* Probe,bool bHubOnly);
}
#endif
