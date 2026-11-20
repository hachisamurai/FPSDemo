// 职责：仅Editor生成R04字体/候选美术的真实UE资产；不覆盖旧UI、WBP、玩家数据或人工编辑的已有候选。
// 使用：先运行Tools/Unreal/prepare_r04_fonts.py，再以-run=FPSDemoEditor.DemoCreateR04Assets启动Editor-Cmd。
#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DemoCreateR04AssetsCommandlet.generated.h"

/** 依赖UnrealEd正式FontFace/Texture API与SavePackage；复合字体保存UObject引用，不序列化进程临时Font指针。 */
UCLASS()
class UDemoCreateR04AssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    /** 离线资产命令，不启动游戏World、服务器、存档或云服务。 */
    UDemoCreateR04AssetsCommandlet();
    /** Params目前无可写路径/覆盖开关；只创建固定R04路径并核对已有字体源字节，返回0成功/1失败。 */
    virtual int32 Main(const FString& Params) override;
};
