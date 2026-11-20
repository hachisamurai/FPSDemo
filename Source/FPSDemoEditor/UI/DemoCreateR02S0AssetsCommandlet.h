// 职责：仅Editor创建R02 S0主题、四衬线FontFace、两复合字体与两UI材质，或在新进程只读核验已有候选。
// 使用：先准备Art/UI/Redesign20261116/R02_Cinematic/S0/Fonts，再运行-run=FPSDemoEditor.DemoCreateR02S0Assets。
// 依赖：UnrealEd/MaterialEditor正式作者API及Runtime UDemoUITheme；绝不修改R04、WBP、游戏配置或玩家进度。
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DemoCreateR02S0AssetsCommandlet.generated.h"

/** 固定/Game/UI/RedesignR02候选目录；已有不同内容拒绝而非覆盖，不提供外部路径/强制覆盖参数。 */
UCLASS()
class UDemoCreateR02S0AssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    /** 初始化为离线Editor作者命令，不启动游戏World/业务/HTTP/云。 */
    UDemoCreateR02S0AssetsCommandlet();

    /** 生成并验证固定S0候选；Params仅保留引擎签名，无写入扩权；返回0技术检查通过，1为缺源/不同内容/保存失败。 */
    virtual int32 Main(const FString& Params) override;
};
