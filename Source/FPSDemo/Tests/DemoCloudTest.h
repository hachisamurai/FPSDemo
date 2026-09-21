#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoCloudTest.generated.h"

/** 显式-DemoCloudTest=upload/restore/offline/exit-online/exit-offline + GUID隔离身份的真实世界/HTTP回归。 */
UCLASS(NotBlueprintable, Transient)
class ADemoCloudTest : public AActor
{
    GENERATED_BODY()
public:
    /** 普通启动不创建；暂停仍逐帧观察退出屏障，网络请求仍由真实子系统限频。 */
    ADemoCloudTest();
    /** DeltaSeconds为游戏帧间秒数；登录等待使用单调时钟，失败有240秒上界。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** Condition与Name是稳定语义断言，失败退出当前隔离进程。 */
    bool Check(bool Condition, const TCHAR* Name);
    int32 Step = 0; // 0等待/1战斗/2确认；10..14为退出专测，跨进程状态由真实SaveGame承担。
    double Started = 0; // 单调秒时钟，不因关卡时间缩放而延长超时。
    FString Mode; // 固定战役与exit-online/exit-offline退出模式，其他值拒绝。
    double ExitObservedAt = 0; // 保存成功后3秒还未结束进程则失败，单调秒数不受暂停影响。
    bool bFailed = false; // 第一次失败后不再推进世界或继续断言。
};
