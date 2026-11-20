// 职责：GI启动时加载默认玩家动画依赖，避免加入Listen World后在首个Avatar构建时阻塞网络时钟；不访问存档或网络。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DemoPlayerAssetPreload.generated.h"
class UDemoPlayerAppearance;

namespace DemoAppearanceAssets
{
    #if WITH_EDITOR
    /** Editor进程显式候选覆盖路径；无开关返回无效路径，GI和Pawn所有初始化入口必须共用本解析，不覆盖正常自定义Appearance。 */
    FPSDEMO_API FSoftObjectPath EditorOverridePath();
    #endif
    /** 默认外观路径；Editor显式候选开关仅供隔离验收，非Editor始终为正式Appearance。 */
    FPSDEMO_API FSoftObjectPath DefaultPath();
}

/** 依赖在本GI存活期间强引用保活；与Lyra先加载Experience资源再建立Pawn的生命周期原则一致。 */
UCLASS()
class FPSDEMO_API UDemoPlayerAssetPreload : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    /** GI创建后、连接游戏World前同步准备资源；失败记录并由原Presentation缺资源分支处理。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** GI退出释放本次引用，不修改共享资源或角色骨架。 */
    virtual void Deinitialize() override;
private:
    // 本GI持有的默认外观以及其强引用动画依赖；不复制、不保存，不阻止Editor结束清理试玩数据。
    UPROPERTY(Transient) TObjectPtr<UDemoPlayerAppearance> Appearance;
};
