// 职责：FP-HANDAIM-20261120 本人FP枪臂“手部瞄准+双臂两骨IK”后处理代理。参考UE5.8 FirstPerson Shooter模板Ctrl_HandAdjusment
// （AimBone对准瞄准点、TwoBoneIK、左手贴枪、弹簧平滑）的思路，在UE5.4用纯C++实现，不复制模板资产/代码，不需修改AnimBP。
// 使用：UDemoFPAnimInstance::CreateAnimInstanceProxy创建；表现组件每帧SetFirstPersonHandAim写输入，代理在主图求值后修改组件空间手臂。
// 依赖：AnimationCore TwoBoneIK、FCSPose；工作线程只读PreUpdate复制的纯值，不访问Actor/World。
#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/DemoFPAnimInstance.h"

/** FP主实例代理：先求值原动画图，再把握枪手绕握点小角度转向准星目标并以两骨IK维持双臂/左手与枪的作者相对关系。 */
struct FDemoFPHandAimProxy : public FAnimInstanceProxy
{
    /** Instance为所属UDemoFPAnimInstance；构造只保存基类状态。 */
    explicit FDemoFPHandAimProxy(UAnimInstance* Instance);
    /** 游戏线程：复制主实例最新手部瞄准输入和本帧DeltaSeconds，工作线程之后只读这些值。 */
    virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
    /** 工作线程：求值原图后施加手部瞄准；始终返回true表示已自行求值根节点（未激活时等同原图输出）。 */
    virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override;
private:
    /** 在已求值的Output上施加修正；骨缺失/无解时保持原姿并返回false（调用方记录原因）。 */
    bool ApplyHandAim(FPoseContext& Output);
    FDemoFPHandAimInput Input; // PreUpdate复制的输入快照，求值期间不变。
    float DeltaSeconds=0.f; // 本次更新游戏秒，用于平滑与权重淡入淡出；暂停时为0不推进。
    float Weight=0.f; // 当前启用权重[0,1]，跨帧状态，只由工作线程求值写入（同一实例求值串行）。
    FQuat SmoothedCorrection=FQuat::Identity; // 上一帧平滑后的组件空间修正旋转，跨帧状态。
    FName LastFailure=NAME_None; // 最近一次失败原因，仅用于去重日志。
    float StatSeconds=0.f; // 诊断汇总窗口已累计的游戏秒；满5秒输出一次FP_HAND_AIM_STATS后清零，只在激活期间累计。
    float StatMaxDesiredDeg=0.f; // 窗口内限幅前所需最大修正角（度），用于判断“没生效”还是“需要的角度本来就小”。
    float StatMaxAppliedDeg=0.f; // 窗口内实际施加最大修正角（度，含限幅/平滑/权重）。
    int32 StatFrames=0; // 窗口内成功施加帧数。
};
