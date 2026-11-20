// 职责：阶段4真实CommonUI页面回归。使用：Editor独立进程-DemoSessionUITest并指定隔离GameUserSettingsINI。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h" // 真实Slate键盘夹具签名。
#include "DemoSessionUITest.generated.h"
UCLASS(Transient,NotBlueprintable)
class FPSDEMO_API ADemoSessionUITest : public AActor
{
    GENERATED_BODY()
public:
    /** 暂停仍Tick，输入/截图在真实渲染帧之间推进。 */
    ADemoSessionUITest();
    /** 只在Editor执行；静态步骤跨原OpenLevel保留，非Editor没有命令入口/故障注入。 */
    virtual void Tick(float DeltaSeconds) override;
private:
#if WITH_EDITOR
    /** 记录实际断言，失败立即退出隔离测试进程。 */
    bool Check(bool Value,const TCHAR* Reason);
    /** 只调用当前真实UMG按钮，缺失/禁用都失败。 */
    bool Press(FName Action);
    /** 向真实Slate焦点发送按下/松开Esc或Tab，验证CommonUI返回。 */
    void Key(FKey Input);
    /** 设置跨World下一步与真实秒延迟；不等待暂停World时钟。 */
    void Next(int32 Stage,double Delay=.3);
    /** 真实UI截图，目录由引擎Saved/Screenshots管理。 */
    void Capture(const TCHAR* Name);
#endif
};
