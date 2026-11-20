// 职责：Editor临时World的阈值转身实机回归；-DemoViewTurnTest显式启动，观察真实Actor、相机和共享枪口，不改动画缓存。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoViewTurnTest.generated.h"

UCLASS(Transient,NotBlueprintable)
class ADemoViewTurnTest : public AActor
{
    GENERATED_BODY()
public:
    /** 创建PostUpdate探针，使本帧动画和武器展示已求值后再观察。 */
    ADemoViewTurnTest();
    /** DeltaSeconds为游戏秒；只写控制器输入，阶段超过宽限后检验真实枪口，最长30秒失败退出。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** Reason为本次失败原因；记录一次并退出测试进程，不更改正式存档。 */
    void Fail(const TCHAR* Reason);
    int32 Stage=-1; // -1等待Avatar；后续按独立预期序列驱动，不复制。
    double StageStart=0.; // 本World秒数，用于有界收敛观察。
    float InitialBody=0.f; // 阶段开始身体角，验证未把越界变成一次瞬移。
    bool bSawIntermediate=false; // 越界阶段必须观察到身体追赶中间态，防止只检查最终值而漏掉跳变。
    int32 StableSamples=0; // 稳定后真实枪口采样数，缺样本不能通过。
};
