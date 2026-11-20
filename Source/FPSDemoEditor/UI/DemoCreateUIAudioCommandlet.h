// 职责：仅Editor导入/验证5个自产R04 UI反馈音。使用-run=FPSDemoEditor.DemoCreateUIAudio，不启动游戏或修改旧资产。
// 依赖：AudioEditor SoundFactory与原始PCM WAV；固定候选目录，不接受任意输出路径。
#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DemoCreateUIAudioCommandlet.generated.h"
UCLASS()
class UDemoCreateUIAudioCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    /** 配置离线作者化运行，不创建Player/GI/云进度。 */
    UDemoCreateUIAudioCommandlet();
    /** Params不用于文件路径/覆盖开关；已有同名资产仅验证，返回0通过/1失败，原资产保留。 */
    virtual int32 Main(const FString& Params) override;
};
