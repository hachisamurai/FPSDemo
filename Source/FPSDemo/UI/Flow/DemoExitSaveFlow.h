#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/DemoMenuTypes.h"
#include "Player/DemoCloudSync.h"
#include "DemoExitSaveFlow.generated.h"

/** MenuFlow拥有的可取消退出等待对象；不直接退出、不持有PC，不改变页面或World暂停。 */
UCLASS()
class FPSDEMO_API UDemoExitSaveFlow : public UObject
{
    GENERATED_BODY()
public:
    /** 新建/重试退出意图，留0.15秒使UI先绘制；bRetry仅改变提示，不跳过本地保存。 */
    void Begin(bool bRetry=false);
    /** 本地保存结果由协调器提交；bSuccess=false不能允许仅本地退出，Detail为错误说明。 */
    void CompleteLocal(bool bSuccess,const FString& Detail);
    /** 本地成功后进入GI云屏障等待；实际HTTP仍由CloudSync唯一管理。 */
    void WaitForCloud();
    /** Result来自GI屏障，Detail只用于显示，绝不用文案决定成功。 */
    void ObserveCloud(EDemoCloudExitState Result,const FString& Detail);
    /** 本地/云全部满足后延迟1秒反馈；Detail为最终显示文案。 */
    void MarkSaved(const FString& Detail);
    /** 取消等待/PC卸载时幂等关闭，不删除耐久云发件箱。 */
    void Cancel();
    /** 只读状态与期限；时间使用FPlatformTime，暂停不会冻结。 */
    EDemoQuitState GetState() const;
    bool IsLocalSaved() const;
    bool IsLocalDue() const;
    bool CanFinish() const;
    const FString& GetMessage() const;
private:
    EDemoQuitState State=EDemoQuitState::Idle; // 此对象唯一退出操作状态，页面路由另由MenuFlow持有。
    bool bLocalSaved=false; // 只有协调器明确完成本地保存后置true。
    double NextActionTime=0; // 单调秒数，首帧延迟/成功展示时限，不使用World计时器。
    FString Message; // 当前退出反馈，不含云认证数据。
};
