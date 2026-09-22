#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoChallengeComponent.generated.h"

/** GameMode持有的本轮资格服务；不拥有账号永久通关事实或武器Actor。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoChallengeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick，只接收已成功提交的武器开火意图。 */
    UDemoChallengeComponent();
    /** RunId为新轮GUID；同ID重复初始化不恢复已经失去的资格。 */
    void BeginRun(const FString& RunId);
    /** RunId/Progress来自已验证检查点，Progress合法0未开火/1仅手枪/2失格；非法拒绝。 */
    bool RestoreProgress(const FString& RunId, int32 Progress);
    /** RunId必须匹配当前轮，WeaponId为真实开火的目录ID；先保存资格，再允许伤害发生。 */
    bool TryCommitWeaponShot(const FString& RunId, FName WeaponId);
    /** 停止后拒绝旧World/晚到开火，不改变已经保存的资格。 */
    void Stop();
private:
    FString ActiveRunId; // 当前World正在服务的轮次，读档恢复，不跨World持有Actor。
    int32 Qualification = 0; // 0未开火/1仅手枪/2不可逆失格，GameState只显示投影。
    bool bStopped = false; // 旅行关闭业务，避免旧World仍接受资格写入。
};
