#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "DemoCoopSpectatorCamera.generated.h"

class ADemoCharacter;
class ADemoPlayerController;

/** 每个本地死者独立拥有的第三人称观战视图；不复制、不Possess，也不读取远端私有第一人称动画。 */
UCLASS(Transient, NotBlueprintable)
class FPSDEMO_API ADemoCoopSpectatorCamera : public ACameraActor
{
    GENERATED_BODY()
public:
    /** 设置纯表现相机与PostPhysics更新；服务器只通过PC RPC发送要观察的队友。 */
    ADemoCoopSpectatorCamera();
    /** Observer为拥有此本地视图的PC，Target为服务器选出的队友；同步初始化位置，切换不生成新镜头。 */
    bool Observe(ADemoPlayerController* Observer, ADemoCharacter* Target);
    /** DeltaSeconds为World秒；只跟随目标，不向其Controller或Movement写入。被正式复活镜头替换后自清理。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 只读当前目标，弱引用失效时返回空；供本地诊断与验收。 */
    ADemoCharacter* GetObservedCharacter() const;
private:
    /** 更新相机位置/视线，使用小球检测防止后置镜头穿墙；目标丢失时保持最后画面直到权威重定向。 */
    void UpdateView();
    // 本机控制器弱引用，不跨World保活；ViewTarget被替换或PC卸载后本Actor销毁。
    TWeakObjectPtr<ADemoPlayerController> ObservingController;
    // 服务器选择的参战队友弱引用，死亡/断线后的下一次RPC可原位切换。
    TWeakObjectPtr<ADemoCharacter> ObservedCharacter;
    // 目标销毁只记录一次日志，不在等待权威重定向时逐帧刷屏。
    bool bLoggedMissingTarget = false;
};
