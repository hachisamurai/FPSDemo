// 职责：常驻Hub唯一的本机有界Profile消费者；Blockout默认子对象，复用原Volume，不创建全局渲染状态。
// 使用：HubLightingSettings明确Profile/原Volume对象名和Brush身份；依赖共享PP白名单、原priority10和室内20层。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Scene.h"
#include "DemoHubAtmosphereComponent.generated.h"

UCLASS()
class FPSDEMO_API UDemoHubAtmosphereComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 默认子对象只配置本机生命周期，无Tick/复制；不在Editor构造时加载或改PP。 */
    UDemoHubAtmosphereComponent();
    /** Game/Listen客户端BeginPlay一次验证原PersistentLevel Volume后应用字段；失败保原件并记原因。 */
    virtual void BeginPlay() override;
    /** 旅行/结束只恢复仍属于自己的白名单字段，外部冲突保留；不整份回写PP。 */
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    /** GT一次绑定：显式Profile、唯一消费者/tag/对象、Brush空间和原混合参数全部合法才认领。 */
    void ApplyHubAtmosphere();
    /** 幂等撤销本消费者，调用时原对象若已销毁不再解引用；不保存地图。 */
    void RestoreHubAtmosphere();
    UPROPERTY(Transient) TObjectPtr<class UDemoRoomAtmosphereProfile> Profile; // 保活加载资产，纯本地只读，离开本World释放。
    UPROPERTY(Transient) FPostProcessSettings Baseline; // 原对象完整内存快照供认领字段恢复，绝不整份回写；UPROPERTY保引用有效。
    UPROPERTY(Transient) FPostProcessSettings Applied; // 通过BuildSettings的白名单集合，决定本组件仅拥有的字段。
    TWeakObjectPtr<class APostProcessVolume> Target; // World拥有原Volume；跨生命周期只弱引用。
    bool bOwnsFields = false; // 应用成功才true；重复清理不会碰其他写入者。
};
