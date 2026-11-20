// 职责：仅 Editor 通过正式资产 API 创建可编辑的 CommonUI Widget Blueprint 外壳。PowerShell 使用 '-run=FPSDemoEditor.DemoCreateCommonUI'；默认不覆盖已有布局，显式 -RepairDefaultFonts 只修复丢失的标题字体。
#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DemoCreateCommonUICommandlet.generated.h"

/** 依赖 FPSDemo 的原生 UI 类及 UMGEditor/Kismet/UnrealEd；生成资产供 Designer 调整布局，业务内容仍由原生页面填充。 */
UCLASS()
class UDemoCreateCommonUICommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    /** 配置离线 Editor 命令行生命周期，不创建服务器或游戏进度。 */
    UDemoCreateCommonUICommandlet();
    /** Params 为剩余参数；固定生成四个项目资产，-RepairDefaultFonts 仅修复两标题的空字体引用并保存；成功返回 0，验证/编译/保存失败返回 1。 */
    virtual int32 Main(const FString& Params) override;
};
