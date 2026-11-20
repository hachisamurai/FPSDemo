// 职责：显式Editor独立-game的极小生产入口Smoke；正常枪Focus和E设备焦点，不依赖Prototype/AA覆盖。
// 使用：-DemoOutlineProductionSmoke；最多2正常PNG/小JSON/90秒，用户负责完整游戏效果验收。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoOutlineProductionSmoke.generated.h"
class ADemoCharacter;
class ADemoPlayerController;
class ADemoGunDisplay;
class ADemoCasinoDevice;
class UDemoLocalOutlineComponent;

/** 只走现生产注册/交互/渲染，不造mask、不读RT、不购买或触发武器攻击。 */
UCLASS()
class FPSDEMO_API UDemoOutlineProductionSmoke : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 仅显式Editor/真实持久Game地图/Standalone；禁止与旧Prototype或审查夹具共同启动。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** 仅独立Game类型，不介入用户正常PIE/Editor视口。 */
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    /** 建独占证据目录与本World末帧弱委托，不修改生产配置。 */
    virtual void OnWorldBeginPlay(UWorld& World) override;
    /** 只清自身委托及仍属于本Pawn/Device的本地租约，不取消他人截图。 */
    virtual void Deinitialize() override;
private:
#if WITH_EDITOR
    /** 有界状态机：正常StartRun→枪Focus→E设备→digit0，各等待fresh native提交约1秒。 */
    void OnPostTick(UWorld* World,ELevelTick Type,float Delta);
    /** 以当前真实SharedEye正常转向Point，不锁相机/改Query或强置Focus。 */
    void Aim(const FVector& Point);
    /** 只读当前生产Renderer最近原生Capture+main提交，不等于GPU/像素证明。 */
    bool WaitNative(const UObject* Requester);
    /** 按固定Name请求正常PNG；仅两个，不绑定内存截图委托。 */
    void Capture(const FString& Name);
    /** 写小JSON并退出自有进程，Scope明确非完整视觉/性能/联机验收。 */
    void Finish(bool Success,const FString& Reason);
    TWeakObjectPtr<ADemoCharacter> Player; TWeakObjectPtr<ADemoPlayerController> PC; // 原本机Pawn/Controller。
    TWeakObjectPtr<ADemoGunDisplay> Gun; TWeakObjectPtr<ADemoCasinoDevice> Device; // 原注册目标，无新造Actor。
    FDelegateHandle TickHandle; // 本World弱委托，Finish/卸载精确移除。
    FString Directory,PendingPNG; TArray<FString> PNGs; // 独占目录及至多2正常图。
    double Started=0.,StepStarted=0.,StableSince=-1.,RaySinceWorld=-1.,ShotStarted=0.; // 单调真实秒；射线dwell沿生产World秒。
    uint64 LastNativeFrame=0,ShotFrame=0; FIntPoint NativeSize=FIntPoint::ZeroValue; // 只读最近提交帧及实际阶段尺寸。
    int32 Step=0,NativeFreshSamples=0,AimCorrections=0; bool bFinished=false; // 有限阶段及少量诊断，不跑审查矩阵。
#endif
};
